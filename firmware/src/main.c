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
#define DAH_DEMO_SONG 7                         /* (Dah) the power-on song: 7 the showcase, 6 after Zimmer's Time, 5 the dark one, 4 the long chords, 3 the slow one, after Archive, 2 the deep house, 1 the
                                                 * trip-hop (kept), 0 the FX test */
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
#elif DAH_DEMO_SONG == 2
/* (Dah) the deep house: 122 BPM in 1/16, A minor (Am7 Fmaj7 Cmaj7 G6, then Dm7 Em7 Fmaj7 G6), sixteen sections of four
 * bars from the SONG; the kick alone on its track, every other one pumping on it (SIDECHAIN):
 *   track 1  PHYS PLUCK, the riff, into the dotted echo   track 2  GRAIN SHIMMER, the air   track 3  ANALOG SOFT PAD,
 *   phased   track 4  VOICE VOX LEAD, the hook, flanged   track 5  ANALOG SQR BASS, the off-beat bass, saturated
 *   track 6  DRUM HARD KIT, the clap, the hats, the rim   track 7  DRUM DEEP KIT, the kick   track 8  FM6 TINE EP, the
 *   stabs, a chorus inside */
static const uint8_t DM_ROOT[2][4] = {{45, 41, 48, 43}, {38, 40, 41, 43}};
static const uint8_t DM_CH[2][4][4] = {{{57, 60, 64, 67}, {57, 60, 64, 65}, {55, 60, 64, 67}, {55, 59, 62, 64}},
                                       {{57, 60, 62, 65}, {55, 59, 62, 64}, {57, 60, 64, 65}, {55, 59, 62, 64}}};
static void demo_var(uint32_t k, uint32_t v) __attribute__((noinline));
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 0: {                                                           /* T1 the riff: chord tones, syncopated */
            static const uint8_t AT[6] = {0, 3, 6, 8, 10, 13}, NI[6] = {0, 2, 3, 1, 2, 3};
            const uint8_t *c = DM_CH[v == 3u][b];
            for (i = 0; i < 6u; i++)
                demo_note(&t->step[s0 + AT[i]], (uint32_t)c[NI[i]] + 12u, i ? 80 : 100);
            if (v == 2u)                                                    /* (denser: the sixteenths between) */
                for (i = 0; i < 16u; i += 4u)
                    demo_note(&t->step[s0 + i + 1u], (uint32_t)c[(i / 4u) % 4u] + 24u, 60);
            break;
        }
        case 1:                                                             /* T2 the air: E and A, held */
            if (!b) {
                static const uint8_t D[2] = {64, 69};
                dm_chord(t, 0, 64, D, 2, 70);
                demo_auto(t, P_E0, 0, 56, 20, 100);
            }
            break;
        case 2:                                                             /* T3 the pad */
            dm_bar(t, b, DM_CH[v == 2u][b], 4, 84);
            break;
        case 3: {                                                           /* T4 the hook */
            static const uint8_t H[3][4][4] = {                             /* (note, length, note, length) a bar */
                {{76, 4, 74, 4}, {72, 8, 0, 0}, {76, 4, 79, 4}, {74, 12, 0, 0}},
                {{76, 4, 74, 4}, {72, 4, 69, 4}, {72, 4, 74, 4}, {76, 12, 0, 0}},
                {{77, 4, 76, 4}, {74, 8, 0, 0}, {72, 4, 74, 4}, {71, 12, 0, 0}}};
            const uint8_t *n = H[v - 1u][b];
            demo_note(&t->step[s0 + 2u], n[0], 100);
            demo_hold(t, s0 + 2u, n[1]);
            if (n[2]) {
                t->step[s0 + 2u + n[1] - 1u].flags |= SF_SLIDE;
                demo_note(&t->step[s0 + 2u + n[1]], n[2], 92);
                demo_hold(t, s0 + 2u + n[1], n[3]);
            }
            break;
        }
        case 4: {                                                           /* T5 the bass: off the beat */
            uint32_t r = DM_ROOT[v == 3u][b];
            for (i = 2; i < 16u; i += 4u) {
                demo_note(&t->step[s0 + i], r + ((i / 4u) & 1u ? 12u : 0u), 108);
                demo_hold(t, s0 + i, 2);
                if (v == 2u) {                                              /* (rolling: a sixteenth after each) */
                    demo_note(&t->step[s0 + i + 1u], r + 7u, 88);
                }
            }
            break;
        }
        case 5:                                                             /* T6 the clap, the hats, the rim */
            if (v != 3u)
                for (i = 0; i < 16u; i++)
                    demo_hit(t, s0 + i, 1u << 3, i % 4u == 2u ? 90 : i & 1u ? 34 : 52);   /* the hats, the off-beat up */
            if (v == 1u || v == 2u) {
                demo_hit(t, s0 + 4u, 1u << 2, 112);
                demo_hit(t, s0 + 12u, 1u << 2, 116);
                for (i = 2; i < 16u; i += 4u)
                    demo_hit(t, s0 + i, 1u << 4, 70);                       /* the open hat, off the beat */
                demo_hit(t, s0 + 7u, 1u << 6, 60);
                demo_hit(t, s0 + 14u, 1u << 6, 54);
            } else if (v == 3u)                                             /* the build: the snare rolling */
                for (i = 0; i < 16u; i += b < 2u ? 4u : b == 2u ? 2u : 1u)
                    demo_hit(t, s0 + i, 1u << 1, 40 + b * 20u + i * 2u);
            break;
        case 6:                                                             /* T7 the kick: four on the floor */
            if (v == 2u && b == 3u)
                break;                                                      /* (the bar before the drop: none) */
            for (i = 0; i < 16u; i += 4u)
                demo_hit(t, s0 + i, 1u << 0, i ? 112 : 124);
            break;
        default:                                                            /* T8 the stabs */
            dm_chord(t, s0 + 3u, 2, DM_CH[v == 2u][b], 4, 90);
            dm_chord(t, s0 + 10u, 2, DM_CH[v == 2u][b], 4, 82);
            break;
        }
    }
    if (k == 2u && v == 1u) {                                               /* the pad: its HIGH CUT breathing */
        demo_auto(t, P_EQHI, 0, 56, 80, 104);
        (void)motion_set_event(t, 63, P_EQHI, 80);
        trkx[2].curve = 1;
    }
}
static void felucca_demo(void)
{
    static const uint8_t SEC[16][8] = {
        {0, 1, 0, 0, 0, 0, 1, 0},             /* the kick, the air */
        {1, 1, 0, 0, 0, 0, 1, 0},             /* the riff */
        {1, 1, 0, 0, 1, 1, 1, 0},             /* the bass, the hats and the clap */
        {2, 1, 1, 0, 1, 1, 1, 1},             /* the pad, the stabs */
        {1, 1, 1, 1, 1, 1, 1, 1},             /* the hook */
        {2, 1, 1, 2, 2, 1, 1, 1},             /* .. rolling */
        {1, 1, 1, 0, 0, 2, 0, 0},             /* the break: no kick, no bass */
        {2, 1, 1, 0, 0, 3, 2, 1},             /* the build: the snare rolling, the kick out before the drop */
        {2, 1, 1, 1, 2, 1, 1, 1},             /* the drop */
        {3, 1, 2, 3, 3, 1, 1, 2},             /* the other chords */
        {3, 1, 2, 3, 3, 2, 1, 2},
        {2, 1, 1, 2, 2, 1, 1, 1},
        {0, 1, 1, 0, 1, 1, 1, 0},             /* the bass and the beat */
        {2, 1, 1, 1, 2, 1, 1, 1},             /* all */
        {1, 1, 1, 0, 0, 2, 1, 1},             /* down */
        {1, 1, 0, 0, 0, 0, 1, 0},             /* the riff and the kick */
    };
    static const uint8_t NV[8] = {3, 1, 2, 3, 3, 3, 2, 2};
    static const uint8_t SND[8][2] = {{ENGI_PHYS, 2}, {8, 3}, {0, 1}, {5, 1}, {0, 2}, {ENGI_DRUM, 5}, {ENGI_DRUM, 1},
                                      {ENGI_FM6, 0}};   /* PLUCK, SHIMMER, SOFT PAD, VOX LEAD, SQR BASS, HARD KIT, DEEP
                                                         * KIT, TINE EP */
    enum { DS_DIN = 200, DS_SC2, DS_CIN, DS_COUT };
    static const int16_t DS[][3] = {          /* track (8 the MASTER), parameter (DS_*: the extras), value */
        {0, P_REV, 50}, {0, P_DLY, 60}, {0, P_PAN, -16},                    /* the riff: into the echo */
        {1, P_ATK, 110}, {1, P_REL, 110}, {1, P_REV, 110},                  /* the air */
        {2, P_ATK, 60}, {2, P_REL, 90}, {2, P_SPRD, 110}, {2, P_REV, 70}, {2, P_CHOR, 40},
        {2, P_ITYPE, IT_PHASER}, {2, P_IA, 10}, {2, P_IB, 70}, {2, P_IMIX, 70},   /* the pad: phased */
        {3, P_VOICE, V_MONO}, {3, P_GLIDE, 50}, {3, P_REV, 80}, {3, P_DLY, 50},
        {3, P_ITYPE, IT_FLANGER}, {3, P_IA, 14}, {3, P_IB, 50}, {3, P_IC, 40}, {3, P_IMIX, 30},
        {4, P_VOICE, V_MONO}, {4, P_REV, 0}, {4, P_DLY, 0}, {4, P_CHOR, 0},  /* the bass: saturated */
        {4, P_DIST, 30}, {4, P_DCOL, -16}, {4, DS_DIN, 8},
        {5, P_REV, 30}, {5, P_DLY, 0}, {5, P_CHOR, 0}, {5, P_PAN, 8},        /* the kit: hit harder */
        {5, DS_CIN, 10}, {5, DS_COUT, 4},
        {6, P_REV, 10}, {6, P_DLY, 0}, {6, P_CHOR, 0},                       /* the kick */
        {7, P_REV, 60}, {7, P_DLY, 30}, {7, P_PAN, 14},                      /* the stabs: a chorus inside */
        {7, P_ITYPE, IT_CHORUS}, {7, P_IA, 20}, {7, P_IB, 80}, {7, P_IMIX, 60},
        {8, P_DIST, 8}, {8, P_EQLO, 8}, {8, P_EQ1F, 100}, {8, P_EQ1G, 8},    /* the MASTER: warm, open, glued */
    };
    static const int8_t EQ[8][6] = {          /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
        {44, 118, 80, 3, 122, 90},            /* riff */
        {60, 110, 90, -4, 84, 60},            /* air */
        {44, 118, 50, -4, 96, 70},            /* pad */
        {48, 118, 80, 3, 108, 90},            /* hook */
        {0, 84, 22, 4, 112, 90},              /* bass */
        {30, 118, 90, 3, 112, 70},            /* kit */
        {0, 110, 18, 5, 112, 60},             /* kick */
        {44, 118, 60, -3, 116, 80},           /* stabs */
    };
    uint32_t i, k, v;
    song.g[G_BPM] = 122;
    song.g[G_SWING] = 8;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 90; song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 50; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 50;   /* (1/8D) */
    song.g[G_CRATE] = 12; song.g[G_CDEPTH] = 80;
    song.g[G_MCOMP] = 56; song.g[G_MREL] = 60;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults_steps(t);
        t->p[P_SLEN] = 64;
        t->p[P_SDIV] = 2;
        demo_sound(t, SND[i][0], SND[i][1]);
        t->p[P_EQLO] = EQ[i][0]; t->p[P_EQHI] = EQ[i][1]; t->p[P_EQ1F] = EQ[i][2]; t->p[P_EQ1G] = EQ[i][3];
        t->p[P_LEVEL] = EQ[i][4];
        trkx[i].rsv0 = (uint8_t)EQ[i][5];
        trkx[i].rsv1 = 60;
        if (i != 6u && i != 5u) {             /* everything but the drums pumps on the kick */
            t->p[P_DKSRC] = 7;
            t->p[P_DKAMT] = i == 4u ? 70 : 80;
            t->p[P_DKREL] = -16;
        }
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
        uint32_t k2 = (uint32_t)DS[i][0], id = (uint32_t)DS[i][1];
        trkx_t *x = &trkx[k2 % NTRK];
        if (k2 == 8u) mtrk.p[id] = DS[i][2];
        else if (id == DS_DIN) x->din = (int8_t)DS[i][2];
        else if (id == DS_SC2) x->sc2 = (uint8_t)DS[i][2];
        else if (id == DS_CIN) x->cin = (int8_t)DS[i][2];
        else if (id == DS_COUT) x->cout = (int8_t)DS[i][2];
        else trk[k2].p[id] = DS[i][2];
    }
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#elif DAH_DEMO_SONG == 3
/* (Dah) slow and dark, after Archive (the sounds alive: LFOs and MOD slots on every synth): 80 BPM in 1/16, E minor, sixteen sections of four bars from the SONG; big held
 * chords, two a section (Em9 Cmaj9, then G6 D/F#), the sections A and B in turn; the beat half time. The arpeggio's filter (its HIGH CUT) one curve over the song, shut at the start, wide open at the drop; the
 * kick alone on its track, everything else pumping on it (SIDECHAIN):
 *   track 1  FM6 PLUCK, the arpeggio (one variant a section: its curve)   track 2  GRAIN SHIMMER, the air
 *   track 3  FM6 PAD, the chords   track 4  PHASE BELL, the motif, far in the echo   track 5  ANALOG SQR BASS, rolling
 *   track 6  DRUM LAYER KIT, the clap, the hats, the rolls   track 7  DRUM TIGHT KIT, the kick
 *   track 8  NOISE WIND, the riser before the drop, the fall after it */
static const uint8_t DM_ROOT[2][4] = {{40, 40, 36, 36}, {43, 43, 42, 42}};   /* (a bar each) */
static const uint8_t DM_CH[2][4][4] = {{{52, 59, 66, 67}, {52, 59, 66, 67}, {48, 55, 62, 64}, {48, 55, 62, 64}},   /* Em9 Cmaj9 */
                                       {{43, 50, 59, 64}, {43, 50, 59, 64}, {42, 50, 57, 64}, {42, 50, 57, 64}}};  /* G6 D/F# */
static const uint8_t AR_CUT[17] = {40, 50, 58, 66, 74, 80, 86, 70, 118, 100, 106, 110, 96, 80, 100, 70, 40};
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u, h;
        switch (k) {
        case 0: {                                                           /* T1 the arpeggio, sixteenths */
            static const uint8_t UP[16] = {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 3, 1, 2, 0, 3};
            h = !(v & 1u);                                                  /* (A and B in turn, as the chords) */
            for (i = 0; i < 16u; i++)
                demo_note(&t->step[s0 + i], (uint32_t)DM_CH[h][b][UP[i]] + 12u, i % 4u ? 74 : 100);
            break;
        }
        case 1:                                                             /* T2 the air: D and A, held */
            if (!b) {
                static const uint8_t D[2] = {50, 57};
                dm_chord(t, 0, 64, D, 2, 76);
                demo_auto(t, P_E0, 0, 56, 10, 110);
            }
            break;
        case 2:                                                             /* T3 the chords: held, two bars */
            if (b == 0u || b == 2u)
                dm_chord(t, s0, 32, DM_CH[v == 2u][b], 4, 86);
            break;
        case 3: {                                                           /* T4 the motif, sparse */
            static const uint8_t M[2][4][4] = {                             /* (note, length, note, length) a bar */
                {{71, 8, 74, 8}, {76, 16, 0, 0}, {74, 8, 71, 8}, {67, 16, 0, 0}},    /* over Em9, Cmaj9 */
                {{74, 8, 71, 8}, {69, 16, 0, 0}, {66, 8, 69, 8}, {64, 16, 0, 0}}};   /* over G6, D/F# */
            const uint8_t *n = M[v - 1u][b];
            demo_note(&t->step[s0], n[0], 96);
            demo_hold(t, s0, n[1]);
            if (n[2]) {
                t->step[s0 + n[1] - 1u].flags |= SF_SLIDE;
                demo_note(&t->step[s0 + n[1]], n[2], 90);
                demo_hold(t, s0 + n[1], n[3]);
            }
            break;
        }
        case 4: {                                                           /* T5 the bass: rolling, off the kick */
            uint32_t r = DM_ROOT[v == 2u][b];
            for (i = 0; i < 16u; i++)
                if (i % 4u) {
                    demo_note(&t->step[s0 + i], r + (i % 4u == 3u ? 12u : 0u), i % 4u == 2u ? 110 : 90);
                }
            break;
        }
        case 5:                                                             /* T6 the clap, the hats */
            if (v != 3u)
                for (i = 0; i < 16u; i++)
                    demo_hit(t, s0 + i, 1u << 3, i % 4u == 2u ? 84 : i & 1u ? 30 : 48);
            if (v == 1u) {                                                  /* (half time: the snare and the clap on 3) */
                demo_hit(t, s0 + 8u, 1u << 1 | 1u << 2, 118);
                demo_hit(t, s0 + 14u, 1u << 1, 40);
                demo_hit(t, s0 + 6u, 1u << 4, 60);
            } else if (v == 3u)                                             /* the build: the snare rolling */
                for (i = 0; i < 16u; i += b < 2u ? 4u : b == 2u ? 2u : 1u)
                    demo_hit(t, s0 + i, 1u << 1, 40 + b * 20u + i * 2u);
            break;
        case 6:                                                             /* T7 the kick */
            if (v == 2u && b == 3u)
                break;                                                      /* (the bar before the drop: none) */
            demo_hit(t, s0, 1u << 0, 124);                                  /* (half time: 1, the and of 2) */
            demo_hit(t, s0 + 10u, 1u << 0, 108);
            break;
        default:                                                            /* T8 the riser (1), the fall (2) */
            if (!b) {
                demo_note(&t->step[0], 62, 100);
                demo_hold(t, 0, 64);
                demo_auto(t, P_EQ1F, 0, 56, v == 1u ? 40 : 110, v == 1u ? 112 : 30);
                demo_auto(t, P_LEVEL, 0, 56, v == 1u ? 40 : 110, v == 1u ? 112 : 0);
                trkx[7].curve = 1;
            }
            break;
        }
    }
    if (k == 0u) {                                                          /* the arpeggio's filter: its curve */
        demo_auto(t, P_EQHI, 0, 56, AR_CUT[v - 1u], AR_CUT[v]);
        (void)motion_set_event(t, 63, P_EQHI, AR_CUT[v]);
        trkx[0].curve = 1;
    }
}
static void felucca_demo(void)
{
    static const uint8_t SEC[16][8] = {
        {1, 1, 1, 0, 0, 0, 0, 0},             /* A: the chords held, the arpeggio in the dark, the air */
        {2, 1, 2, 0, 0, 0, 1, 0},             /* B: the kick */
        {3, 1, 1, 0, 1, 2, 1, 0},             /* the bass, the hats */
        {4, 1, 2, 0, 2, 1, 1, 0},             /* the clap */
        {5, 1, 1, 1, 1, 1, 1, 0},             /* the melody */
        {6, 1, 2, 2, 2, 1, 1, 0},
        {7, 1, 1, 1, 0, 0, 0, 0},             /* the break */
        {8, 1, 2, 0, 0, 3, 2, 1},             /* the build: the roll, the riser, the kick out before the drop */
        {9, 1, 1, 1, 1, 1, 1, 2},             /* the drop, wide open */
        {10, 1, 2, 2, 2, 1, 1, 0},
        {11, 1, 1, 1, 1, 1, 1, 0},
        {12, 1, 2, 2, 2, 1, 1, 0},
        {13, 1, 1, 0, 1, 2, 1, 0},            /* the bass, the kick, the hats */
        {14, 1, 2, 2, 2, 1, 1, 0},
        {15, 1, 1, 1, 0, 0, 0, 0},            /* down */
        {16, 1, 2, 0, 0, 0, 0, 0},            /* the arpeggio closing */
    };
    static const uint8_t NV[8] = {16, 1, 2, 2, 2, 3, 2, 2};
    static const uint8_t SND[8][2] = {{ENGI_FM6, 7}, {8, 3}, {ENGI_FM6, 4}, {2, 4}, {0, 2}, {ENGI_DRUM, 4}, {ENGI_DRUM, 2},
                                      {11, 0}};   /* FM6 PLUCK, GRAIN SHIMMER, FM6 PAD, PHASE BELL, ANALOG SQR BASS, LAYER KIT,
                                                   * TIGHT KIT, NOISE WIND */
    enum { DS_DIN = 200, DS_CIN, DS_COUT };
    static const int16_t DS[][3] = {          /* track (8 the MASTER), parameter (DS_*: the extras), value */
        {0, P_REV, 50}, {0, P_DLY, 40}, {0, P_SPRD, 80},                    /* the arpeggio */
        {0, P_DIST, 16}, {0, P_DCOL, 10},
        {1, P_ATK, 110}, {1, P_REL, 110}, {1, P_REV, 110},                  /* the air */
        {2, P_ATK, 100}, {2, P_REL, 110}, {2, P_SUS, 127}, {2, P_SPRD, 127}, {2, P_REV, 100}, {2, P_CHOR, 70},
        {2, P_GLIDE, 60},
        {2, P_ITYPE, IT_PHASER}, {2, P_IA, 6}, {2, P_IB, 70}, {2, P_IMIX, 60},   /* the chords: phased */
        {3, P_REV, 100}, {3, P_DLY, 80}, {3, P_PAN, -12},                   /* the motif: far in the echo */
        {4, P_VOICE, V_MONO}, {4, P_REV, 0}, {4, P_DLY, 0}, {4, P_CHOR, 0}, /* the bass: driven, glued */
        {4, P_DIST, 26}, {4, P_DCOL, -20}, {4, DS_DIN, 6},
        {5, P_REV, 40}, {5, P_DLY, 0}, {5, P_CHOR, 0}, {5, P_PAN, 6}, {5, DS_CIN, 10}, {5, DS_COUT, 4},
        {6, P_REV, 10}, {6, P_DLY, 0}, {6, P_CHOR, 0},                       /* the kick */
        {7, P_ATK, 60}, {7, P_REL, 100}, {7, P_SUS, 127}, {7, P_REV, 90}, {7, P_DLY, 40}, {7, P_SPRD, 127},
        /* the sounds alive: each its LFO (free, slow, synced to bars) */
        {0, P_LWAVE, 0}, {0, P_LSYNC, 2}, {0, P_LTRIG, 1}, {0, P_LD_FLT, 26},   /* arpeggio: its filter breathing */
        {1, P_LWAVE, 1}, {1, P_LSYNC, 1}, {1, P_LTRIG, 1},                  /* air */
        {2, P_LWAVE, 1}, {2, P_LSYNC, 2}, {2, P_LTRIG, 1}, {2, P_LD_SHP, 30}, {2, P_LD_FLT, 12},   /* chords: their timbre */
        {3, P_LWAVE, 0}, {3, P_LRATE, 70}, {3, P_LFADE, 80}, {3, P_LD_PIT, 3},   /* motif: a vibrato coming in */
        {4, P_ED_FLT, 30}, {4, P_LWAVE, 0}, {4, P_LSYNC, 3}, {4, P_LD_FLT, 10},   /* bass: its filter on each note */
        {7, P_LRATE, 40},   /* riser: round the room */
        {8, P_DIST, 8}, {8, P_EQLO, 8}, {8, P_EQ1F, 100}, {8, P_EQ1G, 6},    /* the MASTER */
    };
    static const int8_t EQ[8][6] = {          /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
        {44, 40, 80, 4, 112, 80},             /* arpeggio (its HIGH CUT the curve's) */
        {40, 110, 50, -4, 96, 60},            /* air */
        {40, 118, 50, -4, 122, 70},           /* chords */
        {48, 118, 80, 2, 116, 90},            /* motif */
        {0, 78, 18, 5, 108, 90},              /* bass */
        {30, 118, 90, 3, 110, 70},            /* kit */
        {0, 110, 18, 5, 114, 60},             /* kick */
        {30, 118, 40, 9, 96, 0},              /* riser (its bell the curve's) */
    };
    uint32_t i, k, v;
    song.g[G_BPM] = 80;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 110; song.g[G_RDAMP] = 70;
    song.g[G_DFDBK] = 60; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 40;   /* (1/8D) */
    song.g[G_CRATE] = 10; song.g[G_CDEPTH] = 80;
    song.g[G_MCOMP] = 56; song.g[G_MREL] = 60;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults_steps(t);
        t->p[P_SLEN] = 64;
        t->p[P_SDIV] = 2;
        demo_sound(t, SND[i][0], SND[i][1]);
        t->p[P_EQLO] = EQ[i][0]; t->p[P_EQHI] = EQ[i][1]; t->p[P_EQ1F] = EQ[i][2]; t->p[P_EQ1G] = EQ[i][3];
        t->p[P_LEVEL] = EQ[i][4];
        trkx[i].rsv0 = (uint8_t)EQ[i][5];
        trkx[i].rsv1 = 60;
        if (i != 6u && i != 5u && i != 7u) {  /* everything but the drums and the riser pumps on the kick */
            t->p[P_DKSRC] = 7;
            t->p[P_DKAMT] = i == 4u ? 76 : 84;
            t->p[P_DKREL] = -16;
        }
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
        uint32_t k2 = (uint32_t)DS[i][0], id = (uint32_t)DS[i][1];
        trkx_t *x = &trkx[k2 % NTRK];
        if (k2 == 8u) mtrk.p[id] = DS[i][2];
        else if (id == DS_DIN) x->din = (int8_t)DS[i][2];
        else if (id == DS_CIN) x->cin = (int8_t)DS[i][2];
        else if (id == DS_COUT) x->cout = (int8_t)DS[i][2];
        else trk[k2].p[id] = DS[i][2];
    }
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#elif DAH_DEMO_SONG == 4
/* (Dah) the long chords: 76 BPM in 1/16, E minor, sixteen sections of four bars from the SONG. A chord held for
 * sections on end (Em four sections, Cmaj7 four, Em again, G two, Em to the end), never still: inside it a voice
 * moves a step at a time and glides there (the chord's slide), the timbres turn (LFOs, the MOD slots), the
 * brightness one curve over the song (the pads' HIGH CUT automated, a variant a section). The beat half time, the
 * kick up front; the SIDECHAIN last on every chain that pumps:
 *   track 1  GRAIN CLOUD PAD, the drone (the root, the fifth), scanning   track 2  PHYS DRONE STRING, bowed under it
 *   track 3  FM6 PAD, the chord, its voices moving   track 4  VOICE CHOIR AAH, the chord above it, breathing
 *   track 5  ANALOG SUB BASS, the root held   track 6  DRUM HARD KIT, the snare, the clap, the hats
 *   track 7  DRUM DEEP KIT, the kick   track 8  PHASE BELL, a few notes, far in the echo */
/* the chord regions (E C E G E), each four bars of four voices, a voice stepping each bar (gliding): two shapes each */
static const uint8_t LC_CH[6][4][4] = {
    {{52, 59, 64, 67}, {52, 59, 64, 69}, {52, 59, 66, 71}, {52, 59, 64, 67}},   /* Em: G A B G on top */
    {{52, 59, 62, 67}, {52, 59, 62, 66}, {52, 57, 62, 66}, {52, 59, 64, 67}},   /* Em7: the seventh, the ninth */
    {{48, 55, 59, 64}, {48, 55, 59, 66}, {48, 55, 60, 67}, {48, 55, 59, 64}},   /* Cmaj7: the #11 */
    {{48, 55, 62, 64}, {48, 55, 62, 66}, {48, 57, 62, 66}, {48, 55, 59, 64}},   /* Cmaj9 */
    {{43, 50, 59, 62}, {43, 50, 59, 64}, {43, 50, 57, 62}, {43, 50, 59, 62}},   /* G: the sixth, the ninth */
    {{43, 50, 55, 62}, {43, 50, 57, 64}, {43, 50, 59, 66}, {43, 50, 59, 62}}};  /* Gmaj7 rising */
static const uint8_t LC_ROOT[6] = {28, 28, 36, 36, 31, 31};   /* (E1, C2, G1) */
/* each section: its shape (LC_CH), the pads' HIGH CUT at its start (and the end's) */
static const uint8_t LC_SEC[16] = {0, 1, 0, 1, 2, 3, 2, 3, 0, 1, 0, 1, 4, 5, 0, 1};
static const uint8_t LC_CUT[17] = {44, 52, 60, 68, 74, 80, 86, 70, 96, 104, 110, 116, 100, 90, 76, 60, 44};
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        switch (k) {
        case 0: case 1: case 4: {                                           /* the drone, the string, the bass: held */
            uint32_t r = LC_ROOT[v - 1u];
            if (b)
                break;
            if (k == 4u) {
                demo_note(&t->step[0], r + 12u, 100);
            } else {
                uint8_t d[2] = {(uint8_t)(r + (k ? 12u : 24u)), (uint8_t)(r + (k ? 19u : 31u))};
                dm_chord(t, 0, 64, d, 2, 90);
                if (k == 0u) {
                    demo_auto(t, P_E0, 0, 56, 10 + v * 12u, 30 + v * 12u);   /* (its grains: a part of the sample each) */
                    demo_auto(t, P_E2, 0, 56, 40, 100);
                }
                break;
            }
            demo_hold(t, 0, 64);
            break;
        }
        case 2: case 3: {                                                   /* the chord, its voices moving */
            const uint8_t *c = LC_CH[LC_SEC[v - 1u]][b];
            uint8_t up[4];
            for (i = 0; i < 4u; i++)
                up[i] = (uint8_t)(c[i] + (k == 3u ? 12u : 0u));
            dm_chord(t, s0, 16, up, 4, k == 3u ? 76 : 90);
            t->step[s0 + 15u].flags |= SF_SLIDE;                            /* (into the next bar: the voice that moves
                                                                             * glides, the others hold) */
            break;
        }
        case 5:                                                             /* T6 half time: the snare and the clap on 3 */
            for (i = 0; i < 16u; i += 2u)
                demo_hit(t, s0 + i, 1u << 3, i % 4u ? 36 : 56);
            demo_hit(t, s0 + 8u, 1u << 1 | 1u << 2, v == 2u ? 124 : 116);
            if (v == 2u && b == 3u)
                for (i = 12; i < 16u; i++)
                    demo_hit(t, s0 + i, 1u << 1, 70 + i * 3u);
            break;
        case 6:                                                             /* T7 the kick: 1, the and of 2 */
            demo_hit(t, s0, 1u << 0, 127);
            demo_hit(t, s0 + 10u, 1u << 0, 112);
            break;
        default: {                                                          /* T8 a few notes, far */
            static const uint8_t N[2][4] = {{71, 0, 67, 0}, {74, 0, 72, 71}};
            if (N[v - 1u][b]) {
                demo_note(&t->step[s0 + 4u], N[v - 1u][b], 90);
                demo_hold(t, s0 + 4u, 8);
            }
            break;
        }
        }
    }
    if (k == 2u || k == 3u) {                                               /* the pads' brightness: one curve */
        demo_auto(t, P_EQHI, 0, 56, LC_CUT[v - 1u], LC_CUT[v]);
        (void)motion_set_event(t, 63, P_EQHI, LC_CUT[v]);
        trkx[k].curve = 1;
    }
}
static void felucca_demo(void)
{
    /* the drone, string, bass: a region each (1 E, 3 C, 5 G); the pads: a variant each section (its shape, its curve) */
    static const uint8_t SEC[16][8] = {
        {1, 0, 1, 0, 0, 0, 0, 0},             /* E: the drone, the chord, out of nothing */
        {1, 1, 2, 0, 0, 0, 0, 0},             /* the string */
        {1, 1, 3, 3, 1, 0, 0, 0},             /* the choir, the bass */
        {1, 1, 4, 4, 1, 0, 1, 1},             /* the kick */
        {3, 3, 5, 5, 3, 1, 1, 0},             /* C: the beat */
        {3, 3, 6, 6, 3, 1, 1, 2},
        {3, 3, 7, 7, 3, 1, 1, 1},
        {3, 3, 8, 8, 3, 2, 1, 0},             /* the roll */
        {1, 1, 9, 9, 1, 1, 1, 2},             /* E: open */
        {1, 1, 10, 10, 1, 1, 1, 1},
        {1, 1, 11, 11, 1, 1, 1, 0},
        {1, 1, 12, 12, 1, 2, 1, 2},
        {5, 5, 13, 13, 5, 1, 1, 1},           /* G: lifting */
        {5, 5, 14, 14, 5, 2, 1, 2},
        {1, 1, 15, 15, 1, 0, 1, 0},           /* E: down */
        {1, 0, 16, 0, 0, 0, 0, 0},            /* the chord alone, closing */
    };
    static const uint8_t NV[8] = {5, 5, 16, 16, 5, 2, 1, 2};
    static const uint8_t SND[8][2] = {{8, 0}, {ENGI_PHYS, 7}, {ENGI_FM6, 4}, {5, 0}, {0, 7}, {ENGI_DRUM, 5},
                                      {ENGI_DRUM, 1}, {2, 4}};   /* CLOUD PAD, DRONE STRING, FM6 PAD, CHOIR AAH, SUB
                                                                 * BASS, HARD KIT, DEEP KIT, PHASE BELL */
    enum { DS_DIN = 200, DS_CIN, DS_COUT };
    static const int16_t DS[][3] = {          /* track (8 the MASTER), parameter (DS_*: the extras), value */
        {0, P_ATK, 110}, {0, P_REL, 120}, {0, P_REV, 110}, {0, P_SPRD, 127},           /* the drone */
        {0, P_LWAVE, 1}, {0, P_LSYNC, 1}, {0, P_LTRIG, 1},
        {1, P_ATK, 90}, {1, P_REL, 110}, {1, P_SUS, 127}, {1, P_SPRD, 100}, {1, P_REV, 80},   /* the string, phased */
        {1, P_ITYPE, IT_PHASER}, {1, P_IA, 4}, {1, P_IB, 80}, {1, P_IMIX, 70},
        {2, P_ATK, 100}, {2, P_REL, 120}, {2, P_SUS, 127}, {2, P_SPRD, 127}, {2, P_GLIDE, 90},   /* the chord: */
        {2, P_REV, 100}, {2, P_CHOR, 80}, {2, P_DLY, 20},
        {2, P_ITYPE, IT_CHORUS}, {2, P_IA, 8}, {2, P_IB, 90}, {2, P_IMIX, 60},     /* .. wide, its timbre turning */
        {2, P_LWAVE, 1}, {2, P_LSYNC, 1}, {2, P_LTRIG, 1}, {2, P_LD_SHP, 40}, {2, P_LD_FLT, 16},
        {3, P_ATK, 110}, {3, P_REL, 120}, {3, P_SPRD, 110}, {3, P_GLIDE, 90}, {3, P_REV, 120}, {3, P_CHOR, 60},
        {3, P_LWAVE, 0}, {3, P_LSYNC, 2}, {3, P_LTRIG, 1}, {3, P_LD_FLT, 20}, {3, P_LD_AMP, 30},   /* the choir: */
        {4, P_VOICE, V_MONO}, {4, P_ATK, 60}, {4, P_SUS, 127}, {4, P_REL, 90}, {4, P_REV, 0}, {4, P_DLY, 0},
        {4, P_CHOR, 0}, {4, P_LWAVE, 0}, {4, P_LSYNC, 1}, {4, P_LD_FLT, 14},
        {5, P_REV, 50}, {5, P_DLY, 10}, {5, P_CHOR, 0}, {5, DS_CIN, 12}, {5, DS_COUT, 6},   /* the kit: hit hard */
        {5, P_DIST, 12},
        {6, P_REV, 14}, {6, P_DLY, 0}, {6, P_CHOR, 0}, {6, DS_CIN, 16}, {6, DS_COUT, 16},   /* the kick: up front */
        {6, P_DIST, 18}, {6, P_E0 + 1, 34}, {6, P_E0 + 2, 70},
        {7, P_REV, 110}, {7, P_DLY, 90}, {7, P_PAN, -14}, {7, P_LWAVE, 0}, {7, P_LRATE, 66}, {7, P_LFADE, 90},
        {7, P_LD_PIT, 3},
        {8, P_DIST, 10}, {8, P_EQLO, 6}, {8, P_EQ1F, 100}, {8, P_EQ1G, 8},
    };
    static const int8_t EQ[8][6] = {          /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
        {44, 104, 60, -3, 96, 60},            /* drone */
        {30, 110, 50, -3, 116, 80},           /* string */
        {40, 60, 60, -3, 120, 70},            /* chord (its HIGH CUT the curve's) */
        {50, 60, 80, 3, 92, 80},              /* choir (.. too) */
        {0, 60, 20, 4, 98, 90},               /* bass */
        {30, 118, 90, 4, 120, 80},            /* kit */
        {0, 118, 18, 6, 126, 70},             /* kick */
        {48, 118, 80, 2, 108, 70},            /* bell */
    };
    uint32_t i, k, v;
    song.g[G_BPM] = 76;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 124; song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 70; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 40;   /* (1/8D) */
    song.g[G_CRATE] = 8; song.g[G_CDEPTH] = 100;
    song.g[G_MCOMP] = 50; song.g[G_MREL] = 70;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults_steps(t);
        t->p[P_SLEN] = 64;
        t->p[P_SDIV] = 2;
        demo_sound(t, SND[i][0], SND[i][1]);
        t->p[P_EQLO] = EQ[i][0]; t->p[P_EQHI] = EQ[i][1]; t->p[P_EQ1F] = EQ[i][2]; t->p[P_EQ1G] = EQ[i][3];
        t->p[P_LEVEL] = EQ[i][4];
        trkx[i].rsv0 = (uint8_t)EQ[i][5];
        trkx[i].rsv1 = 60;
        if (i < 5u) {                         /* the drone, string, chord, choir, bass pump on the kick (last) */
            t->p[P_DKSRC] = 7;
            t->p[P_DKAMT] = i == 4u ? 70 : 60;
            t->p[P_DKREL] = 10;
        }
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
        uint32_t k2 = (uint32_t)DS[i][0], id = (uint32_t)DS[i][1];
        trkx_t *x = &trkx[k2 % NTRK];
        if (k2 == 8u) mtrk.p[id] = DS[i][2];
        else if (id == DS_DIN) x->din = (int8_t)DS[i][2];
        else if (id == DS_CIN) x->cin = (int8_t)DS[i][2];
        else if (id == DS_COUT) x->cout = (int8_t)DS[i][2];
        else trk[k2].p[id] = DS[i][2];
    }
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#elif DAH_DEMO_SONG == 5
/* (Dah) the dark one: 72 BPM in 1/16, B minor, sixteen sections of four bars from the SONG. Huge chords (two pads, eight
 * voices) held a section each: Bm(add9) G(maj7 #11) Em9 F#7(b9), at the end C over B, a cluster, resolving to Bm. Over
 * them a guitar of a synth (driven, fuzzed) sliding slowly into its notes, a light vibrato late in them; the beat half time, heavy:
 *   track 1  GRAIN CLOUD PAD, the drone   track 2  PHYS DRONE STRING, bowed   track 3  FM6 PAD, the chord low
 *   track 4  ANALOG STRINGS, the chord high   track 5  ANALOG SUB BASS   track 6  DRUM HARD KIT, the snare, the hats
 *   track 7  DRUM DEEP KIT, the kick   track 8  ANALOG SAW LEAD, the guitar */
static const uint8_t DK_LO[5][4] = {{47, 54, 59, 62}, {43, 50, 59, 66}, {40, 47, 55, 62}, {42, 49, 58, 64}, {47, 48, 55, 64}};
static const uint8_t DK_HI[5][4] = {{66, 71, 73, 74}, {66, 71, 73, 74}, {66, 67, 71, 74}, {67, 70, 73, 76}, {67, 71, 72, 76}};
static const uint8_t DK_ROOT[5] = {35, 31, 28, 30, 35};   /* Bm G Em F#7(b9) C/B */
/* the guitar's phrases: note (0 a rest), steps, slide into the next */
static const uint8_t DK_GT[5][10][3] = {
    {{71, 8, 1}, {74, 4, 1}, {73, 4, 0}, {71, 16, 0}, {0, 4, 0}, {66, 4, 1}, {69, 4, 1}, {71, 4, 1}, {73, 16, 0}},
    {{74, 8, 1}, {73, 4, 1}, {71, 4, 0}, {67, 16, 1}, {66, 16, 0}, {0, 16, 0}},
    {{67, 4, 1}, {71, 4, 1}, {74, 8, 0}, {76, 8, 1}, {74, 8, 0}, {71, 16, 1}, {67, 16, 0}},
    {{70, 8, 1}, {73, 8, 1}, {76, 16, 0}, {78, 8, 1}, {76, 8, 1}, {73, 16, 0}},
    {{72, 16, 1}, {71, 48, 0}}};
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t i, r = (v - 1u) % 5u;
    track_t *t = &trk[k];
    switch (k) {
    case 0: case 1: {                                                       /* the drone, the string: root, fifth */
        uint8_t d[2] = {(uint8_t)(DK_ROOT[r] + (k ? 12u : 24u)), (uint8_t)(DK_ROOT[r] + (k ? 19u : 31u))};
        if (r == 3u) d[1] = (uint8_t)(DK_ROOT[r] + (k ? 13u : 25u));        /* (F#: its b9 groaning) */
        dm_chord(t, 0, 64, d, 2, 90);
        if (k == 0u)
            demo_auto(t, P_E0, 0, 56, 10 + r * 22u, 30 + r * 22u);
        break;
    }
    case 2: case 3:                                                         /* the chord, held */
        dm_chord(t, 0, 64, k == 2u ? DK_LO[r] : DK_HI[r], 4, k == 2u ? 96 : 80);
        if (r == 4u) {                                                      /* (the cluster: brighter, swelling, then
                                                                             * each voice gliding into Bm: the slide) */
            demo_auto(t, P_EQHI, 0, 56, 70, 110);
            trkx[k].curve = 1;
            t->step[63].flags |= SF_SLIDE;
        }
        break;
    case 4:                                                                 /* the sub */
        demo_note(&t->step[0], DK_ROOT[r] + 12u, 100);
        demo_hold(t, 0, 64);
        break;
    case 5:                                                                 /* the snare on 3, the hats; 2: the roll */
        for (i = 0; i < 64u; i++) {
            uint32_t b = i % 16u;
            if (v == 2u) {
                if (i >= 32u && (i % (i >= 48u ? 1u : 2u)) == 0u)
                    demo_hit(t, i, 1u << 1, 40 + (i - 32u) * 2u);
            } else {
                if (!(b & 1u))
                    demo_hit(t, i, 1u << 3, b % 4u ? 34 : 54);
                if (b == 8u)
                    demo_hit(t, i, 1u << 1 | 1u << 2, 124);
            }
        }
        break;
    case 6:                                                                 /* the kick; 2: one hit, the cluster's */
        for (i = 0; i < 64u; i += 16u) {
            demo_hit(t, i, 1u << 0, 127);
            if (v == 1u) {
                demo_hit(t, i + 10u, 1u << 0, 110);
                if ((i / 16u) & 1u)
                    demo_hit(t, i + 14u, 1u << 0, 90);
            } else
                break;
        }
        break;
    default: {                                                              /* the guitar */
        uint32_t at = 0;
        for (i = 0; i < 10u && DK_GT[r][i][1]; i++) {
            const uint8_t *g = DK_GT[r][i];
            if (g[0]) {
                demo_note(&t->step[at], g[0], 104);
                demo_hold(t, at, g[1]);
                if (g[2])
                    t->step[at + g[1] - 1u].flags |= SF_SLIDE;
            }
            at += g[1];
        }
        break;
    }
    }
}
static void felucca_demo(void)
{
    /* each track's variant: the region (1 Bm, 2 G, 3 Em, 4 F#7b9, 5 C/B); 0 silent */
    static const uint8_t SEC[16][8] = {
        {1, 0, 1, 0, 0, 0, 0, 0},             /* out of nothing: the drone, the chord low */
        {1, 1, 1, 1, 0, 0, 0, 0},             /* the string, the chord high */
        {1, 1, 1, 1, 1, 0, 0, 1},             /* the sub, the guitar */
        {2, 2, 2, 2, 2, 0, 1, 2},             /* G: the kick */
        {1, 1, 1, 1, 1, 1, 1, 1},             /* Bm: the beat */
        {2, 2, 2, 2, 2, 1, 1, 2},
        {3, 3, 3, 3, 3, 1, 1, 3},             /* Em */
        {4, 4, 4, 4, 4, 2, 1, 4},             /* F#7(b9): the roll */
        {1, 1, 1, 1, 1, 1, 1, 1},             /* again, heavier */
        {2, 2, 2, 2, 2, 1, 1, 2},
        {3, 3, 3, 3, 3, 1, 1, 3},
        {4, 4, 4, 4, 4, 2, 1, 4},
        {1, 1, 1, 1, 0, 0, 0, 1},             /* down: the chords, the guitar */
        {4, 4, 4, 4, 4, 0, 0, 0},             /* the tension */
        {5, 5, 5, 5, 5, 0, 2, 5},             /* C over B: the cluster, one hit */
        {1, 1, 1, 1, 1, 0, 0, 0},             /* resolved: Bm */
    };
    static const uint8_t NV[8] = {5, 5, 5, 5, 5, 2, 2, 5};
    static const uint8_t SND[8][2] = {{8, 0}, {ENGI_PHYS, 7}, {ENGI_FM6, 4}, {0, 11}, {0, 7}, {ENGI_DRUM, 5},
                                      {ENGI_DRUM, 1}, {0, 0}};
    enum { DS_DIN = 200, DS_CIN, DS_COUT };
    static const int16_t DS[][3] = {          /* track (8 the MASTER), parameter (DS_*: the extras), value */
        {0, P_ATK, 110}, {0, P_REL, 120}, {0, P_REV, 110}, {0, P_SPRD, 127},
        {0, P_LWAVE, 1}, {0, P_LSYNC, 1}, {0, P_LTRIG, 1},
        {1, P_ATK, 90}, {1, P_REL, 110}, {1, P_SUS, 127}, {1, P_SPRD, 100}, {1, P_REV, 90},
        {1, P_ITYPE, IT_PHASER}, {1, P_IA, 4}, {1, P_IB, 80}, {1, P_IMIX, 60},
        {2, P_ATK, 90}, {2, P_REL, 120}, {2, P_SUS, 127}, {2, P_SPRD, 127}, {2, P_REV, 100}, {2, P_CHOR, 70},
        {2, P_GLIDE, 110}, {3, P_GLIDE, 110},                                /* (the resolution: slow glides) */
        {2, P_ITYPE, IT_CHORUS}, {2, P_IA, 6}, {2, P_IB, 90}, {2, P_IMIX, 50},
        {2, P_LWAVE, 1}, {2, P_LSYNC, 1}, {2, P_LTRIG, 1}, {2, P_LD_SHP, 36}, {2, P_LD_FLT, 14},
        {3, P_ATK, 110}, {3, P_REL, 120}, {3, P_SUS, 127}, {3, P_SPRD, 127}, {3, P_REV, 110}, {3, P_CHOR, 60},
        {3, P_LWAVE, 0}, {3, P_LSYNC, 2}, {3, P_LTRIG, 1}, {3, P_LD_FLT, 24},
        {4, P_VOICE, V_MONO}, {4, P_ATK, 60}, {4, P_SUS, 127}, {4, P_REL, 90}, {4, P_REV, 0}, {4, P_DLY, 0},
        {4, P_CHOR, 0},
        {5, P_REV, 70}, {5, P_DLY, 10}, {5, P_CHOR, 0}, {5, DS_CIN, 12}, {5, DS_COUT, 8}, {5, P_DIST, 14},
        {6, P_REV, 20}, {6, P_DLY, 0}, {6, P_CHOR, 0}, {6, DS_CIN, 16}, {6, DS_COUT, 16}, {6, P_DIST, 20},
        {6, P_E0 + 1, 30}, {6, P_E0 + 2, 80},
        {7, P_VOICE, V_MONO}, {7, P_GLIDE, 96}, {7, P_ATK, 4}, {7, P_SUS, 110}, {7, P_REL, 80},   /* the guitar: */
        {7, P_DIST, 64}, {7, P_DCOL, -16}, {7, DS_DIN, 10},                 /* .. driven, fuzzed, */
        {7, P_ITYPE, IT_FUZZ}, {7, P_IA, 40}, {7, P_IMIX, 40},
        {7, P_LWAVE, 0}, {7, P_LRATE, 70}, {7, P_LFADE, 127}, {7, P_LD_PIT, 1},   /* .. a light vibrato, late, */
        {7, P_REV, 90}, {7, P_DLY, 70}, {7, P_PAN, -8},                      /* .. far in the echo */
        {8, P_DIST, 10}, {8, P_EQLO, 6}, {8, P_EQ1F, 100}, {8, P_EQ1G, 6},
    };
    static const int8_t EQ[8][6] = {          /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
        {44, 100, 60, -3, 90, 60},            /* drone */
        {30, 104, 50, -3, 124, 80},           /* string */
        {30, 96, 50, -4, 104, 70},            /* chord low */
        {50, 100, 80, 2, 100, 80},            /* chord high */
        {0, 60, 20, 4, 104, 90},              /* sub */
        {30, 118, 90, 4, 120, 80},            /* kit */
        {0, 118, 18, 6, 126, 70},             /* kick */
        {40, 100, 70, 4, 112, 80},            /* guitar */
    };
    uint32_t i, k, v;
    song.g[G_BPM] = 72;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 124; song.g[G_RDAMP] = 70;
    song.g[G_DFDBK] = 70; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 40;
    song.g[G_CRATE] = 8; song.g[G_CDEPTH] = 100;
    song.g[G_MCOMP] = 54; song.g[G_MREL] = 70;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults_steps(t);
        t->p[P_SLEN] = 64;
        t->p[P_SDIV] = 2;
        demo_sound(t, SND[i][0], SND[i][1]);
        t->p[P_EQLO] = EQ[i][0]; t->p[P_EQHI] = EQ[i][1]; t->p[P_EQ1F] = EQ[i][2]; t->p[P_EQ1G] = EQ[i][3];
        t->p[P_LEVEL] = EQ[i][4];
        trkx[i].rsv0 = (uint8_t)EQ[i][5];
        trkx[i].rsv1 = 60;
        if (i < 5u) {                         /* the drone, string, chords, sub pump on the kick (last) */
            t->p[P_DKSRC] = 7;
            t->p[P_DKAMT] = i == 4u ? 66 : 56;
            t->p[P_DKREL] = 10;
        }
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
        uint32_t k2 = (uint32_t)DS[i][0], id = (uint32_t)DS[i][1];
        trkx_t *x = &trkx[k2 % NTRK];
        if (k2 == 8u) mtrk.p[id] = DS[i][2];
        else if (id == DS_DIN) x->din = (int8_t)DS[i][2];
        else if (id == DS_CIN) x->cin = (int8_t)DS[i][2];
        else if (id == DS_COUT) x->cout = (int8_t)DS[i][2];
        else trk[k2].p[id] = DS[i][2];
    }
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#elif DAH_DEMO_SONG == 6
/* (Dah) after Hans Zimmer's Time (its chords from a MIDI file): 140 BPM in 1/16, A minor, Am Em G D two bars each
 * (the second time Em with B on top), sixteen sections of four bars from the SONG, out of a piano alone to the brass and
 * the drums, down to the piano again on Am; eight voices at most at once (the budget):
 *   track 1  SAMPLE PIANO, the chord picked   track 2  ANALOG STRINGS, held   track 3  PHYS DRONE STRING, the roots
 *   track 4  ANALOG SUB BASS   track 5  FM6 PLUCK, the eighths   track 6  FM6 BRASS, low   track 7  DRUM DEEP KIT
 *   track 8  VOICE CHOIR AAH, the top */
static const uint8_t TM_CH[5][3] = {{45, 57, 72}, {52, 64, 79}, {43, 55, 71}, {50, 62, 78}, {52, 64, 83}};   /* root, its
                                                                             * octave, the top: Am Em G D Em(B) */
static const uint8_t TM_RG[4][2] = {{0, 1}, {2, 3}, {0, 4}, {0, 0}};   /* the regions: a chord a half */
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t h, i;
    track_t *t = &trk[k];
    if (k == 6u) {                                                          /* the drums: 1 the hits, 2 the climax,
                                                                             * 3 the build (the roll) */
        for (i = 0; i < 64u; i += 16u) {                                    /* (a hit at a time: the voices) */
            demo_hit(t, i, 1u << 0, v != 1u || i % 32u == 0u ? 127 : 84);
            if (v == 2u) {
                demo_hit(t, i + 8u, 1u << 1, 124);                          /* (half time: the snare on 3) */
                demo_hit(t, i + 11u, 1u << 0, 100);
            }
        }
        for (i = 32u; v == 3u && i < 64u; i += i >= 56u ? 1u : i >= 48u ? 2u : 4u)
            demo_hit(t, i, 1u << 1, 50 + (i - 32u) * 2u);
        return;
    }
    for (h = 0; h < 2u; h++) {
        const uint8_t *c = TM_CH[TM_RG[(v - 1u) % 4u][h]];
        uint32_t at = h * 32u, len = v == 4u ? 64u : 32u;
        uint8_t n[4];
        if (v == 4u && h)
            break;                                                          /* (the end: Am, four bars) */
        switch (k) {
        case 0:                                                             /* the piano: quarters up and down */
            for (i = 0; i < len; i += 4u) {
                static const uint8_t UP[4] = {1, 3, 2, 3};
                uint32_t u = UP[i / 4u % 4u];
                demo_note(&t->step[at + i], u == 1u ? c[1] : u == 2u ? c[2] : c[1] + 7u, i % 16u ? 70 : 88);
                demo_hold(t, at + i, 4);
            }
            break;
        case 1:                                                             /* the strings: the chord held */
            n[0] = c[1]; n[1] = (uint8_t)(c[1] + 7u); n[2] = c[2];
            dm_chord(t, at, len, n, 3, 84);
            break;
        case 2:                                                             /* the drone: the root */
            dm_chord(t, at, len, c, 1, 96);
            break;
        case 3:                                                             /* the sub */
            demo_note(&t->step[at], c[0] - 12u, 100);
            demo_hold(t, at, len);
            break;
        case 4:                                                             /* the eighths */
            for (i = 0; i < len; i += 2u) {
                static const uint8_t ARP[4] = {1, 3, 2, 3};
                uint32_t u = ARP[i / 2u % 4u];
                demo_note(&t->step[at + i], u == 1u ? c[1] : u == 2u ? c[2] : c[1] + 7u, i % 8u ? 72 : 100);
                demo_hold(t, at + i, 2);
            }
            break;
        case 5:                                                             /* the brass, low: the root */
            dm_chord(t, at, len, c, 1, 120);
            break;
        default:                                                            /* the choir: the top */
            dm_chord(t, at, len, c + 2, 1, 100);
            break;
        }
    }
}
static void felucca_demo(void)
{
    /* each track's variant: the region (1 Am Em, 2 G D, 3 Am Em(B), 4 Am held; the drums 1 hits, 2 climax, 3 build);
     * 0 silent */
    static const uint8_t SEC[16][8] = {
        {1, 0, 0, 0, 0, 0, 0, 0},             /* the piano alone */
        {2, 0, 0, 0, 0, 0, 0, 0},
        {3, 3, 0, 0, 0, 0, 0, 0},             /* the strings */
        {2, 2, 2, 0, 0, 0, 0, 0},             /* the drone */
        {1, 1, 1, 0, 1, 0, 0, 0},             /* the eighths */
        {2, 2, 0, 2, 2, 0, 1, 0},             /* the sub (for the drone), the first hits */
        {3, 3, 0, 3, 3, 0, 1, 0},
        {0, 2, 0, 2, 2, 0, 3, 2},             /* the choir (for the piano), the build */
        {0, 1, 0, 0, 1, 1, 2, 1},             /* the brass, the drums (the piano, the sub resting: eight voices) */
        {0, 2, 0, 0, 2, 2, 2, 2},
        {0, 3, 0, 0, 3, 3, 2, 3},
        {0, 2, 0, 0, 2, 2, 3, 2},             /* the build again */
        {0, 1, 0, 0, 1, 1, 2, 1},
        {0, 2, 0, 0, 2, 2, 2, 2},
        {3, 3, 3, 0, 0, 0, 0, 3},             /* down */
        {4, 4, 0, 0, 0, 0, 0, 0},             /* the piano, Am */
    };
    static const uint8_t NV[8] = {4, 4, 3, 3, 3, 3, 3, 3};
    static const uint8_t SND[8][2] = {{ENGI_SAMPLE, 0}, {0, 11}, {ENGI_PHYS, 7}, {0, 7}, {ENGI_FM6, 7}, {ENGI_FM6, 3},
                                      {ENGI_DRUM, 1}, {5, 0}};
    static const int16_t DS[][3] = {          /* track (8 the MASTER), parameter, value */
        {0, P_REV, 90}, {0, P_REL, 100},
        {1, P_ATK, 100}, {1, P_REL, 96}, {1, P_SUS, 127}, {1, P_SPRD, 127}, {1, P_REV, 110}, {1, P_CHOR, 40},
        {2, P_ATK, 90}, {2, P_REL, 110}, {2, P_SUS, 127}, {2, P_REV, 90},
        {3, P_VOICE, V_MONO}, {3, P_ATK, 60}, {3, P_SUS, 127}, {3, P_REL, 90}, {3, P_REV, 0}, {3, P_DLY, 0},
        {3, P_CHOR, 0},
        {4, P_VOICE, V_MONO}, {4, P_REV, 70}, {4, P_DLY, 50}, {4, P_PAN, 10},   /* (the eighths: one voice) */
        {5, P_ATK, 70}, {5, P_REL, 100}, {5, P_SUS, 127}, {5, P_SPRD, 100}, {5, P_REV, 100}, {5, P_DIST, 10},
        {6, P_REV, 80}, {6, P_DLY, 0}, {6, P_CHOR, 0}, {6, P_DIST, 16},
        {7, P_ATK, 100}, {7, P_REL, 120}, {7, P_SUS, 127}, {7, P_REV, 120}, {7, P_SPRD, 127},
        {8, P_EQLO, 6}, {8, P_EQ1F, 100}, {8, P_EQ1G, 4},
    };
    static const int8_t EQ[8][6] = {          /* LOW CUT, HIGH CUT (118: open), a bell; the level; the COMPRESSOR */
        {20, 118, 60, 0, 112, 60},            /* piano */
        {40, 104, 70, 2, 100, 70},            /* strings */
        {20, 100, 40, 0, 104, 70},            /* drone */
        {0, 60, 20, 2, 100, 90},              /* sub */
        {40, 110, 80, 2, 84, 70},             /* eighths */
        {20, 96, 40, 3, 108, 80},             /* brass */
        {0, 118, 18, 6, 126, 70},             /* drums */
        {44, 104, 80, 2, 96, 70},             /* choir */
    };
    uint32_t i, k, v;
    song.g[G_BPM] = 140;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 127; song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 50; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 40;
    song.g[G_CRATE] = 8; song.g[G_CDEPTH] = 80;
    song.g[G_MCOMP] = 50; song.g[G_MREL] = 70;
    song.ntrk = 8;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults_steps(t);
        t->p[P_SLEN] = 64;
        t->p[P_SDIV] = 2;
        demo_sound(t, SND[i][0], SND[i][1]);
        t->p[P_EQLO] = EQ[i][0]; t->p[P_EQHI] = EQ[i][1]; t->p[P_EQ1F] = EQ[i][2]; t->p[P_EQ1G] = EQ[i][3];
        t->p[P_LEVEL] = EQ[i][4];
        trkx[i].rsv0 = (uint8_t)EQ[i][5];
        trkx[i].rsv1 = 60;
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++) {
        uint32_t k2 = (uint32_t)DS[i][0];
        if (k2 == 8u) mtrk.p[DS[i][1]] = DS[i][2];
        else trk[k2].p[DS[i][1]] = DS[i][2];
    }
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 16;
    for (i = 0; i < 16u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = 1;
    }
}
#elif DAH_DEMO_SONG == 7
/* (Dah) the showcase: 122 BPM in 1/16, A minor (Am F C G, the break F G Em Am), fifteen sections of four bars from the
 * SONG (2:20): the intro, the verse, the build (the pad's filter opening, CURVE RAMP; the snare rolling), the drop, the
 * break, the build again, the drops, the outro. Every track its four EQ filters, the MASTER its own:
 *   track 1  DRUM DEEP KIT, the kick (COMP; the SIDECHAIN's source)   track 2  DRUM TIGHT KIT, the clap and the hats (COMP)
 *   track 3  ANALOG SQR BASS, off the beat, rolling (DIST, COMP, ducked; its CUT automated)   track 4  ANALOG SOFT PAD,
 *   the chords (WIDTH, ducked, CHORUS; CHRD DIA3 on the keys)   track 5  TRIO ARP LEAD through the ARP (UPDN, 1/16)
 *   track 6  FM6 PLUCK, the riff into the DELAY   track 7  VOICE VOX LEAD, the hook   track 8  GRAIN SHIMMER, the air
 *   (PHASER; its POS automated) */
static const uint8_t SC_CH[5][3] = {{57, 60, 64}, {57, 60, 65}, {55, 60, 64}, {55, 59, 62}, {55, 59, 64}};   /* Am F C G Em */
static const uint8_t SC_RT[5] = {45, 41, 48, 43, 40};
static const uint8_t SC_PR[2][4] = {{0, 1, 2, 3}, {1, 3, 4, 0}};
static const uint8_t SC_HK[24][3] = {          /* the hook: step, note, length (17 common, 3 to end the first, 4 the second) */
    {0, 76, 3}, {3, 76, 3}, {6, 74, 2}, {8, 72, 4}, {12, 74, 2}, {14, 76, 2},
    {16, 69, 6}, {22, 72, 2}, {24, 69, 4}, {28, 67, 2}, {30, 69, 2},
    {32, 67, 3}, {35, 67, 3}, {38, 76, 2}, {40, 74, 4}, {44, 72, 2}, {46, 74, 2},
    {48, 74, 6}, {54, 71, 2}, {56, 67, 8},
    {48, 71, 4}, {52, 72, 4}, {56, 74, 4}, {60, 76, 4}};
static void demo_var(uint32_t k, uint32_t v)
{
    uint32_t b, i;
    track_t *t = &trk[k];
    if (k == 6u) {                                                          /* the hook */
        for (i = 0; i < 24u; i++)
            if (i < 17u || (v == 1u ? i < 20u : i >= 20u)) {
                demo_note(&t->step[SC_HK[i][0]], SC_HK[i][1], i % 3u ? 92 : 108);
                demo_hold(t, SC_HK[i][0], SC_HK[i][2]);
            }
        return;
    }
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u, c0 = SC_PR[v == (k == 2u ? 3u : 2u)][b], r = SC_RT[c0];
        const uint8_t *c = SC_CH[c0];
        switch (k) {
        case 0:                                                             /* the kick: four on the floor (2: the bar
                                                                             * before the drop none) */
            for (i = 0; v < 2u || b < 3u ? i < 16u : 0; i += 4u)
                demo_hit(t, s0 + i, 1u << 0, i ? 112 : 124);
            break;
        case 1:                                                             /* the kit */
            if (v == 3u) {                                                  /* the build: the snare rolling */
                for (i = 0; i < 16u; i += b < 2u ? 4u : b == 2u ? 2u : 1u)
                    demo_hit(t, s0 + i, 1u << 1, 40 + b * 20u + i * 2u);
                break;
            }
            demo_hit(t, s0 + 4u, 1u << 2, 112);
            demo_hit(t, s0 + 12u, 1u << 2, 118);
            for (i = 0; i < 16u; i += v == 1u ? 4u : 1u)
                demo_hit(t, s0 + i + (v == 1u ? 2u : 0u), 1u << 3, v == 1u || i % 4u == 2u ? 88 : i & 1u ? 36 : 54);
            if (v == 2u) {
                demo_hit(t, s0 + 10u, 1u << 4, 72);                         /* the open hat, the rim */
                demo_hit(t, s0 + 7u, 1u << 6, 60);
                demo_hit(t, s0 + 15u, 1u << 6, 50);
            }
            break;
        case 2:                                                             /* the bass: 1 3 off the beat, 2 rolling */
            for (i = v == 2u ? 1u : 2u; i < 16u; i += v == 2u ? (i % 4u == 3u ? 2u : 1u) : 4u) {
                demo_note(&t->step[s0 + i], r + (v == 2u ? (i % 4u == 2u ? 12u : 0u) : (i / 4u) & 1u ? 12u : 0u),
                          i % 4u == 2u ? 112 : 96);
                demo_hold(t, s0 + i, v == 2u ? 1u : 2u);
            }
            break;
        case 3:                                                             /* the pad: the chord held */
            dm_bar(t, b, c, 3, 84);
            break;
        case 4:                                                             /* the arp's chord, held (the ARP plays it) */
            dm_bar(t, b, c, 3, 90);
            break;
        case 5: {                                                           /* the pluck: chord tones, syncopated */
            static const uint8_t AT[6] = {0, 3, 6, 8, 10, 13}, NI[6] = {0, 2, 1, 0, 2, 1};
            for (i = 0; i < 6u; i++) {
                demo_note(&t->step[s0 + AT[i]], c[NI[i]] + 12u, i ? 82 : 104);
                demo_hold(t, s0 + AT[i], 2);
            }
            break;
        }
        default:                                                            /* the air: A and E, held */
            if (!b) {
                static const uint8_t D[2] = {69, 76};
                dm_chord(t, 0, 64, D, 2, 72);
            }
            break;
        }
    }
    if (k == 3u && v == 3u)                                                 /* the build: the pad's CUT opening */
        demo_auto(t, P_E4, 0, 56, 18, 110);
    if (k == 2u && v == 2u)                                                 /* the rolling bass: its CUT up and down */
        demo_auto(t, P_E4, 0, 56, 40, 84);
    if (k == 7u && v == 2u)                                                 /* the air: its POS rising */
        demo_auto(t, P_E1, 0, 56, 10, 120);
}
static void felucca_demo(void)
{
    static const uint8_t SEC[15][9] = {       /* each track's pattern (0 silent), the repeats */
        {0, 0, 0, 1, 0, 1, 0, 1, 1},          /* the intro: the pad, the pluck, the air */
        {1, 1, 0, 1, 0, 1, 0, 1, 1},          /* the kick, the hats */
        {1, 1, 1, 1, 1, 1, 0, 0, 2},          /* the verse: the bass, the arp */
        {1, 2, 1, 1, 1, 0, 0, 0, 1},
        {2, 3, 0, 3, 1, 0, 0, 2, 1},          /* the build: the filter opening, the snare rolling, the air rising */
        {1, 2, 2, 1, 1, 0, 1, 0, 2},          /* the drop: the hook, the bass rolling */
        {1, 2, 2, 1, 1, 0, 2, 0, 1},
        {0, 0, 0, 2, 2, 2, 0, 1, 1},          /* the break: F G Em Am */
        {0, 1, 3, 2, 2, 2, 0, 1, 1},
        {2, 3, 0, 3, 1, 0, 0, 2, 1},          /* the build again */
        {1, 2, 2, 1, 1, 1, 1, 0, 2},          /* the drops, the riff with the hook */
        {1, 2, 2, 1, 1, 1, 2, 0, 1},
        {1, 1, 1, 1, 1, 0, 0, 1, 1},          /* the outro */
        {1, 0, 0, 1, 0, 1, 0, 1, 1},
        {0, 0, 0, 2, 0, 0, 0, 1, 1},          /* on Am */
    };
    static const uint8_t NV[8] = {2, 3, 3, 3, 2, 2, 2, 2};
    static const uint8_t SND[8][2] = {{ENGI_DRUM, 1}, {ENGI_DRUM, 2}, {0, 2}, {0, 1}, {6, 1}, {ENGI_FM6, 7}, {5, 1},
                                      {8, 3}};
    static const int8_t MIX[8][9] = {         /* LEVEL, PAN, WIDTH, CHORUS, DELAY, REVERB, the DUCK's depth, the
                                               * COMPRESSOR's, DIST */
        {110, 0, 0, 0, 0, 6, 0, 60, 0},       /* kick */
        {127, 8, 0, 0, 0, 30, 0, 60, 10},     /* kit */
        {111, 0, 0, 0, 0, 0, 72, 70, 36},     /* bass */
        {88, 0, 127, 50, 0, 80, 90, 0, 0},    /* pad */
        {94, -28, 0, 0, 60, 50, 50, 0, 0},    /* arp */
        {110, 28, 0, 0, 50, 60, 40, 0, 0},    /* pluck */
        {98, 0, 0, 20, 45, 70, 0, 60, 0},     /* lead */
        {80, -10, 100, 0, 30, 110, 60, 0, 0}, /* air */
    };
    static const int8_t EQB[9][4][3] = {      /* the four filters: TYPE (EQT_*), FREQ, SLOPE or GAIN */
        {{EQT_LS, 14, 13}, {EQT_BELL, 46, -17}, {EQT_BELL, 99, 8}, {EQT_LP, 118, 0}},     /* kick: the low up, no mud */
        {{EQT_HP, 30, 0}, {EQT_BELL, 57, -8}, {EQT_BELL, 93, 0}, {EQT_HS, 113, 13}},      /* kit: the air */
        {{EQT_HP, 3, 0}, {EQT_LS, 20, 13}, {EQT_BELL, 66, -13}, {EQT_LP, 93, 0}},         /* bass */
        {{EQT_HP, 43, 0}, {EQT_BELL, 57, -13}, {EQT_HS, 107, 9}, {EQT_LP, 118, 0}},       /* pad */
        {{EQT_HP, 52, 0}, {EQT_BELL, 85, 9}, {EQT_BELL, 99, 0}, {EQT_LP, 118, 0}},        /* arp */
        {{EQT_HP, 46, 0}, {EQT_BELL, 66, 0}, {EQT_HS, 103, 13}, {EQT_LP, 118, 0}},        /* pluck */
        {{EQT_HP, 38, 0}, {EQT_BELL, 79, 13}, {EQT_BELL, 99, 0}, {EQT_LP, 118, 0}},       /* lead: present */
        {{EQT_HP, 60, 1}, {EQT_BELL, 85, 0}, {EQT_BELL, 99, 0}, {EQT_LP, 113, 0}},        /* air */
        {{EQT_HP, 4, 0}, {EQT_BELL, 46, -4}, {EQT_BELL, 85, 0}, {EQT_HS, 113, 6}},        /* the MASTER */
    };
    static const int16_t DS[][3] = {          /* track, parameter, value */
        {2, P_VOICE, V_MONO}, {2, P_DCOL, -12}, {2, P_DOUT, -8},
        {3, P_ATK, 50}, {3, P_REL, 90}, {3, P_CHRD, CH_DIA3},
        {4, P_AMODE, 3}, {4, P_ARATE, 2}, {4, P_AOCT, 2}, {4, P_AGATE, 70},
        {6, P_VOICE, V_MONO}, {6, P_GLIDE, 30},
        {7, P_ATK, 110}, {7, P_REL, 110},
        {7, P_ITYPE, IT_PHASER}, {7, P_IA, 10}, {7, P_IB, 60}, {7, P_IMIX, 60},
    };
    uint32_t i, j, k, v;
    song.g[G_BPM] = 122;
    song.g[G_SWING] = 6;
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 96; song.g[G_RDAMP] = 60;
    song.g[G_DFDBK] = 45; song.g[G_DTIME] = 10; song.g[G_DCOLOR] = 50; song.g[G_DMIX] = 80;   /* (1/8D) */
    song.g[G_CRATE] = 12; song.g[G_CDEPTH] = 70;
    song.g[G_MCOMP] = 50; song.g[G_MREL] = 60;
    song.ntrk = 8;
    for (i = 0; i < 9u; i++) {
        eqc_t *c = i < 8u ? &trkx[i].eq : &eq_mc;
        int16_t *p = i < 8u ? trk[i].p : mtrk.p;
        if (i < 8u) {
            track_t *t = &trk[i];
            const int8_t *m = MIX[i];
            track_defaults_steps(t);
            t->p[P_SLEN] = 64;
            t->p[P_SDIV] = 2;
            demo_sound(t, SND[i][0], SND[i][1]);
            t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;                            /* A minor */
            t->p[P_LEVEL] = (uint8_t)m[0]; t->p[P_PAN] = m[1]; t->p[P_SPRD] = m[2];
            t->p[P_CHOR] = m[3]; t->p[P_DLY] = m[4]; t->p[P_REV] = m[5];
            if (m[6]) {                                                     /* the SIDECHAIN on the kick */
                t->p[P_DKSRC] = 1; t->p[P_DKAMT] = m[6]; t->p[P_DKREL] = -16;
            }
            trkx[i].rsv0 = (uint8_t)m[7];
            trkx[i].rsv1 = 60;
            t->p[P_DIST] = m[8];
            if (i == 2u) trkx[2].din = 8;
            trkx[i].curve = 1;                                              /* (the automation: RAMP) */
        }
        for (j = 0; j < 4u; j++) {
            c->t[j] = (uint8_t)EQB[i][j][0];
            p[EQ_FID[j]] = EQB[i][j][1];
            p[EQ_SID[j]] = EQB[i][j][2];
        }
        c->n = 4; c->o = 0xE4; c->f = EQF_NEW | EQF_4 | EQF_IN;
    }
    for (i = 0; i < sizeof DS / sizeof DS[0]; i++)
        trk[DS[i][0]].p[DS[i][1]] = DS[i][2];
    trkx[3].home[2] = P_SPRD + 1;                                           /* (the pad's HOME: its WIDTH on KNOB 3) */
    for (k = 0; k < 8u; k++)
        for (v = 1; v <= NV[k]; v++) {
            if (v > 1u)
                (void)pattern_new(k, 0);
            demo_var(k, v);
        }
    for (k = 0; k < 8u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);
    chain_config.count = 15;
    for (i = 0; i < 15u; i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = SEC[i][k] ? SEC[i][k] : (uint8_t)CHAIN_SILENT;
        chain_config.row[i].repeat = SEC[i][8];
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
    }
#if FELUCCA_DEMO
    felucca_demo();
    for (i = 0; i < NTRK; i++)                /* (Dah: its EQs as LOW CUT, HIGH CUT and bells: as bands, fx_eq.c) */
        if (!(trkx[i].eq.f & EQF_4))
            eq_adopt(trk[i].p, trk[i].p, &trkx[i].eq);
    if (!(eq_mc.f & EQF_4))
        eq_adopt(mtrk.p, mtrk.p, &eq_mc);
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
