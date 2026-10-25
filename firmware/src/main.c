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
/* Dah's power-on sound (FELUCCA_DEMO): dark and epic, D minor at 80 BPM, seven tracks (song.ntrk), two patterns each (four
 * bars, 64 x 1/16) and a SONG (1 twice, 2 twice); no project written. Tension, then its resolution, over a D pedal:
 *   pattern 1  Dm(add9)  Eb/D (the minor second on the pedal)  Bbmaj7/D  A7b9 (back home)
 *   pattern 2  Dm(add9)  Gm(add9)/D  Ebmaj7  Asus4 > A7 (the suspension resolved in the bar)
 *   track 1  ANALOG SOFT PAD    a big held pad; its filter opening over pattern 1 (CUT automated, a RAMP)
 *   track 2  ANALOG STRINGS     the chords an octave up, held; their reverb swelling (REV automated)
 *   track 3  ANALOG SAW LEAD    MONO, sliding note to note (SLIDE, GLIDE), D Eb D C# ... its delay thrown on the phrase ends
 *   track 4  ANALOG SUB BASS    the pedal's roots held, sliding into the next bar's
 *   track 5  DRUM DEEP KIT      the kick: a slow pulse, then every beat
 *   track 6  DRUM KIT           the snare deep in the hall, a tom fill closing pattern 2
 *   track 7  VOICE CHOIR AAH    the chords held, far behind (the VP-330 ensemble) */
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
    static const uint8_t PAD1[4][4] = {{50, 53, 57, 64}, {50, 51, 55, 58}, {50, 53, 57, 58}, {49, 52, 55, 58}};   /* Dm9 Eb/D
                                                     * Bbmaj7/D A7b9 */
    static const uint8_t PAD2[4][4] = {{50, 53, 57, 64}, {50, 55, 57, 58}, {51, 55, 58, 62}, {50, 52, 57, 64}};   /* Dm9
                                                     * Gm9/D Ebmaj7 Asus4 */
    static const uint8_t SUB[2][4] = {{38, 38, 38, 33}, {38, 38, 39, 33}};
    static const struct { uint8_t step, note, len; } LEAD[2][10] = {
        {{0, 74, 6}, {6, 75, 2}, {8, 74, 8}, {16, 75, 6}, {22, 77, 2}, {24, 75, 8}, {32, 74, 8}, {40, 70, 8},
         {48, 73, 8}, {56, 76, 8}},                     /* D Eb D, Eb F Eb, D Bb, C# E (the leading tone) */
        {{0, 77, 6}, {6, 76, 2}, {8, 74, 8}, {16, 79, 8}, {24, 77, 8}, {32, 75, 12}, {44, 74, 4}, {48, 76, 6},
         {54, 74, 2}, {56, 73, 8}},
    };
    const uint8_t (*pad)[4] = p == 1u ? PAD1 : PAD2;
    uint32_t b, i;
    track_t *t;
    for (b = 0; b < 4u; b++) {
        uint32_t s0 = b * 16u;
        dm_bar(&trk[0], b, pad[b], 4, 74);                                 /* the pad */
        {
            uint8_t up[4];
            for (i = 0; i < 4u; i++) up[i] = (uint8_t)(pad[b][i] + 12u);
            dm_bar(&trk[1], b, up, 4, 62);                                  /* the strings, an octave up */
        }
        demo_note(&trk[3].step[s0], SUB[p - 1u][b], 104);                   /* the sub: held, sliding to the next */
        demo_hold(&trk[3], s0, 16);
        if (b < 3u) trk[3].step[s0 + 15u].flags |= SF_SLIDE;
        t = &trk[4];                                                        /* the kick */
        if (p == 1u) {
            demo_hit(t, s0, 1u << 0, 124);
            demo_hit(t, s0 + 1u, 1u << 0, 84);
            demo_hit(t, s0 + 10u, 1u << 0, 100);
        } else
            for (i = 0; i < 16u; i += 4u)
                demo_hit(t, s0 + i, 1u << 0, i ? 104 : 124);
        t = &trk[5];                                                        /* the snare; the fill */
        if (p == 1u || b < 3u)
            demo_hit(t, s0 + (p == 1u ? 8u : 4u), 1u << 1, 90);
        if (p == 2u && b < 3u)
            demo_hit(t, s0 + 12u, 1u << 1, 96);
        if (p == 2u && b == 3u) {
            for (i = 8; i < 16u; i += 2u)
                demo_hit(t, s0 + i, 1u << 5, 70 + 8 * (i - 8u) / 2u);      /* (lane 5: TOM) */
            demo_hit(t, s0 + 15u, 1u << 1, 112);
        }
        dm_bar(&trk[6], b, pad[b] + 1, 3, 70);                              /* the choir, held */
    }
    for (i = 0; i < 10u; i++) {                                             /* the lead, sliding note to note */
        step_t *st = &trk[2].step[LEAD[p - 1u][i].step];
        demo_note(st, LEAD[p - 1u][i].note, 96);
        demo_hold(&trk[2], LEAD[p - 1u][i].step, LEAD[p - 1u][i].len);
        if (i + 1u < 10u && LEAD[p - 1u][i + 1u].step == LEAD[p - 1u][i].step + LEAD[p - 1u][i].len)
            trk[2].step[LEAD[p - 1u][i].step + LEAD[p - 1u][i].len - 1u].flags |= SF_SLIDE;   /* (into the next) */
    }
    if (p == 1u) {                                                          /* the effects, automated */
        demo_auto(&trk[0], P_E4, 0, 56, 36, 100);                           /* the pad's filter opening */
        demo_auto(&trk[1], P_REV, 0, 56, 40, 120);                          /* the strings' reverb swelling */
        demo_auto(&trk[2], P_DLY, 0, 8, 10, 10);                            /* the lead's delay: thrown at the ends */
        (void)motion_set_event(&trk[2], 24, P_DLY, 90);
        (void)motion_set_event(&trk[2], 32, P_DLY, 10);
        (void)motion_set_event(&trk[2], 56, P_DLY, 100);
    } else {
        demo_auto(&trk[0], P_E4, 0, 56, 100, 70);                           /* the pad: closing a little, the last bar */
        demo_auto(&trk[1], P_REV, 0, 56, 120, 70);
        demo_auto(&trk[2], P_DLY, 0, 56, 30, 30);
        (void)motion_set_event(&trk[2], 32, P_DLY, 100);
        (void)motion_set_event(&trk[2], 44, P_DLY, 30);
    }
}
static void felucca_demo(void)
{
    uint32_t i, k;
    track_t *t;
    song.g[G_BPM] = 80;
    song.g[G_RTYPE] = 2;                      /* HALL, big and dark */
    song.g[G_RSIZE] = 122;
    song.g[G_RDAMP] = 96;
    song.g[G_DFDBK] = 62;
    song.g[G_DTIME] = 10;                     /* 1/8D */
    song.ntrk = 7;                            /* (the last: its power-on sound, hidden) */
    for (i = 0; i < NTRK; i++) {
        track_defaults_steps(&trk[i]);
        trk[i].p[P_SLEN] = 64;
        trk[i].p[P_SDIV] = 2;                 /* 1/16 */
    }
    t = &trk[0];
    demo_sound(t, 0, 1);                      /* ANALOG SOFT PAD: the big pad */
    t->p[P_ATK] = 80; t->p[P_REL] = 100; t->p[P_LEVEL] = 86; t->p[P_REV] = 90; t->p[P_CHOR] = 50;
    trkx[0].curve = 1;                        /* (its automation ramps) */
    t = &trk[1];
    demo_sound(t, 0, 11);                     /* ANALOG STRINGS */
    t->p[P_ATK] = 70; t->p[P_REL] = 96; t->p[P_LEVEL] = 76; t->p[P_CHOR] = 60;
    trkx[1].curve = 1;
    t = &trk[2];
    demo_sound(t, 0, 0);                      /* ANALOG SAW LEAD: MONO, sliding */
    t->p[P_VOICE] = V_MONO; t->p[P_GLIDE] = 70;
    t->p[P_LEVEL] = 72; t->p[P_REV] = 80;
    t = &trk[3];
    demo_sound(t, 0, 7);                      /* ANALOG SUB BASS */
    t->p[P_GLIDE] = 60; t->p[P_LEVEL] = 94; t->p[P_REV] = 0; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[4];
    demo_sound(t, ENGI_DRUM, 1);              /* DEEP KIT: the kick */
    t->p[P_E0 + 1] = 48;
    t->p[P_E0 + 2] = 24;
    t->p[P_E0 + 3] = 84;
    t->p[P_E0 + 4] = 10;
    t->p[P_E0 + 6] = 1;
    t->p[P_LEVEL] = 100; t->p[P_REV] = 8; t->p[P_DLY] = 0; t->p[P_CHOR] = 0;
    t = &trk[5];
    demo_sound(t, ENGI_DRUM, 0);              /* DRUM KIT: snare, toms */
    t->p[P_E0 + 2] = 36;
    t->p[P_LEVEL] = 80; t->p[P_REV] = 120; t->p[P_DLY] = 14;
    t = &trk[6];
    demo_sound(t, 5, 0);                      /* VOICE CHOIR AAH */
    t->p[P_E0 + 4] = 72;                      /* BUZZ up */
    t->p[P_E0 + 5] = 12;                      /* BRTH down */
    t->p[P_E0 + 6] = 100;                     /* Q */
    t->p[P_E0 + 7] = 24;                      /* RAND */
    t->p[P_ATK] = 90; t->p[P_REL] = 100; t->p[P_LEVEL] = 74; t->p[P_REV] = 110;
    demo_pattern(1);
    for (k = 0; k < 7u; k++)                  /* pattern 1 kept, 2 written, 1 back on the tracks */
        (void)pattern_new(k, 0);
    demo_pattern(2);
    for (k = 0; k < 7u; k++)
        (void)pattern_go(k, 1);
    chain_defaults(&chain_config);            /* the SONG: 1 twice, 2 twice (GLO > SONG; PLAY there plays it) */
    chain_config.count = 2;
    for (k = 0; k < NTRK; k++) {
        chain_config.row[0].slot[k] = k < 7u ? 1u : (uint8_t)CHAIN_SILENT;
        chain_config.row[1].slot[k] = k < 7u ? 2u : (uint8_t)CHAIN_SILENT;
    }
    chain_config.row[0].repeat = 2;
    chain_config.row[1].repeat = 2;
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
