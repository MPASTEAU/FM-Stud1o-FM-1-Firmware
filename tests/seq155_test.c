/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 1.5.5 sequencer (#209 #186 #85), on the real sequencer and UI sources (with the stubs of tests/ui_test.c):
 *   CONDITIONS  a step's 1:2 .. 4:4 by the track's loops (each track its own LEN), counted from PLAY, RESTART, each SONG
 *               section; FILL / !FILL with GLO + E4 held (and the GLO lock); a NUDGE early step 1 the next loop's; a
 *               failed condition applies no lock; a step without one rolls its chance as before (the same draws)
 *   the knob    DETAIL's KNOB 1 past 100 % into the conditions (one a detent, a fast turn stops at 100), the marks, EDIT back to 100 %
 *   storage     FUN10 round trip (the chance byte 102..112), proj_bound, a byte past them refused; what 1.5 / 1.5.1 do
 *               with it (their proj_unpack's rule, copied: a project holding a condition is refused, one without loads)
 *   editor      STEP_SET / TRACK_STEP's condition byte (INFO 4B 01 0B), a chance of 100 without it keeps the condition
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* track 1 alone, LEN 16 of 1/16, GATE 64, no swing; notes 60 + i on step i where on bit i; the rest REST */
static track_t *melody(uint32_t on)
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    motion_clear(t);
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0;
    song.g[G_BPM] = 120;
    for (i = 0; i < 16u; i++)
        if ((on >> i) & 1u)
            t->step[i] = (step_t){{(uint8_t)(60u + i)}, 1, ST_NOTE, 0, 0};
    seq_fill = 0;
    return t;
}
static void set_cond(step_t *s, uint32_t c) { step_set_chance_v(s, 100u + c); }

/* the note-ons of track t while `blocks` blocks of CTL play (seq_tick alone, the transport already running) */
static uint32_t ons(track_t *t, uint32_t blocks, uint8_t *note, uint32_t nmax)
{
    uint32_t b, n = 0, i;
    for (b = 0; b < blocks; b++) {
        uint32_t v0 = vage;
        seq_tick(t, CTL);
        for (; v0 < vage; v0++)
            for (i = 0; i < NVOICE; i++)
                if (t->v[i].age == v0 + 1u && n < nmax)
                    note[n++] = t->v[i].note;
    }
    return n;
}
static uint32_t loop_blocks(const track_t *t) { return (uint32_t)t->p[P_SLEN] * div_samples(2) / CTL; }
/* loop by loop from PLAY: bit L of the result = note x played in loop L (the loop's own note 60 on step 1 counts them) */
static uint32_t heard(track_t *t, uint32_t loops, uint32_t x)
{
    uint8_t nt[512];
    uint32_t n, i, L = 0, m = 0;
    seq_start();
    n = ons(t, loops * loop_blocks(t) + 2u, nt, sizeof nt);
    seq_stop();
    for (i = 0; i < n; i++) {
        if (nt[i] == 60u && i)
            L++;
        if (nt[i] == x && L < 32u)
            m |= 1u << L;
    }
    return m;
}

static int conditions(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u | 1u << 2 | 1u << 4 | 1u << 6 | 1u << 8 | 1u << 10 | 1u << 12 | 1u << 14);
    set_cond(&t->step[2], 1);                          /* 1:2 */
    set_cond(&t->step[4], 2);                          /* 2:2 */
    set_cond(&t->step[6], 3);                          /* 1:3 */
    set_cond(&t->step[8], 7);                          /* 2:4 */
    set_cond(&t->step[10], 9);                         /* 4:4 */
    set_cond(&t->step[12], 10);                        /* FILL */
    set_cond(&t->step[14], 11);                        /* !FILL */
    ok = step_cond(&t->step[2]) == 1u && t->step[2].probability == 102u && t->step[14].probability == 112u &&
         step_chance(&t->step[2]) == 100u && step_chance_v(&t->step[10]) == 109u;
    bad += check("conditions: in the chance byte's free values 102..112, at 100 % (step_chance)", ok);
    bad += check("  1:2 on loops 1 3 5 .. (from PLAY, 12 loops)", heard(t, 12, 62) == 0x555u);
    bad += check("  2:2 on loops 2 4 6 ..", heard(t, 12, 64) == 0xAAAu);
    bad += check("  1:3 on loops 1 4 7 10", heard(t, 12, 66) == 0x249u);
    bad += check("  2:4 on loops 2 6 10", heard(t, 12, 68) == 0x222u);
    bad += check("  4:4 on loops 4 8 12", heard(t, 12, 70) == 0x888u);
    bad += check("  past 12 loops the count goes on (1:3 on 13 16 19 22, 4:4 on 16 20 24)",
                 heard(t, 24, 66) == 0x249249u && heard(t, 24, 70) == 0x888888u);
    ok = heard(t, 4, 72) == 0u && heard(t, 4, 74) == 0xFu;
    seq_fill = 1;
    ok &= heard(t, 4, 72) == 0xFu && heard(t, 4, 74) == 0u;
    seq_fill = 0;
    bad += check("  FILL plays only while FILL is held, !FILL only while not", ok);
    {   /* FILL taken mid-pass: from the next step on */
        uint8_t nt[64];
        uint32_t n, i, f = 0;
        seq_start();
        n = ons(t, 11u * div_samples(2) / CTL, nt, 64);   /* (into step 11) */
        seq_fill = 1;
        n += ons(t, 5u * div_samples(2) / CTL, nt + n, 64 - n);
        seq_stop();
        seq_fill = 0;
        for (i = 0; i < n; i++)
            f |= (nt[i] == 72u) | (nt[i] == 74u) << 1;
        bad += check("  FILL pressed on step 11: step 13's FILL plays, step 15's !FILL not", f == 1u);
    }
    {   /* STOP / PLAY: from loop 1 again; RESTART too */
        uint32_t a = heard(t, 3, 64), b = heard(t, 3, 64);
        bad += check("  PLAY counts from loop 1 again (2:2: loop 2 each time)", a == 2u && b == 2u);
    }
    {   /* each track counts its own LEN: T2 LEN 3 with a 1:2 on its step 1 */
        track_t *u = &trk[1];
        uint8_t nt[64];
        uint32_t n, i, c = 0;
        track_defaults_steps(u);
        u->p[P_SLEN] = 3; u->p[P_SDIV] = 2; u->p[P_SSWING] = 0; u->p[P_SGATE] = 64;
        u->step[0] = (step_t){{50}, 1, ST_NOTE, 0, 0};
        set_cond(&u->step[0], 1);
        seq_start();
        for (i = 0; i < 12u * div_samples(2) / CTL; i++)
            seq_tick(t, CTL);                          /* (T1 runs alongside) */
        seq_stop();
        seq_start();
        n = ons(u, (12u * div_samples(2) - div_samples(2) / 2u) / CTL, nt, 64);   /* T2 alone: 4 loops of 3 */
        seq_stop();
        for (i = 0; i < n; i++)
            c += nt[i] == 50u;
        bad += check("  each track counts its own loops (T2 LEN 3: 1:2 twice in 12 steps)", c == 2u);
        track_defaults_steps(u);
    }
    return bad;
}

/* a failed condition: the step plays nothing and applies none of its locks; a step without a condition rolls its chance
 * with the same draws as before (one rng() a step under 100 %, none at 100 % or with a condition) */
static int cond_locks_rolls(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u | 1u << 4);
    uint32_t r0, r1, i;
    set_cond(&t->step[4], 1);                          /* 1:2 */
    trkx[trk_index(t) % NTRK].home[0] = P_LEVEL + 1;   /* (Dah: locks on HOME's knobs only: LEVEL ASSIGN'd there) */
    motion_set_lock(t, 4, P_LEVEL, 7);
    seq_start();
    for (i = 0; i < 5u * div_samples(2) / CTL; i++)
        seq_tick(t, CTL);                              /* loop 1: plays, its lock with it */
    ok = t->p[P_LEVEL] == 7;
    for (i = 0; i < 16u * div_samples(2) / CTL; i++)
        seq_tick(t, CTL);                              /* loop 2, step 5: fails */
    ok &= t->p[P_LEVEL] != 7;
    seq_stop();
    bad += check("conditions: a step that plays applies its lock, one that fails none", ok);
    motion_clear(t);
    t = melody(0xFFFFu);
    for (i = 0; i < 16u; i++)
        if (i % 3u == 0u)
            step_set_chance(&t->step[i], 50);
        else if (i % 3u == 1u)
            set_cond(&t->step[i], 1 + i % NCOND);
    rng_state = 12345u;
    seq_start();
    for (i = 0; i < 16u * div_samples(2) / CTL; i++)
        seq_tick(t, CTL);
    seq_stop();
    r0 = rng();
    rng_state = 12345u;
    for (i = 0; i < 6u; i++)                           /* (the six 50 % steps: one draw each) */
        (void)rng();
    r1 = rng();
    bad += check("  the chance draws: one per step under 100 %, none for a condition (the same sequence as before)", r0 == r1);
    return bad;
}

/* SONG: each section's pattern counts from its first loop; the section's repeats are its loops */
static int cond_song(void)
{
    int bad = 0, ok;
    uint8_t nt[256];
    uint32_t n = 0, i, c1 = 0, c4 = 0, seen = 0;
    track_t *t = melody(1u | 1u << 4 | 1u << 8);
    set_cond(&t->step[4], 3);                          /* 1:3 */
    set_cond(&t->step[8], 9);                          /* 4:4 */
    t->p[P_SLEN] = 12;
    for (i = 1; i < NTRK; i++)
        track_defaults_steps(&trk[i]);
    pat_keep(2);                                       /* (the patterns kept as 2, pstore.c) */
    chain_config.count = 2;
    chain_config.row[0] = (chain_row_t){{2, 2, 2, 2}, 4};
    chain_config.row[1] = (chain_row_t){{2, 2, 2, 2}, 2};
    ok = chain_prepare() == 0;
    while (n < 256u && i++ < 40000u && (chain.running || chain.armed) && !(seen && !chain.row)) {   /* (Dah: once round) */
        uint32_t v0 = vage, k;
        events_block(CTL);
        seen |= chain.row == 1u;
        for (; v0 < vage; v0++)
            for (k = 0; k < NVOICE; k++)
                if (trk[0].v[k].age == v0 + 1u && n < 256u)
                    nt[n++] = trk[0].v[k].note;
    }
    for (i = 0; i < n; i++) {
        c1 += nt[i] == 64u;
        c4 += nt[i] == 68u;
    }
    seq_stop();
    ok &= c1 == 3u && c4 == 1u;                        /* 1:3: section 1 loops 1 4, section 2 loop 1; 4:4: section 1's 4th */
    bad += check("conditions in a SONG: each section from its loop 1 (1:3 x3 over 4 + 2 repeats, 4:4 once)", ok);
    chain_defaults(&chain_config);
    memset(proj_slot, 0, sizeof proj_slot);
    return bad;
}

/* NUDGE: step 1 played early (QUANTIZE OFF, -4/16) belongs to the loop it starts */
static int cond_nudge(void)
{
    track_t *t = melody(1u | 1u << 8);
    t->step[8].note[0] = 60;                           /* (the loop counter's note on step 9 too: two a loop) */
    t->step[0].note[0] = 80;
    set_cond(&t->step[0], 1);                          /* 1:2 */
    step_set_nudge(&t->step[0], -4);
    t->p[P_SQNT] = 0;
    {
        uint8_t nt[128];
        uint32_t n, i, c = 0;
        seq_start();
        n = ons(t, 6u * loop_blocks(t) - div_samples(2) / 2u / CTL, nt, 128);   /* (not the 7th loop's early step 1) */
        seq_stop();
        for (i = 0; i < n; i++)
            c += nt[i] == 80u;
        return check("conditions: a NUDGE early step 1 counts as the next loop's (1:2 x3 in 6 loops)", c == 3u);
    }
}

/* DETAIL's KNOB 1 */
static void go_page_title(const char *ti)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, ti); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    frame();
}
static void kt(uint32_t role, int32_t s) { memset(ui.enc_t, 0, sizeof ui.enc_t); turn(role, s); }
static int cond_ui(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u | 1u << 2);
    go_page(GR_ROLL);
    ui.cursor = 2;
    go_page_title("DETAIL");
    kt(EN_K1, 1);
    ok = step_chance_v(&t->step[2]) == 101u && step_cond(&t->step[2]) == 1u;
    kt(EN_K1, 7);
    ok &= step_cond(&t->step[2]) == 8u;                /* 3:4 */
    kt(EN_K1, 10);
    ok &= step_cond(&t->step[2]) == NCOND;             /* the end: !FILL */
    bad += check("DETAIL: KNOB 1 past 100 % into the conditions, one a detent (1:2 .. !FILL, held at the end)", ok);
    ui.force = 1; frame();
    bad += check("  its card COND, \"!FILL\"; the step marked", step_detailed(&t->step[2]));
    kt(EN_K1, -12);
    ok = step_chance_v(&t->step[2]) == 100u && !step_cond(&t->step[2]) && !t->step[2].probability;
    kt(EN_K1, -3);
    ok &= step_chance(&t->step[2]) == 97u;
    bad += check("  turned back: 100 % (held there on the way out), then the percentages", ok);
    ui.enc_t[EN_K1 - EN_K1] = 0;
    turn(EN_K1, 40);                                   /* a fast turn from 97 %: stops at 100 */
    ok = step_chance_v(&t->step[2]) == 100u;
    kt(EN_K1, 4);
    ok &= step_cond(&t->step[2]) == 4u;                /* 2:3 */
    bad += check("  a fast turn stops at 100 % on the way in", ok);
    go_page_title("DETAIL");
    ui.cursor = 2;
    press(B_EDIT);
    bad += check("DETAIL's EDIT: back to 100 % (no condition)", !t->step[2].probability);
    return bad;
}

/* the FILL key: GLO + E4 held; the lock (two taps) and E4 */
static int fill_key(void)
{
    int bad = 0, ok;
    melody(1);
    frames(400);
    btn_down(B_GLO); frames(100);
    key_down(key_at(0, 6)); frames(50);                /* E4: white place 6 */
    ok = seq_fill == 1u && ui.layer == LAYER_GLO;
    key_up(key_at(0, 6)); frames(50);
    ok &= !seq_fill;
    btn_up(B_GLO); frames(400);
    bad += check("FILL: GLO + E4 held is FILL, let go it is not", ok);
    ui_power_on();
    return bad;
}

/* FUN10 and older firmware */
static int old_accepts(const project_store_t *p)   /* 1.5 / 1.5.1's proj_unpack step rule: a chance byte above 101 refuses */
{
    uint32_t t, i, base;
    for (t = 0; t < NTRK_V10; t++)
        for (i = 0; i < NSTEP; i++) {
            base = 68u + t * (PROJ_NP_W + 2u + NSTEP * 9u) + PROJ_NP_W + 2u + i * 9u;
            if ((p->raw[base + 8u] & 127u) > 101u)
                return 0;
        }
    return 1;
}
static int cond_store(void)
{
    int bad = 0, ok;
    project_t a, b;
    project_store_t pk;
    track_t *t = melody(0xFFu);
    uint32_t i;
    step_set_chance(&t->step[1], 30);
    int old0;
    project_capture(&a);
    old0 = proj_pack(&pk, &a) && old_accepts(&pk);
    ok = old0;
    for (i = 0; i < NCOND; i++)
        set_cond(&a.t[0].step[i], 1u + i);
    a.sum = proj_sum(&a);
    ok &= proj_pack(&pk, &a) && proj_import(&b, &pk, sizeof pk);
    for (i = 0; i < NCOND; i++)
        ok &= step_cond(&b.t[0].step[i]) == 1u + i;
    bad += check("FUN10: the conditions round trip (the chance byte 102..112), the size unchanged", ok && sizeof pk == 3840u);
    bad += check("  1.5 / 1.5.1's rule refuses a project holding one (the slot reads empty there), one without loads",
                 old0 && !old_accepts(&pk));
    pk.raw[68u + PROJ_NP_W + 2u + 8u] = 113;
    {
        uint32_t s = proj_hash(pk.raw, PROJ_STORE_SIZE - 4u);
        memcpy(pk.raw + PROJ_STORE_SIZE - 4u, &s, 4);
    }
    bad += check("  a chance byte past the conditions (113) is refused even with a correct hash", !proj_import(&b, &pk, sizeof pk));
    a.t[0].step[0].probability = 120;
    proj_bound(&a);
    bad += check("  proj_bound: a byte past them back to 100 %, the conditions kept",
                 !a.t[0].step[0].probability && step_cond(&a.t[0].step[1]) == 2u);
    return bad;
}

int main(void)
{
    int bad = conditions() + cond_locks_rolls() + cond_song() + cond_nudge() + cond_ui() + fill_key() + cond_store();
    printf("%s\n", bad ? "SEQ 1.5.5 TEST FAILED" : "seq 1.5.5 tests passed");
    return bad != 0;
}
