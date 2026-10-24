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
/* HA HA HE HU's power-on demo (FELUCCA_DEMO), after a reference track (Aveon, "Nothing": 145 BPM, F, the sub on F1,
 * growl stabs on the beats, bright: centroid ~4.5 kHz, the drop after an ~8 s noise build).
 * The sounds (felucca_demo, power-on):
 *   track 1  the riddim bass: ANALOG's SYNC pair (the synced saw is the metal), driven, into a resonant low-pass; LFO 2
 *            on the 1/16, unipolar, restarted by each stab, sweeping the sync (SHP) and the filter: the wob; ENV DEST
 *            SHP gives each stab its "yoi"; INSERT FOLD on top, MONO
 *   track 2  its sub: ANALOG's sine an octave under the same notes (FM6's pad replaced), MONO
 *   track 3  the intro pad, then the riser: ANALOG SOFT PAD (LOFI replaced) in F minor (Fm Db Eb C), a big reverb; in a
 *            build its NOIS and CUT go up by automation: white noise
 *   track 4  DRUM (the STD kit): kick, clap and snare together, closed and open hats, the crash (GM 49 on the BELL lane)
 * The loop on the tracks at power-on (PLAY): bars 1-2 the intro, the build in bar 2, bars 3-4 the drop.
 * The song (demo_song_boot: PROJECT slots A..D when all four are empty, and the SONG; GLO > SONG, PLAY): INTRO, BUILD,
 * DROP and DROP 2, four bars each, as 48 bars: INTRO x2, BUILD, DROP x2, DROP 2 x2, INTRO, BUILD, DROP x2, DROP 2. */
static const uint8_t DM_CHORD[4][3] = {{53, 56, 60}, {49, 53, 56}, {51, 55, 58}, {48, 52, 55}};   /* Fm Db Eb C */
typedef struct { uint8_t step, len, note; } dm_note_t;
static const dm_note_t DM_DROP1[] = {   /* two bars: the first hit held to the clap, then stabs on the beats */
    {0, 8, 41}, {12, 2, 41}, {14, 1, 41},
    {16, 2, 41}, {20, 2, 41}, {24, 2, 41}, {28, 1, 44}, {30, 1, 39},
};
static const dm_note_t DM_DROP1B[] = {  /* .. again without the held hit: stabs from the first beat */
    {0, 2, 41}, {4, 2, 41}, {6, 1, 41}, {12, 2, 41}, {14, 1, 44},
    {16, 2, 41}, {20, 2, 41}, {22, 1, 46}, {24, 2, 41}, {28, 1, 44}, {30, 1, 39},
};
static const dm_note_t DM_DROP2[] = {   /* DROP 2: the stabs on a 3-3-2 feel, the line moving */
    {0, 2, 41}, {3, 1, 41}, {6, 2, 44}, {10, 2, 41}, {13, 1, 46}, {14, 1, 44},
    {16, 2, 41}, {19, 1, 41}, {22, 2, 39}, {26, 2, 44}, {29, 1, 48}, {30, 1, 46},
};
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
/* every track's steps and automation cleared, LEN 64 */
static void dm_clear(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
    }
}
/* the pad's chords from step at, one every len steps, n of them */
static void dm_chords(uint32_t at, uint32_t len, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        for (k = 0; k < 3u; k++)
            demo_note(&trk[2].step[at + i * len], DM_CHORD[i % 4u][k], 90);
        demo_hold(&trk[2], at + i * len, len);
    }
}
/* the pad into a white-noise riser over steps at .. at + len - 1: its NOIS and CUT up, by automation */
static void dm_riser(uint32_t at, uint32_t len)
{
    track_t *p = &trk[2];
    uint32_t i;
    (void)motion_set_event(p, 0, P_E3, 0);
    (void)motion_set_event(p, 0, P_E4, 46);
    for (i = 0; i < len; i += 2u) {
        (void)motion_set_event(p, at + i, P_E4, (int16_t)(46 + (int32_t)i * 81 / (int32_t)(len - 2u)));
        (void)motion_set_event(p, at + i, P_E3, (int16_t)(i * 127u / (len - 2u)));
    }
}
/* the drums' intro: offbeat hats, soft (and a kick on each bar's 1 when kick) */
static void dm_intro_drums(uint32_t at, uint32_t len, int kick)
{
    uint32_t i;
    for (i = 2; i < len; i += 4u)
        demo_hit(&trk[3], at + i, 1u << DV_HATC, 55);
    for (i = 0; kick && i < len; i += 16u)
        demo_hit(&trk[3], at + i, 1u << DV_KICK, 90);
}
/* the build's snare: quarters, then eighths, then sixteenths, louder and louder; the last sixteenth silent */
static void dm_roll(uint32_t at, uint32_t len)
{
    uint32_t i, q = len / 2u, e = len * 3u / 4u;
    for (i = 0; i + 1u < len; i++) {
        uint32_t every = i < q ? 4u : i < e ? 2u : 1u;
        if (i % every == 0u)
            demo_hit(&trk[3], at + i, 1u << DV_SNARE, 45 + i * 80u / len);
    }
}
/* the drop's bass on tracks 1 (as written) and 2 (an octave down): two bars of n notes from step at */
static void dm_bass(uint32_t at, const dm_note_t *b, uint32_t n)
{
    uint32_t i, k;
    for (k = 0; k < 2u; k++)
        for (i = 0; i < n; i++) {
            demo_note(&trk[k].step[at + b[i].step], (uint32_t)(b[i].note - (k ? 12 : 0)), 120);
            demo_hold(&trk[k], at + b[i].step, b[i].len);
        }
}
/* the drop's drums, two bars from step at: kick, the clap and snare on 3, hats from the first clap (crash: the drop's
 * first bar, no hats under its held hit) or all through, 1/16 pickups, an open hat; busy: DROP 2's sixteenth hats */
static void dm_drop_drums(uint32_t at, int crash, int busy)
{
    static const uint8_t KICK[] = {0, 16, 26}, KICK2[] = {0, 3, 16, 19, 26}, PICK[] = {14, 15, 30, 31};
    track_t *d = &trk[3];
    uint32_t i;
    if (crash)
        demo_note(&d->step[at], 49, 120);     /* brmm */
    for (i = crash ? 8u : 0u; i < 32u; i += busy ? 1u : 2u)
        demo_hit(d, at + i, 1u << DV_HATC, busy && (i & 1u) ? 50 : 80);
    for (i = 0; !busy && i < NELEM(PICK); i++)
        demo_hit(d, at + PICK[i], 1u << DV_HATC, 80);
    for (i = 0; i < (busy ? NELEM(KICK2) : NELEM(KICK)); i++)
        demo_hit(d, at + (busy ? KICK2[i] : KICK[i]), 1u << DV_KICK, 115);
    for (i = 8; i < 32u; i += 16u) {
        demo_hit(d, at + i, 1u << DV_CLAP | 1u << DV_SNARE, 115);
        d->step[at + i].acc |= 1u << DV_CLAP | 1u << DV_SNARE;
    }
    d->step[at + 22].hit = (uint8_t)((d->step[at + 22].hit & ~(1u << DV_HATC)) | 1u << DV_HATO);
}
/* HA HA HE HU's eight tracks: the intro's instruments on tracks 5..8, in F minor over the pad's chords (DM_CHORD):
 *   track 5  FM6 BELL: the chords' tones arpeggiated on the 1/8 (a delay, a long reverb)
 *   track 6  ANALOG PLUCK: the root and fifth, an octave up, on the offbeats (the delay)
 *   track 7  VOICE CHOIR AAH: each chord's top tone held, an octave up (a big reverb, quiet)
 *   track 8  DRUM PERC KIT: claves and congas, the intro's percussion */
static const uint8_t DM_ARP[4][4] = {{65, 68, 72, 68}, {65, 68, 73, 68}, {67, 70, 75, 70}, {67, 72, 76, 72}};
static void dm_intro_inst(uint32_t at, uint32_t len, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        uint32_t c0 = at + i * len;
        for (k = 0; k < len; k += 2u)                 /* the bell's arpeggio, 1/8 */
            demo_note(&trk[4].step[c0 + k], DM_ARP[i % 4u][(k / 2u) % 4u], 80);
        for (k = 2; k < len; k += 4u) {               /* the pluck's offbeats: root and fifth */
            demo_note(&trk[5].step[c0 + k], DM_CHORD[i % 4u][0] + 12u, 85);
            demo_note(&trk[5].step[c0 + k], DM_CHORD[i % 4u][2] + 12u, 85);
        }
        demo_note(&trk[6].step[c0], DM_CHORD[i % 4u][2] + 12u, 70);   /* the choir's held top tone */
        demo_hold(&trk[6], c0, len);
    }
}
static void dm_perc(uint32_t at, uint32_t len)        /* claves on the "and"s, congas before the backbeats */
{
    uint32_t i;
    for (i = 0; i < len; i += 8u) {
        demo_hit(&trk[7], at + i + 3u, 1u << DV_RIM, 70);
        demo_hit(&trk[7], at + i + 6u, 1u << DV_TOM, 80);
    }
}
/* the loop at power-on: two bars of intro (the build in the second), two of the drop */
static void dm_loop(void)
{
    dm_clear();
    dm_chords(0, 8, 4);
    dm_intro_inst(0, 8, 4);
    dm_perc(0, 32);
    dm_riser(16, 16);
    dm_intro_drums(0, 16, 0);
    dm_roll(16, 16);
    dm_bass(32, DM_DROP1, NELEM(DM_DROP1));
    dm_drop_drums(32, 1, 0);
}
static void felucca_demo(void)
{
    track_t *b = &trk[0], *u = &trk[1], *p = &trk[2], *d = &trk[3];
    uint32_t i;
    song.g[G_BPM] = 145;
    /* track 1: the bass, GROWL (WAVE RATIO FM FB CUT RES DRV SUB): a saw phase-modulated by a sine at the note, the
     * modulation's depth swept by LFO 2 on the 1/8 (a saw, unipolar, from each note: round -> bright -> metal, the
     * groove in the timbre), a soft clip after for the crunch, the filter open (the definition kept) */
    demo_sound(b, ENGI_GROWL, 0);
    b->p[P_E0] = 0;                           /* WAVE SAW */
    b->p[P_E1] = 1;                           /* RATIO 1 */
    b->p[P_E2] = 40;                          /* FM: ~30 %, the LFO adds the rest */
    b->p[P_E3] = 20;                          /* FB */
    b->p[P_E4] = 100;                         /* CUT */
    b->p[P_E5] = 20;                          /* RES */
    b->p[P_E6] = 60;                          /* DRV: crunchy, not destroyed */
    b->p[P_E7] = 0;                           /* SUB: track 2's */
    b->p[P_DEC] = 50;
    b->p[P_ED_FLT] = 0;
    b->p[P_ED_SHP] = 0;
    b->p[P_LWAVE] = 2;                        /* SAW */
    b->p[P_LSYNC] = 6;                        /* 1/8 (N_LSYNC) */
    b->p[P_LTRIG] = 0;                        /* NOTE */
    b->p[P_LPOL] = 1;                         /* UNI */
    b->p[P_LD_SHP] = 63;                      /* LFO -> the FM's depth */
    b->p[P_LD_FLT] = 0;
    b->p[P_DIST] = 25;
    b->p[P_ITYPE] = 1;                        /* INSERT SOFT (N_ITYPE): the tube-ish clip */
    b->p[P_IA] = 50;                          /* DRIVE */
    b->p[P_IB] = 110;                         /* TONE */
    b->p[P_IC] = 104;                         /* LEVEL */
    b->p[P_IMIX] = 70;
    /* track 2: the sub, ANALOG's sine an octave down */
    demo_sound(u, 0, 7);                      /* SUB BASS */
    u->p[P_E0] = 3;                           /* SIN */
    u->p[P_E4] = 127;                         /* CUT open */
    u->p[P_E5] = 0;
    u->p[P_E6] = 25;                          /* a little drive: the sub heard on small speakers */
    u->p[P_DEC] = 60;
    u->p[P_DIST] = 0;
    for (i = 0; i < 2u; i++) {                /* both basses MONO, short release, dry */
        trk[i].p[P_VOICE] = V_MONO;
        trk[i].p[P_ATK] = 0;
        trk[i].p[P_SUS] = 127;
        trk[i].p[P_REL] = 12;
        trk[i].p[P_CHOR] = trk[i].p[P_DLY] = trk[i].p[P_REV] = 0;
        trk[i].p[P_DKSRC] = 4;                /* DUCK under track 4's kick: the pump */
        trk[i].p[P_DKAMT] = 80;
        trk[i].p[P_DKREL] = -37;              /* (~100 ms) */
    }
    /* track 3: the intro pad, then the riser */
    demo_sound(p, 0, 1);                      /* SOFT PAD */
    p->p[P_E3] = 0;                           /* NOIS (automated) */
    p->p[P_E4] = 46;                          /* CUT (automated) */
    p->p[P_REV] = 80;
    p->p[P_DLY] = 30;
    p->p[P_LEVEL] = (int16_t)(p->p[P_LEVEL] - 6);   /* -3 dB (LEVEL: 0.5 dB a step) */
    d->p[P_REV] = 24;
    demo_sound(&trk[4], ENGI_FM6, 1);         /* BELL */
    trk[4].p[P_LEVEL] = 96;
    trk[4].p[P_DLY] = 45;
    trk[4].p[P_REV] = 70;
    demo_sound(&trk[5], 0, 8);                /* ANALOG PLUCK */
    trk[5].p[P_LEVEL] = 94;
    trk[5].p[P_DLY] = 55;
    demo_sound(&trk[6], 5, 0);                /* VOICE CHOIR AAH */
    trk[6].p[P_LEVEL] = 88;
    trk[6].p[P_REV] = 100;
    demo_sound(&trk[7], ENGI_DRUM, 3);        /* PERC KIT */
    trk[7].p[P_LEVEL] = 96;
    trk[7].p[P_REV] = 40;
    dm_loop();
    for (i = 0; i < NTRK; i++)
        pat_sig[i] = steps_sig(&trk[i]);
}

/* the music on the tracks -> PROJECT slot `slot`, named `name`: as project_save_as, without its messages. Boot only */
static void demo_save(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    project_capture(p);
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name));
    p->sum = proj_sum(p);
    if (!proj_pack(&proj_wire, p))
        return;
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire))
        return;
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    (void)proj_x_save(p, OBJ_PROJX0 + (slot & 3u), proj_tag(&proj_slot[slot & 3u]));   /* (tracks 5..8) */
}

/* boot, after the autosave: with every PROJECT slot empty, the song's four sections into A..D and the SONG set; the
 * tracks then hold what they held: the session the autosave restored (restored), else the loop (felucca_demo's) */
static void demo_song_boot(int restored)
{
    static const uint8_t ROWS[][2] = {{0, 2}, {1, 1}, {2, 2 | CHAIN_CUT}, {3, 2}, {0, 1}, {1, 1}, {2, 2 | CHAIN_CUT},
                                      {3, 1}};   /* {slot, repeat}; the drops after a build CUT: the build's tails gone */
    char nm[PROJ_NAME_LEN + 1u];
    uint32_t i, k;
    for (i = 0; i < 4u; i++)
        if (project_name(i, nm)) {            /* (a slot in use: the user's music, nothing written; an empty SONG */
            if (!chain_config.count && project_name(0, nm) &&   /* takes the one PROJECT A was saved with) */
                proj_import(&proj_scratch, &proj_slot[0], sizeof(project_store_t)) && chain_valid(&proj_scratch.chain))
                chain_config = proj_scratch.chain;
            return;
        }

    chain_defaults(&chain_config);
    chain_config.count = (uint8_t)NELEM(ROWS);
    for (i = 0; i < NELEM(ROWS); i++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[i].slot[k] = ROWS[i][0];
        chain_config.row[i].repeat = ROWS[i][1];
    }
    dm_clear();                               /* A: INTRO, the pad, then soft hats and a kick on each 1; 5..8 the bell,
                                               * the pluck, the choir, the percussion from bar 3 */
    dm_chords(0, 16, 4);
    dm_intro_inst(0, 16, 4);
    dm_perc(32, 32);
    dm_intro_drums(32, 32, 1);
    demo_save(0, "INTRO");
    dm_clear();                               /* B: BUILD, the pad turned into noise, the kick on the beats, the roll; the
                                               * bell and the pluck on */
    dm_chords(0, 16, 4);
    dm_intro_inst(0, 16, 4);
    dm_riser(0, 64);
    for (i = 0; i < 32u; i += 4u)
        demo_hit(&trk[3], i, 1u << DV_KICK, 100);
    dm_roll(32, 32);
    demo_save(1, "BUILD");
    dm_clear();                               /* C: DROP */
    dm_bass(0, DM_DROP1, NELEM(DM_DROP1));
    dm_bass(32, DM_DROP1B, NELEM(DM_DROP1B));
    dm_drop_drums(0, 1, 0);
    dm_drop_drums(32, 0, 0);
    demo_save(2, "DROP");
    dm_clear();                               /* D: DROP 2 */
    dm_bass(0, DM_DROP2, NELEM(DM_DROP2));
    dm_bass(32, DM_DROP2, NELEM(DM_DROP2));
    dm_drop_drums(0, 1, 1);
    dm_drop_drums(32, 0, 1);
    demo_save(3, "DROP 2");
    if (restored && autosave_boot(1))                /* (the session the autosave restored, again: it is still there) */
        return;
    dm_loop();
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
    fm6_init();                               /* every track's FM6 patch: the init voice */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
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
#if FELUCCA_DEMO
    demo_song_boot(autosave_boot(boot_clean));   /* (1.2) the last session's music, under the splash; the demo song
                                                  * into the empty PROJECT slots (HA HA HE HU) */
#else
    autosave_boot(boot_clean);                /* (1.2) the last session's music, under the splash */
#endif
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
