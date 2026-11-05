/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Sparse, bounded step automation. Main-loop writes publish under the same
 * interrupt guard as parameter batches. The ISR only scans MOTION_MAX fixed records (128 since 1.2, FUN10; 64 before).
 * p[] is the sounding value; motion_base_value() is the patch's saved value.
 * Two kinds of record share the MOTION_MAX (core.h MOTION_LOCK): an automation event sets its value at its step and the value
 * holds (to the next event, the loop's restart, a stop); a parameter lock (1.1, Discussions #75 / #53: hold a step,
 * turn a knob) sets its value at its step only when the step plays (its chance passed) and the next step puts back
 * what would sound without it: the latest automation event of the pass up to that step, else the sound's own value
 * (motion_unlock). A lock lasts the whole step, its RATCH parts too; swing only moves the step's start. */
static motion_store_t motion;
static int16_t motion_base[NTRK][P_COUNT];
static uint32_t motion_active[NTRK][(P_COUNT + 31u) / 32u];
static uint32_t motion_locked[NTRK][(P_COUNT + 31u) / 32u];   /* the ids a lock holds now (put back at the next step) */
static uint8_t motion_base_valid, motion_full;
static uint8_t motion_hold;   /* (Dah) the tracks whose motion waits (a sound loading: ui.c load_motion_hold), a bit each */
#define MOTION_ALL 0xFFFFu                         /* motion_clear_locks: every step's */
#define MRAMP_N 4u
static struct {
    uint8_t n, id[MRAMP_N], len[MRAMP_N], at[MRAMP_N];
    int16_t from[MRAMP_N], to[MRAMP_N];
} mramp[NTRK];
static uint32_t motion_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void motion_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}
static int motion_param(uint32_t id)
{
    /* Sound only: transport, routing, voice allocation and discrete engine
     * changes never become automation. FX sends and continuous mix are safe. */
    return id < P_COUNT && (id <= P_REL || (id >= P_ED_FLT && id <= P_LD_AMP) ||
        (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN ||
        id == P_DETUNE || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || (id >= P_LN0 && id <= P_LN7) ||
        id == P_SPRD || (id >= P_ITYPE && id <= P_IMIX) || (id >= P_EQLO && id <= P_EQ3F) || id == P_DKAMT || id == P_DOUT || id == P_DCOL || (id >= P_ARATE && id <= P_AGATE) || id >= P_E0);   /* (Dah: the
                                        * EQ, DUCK's depth, the ARP's RATE OCT GATE too) */   /* (not the chord keys, nor 1.2's LFO SYNC TRIG
                                        * POL and QUANTIZE, nor 1.5's filter TYPE and ENV SYNC: settings; the DRUM lane
                                        * levels, SPREAD (#148: continuous, as PAN), 1.5's INSERT (TYPE too: a lock
                                        * switches it for a step, fx.c track_insert fades over), E0..E7) */
}
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u))) return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (e->param == MOTION_FINE ? e->value < 1 || e->value > 9 : !motion_param(MOTION_ID(e)) || e->value < -64 || e->value > 127)
            return 0;                                  /* (FINE LEN: its tenths, core.h) */
        if (e->place >= (NPAGE << 9)) return 0;
        for (j = 0; j < i; j++)
            if (m->event[j].place == e->place && MOTION_ID(&m->event[j]) == MOTION_ID(e)) return 0;
    }
    return 1;
}
static uint32_t motion_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++) n += MP_TRK(&motion.event[i]) == k;
    return n;
}
/* track t's parameter locks (of motion_count) */
static uint32_t motion_lock_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++) n += MP_TRK(&motion.event[i]) == k && (motion.event[i].param & MOTION_LOCK);
    return n;
}
/* the steps of track k from step `base` on that hold a lock, bit s = step base + s (the roll and the grid mark them) */
static uint64_t motion_lock_steps(uint32_t k, uint32_t base)
{
    uint32_t i, s;
    uint64_t m = 0;
    for (i = 0; i < motion.count; i++)
        if (MP_TRK(&motion.event[i]) == k && (motion.event[i].param & MOTION_LOCK) && (s = MP_STEP(&motion.event[i])) >= base &&
            s < base + 64u)
            m |= (uint64_t)1 << (s - base);
    return m;
}
/* the lock of track t on step / id: 1 and its value in *v, 0 = none */
static int motion_lock_get(const track_t *t, uint32_t step, uint32_t id, int16_t *v)
{
    uint32_t i, place = MPLACE(trk_index(t), step);
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == place && motion.event[i].param == (id | MOTION_LOCK)) {
            *v = motion.event[i].value;
            return 1;
        }
    return 0;
}
/* the parameters track k's MOTION will change (at least one event): a bit per P_* id in m[].
 * Main loop only (the ISR never writes the events); at most MOTION_MAX records, once per card redraw. */
static uint32_t motion_mask(uint32_t k, uint32_t m[(P_COUNT + 31u) / 32u])
{
    uint32_t i, any = 0;
    memset(m, 0, ((P_COUNT + 31u) / 32u) * sizeof m[0]);
    if (k >= NTRK) return 0;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if (MP_TRK(e) != k || MOTION_ID(e) >= P_COUNT) continue;
        m[MOTION_ID(e) / 32u] |= 1u << (MOTION_ID(e) % 32u);
        any = 1;
    }
    return any;
}
static int16_t motion_base_value(const track_t *t, uint32_t id)
{
    uint32_t k = trk_index(t);
    return (motion_active[k][id / 32u] >> (id % 32u)) & 1u ? motion_base[k][id] : t->p[id];
}
static void motion_restore(track_t *t)
{
    uint32_t k = trk_index(t), id, f = motion_guard();
    mramp[k].n = 0;                                    /* (no line goes on) */
    for (id = 0; id < P_COUNT; id++)
        if ((motion_active[k][id / 32u] >> (id % 32u)) & 1u) t->p[id] = motion_base[k][id];
    memset(motion_active[k], 0, sizeof motion_active[k]);
    memset(motion_locked[k], 0, sizeof motion_locked[k]);
    motion_unguard(f);
}
/* (Dah) a SONG's section starts: as motion_restore, but a parameter the section's own automation (m) moves too keeps the
 * value it has now (still automated: STOP puts it back), so its curve goes on from there with no jump to the sound's */
static void motion_restore_keep(track_t *t, const motion_store_t *m)
{
    uint32_t k = trk_index(t), id, i, f = motion_guard(), keep[(P_COUNT + 31u) / 32u];
    memset(keep, 0, sizeof keep);
    for (i = 0; i < m->count && i < MOTION_MAX; i++)
        if (MP_TRK(&m->event[i]) == k && !(m->event[i].param & MOTION_LOCK) && MOTION_ID(&m->event[i]) < P_COUNT)
            keep[MOTION_ID(&m->event[i]) / 32u] |= 1u << (MOTION_ID(&m->event[i]) % 32u);
    mramp[k].n = 0;
    for (id = 0; id < P_COUNT; id++)
        if (((motion_active[k][id / 32u] & ~keep[id / 32u]) >> (id % 32u)) & 1u) t->p[id] = motion_base[k][id];
    for (i = 0; i < (P_COUNT + 31u) / 32u; i++)
        motion_active[k][i] &= keep[i];
    memset(motion_locked[k], 0, sizeof motion_locked[k]);
    motion_unguard(f);
}
static void motion_rebase(track_t *t)
{
    uint32_t k = trk_index(t), f = motion_guard();
    motion_restore(t);
    memcpy(motion_base[k], t->p, sizeof t->p);
    motion_base_valid = (uint8_t)((motion_base_valid & ~(1u << k)) | (song.playing ? 1u << k : 0u));
    motion_unguard(f);
}
static void motion_begin(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        motion_restore(&trk[k]);
        memcpy(motion_base[k], trk[k].p, sizeof trk[k].p);
    }
    motion_base_valid = (1u << NTRK) - 1u;
}
static void motion_end(void)
{
    for (uint32_t k = 0; k < NTRK; k++) motion_restore(&trk[k]);
    motion_base_valid = 0;
}
/* (Dah) automation always plays (its PLAY ON / OFF gone; the stores' on bits kept as they were, unread): track t's
 * waits while its sound loads (hold 1: what it set goes back) */
static void motion_wait(track_t *t, uint32_t hold)
{
    uint32_t f = motion_guard(), b = 1u << trk_index(t);
    motion_hold = (uint8_t)(hold ? motion_hold | b : motion_hold & ~b);
    if (hold) motion_restore(t);
    motion_unguard(f);
}
static void motion_clear(track_t *t)
{
    uint32_t f = motion_guard(), k = trk_index(t), i, n = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if (MP_TRK(&motion.event[i]) != k) motion.event[n++] = motion.event[i];
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion.on &= (uint8_t)~(1u << k);
    motion_rebase(t);
    motion_full = 0;
    motion_unguard(f);
}
static void motion_reset(track_t *t) { motion_clear(t); }
/* a record of kind 0 (automation) or MOTION_LOCK on track t's step / id: a new one, or the one there (of either kind)
 * becomes this. 0 ok, 1 invalid, 2 the MOTION_MAX are used (motion_full: "AUTOMATION FULL") */
static int motion_put(track_t *t, uint32_t step, uint32_t id, int16_t value, uint32_t kind)
{
    uint32_t k = trk_index(t), i, f;
    const param_desc_t *d;
    if (k >= NTRK || step >= LEN_MAX || !motion_param(id)) return 1;
    d = param_desc_of(eng_idx(t->eng_req), id);
    if (value < d->min || value > d->max || value < -64 || value > 127) return 1;
    value = (int16_t)param_fit(d, value);                    /* (a retired KIT: the kit it plays) */
    f = motion_guard();
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == MPLACE(k, step) && MOTION_ID(&motion.event[i]) == id) break;
    if (i == MOTION_MAX) { motion_full = 1; motion_unguard(f); return 2; }
    motion.event[i].place = MPLACE(k, step);
    motion.event[i].param = (uint8_t)(id | kind);
    motion.event[i].value = (int8_t)value;
    RING_PUBLISH();
    if (i == motion.count) motion.count++;
    motion.on |= (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}
static int motion_set_event(track_t *t, uint32_t step, uint32_t id, int16_t value) { return motion_put(t, step, id, value, 0); }
static int motion_set_lock(track_t *t, uint32_t step, uint32_t id, int16_t value)
{
    return motion_put(t, step, id, value, MOTION_LOCK);
}
static void motion_delete_event(track_t *t, uint32_t step, uint32_t id)   /* (either kind) */
{
    uint32_t f = motion_guard(), place = MPLACE(trk_index(t), step), i, n = 0;
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place != place || MOTION_ID(&motion.event[i]) != id) motion.event[n++] = motion.event[i];
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
}
/* FINE LEN (core.h MOTION_FINE): the tenths of track k's step `step` in store m, 0 none (whole) */
static uint32_t fine_at(const motion_store_t *m, uint32_t k, uint32_t step)
{
    uint32_t i, place = MPLACE(k, step);
    for (i = 0; i < m->count && i < MOTION_MAX; i++)
        if (m->event[i].place == place && m->event[i].param == MOTION_FINE)
            return (uint32_t)m->event[i].value;
    return 0;
}
/* .. set on track t's step (f 1..9), or taken out (0). 0 ok, 1 invalid, 2 the MOTION_MAX are used (motion_full).
 * fine_raw: the audio ISR's (live recording: nothing preempts it), fine_put the main loop's (under the guard) */
static int fine_raw(track_t *t, uint32_t step, uint32_t f)
{
    uint32_t k = trk_index(t), i, place;
    if (k >= NTRK || step >= LEN_MAX || f > 9u) return 1;
    place = MPLACE(k, step);
    for (i = 0; i < motion.count && (motion.event[i].place != place || motion.event[i].param != MOTION_FINE); i++)
        ;
    if (!f) {
        if (i < motion.count) {
            for (; i + 1u < motion.count; i++)
                motion.event[i] = motion.event[i + 1u];
            motion.count--;
            memset(&motion.event[motion.count], 0, sizeof motion.event[0]);
        }
    } else if (i == MOTION_MAX) {
        motion_full = 1;
        return 2;
    } else {
        motion.event[i].place = (uint16_t)place;
        motion.event[i].param = MOTION_FINE;
        motion.event[i].value = (int8_t)f;
        RING_PUBLISH();
        if (i == motion.count) motion.count++;
    }
    return 0;
}
static int fine_put(track_t *t, uint32_t step, uint32_t f)
{
    uint32_t g = motion_guard();
    int r = fine_raw(t, step, f);
    motion_unguard(g);
    return r;
}
/* the steps of track k from step `base` on with a FINE LEN, bit s = step base + s (as motion_lock_steps) */
static uint64_t fine_steps(uint32_t k, uint32_t base)
{
    uint32_t i, s;
    uint64_t m = 0;
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].param == MOTION_FINE && MP_TRK(&motion.event[i]) == k && (s = MP_STEP(&motion.event[i])) >= base &&
            s < base + 64u)
            m |= (uint64_t)1 << (s - base);
    return m;
}
/* the locks of track t's step (MOTION_ALL: of every step); their automation events stay. A lock sounding now goes
 * back at the next step as always (motion_locked). The locks it removed */
static uint32_t motion_clear_locks(track_t *t, uint32_t step)
{
    uint32_t f = motion_guard(), k = trk_index(t), i, n = 0, gone;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if (MP_TRK(e) == k && (e->param & MOTION_LOCK) && (step >= MOTION_ALL || MP_STEP(e) == step)) continue;
        motion.event[n++] = *e;
    }
    gone = motion.count - n;
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
    return gone;
}
/* (Dah) automation and locks only on HOME's four knobs of the track (ASSIGN's, else the engine's): another parameter
 * turned while recording records nothing, and a record of one (an older project's) plays nothing */
static uint32_t home_knob_id(const track_t *t, uint32_t j)    /* HOME's knob j (0..3) of t: its P_* id */
{
    uint32_t a = trkx[trk_index(t) % NTRK].home[j];
    return a && a <= P_COUNT ? a - 1u : ENGINES[t->eng_req % NENGINES]->knob[j];
}
static int motion_home_id(const track_t *t, uint32_t id)
{
    uint32_t j;
    for (j = 0; j < 4u; j++)
        if (home_knob_id(t, j) == id)
            return 1;
    return 0;
}
static int motion_capture(track_t *t, uint32_t id, int16_t value)
{
    uint32_t k = trk_index(t), idx, len, period, f;
    if (!motion_param(id)) return 0;
    if (!rec_on(t) || t != TSEL || !motion_home_id(t, id)) {
        /* A live edit becomes a new base, even when an earlier motion value is
         * currently sounding. Subsequent recorded events can still override it. */
        f = motion_guard();
        if ((motion_base_valid >> k) & 1u) motion_base[k][id] = value;
        motion_unguard(f);
        return 0;
    }
    len = (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX);
    period = div_samples((uint32_t)t->p[P_SDIV]);
    f = motion_guard();
    idx = t->seq_idx % len;
    if (t->seq_pos < 0x7FFFFFFFu && t->seq_pos > step_samples(t, period, idx) / 2u) idx = (idx + 1u) % len;
    /* Mark the immediate knob value transient too, so a stop before its next
     * quantized step still restores the original patch. */
    if ((motion_base_valid >> k) & 1u) motion_active[k][id / 32u] |= 1u << (id % 32u);
    motion_unguard(f);
    return motion_set_event(t, idx, id, value);
}
/* step `step` starts: the locks of the step before go back to what sounds without them: the latest automation event
 * of track t's id at a step up to this one in this pass (motion_restore at step 0 starts each pass), else the sound's
 * own value */
static __attribute__((noinline)) void motion_unlock(track_t *t, uint32_t step, const motion_store_t *m)
{
    uint32_t k = trk_index(t), id, i;
    for (id = 0; id < P_COUNT; id++) {
        int32_t best = -1;
        int16_t v;
        if (!((motion_locked[k][id / 32u] >> (id % 32u)) & 1u))
            continue;
        v = motion_base[k][id];
        for (i = 0; i < m->count; i++) {
            const motion_event_t *e = &m->event[i];
            int32_t s = (int32_t)MP_STEP(e);
            if (MP_TRK(e) != k || e->param != id || s > (int32_t)step || s <= best)
                continue;                               /* (e->param == id: automation only, no MOTION_LOCK) */
            best = s;
            v = (int16_t)param_fit(param_desc_of(eng_idx(t->eng_req), id), e->value);
        }
        t->p[id] = v;
    }
    memset(motion_locked[k], 0, sizeof motion_locked[k]);
}
/* CURVE RAMP (trkx[].curve): an automation event's value moves in a line to the next event of its parameter
 * (the next one in the pass; none: to what the pass starts with, the event on step 1 or the sound's own value, at the
 * loop's end), a block at a time (motion_ramp_block); a lock holds its step all the same. MRAMP_N lines a track at once
 * (more: the newest takes the oldest's place); enum parameters step as always */
static void motion_ramp_add(uint32_t k, uint32_t id, int16_t from, int16_t to, uint32_t len)
{
    uint32_t j;
    for (j = 0; j < mramp[k].n && mramp[k].id[j] != id; j++)
        ;
    if (j == MRAMP_N) {                                 /* (full: the oldest goes; no memmove on the device) */
        for (j = 0; j + 1u < MRAMP_N; j++) {
            mramp[k].id[j] = mramp[k].id[j + 1u]; mramp[k].len[j] = mramp[k].len[j + 1u]; mramp[k].at[j] = mramp[k].at[j + 1u];
            mramp[k].from[j] = mramp[k].from[j + 1u]; mramp[k].to[j] = mramp[k].to[j + 1u];
        }
    } else if (j == mramp[k].n) {
        mramp[k].n++;
    }
    mramp[k].id[j] = (uint8_t)id; mramp[k].from[j] = from; mramp[k].to[j] = to;
    mramp[k].len[j] = (uint8_t)len; mramp[k].at[j] = 0;
}
/* the event at step `step` of track k sets id to v: its line to the next event of id (RAMP only) */
static __attribute__((noinline)) void motion_ramp_from(const track_t *t, const motion_store_t *m, uint32_t step, uint32_t id,
                                                       int16_t v)
{
    uint32_t k = trk_index(t), i, len = (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX), best = len, s;
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    int16_t to = motion_base[k][id], first = to;
    int first_set = 0;
    if (d->fmt == F_ENUM)
        return;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        if (MP_TRK(e) != k || e->param != id)           /* (automation only: e->param == id, no MOTION_LOCK) */
            continue;
        s = MP_STEP(e);
        if (s > step && s < best) {
            best = s;
            to = (int16_t)param_fit(d, e->value);
        }
        if (s == 0u) {
            first = (int16_t)param_fit(d, e->value);
            first_set = 1;
        }
    }
    if (best == len)                                    /* (none after it: to the pass's start) */
        to = first_set ? first : motion_base[k][id];
    if (best > step)
        motion_ramp_add(k, id, v, to, best - step);
}
/* a block of track t: its lines where they are in their steps (pos samples into a step of cur samples) */
static __attribute__((noinline)) void motion_ramp_block(track_t *t, uint32_t pos, uint32_t cur)
{
    uint32_t k = trk_index(t), j, fr = cur ? (pos < cur ? pos : cur) * 256u / cur : 0u;   /* (a step: < 2^24 samples) */
    for (j = 0; j < mramp[k].n; j++) {
        uint32_t id = mramp[k].id[j], span = mramp[k].len[j] * 256u, x = mramp[k].at[j] * 256u + fr;
        if ((motion_locked[k][id / 32u] >> (id % 32u)) & 1u)
            continue;                                   /* (a lock holds its step) */
        t->p[id] = (int16_t)(mramp[k].from[j] + (int32_t)(mramp[k].to[j] - mramp[k].from[j]) * (int32_t)(x < span ? x : span) /
                                                   (int32_t)span);
    }
}
/* a step starts: the lines one step on, the ones arrived gone (step 1 of a pass: every one) */
static void motion_ramp_step(uint32_t k, uint32_t step)
{
    uint32_t j, n = 0;
    for (j = 0; j < mramp[k].n; j++)
        if (step && ++mramp[k].at[j] < mramp[k].len[j]) {
            mramp[k].id[n] = mramp[k].id[j]; mramp[k].len[n] = mramp[k].len[j]; mramp[k].at[n] = mramp[k].at[j];
            mramp[k].from[n] = mramp[k].from[j]; mramp[k].to[n] = mramp[k].to[j];
            n++;
        }
    mramp[k].n = (uint8_t)n;
}

/* step `step` of track t starts (seq.c seq_tick); play: it plays (its chance passed): its locks apply */
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t step, const motion_store_t *m, uint32_t play)
{
    uint32_t k = trk_index(t), i, w, any = 0, place = MPLACE(k, step);
    if (mramp[k].n)
        motion_ramp_step(k, step);
    if ((motion_hold >> k) & 1u) return;
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    for (w = 0; w < (P_COUNT + 31u) / 32u; w++)
        any |= motion_locked[k][w];
    if (!step) motion_restore(t);
    else if (any) motion_unlock(t, step, m);
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->event[i];
        uint32_t id = MOTION_ID(e);
        if (e->place != place || ((e->param & MOTION_LOCK) && !play) || id >= P_COUNT || !motion_home_id(t, id))
            continue;                                   /* (FINE LEN: none; Dah: HOME's four only) */
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
        t->p[id] = (int16_t)param_fit(d, e->value);
        motion_active[k][id / 32u] |= 1u << (id % 32u);
        if (e->param & MOTION_LOCK)
            motion_locked[k][id / 32u] |= 1u << (id % 32u);
        else if (trkx[k].curve)
            motion_ramp_from(t, m, step, id, t->p[id]);   /* (RAMP) */
    }
}
static int motion_replace_track(track_t *t, const motion_store_t *in)
{
    uint32_t k = trk_index(t), n = motion.count - motion_count(t), f;
    if (!motion_valid(in) || n + in->count > MOTION_MAX) return 2;
    for (uint32_t i = 0; i < in->count; i++) if (MP_TRK(&in->event[i]) != k) return 1;
    f = motion_guard();
    motion_clear(t);
    memcpy(motion.event + motion.count, in->event, in->count * sizeof in->event[0]);
    motion.count += in->count;
    motion.on |= in->on & (uint8_t)(1u << k);
    motion_unguard(f);
    return 0;
}

/* ---- 1.1.5: SEQ > AUTOMATION (ui_events.c ev_*): the records of a track as a list, edited one by one ---- */
/* the record of track t on step / id (either kind): its index, -1 none */
static int32_t motion_find(const track_t *t, uint32_t step, uint32_t id)
{
    uint32_t i, place = MPLACE(trk_index(t), step);
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].place == place && MOTION_ID(&motion.event[i]) == id)
            return (int32_t)i;
    return -1;
}
/* an id whose meaning is the engine's own: EDIT's E1..E8, DIGITAL's operators, DRUM's lane levels */
static int motion_engine_id(uint32_t id)
{
    return (id >= P_E0 && id <= P_E7) || (id >= P_FM1_ATK && id <= P_FM4_LEVEL) || (id >= P_LN0 && id <= P_LN7);
}
/* a sound load put engine `to` where `from` was: when they differ, track t's records on the engine's own ids go (their
 * meaning changed); the ones on the common parameters (LEVEL, ENV, LFO, FILTER ENV, the sends, GLIDE, PAN, DETUNE)
 * stay, both kinds. The records it removed */
static uint32_t motion_sound_loaded(track_t *t, uint32_t from, uint32_t to)
{
    uint32_t f, k = trk_index(t), i, n = 0, gone;
    if (eng_idx(from) == eng_idx(to))
        return 0;
    f = motion_guard();
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if (MP_TRK(e) == k && motion_engine_id(MOTION_ID(e)))
            continue;
        motion.event[n++] = *e;
    }
    gone = motion.count - n;
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
    return gone;
}
