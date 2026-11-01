/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the UI (firmware/src/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, TOOLS INIT, user preset) changes the sound only: the
 *              steps, LEN DIV SWING GATE, ARP, SCL, the SLICER and the mix stay the track's. Power-on:
 *              every sequencer empty.
 *   REC        a tap on every screen arms the selected track and starts PLAY, without navigation.
 *              Long REC never clears. On STEP, armed and playing: keys do not write the
 *              cursor step. With the ARP on, the arp's notes are recorded, not the keys.
 *   MIDI IN    MENU > MIDI > MIDI IN (ROUT): CH1-4 (channels 5..16 ignored) / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    USER, PATTERN, FILE: the knobs pick, OCT+ does it, OCT- cancels or goes
 *              HOME, on release, never both together; the OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   GRID       SEQ > STEP on a DRUM track: white keys toggle the selected lane's steps of the page, black
 *              keys 1..8 select the lane (and only they play), 9 held = ACC, 10 / 11 the page; KNOB 1..4 STEP
 *              LANE HIT ACC; the key LEDs (hits, the playhead inverted, the lane, ACC, the page keys); a step's
 *              GM notes shown on their lanes and made the lane's own by an edit; live recording into the grid
 *              (lane keys on the grid, GM keys elsewhere; other GM drums as notes; no TIE holds); 12 BEAT:
 *              its notes fill the grid; sound loads keep it, and other engines play it as GM notes.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 *   SOAK       #63: 150000 random frames (turns, pages, transport, REC / motion, wrapping clocks): no roll
 *              outlives ROLL_FRAMES, the cards always equal a forced redraw once the rolls are over.
 *   FM6 OPS    (1.2, #76) EDIT > OPERATOR, OP ENV, OPERATOR 2 on an FM6 track: KNOB 1 the operator, each of the 84 bytes
 *              the pages edit written alone, SLOT OWN, the sound following, the macros on top,
 *              projects and user presets keep the patch; FM6 only, not automatable.
 *   USB SERIAL #67: MENU > USB SERIAL ON (default, the descriptors as before) / OFF (audio + MIDI only, class 0):
 *              the menu, the settings, the descriptor at boot (built with the console, as the firmware: FELUCCA_CDC).
 *   SCREEN OFF (1.1.5): MENU > DISPLAY > SCREEN OFF against a simulated clock and the panel's commands (lcd_power):
 *              dark after the time, nothing drawn, any input wakes it (SLPOUT, 120 ms, redraw, DISPON) and is
 *              swallowed; dialogs / count-in / UPDATE MODE keep it on; saved; the sound bit for bit while dark.
 * Built and run by tests/run_tests.sh. */
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                                    /* (as src/felucca.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* hal/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* hal/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int host_idle, host_idle_glow;             /* .. the idle animation (1.2: MENU > ANIM IDLE) running, its glow */
static int fm1_led_anim_on(void) { return host_anim || host_idle; }
static void fm1_led_idle_start(uint32_t glow) { host_idle = 1; host_idle_glow = (int)glow; }
static int fm1_led_idle_on(void) { return host_idle; }
static void fm1_led_anim_stop(void) { host_anim = host_idle = 0; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);                /* (a test's: time passing in a blocking loop, as panel_setup's) */
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
#ifndef HOST_IRQ_STUBS                                   /* (tests/drumfuzz_test.c: its own, an emulated ISR) */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#endif
static uint16_t host_screen[240 * 240];
/* #181 a blank frame: while host_fl_on, every write of a frame is watched (host_fl_begin .. host_fl_end); a pixel that
 * shows, between two writes, a value that is neither the one before the frame nor the one after (a fill of the
 * background before a canvas lands over it) flashed */
static uint8_t host_fl_on, host_fl_n[240 * 240];
static uint16_t host_fl_was[240 * 240], host_fl_mid[240 * 240];
static void host_put(uint32_t k, uint16_t v)
{
    if (host_fl_on) {
        if (!host_fl_n[k])
            host_fl_was[k] = host_screen[k];
        else if (host_screen[k] != host_fl_was[k] && host_fl_n[k] < 2u)
            host_fl_mid[k] = host_screen[k], host_fl_n[k] = 2;   /* (2: an in-between value kept) */
        if (!host_fl_n[k])
            host_fl_n[k] = 1;
    }
    host_screen[k] = v;
}
static void host_fl_begin(void) { memset(host_fl_n, 0, sizeof host_fl_n); host_fl_on = 1; }
static uint32_t host_fl_end(uint32_t y0, uint32_t y1)   /* the rows y0..y1-1: pixels that flashed */
{
    uint32_t k, n = 0;
    host_fl_on = 0;
    for (k = y0 * 240u; k < y1 * 240u; k++)
        n += host_fl_n[k] == 2u && host_fl_mid[k] != host_screen[k];
    return n;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_put((y + j) * 240u + x + i, (uint16_t)((c >> 8) | (c << 8)));
}
static void lcd_sync(void) {}
/* MENU > SCREEN OFF (lcd.c lcd_power / lcd_wake_now): what the panel was told, in order (0 off: DISPOFF, SLPIN;
 * 1 SLPOUT; 2 DISPON; 3 lcd_wake_now), with the time; host_bl the panel showing (the backlight itself stays on: PA2 is
 * the key matrix's /OE too, lcd.c lcd_power) */
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
/* (the HOME oscilloscope's buffer, scope_buf: fx.c, with hostsim.c) */
static uint32_t host_blit_rows;                    /* rows blitted (what a frame draws) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
#ifdef UI_BLIT_HOOK
    UI_BLIT_HOOK(x, y, w, h);                     /* (tests/idle_test.c: what an idle frame draws) */
#endif
    host_blit_rows += h;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_put((y + j) * 240u + x + i, p[j * w + i]);
}
static struct { uint32_t stage, late; } felucca_dbg;
static uint32_t shed_count;                      /* (the header's CPU warning, ui_draw.c hld) */
#define FELUCCA_FLASH 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "TEST"
#endif
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
/* (Dah) a 16-step pattern (hostsim.c PATTERNS[] format, a user preset's) into steps 1..16, the rest empty, LEN 16: as
 * SAVE > PHRASES loaded one (ui.c load_pat16, gone with it) */
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint8_t n = i < 16u ? note[i] : 0, fl = i < 16u ? flags[i] : 0;
        s->note[0] = n;
        s->n = n ? 1 : 0;
        s->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
        s->flags = n ? (fl & (SF_ACCENT | SF_SLIDE | SF_RATCH)) : 0;
        s->vel = n ? 96 : 0;
        s->hit = s->acc = 0;
        s->probability = 0;
        if (drum_track(t))                           /* a DRUM track: the lanes' notes as its grid */
            step_to_grid(s);
    }
    t->p[P_SLEN] = 16;
}
/* a FUN10 record holds tracks 1..4 (5..8: the FUNX, project.c proj_pack_x): a round trip through it alone
 * is the same project but for tracks 5..8 (their sounds, steps, patches, channels, sections' slots) */
static int proj_same(const project_t *a, const project_t *b)
{
    static project_t c;
    uint32_t k, i;
    c = *b;
    for (k = NTRK_V10; k < NTRK; k++) {
        c.t[k] = a->t[k];
        memcpy(c.fm6[k], a->fm6[k], FM6_PACKED);
        c.mch[k] = a->mch[k];
        for (i = 0; i < CHAIN_ROWS; i++)
            c.chain.row[i].slot[k] = a->chain.row[i].slot[k];
    }
    c.parts = a->parts;
    c.sum = a->sum;
    return !memcmp(a, &c, sizeof c);
}

static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c felucca_init */
static void ui_power_on(void)
{
    uint32_t i;
    ka.st = 0;                                    /* (Dah: no ASSIGN going on) */
    chain_from_row = 0;                           /* (Dah: SONG's GRID at the top) */
    chain_from_u = 0;
    prv.on = prv.nofollow = 0; prv.zoom = 1; prv.at = -1;   /* (Dah: STEP's / AUTOMATION's VIEW as at power-on) */
    au_lane = 0;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    for (i = 0; i < NTRK; i++)                    /* (the tracks' extras as at power-on) */
        trkx_defaults(&trkx[i]);
    xp_point = xp_shape = 0;
    mix_solo = 0;                                 /* (Dah: MIXER's SOLO, none at power-on) */
    ps_reset();
    memset(eng_mem, 0, sizeof eng_mem); memset(eng_mem_s, 0, sizeof eng_mem_s);   /* (.bss at power-on: the engines' regions) */
    memset(midi_lit_ms, 0, sizeof midi_lit_ms); midi_seen = 0;   /* (1.5.5: no MIDI IN light) */
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(&ui, 0, sizeof ui);
    page_over = 0;                                /* (no layer's knobs; roll_settle's test page gone) */
    cin_left = cin_total = 0;                     /* (no count-in) */
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    memset(&mom, 0, sizeof mom);                  /* MOMENTARY: no value held (the pool at power-on) */
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    ml_off();                                     /* (MIDI LEARN off, nothing learned) */
    fop_op = fop_stg = 0;                         /* (FM6's operator pages: OP 1, stage 1, as at power-on) */
    memset(pat_from, 0, sizeof pat_from);                /* (the header's slot letter: none) */
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    load_depth++;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
    }
    load_depth--;
    song.sel = 0;
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_GREY_INDEX;
    palette_set(UI_GREY_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_latched = 0;
    memcpy((void *)perf_map, PF_DEF, sizeof PF_DEF);    /* .. its default key map (the settings' 0), no key's effect */
    perf_remap = 0;
    memset(kb_fx, 0, sizeof kb_fx);
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
    memset(&motion, 0, sizeof motion);                  /* (main.c: no motion at power-on) */
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_locked, 0, sizeof motion_locked);
    motion_base_valid = motion_full = 0;
    memset(snd_said, 0, sizeof snd_said);               /* (.bss at power-on: nothing said yet) */
    memset(&scrn, 0, sizeof scrn);                        /* MENU > SCREEN OFF: on, idle from now (main.c) */
    scrn.idle = fm1_ms;
    kb_asleep = 0;
    panel_moved = 0;
}
/* card slot drawn with MOTION's mark (ui_draw.c card_mot: its icon, or a long label in the theme colour) */
static int card_motion(uint32_t slot) { return (card_mot >> slot) & 1u; }

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
    chain_poll();                                 /* (the main loop readies a song's next section) */
}
/* every track's pattern as it is now kept as its pattern num too (pstore.c), the one it plays unchanged:
 * what a PROJECT slot A..D (patterns 1..4) was to SONG before */
static void pat_keep(uint32_t num)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (num != ps_num(k))
            ps_put(k, num, ps_buf, ps_encode(&trk[k], ps_buf));
}
/* the tracks play pattern 32 (their steps as they are): 1..4 free for pat_keep, the slots A..D of before */
static void pat_park(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        ps_cur[k] = PS_NUM;
    ps_gen++;
}
/* track k's pattern num decoded (its steps into st): 0 none */
static int pat_steps(uint32_t k, uint32_t num, step_t *st)
{
    static motion_store_t m;
    int16_t tm[4];
    uint8_t q;
    uint32_t n = ps_exists(k, num) ? ps_fetch(k, num) : 0u;
    m.count = 0;
    return n && ps_decode_to(k, ps_buf, n, st, tm, &q, &m);
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
static void btn_down(uint32_t label) { fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; }
static void btn_up(uint32_t label) { fm1_in.buttons &= ~(1u << panel.btn[label]); }
static void hold(uint32_t label)                  /* held 0.8 s, then let go */
{
    uint32_t k;
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns) */
    if (pl_ms | tl_ms) {                          /* (Dah: the lists PRESETS / ALGORITHM show ~1 s: gone, the page as it
                                                   * was, as the checks read it) */
        pl_ms = tl_ms = 0;
        frame();
    }
}
/* (Dah) OCT+ held + SELECT: the tempo (SELECT alone: the pattern); OCT+ let go at the next frame (no octave) */
static void tempo_turn(int32_t s)
{
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    turn(EN_SELECT, s);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
}
static void stop_transport(void) { transport_req = 0; song.playing = 0; }
static int16_t stored_param(uint32_t slot, uint32_t track, uint32_t id)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].p[id] : -32768;
}
static uint8_t stored_note(uint32_t slot, uint32_t track, uint32_t step)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].step[step].note[0] : 0;
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
static void my_steps(track_t *t)                  /* "recorded" steps */
{
    uint32_t i;
    track_defaults_steps(t);
    for (i = 0; i < 32u; i += 3u) {
        t->step[i].note[0] = (uint8_t)(40 + i);
        t->step[i].n = 1;
        t->step[i].time = ST_NOTE;
        t->step[i].vel = 99;
    }
    t->p[P_SLEN] = 32;
}

static void go_page(uint32_t graph)               /* the page that draws graph */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
/* the sound of a track: engine, preset and the parameters that are not the track's own (param_kept) */
static int same_sound(const track_t *a, const track_t *b)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    return a->eng_req == b->eng_req && a->preset == b->preset;
}

/* the track's E values are DRUM's default kit (core.h DRUM_KIT_E: what a SAMPLE PERC sound loads as) */
static int perc_kit(const track_t *t)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    return !memcmp(&t->p[P_E0], KIT, sizeof KIT);
}
/* the GM drums a DRUM track plays: a kick, a snare and a closed hat each strike their lane and sound */
static int gm_hits_play(track_t *t)
{
    static const uint8_t N[3] = {36, 38, 42}, L[3] = {DV_KICK, DV_SNARE, DV_HATC};
    int32_t o[2u * CTL];
    uint32_t k, i, b, ok = 1, m = t->p[P_MUTE], mq = song.master_q12;
    t->p[P_MUTE] = 0;
    song.master_q12 = 4096;                       /* (the MASTER knob: main.c; 0 in this test) */
    for (b = 0; b < 256u && (t->xf_on || t->eng_req != t->engine); b++)   /* the engine switch done (its fade) */
        mix_block(o, CTL);
    for (k = 0; k < 3u; k++) {
        uint64_t e = 0;
        int lane = 0;
        trk_note_on(t, N[k], 100);
        for (b = 0; b < 16u; b++) {
            memset(o, 0, sizeof o);
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            for (i = 0; i < NVOICE; i++)
                lane |= t->v[i].active && t->v[i].note == N[k] && (uint32_t)t->v[i].s[0] == L[k];
        }
        trk_note_off(t, N[k]);
        ok &= ENGINES[t->engine] == &ENG_DRUM && drum_lane(N[k]) == L[k] && lane && e > 0;
    }
    t->p[P_MUTE] = (int16_t)m;
    song.master_q12 = mq;
    return (int)ok;
}

/* sounds never touch the steps; the arp, the scale, the SLICER and the pattern parameters are the track's */
static int test_sound_loads(void)
{
    int bad = 0;
    uint32_t i, empty = 1;
    track_t *t = &trk[0], before;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        empty &= (uint32_t)seq_is_empty(&trk[i]);
    bad += check("power-on: the four sounds, every sequencer empty", empty && load_depth == 0 &&
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && trk[0].preset == TRK_DEF[0][1]);
    my_steps(t);
    t->p[P_E0 + 1] = 77;                          /* a sound edit */
    t->p[P_AMODE] = 2;                            /* and the track's own settings */
    t->p[P_AOCT] = 3;
    t->p[P_SCALE] = 2;
    t->p[P_TRANS] = 5;
    t->p[P_SLCR] = SL_GATE;
    t->p[P_SLPAT] = 4;
    t->p[P_SDIV] = 1;
    t->p[P_LEVEL] = 90;
    before = *t;
    turn(EN_PRESET, 1);                           /* HOME: the next preset */
    bad += check("PRESETS on HOME loads the sound: steps, LEN, DIV untouched", t->preset == (uint8_t)((before.preset + 1u) % ENGINES[0]->npresets) &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_SLEN] == 32 && t->p[P_SDIV] == 1);
    bad += check("..ARP, SCL, the SLICER and the mix stay the track's", t->p[P_AMODE] == 2 && t->p[P_AOCT] == 3 && t->p[P_SCALE] == 2 &&
                 t->p[P_TRANS] == 5 && t->p[P_SLCR] == SL_GATE && t->p[P_SLPAT] == 4 && t->p[P_LEVEL] == 90);
    bad += check("..and no warning about the sequence", !msg_is("T1 SEQ REPLACED"));
    for (i = 0; i < ENGINES[0]->npresets; i++)
        if (str_eq(ENGINES[0]->presets[i].name, "RAVE"))
            break;
    t->p[P_AMODE] = 0;
    t->preset = (uint8_t)(i - 1u);
    turn(EN_PRESET, 1);
    bad += check("RAVE (an ARP preset) leaves the arp off", t->preset == i && t->p[P_AMODE] == 0);
    t->p[P_AMODE] = 2;
    before = *t;
    for (i = 0; i < 6u; i++)                      /* several loads, into the next engine */
        turn(EN_PRESET, 1);
    bad += check("browsing on, into another engine: the steps still untouched", t->eng_req != before.eng_req &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_AMODE] == 2);
    hold(B_SAVE);
    bad += check("SAVE held does not open the SAVE pages", ui.home);
    press(B_SAVE);
    bad += check("SAVE tap opens the SAVE pages (on release)", !ui.home && cur_page()->fam == FAM_SAVE);
    /* TOOLS INIT, user preset load: the sound only */
    ui_power_on();
    my_steps(&trk[1]);
    trk[1].p[P_SLCR] = SL_STUT;
    before = trk[1];
    track_select(1);
    set_engine(trk[1].eng_req);                   /* TOOLS INIT */
    bad += check("TOOLS INIT: the sound only (steps and SLICER kept)",
                 !memcmp(trk[1].step, before.step, sizeof before.step) && trk[1].p[P_SLCR] == SL_STUT);
    track_select(0);
    my_steps(&trk[0]);
    trk[0].p[P_SDIV] = 2;
    up_store(3, "MINE");
    bad += check("a user preset keeps its pattern in the record (format unchanged)", up_used(3) && !up_pat_empty(up_rec(3)) &&
                 up_rec(3)->note[0] == 40);
    trk[0].p[P_SDIV] = 0;
    track_defaults_steps(&trk[0]);
    trk[0].step[5].note[0] = 33;
    trk[0].step[5].n = 1;
    before = trk[0];
    up_load(3);
    bad += check("a user preset load: the sound only (steps, DIV kept)", trk[0].user == 4u &&
                 !memcmp(trk[0].step, before.step, sizeof before.step) && trk[0].p[P_SDIV] == 0);
    /* project load: its loads nested in it */
    project_save(1);
    up_load(3);
    project_load(1);
    bad += check("a project load: every load closed", load_depth == 0);
    ui_power_on();
    bad += check("SAMPLE factory browsing has four melodic presets and no PERC",
                 ENGINES[4]->npresets == 4u && str_eq(ENGINES[4]->presets[0].name, "PIANO") &&
                 str_eq(ENGINES[4]->presets[3].name, "SAX"));
    {   /* TRANH (SET 1, preset 1) is gone: both are PIANO aliases, kept for old data, never offered */
        const param_desc_t *sd = &ENGINES[4]->edit[0];
        uint32_t all, pos, k, e, pos0, shown = 0;
        set_engine_of(TSEL, 4);
        pos0 = preset_all_pos(&all);
        TSEL->preset = 1;
        TSEL->p[P_E0] = 1;
        pos = preset_all_pos(&all);
        for (k = 0; k < all; k++)
            if (preset_all_at(k, &e) == 4u)
                shown++;
        e = preset_all_at(pos0 + 1u, &k);
        apply_preset_to(TSEL, 1);
        bad += check("SAMPLE: old SET 1 / preset 1 (TRANH) play PIANO; browsing and knobs skip them",
                     SMP_SETS[1].z0 == SMP_SETS[0].z0 && SMP_SETS[1].nz == SMP_SETS[0].nz && pos == pos0 &&
                     shown == 3u && e == 4u && k == 2u && TSEL->preset == 0u && TSEL->p[P_E0] == 0 &&
                     str_eq(sd->names[1], "PIANO") && enum_step(sd, 0, 1) == 2 && enum_step(sd, 2, 1) == 0 &&
                     enum_step(sd, 1, 2) == 2 && enum_orig(sd, 1) == 0 && enum_orig(sd, 5) == 5 &&
                     enum_orig(&ENGINES[8]->edit[0], 1) == 0);
        bad += check("SAMPLE / GRAIN SET 4 (PERC, retired): a PIANO alias, knobs skip it, USR1..3 stay 5..7",
                     str_eq(sd->names[SMP_SET_PERC], "PIANO") && enum_orig(sd, SMP_SET_PERC) == 0 &&
                     enum_step(sd, 3, 4) == 5 && enum_step(sd, 5, 4) == 3 && str_eq(sd->names[5], "USR1") &&
                     str_eq(sd->names[7], "USR3") && sd->max == 7 &&
                     enum_orig(&ENGINES[8]->edit[0], SMP_SET_PERC) == 0 && SMP_SETS[SMP_SET_PERC].z0 == SMP_SETS[0].z0);
    }
    set_engine_of(t, 0);
    apply_preset_to(t, SMP_SET_PERC);             /* (engine 0: an ANALOG preset 4, not PERC) */
    bad += check("factory preset 4 of another engine stays that engine's", t->eng_req == 0u && t->preset == 4u);
    set_engine_of(t, ENGI_SAMPLE);
    apply_preset_to(t, SMP_SET_PERC);             /* SAMPLE preset 4 (once PERC): the editor's PRESET, a favourite */
    bad += check("SAMPLE preset 4 (once PERC): DRUM's kit", t->eng_req == ENGI_DRUM && t->preset == 0u && perc_kit(t) &&
                 gm_hits_play(t));
    (void)before;
    return bad;
}

static int test_rec(void)
{
    static const uint8_t FAMS[3] = {FAM_TRK, FAM_SEQ, FAM_ARP};
    int bad = 0;
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        ui_power_on();
        track_select(2);
        open_family(FAMS[k]);
        press(B_REC);
        bad += check(k == 0 ? "REC on TRACKS: arms the selected track, PLAY starts" :
                     k == 1 ? "#133 REC on SEQ > STEP: arms the selected track for step recording, PLAY does not start" :
                     "REC on ARP: arms the selected track, PLAY starts",
                     song.rec == 4u && (k == 1u ? transport_req == 0u && msg_is("KEYS WRITE STEPS") : transport_req == 1u));
        song.playing = 1;
        transport_req = 0;
        frames(400);                                   /* (not a double tap: that locks the REC layer) */
        press(B_REC);
        bad += check("REC again disarms (the transport runs on)", song.rec == 0u && transport_req == 0u);
        frames(400);
        hold(B_REC);                                   /* 1.1.5: held, the REC layer (ui_layer.c); the arming stays */
        bad += check("REC held: its layer, closed when let go; no dialog, the arming as it was",
                     ui.confirm == CF_NONE && song.rec == 0u && !ui.layer && !transport_req);
        ui.confirm = 0;
    }
    ui_power_on();
    press(B_REC);
    bad += check("REC on HOME records without changing the screen", ui.home && song.rec == 1u && transport_req == 1u);
    /* STEP page: armed + playing -> the keys record live only */
    ui_power_on();
    open_family(FAM_SEQ);
    bad += check("SEQ opens STEP", cur_page()->scope == SC_STEP);
    press(B_REC);
    song.playing = 1;
    transport_req = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("STEP, armed and playing: a key does not write the cursor step", !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, not armed: the key only plays (nothing written, the cursor stays)",
                 !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 1;
    song.playing = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, armed and stopped: the key writes the cursor step (and moves on)",
                 trk[0].step[0].n == 1 && ui.cursor == 1);
    /* ARP: the arp's notes are recorded, not the key */
    ui_power_on();
    {
        track_t *t = &trk[0];
        uint32_t i, n = 0, notes = 0;
        t->engine = t->eng_req;
        t->p[P_AMODE] = 1;
        t->p[P_AOCT] = 2;
        t->p[P_APROB] = 127;
        song.rec = 1;
        song.playing = 1;
        input_on(t, 60, 100);
        for (i = 0; i < NSTEP; i++)
            n += t->step[i].n;
        bad += check("ARP on: the key held is not recorded", n == 0);
        arp_tick(t, 1);
        for (i = 0; i < NSTEP; i++)
            if (t->step[i].n)
                notes = t->step[i].note[0];
        bad += check("ARP on: the note the arp plays is recorded", notes == 60u);
        t->p[P_AMODE] = 0;
        input_off(t, 60);
    }
    return bad;
}

static uint32_t gated_notes(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].gate != 0;
    return n;
}

static int test_midi(void)
{
    int bad = 0;
    track_t *a, *b;
    ui_power_on();
    song.sel = 2;
    midi_hint = 0;
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 9, 61, 100);
    bad += check("ROUT CH1-4 (default): channel 1 -> part 1, channel 10 ignored",
                 song.g[G_ROUTE] == 0 && midi_sel_on[0][60] == 1u && !midi_sel_on[9][61] && !gated_notes(&trk[2]));
    bad += check("a note into another track tells the UI", midi_hint == 1u);
    frame(); bad += check("MIDI IN -> T1 shown", msg_is("MIDI IN -> T1"));
    song.g[G_ROUTE] = 1;
    midi_event(0x80, 0, 60, 0); midi_event(0x90, 0, 62, 100);
    bad += check("ROUT changes: note-off releases original owner, new note uses selection",
                 !gated_notes(&trk[0]) && midi_sel_on[0][62] == 3u && midi_track(3) == &trk[2]);
    song.sel = 1; midi_event(0x80, 0, 62, 0);
    bad += check("note-off after selection follows the note-on's track", !midi_sel_on[0][62]);
    midi_event(0x80, 9, 61, 0);
    ui_power_on();
    song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 60, 100);
    song.sel = 1;
    midi_event(0x90, 0, 60, 100);
    bad += check("same MIDI channel/pitch on a new selected track releases its old owner",
                 !gated_notes(&trk[0]) && gated_notes(&trk[1]) && midi_sel_on[0][60] == 2u);
    midi_event(0x80, 0, 60, 0);
    midi_event(0x80, 0, 60, 0);
    bad += check("both note-offs after a MIDI ownership change leave neither track held",
                 !gated_notes(&trk[0]) && !gated_notes(&trk[1]) && !midi_sel_on[0][60]);
    ui_power_on();
    song.sel = 2; song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 61, 100);
    song.g[G_ROUTE] = 0;
    midi_event(0x90, 0, 61, 100);
    midi_event(0x80, 0, 61, 0);
    bad += check("same channel/pitch across a ROUT change cannot leave its previous part held",
                 !gated_notes(&trk[2]) && !gated_notes(&trk[0]) && !midi_sel_on[0][61]);
    {
        uint32_t hold;
        for (hold = 0; hold < 2u; hold++) {
            ui_power_on();
            song.g[G_ROUTE] = 1;
            trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = (int16_t)hold;
            midi_event(0x90, 0, 60, 100);
            song.sel = 1;
            midi_event(0x90, 0, 60, 100);
            bad += check("MIDI ownership transfer releases old ARP keys and respects explicit HOLD",
                         !trk[0].arp_phys && trk[0].nheld == hold && midi_sel_on[0][60] == 2u);
        }
    }
    ui_power_on();
    trk[1].p[P_AMODE] = trk[1].p[P_AHOLD] = 1;
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 64, 100);
    song.playing = 1;
    {
        uint32_t i, held = 0;
        for (i = 0; i < MQ; i++) midi_in_event(0x643C9909u);
        midi_in_event(0x003C8908u);                      /* note-off lost to a full ring */
        events_block(CTL);
        for (i = 0; i < NTRK * NVOICE; i++) held += trk[i / NVOICE].v[i % NVOICE].gate;
        bad += check("MIDI overflow releases held notes and latched ARP, keeps transport running",
                     !held && !trk[1].nheld && !trk[1].arp_phys && !midi_sel_on[0][60] &&
                     !midi_sel_on[1][64] && !midi_in_overflow && mi_r == mi_w && song.playing);
        midi_in_event(0x643D9009u);
        events_block(CTL);
        bad += check("MIDI accepts a fresh note after overflow recovery", midi_sel_on[0][61] == 1u && trk[0].v[0].gate);
    }
    {   /* 1.2: MENU > MIDI > MIDI IN (up to 1.1.5 GLO > SYSTEM K3 ROUT): the project's G_ROUTE, its names ROUT's */
        uint32_t k;
        int16_t r0 = song.g[G_ROUTE];
        int ok = MI_TAB[MI_MIDIIN] == MTAB_MIDI && str_eq(MI_NAME[MI_MIDIIN], "MIDI IN") &&
                 menu_n(MI_MIDIIN) == (uint32_t)GP[G_ROUTE].max + 1u;
        for (k = 0; k < menu_n(MI_MIDIIN); k++) {
            menu_put(MI_MIDIIN, k);
            ok &= song.g[G_ROUTE] == (int16_t)k && menu_get(MI_MIDIIN) == k && str_eq(menu_vname(MI_MIDIIN, k), GP[G_ROUTE].names[k]);
        }
        menu_put(MI_MIDIIN, 1u);
        ok &= str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "SEL");
        song.g[G_ROUTE] = r0;
        bad += check("MENU > MIDI > MIDI IN: CH1-4 / SEL / CH5-8 .. CH13-16, the project's ROUT (G_ROUTE)", ok);
    }
    {   /* #192: PRESETS loads into the selected track for good (no preview): MIDI IN SEL plays it on any channel;
         * CH1-4 (the default) keeps channel 1 on track 1 whatever is selected, so browsing on T3 is not heard there */
        uint8_t e1 = trk[0].eng_req, p1 = trk[0].preset, e3, p3;
        int ok;
        ui_power_on();
        song.sel = 2;
        go_home();
        e3 = TSEL->eng_req; p3 = TSEL->preset;
        e1 = trk[0].eng_req; p1 = trk[0].preset;
        turn(EN_PRESET, 1);
        turn(EN_PRESET, 1);
        ok = (TSEL->eng_req != e3 || TSEL->preset != p3) && trk[0].eng_req == e1 && trk[0].preset == p1;
        song.g[G_ROUTE] = 1;                             /* SEL */
        midi_event(0x90, 0, 60, 100);
        ok &= gated_notes(&trk[2]) == 1u && !gated_notes(&trk[0]) && midi_sel_on[0][60] == 3u;
        midi_event(0x80, 0, 60, 0);
        song.g[G_ROUTE] = 0;                             /* CH1-4 */
        midi_event(0x90, 0, 60, 100);
        ok &= gated_notes(&trk[0]) == 1u && !gated_notes(&trk[2]) && midi_sel_on[0][60] == 1u;
        midi_event(0x80, 0, 60, 0);
        midi_event(0x90, 2, 62, 100);                    /* (CH1-4: channel 3 is T3's) */
        ok &= gated_notes(&trk[2]) == 1u && midi_sel_on[2][62] == 3u;
        midi_event(0x80, 2, 62, 0);
        bad += check("#192 PRESETS on T3: MIDI IN SEL plays the loaded sound, CH1-4 channel 1 stays T1 (channel 3 T3)", ok);
    }
    return bad;
}

static int test_save(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    trk[0].p[P_E0] = 5;
    press(B_SAVE);                                /* SAVE: FILE, the projects; again PATTERN, again USER */
    bad += check("SAVE opens FILE (the projects), nothing picked; again PATTERN (the track's)",
                 !ui.home && cur_page()->graph == GR_FILE && ui.act == 0u && (press(B_SAVE), cur_page()->graph == GR_PATTERN));
    press(B_SAVE);
    bad += check("SAVE a third time: USER with SAVE selected, without writing a slot",
                 !ui.home && cur_page()->graph == GR_USER && ui.act == 4u && !up_used(0));
    press(B_OCTUP);
    bad += check("SAVE then OCT+ opens NAME (the automatic name), writes nothing yet",
                 name_on() && nm.kind == NK_USER_SAVE && str_eq(nm.s, nm.ph) && nm.cur == nm.len && !up_used(0));
    press(B_OCTUP);
    {
        char nmb[16], au[16];
        up_name(0, nmb);
        up_auto_name(au, trk[0].eng_req, 0);
        bad += check("..OCT+ again stores the edited sound with that name (SAVE, OCT+, OCT+)",
                     !name_on() && up_used(0) && up_value(up_rec(0), P_E0) == 5 && ui.act == 0u && str_eq(nmb, au));
    }
    press(B_SAVE);
    press(B_SAVE);
    bad += check("SAVE again: FILE again (round; Dah: PRESETS, TOOLS, PHRASES gone)", cur_page()->graph == GR_FILE);
    go_home();
    press(B_SAVE);
    bad += check("SAVE from HOME returns directly to FILE", cur_page()->graph == GR_FILE && ui.act == 0u);
    press(B_SAVE); press(B_SAVE);                 /* (USER) */
    press(B_OCTUP);
    bad += check("direct SAVE still asks before overwriting an occupied slot",
                 ui.confirm == CF_OVR_USER && up_value(up_rec(0), P_E0) == 5);
    press(B_OCTDN);
    ui_power_on();
    song.playing = 1;
    project_save(0);
    bad += check("project save while playing: refused, STOP TO SAVE", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("user preset save while playing: refused", up_store(0, "X") == 2 && !up_used(0));
    song.playing = 0;
    transport_req = 1;
    project_save(0);
    bad += check("a pending PLAY cannot save a project", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("a pending PLAY cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    transport_req = 0;
    chain.armed = 1;
    bad += check("a pending SONG cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    chain.armed = 0;
    song.playing = 1;
    go_page(GR_FILE);                             /* (Dah: FILE, the projects: KNOB 1 the list, OPEN COPY NEW; OCT- held DELETE) */
    ok = act_cols() == 14u && str_eq(act_name(0), "PROJECT") && str_eq(act_name(1), "OPEN") && str_eq(act_name(2), "COPY") &&
         str_eq(act_name(3), "NEW");
    turn(EN_K4, 1);
    ok &= ui.act == 4u && act_ready();
    turn(EN_K2, 1);
    ok &= ui.act == 2u && !act_ready();               /* (OPEN: no project there) */
    bad += check("FILE: KNOB 1 the project, 2 OPEN (a project there only), 3 COPY, 4 NEW", ok);
    stop_transport();
    /* user presets */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_USER)
            break;
    ui.page = (uint8_t)i;
    page_entered();
    {                                                   /* (Dah) the drawn LFO goes with the sound */
        int8_t d0[LDRAW_N];
        uint32_t j;
        for (j = 0; j < LDRAW_N; j++) trkx[song.sel].draw[j] = (int8_t)(j * 7 - 50);
        trkx[song.sel].smooth = 0;
        memcpy(d0, trkx[song.sel].draw, sizeof d0);
        up_store(5, "DRAWN");
        memset(trkx[song.sel].draw, 0, sizeof d0);
        trkx[song.sel].smooth = 1;
        up_load(5);
        bad += check("USER preset: LFO DRAW's points and SMOOTH stored and loaded with the sound (Dah)",
                     !memcmp(d0, trkx[song.sel].draw, sizeof d0) && trkx[song.sel].smooth == 0);
        up_ui(1u, 5);                                   /* (erased again) */
    }
    up_store(4, "OLD");
    ui.uslot = 4;
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("USER SAVE over a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_USER && ui.confirm_trk == 4u);
    press(B_OCTUP);
    bad += check("YES: NAME with the sound's name (not the old slot's)", name_on() && !str_eq(nm.s, "OLD"));
    press(B_OCTUP);
    {
        char nm[16];
        up_name(4, nm);
        bad += check("OCT+ stores the sound there", ui.confirm == CF_NONE && !str_eq(nm, "OLD"));
    }
    song.playing = 1;
    ui.msg_t = 0;
    turn(EN_K3, 1);                               /* ERASE */
    press(B_OCTUP);
    bad += check("USER ERASE while playing: STOP TO SAVE", msg_is("STOP TO SAVE") && up_used(4));
    stop_transport();
    return bad;
}

static void go_title(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t oct_leds_seen(int all)            /* over 1 s: the OCT LED bits lit at some point (all: throughout) */
{
    uint32_t k, any = 0, every = 3u | OCT_BREATH;
    for (k = 0; k < 64u; k++) {
        frame();
        any |= oct_leds();
        every &= oct_leds();
    }
    return all ? every : any;
}

/* the action pages: the knobs pick, OCT+ does it, OCT- cancels / goes HOME; the LEDs; chords */
static int test_actions(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    ui_power_on();
    press(B_OCTUP);
    bad += check("HOME: OCT+ shifts the octave (lit)", song.octave == 1 && oct_leds() == 2u);
    up_store(0, "ACT");                           /* (Dah: PHRASES gone: MY PRESETS' LOAD the action) */
    t->user = 0;
    go_page(GR_USER);
    ui.uslot = 0;
    bad += check("MY PRESETS, nothing picked: OCT+ and OCT- dark (Dah: no BACK)", oct_leds_seen(0) == 0u);
    turn(EN_K2, 1);
    bad += check("..LOAD picked: OCT+ breathes (a sound to load), OCT- lit (cancels)", oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    {
        char a[16], b[16];
        foot_hint(a, b);
        bad += check("..the footer: OCT+ LOAD, OCT- CANCEL", str_eq(a, "OCT+ LOAD") && str_eq(b, "OCT- CANCEL"));
    }
    press(B_OCTUP);
    bad += check("..OCT+ loads it, the octave stays", t->user == 1u && msg_is("LOADED U01") && song.octave == 1 && !ui.home);
    t->user = 0;
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* both together (the UPDATE MODE hold): nothing */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN];
    host_pressed |= 1u << panel.btn[B_OCTDN];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTDN]);
    frame();
    bad += check("OCT- + OCT+ together: no load, no HOME, no octave change (Dah: ASSIGN, a HOME knob)",
                 !t->user && !ui.home && song.octave == 1 && ka.st == 1u);
    press(B_OCTDN);
    bad += check("  OCT-: ASSIGN off", !ka.st && song.octave == 1);
    go_home();
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* pressed on HOME, let go on MY PRESETS: no load */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    go_page(GR_USER);
    ui.act = 2;                                   /* (LOAD picked) */
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("a press that began elsewhere does not load", !t->user && song.octave == 2);
    press(B_OCTDN);
    bad += check("MY PRESETS: OCT- cancels the pick (Dah: no BACK; the octave stays)", !ui.home && !ui.act && song.octave == 2);
    press(B_OCTDN);
    bad += check("..again: nothing (the octave stays)", !ui.home && song.octave == 2);
    /* the dialogs and the menu */
    open_family(FAM_SEQ);
    my_steps(t);
    confirm_open(CF_INIT_SOUND, song.sel);
    frame();
    bad += check("a dialog: OCT+ breathes, OCT- lit", ui.confirm == CF_INIT_SOUND && oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    bad += check("..it answers on release, not on the press", ui.confirm == CF_INIT_SOUND);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("..OCT+ let go: done", ui.confirm == CF_NONE && msg_is("SOUND INIT"));
    bad += check("back on SEQ: the octave LEDs again", oct_leds() == 2u);
    return bad;
}

static int test_tracks(void)
{
    int bad = 0;
    track_t before;
    ui_power_on();
    open_family(FAM_TRK);
    before = trk[0];
    turn(EN_PRESET, 1);
    bad += check("#94 MIXER: the PRESETS knob (Dah) loads the next sound; the steps and the page stay",
                 trk[0].preset != before.preset && !memcmp(trk[0].step, before.step, sizeof before.step) &&
                 cur_page()->graph == GR_TRK);
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right: MUTE ON (the selected track stays)", trk[0].p[P_MUTE] == 1 && song.sel == 0);
    turn(EN_K1, -3);
    bad += check("KNOB 1 changes LEVEL only (MUTE stays)", trk[0].p[P_MUTE] == 1 && trk[0].p[P_LEVEL] == before.p[P_LEVEL] - 3);
    turn(EN_K2, 2);
    bad += check("KNOB 2 changes PAN", trk[0].p[P_PAN] == before.p[P_PAN] + 2);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left: MUTE OFF", trk[0].p[P_MUTE] == 0);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM selects the track", song.sel == 1);
    return bad;
}

/* key k of colour black (1) / white (0) at place p (seq.c key_place) */
static uint32_t key_at(int black, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == p)
            return k;
    return 0;
}
static void tap_key(uint32_t k)                   /* a key down for a frame, then up */
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << k);
    frame();
}
static uint32_t lane_steps(const track_t *t, uint32_t l)   /* bit i: lane l strikes at step i (0..31) */
{
    uint32_t i, m = 0;
    for (i = 0; i < 32u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}

static int test_grid(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, k, leds, ok;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    open_family(FAM_SEQ);
    frame();
    ok = grid_on() && song.grid == 0u;                  /* (Dah: not armed, the keys play the drums) */
    song.rec = (uint8_t)(1u << song.sel);               /* (armed: the keys are the steps) */
    frame();
    bad += check("SEQ > STEP on a DRUM track is the grid; not armed the keys play, armed they are the steps (Dah)",
                 ok && grid_on() && song.grid == 1u && cur_page()->scope == SC_STEP);
    bad += check("the lanes: KICK SNARE CLAP HATCL HATOP TOM RIM BELL on GM 36 38 39 42 46 45 37 56",
                 str_eq(drum_lane_name(t, 0), "KICK") && str_eq(drum_lane_name(t, 7), "BELL") && DRUM_LANE_NOTE[3] == 42 &&
                 DRUM_LANE_NOTE[6] == 37 && drum_lane(41) == DV_TOM && drum_lane(49) == DV_BELL && drum_lane(35) == DV_KICK);
    tap_key(key_at(1, 1));
    bad += check("black key 2: lane 2 (SNARE) selected", ui.lane == 1u);
    tap_key(key_at(0, 4));
    bad += check("white key 5: SNARE on step 5, the cursor there", t->step[4].hit == 1u << DV_SNARE && t->step[4].time == ST_NOTE &&
                 !t->step[4].n && ui.cursor == 4u);
    leds = grid_leds();
    bad += check("LEDs: white key 5 lit, black key 2 lit, ACC and the page keys dark (LEN 16)",
                 (leds >> key_at(0, 4) & 1u) && (leds >> key_at(1, 1) & 1u) && !(leds >> key_at(0, 3) & 1u) &&
                 !(leds >> key_at(1, 0) & 1u) && !(leds >> key_at(1, GK_ACC) & 1u) && !(leds >> key_at(1, GK_PGUP) & 1u));
    song.playing = 1;
    t->seq_idx = 4;
    leds = grid_leds();
    ok = !(leds >> key_at(0, 4) & 1u);
    t->seq_idx = 6;
    leds = grid_leds();
    ok &= (leds >> key_at(0, 6) & 1u) && !(leds >> key_at(0, 4) & 1u) == 0u;
    song.playing = 0;
    bad += check("LEDs: the step playing inverted (a hit goes dark, an empty step lights)", ok);
    tap_key(key_at(0, 4));
    bad += check("white key 5 again: off, an empty step (REST)", !t->step[4].hit && t->step[4].time == ST_REST);
    /* ACC held: the white keys set accents, their LEDs show them */
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    frame();
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok = t->step[0].hit == 1u << DV_SNARE && t->step[0].acc == 1u << DV_SNARE && (leds >> key_at(0, 0) & 1u) &&
         (leds >> key_at(1, GK_ACC) & 1u);
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok &= t->step[0].hit == 1u << DV_SNARE && !t->step[0].acc && !(leds >> key_at(0, 0) & 1u);
    fm1_in.notes = 0;
    frame();
    leds = grid_leds();
    ok &= (leds >> key_at(0, 0) & 1u) != 0u;
    bad += check("ACC held: a tap adds the hit accented, again drops the accent only; the LEDs show accents", ok);
    /* the knobs: STEP LANE HIT ACC */
    turn(EN_K1, 2);
    turn(EN_K2, 1);
    turn(EN_K3, 1);
    ok = ui.cursor == 2u && ui.lane == 2u && t->step[2].hit == 1u << DV_CLAP;
    turn(EN_K4, 1);
    ok &= t->step[2].acc == 1u << DV_CLAP;
    turn(EN_K3, -1);
    ok &= !t->step[2].hit && !t->step[2].acc && t->step[2].time == ST_REST;
    turn(EN_K4, 1);
    ok &= t->step[2].hit == 1u << DV_CLAP && t->step[2].acc == 1u << DV_CLAP;
    bad += check("KNOB 1 STEP, 2 LANE, 3 HIT on / off, 4 ACC (an accent adds the hit)", ok);
    press(B_EDIT);
    bad += check("EDIT: the cursor step cleared, on to the next", t->step[2].time == ST_REST && !t->step[2].hit && ui.cursor == 3u &&
                 grid_on());
    /* pages: LEN 32, the page keys */
    t->p[P_SLEN] = 32;
    leds = grid_leds();
    tap_key(key_at(1, GK_PGUP));
    ok = ui.bank == 1u && ui.cursor == 19u && (leds >> key_at(1, GK_PGUP) & 1u) && (leds >> key_at(1, GK_PGDN) & 1u);
    tap_key(key_at(0, 15));
    ok &= t->step[31].hit == 1u << DV_CLAP;
    tap_key(key_at(1, GK_PGDN));
    ok &= ui.bank == 0u && ui.cursor == 15u;
    t->p[P_SLEN] = 12;
    frame();
    tap_key(key_at(0, 13));
    ok &= !t->step[13].hit && !t->step[13].n;
    bad += check("pages: black keys 11 / 10 up / down (the cursor along); past LEN a key does nothing", ok);
    t->p[P_SLEN] = 16;
    /* a step's GM notes on their lanes */
    t->step[8] = (step_t){{41, 49, 36, 0}, 3, ST_NOTE, SF_ACCENT, 96, 0, 0};
    ok = step_lanes(&t->step[8]) == ((1u << DV_TOM) | (1u << DV_BELL) | (1u << DV_KICK)) &&
         step_accents(&t->step[8]) == step_lanes(&t->step[8]);
    grid_hit(t, 8, DV_TOM, 0);
    ok &= t->step[8].n == 1 && t->step[8].note[0] == 49 && t->step[8].hit == 1u << DV_KICK && (t->step[8].flags & SF_ACCENT);
    grid_acc(t, 8, DV_KICK, 0);
    ok &= !t->step[8].acc && !(t->step[8].flags & SF_ACCENT) && t->step[8].n == 1 && (step_lanes(&t->step[8]) >> DV_BELL & 1u);
    grid_acc(t, 8, DV_BELL, 1);
    ok &= !t->step[8].n && t->step[8].hit == ((1u << DV_KICK) | (1u << DV_BELL)) && t->step[8].acc == 1u << DV_BELL;
    bad += check("GM notes on the grid: a low tom / crash on their lanes; an edit makes the lane its hit", ok);
    /* the keys in the audio ISR: on the grid only the lane keys play */
    kb_prev = 0;
    fm1_in.notes = (1u << key_at(0, 3)) | (1u << key_at(1, 3)) | (1u << key_at(1, GK_ACC));
    keyboard_block();
    ok = kb_note[key_at(0, 3)] == KB_SILENT && kb_note[key_at(1, 3)] == 42u && kb_note[key_at(1, GK_ACC)] == KB_SILENT;
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("grid keys in the ISR: white keys and ACC silent, black key 4 plays HAT CL (42)", ok);
    /* live recording on the grid: armed and playing, a lane key records its hit at the play head */
    track_defaults_steps(t);
    song.rec = 1;
    song.playing = 1;
    t->seq_idx = 5;
    t->seq_pos = 0;
    fm1_in.notes = 1u << key_at(1, 4);
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = t->step[5].hit == 1u << DV_HATO && !t->step[5].n && t->rskip_n == 0u;
    t->seq_pos = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 5) - 10u;   /* late in step 6: into step 7 */
    fm1_in.notes = 1u << key_at(1, 0);
    keyboard_block();
    ok &= t->step[6].hit == 1u << DV_KICK && t->rskip_n == 1u && t->rskip[0] == 36u;
    rec_hold(t, 7, 16);
    fm1_in.notes = 0;
    keyboard_block();
    ok &= t->step[7].time == ST_REST && !t->rh_n;
    bad += check("REC on the grid: lane keys record hits, quantised (the next step late), no TIE holds", ok);
    /* live recording elsewhere (HOME): the GM keys; a lane's note a hit, another GM drum a note */
    go_home();
    frame();
    t->seq_idx = 9;
    t->seq_pos = 0;
    fm1_in.notes = (1u << 7) | (1u << 13) | (1u << 6);   /* C 36 KICK, F# 42 HAT CL, E 35 (a kick 2 st down) */
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = !song.grid && t->step[9].hit == ((1u << DV_KICK) | (1u << DV_HATC)) && t->step[9].n == 1 && t->step[9].note[0] == 35 &&
         step_lanes(&t->step[9]) == ((1u << DV_KICK) | (1u << DV_HATC));
    song.rec = 0;
    song.playing = 0;
    bad += check("REC on HOME: GM keys quantised into the grid (35 a note on the KICK lane)", ok);
    /* 12 BEAT's notes into the DRUM track fill the grid (step_to_grid; Dah: as SAVE > PHRASES did, gone) */
    track_defaults_steps(t);
    load_pat16(t, PATTERNS[11].note, PATTERNS[11].flags);
    ok = t->p[P_SLEN] == 16 && lane_steps(t, DV_KICK) == ((1u << 0) | (1u << 6) | (1u << 8) | (1u << 11)) &&
         lane_steps(t, DV_SNARE) == ((1u << 4) | (1u << 12)) && lane_steps(t, DV_HATO) == 1u << 14 &&
         lane_steps(t, DV_HATC) == 0xA6AEu && t->step[0].acc == 1u << DV_KICK && t->step[4].acc == 1u << DV_SNARE &&
         t->step[1].acc == 0u && t->step[16].time == ST_REST;
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].n == 0u && !(t->step[i].flags & SF_ACCENT);
    bad += check("12 BEAT's notes into a DRUM track: the grid (hits, the accents on 1 5 9 13)", ok);
    open_family(FAM_SEQ);
    ui.lane = DV_KICK;
    cursor_set(0);
    leds = grid_leds();
    ok = 1;
    for (i = 0; i < 16u; i++)
        ok &= (leds >> key_at(0, i) & 1u) == ((0x0941u >> i) & 1u);
    bad += check("..its KICK lane on the white key LEDs (1 7 9 12)", ok);
    {   /* the sound changes, the grid stays and plays as GM notes on another engine */
        step_t keep[NSTEP];
        memcpy(keep, t->step, sizeof keep);
        go_home();
        turn(EN_PRESET, 1);
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        ok = !memcmp(keep, t->step, sizeof keep) && !grid_on();
        t->seq_hold = 0;
        t->seq_n = 0;
        seq_step(t, &t->step[0], 0, div_samples((uint32_t)t->p[P_SDIV]), 0);
        ok &= t->seq_n == 1u && t->seq_notes[0] == 36u;
        seq_release(t);
        bad += check("a sound load keeps the grid; on ANALOG its hits play as their GM notes", ok);
        set_engine_of(t, ENGI_DRUM);
        t->engine = t->eng_req;
    }
    /* the menu: the keys play again */
    go_page(GR_ROLL);
    song.rec = (uint8_t)(1u << song.sel);               /* (Dah: armed, the grid's) */
    frame();
    k = song.grid;
    hold(B_HOME);
    frame();
    bad += check("HOME held (the menu): the keys are no longer the grid's", k == 1u && ui.menu && !song.grid);
    menu_close();
    return bad;
}

static int test_screen(void)
{
    int bad = 0;
    ui_power_on();
    ui_say("STOP TO SAVE", "");
    ui.force = 1;
    ui_draw();
    bad += check("a message keeps the header's transport and BPM (drawn right of x 80)", ui.msg_t != 0);
    ui.uboot = 3;
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    bad += check("UPDATE MODE countdown drawn over the menu", ui.force == 0);
    ui.uboot = 0;
    ui.menu = 0;
    return bad;
}

/* 1.2 (Discussion #90): sound categories. The factory table covers every preset (Dah: PRESETS > LIST gone); the NAME
 * screen's KNOB 3 sets a user preset's
 * category (from the sound's own), written with the name and kept by a rename; a record of before (version 2) gets
 * one by a rename, its sound unchanged; PROJECT's KNOB 3 a random name */
static int test_categories(void)
{
    int bad = 0, ok = 1;
    uint32_t e, k, total, pos, n, i;
    char b[16], s1[16];
    ui_power_on();
    memset(&favorites, 0, sizeof favorites);
    memset(up_bank, 0, sizeof up_bank);
    for (e = 0; e < NENGINES; e++) {
        const char *c = PRESET_CAT[e];
        if (!eng_ok(e) || e == ENGI_DIGITAL) continue;    /* (DIGITAL: FELUCCA_FM4 builds only, its sounds no category) */
        ok &= str_len(c) == ENGINES[e]->npresets;
        for (k = 0; k < ENGINES[e]->npresets; k++)
            ok &= preset_cat(e, k) >= CAT_BASS && preset_cat(e, k) <= CAT_OTHER;
    }
    bad += check("categories: every factory preset of every engine has one (the table's strings == the preset counts)",
                 ok && preset_cat(0, 2) == CAT_BASS && preset_cat(ENGI_DRUM, 0) == CAT_DRUM && preset_cat(ENGI_FM6, 0) == CAT_KEYS &&
                 preset_cat(ENGI_DRUM, 9) == CAT_DRUM && preset_cat(0, 99) == CAT_OTHER);
    /* NAME: USER SAVE takes the sound's category; KNOB 3 changes it; written with the name */
    select_engine(0);
    apply_preset(2);                                       /* SQR BASS */
    song.playing = 0;
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_OCTUP);
    bad += check("NAME: USER SAVE starts at the sound's category (a factory BASS)", name_on() && nm.cat == CAT_BASS);
    turn(EN_K3, 1);
    ok = nm.cat == CAT_LEAD;
    turn(EN_K3, -5);
    ok &= nm.cat == CAT_BASS;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("  KNOB 3 steps the category (no wrap below BASS); OCT+ writes it with the name",
                 ok && !name_on() && up_used(4) && up_cat(4) == CAT_LEAD && up_rec(4)->packed[UP_CAT_AT] == CAT_LEAD);
    go_page(GR_USER);
    ui.uslot = 4;
    press(B_EDIT);
    ok = name_on() && nm.kind == NK_USER_RENAME && nm.cat == CAT_LEAD;
    turn(EN_K3, 3);                                        /* LEAD -> KEYS */
    press(B_OCTUP);
    up_name(4, b);
    bad += check("  a rename starts at the slot's own category and writes the new one, the sound as it was",
                 ok && up_cat(4) == CAT_KEYS && up_engine(4) == 0u);
    up_load(4);
    name_open(NK_USER_SAVE, 5);
    ok = name_on() && nm.cat == CAT_KEYS;
    press(B_OCTDN);
    bad += check("  saving a sound loaded from a user preset starts at that preset's category", ok && !name_on() && !up_used(5));
    {   /* a record of before categories (version 2: 16-bit values) */
        up_rec_t *r = up_rec(9);
        int16_t v0[P_COUNT], v1[P_COUNT];
        uint32_t c0;
        memset(r, 0, sizeof *r);
        r->used = UP_USED; r->ver = 2; r->engine = ENGI_DRUM; r->np = 72;
        str_cpy(r->name, "OLD KIT", 12);
        for (i = 0; i < 72u; i++) r->p[i] = (int16_t)(i % 100u);
        up_bank[0].magic = UP_BANK_MAGIC; up_bank[0].rsize = sizeof(up_rec_t); up_bank[0].nslot = UP_PER_BANK;
        c0 = up_cat(9);
        up_values(r, v0);
        ok = up_rename_cat(9, "OLD KIT", CAT_FX) != 1;
        up_values(r, v1);
        bad += check("  a version 2 record: listed by its engine (DRUM); a rename with a category makes it version 4, same sound",
                     c0 == CAT_DRUM && ok && r->ver == UP_VER && up_cat(9) == CAT_FX && !memcmp(v0, v1, sizeof v0));
        r->ver = 1; r->engine = ENGI_PHYS;
        bad += check("  a version 1 PHYS record keeps its version (MODEL 2 = DUST): no category",
                     (up_set_cat(r, CAT_PAD), r->ver == 1u && up_cat(9) == cat_of_engine(ENGI_PHYS)));
        memset(r, 0, sizeof *r);
    }
    /* PROJECT: KNOB 3 a random name */
    go_page(GR_SLOTS);
    name_open(NK_PROJ_SAVE, 1);
    turn(EN_K3, 1);
    str_cpy(s1, nm.s, sizeof s1);
    ok = nm.len >= 8u && nm.len <= 12u && nm.cur == nm.len && str_len(s1) == nm.len;
    for (i = 0, k = 0; i < 16u; i++)
        k |= (uint32_t)(str_len(NM_RA[i]) > 5u || str_len(NM_RB[i]) > 6u);
    turn(EN_K3, 1);
    bad += check("NAME: PROJECT's KNOB 3 types a random name (adjective + noun, at most 12), another one each detent",
                 ok && !k && !str_eq(s1, nm.s) && nm.len <= 12u && strchr(nm.s, ' '));
    name_close();
    memset(up_bank, 0, sizeof up_bank);
    memset(&favorites, 0, sizeof favorites);
    return bad;
}

static int test_display_preferences(void)
{
    int bad = 0, k;
    track_t sounds[NTRK];
    uint16_t before[240 * 240];
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_COLOR;
    ui.force = 1; ui_draw();
    memcpy(before, host_screen, sizeof before);
    settings.palette = 5; palette_set(5);
    press(B_OCTUP);
    bad += check("COLOR OCT+ previews the next palette (PAPER, light) without closing the menu",
                 settings.palette == 6 && T_BG == UI_PALETTES[6].bg && ux.light && ui.menu == 1 && !song.octave &&
                 memcmp(before, host_screen, sizeof before) && !memcmp(sounds, trk, sizeof sounds));
    turn(EN_K1, 1);
    bad += check("COLOR KNOB 1 steps on to HI-CON, NIGHT (#50: true black), MONO (1.0.2: black and white), then wraps to GREY",
                 settings.palette == 7 && (turn(EN_K1, 1), settings.palette == 8u) && T_BG == 0u &&
                 str_eq(UI_PALETTES[8].name, "NIGHT") && (turn(EN_K1, 1), settings.palette == UI_BW_INDEX) &&
                 str_eq(UI_PALETTES[UI_BW_INDEX].name, "MONO") && T_BG == 0u && T_TEXT == 0xFFFFu &&
                 (turn(EN_K1, 1), settings.palette == UI_GREY_INDEX) && str_eq(UI_PALETTES[UI_GREY_INDEX].name, "GREY"));
    settings.lowcut = 2;
    ui.menu_sel = MI_LOWCUT;
    press(B_OCTUP);
    k = settings.lowcut == 2u;                         /* (OCT+ stops at the end, as the knobs) */
    press(B_OCTDN);
    k &= settings.lowcut == 1u && fx_lowcut == 1u;
    press(B_OCTDN);
    bad += check("the expanded menu retains all three SPEAKER modes (OCT+ / OCT- step them, stopping at the ends)",
                 k && settings.lowcut == 0 && !fx_lowcut && ui.menu == 1);
    {   /* #42: SPEAKER EQ FLAT (the old OFF, stored 0) LOWCUT BASS+; KNOB 1 steps them, the audio follows at once */
        int ok = str_eq(MI_NAME[MI_LOWCUT], "SPEAKER EQ") && str_eq(SPK_EQ[0], "FLAT") && str_eq(SPK_EQ[1], "LOWCUT") &&
                 str_eq(SPK_EQ[2], "BASS+");
        turn(EN_K1, 1); ok &= settings.lowcut == 1u && fx_lowcut == 1u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u && fx_lowcut == 2u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u;                  /* (KNOB 1 stops at the ends) */
        turn(EN_K1, -1); turn(EN_K1, -1); ok &= settings.lowcut == 0u && !fx_lowcut;
        bad += check("#42 SPEAKER EQ: FLAT (old OFF) / LOWCUT / BASS+ on KNOB 1, fx_lowcut follows", ok);
    }
    press(B_OCTDN);
    k = ui.menu == 1 && settings.lowcut == 0u;         /* (OCT- at the first value: nothing, the menu stays) */
    press(B_HOME);
    bad += check("HOME leaves display preferences without changing musical state (OCT- no longer closes)",
                 k && !ui.menu && settings.palette == UI_GREY_INDEX && !memcmp(sounds, trk, sizeof sounds));
    return bad;
}

/* 1.0.5: the MENU in tabs (ui_menu.c, menu_items.c MI_TAB): every tab 1..MTAB_ROWS rows in a run, each fitting the
 * page in S and LARGE; ALGORITHM between the tabs (wraps, back at a tab's last row), PRESETS within one (wraps), any of
 * KNOB 1..4 and OCT+ / OCT- change a value, OCT+ opens CALIBRATION and ABOUT, HOME closes (OCT- not); reopened at the
 * same tab and row; the tabs slide with ANIM ON (only their strip drawn again), snap with ANIM OFF */
static void wdt_ms(void) { fm1_ms++; }
static void wdt_key(void) { fm1_ms++; if (fm1_ms % 50u == 0u) fm1_in.notes = 1u; }   /* a key pressed after a moment */
static int test_menu_tabs(void)
{
    int bad = 0, ok = 1;
    uint32_t t, i, n;
    for (t = 0; t < MTAB_COUNT; t++) {
        uint32_t f = mtab_first(t);
        n = mtab_rows(t);
        ok &= n >= 1u && n <= MTAB_ROWS && f < MI_COUNT;
        for (i = 0; i < MI_COUNT; i++)                  /* its rows in one run */
            ok &= (MI_TAB[i] == t) == (i >= f && i < f + n);
    }
    ui_prefs = 0;                                       /* (a list: 5 rows in view at least, LARGE too; more scroll) */
    ok &= (int32_t)(240 - HOME_FOOT_H - (HOME_HEAD_H + 3 + HOME_CARD_H + 4) - 2) - 2 * MP_PAD >= 5 * (mr_h() + 2) - 2;
    ui_prefs = PREF_LARGE;
    ok &= (int32_t)(240 - HOME_FOOT_H - (HOME_HEAD_H + 3 + HOME_CARD_H + 4) - 2) - 2 * MP_PAD >= 5 * (mr_h() + 2) - 2;
    for (i = 0; i < MI_COUNT; i++) ok &= MI_TAB[i] < MTAB_COUNT;
    ui_prefs = 0;
    bad += check("MENU tabs: DISPLAY CONTROL AUDIO MIDI SYSTEM (8 8 8 8 7 rows: Dah SYNC SWING; 1.6.1 2ND CORE, a FELUCCA_CPU2=0 build SYSTEM 6; 1.5.5: SELECT, T1..T4 CHANNEL, AUTO SELECT, SCREEN WAKE), 1..8 each in a run, a list "
                 "in the page (S, LARGE)",
                 ok && MTAB_COUNT == 5u && str_eq(MTAB_NAME[0], "DISPLAY") && str_eq(MTAB_NAME[MTAB_MIDI], "MIDI") &&
                 mtab_rows(MTAB_DISPLAY) == 8u && MI_TAB[MI_HOME] == MTAB_DISPLAY && mtab_rows(MTAB_CONTROL) == 8u && MI_TAB[MI_SELECT] == MTAB_CONTROL && mtab_rows(MTAB_AUDIO) == 8u &&
                 mtab_rows(MTAB_MIDI) == 8u && MI_TAB[MI_MFOLLOW] == MTAB_MIDI && MI_TAB[MI_SCRWAKE] == MTAB_MIDI && MI_SCRWAKE + 1u == MI_LCLEAR && MI_TAB[MI_LCLEAR] == MTAB_MIDI && MI_TAB[MI_TCH4] == MTAB_MIDI && mtab_rows(MTAB_SYSTEM) == 6u + FELUCCA_CPU2 &&
#if FELUCCA_CPU2
                 MI_TAB[MI_CPU2] == MTAB_SYSTEM &&
#endif
                 MI_TAB[MI_TUNE] == MTAB_AUDIO &&   /* (1.5: LEARN CLEAR, HELP) */
                 MI_TAB[MI_MIDIIN] == MTAB_MIDI && MI_TAB[MI_INFO] == MTAB_SYSTEM && MI_INFO >= MI_VALUES);

    ui_power_on();
    hold(B_HOME);
    ok = ui.menu == 1 && ui.menu_sel == MI_COLOR && menu_tab() == MTAB_DISPLAY;
    for (i = 0; i < 7u; i++) turn(EN_SELECT, 1);
    ok &= ui.menu_sel == MI_HOME;
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_COLOR;  /* (wraps within the tab) */
    turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_HOME;
    turn(EN_SELECT, -1); turn(EN_SELECT, -1); turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_LEDS;
    bad += check("MENU: PRESETS moves within the tab and wraps there (COLOR .. HOME)", ok);
    turn(EN_ALGO, 1); ok = ui.menu_sel == MI_HOLD && menu_tab() == MTAB_CONTROL;
    turn(EN_SELECT, 1); turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_LATCH;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LOWCUT && menu_tab() == MTAB_AUDIO;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_MIDIIN && menu_tab() == MTAB_MIDI;   /* (1.2) */
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_TCH1;    /* (1.5.5: T1..T4 CHANNEL) */
    turn(EN_SELECT, -1); turn(EN_SELECT, -1); ok &= ui.menu_sel == MI_LCLEAR;   /* (1.5: LEARN CLEAR, the last: round) */
    turn(EN_SELECT, 1); ok &= ui.menu_sel == MI_MIDIIN;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;
    turn(EN_ALGO, -1); turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LOWCUT;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LATCH;   /* (back at the row left there) */
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_LEDS;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_SERIAL && menu_tab() == MTAB_SYSTEM;   /* (wraps) */
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LEDS;
    bad += check("MENU: ALGORITHM steps between the tabs and wraps, each tab back at its last row", ok && ui.menu == 1);
    turn(EN_ALGO, 1); turn(EN_SELECT, -1);            /* CONTROL: from FX LATCH up to KNOB ACCEL */
    ok = ui.menu_sel == MI_ACCEL && !(ui_prefs & PREF_ACCEL);
    for (i = 0; i < 4u; i++) {                         /* any of KNOB 1..4: right ON, left OFF */
        turn(EN_K1 + i, 1); ok &= (ui_prefs & PREF_ACCEL) != 0u;
        turn(EN_K1 + i, -1); ok &= !(ui_prefs & PREF_ACCEL);
    }
    turn(EN_ALGO, 1); turn(EN_SELECT, 1);             /* AUDIO: USB LEVEL */
    ok &= ui.menu_sel == MI_USB;
    turn(EN_K4, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    turn(EN_K2, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;
    bad += check("MENU: any of KNOB 1..4 changes the selected row's value in any tab, the menu stays", ok && ui.menu == 1);
#if FELUCCA_CPU2
    {   /* 1.6.1: 2ND CORE (Dah: ON by default, favorites.c DAH_MIG); up ON, down OFF, each with a
         * restart message; the value in words, not a switch (ui_menu.c MI_TEXT) */
        uint8_t p2 = ui_prefs2;
        ui_prefs2 = 0x7Fu;                             /* (1.6's bits all set but bit 7) */
        ok = menu_get(MI_CPU2) == 0u && str_eq(menu_vname(MI_CPU2, 0), "OFF") &&
             str_eq(menu_vname(MI_CPU2, 1), "ON") && MI_TEXT(MI_CPU2) && !MI_TEXT(MI_HELP);
        ui.menu_sel = MI_CPU2; ui.msg_t = 0;
        press(B_OCTUP);
        ok &= menu_get(MI_CPU2) == 1u && ui_prefs2 == 0xFFu && msg_is("2ND CORE ON: RESTART");
        ui.msg_t = 0;
        press(B_OCTDN);
        ok &= menu_get(MI_CPU2) == 0u && ui_prefs2 == 0x7Fu && msg_is("2ND CORE OFF: RESTART");
        ui_prefs2 = p2; ui.msg_t = 0;
        bad += check("MENU > SYSTEM > 2ND CORE (1.6.1): OFF / ON in words, a restart "
                     "message each way", ok && ui.menu == 1);
    }
#endif

    /* OCT+ / OCT-: the next / previous value on every kind of row (as the knobs: stopping at the ends; COLOR wraps) */
    ok = 1;
    for (i = 0; i < MI_VALUES; i++) {
        uint32_t n = menu_n(i), v, j, want;
        if (!menu_valued(i))                           /* (1.5: LEARN CLEAR, an action) */
            continue;
        uint8_t seen[128] = {0};                       /* (1.2: TUNE has 101 values) */
        ui.menu_sel = (uint8_t)i;
        for (j = 0; j <= n; j++)                       /* down to the first value (COLOR: round) */
            press(B_OCTDN);
        seen[menu_get(i) & 127u] = 1;
        for (j = 0; j <= n; j++) {                     /* up through every value */
            want = menu_step(i, 1);
            press(B_OCTUP);
            ok &= menu_get(i) == want && ui.menu == 1;
            seen[menu_get(i) & 127u] = 1;
        }
        for (j = 0; j < 128u; j++)
            ok &= seen[j] == (j < n);
        for (j = 0; j <= n; j++) {                     /* and down again */
            want = menu_step(i, -1);
            press(B_OCTDN);
            ok &= menu_get(i) == want && ui.menu == 1;
        }
        v = menu_get(i);                               /* the ends: COLOR wraps, the rest stop */
        if (i == MI_COLOR) {
            menu_put(i, n - 1u); press(B_OCTUP); ok &= menu_get(i) == 0u;
            press(B_OCTDN); ok &= menu_get(i) == n - 1u;
        } else {
            press(B_OCTDN); ok &= menu_get(i) == v;
            for (j = 0; j < n; j++) press(B_OCTUP);
            v = menu_get(i);
            press(B_OCTUP); ok &= menu_get(i) == v && v != menu_step(i, -1);
        }
        want = menu_step(i, -1);                       /* a knob steps as OCT does */
        turn(EN_K3, -1); ok &= menu_get(i) == want && ui.menu == 1;
    }
    ui_prefs = 0; settings_hold = HOLD_DEF; settings_leds = LEDS_DIM; ui_style = ST_FLAT;
    settings.lowcut = 0; fx_lowcut = 0; menu_put(MI_COLOR, UI_GREY_INDEX);
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    bad += check("MENU: OCT+ / OCT- step every value row up / down (COLOR wraps, the rest stop), the menu stays", ok);
    {   /* the ON / OFF rows: up (a knob right, OCT+) is ON, the switch's knob to the right, whichever bit it is */
        static const uint8_t SW[] = {MI_LARGE, MI_ANIM, MI_ACCEL, MI_LATCH, MI_BPMLOCK, MI_SERIAL, MI_RESTORE};
        ok = 1;
        for (i = 0; i < NELEM(SW); i++) {
            ui.menu_sel = SW[i];
            press(B_OCTDN); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            press(B_OCTUP); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
            turn(EN_K2, -1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            turn(EN_K2, 1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
        }
        ui.menu_sel = MI_STYLE;
        press(B_OCTUP); ok &= ui_style == ST_LINE;     /* (the others: up the second, LINE) */
        press(B_OCTDN); ok &= ui_style == ST_FLAT;
        ui_prefs = 0;
        bad += check("MENU: on an ON / OFF row OCT+ / a knob right is ON, OCT- / left OFF (ANIM, USB SERIAL, RESTORE LAST too)", ok);
    }
    ui.menu_sel = MI_USB;
    press(B_HOME);
    ok = !ui.menu;
    hold(B_HOME);
    ok &= ui.menu == 1 && ui.menu_sel == MI_USB && menu_tab() == MTAB_AUDIO;
    frames(500);
    ok &= ui.menu == 1;                                 /* (the hold's release: no tap, the menu stays) */
    hold(B_HOME);
    ok &= !ui.menu;
    frames(1000);
    ok &= !ui.menu;                                     /* (the hold that closed it: no reopen) */
    hold(B_HOME);
    bad += check("MENU: HOME pressed closes it, held too (no reopen); held opens it again at the same tab and row",
                 ok && ui.menu == 1 && ui.menu_sel == MI_USB);
    turn(EN_ALGO, 1); turn(EN_ALGO, 1); turn(EN_PRESET, -1);   /* (MIDI,) SYSTEM: ABOUT */
    ok = ui.menu_sel == MI_ABOUT;
    frame();
    ok &= oct_leds() == OCT_BREATH;                     /* (OCT+ opens, OCT- does nothing: dark) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (no close, no back) */
    turn(EN_K1, 1); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    press(B_OCTUP); ok &= ui.menu == 2;
    ok &= oct_leds() == 1u;                             /* (ABOUT: OCT- back, lit) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_INFO;   /* (1.2) INFO: as ABOUT, no scroll */
    frame();
    ok &= oct_leds() == OCT_BREATH;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    press(B_OCTUP); ok &= ui.menu == 3 && oct_leds() == 1u;
    turn(EN_PRESET, 3); turn(EN_K1, 1); turn(EN_ALGO, 1);
    ok &= ui.menu == 3 && !ui.menu_scroll && ui.menu_sel == MI_INFO;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_INFO;
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_PANEL;
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_PANEL;
    {   /* CALIBRATION: OCT+ starts the setup (nothing pressed here: it times out and keeps the table) */
        panel_t p0 = panel;
        host_wdt_hook = wdt_ms;
        press(B_OCTUP);
        host_wdt_hook = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1;
    }
    {   /* CALIBRATION: a piano key cancels at once (no 30 s wait), the table kept */
        panel_t p0 = panel;
        uint32_t t = fm1_ms;
        host_wdt_hook = wdt_key;
        press(B_OCTUP);
        host_wdt_hook = 0;
        fm1_in.notes = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1 && fm1_ms - t < 1000u;
    }
    bad += check("MENU SYSTEM: OCT+ opens ABOUT, INFO (OCT- back to the tab) and CALIBRATION; OCT- does nothing on them", ok);
    turn(EN_PRESET, 1); turn(EN_PRESET, 1); press(B_OCTUP);   /* (INFO,) ABOUT, HOME: closed, from the document */
    ok = ui.menu == 2;
    press(B_HOME);
    ok &= !ui.menu && ui.home;
    hold(B_HOME); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (opened at the tabs, ABOUT's row) */
    turn(EN_ALGO, -1); frame();
    {   /* a value row: OCT- and OCT+ both breathe, each while that way has a value to go to; none lit */
        uint32_t r = ui.menu_sel, want = (menu_step(r, -1) != menu_get(r) ? OCT_BREATH_DN : 0u) |
                                         (menu_step(r, 1) != menu_get(r) ? OCT_BREATH : 0u);
        ok &= r < MI_VALUES && oct_leds() == want && want != 0u;
        menu_put(MI_HOLD, 0u); ui.menu_sel = MI_HOLD;   /* HOLD at its first value: OCT- dark, OCT+ breathing */
        ok &= oct_leds() == OCT_BREATH;
        menu_put(MI_HOLD, 1u);                          /* in between: both breathe */
        ok &= oct_leds() == (OCT_BREATH | OCT_BREATH_DN);
    }
    menu_close();
    bad += check("MENU: HOME closes ABOUT too; reopened at the tabs; a value row breathes OCT- / OCT+ where they can go", ok);

    /* the glide: ANIM ON, a few frames to the next row drawing only the rows' panel; ANIM OFF, at once */
    ui_power_on();
    hold(B_HOME);
    frame();
    turn(EN_K1, 1);
    frame();
    ok = mr.dy > 0 && mr.dy < mr_h() + 2;               /* (a step on the way) */
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == graph_h();                  /* (only the rows' panel) */
    for (i = 0; i < 16u && mr.dy; i++) frame();
    ok &= mr.dy == 0 && i <= 14u;
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == 0u;                         /* (settled: nothing drawn) */
    ui_prefs |= PREF_ANIM_OFF;
    turn(EN_K1, 1);
    frame();
    ok &= mr.dy == 0;
    ui_prefs &= ~PREF_ANIM_OFF;
    menu_close();
    bad += check("MENU rows glide with ANIM ON (their panel only, settled within 14 frames), snap with ANIM OFF", ok);
    return bad;
}

/* 1.2: MENU > AUDIO > TUNE and MENU > MIDI > MIDI IN edit the project's G_TUNE and G_ROUTE (up to 1.1.5 GLO > GLOBAL K4
 * and GLO > SYSTEM K3): the same values, stored with the project as before (a PROJECT save / load, the autosave), never
 * in the settings record */
static int test_menu_project_rows(void)
{
    int bad = 0, ok;
    persist_t p0, p1;
    uint32_t sig;
    ui_power_on();
    memset(&p0, 0, sizeof p0); memset(&p1, 0, sizeof p1);   /* (export keeps some fields as they were) */
    settings_export(&p0);
    sig = autosave_sig();
    hold(B_HOME);
    ui.menu_sel = MI_TUNE;
    ok = ui.menu == 1 && menu_tab() == MTAB_AUDIO && song.g[G_TUNE] == 0 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "0");
    press(B_OCTUP); ok &= song.g[G_TUNE] == 1 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+1");
    turn(EN_K2, 11); ok &= song.g[G_TUNE] == 12;         /* (a fast turn: as far as the knob went) */
    turn(EN_K1, -40); ok &= song.g[G_TUNE] == -28 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-28");
    turn(EN_K3, -100); ok &= song.g[G_TUNE] == -50;      /* (the ends: -50 .. +50 cents) */
    press(B_OCTDN); ok &= song.g[G_TUNE] == -50;
    turn(EN_K4, 200); ok &= song.g[G_TUNE] == 50 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+50");
    turn(EN_K4, -31); ok &= song.g[G_TUNE] == 19;
    bad += check("MENU > AUDIO > TUNE: the project's G_TUNE, -50 .. +50 cents (OCT one, a knob as far as it turned)", ok);
    ui.menu_sel = MI_MIDIIN;
    ok = menu_tab() == MTAB_MIDI && song.g[G_ROUTE] == 0 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH1-8");
    press(B_OCTUP); ok &= song.g[G_ROUTE] == 1;
    turn(EN_K1, 1); turn(EN_K1, 1); ok &= song.g[G_ROUTE] == 3 && str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH9-12");
    bad += check("MENU > MIDI > MIDI IN: the project's G_ROUTE (ROUT), its names", ok);
    menu_close();
    settings_export(&p1);
    ok = !memcmp(&p0, &p1, sizeof p0) && autosave_sig() != sig;
    bad += check("TUNE / MIDI IN: nothing in the settings record; the autosave sees the change (the music's)", ok);
    project_save(2);                                     /* (PROJECT C) */
    song.g[G_TUNE] = 0; song.g[G_ROUTE] = 0;
    project_load(2);
    ok = song.g[G_TUNE] == 19 && song.g[G_ROUTE] == 3 && menu_get(MI_TUNE) == 69u && menu_get(MI_MIDIIN) == 3u;
    song.g[G_TUNE] = -7; song.g[G_ROUTE] = 4;            /* (another project's values: the rows follow the load) */
    project_save(3);
    project_load(2);
    ok &= song.g[G_TUNE] == 19 && song.g[G_ROUTE] == 3;
    project_load(3);
    ok &= song.g[G_TUNE] == -7 && song.g[G_ROUTE] == 4 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "-7") &&
          str_eq(menu_vname(MI_MIDIIN, menu_get(MI_MIDIIN)), "CH13-16");
    bad += check("TUNE / MIDI IN: saved with the project and loaded back (round trip; each project its own)", ok);
    ui_power_on();
    return bad;
}

static int test_information(void)
{
    uint16_t header[240 * H_HEAD], footer[240 * 19], battery[240 * H_HEAD];
    track_t sounds[NTRK];
    int bad = 0, quiet = 1;
    uint32_t x, y;
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_ABOUT;
    press(B_OCTUP);
    ui_draw();
    bad += check("ABOUT opens one document with scrollable license text and credits", ui.menu == 2 && !ui.menu_scroll && menu_scroll_max() > MENU_DOC_H);
    for (y = 146; y < 220; y++)
        for (x = 158; x < 232; x++)
            if (x < 166 || x >= 224 || y < 154 || y >= 212)
                quiet &= host_screen[y * 240 + x] == swap16(UI_QR_LIGHT);
    bad += check("ABOUT QR keeps its complete white quiet zone above the fixed footer", quiet);
    memcpy(header, host_screen, sizeof header);
    memcpy(footer, host_screen + 221 * 240, sizeof footer);
    turn(EN_PRESET, 3);
    ui_draw();
    bad += check("PRESETS scrolls ABOUT with its header and controls fixed",
                 ui.menu == 2 && ui.menu_scroll > 0 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    turn(EN_PRESET, 12);
    ui_draw();
    bad += check("PRESETS continues from ABOUT into CREDITS without changing pages",
                 ui.menu == 2 && ui.menu_scroll > 216 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    uint16_t position = ui.menu_scroll;
    press(B_OCTUP);
    bad += check("OCT+ does not switch pages or alter the octave in the document",
                 ui.menu == 2 && ui.menu_scroll == position && song.octave == 0);
    turn(EN_PRESET, 10000);
    turn(EN_PRESET, 10000);
    ui_draw();
    bad += check("the continuous document stops at its final credit",
                 ui.menu == 2 && ui.menu_scroll == menu_scroll_max());
    turn(EN_PRESET, -10000);
    bad += check("scrolling back through CREDITS returns to ABOUT's top", ui.menu == 2 && !ui.menu_scroll);
    press(B_OCTDN);
    bad += check("OCT- returns to the menu without changing sounds or patterns",
                 ui.menu == 1 && !memcmp(sounds, trk, sizeof sounds));
    press(B_OCTUP);
    bad += check("reopening ABOUT starts the continuous document at the top", ui.menu == 2 && !ui.menu_scroll);
    menu_close();
    ui.msg_t = 0;
    usb.config = 1; usb.suspended = 0;
    song.batt_raw = 530;
    ui.force = 1; draw_head(); ui.force = 0;
    memcpy(battery, host_screen, sizeof battery);
    uint32_t signature = ui.head_sig;
    for (x = 0; x < 4; x++) { fm1_ms += 600; draw_head(); }
    bad += check("USB power shows a steady bolt inside the battery without animation",
                 batt_shown() == 4 && ui.head_sig == signature && !memcmp(battery, host_screen, sizeof battery) &&
                 host_screen[6 * 240 + 217] != 0 && host_screen[9 * 240 + 225] != 0);
    usb.suspended = 1;
    draw_head();
    bad += check("USB suspension restores the battery outline and measured level",
                 batt_shown() == 0 && ui.head_sig != signature && host_screen[6 * 240 + 217] != 0);
    usb.config = 0; usb.suspended = 0;
    song.batt_raw = 531; int levels = batt_shown() == 1;
    song.batt_raw = 561; levels &= batt_shown() == 2;
    song.batt_raw = 591; levels &= batt_shown() == 3;
    bad += check("battery operation retains all three ADC level thresholds", levels);
    return bad;
}

/* GREY is grayscale on every screen: every page of every engine, HOME, the menu, ABOUT, the dialogs (RGB565 gray:
 * R = B, G = 2 R). MONO (black and white, blended per channel) is neutral on every screen: R = B, G within 2 of 2 R
 * (the 5-6-5 rounding of its edges) */
static int screen_gray(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        if ((c >> 11) != (c & 31u) || ((c >> 5) & 63u) != (c >> 11) * 2u)
            return 0;
    }
    return 1;
}
static int screen_neutral(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        int32_t d = (int32_t)((c >> 5) & 63u) - (int32_t)(c >> 11) * 2;
        if ((c >> 11) != (c & 31u) || d < -2 || d > 2)
            return 0;
    }
    return 1;
}
static int mono_screens(uint32_t pal, int (*chk)(void), uint32_t *n)
{
    int ok = 1;
    uint32_t e, i;
    for (e = 0; e < NENGINES; e++) {
        if (!eng_ok(e))
            continue;                               /* (DIGITAL without FELUCCA_FM4: never a track's) */
        for (i = 0; i < NPAGES; i++) {
            ui_power_on(); set_engine_of(TSEL, e); song.playing = 1; song.rec = 1;
            ui.home = 0; ui.page = (uint8_t)i; page_entered();
            if (!page_visible(i)) continue;
            settings.palette = pal; palette_set(pal);
            memset(host_screen, 0, sizeof host_screen);
            ui.force = 1; ui_draw();
            ok &= chk();
            (*n)++;
        }
        ui_power_on(); set_engine_of(TSEL, e); go_home();
        settings.palette = pal; palette_set(pal);
        ui.force = 1; ui_draw(); ok &= chk();
    }
    for (i = 0; i < 4u; i++) {
        ui_power_on(); song.rec = 3;
        settings.palette = pal; palette_set(pal);
        if (i < 2u) { ui.menu = (uint8_t)(i + 1u); ui.menu_scroll = 300; }
        else if (i == 2u) ui.confirm = CF_OVR_PROJ;
        else ui.uboot = 2;
        ui.force = 1; ui_draw(); ok &= chk();
    }
    return ok;
}
static int test_mono_screens(void)
{
    int bad = 0, ok;
    uint32_t e, n = 0, nb = 0;
    ok = mono_screens(UI_GREY_INDEX, screen_gray, &n);
    bad += check("GREY: every pixel of every page, HOME, menu, ABOUT, dialog and countdown is gray", ok && n > 200u);
    ok = mono_screens(UI_BW_INDEX, screen_neutral, &nb);
    bad += check("MONO (black and white): every pixel of every page, HOME, menu, ABOUT, dialog and countdown is neutral",
                 ok && nb == n);
    ui_power_on();
    for (e = 0; e < NPALETTES; e++) {               /* the other palettes are not gray: the check sees colour */
        ui_power_on(); palette_set(e); ui.force = 1; ui_draw();
        ok = screen_gray();
        if (e == UI_GREY_INDEX ? !ok : ok) bad += check("palette colour shows on HOME", 0);
    }
    palette_set(UI_GREY_INDEX);
    return bad;
}

/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The old GLOBAL page's four (1.2: a
 * test page over HOME > CLOCK, whose KNOB 4 is empty since TUNE went to the MENU): BPM (also in the header), SWG, CLK (a
 * name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static const page_t ROLL_PG = {"CLOCK", FAM_TRK, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}};
static void roll_settle(void)                     /* CLOCK + TUNE, nothing rolling, no knob hot */
{
    uint32_t k;
    go_title("ENV");                              /* (Dah: CLOCK's page gone: the test's own over ENV) */
    page_over = &ROLL_PG;                         /* (cur_page(): its knobs, its cards; a layer's way, ui_layer.c) */
    ui.hot_t = 0; ui.bpm_t = 0;
    for (k = 0; k < 12u; k++) frame();
}
/* host_screen against roll_shot: the pixels that differ inside the box (in = 1) or outside it */
static uint32_t roll_diff(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int in)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inside == in && host_screen[y * 240 + x] != roll_shot[y * 240 + x]) n++;
        }
    return n;
}
/* the mean row (x16) of the ink (not the card surface) in columns x0..x1-1, rows y0..y1-1 of a screen */
static int32_t roll_ink_row(const uint16_t *s, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    int32_t x, y, n = 0, sum = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (s[y * 240 + x] != swap16(T_SURF)) { n++; sum += y * 16; }
    return n ? sum / n : -1;
}
static int roll_static_now(void)                  /* the screen is the static render (a forced redraw changes nothing) */
{
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1;
    ui_draw();
    return roll_diff(0, 0, 0, 0, 0) == 0;
}
#define STRIP(c) CARD_X(c), Y_LABEL + ROLL_Y, CARD_X(c) + COL_W, Y_LABEL + ROLL_Y + ROLL_H
static int test_roll(void)
{
    int bad = 0, ok;
    uint32_t k, n, e, id;
    int32_t up, down, st;
    static const struct { const char *a, *b; int d; } DIRS[] = {
        {"120", "121", 1}, {"121", "120", -1}, {"129", "130", 1}, {"-12", "-13", -1}, {"-13", "-12", 1}, {"+5", "+6", 1},
        {"1.25", "1.30", 1}, {"0.9", "0.8", -1}, {"9", "10", 0}, {"0", "-1", 0}, {"+5", "-5", 0}, {"C3", "C4", 0},
        {"1/4", "1/8", 0}, {"OFF", "ON", 0}, {"2X", "4X", 0}, {"120", "120", 0}, {"123456", "123457", 1},
        {"1234567", "1234568", 0}, {"12%", "13%", 0}};

    ok = 1;
    for (k = 0; k < sizeof DIRS / sizeof DIRS[0]; k++)
        if (roll_dir(DIRS[k].a, DIRS[k].b) != DIRS[k].d) { printf("  roll_dir %s -> %s\n", DIRS[k].a, DIRS[k].b); ok = 0; }
    bad += check("roll: numbers of one shape roll by the sign of the change; names, length, sign changes snap", ok);
    ok = 1;                                         /* every page of every engine: names never roll, numbers do */
    n = 0;
    for (e = 0; e < NENGINES; e++)
        for (id = 0; id < NPAGES && eng_ok(e); id++) {
            uint32_t c;
            ui_power_on(); set_engine_of(TSEL, e);
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id)) continue;
            ui.force = 1; ui_draw();
            for (c = 0; c < 4u; c++) {
                int16_t *vp;
                const param_desc_t *d = PAGES[id].scope == SC_GLOBAL || PAGES[id].scope == SC_TRACK ||
                                        PAGES[id].scope == SC_ENGINE ? page_desc(cur_page(), c, &vp) : 0;
                int32_t v, v0;
                if (!d || !d->label || d->label[0] == '-' || ((act_cols() >> c) & 1u)) continue;
                v0 = *vp;
                for (v = d->min; v < d->max && v - d->min < 40; v++) {
                    *vp = (int16_t)v; ui.frame += ROLL_SNAP; draw_columns();
                    *vp = (int16_t)(v + 1); ui.frame += ROLL_SNAP; draw_columns();
                    if (ui.roll[c].from[0] && (d->fmt == F_ENUM || d->fmt == F_NOTE || d->fmt == F_ONOFF)) {
                        printf("  %s/%s %s: %s rolls\n", ENGINES[e]->name, PAGES[id].title, d->label, ui.roll[c].from);
                        ok = 0;
                    }
                    n += ui.roll[c].from[0] != 0;
                }
                *vp = (int16_t)v0;
            }
        }
    ok &= n > 500u;
    bad += check("roll: every page and engine: a name (enum, even ALG 1..8, note, ON/OFF) snaps, numbers roll", ok);

    /* TUNE 0 -> 1 (card 4): a roll of ROLL_FRAMES frames, the last one the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && str_eq(ui.roll[3].from, "0") && ui.roll[3].dir == 1;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES;
    bad += check("roll: a card's value rolls for 9 frames (TUNE 0 -> 1), the value itself at once", ok);
    bad += check("roll: the roll's last frame is pixel-identical to the static render", roll_static_now());

    /* mid-roll: only the value strip differs from the static render */
    ui_power_on(); roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();                       /* (snaps: the static render) */
    ok = roll_diff(STRIP(3), 0) == 0 && roll_diff(STRIP(3), 1) > 20u && !ui.roll[3].from[0];
    bad += check("roll: a mid-roll frame differs from the static render inside the value strip only", ok);

    /* BPM 120 -> 121 (SELECT): the header rolls, the digits 1 and 2 stay; the BPM card too (no unit:
     * the label says BPM: set in M, it rolls like any number) */
    ui_power_on(); roll_settle();
    tempo_turn(1);
    ok = song.g[G_BPM] == 121 && str_eq(ui.roll[ROLL_BPM].from, "120") && ui.roll[ROLL_BPM].dir == 1 &&
         str_eq(ui.roll[0].from, "120") && ui.roll[0].dir == 1;
    frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    {   /* the header's strip: from the metronome's ink to the lock's place (ui_draw.c draw_head) */
        head_geo_t g;
        int32_t x0, x1;
        head_geo(&g, "121");
        x0 = g.sx0; x1 = g.sx1;
        n = roll_diff(x0, 0, x1, H_HEAD, 1);
        ok &= n > 20u && roll_diff(x0, 0, x1, H_HEAD, 0) == roll_diff(STRIP(0), 1);   /* (+ the card's strip) */
        ok &= roll_diff(g.bx, 0, g.bx + text_w(&AF_M, "12"), H_HEAD, 1) == 0;
        ok &= x0 < g.bx && x1 <= 180 && x1 - x0 <= 40;   /* (the group right of the rail, from x 104) */
    }
    ok &= roll_diff(CARD_X(0), Y_LABEL + ROLL_Y, CARD_X(0) + 5 + text_w(&AF_M, "12"), Y_LABEL + ROLL_Y + ROLL_H, 1) == 0;
    /* TUNE 10 -> 11 on a card: the 1 stays */
    ui_power_on(); song.g[G_TUNE] = 10; roll_settle();
    turn(EN_K4, 1); frame(); frame();
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1; ui_draw();
    ok &= song.g[G_TUNE] == 11 && roll_diff(STRIP(3), 1) > 20u && roll_diff(STRIP(3), 0) == 0;
    ok &= roll_diff(CARD_X(3), Y_LABEL + ROLL_Y, CARD_X(3) + 5 + text_w(&AF_M, "1"), Y_LABEL + ROLL_Y + ROLL_H, 1) == 0;
    bad += check("roll: header BPM 120 -> 121 and a card 10 -> 11: only the strip changes, unchanged digits stay put", ok);

    /* the direction: 5 -> 6 and -5 -> -6 at frame 4: the new digit comes from below (up) / above (down) */
    ui_power_on(); song.g[G_TUNE] = 5; roll_settle();
    st = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    turn(EN_K4, 1); frame(); frame(); frame();
    up = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    ok = ui.roll[3].dir == 1;
    ui_power_on(); song.g[G_TUNE] = 7; roll_settle();
    turn(EN_K4, -1); frame(); frame(); frame();
    down = roll_ink_row(host_screen, CARD_X(3) + 5, CARD_X(3) + 16, Y_LABEL + ROLL_Y, Y_LABEL + ROLL_Y + ROLL_H);
    ok &= ui.roll[3].dir == -1 && st > 0 && up > st + 16 && down < st - 16;
    ui_power_on(); song.g[G_TUNE] = -5; roll_settle();
    turn(EN_K4, -1);
    ok &= song.g[G_TUNE] == -6 && str_eq(ui.roll[3].from, "-5") && ui.roll[3].dir == -1;
    bad += check("roll: an increase rolls up, a decrease down (-5 -> -6 is down)", ok);

    /* retarget: a change mid-roll restarts from the value it was going to; a fast turn snaps */
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); frame(); frame(); frame();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && str_eq(ui.roll[3].from, "2") && (uint8_t)(ui.frame - ui.roll[3].t0) == 0;
    for (n = 1; ui.roll[3].from[0] && n < 20u; n++) frame();
    ok &= n == ROLL_FRAMES && roll_static_now();
    bad += check("roll: a change mid-roll retargets (from the last value, restarted) and ends static", ok);
    ui_power_on(); song.g[G_TUNE] = 1; roll_settle();
    turn(EN_K4, 1); turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 3 && !ui.roll[3].from[0] && roll_static_now();
    bad += check("roll: a change within 40 ms of the last one snaps (a fast turn)", ok);

    /* snaps: another shape, a name, the page, the track, the palette, ui.force */
    ui_power_on(); roll_settle();
    turn(EN_K4, -1);
    ok = song.g[G_TUNE] == -1 && !ui.roll[3].from[0] && roll_static_now();
    for (k = 0; k < 5u; k++) frame();
    turn(EN_K3, 1);
    ok &= song.g[G_CLOCK] != 0 && !ui.roll[2].from[0] && roll_static_now();
    bad += check("roll: a sign / length change (0 -> -1) and a name (CLK) snap", ok);
    ok = 1;
    for (k = 0; k < 5u; k++) {
        ui_power_on(); roll_settle();
        turn(EN_K4, 1);
        ok &= ui.roll[3].from[0] != 0;
        if (k == 0) { page_over = 0; go_title("ENV"); }
        else if (k == 1) turn(EN_ALGO, 1);
        else if (k == 2) { settings.palette = 1; palette_set(1); }          /* (no ui.force) */
        else if (k == 3) set_engine_of(TSEL, eng_step(TSEL->eng_req, 1));   /* (no ui.force) */
        else ui.force = 1;
        frame();
        ok &= !ui.roll[3].from[0] && !ui.roll[ROLL_BPM].from[0];
    }
    ui_power_on(); roll_settle();
    tempo_turn(1);
    ok &= ui.roll[ROLL_BPM].from[0] != 0;
    ui.force = 1; frame();
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("roll: a page, track, palette or engine change and ui.force snap", ok);
    palette_set(UI_GREY_INDEX);

    /* MONO: every roll frame is gray */
    ui_power_on(); roll_settle();
    ok = 1;
    tempo_turn(9);
    turn(EN_K4, -1);
    for (k = 0; k < ROLL_FRAMES; k++) { ok &= screen_gray(); frame(); }
    bad += check("roll: MONO roll frames are gray", ok);
    page_over = 0;
    ui_power_on();
    return bad;
}

/* #63 ("parameters moving on their own, fixed by a page change"): a soak of the cards and the rolling digits.
 * ~40 min of UI frames: random turns of KNOB 1..4 and SELECT (bursts back and forth), page / HOME / track
 * changes, the transport and REC (motion recorded and played back), LFO rates, uneven frame times, ui.frame
 * and fm1_ms wrapping. After every frame no roll is older than ROLL_FRAMES; at random quiet moments (no
 * roll, nothing hot) the cards are pixel-identical to a forced redraw (what a page change would show). */
static int test_soak_cards(void)
{
    uint32_t seed = 63u, f, quiet = 0, old = 0, stale = 0, cmp = 0, c;
    static uint16_t shot[240 * CARD_H];
#define SOAK_RND(n) ((seed = seed * 1103515245u + 12345u) >> 8) % (n)
    ui_power_on();
    ui.frame = 0xFFFFFF00u;
    fm1_ms = 0xFFFFFFFFu - 60000u;
    for (f = 0; f < 150000u; f++) {
        uint32_t r = SOAK_RND(1000u);
        if (r < 25u) {                                  /* a knob, 1..3 detents either way */
            uint32_t role = SOAK_RND(5u) ? EN_K1 + SOAK_RND(4u) : EN_SELECT;
            int32_t s = (int32_t)SOAK_RND(3u) + 1;
            host_enc[panel.enc[role]] += (SOAK_RND(2u) ? s : -s) * panel.dir[role];
            quiet = 0;
        } else if (r < 29u) {                           /* a page */
            uint32_t i = SOAK_RND(NPAGES);
            if (page_visible(i)) { ui.home = 0; ui.page = (uint8_t)i; page_entered(); }
            quiet = 0;
        } else if (r < 30u) { ui.home = 1; page_entered(); quiet = 0; }
        else if (r < 31u) transport_req = song.playing ? 2u : 1u;
        else if (r == 31u && SOAK_RND(3u) == 0u) rec_tap();
        else if (r == 32u) { host_enc[panel.enc[EN_ALGO]] += SOAK_RND(2u) ? 1 : -1; quiet = 0; }
        else if (r == 33u) trk[SOAK_RND(NTRK)].p[P_LRATE] = (int16_t)SOAK_RND(128u);
        fm1_ms += SOAK_RND(50u) ? 16u : 1u + SOAK_RND(200u);
        host_ticks += 16000u + SOAK_RND(3u) * 4000u;
        ui_input();
        if (song.playing || transport_req) events_block(FS * 16u / 1000u);
        ui_draw();
        quiet++;
        if (ui.menu || ui.layer || ui.confirm || name_on() || ui.uboot)
            continue;                                   /* (the cards are not drawn) */
        for (c = 0; c < 5u; c++)
            old += ui.roll[c].from[0] && (uint8_t)(ui.frame - ui.roll[c].t0) >= ROLL_FRAMES;
        if (quiet > 45u && !ui.hot_t && !ui.bpm_t && !ui.msg_t && SOAK_RND(8u) == 0u) {
            for (c = 0; c < 5u && !ui.roll[c].from[0]; c++) {}
            if (c < 5u)
                continue;
            memcpy(shot, &host_screen[Y_LABEL * 240], sizeof shot);
            ui.force = 1;
            ui_draw();
            cmp++;
            stale += memcmp(shot, &host_screen[Y_LABEL * 240], sizeof shot) != 0;
        }
    }
#undef SOAK_RND
    if (old || stale || cmp < 500u) printf("  soak: %u old rolls, %u stale cards in %u checks\n", old, stale, cmp);
    stop_transport();
    memset(&motion, 0, sizeof motion);                  /* (what it recorded: the tests below start without) */
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = motion_full = 0;
    ui_power_on();
    return check("#63 soak: 150000 random frames, wrapping clocks, motion: no roll lasts, cards = a forced redraw",
                 !old && !stale && cmp >= 500u);
}

static void screen_save(const char *dir, const char *name)
{
    char path[512];
    uint32_t i;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    ui.msg_t = 0; ui.hot_t = 0; ui.force = 1;
    ui_draw();
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);           /* canvas stores panel-order RGB565 */
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
static void chain_screens(const char *dir)
{
    uint32_t k;
    ui_power_on();
    go_page(GR_SONG);
    screen_save(dir, "song-empty");
    for (k = 0; k < 2u; k++) { my_steps(&trk[0]); project_save(k); }
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(0, 2);
    chain_config.row[1] = chain_row_of(1, 4);
    chain_config.row[2] = chain_row_of(0, 1);
    ui.song_row = 1;
    screen_save(dir, "song-ready");
    chain_prepare(); events_block(32);
    screen_save(dir, "song-playing");
    seq_stop();
}

static int test_chain(void)
{
    uint32_t i, k, period, last, n = 32u;
    int bad = 0, ok;
    step_t before[NTRK][NSTEP];
    int16_t timing[NTRK][4], sounds[NTRK][P_COUNT];
    ui_power_on();
    pat_park();                                       /* (patterns 1 2 kept below, A B of before) */
    for (i = NTRK_V10; i < NTRK; i++)                 /* (no flash here: tracks 5..8, kept in the FUNX, out of it) */
        trk[i].p[P_MUTE] = 1;
    bad += check("SONG empty: PLAY does not start", chain_prepare() == 1 && !transport_req);
    for (k = 0; k < 2u; k++) {
        for (i = 0; i < NTRK_V10; i++) {
            track_defaults_steps(&trk[i]);
            trk[i].p[P_SLEN] = (int16_t)(2u + k);
            trk[i].step[0] = (step_t){{(uint8_t)(60u + 5u * k), 0, 0, 0}, 1, ST_NOTE, 0, 96, 0, 0};
        }
        pat_keep(k + 1u);
    }
    for (i = 0; i < NTRK_V10; i++) {
        my_steps(&trk[i]);
        trk[i].p[P_SLEN] = 9;
        memcpy(before[i], trk[i].step, sizeof before[i]);
        memcpy(timing[i], &trk[i].p[P_SLEN], sizeof timing[i]);
        memcpy(sounds[i], trk[i].p, sizeof sounds[i]);
    }
    chain_config.count = 2;
    chain_config.row[0] = chain_row_of(1, 2);
    chain_config.row[1] = chain_row_of(9, 1);        /* (no pattern 9: its lanes silent, nothing refused) */
    ok = chain_prepare() == 0 && chain.source[1].step[0][0].time != ST_NOTE && chain.source[0].step[0][0].note[0] == 60 &&
         !memcmp(trk[0].step, before[0], sizeof before[0]);
    seq_stop(); chain.armed = 0; transport_req = 0;
    bad += check("SONG a pattern not there: its lanes silent, the tracks untouched", ok);
    chain_config.row[1] = chain_row_of(2, 1);
    project_save(2);
    chain_defaults(&chain_config);
    project_load(2);
    bad += check("SONG rows saved and loaded with their project", chain_config.count == 2 &&
        chain_config.row[0].repeat == 2 && !memcmp(chain_config.row[1].slot, "\2\2\2\2", NTRK_V10));   /* (no flash in this build: tracks 5..8 travel in the FUNX) */
    song.rec = 3;
    bad += check("SONG prepares while stopped, no starts over a pending start", chain_prepare() == 0 && chain_prepare() == 2);
    events_block(n);
    bad += check("SONG starts all tracks at source step 0, recording paused", chain.running && song.playing &&
        !song.rec && trk[0].seq_idx == 0 && seq_steps(&trk[0])[0].note[0] == 60 && trk[0].seq_notes[0] == 60);
    period = div_samples((uint32_t)trk[0].p[P_SDIV]);
    events_block(period);
    events_block(period);
    bad += check("SONG first row repeats without a gap", chain.row == 0 && chain.remaining == 1 && trk[0].seq_idx == 0);
    events_block(period);
    events_block(period);
    ok = chain.row == 1 && chain.remaining == 1;
    for (i = 0; i < NTRK_V10; i++) {
        ok &= trk[i].seq_idx == 0 && trk[i].p[P_SLEN] == 3 && seq_steps(&trk[i])[0].note[0] == 65;
        for (k = 0; k < P_COUNT; k++)
            if (k < P_SLEN || k > P_SGATE) ok &= trk[i].p[k] == sounds[i][k];
    }
    bad += check("SONG row change: four tracks together, sounds unchanged", ok);
    open_family(FAM_SEQ);
    for (k = 0; k < NPAGES && cur_page()->scope != SC_STEP; k++) open_family(FAM_SEQ);
    turn(EN_K2, 1); press(B_EDIT); hold(B_REC);
    bad += check("SONG playing: step edits, clears and recording are blocked", !memcmp(trk[0].step, before[0], sizeof before[0]) && !ui.confirm);
    events_block(period);
    events_block(period);
    events_block(period);
    ok = song.playing && chain.running && chain.row == 0 && trk[0].seq_idx == 0 && seq_steps(&trk[0])[0].note[0] == 60;
    seq_stop();                                         /* (Dah: the song goes round; STOP restores) */
    ok &= !song.playing && !chain.running && song.rec == 3;
    for (i = 0; i < NTRK_V10; i++) ok &= !trk[i].seq_n && !memcmp(trk[i].step, before[i], sizeof before[i]) &&
        !memcmp(&trk[i].p[P_SLEN], timing[i], sizeof timing[i]);
    bad += check("SONG end: round to the first section (Dah); STOP restores editable patterns, timing and record arms", ok);
    {
        static step_t st[NSTEP];
        bad += check("SONG source patterns stay unchanged", pat_steps(0, 1, st) && st[0].note[0] == 60 &&
                     pat_steps(0, 2, st) && st[0].note[0] == 65);
    }
    chain_config.row[0].repeat = 1;
    chain_prepare(); events_block(n);
    trk[0].seq_idx = 1;
    last = step_samples(&trk[0], div_samples((uint32_t)trk[0].p[P_SDIV]), 1);
    trk[0].seq_pos = last - n + 19u;
    events_block(n);
    ok = chain.row == 1;
    for (i = 0; i < NTRK_V10; i++) ok &= trk[i].seq_pos == 19u && trk[i].seq_idx == 0;
    bad += check("SONG transition preserves fractional block time on all tracks", ok);
    transport_req = 2; events_block(n);
    bad += check("SONG manual STOP restores the previous pattern", !chain.running && !song.playing && !memcmp(trk[0].step, before[0], sizeof before[0]));
    chain_prepare(); events_block(n);
    project_load(2);                                 /* (the slot saved above: the working patterns, LEN 9, its song) */
    bad += check("PROJECT load during SONG stops it before loading new timing", !chain.running && !song.playing &&
        trk[0].p[P_SLEN] == 9 && !memcmp(trk[0].step, before[0], sizeof before[0]) && chain_config.count == 2);
    ui_power_on();
    for (i = 0; i < NTRK; i++) {                     /* (each track's patterns 1..4, it plays 1) */
        pattern_new(i, 0); pattern_new(i, 0); pattern_new(i, 0);
        pattern_go(i, 1);
    }
    open_family(FAM_GLO);
    for (k = 0; k < NPAGES && cur_page()->graph != GR_SONG; k++) open_family(FAM_GLO);
    press(B_PLAY);
    bad += check("SONG page (GLO's, 1.2) empty PLAY explains how to start", msg_is("ADD A SECTION") && !transport_req &&
                 cur_page()->fam == FAM_GLO);
    turn(EN_K3, 1);
    ok = chain_config.count == 1 && chain_config.row[0].repeat == 1u;
    for (i = 0; i < NTRK; i++)
        ok &= chain_config.row[0].slot[i] == (i < trk_count() ? 1u : CHAIN_SILENT);
    bad += check("SONG PAT knob (K3) on + ADD adds the first section: every track on the pattern it plays, one repeat", ok);
    turn(EN_K1, 1); turn(EN_K3, 1); turn(EN_K3, 1); turn(EN_K4, 2);
    ok = chain_config.count == 2 && ui.song_row == 1 && !ui.song_trk && chain_config.row[1].repeat == 3u &&
         chain_config.row[1].slot[0] == 2u;
    for (i = 1; i < NTRK; i++)
        ok &= chain_config.row[1].slot[i] == chain_config.row[0].slot[i];
    bad += check("SONG section, pat and repeat knobs build a chain (the new section a copy, T1 on 2)", ok);
    turn(EN_K2, 2); turn(EN_K3, 9);
    ok = ui.song_trk == 2 && chain_config.row[1].slot[2] == 4u;
    turn(EN_K3, -9);
    bad += check("SONG K2 picks the lane (T3), K3 runs - (silent), then the track's patterns (1..4)", ok && ui.song_trk == 2 &&
                 chain_config.row[1].slot[2] == CHAIN_SILENT && chain_config.row[1].slot[0] == 2u);
    turn(EN_K2, 9);                                  /* past T4: SEC, the whole section; K3 its CUT */
    ok = ui.song_trk == NTRK;
    turn(EN_K3, 1);
    ok &= (chain_config.row[1].repeat & CHAIN_CUT) && CHAIN_REP(chain_config.row[1]) == 3u && chain_valid(&chain_config);
    turn(EN_K3, -1);
    ok &= !(chain_config.row[1].repeat & CHAIN_CUT) && chain_config.row[1].repeat == 3u;
    bad += check("SONG K2 past T4: SEC (ALL), K3 sets and clears the section's CUT, its repeats kept", ok);
    turn(EN_K2, -1); turn(EN_K3, -2);
    bad += check("SONG K2 stays in the project's tracks, K3 back to -", ui.song_trk == trk_count() - 1u &&
                 chain_config.row[1].slot[trk_count() - 1u] == CHAIN_SILENT && chain_config.row[1].slot[2] == CHAIN_SILENT);
    turn(EN_K3, 2);
    bad += check("  .. on the last: 2", chain_config.row[1].slot[trk_count() - 1u] == 2);
    ui.song_trk = 1;
    k = key_at(0, 1);                                /* (Dah) the second white key: pattern 2; held over a block */
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); events_block(32);
    ok = kb_note[k] == KB_SILENT && song.grid == 3u;
    fm1_in.notes &= ~(1u << k); frame(); events_block(32);
    bad += check("SONG white key 2 sets the cell's pattern (2), silently; its LED lit", ok && chain_config.row[1].slot[1] == 2 &&
                 (key_leds(0) >> k & 1u));
    k = key_at(0, 15);                               /* (Dah) the last white key, pattern 16: the track has none */
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); events_block(32);
    ok = kb_note[k] == KB_SILENT;
    fm1_in.notes &= ~(1u << k); frame(); events_block(32);
    bad += check("SONG: a key past the track's patterns: silent, the cell as it was", ok && chain_config.row[1].slot[1] == 2);
    turn(EN_K1, 1); tap_key(key_at(0, 2));           /* (the third white key on + ADD: section 2 copied, T2 on 3) */
    {
        chain_row_t e = chain_config.row[1];
        e.slot[1] = 3;
        bad += check("SONG white key 3 on + ADD adds a section, a copy with the cell on 3", chain_config.count == 3 &&
                     !memcmp(&chain_config.row[2], &e, sizeof e));
    }
    chain_config.count = 2; ui.song_row = 1; ui.song_trk = 0;
    turn(EN_K4, 30);
    chain_config.count = 1;
    chain_config.row[0] = chain_row_of(1, 1);
    fm1_in.buttons |= (1u << panel.btn[B_OCTUP]) | (1u << panel.btn[B_OCTDN]);
    host_pressed |= fm1_in.buttons; frame();
    fm1_in.buttons = 0; frame();
    bad += check("SONG OCT chord does not start playback or leave the page", !transport_req && !ui.home);
    {   /* (Dah) REC: a section to add, shown, OCT+ adds it; OCT- drops one pending */
        uint32_t n0;
        press(B_OCTDN);                                 /* (the chord was ASSIGN: OCT- leaves it) */
        n0 = chain_config.count;
        ui.song_row = 0;
        press(B_REC);
        ok = sg_pend == 1u && chain_config.count == n0;
        press(B_OCTUP);

        ok &= !sg_pend && chain_config.count == n0 + 1u;
        press(B_REC);
        press(B_OCTDN);
        ok &= !sg_pend && chain_config.count == n0 + 1u;
        sg_pend = 2; sg_prow = 0;                       /* (OCT- held 0.5 s sets it) */
        press(B_OCTUP);
        ok &= !sg_pend && chain_config.count == n0;
        bad += check("SONG REC: the section to add shown, OCT+ adds it, OCT- drops it; a deletion: OCT+ does it", ok);
    }
    press(B_OCTDN);                                     /* (Dah: the chord was ASSIGN: OCT- leaves it) */
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= fm1_in.buttons; frame();   /* (Dah: OCT+ held 0.5 s: the VIEW) */
    fm1_ms += 600u; frame();
    ok = sgw.on && !transport_req && !chain.armed;
    turn(EN_K2, 1);                                     /* (a knob turned with it: no octave on its release) */
    fm1_in.buttons = 0; frame();
    bad += check("SONG OCT+ held (Dah): the VIEW (GRID ZOOM WINDOW FOLLOW), nothing plays; let go: closed, the octave kept",
                 ok && !sgw.on && song.octave == 0);
    ok = !sgw.on;
    press(B_PLAY);
    bad += check("SONG PLAY starts it, pending start blocks SAVE (OCT+ again: the VIEW closed)", ok && chain.armed &&
                 transport_req == 1);
    project_save(3);
    bad += check("SONG pending start cannot write a project", msg_is("STOP TO SAVE") && !project_used(3));
    press(B_PLAY); events_block(32);
    bad += check("SONG PLAY cancels a pending start", !chain.running && !chain.armed && !song.playing);
    uint32_t old_count = chain_config.count;
    ui.song_row = 0;
    turn(EN_K4, 30);
    bad += check("SONG K4 on a section: its repeats (at most 16), never the count", chain_config.count == old_count &&
                 chain_config.row[0].repeat == 16);
    for (k = chain_config.count; k < CHAIN_ROWS; k++) { turn(EN_K1, 1); turn(EN_K3, 1); }
    bad += check("SONG append row stays bounded at 16", chain_config.count == CHAIN_ROWS && chain_valid(&chain_config));
    return bad;
}

/* SONG's timeline (ui_graph.c song_layout / song_follow / graph_song): widths by bars, the least width, + ADD's slot,
 * the view following the cursor and the playing row, the styles of a slot not saved and of + ADD */
static int sg_seen(const song_lay_t *L, uint32_t r)  /* row r's block (r == n: + ADD's) in the view (Dah: its start, or
                                                     * the whole view when wider) */
{
    int32_t a = L->x[r], b = r < L->n ? L->x[r + 1u] - SG_GAP : L->x[r] + SG_ADDW;
    return b - a <= SG_W ? a >= sgv.off && b <= sgv.off + SG_W : a >= sgv.off && a < sgv.off + SG_W;
}
static uint16_t sg_px(int32_t x, int32_t y) { return swap16(host_screen[(uint32_t)(Y_GRAPH + y) * 240u + (uint32_t)x]); }
static int test_song_timeline(void)
{
    song_lay_t L;
    uint32_t k, r;
    int bad = 0, ok;
    int32_t w0, w1, w2, x;
    ui_power_on();
    pat_park();                                      /* (patterns 1..3 below, A..C of before) */
    trk[0].p[P_SDIV] = 2;                            /* 1/16 */
    trk[0].p[P_SLEN] = 16; pat_keep(1);          /* A: 1 bar */
    trk[0].p[P_SLEN] = 32; pat_keep(2);          /* B: 2 bars */
    trk[0].p[P_SLEN] = 1; trk[0].p[P_SDIV] = 3; pat_keep(3);   /* C: one 1/32, 1/32 bar */
    fm1_ms += 1000u;                                 /* (the slots read again) */
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(1, 1); chain_config.row[1] = chain_row_of(2, 2); chain_config.row[2] = chain_row_of(1, 1);
    ui.song_row = 0;
    go_page(GR_SONG); frame();
    song_layout(&L);
    w0 = L.x[1] - L.x[0]; w1 = L.x[2] - L.x[1]; w2 = L.x[3] - L.x[2];
    bad += check("SONG timeline (Dah): 1 + 4 + 1 bars at the VIEW's zoom (a bar SG_ZOOM0 px), + ADD's slot at the end",
                 !L.fit && L.add && L.u0[3] == 6u * SG_BARU && w0 == (int32_t)sg_zoom() && w1 == 4 * (int32_t)sg_zoom() &&
                 w2 == w0 && L.x[4] == L.x[3] + SG_ADDW);
    chain_config.row[1] = chain_row_of(3, 1);       /* the 1/32 bar between two of 1 bar x 16 */
    chain_config.row[0].repeat = chain_config.row[2].repeat = 16;
    song_layout(&L);
    w0 = L.x[1] - L.x[0]; w1 = L.x[2] - L.x[1];
    bad += check("SONG timeline: a block far shorter than the rest keeps the least width (its letter) (Dah: 33 bars: not "
                 "fitted, scrolled)", w1 == SG_MINW && w0 == 16 * (int32_t)sg_zoom() && !L.fit);
    chain_config.count = CHAIN_ROWS;                 /* 16 rows of 2 bars x 16: 512 bars, scrolled */
    for (k = 0; k < CHAIN_ROWS; k++) chain_config.row[k] = chain_row_of(2, 16);
    song_layout(&L);
    bad += check("SONG timeline: a long song scrolls (the zoom's px a bar), no + ADD at 16 rows",
                 !L.fit && !L.add && L.x[1] - L.x[0] == 32 * (int32_t)sg_zoom() && L.x[L.n + 1u] == L.x[L.n]);
    ui_prefs |= PREF_ANIM_OFF;
    ok = 1;
    for (r = 0; r < CHAIN_ROWS; r += 3u) {
        ui.song_row = (uint8_t)r; frame();
        ok &= sg_seen(&L, r);
    }
    ui.song_row = 0; frame();
    ok &= sgv.off == 0;
    bad += check("SONG timeline: ANIM OFF, the cursor's block in view at once, every row", ok);
    ui_prefs &= ~PREF_ANIM_OFF;
    turn(EN_K1, 15);                                 /* (KNOB 1: the last row) */
    ok = ui.song_row == 15u && !sg_seen(&L, 15) && sgv.off > 0;   /* on its way */
    frames(800);
    bad += check("SONG timeline: ANIM ON, the view glides to the cursor's block, then holds it (its start: wider)",
                 ok && sg_seen(&L, 15) && sgv.off == L.x[15]);
    chain_config.count = 12;                         /* + ADD's slot followed too */
    song_layout(&L);
    ui.song_row = 12; frames(800);
    bad += check("SONG timeline: the cursor on + ADD (after 12 rows): its slot in view", L.add && sg_seen(&L, 12));
    /* playing: the playing row followed, the cursor after it moves */
    for (k = 0; k < 12u; k++) chain_config.row[k] = chain_row_of(1, 16);  /* 16 bars each (A saved): scrolled */
    song_layout(&L);
    ui.song_row = 0; frames(800);
    chain_prepare(); events_block(32);
    for (k = 0; k < 4000u && chain.running && chain.row < 9u; k++) events_block(div_samples(2));
    frames(800);
    bad += check("SONG timeline: playing row 10 far from the cursor (row 1): the playing block in view",
                 chain.running && chain.row == 9u && ui.song_row == 0u && sg_seen(&L, 9) && !sg_seen(&L, 0) &&
                 song_playx(&L) >= L.x[9] && song_playx(&L) < L.x[10]);
    turn(EN_K1, 1); frames(800);
    ok = sg_seen(&L, 1);                              /* (Dah: a section picked: its block a while) */
    frames(4000);                                     /* (then the playhead again) */
    bad += check("SONG timeline: playing, a section picked: its block shown a while, then the playhead held in the middle (Dah)",
                 ok && ui.song_row == 1u && SG_X0 + song_playx(&L) - sgv.off >= SG_X0 + SG_W / 2 - 1 &&
                 SG_X0 + song_playx(&L) - sgv.off <= SG_X0 + SG_W / 2 + 1);
    seq_stop(); chain_stop(); frames(100);
    /* styles: a slot not saved outlined (DIM), the selected one filled (THEME), the others RAISE; + ADD dashed */
    chain_config.count = 3;
    chain_config.row[0] = chain_row_of(1, 1); chain_config.row[1] = chain_row_of(4, 1); chain_config.row[2] = chain_row_of(1, 1);
    ui.song_row = 0; fm1_ms += 1000u; go_page(GR_SONG); frames(100);
    song_layout(&L);
    x = SG_X0 + L.x[1] + 14;                         /* (thin lanes, the letter from the lane's top: past it) */
    ok = sg_px(SG_X0 + L.x[0] + 14, SG_Y + SG_LH - 1) == T_THEME && sg_px(SG_X0 + L.x[2] + 14, SG_Y + SG_LH - 1) == T_RAISE;
    ok &= sg_px(x, SG_Y) == T_DIM && sg_px(x, SG_Y + SG_LH - 3) == T_SURF && sg_px(SG_X0 + L.x[1], SG_Y + 3) == T_DIM;
    bad += check("SONG timeline: selected THEME, others RAISE, a slot not saved only outlined (DIM)", ok);
    x = SG_X0 + L.x[3];
    bad += check("SONG timeline: + ADD's slot dashed (DIM), THEME when selected",
                 sg_px(x, SG_Y) == T_DIM && sg_px(x + 1, SG_Y) == T_DIM && sg_px(x + 2, SG_Y) == T_SURF &&
                 sg_px(x + 4, SG_Y) == T_DIM && sg_px(x + 4, SG_Y + 3) == T_SURF);
    ui.song_row = 3; frames(100);
    bad += check("SONG timeline: + ADD selected", sg_px(x, SG_Y) == T_THEME && sg_px(x + 2, SG_Y) == T_SURF);
    chain_defaults(&chain_config);
    ui.song_row = 0;
    return bad;
}

/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */
/* Discussions #92 / #94: the PRESETS knob on every page. STEP and the DRUM grid: the step cursor (a step held: its
 * NUDGE, 1.2: tests/fun10_test.c), the sound and the steps untouched; (Dah) every page: the next sound, the steps
 * untouched */
static int test_presets_knob(void)
{
    int bad = 0, cur_ok = 1, sel_ok = 1, snd_ok = 1, tools_ok = 1, grid_ok, else_ok = 1;
    uint32_t i, n_snd = 0, n_cur = 0, n_else = 0;
    for (i = 0; i < NPAGES; i++) {
        track_t before;
        uint32_t g = PAGES[i].graph, uslot;
        int16_t slot;
        ui_power_on();
        song.sel = 0;
        my_steps(TSEL);
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        frame();
        before = *TSEL;
        uslot = ui.uslot; slot = song.g[G_SLOT];
        (void)uslot; (void)slot;
        turn(EN_PRESET, 1);
        {                                               /* (Dah) PRESETS: the next sound on every page, the steps stay */
            int same_snd = TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p);
            int same_steps = !memcmp(TSEL->step, before.step, sizeof before.step);
            if (detail_on())                           /* (DETAIL: the cursor, its KNOB 1 CHANCE) */
                cur_ok &= same_snd && same_steps;
            else
                snd_ok &= !same_snd && same_steps && ui.page == i && !ui.home;
            n_snd++;
        }
    }
    (void)cur_ok; (void)sel_ok; (void)tools_ok; (void)else_ok; (void)n_cur; (void)n_else;
    bad += check("#94 (Dah) PRESETS on every page: the next sound, the steps stay", snd_ok &&
                 n_snd >= 20u);
    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    TSEL->p[P_SLEN] = 32;
    go_page(GR_ROLL);
    frame();
    turn(EN_K1, 17);
    grid_ok = grid_on() && ui.cursor == 17u && ui.bank == 1u && TSEL->eng_req == ENGI_DRUM;
    bad += check("#92 the DRUM grid: KNOB 1 moves the cursor (and the page) (Dah: PRESETS the sound)", grid_ok);
    ui_power_on();
    hold(B_HOME);
    i = ui.menu_sel;
    turn(EN_PRESET, 1);
    bad += check("#94 the menu: PRESETS still moves the selection", ui.menu == 1 && ui.menu_sel == i + 1u);
    return bad;
}

/* 1.2 (Discussion #153): the pages by button. HOME: HOME > MIXER > CLOCK; SEQ: STEP <-> AUTOMATION, held SEQ TOOLS
 * everywhere; GLO: SONG (GLOBAL became CLOCK, SYSTEM the MENU's); SAVE: USER -> PRESETS -> PHRASES -> PROJECT -> TOOLS (1.5;
 * Dah: FILE -> PATTERN -> MY PRESETS);
 * a remembered page that left its family. (Dah) AUTOMATION: HOME's four knobs as lanes, the roll's controls */
static int test_page_cycles(void)
{
    static const char *const SAVE_CYC[4] = {"FILE", "PATTERN", "MY PRESETS", "FILE"};   /* (1.5, #173;
                                                     * FILE, the projects, then PATTERN, A..D) */
    int bad = 0, ok = 1;
    uint32_t i;
    /* the cycles: each button's taps, in order */
    ui_power_on();
    for (i = 0; i < 4u; i++) {
        press(B_SAVE); frames(320);
        ok &= !ui.home && cur_page()->fam == FAM_SAVE && str_eq(cur_page()->title, SAVE_CYC[i]);
    }
    bad += check("1.5 (#173): SAVE cycles FILE > PATTERN > USER > FILE (Dah: PRESETS, TOOLS, PHRASES gone)", ok);
    ok = 1;
    for (i = 0; i < NPAGES; i++) {                       /* every page in a family its button reaches, and its LED */
        uint32_t f = PAGES[i].fam;
        ok &= f != FAM_SEQ || PAGES[i].graph == GR_ROLL || PAGES[i].graph == GR_EVENTS;
        ok &= f != FAM_GLO || PAGES[i].graph == GR_SONG || str_eq(PAGES[i].title, "DLY") || str_eq(PAGES[i].title, "REVERB") ||
              str_eq(PAGES[i].title, "CHORUS");             /* (SONG, then the global FX) */
        ok &= f != FAM_TRK || PAGES[i].graph == GR_TRK || str_eq(PAGES[i].title, "CLOCK");   /* HOME's: MIXER CLOCK */
        ok &= PAGES[i].graph != GR_TRK || f == FAM_TRK;
        ui_power_on(); ui.home = 0; ui.page = (uint8_t)i; page_entered();
        ok &= FAM_BTN[cur_fam()] == (f == FAM_TRK ? B_HOME : f == FAM_GLO ? B_GLO : f == FAM_SEQ ? B_SEQ :
                                    f == FAM_SAVE ? B_SAVE : FAM_BTN[f]);
    }
    for (i = 0; i < NPAGES; i++)
        ok &= (!str_eq(PAGES[i].title, "PATTERN") || PAGES[i].fam == FAM_SAVE) && !str_eq(PAGES[i].title, "CHANCE") &&
              !str_eq(PAGES[i].title, "AUTO LIST");   /* (PATTERN is SAVE's, the slots A..D) */
    bad += check("1.2: SEQ has STEP and AUTOMATION only; SONG is GLO's only page, MIXER and CLOCK HOME's "
                 "(their LED HOME)", ok);
    {   /* 1.2: GLOBAL and SYSTEM are gone; their knobs: BPM SWG CLK on HOME > CLOCK, TUNE and ROUT on no page (the MENU's),
         * MIDI SYNC CPU on none (INFO shows the USB state and the CPU) */
        uint32_t k, clock = 0;
        ok = 1;
        for (i = 0; i < NPAGES; i++) {
            ok &= !str_eq(PAGES[i].title, "GLOBAL") && !str_eq(PAGES[i].title, "SYSTEM");
            for (k = 0; k < 4u && PAGES[i].scope == SC_GLOBAL; k++)
                ok &= PAGES[i].id[k] != G_TUNE && PAGES[i].id[k] != G_ROUTE && PAGES[i].id[k] != G_MIDI &&
                      PAGES[i].id[k] != G_SYNC && PAGES[i].id[k] != G_INFO;
            if (str_eq(PAGES[i].title, "CLOCK"))
                clock = i + 1u;
        }
        ok &= !clock;                                   /* (Dah: CLOCK gone, its SYNC and SWING GLO's menu's) */
        bad += check("1.2: no GLOBAL, no SYSTEM page (Dah: no CLOCK, SYNC SWING in GLO's menu), TUNE and ROUT on no page", ok);
    }
    /* remembered pages: one that left its family, or out of range, gives the family's first (1.2: GLO's whatever it was,
     * GLOBAL and SYSTEM too, now SONG; HOME's never remembered: from HOME the MIXER) */
    ui_power_on();
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_TRK) ui.fam_last[FAM_GLO] = (uint8_t)i;
        else if (PAGES[i].graph == GR_USER) ui.fam_last[FAM_SEQ] = (uint8_t)i;
    ui.fam_last[FAM_SAVE] = 200u;
    ui.fam_last[FAM_ENV] = 250u;
    open_family(FAM_GLO); ok = str_eq(cur_page()->title, "SONG");
    open_family(FAM_SEQ); ok &= cur_page()->graph == GR_ROLL;
    open_family(FAM_ENV); ok &= str_eq(cur_page()->title, "ENV");
    open_family(FAM_SAVE); ok &= str_eq(cur_page()->title, "FILE");
    open_family(FAM_GLO); open_family(FAM_GLO);          /* (Dah: SONG, again SONG) */
    ok &= str_eq(cur_page()->title, "SONG");
    go_home(); open_family(FAM_GLO); ok &= str_eq(cur_page()->title, "SONG");
    for (i = 0; i < 256u; i++) {                         /* any remembered index (the old GLOBAL 25, SYSTEM 27, ..) */
        go_home(); ui.fam_last[FAM_GLO] = (uint8_t)i; open_family(FAM_GLO);
        ok &= str_eq(cur_page()->title, "SONG") && ui.fam_last[FAM_GLO] == ui.page;
        go_home(); ui.fam_last[FAM_TRK] = (uint8_t)i; press(B_HOME);
        ok &= cur_page()->graph == GR_TRK && !ui.home;
    }
    bad += check("1.2: a remembered page no longer in its family (or none) lands on the family's first; GLO always SONG, "
                 "HOME always the MIXER first", ok);

    {   /* (Dah) AUTOMATION, as STEP's roll: KNOB 1 STEP, 2 LANE, 3 VALUE (the lane's point), 4 CURVE; EDIT clears the
         * point, the cursor on; REC arms; OCT+ held the roll's VIEW */
        track_t *t;
        uint32_t k, l0 = 4u, l1 = 4u, id0 = 0xFFu, id1 = 0xFFu;
        int32_t v0;
        int pt;
        ui_power_on(); t = TSEL;
        go_page(GR_EVENTS); frame();
        ok = act_cols() == 0u && ui.cursor == 0u;
        for (k = 0; k < 4u; k++) {
            uint32_t id = au_id(k);
            ok &= id == 0xFFu || id == ENGINES[t->eng_req % NENGINES]->knob[k];
            if (id != 0xFFu && track_desc(t, id)->fmt != F_ENUM) {
                if (l0 == 4u) l0 = k, id0 = id;
                else if (l1 == 4u) l1 = k, id1 = id;
            }
        }
        ok &= l1 < 4u;
        memset(ui.enc_t, 0, sizeof ui.enc_t); turn(EN_K1, 3); ok &= ui.cursor == 3u;
        bad += check("AUTOMATION: the lanes are HOME's four knobs; KNOB 1 the cursor", ok);
        cursor_set(4); frame();
        turn(EN_K2, -3); ok = au_lane == 0u;
        for (k = 0; k < l0; k++) turn(EN_K2, 1);
        ok &= au_lane == l0;
        v0 = lane_value(t, 4, id0, &pt);
        turn(EN_K3, 3);
        ok &= motion_find(t, 4, id0) >= 0 && lane_value(t, 4, id0, &pt) != v0 && pt &&
              !(motion.event[motion_find(t, 4, id0)].param & MOTION_LOCK) && ui.hot_col == 2u && motion_count(t) == 1u;
        v0 = lane_value(t, 4, id0, &pt);
        ok &= lane_value(t, 5, id0, &pt) == v0 && !pt && motion_find(t, 3, id0) < 0;
        bad += check("  KNOB 2 the lane, KNOB 3 an event on it at the cursor from the lane's value, held after it", ok);
        turn(EN_K4, 1); ok = trkx[song.sel % NTRK].curve == 1;
        turn(EN_K4, -1); ok &= trkx[song.sel % NTRK].curve == 0;
        bad += check("  KNOB 4 CURVE: RAMP / STEP", ok);
        for (k = l0; k < l1; k++) turn(EN_K2, 1);
        (void)motion_set_lock(t, 4, id1, track_desc(t, id1)->min);
        press(B_EDIT);
        ok = au_lane == l1 && motion_find(t, 4, id1) < 0 && motion_find(t, 4, id0) >= 0 && msg_is("POINT CLEARED") &&
             ui.cursor == 5u && cur_page()->graph == GR_EVENTS;
        press(B_EDIT);
        ok &= msg_is("NO POINT HERE") && ui.cursor == 6u;
        bad += check("  EDIT: the lane's point at the cursor (a lock) cleared, the cursor on (STEP's EDIT CLEAR)", ok);
        cursor_set(4);
        press(B_REC); frames(800);
        ok = song.rec != 0u && motion_find(t, 4, id0) >= 0;
        press(B_REC); frames(800);
        ok &= !song.rec;
        transport_req = 2; frame();
        bad += check("  REC arms and disarms as on STEP (no point deleted)", ok);
        prv.zoom = 1; prv.at = -1; prv.nofollow = 0;
        btn_down(B_OCTUP); frames(700);
        ok = prv.on;
        turn(EN_K2, -1);
        ok &= prv_cols() == 32u;
        btn_up(B_OCTUP); frame();
        ok &= !prv.on && prv_cols() == 32u;
        prv.zoom = 1; prv.at = -1;
        bad += check("  OCT+ held: the roll's VIEW (KNOB 2 ZOOM out: 32 steps)", ok);
        chain.running = 1;
        cursor_set(7);
        for (k = l1; k > l0; k--) turn(EN_K2, -1);
        turn(EN_K3, 2);
        ok = motion_find(t, 7, id0) < 0 && msg_is("STOP TO EDIT");
        chain.running = 0;
        bad += check("  while a song plays: STOP TO EDIT, nothing changes", ok);
    }
    return bad;
}

static int led_of(const uint8_t *l, uint32_t id);
static int test_product_ux(void)
{
    int bad = 0, ok = 1;
    uint32_t i, p, b;
    for (i = 0; i < NPAGES; i++) {
        ui_power_on(); set_engine_of(TSEL, 1);         /* (DIGITAL; without FELUCCA_FM4 FM6: no OP pages) */
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        press(B_REC);
        if (PAGES[i].graph == GR_SONG)                 /* (Dah: SONG: REC adds a section, here at the end) */
            ok &= song.rec == 0 && !transport_req && sg_pend == 1u;   /* (Dah: shown, OCT+ adds it) */
        else if (PAGES[i].graph == GR_PATTERN)
            ok &= song.rec == 0 && !transport_req && ui.confirm == CF_DEL_PAT;
        else if (PAGES[i].graph == GR_FILE)
            ok &= song.rec == 0 && !transport_req && msg_is("NO PROJECT");
        else if (PAGES[i].graph == GR_USER)
            ok &= song.rec == 0 && !transport_req && msg_is("EMPTY SLOT");
        else if (PAGES[i].id[0] == P_FXORD && PAGES[i].fam == FAM_FX)   /* (Dah) CHAIN: removes the effect picked */
            ok &= song.rec == 0 && !transport_req && (msg_is("NO EFFECT THERE") || msg_is("EFFECT REMOVED"));
        else if (PAGES[i].graph == GR_ROLL)            /* #133: STEP's roll arms step recording, stopped */
            ok &= song.rec == 1 && !transport_req && ui.page == i && !ui.home;
        else ok &= song.rec == 1 && transport_req == 1 && ui.page == i && !ui.home;
    }
    bad += check("REC stays on every page; PATTERN, FILE, USER, CHAIN: it deletes, SONG adds (the dialog; none: why) (STEP: step recording, stopped)", ok);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the MENU does nothing (the menu stays, no arm, no transport start)",
                 ui.menu == 1 && song.rec == 0 && !transport_req);
    ui.menu_sel = MI_ABOUT; press(B_OCTUP); press(B_REC);
    bad += check("REC in ABOUT does nothing (the document and its scroll stay)",
                 ui.menu == 2 && !ui.menu_scroll && song.rec == 0 && !transport_req);
    ui_power_on(); press(B_HOME);
    bad += check("1.2: HOME tapped on HOME opens the four-channel MIXER", cur_page()->graph == GR_TRK && !ui.home &&
                 cur_fam() == FAM_TRK && FAM_BTN[cur_fam()] == B_HOME);
    int16_t len = TSEL->p[P_SLEN], send = TSEL->p[P_SPRD];
    turn(EN_K3, 1);
    bad += check("MIXER K3 edits WIDTH (Dah: the stereo spread, not the reverb), never sequence length",
                 TSEL->p[P_SLEN] == len && TSEL->p[P_SPRD] == send + 1);
    press(B_HOME);
    ok = ui.home && !ui.menu;                           /* (Dah: no CLOCK: MIXER > HOME) */
    press(B_HOME);
    ok &= cur_page()->graph == GR_TRK;
    {   /* (MIXER: HOME's LED lit; GLO's not) */
        uint8_t pos0[sizeof led_pos];
        ui_leds();
        memcpy(pos0, led_pos, sizeof pos0);
        memset(led_pos, 0xFF, sizeof led_pos);
        led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
        led_pos[panel.btn[B_GLO]] = 0u << 3 | 2u;
        frame(); ui_leds();
        ok &= led_of(fm1_led, panel.btn[B_HOME]) && !led_of(fm1_led, panel.btn[B_GLO]);
        memcpy(led_pos, pos0, sizeof pos0);
    }
    go_title("ENV"); press(B_HOME);
    ok &= ui.home;
    bad += check("1.2: HOME cycles HOME > MIXER > HOME (Dah: no CLOCK; MIXER: HOME's LED); from any other page HOME", ok);
    hold(B_HOME);
    bad += check("1.2: HOME held still opens the menu", ui.menu == 1);
    ui_power_on();
    frames(320);                                       /* (taps further apart than a double tap: #83) */
    press(B_GLO); frames(320); ok = cur_page()->fam == FAM_GLO && str_eq(cur_page()->title, "SONG");
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "SONG") && cur_page()->fam == FAM_GLO && !ui.home;
    go_title("MIXER"); press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "SONG");
    bad += check("GLO opens SONG directly, again SONG (Dah: DELAY REVERB CHORUS are FX's); from elsewhere SONG", ok);
    go_home(); btn_down(B_SEQ); frames(500);
    ok = ui.layer == LAYER_SEQ && ui.home;
    btn_up(B_SEQ); frame();
    bad += check("1.2: SEQ held on HOME is SEQ TOOLS (no SONG, no tap on release)", ok && ui.home && !ui.layer);
    go_page(GR_SONG);
    song.rec = 1; chain.armed = 1; press(B_REC);
    ok = song.rec == 0 && msg_is("REC OFF") && !ui.confirm;   /* (Dah: armed, REC disarms first, every page) */
    frames(320);
    press(B_REC);
    ok &= song.rec == 0 && msg_is("STOP TO EDIT") && !ui.confirm;
    bad += check("REC on SONG armed: disarms (REC OFF); again with the song pending: nothing deleted (STOP TO EDIT)", ok);
    ui_power_on(); open_family(FAM_EDIT);
    ok = 1;
    for (i = 0; i < 8u; i++) { open_family(FAM_EDIT); ok &= page_visible(ui.page) && !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); }
    bad += check("operator envelope pages are hidden on non-DIGITAL instruments", ok);
#if FELUCCA_FM4
    set_engine_of(TSEL, 1); open_family(FAM_EDIT); ok = 0;
    for (i = 0; i < 12u; i++) { if (cur_page()->id[0] == P_FM1_ATK) ok = 1; open_family(FAM_EDIT); }
    bad += check("DIGITAL exposes four envelopes and independent operator levels", ok);
    go_title("OP1 ENV"); turn(EN_K1, 4);
    bad += check("operator envelope K1 edits only OP1 attack", TSEL->p[P_FM1_ATK] == 4 && !TSEL->p[P_FM2_ATK]);
    track_select(2); frame();
    bad += check("changing to a non-FM track leaves the stale operator page", page_visible(ui.page) && cur_page()->id[0] == P_E0);
#else
    set_engine_of(TSEL, ENGI_DIGITAL); open_family(FAM_EDIT); ok = TSEL->eng_req == ENGI_FM6;
    for (i = 0; i < 12u; i++) { ok &= !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); open_family(FAM_EDIT); }
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            ok &= !page_visible(i);
    bad += check("DIGITAL retired: engine 1 asked for loads FM6; the OP ENV / OP LEVEL pages never show", ok);
#endif
    ui_power_on(); song.rec = 1; seq_start(); go_home();
    int16_t baseline = TSEL->p[P_E0]; turn(EN_K1, 1);
    bad += check("REC plus a sound knob records motion through the UI", motion_count(TSEL) == 1);
    press(B_PLAY); events_block(32);
    bad += check("stopping returns the original sound after a recorded knob gesture", TSEL->p[P_E0] == baseline);
    go_page(GR_EVENTS);
    bad += check("#93 the page is AUTOMATION (GR_EVENTS inside)", str_eq(cur_page()->title, "AUTOMATION"));
    motion_full = 1; frame();
    bad += check("#93 the events used up: AUTOMATION FULL", msg_is("AUTOMATION FULL") && !motion_full);
    {   /* #63: a card shows MOTION's icon while the track's motion changes its parameter */
        uint32_t id = ENGINES[TSEL->eng_req]->knob[0];
        ui_power_on(); go_home(); frame();
        ok = !card_motion(0) && !card_motion(1);
        song.rec = 1; seq_start(); go_home(); turn(EN_K1, 1); frame();
        ok &= card_motion(0) && !card_motion(1) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a recorded knob marks its HOME card (and no other)", ok && motion_count(TSEL) == 1);
        song.rec = 0; press(B_PLAY); events_block(32); frame();
        ok = card_motion(0);
        track_select(1); frame();
        ok &= !card_motion(0);                           /* (another track: its own motion, none) */
        track_select(0); frame();
        ok &= card_motion(0);
        bad += check("MOTION: stopped still shown; only on the track that has the motion", ok);
        go_page(GR_TRK); frame();
        ok = !card_motion(0) && !card_motion(1) && !card_motion(2);
        motion_set_event(TSEL, 3, P_PAN, 20); frame();
        ok &= card_motion(1) && !card_motion(0) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a mix parameter's event marks its MIXER card (PAN)", ok);
        motion_clear(TSEL); frame();
        ok = motion_count(TSEL) == 0;
        go_home(); frame();
        ok &= !card_motion(0) && !card_motion(1);
        go_page(GR_TRK); frame();
        ok &= !card_motion(1);
        bad += check("MOTION: cleared, the icon off every card", ok && id < P_COUNT);
    }
    ui_power_on(); ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_PLAY]] = 1; led_pos[panel.btn[B_REC]] = 2;
    song.playing = song.rec = 1; ok = 1;
    for (i = 0; i < 120u; i++) { fm1_ms += 17; ui_leds(); ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u); }
    bad += check("PLAY's green and REC LEDs stay lit across time and audio block phase", ok);
    {   /* the power-on sweep (hal/fm1_input.h fm1_led_anim_on) has the LEDs: the UI writes none of them while it runs,
         * all of its own picture on the next frame after */
        uint8_t l0[FM1_NCOL], d0[FM1_NCOL];
        memset(fm1_led, 0x5A, sizeof fm1_led);
        memset(fm1_led_dim, 0xA5, sizeof fm1_led_dim);
        memcpy(l0, fm1_led, sizeof l0);
        memcpy(d0, fm1_led_dim, sizeof d0);
        host_anim = 1;
        ui_leds();
        ok = !memcmp(l0, fm1_led, sizeof l0) && !memcmp(d0, fm1_led_dim, sizeof d0);
        host_anim = 0;
        ui_leds();
        ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u) && fm1_led[1] != 0x5A;
        bad += check("the power-on LED sweep: the UI leaves the LEDs alone while it runs, takes them after", ok);
    }
    led_pos_init();
    ok = 1;
    for (p = 0; p < NPALETTES; p++) for (b = 0; b < 1u; b++) {
        uint16_t graph[240 * H_GRAPH], columns[240 * (Y_SEP_END - Y_LABEL)];
        ui_power_on(); palette_set(p); settings.zoom = 1;
        open_family(FAM_ENV); frame();
        memcpy(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        memcpy(columns, host_screen + Y_LABEL * 240, sizeof columns);
        ui.hot_col = 1; ui.hot_t = 40; ui.force = 1; ui_draw();
        ok &= !memcmp(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        int changed = 0;
        for (uint32_t y = 0; y < Y_SEP_END - Y_LABEL; y++) for (uint32_t x = 0; x < 240; x++) {
            uint16_t old = columns[y * 240 + x], now = host_screen[(Y_LABEL + y) * 240 + x];
            if (x >= 64 && x < 119) changed |= old != now;
            else ok &= old == now;
        }
        ok &= changed;
    }
    bad += check("all palettes: active column is subtle, stable-size, no zoom over graph", ok);
    ui_power_on(); set_engine_of(TSEL, 4); open_family(FAM_EDIT);
    memset(&sample_wave, 0, sizeof sample_wave); last_note = 60;
    voice_t voices[NVOICE]; memcpy(voices, TSEL->v, sizeof voices);
    ok = 1;
    for (i = 0; i < 2000u && !sample_wave.ready; i++) {
        uint32_t old = sample_wave.pos; sample_wave_tick(TSEL);
        ok &= sample_wave.pos - old <= 512u;
    }
    ok &= sample_wave.ready && !memcmp(voices, TSEL->v, sizeof voices);
    int16_t lo[SAMPLE_WAVE_COLS] = {0}, hi[SAMPLE_WAVE_COLS] = {0};
    voice_t probe = {0}; const smp_zone_t *zone = sample_wave.zone;
    if (zone) for (i = 0; i < zone->n; i++) {
        int32_t v = sample_next(zone, &probe, 0); uint32_t col = i * SAMPLE_WAVE_COLS / zone->n;
        if (v < lo[col]) lo[col] = (int16_t)v;
        if (v > hi[col]) hi[col] = (int16_t)v;
    }
    ok &= !memcmp(lo, sample_wave.lo, sizeof lo) && !memcmp(hi, sample_wave.hi, sizeof hi);
    bad += check("sample waveform is bounded, matches audio IMA decode and leaves voices intact", ok);
    ui_power_on(); return bad;
}

static int test_panel(void)
{
    int bad = 0;
    panel = PANEL_DEFAULT;
    panel.btn[B_PLAY] = panel.btn[B_REC];
    panel_init();
    bad += check("duplicate button mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    bad += check("duplicate knob mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.btn[B_PLAY] = 255;
    panel_init();
    bad += check("out-of-range panel data recovers without an invalid shift", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.dir[EN_K1] = -1;
    panel_init();
    bad += check("a valid reversed knob mapping is retained", panel.dir[EN_K1] == -1);
    return bad;
}

/* ------------------------------------------------- the FX hold layer --- */
static void key_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); keyboard_block(); }
static uint32_t gates(void)                        /* voices of every track with their key down */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static uint32_t white(uint32_t w) { return key_at(0, w); }

/* a layer's key LEDs as the old 250 ms blink read them: lit, and the breathing ones (#119) "on" at even quarters,
 * so (a ^ b) over ms 0 and 250 = the ones that can be pressed, (a & b) = lit */
static uint32_t leds_at(uint32_t ms)
{
    uint32_t br, m;
    fm1_ms = ms;
    m = layer_leds(&br);
    return m | (((ms / 250u) & 1u) ? 0u : br);
}

static int test_layer(void)
{
    int bad = 0, ok, flash;
    uint32_t i, mo, k;
    /* tap: on release, the page; held briefly: no map ever */
    ui_power_on();
    btn_down(B_FX); frame();
    ok = ui.home;
    frames(64); btn_up(B_FX); frame();
    bad += check("FX tap: nothing on press, CHAIN when let go (Dah: FX's first page)", ok && !ui.home &&
                 str_eq(cur_page()->title, "CHAIN"));
    frames(320);                                       /* (not a double tap: #83) */
    press(B_FX);
    bad += check("  a second tap: the next page of the family (an effect's in the chain, or CHAIN again)",
                 cur_page()->fam == FAM_FX && !ui.home);
    ui_power_on();
    btn_down(B_FX);
    for (flash = 0, i = 0; i < 22u; i++) { frame(); flash |= ui.layer; }      /* 0.35 s */
    btn_up(B_FX); frame();
    bad += check("  held 0.35 s (HOLD 0.4): still a tap, the map never shown", !flash && !ui.home);
    /* peek: past HOLD alone, the map; letting go does nothing */
    ui_power_on();
    btn_down(B_FX); frames(368);
    ok = !ui.layer;
    frames(64);
    ok &= ui.layer == LAYER_FX;
    btn_up(B_FX); frame();
    bad += check("FX held past 0.4 s: the map (not before); let go: no page, the map gone",
                 ok && ui.home && !ui.layer);
    /* the HOLD setting: 0.6 s */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_HOLD; turn(EN_K1, 1); turn(EN_K1, 1); ok = settings_hold == 3u; hold(B_HOME);
    btn_down(B_FX); frames(512); flash = ui.layer; btn_up(B_FX); frame();
    bad += check("menu HOLD 0.6 s (KNOB 1): FX held 0.5 s is a tap, no map", ok && !flash && !ui.home && !ui.menu);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); settings_hold = HOLD_DEF;
        bad += check("  HOLD is saved with the settings and read back", settings_import(&p, sizeof p) && settings_hold == 3u);
    }
    /* the LEDS setting: KNOB 1 / OCT+ up to INV, left / OCT- down to OFF; saved in the retired zoom field */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_LEDS;
    ok = settings_leds == LEDS_DIM;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV && !leds_inv();
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV && leds_inv() == 1u;   /* (1.5.6, #212: INV LO, INV HI after INV) */
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV && leds_inv() == 2u;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV && leds_inv() == 2u;
    turn(EN_K1, -1); turn(EN_K1, -1); ok &= settings_leds == LEDS_INV && !leds_inv();
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM_LO;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM_LO && ui.menu == 1u;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV && leds_inv() == 1u && ui.menu == 1u;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV && leds_inv() == 2u;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV && leds_inv() == 2u && ui.menu == 1u;   /* (stops at the end) */
    for (k = 0; k < 6u; k++) press(B_OCTDN);
    ok &= settings_leds == LEDS_OFF && ui.menu == 1u;
    ok &= !strcmp(LEDS_NAME[LEDS_OFF], "OFF") && !strcmp(LEDS_NAME[LEDS_DIM_LO], "DIM LO") &&
          !strcmp(LEDS_NAME[LEDS_DIM], "DIM HI") && !strcmp(LEDS_NAME[LEDS_INV], "INV");
    {   /* a setting changed from elsewhere (the editor's MENU_SET: menu_put) while the MENU is shown: redrawn */
        static uint16_t before[240 * 240];
        frame();
        memcpy(before, host_screen, sizeof before);
        menu_put(MI_LEDS, 3);                          /* (OFF -> INV, the row shown) */
        frame();
        ok &= settings_leds == LEDS_INV && memcmp(before, host_screen, sizeof before) && menu_get(MI_LEDS) == 3u;
        menu_put(MI_LEDS, 0);
        frame();
        ok &= !memcmp(before, host_screen, sizeof before);
    }
    hold(B_HOME);
    bad += check("menu LEDS: DIM HI by default; KNOB 1 OFF < DIM LO < DIM HI < INV < INV LO < INV HI (stops at the ends), OCT+ / OCT- the same; "
                 "a change from the editor redraws the shown MENU", ok && !ui.menu);
    {
        persist_t p = {0};
        uint32_t m;
        p.magic = PERSIST_MAGIC; p.panel = panel;
        ok = 1;
        settings.zoom = 0;                              /* (DIM HI keeps an older record's 0 / 1 as saved) */
        for (m = 0; m < LEDS_COUNT; m++) {
            settings_leds = (uint8_t)m;
            settings_export(&p); settings_leds = LEDS_DIM;
            ok &= settings_import(&p, sizeof p) && settings_leds == m && leds_stored_ok(p.zoom) &&
                  p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
            settings_export(&p);                    /* (idempotent: saved again, the same field) */
            ok &= p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
        }
        settings_leds = LEDS_DIM;
        settings_export(&p);
        bad += check("  LEDS is saved with the settings and read back (all four); DIM HI saves the field as before (0)",
                     ok && p.zoom == 0u && settings_import(&p, sizeof p) && settings_leds == LEDS_DIM);
    }
    /* combo: a key with FX: at once, silent, no MIDI, no recording, no step */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    go_page(GR_ROLL); my_steps(TSEL); song.rec = 1; song.playing = 1;
    {
        step_t before[NSTEP];
        uint32_t cur = ui.cursor;
        memcpy(before, TSEL->step, sizeof before);
        btn_down(B_FX); key_down(white(2)); frame();
        ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && (kb_layer >> white(2)) & 1u &&
             (perf_held & PF_BIT(PF_R32));                    /* white key 3 (A3): REPEAT 1/32 */
        for (i = 0; i < 40u; i++) events_block(CTL);
        ok &= !memcmp(before, TSEL->step, sizeof before) && ui.cursor == cur;
        bad += check("FX + a key: the map at once; no voice, no MIDI, no recording, no step written", ok);
        key_up(white(2)); frame();
        bad += check("  the key let go: its effect off, no note-off sent", !perf_held && !kb_layer && mo_w == mo);
        btn_up(B_FX); frame();
        bad += check("  FX let go after a combo: no tap, the map gone", !ui.layer && cur_page()->graph == GR_ROLL);
    }
    song.playing = 0; song.rec = 0;
    /* FX let go first: the effect stays with its key, the map too; a key pressed now is a note */
    ui_power_on();
    btn_down(B_FX); key_down(white(4)); frame();          /* white key 5 (C4): LPF */
    btn_up(B_FX); frame();
    ok = (perf_held & PF_BIT(PF_LPF)) && ui.layer == LAYER_FX;
    key_down(white(0)); frame();
    ok &= gates() > 0 && !((kb_layer >> white(0)) & 1u);
    key_up(white(0)); key_up(white(4)); frame();
    bad += check("FX let go first: the key holds its effect and the map; a new key is a note", ok && !ui.layer && !perf_held);
    /* OCT UP (G4) and OCT DN (A4), the 9th and 10th white keys: held effects, silent; KNOB 4 the shimmer while
     * held, back to 0 with FX; B4 on does nothing */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    btn_down(B_FX); key_down(white(8)); frame();
    ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && perf_held == PF_BIT(PF_OUP);
    perf_begin(CTL);                                    /* (an audio block: OCT UP runs, KNOB 4 is SHIMR) */
    turn(EN_K4, 40);
    ok &= perf_k[3] == 40;
    key_up(white(8)); key_down(white(9)); frame();
    ok &= perf_held == PF_BIT(PF_ODN) && !gates() && mo_w == mo;
    key_up(white(9)); key_down(white(10)); frame();
    ok &= perf_held == PF_BIT(PF_FLG) && (kb_layer >> white(10)) & 1u && !gates();   /* (1.2: B4 FLANGER, C5 PHASER) */
    key_up(white(10)); key_down(white(12)); frame();
    ok &= perf_held == PF_BIT(PF_RTHR) && (kb_layer >> white(12)) & 1u && !gates();   /* (1.5.1: D5 REV THROW) */
    key_up(white(12)); key_down(white(14)); frame();
    ok &= !perf_held && (kb_layer >> white(14)) & 1u && !gates();
    key_up(white(14)); btn_up(B_FX); frame();
    bad += check("FX + G4 / A4: OCT UP / OCT DN held, silent, no MIDI; KNOB 4 turns; B4 FLANGER, D5 REV THROW, F5 nothing; FX let go: K4 0",
                 ok && !perf_held && !ui.layer && !perf_k[3] && mo_w == mo);
    /* REVERB > TYPE (FX family, global): ROOM / SPRING / HALL on KNOB 1 (HALL at power-on since 1.2), kept by a
     * project */
    ui_power_on();
    TSEL->p[P_REV] = 40;                                 /* (Dah: REVERB in the chain: its page, FX's) */
    go_title("REVERB");
    ok = cur_page()->fam == FAM_FX && cur_page()->id[0] == (PS_TRK | P_REV) && cur_page()->id[1] == G_RTYPE &&
         cur_page()->id[2] == G_RSIZE && cur_page()->id[3] == G_RDAMP && song.g[G_RTYPE] == 2 &&
         str_eq(GP[G_RTYPE].names[2], "HALL");
    turn(EN_K2, -2);
    ok &= song.g[G_RTYPE] == 0 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "ROOM");
    turn(EN_K2, 1);
    ok &= song.g[G_RTYPE] == 1 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "SPRING");
    turn(EN_K2, 5);
    ok &= song.g[G_RTYPE] == 2;
    turn(EN_K2, -1);
    ok &= song.g[G_RTYPE] == 1;
    song.playing = 0;
    project_save(2);
    turn(EN_K2, -1);
    ok &= song.g[G_RTYPE] == 0;
    project_load(2);
    bad += check("REVERB page (Dah: FX's, SEND first): TYPE HALL (power-on) / ROOM / SPRING on KNOB 2 (SIZE, DAMP after it); a project keeps SPRING",
                 ok && song.g[G_RTYPE] == 1);
    TSEL->p[P_CHOR] = 30;
    go_title("CHORUS");
    bad += check("  CHORUS page: SEND CRT CDP", cur_page()->fam == FAM_FX && cur_page()->id[0] == (PS_TRK | P_CHOR) &&
                 cur_page()->id[1] == G_CRATE && cur_page()->id[2] == G_CDEPTH && cur_page()->id[3] == 0xFFu);
    /* chording: a key held before FX stays a note, its note-off arrives */
    ui_power_on();
    mo = mo_w;
    key_down(white(4)); frame(); btn_down(B_FX); frame(); key_up(white(4)); frame(); btn_up(B_FX); frame();
    for (i = 0; i < 4u; i++) events_block(CTL);
    bad += check("a key held before FX stays a note: note-on, note-off, nothing left held", mo_w == mo + 2u && !gates() &&
                 !kb_layer);
    /* knob macros: not the page's, not recorded, back to 0 when FX is let go */
    ui_power_on();
    go_title("ENV"); song.rec = 1; song.playing = 1;
    {
        int16_t atk = TSEL->p[P_ATK];
        uint32_t ev = motion.count;
        btn_down(B_FX); frame();
        turn(EN_K1, -12); turn(EN_K2, 30);
        ok = perf_k[0] == -12 && perf_k[1] == 30 && TSEL->p[P_ATK] == atk && motion.count == ev && ui.layer == LAYER_FX;
        turn(EN_PRESET, 3);
        ok &= user_of(TSEL) == UP_SLOTS && TSEL->preset == trk[0].preset;
        btn_up(B_FX); frame();
        bad += check("FX + KNOB 1 / 2: the macros, not ATK, not recorded; FX let go: back to 0, no tap",
                     ok && !perf_k[0] && !perf_k[1] && str_eq(cur_page()->title, "ENV"));
    }
    song.rec = 0; song.playing = 0;
    /* buttons in the layer: PLAY works, SAVE, HOME, a page button are swallowed */
    ui_power_on();
    btn_down(B_FX); frame();
    press(B_PLAY);
    ok = transport_req == 1u && ui.layer == LAYER_FX;
    transport_req = 0;
    press(B_SAVE); ok &= ui.home;
    hold(B_SAVE); ok &= ui.home;
    press(B_ENV); ok &= ui.home;
    hold(B_HOME); ok &= !ui.menu && ui.home;
    btn_up(B_FX); frame();
    bad += check("in the layer: PLAY plays; SAVE (tap and hold), ENV, HOME (hold) do nothing", ok && ui.home && !ui.menu);
    btn_down(B_FX); frame(); btn_down(B_ENV); frame(); btn_up(B_FX); frame(); btn_up(B_ENV); frame();
    bad += check("  a page button pressed with FX, let go after it: still nothing", ui.home);
    /* release-act: every page button acts when let go */
    {
        static const uint8_t PB[6] = {B_ENV, B_LFO, B_SCL, B_ARP, B_GLO, B_EDIT};
        static const uint8_t PF[6] = {FAM_ENV, FAM_LFO, FAM_SCL, FAM_ARP, FAM_GLO, FAM_EDIT};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            ui_power_on();
            btn_down(PB[i]); frame();
            ok &= ui.home;
            btn_up(PB[i]); frame();
            ok &= !ui.home && cur_page()->fam == PF[i];
            if (PB[i] == B_SCL || PB[i] == B_GLO || PB[i] == B_EDIT)
                continue;                             /* (a layer of their own: held long is a peek) */
            ui_power_on();
            hold(PB[i]);                              /* (no layer of their own: a long press is a tap too) */
            ok &= !ui.home && cur_page()->fam == PF[i];
        }
        bad += check("ENV LFO SCL ARP GLO EDIT act when let go, not on press (ENV LFO ARP held long: the same)", ok);
    }
    ui_power_on();
    go_page(GR_ROLL); my_steps(TSEL); ui.cursor = 0;
    btn_down(B_EDIT); frame();
    ok = step_on(&TSEL->step[0]) && ui.cursor == 0;
    btn_up(B_EDIT); frame();
    bad += check("EDIT on STEP clears the step when let go (not on press)", ok && !step_on(&TSEL->step[0]) && ui.cursor == 1);
    ui_power_on(); hold(B_HOME);
    ok = ui.menu == 1; hold(B_HOME); ok &= !ui.menu;
    go_home(); hold(B_SEQ); ok &= ui.home && !ui.layer;
    bad += check("HOME (menu) hold of 0.7 s unchanged; SEQ held: its layer, no page (1.2)", ok);
    /* no layer in the menu or a dialog: FX + a key is a note */
    ui_power_on(); hold(B_HOME);
    btn_down(B_FX); frame(); key_down(white(3)); frame();
    ok = !ui.layer && !perf_mask && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_FX); frame();
    bad += check("in the menu: no layer, FX + a key plays the key; FX let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_INIT_SOUND;
    btn_down(B_FX); frames(512);
    ok = !ui.layer;
    btn_up(B_FX); frame();
    bad += check("in a dialog: FX held shows no map and taps nothing", ok && ui.confirm == CF_INIT_SOUND);
    /* UBOOT: the countdown closes the map and stops every effect; OCT- + OCT+ are still seen */
    ui_power_on();
    btn_down(B_FX); key_down(white(5)); frame();
    btn_down(B_OCTDN); btn_down(B_OCTUP); frame();
    ok = ui.layer == LAYER_FX && (fm1_in.buttons & 3u << panel.btn[B_OCTDN]) != 0u;
    ui.uboot = 3; frame();                          /* (main.c: OCT- + OCT+ held 2 s) */
    ok &= !ui.layer && perf_kill && !perf_mask && perf_begin(CTL) >= 0;
    ok &= (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
          ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));
    ui.uboot = 0;
    btn_up(B_OCTDN); btn_up(B_OCTUP); key_up(white(5)); btn_up(B_FX); frame();
    bad += check("UBOOT countdown over the layer: the map closes, effects off; OCT- + OCT+ untouched", ok && !perf_kill && !kb_layer);
    /* the LEDs of the map */
    ui_power_on();
    btn_down(B_FX); key_down(white(1)); frame();
    {
        uint32_t a, b2;
        song.g[G_BPM] = 120;
        a = leds_at(0); b2 = leds_at(250);
        k = white(1);
        ok = ((a >> k) & 1u) && ((b2 >> k) & 1u);                    /* held: lit */
        for (i = 0; i < 14u; i++)                                     /* the other effects (F3 .. E5): breathe */
            ok &= i == 1u || ((a ^ b2) >> white(i)) & 1u;
        for (i = 14; i < 16u; i++)                                    /* F5 G5: no effect, dark */
            ok &= !((a | b2) >> white(i) & 1u);
        ok &= ((a ^ b2) >> key_at(1, 0)) & 1u && !((a | b2) >> key_at(1, 4) & 1u);   /* a mute breathes, a spare black dark */
        song.g[G_BPM] = 72;
        a = leds_at(0); b2 = leds_at(250);
        ok &= !((a | b2) >> white(0) & 1u) && !((a | b2) >> white(3) & 1u);   /* 1/8, REVERSE: too long at 72 */
        ok &= ((a ^ b2) >> white(2)) & 1u;                            /* 1/32 still blinks */
        song.g[G_BPM] = 120;
    }
    key_up(white(1)); btn_up(B_FX); frame();
    bad += check("map LEDs: held lit, the 14 effects breathe (1.5.1: D5 E5 the throws), F5 G5 and a too-long REPEAT dark", ok);
    usb.config = 0;
    return bad;
}

/* ------------------------------------------------------------ NAME --- */
static void nm_tap(uint32_t k)                    /* a key tapped in NAME: down a frame, up a frame (32 ms) */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame();
    fm1_in.notes &= ~(1u << k); frame();
}
static uint32_t nm_black_key(uint32_t f, uint32_t octave)   /* the black key of function f (NB_*: F# G# A# C# D#), octave 0 / 1 */
{
    return key_at(1, f + 5u * octave);
}
static int test_name(void)
{
    int bad = 0, ok;
    uint32_t i, mo;
    char b[16];
    ui_power_on();
    go_page(GR_USER);
    ui.uslot = 2;
    press(B_OCTUP);                               /* (SAVE picked on entry) */
    up_auto_name(b, TSEL->eng_req, 2);
    bad += check("NAME: USER SAVE into an empty slot opens NAME, prefilled with the automatic name",
                 name_on() && nm.kind == NK_USER_SAVE && nm.slot == 2u && str_eq(nm.s, b) && nm.cur == nm.len);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    bad += check("  DELETE removes the character before the cursor, down to empty", nm.len == 0u && nm.cur == 0u && !nm.s[0]);
    /* multi-tap: the same key cycles, another key commits, 0.8 s commits */
    nm_tap(white(0));
    ok = nm.len == 1u && nm.s[0] == 'A' && nm.key == 1u && nm.cur == 0u;
    nm_tap(white(0));
    ok &= nm.s[0] == 'B' && nm.len == 1u;
    nm_tap(white(0));
    bad += check("  a white key types its first letter; tapped again within 0.8 s: the next one, cycling (A B A)",
                 ok && nm.s[0] == 'A' && nm.len == 1u && nm.cur == 0u);
    nm_tap(white(1));
    bad += check("  another key keeps the letter and types its own (A, C)", nm.len == 2u && str_eq(nm.s, "AC") && nm.cur == 1u &&
                 nm.key == 2u);
    frames(800);
    bad += check("  0.8 s without a tap keeps it: the cursor moves on", nm.key == 0u && nm.cur == 2u && str_eq(nm.s, "AC"));
    nm_tap(white(1));
    frames(400);
    nm_tap(white(1));
    bad += check("  a second tap within 0.8 s (0.43 s later) still cycles (C -> D)", str_eq(nm.s, "ACD") && nm.key == 2u);
    frames(800);
    nm_tap(white(4)); nm_tap(white(4)); nm_tap(white(4));   /* IJK: K */
    nm_tap(nm_black_key(NB_SPACE, 1));                       /* SPACE (the upper octave's G#) keeps K first */
    bad += check("  a three-letter key (IJK) cycles to K; SPACE keeps it and adds a space", str_eq(nm.s, "ACDK ") && nm.cur == 5u &&
                 !nm.key);
    nm_tap(nm_black_key(NB_DEL, 1));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(white(2));                             /* E inserted before D */
    frames(800);
    bad += check("  DELETE, cursor left twice, a letter inserted at the cursor", str_eq(nm.s, "ACEDK") && nm.cur == 3u);
    nm_tap(nm_black_key(NB_RIGHT, 1));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    bad += check("  cursor right stops at the end", nm.cur == 5u);
    /* 123 */
    nm_tap(nm_black_key(NB_MODE, 0));
    nm_tap(white(1)); nm_tap(white(9)); nm_tap(white(15));
    bad += check("  D# switches to 123: one tap, one character (2, 0, +), no cycling", nm.num && str_eq(nm.s, "ACEDK20+") &&
                 nm.cur == 8u && !nm.key);
    nm_tap(nm_black_key(NB_MODE, 1));
    nm_tap(white(15)); nm_tap(white(15)); nm_tap(white(15));
    frames(800);
    bad += check("  back to ABC: the last key (0-.) gives 0, then -, then .", !nm.num && str_eq(nm.s, "ACEDK20+.") && nm.cur == 9u);
    /* the knobs */
    turn(EN_K1, -2);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 moves the cursor, KNOB 2 changes the character there (+ -> SPACE, wrapping)",
                 nm.cur == 7u && str_eq(nm.s, "ACEDK20 ."));
    turn(EN_K2, 5);
    bad += check("  KNOB 2 on: SPACE + 5 = E", nm.s[7] == 'E');
    turn(EN_K1, 9);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 to the end, KNOB 2 adds a character there (A)", str_eq(nm.s, "ACEDK20E.A") && nm.cur == 9u && nm.len == 10u);
    turn(EN_K2, -2);
    bad += check("  .. and turns it back past SPACE to the last symbol (+)", nm.s[9] == '+');
    /* full */
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(white(7)); frames(800); nm_tap(white(7)); frames(800);
    ui.msg_t = 0;
    nm_tap(white(7));
    bad += check("  12 characters at most: NAME FULL, nothing typed", nm.len == 12u && msg_is("NAME FULL") && str_eq(nm.s, "ACEDK20E.+PP"));
    /* held DELETE repeats */
    fm1_in.notes |= 1u << nm_black_key(NB_DEL, 0); host_notes |= 1u << nm_black_key(NB_DEL, 0);
    frames(16 * 40);
    fm1_in.notes &= ~(1u << nm_black_key(NB_DEL, 0)); frame();
    bad += check("  DELETE held 0.64 s: one, then it repeats after 0.45 s, every 0.09 s (3 in all)", nm.len == 9u);
    /* the keys never sound, record or send MIDI */
    usb.config = 1; mo = mo_w; song.rec = 1;
    {
        step_t before[NSTEP];
        memcpy(before, TSEL->step, sizeof before);
        key_down(white(3)); key_down(nm_black_key(NB_SPACE, 0)); frame();
        for (i = 0; i < 8u; i++) events_block(CTL);
        ok = !gates() && mo_w == mo && !memcmp(before, TSEL->step, sizeof before) && song.grid == 2u;
        key_up(white(3)); key_up(nm_black_key(NB_SPACE, 0)); frame();
        bad += check("  keys in NAME: no voice, no MIDI, nothing recorded (seq.c: song.grid 2)", ok && mo_w == mo);
    }
    song.rec = 0; usb.config = 0;
    {   /* LEDs: all 27 keys do something; the cycling key blinks */
        uint32_t a, c, t0;
        nm_tap(nm_black_key(NB_DEL, 0)); nm_tap(nm_black_key(NB_DEL, 0));
        nm_tap(white(5));
        t0 = (fm1_ms / 500u + 1u) * 500u;
        fm1_ms = t0; a = name_leds(); fm1_ms = t0 + 250u; c = name_leds();
        nm.t = fm1_ms;                            /* (the letter still cycling) */
        bad += check("  LEDs: every key lit, the cycling key (LM) blinks", ((a | c) == 0x7FFFFFFu) && ((a ^ c) == (1u << white(5))));
    }
    /* cancel */
    {
        uint32_t page = ui.page;
        press(B_OCTDN);
        bad += check("  OCT- cancels: nothing written, back on the USER page", !name_on() && !up_used(2) && ui.page == page && !ui.home);
    }
    /* typing a name and saving it */
    ui.act = 4;
    press(B_OCTUP);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(1)); nm_tap(white(0)); nm_tap(white(0)); nm_tap(nm_black_key(NB_RIGHT, 0));    /* C B */
    nm_tap(nm_black_key(NB_SPACE, 0)); nm_tap(nm_black_key(NB_MODE, 0)); nm_tap(white(1));        /* " 2" */
    nm_tap(nm_black_key(NB_SPACE, 0));                                                           /* a trailing space */
    song.playing = 1;
    press(B_OCTUP);
    bad += check("  OCT+ while playing: STOP TO SAVE, NAME stays", name_on() && msg_is("STOP TO SAVE") && !up_used(2));
    stop_transport();
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+ stopped: saved as typed, the trailing space dropped (CB 2)", !name_on() && up_used(2) && str_eq(b, "CB 2") &&
                 msg_is("SAVED (RAM)"));
    /* SAVE held does nothing in NAME; the FX layer neither */
    ui.act = 4; press(B_OCTUP); press(B_OCTUP);   /* (U03 used: OVERWRITE?, YES) */
    bad += check("  overwrite: the dialog, then NAME", name_on() && nm.kind == NK_USER_SAVE);
    ui.msg_t = 0;
    hold(B_SAVE);
    btn_down(B_FX); frames(600); ok = !ui.layer; btn_up(B_FX); frame();
    bad += check("  SAVE held, FX held: nothing in NAME (no map, no page)", name_on() && ok && cur_page()->graph == GR_USER &&
                 !ui.msg_t);
    press(B_OCTDN);
    /* rename: EDIT on USER */
    ui.uslot = 5;
    press(B_EDIT);
    bad += check("  EDIT on an empty slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT") && cur_page()->graph == GR_USER);
    ui.uslot = 2;
    up_load(2);                                   /* (the sound now comes from U03) */
    trk[0].p[P_E0] = 33;                          /* an edit: a rename must not store it */
    press(B_EDIT);
    bad += check("  EDIT on a used slot: NAME (rename) with its name", name_on() && nm.kind == NK_USER_RENAME && str_eq(nm.s, "CB 2"));
    nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(10)); nm_tap(white(10));          /* VW: W */
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+: renamed (CB W), the sound stored there unchanged", !name_on() && str_eq(b, "CB W") &&
                 up_value(up_rec(2), P_E0) != 33 && msg_is("RENAMED (RAM)"));
    press(B_EDIT);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    press(B_OCTUP);
    {
        char au[16];
        up_name(2, b);
        up_auto_name(au, up_rec(2)->engine, 2);
        bad += check("  a name deleted to nothing: the automatic one", str_eq(b, au));
    }
    /* the sound's own name is the prefill */
    trk[0].user = 3;
    ui.uslot = 7; ui.act = 4;
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  SAVE of a sound that came from U03: prefilled with U03's name", name_on() && str_eq(nm.s, b));
    press(B_OCTDN);
    /* the project open: FILE, EDIT renames it */
    go_page(GR_FILE);
    proj_name[0] = 0;
    press(B_EDIT);
    ok = name_on() && nm.kind == NK_FILE_RENAME && str_eq(nm.ph, "PROJECT");
    for (i = 0; i < 4u; i++) { nm_tap(white(7)); nm_tap(white(6)); frames(800); }   /* P N P N .. */
    press(B_OCTUP);
    bad += check("FILE: EDIT renames the project open: NAME, typed (PNPNPNPN), its name", ok && !name_on() &&
                 str_eq(proj_name, "PNPNPNPN"));
    press(B_EDIT);
    bad += check("  again: NAME prefilled with it", name_on() && str_eq(nm.s, "PNPNPNPN"));
    hold(B_HOME);
    bad += check("  HOME held in NAME: the menu, NAME closed", ui.menu && !name_on());
    hold(B_HOME);
    return bad;
}

/* the EDIT cycle (no ENGINE page: engines are the EDIT layer's) and its memory */
static int engine_cycle(const char *const *want, uint32_t n)   /* EDIT tapped from HOME, then n - 1 times more */
{
    uint32_t i;
    int ok = 1;
    frames(320);
    go_home(); frame();
    for (i = 0; i < n; i++) {
        press(B_EDIT);
        frames(320);                                  /* (taps further apart than a double tap: #83 locks) */
        ok &= str_eq(cur_page()->title, want[i]);
    }
    return ok;
}
static int test_edit_cycle(void)
{
    static const char *const CYC_A[] = {"EDIT 1", "EDIT 2", "FILTER", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};   /* (1.5: FILTER) */
    static const char *const CYC_D[] = {"EDIT 1", "EDIT 2", "OP1 ENV", "OP2 ENV", "OP3 ENV", "OP4 ENV",
                                        "OP LEVEL", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    btn_down(B_EDIT); frame();
    ok = ui.home;
    btn_up(B_EDIT); frame();
    bad += check("EDIT opens EDIT 1, when let go (not on press)", ok && str_eq(cur_page()->title, "EDIT 1"));
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "ENGINE");
    bad += check("no ENGINE page (engines are the EDIT layer's)", ok);
    set_engine_of(TSEL, 0);
    bad += check("EDIT cycle (ANALOG): EDIT 1 EDIT 2 FILTER VOICE VOICE 2 VOICE 3 EDIT 1", engine_cycle(CYC_A, NELEM(CYC_A)));
#if FELUCCA_FM4
    set_engine_of(TSEL, 1);
    bad += check("EDIT cycle (DIGITAL): EDIT 1 EDIT 2 OP1..OP4 ENV OP LEVEL VOICE VOICE 2 VOICE 3 EDIT 1",
                 engine_cycle(CYC_D, NELEM(CYC_D)));
#else
    set_engine_of(TSEL, ENGI_DIGITAL);
    {
        static const char *const CYC_F[] = {"EDIT 1", "EDIT 2", "OPERATOR", "OP ENV", "OPERATOR 2", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
        bad += check("EDIT cycle (engine 1 asked for: FM6, DIGITAL retired): EDIT 1 EDIT 2 OPERATOR OP ENV OPERATOR 2 VOICE .. VOICE 3",
                     TSEL->eng_req == ENGI_FM6 && engine_cycle(CYC_F, NELEM(CYC_F)));
    }
    (void)CYC_D;
#endif
    {   /* #97: a DRUM track's lane levels on EDIT > LANES (KICK SNARE CLAP HATCL) and LANES 2 (HATOP TOM RIM BELL) */
        static const char *const CYC_K[] = {"EDIT 1", "EDIT 2", "LANES", "LANES 2", "DESIGN", "DESIGN 2", "VOICE", "VOICE 2",
                                            "VOICE 3", "EDIT 1"};
        set_engine_of(TSEL, ENGI_DRUM);
        bad += check("EDIT cycle (DRUM): EDIT 1 EDIT 2 LANES LANES 2 DESIGN DESIGN 2 VOICE VOICE 2 VOICE 3 EDIT 1",
                     engine_cycle(CYC_K, NELEM(CYC_K)));
        {   /* the drum designer (ui_drumlane.c, eng_drum.c dl_*): a lane's own VOICE TUNE DECAY / TONE SNAP STACK */
            uint32_t tr = song.sel % NTRK;
            bad += check("DESIGN: a kit's lanes at their defaults (no lanes in the patch)", !dl_on(tr) &&
                         dl_get(tr, DV_CLAP, DL_VOICE) == 0u && dl_get(tr, DV_CLAP, DL_TUNE) == DL_DEF[DL_TUNE]);
            go_title("DESIGN"); frame();
            ui.lane = 0;
            turn(EN_K1, 2);
            turn(EN_K2, 5);
            turn(EN_K3, 3);
            frame();
            bad += check("DESIGN: KNOB 1 the lane (CLAP), KNOB 2 its VOICE (HAT CL), KNOB 3 its TUNE (+3), the other lanes stay",
                         ui.lane == DV_CLAP && dl_on(tr) && dl_get(tr, DV_CLAP, DL_VOICE) == 5u &&
                         dl_role(5) == DVT_HATC && dl_get(tr, DV_CLAP, DL_TUNE) == DL_DEF[DL_TUNE] + 3u &&
                         dl_get(tr, DV_KICK, DL_VOICE) == 0u && dl_get(tr, DV_SNARE, DL_TUNE) == DL_DEF[DL_TUNE]);
            go_title("DESIGN 2"); frame();
            turn(EN_K4, 1);
            frame();
            bad += check("DESIGN 2: KNOB 4 stacks the KICK on the CLAP lane: a clap strikes a kick with it",
                         dl_get(tr, DV_CLAP, DL_STACK) == 1u && drum_stack_note(TSEL, 39) == 36u && drum_stack_note(TSEL, 36) == 0u);
            {
                char nm[12];
                dl_voice_name(1u + DVT_COUNT + 2u * DV_NLANE + 1u, nm);   /* kit 66's snare */
                bad += check("DESIGN: a model kit's piece named by its kit and lane (66 SD)", str_eq(nm, "66 SD"));
            }
            {   /* phase 2: a lane on SYN 1..5 (the layered voice) shows its layer set's pages, SYN TONE .. SYN METAL */
                static const char *const CYC_S[] = {"EDIT 1", "EDIT 2", "LANES", "LANES 2", "DESIGN", "DESIGN 2",
                                                    "SYN TONE", "SYN TONE 2", "SYN NOISE", "SYN NOISE 2", "SYN METAL",
                                                    "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
                const char *lb, *u;
                char val[12];
                int32_t r;
                uint32_t ic, c0 = ds_get(tr, 2, DS_NCLP), r0 = ds_get(tr, 2, DS_NRES);
                char nm[12];
                dl_voice_name(DL_NVOICE + 2u, nm);
                bad += check("DESIGN: VOICE SYN 3 named, plays the layered voice", str_eq(nm, "SYN 3") &&
                             dl_role(DL_NVOICE + 2u) == DVT_SYNTH && dl_syn(DL_NVOICE + 2u) == 2 && DL_MAX[DL_VOICE] ==
                             DL_NVOICE + DS_NSET - 1u);
                ui.lane = DV_SNARE;
                bad += check("EDIT cycle (DRUM): no SYN pages while the lane picked plays no layer set",
                             engine_cycle(CYC_K, NELEM(CYC_K)));
                go_title("DESIGN"); frame();
                ui.lane = DV_CLAP;
                turn(EN_K2, (int32_t)(DL_NVOICE + 2u) - (int32_t)dl_get(tr, DV_CLAP, DL_VOICE));
                frame();
                bad += check("DESIGN: KNOB 2 puts the CLAP lane on SYN 3", dl_syn_of(tr, DV_CLAP) == 2 && ds_on(tr));
                bad += check("EDIT cycle (DRUM, the CLAP lane on SYN 3): .. DESIGN 2 SYN TONE SYN TONE 2 SYN NOISE SYN NOISE 2 "
                             "SYN METAL VOICE ..", engine_cycle(CYC_S, NELEM(CYC_S)));
                go_title("SYN TONE"); frame();
                (void)dln_card(2, &lb, val, &u, &r, &ic);
                bad += check("SYN TONE: PITCH of SYN 3 shown in Hz (185 HZ), its layer at LEVEL 0 DIM",
                             str_eq(lb, "PITCH") && str_eq(val, "185") && str_eq(u, "HZ") && !dln_card(2, &lb, val, &u, &r, &ic));
                turn(EN_K1, 1);
                frame();
                bad += check("SYN TONE: KNOB 1 stays on the CLAP lane (no other lane plays a set)", ui.lane == DV_CLAP);
                dl_set(tr, DV_KICK, DL_VOICE, DL_NVOICE);
                turn(EN_K1, -1);
                frame();
                bad += check("SYN TONE: KNOB 1 steps over the lanes playing no set (CLAP -> KICK)", ui.lane == DV_KICK &&
                             str_eq(cur_page()->title, "SYN TONE"));
                turn(EN_K2, -3);
                frame();
                bad += check("SYN TONE: KNOB 2 the KICK's set's tonal LEVEL (SYN 1), not the CLAP's",
                             ds_get(tr, 0, DS_TLVL) == DV_SYN_DEF[0][DS_TLVL] - 3u && ds_get(tr, 2, DS_TLVL) == 0u);
                turn(EN_K1, 1);
                go_title("SYN NOISE 2"); frame();
                turn(EN_K2, 1);
                turn(EN_K3, 2);
                frame();
                (void)dln_card(1, &lb, val, &u, &r, &ic);
                bad += check("SYN NOISE 2: FILTER steps the resonance (BP 1 -> BP 2), CLAP 4 -> 6 strikes", ui.lane == DV_CLAP &&
                             ds_get(tr, 2, DS_NRES) == r0 + 1u && ds_get(tr, 2, DS_NFLT) == 1u && str_eq(val, "BP 2") &&
                             ds_get(tr, 2, DS_NCLP) == c0 + 2u);
                ui.lane = DV_SNARE;                      /* (as the grid's lane, a sound load of the VOICE) */
                ok = !dln_ok() && !page_visible(ui.page);
                frame();
                bad += check("SYN NOISE 2: the lane picked elsewhere plays no set: its knobs do nothing, the page gives way to "
                             "DESIGN", ok && str_eq(cur_page()->title, "DESIGN"));
                ui.lane = DV_CLAP;
                apply_preset_to(TSEL, 4);
                bad += check("LAYER KIT: a factory kit with its lanes designed loads them (SYN 1..5 on 6 lanes, the sets)",
                             str_eq(ENG_DRUM.presets[4].name, "LAYER KIT") && dl_on(tr) && ds_on(tr) &&
                             dl_syn_of(tr, DV_KICK) == 0 && dl_syn_of(tr, DV_HATO) == 3 && dl_syn_of(tr, DV_TOM) < 0 &&
                             ds_get(tr, 1, DS_TPIT) == DRUM_DESIGN[0].set[1][DS_TPIT] && preset_cat(ENGI_DRUM, 5) == CAT_DRUM);
            }
            apply_preset_to(TSEL, 0);
            bad += check("DESIGN: a factory kit load puts every lane back (no lanes in the patch)", !dl_on(tr));
        }
        go_title("LANES 2"); frame();
        ok = TSEL->p[P_LN5] == 127;
        turn(EN_K2, -3);
        ok &= TSEL->p[P_LN5] == 124 && TSEL->p[P_LN4] == 127 && TSEL->p[P_LN6] == 127;
        bad += check("LANES 2: KNOB 2 turns TOM's level (100 % by default), the other lanes stay", ok);
        apply_preset_to(TSEL, 0);
        bad += check("a factory preset load: every lane 100 % again", TSEL->p[P_LN5] == 127);
        ui.fam_last[FAM_EDIT] = ui.page;
        set_engine_of(TSEL, 0); frame();
        bad += check("LANES 2 of a track no longer DRUM falls back to EDIT 1", str_eq(cur_page()->title, "EDIT 1"));
    }
    go_title("OP2 ENV"); ui.fam_last[FAM_EDIT] = ui.page;
    set_engine_of(TSEL, 0); frame();
    bad += check("an OP page of a track no longer DIGITAL falls back to EDIT 1", str_eq(cur_page()->title, "EDIT 1"));
    go_title("EDIT 2"); ui.fam_last[FAM_EDIT] = ui.page;
    go_home(); press(B_EDIT);
    bad += check("EDIT remembers its last page (as every family)", str_eq(cur_page()->title, "EDIT 2"));
    return bad;
}

/* a missing sample (an empty user slot; a set this build lacks): it plays a sine (eng_sample.c smp_sine) and the UI
 * says NO SAMPLE, led by the no-file icon (ui.c MSG_NO_SAMPLE), once per track and source (ui_input.c
 * sample_notice): when it is chosen, not on its notes; again for another source; after a project load (behind
 * LOADED); the icon and the words fit the header */
static int test_sample_alert(void)
{
    const char *nf = MSG_NO_SAMPLE;
    int bad = 0, ok, seen;
    uint32_t i;
    char b[48];
    ui_power_on();
    frames(100);
    bad += check("missing sample: the power-on sounds have their samples (no alert)", !snd_missing(&trk[0], &i) &&
                 !snd_missing(&trk[1], &i) && !snd_missing(&trk[2], &i) && !snd_missing(&trk[3], &i) &&
                 !str_eq(ui.msg, nf));
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);      /* SET USR2: empty */
    frame();
    ok = msg_is(nf);
    frames(1500);
    trk_note_on(TSEL, 60, 100);
    frames(100);
    trk_note_off(TSEL, 60);
    frames(100);
    ok &= !ui.msg_t;
    bad += check("SAMPLE on an empty USR2: NO SAMPLE once (not again on a note)", ok);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);      /* USR3: said for it */
    frame();
    ok = msg_is(nf);
    TSEL->p[P_E0] = 0;                              /* PIANO: nothing; then USR3 again: said again */
    frames(1500);
    ok &= !ui.msg_t;
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);
    frame();
    ok &= msg_is(nf);
    bad += check("SAMPLE: another empty slot is said; back to it after a sample: said again", ok);
    frames(1500);
    set_engine_of(TSEL, 8u);                         /* GRAIN, SRC USR2 */
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frame();
    bad += check("GRAIN on an empty USR2: said", msg_is(nf));
    frames(1500);
#if FELUCCA_SLICE
    set_engine_of(TSEL, 13u);                        /* SLICE, SRC USR3 */
    TSEL->p[P_E0] = 3;
    frame();
    ok = msg_is(nf);
    frames(1500);
    TSEL->p[P_E0] = 4;                               /* PIANO: there (the CC0 build) */
    frame();
    ok &= !ui.msg_t && !snd_missing(TSEL, &i) == (SLC_PIANO.len != 0u);
    bad += check("SLICE on an empty USR3: said; PIANO: not (it has its sample)", ok);
    frames(1500);
#endif
    /* a project with an empty slot: said after LOADED */
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frames(1500);
    project_save(2);
    frames(1500);
    project_load(2);
    frame();
    ok = msg_is("LOADED") && str_eq(ui.msg2, nf);
    for (seen = 0, i = 0; i < 200u && !seen; i++) {
        frame();
        seen = msg_is(nf);
    }
    bad += check("a project load with an empty slot: LOADED, then NO SAMPLE", ok && seen);
    bad += check("missing sample: the icon and NO SAMPLE fit the header",
                 nf[0] == MSG_NOFILE[0] && !text_fit(b, sizeof b, nf + 1, &AF_S, 236 - 106 - 16 - KH_GAP));
    ui_power_on();
    return bad;
}

#if FELUCCA_SLICE
/* SLICES (EDIT family, a SLICE track: ui_slice.c; the MAN slices of eng_slice.c, ported from hugelton/Felucca#27 by
 * andreahaku): in the EDIT cycle after EDIT 2 on SLICE only; BREAK shows its slices, edits need a user slot; the first
 * edit takes the slices shown (8 equal) as MAN and sets DIV MAN; KNOB 1 the marker (then END), KNOB 2 moves it,
 * KNOB 3 / 4 pick SPLIT / JOIN and OCT+ does it (it stays picked), OCT- drops the pick, then goes HOME; a key picks
 * its slice and the keys of the selected slice are lit; the slot is marked for the store; another engine: EDIT 1 */
static void host_slot_make(uint32_t k)                /* USR k + 1: 2 s at 22.05 kHz, a noise burst every 0.25 s */
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)((uint8_t *)host_slots + k * SMP_USER_SIZE);
    uint8_t *d = (uint8_t *)h + SMP_USER_DATA;
    uint32_t n = 44100u, i, r = 12345u;
    memset(h, 0, SMP_USER_SIZE);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 1;
    memcpy(h->name, "TEST", 4);
    h->data_len = n / 2u;
    for (i = 0; i < n / 2u; i++) {                   /* (IMA codes: a burst of random ones, then +-1/8 steps) */
        r = r * 1103515245u + 12345u;
        d[i] = i % 2756u < 400u ? (uint8_t)(r >> 16) : 0x80u;
    }
    h->zone[0].n = n;
    h->zone[0].le = n - 1u;
    h->zone[0].rate = 32768u;                        /* 22050 / 44100, Q16 */
    h->zone[0].root16 = 60 * 16;
    h->zone[0].hi = 127;
}
static int test_slices(void)
{
    static const char *const CYC_S[] = {"EDIT 1", "EDIT 2", "SLICES", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
    int bad = 0, ok;
    uint32_t n, j, p0, k, note;
    ui_power_on();
    set_engine_of(TSEL, 13u);                        /* SLICE CHOP: BREAK, 16 */
    bad += check("SLICES: SLICE's EDIT cycle EDIT 1 EDIT 2 SLICES VOICE VOICE 2 VOICE 3 EDIT 1", engine_cycle(CYC_S, NELEM(CYC_S)));
    go_page(GR_SLICES); frame();
    n = slice_count();
    p0 = TSEL->p[P_E1];
    turn(EN_K1, 2); turn(EN_K2, 3);
    ok = n == 16u && slice_sel() == 2u && TSEL->p[P_E1] == p0 && msg_is("SRC USR1-3 TO EDIT") && !slice_act_ready(2);
    bad += check("SLICES on BREAK: its 16 slices shown, KNOB 1 picks; edits need USR1-3 (DIV unchanged)", ok);
    TSEL->p[P_E0] = 2;                               /* SRC USR2, empty: no slices (a sine plays): NO SAMPLE */
    frame();
    ok = slice_count() == 0u && (msg_is(MSG_NO_SAMPLE) || str_eq(ui.msg2, MSG_NO_SAMPLE));
    turn(EN_K2, 1);
    ok &= TSEL->p[P_E1] == p0 && msg_is("USR2 EMPTY");
    TSEL->p[P_E0] = 0;
    bad += check("SLICES on an empty USR2: NO SAMPLE, no slices; edits: \"USR2 EMPTY\"", ok);
    TSEL->p[P_E0] = 4;                               /* SRC PIANO: its 16 slices shown, no edits */
    frame();
    turn(EN_K2, 1);
    ok = slice_count() == 16u && TSEL->p[P_E1] == p0 && msg_is("SRC USR1-3 TO EDIT");
    TSEL->p[P_E0] = 0;
    frame();
    bad += check("SLICES on PIANO: its 16 slices shown; edits need USR1-3 (DIV unchanged)", ok);

    host_slot_make(0);
    smp_user_scan(0);
    TSEL->p[P_E0] = 1;                               /* SRC USR1, DIV 8 */
    TSEL->p[P_E1] = 1;
    frame();
    n = slice_count();
    p0 = slice_mark(2);
    turn(EN_K2, 5);
    ok = n == 8u && TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("DIV MAN") && slice_count() == 8u && slice_sel() == 2u &&
         slice_mark(2) > p0 && slc_man_of(slc_get(1)) && (slc_man_save & 1u);
    bad += check("SLICES on USR1: the first KNOB 2 turn takes the 8 slices as MAN (DIV MAN), moves slice 3's start", ok);
    turn(EN_K3, 1);
    ok = ui.act == 3u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u && ui.act == 3u;
    press(B_OCTUP);
    ok &= slice_count() == 10u && slice_sel() == 4u;
    bad += check("SLICES: KNOB 3 picks SPLIT, OCT+ splits the slice (again: it stays picked)", ok);
    turn(EN_K4, 1);
    ok = ui.act == 4u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u;
    turn(EN_K1, -20);
    ok &= slice_sel() == 0u && !act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && msg_is("FIRST SLICE");
    bad += check("SLICES: KNOB 4 picks JOIN, OCT+ joins to the slice before; not the first slice", ok);
    turn(EN_K1, 20);
    p0 = slice_mark(slice_count());
    turn(EN_K2, -4);
    ok = slice_sel() == slice_count() && slice_mark(slice_count()) < p0 && !act_ready();
    bad += check("SLICES: after the last slice the END marker: KNOB 2 trims the tail", ok);
    {   /* DIV set back to a grid: the first touch shows the slot's MAN slices (DIV MAN), it does not replace them */
        slc_man_t keep = *slc_man_of(slc_get(1));
        TSEL->p[P_E1] = 2;                           /* DIV 16 */
        frame();
        turn(EN_K1, -1); turn(EN_K2, 1);
        ok = TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("DIV MAN") && slc_man_of(slc_get(1))->n == keep.n &&
             !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        turn(EN_K1, -20); turn(EN_K2, 1);            /* then KNOB 2 edits that table */
        ok &= slc_man_of(slc_get(1))->n == keep.n && memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: DIV 16 over a slot's MAN slices: KNOB 2 goes back to them (DIV MAN), keeps them", ok);
    }

    note = 5u;                                       /* (the 6th key: SLICE maps every key) */
    key_down(note); frame(); key_up(note); frame();
    j = slc_note_slice(TSEL->p, kb_map(TSEL, note), slice_count());
    ok = slice_sel() == j && ((slice_leds() >> note) & 1u);
    for (k = 0; k < 27u; k++)
        ok &= ((slice_leds() >> k) & 1u) == (slc_note_slice(TSEL->p, kb_map(TSEL, k), slice_count()) == j);
    bad += check("SLICES: a key picks the slice it plays; the keys of the selected slice are lit", ok);
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok = ui.act == 0u && !ui.home;
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= !ui.home;
    bad += check("SLICES: OCT- drops the pick, then nothing (Dah: no BACK)", ok);
    go_page(GR_SLICES); frame();
    set_engine_of(TSEL, 0); frame();
    bad += check("SLICES: on another engine the page is not there (EDIT 1)", str_eq(cur_page()->title, "EDIT 1"));
    {   /* the engine changed (the editor's SET between two ui_input passes) while the page is still the current one:
         * its knobs and actions do nothing to the track or the slot */
        track_t before;
        slc_man_t keep;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = SLC_DIV_MAN;
        go_page(GR_SLICES); frame();
        keep = *slc_man_of(slc_get(1));
        set_engine(3u);                              /* LOFI, as ed_service's SET G_ENGSEL */
        TSEL->p[P_E0] = 1;
        before = *TSEL;
        slc_man_save = 0;
        host_enc[panel.enc[EN_K2]] += 2 * panel.dir[EN_K2];
        host_ticks += 16000u; fm1_ms += 16u;
        ui_input();                                  /* (no ui_draw yet) */
        ok = cur_page()->graph == GR_SLICES && !slice_page_ok() && !act_cols();
        ui.act = 3u;
        act_do();
        ui.act = 0;
        ok &= !memcmp(&before, TSEL, sizeof before) && !slc_man_save && slc_man_of(slc_get(1))->n == keep.n &&
              !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: left by an engine change not yet drawn: KNOB 2 and OCT+ change nothing", ok);
        frame();
    }
    {   /* another sample of the same length uploaded into the slot: the waveform is decoded again */
        int8_t lo0[SP_COLS], hi0[SP_COLS];
        uint8_t *d = (uint8_t *)host_slots + SMP_USER_DATA;
        smp_user_hdr_t *h = (smp_user_hdr_t *)host_slots;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = 2;
        go_page(GR_SLICES); ui.force = 1; frame();
        memcpy(lo0, sp.lo, sizeof lo0);
        memcpy(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < h->data_len; k++)
            d[k] = 0x80u;                            /* silence, the same length */
        h->crc ^= 1u;
        smp_user_scan(0);                            /* (the upload's END) */
        ui.force = 1; frame();
        ok = memcmp(lo0, sp.lo, sizeof lo0) || memcmp(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < SP_COLS; k++)
            ok &= sp.lo[k] >= -1 && sp.hi[k] <= 1;   /* (near silence) */
        bad += check("SLICES: a re-upload of the same length redraws the waveform", ok);
        set_engine_of(TSEL, 0); frame();
    }
    memset(host_slots, 0, sizeof host_slots);        /* (the other tests: empty slots) */
    smp_user_scan(0);
    slc_man_save = 0;
    return bad;
}
#endif

/* ------------------------------------------------ the GLO SCL EDIT layers --- */
static uint32_t black(uint32_t b) { return key_at(1, b); }
static void lay_combo(uint32_t btn, uint32_t k) { btn_down(btn); key_down(k); frame(); }
static void oct_back(void) { btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame(); }

static int test_quick_layers(void)
{
    static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[3] = {LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const char *const HINT[3] = {"HOLD [GLO] QUICK", "HOLD [SCL] QUICK", "HOLD [EDIT] QUICK"};
    int bad = 0, ok, flash, hint;
    uint32_t i, k, a, b2, mo;
    track_t before;
    /* tap / peek / combo, the hint once */
    ok = 1; hint = 1;
    for (i = 0; i < 3u; i++) {
        ui_power_on();
        btn_down(LB[i]);
        for (flash = 0, k = 0; k < 20u; k++) { frame(); flash |= ui.layer; }      /* 0.32 s */
        btn_up(LB[i]); frame();
        ok &= !flash && !ui.home && (LB[i] == B_GLO ? str_eq(cur_page()->title, "SONG") :
                                     LB[i] == B_SCL ? cur_page()->fam == FAM_SCL : str_eq(cur_page()->title, "EDIT 1"));
        hint &= msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); frames(368);
        ok &= !ui.layer;
        frames(64);
        ok &= ui.layer == LL[i] && ((layer_seen >> LL[i]) & 1u);
        btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
        ui.msg_t = 0;
        press(LB[i]);
        hint &= !msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); key_down(white(9)); frame();
        ok &= ui.layer == LL[i] && !gates();
        key_up(white(9)); btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
    }
    bad += check("GLO SCL EDIT: a tap opens the page, held past HOLD the map (no page), a key: the map at once", ok);
    bad += check("  after a tap \"HOLD [BTN] QUICK\" until the layer has been opened once", hint);
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        layer_seen = 0x1Cu;
        settings_export(&p); layer_seen = 0;
        bad += check("  the seen bits are kept with the settings (a spare favorites byte)",
                     settings_import(&p, sizeof p) && layer_seen == 0x1Cu);
    }

    /* GLO: mutes latch (lit = sounding), SOLO while held, UNMUTE ALL, TAP, levels, OCT- */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    lay_combo(B_GLO, black(1));
    ok = trk[1].p[P_MUTE] == 1 && ui.layer == LAYER_GLO && !gates() && mo_w == mo;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> black(0)) & 1u && !(((a | b2) >> black(1)) & 1u);           /* T1 sounding lit, T2 muted dark */
    ok &= ((a ^ b2) >> white(0)) & 1u && ((a ^ b2) >> white(7)) & 1u && ((a ^ b2) >> white(5)) & 1u &&   /* (D4: LEARN) */
          ((a ^ b2) >> white(6)) & 1u && !(((a | b2) >> white(8)) & 1u);   /* (E4: FILL, 1.5.5; G4 none) */
    key_up(black(1)); btn_up(B_GLO); frame();
    bad += check("GLO + black key 2: T2 MUTE latched (SET), silent, no MIDI; LEDs: sounding lit, muted dark", ok &&
                 trk[1].p[P_MUTE] == 1 && !ui.layer);
    lay_combo(B_GLO, black(1)); key_up(black(1)); frame();
    key_down(white(4)); key_up(white(4)); frame();
    lay_combo(B_GLO, black(2)); key_up(black(2)); frame();
    ok = trk[1].p[P_MUTE] == 0 && trk[2].p[P_MUTE] == 1;
    key_down(white(4)); frame(); key_up(white(4)); frame();
    btn_up(B_GLO); frame();
    bad += check("  again: unmuted; C4: UNMUTE ALL", ok && !trk[2].p[P_MUTE] && !trk[1].p[P_MUTE]);
    lay_combo(B_GLO, white(2));
    perf_begin(CTL);
    ok = perf_solo == 4u && ((perf_act >> PF_M1) & 15u) == 0xBu;   /* T3 solo: T1 T2 T4 muted */
    btn_up(B_GLO); frame();
    perf_begin(CTL);
    ok &= perf_solo == 4u && ui.layer == LAYER_GLO;                 /* GLO let go first: the solo stays with its key */
    key_up(white(2)); frame();
    perf_begin(CTL);
    bad += check("GLO + A3 held: SOLO T3 (the others muted), lasts while the key is held, not P_MUTE",
                 ok && !perf_solo && !((perf_act >> PF_M1) & 15u) && !trk[0].p[P_MUTE] && !ui.layer);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    song.g[G_CLOCK] = 1;
    lay_combo(B_GLO, white(7)); key_up(white(7)); frame();
    ok &= msg_is("TAP: CLK IS EXT") && song.g[G_BPM] == 120;
    btn_up(B_GLO); frame(); song.g[G_CLOCK] = 0;
    bad += check("GLO + F4 x3 at 0.5 s: TAP 120 BPM; with CLK EXT: dimmed, it says why", ok);
    {
        int16_t l2 = trk[2].p[P_LEVEL], atk = TSEL->p[P_ATK];
        go_title("ENV");
        btn_down(B_GLO); frame();
        turn(EN_K3, -10);
        ok = trk[2].p[P_LEVEL] == l2 - 10 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_GLO;
        key_down(black(0)); frame(); key_up(black(0)); frame();
        ok &= trk[0].p[P_MUTE] == 1 && song.octave == 0;
        oct_back();
        ok &= song.octave == 0 && ui.layer == LAYER_GLO;
        btn_up(B_GLO); frame();
        bad += check("GLO KNOB 3: T3 LEVEL from any page; OCT-: no octave", ok &&
                     str_eq(cur_page()->title, "ENV"));
    }
    song.playing = 1; transport_req = 0;
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok = transport_req == 3u;
    events_block(CTL);
    ok &= song.playing && transport_req == 0u;
    btn_up(B_GLO); frame();
    stop_transport();
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_GLO); frame(); transport_req = 0;
    bad += check("GLO + PLAY: RESTART playing (from the top, not stopped); stopped: PLAY", ok);
    usb.config = 0;

    /* SCL: a key is ROOT, KNOB 2 the scale, the LEDs, OCT- */
    ui_power_on();
    TSEL->p[P_ROOT] = 0; TSEL->p[P_SCALE] = 1;                     /* C MAJ */
    lay_combo(B_SCL, black(4));                                    /* D#4 */
    ok = TSEL->p[P_ROOT] == 3 && ui.layer == LAYER_SCL && !gates();
    key_up(black(4)); frame();
    key_down(white(1)); key_up(white(1)); frame();                  /* G3 */
    ok &= TSEL->p[P_ROOT] == 7;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 2;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> white(1)) & 1u && ((a & b2) >> white(8)) & 1u;   /* G3 G4: the root, lit */
    ok &= ((a ^ b2) >> white(2)) & 1u && ((a ^ b2) >> black(4)) & 1u;   /* A, A# (G minor): blink */
    ok &= !(((a | b2) >> black(0)) & 1u);                               /* F#: not in G minor, dark */
    btn_up(B_SCL); frame();
    bad += check("SCL + D#4, then G3: ROOT D#, G (latched); KNOB 2: SCL; LEDs: root lit, the scale breathes", ok &&
                 TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2 && ui.home);
    btn_down(B_SCL); frame(); key_down(white(4)); key_up(white(4)); frame(); turn(EN_K2, 3);
    ok = TSEL->p[P_ROOT] == 0 && TSEL->p[P_SCALE] == 5;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("  OCT- in SCL: nothing (ROOT and SCL stay), no octave", ok && TSEL->p[P_ROOT] == 0 && TSEL->p[P_SCALE] == 5 &&
                 song.octave == 0);

    /* EDIT: the engines from F3 (NENGINES of them), sound loads, INIT with the dialog */
    ui_power_on();
    set_engine_of(TSEL, 0); go_home(); frame();
    my_steps(TSEL); TSEL->p[P_SLCR] = SL_STUT; song.playing = 1;
    before = *TSEL;
    sync_reload = 0;
    lay_combo(B_EDIT, white(eng_rank(2)));             /* (the keys: the engines one can pick, engines.c eng_vis) */
    ok = TSEL->eng_req == 2u && TSEL->preset == 0u && ui.layer == LAYER_EDIT && sync_reload && !gates();
    key_up(white(eng_rank(2))); frame();
    key_down(white(1)); key_up(white(1)); frame();
    ok &= TSEL->eng_req == ENGI_FM6;                    /* (G3: FM6, second in ENGINE_ORDER) */
    key_down(white(NENG_SHOWN - 1u)); key_up(white(NENG_SHOWN - 1u)); frame();
    ok &= TSEL->eng_req == ENGI_DRUM;                   /* (the last key: DRUM) */
    ok &= !memcmp(TSEL->step, before.step, sizeof before.step) && TSEL->p[P_SLEN] == before.p[P_SLEN] &&
          TSEL->p[P_SLCR] == SL_STUT;
    btn_up(B_EDIT); frame();
    bad += check("EDIT + white key n: the n-th engine shown (FM6 2nd, DRUM last), while playing; steps, LEN, SLICER stay", ok);
    a = 0;
    ok = 1;
    btn_down(B_EDIT); frames(480);
    for (i = 0; i < LY_INIT; i++) {
        key_down(white(i)); key_up(white(i)); frame();
        a += i < NENG_SHOWN ? TSEL->eng_req == eng_vis(i) : 0u;
        ok &= eng_ok(TSEL->eng_req);
    }
    btn_up(B_EDIT); frame();
    bad += check("  every engine one can pick has its white key from F3 (NENG_SHOWN, not a fixed count; never DIGITAL)",
                 ok && a == NENG_SHOWN);
    set_engine_of(TSEL, 0);
    lay_combo(B_EDIT, white(eng_rank(3))); key_up(white(eng_rank(3))); frame();
    turn(EN_K2, 1);
    ok = TSEL->eng_req == 3u && TSEL->preset == 1u;
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 2: the engine's next sound", ok && ui.home);
    btn_down(B_EDIT); frame(); turn(EN_K1, 1);
    ok = TSEL->eng_req == eng_step(0, 1);
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 1: the next engine", ok);
    {                                                   /* #124: SAMPLE's alias (1 = PIANO) is not a stop on KNOB 2 */
        uint32_t tot, n, c0, c1, c2, seen = 0, alias = 0;
        set_engine_of(TSEL, ENGI_SAMPLE); go_home(); frame();
        eng_list_pos(&tot);
        btn_down(B_EDIT); frame();
        c0 = eng_list_pos(&tot);
        turn(EN_K2, 1);
        ok = TSEL->preset == 2u && user_of(TSEL) >= UP_SLOTS;
        c1 = eng_list_pos(&tot);
        turn(EN_K2, 1);
        ok &= TSEL->preset == 3u;
        c2 = eng_list_pos(&tot);
        ok &= c0 == 0u && c1 == 1u && c2 == 2u && tot >= SMP_NPRESETS - SMP_NALIAS;
        apply_preset(0);
        for (n = 0; n < tot; n++) {                     /* right: every sound once, back to the first */
            turn(EN_K2, 1);
            seen |= 1u << eng_list_pos(&tot);
            alias |= user_of(TSEL) >= UP_SLOTS && TSEL->preset == 1u;
        }
        ok &= seen == (1u << tot) - 1u && TSEL->preset == 0u && user_of(TSEL) >= UP_SLOTS;
        seen = 0;
        for (n = 0; n < tot; n++) {                     /* left: the same, backwards */
            turn(EN_K2, -1);
            seen |= 1u << eng_list_pos(&tot);
            alias |= user_of(TSEL) >= UP_SLOTS && TSEL->preset == 1u;
        }
        ok &= seen == (1u << tot) - 1u && TSEL->preset == 0u && !alias;
        btn_up(B_EDIT); frame();
        bad += check("  #124 SAMPLE: KNOB 2 right PIANO, FLUTE, SAX (No. 1 2 3), both ways round the list, never the alias", ok);
    }
    song.playing = 0;
    lay_combo(B_EDIT, white(LY_INIT));
    ok = ui.confirm == CF_INIT_SOUND;
    frame();
    ok &= !ui.layer;
    key_up(white(LY_INIT)); btn_up(B_EDIT); frame();
    ok &= ui.home && ui.confirm == CF_INIT_SOUND;
    TSEL->p[P_E0] = (int16_t)(TSEL->p[P_E0] + 5);
    press(B_OCTUP);
    bad += check("EDIT + the key after the engines: INITIALIZE SOUND? dialog (closes the layer, no tap); OCT+ inits", ok && !ui.confirm &&
                 msg_is("SOUND INIT") && TSEL->p[P_E0] == ENGINES[eng_step(0, 1)]->presets[0].e[0]);

    /* no layer in the menu or a dialog: GLO + a key plays */
    ui_power_on(); hold(B_HOME);
    btn_down(B_GLO); frame(); key_down(white(3)); frame();
    ok = !ui.layer && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_GLO); frame();
    bad += check("in the menu: no GLO layer, GLO + a key plays it; GLO let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_INIT_SOUND;
    btn_down(B_SCL); frames(512);
    ok = !ui.layer;
    btn_up(B_SCL); frame();
    bad += check("in a dialog: SCL held shows no map and taps nothing", ok && ui.confirm == CF_INIT_SOUND);
    /* one layer at a time: a second layer button is ignored, its keys stay notes after */
    ui_power_on();
    btn_down(B_GLO); frame(); btn_down(B_EDIT); frame(); key_down(white(1)); frame();
    ok = ui.layer == LAYER_GLO && TSEL->eng_req == trk[0].eng_req && perf_solo == 2u;
    key_up(white(1)); btn_up(B_GLO); frame();
    key_down(white(1)); frame();
    ok &= gates() > 0 && !ui.layer;
    key_up(white(1)); btn_up(B_EDIT); frame();
    bad += check("GLO then EDIT: GLO's layer; EDIT ignored (no layer, no page, its keys notes)", ok && ui.home);
    return bad;
}

/* bug fixes (UI): one regression each */
static int test_bughunt_ui(void)
{
    int bad = 0, ok;
    /* 1: REC in NAME, the menu or a dialog: nothing (PLAY cannot stop the transport there) */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 3;
    press(B_OCTUP);                                     /* NAME opens */
    press(B_REC);
    bad += check("REC in NAME does nothing (no arm, no transport start); NAME stays",
                 name_on() && !song.rec && !transport_req && !song.playing);
    ui_power_on(); hold(B_HOME); press(B_REC); press(B_PLAY);
    bad += check("REC in the menu does nothing (then PLAY: nothing either)", ui.menu && !song.rec && !transport_req);
    ui_power_on();
    up_ui(2, 5);
    go_page(GR_USER); ui.uslot = 5; press(B_OCTUP);
    press(B_REC);
    ok = ui.confirm == CF_OVR_USER && !song.rec && !transport_req;
    press(B_OCTUP);
    bad += check("REC in the OVERWRITE? dialog does nothing; OCT+ then opens NAME", ok && name_on());
    {   /* the ARP button flashes on the beat while an ARP plays (the bar's first beat longer); dark otherwise */
        uint32_t q, b, r[5];
        ui_power_on(); frame();
        b = beat_samples();
        r[0] = arp_led();                               /* no ARP playing */
        trk[2].p[P_AMODE] = 1; trk[2].nheld = 1; trk[2].held[0] = 60;
        beat_n = 1; beat_pos = 0; r[1] = arp_led();
        beat_pos = b / 3u; r[2] = arp_led();
        beat_n = 0; r[3] = arp_led();
        beat_pos = b * 3u / 4u; r[4] = arp_led();
        bad += check("ARP LED: none without an ARP; flashes on the beat, the bar's first beat longer",
                     r[0] == 2u && r[1] == 1u && r[2] == 0u && r[3] == 1u && r[4] == 0u);
        for (q = 0; q < 4u * b / 128u + 4u; q++) events_block(128);
        bad += check("  the beat counter runs (a bar of blocks wraps the beat of the bar)", beat_n < 4u && beat_pos < b);
        trk[2].p[P_AMODE] = 0; trk[2].nheld = 0;
    }
    /* 3: USER ERASE asks first (ERASE U02?): OCT- keeps the preset, OCT+ erases it */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 1;
    up_ui(2, 1);
    turn(EN_K1 + 2, 1);                                 /* KNOB 3: ERASE picked */
    press(B_OCTUP);
    ok = ui.confirm == CF_ERASE_USER && ui.confirm_trk == 1u && up_used(1);
    {
        char a[24], b[24];
        confirm_text(a, b);
        ok &= str_eq(a, "ERASE U02?") && b[0];
    }
    press(B_OCTDN);
    ok &= !ui.confirm && up_used(1);
    turn(EN_K1 + 2, 1); press(B_OCTUP); press(B_OCTUP);
    bad += check("USER ERASE: the ERASE U02? dialog; OCT- keeps the preset, OCT+ erases it",
                 ok && !ui.confirm && !up_used(1));
    turn(EN_K1 + 2, 1); press(B_OCTUP);
    bad += check("  an empty slot: no dialog, EMPTY SLOT", !ui.confirm && msg_is("EMPTY SLOT"));
    {   /* 4: AUTOMATION (CHANCE's since 1.2) is not STEP: no grid, no key entry, no EDIT clear (its knobs only) */
        uint32_t before;
        step_t s0;
        ui_power_on();
        track_select(3); frame();                       /* T4 DRUM */
        go_page(GR_EVENTS); frame();
        before = lane_steps(TSEL, ui.lane);
        tap_key(white(2));
        bad += check("AUTOMATION on a DRUM track: no grid (title, LEDs), a white key toggles no hit",
                     drum_track(TSEL) && !grid_on() && !keys_mode() && lane_steps(TSEL, ui.lane) == before);
        ui_power_on(); go_page(GR_EVENTS); frame();
        my_steps(TSEL);
        s0 = TSEL->step[0];
        tap_key(white(5)); frame();
        ok = !memcmp(&s0, &TSEL->step[0], sizeof s0) && ui.cursor == 0u;
        press(B_EDIT);
        ok &= !memcmp(&s0, &TSEL->step[0], sizeof s0) && !msg_is("STEP CLEARED");
        bad += check("AUTOMATION on a melodic track: a key writes no step, EDIT clears none", ok);
        ui_power_on(); go_page(GR_ROLL); frame(); song.rec = 1;   /* (#133: armed, stopped) */
        tap_key(white(5)); frame();
        bad += check("  STEP still: armed, a key writes the cursor step", step_on(&TSEL->step[0]) || TSEL->step[0].n);
        song.rec = 0;
    }
    {   /* 5: ALGORITHM does nothing while a layer's button is held (as PRESETS); after it, the track again */
        int16_t r0;
        ui_power_on();
        r0 = trk[0].p[P_ROOT];
        btn_down(B_SCL); frames(500);
        turn(EN_ALGO, 1);
        ok = ui.layer == LAYER_SCL && song.sel == 0u;
        key_down(white(1)); frame(); key_up(white(1)); frame();   /* ROOT on T1 */
        ok &= trk[0].p[P_ROOT] != r0;
        btn_up(B_SCL); frame();
        btn_down(B_EDIT); turn(EN_ALGO, 1);             /* armed, not open yet: ignored too */
        ok &= song.sel == 0u;
        btn_up(B_EDIT); frame(); go_home(); frame();
        turn(EN_ALGO, 1);
        bad += check("ALGORITHM ignored with a layer's button held (OCT- puts back all); then T2",
                     ok && song.sel == 1u);
    }
    /* 7: REC works in the layers, as PLAY: it arms the track (PLAY starts); the layer stays, no page */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            btn_down(LB[i]); frame();
            press(B_REC);
            ok &= song.rec == 1u && transport_req == 1u && ui.layer != 0u;
            btn_up(LB[i]); frame();
            ok &= ui.home;
        }
    }
    bad += check("REC in every layer: arms the track and starts PLAY; the layer stays, no page", ok);
    /* 8: the OCT- LED lit in the SET layers (OCT- the layer's), OCT+ dark; FX (HOLD): the octave as usual */
    ok = 1;
    {
        static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            ui_power_on();
            song.octave = 1;                            /* (outside: OCT+ lit) */
            btn_down(LB[i]); frames(500);
            ok &= layer_set_open() && oct_leds_seen(1) == 1u && oct_leds_seen(0) == 1u;
            btn_up(LB[i]); frame();
            ok &= oct_leds() == 2u;
        }
        ui_power_on();
        btn_down(B_FX); frames(500);
        ok &= ui.layer == LAYER_FX && oct_leds() == 0u;
        btn_up(B_FX); frame();
    }
    bad += check("SET layers: the OCT- LED lit, OCT+ dark; after: the octave; FX: the octave", ok);
    /* 9: no "HOLD [EDIT] QUICK" over NAME (EDIT tapped on USER / PROJECT renames) */
    ui_power_on();
    layer_seen = 0;
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    ui.msg_t = 0;
    press(B_EDIT);
    ok = name_on() && !msg_is("HOLD [EDIT] QUICK");
    ui_power_on();
    layer_seen = 0;
    press(B_EDIT);
    bad += check("EDIT tap = RENAME on USER: no layer hint over NAME (elsewhere the hint as before)",
                 ok && msg_is("HOLD [EDIT] QUICK"));
    return bad;
}

/* the chord keys on the device (chord.c): SCL tapped again is the CHORD page, KNOB 1 CHRD, KNOB 2 VOIC, the graph
 * names the last chord (MONO gray); a sound load keeps them; the SCL layer's KNOB 3 / 4 are CHRD / VOIC, OCT-
 * puts them back */
static int test_chord_page(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    go_home(); frame();
    press(B_SCL); frames(400);
    ok = str_eq(cur_page()->title, "CHORD") && cur_page()->graph == GR_CHORD && cur_page()->fam == FAM_SCL;
    bad += check("SCL tapped: the CHORD page first (CHRD VOIC, the chord graph)", ok && cur_page()->id[0] == P_CHRD &&
                 cur_page()->id[1] == P_VOIC);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  CHRD OFF: the page says what to do, MONO gray", screen_gray());
    turn(EN_K1, 1); frame();
    turn(EN_K2, 1); frame();
    bad += check("  KNOB 1: CHRD DIA3, KNOB 2: VOIC OPEN", t->p[P_CHRD] == CH_DIA3 && t->p[P_VOIC] == VC_OPEN);
    key_down(7); frame();                                          /* C4 in C (SCALE CHR: the major of C) */
    ok = gates() == 3u && chord_last[song.sel].root == 60;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok &= screen_gray();
    key_up(7); frame();
    bad += check("  a key plays the chord, the graph shows it (C), MONO gray", ok && !gates());
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;
    apply_preset_to(t, 1);
    bad += check("  a sound load keeps CHRD and VOIC (SCL settings)", t->p[P_CHRD] == CH_MIN7 && t->p[P_VOIC] == VC_BASS);
    set_engine_of(t, ENGI_DRUM); t->engine = t->eng_req; frame();
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  a kit (DRUM): the page says so, MONO gray", chord_kit(t) && screen_gray());
    ui_power_on();
    t = TSEL;
    t->p[P_CHRD] = CH_OFF; t->p[P_VOIC] = VC_CLOSE;
    go_home(); frame();
    btn_down(B_SCL); frames(800);
    ok = ui.layer == LAYER_SCL;
    turn(EN_K3, 2); frame();
    turn(EN_K4, 1); frame();
    ok &= t->p[P_CHRD] == CH_DIA7 && t->p[P_VOIC] == VC_OPEN && t->p[P_QUANT] == 0 && t->p[P_TRANS] == 0;
    turn(EN_K1, 2); frame();
    ok &= t->p[P_ROOT] == 2;
    btn_up(B_SCL); frame();
    bad += check("SCL layer: KNOB 1 ROOT, 3 CHRD, 4 VOIC (QNT TRN untouched)", ok && ui.home);
    btn_down(B_SCL); frames(800);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = screen_gray();
    btn_up(B_SCL); frame();
    bad += check("  its map and cards MONO gray", ok);
    return bad;
}

/* the STEP page's piano roll (ui_graph.c graph_roll), read back from the screen: per column the rows whose centre
 * pixel (the bar's middle, x + 6) is a bar colour, against the notes the step sounds (its own; a TIE: the note
 * before's; a REST: none) */
static int pr_is_bar(uint32_t x, uint32_t y)
{
    uint16_t c = swap16(host_screen[y * 240u + x]);
    return c == T_THEME || c == T_ACCENT || c == T_TEXT;
}
static int pr_columns_match(const track_t *t, uint32_t *nbars)
{
    uint32_t i, r, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int ok = 1;
    pr_set_t ps;                                         /* (overlaps: what sounds at each step, ui_graph.c pr_walk) */
    pr_walk(t, base, len, &ps);
    for (i = 0; i < 16u && base + i < len; i++) {
        uint32_t want = 0, got = 0;
        pr_next(&ps, ui_st(t, base + i), base + i);
        for (j = 0; j < ps.n; j++) {
            int32_t n = ps.note[j], rr = (int32_t)proll.lo + PR_ROWS - 1 - n;
            if (n >= 0 && rr >= 0 && rr < PR_ROWS) want |= 1u << rr;
        }
        for (r = 0; r < PR_ROWS; r++)
            if (pr_is_bar((uint32_t)(PR_X0 + (int32_t)i * PR_CW + 6), Y_GRAPH + PR_Y0 + r * PR_RH + 2u)) got |= 1u << r;
        if (got != want) {
            printf("ui:   roll column %u: rows %05x drawn, %05x wanted\n", base + i, got, want);
            ok = 0;
        }
        for (r = 0; r < PR_ROWS; r++) *nbars += (got >> r) & 1u;
    }
    return ok;
}
/* 1.2, Discussion #133: step recording on SEQ > STEP. Not armed, the keys only play (being on SEQ writes nothing);
 * REC there arms step recording without starting the transport; armed and stopped a key (keys held together: a
 * chord) writes the cursor step and letting go moves the cursor on one step, wrapping inside LEN; PLAY records live
 * (at the play head, not the cursor). The DRUM grid keeps its own keys (white keys = steps, REC starts PLAY) */
static int test_step_rec(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t before[NSTEP];
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    memcpy(before, t->step, sizeof before);
    key_down(7); frame();
    ok = kb_chn[7] != 0u;                                             /* it sounds */
    key_up(7); frame();
    ok &= !memcmp(before, t->step, sizeof before) && ui.cursor == 3u && !song.rec && !transport_req;
    bad += check("#133 STEP, not armed, stopped: a key sounds, writes nothing, the cursor stays", ok);
    song.playing = 1;
    key_down(7); frame(); key_up(7); frame();
    song.playing = 0;
    bad += check("#133 STEP, not armed, playing: nothing written either", !memcmp(before, t->step, sizeof before) &&
                 ui.cursor == 3u);
    song.rec = 2u;                                                    /* T2 armed, T1 selected */
    key_down(7); frame(); key_up(7); frame();
    bad += check("#133 another track armed: the selected track's STEP writes nothing",
                 !memcmp(before, t->step, sizeof before) && ui.cursor == 3u);
    song.rec = 0;
    press(B_REC);
    bad += check("#133 REC on STEP arms step recording: stopped, KEYS WRITE STEPS",
                 song.rec == 1u && !transport_req && !song.playing && !seq_counting() && msg_is("KEYS WRITE STEPS"));
    key_down(7); frame();
    ok = t->step[3].n == 1u && t->step[3].note[0] == kb_map(t, 7) && t->step[3].time == ST_NOTE && ui.cursor == 3u;
    key_up(7); frame();
    ok &= ui.cursor == 4u && !memcmp(before, t->step, 3u * sizeof before[0]) &&
          !memcmp(before + 4, t->step + 4, (NSTEP - 4u) * sizeof before[0]);
    bad += check("#133 armed, stopped: a key writes its note into the cursor step (only), letting go moves on one", ok);
    key_down(7); frame(); key_down(11); frame(); key_down(14); frame();
    ok = t->step[4].n == 3u && ui.cursor == 4u;
    key_up(7); frame();
    ok &= ui.cursor == 4u;                                            /* (two keys still down: the step stays open) */
    key_up(11); key_up(14); frame();
    ok &= ui.cursor == 5u && t->step[4].note[0] == kb_map(t, 7) && t->step[4].note[1] == kb_map(t, 11) &&
          t->step[4].note[2] == kb_map(t, 14);
    bad += check("#133 keys held together: one chord step (POLY), the cursor moves on once all are up", ok);
    t->p[P_VOICE] = V_MONO;
    key_down(7); frame(); key_down(9); frame(); key_up(7); key_up(9); frame();
    bad += check("#133 MONO: one note, the last key", t->step[5].n == 1u && t->step[5].note[0] == kb_map(t, 9) &&
                 ui.cursor == 6u);
    t->p[P_SLEN] = 8;
    cursor_set(7);
    frame();
    key_down(9); frame(); key_up(9); frame();
    bad += check("#133 the last step of LEN 8: written, the cursor wraps to step 1", t->step[7].n == 1u && ui.cursor == 0u);
    cursor_set(4);                                                    /* (an empty step, away from the play head) */
    step_clear(&t->step[4]);
    frame();
    press(B_PLAY);
    events_block(CTL);
    ok = song.playing && live_rec_sel() && t->seq_idx < 2u;
    key_down(7); frame(); key_up(7); frame();
    ok &= ui.cursor == 4u && !t->step[4].n && !t->step[4].hit && (t->step[0].n || t->step[1].n);
    bad += check("#133 PLAY with the track armed: live recording at the play head (the cursor and its step stay)", ok);
    stop_transport();
    press(B_REC);                                                     /* disarm: the keys only play again */
    memcpy(before, t->step, sizeof before);
    key_down(7); frame(); key_up(7); frame();
    bad += check("#133 REC again disarms: the keys only play, nothing written",
                 !song.rec && !memcmp(before, t->step, sizeof before) && ui.cursor == 4u);
    /* the DRUM grid as before: white keys are the steps (armed or not), REC from stop starts PLAY */
    ui_power_on();
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    frame();
    memcpy(before, t->step, sizeof before);
    tap_key(key_at(0, 4));
    ok = grid_on() && !memcmp(before, t->step, sizeof before);       /* (Dah: not armed, the key plays, writes nothing) */
    press(B_REC);
    ok &= song.rec == 1u && transport_req == 0u && msg_is("KEYS WRITE STEPS");   /* (armed, stopped: step entry) */
    tap_key(key_at(0, 4));
    tap_key(key_at(0, 6));
    tap_key(key_at(0, 4));
    ok &= t->step[6].hit != 0u && !t->step[4].hit;
    bad += check("#133 the DRUM grid (Dah): not armed the keys play; REC from stop: the white keys toggle steps", ok);
    song.rec = 0;
    return bad;
}

/* 1.2: MENU > CONTROL > STEP PREVIEW (Discussion #169) and CHORD ENTRY (#155), MENU > DISPLAY > SCOPE (#165; its sound
 * path: click_test.c). Every new setting 0 by default (today's behaviour), a bit of its own byte */
static uint32_t gated(const track_t *t, uint32_t note)   /* voices of t sounding note with their gate on */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && (note > 127u || t->v[i].note == note);
    return n;
}
static void blocks(uint32_t n) { while (n--) events_block(CTL); }
static void tap_k(uint32_t k) { key_down(k); frame(); key_up(k); frame(); }
static int test_step_preview_chord_entry(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t before[NSTEP];
    uint32_t i, rec0, mc0;
    ui_power_on();
    ok = !ui_prefs2 && !menu_get(MI_SCOPE) && !menu_get(MI_PREVIEW) && !menu_get(MI_CHADD) && !scope_mix &&
         str_eq(menu_vname(MI_SCOPE, 0), "OUT") && str_eq(menu_vname(MI_SCOPE, 1), "MIX") &&
         str_eq(menu_vname(MI_PREVIEW, 1), "ON") && str_eq(menu_vname(MI_CHADD, 0), "HOLD") &&
         str_eq(menu_vname(MI_CHADD, 1), "ADD") && MI_TAB[MI_SCOPE] == MTAB_DISPLAY && MI_TAB[MI_PREVIEW] == MTAB_CONTROL &&
         MI_TAB[MI_CHADD] == MTAB_CONTROL;
    menu_put(MI_SCOPE, 1); ok &= ui_prefs2 == 1u && scope_mix && !ui_prefs && !(ui_rec_prefs & 0x40u);
    menu_put(MI_PREVIEW, 1); menu_put(MI_CHADD, 1); ok &= ui_prefs2 == 7u;
    menu_put(MI_SCOPE, 0); menu_put(MI_PREVIEW, 0); menu_put(MI_CHADD, 0); ok &= !ui_prefs2 && !scope_mix;
    bad += check("1.2 MENU: SCOPE OUT / MIX, STEP PREVIEW OFF / ON, CHORD ENTRY HOLD / ADD: 0 by default, a bit each", ok);

    /* STEP PREVIEW */
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[3] = (step_t){{60, 64}, 2, ST_NOTE, 0, 100};
    t->step[4] = (step_t){{67}, 1, ST_NOTE, 0, 100};
    t->step[4].probability = 101;                       /* (chance 0: the preview plays it all the same) */
    go_page(GR_ROLL);
    cursor_set(2);
    frame();
    memcpy(before, t->step, sizeof before);
    rec0 = song.rec; mc0 = motion.count;
    turn(EN_K1, 1);
    blocks(2);
    ok = ui.cursor == 3u && !aud_req && !gated(t, 128u);
    bad += check("STEP PREVIEW OFF (the default): moving the cursor sounds nothing", ok);
    menu_put(MI_PREVIEW, 1);
    turn(EN_K1, 1);
    ok = ui.cursor == 4u && aud_req;
    blocks(1);
    ok &= gated(t, 67) == 1u && gated(t, 128u) == 1u && !aud_req;
    turn(EN_K1, -1);
    blocks(1);
    ok &= ui.cursor == 3u && gated(t, 60) && gated(t, 64) && !gated(t, 67);
    blocks(div_samples(2) / CTL + 2u);
    ok &= !gated(t, 128u);
    bad += check("STEP PREVIEW ON: KNOB 1 to a step sounds it once on the track (a chord whole; chance not rolled), a 1/16 at GATE", ok);
    turn(EN_K1, 1);
    blocks(1);
    ok = ui.cursor == 4u && gated(t, 67) == 1u;
    blocks(div_samples(2) / CTL + 2u);
    turn(EN_K1, -3);                                    /* (an empty step: nothing) */
    blocks(1);
    ok &= ui.cursor == 1u && !gated(t, 128u);
    bad += check("STEP PREVIEW: KNOB 1 on, it sounds; an empty step sounds nothing", ok);
    ok = !memcmp(before, t->step, sizeof before) && song.rec == rec0 && motion.count == mc0 && !song.playing;
    bad += check("STEP PREVIEW never writes: the steps, the arming and the motion as they were", ok);
    song.playing = 1;
    turn(EN_K1, 2);
    ok = ui.cursor == 3u && !aud_req;
    aud_req = (uint16_t)(3u << 8 | 1u);                 /* (one asked for just before PLAY: dropped) */
    blocks(1);
    ok &= !aud_req && !gated(t, 60);
    song.playing = 0;
    blocks(div_samples(2) / CTL + 2u);
    song.rec = 1u;                                      /* armed: step recording, stopped: it sounds */
    turn(EN_K1, 1);
    blocks(1);
    ok &= ui.cursor == 4u && gated(t, 67) == 1u && !memcmp(before, t->step, sizeof before);
    blocks(div_samples(2) / CTL + 2u);
    tap_k(7);                                           /* step entry's moving on (HOLD): no preview */
    blocks(1);
    ok &= ui.cursor == 5u && t->step[4].note[0] == kb_map(t, 7) && !aud_req;
    song.rec = 0;
    bad += check("STEP PREVIEW: not while playing (live recording included); armed and stopped it sounds; step entry's move does not", ok);
    /* the DRUM grid: KNOB 1 STEP sounds the step's hits */
    ui_power_on();
    menu_put(MI_PREVIEW, 1);
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    song.rec = (uint8_t)(1u << song.sel);               /* (Dah: armed, the keys are the steps) */
    frame();
    tap_key(key_at(0, 2));                              /* a hit at step 3 (the selected lane) */
    blocks(div_samples(2) / CTL + 2u);
    for (i = 0; i < NVOICE; i++) t->v[i].active = 0;
    cursor_set(1);
    memcpy(before, t->step, sizeof before);
    turn(EN_K1, 1);
    ok = grid_on() && ui.cursor == 2u && aud_req && t->step[2].hit;
    blocks(1);
    for (i = 0, mc0 = 0; i < NVOICE; i++) mc0 += t->v[i].active;
    ok &= mc0 >= 1u && !memcmp(before, t->step, sizeof before);
    bad += check("STEP PREVIEW on the DRUM grid: KNOB 1 to a step sounds its hits, writes nothing", ok);
    menu_put(MI_PREVIEW, 0);

    /* CHORD ENTRY ADD */
    ui_power_on();
    menu_put(MI_CHADD, 1);
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[5] = (step_t){{50}, 1, ST_NOTE, 0, 100};
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    press(B_REC);
    ok = song.rec == 1u && !transport_req && !song.playing && msg_is("KEYS ADD TO STEP");
    bad += check("#155 CHORD ENTRY ADD: REC on STEP arms step recording, KEYS ADD TO STEP", ok);
    tap_k(7);
    ok = t->step[3].n == 1u && t->step[3].note[0] == kb_map(t, 7) && t->step[3].time == ST_NOTE && ui.cursor == 3u;
    tap_k(11); tap_k(14);
    ok &= t->step[3].n == 3u && t->step[3].note[1] == kb_map(t, 11) && t->step[3].note[2] == kb_map(t, 14) && ui.cursor == 3u;
    tap_k(16); tap_k(18);
    ok &= t->step[3].n == 4u && t->step[3].note[3] == kb_map(t, 16) && ui.cursor == 3u;   /* (a 5th: no room) */
    bad += check("#155 ADD: keys one after another build the cursor step's chord (up to 4), the cursor stays", ok);
    tap_k(11);
    ok = t->step[3].n == 4u && t->step[3].note[0] == kb_map(t, 7) && t->step[3].note[1] == kb_map(t, 11) &&
         t->step[3].note[2] == kb_map(t, 14) && t->step[3].note[3] == kb_map(t, 16) && ui.cursor == 3u;
    bad += check("#155 ADD (Dah): a key whose note is there keeps it (recording never takes a note out)", ok);
    turn(EN_K1, 2);
    tap_k(9);
    ok = ui.cursor == 5u && t->step[5].n == 2u && t->step[5].note[0] == 50u && t->step[5].note[1] == kb_map(t, 9) &&
         t->step[3].n == 4u && !t->step[4].n;
    tap_k(9);
    ok &= t->step[5].n == 2u && t->step[5].note[0] == 50u && t->step[5].note[1] == kb_map(t, 9) && t->step[5].time == ST_NOTE;
    turn(EN_K1, -1);
    tap_k(9); tap_k(9);
    ok &= ui.cursor == 4u && t->step[4].n == 1u && t->step[4].note[0] == kb_map(t, 9) && t->step[4].time == ST_NOTE;
    bad += check("#155 ADD: KNOB 1 moves on; a written step gets notes beside its own; a key twice: its note once", ok);
    t->step[6].time = ST_TIE;
    turn(EN_K1, 2);
    tap_k(7);
    ok = ui.cursor == 6u && t->step[6].time == ST_NOTE && t->step[6].n == 1u;
    t->p[P_VOICE] = V_MONO;
    turn(EN_K1, 1);
    tap_k(7); tap_k(9);
    ok &= ui.cursor == 7u && t->step[7].n == 1u && t->step[7].note[0] == kb_map(t, 9);
    tap_k(9);
    ok &= !t->step[7].n;
    bad += check("#155 ADD: a TIE step becomes a note step; MONO keeps one note (another key replaces it, the same takes it out)", ok);
    t->p[P_VOICE] = V_POLY;
    turn(EN_K1, 1);
    key_down(7); frame(); key_down(11); frame();       /* keys held together: each adds */
    key_up(7); key_up(11); frame();
    ok = ui.cursor == 8u && t->step[8].n == 2u;
    press(B_EDIT);
    ok &= !t->step[8].n && ui.cursor == 9u;             /* (EDIT still clears the step and moves on) */
    bad += check("#155 ADD: keys held together add each; EDIT clears the step and moves on as before", ok);
    song.rec = 0;
    memcpy(before, t->step, sizeof before);
    tap_k(7);
    bad += check("#155 ADD, not armed: the keys only play, nothing written", !memcmp(before, t->step, sizeof before));
    menu_put(MI_CHADD, 0);
    return bad;
}

static int test_piano_roll(void)
{
    int bad = 0, ok;
    uint32_t i, nb = 0, lo0, steps = 0;
    track_t *t;
    ui_power_on();
    t = TSEL;
    load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);               /* ACID: A2..A3, ties, accents, slides */
    t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
    go_page(GR_ROLL); ui.cursor = 0; ui.bank = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = pr_columns_match(t, &nb);
    bad += check("piano roll: ACID's bars drawn at their notes' rows, ties carried, rests empty", ok && nb >= 10u);
    bad += check("  the view holds the page's notes (A2..A3 in 21 rows), MONO gray", proll.lo <= 45 && proll.lo + PR_ROWS - 1 >= 57 &&
                 screen_gray());
    t->p[P_VOICE] = V_POLY;                                           /* chords: up to 4 bars in a column */
    for (i = 0; i < 16u; i += 4u) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->n = 4; s->note[0] = 57; s->note[1] = 60; s->note[2] = 64; s->note[3] = 67;
        t->step[i + 1u].time = ST_TIE;
    }
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  4-note chords stacked, their ties carry all four", pr_columns_match(t, &nb));
    for (i = 0; i < NSTEP; i++) {                                     /* LEN 32, page 2 */
        step_clear(&t->step[i]);
        if (i >= 16u && i % 2u == 0u) { t->step[i].time = ST_NOTE; t->step[i].n = 1; t->step[i].note[0] = (uint8_t)(72 + i % 7u); }
    }
    t->p[P_SLEN] = 32; cursor_set(18);
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  LEN 32, the cursor on page 2: its 16 steps drawn", ui.bank == 1u && pr_columns_match(t, &nb) && nb == 8u);
    lo0 = proll.lo;                                                   /* two octaves up: the view follows in steps */
    for (i = 16; i < 32u; i++) if (t->step[i].n) t->step[i].note[0] += 24;
    for (i = 0; i < 20u && proll.lo != lo0 + 24u; i++) { frame(); steps++; }
    bad += check("  notes moved two octaves: the view follows over frames (not at once), then rests",
                 proll.lo == lo0 + 24u && steps >= 3u && steps < 20u);
    {
        uint32_t sig = ui.graph_sig;
        frame(); frame();
        bad += check("  nothing changed: the graph is not drawn again", ui.graph_sig == sig);
    }
    nb = 0;
    bad += check("  after the follow the bars match again", pr_columns_match(t, &nb));
    song.rec = 1;                                                     /* (#133: armed, the key writes the step) */
    key_down(7); frames(200);                                         /* a key held: its row lit on the strip */
    {
        int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - (int32_t)kb_chord[7][0];
        uint16_t c = r >= 0 && r < PR_ROWS ? swap16(host_screen[(Y_GRAPH + PR_Y0 + (uint32_t)r * PR_RH + 1u) * 240u + PR_KX + 7]) : 0;
        bad += check("  a key held: its row on the keyboard strip is the accent", kb_chn[7] && c == T_ACCENT);
    }
    key_up(7); frames(100);
    song.rec = 0;
    return bad;
}

/* bug fixes, second round (UI) */
static uint32_t color_in(uint16_t c, int32_t x0, int32_t y0, int32_t x1, int32_t y1)   /* pixels of colour c */
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((c >> 8) | (c << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static uint32_t accent_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((T_ACCENT >> 8) | (T_ACCENT << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static int test_bughunt_ui2(void)
{
    int bad = 0;
    uint32_t i;
    {   /* 1. a layer button pressed under a dialog and still held after it closes: dead, its keys stay notes */
        static const uint32_t BTN[2] = {B_FX, B_GLO};
        for (i = 0; i < 2u; i++) {
            uint32_t k = white(0), ok;
            ui_power_on(); TSEL->p[P_VOICE] = V_POLY;
            go_home(); frame();
            confirm_open(CF_INIT_SOUND, 0); frame();
            btn_down(BTN[i]); frame(); frames(100);
            btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* the dialog closes, the button still held */
            frames(800);
            ok = !ui.confirm && (ui.ly_t0 & LY_DEAD) && !ui.layer && !kb_mask && !perf_mask;
            key_down(k); frame();
            ok = ok && kb_note[k] != KB_SILENT && gates() == 1u && !((kb_layer >> k) & 1u) && !perf_held && !ui.layer;
            key_up(k); frame();
            btn_up(BTN[i]); frame();
            bad += check(i ? "dead GLO held from a dialog: no map, the key plays a note"
                           : "dead FX held from a dialog: no map, the key plays a note, no effect", ok);
        }
    }
    {   /* 2. STEP entry with CHRD on writes what the key sounds, as live recording does */
        static const uint32_t KEY[2] = {7u, 8u};                      /* C4, C#4 (out of C major) */
        uint32_t v, j, ok = 1;
        for (v = 0; v < 2u; v++)
            for (j = 0; j < 2u; j++) {
                track_t *t;
                step_t e, r;
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_page(GR_ROLL); cursor_set(0); frame(); song.rec = 1;   /* (#133: armed) */
                key_down(KEY[j]); frame();
                e = t->step[0];
                key_up(KEY[j]); frame(); song.rec = 0;
                ok &= e.n == (v ? 1u : 3u) && e.note[0] == 60u && (v || (e.note[1] == 64u && e.note[2] == 67u));
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_home(); frame();
                song.rec = 1; song.playing = 1; t->seq_idx = 3; t->seq_pos = 0;
                fm1_in.notes = 1u << KEY[j]; keyboard_block();
                r = t->step[3];
                fm1_in.notes = 0; keyboard_block();
                song.rec = 0; song.playing = 0;
                ok &= r.n == e.n && !memcmp(r.note, e.note, e.n);
            }
        bad += check("STEP entry with CHRD: POLY the chord, MONO its root, as live recording writes", ok);
    }
    {   /* 3. MIXER (eight strips): the value just turned is drawn TEXT (K1 the fader's thumb and the dB,
         * K2 the PAN dot, K3 the REV bar); K4 MUTE dims the strip (its cushion DIM) */
        uint32_t k, a[4][4], ok = 1;
        for (k = 0; k < 4u; k++) {
            int32_t X, Y = Y_GRAPH;
            ui_power_on();
            X = MX_X0 + MX_PITCH;                                     /* (Dah: track 1's strip, after the MASTER's) */
            settings.palette = 1; palette_set(1);
            go_page(GR_TRK); frame(); frame();
            turn(EN_K1 + k, 1);                                       /* (K4 right: MUTE on) */
            ui.force = 1; frame();
            {   /* (Dah: no dB; PAN and REV at the panel's foot) */
                int32_t H = (int32_t)graph_h();
                a[k][0] = color_in(T_TEXT, X + MX_FX, Y + MX_FY - 1, X + MX_FX + 11, Y + MX_FY + MX_FH);   /* LEVEL: the thumb */
                a[k][1] = color_in(T_TEXT, X + 2, Y + H - 18, X + MX_W - 2, Y + H - 11);   /* PAN */
                a[k][2] = color_in(T_TEXT, X + 2, Y + H - 9, X + MX_W - 2, Y + H - 4);     /* REV */
                a[k][3] = color_in(T_DIM, X + MX_W / 2 - 5, Y + 6, X + MX_W / 2 + 5, Y + 15);   /* MUTE: the cushion DIM */
            }
        }
        for (k = 0; k < 4u; k++) {
            uint32_t j;
            for (j = 0; j < 4u; j++)
                ok &= j == k ? a[k][j] > 0u : a[k][j] == 0u;
        }
        bad += check("MIXER: K1..K4 light LEVEL / PAN / REV in TEXT, MUTE dims the strip, nothing else", ok);
    }
    /* 4. (the slots' rename retired; FILE's EDIT renames the project open: test_names) */
    {   /* 5. NAME: PLAY stops a transport started meanwhile (so the name can be saved), never starts it; REC ignored */
        uint32_t ok;
        ui_power_on(); go_page(GR_USER); ui.uslot = 3; press(B_OCTUP);
        ok = name_on();
        press(B_PLAY); frame();
        ok &= !transport_req && !song.playing && name_on();          /* stopped: PLAY starts nothing */
        press(B_REC); frame();
        ok &= !song.rec && !transport_req && name_on();
        song.playing = 1;                                             /* an external MIDI Start / the editor */
        press(B_OCTUP);
        ok &= name_on() && msg_is("STOP TO SAVE");
        press(B_REC); frame();
        ok &= !song.rec && !transport_req;
        press(B_PLAY); frame();
        ok &= transport_req == 2u && name_on();                       /* PLAY stops it, NAME stays */
        stop_transport();
        press(B_OCTUP);
        ok &= !name_on() && up_used(3);
        bad += check("NAME: PLAY stops a running transport (never starts), REC ignored, then OCT+ saves", ok);
    }
    {   /* 6. the piano roll's lowest C name ("C-1") stays inside the panel: nothing drawn left of x 3 */
        uint32_t p, x, y, out = 0;
        uint16_t bg;
        for (p = 0; p < NPALETTES; p++) {
            track_t *t;
            uint8_t lo = 0, hi = 127;
            ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 16;
            settings.palette = (uint8_t)p; palette_set(p);
            t->step[0].time = t->step[5].time = ST_NOTE; t->step[0].n = t->step[5].n = 1;
            t->step[0].note[0] = lo; t->step[5].note[0] = hi;
            go_page(GR_ROLL); cursor_set(0); ui.force = 1; frame(); frame();
            bg = (uint16_t)((T_BG >> 8) | (T_BG << 8));
            for (y = Y_GRAPH; y < Y_GRAPH + H_GRAPH; y++)
                for (x = 0; x < 3u; x++)
                    out += host_screen[y * 240u + x] != bg;
        }
        bad += check("piano roll: the C-1 label stays inside the panel (every palette)", !out);
    }
    {   /* 7. EDIT: INIT is drawn in the cell after the engines, and that cell's white key is the one that inits */
        uint32_t p, ok = LY_INIT == NENG_SHOWN && LY_INIT < 16u;      /* (layer_edit: INIT in cell NENG_SHOWN) */
        for (p = NENG_SHOWN; p < 16u; p++) {
            ui_power_on(); go_home(); frame();
            btn_down(B_EDIT); key_down(white(p)); frame(); frames(50);
            ok &= (ui.confirm == CF_INIT_SOUND) == (p == LY_INIT);
            key_up(white(p)); btn_up(B_EDIT); frame();
        }
        bad += check("EDIT: INIT's map cell (after the engines) and its key agree", ok);
    }
    return bad;
}

#ifndef UI_TEST_NO_MAIN
/* L is a sparse face (tools/gen_aa_font.py L_CHARS): every string drawn in it has all its glyphs */
static int test_large_face(void)
{
    static const char *const FIXED[] = {FW_NAME, "0123456789"};   /* main.c, ui_menu.c; ui_draw.c draw_uboot */
    uint32_t i, missing = 0;
    const char *s;
    for (i = 0; i < NB + NE + 2u; i++)                              /* ui_input.c setup_show: the control names */
        for (s = i < NB ? B_NAME[i] : i < NB + NE ? E_NAME[i - NB] : FIXED[i - NB - NE]; *s; s++)
            missing += glyph_at(&AF_L, (uint8_t)*s) < 0;
    return check("the L face holds every glyph of the strings drawn in it", !missing);
}

#if FELUCCA_FM4
/* The DIGITAL algorithm charts (ui_graph.c FM_CELL / FM_MOD) against the DSP: src/eng_digital.c's switch (alg)
 * in digital_render_legacy and digital_render_custom, written out here as "source>destination" routes and the
 * operators mixed to the output. Op 4's feedback is on every algorithm (drawn always). Also: the carriers sit
 * on the bottom row and only they, a modulator one row above what it modulates, no two operators in a cell,
 * every operator heard, and the eight charts distinct. */
static int test_fm_charts(void)
{
    static const char *const DSP[8][2] = {
        {"4>3 3>2 2>1", "1"},          /* o3 = op(o4), o2 = op(o3), s = op(o2) */
        {"3>2 4>2 2>1", "1"},          /* o2 = op((o3 + o4) / 2), s = op(o2) */
        {"3>2 2>1 4>1", "1"},          /* o2 = op(o3), s = op((o2 + o4) / 2) */
        {"4>3 2>1 3>1", "1"},          /* o3 = op(o4), s = op((o2 + o3) / 2) */
        {"4>3 2>1", "13"},             /* o3 = op(o4), o1 = op(o2), s = (o1 + o3) / 2 */
        {"4>1 4>2 4>3", "123"},        /* o1, o2, o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"4>3", "123"},                /* o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"", "1234"}};                 /* s = (o1 + o2 + o3 + o4) / 4 */
    uint32_t a, b, k, j, routes = 1, layout = 1, distinct = 1;
    for (a = 0; a < 8u; a++) {
        uint32_t want = 0, car = 0, ok = 1;
        const char *s;
        for (s = DSP[a][0]; *s; s++)
            if (*s == '>') want |= 1u << (4u * (uint32_t)(s[1] - '1') + (uint32_t)(s[-1] - '1'));
        for (s = DSP[a][1]; *s; s++) car |= 1u << (*s - '1');
        if (FM_MOD[a] != want) { printf("ui: algorithm %u: routes %04x, the DSP %04x\n", a + 1u, FM_MOD[a], want); routes = 0; }
        for (k = 0; k < 4u; k++) {
            uint32_t row = FM_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u)) ok = 0;
            for (j = 0; j < 4u; j++) {
                if (j != k && FM_CELL[a][j] == FM_CELL[a][k]) ok = 0;
                if (FM_MOD[a] >> (4u * j + k) & 1u) { outs++; if (row != (FM_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;
        }
        if (!ok) { printf("ui: algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
        for (b = 0; b < a; b++)
            if (FM_MOD[a] == FM_MOD[b] && !memcmp(FM_CELL[a], FM_CELL[b], 4)) distinct = 0;
    }
    return check("DIGITAL charts: the 8 algorithms' routes and carriers equal eng_digital.c's", routes) +
           check("DIGITAL charts: carriers on the bottom row, modulators a row up, one per cell, distinct", layout && distinct);
}
#endif

/* The FM6 algorithm charts (ui_graph.c graph_fm6: FM6_CELL, its routes fm6_routes and feedback operator
 * fm6_fb_op read from fm6_core.c FM6_ALG) against the 32 algorithms of 6-operator FM written out here as
 * "source>destination" routes, the carriers and the operator with feedback (fm6_core.c's: on algorithms 4 and
 * 6 the loop is the sixth operator's own, as msfa plays it). Also against the core itself: the carriers
 * fm6_carriers (fm6_car_ops), the feedback flags. The layout: the carriers on the bottom row and only they, a modulator one
 * row above everything it modulates, one operator per cell, columns 0..5, at most 4 rows. (The charts as drawn:
 * tests/ui_render.c's lint, fm6_lint.) */
static int test_fm6_charts(void)
{
    static const struct { const char *r, *car; char fb; } ALGS[32] = {
        {"2>1 4>3 5>4 6>5", "13", '6'}, {"2>1 4>3 5>4 6>5", "13", '2'}, {"2>1 3>2 5>4 6>5", "14", '6'},
        {"2>1 3>2 5>4 6>5", "14", '6'}, {"2>1 4>3 6>5", "135", '6'}, {"2>1 4>3 6>5", "135", '6'},
        {"2>1 4>3 5>3 6>5", "13", '6'}, {"2>1 4>3 5>3 6>5", "13", '4'}, {"2>1 4>3 5>3 6>5", "13", '2'},
        {"2>1 3>2 5>4 6>4", "14", '3'}, {"2>1 3>2 5>4 6>4", "14", '6'}, {"2>1 4>3 5>3 6>3", "13", '2'},
        {"2>1 4>3 5>3 6>3", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '2'},
        {"2>1 3>1 5>1 4>3 6>5", "1", '6'}, {"2>1 3>1 5>1 4>3 6>5", "1", '2'}, {"2>1 3>1 4>1 5>4 6>5", "1", '3'},
        {"2>1 3>2 6>4 6>5", "145", '6'}, {"3>1 3>2 5>4 6>4", "124", '3'}, {"3>1 3>2 6>4 6>5", "1245", '3'},
        {"2>1 6>3 6>4 6>5", "1345", '6'}, {"3>2 6>4 6>5", "1245", '6'}, {"6>3 6>4 6>5", "12345", '6'},
        {"6>4 6>5", "12345", '6'}, {"3>2 5>4 6>4", "124", '6'}, {"3>2 5>4 6>4", "124", '3'},
        {"2>1 4>3 5>4", "136", '5'}, {"4>3 6>5", "1235", '6'}, {"4>3 5>4", "1236", '5'},
        {"6>5", "12345", '6'}, {"", "123456", '6'}};
    uint32_t a, k, j, routes = 1, layout = 1, core = 1;
    for (a = 0; a < 32u; a++) {
        uint8_t m[6], want[6] = {0, 0, 0, 0, 0, 0};
        uint32_t car = 0, ok = 1, fbs = 0;
        const char *s;
        for (s = ALGS[a].r; *s; s++)
            if (*s == '>') want[s[1] - '1'] |= (uint8_t)(1u << (s[-1] - '1'));
        for (s = ALGS[a].car; *s; s++) car |= 1u << (*s - '1');
        fm6_routes(a, m);
        if (memcmp(m, want, 6) || fm6_fb_op(a) != (uint32_t)(ALGS[a].fb - '1')) {
            printf("ui: FM6 algorithm %u: the chart's routes or feedback differ from the written ones\n", a + 1u);
            routes = 0;
        }
        for (k = 0; k < 6u; k++)                                    /* the core: who writes the output, who has FB */
            fbs += (FM6_ALG[a][k] & 0xC0u) == 0xC0u;
        if (fm6_car_ops(a) != car || fbs != 1u) {
            printf("ui: FM6 algorithm %u: fm6_core.c's carriers / feedback differ\n", a + 1u);
            core = 0;
        }
        for (k = 0; k < 6u; k++) {
            uint32_t row = FM6_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u) || (FM6_CELL[a][k] & 0x0Fu) > 5u || row > 3u) ok = 0;
            for (j = 0; j < 6u; j++) {
                if (j != k && FM6_CELL[a][j] == FM6_CELL[a][k]) ok = 0;
                if (m[j] >> k & 1u) { outs++; if (FM6_CELL[a][k] >> 4 != (FM6_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;               /* every operator heard */
        }
        if (!ok) { printf("ui: FM6 algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
    }
    return check("FM6 charts: the 32 algorithms' routes, carriers and feedback operator equal fm6_core.c's", routes && core) +
           check("FM6 charts: carriers on the bottom row, modulators a row up, one per cell, at most 4 rows", layout);
}

#if !FELUCCA_FM4
/* DIGITAL retired (src/fm4_convert.c): engine 1 is on no track and in no list; its sounds arrive as FM6 with a patch
 * of their own: a user preset of it, its preset numbers (set_engine_of / apply_preset_to), a track that got engine 1
 * any other way (the main loop's net); the favourites of its presets move to FM6's; the power-on pad is FM6 PAD */
static int test_fm4_retired(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, e, total, seen = 0, all = ((1u << NENGINES) - 1u) & ~(1u << ENGI_DIGITAL);
    int16_t p[P_COUNT];
    uint8_t v[FP_SIZE + 1u];
    ui_power_on();
    bad += check("power-on: track 2 is FM6 PAD (TRK_DEF, was DIGITAL PAD)", trk[1].eng_req == ENGI_FM6 &&
                 str_eq(ENGINES[ENGI_FM6]->presets[trk[1].preset].name, "PAD"));
    preset_all_pos(&total);
    for (i = 0; i < total; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES)
            seen |= 1u << e;
    }
    bad += check("PRESETS: the list holds every engine's presets but DIGITAL's", seen == all && NENG_SHOWN == NENGINES - 1u);
    bad += check("  the engines in order, DIGITAL skipped, round", eng_step(0, 1) == ENGI_FM6 && eng_step(ENGI_FM6, 1) == 2u &&
                 eng_step(ENGI_FM6, -1) == 0u && eng_step(0, -1) == ENGI_DRUM);
    {   /* the display order (engines.c ENGINE_ORDER): every engine one can pick once; the PRESETS list follows it */
        static const char *const ORDER[] = {"ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                            "PHYS", "NOISE", "SLICE",
#if FELUCCA_GROWL
                                            "GROWL",
#endif
                                            "DRUM"};
        uint32_t last = 0xFFu, r = 0, n = 0;
        ok = NENG_SHOWN == NELEM(ORDER);
        for (i = 0; ok && i < NENG_SHOWN; i++)
            ok &= str_eq(ENGINES[eng_vis(i)]->name, ORDER[i]) && eng_rank(eng_vis(i)) == i && eng_ok(eng_vis(i));
        preset_all_pos(&total);
        for (i = 0; i < total; i++) {                  /* the list's engines, each once, in that order */
            e = preset_all_at(i, &k);
            if (e >= NENGINES || e == last)
                continue;
            ok &= n < NENG_SHOWN && e == eng_vis(n);
            n++;
            last = e;
            r++;
        }
        bad += check("engines shown ANALOG FM6 PHASE ... NOISE SLICE DRUM (ENGINE_ORDER); PRESETS lists them so",
                     ok && r == NENG_SHOWN);
    }
    /* a user preset stored with engine 1: kept as it is, it loads as FM6 with the converted patch */
    for (i = 0; i < P_COUNT; i++)
        p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(p, 5);                           /* DIGITAL PAD */
    {
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_DIGITAL;
        r.np = P_COUNT;
        memcpy(r.name, "OLD PAD", 7);
        for (i = 0; i < P_COUNT; i++)
            up_set_value(&r, i, p[i]);
        up_put(7, &r);
    }
    fm4_convert(p, v);
    up_load(7);
    bad += check("a DIGITAL user preset loads as FM6: the converted patch its own (SLOT OWN), preset = FM6 PAD",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 4u && TSEL->p[P_E7] == FM6_OWN && !TSEL->p[P_E0] &&
                 !memcmp(fm6_patch[song.sel], v, FP_SIZE) && fm6_slot[song.sel] == FM6_OWN && TSEL->user == 8u);
    frame();
    bad += check("  .. and the main loop keeps that patch (not a factory one)", !memcmp(fm6_patch[song.sel], v, FP_SIZE));
    eng_list_pos(&total);
    bad += check("  the record stays DIGITAL in the bank, listed with FM6's sounds (EDIT KNOB 2)",
                 up_rec(7)->engine == ENGI_DIGITAL && up_engine(7) == ENGI_FM6 &&
                 total == ENGINES[ENGI_FM6]->npresets + 1u);
    up_store(8, "AGAIN");
    {
        uint8_t pk[FM6_PACKED], w[FP_SIZE + 1u];
        bad += check("  saved again: an FM6 user preset with SLOT OWN and the converted patch beside it",
                     up_rec(8)->engine == ENGI_FM6 && up_value(up_rec(8), P_E7) == FM6_OWN && !upf_get(8, pk) &&
                     (fm6_unpack(pk, w), !memcmp(w, v, FP_SIZE)));
        fm6_load_slot(song.sel, 2);                    /* another patch on the track, then the preset again */
        up_load(8);
        frame();
        bad += check("  .. which loads back: the same patch, SLOT OWN", !memcmp(fm6_patch[song.sel], v, FP_SIZE) &&
                     TSEL->p[P_E7] == FM6_OWN && fm6_slot[song.sel] == FM6_OWN);
    }
    /* preset numbers of engine 1 */
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("engine 1 asked for: DIGITAL E.PIANO converted (FM6, PTCH TINE EP, the patch named E.PIANO)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 0u && !memcmp(fm6_patch[song.sel] + FP_NAME, "E.PIANO", 7));
    TSEL->eng_req = ENGI_DIGITAL;                      /* (the ISR's view: no other path does this) */
    apply_preset_to(TSEL, 1);
    bad += check("a DIGITAL preset number (apply_preset_to): its sound converted (BELL)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 1u && !memcmp(fm6_patch[song.sel] + FP_NAME, "BELL", 4));
    for (i = 0; i < P_COUNT; i++)
        trk[2].p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(trk[2].p, 2);
    trk[2].eng_req = ENGI_DIGITAL;
    frame();
    bad += check("a track left on engine 1 any other way: the main loop converts it (BASS -> FM BASS)",
                 trk[2].eng_req == ENGI_FM6 && trk[2].preset == 2u);
    return bad;
}
#endif

/* #38: the keys of the notes the selected track's sequencer and ARP sound light while it plays */
static int test_play_leds(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t i;
    ui_power_on();
    song.sel = 0;
    t->step[0] = (step_t){.note = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.note = {100}, .n = 1, .time = ST_NOTE};
    trk[1].step[0] = (step_t){.note = {67}, .n = 1, .time = ST_NOTE};
    t->seq_active = trk[1].seq_active = 1;
    bad += check("stopped, nothing held: no key lit", key_leds(0) == 0u && play_leds() == 0u);
    seq_start();
    events_block(CTL);
    bad += check("playing: C4 and E4 of step 1 light keys 8 and 12 (from F3), track 2's G4 does not",
                 t->seq_n == 2u && trk[1].seq_n == 1u && key_leds(0) == (1u << 7 | 1u << 11));
    song.sel = 1;
    bad += check("the selected track's notes: track 2's G4 on key 15", play_leds() == 1u << 14);
    song.sel = 0;
    fm1_in.notes = 1u << 0;
    bad += check("a key held still lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    song.octave = -1;
    ok = play_leds() == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= play_leds() == 0u;                           /* (C4 and E4 below the keys an octave up) */
    song.octave = 0;
    t->p[P_TRANS] = 2;
    ok &= play_leds() == (1u << 5 | 1u << 9);
    t->p[P_TRANS] = 0;
    bad += check("the keys follow the octave and TRN; notes off the keyboard are not shown", ok);
    t->p[P_QUANT] = 1;                                 /* SNAP in C major: C#4 rounds down to C4 too */
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    ok = kb_map(t, 8) == 60u && play_leds() == (1u << 7 | 1u << 11);
    t->p[P_QUANT] = t->p[P_SCALE] = 0;
    bad += check("QNT SNAP: only the key of the note itself lights, not the keys rounding onto it", ok);
    t->arp_note = 65;
    bad += check("the ARP's note lights its key too", play_leds() == (1u << 7 | 1u << 11 | 1u << 12));
    t->arp_note = 0;
    events_block(div_samples((uint32_t)t->p[P_SDIV]) + CTL);
    bad += check("a note above the keyboard (G#7) lights nothing", t->seq_n == 1u && t->seq_notes[0] == 100u &&
                 play_leds() == 0u);
    transport_req = 2;
    events_block(CTL);
    bad += check("stopped: the notes end, their keys go dark", !song.playing && key_leds(0) == 0u);
    /* the layer, the grid and NAME keep their keys */
    seq_start();
    events_block(CTL);
    ui.layer = 1;
    ok = key_leds(0) == layer_leds(0);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    transport_req = 2; events_block(CTL);
    seq_start(); events_block(CTL);
    i = play_leds();
    ok &= t->seq_n == 1u && i != 0u && kb_map(t, (uint32_t)__builtin_ctz(i)) == DRUM_LANE_NOTE[DV_KICK];
    open_family(FAM_SEQ);
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("the layer's map and the DRUM grid keep their keys; elsewhere a kit's hits light the keys that play them", ok);
    transport_req = 2; events_block(CTL);
    return bad;
}

/* Discussion #89: on SEQ > STEP (the piano roll) while stopped the keys of the cursor step's notes light, at the
 * octave now (the lowest key that gives each); with the keys held; playing, what plays instead (#38); not on
 * other pages, not for a REST step; the DRUM grid keeps its own map */
static int test_cursor_step_leds(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    song.sel = 0;
    t = TSEL;
    t->step[0] = (step_t){.note = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.note = {100}, .n = 1, .time = ST_NOTE};
    t->step[2] = (step_t){.note = {62}, .n = 1, .time = ST_REST};
    t->step[3] = (step_t){.note = {67}, .n = 1, .time = ST_TIE};
    t->seq_active = 1;
    bad += check("#89 HOME, stopped: no step's keys lit", key_leds(0) == 0u);
    go_page(GR_ROLL);
    frame();
    bad += check("#89 STEP, stopped: the cursor step's C4 and E4 light keys 8 and 12",
                 ui.cursor == 0u && key_leds(0) == (1u << 7 | 1u << 11));
    song.octave = -1;
    ok = key_leds(0) == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (below the keys an octave up) */
    song.octave = 0;
    bad += check("#89 the step's keys follow the octave; notes off the keyboard are not shown", ok);
    fm1_in.notes = 1u << 0;
    bad += check("#89 a key held lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    turn(EN_K1, 1);
    ok = ui.cursor == 1u && key_leds(0) == 0u;          /* G#7: off the keyboard */
    turn(EN_K1, 1);
    ok &= ui.cursor == 2u && key_leds(0) == 0u;         /* a REST */
    turn(EN_K1, 1);
    ok &= ui.cursor == 3u && key_leds(0) == 1u << 14;   /* a TIE carries its G4 */
    turn(EN_K1, 1);
    ok &= ui.cursor == 4u && key_leds(0) == 0u;         /* empty */
    bad += check("#89 KNOB 1 moves the cursor and the keys follow its step (REST, empty, off the keys: dark)", ok);
    turn(EN_K1, -4);
    seq_start();
    events_block(CTL);
    bad += check("#89 playing: the keys show what plays, not the cursor step", song.playing && ui.cursor == 0u &&
                 key_leds(0) == play_leds());
    turn(EN_K1, 3);
    bad += check("#89   (the cursor on G4's step changes nothing while playing)", key_leds(0) == play_leds() &&
                 !((key_leds(0) >> 14) & 1u));
    transport_req = 2;
    events_block(CTL);
    frame();
    bad += check("#89 stopped again: the cursor step's keys", !song.playing && key_leds(0) == 1u << 14);
    go_page(GR_EVENTS);
    frame();
    bad += check("#89 AUTOMATION (same cursor): no step keys", key_leds(0) == 0u);
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    go_page(GR_ROLL);
    frame();
    bad += check("#89 the DRUM grid keeps its map", grid_on() && key_leds(0) == grid_leds());
    return bad;
}

/* Discussion #81: the notes MIDI IN holds on the selected track light their keys as the sequencer's do: USB and TRS,
 * routed by ROUT; at the octave now, the lowest key; off the keyboard nothing; the pedal holds them; the layer's map
 * and the DRUM grid keep their keys */
static void usb_note(uint32_t ch, uint32_t note, uint32_t vel)   /* a USB-MIDI packet, as usb.c queues it */
{
    uint32_t st = (vel ? 0x90u : 0x80u) | ch;
    midi_in_event((st >> 4) | st << 8 | note << 16 | vel << 24);
    events_block(CTL);
}
static void trs_note(uint32_t a, uint32_t b, uint32_t c)        /* TRS MIDI IN: midi_uart.c's parser */
{
    um_byte(a); um_byte(b); um_byte(c);
    events_block(CTL);
}
static int test_midi_leds(void)
{
    int bad = 0, ok;
    ui_power_on();
    memset(&um, 0, sizeof um);
    song.sel = 0;
    usb_note(0, 60, 100);                              /* channel 1 -> T1 (ROUT CH1-4) */
    bad += check("#81 USB MIDI C4 into the selected track lights key 8 (C4)", midi_sel_on[0][60] == 1u &&
                 key_leds(0) == 1u << 7 && play_leds() == 1u << 7);
    usb_note(1, 67, 100);                              /* channel 2 -> T2: not the selected track */
    ok = key_leds(0) == 1u << 7;
    song.sel = 1;
    ok &= key_leds(0) == 1u << 14;
    song.sel = 0;
    bad += check("  another track's MIDI notes light nothing; selected, its G4 lights key 15", ok);
    usb_note(1, 67, 0);
    usb_note(0, 60, 0);
    bad += check("  note-off: dark", key_leds(0) == 0u && !midi_owners[0]);
    trs_note(0x90, 64, 90);                           /* TRS: E4 on channel 1 */
    ok = key_leds(0) == 1u << 11;
    trs_note(0x80, 64, 0);
    bad += check("  TRS MIDI IN: E4 lights key 12 while held", ok && key_leds(0) == 0u);
    song.g[G_ROUTE] = 1;                               /* ROUT SEL: every channel to the selected track */
    song.sel = 2;
    usb_note(9, 62, 100);
    ok = midi_sel_on[9][62] == 3u && key_leds(0) == 1u << 9;
    usb_note(9, 62, 0);
    song.g[G_ROUTE] = 0;
    song.sel = 0;
    bad += check("  ROUT SEL: channel 10 into the selected track lights its key", ok && key_leds(0) == 0u);
    usb_note(0, 60, 100);
    song.octave = -1;
    ok = key_leds(0) == 1u << 19;
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (C4 below the keys an octave up) */
    song.octave = 0;
    usb_note(0, 60, 0);
    usb_note(0, 100, 100);                             /* G#7: above the keyboard */
    usb_note(0, 40, 100);                              /* E2: below it */
    ok &= key_leds(0) == 0u && midi_owners[0] == 2u;
    usb_note(0, 100, 0);
    usb_note(0, 40, 0);
    bad += check("  the keys follow the octave; notes off the keyboard are not shown", ok);
    trk[0].p[P_QUANT] = 1;                             /* SNAP in C major: C#4 rounds down to C4 too */
    trk[0].p[P_SCALE] = 1;
    trk[0].p[P_ROOT] = 0;
    usb_note(0, 60, 100);
    ok = kb_map(&trk[0], 8) == 60u && key_leds(0) == 1u << 7;
    usb_note(0, 60, 0);
    trk[0].p[P_QUANT] = trk[0].p[P_SCALE] = 0;
    bad += check("  QNT SNAP: only the lowest key giving the note lights", ok && key_leds(0) == 0u);
    midi_event(0xB0, 0, 64, 127);                      /* the pedal down: a released note still sounds */
    usb_note(0, 65, 100);
    usb_note(0, 65, 0);
    ok = key_leds(0) == 1u << 12;
    midi_event(0xB0, 0, 64, 0);
    bad += check("  a note the pedal holds stays lit until the pedal is up", ok && key_leds(0) == 0u);
    fm1_in.notes = 1u << 0;                            /* with a key held and the sequencer */
    usb_note(0, 60, 100);
    ok = key_leds(0) == (1u << 0 | 1u << 7);
    fm1_in.notes = 0;
    trk[0].step[0] = (step_t){.note = {64}, .n = 1, .time = ST_NOTE};
    trk[0].seq_active = 1;
    seq_start();
    events_block(CTL);
    ok &= key_leds(0) == (1u << 7 | 1u << 11);
    bad += check("  with a key held and the sequencer's notes: all of them lit", ok);
    ui.layer = LAYER_SCL;
    {
        uint32_t b1, b2;
        ok = key_leds(&b1) == layer_leds(&b2) && b1 == b2 && b1;
    }
    ui.layer = 0;
    open_family(FAM_SEQ);                              /* STEP on a melodic track: the keys as elsewhere */
    frame();
    ok &= !grid_on() && (key_leds(0) & (1u << 7));
    set_engine_of(&trk[0], ENGI_DRUM);
    trk[0].engine = trk[0].eng_req;
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("  the layer's map and the DRUM grid keep their keys over the MIDI notes", ok);
    transport_req = 2;
    events_block(CTL);
    usb_note(0, 60, 0);
    return bad;
}

/* #119: what can be pressed breathes (hal/fm1_input.h fm1_led_breath: dark .. ~60 % of lit), never a hard blink: every
 * layer, held and locked (double tap), in every MENU > LEDS mode: its keys that can be pressed and its button in the
 * breath set only (neither lit nor the glow), the ones in effect lit, the picture the same at any time (no 250 ms
 * toggle left); OCT+ in a dialog likewise; closing the layer leaves nothing breathing; the glow's level and
 * the breath's peak follow LEDS (fm1_led_dim_level: DIM LO the darker glow and ~30 %, else DIM HI's and ~60 %) */
static uint32_t breath_keys(void)                     /* the breathing keys, bit k = key k (the real positions) */
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static uint32_t lit_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static int led_of(const uint8_t *l, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u);
}
static int test_breath(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const uint8_t MODES[4] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0, ok = 1, okl = 1, okm = 1, oko = 1;
    uint32_t i, m, lk, t, held;
    ui_power_on();
    ui_leds();
    for (i = 0; i < 41u; i++)                           /* (the stub key map has none: 4 LEDs a column) */
        led_pos[i] = (uint8_t)((i / 4u) << 3 | (1u + i % 4u));
    for (i = 0; i < 4u; i++)
        for (lk = 0; lk < 2u; lk++)
            for (m = 0; m < 4u; m++) {
                uint32_t br0 = 0, lit0 = 0, want_br, want_lit, hk = LL[i] == LAYER_GLO ? white(1) : white(2);
                ui_power_on();
                settings_leds = MODES[m];
                go_title("ENV"); frame();
                if (lk) {                                   /* locked (#83), then a key held */
                    press(LB[i]); frames(64); press(LB[i]); frames(100);
                    okl &= ui.lock == LL[i];
                } else {
                    btn_down(LB[i]);
                }
                key_down(hk); frame();
                okl &= ui.layer == LL[i];
                want_lit = layer_leds(&want_br);
                held = LL[i] == LAYER_FX || LL[i] == LAYER_GLO;  /* (HOLD keys: lit while held) */
                for (t = 0; t < 8u; t++) {                  /* 0 .. 1.75 s: the same picture */
                    fm1_ms += 250u;
                    ui_leds();
                    if (!t) {
                        br0 = breath_keys();
                        lit0 = lit_keys(fm1_led);
                    }
                    ok &= breath_keys() == br0 && lit_keys(fm1_led) == lit0 && !lit_keys(fm1_led_dim) &&
                          led_of(fm1_led_breath, panel.btn[LB[i]]) && !led_of(fm1_led, panel.btn[LB[i]]) &&
                          !led_of(fm1_led_dim, panel.btn[LB[i]]);
                }
                ok &= br0 == want_br && lit0 == want_lit && br0 && !(br0 & lit0);
                ok &= !held || (((lit0 >> hk) & 1u) && !((br0 >> hk) & 1u));   /* held: lit, not breathing */
                okm &= host_dim_lo == (MODES[m] == LEDS_DIM_LO);
                key_up(hk); frame();
                if (lk)
                    press(LB[i]);
                else
                    btn_up(LB[i]);
                frames(64);
                ui_leds();
                for (t = 0; t < FM1_NCOL; t++)
                    okl &= !ui.layer && !fm1_led_breath[t];
            }
    settings_leds = LEDS_DIM;
    bad += check("#119 FX GLO SCL EDIT, held and locked, every LEDS mode: the keys that can be pressed breathe", ok);
    bad += check("  ..held / in effect lit, never the glow, the layer's button breathes, steady over time", ok);
    bad += check("  ..closed: nothing breathes", okl);
    bad += check("  ..the breath's peak follows LEDS (DIM LO ~30 % of lit, else ~60 %)", okm);
    /* OCT+ in a dialog: breathing, OCT- lit, in every mode */
    for (m = 0; m < 4u; m++) {
        ui_power_on();
        settings_leds = MODES[m];
        go_title("ENV"); frame();
        ui.confirm = CF_INIT_SOUND;
        for (t = 0; t < 4u; t++) {
            fm1_ms += 250u;
            ui_leds();
            oko &= led_of(fm1_led_breath, panel.btn[B_OCTUP]) && !led_of(fm1_led, panel.btn[B_OCTUP]) &&
                   !led_of(fm1_led_dim, panel.btn[B_OCTUP]) && led_of(fm1_led, panel.btn[B_OCTDN]) ==
                   (MODES[m] != LEDS_INV) && !led_of(fm1_led_breath, panel.btn[B_OCTDN]);
        }
        ui.confirm = 0;
        ui_leds();
        oko &= !led_of(fm1_led_breath, panel.btn[B_OCTUP]);
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    bad += check("#119 a dialog: OCT+ breathes (not lit, not the glow), OCT- lit (INV: dark), every LEDS mode", oko);
    ui_power_on();
    return bad;
}

/* #35: every idle button and key glows dim, the active ones are lit; a map of the keys' own stays lit / dark */
static int test_idle_glow(void)
{
    int bad = 0, ok;
    ui_power_on();
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
    led_pos[panel.btn[B_ENV]] = 0u << 3 | 2u;
    led_pos[panel.btn[B_PLAY]] = 0u << 3 | 3u;
    led_pos[14u + 0u] = 1u << 3 | 1u;              /* keys 1 and 2 */
    led_pos[14u + 1u] = 1u << 3 | 2u;
    go_home();
    ui_leds();
    bad += check("HOME: its button lit, ENV and PLAY (stopped) glow, the keys glow, none lit",
                 fm1_led[0] == 2u && fm1_led_dim[0] == 14u && fm1_led[1] == 0u && fm1_led_dim[1] == 6u);
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok = fm1_led[0] == 2u && (fm1_led_dim[0] & 8u) == 0u && fm1_led[8] == 2u && fm1_led[1] == 2u && fm1_led_dim[1] == 6u;
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("a key held lit over the glow; PLAY running: its green, its own LED dark", ok);
    ui.layer = 1;
    ui_leds();
    ok = fm1_led_dim[1] == 0u && fm1_led_dim[0] == 14u;
    ui.layer = 0;
    ui_leds();
    ok &= fm1_led_dim[1] == 6u;
    bad += check("a layer's map: the keys lit or dark (no glow), the buttons still glow", ok);
    /* MENU > LEDS INV: the active ones dark, the idle ones lit (stock), no glow; a breath as in DIM (#119); a map
     * of the keys' own as in DIM */
    settings_leds = LEDS_INV;
    ui_leds();
    ok = fm1_led[0] == 12u && fm1_led_dim[0] == 0u && fm1_led[1] == 6u && fm1_led_dim[1] == 0u;   /* HOME dark */
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok &= fm1_led[0] == 4u && fm1_led_dim[0] == 0u && fm1_led[1] == 4u && fm1_led_dim[1] == 0u && fm1_led[8] == 2u;   /* PLAY, key 1 dark; green */
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("LEDS INV: the page's button, PLAY running and a key held go dark, the rest lit, no glow", ok);
    ui.layer = 1;
    led_pos[panel.btn[layer_btn()]] = 0u << 3 | 4u;   /* the layer's button */
    {
        uint32_t seen = 0, t;
        for (t = 0; t < 4u; t++) {                  /* the layer's button breathes: never lit (inverted), never the glow */
            fm1_ms = t * 250u;
            ui_leds();
            seen |= ((fm1_led[0] >> 4) & 1u ? 1u : 2u) | ((fm1_led_dim[0] >> 4) & 1u ? 4u : 0u) |
                    ((fm1_led_breath[0] >> 4) & 1u ? 8u : 0u);
        }
        ok = seen == 10u && fm1_led_dim[1] == 0u && fm1_led[1] == (uint8_t)((layer_leds(0) & 3u) << 1);
    }
    ui.layer = 0;
    fm1_ms = 0;
    bad += check("LEDS INV: the layer's button breathes (not inverted); a layer's map stays lit / dark as in DIM", ok);
    settings_leds = LEDS_DIM;
    led_pos_init();
    return bad;
}

/* "On the step sequencer all the LEDs go out": SEQ > STEP, the piano roll and the DRUM grid, stopped and playing,
 * in every LEDS mode, with the LEDs where the hardware has them (hal/fm1_input.h FM1_KEYMAP). The buttons keep
 * their glow and their active states; on the piano roll the keys glow under the held and playing ones; on the
 * grid its map is lit and its keys that do something glow (DIM LO / HI). Before: keys_own() took the grid's keys
 * out of the glow, so in DIM 26 of 27 keys of an empty 16-step grid went fully dark */
/* the white keys of the grid's steps (bit p = step p of the page shown), bit k = key k */
static uint32_t step_keys(uint32_t steps)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) < 16u && ((steps >> key_place(k)) & 1u))
            m |= 1u << k;
    return m;
}
static int test_step_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0;
    uint32_t id, p, r, mi, drum, run, k;
    ui_power_on();
    ui_leds();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (drum = 0; drum < 2u; drum++)
        for (run = 0; run < 2u; run++)
            for (mi = 0; mi < LEDS_COUNT; mi++) {
                uint32_t m = MODES[mi], glow = m == LEDS_DIM || m == LEDS_DIM_LO, keys_lit = 0, keys_dim = 0, btn_bad = 0;
                uint32_t want_dim = 0, kc, keys;
                char what[128];
                ui_power_on();
                song.sel = 0;
                if (drum) {
                    set_engine_of(TSEL, ENGI_DRUM);
                    TSEL->engine = TSEL->eng_req;
                    TSEL->p[P_SLEN] = 16;
                    TSEL->step[2] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};   /* one hit, on step 3 */
                } else {
                    TSEL->step[0] = (step_t){.note = {60}, .n = 1, .time = ST_NOTE};   /* C4 on step 1 */
                }
                TSEL->seq_active = 1;
                settings_leds = (uint8_t)m;
                open_family(FAM_SEQ);
                frame();
                for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) {
                    open_family(FAM_SEQ);
                    frame();
                }
                if (run) {
                    seq_start();
                    events_block(CTL);
                }
                ui_leds();
                for (k = 0; k < NB; k++) {                  /* a button: lit if active, else glow / dark / lit */
                    uint8_t q = led_pos[panel.btn[k]];
                    uint32_t lit = (fm1_led[q >> 3] >> (q & 7u)) & 1u, dim = (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u;
                    int active = k == B_SEQ || (k == B_PLAY && run);
                    btn_bad += m == LEDS_INV ? lit == (uint32_t)active || dim :
                               active ? (k == B_PLAY ? lit || dim : !lit) : lit || dim != glow;
                }
                keys = key_leds(0);
                for (k = 0; k < 27u; k++) {
                    uint8_t q = led_pos[14u + k];
                    keys_lit += (fm1_led[q >> 3] >> (q & 7u)) & 1u;
                    keys_dim += ((fm1_led_dim[q >> 3] & ~fm1_led[q >> 3]) >> (q & 7u)) & 1u;
                }
                kc = (uint32_t)__builtin_popcount(keys);
                if (glow)
                    want_dim = (uint32_t)__builtin_popcount(0x7FFFFFFu & ~keys);   /* (Dah: the DRUM grid glows as any page) */
                snprintf(what, sizeof what, "STEP %s, %s, LEDS %s: buttons and keys as the mode says (%u lit, %u glow)",
                         drum ? "DRUM grid" : "piano roll", run ? "playing" : "stopped", LEDS_NAME[m],
                         (unsigned)keys_lit, (unsigned)keys_dim);
                bad += check(what, !btn_bad && host_dim_lo == (m == LEDS_DIM_LO) &&
                                       grid_on() == (int)drum && (!run || kc) &&
                                       keys_dim == want_dim && keys_lit == (m == LEDS_INV && !drum ? 27u - kc : kc) &&
                                       (!glow || keys_lit + keys_dim >= 25u) &&
                                       ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) == run);
                bad += check("  the mid level: the grid's beats in DIM HI / LO only, never a lit key",
                             lit_keys(fm1_led_mid) == 0u &&
                             !(lit_keys(fm1_led_mid) & lit_keys(fm1_led)));
                if (run) {
                    transport_req = 2;
                    events_block(CTL);
                }
            }
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, outside the keys' own maps the keys of the selected track's scale glow
 * (the others dark), the root's at the mid level; the keys held / playing lit over it; LEDS OFF / DIM LO / DIM HI
 * alike, INV turned round (root lit, scale mid, the others glowing, the active dark); TRN; QNT WHITE: the white
 * keys; a DRUM track, a layer, the default OFF: as before */
/* MENU > DISPLAY > SCREEN OFF (1.1.5, ui.c scr_*): the screen and its backlight off after NEVER / 5 / 15 / 30 (default)
 * / 60 minutes without panel input; the press, key or turn that wakes it does nothing else; the dialogs, the count-in
 * and the UPDATE MODE countdown keep it on and wake it; the sound goes on as if it were on (forked sessions, bit for
 * bit); the setting saved (a spare favorites byte, 0 = NEVER since 1.1.5.1) and offered in the MENU */
static void scr_idle(uint32_t ms)                  /* nobody touches it for ms (a frame a minute, then frames) */
{
    while (ms > 60000u) { fm1_ms += 60000u - 16u; host_ticks += 60000u * 1000u; frame(); ms -= 60000u; }
    frames(ms);
}
static int scr_woke(uint32_t from)                 /* since host_pw_n was `from`: SLPOUT >= 120 ms after the SLPIN,
                                                    * DISPON + backlight >= 120 ms after the SLPOUT, nothing else */
{
    return host_pw_n == from + 2u && host_pw[from] == 1u && host_pw[from + 1u] == 2u && host_bl &&
           host_pw_ms[from] - host_pw_ms[from - 1u] >= 120u && host_pw_ms[from + 1u] - host_pw_ms[from] >= 120u &&
           scrn.st == SCR_ON;
}
static uint64_t scr_session(uint32_t setting, uint32_t *flags)
{
    int32_t o[2u * CTL];
    uint64_t h = 1469598103934665603ull, e = 0;
    uint32_t f, b, i, off = 0;
    ui_power_on();
    scr_put(setting);
    song.master_q12 = 4096;
    my_steps(&trk[0]);
    transport_req = 1;
    for (f = 0; f < 400u; f++) {
        if (f == 50u)
            fm1_ms += 6u * 60000u;                 /* six minutes untouched: 5 MIN goes dark */
        frame();
        off |= !host_bl;
        for (b = 0; b < 4u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
        }
    }
    *flags = off | (song.playing ? 2u : 0u) | (e ? 4u : 0u);
    stop_transport();
    return h;
}
static uint64_t scr_session_fork(uint32_t setting, uint32_t *flags)   /* from the same state each time */
{
    int fd[2];
    uint64_t r[2] = {0, 0};
    pid_t p;
    if (pipe(fd))
        return 0;
    p = fork();
    if (!p) {
        uint32_t fl = 0;
        r[0] = scr_session(setting, &fl);
        r[1] = fl;
        if (write(fd[1], r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r) r[0] = 0;
    close(fd[0]);
    waitpid(p, 0, 0);
    *flags = (uint32_t)r[1];
    return r[0];
}
/* 1.2 (Discussions #72 / #73): MOMENTARY, LFO held. KNOB 1..4 on HOME and the sound / FX pages change their values as
 * ever and LFO let go puts each back (no LFO page then; LFO alone still opens it); OCT+ with LFO held keeps them (no
 * octave); not recorded as automation; a sound load meanwhile puts the values back first; the knobs on STEP / action pages / lists act as ever; a layer's knobs are the layer's; up to 8 values */
static int test_momentary(void)
{
    int bad = 0, ok;
    uint32_t id0, id1, k;
    int16_t v0, v1, g0;
    ui_power_on();
    ui.home = 1;
    id0 = ENGINES[TSEL->eng_req]->knob[0];
    id1 = ENGINES[TSEL->eng_req]->knob[1];
    v0 = TSEL->p[id0];
    v1 = TSEL->p[id1];
    btn_down(B_LFO); frame();
    turn(EN_K1, 6);
    turn(EN_K2, -4);
    ok = TSEL->p[id0] != v0 && TSEL->p[id1] != v1 && mom.n == 2u && msg_is("MOMENTARY  [OCT+] KEEP");
 ui_leds();
    btn_up(B_LFO); frame();
    bad += check("MOMENTARY: LFO held, KNOB 1 / 2 on HOME change their values; LFO let go puts them back (no LFO page)",
                 ok && TSEL->p[id0] == v0 && TSEL->p[id1] == v1 && !mom.n && ui.home && msg_is("BACK"));
    press(B_LFO);
    bad += check("  LFO alone still opens its page", !ui.home && cur_page()->fam == FAM_LFO);
    go_home();
    turn(EN_K1, 2);
    bad += check("  without LFO a turn stays", TSEL->p[id0] != v0 && !mom.n);
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 5);
    k = (uint32_t)song.octave;
    press(B_OCTUP);
    ok = !mom.n && msg_is("KEPT") && song.octave == (int8_t)k;
    btn_up(B_LFO); frame();
    bad += check("  OCT+ with LFO held keeps them (no octave shift), LFO let go then changes nothing",
                 ok && TSEL->p[id0] != v0 && ui.home);
    /* an FX page (a global) and an engine page */
    TSEL->p[P_REV] = 40;                                 /* (Dah: REVERB, FX's: SEND TYPE SIZE DAMP) */
    go_title("REVERB"); frame();
    g0 = song.g[G_RSIZE];
    btn_down(B_LFO); frame();
    turn(EN_K3, 9);
    ok = song.g[G_RSIZE] != g0;
    btn_up(B_LFO); frame();
    bad += check("  a global FX value (REVERB SIZE) too: back on release", ok && song.g[G_RSIZE] == g0 && cur_page()->scope == SC_GLOBAL);
    /* not recorded as automation; recording a plain turn still works */
    go_home();
    song.playing = 1; song.rec = 1u << song.sel;
    k = motion.count;
    btn_down(B_LFO); frame();
    turn(EN_K1, 7);
    ok = motion.count == k;
    btn_up(B_LFO); frame();
    song.playing = 0; song.rec = 0;
    bad += check("  a momentary turn is not recorded as automation (armed and playing)", ok);
    /* a sound load meanwhile: the values back first, then the load (its sound stays) */
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    turn(EN_K1, 8);
    turn(EN_PRESET, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    (void)v0;
    bad += check("  a sound load with LFO held: the values back first, the load stays", ok);
    /* STEP: the knobs edit the step as ever (no momentary) */
    for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) { open_family(FAM_SEQ); frame(); }
    btn_down(B_LFO); frame();
    turn(EN_K2, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    bad += check("  on STEP (and the action pages, the lists) the knobs act as ever", ok && cur_page()->graph == GR_ROLL);
    /* #195: MIXER's LEVEL PAN REV MUTE (the selected track's) are momentary too, as on HOME LEVELS and the FX page */
    go_page(GR_TRK); frame();
    {
        int16_t lv = TSEL->p[P_LEVEL], pn = TSEL->p[P_PAN], rv = TSEL->p[P_SPRD], mu = TSEL->p[P_MUTE];
        song.playing = 1; song.rec = 1u << song.sel;
        k = motion.count;
        btn_down(B_LFO); frame();
        turn(EN_K1, -5);
        turn(EN_K2, 7);
        turn(EN_K3, 9);
        turn(EN_K4, 1);
        ok = TSEL->p[P_LEVEL] != lv && TSEL->p[P_PAN] != pn && TSEL->p[P_SPRD] != rv && TSEL->p[P_MUTE] != mu &&
             mom.n == 4u && motion.count == k;
        btn_up(B_LFO); frame();
        song.playing = 0; song.rec = 0;
        ok &= TSEL->p[P_LEVEL] == lv && TSEL->p[P_PAN] == pn && TSEL->p[P_SPRD] == rv && TSEL->p[P_MUTE] == mu &&
              !mom.n && cur_page()->graph == GR_TRK && msg_is("BACK");
        turn(EN_K3, 4);                            /* (without LFO: a turn stays) */
        ok &= TSEL->p[P_SPRD] != rv && !mom.n;
        TSEL->p[P_SPRD] = rv;
        bad += check("  #195 MIXER: LEVEL PAN REV MUTE momentary (not recorded), back on release; without LFO they stay", ok);
    }
    /* a layer: its knobs */
    go_home();
    v0 = TSEL->p[id0];
    btn_down(B_LFO); frame();
    btn_down(B_GLO); frame();
    turn(EN_K1, 3);
    ok = !mom.n && TSEL->p[id0] == v0;
    btn_up(B_GLO); frame();
    btn_up(B_LFO); frame();
    bad += check("  with a layer's button held the knobs are the layer's", ok);
    /* at most MOM_N values */
    go_home(); frames(400);                        /* (#39: the layer let go, the knobs quiet a moment) */
    btn_down(B_LFO); frame();
    for (k = 0; k < MOM_N; k++) {
        mom.vp[k] = &trk[k % NTRK].p[P_LEVEL + k / NTRK]; mom.v[k] = *mom.vp[k];
    }
    mom.n = MOM_N;
    v0 = TSEL->p[id0];
    ui.msg_t = 0;
    turn(EN_K1, -3);
    ok = TSEL->p[id0] == v0 && msg_is("MOMENTARY FULL");
    btn_up(B_LFO); frame();
    bad += check("  at most 8 values: MOMENTARY FULL, the turn does nothing", ok && !mom.n);
    return bad;
}
/* 1.2 (Discussion #135): MENU > ANIM IDLE, the LEDs' idle animation (ui_input.c idle_leds; the picture and the scan:
 * tests/input_test.c). After a minute without panel input it starts (ui_leds then writes none), with the glow as LEDS
 * says; a key or a button held, a knob turned end it and act as ever (the turn changes its value, the key plays);
 * never while playing, armed, in a layer, the menu, a dialog or NAME, with a MIDI note lit, with ANIM ON or OFF; kept
 * with the settings ([15][10] bit 0) */
static int test_idle_leds(void)
{
    int bad = 0, ok;
    int16_t v;
    persist_t p;
    ui_power_on();
    host_idle = host_anim = 0;
    menu_put(MI_ANIM, 2);
    settings_leds = LEDS_DIM;
    frames(160);
    fm1_ms += IDLE_LEDS_MS - 1000u; frame(); ui_leds();
    ok = !host_idle;
    fm1_ms += 1000u; frame(); ui_leds();
    bad += check("ANIM IDLE: the idle animation after a minute without input (not before), with the glow (LEDS DIM)",
                 ok && host_idle && host_idle_glow);
    memset(fm1_led, 0x5A, sizeof fm1_led);
    ui_leds();
    bad += check("  it has the LEDs: ui_leds writes none", fm1_led[0] == 0x5A && fm1_led[3] == 0x5A);
    v = TSEL->p[ENGINES[TSEL->eng_req]->knob[0]];
    ui.home = 1;
    turn(EN_K1, 3); ui_leds();
    bad += check("  a knob turned ends it at once and is not swallowed (the value turns)", !host_idle &&
                 TSEL->p[ENGINES[TSEL->eng_req]->knob[0]] != v && fm1_led[0] != 0x5A);
    frame(); fm1_ms += IDLE_LEDS_MS; frame(); ui_leds();   /* (the turn counted as input: a minute from it) */
    ok = host_idle;
    fm1_in.notes = 1u << 4; host_notes |= 1u << 4; frame(); ui_leds();
    ok &= !host_idle;
    fm1_in.notes = 0; frame(); ui_leds();
    ok &= !host_idle;
    fm1_ms += IDLE_LEDS_MS - 100u; frame(); ui_leds();
    bad += check("  a key held ends it (the scan does at once) and counts as input: a minute again", ok && !host_idle);
    fm1_ms += 200u; frame(); ui_leds();
    ok = host_idle;
    song.playing = 1; frame(); ui_leds();
    ok &= !host_idle;
    song.playing = 0; song.rec = 1; frame(); ui_leds();
    ok &= !host_idle;
    song.rec = 0; ui.menu = 1; frame(); ui_leds();
    ok &= !host_idle;
    ui.menu = 0; ui.confirm = CF_INIT_SOUND; frame(); ui_leds();
    ok &= !host_idle;
    ui.confirm = 0; ui.lock = LAYER_GLO; ui.layer = LAYER_GLO; ui_leds();
    ok &= !host_idle;
    ui.lock = 0; ui.layer = 0; fm1_ms += IDLE_LEDS_MS; ui_leds();   /* (a dialog counts as input, as for SCREEN OFF) */
    ok &= host_idle;
    bad += check("  never while playing, armed, in the menu, a dialog or a layer; back when they end", ok);
    menu_put(MI_ANIM, 0); ui_leds();
    ok = !host_idle;
    menu_put(MI_ANIM, 1); ui_leds();
    ok &= !host_idle;
    menu_put(MI_ANIM, 2); settings_leds = LEDS_OFF; ui_leds();
    bad += check("  ANIM ON / OFF: none; LEDS OFF: no glow", ok && host_idle && !host_idle_glow);
    settings_export(&p);
    ok = p.favorites.factory[15][10] == 1u && !(p.favorites.factory[15][30] & PREF_ANIM_OFF);
    p.favorites.factory[15][10] = 0;
    ok &= settings_import(&p, sizeof p) == 1 && menu_get(MI_ANIM) == 0u;
    bad += check("  kept with the settings ([15][10] bit 0; 0, every older setting: ANIM ON)", ok);
    host_idle = 0;
    ui_idle = 0;
    settings_leds = LEDS_DIM;
    return bad;
}
static int test_screen_off(void)
{
    int bad = 0, ok;
    uint32_t n, rows, i;
    int16_t *vp, v0;

    ui_power_on();
    ok = ui_scr == 0u && scr_get() == 0u && SCR_MIN[scr_get()] == 0u && str_eq(MI_NAME[MI_SCROFF], "SCREEN OFF") &&
         MI_TAB[MI_SCROFF] == MTAB_DISPLAY && menu_n(MI_SCROFF) == 5u && str_eq(menu_vname(MI_SCROFF, 0), "NEVER") &&
         str_eq(menu_vname(MI_SCROFF, 1), "5 MIN") && str_eq(menu_vname(MI_SCROFF, 2), "15 MIN") &&
         str_eq(menu_vname(MI_SCROFF, 3), "30 MIN") && str_eq(menu_vname(MI_SCROFF, 4), "60 MIN") &&
         menu_get(MI_SCROFF) == 0u;
    bad += check("SCREEN OFF: MENU > DISPLAY row, NEVER 5 15 30 60 MIN, NEVER by default (stored 0, 1.1.5.1)", ok);
    menu_put(MI_SCROFF, 3u);                         /* 30 MIN for what follows */

    host_pw_n = 0;
    scr_idle(30u * 60000u - 2000u);
    ok = host_pw_n == 0u && host_bl && scrn.st == SCR_ON;
    scr_idle(2000u);
    ok &= host_pw_n == 1u && host_pw[0] == 0u && !host_bl && scrn.st == SCR_OFF && kb_asleep;
    rows = host_blit_rows;
    ui_message("SAVED");
    scr_idle(5000u);
    ok &= host_blit_rows == rows && host_pw_n == 1u && !ui.msg_t;
    bad += check("SCREEN OFF: 30 MIN untouched -> DISPOFF + SLPIN once; dark: nothing drawn", ok);

    n = host_pw_n;
    ok = ui.home && !ui.menu;
    btn_down(B_ENV); frame();                         /* a page button wakes it, does not open its page */
    ok &= scrn.st == SCR_SLPOUT && host_pw_n == n + 1u && host_pw[n] == 1u;   /* (SLPIN long ago: SLPOUT at once) */
    frames(100u);
    ok &= host_pw_n == n + 1u && host_blit_rows == rows;   /* (dark until 120 ms after it) */
    rows = host_blit_rows;
    frames(200u);
    ok &= scr_woke(n) && host_blit_rows - rows >= 200u;   /* (the frame before DISPON redrew it all) */
    btn_up(B_ENV); frame(); frames(100u);
    ok &= ui.home && !ui.menu && !ui.layer && !scrn.eat && !kb_asleep;
    press(B_ENV);                                     /* awake: the button works again */
    ok &= !ui.home && cur_page()->fam == FAM_ENV;
    bad += check("SCREEN OFF: a button wakes it (SLPOUT, 120 ms, the page redrawn, DISPON), swallowed", ok);

    go_home();
    for (i = 0; i < 3u; i++) {                        /* HOME, SAVE, REC held across the wake: no menu, page, clear */
        static const uint32_t B[3] = {B_HOME, B_SAVE, B_REC};
        scr_idle(31u * 60000u);
        n = host_pw_n;
        ok = !host_bl;
        hold(B[i]); frames(300u);
        ok &= scr_woke(n) && ui.home && !ui.menu && !ui.confirm && !song.rec && !song.playing && !transport_req;
        bad += check(i == 0 ? "SCREEN OFF: HOME held to wake: no menu, no HOME tap after" :
                     i == 1 ? "SCREEN OFF: SAVE held to wake: no SAVE page" :
                              "SCREEN OFF: REC held to wake: nothing armed, no clear, no transport", ok);
    }

    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl && !gates();
    key_down(white(0)); frame();                      /* a key: wakes it, no note (seq.c kb_asleep) */
    ok &= !gates();
    frames(300u);
    ok &= scr_woke(n) && !gates();
    key_up(white(0)); frame(); frames(50u);
    ok &= !gates() && !kb_asleep;
    key_down(white(0)); frame();
    ok &= gates() == 1u;                              /* awake: the key plays */
    key_up(white(0)); frame();
    bad += check("SCREEN OFF: a key wakes it silently (no note on, none off), the next one plays", ok);

    scr_idle(31u * 60000u);
    n = host_pw_n;
    home_param(0, &vp);
    v0 = *vp;
    for (i = 0; i < 20u; i++) turn(EN_K1, 1);        /* a knob turned to wake it, still turning (on past the wake) */
    ok = scr_woke(n) && *vp == v0;
    turn(EN_PRESET, 1); turn(EN_ALGO, 1); turn(EN_SELECT, 1); turn(EN_K4, -1);
    ok &= *vp == v0 && song.sel == 0u && song.g[G_BPM] == GP[G_BPM].def && scrn.eat;
    frames(400u);                                     /* at rest SCR_EAT_MS: the knobs are the page's again */
    turn(EN_K1, 1);
    ok &= *vp != v0;
    bad += check("SCREEN OFF: a knob wakes it; turns until it rests 0.3 s change nothing (K1..4, PRESETS, ALGO, SELECT)", ok);

    menu_put(MI_SCROFF, 0u);                         /* NEVER */
    n = host_pw_n;
    scr_idle(3u * 3600000u);
    ok = host_pw_n == n && host_bl && ui_scr == 0u;
    menu_put(MI_SCROFF, 1u);                         /* 5 MIN */
    scr_idle(4u * 60000u + 50000u);
    ok &= host_pw_n == n && host_bl;
    scr_idle(15000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    press(B_PLAY); frames(300u);                      /* PLAY wakes it, does not start the transport */
    ok &= scr_woke(n + 1u) && !song.playing && !transport_req;
    bad += check("SCREEN OFF: NEVER stays on for hours; 5 MIN goes dark after 5; PLAY to wake starts nothing", ok);

    n = host_pw_n;
    ui.confirm = CF_INIT_SOUND; ui.force = 1;          /* a dialog: never dark */
    scr_idle(20u * 60000u);
    ok = host_pw_n == n && host_bl;
    ui.confirm = 0; ui.force = 1;
    cin_left = 1000u;                                 /* the count-in */
    scr_idle(20u * 60000u);
    ok &= host_pw_n == n && host_bl;
    cin_left = 0;
    scr_idle(6u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    ui.confirm = CF_INIT_SOUND; ui.force = 1;          /* one opening while dark (the editor, MIDI): it wakes */
    frames(300u);
    ok &= scr_woke(n + 1u) && ui.confirm == CF_INIT_SOUND;
    ui.confirm = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    ui.uboot = 3; ui.force = 1;                       /* the UPDATE MODE countdown */
    frames(300u);
    ok &= scr_woke(n);
    ui.uboot = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    scr_wake_now();                                   /* main.c: the crash screen, UPDATE, UBOOT */
    ok &= host_pw_n == n + 1u && host_pw[n] == 3u && host_bl && scrn.st == SCR_ON;
    bad += check("SCREEN OFF: a dialog, the count-in, UPDATE MODE keep it on and wake it; the crash screen at once", ok);

    {   /* the setting with the settings: SCR_CODE in favorites.factory[15][27]; 1.1.5's codes: 0 (its 30 MIN, its
         * default) now NEVER, 3 (its NEVER) NEVER, 2 1 7 as they were; 30 MIN is 4 now; unknown NEVER */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        menu_put(MI_SCROFF, 4u);
        settings_export(&p); ui_scr = 0;
        ok = p.favorites.factory[15][27] == 7u && settings_import(&p, sizeof p) && scr_get() == 4u;
        p.favorites.factory[15][27] = 0;
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 3;              /* 1.1.5's NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        p.favorites.factory[15][27] = 2;              /* 5 MIN, as on 1.1.5 */
        ok &= settings_import(&p, sizeof p) && scr_get() == 1u;
        menu_put(MI_SCROFF, 3u); ok &= ui_scr == 4u && scr_get() == 3u;
        p.favorites.factory[15][27] = 6;              /* unknown: NEVER */
        ok &= settings_import(&p, sizeof p) && scr_get() == 0u;
        ui_scr = 0x55; ok &= scr_get() == 0u;
        bad += check("SCREEN OFF: saved with the settings (0, 1.1.5's 30 MIN, and unknown = NEVER)", ok);
    }

    ui_power_on();                                    /* in the MENU: OCT+ / a knob step it */
    menu_put(MI_SCROFF, 3u);
    hold(B_HOME);
    for (i = 0; i < 5u; i++) turn(EN_PRESET, 1);
    ok = ui.menu == 1 && ui.menu_sel == MI_SCROFF;
    press(B_OCTUP); ok &= scr_get() == 4u;
    press(B_OCTUP); ok &= scr_get() == 4u;           /* (stops at the end) */
    for (i = 0; i < 4u; i++) turn(EN_K2, -1);
    ok &= scr_get() == 0u;
    press(B_OCTDN); ok &= scr_get() == 0u && ui.menu == 1;
    press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); ok &= scr_get() == 3u;
    press(B_HOME); ok &= ui.menu == 0;
    bad += check("SCREEN OFF: the MENU row steps NEVER .. 60 MIN (OCT-/+, any knob), stops at the ends", ok);

    {
        uint32_t f0 = 0, f1 = 0;
        uint64_t a = scr_session_fork(0u, &f0), b = scr_session_fork(1u, &f1);
        bad += check("SCREEN OFF: the sound bit for bit as with NEVER while dark (sequencer playing)",
                     a && a == b && (f0 & 6u) == 6u && (f1 & 6u) == 6u && !(f0 & 1u) && (f1 & 1u));
    }
    ui_power_on();
    return bad;
}

/* 1.5.5: MENU > MIDI > SCREEN WAKE (ui.c scr_input, PREF_WAKE_PANEL: bit 5 of ui_prefs2). +MIDI (the default, 0 in every
 * older setting): a MIDI IN note-on (USB or TRS, a channel that plays a track) keeps the screen on and wakes it, and plays
 * (not swallowed as a panel key is); clock, CCs, bend, note-offs, SysEx and channels nobody hears do nothing to it.
 * PANEL: the panel only, as up to 1.5.1 */
static void sw_usb(uint32_t cin, uint32_t b0, uint32_t b1, uint32_t b2)   /* a USB-MIDI event packet in, the ISR run */
{
    midi_in_event(cin | b0 << 8 | b1 << 16 | b2 << 24);
    events_block(32);
}
static void sw_trs(const uint8_t *b, uint32_t n)       /* TRS MIDI IN bytes, the ISR run */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        um_byte(b[i]);
    events_block(32);
}
static void sw_noise(void)                             /* everything that is not a note-on */
{
    static const uint8_t TRS[] = {0xF8, 0xB0, 7, 90, 0xF8, 0xE0, 0, 70, 0xF0, 0x7D, 1, 2, 0xF7, 0x9A, 60, 100, 0xF8};
    uint32_t i;
    for (i = 0; i < 48u; i++)                          /* two beats of USB clock (clock in: MIDI USB) */
        sw_usb(0xF, 0xF8, 0, 0);
    sw_usb(0xB, 0xB0, 1, 64);                          /* CC1, CC64 */
    sw_usb(0xB, 0xB0, 64, 127);
    sw_usb(0xB, 0xB0, 64, 0);
    sw_usb(0xE, 0xE0, 0, 80);                          /* bend */
    sw_usb(0xD, 0xD0, 50, 0);                          /* aftertouch */
    sw_usb(0x4, 0xF0, 0x7D, 0x01);                     /* SysEx (not the soft key) */
    sw_usb(0x6, 0x02, 0xF7, 0);
    sw_usb(0x9, 0x9A, 60, 100);                        /* channel 11: no track hears it (MIDI IN CH1-8) */
    sw_usb(0x8, 0x80, 61, 0);                          /* a note-off, and a note-on of velocity 0 */
    sw_usb(0x9, 0x90, 61, 0);
    sw_trs(TRS, sizeof TRS);                           /* TRS: clock, CC7, bend, SysEx, channel 5 */
}
static int test_screen_wake(void)
{
    static const uint8_t TRS_ON[] = {0x90, 64, 100}, TRS_OFF[] = {0x80, 64, 0};
    int bad = 0, ok;
    uint32_t n, i;

    ui_power_on();
    ok = !(ui_prefs2 & 0x20u) && menu_get(MI_SCRWAKE) == 0u && str_eq(MI_NAME[MI_SCRWAKE], "SCREEN WAKE") &&
         MI_TAB[MI_SCRWAKE] == MTAB_MIDI && menu_n(MI_SCRWAKE) == 2u && str_eq(menu_vname(MI_SCRWAKE, 0), "+MIDI") &&
         str_eq(menu_vname(MI_SCRWAKE, 1), "PANEL") && PREF_WAKE_PANEL == (0x20u << 16) && !MI_PROJECT(MI_SCRWAKE) &&
         menu_valued(MI_SCRWAKE);
    menu_put(MI_SCRWAKE, 1u);
    ok &= ui_prefs2 == 0x20u && menu_get(MI_SCRWAKE) == 1u;
    menu_put(MI_SCRWAKE, 0u);
    ok &= ui_prefs2 == 0u;
    bad += check("SCREEN WAKE: MENU > MIDI row, +MIDI (default, bit 5 of ui_prefs2 clear) / PANEL", ok);

    {   /* the settings: every older record reads +MIDI; PANEL kept */
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        menu_put(MI_SCRWAKE, 1u);
        settings_export(&p);
        ok = p.favorites.factory[15][24] == 0x20u;
        ui_prefs2 = 0;
        ok &= settings_import(&p, sizeof p) && menu_get(MI_SCRWAKE) == 1u;
        p.favorites.factory[15][24] = 0x1Fu;          /* 1.5.5 before SCREEN WAKE: every other flag of the byte set */
        ok &= settings_import(&p, sizeof p) && menu_get(MI_SCRWAKE) == 0u && (ui_prefs2 & 0x10u);
        memset(&p.favorites, 0, sizeof p.favorites);   /* 1.0 .. 1.5.1: the byte 0 */
        ok &= settings_import(&p, sizeof p) && menu_get(MI_SCRWAKE) == 0u;
        p.magic = 0x50455233u;                         /* PER3 (no favorites at all) */
        ui_prefs2 = 0x20u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && menu_get(MI_SCRWAKE) == 0u;
        bad += check("SCREEN WAKE: older settings (the byte 0, PER3) read +MIDI; PANEL saved and read back", ok);
    }

    ui_power_on();
    menu_put(MI_SCROFF, 3u);                           /* 30 MIN */
    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl && scrn.st == SCR_OFF && kb_asleep && !gates();
    sw_usb(0x9, 0x90, 60, 100);                        /* USB note-on, channel 1 = T1: it plays at once */
    ok &= gates() == 1u && midi_seen == 1u;
    frame();
    ok &= scrn.st == SCR_SLPOUT && host_pw_n == n + 1u && host_pw[n] == 1u && gates() == 1u;
    frames(300u);
    ok &= scr_woke(n) && gates() == 1u && !midi_seen;   /* (midi_follow had it as before: the light) */
    sw_usb(0x8, 0x80, 60, 0);
    ok &= !gates();
    frames(50u);
    ok &= !kb_asleep && !scrn.eat;
    key_down(white(0)); frame();                       /* the panel as ever: a key plays */
    ok &= gates() == 1u;
    key_up(white(0)); frame();
    bad += check("SCREEN WAKE +MIDI: a USB note-on wakes it (SLPOUT, DISPON) and plays (not swallowed); keys after play", ok);

    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl;
    sw_trs(TRS_ON, sizeof TRS_ON);                     /* TRS note-on */
    ok &= gates() == 1u;
    frames(300u);
    ok &= scr_woke(n) && gates() == 1u;
    sw_trs(TRS_OFF, sizeof TRS_OFF);
    ok &= !gates();
    bad += check("SCREEN WAKE +MIDI: a TRS note-on wakes it and plays", ok);

    frames(50u);
    n = host_pw_n;
    scr_idle(29u * 60000u);
    sw_usb(0x9, 0x90, 60, 100); frame();               /* a note at 29 minutes: the time counts from it */
    ok = scrn.idle == fm1_ms && host_bl;
    sw_usb(0x8, 0x80, 60, 0);
    scr_idle(29u * 60000u);
    ok &= host_pw_n == n && host_bl;
    for (i = 0; i < 3u; i++) {                         /* notes keep it on for hours */
        sw_trs(TRS_ON, sizeof TRS_ON); frame(); sw_trs(TRS_OFF, sizeof TRS_OFF);
        scr_idle(25u * 60000u);
    }
    ok &= host_pw_n == n && host_bl;
    scr_idle(6u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    bad += check("SCREEN WAKE +MIDI: a note-on resets the idle time (on for hours while played; dark 30 MIN after the last)", ok);

    song.g[G_CLOCK] = 1;                               /* (clock in from USB: the pulses are taken) */
    n = host_pw_n;
    for (i = 0; i < 5u; i++) { sw_noise(); frames(100u); }
    ok = host_pw_n == n && !host_bl && scrn.st == SCR_OFF && !gates();
    sw_usb(0x9, 0x90, 60, 100); frames(300u);
    ok &= scr_woke(n);
    sw_usb(0x8, 0x80, 60, 0);
    n = host_pw_n;
    scr_idle(25u * 60000u);
    for (i = 0; i < 24u; i++) { sw_noise(); scr_idle(15000u); }   /* six minutes of clock, CCs, SysEx while on */
    ok &= host_pw_n == n + 1u && !host_bl;            /* (dark at 30 minutes all the same) */
    song.g[G_CLOCK] = 0;
    bad += check("SCREEN WAKE +MIDI: clock, CCs, bend, aftertouch, note-offs, SysEx, unheard channels neither wake it nor keep it on", ok);

    menu_put(MI_SCRWAKE, 1u);                          /* PANEL: as up to 1.5.1 */
    n = host_pw_n;
    ok = !host_bl;
    sw_usb(0x9, 0x90, 60, 100); frames(300u);
    ok &= host_pw_n == n && !host_bl && scrn.st == SCR_OFF && gates() == 1u;   /* (the note plays in the dark) */
    sw_usb(0x8, 0x80, 60, 0);
    sw_trs(TRS_ON, sizeof TRS_ON); frames(300u); sw_trs(TRS_OFF, sizeof TRS_OFF);
    ok &= host_pw_n == n && !host_bl && !gates();
    key_down(white(0)); frame();                       /* a key wakes it, swallowed */
    ok &= !gates();
    frames(300u);
    ok &= scr_woke(n) && !gates();
    key_up(white(0)); frame(); frames(50u);
    n = host_pw_n;
    scr_idle(29u * 60000u);
    sw_usb(0x9, 0x90, 60, 100); frame(); sw_usb(0x8, 0x80, 60, 0);
    scr_idle(2u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;            /* (a note kept nothing on) */
    bad += check("SCREEN WAKE PANEL: MIDI notes play but neither wake it nor keep it on; a key wakes it, swallowed", ok);

    ui_power_on();                                     /* in the MENU: OCT+ / a knob, up is +MIDI */
    hold(B_HOME);
    ui.menu_sel = MI_SCRWAKE; ui.force = 1; frame();
    ok = ui.menu == 1 && menu_get(MI_SCRWAKE) == 0u;
    press(B_OCTUP); ok &= menu_get(MI_SCRWAKE) == 0u;  /* (stops at the end) */
    press(B_OCTDN); ok &= menu_get(MI_SCRWAKE) == 1u && (ui_prefs2 & 0x20u);
    press(B_OCTDN); ok &= menu_get(MI_SCRWAKE) == 1u;
    turn(EN_K3, 1); ok &= menu_get(MI_SCRWAKE) == 0u && !(ui_prefs2 & 0x20u);
    turn(EN_K3, -1); ok &= menu_get(MI_SCRWAKE) == 1u;
    press(B_OCTUP); ok &= menu_get(MI_SCRWAKE) == 0u;
    press(B_HOME); ok &= ui.menu == 0;
    bad += check("SCREEN WAKE: the MENU row steps PANEL / +MIDI (up +MIDI), stops at the ends", ok);

    ui_power_on();                                     /* +MIDI: one note with the menu up, or in NAME, then nothing */
    menu_put(MI_SCROFF, 3u);                           /* 30 MIN */
    hold(B_HOME);
    n = host_pw_n;
    sw_usb(0x9, 0x90, 60, 100); frame(); sw_usb(0x8, 0x80, 60, 0);
    ok = ui.menu && !midi_seen && scrn.idle == fm1_ms;
    scr_idle(31u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl && scrn.st == SCR_OFF;   /* (dark 30 MIN after it: not kept on for ever) */
    key_down(white(0)); frame(); key_up(white(0)); frames(400u);
    ok &= scrn.st == SCR_ON;
    hold(B_HOME);
    name_open(NK_USER_SAVE, 5);
    n = host_pw_n;
    sw_usb(0x9, 0x90, 60, 100); frame(); sw_usb(0x8, 0x80, 60, 0);
    ok &= name_on() && !midi_seen;
    scr_idle(31u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    name_close();
    bad += check("SCREEN WAKE +MIDI: a note with the menu up or in NAME keeps it on 30 MIN, not for ever (its flag taken)", ok);
    ui_power_on();
    return bad;
}

static int test_scale_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    const uint32_t ALL = 0x7FFFFFFu, C_KEYS = 1u << 7 | 1u << 19;
    int bad = 0, ok;
    uint32_t id, p, r, k, mi, root = 0, sc, white = 0;
    track_t *t;
    ui_power_on();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (k = 0; k < 27u; k++)
        white |= (uint32_t)!key_black(k) << k;
    song.sel = 0;
    t = TSEL;
    t->p[P_SCALE] = 1;                                  /* C major */
    t->p[P_ROOT] = 0;
    settings_leds = LEDS_DIM;
    ui_leds();
    ok = !menu_get(MI_SCLLED) && str_eq(MI_NAME[MI_SCLLED], "SCALE LEDS") && MI_TAB[MI_SCLLED] == MTAB_CONTROL &&
         !scale_leds(&root) && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    bad += check("#127 SCALE LEDS OFF (the default): every key glows as before", ok);
    menu_put(MI_SCLLED, menu_step(MI_SCLLED, 1));
    ok = menu_get(MI_SCLLED) == 1u && (ui_rec_prefs & 0x40u) && !ui_prefs && str_eq(menu_vname(MI_SCLLED, 1), "ON");
    sc = scale_leds(&root);
    bad += check("#127 MENU > CONTROL > SCALE LEDS ON (a bit of its own); C major: the white keys, the root C4 C5",
                 ok && sc == white && root == C_KEYS);
    for (mi = 0; mi < LEDS_COUNT; mi++) {
        uint32_t m = MODES[mi], held = 1u << 1 | 1u << 7;   /* F#3 (out of the scale) and C4 (the root) held */
        char what[128];
        settings_leds = (uint8_t)m;
        fm1_in.notes = 0;
        ui_leds();
        if (m == LEDS_INV)
            ok = lit_keys(fm1_led) == C_KEYS && lit_keys(fm1_led_mid) == (white & ~C_KEYS) &&
                 lit_keys(fm1_led_dim) == (ALL & ~white);
        else
            ok = !lit_keys(fm1_led) && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        fm1_in.notes = held;
        ui_leds();
        if (m == LEDS_INV)
            ok &= lit_keys(fm1_led) == (C_KEYS & ~held) && !(lit_keys(fm1_led_mid) & held) &&
                  !(lit_keys(fm1_led_dim) & held) && lit_keys(fm1_led_mid) == (white & ~C_KEYS);
        else
            ok &= lit_keys(fm1_led) == held && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        snprintf(what, sizeof what, "#127 LEDS %s: %s; the keys held %s", LEDS_NAME[m],
                 m == LEDS_INV ? "root lit, the scale mid, the others glow" : "the scale glows, the root brighter",
                 m == LEDS_INV ? "dark" : "lit over it");
        bad += check(what, ok);
    }
    fm1_in.notes = 0;
    settings_leds = LEDS_DIM;
    t->p[P_ROOT] = 2;                                   /* D major: F# and C# in, F and C out, D the root */
    sc = scale_leds(&root);
    ok = (sc >> 1 & 1u) && (sc >> 8 & 1u) && !(sc & 1u) && !(sc >> 7 & 1u) && root == (1u << 9 | 1u << 21) &&
         __builtin_popcount(sc) == 16;
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 2;                                  /* C major, TRN +2: F3 plays G3 (in), A#3 plays C4 (the root) */
    sc = scale_leds(&root);
    ok &= (sc & 1u) && !(sc >> 1 & 1u) && root == (1u << 5 | 1u << 17);
    t->p[P_TRANS] = 0;
    bad += check("#127 the keys as they play: ROOT D, TRN +2", ok);
    t->p[P_QUANT] = QN_SNAP;                            /* SNAP: the keys' own notes (the others round down) */
    sc = scale_leds(&root);
    ok = sc == white && root == C_KEYS;
    t->p[P_QUANT] = QN_WHITE;                           /* WHITE, PEN: every white key; a root every 5 white keys */
    t->p[P_SCALE] = 5;
    sc = scale_leds(&root);
    ok &= sc == white && root == (1u << 7 | 1u << 16 | 1u << 24);
    ui_leds();
    ok &= lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == root;
    t->p[P_QUANT] = QN_OFF;
    t->p[P_SCALE] = 0;                                  /* CHR: every key, the C keys brighter */
    sc = scale_leds(&root);
    ok &= sc == ALL && root == C_KEYS;
    bad += check("#127 QNT SNAP: the keys of the scale; QNT WHITE: all the white keys, its roots; CHR: every key", ok);
    t->p[P_SCALE] = 1;
    ui.layer = LAYER_SCL;                               /* a layer: its own map, no scale glow under it */
    ui_leds();
    ok = !lit_keys(fm1_led_mid) && !lit_keys(fm1_led_dim);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    sc = scale_leds(&root);
    ui_leds();
    ok &= !sc && !root && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    open_family(FAM_SEQ);
    frame();
    for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) {
        open_family(FAM_SEQ);
        frame();
    }
    ui_leds();
    ok &= grid_on() && lit_keys(fm1_led_dim) == grid_glow() &&
          lit_keys(fm1_led_mid) == (grid_beats() & ~key_leds(0));
    bad += check("#127 a layer, a DRUM track (its keys as before) and the DRUM grid keep their own LEDs", ok);
    menu_put(MI_SCLLED, 0);
    ok = !(ui_rec_prefs & 0x40u) && !scale_leds(&root);
    bad += check("#127 SCALE LEDS OFF again: the bit clears", ok);
    ui_rec_prefs = 0;
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* #37: QNT SEQ on the SCL page: the keys as SNAP, the sequence snapped as it plays, saved with the project */
static int test_seq_quant(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    char val[16];
    const char *unit = 0;
    ui_power_on();
    song.sel = 0;
    open_family(FAM_SCL);
    frame();
    if (cur_page()->graph == GR_CHORD)               /* (Dah) the scale: SCL's second page */
        open_family(FAM_SCL), frame();
    ok = cur_page()->id[2] == P_QUANT && t->p[P_QUANT] == 0;
    turn(EN_K3, 10);
    param_format(&TP[P_QUANT], t->p[P_QUANT], val, &unit);
    ok &= t->p[P_QUANT] == 4 && str_eq(val, "SEQ+T");
    turn(EN_K3, -1);
    param_format(&TP[P_QUANT], t->p[P_QUANT], val, &unit);
    bad += check("SCL KNOB 3 QNT: OFF SNAP WHITE SEQ SEQ+T, SEQ+T last (1.5.6, #218; default OFF)",
                 ok && t->p[P_QUANT] == 3 && str_eq(val, "SEQ"));
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    ok = kb_map(t, 8) == 60u && kb_map(t, 7) == 60u;   /* C#4 key -> C4, as SNAP */
    t->step[0] = (step_t){.note = {61, 66}, .n = 2, .time = ST_NOTE};
    t->seq_active = 1;
    seq_start();
    events_block(CTL);
    ok &= t->seq_n == 2u && t->seq_notes[0] == 60u && t->seq_notes[1] == 65u && t->step[0].note[0] == 61u &&
          t->step[0].note[1] == 66u;
    transport_req = 2;
    events_block(CTL);
    bad += check("QNT SEQ: the keys snap as SNAP; C#4 F#4 of a step play C4 F4 in C major, the step keeps C#4 F#4", ok);
    project_save(1);
    t->p[P_QUANT] = 0;
    project_load(1);
    bad += check("QNT SEQ saved and loaded with the project", trk[0].p[P_QUANT] == 3);
    return bad;
}

/* #39: KNOB 1..4 with a layer's button held are the layer's only, also as it is let go: a knob turned in the frame
 * the button is let go, while its map still shows (an effect key held after FX), or just after: the page under the
 * layer (ARP: ARP MODE on) or HOME kept its values. GLO / SCL / EDIT change only what their own knobs edit */
static int16_t lk_p[NTRK][P_COUNT], lk_g[G_COUNT];
static void lk_snap(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        memcpy(lk_p[i], trk[i].p, sizeof lk_p[i]);
    memcpy(lk_g, song.g, sizeof lk_g);
}
static int lk_same(uint32_t l)                      /* nothing changed but the layer's own (l: the layer) */
{
    uint32_t i, j;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < P_COUNT; j++) {
            int own = (l == LAYER_GLO && j == P_LEVEL) ||
                      (l == LAYER_SCL && i == 0u && (j == P_ROOT || j == P_SCALE || j == P_CHRD || j == P_VOIC)) ||
                      (l == LAYER_EDIT && i == 0u && (j < P_AMODE || j > P_AORDER));   /* (a load keeps the ARP) */
            if (lk_p[i][j] != trk[i].p[j] && !own)
                return 0;
        }
    for (j = 0; j < G_COUNT; j++)
        if (lk_g[j] != song.g[j] && l != LAYER_EDIT)
            return 0;
    return 1;
}
static void lk_turn(int32_t s)                       /* KNOB 1..4 together, no frame */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        host_enc[panel.enc[EN_K1 + k]] += s * panel.dir[EN_K1 + k];
}
static int test_layer_knob_leak(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    uint32_t b, sc, pg, fails = 0, arp_fx = 0;
    int bad = 0, ok;
    for (b = 0; b < 4u; b++)
        for (sc = 0; sc < 4u; sc++)
            for (pg = 0; pg <= NPAGES; pg++) {          /* every page, HOME last */
                ui_power_on();
                if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
                frames(100);
                lk_snap();
                btn_down(LB[b]);
                if (sc == 3u) key_down(white(1));
                frame();
                lk_turn(5); frame();
                if (sc == 0u) { lk_turn(2); btn_up(LB[b]); frame(); }          /* turning as it is let go */
                if (sc == 1u) { btn_up(LB[b]); frame(); lk_turn(2); frame(); } /* .. the next frame */
                if (sc == 2u) { btn_up(LB[b]); frames(160); lk_turn(2); frame(); }   /* .. 160 ms after */
                if (sc == 3u) { btn_up(LB[b]); frame(); lk_turn(3); frame(); key_up(white(1)); frame(); }   /* the map */
                frames(400);
                ok = lk_same(LL[b]) && !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                if (!ok) {
                    fails++;
                    if (fails < 6u)
                        printf("ui:   leak: %s scenario %u page %s\n", B_NAME[LB[b]], sc,
                               pg < NPAGES ? PAGES[pg].title : "HOME");
                }
                if (b == 0u && pg < NPAGES && PAGES[pg].graph == GR_ARP && trk[0].p[P_AMODE] == lk_p[0][P_AMODE])
                    arp_fx++;
            }
    bad += check("#39 FX / GLO / SCL / EDIT + KNOB 1..4, let go while turning: no page under the layer changes",
                 !fails && arp_fx == 4u);
    /* the quiet time is short: KNOB 1 edits the page again 0.3 s after FX */
    ui_power_on();
    go_page(GR_ARP);
    {
        int16_t m = TSEL->p[P_AMODE];
        btn_down(B_FX); frame(); turn(EN_K1, 4); btn_up(B_FX); frame();
        frames(320);
        turn(EN_K1, 1);
        ok = TSEL->p[P_AMODE] == m + 1 && !perf_k[0] && str_eq(cur_page()->title, "ARP");
    }
    bad += check("  0.3 s after FX is let go KNOB 1 is ARP MODE again; a knob turned with FX opened no FX page", ok);
    /* a tap of FX with a knob turned in that frame: a combo, no FX page */
    ui_power_on();
    go_page(GR_ARP);
    btn_down(B_FX); frame(); lk_turn(1); btn_up(B_FX); frame(); frames(100);
    bad += check("  FX tapped while a knob turns: no FX page, no ARP change", str_eq(cur_page()->title, "ARP") &&
                 TSEL->p[P_AMODE] == 0);
    return bad;
}

/* #102: the TIMER5 scan counts the knobs' detents while ui_input runs; one that came after the layer's read of
 * KNOB 1..4 (host_enc_late) was read again by the page's: the layer's control and the page under it both moved
 * (HOME's KNOB 1 with FX's FILTER). Every layer (FX GLO SCL EDIT) x KNOB 1..4 x held past HOLD / a combo / locked
 * by a double tap, over every page and HOME, with late detents on the knob, PRESETS and ALGORITHM while it is open
 * and on the knob as it closes: the same state as with the detents read in time (the layer gets them all, one a
 * frame later), the layer's own control moved, and nothing under the layer changed (lk_same) */
typedef struct {
    int16_t p[NTRK][P_COUNT], g[G_COUNT];
    int8_t k[4];
    uint32_t sel, snd[NTRK], mot[NTRK];
    uint8_t home, page;
} rc_state_t;
static uint32_t rc_snd(const track_t *t) { return t->eng_req | (uint32_t)t->preset << 8 | (uint32_t)t->user << 16; }
static uint32_t rc_snd0;
static void rc_take(rc_state_t *st)
{
    uint32_t i;
    memset(st, 0, sizeof *st);
    for (i = 0; i < NTRK; i++) {
        memcpy(st->p[i], trk[i].p, sizeof st->p[i]);
        st->snd[i] = rc_snd(&trk[i]);
        st->mot[i] = motion_count(&trk[i]);
    }
    memcpy(st->g, song.g, sizeof st->g);
    memcpy(st->k, perf_k, sizeof st->k);
    st->sel = song.sel;
    st->home = ui.home;
    st->page = ui.page;
}
static void rc_enc(uint32_t role, int32_t s, int late)
{
    if (late)
        host_enc_late[panel.enc[role]] += s * panel.dir[role];
    else
        host_enc[panel.enc[role]] += s * panel.dir[role];
}
/* one run: layer button lb, mode (0 held past HOLD, 1 a combo, 2 locked), knob k, page pg (NPAGES: HOME); late:
 * the detents come after the reads. a: the state with the layer open, b: after it closed */
static void rc_run(uint32_t lb, uint32_t mode, uint32_t k, uint32_t pg, int late, rc_state_t *a, rc_state_t *b)
{
    int32_t d = (lb == B_GLO || (lb == B_FX && k == 3u)) ? -1 : 1;   /* (GLO LEVEL, FX DEPTH: room to move) */
    uint32_t i;
    ui_power_on();
    if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
    frames(64);
    lk_snap();
    rc_snd0 = rc_snd(TSEL);
    if (mode == 2u) {
        press(lb); frames(64); press(lb); frames(64);
    } else {
        btn_down(lb);
        frame();
        if (mode == 0u)
            frames(560);                                /* (past HOLD: the map, a peek) */
    }
    for (i = 0; i < 3u; i++) {
        rc_enc(EN_K1 + k, d, late && (mode != 1u || i));   /* (a combo: the first in time, it opens the map) */
        if (i == 1u) {
            rc_enc(EN_PRESET, 1, late);                 /* (in time: the layer's read drops them) */
            if (lb != B_GLO)                            /* (Dah: GLO + ALGORITHM is the pattern, a combo of its own) */
                rc_enc(EN_ALGO, 1, late);
        }
        frame();
    }
    frame();                                            /* (the last late one) */
    rc_take(a);
    if (mode == 2u) {
        press(lb);
    } else {
        btn_up(lb);
        frame();
    }
    for (i = 0; i < 3u; i++) {                          /* turning on as it closes (#39: KNOB 1..4 quiet) */
        rc_enc(EN_K1 + k, d, late);
        frame();
    }
    frames(400);
    rc_take(b);
}
static int test_layer_knob_race(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static rc_state_t a0, b0, a1, b1;
    uint32_t l, mode, k, pg, fails = 0, runs = 0;
    int bad = 0, ok;
    for (l = 0; l < 4u; l++)
        for (mode = 0; mode < 3u; mode++)
            for (k = 0; k < 4u; k++)
                for (pg = 0; pg <= NPAGES; pg++) {
                    if (pg < NPAGES && !page_visible(pg))
                        continue;
                    if (mode == 2u && LB[l] == B_EDIT && pg < NPAGES &&   /* (EDIT's tap acts there: no lock) */
                        (PAGES[pg].graph == GR_USER || PAGES[pg].graph == GR_SLOTS || PAGES[pg].graph == GR_ROLL ||
                         PAGES[pg].graph == GR_FILE))           /* (FILE, the project renamed) */
                        continue;
                    rc_run(LB[l], mode, k, pg, 0, &a0, &b0);
                    rc_run(LB[l], mode, k, pg, 1, &a1, &b1);
                    ok = !memcmp(&a0, &a1, sizeof a0) && !memcmp(&b0, &b1, sizeof b0) &&
                         lk_same(LL[l] == LAYER_EDIT && k == 2u ? 0u : LL[l]) && song.sel == 0u &&
                         !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                    if (LL[l] == LAYER_FX)                  /* the layer's own control moved */
                        ok &= a1.k[k] == 3;
                    else if (LL[l] == LAYER_GLO)
                        ok &= a1.p[k][P_LEVEL] == lk_p[k][P_LEVEL] - 3;
                    else if (LL[l] == LAYER_SCL)
                        ok &= a1.p[0][LY_SCL.id[k]] != lk_p[0][LY_SCL.id[k]];
                    else if (k < 2u)                        /* (EDIT KNOB 3 FAV: a mark, KNOB 4 nothing) */
                        ok &= a1.snd[0] != rc_snd0;
                    runs++;
                    if (!ok) {
                        fails++;
                        if (fails < 6u)
                            printf("ui:   race: %s mode %u KNOB %u page %s\n", B_NAME[LB[l]], mode, k + 1u,
                                   pg < NPAGES ? PAGES[pg].title : "HOME");
                    }
                }
    bad += check("#102 FX GLO SCL EDIT (held, combo, locked) x KNOB 1..4, detents counted mid-pass: the layer's only",
                 !fails && runs > 400u);
    /* HOME, REC armed and playing: FX's macros and GLO's T2..T4 levels record nothing into T1's automation */
    ok = 1;
    for (l = 0; l < 2u; l++)
        for (k = l; k < 4u; k++) {
            int16_t *vp, c;
            uint32_t i;
            ui_power_on();
            song.rec = 1; seq_start(); go_home(); frames(64);
            home_param(k, &vp);
            c = *vp;
            btn_down(LB[l]); frames(560);
            for (i = 0; i < 4u; i++) { rc_enc(EN_K1 + k, l ? -1 : 1, 1); frame(); }
            frame();
            btn_up(LB[l]); frames(400);
            ok &= !motion_count(TSEL) && *vp == c;
            stop_transport();
            song.rec = 0;
        }
    bad += check("  HOME with REC: FX's KNOB 1..4, GLO's KNOB 2..4 record no automation, HOME's sound stays", ok);
    /* FX LATCH: the macro stays when FX is let go, HOME's KNOB 1 never moved; FX + OCT- turns it off */
    ui_power_on();
    fx_latch = 1;
    go_home(); frames(64);
    {
        int16_t *vp, c;
        uint32_t i;
        home_param(0, &vp);
        c = *vp;
        btn_down(B_FX); frames(560);
        for (i = 0; i < 5u; i++) { rc_enc(EN_K1, 1, 1); frame(); }
        frame();
        btn_up(B_FX); frames(400);
        ok = perf_k[0] == 5 && *vp == c;
        btn_down(B_FX); frames(560); press(B_OCTDN); btn_up(B_FX); frames(400);
        ok &= !perf_k[0] && *vp == c;
    }
    fx_latch = 0;
    bad += check("  FX LATCH: FILTER stays after FX is let go, HOME's KNOB 1 never moves; FX + OCT- turns it off", ok);
    /* no layer: a late detent is HOME's KNOB 1's, once (the next pass) */
    ui_power_on();
    go_home(); frames(64);
    {
        int16_t *vp, c;
        home_param(0, &vp);
        c = *vp;
        rc_enc(EN_K1, 1, 1); frame(); frame();
        ok = *vp == c + 1;
    }
    bad += check("  no layer: a detent counted mid-pass is HOME's KNOB 1, once", ok);
    return bad;
}

/* Discussion #83: the layer lock. A double tap of FX / GLO / SCL / EDIT (within LY_DTAP_MS, both let go before HOLD)
 * opens the map with no button held, over the page the first tap left (as it was); the keys and KNOB 1..4 act as held;
 * a tap closes it (no page), so does another page button (which opens its page), HOME, the menu, a dialog; OCT- puts
 * back as in the layer; a single tap, a peek, a combo and two slow taps are as before; the header shows the lock */
/* ------------------------------------------- the SEQ layer (1.2; Dah: QUANTIZE, CLEAR ALL) --- */
static void tl_note(step_t *s, uint32_t n) { memset(s, 0, sizeof *s); s->time = ST_NOTE; s->n = 1; s->note[0] = (uint8_t)n; s->vel = 96; }
static void tl_tie(step_t *s) { memset(s, 0, sizeof *s); s->time = ST_TIE; }
/* the selected track: LEN 8, C4 tied, E4, G4 (steps 1 2, 4, 6), a note past LEN (21), a lock on step 4 */
static void tl_melody(void)
{
    uint32_t i;
    track_defaults_steps(TSEL);
    for (i = 0; i < NSTEP; i++)
        step_clear(&TSEL->step[i]);
    tl_note(&TSEL->step[0], 60);
    tl_tie(&TSEL->step[1]);
    tl_note(&TSEL->step[3], 64);
    tl_note(&TSEL->step[5], 67);
    tl_note(&TSEL->step[20], 72);
    TSEL->p[P_SLEN] = 8;
    motion_set_lock(TSEL, 3, P_REV, 50);
}
static int tl_lock_at(uint32_t step) { int16_t v; return motion_lock_get(TSEL, step, P_REV, &v) && v == 50; }
static void tl_key(uint32_t w) { key_down(white(w)); frame(); key_up(white(w)); frame(); }
static int test_seq_tools(void)
{
    int bad = 0, ok;
    uint32_t i, k, a, b2, pg;
    step_t ref[NSTEP];

    /* where it opens (1.2): every page; up to 1.1.5 away from the SEQ pages SEQ held opened SONG */
    ok = 1;
    for (i = 0; i <= NPAGES; i++) {
        ui_power_on();
        if (i == NPAGES) go_home(); else { ui.home = 0; ui.page = (uint8_t)i; page_entered(); }
        if (i < NPAGES && !page_visible(i))
            continue;
        frame();
        pg = ui.page;
        btn_down(B_SEQ); frames(500);
        ok &= ui.layer == LAYER_SEQ && ui.page == pg && !ui.confirm;
        btn_up(B_SEQ); frame();
        ok &= !ui.layer && ui.page == pg && ui.home == (i == NPAGES);
    }
    bad += check("1.2: SEQ held opens SEQ TOOLS on every page and HOME (no SONG, the page stays)", ok);
    ui_power_on();
    go_page(GR_ROLL); frame();
    press(B_SEQ); frames(320);
    ok = str_eq(cur_page()->title, "DETAIL") && msg_is("HOLD [SEQ] QUICK");   /* (1.5.1, #199) */
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_EVENTS && str_eq(cur_page()->title, "AUTOMATION");
    press(B_SEQ); frames(320);
    ok &= str_eq(cur_page()->title, "STEP");
    press(B_SEQ); frames(320);
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_EVENTS;
    bad += check("1.2: a tap on STEP: DETAIL (1.5.1), AUTOMATION, then STEP again (three SEQ pages; the hint until the layer "
                 "was opened)", ok);
    go_title("ENV"); frame();
    press(B_SEQ); frames(320);
    ok = cur_page()->graph == GR_EVENTS;                /* (the SEQ page shown last) */
    go_home(); frame();
    for (k = 0; k < NPAGES; k++)                        /* a remembered page that is no SEQ page any more: STEP */
        if (PAGES[k].graph == GR_SONG) ui.fam_last[FAM_SEQ] = (uint8_t)k;
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_ROLL;
    bad += check("  from elsewhere the SEQ page shown last; one remembered that left SEQ: STEP", ok);

    /* (Dah) QUANTIZE: PRESETS its amount (OFF 25 50 75 ON, at once), OCT+ APPLY writes it (QUANTIZE OFF) */
    ui_power_on();
    tl_melody();
    step_set_nudge(&TSEL->step[3], 4); step_set_nudge(&TSEL->step[5], -6);
    memcpy(ref, TSEL->step, sizeof ref);
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frame();
    turn(EN_PRESET, -1);
    ok = TSEL->p[P_SQNT] == 4 && msg_is("QUANTIZE 75%") && ui.layer == LAYER_SEQ;
    turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1);
    ok &= TSEL->p[P_SQNT] == 0 && msg_is("QUANTIZE OFF");
    press(B_OCTUP);
    ok &= msg_is("QUANTIZE IS OFF") && !memcmp(TSEL->step, ref, sizeof ref);
    turn(EN_PRESET, 1); turn(EN_PRESET, 1);
    ok &= TSEL->p[P_SQNT] == 3 && msg_is("QUANTIZE 50%");
    bad += check("SEQ layer QUANTIZE: PRESETS the amount (OFF 25 50 75 ON, the ends held); APPLY with it OFF: nothing", ok);
    press(B_OCTUP);
    ok = step_nudge(&TSEL->step[3]) == 2 && step_nudge(&TSEL->step[5]) == -3 && TSEL->p[P_SQNT] == 0 &&
         msg_is("QUANTIZED 2 STEPS") && ui.layer == LAYER_SEQ && song.octave == 0;
    btn_up(B_SEQ); frame();
    bad += check("  OCT+ APPLY: the nudges as they play written (50 %: half), QUANTIZE OFF", ok);
    TSEL->p[P_SQNT] = 1;
    btn_down(B_SEQ); frame();
    turn(EN_PRESET, -2); turn(EN_K1, -2);
    ok = TSEL->p[P_SQNT] != 1 && TSEL->p[P_SLEN] == 6;
    oct_back();
    ok &= TSEL->p[P_SQNT] != 1 && TSEL->p[P_SLEN] == 6 && song.octave == 0;
    btn_up(B_SEQ); frame();
    bad += check("  OCT- tap: nothing (no octave); KNOB 1..4 LEN DIV SWING GATE", ok);
    TSEL->p[P_SQNT] = 1; TSEL->p[P_SLEN] = 8;

    /* (Dah) CLEAR ALL: F3, its dialog; the steps and the automation gone, the pattern (LEN) kept */
    memcpy(ref, TSEL->step, sizeof ref);
    btn_down(B_SEQ); frame();
    tl_key(1); tl_key(4);                               /* (the other keys: nothing) */
    ok = !memcmp(TSEL->step, ref, sizeof ref) && !ui.confirm;
    tl_key(0);
    ok &= ui.confirm == CF_CLEAR_PAT;
    btn_up(B_SEQ); frame();
    {
        char qa[24], qb[24];
        confirm_text(qa, qb);
        ok &= str_eq(qa, "CLEAR PATTERN 1?");
    }
    press(B_OCTDN);
    ok &= !ui.confirm && !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3);
    btn_down(B_SEQ); frame(); tl_key(0); btn_up(B_SEQ); frame();
    press(B_OCTUP);
    for (i = 0; i < NSTEP; i++)
        ok &= TSEL->step[i].time == ST_REST && !TSEL->step[i].n;
    ok &= !motion_count(TSEL) && TSEL->p[P_SLEN] == 8 && msg_is("PATTERN CLEARED") && ps_num(song.sel) == 1u;
    bad += check("  F3 CLEAR ALL: CLEAR PATTERN 1? (OCT- keeps it); OCT+ every step and its automation gone, LEN and the "
                 "pattern kept", ok);
    btn_down(B_SEQ); frames(500);
    a = leds_at(0); b2 = leds_at(250);
    ok = ((a ^ b2) >> white(0)) & 1u && !(((a | b2) >> white(1)) & 1u) && !(((a | b2) >> white(7)) & 1u) &&
         !(((a | b2) >> black(0)) & 1u);
    btn_up(B_SEQ); frame();
    bad += check("  LEDs: F3 CLEAR ALL breathes, nothing else", ok);

    /* the lock (a double tap), a song playing */
    ui_power_on();
    tl_melody();
    go_page(GR_ROLL); frame();
    pg = ui.page;
    press(B_SEQ); frames(64); press(B_SEQ); frames(500);
    ok = ui.lock == LAYER_SEQ && ui.layer == LAYER_SEQ && ui.page == pg;
    turn(EN_PRESET, -1);
    ok &= TSEL->p[P_SQNT] == 4;
    press(B_SEQ); frames(16);
    ok &= !ui.lock && !ui.layer && ui.page == pg;
    bad += check("  double tapped on STEP: locked over STEP, PRESETS the amount; a tap closes it", ok);
    chain.running = 1;
    memcpy(ref, TSEL->step, sizeof ref);
    btn_down(B_SEQ); frame();
    turn(EN_PRESET, 1);
    ok = msg_is("STOP TO EDIT") && TSEL->p[P_SQNT] == 4;
    tl_key(0);
    ok &= msg_is("STOP TO EDIT") && !ui.confirm && !memcmp(TSEL->step, ref, sizeof ref);
    btn_up(B_SEQ); frame();
    chain.running = 0;
    bad += check("  while a song plays: STOP TO EDIT, nothing changes", ok);
    return bad;
}

static int test_layer_lock(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    int bad = 0, ok, okk;
    uint32_t i, pg;
    ok = 1; okk = 1;
    for (i = 0; i < 4u; i++) {                          /* double tap on ENV: locked, ENV under it */
        ui_power_on();
        go_title("ENV"); frame();
        pg = ui.page;
        press(LB[i]);
        ok &= !ui.lock && !ui.layer;                    /* the first tap: its page at once */
        frames(64);
        press(LB[i]);
        frames(500);                                    /* (no button held, long past HOLD) */
        ok &= ui.lock == LL[i] && ui.layer == LL[i] && layer_locked() && !ui.home && ui.page == pg &&
              !fm1_in.buttons && ((layer_seen >> LL[i]) & 1u) && !msg_is("HOLD [GLO] QUICK");
        draw_head();
        okk &= layer_locked();
        press(LB[i]);                                   /* a tap: closed, no page */
        frames(16);
        okk &= !ui.lock && !ui.layer && !ui.home && ui.page == pg;
    }
    bad += check("#83 FX GLO SCL EDIT double tapped: the map stays open with no button held, over the page as it was", ok);
    bad += check("  a tap of the button closes it, no page opens", okk);
    {   /* #183: in every lock PLAY starts and stops (GLO's lock no RESTART: that needs GLO held) */
        static const uint8_t PB[6] = {B_FX, B_GLO, B_SCL, B_EDIT, B_SEQ, B_REC};
        static const uint8_t PL[6] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_SEQ, LAYER_REC};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            uint32_t k;
            ui_power_on();
            go_title("ENV"); frame();
            press(PB[i]); frames(64); press(PB[i]); frames(100);
            transport_req = 0;
            okk = ui.lock == PL[i];
            press(B_PLAY);                              /* stopped: PLAY */
            okk &= transport_req == 1u;
            events_block(CTL);
            okk &= song.playing != 0;
            for (k = 0; k < 2u; k++) {                  /* playing: PLAY stops, twice over */
                press(B_PLAY);
                okk &= transport_req == 2u;
                events_block(CTL);
                okk &= !song.playing && ui.lock == PL[i];
                press(B_PLAY);
                okk &= transport_req == 1u;
                events_block(CTL);
                okk &= song.playing != 0;
            }
            stop_transport();
            transport_req = 0;
            if (!okk)
                printf("    #183: %s locked, PLAY does not start / stop\n", LAYERS[PL[i]].head);
            ok &= okk;
        }
        bad += check("#183 FX GLO SCL EDIT SEQ REC locked: PLAY starts and stops (no RESTART without GLO held)", ok);
    }
    /* the keys and KNOB 1..4 act as held */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    usb.config = 1;
    {
        uint32_t mo = mo_w;
        int16_t l0 = trk[0].p[P_LEVEL];
        key_down(black(1)); frame(); key_up(black(1)); frame();
        ok = trk[1].p[P_MUTE] == 1 && !gates() && mo_w == mo;
        turn(EN_K1, 3);
        ok &= trk[0].p[P_LEVEL] == l0 + 3 && str_eq(cur_page()->title, "ENV");
        press(B_OCTDN);                                 /* OCT-: nothing, still locked */
        ok &= trk[1].p[P_MUTE] == 1 && trk[0].p[P_LEVEL] == l0 + 3 && song.octave == 0 && ui.lock == LAYER_GLO;
    }
    bad += check("  GLO locked: a black key mutes (silent, no MIDI), KNOB 1 T1 LEVEL, OCT- no octave, still locked", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    key_down(white(4)); frame();                        /* C4: LPF while held */
    ok = (perf_held & PF_BIT(PF_LPF)) && !gates();
    key_up(white(4)); frame();
    ok &= !(perf_held & PF_BIT(PF_LPF));
    turn(EN_K2, 20);
    ok &= perf_k[1] == 20;
    press(B_FX); frames(16);
    ok &= !ui.lock && !perf_k[1];                      /* (closed: the macros snap back, as FX let go) */
    bad += check("  FX locked: a key's effect while held, KNOB 2 CRUSH; closed: the macros snap back", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_SCL); press(B_SCL); frames(100);
    key_down(white(1)); frame(); key_up(white(1)); frame();   /* G3: ROOT G */
    ok = TSEL->p[P_ROOT] == 7 && !gates();
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 1 && str_eq(cur_page()->title, "ENV");
    bad += check("  SCL locked: a key sets ROOT, KNOB 2 SCL, the page stays ENV", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_EDIT); press(B_EDIT); frames(100);
    turn(EN_K1, 1);
    ok = TSEL->eng_req != trk[1].eng_req || TSEL->eng_req != TRK_DEF[0][0];
    ok &= str_eq(cur_page()->title, "ENV") && ui.lock == LAYER_EDIT;
    key_down(white(LY_INIT)); frame(); key_up(white(LY_INIT)); frame();   /* INIT: the dialog closes it */
    ok &= ui.confirm == CF_INIT_SOUND && !ui.lock && !ui.layer;
    press(B_OCTDN);
    bad += check("  EDIT locked: KNOB 1 the engine; INIT's dialog closes the lock", ok);
    /* closing: another page button opens its page, HOME goes home, another layer's tap its page, the menu */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    press(B_LFO);
    ok = !ui.lock && !ui.layer && str_eq(cur_page()->title, "LFO");
    press(B_SCL); press(B_SCL); frames(100);
    ok &= ui.lock == LAYER_SCL && str_eq(cur_page()->title, "LFO");
    press(B_HOME); frames(16);
    ok &= !ui.lock && !ui.layer && ui.home;
    press(B_GLO); press(B_GLO); frames(100);
    press(B_SCL); frames(400);
    ok &= !ui.lock && !ui.layer && cur_page()->fam == FAM_SCL;
    press(B_FX); press(B_FX); frames(100);
    hold(B_HOME);
    ok &= ui.menu && !ui.lock && !ui.layer;
    hold(B_HOME);
    bad += check("  LFO, HOME, another layer's button, the menu close it (and act as always)", ok);
    ui_power_on();
    go_title("ENV"); frame();
    press(B_GLO); press(B_GLO); frames(100);
    btn_down(B_GLO); frames(500);                       /* its button held: the map stays (a peek), let go: closed */
    ok = !ui.lock && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && str_eq(cur_page()->title, "ENV");
    frames(400);
    {
        int16_t a = TSEL->p[P_ATK];
        turn(EN_K1, 1);
        ok &= TSEL->p[P_ATK] == a + 1;                  /* (the page's knobs again after the quiet time) */
    }
    bad += check("  its button held after the lock: a peek, let go closes it; KNOB 1 is ENV's again", ok);
    /* unchanged: a single tap, two slow taps, a tap and a peek, a tap and a combo, PLAY / REC in the lock */
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_SEQ); frames(400);                          /* (1.2: SEQ, GLO's one page cycles no more) */
    ok = str_eq(cur_page()->title, "STEP") && !ui.lock;
    press(B_SEQ); frames(400);
    ok &= str_eq(cur_page()->title, "DETAIL") && !ui.lock && !ui.layer;   /* (1.5.1) */
    press(B_SEQ); frames(400);
    ok &= str_eq(cur_page()->title, "AUTOMATION") && !ui.lock && !ui.layer;
    press(B_SEQ);
    btn_down(B_SEQ); frames(500);
    ok &= ui.layer == LAYER_SEQ && !ui.lock;
    btn_up(B_SEQ); frame();
    ok &= !ui.layer && !ui.lock && str_eq(cur_page()->title, "STEP");
    press(B_GLO); frames(400);
    ok &= str_eq(cur_page()->title, "SONG") && !ui.lock;
    press(B_GLO);
    btn_down(B_GLO); frames(500);
    ok &= ui.layer == LAYER_GLO && !ui.lock;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && !ui.lock && str_eq(cur_page()->title, "SONG");
    press(B_SCL);
    lay_combo(B_SCL, white(2));
    key_up(white(2)); btn_up(B_SCL); frame(); frames(400);
    ok &= !ui.layer && !ui.lock && TSEL->p[P_ROOT] == 9 && cur_page()->fam == FAM_SCL;
    bad += check("  unchanged: a tap opens the page, slow taps cycle, a tap then a peek or a combo: no lock", ok);
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_GLO); frames(16);
    press(B_LFO); frames(16);
    press(B_GLO); frames(400);
    ok = !ui.lock && str_eq(cur_page()->title, "SONG");   /* (another button between the taps: two taps) */
    ui_power_on();
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    press(B_PLAY);
    ok &= song.playing || transport_req;
    ok &= ui.lock == LAYER_FX;
    stop_transport();
    bad += check("  another button between the taps: no lock; PLAY in a locked layer plays, the lock stays", ok);
    {   /* EDIT on STEP clears a step: two quick taps clear two (no lock) */
        track_t *t;
        ui_power_on();
        t = TSEL;
        my_steps(t);
        go_page(GR_ROLL); frame();
        ui.cursor = 0;
        press(B_EDIT); press(B_EDIT); frames(100);
        bad += check("  EDIT on STEP (it clears the step): two quick taps clear two steps, no lock",
                     !ui.lock && !ui.layer && ui.cursor == 2u && t->step[0].time == ST_REST);
    }
    return bad;
}

/* 1.1.5: the REC layer (ui_layer.c). REC held past HOLD on any page opens it (up to 1.1.4 REC held on SEQ asked "CLEAR Tn
 * SEQUENCE?", Discussion #91: gone, and its "HOLD: CLEAR" hint and the footer's REC + trash with it); a tap still
 * arms / disarms. F3 CLEAR (no dialog), G3 CLICK, A3 COUNT-IN, B3 CLICK LEVEL (each the next
 * value, the same store as MENU > AUDIO, saved when the layer closes), KNOB 1..3 the same; OCT- held RESET; the LEDs;
 * the double-tap lock (the arming as before the taps); REC in another layer still arms */
static void rec_down(void) { btn_down(B_REC); }
static void rec_up(void) { btn_up(B_REC); frame(); }
static uint32_t menu_rec(uint32_t r) { return menu_get(r == 0u ? MI_CLICK : r == 1u ? MI_COUNTIN : MI_CLKLVL); }
static int test_rec_layer(void)
{
    static const uint8_t G[8] = {GR_ROLL, GR_EVENTS, GR_USER, GR_PATTERN, GR_SONG, GR_FILE, GR_ADSR, GR_TRK};
    int bad = 0, ok = 1;
    uint32_t k, i, n;
    step_t before[NSTEP];
    for (k = 0; k < 9u; k++) {                          /* every page (and HOME): held, the layer; let go, nothing */
        ui_power_on();
        song.sel = 1;
        my_steps(&trk[1]);
        if (k < 8u)
            go_page(G[k]);
        frame();
        memcpy(before, trk[1].step, sizeof before);
        n = ui.page;
        rec_down(); frames(512);
        ok &= ui.layer == LAYER_REC && ui.ly == LAYER_REC && !ui.confirm && song.rec == 0u && !transport_req &&
              !str_eq(ui.msg, "HOLD: CLEAR");
        rec_up();
        ok &= !ui.layer && !ui.confirm && song.rec == 0u && !transport_req && ui.page == n && (k < 8u ? !ui.home : ui.home) &&
              !memcmp(trk[1].step, before, sizeof before);
    }
    bad += check("REC layer: REC held on STEP AUTOMATION USER PATTERN SONG FILE ENV MIXER HOME opens it", ok);
    bad += check("  no CLEAR dialog any more, nothing armed, cleared or started (#91's hold replaced)", ok);
    ok = 1;
    for (k = 0; k < 4u; k++) {                          /* the menu's HOLD time, as the other layers */
        ui_power_on();
        settings_hold = (uint8_t)k;
        rec_down();
        for (i = 0; i < 40u && !ui.layer; i++)
            frame();
        ok &= ui.layer == LAYER_REC && i * 16u >= HOLD_MS[k] && i * 16u <= HOLD_MS[k] + 32u;
        rec_up();
    }
    bad += check("  it opens after MENU > HOLD (0.3 .. 0.6 s), not the old 0.7 s", ok);
    ui_power_on();
    press(B_REC);
    ok = ui.home && song.rec == 1u && transport_req == 1u && !ui.layer;
    frames(400); transport_req = 0; song.playing = 1;
    press(B_REC);
    ok &= song.rec == 0u && !ui.layer && song.playing;
    song.playing = 0;
    go_page(GR_ROLL); frame(); frames(400);
    press(B_REC);
    ok &= song.rec == 1u && transport_req == 0u && msg_is("KEYS WRITE STEPS");
    bad += check("  a tap still arms / disarms when let go (HOME starts PLAY, STEP: step recording)", ok);
    ui_power_on();
    rec_down(); frame();
    press(B_PLAY);                                      /* REC + PLAY: PLAY plays, REC let go arms (no map) */
    ok = transport_req == 1u && !ui.layer;
    song.playing = 1; transport_req = 0;
    rec_up();
    ok &= song.rec == 1u && !ui.layer && song.playing;
    bad += check("  REC held + PLAY: plays, and REC let go before HOLD still arms (no combo)", ok);
    ui_power_on();
    song.sel = 2;
    chain.running = 1;
    press(B_REC);
    ok = song.rec == 0u && msg_is("STOP TO RECORD");
    chain.running = 0;
    frames(400);
    btn_down(B_GLO); frame();
    press(B_REC);                                       /* REC in GLO's layer: a tap, armed */
    ok &= song.rec == 4u && ui.ly == LAYER_GLO;
    btn_up(B_GLO); frame();
    frames(400);
    btn_down(B_GLO); frames(512); press(B_REC);
    ok &= song.rec == 0u && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    bad += check("  a song playing: STOP TO RECORD; REC in another layer still arms / disarms", ok);

    /* F3 CLEAR: no dialog; an empty track: NOTHING TO CLEAR; a song playing: STOP TO EDIT */
    ui_power_on();
    song.sel = 1;
    my_steps(&trk[1]);
    go_page(GR_ROLL); frame();
    memcpy(before, trk[1].step, sizeof before);
    rec_down(); frame();
    key_down(white(0)); frame();                        /* a combo: the map at once, cleared */
    ok = ui.layer == LAYER_REC && seq_is_empty(&trk[1]) && !ui.confirm && msg_is("SEQUENCE CLEARED") && !gates();
    key_up(white(0)); frame();
    rec_up();
    ok &= !ui.layer && song.rec == 0u && !transport_req && seq_is_empty(&trk[1]);
    bad += check("REC + F3: CLEAR T2's sequence at once (no dialog, the key silent), nothing armed", ok);
    frames(400);
    track_defaults_steps(&trk[1]);
    rec_down(); frame(); key_down(white(0)); frame();
    ok = msg_is("NOTHING TO CLEAR");
    key_up(white(0)); frame(); rec_up();
    my_steps(&trk[1]);
    chain.running = 1;
    rec_down(); frame(); key_down(white(0)); frame();
    ok &= msg_is("STOP TO EDIT") && !seq_is_empty(&trk[1]);
    key_up(white(0)); frame(); rec_up();
    chain.running = 0;
    song.playing = 1; song.rec = 2u;                    /* recording live: a retake, the arming and PLAY stay */
    rec_down(); frame(); key_down(white(0)); frame();
    ok &= seq_is_empty(&trk[1]) && song.rec == 2u && song.playing;
    key_up(white(0)); frame(); rec_up();
    song.playing = 0;
    bad += check("  empty: NOTHING TO CLEAR; a song playing: STOP TO EDIT; armed and playing: clears, records on", ok);

    /* G3 A3 B3: the settings, the next value each press, MENU > AUDIO's values; KNOB 1..3 */
    ui_power_on();
    ui_rec_prefs = 0;
    rp_apply();
    ok = 1;
    rec_down(); frames(512);
    for (k = 0; k < 3u; k++) {
        static const char *const NM[3][3] = {{"CLICK REC", "CLICK ON", "CLICK OFF"},
                                             {"COUNT-IN ON", "COUNT-IN OFF", ""},   /* (Dah: the countdown, ON / OFF) */
                                             {"CLICK LEVEL HIGH", "CLICK LEVEL LOW", "CLICK LEVEL MID"}};
        static const uint32_t WANT[3][3] = {{1, 2, 0}, {1, 0, 0}, {2, 0, 1}};
        for (i = 0; i < (k == 1u ? 2u : 3u); i++) {
            key_down(white(k + 1u)); frame(); key_up(white(k + 1u)); frame();
            ok &= menu_rec(k) == WANT[k][i] && msg_is(NM[k][i]) && !gates() &&
                  str_eq(menu_vname(k == 0u ? MI_CLICK : k == 1u ? MI_COUNTIN : MI_CLKLVL, menu_rec(k)),
                         rl_name(k, rp_get(RL_F[k])));
        }
    }
    ok &= ui_rec_prefs == 0u;                           /* (round: as it was) */
    bad += check("REC + G3 CLICK OFF>REC>ON, A3 COUNT-IN (Dah: the countdown) OFF>ON, B3 LEVEL MID>HIGH>LOW: MENU's values", ok);
    key_down(white(1)); frame(); key_up(white(1)); frame();        /* CLICK REC */
    key_down(white(2)); frame(); key_up(white(2)); frame();        /* COUNT-IN 1 BAR */
    key_down(white(3)); frame(); key_up(white(3)); frame();        /* LEVEL HIGH */
    ok = click_mode == 1u && cin_bars == 1u && click_lvl == 2u && lys.rp_dirty;
    rec_up();
    ok &= !ui.layer && !lys.rp_dirty;                  /* (closed: settings_save) */
    {
        persist_t p = {0};
        uint8_t was = ui_rec_prefs;
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p); ui_rec_prefs = 0;
        ok &= settings_import(&p, sizeof p) && ui_rec_prefs == was && menu_get(MI_CLICK) == 1u &&
              menu_get(MI_COUNTIN) == 1u && menu_get(MI_CLKLVL) == 2u;
    }
    bad += check("  applied at once (click.c, seq.c), saved when the layer closes, kept with the settings", ok);
    rec_down(); frames(512);
    turn(EN_K1, 1);                                     /* CLICK REC -> ON */
    turn(EN_K2, -1);                                    /* COUNT-IN 1 BAR -> OFF */
    turn(EN_K3, -5);                                    /* LEVEL HIGH -> LOW (stops at the end) */
    turn(EN_K4, 3);                                     /* nothing */
    ok = menu_get(MI_CLICK) == 2u && menu_get(MI_COUNTIN) == 0u && menu_get(MI_CLKLVL) == 0u && ui.home;
    rec_up();
    bad += check("  KNOB 1..3 CLICK COUNT-IN LEVEL, stopping at the ends; KNOB 4 nothing; the page untouched", ok);

    /* an OCT- tap: nothing (no octave, the settings and the track stay) */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    song.sel = 1;
    my_steps(&trk[1]);
    rec_down(); frames(512);
    key_down(white(1)); frame(); key_up(white(1)); frame();
    key_down(white(0)); frame(); key_up(white(0)); frame();
    ok = seq_is_empty(&trk[1]) && click_mode == 1u;
    press(B_OCTDN);
    ok &= seq_is_empty(&trk[1]) && click_mode == 1u && song.octave == 0 && ui.layer == LAYER_REC;
    rec_up();
    bad += check("  an OCT- tap: nothing, no octave shift", ok);

    /* the LEDs: CLEAR breathes when there is something; a setting on lit (LEVEL: with the click on), else breathing */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    my_steps(TSEL);
    rec_down(); frames(512);
    {
        uint32_t a = leds_at(0), b = leds_at(250), w0 = 1u << white(0), w1 = 1u << white(1), w2 = 1u << white(2),
                 w3 = 1u << white(3);
        ok = ((a ^ b) & (w0 | w1 | w2 | w3)) == (w0 | w1 | w2 | w3) && !(a & b) && !((a | b) & ~(w0 | w1 | w2 | w3));
        rp_put(RP_CLICK, 2); rp_put(RP_COUNTIN, 2);
        a = leds_at(0); b = leds_at(250);
        ok &= (a & b) == (w1 | w2 | w3) && (a ^ b) == w0;
        track_defaults_steps(TSEL);
        a = leds_at(0); b = leds_at(250);
        ok &= !((a | b) & w0);
    }
    rec_up();
    bad += check("  LEDs: CLEAR breathes (dark when empty), CLICK / COUNT-IN lit when on, LEVEL with the click", ok);

    /* the double-tap lock: the arming as before the taps, the keys and knobs as held, a tap of REC closes it */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    go_title("ENV"); frame();
    n = ui.page;
    press(B_REC);
    ok = song.rec == 1u && transport_req == 1u && msg_is("HOLD [REC] QUICK");   /* (until it has been opened once) */
    frames(64);
    press(B_REC);
    frames(500);
    ok &= ui.lock == LAYER_REC && ui.layer == LAYER_REC && layer_locked() && song.rec == 0u && transport_req == 0u &&
          ui.page == n && !fm1_in.buttons && !msg_is("HOLD [REC] QUICK") && ((layer_seen >> LAYER_REC) & 1u);
    key_down(white(1)); frame(); key_up(white(1)); frame();
    turn(EN_K2, 1);
    ok &= click_mode == 1u && cin_bars == 1u && !gates() && ui.lock == LAYER_REC;
    press(B_PLAY);                                      /* PLAY works in it */
    ok &= transport_req == 1u && ui.lock == LAYER_REC;
    transport_req = 0;
    press(B_REC);                                       /* a tap: closed, nothing armed */
    frames(16);
    ok &= !ui.lock && !ui.layer && song.rec == 0u && ui.page == n && !lys.rp_dirty;
    bad += check("#83 REC double tapped: locked, the arming as before; keys, knobs, PLAY; a REC tap closes it", ok);
    ui_power_on();
    song.playing = 1;
    press(B_REC); frames(64); press(B_REC); frames(100);
    ok = ui.lock == LAYER_REC && song.rec == 0u && song.playing && transport_req == 0u;
    press(B_REC); frames(16);
    song.playing = 0;
    press(B_GLO); press(B_GLO); frames(100);            /* another layer locked: REC still arms, the lock stays */
    press(B_REC);
    ok &= ui.lock == LAYER_GLO && song.rec == 1u;
    bad += check("  playing: the transport stays; in GLO's lock a REC tap arms, GLO stays locked", ok);
    return bad;
}

/* 1.2: the FX layer's white keys are the user's: a key held in the layer + PRESETS steps its effect (NONE, the effects,
 * round; the key holds the new one at once), EDIT its default back; PRESETS loads no sound there, with no key held it
 * says how; kept with the settings when the layer closes (an older setting: the default map); the lock frees a hand */
static int test_fx_map(void)
{
    int bad = 0, ok;
    uint32_t b4 = white(10), p, snd;
    ui_power_on();
    memset(fx_keys, 0, 10);
    frame();
    for (ok = 1, p = 0; p < PF_KEYS; p++)
        ok &= perf_map[p] == PF_DEF[p];
    bad += check("FX map: settings of 0 (before 1.2) are the default: 1.1.5's ten keys, B4 FLANGER, C5 PHASER, D5 E5 the throws", ok);
    snd = TSEL->preset | (uint32_t)TSEL->eng_req << 8;
    btn_down(B_FX); key_down(b4); frame();
    ok = perf_held == PF_BIT(PF_FLG) && ui.layer == LAYER_FX;
    turn(EN_PRESET, 1); keyboard_block();               /* (keyboard_block: the ISR's next block) */
    ok &= perf_map[10] == PF_PHS && perf_held == PF_BIT(PF_PHS) && msg_is("B4 PHASER");
    turn(EN_PRESET, 1); keyboard_block();               /* 1.5.1: the throws, after PHASER */
    ok &= perf_map[10] == PF_RTHR && perf_held == PF_BIT(PF_RTHR) && msg_is("B4 REV THROW");
    turn(EN_PRESET, 1); keyboard_block();
    ok &= perf_map[10] == PF_ETHR && perf_held == PF_BIT(PF_ETHR) && msg_is("B4 ECHO THROW");
    turn(EN_PRESET, 1); keyboard_block();               /* past the last: NONE */
    ok &= perf_map[10] == PF_N && !perf_held && msg_is("B4 NONE") && (kb_layer >> b4) & 1u;
    turn(EN_PRESET, 1); keyboard_block();               /* round: the first */
    ok &= perf_map[10] == PF_R8 && perf_held == PF_BIT(PF_R8);
    turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1); turn(EN_PRESET, -1); keyboard_block();
    ok &= perf_map[10] == PF_PHS && perf_held == PF_BIT(PF_PHS) && lys.rp_dirty;
    ok &= (TSEL->preset | (uint32_t)TSEL->eng_req << 8) == snd && !gates();
    bad += check("  FX + B4 held + PRESETS: PHASER, REV THROW, ECHO THROW, NONE, round to REPEAT 1/8 and back, each held at once; no sound load", ok);
    btn_down(B_EDIT); frame(); btn_up(B_EDIT); frame(); keyboard_block();
    ok = perf_map[10] == PF_FLG && perf_held == PF_BIT(PF_FLG) && msg_is("B4 FLANGER (DEF)") && ui.layer == LAYER_FX;
    bad += check("  EDIT with the key held: its default (FLANGER) back, the layer open", ok);
    turn(EN_PRESET, -1); keyboard_block();              /* B4: OCT DN */
    key_up(b4); frame();
    ok = !perf_held && perf_map[10] == PF_ODN;
    turn(EN_PRESET, 1);                                 /* no key held: nothing; how */
    ok &= perf_map[10] == PF_ODN && msg_is("HOLD A KEY: ASSIGN");
    key_down(white(1)); frame();                        /* G3 */
    ok &= perf_held == PF_BIT(PF_R16);
    key_up(white(1)); btn_up(B_FX); frame();
    ok &= !ui.layer && !lys.rp_dirty;                   /* (closed: settings_save) */
    bad += check("  the key let go: PRESETS changes nothing (HOLD A KEY: ASSIGN); FX let go: the map saved", ok);
    {   /* with the settings: exported, imported; an older record: the default */
        persist_t pp;
        settings_export(&pp);
        memset(fx_keys, 0, 10);
        frame();
        ok = perf_map[10] == PF_FLG;
        ok &= settings_import(&pp, (int)sizeof pp) == 1;
        frame();
        ok &= perf_map[10] == PF_ODN;
        for (p = 0; p < PF_KEYS; p++)
            ok &= p == 10u || perf_map[p] == PF_DEF[p];
        memset(&pp.favorites, 0, sizeof pp.favorites);
        pp.magic = 0x50455233u;                         /* PER3: no favorites */
        ok &= settings_import(&pp, (int)(sizeof pp - sizeof pp.favorites)) == 2;
        frame();
        for (p = 0; p < PF_KEYS; p++)
            ok &= perf_map[p] == PF_DEF[p];
        bad += check("  the map with the settings (exported, imported: B4 OCT DN, the rest default); older settings: default", ok);
    }
    /* the lock (a double tap of FX): one hand holds a key, the other turns PRESETS; EDIT keeps the lock */
    ui_power_on();
    memset(fx_keys, 0, 10);
    go_title("ENV"); frame();
    press(B_FX); press(B_FX); frames(100);
    ok = ui.lock == LAYER_FX;
    key_down(white(1)); frame();                        /* G3: REPEAT 1/16 */
    turn(EN_PRESET, 1); keyboard_block();
    ok &= perf_map[1] == PF_R32 && perf_held == PF_BIT(PF_R32) && msg_is("G3 REPEAT 1/32");
    btn_down(B_EDIT); frame(); btn_up(B_EDIT); frame(); keyboard_block();
    ok &= perf_map[1] == PF_R16 && perf_held == PF_BIT(PF_R16) && ui.lock == LAYER_FX && ui.layer == LAYER_FX &&
          str_eq(cur_page()->title, "ENV");
    turn(EN_PRESET, -1); keyboard_block();
    ok &= perf_map[1] == PF_R8;
    key_up(white(1)); frame();
    press(B_EDIT);                                      /* no key held: EDIT closes the lock as before */
    ok &= !ui.lock && !perf_held;
    bad += check("  locked (FX double tap): G3 held + PRESETS: REPEAT 1/32; EDIT: 1/16, the lock stays; no key: EDIT closes it", ok);
    {   /* the cells name what the keys hold: G3 "1/8" now, B4 FLNG */
        ok = str_eq(PF_CELL[perf_map[1]], "1/8") && str_eq(PF_CELL[perf_map[10]], "FLNG") && str_eq(PF_CELL[perf_map[12]], "VERB") &&
             str_eq(PF_CELL[perf_map[13]], "ECHO") && perf_map[14] == PF_N;
        bad += check("  the map's cells: G3 1/8 (remapped), B4 FLNG, D5 VERB, E5 ECHO (1.5.1's throws), F5 empty", ok);
    }
    memset(fx_keys, 0, 10);
    frame();
    return bad;
}

/* #40: KNOB 4's DEPTH rises to the right (it fell); MENU > FX LATCH ON: a key with FX turns its effect on, again off,
 * nothing held needed; the macros stay after FX; FX + OCT- all off; the menu turns them off; FX's LED shows them;
 * OFF (the default, and every older setting): as before */
static int test_fx_latch(void)
{
    int bad = 0, ok;
    uint32_t lpf = white(4), m1 = black(0);
    ui_power_on();
    btn_down(B_FX); frame();
    turn(EN_K4, 5);  ok = perf_k[3] == 0;                         /* DEPTH 100 %: right stays at 100 */
    turn(EN_K4, -30); ok &= perf_k[3] == 30;                      /* left: 70 % */
    turn(EN_K4, 10); ok &= perf_k[3] == 20;                       /* right: 80 % */
    key_down(white(8)); frame(); perf_begin(CTL);                 /* OCT UP: KNOB 4 is SHIMR, right is more */
    turn(EN_K4, 10); ok &= perf_k[3] == 30;
    key_up(white(8)); frame(); btn_up(B_FX); frame();
    bad += check("#40 FX + KNOB 4: DEPTH rises to the right, falls to the left; SHIMR (OCT UP) rises to the right",
                 ok && !perf_k[3]);
    /* OFF (the default; a byte older settings leave 0) */
    ui_power_on();
    ok = !(fx_latch & 1u);
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    ok &= !perf_latched && !perf_held;
    bad += check("  FX LATCH OFF by default: a key's effect ends when it is let go", ok);
    /* ON */
    ui_power_on();
    fx_latch = 1;
    frame();                                                      /* (ui_input hands the setting to the ISR) */
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame();
    turn(EN_K2, 30);
    ok = perf_latched == PF_BIT(PF_LPF) && !perf_held && ui.layer == LAYER_FX && str_eq(layer_head(), "[FX] LATCH") &&
         (layer_leds(0) >> lpf) & 1u;
    btn_up(B_FX); frame();
    ok &= perf_latched == PF_BIT(PF_LPF) && perf_k[1] == 30 && !ui.layer;
    ok &= perf_begin(CTL) && (perf_act & PF_BIT(PF_LPF));                 /* it runs with nothing held */
    bad += check("  FX LATCH ON: FX + C4 latches LPF, CRUSH stays at 30 after FX is let go", ok);
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_FX]] = 0u << 3 | 1u;
    go_home(); ui_leds();
    ok = (fm1_led[0] & 2u) != 0u;
    btn_down(B_FX); key_down(m1); frame(); key_up(m1); frame();   /* a mute latches too */
    ok &= perf_latched == (PF_BIT(PF_LPF) | PF_BIT(PF_M1));
    key_down(lpf); frame(); key_up(lpf); frame();                 /* C4 again: LPF off */
    ok &= perf_latched == PF_BIT(PF_M1);
    btn_up(B_FX); frame();
    bad += check("  the FX LED lit while something is on; a mute key latches; C4 again: LPF off", ok);
    btn_down(B_FX); frame(); turn(EN_K1, -40);
    ok = song.octave == 0 && oct_leds() == 1u;
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= !perf_latched && !perf_k[0] && !perf_k[1] && song.octave == 0 && msg_is("FX ALL OFF");
    btn_up(B_FX); frame();
    bad += check("  FX + OCT-: every latched effect and macro off (no octave)", ok);
    /* the quick combo: FX down and OCT- before HOLD, nothing else touched (it was nobody's: no ALL OFF, no octave) */
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); turn(EN_K2, 20); btn_up(B_FX); frame();
    frames(400);
    ok = perf_latched == PF_BIT(PF_LPF) && perf_k[1] == 20 && !ui.layer;
    btn_down(B_FX); frame(); btn_down(B_OCTDN); frame();
    ok &= ui.layer == LAYER_FX && song.octave == 0;               /* (a combo: the map at once) */
    btn_up(B_OCTDN); frame();
    ok &= !perf_latched && !perf_k[1] && song.octave == 0 && msg_is("FX ALL OFF");
    {   uint32_t h = ui.home, pg = ui.page;
        btn_up(B_FX); frame();
        ok &= !ui.layer && ui.home == h && ui.page == pg && !ui.lock;   /* (no tap: no page) */
    }
    bad += check("  FX LATCH: FX + OCT- quick (before HOLD): all off, no octave, no page", ok);
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    frames(400);
    btn_down(B_FX); btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* (both in one frame) */
    ok = !perf_latched && song.octave == 0 && msg_is("FX ALL OFF");
    btn_up(B_FX); frame();
    btn_down(B_FX); frame(); frames(800);                         /* held past HOLD, then OCT- */
    ok &= ui.layer == LAYER_FX;
    press(B_OCTDN);
    ok &= song.octave == 0;
    btn_up(B_FX); frame();
    press(B_OCTDN);                                               /* no FX: the octave as always */
    ok &= song.octave == -1 && !ui.layer;
    press(B_OCTUP);
    ok &= song.octave == 0;
    bad += check("  FX LATCH: FX + OCT- in one frame / held: no octave; OCT- alone: the octave", ok);
    fx_latch = 0;
    frame();
    btn_down(B_FX); frame(); btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok = song.octave == -1 && ui.layer == LAYER_FX;               /* LATCH OFF: FX's HOLD layer, OCT- is the octave */
    {   uint32_t h = ui.home, pg = ui.page;
        btn_up(B_FX); frame();
        ok &= !ui.layer && ui.home == h && ui.page == pg;
    }
    btn_down(B_FX); frame(); frames(800); press(B_OCTUP); btn_up(B_FX); frame();
    ok &= song.octave == 0;
    btn_down(B_FX); frame(); btn_up(B_FX); frame();               /* FX tap: its page still */
    ok &= !ui.home && cur_page()->fam == FAM_FX;
    go_home();
    frames(400);
    btn_down(B_GLO); frame(); btn_down(B_OCTDN); frame();          /* a SET layer the same: GLO + OCT- quick */
    ok &= ui.layer == LAYER_GLO;
    btn_up(B_OCTDN); frame();
    ok &= song.octave == 0;
    btn_up(B_GLO); frame();
    ok &= ui.home && !ui.layer;
    bad += check("  FX LATCH OFF: FX + OCT- quick / held shifts the octave (no page); FX tap: the FX page; "
                 "GLO + OCT- quick: no octave", ok);
    fx_latch = 1;
    frame();
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    hold(B_HOME);
    ok = ui.menu && !perf_latched;
    ui.menu_sel = MI_LATCH;
    press(B_OCTDN); ok &= !(fx_latch & 1u) && ui.menu;
    press(B_OCTUP); ok &= (fx_latch & 1u) && ui.menu;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    turn(EN_K1, 1); ok &= (fx_latch & 1u) != 0u;
    turn(EN_K1, -1); ok &= !(fx_latch & 1u);
    press(B_HOME);
    bad += check("  the menu turns the latched effects off; MENU > FX LATCH: OCT+ / KNOB 1 right ON, OCT- / left OFF",
                 ok && !ui.menu);
    {   /* kept with the settings, in the favorites record (an older one: OFF) */
        persist_t p;
        fx_latch = 1;
        settings_export(&p);
        fx_latch = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (fx_latch & 1u);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                    /* PER3: no favorites */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(fx_latch & 1u);
        bad += check("  FX LATCH saved with the settings; settings from before it: OFF", ok);
    }
    fx_latch = 0;
    return bad;
}

/* #48: the note divisions step by their length, longest first (triplets between their neighbours), on every list
 * that has them (SEQ DIV, ARP RATE, the delay's TIME, the SLICER's RATE); the stored values and their sound stay */
static int test_div_order(void)
{
    int bad = 0, ok = 1;
    uint32_t id, c, k, found = 0;
    static const param_desc_t *const LISTS[] = {&TP[P_SDIV], &TP[P_ARATE], &GP[G_DTIME], &TP[P_SLRATE]};
    ui_power_on();
    for (k = 0; k < NELEM(LISTS); k++) {           /* left end -> right end: each one shorter than the one before */
        const param_desc_t *d = LISTS[k];
        int32_t v = d->def, n, seen = 0;
        uint32_t prev = 0xFFFFFFFFu, len;
        for (n = 0; n < 40; n++) v = param_turn(d, v, -1);
        for (n = 0; n <= d->max - d->min; n++) {
            len = d->names == N_SLDIV ? 1000000u / SL_DEN[v] : v >= 10 ? (beat_samples() * 3u) >> DIV_DOT[v - 10] :
                  div_samples((uint32_t)v);                     /* (TIME's dotted ones, 1.5.1: fx.c delay_samples) */
            if (!(len < prev && enum_rank(d, v) == d->min + n && !((seen >> v) & 1))) {
                printf("  %s: %s (value %d, place %d, length %u) after %u\n", d->label, d->names[v], (int)v,
                       (int)enum_rank(d, v), len, prev);
                ok = 0;
            }
            seen |= 1 << v;
            prev = len;
            v = param_turn(d, v, 1);
        }
        ok &= seen == (1 << (d->max - d->min + 1)) - 1;
    }
    ok &= str_eq(N_DIV[2], "1/16") && str_eq(N_DIV[4], "8T") && str_eq(N_DIV[9], "4BAR") && TP[P_SDIV].def == 2;
    ok &= param_turn(&TP[P_SDIV], 2, 1) == 5 && param_turn(&TP[P_SDIV], 2, -1) == 4 &&   /* 1/16: right 16T, left 8T */
          param_turn(&TP[P_SDIV], 3, 1) == 3 && param_turn(&TP[P_SDIV], 9, -1) == 9 &&   /* the ends hold */
          param_turn(&TP[P_SDIV], 2, -6) == 8 && param_turn(&TP[P_SDIV], 2, 40) == 3;    /* 6 left: 2BAR; far: clamped */
    bad += check("#48 DIV / RATE / TIME / SLICER RATE step longest -> shortest, every value once, the ends hold", ok);
    ok = 1;                                         /* the real knob on the pages that show them, the gauge follows */
    for (id = 0; id < NPAGES; id++)
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d;
            ui_power_on();
            TSEL->p[P_SLCR] = 1;                        /* (Dah: the SLICER in the chain: its page shows) */
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || !(d = page_desc(cur_page(), c, &vp)) || !vp || !enum_order(d) || PAGES[id].id[0] == P_FXORD)
                continue;                               /* (Dah: CHAIN's knobs are its list's) */
            found++;
            *vp = d->names != N_SLDIV ? 2 : 1;      /* 1/16 */
            turn(EN_K1 + c, 1);
            ok &= *vp == (d->names != N_SLDIV ? 5 : 4);  /* 16T */
            turn(EN_K1 + c, -2);
            ok &= *vp == (d->names != N_SLDIV ? 4 : 3);  /* 8T */
        }
    ui_power_on();                                  /* SEQ's DIV: SEQ TOOLS' KNOB 2 (1.2: the PATTERN page is gone) */
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);
    TSEL->p[P_SDIV] = 2;
    turn(EN_K2, 1);
    ok &= ui.layer == LAYER_SEQ && TSEL->p[P_SDIV] == 5;
    turn(EN_K2, -2);
    ok &= TSEL->p[P_SDIV] == 4;
    btn_up(B_SEQ); frame();
    bad += check("#48 the pages' knobs step the divisions by length (SEQ TOOLS, ARP, DLY, SLICER)", ok && found >= 3u);
    return bad;
}

/* #52 MENU > KNOB ACCEL (OFF by default, #23): ON, a sustained fast turn of a wide value goes x2..x4 (x8 over a range
 * above 64, #126); a slow turn, the start of a turn, a lone quick detent and a reversal are one step per detent; the
 * sign is always the detents' */
static uint32_t acc_ms(uint32_t role, const uint16_t *gaps, uint32_t n, int32_t dir, int32_t range, int32_t *out)
{
    uint32_t k, sum = 0;
    for (k = 0; k < n; k++) {
        fm1_ms += gaps[k];
        out[k] = accel(role, dir, range);
        sum += (uint32_t)(out[k] < 0 ? -out[k] : out[k]);
    }
    return sum;
}
static int test_knob_accel(void)
{
    int bad = 0, ok;
    uint32_t k, seed = 12345u;
    int32_t o[16];
    static const uint16_t SLOW[8] = {500, 60, 60, 45, 41, 60, 80, 60}, MID[8] = {500, 30, 30, 30, 26, 30, 30, 30},
                          F20[8] = {500, 20, 20, 20, 20, 20, 20, 20}, F10[8] = {500, 10, 10, 10, 10, 10, 10, 10},
                          BOUNCE[8] = {500, 2, 300, 1, 300, 3, 300, 2};
    ui_power_on();
    ok = acc_ms(EN_K1, F10, 8, 1, 127, o) == 8u;                  /* OFF (the default) */
    ui_prefs = PREF_ACCEL;
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    ok &= acc_ms(EN_K1, SLOW, 8, 1, 127, o) == 8u && acc_ms(EN_K2, MID, 8, -1, 127, o) == 8u && o[7] == -1;
    ok &= acc_ms(EN_K3, BOUNCE, 8, 1, 127, o) == 8u;
    bad += check("#52 KNOB ACCEL: OFF by default; ON, slow (>= 26 ms a detent) and lone quick detents: 1 step each", ok);
    fm1_ms += 1000;
    acc_ms(EN_K1, F20, 8, 1, 64, o);
    ok = o[0] == 1 && o[1] == 1 && o[2] == 2 && o[7] == 2;          /* 20 ms a detent: x2 from the third */
    acc_ms(EN_K1, F10, 8, 1, 64, o);                               /* (a pause of 500 ms first: a new turn) */
    ok &= o[0] == 1 && o[1] == 1 && o[2] == 4 && o[7] == 4;         /* 10 ms: x4 (the cap of a range <= 64) */
    fm1_ms += 15; ok &= accel(EN_K1, 1, 64) == 3;                  /* 15 ms: x3 */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* reversed: one step, the right sign */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* (the turn starts again) */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -4;
    fm1_ms += 30; ok &= accel(EN_K1, -3, 64) == -12;               /* 3 detents in one read (10 ms each): x4 */
    bad += check("#52 KNOB ACCEL ON: 20 ms a detent x2, 15 ms x3, 10 ms x4 from a turn's third detent; reversal x1", ok);
    {   /* #126 a range above 64: 10 ms x5, 8 ms x6, 6 ms and less x8; a flick of 12 detents in ~120 ms, faster in
         * the middle, moves a -100..100 value ~50 (was 38); 12 even ones 52 */
        static const uint16_t FLICK[12] = {500, 16, 12, 10, 8, 7, 7, 8, 10, 12, 16, 20}, F6[8] = {500, 6, 6, 5, 4, 6, 6, 6};
        uint32_t f;
        fm1_ms += 1000;
        f = acc_ms(EN_K2, FLICK, 12, 1, 200, o);
        ok = f == 47u && o[0] == 1 && o[1] == 1;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F10, 8, -1, 200, o) == 2u + 6u * 5u && o[7] == -5;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 127, o) == 2u + 6u * 8u;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 64, o) == 2u + 6u * 4u;      /* (a range <= 64: x4 at most) */
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 0, o) == 8u;                  /* (a list) */
        printf("  #126 a flick of 12 detents over %u ms on -100..100: %u steps\n", 126u, (unsigned)f);
        bad += check("#126 KNOB ACCEL ON over a range above 64: 10 ms x5 .. 6 ms x8; a 12-detent flick ~120 ms: 47", ok);
    }
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 32, o);
    ok = o[7] == 1;                                                /* a narrow range (<= 32): never */
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 0, o);
    ok &= o[7] == 1;                                               /* a list of names (range 0 at the callers) */
    for (k = 0; k < 20000u && ok; k++) {                           /* random turns: the sign is the detents', at */
        int32_t s, r, gap;                                         /* most x8, a slow detent exactly one step */
        seed = seed * 1103515245u + 12345u;
        s = (int32_t)((seed >> 16) % 7u) - 3;
        gap = (int32_t)((seed >> 8) % 90u);
        fm1_ms += (uint32_t)gap;
        r = accel(EN_K1 + (seed & 3u), s, 127);
        ok &= s ? (r > 0) == (s > 0) && r * s <= 8 * s * s && r * s >= s * s : r == 0;
        if (gap >= 40 * (s < 0 ? -s : s) && r != s) ok = 0;
    }
    bad += check("#52 KNOB ACCEL: never on narrow values or lists; 20000 random reads keep the sign, x1..x8, slow = x1", ok);
    {   /* the real knob: LEVEL on MIXER, a detent a frame (16 ms): 1 + 1 + 3 x 6 = 20 for 8 detents; OFF: 8 */
        int16_t v0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_title("MIXER");
        TSEL->p[P_LEVEL] = 40; v0 = TSEL->p[P_LEVEL];
        fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok = TSEL->p[P_LEVEL] - v0 == 20;
        ui_prefs = 0; TSEL->p[P_LEVEL] = 40; fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok &= TSEL->p[P_LEVEL] == 48;
        bad += check("#52 KNOB ACCEL on MIXER LEVEL: a fast turn of 8 detents moves 20 (OFF: 8)", ok);
    }
    {   /* #126 the FX layer's KNOB 1 (FILTER -100..100): a flick of 12 detents 10 ms apart ~52 (was 12: the layer's
         * knobs went round accel); OFF: 12; slow (60 ms): 12; 6 right then 6 left fast: back to 0; GLO's LEVEL too */
        static const uint32_t GAP[4] = {10u, 10u, 60u, 10u};
        static const uint32_t ON[4] = {1u, 0u, 1u, 1u};
        int32_t got[4], g;
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui_power_on();
            ui_prefs = ON[c] ? PREF_ACCEL : 0u;
            go_home(); frames(64);
            btn_down(B_FX); frames(560);
            for (k = 0; k < 12u; k++) {
                host_enc[panel.enc[EN_K1]] += (c == 3u && k >= 6u ? -1 : 1) * panel.dir[EN_K1];
                fm1_ms += GAP[c] - 16u;                         /* (frame() adds 16) */
                frame();
            }
            got[c] = perf_k[0];
            btn_up(B_FX); frames(400);
        }
        printf("  #126 FX FILTER, 12 detents: ON 10 ms %d, OFF %d, ON 60 ms %d, ON 6 right 6 left %d\n", (int)got[0],
               (int)got[1], (int)got[2], (int)got[3]);
        ok = got[0] >= 35 && got[0] <= 60 && got[1] == 12 && got[2] == 12 && got[3] == 0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_home(); frames(64);
        trk[1].p[P_LEVEL] = 0;
        btn_down(B_GLO); frames(560);
        for (k = 0; k < 12u; k++) {
            host_enc[panel.enc[EN_K2]] += panel.dir[EN_K2];
            fm1_ms -= 6u;
            frame();
        }
        g = trk[1].p[P_LEVEL];
        btn_up(B_GLO); frames(400);
        ok &= g > 12 + 12;
        bad += check("#126 KNOB ACCEL on the FX layer (FILTER: a 12-detent flick >= 35, OFF 12, slow 12) and GLO LEVEL", ok);
    }
    {   /* saved with the settings; settings from before it: OFF */
        persist_t p;
        ui_prefs = PREF_ACCEL;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_ACCEL;
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        bad += check("#52 KNOB ACCEL saved with the settings; settings from before it: OFF", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* #51 ("a melody appears when I change presets"): browsing every sound in the list (every engine's presets, then user
 * presets, one of them holding a pattern), on HOME and the PRESETS page, stopped and playing, on an empty track: no
 * step is written, the ARP stays off, nothing is held, the transport is not started, no message about a pattern. (In
 * 0.9 a preset carried a 16-step pattern and replaced the steps; from 1.0 only SEQ > PHRASES loaded one, now none.) */
static int test_browse_no_pattern(void)
{
    int bad = 0, ok = 1;
    uint32_t pass, n, total, loads = 0;
    track_t *t;
    for (pass = 0; pass < 4u; pass++) {
        ui_power_on();
        t = TSEL;
        my_steps(t); t->p[P_SDIV] = 2;
        up_store(3, "MINE");                          /* a user preset that holds a pattern */
        up_store(5, "PLAIN");
        track_defaults_steps(t);
        ok &= seq_is_empty(t) && !up_pat_empty(up_rec(3));
        if (pass & 1u) song.playing = 1;              /* playing: the other tracks run, this one is empty */
        if (pass & 2u) go_page(GR_USER);
        preset_all_pos(&total);
        for (n = 0; n < total + 2u; n++) {
            turn(EN_PRESET, 1);
            loads++;
            if (!seq_is_empty(t) || t->p[P_AMODE] || t->nheld || (!(pass & 1u) && (song.playing || transport_req))) {
                printf("  pass %u, sound %u (%s %u): steps %d ARP %d held %u playing %d\n", pass, n,
                       ENGINES[t->eng_req % NENGINES]->name, t->preset, !seq_is_empty(t), t->p[P_AMODE], t->nheld,
                       song.playing);
                ok = 0;
                break;
            }
        }
    }
    if (!ok || loads < 200u) printf("  ok %d loads %u\n", ok, loads);
    bad += check("#51 every sound browsed (HOME, PRESETS; stopped, playing): no step, ARP off, nothing held, no start",
                 ok && loads >= 200u);
    return bad;
}

/* MENU > USB LEVEL: MASTER (the default, every older setting) / FIXED; the audio follows at once (audio_test: the levels) */
static int test_usb_level(void)
{
    int bad = 0, ok;
    ui_power_on();
    frame();
    ok = !fx_usb_fixed && str_eq(MI_NAME[MI_USB], "USB LEVEL") && str_eq(menu_flag(MI_USB)->name[0], "MASTER");
    hold(B_HOME);
    ui.menu_sel = MI_USB;
    ui.force = 1; ui_draw();
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;      /* right: FIXED */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;   /* left: MASTER */
    press(B_OCTUP); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed && ui.menu;
    press(B_OCTDN); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed && ui.menu;
    press(B_OCTUP);
    press(B_HOME);
    ok &= !ui.menu && fx_usb_fixed;
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_USB_FIXED;
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        frame();
        ok &= !fx_usb_fixed;
    }
    bad += check("MENU > USB LEVEL: MASTER by default, KNOB 1 / OCT+ FIXED, left / OCT- MASTER, saved, the audio follows", ok);
    ui_prefs = 0;
    frame();
    return bad;
}

/* 1.1 (Discussion #131) MENU > AUDIO CLICK (OFF REC ON), CLICK LEVEL (LOW MID HIGH), COUNT-IN (OFF 1 BAR 2 BARS): OFF / MID /
 * OFF by default and in every older setting; a knob applies at once (click.c, seq.c); saved and read back, a stored 3
 * (no value) the default. The count-in from the UI: REC while stopped arms and counts in, the screen counts 4 3 2 1 (the
 * beat's cell moves), PLAY's green LED on each beat (its own dark), the STEP keys' entry paused, PLAY stops it (the page
 * back); at its end the sequencer plays and the page is back */
static uint16_t px_at(uint32_t x, uint32_t y) { return host_screen[y * 240u + x]; }
static void cin_beat(void)                       /* the audio until the count-in's next beat (or its end) */
{
    uint32_t left = cin_left, k;
    for (k = 0; k < 4000u && cin_left == left && seq_counting(); k++)
        events_block(CTL);
}
static int test_click_menu(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    frame();
    ok = !ui_rec_prefs && click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars &&
         str_eq(MI_NAME[MI_CLICK], "CLICK") && str_eq(MI_NAME[MI_CLKLVL], "CLICK LEVEL") && str_eq(MI_NAME[MI_COUNTIN], "COUNT-IN") &&
         MI_TAB[MI_CLICK] == MTAB_AUDIO && MI_TAB[MI_CLKLVL] == MTAB_AUDIO && MI_TAB[MI_COUNTIN] == MTAB_AUDIO &&
         menu_get(MI_CLICK) == 0u && menu_get(MI_CLKLVL) == 1u && menu_get(MI_COUNTIN) == 0u;
    for (i = 0; i < 3u; i++)
        ok &= str_eq(menu_vname(MI_CLICK, i), (const char *[]){"OFF", "REC", "ON"}[i]) &&
              str_eq(menu_vname(MI_CLKLVL, i), (const char *[]){"LOW", "MID", "HIGH"}[i]) &&
              str_eq(menu_vname(MI_COUNTIN, i), (const char *[]){"OFF", "ON", "ON"}[i]);   /* (Dah: the countdown) */
    bad += check("MENU > AUDIO: CLICK OFF / REC / ON, CLICK LEVEL LOW / MID / HIGH, COUNT-IN OFF / 1 BAR / 2 BARS; OFF MID OFF by default", ok);
    hold(B_HOME);
    ui.menu_sel = MI_LOWCUT; ui.force = 1; ui_draw();
    turn(EN_PRESET, 1); turn(EN_PRESET, 1);
    ok = ui.menu_sel == MI_CLICK;
    turn(EN_K1, 1); ok &= click_mode == CLICK_REC;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;      /* (stops at the end) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_CLKLVL;
    press(B_OCTDN); ok &= click_lvl == 0u;
    press(B_OCTUP); press(B_OCTUP); ok &= click_lvl == 2u;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_COUNTIN;
    turn(EN_K4, 1); ok &= cin_bars == 1u;
    turn(EN_K4, 1); ok &= cin_bars == 1u;               /* (Dah: ON the last) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_TUNE;     /* (1.2: TUNE) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_SYNC;     /* (Dah: SYNC, SWING: CLOCK's) */
    turn(EN_K1, 1); ok &= song.g[G_CLOCK] == 1 && str_eq(menu_vname(MI_SYNC, menu_get(MI_SYNC)), "USB");
    turn(EN_K1, -1); ok &= song.g[G_CLOCK] == 0;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_SWING;
    turn(EN_K1, 1); ok &= song.g[G_SWING] == 5 && str_eq(menu_vname(MI_SWING, menu_get(MI_SWING)), "5%");
    turn(EN_K1, -1); ok &= song.g[G_SWING] == 0;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_LOWCUT;   /* (wraps within AUDIO) */
    press(B_HOME);
    ok &= !ui.menu;
    bad += check("MENU > AUDIO: PRESETS to CLICK, CLICK LEVEL, COUNT-IN, TUNE, SYNC, SWING; knobs / OCT step them, applied at once", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_rec_prefs = 0; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && (frame(), click_mode == CLICK_ON && click_lvl == 2u && cin_bars == 1u);
        p.favorites.factory[15][28] = 3u | 3u << 2 | 1u << 4;   /* CLICK and LEVEL no value, COUNT-IN 1 BAR */
        ok &= settings_import(&p, (int)sizeof p) == 1 && p.favorites.factory[15][28] == 1u << 4;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && cin_bars == 1u;
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before the favorites (PER3): the defaults */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_rec_prefs;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars;
    }
    bad += check("CLICK / CLICK LEVEL / COUNT-IN saved and read back; a stored 3 the default; older settings OFF / MID / OFF", ok);

    /* the count-in from the UI (Dah: the countdown, 3 2 1 as one big digit, the click from its first beat) */
    ui_power_on();
    menu_put(MI_COUNTIN, 1u);
    go_page(GR_ROLL);
    frame();
    press(B_REC);                                       /* (#133: on STEP REC arms step recording, stopped) */
    ok = (song.rec & 1u) && transport_req == 0u && !live_rec_sel();
    press(B_PLAY);
    ok &= transport_req == 1u;
    events_block(CTL);
    ok &= seq_counting() && !song.playing && cin_left == 4u && live_rec_sel();
    frame();
    {
        int32_t cy = H_HEAD + 18 + 8;                   /* (the digit's top segment: 4 has none, 3 has it) */
        ok &= px_at(120, cy) == px_at(8, cy);            /* (4: no top) */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;                  /* PLAY's green: on the beat */
        for (i = 0; i < 400u; i++) events_block(CTL);    /* (~0.3 s: past the first quarter of the beat) */
        ui_leds();
        ok &= !(fm1_led[8] & 2u);
        cin_beat();
        frame();
        ok &= cin_left == 3u && px_at(120, cy) != px_at(8, cy);   /* (3 now: its top) */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;
    }
    bad += check("count-in from the UI: REC on STEP arms, PLAY counts down (STEP entry paused); 4 3 2 1, one big digit; PLAY's green on the beats", ok);
    press(B_PLAY);
    ok = transport_req == 2u;
    events_block(CTL);
    frame();
    ok &= !seq_counting() && !song.playing && (song.rec & 1u) && px_at(160, H_HEAD + 18 + 110) != (uint16_t)(T_REC >> 8 | T_REC << 8);   /* (the digit gone) */
    bad += check("PLAY while counting in stops it: nothing plays, still armed, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    ok = seq_counting() && cin_left == 4u;
    for (i = 0; i < 4u; i++) cin_beat();
    frame();
    ok &= !seq_counting() && song.playing && trk[0].seq_idx == 0u && px_at(160, H_HEAD + 18 + 110) != (uint16_t)(T_REC >> 8 | T_REC << 8);
    bad += check("PLAY armed and stopped: the count-in again; at its end the sequencer plays from step 1, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    menu_put(MI_COUNTIN, 0u);
    stop_transport();
    return bad;
}

/* #67 MENU > USB SERIAL: ON by default (and in every older setting): the console presented, the descriptors as
 * before. OFF: audio + MIDI only, device class 0, bcdUSB 1.10 (CFG_DESC_PLAIN: a FELUCCA_CDC=0 build's bytes), applied
 * when the menu closes (one re-enumeration); saved; at boot the saved choice before USB starts (never on then off) */
static int dev_class(void)                              /* the device class GET_DESCRIPTOR sends now (-1: none) */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u ? d[4] : -1;
}
static int dev_is(const uint8_t *want)                  /* the device descriptor sent now = want */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u && !memcmp(d, want, 18);
}
static int plain_desc(void)                             /* device + configuration = the console-less bytes */
{
    const uint8_t *d, *c;
    uint16_t n, m;
    return get_desc(0x0100, &d, &n) && get_desc(0x0200, &c, &m) && n == 18u && !memcmp(d, DEV_DESC_PLAIN, 18) &&
           m == sizeof CFG_DESC_PLAIN && !memcmp(c, CFG_DESC_PLAIN, m);
}
static int test_usb_serial(void)
{
    int bad = 0, ok;
    ui_power_on();
    usb.up = 0;                                          /* (no SIE here: usb_cdc_switch only flips the descriptors) */
    frame();
    ok = usb_cdc_on && dev_class() == 0xEF && !(ui_prefs & PREF_SERIAL_OFF) && str_eq(MI_NAME[MI_SERIAL], "USB SERIAL") &&
         str_eq(menu_flag(MI_SERIAL)->name[0], "ON") && str_eq(menu_flag(MI_SERIAL)->name[1], "OFF");
    ok &= dev_is(DEV_DESC);
    bad += check("#67 MENU > USB SERIAL: ON by default, the console presented (misc / IAD, as before)", ok);
    hold(B_HOME);
    ui.menu_sel = MI_SERIAL;
    ui.force = 1; ui_draw();
    turn(EN_K1, -1);
    ok = (ui_prefs & PREF_SERIAL_OFF) && usb_cdc_on;    /* left: OFF; applied when the menu closes */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_SERIAL_OFF);   /* right: ON (the switch's knob right) */
    press(B_OCTDN); ok &= (ui_prefs & PREF_SERIAL_OFF) && ui.menu && usb_cdc_on;   /* OCT-: OFF */
    press(B_HOME);
    ok &= ui.menu == 0 && usb_cdc_on;                 /* (HOME acts as it is let go: closed this frame) */
    frame();
    ok &= !ui.menu && !usb_cdc_on && !usb.config && plain_desc() && dev_class() == 0;
    bad += check("  KNOB 1 left / OCT- OFF, right ON; the menu closed (HOME): audio + MIDI only, class 0", ok);
    {
        persist_t p;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;     /* (again: the same) */
        /* boot (main.c): the settings, usb_serial_apply, then usb_start: the console-less descriptors from the start */
        usb_cdc_on = 1;
        usb.up = 0;
        usb_serial_apply();
        ok &= !usb_cdc_on && !usb.up && plain_desc();
        memset(&p.favorites, 0, sizeof p.favorites);   /* settings from before it (PER3): ON */
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !(ui_prefs & PREF_SERIAL_OFF);
        usb_serial_apply();
        ok &= usb_cdc_on && dev_is(DEV_DESC);
    }
    bad += check("  saved with the settings (older ones: ON); at boot the device enumerates with the saved choice", ok);
    {   /* OFF: the soft key and the update frames still come in (USB-MIDI SysEx, EP1) */
        static const uint8_t KEY[8] = {0x04, 0xF0, 0x22, 0x24, 0x07, 0x35, 0x7D, 0xF7};
        ui_prefs = PREF_SERIAL_OFF;
        frame();
        usb.uboot_req = 0;
        ok = !usb_cdc_on && ep1_take(KEY, sizeof KEY) && usb.uboot_req;
        usb.uboot_req = 0;
        bad += check("  OFF: the soft key (SysEx on the MIDI endpoint) still asks for UPDATE MODE", ok);
    }
    ui_prefs = 0;
    frame();
    return bad;
}

/* #58 MENU > BPM LOCK: OFF by default (and in every older setting): SELECT sets the tempo on every page. ON: SELECT
 * leaves it on the pages ("BPM LOCKED" once per turn burst); HOME > CLOCK's BPM knob, GLO + SELECT (a combo: the map
 * opens, no GLO page; OCT- puts it back) and GLO's F4 TAP still set it; the menu never takes SELECT as the tempo */
static int test_bpm_lock(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t i, page;
    ui_power_on();
    ui_prefs = 0;
    frame();
    b0 = song.g[G_BPM];
    tempo_turn(2);
    ok = song.g[G_BPM] == b0 + 2 && !msg_is("BPM LOCKED") && str_eq(menu_flag(MI_BPMLOCK)->name[0], "OFF");
    frame();
    ok &= song.octave == 0;
    turn(EN_SELECT, 1);                                 /* (Dah: SELECT alone: the next pattern, a new one: asked) */
    ok &= ui.confirm == CF_NEW_PAT;
    press(B_OCTUP);
    ok &= song.g[G_BPM] == b0 + 2 && ps_num(0) == 2u;
    turn(EN_SELECT, -1);
    ok &= ps_num(0) == 1u;
    bad += check("#58 (Dah) OCT+ held + SELECT sets the tempo (no octave after), SELECT alone the pattern", ok);

    hold(B_HOME);
    ui.menu_sel = MI_BPMLOCK;
    ui.force = 1; ui_draw();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 3);
    ok = str_eq(MI_NAME[MI_BPMLOCK], "BPM LOCK") && ui.menu == 1u && song.g[G_BPM] == b0;   /* (the menu: no tempo) */
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_BPM_LOCK) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_BPM_LOCK);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_BPM_LOCK) && ui.menu;      /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_BPM_LOCK) && ui.menu;     /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_BPM_LOCK;                                  /* (no other setting moved) */
    press(B_HOME);
    bad += check("#58 MENU > BPM LOCK: KNOB 1 right / OCT+ ON, left / OCT- OFF; SELECT in the menu is no tempo", ok && !ui.menu);

    /* ON: no tempo from SELECT on HOME and the pages; "BPM LOCKED" once per burst */
    ok = 1;
    for (i = 0; i < 3u; i++) {
        if (i == 1u) go_title("ENV");
        if (i == 2u) go_title("STEP");
        frame();
        ui.msg_t = 0; fm1_ms += 1200u;
        b0 = song.g[G_BPM];
        turn(EN_SELECT, 1);
        ok &= song.g[G_BPM] == b0 && !ui.bpm_t;
        turn(EN_SELECT, -4);
        ok &= song.g[G_BPM] == b0;
    }
    ok &= text_w(&AF_S, "BPM LOCKED") <= 236 - 106;                   /* (fits the header's message zone) */
    bad += check("#58 BPM LOCK ON: SELECT leaves the tempo on HOME and the pages (Dah: the pattern's)", ok);

    /* ON: OCT+ held + SELECT (Dah: CLOCK's page gone) */
    go_home(); frame();
    b0 = song.g[G_BPM];
    tempo_turn(1);
    frame();
    ok = song.g[G_BPM] == b0 + 1;
    bad += check("#58 BPM LOCK ON: OCT+ held + SELECT still sets the tempo (Dah)", ok);

    /* ON: GLO + SELECT (a quick turn: a combo, no GLO page), OCT- puts it back, F4 TAP */
    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    ui.msg_t = 0;
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 + 3 && ui.bpm_t && !msg_is("BPM LOCKED") && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= ui.page == page && !ui.home;                                /* (no tap: still ENV) */
    btn_down(B_GLO); frame();
    turn(EN_SELECT, -5);
    ok &= song.g[G_BPM] == b0 - 2;
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + SELECT sets the tempo (a combo: the map, no GLO page)",
                 ok && ui.page == page);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(7)); frame(); key_up(white(7)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + F4 TAP still sets the tempo", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        persist_t p, q;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_BPM_LOCK;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_prefs == PREF_BPM_LOCK && !memcmp(&p, &q, sizeof p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                        /* (PER3: before the favorites record) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        frame();
        b0 = song.g[G_BPM];
        go_home(); frame();
        tempo_turn(1);
        ok &= song.g[G_BPM] == b0 + 1;
        bad += check("#58 BPM LOCK saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* #15 / Discussion #80 MENU > LARGE: OFF by default (and in every older setting); KNOB 1 right ON / left OFF, OCT+
 * toggles, nothing else moves. ON: HOME and the value pages draw tall cards (a K1..K4 pill over each knob's column,
 * the value in L) and the strip under them; the list / graph pages and the layers keep the layout (their labels in
 * M); the footer stays; a knob turned redraws its own card only; saved with the settings, older settings OFF
 * (idempotent) */
static uint16_t lg_px(int32_t x, int32_t y) { return swap16(host_screen[y * 240 + x]); }
static int lg_value_rows(uint32_t c)                    /* rows of card c's value band holding the value's colour */
{
    int32_t x, y, n = 0;
    for (y = Y_LABEL + 40; y < Y_LABEL + 76; y++)
        for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
            if (lg_px(x, y) == T_THEME) { n++; break; }
    return n;
}
static int test_large(void)
{
    int bad = 0, ok;
    uint32_t c;
    static uint16_t off[240 * 240];
    ui_power_on();
    song.playing = 0;
    frame();
    ok = !(ui_prefs & PREF_LARGE) && large_kind() == LK_OFF && card_h() == CARD_H && graph_y() == Y_GRAPH;
    ok &= MI_LARGE == MI_STYLE + 1u && str_eq(MI_NAME[MI_LARGE], "LARGE") && menu_flag(MI_LARGE) &&
          str_eq(menu_flag(MI_LARGE)->name[0], "OFF") && str_eq(menu_flag(MI_LARGE)->name[1], "ON");
    ui.force = 1; frame();
    memcpy(off, host_screen, sizeof off);
    hold(B_HOME); ui.menu_sel = MI_LARGE;
    ok &= ui.menu == 1u;
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_LARGE) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_LARGE);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_LARGE) && ui.menu;     /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_LARGE) && ui.menu;    /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_LARGE && ui_style == ST_FLAT;          /* (no other setting moved) */
    press(B_HOME);
    bad += check("#15 MENU > LARGE: OFF by default, under STYLE; KNOB 1 right / OCT+ ON, left / OCT- OFF", ok && !ui.menu);

    /* HOME: tall cards (rows 28..132) and the strip (136..198); the footer as it was */
    frame();
    ok = ui.home && large_kind() == LK_TALL && card_h() == LG_CARD_H && graph_y() == LG_Y_GRAPH && strip_kind() == SK_SCOPE;
    ok &= lg_px(30, Y_SEP_END + 1) == T_SURF && off[(Y_SEP_END + 1) * 240 + 30] == swap16(T_BG);     /* a card where the gap was */
    ok &= lg_px(30, LG_Y_GRAPH - 2) == T_BG && off[(LG_Y_GRAPH - 2) * 240 + 30] == swap16(T_SURF);   /* a gap in the panel */
    ok &= lg_px(30, LG_Y_GRAPH + 1) == T_SURF;
    for (c = 0; c < 4u; c++)                                      /* each column's K pill, over its knob */
        ok &= lg_px(CARD_X(c) + 8, Y_LABEL + LG_MY + LG_MH / 2) == T_KEY;
    ok &= !memcmp(off + (Y_FOOT + FOOT_GAIN) * 240, host_screen + (Y_FOOT + FOOT_GAIN) * 240,
                  240u * (H_FOOT - FOOT_GAIN) * sizeof *off);   /* (Dah: its second row; LARGE OFF the first is the panel's) */
    bad += check("#15 LARGE ON: HOME's cards tall (a K1..K4 pill over each column), the scope a strip, the footer kept", ok);

    /* the value in L: CLOCK's BPM 124 stands about twice as tall as in M */
    go_title("ENV"); page_over = &ROLL_PG; ui.force = 1; frame();   /* (Dah: CLOCK's page gone: the test's own) */
    ok = large_kind() == LK_TALL && strip_kind() == SK_TITLE && cur_page()->id[0] == G_BPM && lg_value_rows(0) >= AF_L_CAP_H - 3;
    ui_prefs = 0; ui.force = 1; frame();
    ok &= lg_value_rows(0) <= AF_M_CAP_H + 2;
    ui_prefs = PREF_LARGE; ui.force = 1; frame();
    page_over = 0;
    bad += check("#15 LARGE: a card value in L (CLOCK's BPM 120: about 21 rows of figures, M's 11)", ok);

    /* a knob turned: its own card redrawn, the other three not */
    go_title("ENV"); frame(); frames(600);
    memcpy(off, host_screen, sizeof off);
    turn(EN_K2, 3);
    {
        int32_t x, y, diff[4] = {0, 0, 0, 0};
        for (y = Y_LABEL; y < Y_LABEL + LG_CARD_H; y++)
            for (c = 0; c < 4u; c++)
                for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
                    diff[c] += off[y * 240 + x] != host_screen[y * 240 + x];
        ok = large_kind() == LK_TALL && strip_kind() == SK_ADSR && diff[1] && !diff[0] && !diff[2] && !diff[3];
    }
    bad += check("#15 LARGE: ENV tall with its ADSR in the strip; KNOB 2 turned redraws card 2 only", ok);

    /* per page type: lists and rolls keep the layout, the layers too; MIXER's tracks in the strip */
    go_page(GR_USER); frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && graph_h() == H_GRAPH + FOOT_GAIN;
    go_page(GR_ROLL); frame();
    ok &= large_kind() == LK_LABEL;
    go_page(GR_TRK); frame();
    ok &= large_kind() == LK_TALL && strip_kind() == SK_TRK && lg_px(CARD_X(0) + 8, LG_Y_GRAPH + 56) == T_SURF;
    go_title("ENV"); frame();
    btn_down(B_GLO); frames(800);
    ok &= ui.layer && large_kind() == LK_LABEL && card_h() == CARD_H;
    btn_up(B_GLO); frame(); frame();
    ok &= !ui.layer && large_kind() == LK_TALL && lg_px(30, LG_Y_GRAPH - 2) == T_BG;
    bad += check("#15 LARGE: PRESETS and the roll keep the layout (labels in M), the layers too; MIXER tall, its tracks in the strip", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        persist_t p, q;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_LARGE;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_prefs == PREF_LARGE && !memcmp(&p, &q, sizeof p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                                    /* (PER3: before the favorites record) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        ui.force = 1; frame();
        ok &= large_kind() == LK_OFF && lg_px(30, Y_SEP_END + 1) == T_BG;
        bad += check("#15 LARGE saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* MENU (1.0.5: in tabs): every row reachable with ALGORITHM and PRESETS, each tab its own page; #46 MENU > ANIM: ON by
 * default (and in every older setting), OFF snaps the rolling digits, the piano roll and the menu's tabs */
static int test_menu_prefs(void)
{
    int bad = 0, ok = 1;
    uint32_t k, j;
    uint64_t seen = 0;                               /* (1.5.5: more than 32 rows) */
    static uint16_t shot[MTAB_COUNT][240 * 240];
    ui_power_on();
    ui_prefs = 0;
    hold(B_HOME);
    ui.menu_sel = 0;
    for (k = 0; k < MTAB_COUNT; k++) {              /* ALGORITHM right through the tabs, PRESETS through each one's rows */
        ok &= menu_tab() == k && ui.menu_sel == mtab_first(k);
        for (j = 0; j < mtab_rows(k); j++) {
            seen |= 1ull << ui.menu_sel;
            turn(EN_PRESET, 1);
        }
        ok &= ui.menu_sel == mtab_first(k);         /* (round the tab, back at its first row) */
        ui.force = 1; ui_draw();
        memcpy(shot[k], host_screen, sizeof shot[k]);
        for (j = 0; j < k; j++) ok &= memcmp(shot[j], shot[k], sizeof shot[k]) != 0;
        turn(EN_ALGO, 1);
    }
    ok &= seen == (1ull << MI_COUNT) - 1u && ui.menu_sel == 0u;
    bad += check("MENU: ALGORITHM and PRESETS reach every row, each tab a page of its own", ok);
    /* ANIM */
    ui.menu_sel = MI_ANIM;
    ok = str_eq(MI_NAME[MI_ANIM], "ANIM") && !(ui_prefs & PREF_ANIM_OFF);
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF) && (ui_idle & 1u) && menu_get(MI_ANIM) == 2u;   /* right: IDLE (1.2) */
    turn(EN_K1, 1); ok &= (ui_idle & 1u) && str_eq(menu_vname(MI_ANIM, menu_get(MI_ANIM)), "IDLE");  /* (the end) */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_ANIM_OFF) && !ui_idle && menu_get(MI_ANIM) == 0u;      /* left: ON */
    turn(EN_K1, -1); ok &= (ui_prefs & PREF_ANIM_OFF) != 0 && !ui_idle;                             /* left: OFF */
    turn(EN_K1, -1); ok &= (ui_prefs & PREF_ANIM_OFF) != 0;                                         /* (the end) */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF) && !ui_idle;                                  /* right: ON */
    press(B_OCTDN); ok &= (ui_prefs & PREF_ANIM_OFF) && ui.menu;  /* OCT-: OFF */
    ok &= !(ui_prefs & ~(uint32_t)PREF_ANIM_OFF);                 /* (no other setting moved) */
    press(B_HOME);
    bad += check("#46 MENU > ANIM: ON by default, KNOB 1 / OCT steps OFF ON IDLE (1.2), the values 0 ON 1 OFF 2 IDLE",
                 ok && !ui.menu && str_eq(ANIM_N[0], "ON") && str_eq(ANIM_N[1], "OFF"));
    ui_prefs = 0;
    ui_idle = 0;
    ui_power_on(); roll_settle();
    turn(EN_K4, 1);
    ok = song.g[G_TUNE] == 1 && ui.roll[3].from[0];              /* ON: TUNE 0 -> 1 rolls */
    ui_power_on(); ui_prefs = PREF_ANIM_OFF; roll_settle();      /* (ui_power_on clears the favorites record) */
    turn(EN_K4, 1);
    ok &= song.g[G_TUNE] == 1 && !ui.roll[3].from[0] && roll_static_now();   /* OFF: the value at once, static */
    tempo_turn(1);
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("#46 ANIM OFF: the card values and the header BPM snap (no rolling digits), the frame is static", ok);
    {   /* the piano roll's view: ON glides toward the notes over frames, OFF jumps there at once */
        track_t *t = TSEL;
        uint32_t lo_on;
        ui_power_on();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[0].n = 1; t->step[0].note[0] = 30; t->step[0].time = ST_NOTE;
        ui.force = 0;
        proll.init = 0; pr_follow(t); ui.frame++;
        t->step[0].note[0] = 90; pr_follow(t); lo_on = proll.lo;
        ui_prefs = PREF_ANIM_OFF;
        proll.init = 0; t->step[0].note[0] = 30; pr_follow(t); ui.frame++;
        t->step[0].note[0] = 90; pr_follow(t);
        ok = proll.moving == 0 && proll.lo > lo_on && lo_on != 0u;
        if (!ok) printf("  piano roll view: ON %u, OFF %u (moving %u)\n", lo_on, proll.lo, proll.moving);
        bad += check("#46 ANIM OFF: the piano roll's view jumps to the notes (ON: it glides)", ok);
    }
    {   /* saved with the settings; settings from before it: ON */
        persist_t p;
        ui_prefs = PREF_ANIM_OFF;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (ui_prefs & PREF_ANIM_OFF);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && !ui_prefs;
        bad += check("#46 ANIM saved with the settings; settings from before it: ON", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* MENU > STYLE (ui_menu.c, gfx.c ST_*): FLAT by default; KNOB 1 right LINE, left FLAT (stops at the ends), OCT+
 * toggles; drawn so from the next frame (ux.style, the tokens: LINE has no SURF, a rule under the header); saved in a
 * spare byte of the settings and read back; settings from before it, and an unknown value, read as FLAT; 2, the
 * retired PIXEL (1.0.1), as LINE (again on a second import: idempotent) */
static int test_style(void)
{
    int bad = 0, ok;
    const uint32_t rule = 26u * 240u + 120u;           /* (the rule under the header: ui_draw.c draw_rules) */
    ui_power_on();
    frame();
    ok = ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf != ux.bg;
    ui.force = 1; frame();
    ok &= host_screen[rule] == swap16(T_BG);
    hold(B_HOME); ui.menu_sel = MI_STYLE;
    ok &= ui.menu == 1u && str_eq(MI_NAME[MI_STYLE], "STYLE");
    turn(EN_K1, 1); ok &= ui_style == ST_LINE && ux.style == ST_LINE && ux.surf == ux.bg;
    turn(EN_K1, 1); ok &= ui_style == ST_LINE;
    turn(EN_K1, -1); ok &= ui_style == ST_FLAT;
    turn(EN_K1, -1); ok &= ui_style == ST_FLAT;
    press(B_OCTUP); ok &= ui_style == ST_LINE && ui.menu == 1u;
    press(B_OCTUP); ok &= ui_style == ST_LINE;
    press(B_OCTDN); ok &= ui_style == ST_FLAT && ux.style == ST_FLAT && ux.surf != ux.bg && ui.menu == 1u;
    press(B_OCTUP); ok &= ui_style == ST_LINE;
    ok &= !(ui_prefs & 0xFFu) && settings_leds == LEDS_DIM;   /* (no other setting moved) */
    hold(B_HOME);
    ok &= !ui.menu && ux.style == ST_LINE && host_screen[rule] == swap16(T_RULE);
    bad += check("MENU > STYLE: FLAT by default, KNOB 1 / OCT-+ FLAT / LINE, LINE drawn (rules, no SURF)", ok);
    {
        persist_t p, q;
        settings_export(&p);
        ui_style = ST_FLAT; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_LINE;
        frame();
        ok &= ux.style == ST_LINE;
        p.favorites.factory[15][29] = 7;                /* unknown: FLAT, and so again */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_FLAT && !p.favorites.factory[15][29];
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_style == ST_FLAT && !memcmp(&p, &q, sizeof p);
        p.favorites.factory[15][29] = 2;                /* the retired PIXEL: LINE, and so again */
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_style == ST_LINE && p.favorites.factory[15][29] == ST_LINE;
        q = p;
        ok &= settings_import(&q, (int)sizeof q) == 1 && ui_style == ST_LINE && !memcmp(&p, &q, sizeof p);
        frame();
        ok &= ux.style == ST_LINE;
        ui_style = 2; frame();                          /* (a live PIXEL byte, a restore: LINE) */
        ok &= ui_style == ST_LINE && ux.style == ST_LINE;
        ui_style = ST_LINE;
        settings_export(&p);
        memset(&p.favorites, 0, sizeof p.favorites);
        p.magic = 0x50455233u;                          /* settings from before STYLE (PER3) */
        ok &= settings_import(&p, (int)(sizeof p - sizeof p.favorites)) == 2 && ui_style == ST_FLAT;
        frame();
        ok &= ux.style == ST_FLAT && ux.surf != ux.bg;
        bad += check("  STYLE saved with the settings and read back; older settings and unknown values: FLAT, PIXEL: LINE (idempotent)", ok);
    }
    ui_power_on(); frame();
    return bad;
}

/* the header (ui_draw.c draw_head, 1.2's three slots): its elements measured in the render (every pixel off BG; an
 * element is a run of inked columns, split at 2 empty columns and at the battery's cell), each one's ink centred on row
 * 12 (top + bottom + 1 within 1 px of H_HEAD); the track cushion's ink from x 4; the battery's in 214..236, nothing
 * between 208 and it; with the centre shown the BPM's ink within 2 px of x 120; a message / a label centred on x 120
 * (its ink between 40 and 205, within 3 px); GLO with the tempo turned: the label right of the BPM and its lock, no
 * battery. Every state of the header, FLAT and LINE, every palette */
#define HEAD_EL 32
static int32_t hel[HEAD_EL][4], nhel;                  /* x0 x1 (last inked column) top bot */
static int head_centres(const char *what, int print)
{
    uint16_t bg = swap16(T_BG);
    int32_t x = 0, ok = 1;
    nhel = 0;
    while (x < 240) {
        int32_t x0, x1, top = 99, bot = -1, gap = 0, y, ink;
        for (; x < 240; x++) {                        /* the next inked column */
            for (y = 0, ink = 0; y < H_HEAD; y++) ink |= host_screen[(Y_HEAD + y) * 240 + x] != bg;
            if (ink) break;
        }
        if (x >= 240) break;
        x0 = x1 = x;
        for (; x < 240 && gap < 2; x++) {
            if (x == HEAD_BAT_X && x != x0) break;     /* the battery's cell ends the element */
            for (y = 0, ink = 0; y < H_HEAD; y++)
                if (host_screen[(Y_HEAD + y) * 240 + x] != bg) {
                    ink = 1;
                    if (y < top) top = y;
                    if (y > bot) bot = y;
                }
            gap = ink ? 0 : gap + 1;
            if (ink) x1 = x;
        }
        x = x1 + 1;
        if (print) printf("    head %-10s x %3d..%3d ink rows %2d..%2d centre %4.1f (%+.1f)\n", what, x0, x1, top, bot,
                          (top + bot + 1) / 2.0, (top + bot + 1) / 2.0 - H_HEAD / 2);
        ok &= top + bot + 1 >= H_HEAD - 2 && top + bot + 1 <= H_HEAD + 2;
        if (nhel < HEAD_EL) { hel[nhel][0] = x0; hel[nhel][1] = x1; hel[nhel][2] = top; hel[nhel][3] = bot; nhel++; }
    }
    return ok;
}
/* the elements inside columns a .. b as one span (x0 .. x1): 2 x its centre, 0 none */
static int32_t head_span2(int32_t a, int32_t b, int32_t *x0, int32_t *x1)
{
    int32_t i;
    *x0 = 999; *x1 = -1;
    for (i = 0; i < nhel; i++)
        if (hel[i][0] >= a && hel[i][1] <= b) {
            if (hel[i][0] < *x0) *x0 = hel[i][0];
            if (hel[i][1] > *x1) *x1 = hel[i][1];
        }
    return *x1 < 0 ? 0 : *x0 + *x1 + 1;
}
enum { HS_STOPPED, HS_PLAY_REC, HS_SONG, HS_MSG, HS_OCT, HS_USB, HS_KEYCAP, HS_REC_OTHER, HS_BPM_TURN, HS_BPM_LOCK,
       HS_NOFILE, HS_REC_LAYER, HS_LAYER_LOCK, HS_GLO_TURN, HS_MSG2, HS_BEAT2, HS_COUNTIN, HS_BATT1, HS_BATT0, HS_BPM90,
       HS_N };
static const char *const HS_NAME[HS_N] = {"stopped", "play+REC", "song", "message", "OCT", "USB", "keycap", "REC other",
    "BPM turn", "BPM LOCK", "NO SAMPLE", "REC layer", "EDIT lock", "GLO turn", "msg2", "beat 2", "count-in", "battery 1",
    "battery 0", "BPM 90"};
static void head_state(uint32_t s)
{
    song.playing = s == HS_PLAY_REC || s == HS_SONG || s == HS_BEAT2 || s == HS_OCT;
    song.rec = s == HS_PLAY_REC || s == HS_COUNTIN ? 1u << song.sel : s == HS_REC_OTHER ? 1u << ((song.sel + 1u) % NTRK) : 0u;
    clk_pos = 0; clk_step = s == HS_BEAT2 ? 4u : 8u;    /* (the 2nd beat: the metronome mirrored; else the 3rd) */
    if (s == HS_SONG) { chain.running = 1; chain.row = 2; }
    if (s == HS_MSG) ui_message("SAVED 03 BASS");
    if (s == HS_MSG2) ui_say("LOADED ", "07 A VERY LONG PATTERN NAME");
    if (s == HS_OCT) song.octave = 2;
    usb.config = s == HS_USB; usb.suspended = 0;
    song.batt_raw = s == HS_BATT1 ? 540 : s == HS_BATT0 ? 500 : 600;
    if (s == HS_KEYCAP) ui_message("[OCT+] SAVE");
    if (s == HS_BPM_TURN || s == HS_GLO_TURN) ui.bpm_t = 40;
    if (s == HS_BPM_LOCK || s == HS_GLO_TURN) ui_prefs |= PREF_BPM_LOCK;
    if (s == HS_NOFILE) ui_message(MSG_NO_SAMPLE);
    if (s == HS_REC_LAYER) { song.rec = 1u << song.sel; ui.layer = LAYER_REC; }
    if (s == HS_LAYER_LOCK) ui.layer = ui.lock = LAYER_EDIT;
    if (s == HS_GLO_TURN) ui.layer = LAYER_GLO;
    if (s == HS_COUNTIN) { cin_total = 4; cin_left = 3; }
    song.g[G_BPM] = s == HS_BPM90 ? 90 : 124;
    ui.roll_bpm = song.g[G_BPM];
}
static void head_clear(void)
{
    chain.running = 0; ui.msg_t = 0; song.octave = 0; usb.config = 0; song.playing = 0; song.rec = 0; ui.bpm_t = 0;
    ui_prefs &= ~PREF_BPM_LOCK; ui.layer = ui.lock = 0; cin_total = cin_left = 0; song.batt_raw = 600;
}
static int test_head_centres(void)
{
    int bad = 0, ok = 1, place = 1;
    uint32_t st, p, s;
    for (st = ST_FLAT; st <= ST_LINE; st++)
        for (p = 0; p < NPALETTES; p++)
            for (s = 0; s < HS_N; s++) {
                int32_t a, b, c2, msg = s == HS_MSG || s == HS_KEYCAP || s == HS_NOFILE || s == HS_REC_LAYER ||
                                        s == HS_LAYER_LOCK || s == HS_MSG2;
                int pr = !st && !p, okp, rail;
                ui_power_on();
                ui_style = (uint8_t)st; style_apply();
                settings.palette = p; palette_set(p);
                head_state(s);
                rail = !ui.msg_t && !ui.layer;
                memset(host_screen, 0, sizeof host_screen);
                ui.force = 1; draw_head(); ui.force = 0;
                ok &= head_centres(HS_NAME[s], pr);
                okp = nhel > 0 && hel[0][0] == (rail ? 3 : 4);   /* the track cushion first, its ink from x 4 (
                                                        * the rail's, Dah: the MASTER's M from x 3) */
                if (s != HS_GLO_TURN) {                /* the battery last, in its cell; nothing between 208 and it */
                    okp &= head_span2(HEAD_BAT_X, 239, &a, &b) && a >= 214 && b <= 236;
                    okp &= !head_span2(HEAD_GRP_R, HEAD_BAT_X - 1, &a, &b);
                } else {                               /* GLO, the tempo turned: the BPM, its lock, the label after */
                    head_geo_t g;
                    head_geo(&g, "124");
                    okp &= head_span2(g.bx + g.b[2] + 20, 239, &a, &b) && a >= 142;
                }
                if (!msg) {                            /* 1.5.6: the group's ink on x 120 (the play state .. the BPM /
                                                        * its lock; a song's disc 4 px wider than the slot: -2.5) */
                    head_geo_t g;
                    head_geo(&g, s == HS_BPM90 ? "90" : "124");
                    c2 = head_span2(g.gx0 - 6, g.gx1 - 1, &a, &b);
                    if (rail)                          /* (from x 104, right of the rail; clear of the
                                                        * octave / song row and the pattern's badge) */
                        okp &= a >= RAIL_GX - 6 && b < (s == HS_OCT || s == HS_SONG ? 176 : HEAD_PAT_X - 4);
                    else {
                        okp &= s == HS_SONG ? c2 >= 234 && c2 <= 242 : c2 >= 238 && c2 <= 242;
                        okp &= a >= 40 && b < (s == HS_OCT ? 160 : 180);   /* (clear of the REC mark, the octave) */
                    }
                } else {                               /* the message centred on x 120 */
                    c2 = head_span2(40, 205, &a, &b);
                    okp &= c2 >= 234 && c2 <= 246 && a >= 40 && b <= 205;
                }
                if (pr && !okp) printf("    head %s: misplaced\n", HS_NAME[s]);
                place &= okp;
                head_clear();
            }
    ui_style = ST_FLAT; style_apply();
    bad += check("header: every element's ink centred on row 12 (+-1 px), every state, FLAT and LINE, every palette", ok);
    bad += check("header: the rail from 2, the BPM group from 104 (a message on x 120), octave / pattern to 208, battery "
                 "214..236 (GLO with the tempo turned: the label right of the BPM and its lock)", place);
    ui_power_on();
    return bad;
}

/* the header's metronome (ui_draw.c head_metro): it swings on the beats the click is on (seq.c click_beat_now): the
 * bar's 2nd and 4th beats mirrored, the 1st the accent's, the 3rd as stopped; a beat redraws its ink only (the rest
 * of the screen untouched, the header's signature kept) */
static int test_head_metronome(void)
{
    static uint16_t shot[4][H_HEAD * 240], still[H_HEAD * 240];
    head_geo_t g;
    int bad = 0, ok = 1, only = 1, b;
    int32_t x, y, w;
    ui_power_on();
    settings.palette = 0; palette_set(0);
    song.g[G_BPM] = 124; ui.roll_bpm = 124;
    head_geo(&g, "124");
    w = g.mx1 - g.mx0;
    ui.force = 1; draw_head(); ui.force = 0;           /* stopped */
    memcpy(still, host_screen, sizeof still);
    song.playing = 1; clk_pos = 0;
    for (b = 0; b < 4; b++) {
        uint32_t sig;
        clk_step = (uint32_t)b * 4u + (b & 1u) * 2u;   /* (inside the beat: its 1st or 3rd 1/16) */
        ui.force = 1; draw_head(); ui.force = 0;
        memcpy(shot[b], host_screen, sizeof shot[b]);
        sig = ui.head_sig;
        clk_step = (uint32_t)((b + 1) % 4) * 4u;       /* the next beat: only the metronome's ink changes */
        draw_head();
        only &= ui.head_sig == sig;
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < 240; x++)
                if (host_screen[y * 240 + x] != shot[b][y * 240 + x]) only &= x >= g.mx0 && x < g.mx1;
    }
    for (b = 0; b < 4; b++)                            /* 1.1.5: no swing: every beat as stopped */
        for (y = 0; y < H_HEAD; y++)
            for (x = 0; x < w; x++)
                ok &= shot[b][y * 240 + g.mx0 + x] == still[y * 240 + g.mx0 + x];
    bad += check("header metronome: still on every beat, as stopped", ok);
    bad += check("header metronome: a beat changes nothing in the header", only);
    song.playing = 0;
    ui_power_on();
    return bad;
}

/* 1.5.6: the header's centre group (ui_draw.c head_geo): the ink from the play state's left to the BPM's (or its
 * lock's) right centred on x 120 within +-1 px, 2- and 3-digit tempi, stopped and playing, BPM LOCK off and on; the
 * group (the play state, the metronome) stays put on PLAY and while the tempo changes within a digit count */
static int test_head_group(void)
{
    static const int16_t T[] = {40, 60, 88, 99, 100, 120, 124, 188, 200, 240};
    int bad = 0, ok = 1, still = 1;
    int32_t m0[2][2] = {{-1, -1}, {-1, -1}};
    uint32_t i, pl, lk;
    for (lk = 0; lk < 2u; lk++)
        for (i = 0; i < sizeof T / sizeof T[0]; i++)
            for (pl = 0; pl < 2u; pl++) {
                int32_t a, b, c2, j, play = -1, metro = -1;
                head_geo_t g;
                char s[8];
                ui_power_on();
                settings.palette = 0; palette_set(0);
                if (lk) ui_prefs |= PREF_BPM_LOCK;
                song.playing = (uint8_t)pl; clk_pos = 0; clk_step = 8u;
                song.g[G_BPM] = T[i]; ui.roll_bpm = T[i];
                fmt_int(s, T[i]);
                head_geo(&g, s);
                memset(host_screen, 0, sizeof host_screen);
                ui.force = 1; draw_head(); ui.force = 0;
                head_centres("group", 0);
                c2 = head_span2(g.gx0 - 6, g.gx1 + 2, &a, &b);
                ok &= a >= RAIL_GX - 1 && a <= RAIL_GX + 3 && b < HEAD_PAT_X - 4;   /* (right of the rail) */
                for (j = 0; j < nhel; j++) {           /* the play state, the metronome: their ink's left */
                    if (hel[j][0] >= a && play < 0) play = hel[j][0];
                    else if (hel[j][0] > a && metro < 0) metro = hel[j][0];
                }
                still &= metro == g.mx0 && play >= g.px0 && play <= g.px0 + 2;
                if (m0[lk][T[i] >= 100] < 0) m0[lk][T[i] >= 100] = metro;
                still &= m0[lk][T[i] >= 100] == metro; /* (the same for every tempo of as many digits) */
                if (!lk && (T[i] == 60 || T[i] == 120 || T[i] == 200))
                    printf("    head group %3d BPM %-7s ink x %3d..%3d centre %5.1f (play state %3d.., metronome %3d..)\n",
                           T[i], pl ? "playing" : "stopped", a, b, c2 / 2.0, play, metro);
                ui_prefs &= ~PREF_BPM_LOCK;
                song.playing = 0;
            }
    bad += check("header: the group (play state .. BPM / lock) from x 104, right of the rail, clear of the pattern: 2 and "
                 "3 digits, stopped and playing, BPM LOCK off and on", ok);
    bad += check("header: the play state and the metronome stay put on PLAY / STOP and while the tempo changes", still);
    ui_power_on();
    return bad;
}

/* SONG lanes (1.2, song_chain.c): a section names a slot per track ("-": silent). The rows of before (one slot each)
 * play bit for bit as before lanes (tests/song_probe.h, against the firmware before); each lane plays its own slot's
 * steps, LEN / DIV, QUANTIZE and automation; "-" plays nothing and releases; the section's length is its clock
 * track's loop (the first lane not silent; every lane silent: a 4/4 bar); all four restart together; STOP restores */
#include "song_probe.h"
/* a section of tracks 1..4's slots s[0..3], tracks 5..8 silent */
static chain_row_t lrow(const uint8_t *s, uint32_t rep)
{
    chain_row_t w;
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        w.slot[k] = k < NTRK_V10 ? s[k] : CHAIN_SILENT;
    w.repeat = (uint8_t)rep;
    return w;
}
static uint32_t lanes_until(uint32_t row, uint32_t max)   /* blocks of 32 until the section changes (or the song ends) */
{
    uint32_t n = 0;
    while (n < max && chain.running && chain.row == row) {
        events_block(32);
        chain_poll();                                 /* (the main loop: the next section ready) */
        n++;
    }
    return n;
}
static int lanes_near(uint32_t blocks, uint32_t samples) { return blocks * 32u + 32u >= samples && blocks * 32u <= samples + 32u; }   /* (to a block) */
static int test_song_lanes(void)
{
    int bad = 0, ok;
    uint32_t n = 0, h, k, i, st, rev[NTRK];
    h = song_probe(&n); if (getenv("PROBE_PRINT")) printf("PROBE %08X %u\n", h, n);
    bad += check("SONG of rows (one slot each): bit for bit as before lanes, block by block (song_probe.h)",
                 h == SONG_PROBE_GOLDEN && n == SONG_PROBE_BLOCKS);
    seq_stop();
    ui_power_on();
    pat_park();                                           /* (A, B below: patterns 1, 2) */
    song_round = 0;                                       /* (Dah: these songs stop at their end, as before) */
    for (k = 0; k < NTRK_V10; k++) rev[k] = (uint32_t)trk[k].p[P_REV];
    /* A: notes 60 + k, LEN 4 of 1/16, QUANTIZE ON; T3 automated (REV 100 on its step 1). B: notes 72 + k, T1 LEN 8,
     * T2 LEN 6, T3 LEN 4 of 1/8 QUANTIZE OFF, T4 LEN 2 */
    for (st = 0; st < 2u; st++) {
        static const int16_t LEN[2][NTRK_V10] = {{4, 4, 4, 4}, {8, 6, 4, 2}};
        for (k = 0; k < NTRK_V10; k++) {
            track_t *t = &trk[k];
            track_defaults_steps(t);
            motion_clear(t);
            for (i = 0; i < 16u; i++) {
                t->step[i].note[0] = (uint8_t)((st ? 72u : 60u) + k);
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
                t->step[i].vel = 100;
            }
            t->p[P_SLEN] = LEN[st][k];
            t->p[P_SDIV] = st && k == 2u ? 1 : 2;
            t->p[P_SSWING] = 0;
            t->p[P_SGATE] = 120;
            t->p[P_SQNT] = !(st && k == 2u);
        }
        if (!st) {
            motion_set_event(&trk[2], 0, P_REV, 100);
        }
        pat_keep(st + 1u);
    }
    for (k = 0; k < NTRK_V10; k++) {                      /* the working patterns: notes 48 + k, LEN 3 of 1/16, no automation */
        track_t *t = &trk[k];
        motion_clear(t);
        for (i = 0; i < 16u; i++) t->step[i].note[0] = (uint8_t)(48u + k);
        t->p[P_SLEN] = 3;
        t->p[P_SDIV] = 2;
        t->p[P_SQNT] = 1;
    }
    chain_config.count = 4;
    chain_config.row[0] = lrow((const uint8_t[]){1, 2, CHAIN_SILENT, 2}, 1);
    chain_config.row[1] = lrow((const uint8_t[]){CHAIN_SILENT, 2, 1, CHAIN_SILENT}, 2);
    chain_config.row[2] = lrow((const uint8_t[]){CHAIN_SILENT, CHAIN_SILENT, CHAIN_SILENT, CHAIN_SILENT}, 1);
    chain_config.row[3] = lrow((const uint8_t[]){2, 1, 2, 1}, 1);
    bad += check("SONG lanes prepare: the slots the lanes name (A, B), the silent ones none", chain_prepare() == 0);
    events_block(32);
    ok = chain.running && chain.row == 0u && chain.clk == 0u;
    ok &= seq_steps(&trk[0])[0].note[0] == 60 && seq_steps(&trk[1])[0].note[0] == 73 && seq_steps(&trk[3])[0].note[0] == 75;
    ok &= seq_steps(&trk[2]) == chain_silent && !trk[2].seq_n && trk[0].seq_n == 1 && trk[0].seq_notes[0] == 60;
    ok &= trk[0].p[P_SLEN] == 4 && trk[1].p[P_SLEN] == 6 && trk[3].p[P_SLEN] == 2 && trk[1].seq_notes[0] == 73;
    bad += check("SONG lanes: each track its own slot's steps and LEN (T1 A, T2 B, T3 -, T4 B)", ok);
    n = 1u + lanes_until(0, 4000);
    bad += check("  section 1 lasts T1's loop (A: 4 x 1/16)", chain.row == 1u && lanes_near(n, 4u * div_samples(2)));
    ok = chain.clk == 1u && !trk[0].seq_n && !trk[3].seq_n && trk[2].seq_notes[0] == 62 && trk[1].seq_notes[0] == 73;
    for (k = 0; k < NTRK_V10; k++) ok &= trk[k].seq_idx == 0u;
    ok &= trk[2].p[P_REV] == 100 && trk[2].p[P_SLEN] == 4 && trk[2].p[P_SQNT] == 1;
    bad += check("SONG lanes: T1 \"-\" releases its note; all four from step 1 together; T3 plays A with its automation", ok);
    ok = 1;
    n = 0;
    while (n < 8000u && chain.running && chain.row == 1u) {
        events_block(32);
        chain_poll();
        n++;
        ok &= chain.row != 1u || (!trk[0].seq_n && !trk[3].seq_n);
    }
    bad += check("SONG lanes: T1 silent: the section counts T2's loop (B: 6 x 1/16) x 2, the silent lanes stay silent",
                 ok && chain.row == 2u && lanes_near(n, 12u * div_samples(2)));
    ok = chain.clk == 0u && trk[2].p[P_REV] == (int16_t)rev[2];
    n = 0;
    while (n < 8000u && chain.running && chain.row == 2u) {
        events_block(32);
        chain_poll();
        n++;
        for (k = 0; k < NTRK_V10; k++) ok &= chain.row != 2u || !trk[k].seq_n;   /* (the block it ends in plays the next) */
    }
    bad += check("SONG lanes: every lane silent: nothing plays for a 4/4 bar (16 x 1/16); T3's automation back",
                 ok && chain.row == 3u && lanes_near(n, 16u * div_samples(2)));
    ok = trk[0].seq_notes[0] == 72 && trk[1].seq_notes[0] == 61 && trk[2].seq_notes[0] == 74 && trk[3].seq_notes[0] == 63;
    ok &= trk[0].p[P_SLEN] == 8 && trk[1].p[P_SLEN] == 4 && trk[2].p[P_SDIV] == 1 && !trk[2].p[P_SQNT] && trk[3].p[P_SLEN] == 4;
    bad += check("SONG lanes: B A B A: T3 B's 1/8 and QUANTIZE OFF, the others theirs", ok);
    n = lanes_until(3, 8000);
    ok = !chain.running && !song.playing && lanes_near(n + 1u, 8u * div_samples(2));
    for (k = 0; k < NTRK_V10; k++)
        ok &= trk[k].p[P_SLEN] == 3 && trk[k].p[P_SDIV] == 2 && trk[k].p[P_SQNT] == 1 && trk[k].step[0].note[0] == 48u + k &&
              trk[k].p[P_REV] == (int16_t)rev[k] && !trk[k].seq_n;
    bad += check("SONG lanes: the end (T1 B's 8 steps) stops; the working patterns, LEN DIV and QUANTIZE back", ok);
    chain_config.count = 1;
    chain_config.row[0] = lrow((const uint8_t[]){CHAIN_SILENT, 3, CHAIN_SILENT, CHAIN_SILENT}, 1);   /* (no pattern 3) */
    ok = chain_prepare() == 0 && chain.armed;
    events_block(32);
    ok &= chain.running && seq_steps(&trk[1])[0].time != ST_NOTE && !trk[1].seq_n;
    seq_stop();
    chain_config.row[0].slot[1] = CHAIN_SILENT;
    ok &= chain_prepare() == 0 && chain.armed;
    events_block(32);
    ok &= chain.running && seq_steps(&trk[1]) == chain_silent;
    seq_stop();
    bad += check("SONG lanes: a lane on a pattern not there plays nothing (nothing refused); a silent song too", ok);
    chain_config.row[0].slot[0] = 33;
    bad += check("SONG lanes: a pattern past the 32 is no section", !chain_valid(&chain_config));
    chain_defaults(&chain_config);
    song_round = 1;
    return bad;
}

/* SONG's lanes drawn (ui_graph.c graph_song): a run of one slot merges into one block (no gap at the boundary), "-" is a
 * gap, the cell selected is THEME (only its section), the playhead crosses all four lanes */
static int test_song_lanes_drawn(void)
{
    song_lay_t L;
    int bad = 0, ok;
    int32_t xb, y0, y1, y2;
    uint32_t k;
    ui_power_on();
    pat_park();                                      /* (patterns 1 2, A B of before) */
    trk[0].p[P_SLEN] = 16; trk[0].p[P_SDIV] = 2;
    pat_keep(1); pat_keep(2);
    fm1_ms += 1000u;
    chain_config.count = 3;
    chain_config.row[0] = (chain_row_t){{1, 1, CHAIN_SILENT, 2}, 1};
    chain_config.row[1] = (chain_row_t){{1, 2, CHAIN_SILENT, 2}, 1};
    chain_config.row[2] = (chain_row_t){{2, 2, 1, CHAIN_SILENT}, 1};
    ui.song_row = 1; ui.song_trk = 0;
    ui_prefs |= PREF_ANIM_OFF;
    go_page(GR_SONG); frames(100);
    song_layout(&L);
    xb = SG_X0 + L.x[1] - 1;                         /* the boundary between sections 1 and 2 */
    y0 = SG_LY(0) + SG_LH - 1; y1 = SG_LY(1) + SG_LH - 1; y2 = SG_LY(2) + SG_LH - 1;   /* (under the letters) */
    ok = sg_px(xb, SG_LY(3) + SG_LH - 1) == T_RAISE;  /* T4: B B -: one block over the boundary */
    ok &= sg_px(xb, y1) != T_RAISE && sg_px(xb, y1) != T_THEME;   /* T2: A then B: two blocks, the gap */
    ok &= sg_px(SG_X0 + L.x[0] + 4, y2) != T_RAISE && sg_px(SG_X0 + L.x[0] + 4, y2) != T_THEME;   /* T3 "-": nothing */
    bad += check("SONG lanes drawn: a run of one slot one block, another slot a new one, \"-\" a gap", ok);
    ok = sg_px(SG_X0 + L.x[1] + 2, y0) == T_THEME && sg_px(SG_X0 + L.x[0] + 2, y0) == T_RAISE && sg_px(xb, y0) == T_RAISE;
    bad += check("SONG lanes drawn: the cell (section 2, T1) THEME, the rest of its block RAISE", ok);
    ui.song_trk = 2; ui.song_row = 0; frames(100);
    ok = sg_px(SG_X0 + L.x[0] + 4, SG_LY(2)) == T_THEME && sg_px(SG_X0 + L.x[0], SG_LY(2) + 4) == T_THEME &&
         sg_px(SG_X0 + L.x[0] + 4, SG_LY(2) + 4) != T_THEME;
    bad += check("SONG lanes drawn: the cursor on a \"-\" cell: its outline only", ok);
    chain_prepare(); events_block(32);
    for (k = 0; k < 2400u; k++) events_block(32);   /* (Dah: past the first block's letter at the zoom's scale) */
    ui.song_row = 2; frames(50);
    ok = 1;
    {
        int32_t sp = SG_X0 + song_playx(&L) - sgv.off;
        for (k = 0; k < NTRK; k++) ok &= sg_px(sp, SG_LY(k) + 1) == T_ACCENT;
        ok &= sp > SG_X0 + 4 && sg_px(sp, SG_Y + SG_H) == T_ACCENT && sg_px(sp, SG_RY + 3) == T_ACCENT;
    }
    bad += check("SONG lanes drawn: the playhead crosses the four lanes to the ruler (ACCENT)", ok);
    seq_stop(); frames(50);
    ui_prefs &= ~PREF_ANIM_OFF;
    chain_defaults(&chain_config);
    ui.song_row = ui.song_trk = 0;
    return bad;
}

/* SONG's pictures (ui_graph.c sg_pic, sg_picture): a slot's picture is that track's steps there, as HOME's rack (a
 * byte a step: rest, a TIE carrying the note before, note, accent; the notes' rows), the other tracks' their own; REPS
 * passes notched; a slot saved again is read again. (The panel's text in its bands: tests/ui_render.c song_bands) */
static uint32_t sg_bar(int32_t x, int32_t k)          /* lane k's bar height at column x (THEME or INK pixels) */
{
    uint32_t n = 0;
    int32_t y;
    for (y = SG_LY(k) + SG_PAD; y < SG_LY(k) + SG_PAD + SG_PH; y++)
        n += sg_px(x, y) == T_THEME || sg_px(x, y) == T_INK;
    return n;
}
static const uint8_t *sg_p2(uint32_t k, uint32_t *sl)   /* (track k's pattern 2) */
{
    uint32_t l;
    return sg_pat(k, 2, &l) ? sg_pic(k, 2, sl) : 0;
}
static int test_song_pictures(void)
{
    song_lay_t L;
    int bad = 0, ok;
    uint32_t i, d, sl = 0;
    const uint8_t *p;
    int32_t sx, wt;
    ui_power_on();
    sgw.zoom = 192;                                  /* (Dah: zoomed in, a bar over the view: the cells wide) */
    pat_park();                                      /* (pattern 2 below, B of before) */
    for (i = 0; i < NTRK; i++) {                     /* every track empty, 1/16, LEN 16 */
        memset(trk[i].step, 0, sizeof trk[i].step);
        for (d = 0; d < NSTEP; d++) trk[i].step[d].time = ST_REST;
        trk[i].p[P_SLEN] = 16; trk[i].p[P_SDIV] = 2;
    }
    trk[2].step[5] = (step_t){{60}, 1, ST_NOTE, SF_ACCENT, 100, 0, 0, 0};   /* T3: an accent, its tie, a note, a hit */
    trk[2].step[6].time = ST_TIE;
    trk[2].step[8] = (step_t){{62}, 1, ST_NOTE, 0, 100, 0, 0, 0};
    trk[2].step[12] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1, 0, 0};
    trk[2].step[14] = (step_t){{64}, 1, ST_NOTE, 0, 100, 0, 0, 101};      /* (CHANCE 0: silent, none) */
    trk[0].step[0] = (step_t){{40}, 1, ST_NOTE, 0, 100, 0, 0, 0};         /* T1: step 1 only */
    pat_keep(2);
    fm1_ms += 1000u;
    chain_config.count = 1;
    chain_config.row[0] = lrow((const uint8_t[]){CHAIN_SILENT, CHAIN_SILENT, 2, CHAIN_SILENT}, 1);   /* T3 alone: it clocks */
    ui.song_row = 0; ui.song_trk = 0;
    ui_prefs |= PREF_ANIM_OFF;
    go_page(GR_SONG); frames(100);
    p = sg_p2(2, &sl);
    ok = p && sl == 16u && (p[5] & 0xC0u) == SGP_ACC && (p[6] & 0xC0u) == SGP_TIE && (p[6] & 0x3Fu) == (p[5] & 0x3Fu) &&
         (p[8] & 0xC0u) == SGP_NOTE && (p[8] & 0x3Fu) > (p[5] & 0x3Fu) && (p[12] & 0xC0u) == SGP_NOTE && !p[14] && !p[0];
    p = sg_p2(0, &sl);
    ok &= p && (p[0] & 0xC0u) == SGP_NOTE && !p[1] && !sg_p2(1, &sl);
    bad += check("SONG pictures: a slot's picture is that track's steps (an accent, its tie at its rows, a note higher, "
                 "a hit; CHANCE 0 none), the others their own", ok);
    chain_config.row[0].repeat = 2;                  /* x2: the pattern twice, a notch between */
    sgw.zoom = 96;
    ui.song_trk = 0; frames(100);
    song_layout(&L);
    sx = SG_X0 + L.x[0]; wt = L.x[1] - L.x[0] - SG_GAP;
    bad += check("SONG pictures: REPS 2 draws the pattern twice, notched between (the block whole)",
                 sg_px(sx + wt / 2, SG_LY(2) + SG_PAD + 1) == T_SURF && sg_px(sx + wt / 2, SG_LY(2)) == T_RAISE);
    ui_prefs &= ~PREF_ANIM_OFF;
    chain_defaults(&chain_config);
    ui.song_row = ui.song_trk = 0;
    sgw.zoom = 0;
    return bad;
}

/* the header's slot letter (ui_draw.c head_slot): the PROJECT slot of the selected track's pattern; "A*" edited since;
 * none at power-on; a song playing: its section's slot ("-" silent); a message hides it */
static uint32_t head_ink_cols(int32_t x0, int32_t x1)  /* header columns x0..x1 with anything not BG */
{
    uint32_t n = 0;
    for (int32_t x = x0; x <= x1; x++)
        for (int32_t y = 2; y < 22; y++)
            if (swap16(host_screen[(uint32_t)y * 240u + (uint32_t)x]) != T_BG) { n++; break; }
    return n;
}
static int head_is(const char *s)
{
    char b[3];
    int on = head_slot(b);
    return s ? on && b[0] == s[0] && b[1] == s[1] : !on;
}
static int test_head_slot(void)                    /* (the pattern's number, pstore.c) */
{
    int bad = 0, ok;
    ui_power_on();
    frames(50);
    ok = head_is("1") && swap16(host_screen[12u * 240u + HEAD_PAT_X + 1u]) == ux.trk[0];   /* (the badge, top right; Dah: its edge, "P1" inside) */
    pattern_new(0, 0); ui.msg_t = 0; frames(50);
    ok &= head_is("2");
    song.sel = 1; ok &= head_is("1");
    song.sel = 0;
    bad += check("header: the pattern the selected track plays (1 at power-on, 2 after NEW), on the track's colour", ok);
    chain_config.count = 1;
    chain_config.row[0] = (chain_row_t){{1, CHAIN_SILENT, 1, 1}, 4};
    chain_prepare(); events_block(32); frames(50);
    ok = chain.running && head_is("1");
    song.sel = 1; ok &= head_is("-");
    song.sel = 0;
    ui_message("SAVED"); frames(20);
    ok &= !head_ink_cols(23, 32);
    seq_stop(); ui.msg_t = 0; frames(50);
    bad += check("header: a song playing: its lane's pattern (T1 \"1\", T2 \"-\"); a message hides it; STOP: \"2\"", ok &&
                 head_is("2"));
    chain_defaults(&chain_config);
    return bad;
}

/* FM6's operator pages (EDIT > OPERATOR, OP ENV, OPERATOR 2: ui_fm6op.c, Discussion #76): in an FM6 track's EDIT cycle
 * only; KNOB 1 the operator (shared by the three pages), KNOB 2..4 one byte each of the track's patch: every operator's
 * 14 bytes, each detent exactly that byte, SLOT OWN; the sound follows (a modulator's LEVEL brightens, a carrier's
 * lowers it, COARSE doubles the pitch, a slower attack, FIXED); projects and user presets keep the edited patch; the macros stay on top of it */
typedef struct { double e, early, hf; uint32_t zc; } fm6_snd_t;
static fm6_snd_t fm6_sound(void)                    /* the selected track's C4 at velocity 100, 128 blocks held */
{
    track_t *t = TSEL;
    fm6_snd_t r = {0, 0, 0, 0};
    int32_t o[2u * CTL], prev = 0;
    uint32_t b, i, n = 0, mq = song.master_q12;
    song.master_q12 = 4096;
    for (b = 0; b < 256u && (b < 1u || t->xf_on || t->eng_req != t->engine); b++)
        mix_block(o, CTL);                           /* (a block first: a pending panic_req done) */
    trk_note_on(t, 60, 100);
    for (b = 0; b < 128u; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, n++) {
            double x = o[2u * i], d = x - prev;
            r.e += x * x;
            r.hf += d * d;
            if (n < 2048u) r.early += x * x;
            if (n && ((prev < 0) != (o[2u * i] < 0))) r.zc++;
            prev = o[2u * i];
        }
    }
    trk_note_off(t, 60);
    for (b = 0; b < 400u; b++)                       /* (the release, the voice ends: the next note from silence) */
        mix_block(o, CTL);
    r.hf = r.e > 0 ? r.hf / r.e : 0;
    song.master_q12 = mq;
    return r;
}
static int fm6_same(const fm6_snd_t *a, const fm6_snd_t *b)   /* the same sound (the reverb's and LFO's state aside) */
{
    double d = a->e > b->e ? a->e - b->e : b->e - a->e;
    if (d > 0.002 * a->e || a->zc + 1u < b->zc || b->zc + 1u < a->zc)
        printf("    not the same: energy %.6g / %.6g, zero crossings %u / %u\n", a->e, b->e, a->zc, b->zc);
    return d <= 0.002 * a->e && a->zc + 1u >= b->zc && b->zc + 1u >= a->zc;
}
static int patch_diff(const uint8_t *a, const uint8_t *b, uint32_t *at)   /* bytes that differ (the last at *at) */
{
    int n = 0;
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++)
        if (a[i] != b[i]) { n++; *at = i; }
    return n;
}
static void fop_pick(uint32_t opn)                   /* KNOB 1: OP opn (1..6) */
{
    turn(EN_K1, -6);
    turn(EN_K1, (int32_t)opn - 1);
}
static int slot_own(void) { return TSEL->p[P_E7] == FM6_OWN && fm6_slot[song.sel] == FM6_OWN; }
static int test_fm6_ops(void)
{
    static const char *const CYC_F[] = {"EDIT 1", "EDIT 2", "OPERATOR", "OP ENV", "OPERATOR 2", "VOICE", "VOICE 2", "VOICE 3", "EDIT 1"};
    static const struct { const char *page; uint32_t k, off; } F[] = {
        {"OPERATOR", EN_K2, FP_FC}, {"OPERATOR", EN_K3, FP_FF}, {"OPERATOR", EN_K4, FP_OL},
        {"OPERATOR 2", EN_K2, FP_MODE}, {"OPERATOR 2", EN_K3, FP_DET}, {"OPERATOR 2", EN_K4, FP_KVS},
        {"OP ENV", EN_K3, FP_R1}, {"OP ENV", EN_K4, FP_L1}};
    int bad = 0, ok;
    uint32_t opn, f, s, at, n = 0, tr;
    uint8_t before[FP_SIZE + 1u], v[FP_SIZE + 1u], mine[FP_SIZE + 1u];
    fm6_snd_t s0, s1;
    ui_power_on();
    song.sel = 1;                                    /* T2: FM6 PAD (SLOT F5) */
    tr = song.sel;
    bad += check("FM6 ops: FM6's EDIT cycle EDIT 1 EDIT 2 OPERATOR OP ENV OPERATOR 2 VOICE VOICE 2 EDIT 1",
                 TSEL->eng_req == ENGI_FM6 && engine_cycle(CYC_F, NELEM(CYC_F)));
    ok = TSEL->p[P_E7] == 4 && fm6_slot[tr] == 4u;
    go_title("OPERATOR"); frame();
    fop_pick(3);
    memcpy(before, fm6_patch[tr], FP_SIZE);
    turn(EN_K4, -5);                                 /* OP 3 LEVEL, one gesture of two turns */
    turn(EN_K4, -2);
    ok &= patch_diff(before, fm6_patch[tr], &at) == 1 && at == 3u * FP_OP + FP_OL &&
          fm6_patch[tr][at] == before[at] - 7u && slot_own() && fop_op == 2u;
    bad += check("FM6 ops: OPERATOR KNOB 4 on OP 3: its LEVEL byte only; F5 -> SLOT OWN", ok);

    ok = 1;                                          /* every operator, every byte the pages edit: that byte only */
    for (opn = 1; opn <= 6u; opn++)
        for (f = 0; f < NELEM(F); f++)
            for (s = 0; s < (F[f].off == FP_R1 || F[f].off == FP_L1 ? 4u : 1u); s++) {
                uint32_t b = (6u - opn) * FP_OP + F[f].off + s, max = fm6_max(b);
                int32_t d;
                go_title(F[f].page); frame();
                fop_pick(opn);
                if (F[f].off == FP_R1 || F[f].off == FP_L1) { turn(EN_K2, -4); turn(EN_K2, (int32_t)s); }
                memcpy(v, fm6_patch[tr], FP_SIZE);
                d = v[b] < max ? 1 : -1;
                frames(1600);
                turn(F[f].k, d);
                if (patch_diff(v, fm6_patch[tr], &at) != 1 || at != b || fm6_patch[tr][b] != (uint8_t)(v[b] + d) || !slot_own()) {
                    printf("ui: FM6 ops: OP %u %s KNOB %u stage %u: byte %u, not %u\n", opn, F[f].page, F[f].k - EN_K1 + 1u,
                           s + 1u, at, b);
                    ok = 0;
                }
                turn(F[f].k, 200);                   /* the range: never past the byte's highest, nor below 0 */
                ok &= fm6_patch[tr][b] == max;
                turn(F[f].k, -200);
                ok &= fm6_patch[tr][b] == 0u;
                n++;
            }
    bad += check("FM6 ops: 6 operators x 14 bytes, each knob writes its byte only (+-1, 0..its highest), SLOT OWN", ok && n == 84u);

    /* the sound: the init voice (ALG 1: OP 1 the carrier at 99, OP 2 its modulator at 0), the track's other sends off */
    TSEL->p[P_CHOR] = TSEL->p[P_DLY] = TSEL->p[P_REV] = TSEL->p[P_DIST] = 0;
    TSEL->p[P_LD_PIT] = TSEL->p[P_LD_FLT] = TSEL->p[P_LD_SHP] = TSEL->p[P_LD_AMP] = 0;
    for (f = 0; f < 7u; f++) TSEL->p[P_E0 + f] = 0;
    fm6_unpack(FM6_INIT, v);
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
    {   /* (the tails of the tests before, eight tracks' worth, die away first: ~2 s) */
        int32_t oo[2u * CTL];
        for (f = 0; f < 3000u; f++)
            mix_block(oo, CTL);
    }
    s0 = fm6_sound();
    go_title("OPERATOR"); frame();
    fop_pick(2);
    turn(EN_K4, 80);                                 /* OP 2 LEVEL 0 -> 80: FM, brighter */
    s1 = fm6_sound();
    ok = s1.hf > 2.0 * s0.hf && fm6_patch[tr][4u * FP_OP + FP_OL] == 80u;
    printf("    OP 2 LEVEL 80: HF share %.4f -> %.4f\n", s0.hf, s1.hf);
    turn(EN_K4, -80);
    s1 = fm6_sound();
    ok &= fm6_same(&s0, &s1);
    bad += check("FM6 ops: the sound: a modulator's LEVEL brightens it; back to 0 the same sound again", ok);
    fop_pick(1);
    turn(EN_K4, -20);                                /* OP 1 (the carrier) LEVEL 99 -> 79 */
    s1 = fm6_sound();
    ok = s1.e < 0.2 * s0.e && s1.e > 0;
    printf("    OP 1 LEVEL 79: energy x%.3f", s1.e / s0.e);
    turn(EN_K4, 20);
    turn(EN_K2, 1);                                  /* OP 1 COARSE 1 -> 2: an octave up */
    s1 = fm6_sound();
    ok &= s1.zc > 1.8 * s0.zc && s1.zc < 2.2 * s0.zc;
    printf("; COARSE 2: zero crossings %u -> %u", s0.zc, s1.zc);
    turn(EN_K2, -1);
    go_title("OP ENV"); frame();
    turn(EN_K2, -4);
    turn(EN_K3, -70);                                /* stage 1 (ATK) RATE 99 -> 29: a slow attack */
    s1 = fm6_sound();
    ok &= s1.early < 0.5 * s0.early;
    printf("; ATK RATE 29: the first 2048 samples' energy x%.3f", s1.early / s0.early);
    turn(EN_K3, 70);
    go_title("OPERATOR 2"); frame();
    turn(EN_K2, 1);                                  /* OP 1 MODE FIXED (COARSE 1: 10 Hz x 10 ^ FINE / 100) */
    s1 = fm6_sound();
    ok &= s1.zc < s0.zc / 4u;
    printf("; FIXED: zero crossings %u\n", s1.zc);
    turn(EN_K2, -1);
    s1 = fm6_sound();
    ok &= fm6_same(&s0, &s1);
    bad += check("FM6 ops: a carrier's LEVEL lowers it, COARSE 2 an octave up, ATK RATE 29 slower, FIXED 10 Hz; turned back: as before",
                 ok);

    /* the macros on top: MRAT / MLVL move the edited patch as they move any (the patch itself stays) */
    go_title("OPERATOR"); frame();
    fop_pick(2);
    turn(EN_K4, 70);                                 /* OP 2 LEVEL 70, COARSE 1 -> 3 */
    turn(EN_K2, 2);
    memcpy(mine, fm6_patch[tr], FP_SIZE);
    TSEL->p[P_E3] = 2;                               /* MRAT +2 */
    s1 = fm6_sound();
    ok = fm6_eff[tr].p[4u * FP_OP + FP_FC] == 5u && !memcmp(mine, fm6_patch[tr], FP_SIZE);
    turn(EN_K2, 1);                                  /* an edit under MRAT: the macro still adds to it */
    s1 = fm6_sound();
    ok &= fm6_eff[tr].p[4u * FP_OP + FP_FC] == 6u && fm6_patch[tr][4u * FP_OP + FP_FC] == 4u;
    TSEL->p[P_E3] = 0;
    s0 = fm6_sound();
    TSEL->p[P_E2] = -64;                             /* MLVL -64: the modulators down, darker */
    s1 = fm6_sound();
    ok &= s1.hf < 0.5 * s0.hf && fm6_patch[tr][4u * FP_OP + FP_OL] == 70u;
    TSEL->p[P_E2] = 0;
    bad += check("FM6 ops: the macros apply on top of the edited patch (MRAT adds to COARSE, MLVL darker); the patch stays", ok);

    /* kept: a project and a user preset hold the edited patch (SLOT OWN) */
    memcpy(mine, fm6_patch[tr], FP_SIZE);
    stop_transport();
    project_save(2);
    f = (uint32_t)up_store(9, "OPS");
    ok = f == 0u || f == 3u;                         /* (3: the host has no flash, kept in RAM) */
    TSEL->p[P_E7] = 0;                               /* SLOT F1: another patch */
    fm6_poll();
    ok &= memcmp(mine, fm6_patch[tr], FP_SIZE) != 0;
    project_load(2);
    ok &= !memcmp(mine, fm6_patch[tr], FP_SIZE) && slot_own();
    TSEL->p[P_E7] = 1; fm6_poll();
    up_load(9);
    ok &= !memcmp(mine, fm6_patch[tr], FP_SIZE) && slot_own();
    bad += check("FM6 ops: the edited patch round-trips through a project and a user preset (SLOT OWN)", ok);

    /* nothing else: not on other engines, not automatable */
    ok = 1;
    song.sel = 0;                                    /* T1 ANALOG: no operator page */
    for (f = 0; f < NPAGES; f++)
        if (PAGES[f].graph >= GR_FMOP && PAGES[f].graph <= GR_FMENV) ok &= !page_visible(f);
    song.sel = 1;
    go_title("OPERATOR"); frame();
    song.rec = 2u; song.playing = 1;
    f = motion.count;
    turn(EN_K3, 3);
    ok &= motion.count == f;
    stop_transport(); song.rec = 0;
    bad += check("FM6 ops: the pages on FM6 only; a turn while recording writes no automation", ok);
    return bad;
}

/* 1.3 (Discussions #112, #134): MENU > DISPLAY > HOME SCOPE / TRACKS (ui.c ui_home_view, settings byte [15][26]) and
 * HOME > TRACKS (ui_graph.c draw_home_tracks): the setting (its default, names, tab, byte, the other settings' bytes
 * untouched, an unknown value as SCOPE, the menu's knob, the settings record); the rows read from the screen: a note's
 * bar, an empty step's stub, a TIE carried on from its note, an accent taller, the step playing on its raised cell,
 * LEN > 16 on the play head's page (stopped: the selected track's cursor page, the others' first) with its pips, DRUM
 * hits as pads (two for two hits), MUTE (its badge, the steps DIM), the selected row THEME, the meter; HOME's knobs
 * still edit the selected track, ALGORITHM moves the selection, HOME's tap still goes HOME > MIXER > CLOCK > HOME;
 * MENU > LARGE: the cards as on a list page, the rows in the panel */
static uint16_t tr_px(int32_t x, int32_t y) { return swap16(host_screen[(uint32_t)y * 240u + (uint32_t)x]); }
static int32_t tr_y(uint32_t c) { return Y_GRAPH + (int32_t)c * TR_PITCH; }
static int32_t tr_x(uint32_t i) { return 3 + tr_bx(i) + TR_BW / 2; }   /* a step's bar, its middle column */
/* 1.5 (#173): the FX map's columns light / dark in turn (ui_layer.c LS_ALT): an idle cell's fill, read inside its top
 * edge, the same in columns 1 and 3 and in 2 and 4 and different between them, every row, FLAT and LINE (LINE: the
 * 1st / 3rd unfilled, the panel's colour); MONO (its one grey) all alike */
static int test_fx_map_columns(void)
{
    int bad = 0, ok = 1;
    uint32_t st, pal, r, c;
    for (pal = 0; pal < 2u; pal++)
        for (st = ST_FLAT; st <= ST_LINE; st++) {
            ui_power_on();
            settings.palette = (uint8_t)(pal ? UI_BW_INDEX : UI_GREY_INDEX);
            ui_style = (uint8_t)st;
            style_apply();
            palette_set(settings.palette);
            ui.home = 1; ui.layer = LAYER_FX; ui.force = 1;
            draw_layer();
            for (r = 0; r < 4u; r++) {
                uint16_t px[4];
                for (c = 0; c < 4u; c++)
                    px[c] = tr_px(LC_X(c) + 27, Y_GRAPH + 4 + (LF_H + 4) * (int32_t)r + 1);
                ok &= px[0] == px[2] && px[1] == px[3] && (pal ? px[0] == px[1] : px[0] != px[1]);
                ok &= !st || pal || px[0] == T_SURF;
            }
            ui.layer = 0;
        }
    ui_style = ST_FLAT; style_apply(); settings.palette = UI_GREY_INDEX; palette_set(UI_GREY_INDEX);
    bad += check("1.5 #173: the FX map's columns alternate light / dark (FLAT, LINE; MONO: one grey)", ok);
    return bad;
}

/* 1.5 (Discussion #156): HELP. MENU > SYSTEM > HELP (OFF by default, bit 3 of ui_prefs2 alone); every page a button
 * reaches (DIGITAL's retired OP pages aside), HOME, HOME LEVELS, the DRUM grid and every layer has a hint of 2..3 key
 * hints that fits its footer row with every word (cv_key_row drops none); ON: a page entered shows its hint ~2 s, then
 * the footer as before; OFF: none */
static int help_fits(const char *s)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b), i;
    int32_t w = 6 * ((int32_t)n - 1);
    int ok = n >= 2u && n <= 3u;
    for (i = 0; i < n; i++) {
        ok &= kh[i].key < KC_COUNT && kh[i].act[0] != 0;
        w += kh_w(kh[i].key, kh[i].act);
    }
    if (!ok || w > 224) printf("  help: \"%s\" %u hints, %d px of 224\n", s, n, w);
    return ok && w <= 224;
}
static int test_help(void)
{
    int bad = 0, ok;
    uint8_t before[32];
    uint32_t i, l;
    ui_power_on();
    memcpy(before, favorites.factory[15], 32);
    ok = !help_on() && menu_get(MI_HELP) == 0u && str_eq(MI_NAME[MI_HELP], "HELP") && MI_TAB[MI_HELP] == MTAB_SYSTEM &&
         MI_HELP < MI_VALUES && str_eq(menu_vname(MI_HELP, 1), "ON");
    menu_put(MI_HELP, 1);
    ok &= help_on() && menu_get(MI_HELP) == 1u && favorites.factory[15][24] == (uint8_t)(before[24] | 8u);
    for (i = 0; i < 32u; i++)
        ok &= i == 24u || favorites.factory[15][i] == before[i];
    bad += check("1.5 MENU > SYSTEM > HELP: OFF by default, ON a bit of its own (ui_prefs2 bit 3)", ok);

    ok = 1;
    for (i = 0; i < NPAGES; i++) {                     /* every page but DIGITAL's OP1..4 ENV / OP LEVEL (FM4 only) */
        uint8_t h = ui.home, pg = ui.page;
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            continue;
        ui.home = 0; ui.page = (uint8_t)i;
        if (!help_page()) { printf("  help: no hint for %s\n", PAGES[i].title); ok = 0; }
        else ok &= help_fits(help_page());
        ui.home = h; ui.page = pg;
    }
    ok &= help_fits(H_HOME) && help_fits(H_LEVELS) && help_fits(H_GRID);
    for (l = LAYER_FX; l < LAYER_N; l++)
        ok &= help_fits(HELP_LAYER[l]);
    bad += check("1.5 HELP: a hint for every page, HOME, LEVELS, the grid and every layer; each fits its row, no word dropped", ok);

    ui_power_on();
    menu_put(MI_HELP, 1);
    press(B_ENV);
    ok = ui.help_t > 0u && str_eq(help_page(), "[K1-4]A D S R[ENV]ENV DEST");
    frames(2200);
    ok &= ui.help_t == 0u;
    press(B_SAVE);                                       /* (FILE first) */
    ok &= ui.help_t > 0u && cur_page()->graph == GR_FILE && str_eq(help_page(), "[PRESETS]PROJECT[K1]OPEN[K4]DELETE");
    press(B_HOME);
    ok &= ui.home && ui.help_t > 0u && str_eq(help_page(), H_HOME);
    menu_put(MI_HOME, HV_LEVELS);
    ok &= str_eq(help_page(), H_LEVELS);
    menu_put(MI_HOME, HV_SCOPE);
    menu_put(MI_HELP, 0);
    press(B_ENV);
    ok &= ui.help_t == 0u;
    bad += check("1.5 HELP ON: a page entered (a button, HOME) shows its hint ~2 s, then not; OFF: never", ok);
    return bad;
}

/* the tracks' extras (ui_lfodraw.c): LFO DRAW's points (WAVE DRAW, stepped or joined), CURVE RAMP (motion.c
 * motion_ramp_*: an automation event's value moves in a line to the next) */
static int test_extras(void)
{
    int bad = 0, ok;
    track_t *t;
    int8_t tri[LDRAW_N];
    uint32_t i, n;
    ui_power_on();
    t = TSEL;
    go_title("LFO DRAW"); frame();
    ok = cur_page()->graph == GR_LDRAW && cur_page()->fam == FAM_LFO && t->p[P_LWAVE] != 5;
    turn(EN_K1, 3);
    turn(EN_K2, 5);
    ok &= xp_point == 3u && trkx[0].draw[3] == 58 + 5 && t->p[P_LWAVE] == 5;
    turn(EN_K3, 1);
    ldraw_template(tri, 1);
    ok &= !memcmp(trkx[0].draw, tri, sizeof tri);
    turn(EN_K4, -1);
    ok &= !trkx[0].smooth && lfo_wave(t, 3u << 28) == trkx[0].draw[3] * 520 && lfo_wave(t, (3u << 28) + (1u << 27)) == trkx[0].draw[3] * 520;
    turn(EN_K4, 1);
    ok &= trkx[0].smooth && lfo_wave(t, (3u << 28) + (1u << 27)) == (trkx[0].draw[3] * 520 + trkx[0].draw[4] * 520) / 2;
    bad += check("LFO DRAW: POINT VALUE (WAVE to DRAW) SHAPE (a template) SMOOTH (stepped / joined), as the LFO plays it", ok);

    for (n = 0; n < 2u; n++) {                       /* REV 0 on step 1, 96 on step 9: halfway at step 5 (RAMP), 0 (STEP) */
        int16_t mid;
        ui_power_on();
        t = &trk[0];
        track_defaults_steps(t);
        t->p[P_SLEN] = 16; t->p[P_SDIV] = 2; t->p[P_SSWING] = 0; song.g[G_SWING] = 0; t->p[P_REV] = 30;
        trkx[0].curve = (uint8_t)n;
        motion_set_event(t, 0, P_REV, 0);
        motion_set_event(t, 8, P_REV, 96);
        seq_start();
        for (i = 0; i < 400000u && !(t->seq_idx == 4u && t->seq_pos >= step_samples(t, div_samples(2), 4) / 2u); i++)
            seq_tick(t, CTL);
        mid = t->p[P_REV];
        for (i = 0; i < 400000u && t->seq_idx != 12u; i++)
            seq_tick(t, CTL);
        ok = n ? mid >= 52 - 4 && mid <= 52 + 4 : mid == 0;
        ok &= n ? t->p[P_REV] >= 48 - 4 && t->p[P_REV] <= 48 + 4 : t->p[P_REV] == 96;   /* (back to step 1's 0 at the loop) */
        seq_stop();
        ok &= t->p[P_REV] == 30;
        bad += check(n ? "CURVE RAMP: REV 0 -> 96 in a line (halfway at step 4.5: ~52), then to step 1's 0 by the loop's end; stop: back"
                       : "CURVE STEP: REV holds 0 to step 9, then 96 (as always); stop: back", ok);
    }
    ui_power_on();
    return bad;
}
/* the rack's pixels: row c's y, step i's column (+2: inside its 6 px), a column x of the row, its bg */
static uint16_t rk_at(int32_t x, uint32_t c, int32_t dy) { return tr_px(3 + x, Y_GRAPH + 1 + (int32_t)c * RK_PITCH + dy); }
static uint16_t rk_px(uint32_t i, uint32_t c, int32_t dy) { return rk_at(RK_SX + (int32_t)i * RK_SP + 2, c, dy); }
static uint16_t rk_bg(uint32_t c) { return rk_at(RK_SX - 2, c, 1); }
static int rk_ring(uint32_t i, uint32_t c) { return rk_at(RK_SX + (int32_t)i * RK_SP - 1, c, 6) == T_TEXT; }
static int test_home_tracks(void)
{
    int bad = 0, ok;
    uint8_t before[32];
    persist_t p;
    track_t *t;
    uint32_t i, id;
    int16_t v0;
    ui_power_on();
    memcpy(before, favorites.factory[15], 32);
    ok = ui_home_view == 0u && !home_tracks() && menu_get(MI_HOME) == 0u && menu_n(MI_HOME) == 3u &&
         str_eq(MI_NAME[MI_HOME], "HOME") && str_eq(menu_vname(MI_HOME, 0), "SCOPE") &&
         str_eq(menu_vname(MI_HOME, 1), "TRACKS") && str_eq(menu_vname(MI_HOME, 2), "LEVELS") &&   /* (1.5: LEVELS) */
         MI_TAB[MI_HOME] == MTAB_DISPLAY && MI_HOME < MI_VALUES;
    menu_put(MI_HOME, 1);
    ok &= ui_home_view == 1u && home_tracks() && menu_get(MI_HOME) == 1u;
    for (i = 0; i < 32u; i++)                           /* (byte 26 only: SCREEN OFF, the 1.2 flags, the FX keys kept) */
        ok &= i == 26u || favorites.factory[15][i] == before[i];
    memset(&p, 0, sizeof p);
    settings_export(&p);
    ok &= p.favorites.factory[15][26] == 1u;
    menu_put(MI_HOME, 0);
    ok &= settings_import(&p, (int)sizeof p) == 1 && ui_home_view == 1u;   /* (back from the record) */
    ui_home_view = 9;                                   /* a later firmware's view: SCOPE here, kept as saved */
    ok &= !home_tracks() && menu_get(MI_HOME) == 0u;
    settings_export(&p);
    ok &= settings_import(&p, (int)sizeof p) == 1 && ui_home_view == 9u;
    bad += check("1.3 MENU > DISPLAY > HOME: SCOPE (0, the default) / TRACKS, its own settings byte, saved; unknown = SCOPE", ok);

    ui_power_on();
    hold(B_HOME);                                       /* the menu: DISPLAY, down to HOME, K1 right */
    for (i = 0; i < 7u; i++) turn(EN_PRESET, 1);
    ok = ui.menu == 1 && ui.menu_sel == MI_HOME;
    turn(EN_K1, 1); ok &= home_tracks() && !home_levels();
    turn(EN_K3, 1); ok &= home_levels();               /* (1.5: LEVELS, the last) */
    turn(EN_K3, 1); ok &= home_levels();               /* (stops there) */
    press(B_OCTDN); ok &= home_tracks() && !home_levels();
    press(B_OCTDN); ok &= !home_tracks();
    press(B_OCTUP); ok &= home_tracks();
    press(B_HOME); frame();
    ok &= !ui.menu && ui.home;
    bad += check("1.3 MENU: HOME's row (any knob, OCT+ / OCT-: SCOPE TRACKS LEVELS), the menu closed: HOME", ok);

    /* 1.5 (#134): HOME LEVELS: the rows of TRACKS, KNOB 1..4 the LEVEL of T1..T4 (the selected track's engine untouched),
     * MOMENTARY on any of them. And ANIM no longer sets
     * HOME (up to 1.4.1 its MENU row fell through into HOME's) */
    ui_power_on();
    menu_put(MI_HOME, HV_LEVELS);
    song.sel = 0;
    go_home(); frame();
    {
        int16_t lv[NTRK], e0[NTRK][P_COUNT];
        uint32_t k;
        const param_desc_t *d;
        int16_t *vp;
        for (k = 0; k < NTRK; k++) { lv[k] = trk[k].p[P_LEVEL]; memcpy(e0[k], trk[k].p, sizeof e0[k]); }
        turn(EN_K2, -5);
        turn(EN_K4, 3);
        ok = home_tracks() && home_levels() && trk[1].p[P_LEVEL] < lv[1] && trk[3].p[P_LEVEL] > lv[3] &&
             trk[0].p[P_LEVEL] == lv[0] && trk[2].p[P_LEVEL] == lv[2];
        for (k = 0; k < P_COUNT; k++)                   /* (nothing else of any track) */
            ok &= (k == P_LEVEL || (trk[0].p[k] == e0[0][k] && trk[1].p[k] == e0[1][k] && trk[2].p[k] == e0[2][k] &&
                                    trk[3].p[k] == e0[3][k]));
        d = home_param(2, &vp);
        ok &= vp == &trk[2].p[P_LEVEL] && d == &TP[P_LEVEL] && home_trk(2) == &trk[2];
        bad += check("1.5 HOME LEVELS: KNOB 2 / 4 the LEVEL of T2 / T4, nothing else of any track", ok);
        k = (uint32_t)trk[2].p[P_LEVEL];
        btn_down(B_LFO); frame();
        turn(EN_K3, -6);
        ok = trk[2].p[P_LEVEL] < (int16_t)k && mom.n == 1u;
        btn_up(B_LFO); frame();
        bad += check("1.5 HOME LEVELS: MOMENTARY (LFO held) on T3's LEVEL, back when LFO is let go",
                     ok && trk[2].p[P_LEVEL] == (int16_t)k && !mom.n);
        menu_put(MI_HOME, HV_TRACKS);
        lv[1] = trk[1].p[P_LEVEL];
        turn(EN_K2, 4);
        bad += check("1.5 HOME TRACKS (not LEVELS): KNOB 2 the selected track's engine again, no level",
                     trk[1].p[P_LEVEL] == lv[1] && !home_levels());
        menu_put(MI_HOME, HV_LEVELS);
        menu_put(MI_ANIM, 1u);
        ok = home_levels() && menu_get(MI_ANIM) == 1u;
        menu_put(MI_ANIM, 0u);
        ok &= home_levels() && menu_get(MI_ANIM) == 0u;
        menu_put(MI_HOME, HV_SCOPE);
        menu_put(MI_ANIM, 2u);
        ok &= !home_tracks() && menu_get(MI_ANIM) == 2u;
        menu_put(MI_ANIM, 0u);
        bad += check("1.5 MENU ANIM leaves HOME as it is (it fell through into HOME up to 1.4.1)", ok);
    }

    /* the rack, the eight rows at once (ui_graph.c rack_row): T1 (not selected) a line, T2 selected (RAISE),
     * T3 muted, T4 the DRUM beat. GREY: every track's colour the THEME */
    ui_power_on();
    ui_home_view = HV_TRACKS;
    song.sel = 1;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[4] = (step_t){{62}, 1, ST_NOTE, 0, 100};
    t->step[5] = (step_t){{62}, 0, ST_TIE, 0, 100};     /* (carried on from step 4's note) */
    t->step[6] = (step_t){{62}, 0, ST_TIE, 0, 100};
    t->step[8] = (step_t){{64}, 1, ST_NOTE, SF_ACCENT, 100};
    t->step[10] = (step_t){{0}, 0, ST_TIE, 0, 100};     /* (a TIE after an empty step: nothing to carry) */
    go_home(); frame();
    ok = rk_px(0, 0, 12) == T_THEME && rk_px(1, 0, 14) == T_LINE && rk_px(1, 0, 5) == T_SURF &&   /* a note (Dah: at its
                                                         * height, 60 the page's lowest), empty */
         rk_px(5, 0, 7) == ux_mix(T_SURF, T_THEME, 55) && rk_px(6, 0, 7) == ux_mix(T_SURF, T_THEME, 55) &&   /* tied */
         rk_px(8, 0, 2) == T_TEXT &&                                                        /* accented (64: the top) */
         rk_px(10, 0, 14) == T_LINE && rk_px(10, 0, 6) == T_SURF &&                          /* a lone TIE */
         rk_bg(0) == T_SURF && rk_bg(1) == T_RAISE && home_tracks() && large_kind() == LK_OFF;   /* T2 selected */
    bad += check("HOME > TRACKS (the rack): a row's steps: notes, empty stubs, TIEs carried on (half), accents, a lone TIE", ok);
    song.playing = 1;                                   /* the play head on step 3 (empty), then on the note 4 */
    t->seq_idx = 3;
    frame();
    ok = rk_ring(3, 0) && !rk_ring(2, 0);
    t->seq_idx = 4;
    frame();
    ok &= rk_ring(4, 0) && !rk_ring(3, 0) && rk_px(4, 0, 7) == T_THEME;
    bad += check("HOME > TRACKS: the step playing ringed (TEXT); it moves with the play head", ok);

    t->p[P_SLEN] = 40;                                  /* LEN 40: 3 pages; step 20 a note, step 16 empty */
    t->step[20] = (step_t){{65}, 1, ST_NOTE, 0, 100};
    t->seq_idx = 21;
    frame();
    ok = rk_px(4, 0, 7) == T_THEME && rk_px(0, 0, 14) == T_LINE && rk_ring(5, 0);   /* playing 21 = its 6th */
    t->seq_idx = 36;                                   /* the last page: 8 steps, the rest blank */
    frame();
    ok &= rk_px(7, 0, 14) == T_LINE && rk_px(8, 0, 14) == T_SURF;
    song.playing = 0;                                   /* stopped: T1 not selected, its first page */
    frame();
    ok &= rk_px(0, 0, 12) == T_THEME && rk_px(4, 0, 7) == T_THEME;
    song.sel = 0;                                       /* .. selected: its cursor's page */
    ui.cursor = 20; ui.bank = 1;
    frame();
    ok &= rk_px(4, 0, 7) == T_THEME && rk_px(0, 0, 14) == T_LINE && rk_bg(0) == T_RAISE;
    bad += check("HOME > TRACKS: LEN > 16: the play head's page (stopped: the cursor's, selected), past LEN blank", ok);

    song.sel = 1;
    t = &trk[3];                                        /* T4, DRUM: a hit, an accented hit */
    ok = drum_track(t);
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->p[P_SLEN] = 16;
    t->step[0].time = t->step[2].time = t->step[4].time = ST_NOTE;
    t->step[0].hit = 1; t->step[2].hit = 1 | 8; t->step[4].hit = 2; t->step[4].acc = 2;
    t->p[P_MUTE] = 0;
    frame();
    ok &= rk_px(0, 3, 6) == T_THEME && rk_px(2, 3, 6) == T_THEME && rk_px(4, 3, 6) == T_TEXT && rk_px(1, 3, 14) == T_LINE;
    bad += check("HOME > TRACKS: DRUM hits as notes, an accented hit TEXT", ok);

    t = &trk[2];                                        /* T3 muted: its cushion and steps DIM, no meter */
    for (i = 0; i < NSTEP; i++) step_clear(&t->step[i]);
    t->p[P_SLEN] = 16;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->p[P_MUTE] = 1;
    trk[3].peak = 20000;
    frame();
    ok = rk_px(0, 2, 7) == T_DIM && rk_at(3, 2, 7) == T_DIM && rk_at(RK_MX + 1, 3, RK_H - 4) != T_LINE && rk_at(RK_MX + 1, 3, RK_H - 4) != T_BG &&   /* (Dah: upright, lit at its foot) */
         rk_at(RK_MX + 1, 2, 7) == T_LINE;   /* (Dah: the meter centred on the taller row) */
    t->p[P_MUTE] = 0;
    frame();
    ok &= rk_px(0, 2, 7) == T_THEME && rk_at(3, 2, 7) == T_THEME;
    bad += check("HOME > TRACKS: MUTE: the cushion and steps DIM; unmuted again: as before; the output meter", ok);

    /* HOME's knobs: the selected track's; ALGORITHM moves the selection; HOME tapped: MIXER, CLOCK, HOME */
    song.sel = 2; ui.force = 1; frame();
    id = ENGINES[trk[2].eng_req % NENGINES]->knob[0];
    v0 = trk[2].p[id];
    {
        int16_t o0 = trk[0].p[id], o1 = trk[1].p[id];
        turn(EN_K1, 3);
        if (trk[2].p[id] == v0) turn(EN_K1, -6);        /* (at its top: the other way) */
        ok = trk[2].p[id] != v0 && trk[0].p[id] == o0 && trk[1].p[id] == o1 && ui.home && home_tracks();
    }
    turn(EN_ALGO, 1);
    frame();
    ok &= song.sel == 3u && rk_bg(3) == T_RAISE && rk_bg(2) == T_SURF;
    id = ENGINES[trk[3].eng_req % NENGINES]->knob[1];
    v0 = trk[3].p[id];
    turn(EN_K2, -2);
    if (trk[3].p[id] == v0) turn(EN_K2, 4);
    ok &= trk[3].p[id] != v0;
    bad += check("HOME > TRACKS: K1..K4 edit the selected track (HOME's four), ALGORITHM moves the selected row", ok);
    press(B_HOME); ok = !ui.home && cur_page()->graph == GR_TRK;
    press(B_HOME); ok &= ui.home;                       /* (Dah: no CLOCK) */
    frame();
    ok &= rk_bg(3) == T_RAISE;
    bad += check("HOME > TRACKS: HOME tapped still goes HOME > MIXER > HOME (the rows back)", ok);

    ui_prefs |= PREF_LARGE;                             /* MENU > LARGE: the cards as on a list page, the rows */
    ui.force = 1; frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && rk_bg(3) == T_RAISE &&
         rk_px(4, 3, 6) == T_TEXT;
    ui_home_view = HV_SCOPE; ui.force = 1; frame();
    ok &= large_kind() == LK_TALL;
    ui_prefs = 0;
    bad += check("HOME > TRACKS with LARGE: cards with M labels, the rows kept (SCOPE: the tall cards as before)", ok);
    ui_power_on();
    return bad;
}

/* #181: a sound load (PRESETS) or another track (ALGORITHM) draws the page again (ui.force) with no blank frame on the
 * way: MIXER cleared its panel to the background before its four columns landed (HOME > TRACKS its rows), a flash on
 * every detent. Every pixel goes from what it showed straight to what it shows next (host_fl_*), on MIXER, HOME (the
 * scope), HOME > TRACKS and ENV, FLAT and LINE, MENU > LARGE off and on; a full redraw still covers the screen */
/* (Dah) GLO + ALGORITHM: the pattern, every track's (FL Studio's), past the last a new one; its list ~1 s; an empty
 * pattern: its record alone (4 bytes) */
/* (Dah) HOME > TRACKS: OCT+ held, the VIEW: KNOB 2 ZOOM (8 16 32 64 steps), KNOB 1 MOVE, KNOB 4 FOLLOW; let go: the
 * octave kept */
static int hvw_cols_ok(uint32_t n) { return hv_cols() == n; }
static int test_home_view(void)
{
    int ok;
    ui_power_on();
    ui_home_view = 1;
    ui.home = 1;
    ui.force = 1;
    frame();
    trk[0].p[P_SLEN] = 64;
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP]; host_pressed |= fm1_in.buttons; frame();
    fm1_ms += 600u; frame();
    ok = hvw.on && hvw_cols_ok(16u);
    turn(EN_K2, -1);
    ok &= hvw_cols_ok(32u);
    turn(EN_K2, -1);
    ok &= hvw_cols_ok(64u);
    turn(EN_K2, 1);
    turn(EN_K2, 1);
    ok &= hvw_cols_ok(16u);
    {
        uint32_t b0 = hv_base(&trk[0], 1);
        turn(EN_K1, -1);
        ok &= hvw.nofollow && hv_base(&trk[0], 1) + 8u == b0;
    }
    turn(EN_K4, 1);
    ok &= !hvw.nofollow;
    turn(EN_K3, -1);                                    /* (Dah) KNOB 3: the track on no pattern ("-") */
    ok &= (seq_off & 1u) && seq_play_steps(&trk[0]) == chain_silent && seq_steps(&trk[0]) == trk[0].step;
    turn(EN_K3, 1);
    ok &= !(seq_off & 1u) && ps_num(0) == 1u && seq_play_steps(&trk[0]) == trk[0].step;
    fm1_in.buttons = 0; frame();
    ok &= !hvw.on && song.octave == 0;
    turn(EN_K2, -1);                                    /* (closed: KNOB 2 is HOME's again) */
    ok &= hvw_cols_ok(16u);
    ui_home_view = 0;
    hvw.zoom = 1; hvw.at = -1;
    return check("HOME TRACKS OCT+ held (Dah): the VIEW (MOVE, ZOOM 8..64 steps, PAT: the track's, - none, FOLLOW); let go: closed, no octave", ok);
}
static int test_glo_pattern(void)
{
    int bad = 0, ok;
    uint32_t k, used;
    ui_power_on();
    frame();
    go_title("ENV"); frame();
    k = ui.page;
    trk[0].step[0].time = ST_NOTE;
    trk[0].step[0].n = 1;
    trk[0].step[0].note[0] = 60;
    trk[0].step[0].vel = 100;
    (void)k;
    btn_down(B_GLO); frame();
    host_enc[panel.enc[EN_ALGO]] += panel.dir[EN_ALGO];
    frame();
    ok = ui.confirm == CF_NEW_PAT && ps_num(0) == 1u;    /* (past the last: NEW PATTERN 2? asked; OCT+ yes) */
    btn_up(B_GLO); frame();
    press(B_OCTUP);
    btn_down(B_GLO); frame();
    ok &= tl_on() && tl_pat;
    tl_ms = 0;
    frame();
    for (k = 0; k < trk_count(); k++)
        ok &= ps_num(k) == 2u;
    ok &= trk[0].step[0].time == ST_REST;
    turn(EN_ALGO, -1);
    for (k = 0; k < trk_count(); k++)
        ok &= ps_num(k) == 1u;
    ok &= trk[0].step[0].time == ST_NOTE && trk[0].step[0].note[0] == 60;
    used = ps_used;
    turn(EN_ALGO, 1);
    ok &= ps_num(0) == 2u && ps_used > used;
    turn(EN_ALGO, -1);
    ok &= ps_num(0) == 1u && ps_used == used;
    btn_up(B_GLO); frame();
    ok &= str_eq(cur_page()->title, "ENV");
    bad += check("GLO + ALGORITHM: every track to the next pattern, past the last a new one, its list shown", ok);
    used = ps_used;
    for (k = 1; k < trk_count(); k++)
        ok &= ps_find(k, 2) >= 0 && ps_rlen((uint32_t)ps_find(k, 2)) == 0u;
    bad += check("an empty pattern stored: its record alone, 4 bytes", ok);
    return bad;
}
static int test_no_flash(void)
{
    static const char *const VIEW[4] = {"MIXER", "HOME", "HOME TRACKS", "ENV"};
    static const uint8_t KN[2] = {EN_PRESET, EN_ALGO};
    int bad = 0, ok = 1;
    uint32_t v, st, lg, k, n, miss;
    for (v = 0; v < 4u; v++)
        for (st = 0; st < 2u; st++)
            for (lg = 0; lg < 2u; lg++)
                for (k = 0; k < 2u; k++) {
                    ui_power_on();
                    ui_style = st ? ST_LINE : ST_FLAT;
                    ui_prefs = lg ? PREF_LARGE : 0u;
                    ui_home_view = v == 2u;
                    if (v == 1u || v == 2u) {
                        ui.home = 1;
                        ui.force = 1;
                    } else {
                        go_title(VIEW[v]);
                    }
                    frames(100);
                    miss = song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    host_fl_begin();
                    host_enc[panel.enc[KN[k]]] += panel.dir[KN[k]];   /* (a sound loaded / the next track: the page
                                                         * drawn again; Dah: its list over the panel) */
                    frame();
                    host_ticks += 200000u;
                    n = host_fl_end(0, 240);
                    pl_ms = tl_ms = 0;
                    frame();
                    ok &= (!k && (v == 0u || v == 3u)) ||   /* (PRESETS loads on HOME only) */
                          miss != song.sel * 65536u + TSEL->eng_req * 256u + TSEL->preset;
                    if (n)
                        printf("    #181 %s %s%s, %s: %u pixels flashed\n", VIEW[v], st ? "LINE" : "FLAT",
                               lg ? " LARGE" : "", k ? "ALGORITHM" : "PRESETS", n);
                    ok &= n == 0u && ui.force == 0;
                    for (n = 0; n < 240u * 240u; n++)    /* and the redraw still covers the whole screen */
                        host_screen[n] = 0x5A5A;
                    ui.force = 1;
                    frame();
                    for (n = miss = 0; n < 240u * 240u; n++)
                        miss += host_screen[n] == 0x5A5A;
                    if (miss)
                        printf("    #181 %s %s%s: %u pixels not drawn\n", VIEW[v], st ? "LINE" : "FLAT", lg ? " LARGE" : "", miss);
                    ok &= miss == 0u;
                }
    ui_style = ST_FLAT;
    ui_prefs = 0;
    ui_home_view = 0;
    bad += check("#181 PRESETS / ALGORITHM on MIXER, HOME, HOME > TRACKS, ENV (FLAT, LINE, LARGE): redrawn, no blank frame", ok);
    return bad;
}

/* 1.4.1 ENGINE QUICK ACTIONS (ui_layer.c EQA): the EDIT layer's black keys are the selected track's engine's. DRUM:
 * black keys 1..8 MUTE its lanes (eng_drum.c drum_mute). qa_beat: T4's own output (track_render) of a beat whose hits
 * strike the lanes of `lanes` (trk_note_on: the keys', MIDI's and the sequencer's way in), 16 steps of 8 blocks; its
 * hash and the energy under ~150 Hz (a one-pole low-pass: the kick's) */
static uint64_t qa_beat(uint32_t lanes, double *low)
{
    static const uint8_t NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};
    track_t *t = &trk[3];
    int32_t o[CTL];
    uint32_t s, b, i, l, p;
    uint64_t h = 1469598103934665603ull;
    double lp = 0;
    for (p = 0; p < NPART; p++)
        memset(trk[p].v, 0, sizeof trk[p].v);
    memset(eng_mem, 0, sizeof eng_mem); memset(eng_mem_s, 0, sizeof eng_mem_s);   /* (tracks 3.. in the small regions) */
    *low = 0;
    for (s = 0; s < 16u; s++) {
        uint32_t hit = (s % 4u == 0u ? 1u : 0u) | (s % 8u == 4u ? 2u : 0u) | (s == 14u ? 4u : 0u) |
                       (s % 2u == 0u ? 8u : 0u) | (s == 6u ? 16u : 0u) | (s == 11u ? 32u : 0u) | (s == 3u ? 64u : 0u) |
                       (s == 9u ? 128u : 0u);
        for (l = 0; l < NLANE; l++)
            if (((hit & lanes) >> l) & 1u)
                trk_note_on(t, NOTE[l], s == 0u ? 120u : 100u);
        for (b = 0; b < 8u; b++) {
            track_render(t, o, CTL);
            for (i = 0; i < CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                lp += ((double)o[i] - lp) * 0.02;       /* (~150 Hz at 48 kHz) */
                *low += lp * lp;
            }
        }
        for (l = 0; l < NLANE; l++)
            if (((hit & lanes) >> l) & 1u)
                trk_note_off(t, NOTE[l]);
    }
    return h;
}
static int test_edit_quick_actions(void)
{
    int bad = 0, ok;
    uint32_t i, a, b2, n;
    uint64_t h_all, h_nokick, h_mute, h_again;
    double e_all, e_nokick, e_mute, e_again;
    int32_t o[2u * CTL];

    /* the map per engine: DRUM 8 actions (the lanes), every other engine none */
    ui_power_on();
    ok = 1;
    for (i = 0; i < NENGINES; i++) {
        trk[0].eng_req = (uint8_t)i;
        n = eqa_count(&trk[0]);
        ok &= i == ENGI_DRUM ? n == NLANE : n == 0u;
        ok &= !eqa_of(&trk[0], i == ENGI_DRUM ? NLANE : 0u);
    }
    ui_power_on();
    bad += check("1.4.1 EDIT quick actions: DRUM 8 (black keys 1..8), every other engine none", ok);

    /* DRUM (T4): EDIT held, a black key toggles its lane's mute (SET, latched); LEDs lit = sounding, dark = muted */
    ui_power_on();
    track_select(3); go_home(); frame();
    ok = drum_track(TSEL) && !drum_mute[3];
    btn_down(B_EDIT); frames(500);
    a = leds_at(0); b2 = leds_at(250);
    for (i = 0; i < 11u; i++)                         /* 1..8 lit, steady; 9..11 dark */
        ok &= (((a & b2) >> black(i)) & 1u) == (i < NLANE) && !(((a ^ b2) >> black(i)) & 1u);
    key_down(black(0)); frame(); key_up(black(0)); frame();
    key_down(black(5)); frame(); key_up(black(5)); frame();
    ok &= drum_mute[3] == (1u | 32u) && msg_is("TOM MUTED") && !gates() && ui.layer == LAYER_EDIT;
    a = leds_at(0); b2 = leds_at(250);
    ok &= !(((a | b2) >> black(0)) & 1u) && !(((a | b2) >> black(5)) & 1u) && ((a & b2) >> black(1)) & 1u;
    key_down(black(5)); frame(); key_up(black(5)); frame();
    ok &= drum_mute[3] == 1u && msg_is("TOM ON");
    btn_up(B_EDIT); frame();
    ok &= drum_mute[3] == 1u && ui.home && !ui.layer;
    bad += check("  DRUM: EDIT + black key 1 / 6: KICK / TOM muted (latched; LEDs dark), again: on (lit); 9..11 dark", ok);
    ok = motion_count(TSEL) == 0u;
    bad += check("  the mutes record nothing (no sound load)", ok);

    /* the white keys as before (an engine pick from DRUM: the sound load clears the mutes) */
    btn_down(B_EDIT); frames(500);
    key_down(white(0)); frame(); key_up(white(0)); frame();
    ok = TSEL->eng_req == eng_vis(0) && !drum_mute[3] && eqa_count(TSEL) == 0u;
    a = leds_at(0); b2 = leds_at(250);
    for (i = 0; i < 11u; i++)                         /* (ANALOG: no actions, the black keys dark and inert) */
        ok &= !(((a | b2) >> black(i)) & 1u);
    {
        track_t before = *TSEL;
        uint8_t m[NPART];
        memcpy(m, drum_mute, sizeof m);
        ui.msg_t = 0;
        for (i = 0; i < 11u; i++) {
            key_down(black(i)); frame(); key_up(black(i)); frame();
        }
        ok &= !memcmp(m, drum_mute, sizeof m) && !ui.msg_t && !gates() && before.eng_req == TSEL->eng_req &&
              !memcmp(before.p, TSEL->p, sizeof before.p) && ui.layer == LAYER_EDIT;
    }
    btn_up(B_EDIT); frame();
    bad += check("  a white key loads its engine (the mutes go with the sound); ANALOG's black keys dark, inert", ok);
    set_engine_of(TSEL, ENGI_DRUM); TSEL->engine = TSEL->eng_req; frame();
    drum_mute[3] = 1u;                                /* (DRUM again, KICK muted) */

    /* an OCT- tap: nothing (the mutes stay) */
    btn_down(B_EDIT); frames(500);
    key_down(black(0)); frame(); key_up(black(0)); frame();   /* KICK on */
    key_down(black(2)); frame(); key_up(black(2)); frame();   /* CLAP muted */
    key_down(black(7)); frame(); key_up(black(7)); frame();   /* BELL muted */
    ok = drum_mute[3] == (4u | 128u);
    oct_back();
    ok &= drum_mute[3] == (4u | 128u) && song.octave == 0 && ui.layer == LAYER_EDIT;
    btn_up(B_EDIT); frame();
    bad += check("  an OCT- tap in the layer: nothing (the mutes stay), no octave", ok);
    drum_mute[3] = 1u;

    /* locked (a double tap), and while playing */
    go_title("ENV"); frame();
    press(B_EDIT); frames(64); press(B_EDIT); frames(500);
    ok = ui.lock == LAYER_EDIT;
    key_down(black(1)); frame(); key_up(black(1)); frame();
    ok &= drum_mute[3] == 3u && str_eq(cur_page()->title, "ENV") && !gates() && ui.lock == LAYER_EDIT;
    press(B_EDIT); frames(16);
    ok &= !ui.lock && !ui.layer && drum_mute[3] == 3u;
    bad += check("  EDIT locked: a black key toggles its lane; closed: kept", ok);
    drum_mute[3] = 1u;
    song.playing = 1;
    btn_down(B_EDIT); frames(500);
    key_down(black(3)); frame(); key_up(black(3)); frame();
    ok = drum_mute[3] == (1u | 8u) && song.playing && msg_is("HATCL MUTED");
    btn_up(B_EDIT); frame();
    song.playing = 0;
    bad += check("  while playing: HATCL muted at once", ok && drum_mute[3] == 9u);

    /* the kit's own names: KIT 66 plays a conga on TOM */
    TSEL->p[P_E0] = 6;                                /* (DK_66) */
    ok = str_eq(eqa_of(TSEL, 5)->name(TSEL, 5), "CG") && str_eq(eqa_of(TSEL, 0)->name(TSEL, 0), "BD");
    btn_down(B_EDIT); frames(500);
    key_down(black(5)); frame(); key_up(black(5)); frame();
    ok &= msg_is("CONGA MUTED") && drum_mute[3] == (9u | 32u);
    btn_up(B_EDIT); frame();
    TSEL->p[P_E0] = 0;
    bad += check("  a model kit: the cells and messages its lanes' names (CG, CONGA MUTED)", ok);

    /* the grid shows them (its signature changes: redrawn) */
    go_page(GR_ROLL); frame();
    {
        uint32_t s0 = graph_signature();
        drum_mute[3] ^= 2u;
        ok = graph_signature() != s0;
        drum_mute[3] ^= 2u;
        ok &= graph_signature() == s0;
    }
    bad += check("  the DRUM grid redraws when a lane is muted / unmuted", ok);

    /* not saved: a sound load into the track and a project load clear them */
    go_home(); frame();
    turn(EN_PRESET, 1); frame();
    ok = !drum_mute[3];
    {
        int r = project_save(0);
        drum_mute[3] = 5u;
        project_load(0);
        ok &= r == 0 && !drum_mute[3];
    }
    bad += check("  not saved: a sound load into the track and a project load clear them", ok);

    /* the sound: KICK muted = the beat without its kicks, bit for bit; unmuted: bit for bit as never muted */
    ui_power_on();
    track_select(3); go_home(); frame();
    song.master_q12 = 4096;
    for (i = 0; i < 256u && (trk[3].xf_on || trk[3].eng_req != trk[3].engine); i++)
        mix_block(o, CTL);
    h_all = qa_beat(0xFFu, &e_all);
    h_nokick = qa_beat(0xFEu, &e_nokick);
    drum_mute[3] = 1u;
    h_mute = qa_beat(0xFFu, &e_mute);
    drum_mute[3] = 0u;
    h_again = qa_beat(0xFFu, &e_again);
    ok = h_mute == h_nokick && h_all != h_mute && h_again == h_all && e_mute == e_nokick && e_all > e_mute;
    {   /* the kick struck alone: its energy, none muted */
        double ek, ekm;
        (void)qa_beat(1u, &ek);
        drum_mute[3] = 1u;
        (void)qa_beat(1u, &ekm);
        drum_mute[3] = 0u;
        ok &= ek > 0.0 && ekm == 0.0;
        printf("    low band energy: the beat %.4g, KICK muted %.4g (%.1f dB); the kicks alone %.4g, muted %.4g\n", e_all,
               e_mute, 10.0 * log10(e_mute / e_all), ek, ekm);
    }
    bad += check("  KICK muted: the beat as with no kick struck, the kicks alone silent; unmuted: as before (bit for bit)", ok);
    drum_mute[3] = 0xFFu;
    (void)qa_beat(0xFFu, &e_mute);
    drum_mute[3] = 0u;
    bad += check("  every lane muted: silent", e_mute == 0.0);
    {   /* muted mid-ring and unmuted: the lane rang on unheard (its voice, its state), as never muted after */
        static const uint8_t NOTE2[2] = {36, 46};
        uint64_t hm = 0, hu = 0;
        uint32_t r, j, k;
        for (r = 0; r < 2u; r++) {
            uint64_t h = 1469598103934665603ull;
            for (k = 0; k < NPART; k++)
                memset(trk[k].v, 0, sizeof trk[k].v);
            memset(eng_mem, 0, sizeof eng_mem); memset(eng_mem_s, 0, sizeof eng_mem_s);   /* (tracks 3.. in the small regions) */
            for (j = 0; j < 2u; j++)
                trk_note_on(&trk[3], NOTE2[j], 100);
            for (j = 0; j < 64u; j++) {
                drum_mute[3] = (uint8_t)(r && j >= 8u && j < 24u ? 16u : 0u);   /* (HATOP muted 8 .. 23) */
                track_render(&trk[3], o, CTL);
                if (j >= 24u)
                    for (k = 0; k < CTL; k++)
                        h = (h ^ (uint32_t)o[k]) * 1099511628211ull;
            }
            *(r ? &hm : &hu) = h;
        }
        drum_mute[3] = 0;
        bad += check("  HATOP muted mid-ring, unmuted: it sounds on bit for bit as if never muted (a channel mute)", hm == hu);
    }
    ui_power_on();
    return bad;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0;
    bad += test_large_face();
    bad += test_sound_loads();
    bad += test_rec();
    bad += test_step_rec();
    bad += test_step_rec();
    bad += test_step_preview_chord_entry();
    bad += test_presets_knob();
    bad += test_midi();
    bad += test_save();
    bad += test_actions();
    bad += test_tracks();
    bad += test_grid();
    bad += test_screen();
    bad += test_categories();
    bad += test_display_preferences();
    bad += test_menu_tabs();
    bad += test_menu_project_rows();
    bad += test_information();
    bad += test_chain();
    bad += test_song_timeline();
    bad += test_song_lanes();
    bad += test_song_lanes_drawn();
    bad += test_song_pictures();
    bad += test_head_slot();
    bad += test_product_ux();
    bad += test_page_cycles();
    bad += test_mono_screens();
    bad += test_roll();
    bad += test_soak_cards();
    bad += test_panel();
    bad += test_layer();
    bad += test_layer_knob_leak();
    bad += test_layer_knob_race();
    bad += test_fx_latch();
    bad += test_fx_map();
    bad += test_layer_lock();
    bad += test_rec_layer();
    bad += test_seq_tools();
    bad += test_edit_quick_actions();
    bad += test_menu_prefs();
    bad += test_style();
    bad += test_head_centres();
    bad += test_head_metronome();
    bad += test_head_group();
    bad += test_div_order();
    bad += test_knob_accel();
    bad += test_usb_level();
    bad += test_click_menu();
    bad += test_usb_serial();
    bad += test_bpm_lock();
    bad += test_large();
    bad += test_browse_no_pattern();
    bad += test_name();
    bad += test_edit_cycle();
#if FELUCCA_SLICE
    bad += test_slices();
#endif
    bad += test_sample_alert();
    bad += test_quick_layers();
    bad += test_chord_page();
    bad += test_bughunt_ui();
    bad += test_bughunt_ui2();
    bad += test_piano_roll();
    bad += test_play_leds();
    bad += test_cursor_step_leds();
    bad += test_midi_leds();
    bad += test_seq_quant();
    bad += test_idle_glow();
    bad += test_breath();
    bad += test_step_leds();
    bad += test_scale_leds();
    bad += test_screen_off();
    bad += test_screen_wake();
    bad += test_idle_leds();
    bad += test_momentary();
    bad += test_fm6_charts();
    bad += test_fm6_ops();
    bad += test_home_tracks();
    bad += test_extras();
    bad += test_fx_map_columns();
    bad += test_help();
    bad += test_no_flash();
    bad += test_glo_pattern();
    bad += test_home_view();
#if FELUCCA_FM4
    bad += test_fm_charts();                        /* (DIGITAL's charts: built with FELUCCA_FM4=1 only) */
#else
    bad += test_fm4_retired();
#endif

    if (getenv("UI_SCREEN_DIR")) chain_screens(getenv("UI_SCREEN_DIR"));
    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif
