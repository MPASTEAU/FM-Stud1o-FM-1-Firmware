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
/* pattern p (1 or 2) of the demo onto the tracks' steps (all empty) */
static void demo_pattern(uint32_t p)
{
    static const uint8_t PAD1[4][4] = {{50, 53, 57, 64}, {50, 51, 55, 58}, {50, 53, 57, 58}, {49, 52, 55, 58}};
    static const uint8_t PAD2[4][4] = {{50, 53, 57, 64}, {50, 55, 57, 58}, {51, 55, 58, 62}, {50, 52, 57, 64}};
    static const uint8_t SUB[2][4] = {{38, 38, 38, 33}, {38, 38, 39, 33}};
    static const struct { uint8_t step, note, len; } LEAD[2][10] = {
        {{0, 74, 6}, {6, 75, 2}, {8, 74, 8}, {16, 75, 6}, {22, 77, 2}, {24, 75, 8}, {32, 74, 8}, {40, 70, 8},
         {48, 73, 8}, {56, 76, 8}},
        {{0, 77, 6}, {6, 76, 2}, {8, 74, 8}, {16, 79, 8}, {24, 77, 8}, {32, 75, 12}, {44, 74, 4}, {48, 76, 6},
         {54, 74, 2}, {56, 73, 8}},
    };
    /* the toms (PHYS TOMS: G1 low, C2, D2 high): pattern 1 a march, pattern 2 a gallop; 0 none */
    static const uint8_t TOM1[16] = {38, 0, 0, 38, 0, 0, 43, 0, 38, 0, 0, 43, 0, 0, 45, 0};
    static const uint8_t TOM2[16] = {38, 0, 38, 43, 0, 43, 38, 0, 38, 0, 38, 43, 0, 45, 43, 45};
    const uint8_t (*pad)[4] = p == 1u ? PAD1 : PAD2;
    uint32_t b, i;
    track_t *t;
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        dm_bar(&trk[1], b, pad[b], 4, 74);                                 /* the pad */
        {
            uint8_t up[4];
            for (i = 0; i < 4u; i++) up[i] = (uint8_t)(pad[b][i] + 12u);
            dm_bar(&trk[2], b, up, 3, 80);                                  /* the arp's chord, an octave up */
        }
        demo_note(&trk[4].step[s0], SUB[p - 1u][b], 104);                   /* the sub: held, sliding to the next */
        demo_hold(&trk[4], s0, 16);
        if (b < 3u) trk[4].step[s0 + 15u].flags |= SF_SLIDE;
        dm_bar(&trk[7], b, pad[b] + 1, 3, 70);                              /* the choir, held */
        if (b >= 1u) {                                                      /* the dissonance sliding into what */
            trk[1].step[s0 + 15u].flags |= SF_SLIDE;                        /* resolves it: the pad's and the choir's */
            trk[7].step[s0 + 15u].flags |= SF_SLIDE;                        /* voices glide over (seq.c seq_chord_slide) */
        }
        t = &trk[0];                                                        /* the toms */
        for (i = 0; i < 16u; i++) {
            uint8_t n = (p == 1u ? TOM1 : TOM2)[i];
            if (b == 3u && i >= 8u)                                         /* the last half bar: a roll (1: up; 2: down) */
                n = p == 1u ? (uint8_t)(i < 12u ? 38 : 43 + (i & 1u) * 2u) : (uint8_t)(i < 12u ? 45 : 43 - (i & 1u) * 5u);
            if (n)
                demo_note(&t->step[s0 + i], n, (uint32_t)(b == 3u && i >= 8u ? 70 + (i - 8u) * 7u : i % 4u ? 78 : 112));
        }
        t = &trk[5];                                                        /* the kick */
        if (p == 1u) {
            demo_hit(t, s0, 1u << 0, 124);
            demo_hit(t, s0 + 6u, 1u << 0, 96);
            demo_hit(t, s0 + 10u, 1u << 0, 108);
        } else {
            for (i = 0; i < 16u; i += 4u)
                demo_hit(t, s0 + i, 1u << 0, i ? 108 : 126);
            demo_hit(t, s0 + 7u, 1u << 0, 84);                              /* (the push before 3) */
        }
        t = &trk[6];                                                        /* snare, hats */
        if (p == 1u) {
            demo_hit(t, s0 + 8u, 1u << 1, 96);
            for (i = 0; i < 16u; i += 2u)
                demo_hit(t, s0 + i, 1u << 3, i % 4u ? 40 : 56);             /* (lane 3: HAT CL, quiet eighths) */
            if (b == 3u)
                for (i = 12; i < 16u; i++)
                    demo_hit(t, s0 + i, 1u << 1, 60 + (i - 12u) * 16u);     /* the roll into the next bar */
        } else {
            demo_hit(t, s0 + 4u, 1u << 1, 104);
            demo_hit(t, s0 + 12u, 1u << 1, 110);
            demo_hit(t, s0 + 7u, 1u << 1, 36);                              /* ghosts */
            demo_hit(t, s0 + 14u, 1u << 1, 40);
            for (i = 2; i < 16u; i += 4u)
                demo_hit(t, s0 + i, 1u << 4, 64);                           /* (lane 4: HAT OP, the off-beats) */
            if (b == 0u)
                demo_hit(t, s0, 1u << 7, 96);                               /* (lane 7: the cymbal on the downbeat) */
        }
    }
    for (i = 0; i < 10u; i++) {                                             /* the lead, sliding note to note */
        demo_note(&trk[3].step[LEAD[p - 1u][i].step], LEAD[p - 1u][i].note, 96);
        demo_hold(&trk[3], LEAD[p - 1u][i].step, LEAD[p - 1u][i].len);
        if (i + 1u < 10u && LEAD[p - 1u][i + 1u].step == LEAD[p - 1u][i].step + LEAD[p - 1u][i].len)
            trk[3].step[LEAD[p - 1u][i].step + LEAD[p - 1u][i].len - 1u].flags |= SF_SLIDE;
    }
    for (i = 1; i <= 3u; i++)                                               /* the filters, one curve for all three: */
        if (p == 1u) {                                                      /* pattern 1 opens them all the way, */
            demo_auto(&trk[i], P_E4, 0, 56, 22, 110);
            (void)motion_set_event(&trk[i], 63, P_E4, 118);
        } else {                                                            /* 2 holds them open, closes at its end */
            (void)motion_set_event(&trk[i], 0, P_E4, 118);
            (void)motion_set_event(&trk[i], 52, P_E4, 118);
            (void)motion_set_event(&trk[i], 58, P_E4, 70);
            (void)motion_set_event(&trk[i], 63, P_E4, 24);
            motion_set_enabled(&trk[i], 1);
        }
    if (p == 1u) {                                                          /* the ARP: its GATE opening with the filter, */
        demo_auto(&trk[2], P_AGATE, 0, 56, 18, 100);
        (void)motion_set_event(&trk[2], 0, P_AOCT, 2);
        (void)motion_set_event(&trk[2], 48, P_AOCT, 3);                     /* three octaves for the last bar */
    } else {                                                                /* 1/16, then 1/32 for the last bar, four */
        (void)motion_set_event(&trk[2], 0, P_ARATE, 1);                     /* octaves on the climb */
        (void)motion_set_event(&trk[2], 0, P_AOCT, 2);
        (void)motion_set_event(&trk[2], 32, P_AOCT, 3);
        (void)motion_set_event(&trk[2], 48, P_ARATE, 2);
        (void)motion_set_event(&trk[2], 48, P_AOCT, 4);
        demo_auto(&trk[2], P_AGATE, 0, 56, 100, 40);
    }
    if (p == 1u) {
        demo_auto(&trk[2], P_REV, 0, 56, 30, 110);                          /* the arp's reverb swelling */
        demo_auto(&trk[3], P_DLY, 0, 8, 12, 12);                            /* the lead's delay thrown at the ends */
        (void)motion_set_event(&trk[3], 24, P_DLY, 90);
        (void)motion_set_event(&trk[3], 32, P_DLY, 12);
        (void)motion_set_event(&trk[3], 56, P_DLY, 100);
    } else {
        demo_auto(&trk[2], P_REV, 0, 56, 110, 80);
        demo_auto(&trk[3], P_DLY, 0, 56, 30, 30);
        (void)motion_set_event(&trk[3], 32, P_DLY, 100);
        (void)motion_set_event(&trk[3], 44, P_DLY, 30);
    }
}
static void felucca_demo(void)
{
    uint32_t i, k;
    track_t *t;
    song.g[G_BPM] = 110;                      /* (steps of 1/8: the half-time pulse) */
    song.g[G_RTYPE] = 2;                      /* HALL, big and dark */
    song.g[G_RSIZE] = 122;
    song.g[G_RDAMP] = 96;
    song.g[G_DFDBK] = 62;
    song.g[G_DTIME] = 10;                     /* 1/8D */
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 1;                 /* 1/8 */
    }
    t = &trk[0];
    demo_sound(t, ENGI_PHYS, 6);              /* PHYS TOMS: the war drums */
    t->p[P_LEVEL] = 100; t->p[P_REV] = 70; t->p[P_DLY] = 0;
    t = &trk[1];
    demo_sound(t, 0, 1);                      /* ANALOG SOFT PAD */
    t->p[P_ATK] = 80; t->p[P_REL] = 100; t->p[P_LEVEL] = 86; t->p[P_REV] = 90; t->p[P_CHOR] = 50;
    t = &trk[2];
    demo_sound(t, 0, 8);                      /* ANALOG PLUCK: the arpeggiator, on the chords held */
    t->p[P_AMODE] = 3;                        /* UPDN */
    t->p[P_ARATE] = 1;                        /* 1/8 (a note a step) */
    t->p[P_AOCT] = 2;
    t->p[P_AGATE] = 70;
    t->p[P_LEVEL] = 78; t->p[P_REV] = 60; t->p[P_DLY] = 40; t->p[P_CHOR] = 30;
    t = &trk[3];
    demo_sound(t, 0, 0);                      /* ANALOG SAW LEAD: MONO, sliding */
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 70;
    t->p[P_LEVEL] = 70; t->p[P_REV] = 80;
    t = &trk[4];
    demo_sound(t, 0, 7);                      /* ANALOG SUB BASS */
    t->p[P_GLIDE] = 60; t->p[P_LEVEL] = 94; t->p[P_REV] = 0; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[5];
    demo_sound(t, ENGI_DRUM, 1);              /* DEEP KIT: the kick */
    t->p[P_E0 + 1] = 48;
    t->p[P_E0 + 2] = 28;
    t->p[P_E0 + 4] = 24;
    t->p[P_E0 + 6] = 1;
    t->p[P_LEVEL] = 104; t->p[P_REV] = 8; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[6];
    demo_sound(t, ENGI_DRUM, 0);              /* DRUM KIT: snare, hats, the cymbal */
    t->p[P_E0 + 2] = 44;
    t->p[P_LEVEL] = 84; t->p[P_REV] = 96; t->p[P_DLY] = 12;
    t = &trk[7];
    demo_sound(t, 5, 0);                      /* VOICE CHOIR AAH */
    t->p[P_E0 + 4] = 72;
    t->p[P_E0 + 5] = 12;
    t->p[P_E0 + 6] = 100;
    t->p[P_E0 + 7] = 24;
    t->p[P_ATK] = 90; t->p[P_REL] = 100; t->p[P_LEVEL] = 74; t->p[P_REV] = 110;
    {                                         /* the mix: each one its own band (EQ: LOW CUT, HIGH CUT, a bell) */
        static const int8_t EQ[8][5] = {
            {14, 118, 22, 20, 0},             /* toms: no rumble, their body up (120 Hz) */
            {34, 104, 50, -24, 0},            /* pad: out of the sub's way, its mud (~400 Hz) down */
            {46, 118, 96, 16, 0},             /* arp: above the pad, its bite (~3 kHz) */
            {38, 118, 88, 20, 0},             /* lead: its edge (~2 kHz) */
            {0, 50, 20, 12, 0},               /* sub: nothing over ~350 Hz, its weight (~70 Hz) */
            {0, 118, 40, 0, 0},               /* kick: as it is */
            {36, 118, 108, 12, 0},            /* snare, hats: no low end, air (~6 kHz) */
            {42, 100, 60, -20, 0},            /* choir: between the pad and the arp */
        };
        for (i = 0; i < NTRK; i++) {
            trk[i].p[P_EQLO] = EQ[i][0];
            trk[i].p[P_EQHI] = EQ[i][1];
            trk[i].p[P_EQ1F] = EQ[i][2];
            trk[i].p[P_EQ1G] = EQ[i][3];
        }
    }
    for (i = 1; i <= 2u; i++)
        trkx[i].curve = 1;                    /* (their automation ramps) */
    trkx[3].curve = 1;
    {                                         /* the sidechain: pad, strings, sub and choir pump on the kick (T6) */
        static const int8_t DK[4][3] = {{1, 120, 14}, {2, 112, 14}, {4, 127, 8}, {7, 100, 18}};   /* track, DEPTH, REL */
        for (i = 0; i < 4u; i++) {
            trk[DK[i][0]].p[P_DKSRC] = 6;
            trk[DK[i][0]].p[P_DKAMT] = (int16_t)DK[i][1];
            trk[DK[i][0]].p[P_DKREL] = (int16_t)DK[i][2];
        }
    }
    demo_pattern(1);
    for (k = 0; k < 8u; k++)                  /* pattern 1 kept, 2 written, 1 back on the tracks */
        (void)pattern_new(k, 0);
    demo_pattern(2);
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);            /* the SONG: 1 twice, 2 twice (GLO > SONG; PLAY there plays it) */
    chain_config.count = 2;
    for (k = 0; k < NTRK; k++) {
        chain_config.row[0].slot[k] = 1u;
        chain_config.row[1].slot[k] = 2u;
    }
    chain_config.row[0].repeat = 1;          /* (Dah: 1 then 2, round: the filter's arc over both) */
    chain_config.row[1].repeat = 1;
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
