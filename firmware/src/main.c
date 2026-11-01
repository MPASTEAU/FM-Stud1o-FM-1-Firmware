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
/* Dah's power-on sound (FELUCCA_DEMO): dark trip-hop, 86 BPM in 1/16, E minor (Em Em Cmaj7 B7, then Am7 Em Cmaj7 B7),
 * sixteen sections of four bars (3 min) from the SONG, eight tracks, the sound open (no high cut but the bass's):
 *   track 1  PHYS HARP, the haunting arpeggio   track 2  GRAIN CLOUD PAD, the dark air   track 3  ANALOG SOFT PAD
 *   track 4  ANALOG SAW LEAD, the theme, sliding   track 5  TRIO FAT BASS, the riff   track 6  DRUM HARD KIT, kick
 *   snare hats   track 7  DRUM DEEP KIT, the sub kick under it, the claps, the rolls   track 8  FM6 TINE EP, the stabs
 * The pads duck on the kick (SIDECHAIN). Each track's patterns are its variants (demo_var); no project written */
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
}
static void felucca_demo(void) __attribute__((noinline));   /* (its own, built for size: tools/size_fns.py) */
static void demo_var(uint32_t k, uint32_t v) __attribute__((noinline));
#ifndef DAH_DEMO_SONG
#define DAH_DEMO_SONG 1                         /* (Dah) the power-on song: 1 the trip-hop, 0 the FX test */
#endif
#if DAH_DEMO_SONG == 1
/* the progressions (A, B): each bar's root (the bass, octave 2) and its chord (four notes, from E3) */
static const uint8_t DM_ROOT[2][4] = {{40, 40, 36, 35}, {33, 40, 36, 35}};
static const uint8_t DM_CH[2][4][4] = {{{52, 55, 59, 62}, {52, 55, 59, 66}, {52, 55, 59, 60}, {51, 54, 57, 59}},
                                       {{52, 55, 57, 60}, {52, 55, 59, 62}, {52, 55, 59, 60}, {51, 54, 57, 63}}};
/* track k's variant v (1..) onto its steps (empty) */
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 0: {                                                           /* T1 the harp: eighths up and down */
            static const uint8_t AR[8] = {0, 1, 2, 3, 2, 3, 1, 2};
            const uint8_t *c = DM_CH[v == 3u][b];
            for (i = 0; i < 8u; i++) {
                uint32_t n = c[AR[i]] + (v == 2u && AR[i] == 3u ? 12u : 0u);
                demo_note(&t->step[s0 + i * 2u], n, i ? 76 : 96);
            }
            if (v == 2u && b == 3u)                                         /* (a run up into the next) */
                for (i = 12; i < 16u; i++)
                    demo_note(&t->step[s0 + i], (uint32_t)c[i - 12u] + 12u, 70 + i);
            break;
        }
        case 1:                                                             /* T2 the dark air: E and B, held */
            if (!b) {
                static const uint8_t D[2] = {40, 47};
                dm_chord(t, 0, 64, D, 2, 80);
                demo_auto(t, P_E0, 0, 56, 10, 110);
            }
            break;
        case 2:                                                             /* T3 the pad: the chord, a bar */
            dm_bar(t, b, DM_CH[v == 2u][b], 4, 86);
            break;
        case 3: {                                                           /* T4 the theme: long, sliding */
            static const uint8_t TH[3][4][4] = {                            /* (note, length, note, length) a bar */
                {{71, 8, 69, 8}, {66, 16, 0, 0}, {64, 8, 67, 8}, {66, 12, 63, 4}},
                {{76, 12, 74, 4}, {71, 16, 0, 0}, {72, 8, 71, 8}, {66, 16, 0, 0}},
                {{69, 8, 72, 8}, {71, 16, 0, 0}, {67, 8, 64, 8}, {66, 12, 63, 4}}};
            const uint8_t *n = TH[v - 1u][b];
            demo_note(&t->step[s0], n[0], 100);
            demo_hold(t, s0, n[1]);
            if (n[2]) {
                t->step[s0 + n[1] - 1u].flags |= SF_SLIDE;
                demo_note(&t->step[s0 + n[1]], n[2], 96);
                demo_hold(t, s0 + n[1], n[3]);
            }
            break;
        }
        case 4: {                                                           /* T5 the riff: the root, its octave, its fifth */
            static const uint8_t RF[6][3] = {{0, 3, 0}, {4, 2, 0}, {7, 2, 12}, {10, 2, 0}, {13, 1, 7}, {14, 2, 10}};
            uint32_t r = DM_ROOT[v == 2u][b];
            for (i = 0; i < 6u; i++) {
                demo_note(&t->step[s0 + RF[i][0]], r + RF[i][2], i ? 96 : 112);
                demo_hold(t, s0 + RF[i][0], RF[i][1]);
            }
            t->step[s0 + 15u].flags |= SF_SLIDE;
            break;
        }
        case 5:                                                             /* T6 the kit: kick, snare, hats */
            if (v == 4u) {                                                  /* (half: the kick, a rim, the hats) */
                demo_hit(t, s0, 1u << 0, 104);
                demo_hit(t, s0 + 12u, 1u << 6, 80);
                for (i = 2; i < 16u; i += 4u)
                    demo_hit(t, s0 + i, 1u << 3, 50);
                break;
            }
            demo_hit(t, s0, 1u << 0, 120);                                  /* the boom-bap */
            demo_hit(t, s0 + 10u, 1u << 0, 104);
            if (v >= 2u)
                demo_hit(t, s0 + 7u, 1u << 0, 86);
            demo_hit(t, s0 + 4u, 1u << 1 | 1u << 2, 118);                  /* snare and clap on 2 and 4 */
            demo_hit(t, s0 + 12u, 1u << 1 | 1u << 2, 122);
            if (v >= 2u)
                demo_hit(t, s0 + 15u, 1u << 1, 44);                         /* a ghost */
            for (i = 0; i < 16u; i += 2u)
                demo_hit(t, s0 + i, 1u << 3, i % 4u ? 52 : 74);
            if (b & 1u)
                demo_hit(t, s0 + 14u, 1u << 4, 66);                         /* an open hat */
            if (v == 3u && b == 3u)                                         /* the fill */
                for (i = 8; i < 16u; i++)
                    demo_hit(t, s0 + i, 1u << 1, 60 + i * 4u);
            break;
        case 6:                                                             /* T7 the sub kick, the claps; the roll */
            if (v == 2u) {
                for (i = 0; i < 16u; i += b < 2u ? 4u : b == 2u ? 2u : 1u)
                    demo_hit(t, s0 + i, 1u << 1, 40 + b * 20u + i * 2u);
                break;
            }
            demo_hit(t, s0, 1u << 0, 110);
            demo_hit(t, s0 + 10u, 1u << 0, 90);
            demo_hit(t, s0 + 12u, 1u << 2, 92);
            break;
        default:                                                            /* T8 the stabs */
            dm_chord(t, s0, 6, DM_CH[v == 2u][b], 4, 92);
            dm_chord(t, s0 + 10u, 4, DM_CH[v == 2u][b], 4, 78);
            break;
        }
    }
}
static void felucca_demo(void)                /* (Dah: the trip-hop song: DAH_DEMO_SONG 1) */
{
    /* the song: each section's variant per track (0: silent there) */
    static const uint8_t SEC[16][8] = {
        {1, 1, 0, 0, 0, 0, 0, 0},             /* the harp in the dark */
        {1, 1, 0, 0, 1, 4, 0, 0},             /* the riff, the kick alone */
        {2, 1, 0, 0, 1, 1, 1, 0},             /* the beat */
        {1, 1, 1, 0, 1, 1, 1, 1},             /* the pad, the stabs */
        {2, 1, 1, 1, 1, 2, 1, 1},             /* the theme */
        {1, 1, 1, 2, 1, 2, 1, 1},             /* .. up */
        {2, 1, 1, 0, 0, 0, 0, 1},             /* the break */
        {1, 1, 1, 0, 1, 3, 2, 1},             /* the build: the roll, the fill */
        {2, 1, 1, 1, 1, 2, 1, 1},             /* the drop */
        {3, 1, 2, 3, 2, 2, 1, 2},             /* the other chords */
        {3, 1, 2, 0, 2, 1, 1, 2},
        {1, 1, 1, 2, 1, 2, 1, 1},             /* the theme up */
        {0, 1, 0, 0, 1, 1, 1, 0},             /* the riff and the beat alone */
        {2, 1, 1, 1, 1, 2, 1, 1},             /* all */
        {1, 1, 1, 0, 0, 4, 0, 1},             /* down */
        {1, 1, 0, 0, 0, 0, 0, 0},             /* the harp in the dark */
    };
    static const uint8_t NV[8] = {3, 1, 2, 3, 2, 4, 2, 2};   /* each track's variants */
    uint32_t i, k, v;
    song.g[G_BPM] = 86;
    song.g[G_SWING] = 18;                     /* (a lazy beat) */
    song.g[G_RTYPE] = 2;                      /* HALL */
    song.g[G_RSIZE] = 100;
    song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 56;
    song.g[G_DTIME] = 10;                     /* 1/8D */
    song.g[G_DCOLOR] = 50;
    song.g[G_CRATE] = 10;
    song.g[G_CDEPTH] = 80;
    song.g[G_MCOMP] = 40;                     /* the MASTER glued */
    song.g[G_MREL] = 60;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;                 /* 1/16 */
    }
    {                                         /* the sounds, then what they are set to: track (8 the MASTER), id
                                               * (DS_X + n: the track's extras), value */
        static const uint8_t SND[8][2] = {{ENGI_PHYS, 8}, {8, 0}, {0, 1}, {0, 0}, {6, 0}, {ENGI_DRUM, 5}, {ENGI_DRUM, 1},
                                          {ENGI_FM6, 0}};   /* HARP, CLOUD PAD, SOFT PAD, SAW LEAD, FAT BASS, HARD
                                                             * KIT, DEEP KIT, TINE EP */
        enum { DS_DIN = 200, DS_SC2, DS_CAMT, DS_CREL, DS_CIN, DS_COUT };
        static const int16_t DS[][3] = {
            {0, P_REV, 80}, {0, P_DLY, 50}, {0, P_PAN, -14},   /* harp: a slow chorus inside, an echo */
            {0, P_ITYPE, IT_CHORUS}, {0, P_IA, 10}, {0, P_IB, 60}, {0, P_IMIX, 50},
            {1, P_ATK, 110}, {1, P_REL, 110}, {1, P_REV, 100},   /* the dark air */
            {2, P_ATK, 70}, {2, P_REL, 90}, {2, P_SPRD, 110}, {2, P_REV, 70}, {2, P_CHOR, 60},   /* pad: slow phaser */
            {2, P_ITYPE, IT_PHASER}, {2, P_IA, 8}, {2, P_IB, 70}, {2, P_IMIX, 70},
            {3, P_VOICE, V_MONO}, {3, P_GLIDE, 70}, {3, P_ATK, 20}, {3, P_REV, 70}, {3, P_DLY, 60}, {3, P_CHOR, 30},
            {3, P_DIST, 22}, {3, P_DCOL, -10},           /* lead: driven a little, flanged, the long echo */
            {3, P_ITYPE, IT_FLANGER}, {3, P_IA, 12}, {3, P_IB, 50}, {3, P_IC, 50}, {3, P_IMIX, 35},
            {4, P_VOICE, V_MONO}, {4, P_GLIDE, 40}, {4, P_REV, 0}, {4, P_DLY, 0}, {4, P_CHOR, 0},
            {4, P_DIST, 34}, {4, P_DCOL, -20}, {4, DS_DIN, 10},   /* bass: saturated, glued, pumping on the kicks */
            {4, P_ITYPE, IT_SOFT}, {4, P_IA, 30}, {4, P_IMIX, 50},
            {4, P_DKSRC, 6}, {4, DS_SC2, 7}, {4, P_DKAMT, 55}, {4, P_DKREL, -20},
            {5, P_REV, 24}, {5, P_DLY, 0}, {5, P_CHOR, 0},   /* kit: crushed a little, hit harder */
            {5, P_DIST, 14}, {5, DS_CIN, 12}, {5, DS_COUT, 6},
            {6, P_REV, 50}, {6, P_DLY, 0}, {6, P_CHOR, 0},
            {7, P_REV, 60}, {7, P_DLY, 30}, {7, P_CHOR, 50}, {7, P_PAN, 14},   /* stabs: a chorus inside, chopped */
            {7, P_ITYPE, IT_CHORUS}, {7, P_IA, 20}, {7, P_IB, 80}, {7, P_IMIX, 60}, {7, P_SLCR, 1}, {7, P_SLDEPTH, 60},
            {1, P_DKSRC, 6}, {1, DS_SC2, 7}, {1, P_DKAMT, 80}, {1, P_DKREL, -10},   /* the air and the pad duck on */
            {2, P_DKSRC, 6}, {2, DS_SC2, 7}, {2, P_DKAMT, 80}, {2, P_DKREL, -10},   /* the kicks (both kits), dry */
            {8, P_DIST, 10}, {8, P_EQLO, 8}, {8, P_EQ1F, 100}, {8, P_EQ1G, 10},   /* the MASTER: warm, open, glued */
        };
        static const int8_t EQ[8][6] = {      /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
            {40, 118, 80, 3, 118, 80},        /* harp */
            {36, 118, 50, -4, 96, 60},        /* air */
            {40, 118, 50, -4, 96, 70},        /* pad: the low mids out */
            {44, 118, 80, 3, 100, 80},        /* lead */
            {0, 80, 20, 4, 112, 100},         /* bass */
            {0, 118, 24, 3, 108, 90},         /* kit */
            {0, 118, 18, 4, 100, 40},         /* sub kick, claps */
            {40, 118, 60, -3, 100, 70},       /* stabs */
        };
        for (i = 0; i < NTRK; i++) {
            demo_sound(&trk[i], SND[i][0], SND[i][1]);
            trk[i].p[P_EQLO] = EQ[i][0];
            trk[i].p[P_EQHI] = EQ[i][1];
            trk[i].p[P_EQ1F] = EQ[i][2];
            trk[i].p[P_EQ1G] = EQ[i][3];
            trk[i].p[P_LEVEL] = EQ[i][4];
            trkx[i].rsv0 = (uint8_t)EQ[i][5];
            trkx[i].rsv1 = i == 5u ? 40 : 60;
        }
        for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
            uint32_t k2 = (uint32_t)DS[i][0], id = (uint32_t)DS[i][1];
            int16_t v2 = DS[i][2];
            trkx_t *x = &trkx[k2 % NTRK];
            if (k2 == 8u) mtrk.p[id] = v2;
            else if (id == DS_DIN) x->din = (int8_t)v2;
            else if (id == DS_SC2) x->sc2 = (uint8_t)v2;
            else if (id == DS_CIN) x->cin = (int8_t)v2;
            else if (id == DS_COUT) x->cout = (int8_t)v2;
            else trk[k2].p[id] = v2;
        }
        song.g[G_MCOMP] = 56;
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
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#else
/* (Dah) the FX test, the power-on sound for now: four tracks, one pattern of four bars looping (86 BPM, E minor), every
 * effect on them: the kit (LAYER KIT: DIST, EQ, COMP; the reverb), the bass (ANALOG ACID: DIST, INSERT SOFT, EQ, COMP,
 * SIDECHAIN), the pad (FM6 PAD: INSERT PHASER, SLICER, EQ, SIDECHAIN; CHORUS, REVERB), the lead (VOICE VOX LEAD: INSERT
 * FLANGER, DIST, EQ, COMP; DELAY, REVERB);
 * the MASTER's EQ and COMP */
static void felucca_demo(void)
{
    uint32_t i, b;
    track_t *t;
    song.g[G_BPM] = 86;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 100; song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 60; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 50;
    song.g[G_CRATE] = 16; song.g[G_CDEPTH] = 90;
    song.g[G_MCOMP] = 50; song.g[G_MREL] = 60;
    mtrk.p[P_EQLO] = 10;                       /* (the MASTER: its EQ, a low cut and a bell) */
    mtrk.p[P_EQ1F] = 70; mtrk.p[P_EQ1G] = 8;
    song.ntrk = 4;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;
    }
    t = &trk[0];
    demo_sound(t, ENGI_DRUM, 4);              /* LAYER KIT */
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        demo_hit(t, s0, 1u << 0, 120);
        demo_hit(t, s0 + 10u, 1u << 0, 104);
        demo_hit(t, s0 + 4u, 1u << 1 | 1u << 2, 118);
        demo_hit(t, s0 + 12u, 1u << 1 | 1u << 2, 122);
        for (i = 0; i < 16u; i += 2u)
            demo_hit(t, s0 + i, 1u << 3, i % 4u ? 52 : 74);
        if (b & 1u)
            demo_hit(t, s0 + 14u, 1u << 4, 66);
    }
    t->p[P_DIST] = 20; t->p[P_EQLO] = 4; t->p[P_EQ1F] = 22; t->p[P_EQ1G] = 10; t->p[P_REV] = 20;
    trkx[0].rsv0 = 70; trkx[0].rsv1 = 50;
    t = &trk[1];
    demo_sound(t, 0, 4);                      /* ANALOG ACID */
    t->p[P_LEVEL] = 118;
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 40;
    for (b = 0; b < 4u; b++) {
        static const uint8_t R[4] = {40, 40, 36, 35};
        demo_note(&t->step[b * 16u], R[b], 112); demo_hold(t, b * 16u, 3);
        demo_note(&t->step[b * 16u + 6u], R[b] + 12u, 96); demo_hold(t, b * 16u + 6u, 2);
        demo_note(&t->step[b * 16u + 10u], R[b], 100); demo_hold(t, b * 16u + 10u, 4);
    }
    t->p[P_DIST] = 40; t->p[P_ITYPE] = IT_SOFT; t->p[P_IMIX] = 70;
    t->p[P_EQHI] = 80; t->p[P_EQ1F] = 20; t->p[P_EQ1G] = 12;
    trkx[1].rsv0 = 80; trkx[1].rsv1 = 50;
    t->p[P_DKSRC] = 1; t->p[P_DKAMT] = 70; t->p[P_DKREL] = -10;
    t->p[P_REV] = 0; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[2];
    demo_sound(t, ENGI_FM6, 4);               /* FM6 PAD */
    for (b = 0; b < 4u; b++) {
        static const uint8_t C[4][4] = {{52, 55, 59, 62}, {52, 55, 59, 66}, {52, 55, 59, 60}, {51, 54, 57, 59}};
        dm_bar(t, b, C[b], 4, 90);
    }
    t->p[P_ITYPE] = IT_PHASER; t->p[P_IA] = 20; t->p[P_IB] = 80; t->p[P_IMIX] = 90;
    t->p[P_SLCR] = 1; t->p[P_SLDEPTH] = 90;
    t->p[P_EQLO] = 40; t->p[P_EQ1F] = 80; t->p[P_EQ1G] = 10;
    t->p[P_DKSRC] = 1; t->p[P_DKAMT] = 80; t->p[P_DKREL] = -10;
    t->p[P_CHOR] = 60; t->p[P_REV] = 70; t->p[P_DLY] = 0;
    t = &trk[3];
    demo_sound(t, 5, 1);                      /* VOICE VOX LEAD */
    t->p[P_LEVEL] = 84;
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 60;
    {
        static const uint8_t L[8][2] = {{71, 8}, {69, 8}, {66, 16}, {64, 8}, {67, 8}, {66, 12}, {63, 4}, {0, 0}};
        uint32_t at = 0;
        for (i = 0; L[i][1]; i++) {
            demo_note(&t->step[at], L[i][0], 100);
            demo_hold(t, at, L[i][1]);
            t->step[at + L[i][1] - 1u].flags |= SF_SLIDE;
            at += L[i][1];
        }
    }
    t->p[P_ITYPE] = IT_FLANGER; t->p[P_IA] = 30; t->p[P_IB] = 70; t->p[P_IC] = 60; t->p[P_IMIX] = 60;
    t->p[P_DIST] = 30; t->p[P_EQLO] = 44; t->p[P_EQ1F] = 84; t->p[P_EQ1G] = 8;
    trkx[3].rsv0 = 60; trkx[3].rsv1 = 60;
    t->p[P_DLY] = 70; t->p[P_REV] = 60; t->p[P_CHOR] = 0;
    for (i = 0; i < 4u; i++)
        (void)pattern_go(i, 1);
    chain_defaults(&chain_config);            /* the SONG: one section, round */
    chain_config.count = 1;
    for (i = 0; i < NTRK; i++)
        chain_config.row[0].slot[i] = i < 4u ? 1u : (uint8_t)CHAIN_SILENT;
    chain_config.row[0].repeat = 1;
}
#endif
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
    load_depth++;                             /* (ui.c: the power-on loads are inner ones) */
    mix_solo = 0;                             /* (Dah: MIXER's SOLO, none) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    ps_reset();                               /* (each track on its pattern 1, no other) */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        trkx_defaults(&trkx[i]);              /* (LFO DRAW's points, CURVE STEP) */
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        eng_mem_claim(t);                     /* (zeros already: no note can come before) */
        track_defaults_steps(t);              /* (a sound load never touches them) */
    }
#if FELUCCA_DEMO
    felucca_demo();
    for (i = 0; i < NTRK; i++) {              /* (Dah: its EQs as LOW CUT, HIGH CUT and bells: as bands, fx_eq.c;
                                               * a track's then as BASS and TREBLE) */
        if (!(trkx[i].eq.f & EQF_4))
            eq_adopt(trk[i].p, trk[i].p, &trkx[i].eq);
        tone_adopt(trk[i].p, &trkx[i].eq);
    }
    if (!(eq_mc.f & EQF_4))
        eq_adopt(mtrk.p, mtrk.p, &eq_mc);
#endif
    load_depth--;
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
    autosave_boot(boot_clean);                /* (1.2) the last session's music, under the splash (else the
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
        chain_poll();                                 /* a SONG's next section ready: pstore.c */
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
    if (p3 & 1u)
        bootguard.failed = 0;                  /* (a power-on: the power was cut, no boot died; a crash resets by software,
                                                * a hang by the watchdog) */
    else if (bootguard.pending)
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
