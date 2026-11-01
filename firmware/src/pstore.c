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
#define PS_PAT_MAX (10u + 1u + NSTEP * 11u + 1u + MOTION_MAX * 3u + 1u)   /* a pattern at most (overlaps: 11 a step) */
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
/* a step <-> FUN10's 9 bytes (project.c proj_pack_trk / proj_unpack_trk). (overlaps) A NOTE step with notes going on
 * (core.h st_cont): time 3 there (older firmware refuses it), the notes going on in a 10th byte (b[9]) after its bytes */
#define PS_GOES 3u
static void ps_step_pack(const step_t *s, uint8_t *b)
{
    uint32_t r = step_ratchet(s) - 1u, nd = (uint32_t)step_nudge(s) & 15u, j;
    for (j = 0; j < 4u; j++)
        b[j] = (uint8_t)((s->note[j] > 127u ? 127u : s->note[j]) | (nd >> j & 1u) << 7);
    b[4] = (uint8_t)(s->n | (st_cont(s) ? PS_GOES : st_kind(s)) << 3 | (s->flags & 3u) << 5);
    b[5] = (uint8_t)((s->vel > 127u ? 127u : s->vel) | (r & 1u) << 7);
    b[6] = s->hit;
    b[7] = s->acc & s->hit;
    b[8] = (uint8_t)(s->probability | (r >> 1) << 7);
    b[9] = (uint8_t)st_cont(s);
}
static int ps_goes(const uint8_t *b) { return (b[4] >> 3 & 3u) == PS_GOES; }   /* (its 10th byte follows) */
static int ps_step_unpack(step_t *s, const uint8_t *b, uint32_t goes)   /* (goes: that 10th byte, 0 none) */
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
    if (s->time == PS_GOES) {
        if (!goes || goes >= 1u << (s->n & 7u))
            return 0;
        s->time = ST_NOTE;
        st_set_cont(s, goes);
    }
    if ((meta & 255u) > 127u || s->n > 4u || st_kind(s) > ST_REST || s->probability >= COND_FIRST + NCOND || (s->acc & ~s->hit))
        return 0;
    step_set_ratchet(s, (meta >> 8) + 1u);
    step_set_nudge(s, (int32_t)(nd ^ 8u) - 8);
    return 1;
}
#define PS_V2 0xFEu                                     /* (Dah) the compact steps follow (else: their count) */
static int ps_step_empty(const step_t *s)
{
    return st_kind(s) == ST_REST && !s->n && !s->hit && !s->vel && !s->flags && !s->probability;
}
static const uint8_t PS_TIMING[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_SQNT};
/* (Dah) the steps st[NSTEP], compact, into b: the bytes */
static uint32_t ps_steps_put(const step_t *st, uint8_t *b)
{
    uint32_t i = 0, pos = 0, j;
    uint8_t r[10];
    while (i < NSTEP) {
        const step_t *s = &st[i];
        if (ps_step_empty(s)) {
            i++;
            continue;
        }
        ps_step_pack(s, r);
        if (i < NSTEP - 1u && r[4] == (ST_TIE << 3) && !(r[0] | r[1] | r[2] | r[3] | r[5] | r[6] | r[7] | r[8])) {   /* a plain
                                                         * TIE: a run (not from the last step: 0xC0 | 63 is the end) */
            uint8_t q[10];
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
        if (ps_goes(r))
            b[pos++] = r[9];
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
            if (p + 10u > n || b[p] >= NSTEP || !ps_step_unpack(&st[b[p]], b + p + 1u, 0))   /* (never a note going on) */
                return 0;
            p += 10u;
        }
        *pos = p;
        return 1;
    }
    for (p++; p < n && b[p] != 0xFFu;) {
        uint32_t l = b[p++], at = l & 63u, j;
        uint8_t r[9];
        const uint8_t *q = r;
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
        } else {                                        /* a whole one */
            if (l >= NSTEP || p + 9u > n)
                return 0;
            q = b + p;
            p += 9u;
        }
        if (l < 0xC0u && !ps_step_unpack(&st[at], q, ps_goes(q) && p < n ? b[p++] : 0u))
            return 0;
    }
    if (p >= n)
        return 0;
    *pos = p + 1u;
    return 1;
}
/* ---- (long patterns) the pages after the first: LEN up to LEN_MAX, NPAGE pages of NSTEP. Page 0 is the pattern's
 * record as always (on the track for the one played); page p (1..NPAGE-1) of pattern num a record of its own, keyed
 * PS_PG(num, p): its steps (PS_V2's, indices in the page), then its automation (count, then step in the page, id,
 * value). An empty page: no record. The automation of every page of the pattern played is in the motion store (core.h
 * MPLACE: the page in place's bits 9..10); its pages' records get it when the pattern is stored away, the project
 * saved or a song reads them (pg_sync). The UI reads and edits a page through pgc (two pages decoded, written back when
 * changed), the audio ISR plays it through its ring (song_chain.c pgr, filled by pg_service) ---- */
#define PS_PG(num, p) ((uint32_t)(num) | (uint32_t)(p) << 6)
static step_t ps_st[NSTEP];                            /* (Dah) the main loop's: a pattern decoded (ps_decode, ps_info,
                                                         * ui_pattern.c pt_steps, the pages; none calls
                                                         * another) */
static motion_store_t ps_m;
typedef struct { step_t st[NSTEP]; uint8_t k, num, p, ok, dirty; uint32_t use; } pgc_t;
static pgc_t pgc[2] __attribute__((section(".pool")));
static uint32_t pgc_tick;
static void pg_clear(step_t *st)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        memset(&st[i], 0, sizeof st[i]);
        st[i].time = ST_REST;
    }
}
/* page p of track k's pattern num -> st (its steps), its automation appended to m (0: none); none stored: empty */
static void pg_load(uint32_t k, uint32_t num, uint32_t p, step_t *st, motion_store_t *m)
{
    int32_t at = ps_find(k, PS_PG(num, p));
    uint32_t pos = 0, n, c;
    const uint8_t *b;
    pg_clear(st);
    if (at < 0)
        return;
    b = ps_heap + at + 4u;
    n = ps_rlen((uint32_t)at);
    if (!ps_steps_get(b, n, &pos, st))
        return;
    for (c = pos < n ? b[pos++] : 0u; c-- && pos + 3u <= n; pos += 3u)
        if (m && m->count < MOTION_MAX) {
            motion_event_t *e = &m->event[m->count++];
            e->place = MPLACE(k, p * NSTEP + (b[pos] & 63u));
            e->param = b[pos + 1u];
            e->value = (int8_t)b[pos + 2u];
        }
}
/* page p (steps st, the motion store's automation of it) of the pattern track k plays into its record; 1 no room */
static int pg_store(uint32_t k, uint32_t p, const step_t *st)
{
    uint32_t i, n = 0, at, pos;
    int32_t o;
    ps_buf[0] = PS_V2;
    pos = 1u + ps_steps_put(st, ps_buf + 1u);
    at = pos++;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if (MP_TRK(e) != k || (e->place >> 9) != p)
            continue;
        ps_buf[pos++] = (uint8_t)(e->place & 63u);
        ps_buf[pos++] = e->param;
        ps_buf[pos++] = (uint8_t)e->value;
        n++;
    }
    ps_buf[at] = (uint8_t)n;
    o = ps_find(k, PS_PG(ps_num(k), p));
    if (o >= 0 && ps_rlen((uint32_t)o) == pos && !memcmp(ps_heap + o + 4u, ps_buf, pos))
        return 0;                                       /* (as it is: the heap untouched) */
    for (i = 0; i < NSTEP && ps_step_empty(&st[i]); i++)
        ;
    if (i == NSTEP && !n) {                             /* (empty: no record) */
        if (o >= 0) {
            ps_del(k, PS_PG(ps_num(k), p));
            pg_rev[k]++;
        }
        return 0;
    }
    pg_rev[k]++;
    return ps_put(k, PS_PG(ps_num(k), p), ps_buf, pos);
}
static void pgc_flush(pgc_t *e)                         /* a page edited, into its record */
{
    if (!e->ok || !e->dirty)
        return;
    e->dirty = 0;
    if (e->num != ps_num(e->k) || pg_store(e->k, e->p, e->st))
        ui_message("MEMORY FULL");                      /* (kept here meanwhile: it plays, it is shown) */
}
static void pgc_flush_all(void) { pgc_flush(&pgc[0]); pgc_flush(&pgc[1]); }
static void pgc_drop(uint32_t k)                        /* track k's (NTRK: every one) pages decoded: read again */
{
    uint32_t i;
    for (i = 0; i < 2u; i++)
        if (k >= NTRK || pgc[i].k == k)
            pgc[i].ok = 0;
}
/* page p (1..NPAGE-1) of track k's pattern num, decoded (the least used of the two read again; an edit there: dirty) */
static pgc_t *pgc_peek(uint32_t k, uint32_t num, uint32_t p)   /* .. decoded already, else 0 */
{
    uint32_t i;
    for (i = 0; i < 2u; i++)
        if (pgc[i].ok && pgc[i].k == k && pgc[i].num == num && pgc[i].p == p)
            return &pgc[i];
    return 0;
}
static pgc_t *pgc_get(uint32_t k, uint32_t num, uint32_t p)
{
    pgc_t *e = pgc_peek(k, num, p);
    if (!e) {
        e = !pgc[0].ok ? &pgc[0] : !pgc[1].ok ? &pgc[1] : pgc[0].use <= pgc[1].use ? &pgc[0] : &pgc[1];
        pgc_flush(e);
        pg_load(k, num, p, e->st, 0);
        e->k = (uint8_t)k; e->num = (uint8_t)num; e->p = (uint8_t)p;
        e->ok = 1; e->dirty = 0;
    }
    e->use = ++pgc_tick;
    return e;
}
/* the UI's step i (0..LEN_MAX - 1) of track t's pattern (a song playing: its section's, as seq_steps): page 0 on the
 * track (or the section's copy), the others decoded. The pointer holds until two other pages are read */
static const step_t *ui_st(const track_t *t, uint32_t i)
{
    uint32_t k = trk_index(t) % NTRK;
    if (i < NSTEP)
        return &seq_steps(t)[i];
    return &pgc_get(k, chain.running ? chain.lane[k] : ps_num(k), (i / NSTEP) % NPAGE)->st[i % NSTEP];
}
/* .. to edit (the track's own pattern): written back by the next pg_service (ui_input) */
static step_t *ed_st(track_t *t, uint32_t i)
{
    uint32_t k = trk_index(t) % NTRK;
    pgc_t *e;
    if (i < NSTEP)
        return &t->step[i];
    e = pgc_get(k, ps_num(k), (i / NSTEP) % NPAGE);
    e->dirty = 1;
    return &e->st[i % NSTEP];
}
static int pg_mpage(uint32_t k, uint32_t p)             /* track k's automation on page p: any */
{
    uint32_t i;
    for (i = 0; i < motion.count; i++)
        if (MP_TRK(&motion.event[i]) == k && (motion.event[i].place >> 9) == p)
            return 1;
    return 0;
}
/* the pattern track k plays: its pages' records as they are (the pages edited written back, each one's automation as
 * the motion store has it); main loop, before they are read or stored away. 1 = no room */
static int pg_sync(uint32_t k)
{
    uint32_t p;
    int r = 0;
    pgc_flush_all();
    for (p = 1; p < NPAGE; p++) {
        if (ps_find(k, PS_PG(ps_num(k), p)) < 0 && !pg_mpage(k, p))
            continue;
        pg_load(k, ps_num(k), p, ps_st, 0);
        r |= pg_store(k, p, ps_st);
    }
    return r;
}
/* the pattern track k plays now (decoded onto it): its pages' automation into the motion store, as room allows */
static void pg_motion_load(uint32_t k)
{
    uint32_t p, i, f;
    ps_m.count = 0;
    for (p = 1; p < NPAGE; p++)
        pg_load(k, ps_num(k), p, ps_st, &ps_m);
    f = motion_guard();
    for (i = 0; i < ps_m.count && motion.count < MOTION_MAX; i++)
        motion.event[motion.count++] = ps_m.event[i];
    if (ps_m.count)
        motion.on |= (uint8_t)(1u << k);
    motion_unguard(f);
}
static uint32_t pg_mcount(uint32_t k, uint32_t num)     /* the automation records of pattern num's pages */
{
    uint32_t p;
    ps_m.count = 0;
    for (p = 1; p < NPAGE; p++)
        pg_load(k, num, p, ps_st, &ps_m);
    return ps_m.count;
}
static void pg_ring_drop(uint32_t k)                    /* track k's ring: filled again (another pattern) */
{
    uint32_t f = motion_guard();
    memset(pgr[k % NTRK].abs, 0, sizeof pgr[0].abs);
    pgr[k % NTRK].dirty = 0;
    motion_unguard(f);
}
static void pg_del(uint32_t k, uint32_t num)            /* pattern num's pages gone */
{
    uint32_t p;
    for (p = 1; p < NPAGE; p++)
        ps_del(k, PS_PG(num, p));
    pgc_drop(k);
    pg_rev[k % NTRK]++;
}
static int pg_copy(uint32_t k, uint32_t from, uint32_t to)   /* pattern from's pages as pattern to's; 1 no room */
{
    uint32_t p;
    for (p = 1; p < NPAGE; p++) {
        int32_t at = ps_find(k, PS_PG(from, p));
        uint32_t n;
        if (at < 0)
            continue;
        n = ps_rlen((uint32_t)at);
        memcpy(ps_buf, ps_heap + at + 4u, n);
        if (ps_put(k, PS_PG(to, p), ps_buf, n))
            return 1;
    }
    return 0;
}
/* main loop (ui_input, every pass): the pages the audio ISR plays from (song_chain.c pgr). Each track: the steps live
 * recording wrote into its ring, back into their page; playing a LEN over NSTEP, the steps from the one playing on
 * (PGR_N of them), those from NSTEP up, from the pattern store (its pattern's pages, or its SONG section's): a slot not
 * holding its step yet once it is within PGR_N / 2 of the step playing (they all, then), or holding it as it was before
 * an edit (its page decoded: pgc), takes it */
static void pg_service(void)
{
    uint32_t k, f = motion_guard(), w = rec_wipe_req;
    rec_wipe_req = 0;
    motion_unguard(f);
    for (k = 0; w >> k; k++)                            /* (Dah) REC MODE REPLACE: a new take, the pages' records gone
                                                         * (the ring's steps, wiped and recorded into, come back below) */
        if ((w >> k) & 1u)
            pg_del(k, ps_num(k));
    for (k = 0; k < NTRK; k++)                          /* (Dah) .. on a DRUM track, a lane's hits on its pages */
        if (rp_wl[k]) {
            uint32_t m, p, i;
            f = motion_guard();
            m = rp_wl[k];
            rp_wl[k] = 0;
            motion_unguard(f);
            for (p = 1; p < NPAGE && p * NSTEP < (uint32_t)clamp(trk[k].p[P_SLEN], 1, LEN_MAX); p++) {
                pgc_t *e = pgc_get(k, ps_num(k), p);
                for (i = 0; i < NSTEP; i++)
                    lane_out(&e->st[i], m);
                e->dirty = 1;
            }
        }
    pgc_flush_all();
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX), num = chain.running ? chain.lane[k] : ps_num(k), d, f, cur, fill = 0;
        while (pgr[k].dirty) {
            uint32_t j = 0, a;
            step_t st;
            while (!((pgr[k].dirty >> j) & 1u))
                j++;
            f = motion_guard();
            a = pgr[k].abs[j];
            st = pgr[k].st[j];
            pgr[k].dirty &= (uint16_t)~(1u << j);
            motion_unguard(f);
            if (a >= NSTEP && pgr[k].num == ps_num(k) && !chain.running) {
                pgc_t *e = pgc_get(k, pgr[k].num, a / NSTEP);
                e->st[a % NSTEP] = st;
                e->dirty = 1;
            }
        }
        if (len <= NSTEP || !song.playing || k >= trk_count())
            continue;
        if (pgr[k].num != num) {
            pg_ring_drop(k);
            pgr[k].num = (uint8_t)num;
        }
        cur = t->seq_idx;
        for (d = 0; d < PGR_N; d++) {                   /* (any missing near) */
            uint32_t a = cur + d;
            if (a >= len)
                break;
            if (a >= NSTEP && pgr[k].abs[a % PGR_N] != a && d < PGR_N / 2u)
                fill = 1;
        }
        for (d = 0; d < PGR_N; d++) {
            uint32_t a = cur + d, j = a % PGR_N;
            pgc_t *e;
            if (a >= len)
                break;
            if (a < NSTEP)
                continue;
            e = pgr[k].abs[j] == a ? pgc_peek(k, num, a / NSTEP) : fill ? pgc_get(k, num, a / NSTEP) : 0;
            if (!e || (pgr[k].abs[j] == a && !memcmp(&pgr[k].st[j], &e->st[a % NSTEP], sizeof(step_t))))
                continue;
            f = motion_guard();
            if (!((pgr[k].dirty >> j) & 1u)) {          /* (recorded into meanwhile: back first, next pass) */
                pgr[k].st[j] = e->st[a % NSTEP];
                RING_PUBLISH();
                pgr[k].abs[j] = (uint8_t)a;
            }
            motion_unguard(f);
        }
    }
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
    for (i = 1; i < NPAGE; i++)                         /* (its other pages: none) */
        if (ps_find(k % NTRK, PS_PG(ps_num(k), i)) >= 0)
            return 0;
    return motion_count(t) == 0u && t->p[P_SLEN] <= NSTEP;
}
static int ps_store(uint32_t k)
{
    uint32_t n;
    if (pg_sync(k % NTRK))
        return 1;
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
        if (pos < len && motion.count - motion_count(&trk[k]) + r[pos] + pg_mcount(k, num) > MOTION_MAX)
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
    pg_motion_load(k);
    pg_ring_drop(k);
    pg_rev[k]++;
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
        if (ps_put(k % NTRK, num, ps_buf, n) || pg_copy(k % NTRK, ps_num(k), num)) {
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
        if (pg_sync(k))
            return 1;
        if (ps_empty(k))
            continue;
        n = ps_encode(&trk[k], ps_buf);
        if (ps_put(k, num, ps_buf, n) || pg_copy(k, ps_num(k), num))
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
    pg_del(k, num);
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
        pg_motion_load(k);
    } else {
        ps_cur[k] = 1;
        ps_blank(k);
    }
    pg_ring_drop(k);
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
    if (tm[0] > (int16_t)NSTEP) {                       /* (long: NSTEP cells, each its steps' first note, else a TIE) */
        uint32_t len = (uint32_t)tm[0], c, i;
        for (c = 0; c < NSTEP; c++) {
            step_t pick;
            memset(&pick, 0, sizeof pick);
            pick.time = ST_REST;
            for (i = c * len / NSTEP; i < (c + 1u) * len / NSTEP; i++) {
                const step_t *st = i < NSTEP ? &ps_st[i] : &pgc_get(k, num, i / NSTEP)->st[i % NSTEP];
                if (st_kind(st) == ST_NOTE && (st->n || st->hit)) {
                    pick = *st;
                    break;
                }
                if (st_kind(st) == ST_TIE && st_kind(&pick) == ST_REST)
                    pick = *st;
            }
            ps_st[c] = pick;
        }
    }
    return len48_of(tm[0], tm[1]);
}
static void ps_reset(void)                              /* no pattern but the ones on the tracks, each number 1 */
{
    uint32_t f = motion_guard();
    pgr_drop_all();
    motion_unguard(f);
    pgc_drop(NTRK);
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
    for (k = 0; k < trk_count(); k++)
        (void)pg_sync(k);                               /* (the pages' automation as the tracks have it) */
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
        } else {
            uint32_t p;
            for (p = 1; p < NPAGE; p++)                 /* (its pages' automation; their steps: pg_service) */
                pg_load(k, s, p, ps_st, &src->motion);
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
