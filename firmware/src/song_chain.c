/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A song uses the sequences of four saved projects, with the current sounds.
 * Sources are copied in the main loop before PLAY. The ISR changes only their
 * index and five timing parameters; the editable steps stay untouched.
 *
 * Lanes (1.2): a section (a row) names the slot each track plays (core.h chain_row_t). Each track reads its steps (with
 * CHANCE, RATCH, NUDGE), its automation and locks, LEN / DIV / SWING / GATE and QUANTIZE from its own slot's copy for
 * that track only; all four slots in use are copied before PLAY (chain_prepare: the four sources were there already
 * for whole rows, so lanes cost no more RAM). Sections keep one boundary for all four tracks: a section lasts `repeat`
 * loops of its clock track (chain_clock): the first track that is not silent there (track 1 when it plays: every song
 * before lanes counts as it did), or, every track silent, a 4/4 bar (CHAIN_SIL_*). At each section all four tracks
 * start together from their step 1, as rows always did.
 * A silent track ("-", CHAIN_SILENT) plays nothing in that section: at its start the track's sequenced notes are
 * released (seq_release, as at every section) and its automation goes back to the sound's own values (motion_restore);
 * no step, automation or lock of any slot plays there. Live keys and the arp still play it (the sound stays). */
typedef struct {
    step_t step[NTRK][NSTEP];
    int16_t timing[NTRK][4];
    uint8_t qnt[NTRK];                       /* each track's QUANTIZE (P_SQNT) as saved */
    motion_store_t motion;
} chain_pattern_t;
static chain_config_t chain_config;
static uint8_t song_round = 1;                       /* (Dah) the song goes round (0: it stops at its end, as before) */
static struct {
    chain_pattern_t source[2];               /* the section playing's patterns, the next one's: row & 1 */
    uint8_t src_row[2], src_ok[2];
    chain_config_t config;
    int16_t timing[NTRK][4];                 /* the editable LEN DIV SWING GATE and QUANTIZE, back on STOP */
    uint8_t qnt[NTRK];
    volatile uint8_t armed, running, row, remaining;
    uint8_t lane[NTRK];                      /* the pattern each track plays in this section, CHAIN_SILENT none */
    uint8_t cur;                             /* (the buffer of the section playing) */
    uint8_t clk;                             /* the track whose loop counts the section (chain_clock) */
    uint8_t rec;
    uint32_t carry;
} chain __attribute__((section(".pool")));   /* (eight tracks' sources, out of RAM's .bss) */
/* a silent lane: its steps (empty: nothing plays) and its timing (a 4/4 bar of 1/16: the section's length when every
 * track is silent) */
static const step_t chain_silent[NSTEP] = {{{0}}};
#define CHAIN_SIL_LEN 16
#define CHAIN_SIL_DIV 2                      /* 1/16 */

static void seq_release(track_t *t);
static void seq_stop(void);
static uint32_t div_samples(uint32_t div);
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);

static chain_row_t chain_row_of(uint32_t slot, uint32_t repeat)   /* a section with all four tracks on one slot */
{
    chain_row_t r;
    memset(r.slot, (int)slot, sizeof r.slot);
    r.repeat = (uint8_t)repeat;
    return r;
}
static void chain_defaults(chain_config_t *c)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < CHAIN_ROWS; i++)
        c->row[i].repeat = 1;
}
static int chain_valid(const chain_config_t *c)
{
    uint32_t i, k;
    if (c->count > CHAIN_ROWS)
        return 0;
    for (i = 0; i < c->count; i++) {
        for (k = 0; k < NTRK; k++)
            if (c->row[i].slot[k] > 32u)       /* (a pattern number, pstore.c PS_NUM; 0 silent) */
                return 0;
        if (!CHAIN_REP(c->row[i]) || CHAIN_REP(c->row[i]) > 16u)
            return 0;
    }
    return 1;
}
/* the chain of FUN6..FUN9 (rows {slot, repeat}) -> today's: each row a section with the four tracks on its slot. 0: not
 * a valid one (as chain_valid then) */
static int chain_from_v9(chain_config_t *c, const chain_v9_t *o)
{
    uint32_t i;
    chain_defaults(c);
    if (o->count > CHAIN_ROWS)
        return 0;
    for (i = 0; i < o->count; i++) {
        if (o->row[i].slot >= 4u || !o->row[i].repeat || o->row[i].repeat > 16u)
            return 0;
        c->row[i] = chain_row_of(o->row[i].slot + 1u, o->row[i].repeat);   /* (A..D: patterns 1..4) */
    }
    c->count = o->count;
    return 1;
}
/* the sections of FUN10 (four tracks a row) <-> today's (eight): tracks base..base + 3 of each row, the
 * others silent (from), the repeats and CUT as they are. 0: not a valid one */
static int chain_from_v10(chain_config_t *c, const chain_v10_t *o)
{
    uint32_t i, k;
    chain_defaults(c);
    if (o->count > CHAIN_ROWS)
        return 0;
    for (i = 0; i < CHAIN_ROWS; i++) {
        for (k = 0; k < NTRK; k++)                     /* (A..D: patterns 1..4; "-" (4): silent) */
            c->row[i].slot[k] = k < NTRK_V10 && o->row[i].slot[k] < 4u ? (uint8_t)(o->row[i].slot[k] + 1u) : (uint8_t)CHAIN_SILENT;
        c->row[i].repeat = o->row[i].repeat;
    }
    c->count = o->count;
    return chain_valid(c);
}
static void chain_to_v10(chain_v10_t *o, const chain_config_t *c, uint32_t base)
{
    uint32_t i, k;
    memset(o, 0, sizeof *o);
    o->count = c->count;
    for (i = 0; i < CHAIN_ROWS; i++) {
        for (k = 0; k < NTRK_V10; k++)                 /* (patterns 1..4: A..D; others "-") */
            o->row[i].slot[k] = c->row[i].slot[base + k] && c->row[i].slot[base + k] <= 4u ? (uint8_t)(c->row[i].slot[base + k] - 1u) : 4u;
        o->row[i].repeat = c->row[i].repeat;
    }
}
/* the track whose loops count section r's length: the first one not silent; every track silent: track 1 (a bar) */
static uint32_t chain_clock(const chain_row_t *r)
{
    uint32_t k;
    for (k = 0; k < trk_count(); k++)
        if (r->slot[k] != CHAIN_SILENT)
            return k;
    return 0;
}
/* a track parameter a playing song sets from its slots (the editor and the pages leave it alone meanwhile) */
static int chain_owns(uint32_t id) { return (id >= P_SLEN && id <= P_SGATE) || id == P_SQNT; }
static uint8_t seq_off;                              /* (Dah) the tracks on no pattern (HOME's VIEW, KNOB 3 "-"): their
                                                      * steps kept, none played (not saved: a live state) */
static const step_t *seq_steps(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk), s;
    if (!chain.running)
        return t->step;
    s = chain.lane[k];
    return s != CHAIN_SILENT ? chain.source[chain.cur].step[k] : chain_silent;
}
static const step_t *seq_play_steps(const track_t *t)   /* (Dah) the steps the sequencer plays: none on no pattern */
{
    return !chain.running && (seq_off >> ((uint32_t)(t - trk) % NTRK)) & 1u ? chain_silent : seq_steps(t);
}
/* (long patterns, LEN over NSTEP) the steps from NSTEP on are not on the track: the audio ISR plays them from a ring of
 * PGR_N per track, slot i % PGR_N the step i (abs), which the main loop fills ahead of the step playing from the pattern
 * store (pstore.c pg_service). Not there (the main loop late): a step that plays nothing (pg_late counts them). Live
 * recording into one marks it (dirty): the main loop puts it back into its page */
#define PGR_N 16u
static struct {
    step_t st[PGR_N];
    uint8_t abs[PGR_N];                      /* the step slot j holds (NSTEP..LEN_MAX - 1), 0 none */
    uint16_t dirty;                          /* the slots live recording wrote */
    uint8_t num;                             /* the pattern they came from */
} pgr[NTRK];
static volatile uint32_t pg_late;
static uint16_t pg_rev[NTRK];                /* +1 at each change of a track's pages (the screens' signatures: ui.c steps_sig) */
static step_t pg_trash;                      /* (recording into a step not there: lost) */
static __attribute__((noinline)) const step_t *seq_far(const track_t *t, uint32_t i)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, j = i % PGR_N;
    if ((!chain.running && ((seq_off >> k) & 1u)) || i >= LEN_MAX)
        return chain_silent;
    if (pgr[k].abs[j] != i) {
        pg_late++;
        return chain_silent;
    }
    return &pgr[k].st[j];
}
/* step i (0..LEN - 1) as the sequencer plays it */
static inline const step_t *seq_at(const track_t *t, uint32_t i) { return i < NSTEP ? &seq_play_steps(t)[i] : seq_far(t, i); }
/* step i of track t for live recording to write (the audio ISR) */
static step_t *seq_wr(track_t *t, uint32_t i)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, j = i % PGR_N;
    if (i < NSTEP)
        return &t->step[i];
    if (i >= LEN_MAX || pgr[k].abs[j] != i)
        return &pg_trash;
    pgr[k].dirty |= (uint16_t)(1u << j);
    return &pgr[k].st[j];
}
static void pgr_drop_all(void)               /* (the audio ISR: a SONG's section starts, its patterns' steps) */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        memset(pgr[k].abs, 0, sizeof pgr[k].abs);
        pgr[k].dirty = 0;
    }
}
static void motion_restore(track_t *t);
static void motion_restore_keep(track_t *t, const motion_store_t *m);
/* a section starts (chain_start, chain_tick: once a section, not per block; not inlined: off the audio ISR's loop) */
static __attribute__((noinline)) void chain_apply(void)
{
    uint32_t i;
    const chain_row_t *r = &chain.config.row[chain.row];
    uint32_t b = chain.cur ^ 1u, ok = chain.src_ok[b] && chain.src_row[b] == chain.row;   /* (Dah: the other buffer) */
    chain.cur = (uint8_t)b;
    chain.clk = (uint8_t)chain_clock(r);
    pgr_drop_all();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        uint32_t s = ok && i < trk_count() ? r->slot[i] : CHAIN_SILENT;   /* (the buffer not ready: silent) */
        chain.lane[i] = (uint8_t)s;
        if (!(s != CHAIN_SILENT && (t->seq_hold || (t->seq_keep & 15u)) && t->seq_n))
            seq_release(t);                             /* (Dah: a SLIDE over the section's start held: it slides;
                                                         * overlaps: a note going on into its first step goes on) */
        if (s != CHAIN_SILENT && ok)
            motion_restore_keep(t, &chain.source[b].motion);   /* (Dah: its curves go on from where they are) */
        else
            motion_restore(t);
        if (s != CHAIN_SILENT) {
            memcpy(&t->p[P_SLEN], chain.source[b].timing[i], sizeof chain.timing[i]);
            t->p[P_SQNT] = chain.source[b].qnt[i];
        } else {
            t->p[P_SLEN] = CHAIN_SIL_LEN;
            t->p[P_SDIV] = CHAIN_SIL_DIV;
            t->p[P_SSWING] = 0;
            t->p[P_SQNT] = 1;
        }
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu;
        t->rh_n = t->rskip_n = 0;
    }
}
static void chain_cut(uint32_t r);
/* (Dah) where the song starts (SONG's VIEW GRID): the section, 1/48 quarters into it; chain_prepare and chain_start */
static uint8_t chain_from_row;
static uint32_t chain_from_u;
static void chain_start(void)
{
    uint32_t i;
    if (!chain.armed)
        return;
    chain.rec = song.rec;
    song.rec = 0;
    for (i = 0; i < NTRK; i++) {
        memcpy(chain.timing[i], &trk[i].p[P_SLEN], sizeof chain.timing[i]);
        chain.qnt[i] = (uint8_t)trk[i].p[P_SQNT];
    }
    chain.row = chain_from_row < chain.config.count ? chain_from_row : 0u;   /* (Dah: from the GRID) */
    chain.cur = 1;                                  /* (that row in buffer 0: chain_prepare's) */
    chain.remaining = CHAIN_REP(chain.config.row[chain.row]);
    chain_cut(chain.row);
    chain.carry = 0;
    chain.running = 1;
    chain.armed = 0;
    chain_apply();
}
/* (Dah) seq_start, after it set every track at its step 1: into the GRID's section, each track at its step there */
static void chain_seek(void)
{
    uint32_t i;
    if (chain.running && chain_from_u)
        for (i = 0; i < NTRK; i++) {
            track_t *t = &trk[i];
            uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, ss = div_samples((uint32_t)t->p[P_SDIV]), at;
            if (chain.lane[i] == CHAIN_SILENT || !ss)
                continue;
            at = chain_from_u * (div_samples(0) / 48u) / ss;   /* (1/48 quarters -> samples -> its steps) */
            if (i == chain.clk) {                   /* (the passes it is past: the section's repeats left) */
                uint32_t done = at / len;
                chain.remaining = (uint8_t)(chain.remaining > done ? chain.remaining - done : 1u);
            }
            t->seq_idx = (uint16_t)((at % len + len - 1u) % len);
        }
}
/* (Dah) SONG's GRID turned while the song plays: it stops and starts again from there (ui_input.c) */
static volatile uint8_t chain_seek_req;
static void chain_stop(void)
{
    uint32_t i;
    chain.armed = 0;
    if (!chain.running)
        return;
    for (i = 0; i < NTRK; i++) {
        memcpy(&trk[i].p[P_SLEN], chain.timing[i], sizeof chain.timing[i]);
        trk[i].p[P_SQNT] = chain.qnt[i];
    }
    song.rec = chain.rec;
    chain.running = 0;
}
/* section r starts; with CUT, the FX buses' tails cleared (fx.c fx_cut) and every voice faded at once */
static void chain_cut(uint32_t r)
{
    uint32_t k, i;
    if (!(chain.config.row[r].repeat & CHAIN_CUT))
        return;
    fx_cut = 1;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                voice_kill(&trk[k].v[i]);
}
static void chain_tick(uint32_t n)
{
    const track_t *t = &trk[chain.clk % NTRK];
    uint32_t length;
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != (uint32_t)t->p[P_SLEN])
        return;
    length = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    if (t->seq_pos + n < length)
        return;
    if (chain.remaining > 1u) {
        chain.remaining--;
        return;
    }
    if (chain.row + 1u >= chain.config.count && !song_round) {
        seq_stop();
        return;
    }
    chain.carry = t->seq_pos + n - length;
    chain.src_ok[chain.cur] = 0;                    /* (free for the one after, chain_poll) */
    chain.row = chain.row + 1u < chain.config.count ? chain.row + 1u : 0u;   /* (Dah: the song round, no stop) */
    chain.remaining = CHAIN_REP(chain.config.row[chain.row]);
    chain_cut(chain.row);
    chain_apply();
}

