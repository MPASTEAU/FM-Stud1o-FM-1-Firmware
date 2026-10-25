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
/* Dah's power-on sound (FELUCCA_DEMO): epic and dark, D minor at 84 BPM, eight tracks, two patterns each (four bars,
 * 64 x 1/16) and a SONG (1 twice, 2 twice); no project written. Tension and its resolution over a D pedal:
 *   pattern 1, the build   Dm9  Eb/D (the minor second on the pedal)  Bbmaj7/D  A7b9; the filters of the pad, the strings
 *                          and the lead open together over it (one curve, RAMP), the toms march, a roll closes it
 *   pattern 2, the charge  Dm9  Gm9/D  Ebmaj7  Asus4: the filters open, the kick on every beat, the toms gallop, the snare
 *                          on 2 and 4 with its ghosts, open hats on the off-beats, a fill down the toms at the end
 *   track 1  PHYS TOMS (low, pitched: the war drums)   track 2  ANALOG SOFT PAD (the big pad)
 *   track 3  ANALOG PLUCK, its ARP (UPDN 1/16 over two octaves) running over the chords
 *   track 4  ANALOG SAW LEAD (MONO, sliding)   track 5  ANALOG SUB BASS (sliding)   track 6  DRUM DEEP KIT (the kick)
 *   track 7  DRUM KIT (snare, hats)   track 8  VOICE CHOIR AAH
 * The pad, strings, sub and choir duck on the kick (DUCK T6, deep: they pump) */
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
/* Sound design, slow and long (84 BPM in 1/16, over an A pedal): a huge detuned drone (ANALOG, two oscillators, spread,
 * a phaser) whose filter opens and closes over the whole song, a grain cloud scanning its sample (its POS, DENS, RAND
 * moving), a flanged pad whose voices slide a semitone at a time, a vowel lead (GROWL's FM and cutoff opening), a noise
 * that drifts, turns metal, rises; the sub (held) and the kick land together (section 6). Each track's patterns are its
 * variants (demo_var: the drone and the cloud one a section, their curves going on from one into the next), the SONG's
 * fourteen sections name one per track: 2 min 40 for a few kilobytes */
static const uint8_t DM_A[4][4] = {{57, 60, 64, 71}, {57, 60, 65, 71}, {55, 60, 65, 71}, {55, 59, 64, 71}};   /* Am9 Fmaj7#11/A Gsus G6 */
static const uint8_t DM_B[4][4] = {{57, 62, 65, 72}, {57, 62, 64, 71}, {56, 60, 64, 71}, {57, 60, 64, 71}};   /* Dm7/A Asus E/G# Am9 */
/* the drone's cutoff at each section's start (and the song's end); the cloud's POS and DENS likewise */
static const uint8_t DR_CUT[15] = {14, 24, 40, 52, 64, 76, 84, 90, 72, 58, 88, 96, 70, 34, 14};
static const uint8_t CL_POS[15] = {0, 10, 24, 40, 56, 70, 82, 96, 108, 118, 127, 100, 70, 40, 0};
static const uint8_t CL_DEN[15] = {20, 30, 44, 56, 70, 76, 84, 96, 110, 120, 127, 104, 80, 50, 20};
/* track k's variant v (1..) onto its steps (empty) */
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    const uint8_t (*h)[4] = k == 2u && v == 2u ? DM_B : DM_A;
    switch (k) {
    case 0: {                                                               /* T1 the drone: A and E, the whole section */
        static const uint8_t D[2] = {33, 40};
        dm_chord(t, 0, 64, D, 2, 100);
        demo_auto(t, P_E4, 0, 56, DR_CUT[v - 1u], DR_CUT[v]);              /* its filter: one curve over the song */
        (void)motion_set_event(t, 63, P_E4, DR_CUT[v]);
        demo_auto(t, P_IB, 0, 56, 40 + v * 5u, 50 + v * 5u);                /* the phaser deeper and deeper */
        trkx[0].curve = 1;
        return;
    }
    case 1:                                                                 /* T2 the grain cloud: scanning */
        for (b = 0; b < 4u; b++) {
            static const uint8_t D[2] = {0, 0};
            uint8_t c[2] = {DM_A[b][2], DM_A[b][3]};
            (void)D;
            dm_bar(t, b, c, 2, 80);
            t->step[b * 16u + 15u].flags |= SF_SLIDE;
        }
        demo_auto(t, P_E0, 0, 56, CL_POS[v - 1u], CL_POS[v]);              /* POS: through the sample */
        (void)motion_set_event(t, 63, P_E0, CL_POS[v]);
        demo_auto(t, P_E2, 0, 56, CL_DEN[v - 1u], CL_DEN[v]);              /* DENS: thicker */
        (void)motion_set_event(t, 63, P_E2, CL_DEN[v]);
        trkx[1].curve = 1;
        return;
    default:
        break;
    }
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 2:                                                             /* T3 the flanged pad, sliding */
            dm_bar(t, b, h[b], 4, 84);
            t->step[s0 + 15u].flags |= SF_SLIDE;
            break;
        case 3: {                                                           /* T4 the vowel lead: long, sliding */
            static const uint8_t L[2][4][2] = {{{64, 16}, {62, 16}, {60, 16}, {59, 16}},
                                               {{69, 16}, {67, 8}, {64, 24}, {0, 0}}};
            if (!L[v - 1u][b][1])
                break;
            demo_note(&t->step[s0], L[v - 1u][b][0], 96);
            demo_hold(t, s0, L[v - 1u][b][1]);
            t->step[(s0 + L[v - 1u][b][1] - 1u) % 64u].flags |= SF_SLIDE;
            break;
        }
        case 4:                                                             /* T5 the sub: A, held */
            if (!b) {
                demo_note(&t->step[0], 33, 110);
                demo_hold(t, 0, 64);
            }
            break;
        case 5:                                                             /* T6 the kick: four on the floor */
            for (i = 0; i < 16u; i += 4u)
                demo_hit(t, s0 + i, 1u << 0, i ? 100 : 114);
            if (v == 2u && b == 3u)
                demo_hit(t, s0 + 14u, 1u << 0, 76);
            break;
        case 6:                                                             /* T7 the kit */
            if (v <= 2u) {
                demo_hit(t, s0 + 4u, 1u << 2, 92);                          /* the clap on 2 and 4 */
                demo_hit(t, s0 + 12u, 1u << 2, 96);
                for (i = 2; i < 16u; i += 4u)
                    demo_hit(t, s0 + i, 1u << 3, 44);                       /* a hat off the beat */
                if (v == 2u && b == 3u)
                    for (i = 8; i < 16u; i++)
                        demo_hit(t, s0 + i, 1u << 5, 112 - (i - 8u) * 5u);  /* a tom fill down */
            } else if (v == 3u) {
                for (i = 0; i < 16u; i += 2u)                               /* the build: toms, louder */
                    demo_hit(t, s0 + i, 1u << 5, 46 + b * 16u + i * 2u);
            } else if (b >= 2u) {                                           /* into the boom, toms then snare */
                for (i = 0; i < 16u; i += b == 3u && i >= 8u ? 1u : 2u)
                    demo_hit(t, s0 + i, b == 3u && i >= 8u ? 1u << 1 : 1u << 5, 50 + (b - 2u) * 30u + i * 3u);
            }
            break;
        default:                                                            /* T8 the noise: held */
            if (!b) {
                demo_note(&t->step[0], 57, 90);
                demo_hold(t, 0, 64);
            }
            break;
        }
    }
    if (k == 3u) {                                                          /* the vowel: FM and cutoff opening */
        demo_auto(t, P_E2, 0, 56, 10, 96);
        demo_auto(t, P_E4, 0, 56, 40, 100);
        trkx[3].curve = 1;
    } else if (k == 7u) {                                                   /* the noise: wind, metal, a rise */
        if (v == 1u) {
            demo_auto(t, P_E2, 0, 56, 30, 60);
            (void)motion_set_event(t, 0, P_LEVEL, 40);
        } else if (v == 2u) {
            (void)motion_set_event(t, 0, P_E0, 3);                          /* METAL */
            demo_auto(t, P_E2, 0, 56, 40, 100);
            demo_auto(t, P_E7, 0, 56, 0, 70);                               /* crushed, more and more */
            (void)motion_set_event(t, 0, P_LEVEL, 56);
        } else {
            demo_auto(t, P_E2, 0, 56, 30, 127);                             /* the rise */
            demo_auto(t, P_EQHI, 0, 56, 40, 118);
            demo_auto(t, P_LEVEL, 0, 56, 20, 110);
            (void)motion_set_event(t, 63, P_LEVEL, 0);
        }
        trkx[7].curve = 1;
    }
}
static void felucca_demo(void)
{
    /* the song: each section's variant per track (0: silent there); the drone and the cloud one a section */
    static const uint8_t SEC[14][8] = {
        {1, 0, 0, 0, 0, 0, 0, 1},             /* the drone alone, wind */
        {2, 2, 0, 0, 0, 0, 0, 1},             /* the cloud */
        {3, 3, 1, 0, 0, 0, 0, 2},             /* the pad; the noise turns metal */
        {4, 4, 1, 0, 0, 0, 0, 2},
        {5, 5, 1, 1, 0, 0, 4, 3},             /* the vowel; toms, a roll, the rise */
        {6, 6, 1, 0, 1, 1, 1, 0},             /* the boom: the sub with the kick */
        {7, 7, 1, 1, 1, 2, 2, 2},
        {8, 8, 2, 2, 1, 1, 1, 0},             /* the other chords */
        {9, 9, 1, 0, 0, 0, 0, 2},             /* the break */
        {10, 10, 1, 0, 0, 0, 3, 3},           /* the build on toms, the rise */
        {11, 11, 1, 2, 1, 2, 2, 2},           /* the climax */
        {12, 12, 2, 1, 1, 1, 2, 0},
        {13, 13, 1, 0, 0, 0, 0, 1},           /* the outro, closing */
        {14, 14, 0, 0, 0, 0, 0, 1},           /* the drone alone again */
    };
    static const uint8_t NV[8] = {14, 14, 2, 2, 1, 2, 4, 3};   /* each track's variants */
    uint32_t i, k, v;
    track_t *t;
    song.g[G_BPM] = 84;
    song.g[G_RTYPE] = 2;                      /* HALL, vast */
    song.g[G_RSIZE] = 124;
    song.g[G_RDAMP] = 70;
    song.g[G_DFDBK] = 70;
    song.g[G_DTIME] = 10;                     /* 1/8D */
    song.g[G_CRATE] = 12;
    song.g[G_CDEPTH] = 100;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;                 /* 1/16 */
    }
    t = &trk[0];
    demo_sound(t, 0, 1);                      /* ANALOG: the drone, two saws detuned wide, spread, phased */
    t->p[P_E0] = 2; t->p[P_E0 + 1] = 70; t->p[P_E0 + 2] = 90; t->p[P_E0 + 3] = 10; t->p[P_E0 + 5] = 64; t->p[P_E0 + 6] = 40;
    t->p[P_ATK] = 110; t->p[P_REL] = 110; t->p[P_SUS] = 127;
    t->p[P_SPRD] = 127; t->p[P_DETUNE] = 90;
    t->p[P_LRATE] = 18; t->p[P_LD_FLT] = 18;   /* (a slow breath on the filter) */
    t->p[P_ITYPE] = 6; t->p[P_IA] = 8; t->p[P_IB] = 60; t->p[P_IC] = 90; t->p[P_IMIX] = 100;   /* PHASR, slow, deep */
    t->p[P_LEVEL] = 96; t->p[P_REV] = 70; t->p[P_CHOR] = 40;
    t = &trk[1];
    demo_sound(t, 8, 0);                      /* GRAIN CLOUD PAD: the cloud */
    t->p[P_E0 + 1] = 100; t->p[P_E0 + 4] = 110; t->p[P_E0 + 5] = 50;
    t->p[P_ATK] = 100; t->p[P_REL] = 110; t->p[P_LEVEL] = 82; t->p[P_REV] = 100; t->p[P_DLY] = 30;
    t = &trk[2];
    demo_sound(t, 0, 3);                      /* ANALOG PWM STR: the pad, flanged */
    t->p[P_ATK] = 80; t->p[P_REL] = 100; t->p[P_SPRD] = 100;
    t->p[P_LRATE] = 30; t->p[P_LD_SHP] = 40;   /* (the pulse width breathing) */
    t->p[P_ITYPE] = 7; t->p[P_IA] = 6; t->p[P_IB] = 90; t->p[P_IC] = 70; t->p[P_IMIX] = 70;   /* FLANG, slow */
    t->p[P_LEVEL] = 82; t->p[P_REV] = 80; t->p[P_CHOR] = 50;
    t = &trk[3];
    demo_sound(t, ENGI_GROWL, 2);             /* GROWL VOWEL: the lead, talking */
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 90;
    t->p[P_ATK] = 60; t->p[P_REL] = 90; t->p[P_LEVEL] = 70; t->p[P_REV] = 100; t->p[P_DLY] = 60;
    t = &trk[4];
    demo_sound(t, 0, 7);                      /* ANALOG SUB BASS: held */
    t->p[P_VOICE] = V_MONO; t->p[P_ATK] = 20; t->p[P_SUS] = 127;
    t->p[P_LEVEL] = 104; t->p[P_REV] = 0; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[5];
    demo_sound(t, ENGI_DRUM, 1);              /* DEEP KIT: the kick */
    t->p[P_E0 + 1] = 40; t->p[P_E0 + 2] = 30;
    t->p[P_LEVEL] = 102; t->p[P_REV] = 8; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[6];
    demo_sound(t, ENGI_DRUM, 0);              /* DRUM KIT: clap, hat, toms */
    t->p[P_LEVEL] = 76; t->p[P_REV] = 80; t->p[P_DLY] = 20; t->p[P_CHOR] = 0;
    t = &trk[7];
    demo_sound(t, 11, 0);                     /* NOISE: wind, metal, a rise */
    t->p[P_E0 + 6] = 80;                      /* (DRFT) */
    t->p[P_ITYPE] = 3; t->p[P_IA] = 30; t->p[P_IMIX] = 50;   /* FOLD: its grit */
    t->p[P_ATK] = 90; t->p[P_LEVEL] = 50; t->p[P_REV] = 90; t->p[P_DLY] = 40;
    {                                         /* the bands (EQ: LOW CUT, HIGH CUT, a bell): each one clear */
        static const int8_t EQ[8][4] = {
            {14, 118, 40, -10},               /* drone: from 60 Hz, its 230 Hz down */
            {56, 118, 100, 8},                /* cloud: 500 Hz up, air */
            {47, 106, 60, -12},               /* pad: 300 Hz .. 6 kHz */
            {52, 112, 84, 6},                 /* vowel */
            {0, 34, 18, 8},                   /* sub: under 130 Hz */
            {0, 96, 22, 6},                   /* kick */
            {40, 118, 104, 8},                /* kit */
            {60, 118, 110, 6},                /* noise */
        };
        for (i = 0; i < NTRK; i++) {
            trk[i].p[P_EQLO] = EQ[i][0];
            trk[i].p[P_EQHI] = EQ[i][1];
            trk[i].p[P_EQ1F] = EQ[i][2];
            trk[i].p[P_EQ1G] = EQ[i][3];
        }
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
