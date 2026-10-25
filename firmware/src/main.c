/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA boot and main loop. Boot order: WDT first, boot-loop guard, fatal vectors,
 * guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
#if FELUCCA_CPU2
extern uint32_t _c1_start[], _c1_end[], _c1_load[];
#endif


/* TIMER5 outranks ALNK0, so the scan keeps its 100 us pace while a half buffer renders: before,
 * the ticks stopped for the whole render (0.7 ms idle, several ms loaded), the column lit when it
 * began stayed lit that long (a ~16 Hz flicker over all LEDs, beating with the scan) and the
 * encoders lost frames. Nested in ALNK0 it only scans (GPIO + fm1_in, nothing the audio ISR touches)
 * and counts ms; USB and UART polls wait for the first tick after the render, as they always did,
 * and the time spent nested is handed to the audio ISR so its load figures stay render-only.
 * The USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes up to
 * ~5 ms): uac_service also runs nested. It touches only EP4 (INDEX is set on every access) and the
 * consumer side of the audio ring, and usb_poll never runs nested, so the two never interleave. */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due)
            uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    scr_wake_now();                                   /* (MENU > SCREEN OFF: the panel on first) */
    lcd_fill(0, 0, 240, 240, UI_CRASH_BG);            /* fixed, outside the palettes */
    draw_text_line(0, 8, 240, &AF_M, FW_NAME " CRASH", UI_CRASH_INK, UI_CRASH_BG, 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->emu);
    draw_text_line(10, 84, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->dbg);
    draw_text_line(10, 102, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->rets);
    draw_text_line(10, 120, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

#if FELUCCA_DEMO
/* Dah's power-on sound (FELUCCA_DEMO): drones, slow and soft (66 BPM in 1/16, D dorian over a D pedal, nothing shrill:
 * every track's HIGH CUT at 4 kHz at most), fourteen sections of four bars (3 min 20) from the SONG, eight tracks:
 *   track 1  ANALOG STRINGS, the drone (D and A), its brightness one curve over the song (the EQ's HIGH CUT), phased
 *   track 2  GRAIN CLOUD PAD, scanning its sample (POS, DENS)   track 3  PHASE STRING, bowed fifths
 *   track 4  VOICE CHOIR AAH, low, its chords sliding a voice at a time   track 5  ANALOG SUB BASS, held
 *   track 6  NOISE WIND, its band drifting   track 7  WHEEL SOFT FLUTE, long notes into the delay
 *   track 8  DRUM DEEP KIT, a heartbeat; the drone and the choir breathe with it (SIDECHAIN, soft)
 * Each track's patterns are its variants (demo_var); no project written */
static void demo_note(step_t *s, uint32_t note, uint32_t vel)
{
    s->note[s->n < 4u ? s->n : 3u] = (uint8_t)note;
    s->n = (uint8_t)(s->n < 4u ? s->n + 1u : 4u);
    s->time = ST_NOTE;
    s->vel = (uint8_t)vel;
}
static void demo_hold(track_t *t, uint32_t step, uint32_t len)
{
    uint32_t k;
    for (k = 1; k < len && step + k < NSTEP; k++)
        t->step[step + k].time = ST_TIE;
}
static void demo_hit(track_t *d, uint32_t step, uint32_t lanes, uint32_t vel)
{
    step_t *s = &d->step[step % NSTEP];
    s->hit |= (uint8_t)lanes;
    s->time = ST_NOTE;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
}
/* a track's sound set to engine e's preset pr, as the power-on does (felucca_init) */
static void demo_sound(track_t *t, uint32_t e, uint32_t pr)
{
    set_engine_of(t, e);
    apply_preset_to(t, pr);
    t->engine = t->eng_req;
    eng_mem_claim(t);
}
/* notes n[0..k-1] on track t from step at, held len steps */
static void dm_chord(track_t *t, uint32_t at, uint32_t len, const uint8_t *n, uint32_t k, uint32_t vel)
{
    uint32_t i;
    for (i = 0; i < k; i++)
        demo_note(&t->step[at], n[i], vel);
    demo_hold(t, at, len);
}
/* one bar's chord, held, on track t from bar b (16 steps) */
static void dm_bar(track_t *t, uint32_t b, const uint8_t *n, uint32_t k, uint32_t vel) { dm_chord(t, b * 16u, 16, n, k, vel); }
/* a line of automation on track t: id from v0 to v1 over steps s0 .. s1, a point every 8 steps (CURVE RAMP between) */
static void demo_auto(track_t *t, uint32_t id, uint32_t s0, uint32_t s1, int32_t v0, int32_t v1)
{
    uint32_t s;
    for (s = s0; s <= s1 && s < NSTEP; s += 8u)
        (void)motion_set_event(t, s, id, (int16_t)(v0 + (v1 - v0) * (int32_t)(s - s0) / (int32_t)(s1 > s0 ? s1 - s0 : 1u)));
    motion_set_enabled(t, 1);
}
/* the choir's chords (D dorian, low: E4 at the top) */
static const uint8_t DM_A[4][4] = {{50, 53, 57, 64}, {50, 53, 58, 62}, {48, 53, 57, 64}, {48, 55, 60, 62}};   /* Dm9 Bb/D F/C Csus2 */
static const uint8_t DM_B[4][4] = {{50, 55, 58, 62}, {51, 55, 58, 62}, {50, 53, 57, 60}, {52, 57, 62, 64}};   /* Gm/D Eb Dm7 Asus */
/* the drone's HIGH CUT at each section's start (and the song's end); the cloud's POS and DENS likewise */
static const uint8_t DR_CUT[15] = {50, 58, 64, 70, 74, 78, 82, 84, 70, 64, 80, 88, 76, 62, 50};
static const uint8_t CL_POS[15] = {0, 8, 20, 34, 50, 64, 78, 92, 104, 116, 127, 104, 78, 50, 20};
static const uint8_t CL_DEN[15] = {10, 20, 34, 48, 60, 70, 80, 90, 100, 110, 120, 100, 76, 46, 16};
/* track k's variant v (1..) onto its steps (empty) */
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    const uint8_t (*h)[4] = v == 2u ? DM_B : DM_A;
    switch (k) {
    case 0: {                                                               /* T1 the drone: D and A, the whole section */
        static const uint8_t D[3] = {38, 45, 50};
        dm_chord(t, 0, 64, D, 3, 100);
        if (v == 1u || v == 14u) {                                          /* (out of silence; back into it) */
            demo_auto(t, P_LEVEL, 0, 56, v == 1u ? 0 : 100, v == 1u ? 100 : 0);
            (void)motion_set_event(t, 63, P_LEVEL, v == 1u ? 100 : 0);
        }
        demo_auto(t, P_EQHI, 0, 56, DR_CUT[v - 1u], DR_CUT[v]);           /* its brightness: one curve over the song */
        (void)motion_set_event(t, 63, P_EQHI, DR_CUT[v]);
        demo_auto(t, P_IB, 0, 56, 20 + v * 4u, 24 + v * 4u);                /* the phaser deeper and deeper */
        trkx[0].curve = 1;
        return;
    }
    case 1:                                                                 /* T2 the cloud: scanning */
        for (b = 0; b < 4u; b++) {
            uint8_t c[2] = {(uint8_t)(DM_A[b][1] - 12u), (uint8_t)(DM_A[b][2] - 12u)};
            dm_bar(t, b, c, 2, 80);
            t->step[b * 16u + 15u].flags |= SF_SLIDE;
        }
        demo_auto(t, P_E0, 0, 56, CL_POS[v - 1u], CL_POS[v]);              /* POS: through the sample */
        (void)motion_set_event(t, 63, P_E0, CL_POS[v]);
        demo_auto(t, P_E2, 0, 56, CL_DEN[v - 1u], CL_DEN[v]);              /* DENS: thicker, then thinner */
        (void)motion_set_event(t, 63, P_E2, CL_DEN[v]);
        if (v == 14u)
            demo_auto(t, P_LEVEL, 0, 56, 100, 0);
        trkx[1].curve = 1;
        return;
    case 2: {                                                               /* T3 bowed fifths: D-A, then Bb-F, C-G */
        static const uint8_t F[2][2][2] = {{{38, 45}, {34, 41}}, {{36, 43}, {38, 45}}};
        dm_chord(t, 0, 32, F[v - 1u][0], 2, 96);
        dm_chord(t, 32, 32, F[v - 1u][1], 2, 96);
        t->step[31].flags |= SF_SLIDE;
        demo_auto(t, P_IB, 0, 56, 40, 70);                                  /* (the phaser opening, the section) */
        return;
    }
    case 5:                                                                 /* T6 the wind: its band drifting */
        dm_chord(t, 0, 64, (const uint8_t[]){50}, 1, 90);
        demo_auto(t, P_EQ1F, 0, 56, v == 2u ? 76 : 44, v == 2u ? 44 : v == 3u ? 90 : 72);
        (void)motion_set_event(t, 63, P_EQ1F, v == 2u ? 44 : v == 3u ? 90 : 72);
        if (v == 3u)                                                        /* (rising: its HIGH CUT opening too) */
            demo_auto(t, P_EQHI, 0, 56, 72, 90);
        trkx[5].curve = 1;
        return;
    default:
        break;
    }
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 3:                                                             /* T4 the choir: a chord a bar, sliding */
            dm_bar(t, b, h[b], 4, 80);
            t->step[s0 + 15u].flags |= SF_SLIDE;
            break;
        case 4:                                                             /* T5 the sub: D, held */
            if (!b) {
                demo_note(&t->step[0], 38, 100);
                demo_hold(t, 0, 64);
            }
            break;
        case 6: {                                                           /* T7 the flute: long notes, low, sliding */
            static const uint8_t L[2][4] = {{57, 55, 53, 52}, {62, 60, 57, 55}};
            demo_note(&t->step[s0], L[v - 1u][b], 74);
            demo_hold(t, s0, 12);
            t->step[s0 + 11u].flags |= SF_SLIDE;
            break;
        }
        default:                                                            /* T8 the heartbeat: lub-dub */
            demo_hit(t, s0, 1u << 0, 84);
            demo_hit(t, s0 + 3u, 1u << 0, 56);
            if (v == 2u) {
                demo_hit(t, s0 + 8u, 1u << 0, 74);
                demo_hit(t, s0 + 11u, 1u << 0, 50);
            }
            break;
        }
    }
}
static void felucca_demo(void)
{
    /* the song: each section's variant per track (0: silent there); the drone and the cloud one a section */
    static const uint8_t SEC[14][8] = {
        {1, 0, 0, 0, 0, 1, 0, 0},             /* out of silence: the drone, the wind */
        {2, 2, 0, 0, 0, 1, 0, 0},             /* the cloud */
        {3, 3, 1, 0, 0, 2, 0, 0},             /* the bowed fifths */
        {4, 4, 1, 1, 0, 1, 0, 0},             /* the choir */
        {5, 5, 2, 1, 1, 0, 1, 0},             /* the sub, the flute */
        {6, 6, 1, 1, 1, 2, 1, 1},             /* the heartbeat */
        {7, 7, 2, 2, 1, 1, 2, 2},             /* the other chords */
        {8, 8, 1, 1, 1, 2, 1, 2},
        {9, 9, 0, 0, 0, 2, 1, 0},             /* the break: the drone, the cloud, the wind, the flute */
        {10, 10, 1, 1, 0, 3, 0, 0},           /* the wind rising */
        {11, 11, 2, 2, 1, 1, 2, 2},           /* the fullest */
        {12, 12, 1, 1, 1, 2, 1, 1},
        {13, 13, 1, 1, 0, 1, 0, 0},           /* closing */
        {14, 14, 0, 0, 0, 2, 0, 0},           /* the drone and the cloud into silence */
    };
    static const uint8_t NV[8] = {14, 14, 2, 2, 1, 3, 2, 2};   /* each track's variants */
    uint32_t i, k, v;
    track_t *t;
    song.g[G_BPM] = 66;
    song.g[G_RTYPE] = 2;                      /* HALL, vast */
    song.g[G_RSIZE] = 124;
    song.g[G_RDAMP] = 90;                     /* (dark) */
    song.g[G_DFDBK] = 80;
    song.g[G_DTIME] = 10;                     /* 1/8D */
    song.g[G_DCOLOR] = 30;
    song.g[G_CRATE] = 8;
    song.g[G_CDEPTH] = 100;
    song.g[G_MCOMP] = 60;                     /* the MASTER glued */
    song.g[G_MREL] = 80;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;                 /* 1/16 */
    }
    t = &trk[0];
    demo_sound(t, 0, 11);                     /* ANALOG STRINGS: the drone, spread, phased */
    t->p[P_ATK] = 90; t->p[P_REL] = 110; t->p[P_SUS] = 127; t->p[P_SPRD] = 120;
    t->p[P_ITYPE] = 6; t->p[P_IA] = 6; t->p[P_IB] = 24; t->p[P_IC] = 60; t->p[P_IMIX] = 80;   /* PHASR, slow */
    t->p[P_REV] = 70; t->p[P_CHOR] = 60;
    t = &trk[1];
    demo_sound(t, 8, 0);                      /* GRAIN CLOUD PAD */
    t->p[P_ATK] = 100; t->p[P_REL] = 110; t->p[P_REV] = 100; t->p[P_DLY] = 30;
    t = &trk[2];
    demo_sound(t, 2, 2);                      /* PHASE STRING: bowed (PHYS and GRAIN: tracks 1, 2 only) */
    t->p[P_ATK] = 70; t->p[P_REL] = 110; t->p[P_SUS] = 127; t->p[P_SPRD] = 90;
    t->p[P_VOICE] = V_POLY; t->p[P_GLIDE] = 80;
    t->p[P_ITYPE] = 6; t->p[P_IA] = 4; t->p[P_IB] = 40; t->p[P_IC] = 50; t->p[P_IMIX] = 60;
    t->p[P_REV] = 80;
    t = &trk[3];
    demo_sound(t, 5, 0);                      /* VOICE CHOIR AAH: low */
    t->p[P_ATK] = 90; t->p[P_REL] = 110; t->p[P_SPRD] = 100; t->p[P_GLIDE] = 70;
    t->p[P_REV] = 100; t->p[P_CHOR] = 50;
    t = &trk[4];
    demo_sound(t, 0, 7);                      /* ANALOG SUB BASS: held */
    t->p[P_VOICE] = V_MONO; t->p[P_ATK] = 60; t->p[P_SUS] = 127; t->p[P_REL] = 90;
    t->p[P_REV] = 0; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[5];
    demo_sound(t, 11, 0);                     /* NOISE WIND */
    t->p[P_ATK] = 110; t->p[P_REL] = 110; t->p[P_SUS] = 127; t->p[P_SPRD] = 127;
    t->p[P_REV] = 90; t->p[P_DLY] = 0; t->p[P_PAN] = 10;
    t = &trk[6];
    demo_sound(t, 7, 3);                      /* WHEEL SOFT FLUTE: into the delay */
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 90; t->p[P_ATK] = 70; t->p[P_REL] = 100;
    t->p[P_REV] = 100; t->p[P_DLY] = 80; t->p[P_PAN] = -20;
    t = &trk[7];
    demo_sound(t, ENGI_DRUM, 1);              /* DEEP KIT: the heartbeat */
    t->p[P_E0 + 1] = 30; t->p[P_E0 + 2] = 20;
    t->p[P_REV] = 30; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    {                                         /* the bands (EQ: LOW CUT, HIGH CUT, a bell), the level, the COMPRESSOR */
        static const int8_t EQ[8][6] = {
            {14, 50, 40, -6, 110, 100},       /* drone: from 60 Hz, its HIGH CUT automated */
            {40, 88, 60, -4, 118, 80},        /* cloud: 230 Hz .. 3 kHz */
            {20, 84, 50, 3, 92, 100},         /* bowed: .. 2.6 kHz */
            {44, 86, 66, 3, 110, 100},        /* choir: .. 2.8 kHz */
            {0, 34, 18, 6, 80, 0},            /* sub: under 130 Hz */
            {36, 72, 60, 30, 96, 60},         /* wind: 200 Hz .. 1.6 kHz, its band moving */
            {40, 88, 66, 3, 100, 90},         /* flute */
            {0, 70, 22, 6, 96, 0},            /* heartbeat: dull */
        };
        for (i = 0; i < NTRK; i++) {
            trk[i].p[P_EQLO] = EQ[i][0];
            trk[i].p[P_EQHI] = EQ[i][1];
            trk[i].p[P_EQ1F] = EQ[i][2];
            trk[i].p[P_EQ1G] = EQ[i][3];
            trk[i].p[P_LEVEL] = EQ[i][4];
            trkx[i].rsv0 = (uint8_t)EQ[i][5];
            trkx[i].rsv1 = 70;
        }
    }
    for (i = 0; i < 4u; i += 3u) {            /* the drone and the choir breathe with the heartbeat */
        trk[i].p[P_DKSRC] = 8;
        trk[i].p[P_DKAMT] = 40;
        trk[i].p[P_DKREL] = 90;
    }
    for (k = 0; k < 8u; k++)                  /* each track's variants: 1 on it, the others new patterns */
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);            /* the SONG, round (GLO > SONG; PLAY plays it) */
    chain_config.count = 14;
    for (i = 0; i < 14u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
    for (i = 0; i < NTRK; i++)
        pat_sig[i] = steps_sig(&trk[i]);
}
#endif

/* power-on: the parts with their default sounds (TRK_DEF); the sequencers empty */
static void felucca_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    mst_defaults();                           /* (Dah: the MASTER's chain, empty) */
    chain_from_row = 0;                       /* (Dah: SONG's GRID at the top) */
    chain_from_u = 0;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    mix_solo = 0;                             /* (Dah: MIXER's SOLO, none) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    ps_reset();                               /* (HA HA HE HU: each track on its pattern 1, no other) */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        trkx_defaults(&trkx[i]);              /* (HA HA HE HU: LFO DRAW's points, CURVE STEP) */
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        eng_mem_claim(t);                     /* (zeros already: no note can come before) */
        track_defaults_steps(t);              /* (a sound load never touches them) */
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);            /* a default pattern, not the user's */
        pat_last[i] = TRK_DEF[i][2];
    }
#if FELUCCA_DEMO
    felucca_demo();
#endif
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;                   /* (the pot's level replaces it before the audio starts: master_boot) */
    ui.home = 1;
    ui.force = 1;
}

/* The power-on LED sweep (1.1, hal/fm1_led_anim.h: a soft light over the keys left to right, then the buttons swell
 * and settle on the glow; ~0.70 s), started before the scan and run by it: nothing waits for it, the splash's 430 ms
 * and the first ~0.27 s of the UI go by under it (the UI's LEDs from its end, or at once on a key or a button down).
 * Every LEDS setting: it is no idle glow; with DIM HI / DIM LO it ends on that glow (and is drawn over that glow
 * level), with OFF / INV dark. Not with MENU > ANIM OFF (no motion: the LEDs as the UI has them at once), not after a crash (a crash record newer than the last boot saw: the crash screen's
 * reset), a hang (a WDT reset) or a failed boot (bootguard: the last one died within its first 30 s): straight to the
 * UI. The UBOOT paths never get here. */
static uint32_t crash_seen __attribute__((section(".noinit")));   /* fm1_crash.count at the last boot */
static uint8_t boot_clean;                    /* no crash, hang (WDT) or failed boot before this one (boot_leds):
                                               * the sweep runs and the autosave is restored (project.c autosave_boot) */
static void boot_leds(void)
{
    uint32_t crashed = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != crash_seen
#if FELUCCA_CPU2
                       && fm1_crash.core != 1u    /* (the second core's own fault left core 0 playing) */
#endif
                       ;
    crash_seen = fm1_crash.count;
    boot_clean = !crashed && !bootguard.failed && !(fm1_boot.p3_rst & 4u);
    fm1_led_dim_level(settings_leds == LEDS_DIM_LO);
    if (boot_clean && !(ui_prefs & PREF_ANIM_OFF))
        fm1_led_anim_start(settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO);
}

static void fm1_main(void)
{
    uint32_t ms;
#if FELUCCA_CPU2
    uint8_t c1_told = 0;                                /* 2ND CORE MISREAD shown (and ON cleared) */
#endif
    persist_boot();
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    settings_init();
    usb_serial_apply();                                 /* (#67: the saved USB SERIAL before usb_start) */
    lcd_init();
    draw_splash();                                      /* ui_draw.c (1.1.5) */
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    master_boot();
    kb_boot_hold = 1;                         /* #137: no notes from the keys before the UI (seq.c keyboard_block) */
#if FELUCCA_CPU2
    if (!bootguard.failed && (ui_prefs2 & (PREF_CPU2_ON >> 16))) {
        /* the second core (hal/fm1_cpu1.h; fx.c mix_block hands it parts), at power-on, before the audio and
         * the timers (started later it never reaches its entry). It starts in the chip's ROM: the PC limits open
         * meanwhile. MENU > 2ND CORE OFF (the default), a crash in the last boot's first 30 s, or no answer: it is
         * never started, one core, as 1.6 */
        fm1_guard_pc_open();
        fm1_cpu1_start();
        fm1_guard_enable(FM1_GUARD_PC);
    }
#endif
    audio_init();
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    boot_leds();
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    autosave_boot(boot_clean);                /* (1.2) the last session's music, under the splash (HA HA HE HU: else the
                                               * drone, felucca_demo; the PROJECT slots stay empty) */
    for (ms = 0; ms < FW_SPLASH_MS; ms += 10u) {   /* the splash; the pot followed (MIDI IN plays under it) */
        fm1_delay_ms(10);
        fm1_wdt_feed();                       /* (a longer splash than Felucca's: the watchdog's 8 s stay far) */
        master_poll();
    }
    kb_boot_hold = 0;
    lcd_fill(0, 0, 240, 240, T_BG);
    scrn.idle = fm1_ms;                        /* (MENU > SCREEN OFF: from here) */

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
#if FELUCCA_CPU2
        if (*(volatile uint8_t *)&fm1_c1_gave_up && !c1_told) {   /* (the ISR's) */
            c1_told = 1;                                /* (once: ON set again afterwards stays, for the next power-on) */
            if (ui_prefs2 & (PREF_CPU2_ON >> 16)) {
                ui_prefs2 &= (uint8_t)~(PREF_CPU2_ON >> 16);   /* core 1 misread or stopped answering on this FM-1: one
                                                         * core from now on (MENU > 2ND CORE ON tries again) */
                settings_later();
            }
            ui_message("2ND CORE MISREAD: OFF");     /* (ui.msg: 23 characters at most) */
        }
#endif
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        master_poll();
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s over the whole screen (the menu and the dialogs too: ui_draw), letting
             * go cancels it */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (ui.uboot) {
                    ui.uboot = 0;
                    ui.force = 1;                       /* what the countdown covered */
                    ui_message("UPDATE CANCELLED");
                }
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != ui.uboot) {
                    ui.uboot = (uint8_t)left;
                    ui.force = 1;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                scr_wake_now();
                lcd_fill(0, 0, 240, 240, T_BG);
                draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            scr_wake_now();                             /* (its progress on the screen) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            scr_wake_now();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UBOOT (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = 1;
        ui_input();
        settings_poll();                              /* queued settings save: only while stopped */
        autosave_poll();                              /* (1.2) the music, stopped and idle: project.c */
        chain_poll();                                 /* (HA HA HE HU) a SONG's next section ready: pstore.c */
        felucca_dbg.stage = 2;
        ui_leds();
        ui_draw();
        felucca_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
#if FELUCCA_CPU2
    fm1_cpu1_halt();                           /* (after Jangada, X0X): a second core still running from before
                                                * this reset (a watchdog, a fault, another firmware) would read the RAM
                                                * cleared below */
#endif
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
#if FELUCCA_CPU2
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;                                /* the second core's waiting loop (hal/fm1_cpu1.h) */
#endif
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
