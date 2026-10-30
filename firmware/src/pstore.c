/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* each track's patterns, as many as fit (numbered 1..PS_NUM). The one a track plays and edits is on the
 * track (its steps, LEN DIV SWING GATE QUANTIZE, its automation and locks in the shared motion store); the others are
 * in ps_heap, compact: a record [track][number][length, 2 bytes][the pattern], the pattern as
 *   LEN DIV SWING GATE QUANTIZE (2 bytes each), the steps that are not empty (their count, then index + FUN10's 9
 *   bytes each: notes with the NUDGE, n time flags, velocity with RATCH, hits, accents, chance with RATCH), the
 *   automation (its count, then step, id with the lock bit, value: 3 bytes each)
 * so an empty pattern costs 17 bytes and a note a step 10. (Dah) Since then the steps compact (PS_V2 after the timing):
 * a run of plain TIEs 2 bytes (its first step | 0xC0, its length: a note held over the whole pattern, 6 bytes in all),
 * a note step its index | 0x80, n time flags (bit 7: the rare fields follow), its n notes, its velocity [, hits,
 * accents, chance]; a step with a NUDGE its index and FUN10's 9 bytes; 0xFF after the last. The older form (a count)
 * still reads. pattern_go puts the track's pattern in the heap and the one
 * asked on the track (main loop; the audio ISR sees the change at once, under the guard). The project's file
 * (projfs.c) keeps the heap and each track's number; the SONG's sections name a number per track. */
#define PS_NUM 32u                                      /* patterns a track, 1..PS_NUM */
#define PS_HEAP 8192u
#define PS_PAT_MAX (10u + 1u + NSTEP * 10u + 1u + MOTION_MAX * 3u + 1u)   /* a pattern at most */
static uint8_t ps_heap[PS_HEAP] __attribute__((section(".pool")));
static uint8_t ps_buf[PS_PAT_MAX] __attribute__((section(".pool")));   /* one pattern on its way */
static uint32_t ps_used;
static uint32_t ps_gen;                                 /* +1 at each change of the heap (SONG's pictures read again) */
static uint8_t ps_cur[NTRK];                            /* the number each track plays (0: none yet, as 1) */

static uint32_t ps_num(uint32_t k) { return ps_cur[k % NTRK] ? ps_cur[k % NTRK] : 1u; }
static uint32_t ps_rlen(uint32_t at) { return (uint32_t)ps_heap[at + 2u] | (uint32_t)ps_heap[at + 3u] << 8; }
/* track k's pattern num in the heap: its record's offset, -1 none */
static int32_t ps_find(uint32_t k, uint32_t num)
{
    uint32_t at = 0;
    while (at + 4u <= ps_used) {
        if (ps_heap[at] == k && ps_heap[at + 1u] == num)
            return (int32_t)at;
        at += 4u + ps_rlen(at);
    }
    return -1;
}
static void ps_del(uint32_t k, uint32_t num)
{
    int32_t at = ps_find(k, num);
    uint32_t n, i;
    if (at < 0)
        return;
    n = 4u + ps_rlen((uint32_t)at);
    for (i = (uint32_t)at; i + n < ps_used; i++)        /* (no memmove on the device) */
        ps_heap[i] = ps_heap[i + n];
    ps_used -= n;
    ps_gen++;
}
/* track k's pattern num := p[0..n-1]; 1 = no room (the heap as it was) */
static int ps_put(uint32_t k, uint32_t num, const uint8_t *p, uint32_t n)
{
    int32_t at = ps_find(k, num);
    uint32_t old = at < 0 ? 0u : 4u + ps_rlen((uint32_t)at);
    if (ps_used - old + 4u + n > PS_HEAP)
        return 1;
    ps_del(k, num);
    ps_heap[ps_used] = (uint8_t)k;
    ps_heap[ps_used + 1u] = (uint8_t)num;
    ps_heap[ps_used + 2u] = (uint8_t)n;
    ps_heap[ps_used + 3u] = (uint8_t)(n >> 8);
    memcpy(ps_heap + ps_used + 4u, p, n);
    ps_used += 4u + n;
    ps_gen++;
    return 0;
}
/* a step <-> FUN10's 9 bytes (project.c proj_pack_trk / proj_unpack_trk) */
static void ps_step_pack(const step_t *s, uint8_t *b)
{
    uint32_t r = step_ratchet(s) - 1u, nd = (uint32_t)step_nudge(s) & 15u, j;
    for (j = 0; j < 4u; j++)
        b[j] = (uint8_t)((s->note[j] > 127u ? 127u : s->note[j]) | (nd >> j & 1u) << 7);
    b[4] = (uint8_t)(s->n | s->time << 3 | (s->flags & 3u) << 5);
    b[5] = (uint8_t)((s->vel > 127u ? 127u : s->vel) | (r & 1u) << 7);
    b[6] = s->hit;
    b[7] = s->acc & s->hit;
    b[8] = (uint8_t)(s->probability | (r >> 1) << 7);
}
static int ps_step_unpack(step_t *s, const uint8_t *b)
{
    uint32_t meta, nd = 0, j;
    for (j = 0; j < 4u; j++) {
        nd |= (uint32_t)(b[j] >> 7) << j;
        s->note[j] = b[j] & 127u;
    }
    meta = b[4];
    s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
    s->vel = b[5] & 127u; meta |= (b[5] & 128u) << 1;
    s->hit = b[6]; s->acc = b[7];
    s->probability = b[8] & 127u; meta |= (b[8] & 128u) << 2;
    if ((meta & 255u) > 127u || s->n > 4u || s->time > ST_REST || s->probability >= COND_FIRST + NCOND || (s->acc & ~s->hit))
        return 0;
    step_set_ratchet(s, (meta >> 8) + 1u);
    step_set_nudge(s, (int32_t)(nd ^ 8u) - 8);
    return 1;
}
#define PS_V2 0xFEu                                     /* (Dah) the compact steps follow (else: their count) */
static int ps_step_empty(const step_t *s)
{
    return s->time == ST_REST && !s->n && !s->hit && !s->vel && !s->flags && !s->probability;
}
static const uint8_t PS_TIMING[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_SQNT};
/* (Dah) the steps st[NSTEP], compact, into b: the bytes */
static uint32_t ps_steps_put(const step_t *st, uint8_t *b)
{
    uint32_t i = 0, pos = 0, j;
    uint8_t r[9];
    while (i < NSTEP) {
        const step_t *s = &st[i];
        if (ps_step_empty(s)) {
            i++;
            continue;
        }
        ps_step_pack(s, r);
        if (i < NSTEP - 1u && r[4] == (ST_TIE << 3) && !(r[0] | r[1] | r[2] | r[3] | r[5] | r[6] | r[7] | r[8])) {   /* a plain
                                                         * TIE: a run (not from the last step: 0xC0 | 63 is the end) */
            uint8_t q[9];
            for (j = i + 1u; j < NSTEP; j++) {
                ps_step_pack(&st[j], q);
                if (memcmp(q, r, 9))
                    break;
            }
            b[pos++] = (uint8_t)(0xC0u | i);
            b[pos++] = (uint8_t)(j - i);
            i = j;
            continue;
        }
        if (!((r[0] | r[1] | r[2] | r[3]) & 0x80u)) {    /* no NUDGE: compact */
            uint32_t x = (r[6] | r[7] | r[8]) != 0u, n = r[4] & 7u;
            b[pos++] = (uint8_t)(0x80u | i);
            b[pos++] = (uint8_t)(r[4] | x << 7);
            for (j = 0; j < n; j++)
                b[pos++] = r[j];
            b[pos++] = r[5];
            if (x) {
                b[pos++] = r[6]; b[pos++] = r[7]; b[pos++] = r[8];
            }
        } else {
            b[pos++] = (uint8_t)i;
            memcpy(b + pos, r, 9);
            pos += 9u;
        }
        i++;
    }
    b[pos++] = 0xFFu;
    return pos;
}
/* .. back (either form at b + *pos: PS_V2's, or the count's), into st (all reset first); 0 = not steps */
static int ps_steps_get(const uint8_t *b, uint32_t n, uint32_t *pos, step_t *st)
{
    uint32_t i, c, p = *pos;
    for (i = 0; i < NSTEP; i++) {
        memset(&st[i], 0, sizeof st[i]);
        st[i].time = ST_REST;
    }
    if (p >= n)
        return 0;
    if (b[p] != PS_V2) {                                /* the older form: a count, then index + 9 bytes each */
        for (c = b[p++]; c--;) {
            if (p + 10u > n || b[p] >= NSTEP || !ps_step_unpack(&st[b[p]], b + p + 1u))
                return 0;
            p += 10u;
        }
        *pos = p;
        return 1;
    }
    for (p++; p < n && b[p] != 0xFFu;) {
        uint32_t l = b[p++], at = l & 63u, j;
        uint8_t r[9];
        if ((l & 0xC0u) == 0xC0u) {                     /* a run of TIEs */
            if (p >= n || at + b[p] > NSTEP || !b[p])
                return 0;
            for (j = 0; j < b[p]; j++)
                st[at + j].time = ST_TIE;
            p++;
        } else if (l & 0x80u) {                         /* a compact step */
            uint32_t x, ns;
            if (p >= n)
                return 0;
            memset(r, 0, sizeof r);
            r[4] = b[p] & 0x7Fu;
            x = b[p++] >> 7;
            ns = r[4] & 7u;
            if (ns > 4u || p + ns + 1u + (x ? 3u : 0u) > n)
                return 0;
            for (j = 0; j < ns; j++)
                r[j] = b[p++];
            r[5] = b[p++];
            if (x) {
                r[6] = b[p++]; r[7] = b[p++]; r[8] = b[p++];
            }
            if (!ps_step_unpack(&st[at], r))
                return 0;
        } else {                                        /* a whole one */
            if (l >= NSTEP || p + 9u > n || !ps_step_unpack(&st[at], b + p))
                return 0;
            p += 9u;
        }
    }
    if (p >= n)
        return 0;
    *pos = p + 1u;
    return 1;
}
/* track t's pattern (on the track, its records in the motion store) into b: its length */
static uint32_t ps_encode(const track_t *t, uint8_t *b)
{
    uint32_t k = trk_index(t), pos = 0, i, n = 0, at;
    for (i = 0; i < 5u; i++) {
        int16_t v = motion_base_value(t, PS_TIMING[i]);
        if (chain.running)                              /* (Dah: a song plays: t->p holds its section's timing; the
                                                         * track's own is kept in chain.timing / chain.qnt) */
            v = i < 4u ? chain.timing[k % NTRK][i] : (int16_t)chain.qnt[k % NTRK];
        b[pos++] = (uint8_t)v;
        b[pos++] = (uint8_t)((uint16_t)v >> 8);
    }
    b[pos++] = PS_V2;
    pos += ps_steps_put(t->step, b + pos);
    at = pos++;
    n = 0;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) != k)
            continue;
        b[pos++] = (uint8_t)(e->place & 63u);
        b[pos++] = e->param;
        b[pos++] = (uint8_t)e->value;
        n++;
    }
    b[at] = (uint8_t)n;
    b[pos++] = (uint8_t)((motion.on >> k) & 1u);       /* (its automation PLAY: it goes with the pattern) */
    return pos;
}
/* b[0..n-1] onto track t (steps, timing, automation); 0 = not a pattern (the track as it was, empty) */
static step_t ps_st[NSTEP];                            /* (Dah) the main loop's: a pattern decoded (ps_decode, ps_info,
                                                         * ui_pattern.c pt_steps; none calls another) */
static motion_store_t ps_m;
static int ps_decode(track_t *t, const uint8_t *b, uint32_t n)
{
    int16_t tv[5];
    uint32_t k = trk_index(t), pos = 10u, i, c, f;
    if (n < 12u)
        return 0;
    for (i = 0; i < 5u; i++)
        tv[i] = (int16_t)((uint16_t)b[2u * i] | (uint16_t)b[2u * i + 1u] << 8);
    if (!ps_steps_get(b, n, &pos, ps_st) || pos >= n)
        return 0;
    memset(&ps_m, 0, sizeof ps_m);
    for (c = b[pos++]; c--;) {
        motion_event_t *e;
        if (pos + 3u > n || ps_m.count >= MOTION_MAX)
            return 0;
        e = &ps_m.event[ps_m.count++];
        e->place = (uint16_t)(k << 6 | (b[pos] & 63u));
        e->param = b[pos + 1u];
        e->value = (int8_t)b[pos + 2u];
        pos += 3u;
    }
    ps_m.on = (uint8_t)((pos < n ? b[pos] & 1u : ps_m.count != 0u) << k);   /* (its automation PLAY) */
    f = motion_guard();
    memcpy(t->step, ps_st, sizeof ps_st);
    for (i = 0; i < 5u; i++) {
        const param_desc_t *d = &TP[PS_TIMING[i]];
        t->p[PS_TIMING[i]] = (int16_t)clamp(tv[i], d->min, d->max);
    }
    motion_unguard(f);
    if (motion_replace_track(t, &ps_m))                   /* (no room in the shared store: the pattern without its automation) */
        motion_clear(t);
    return 1;
}
/* track k's pattern as it is on the track, into the heap (its number); 1 = no room */
static int ps_empty(uint32_t k)                         /* (Dah) track k's steps all empty, no automation */
{
    const track_t *t = &trk[k % NTRK];
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (!ps_step_empty(&t->step[i]))
            return 0;
    return motion_count(t) == 0u;
}
static int ps_store(uint32_t k)
{
    uint32_t n;
    n = ps_empty(k) ? 0u : ps_encode(&trk[k % NTRK], ps_buf);   /* (Dah) an empty pattern: its record alone, 4
                                                         * bytes (it comes back empty, the timing the track's) */
    return ps_put(k % NTRK, ps_num(k), ps_buf, n);
}
/* an empty pattern on track k (its timing kept) */
static void ps_blank(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t i, f = motion_guard();
    for (i = 0; i < NSTEP; i++) {
        memset(&t->step[i], 0, sizeof t->step[i]);
        t->step[i].time = ST_REST;
    }
    motion_unguard(f);
    motion_clear(t);
}
/* track k plays pattern num (1..PS_NUM): the one on it into the heap, num onto it (none in the heap: a new, empty one,
 * the timing kept). 1 = no room for the one it leaves (nothing changed) */
/* (Dah) the SONG's sections no longer name track k's pattern num (0: any): "-" there, a later pattern of that number
 * does not take them over */
static void ps_song_forget(uint32_t k, uint32_t num)
{
    uint32_t r;
    for (r = 0; r < chain_config.count && r < CHAIN_ROWS; r++)
        if (num ? chain_config.row[r].slot[k % NTRK] == num : 1)
            chain_config.row[r].slot[k % NTRK] = (uint8_t)CHAIN_SILENT;
}
static int pattern_go(uint32_t k, uint32_t num)
{
    int32_t at;
    k %= NTRK;
    if (num < 1u || num > PS_NUM)
        return 1;
    if (num == ps_num(k))
        return 0;
    at = ps_find(k, num);
    if (at >= 0) {                                      /* (Dah) its automation must fit the shared store, else it would
                                                         * be dropped, and stored again without: refused (MEMORY FULL) */
        const uint8_t *r = ps_heap + at + 4u;
        uint32_t len = ps_rlen((uint32_t)at), pos = 11u + 10u * (len > 10u ? r[10] : 0u);
        if (pos < len && motion.count - motion_count(&trk[k]) + r[pos] > MOTION_MAX)
            return 1;
    }
    if (ps_store(k))
        return 1;
    ps_cur[k] = (uint8_t)num;
    ps_gen++;
    at = ps_find(k, num);
    if (at < 0 || !ps_decode(&trk[k], ps_heap + at + 4u, ps_rlen((uint32_t)at)))
        ps_blank(k);
    ps_del(k, num);                                     /* (the one played lives on the track: no stale copy) */
    ui.force = 1;
    return 0;
}
/* track k's patterns, the one it plays included: their numbers in order into nums (n of them) */
static uint32_t ps_list(uint32_t k, uint8_t *nums)
{
    uint32_t n = 0, num;
    for (num = 1; num <= PS_NUM; num++)
        if (num == ps_num(k) || ps_find(k % NTRK, num) >= 0)
            nums[n++] = (uint8_t)num;
    return n;
}
static uint32_t ps_free_num(uint32_t k)                 /* the first number track k has no pattern on, 0 = all taken */
{
    uint32_t num;
    for (num = 1; num <= PS_NUM; num++)
        if (num != ps_num(k) && ps_find(k % NTRK, num) < 0)
            return num;
    return 0;
}
/* a new pattern on track k: empty (dup 0) or a copy of the one it plays; it plays it. 0 done, 1 no room, 2 no number */
static int pattern_new(uint32_t k, int dup)
{
    uint32_t num = ps_free_num(k);
    if (!num)
        return 2;
    if (ps_store(k))
        return 1;
    if (dup) {                                          /* (the copy: the same bytes under the new number) */
        uint32_t n = ps_encode(&trk[k % NTRK], ps_buf);
        if (ps_put(k % NTRK, num, ps_buf, n)) {
            ps_del(k % NTRK, ps_num(k));                /* (Dah: refused: no stale copy of the one played left) */
            return 1;
        }
    }
    return pattern_go(k, num) ? 1 : 0;
}
/* (Dah) the patterns as FL Studio's: a number is every track's pattern of that number (pattern 1: all the
 * instruments' 1). Every track to num (its own, or an empty one); a number none has; new (empty or a copy of each
 * track's); one gone on every track. 0 done, 1 no room, 2 no number */
static int ps_exists(uint32_t k, uint32_t num);
static int pattern_go_all(uint32_t num)
{
    uint32_t k;
    int r = 0;
    for (k = 0; k < trk_count(); k++)
        r |= pattern_go(k, num);
    return r;
}
static uint32_t ps_free_all(void)
{
    uint32_t num, k;
    for (num = 1; num <= PS_NUM; num++) {
        for (k = 0; k < trk_count() && !ps_exists(k, num); k++)
            ;
        if (k == trk_count())
            return num;
    }
    return 0;
}
static uint32_t ps_list_all(uint8_t *nums)             /* the numbers any track has a pattern on, in order */
{
    uint32_t n = 0, num, k;
    for (num = 1; num <= PS_NUM; num++) {
        for (k = 0; k < trk_count() && !ps_exists(k, num); k++)
            ;
        if (k < trk_count())
            nums[n++] = (uint8_t)num;
    }
    return n;
}
static int pattern_new_all(int dup)
{
    uint32_t num = ps_free_all(), k;
    if (!num)
        return 2;
    for (k = 0; dup && k < trk_count(); k++) {          /* (the copies: each track's bytes under the new number) */
        uint32_t n;
        if (ps_empty(k))
            continue;
        n = ps_encode(&trk[k], ps_buf);
        if (ps_put(k, num, ps_buf, n))
            return 1;
    }
    return pattern_go_all(num) ? 1 : 0;
}
/* track k's pattern num gone; the one it plays: it goes to another (or an empty 1) */
static void confirm_open(uint32_t kind, uint32_t trk);
/* (Dah) GLO + ALGORITHM: the previous, the next pattern (every track); past the last a new, empty one */
static void pattern_turn(int32_t s)
{
    uint8_t nums[PS_NUM];
    uint32_t n = ps_list_all(nums), i, cur = ps_num(song.sel);
    int r;
    for (i = 0; i + 1u < n && nums[i] != cur; i++)
        ;
    if (s < 0 && !i)
        return;
    if (s > 0 && i + 1u >= n) {                         /* (Dah: a new one asked first, as ADD TRACK) */
        if (!ps_free_all())
            ui_message("NO MORE PATTERNS");
        else if (!ui.confirm)
            confirm_open(CF_NEW_PAT, ps_free_all());
        return;
    }
    (void)r;
    if (pattern_go_all(nums[s > 0 ? i + 1u : i - 1u]))
        ui_message("MEMORY FULL");
}
static void pattern_del(uint32_t k, uint32_t num);
static void pattern_del_all(uint32_t num)              /* (Dah) num on every track gone, every track then on the same one */
{
    uint32_t k, to;
    for (k = 0; k < trk_count(); k++)
        pattern_del(k, num);
    to = ps_num(song.sel);
    (void)pattern_go_all(to);
}
static void pattern_del(uint32_t k, uint32_t num)
{
    uint8_t nums[PS_NUM];
    uint32_t n, i;
    k %= NTRK;
    ps_del(k, num);
    ps_song_forget(k, num);
    if (num != ps_num(k))
        return;
    n = ps_list(k, nums);
    for (i = 0; i < n && nums[i] == num; i++)
        ;
    if (i < n) {                                        /* (the next one there, without storing this one again) */
        int32_t at = ps_find(k, nums[i]);
        ps_cur[k] = nums[i];
        if (at < 0 || !ps_decode(&trk[k], ps_heap + at + 4u, ps_rlen((uint32_t)at)))
            ps_blank(k);
    } else {
        ps_cur[k] = 1;
        ps_blank(k);
    }
    ui.force = 1;
}
/* track k's pattern num: there? (the one played always is) */
static int ps_exists(uint32_t k, uint32_t num) { return num && num <= PS_NUM && (num == ps_num(k) || ps_find(k % NTRK, num) >= 0); }
/* track k's pattern num into ps_buf (the one played: encoded from the track): its length, 0 none */
static uint32_t ps_fetch(uint32_t k, uint32_t num)
{
    int32_t at;
    if (num == ps_num(k))
        return ps_encode(&trk[k % NTRK], ps_buf);
    at = ps_find(k % NTRK, num);
    if (at < 0)
        return 0;
    memcpy(ps_buf, ps_heap + at + 4u, ps_rlen((uint32_t)at));
    return ps_rlen((uint32_t)at);
}
/* b[0..n-1] (a pattern of track k) into a SONG section's buffers: its steps, LEN DIV SWING GATE, QUANTIZE, its
 * automation appended to m. 0 = not one (the steps empty) */
static int ps_decode_to(uint32_t k, const uint8_t *b, uint32_t n, step_t *st, int16_t *timing, uint8_t *qnt, motion_store_t *m)
{
    uint32_t pos = 10u, i, c;
    for (i = 0; i < NSTEP; i++) {
        memset(&st[i], 0, sizeof st[i]);
        st[i].time = ST_REST;
    }
    if (n < 12u)
        return 0;
    (void)c;
    for (i = 0; i < 4u; i++)
        timing[i] = (int16_t)clamp((int16_t)((uint16_t)b[2u * i] | (uint16_t)b[2u * i + 1u] << 8), TP[P_SLEN + i].min,
                                   TP[P_SLEN + i].max);
    *qnt = (uint8_t)clamp((int16_t)((uint16_t)b[8] | (uint16_t)b[9] << 8), TP[P_SQNT].min, TP[P_SQNT].max);
    if (!ps_steps_get(b, n, &pos, st))
        return 0;
    for (c = pos < n ? b[pos++] : 0u; c--;) {
        motion_event_t *e;
        if (pos + 3u > n)
            return 0;
        if (m->count < MOTION_MAX) {
            e = &m->event[m->count++];
            e->place = (uint16_t)(k << 6 | (b[pos] & 63u));
            e->param = b[pos + 1u];
            e->value = (int8_t)b[pos + 2u];
        }
        pos += 3u;
    }
    if (pos < n ? b[pos] & 1u : 0u)                     /* (its automation PLAY) */
        m->on |= (uint8_t)(1u << k);
    return 1;
}
/* SONG's picture of track k's pattern num: its length (1/48 quarters, 0 none), its LEN (*slen); its steps left
 * decoded in ps_st (ui_graph.c sg_pic draws them) */
static uint32_t len48_of(int32_t len, int32_t div);
static uint32_t ps_info(uint32_t k, uint32_t num, uint8_t *slen)
{
    int16_t tm[4];
    uint8_t q;
    uint32_t n = ps_exists(k, num) ? ps_fetch(k, num) : 0u;
    *slen = 0;
    ps_m.count = 0;
    if (!n || !ps_decode_to(k, ps_buf, n, ps_st, tm, &q, &ps_m))
        return 0;
    *slen = (uint8_t)clamp(tm[0], 1, NSTEP);
    return len48_of(tm[0], tm[1]);
}
static void ps_reset(void)                              /* no pattern but the ones on the tracks, each number 1 */
{
    ps_used = 0;
    ps_gen++;
    memset(ps_cur, 0, sizeof ps_cur);
    seq_off = 0;                                        /* (Dah: every track on its pattern again) */
}

/* section r's patterns into buffer b (main loop: chain_prepare, chain_poll; Dah: the buffers in turn, the
 * song round); each track's pattern decoded from the heap (pstore.c), a silent or missing one empty */
static void chain_fill(uint32_t r, uint32_t b)
{
    uint32_t k, n;
    chain_pattern_t *src = &chain.source[b];
    chain.src_ok[b] = 0;
    memset(&src->motion, 0, sizeof src->motion);
    for (k = 0; k < NTRK; k++) {
        uint32_t s = k < trk_count() && r < chain.config.count ? chain.config.row[r].slot[k] : CHAIN_SILENT;
        n = s != CHAIN_SILENT && ps_exists(k, s) ? ps_fetch(k, s) : 0u;
        if (!n || !ps_decode_to(k, ps_buf, n, src->step[k], src->timing[k], &src->qnt[k], &src->motion)) {
            uint32_t i;
            for (i = 0; i < NSTEP; i++) {
                memset(&src->step[k][i], 0, sizeof src->step[k][i]);
                src->step[k][i].time = ST_REST;
            }
            memcpy(src->timing[k], &trk[k].p[P_SLEN], sizeof src->timing[k]);
            src->qnt[k] = (uint8_t)trk[k].p[P_SQNT];
        }
    }
    chain.src_row[b] = (uint8_t)r;
    RING_PUBLISH();
    chain.src_ok[b] = 1;
}
/* main loop: the next section's patterns ready before it starts */
static void chain_poll(void)
{
    uint32_t r;
    if (!chain.running)
        return;
    r = chain.row + 1u < chain.config.count ? chain.row + 1u : 0u;   /* (Dah: after the last, the first again) */
    if (!(chain.src_ok[chain.cur ^ 1u] && chain.src_row[chain.cur ^ 1u] == r))
        chain_fill(r, chain.cur ^ 1u);
}
