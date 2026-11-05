/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_HOME};   /* MIXER: HOME's (1.2); REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
/* the OCT LEDs, bit 0 OCT- lit, bit 1 OCT+ lit, OCT_BREATH OCT+ breathing (can be pressed: dark .. ~60 %,
 * hal/fm1_input.h fm1_led_breath, #119). In the dialogs and on action pages OCT- (back) is lit and OCT+ breathes
 * while it would do something; the menu: on a value row OCT- and OCT+ both breathe while that way has a value to go to,
 * on CALIBRATION / ABOUT OCT+ breathing alone (opens), in ABOUT OCT- lit (back); elsewhere the octave shift */
#define OCT_BREATH 4u
#define OCT_BREATH_DN 8u                                /* OCT- breathing (the menu: the previous value) */
/* MOMENTARY (1.2, Discussions #72 / #73): LFO held, the knobs' changes are temporary. KNOB 1..4 on HOME and on the
 * sound and FX pages (a value of the selected track or a global sound / FX one, as edit_param sets it; MIXER's LEVEL
 * PAN REV MUTE too, #195; not on the action pages, the lists, STEP, AUTOMATION or SONG, where the knobs pick) change
 * their value as ever, and each value turned is put back as it was when LFO is let go: hold LFO, sweep the filter,
 * let go, it snaps back. OCT+ while
 * LFO is still held KEEPS them (the changes stay, nothing is put back; OCT+ breathes while something would go back;
 * LFO has no LED). LFO's tap (its page) is dropped once any knob turned with it; LFO alone still opens LFO. Temporary
 * turns are not recorded as automation (motion_capture); a sound or project load meanwhile (the
 * PRESETS knob, the editor, a song's section) puts that track's values (and the global ones) back first (mom_back: the
 * load's sound stays after). The menu, a dialog, NAME or a layer opening put
 * them back too. Up to MOM_N values at a time ("MOMENTARY FULL" past them: the turn does nothing). LFO held did
 * nothing (it is no layer; OCT- is the octave and a layer's, so not it), so no gesture changes: LFO + a knob
 * turned opened LFO's page on release before, and edited the page under it for good. The values: ui.c mom */
static int mom_down(void)                              /* LFO held where its turns are momentary */
{
    return ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u) && !ui.menu && !ui.confirm && !name_on() && !ui.layer && !ui.uboot;
}
/* KNOB k turned s with LFO held: 1 = taken (a momentary value, or refused full), 0 = not a momentary value (the turn
 * goes as ever: the column picks, steps, acts) */
static int mom_turn(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int16_t *vp = 0;
    uint32_t i, g;
    const track_t *t = ui.home ? home_trk(k) : TSEL;     /* (HOME LEVELS: KNOB k is track k's) */
    if (ui.home) {
        d = home_param(k, &vp);
    } else if (pg->scope == SC_TRK && !lock_held()) {  /* MIXER (#195): the selected track's LEVEL PAN REV MUTE */
        static const uint8_t MIX_ID[4] = {P_LEVEL, P_PAN, P_SPRD, P_MUTE};   /* (tracks_edit's knobs; Dah: WIDTH) */
        vp = &TSEL->p[MIX_ID[k & 3u]];
        d = &TP[MIX_ID[k & 3u]];
    } else {
        if (act_cols() || lock_held() || !(pg->scope == SC_TRACK || pg->scope == SC_ENGINE || pg->scope == SC_GLOBAL) ||
            pg->graph == GR_EVENTS || pg->graph == GR_SONG || pg->graph == GR_USER ||
            (pg->graph == GR_SLICES && k < 2u))
            return 0;
        d = page_desc(pg, k, &vp);
    }
    g = vp && vp >= song.g && vp < song.g + G_COUNT ? (uint32_t)(vp - song.g) : G_COUNT;
    if (!d || !vp || d->max == d->min ||
        !((vp >= t->p && vp < t->p + P_COUNT) || (g <= G_CDEPTH && g != G_CLOCK) || g == G_RTYPE))
        return 0;                                      /* (only the values a sound and the FX are: no CLK, no actions) */
    for (i = 0; i < mom.n && mom.vp[i] != vp; i++)
        ;
    if (i == mom.n) {                                  /* the first turn of this value: what it was */
        if (mom.n >= MOM_N) {
            ui_message("MOMENTARY FULL");
            return 1;
        }
        mom.vp[i] = vp;
        mom.v[i] = *vp;
        mom.n++;
        if (i == 0u)
            ui_message("MOMENTARY  [OCT+] KEEP");
    }
    *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, vp == &TSEL->p[P_SPRD] && pg->scope == SC_TRK ? -s : s,
                                            d->fmt == F_ENUM ? 0 : d->max - d->min));   /* (the MIXER's WIDTH: reversed) */
    return 1;
}

/* 1.5.6 (#84): MENU > SELECT MODW / VEL: SELECT plays. MODW: the selected track's mod wheel 0..127, sent as CC1
 * on the track's channel, where the keys' notes go (nothing on the device reads it); VEL: the panel keys'
 * velocity 1..127 (100 from power-on, as before; MIDI OUT and live recording take it). Performance state, not saved, as
 * a keyboard's wheel. The value shows in the header. GLO + SELECT is the tempo, as with PAGE */
static void sel_play(uint32_t m, int32_t s)
{
    char b[8];
    if (m == SEL_MODW) {
        track_t *t = TSEL;
        t->mw = (uint8_t)clamp((int32_t)t->mw + accel(EN_SELECT, s, 127), 0, 127);   /* (a byte: the ISR sees one or the
                                                                                  * other) */
        fm1_irq_off();                                 /* (the out ring is the audio ISR's: it must not run now) */
        midi_out_event(0x0Bu | (0xB0u | trk_midi_ch(song.sel)) << 8 | 1u << 16 | (uint32_t)t->mw << 24);
        fm1_irq_on();
        fmt_int(b, t->mw);
        ui_say("MODW ", b);
    } else {
        kb_vel = (uint8_t)clamp((int32_t)kb_vel + accel(EN_SELECT, s, 126), 1, 127);
        fmt_int(b, kb_vel);
        ui_say("VEL ", b);
    }
}

static uint32_t oct_leds(void)
{
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (breathing) writes */
        return 1u | OCT_BREATH;
    if (layer_set_open())                               /* a SET layer: OCT- the layer's (held: RESET), OCT+ nothing */
        return 1u;
    if (ui.menu && !ui.confirm)
        return ui.menu >= 2u ? 1u :                    /* (INFO, ABOUT: OCT- back, lit) */
               ui.menu_sel == MI_LCLEAR ? (ml_count() ? OCT_BREATH : 0u) :   /* LEARN CLEAR: while CCs are learned */
               ui.menu_sel >= MI_VALUES ? OCT_BREATH :  /* CALIBRATION / INFO / ABOUT: OCT+ opens */
               (menu_step(ui.menu_sel, -1) != menu_get(ui.menu_sel) ? OCT_BREATH_DN : 0u) |   /* a value: each */
               (menu_step(ui.menu_sel, 1) != menu_get(ui.menu_sel) ? OCT_BREATH : 0u);         /* way it can go */
    if (ui.confirm)
        return 1u | OCT_BREATH;
    if (act_cols())                                     /* (Dah) OCT- lit only when it cancels (no BACK) */
        return (ui.act ? 1u : 0u) | (act_ready() ? OCT_BREATH : 0u);
    if (mom.n && mom_down())                            /* MOMENTARY: OCT+ keeps the changes */
        return (song.octave < 0 ? 1u : 0u) | OCT_BREATH;
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* (long patterns) the masks of steps held (locks, NUDGE, DETAIL): bit i is step ed_base() + i, the cursor's page */
static uint32_t ed_base(void) { return (uint32_t)ui.cursor / NSTEP * NSTEP; }
static uint64_t ed_keys(uint32_t keys, uint32_t len)   /* the grid's white keys held, on the page shown */
{
    uint32_t k, i;
    uint64_t m = 0;
    for (k = 0; k < 27u; k++)
        if (((keys >> k) & 1u) && !key_black(k) && (i = ui.bank * 16u + key_place(k)) < len && i - ed_base() < NSTEP)
            m |= (uint64_t)1 << (i - ed_base());
    return m;
}
/* the key LEDs on the grid, bit k = key k: the white keys show where the selected lane hits on the page
 * shown (its accents while ACC is held), the step playing inverted (a light walks over them); the black keys
 * the lane selected, ACC while held, the page keys while there is more than one page */
static uint32_t grid_leds(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = detail_on() ? 0xFFu : 1u << ui.lane;   /* (DETAIL: any hit) */
    uint32_t acc = !detail_on() && black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            on = i < len && ((acc ? step_accents(ui_st(t, i)) : step_lanes(ui_st(t, i))) & b) != 0u;
            on ^= (uint32_t)(p == ph);
        } else {
            on = p < NLANE ? p == ui.lane : p == GK_ACC ? acc : len > 16u;
        }
        m |= on << k;
    }
    return m;
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}
/* (Dah) SCL as ARP: lit while CHORD KEYS add notes, a track with them on sounding a chord (two voices or more held) */
static uint32_t chord_led(void)
{
    uint32_t k, i, n;
    for (k = 0; k < NPART; k++) {
        if (!trk[k].p[P_CHRD] || drum_track(&trk[k]) || trk_vmode(&trk[k]) != V_POLY)
            continue;
        for (i = n = 0; i < NVOICE; i++)
            n += trk[k].v[i].active && trk[k].v[i].gate;
        if (n >= 2u)
            return 1u;
    }
    return 2u;
}

/* Discussion #81: the keys of the notes MIDI IN (USB and TRS, routed by ROUT) holds on track t, bit k = key k: as
 * play_leds, where the keys play that note at the octave now, the lowest key that gives it; a note no key plays is
 * not shown. Read from midi_control.c's own state (midi_note_held: the notes each channel holds for the track, the
 * pedal's too, and the tones of MIDI chords): no state here; nothing to do while MIDI holds none of the track's */
static uint32_t midi_leds(const track_t *t)
{
    uint32_t k, note, m = 0, seen[4] = {0, 0, 0, 0};
    if (!midi_owners[trk_index(t)])
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        if (note > 127u || ((seen[note >> 5] >> (note & 31u)) & 1u))
            continue;                                   /* (silent, or a lower key gives it) */
        seen[note >> 5] |= 1u << (note & 31u);
        m |= (uint32_t)midi_note_held(t, note) << k;
    }
    return m;
}

/* the keys of the notes the selected track's sequencer and ARP sound now (#38), bit k = key k: where the keys
 * play that note (kb_map: the octave, TRN, QNT, an engine's own map), the lowest key that gives it (QNT SNAP
 * rounds the keys above down onto it); a note no key plays is not shown. A snapshot of the ISR's seq_notes /
 * arp_note (arp_ch): no state of its own, nothing to do while nothing sounds */
static uint32_t play_leds(void)
{
    const track_t *t = TSEL;
    uint8_t s[4 + NLANE + 4];
    uint32_t n = t->seq_n < 4u + NLANE ? t->seq_n : 4u + NLANE, i, k, note, used = 0, m = 0, hit;
    for (i = 0; i < n; i++)
        s[i] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (i = 0; i < 3u; i++)                            /* (the ARP's CHORD) */
        if (t->arp_ch[i])
            s[n++] = t->arp_ch[i];
    for (k = 0; n && k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0, hit = 0; i < n; i++)
            if (s[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                hit = 1;
            }
        m |= hit << k;
    }
    return m | midi_leds(t);
}

/* Discussion #89: on SEQ > STEP (the piano roll; the DRUM grid has its own map) while stopped, the keys of the
 * notes stored in the cursor step, bit k = key k: as play_leds, where the keys play that note now (kb_map: the
 * octave, TRN, QNT), the lowest key that gives it; a note no key plays is not shown, nor a REST step's. Playing,
 * the keys show what plays (play_leds) instead. Read from the step: no state of its own */
static uint32_t step_leds(void)
{
    const track_t *t = TSEL;
    const step_t *st;
    uint32_t i, k, note, used = 0, m = 0;
    if (ui.home || ui.menu || ui.confirm || !song.seq_mode || cur_page()->graph != GR_ROLL || song.playing ||
        chain_busy())
        return 0;
    st = ui_st(t, ui.cursor);
    if (!st->n || st_kind(st) == ST_REST)
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0; i < st->n && i < 4u; i++)
            if (st->note[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                m |= 1u << k;
            }
    }
    return m;
}

/* 1: the keys show a map of their own (NAME, a layer's map: SCL's scale, FX; the DRUM grid, SLICES), lit or
 * dark; 0: the keys held and the notes playing, over the idle glow */
static int keys_own(void)
{
    return (name_on() && !ui.menu) || ui.layer || grid_write() || song.grid == 4u
#if FELUCCA_SLICE
           || (!ui.menu && !name_on() && slice_page_on())
#endif
        ;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, the keys of the selected track's scale (ROOT, SCALE), bit k = key k, as
 * the keys play now: the note a key gives with TRN (QNT SNAP / SEQ: the key's own note; the others round down onto
 * the scale); QNT WHITE every white key (they walk the scale; the black ones are silent). *root: the keys of the
 * scale's root. None where the engine maps the keys itself (DRUM's GM map, the slices): 0 there, and OFF */
static uint32_t scale_leds(uint32_t *root)
{
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t k, m = 0, r = 0, mask = scale_mask(t), note;
    int32_t pc;
    *root = 0;
    if (!(ui_rec_prefs & (PREF_SCALE_LEDS >> 8)))
        return 0;
    for (k = 0; k < 27u; k++) {
        if (e->keys && e->keys(t, k) >= 0)
            continue;
        if (t->p[P_QUANT] == QN_WHITE) {
            if ((note = kb_map(t, k)) > 127u)
                continue;                               /* (a black key: silent) */
            pc = (int32_t)note - t->p[P_TRANS];         /* (WHITE walks the scale before TRN) */
        } else {
            pc = 53 + (int32_t)k + t->p[P_TRANS];
        }
        pc = ((pc - t->p[P_ROOT]) % 12 + 12) % 12;
        m |= ((mask >> pc) & 1u) << k;
        r |= (uint32_t)(pc == 0) << k;
    }
    *root = r & m;
    return m;
}

static uint32_t song_leds(void);
/* MENU > ANIM IDLE (1.2, Discussion #135): after IDLE_LEDS_MS without panel input (ui.c scrn.idle: a button or key
 * pressed or held, a knob turned; what SCREEN OFF counts) the LEDs play the idle animation (hal/fm1_led_anim.h
 * fm1_idle_level: a soft light drifting over the keys and back, the buttons breathing with it; the scan draws it,
 * hal/fm1_input.h fm1_led_idle_start). Only with nothing going on: stopped (no count-in, no song, nothing armed or
 * waiting to start), no layer (held or locked), menu, dialog, NAME or UPDATE countdown, no key held or lit by the
 * selected track (MIDI IN notes), not with ANIM OFF. Any input ends it at once and is not swallowed (as SCREEN OFF's
 * wake would be): a key or a button down ends it in the scan before its next frame (the press acts as ever), a knob
 * here (the turn acts as ever); so does the transport starting (MIDI Start, the editor) or anything above. 1 = it has
 * the LEDs (ui_leds writes none). A dialog or the count-in count as input, as for SCREEN OFF (ui.c scr_frame) */
#define IDLE_LEDS_MS 60000u
static int idle_leds(void)
{
    int due = (ui_idle & 1u) && !(ui_prefs & PREF_ANIM_OFF) && !song.playing && !seq_counting() && !chain_busy() &&
              !song.rec && !transport_req && !ui.layer && !ui.lock && !ui.menu && !ui.confirm && !ui.uboot && !name_on() &&
              !(fm1_in.notes | fm1_in.buttons) && !panel_moved && !play_leds() && fm1_ms - scrn.idle >= IDLE_LEDS_MS;
    if (!due) {
        if (fm1_led_idle_on())
            fm1_led_anim_stop();
        return 0;
    }
    if (!fm1_led_anim_on())                            /* (not over the power-on sweep) */
        fm1_led_idle_start(settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO);
    return fm1_led_idle_on();
}
/* the key LEDs, bit k = key k: NAME's keys, the layer's map, the DRUM grid, else the keys held and the notes
 * the selected track's sequencer, ARP and MIDI IN play, on STEP while stopped the cursor step's notes (#89; and
 * on SLICES the keys of the selected slice; on SONG the key of the cell's slot, 1.2) */
static uint32_t key_leds(uint32_t *br)                 /* (*br: a layer's keys that breathe, layer_leds) */
{
    uint32_t c;
    if (br)
        *br = 0;
    c = name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds(br) : grid_write() ? grid_leds() :   /* (Dah: the grid
                                                         * armed only: not armed its keys play, lit as they play) */
                 song_keys_on() ? song_leds() :         /* (Dah: SONG: the patterns only, no notes played) */
                 (fm1_in.notes & ~kb_layer) | play_leds() | step_leds();
#if FELUCCA_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    if (!ui.layer && !ui.menu && !name_on() && !ui.home && cur_page()->graph >= GR_DLANE &&
        cur_page()->graph <= GR_DSYN5 && TSEL->eng_req % NENGINES == ENGI_DRUM) {   /* (Dah) a DRUM lane's pages: the
                                                         * keys that play the lane picked */
        uint32_t k;
        for (k = 0; k < 27u; k++)
            if (drum_lane((uint32_t)drum_keys(TSEL, k)) == dln_lane())
                c |= 1u << k;
    }
    return c;
}

/* (Dah) LIVE's keys (live_keys): the black keys of the project's tracks glow, the selected one's lit, the volume's two
 * glow, MUTE breathes while the track is muted and blinks while a change waits for the pattern's start; the white keys
 * the selected track's pattern a page of 16 at a time (the one playing): the step playing lit, the steps with notes or
 * hits at the mid level. Lit (the return), *br breathing, *g glowing, *mid the mid level */
static uint32_t live_leds(uint32_t *br, uint32_t *g, uint32_t *mid)
{
    const track_t *t = TSEL;
    uint32_t k, p, m = 0, len = (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX), i = song.sel % NTRK;
    uint32_t pg = song.playing && t->seq_idx < len ? t->seq_idx / 16u : 0u;
    uint32_t ph = song.playing && t->seq_idx < len ? t->seq_idx % 16u : 0xFFu;
    *br = *g = *mid = 0;
    for (k = 0; k < 27u; k++) {
        p = key_place(k);
        if (key_black(k)) {
            if (p < NTRK) {
                m |= (uint32_t)(p == i) << k;
                *g |= (uint32_t)(p < trk_count()) << k;
            } else if (p < NTRK + 2u) {
                *g |= 1u << k;
            } else if (live_mreq[i]) {
                m |= (uint32_t)((fm1_ms >> 7) & 1u) << k;   /* (waiting: ~4 Hz) */
                *g |= 1u << k;
            } else if (t->p[P_MUTE]) {
                *br |= 1u << k;
            } else {
                *g |= 1u << k;
            }
        } else if (pg * 16u + p < len) {
            const step_t *st = ui_st(t, pg * 16u + p);
            m |= (uint32_t)(p == ph) << k;
            *mid |= (uint32_t)step_on(st) << k;
        }
    }
    return m;
}

/* The LEDs: lit = active (the page's family, PLAY / REC running, the keys held or playing, a map's keys), the ARP
 * beat flashes, the ones that can be pressed breathe (#119: the open layer's button, OCT+ when it would act, a
 * layer's keys; dark .. ~60 % of lit and back, hal/fm1_input.h fm1_led_breath, in every mode: never a hard blink in
 * a dark room; DIM LO up to ~30 %, DIM HI OFF INV ~60 %; with LEDS OFF it is the only glow; INV leaves it as it is,
 * between the dark active ones and the lit idle ones), every other button and key glows dim (#35: the
 * buttons of the black FM-1 can be found in the dark; hal/fm1_input.h fm1_led_dim, a short pulse each frame).
 * MENU > LEDS: DIM HI (default) that glow, DIM LO a darker one (fm1_led_dim_level), OFF no glow (as 1.0); INV
 * turns it around, as the stock firmware: the idle ones fully lit, the active ones dark, no glow (the ARP beat:
 * lit / dark). The keys' own maps (keys_own) stay lit or dark in every mode: their dark keys read as dark; only the
 * DRUM grid's keys that do something (grid_glow) glow under it, so STEP on a DRUM track is never a dark
 * keyboard (the steps of an empty pattern); on it the first step of each four (grid_beats: 1 5 9 13 of a page, where
 * no hit shows) glows steadier and brighter, so a bar's beats can be counted: DIM HI / DIM LO the mid level
 * (hal/fm1_input.h fm1_led_mid: lit 1 frame in 8, DIM LO in 12, over the glow), OFF and INV the glow alone (no
 * other key glows there). 1.2, MENU > SCALE LEDS ON (scale_leds; not over a map of the keys' own, nor on a DRUM or
 * SLICE track): the keys of the selected track's scale glow and the others are dark, the root's at the mid level, in
 * DIM HI, DIM LO and OFF alike (asked for: OFF only takes the other glow away); INV turns it around as it does the
 * rest: the root's keys lit, the scale's at the mid level, the others glowing; the keys held and playing as ever
 * (lit, INV dark). Each picture is built off-line and copied one byte per column, the
 * glow first: an LED going from lit to dim never has a dark frame */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, og[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint8_t nm[FM1_NCOL] = {0}, sg[FM1_NCOL] = {0}, sm[FM1_NCOL] = {0}, iv[FM1_NCOL] = {0};
    uint32_t k, c, g, br, beat, sc, sr = 0;
    uint32_t fam = cur_fam(), mode = settings_leds, inv = leds_inv();   /* (1.5.6: INV LO 1, INV HI 2) */
    int keys_map = keys_own();
    static uint8_t ready;
    if (idle_leds() || fm1_led_anim_on())              /* the idle animation, the power-on sweep have them (main.c */
        return;                                         /* boot_leds) */
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (chord_led() != 2u && (!ui.layer || layer_btn() != B_SCL))
        led_put(nl, panel.btn[B_SCL], 1);              /* (Dah: a chord sounding) */
    if (!ui.layer && (perf_latched || perf_k[0] || perf_k[1] || perf_k[2] || perf_k[3]))
        led_put(nl, panel.btn[B_FX], 1);                /* FX LATCH: lit while an effect or a macro is on */
    if (ui.layer)                                       /* the layer's button breathes while its map is up */
        led_put(nb, panel.btn[layer_btn()], 1);
    if (ui.menu)                                        /* (Dah) the menu: HOME breathes, as SEQ while its layer is up */
        led_put(nb, panel.btn[B_HOME], 1);
    led_put(nl, panel.btn[B_REC], song.rec != 0u && ui.layer != LAYER_REC);   /* (its layer: it breathes) */
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)((k >> 1) & 1u));
    led_put(nb, panel.btn[B_OCTUP], ((k & OCT_BREATH) && (ui.home || cur_page()->graph != GR_SONG)) || sgw.on || prv.on || hvw.on);
                                                        /* (Dah: the VIEW open, OCT+ held: it glows; SONG: then only) */
    led_put(nb, panel.btn[B_OCTDN], (k & OCT_BREATH_DN) != 0u);
    c = key_leds(&br);
    g = !keys_map ? 0u : grid_write() && !ui.layer && !name_on() ? 0x7FFFFFFu : 0u;   /* (Dah: the DRUM grid glows as any
                                                         * page, no map of what does what: it misled) */
    if (song_keys_on()) {                               /* (Dah) SONG: the white keys the cell's track's patterns: its
                                                         * own lit, the others it has breathing */
        uint32_t kk;
        for (kk = 0; ui.song_trk < NTRK && kk < 27u; kk++) {
            int32_t s2 = song_key(kk);
            if (s2 >= 0 && !((c >> kk) & 1u) && ps_exists(ui.song_trk, (uint32_t)s2 + 1u))
                br |= 1u << kk;
        }
        keys_map = 1;
    }
    beat = 0;                                           /* (Dah: no beats of the grid brighter) */
    if (song.grid == 4u && !ui.layer)                   /* (Dah) LIVE: its own picture */
        c = live_leds(&br, &g, &beat);
    sc = keys_map ? 0u : scale_leds(&sr);               /* SCALE LEDS: the scale's keys glow, the root's brighter */
    for (k = 0; k < 27u; k++) {
        uint32_t in = (sc >> k) & 1u, rt = (sr >> k) & 1u, idle = !((c >> k) & 1u);
        led_put(nm, 14u + k, (int)((beat >> k) & 1u));
        led_put(keys_map ? own : nl, 14u + k, (int)((c >> k) & 1u));
        led_put(keys_map ? og : nd, 14u + k, keys_map ? (int)((g >> k) & 1u) : !sc || (mode == LEDS_INV && rt));
        led_put(nb, 14u + k, (int)((br >> k) & 1u));
        if (!sc)
            continue;
        led_put(sg, 14u + k, mode == LEDS_INV ? !in && idle : (int)in);   /* (INV: the active ones dark) */
        led_put(sm, 14u + k, mode == LEDS_INV ? in && !rt && idle : (int)rt);
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    if (song.playing || seq_counting())                 /* playing: PLAY's green, its own LED dark in every mode */
        led_clear(nd, panel.btn[B_PLAY]);
    if (live_mode)                                      /* (Dah) LIVE MODE: PLAY blinks (~1 Hz) */
        led_put(nl, panel.btn[B_PLAY], (int)((fm1_ms >> 9) & 1u));
    for (c = 0; c < FM1_NCOL; c++) {
        if (mode == LEDS_INV) {                         /* INV: the active ones dark, the rest lit (1.5.6, #212: */
            iv[c] = (uint8_t)(nd[c] & ~nl[c]);          /* INV LO / HI: at the steady mid level, below) */
            nl[c] = inv ? 0u : iv[c];
        }
        nd[c] = mode == LEDS_DIM || mode == LEDS_DIM_LO ? (uint8_t)(nd[c] | og[c]) : nm[c];   /* OFF, INV: no glow but */
        if (mode != LEDS_DIM && mode != LEDS_DIM_LO)    /* the grid's beats (the one level above dark there); DIM HI */
            nm[c] = inv ? iv[c] : 0u;                   /* / LO: the beats at the mid level over the glow; INV LO / HI:
                                                         * INV's lit ones there */
        nd[c] |= sg[c];                                 /* SCALE LEDS: in every mode (INV: inverted above) */
        nm[c] |= sm[c];
        nm[c] &= (uint8_t)~(nb[c] | own[c]);
        nl[c] |= own[c];
        nl[c] &= (uint8_t)~nb[c];                       /* breathing: neither lit (INV's idle) nor the glow */
        nd[c] &= (uint8_t)~nb[c];
    }
    if (mx_master && !ui.menu && !ui.confirm) {         /* (Dah) the MASTER: FX breathes, the rest dark */
        for (c = 0; c < FM1_NCOL; c++)
            nl[c] = nd[c] = nm[c] = nb[c] = 0;
        led_put(nb, panel.btn[B_FX], 1);
    }
    if (song.playing || (seq_counting() && cin_pos < click_beat_len() / 4u))   /* counting in: green on each beat */
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(mode == LEDS_DIM_LO || inv == 1u);   /* (INV LO: the darker mid level, ~8 %; HI ~12.5 %) */
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
/* Predictable hardware response (#23): each decoded detent is one value step; a fast turn keeps its full signed
 * detent count. MENU > KNOB ACCEL ON (#52, OFF by default) multiplies a fast turn of a wide value (range > 32, not a
 * list of names; the FX and GLO layers' knobs too, #126) by 2..4, by up to 8 over a range above 64. The main loop
 * reads the knobs many times a frame (main.c), so a read holds one detent as a rule: the speed is the time per
 * detent, ACC_RATE / ms -> 25 ms x2, 16 ms x3, 12 ms x4, 10 ms x5 .. 6 ms or less x8 (a flick: 12 detents in 120 ms
 * move a -100..100 FX macro ~50). Only the longer of this read's and the previous read's time counts, and only while
 * the turn goes on (both under ACC_GAP ms) in one direction: a slow turn, the first two detents of a turn, a single
 * quick detent (a bounce) and a reversal are one step per detent, and the sign is always the detents'.
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 its ms per detent (127 slow) */
#define ACC_GAP 40u
#define ACC_RATE 50u
/* the factor of this read of knob `role` (s detents), at most cap (1: slow); acc_rate itself, whatever KNOB ACCEL says:
 * FINE LEN's knob (ui_input.c step_edit), a tenth a detent turned slowly, a step fast */
static uint32_t acc_rate(uint32_t role, int32_t s, uint32_t cap)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (!s)
        return 1u;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        m = m < 1u ? 1u : m > cap ? cap : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return m;
}
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    if (!(ui_prefs & PREF_ACCEL) || range <= 32 || !s)
        return s;
    return s * (int32_t)acc_rate(role, s, range > 64 ? 8u : 4u);   /* (the cap) */
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 REV send, 4 MUTE of the selected track (right = ON, left = OFF: the
 * track itself is ALGORITHM's, on every page). Pattern length stays on SEQ. */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    if (mx_master) {                                    /* (Dah) the MASTER: its COMPRESSOR, AMOUNT and RELEASE */
        if (slot < 2u) {
            uint32_t g = slot ? G_MREL : G_MCOMP;
            song.g[g] = (int16_t)clamp(song.g[g] + accel(EN_K1 + slot, steps, 127), 0, 127);
        }
        return;
    }
    switch (slot) {
    case 3: {                                           /* (Dah) STATE: NORMAL, MUTE, SOLO (the others grey, their own MUTE
                                                         * kept: SOLO off, they are as they were) */
        uint32_t b = 1u << (song.sel % NTRK), st = (mix_solo & b) ? 2u : t->p[P_MUTE] ? 1u : 0u;
        st = (uint32_t)clamp((int32_t)st + (steps > 0 ? 1 : -1), 0, 2);
        t->p[P_MUTE] = (int16_t)(st == 1u);
        mix_solo = (uint8_t)(st == 2u ? mix_solo | b : mix_solo & ~b);
        ml_knob(t, P_MUTE);                             /* (MIDI LEARN: picked) */
        return;
    }
    case 0:
        vp = &t->p[P_LEVEL];
        d = &TP[P_LEVEL];
        break;
    case 2:                                             /* (Dah) WIDTH: the voices' stereo spread, as FL's STEREO
                                                         * SEPARATION: to the left separated, to the right merged */
        vp = &t->p[P_SPRD];
        d = &TP[P_SPRD];
        steps = -steps;
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    motion_capture(t, (uint32_t)(vp - t->p), *vp);
    ml_knob(t, (uint32_t)(vp - t->p));
}

/* SEQ > STEP's piano roll (not the DRUM grid, whose white keys stay its steps): where step recording writes */
static int roll_on(void)
{
    return !ui.home && !ui.menu && !ui.confirm && song.seq_mode && cur_page()->graph == GR_ROLL && !drum_track(TSEL);
}

/* SONG: + ADD selected (song_row == count < CHAIN_ROWS): a section there, copying the one before it (the first: every
 * track on A, x1) */
static void song_add(void)
{
    uint32_t n = chain_config.count;
    if (n >= CHAIN_ROWS)
        return;
    if (n)
        chain_config.row[n] = chain_config.row[n - 1u];
    else {                                              /* (the first: each track on the pattern it plays) */
        uint32_t k;
        chain_config.row[0] = chain_row_of(CHAIN_SILENT, 1);
        for (k = 0; k < trk_count(); k++)
            chain_config.row[0].slot[k] = (uint8_t)ps_num(k);
    }
    chain_config.count = (uint8_t)(n + 1u);
    ui.song_row = (uint8_t)n;
}
/* (Dah) LIVE's keys (song.grid 4: none sounds; MIDI IN plays on): black keys 1..8 pick tracks 1..8 (the project's), the
 * 9th the selected track's volume to 0 %, the 10th to 100 %, the 11th (the last) its MUTE on or off: at once stopped,
 * playing at its pattern's start (seq.c; pressed again before it: dropped) */
static void live_vol(int32_t v)
{
    v = clamp(v, 0, 100);
    live_cut[song.sel % NTRK] = (uint8_t)(100 - v);
}
static void live_keys(uint32_t notes)
{
    uint32_t k, p, i = song.sel % NTRK;
    for (k = 0; k < 27u; k++) {
        if (!((notes >> k) & 1u) || !key_black(k))
            continue;
        p = key_place(k);
        if (p < NTRK)
            track_select(p);
        else if (p == NTRK)
            live_vol(0);
        else if (p == NTRK + 1u)
            live_vol(100);
        else if (song.playing || chain_busy())
            live_mreq[i] = (uint8_t)!live_mreq[i];
        else
            TSEL->p[P_MUTE] = (int16_t)!TSEL->p[P_MUTE];
        ui.force = 1;
    }
}
/* SONG's keys (song.grid 3; Dah: key k pattern k + 1, the track's ones glowing, the cell's lit) set the selected cell's slot (on + ADD it
 * adds the section first: a copy of the one before, the first with every track on that slot) */
static void song_keys(uint32_t notes)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        int32_t s = (notes >> k) & 1u ? song_key(k) : -1;
        if (s < 0)
            continue;
        if (ui.song_trk < NTRK && !ps_exists(ui.song_trk, (uint32_t)s + 1u)) {   /* (Dah: a pattern the track has) */
            ui_message("NO SUCH PATTERN");
            continue;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (ui.song_row >= chain_config.count) {
            if (ui.song_row >= CHAIN_ROWS)
                return;
            song_add();
            if (chain_config.count == 1u)
                chain_config.row[0] = chain_row_of((uint32_t)s + 1u, 1);   /* (A..D's keys, patterns 1..4) */
        }
        if (ui.song_trk >= NTRK) {                    /* SEC: the key puts the slot on every track of the section */
            uint32_t k;
            for (k = 0; k < NTRK; k++)                  /* (Dah: the project's tracks; the others "-") */
                chain_config.row[ui.song_row].slot[k] = k < trk_count() ? (uint8_t)(s + 1) : (uint8_t)CHAIN_SILENT;
        } else {                                      /* (Dah: the cell's own again: out, "-") */
            uint8_t *sl = &chain_config.row[ui.song_row].slot[ui.song_trk];
            *sl = *sl == (uint8_t)(s + 1) ? (uint8_t)CHAIN_SILENT : (uint8_t)(s + 1);
        }
        ui.force = 1;
    }
}
/* the key LEDs of SONG: the key of the selected cell's slot (none for "-" and on + ADD) */
static uint32_t song_leds(void)
{
    uint32_t k, s;
    if (ui.song_trk >= NTRK || ui.song_row >= chain_config.count || !(s = chain_config.row[ui.song_row].slot[ui.song_trk]) ||
        s > 27u)                                        /* (Dah: SEC, every lane: none lit) */
        return 0;
    for (k = 0; k < 27u; k++)
        if (song_key(k) == (int32_t)s - 1)
            return 1u << k;
    return 0;
}

/* REC tap on every page: arm / disarm recording on the selected track without navigating; arming while stopped
 * starts the transport too (live recording), except on STEP's piano roll (1.2, Discussion #133): there it arms step
 * recording and the transport stays stopped: the keys write the cursor step (seq_entry), PLAY then records live.
 * Armed and playing, STEP's keys record live (at the play head) instead of writing the cursor step */
/* (Dah) ASSIGN, HOME's four knobs: OCT- and OCT+ together, then a knob turned on any page (its parameter: the source),
 * OCT+ (OK), then one of the four (HOME's knob that drives it from now on: the track's, trkx home). OCT+ before a
 * source: a HOME knob back to its engine's. OCT- leaves. The header says what is asked */
static int ka_chain_page(void) { return !ui.home && !ui.menu && cur_page()->id[0] == P_FXORD && cur_page()->fam == FAM_FX; }
static void ka_apply(uint32_t k);
static void ka_start(void)
{
    if (ui.home)                                        /* (Dah: not on HOME: its knobs are the ones assigned) */
        return;
    ka.dst = 0xFFu;
    ka.pg = ui.page;                                    /* (another page, HOME, a layer, the menu: it stops) */
    ka.st = 1;                                          /* (Dah: the panel says it, ui_graph.c graph_assign) */
    ui.force = 1;
}
static void ka_oct(uint32_t oct)
{
    if (oct & 1u) {
        ka.st = 0;
        ui_message("ASSIGN OFF");
    } else if (ka.st == 1)
        ka.st = 4;                                      /* (no source: a knob back to its engine's) */
    else if (ka.st == 2)
        ka.st = 3;
    else if (ka.st >= 3 && ka.dst < 4u) {              /* (Dah) the HOME knob shown in its new place: OCT+ makes it so */
        ka_apply(ka.dst);
        return;
    }
    ui.force = 1;                                       /* (Dah: the panel says what is asked) */
}
/* the parameter knob k edits here (HOME: its own), 0xFF none (an action, a list, a global value, the MIXER) */
static uint32_t ka_id_of(uint32_t k)
{
    int16_t *vp = 0;
    const param_desc_t *d;
    if (cur_page()->scope == SC_TRACK || cur_page()->scope == SC_ENGINE || cur_page()->scope == SC_GLOBAL)
        d = page_desc(cur_page(), k, &vp);              /* (a global page: its SEND only, below) */
    else
        return 0xFFu;
    return d && vp && vp >= TSEL->p && vp < TSEL->p + P_COUNT ? (uint32_t)(vp - TSEL->p) : 0xFFu;
}
static void ka_knob(uint32_t k)
{
    if (ka.st == 1 || ka.st == 2) {
        uint32_t id = ka_id_of(k);
        if (id == 0xFFu) {
            ui_message("NOT A SOUND KNOB");
            return;
        }
        ka.src = (uint8_t)id;
        ka.srck = (uint8_t)(k & 3u);
        ka.st = 2;
        ui.force = 1;
        return;
    }
    ka.dst = (uint8_t)(k & 3u);                         /* (Dah) the HOME knob picked: shown replaced, OCT+ to confirm */
    ui.force = 1;
}
static void ka_apply(uint32_t k)
{
    char m[24] = "HOME K";
    trkx[song.sel % NTRK].home[k & 3u] = (uint8_t)(ka.st == 3 ? ka.src + 1u : 0u);
    m[6] = (char)('1' + (k & 3u));
    m[7] = ':'; m[8] = ' ';
    if (ka.st == 3)
        str_cpy(m + 9, track_desc(TSEL, ka.src)->label, 8);
    else
        str_cpy(m + 9, "RESET", 6);
    ka.st = 0;
    ui_message(m);
    ui.force = 1;
}

static int rec_tap(void)                                /* 1: the arming changed */
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    uint32_t g = ui.home || ui.menu || ui.confirm || name_on() ? GR_NONE : cur_page()->graph;
    if (song.rec) {                                     /* (Dah) armed (this track or another): REC disarms them all, on
                                                         * every page; the LED goes out (a list page deletes when none is) */
        song.rec = 0;
        ui.force = 1;                                   /* (Dah: no message, the header's dot says it) */
        return 1;
    }
    if (g == GR_PATTERN) {                              /* on a list REC deletes: the dialog */
        if (chain_busy()) ui_message("STOP TO EDIT");
        else confirm_open(CF_DEL_PAT, ps_num(song.sel));
        return 0;
    }
    if (!ui.home && !ui.menu && !ui.confirm && cur_page()->id[0] == P_FXORD && cur_page()->fam == FAM_FX) {   /* (Dah)
                                                         * CHAIN: the effect picked out of the chain */
        uint8_t u[FXU_N_ALL];
        if (ch_cur < fx_list(fx_tsel(), u) && fx_ret_fixed(fx_tsel(), u[ch_cur]))
            ui_message("ALWAYS ON THE MASTER");         /* (Dah: its returns stay) */
        else if (ch_cur < fx_list(fx_tsel(), u)) {
            fx_chain_remove(fx_tsel());
            ui_message("EFFECT REMOVED");
            ui.force = 1;
        } else
            ui_message("NO EFFECT THERE");
        return 0;
    }
    if (g == GR_FILE) {
        if (pfs_there(ui.fsel)) confirm_open(CF_DEL_ARCH, ui.fsel);
        else ui_message("NO PROJECT");
        return 0;
    }
    if (g == GR_USER) {                                 /* USER: the preset picked */
        if (!up_used(ui.uslot)) ui_message("EMPTY SLOT");
        else if (transport_busy()) ui_message("STOP TO ERASE");
        else confirm_open(CF_ERASE_USER, ui.uslot);
        return 0;
    }
    if (g == GR_SONG) {                                 /* (Dah) SONG: REC the section picked out; OCT- held adds one
                                                         * before it (a copy of it), on + ADD at the end */
        if (chain_busy()) ui_message("STOP TO EDIT");
        else if (ui.song_row >= chain_config.count) ui_message("NO SECTION THERE");
        else {                                          /* (shown blinking: OCT+ deletes it) */
            sg_pend = 2;
            sg_prow = (uint8_t)ui.song_row;
            ui_message("DELETE? OCT+ YES");
        }
        ui.force = 1;
        return 0;
    }
    if (chain_busy()) {
        ui_message("STOP TO RECORD");
        return 0;
    }
    if (!ui.home && cur_page()->graph == GR_SONG && !(song.rec & bit)) {
        ui_message("[SEQ] TO RECORD");
        return 0;
    }
    song.rec ^= bit;
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    if ((song.rec & bit) && !song.playing && !seq_counting() && grid_on()) {
        if (help_on())                                  /* (Dah: what the keys do now: HELP's) */
            ui_message("KEYS WRITE STEPS");               /* (Dah: the DRUM grid: armed, the keys are the steps) */
        return 1;
    }
    if ((song.rec & bit) && !song.playing && !seq_counting() && roll_on()) {
        if (help_on())
            ui_message(ui_prefs2 & (PREF_CHORD_ADD >> 16) ? "KEYS ADD TO STEP" : "KEYS WRITE STEPS");   /* step recording:
                                                         * stopped, at the cursor (CHORD ENTRY ADD: adding) */
        return 1;
    }
    if ((song.rec & bit) && !song.playing && !seq_counting() && (ui_leds_inv & REC_PLAY))
        transport_req = 1;                              /* (Dah: REC's PLAY ON only; OFF: armed, PLAY starts it) */
    if (!help_on())
        ;
    else if ((song.rec & bit) && grid_on())
        ui_message("LANE KEYS RECORD");               /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && cur_page()->graph == GR_ROLL)
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
    return 1;
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void)
{
    return ((song.rec >> song.sel) & 1u) && (song.playing || transport_req == 1u || seq_counting());
}

static void factory_reset(void);                       /* (project.c) */
/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint8_t)trk;
    ui.act = 0;
    ui.force = 1;
}

/* MENU > CONTROL > STEP PREVIEW ON (1.2, Discussion #169): on SEQ > STEP (the roll and the DRUM grid) with the transport
 * stopped, a move of the cursor by a knob (KNOB 1, PRESETS) or the grid's page keys sounds the step it lands on once,
 * on the selected track (seq.c seq_audition). Not while playing, counting in or in a song; step recording moving on
 * after its keys (seq_entry) does not; a grid key on a step neither (it edits the step: a hit placed is heard, place_heard;
 * a lane key plays the lane).
 * OFF (the default): the cursor moves silently, as before */
static void cursor_move(int32_t c)
{
    cursor_set(c);
    follow_hold();                                      /* (1.6, #242: the roll stays where the cursor is) */
    if ((ui_prefs2 & (PREF_PREVIEW >> 16)) && !song.playing && !seq_counting() && transport_req != 1u && !chain_busy() &&
        !ui.home && song.seq_mode && cur_page()->graph == GR_ROLL) {
        if (ui.cursor >= NSTEP)                         /* (long patterns: the step, from its page) */
            aud_far = *ui_st(TSEL, ui.cursor);
        aud_req = (uint16_t)((uint32_t)ui.cursor << 8 | (song.sel + 1u));
    }
}

/* the grid's page down (-1) / up (+1): the cursor to the same place on it (at most the last step) */
static void page_go(int32_t d)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], pages = (len + 15u) / 16u, b;
    if (pages < 2u)
        return;
    b = (ui.bank + pages + (uint32_t)d) % pages;
    cursor_move((int32_t)(b * 16u + ui.cursor % 16u < len ? b * 16u + ui.cursor % 16u : len - 1u));
}

/* the grid's knobs: 1 STEP (the cursor), 2 LANE, 3 HIT and 4 ACC of the lane at the cursor (right on, left off) */
/* (Dah) a note or a hit placed on SEQ (a grid key's hit, step entry's note, the NOTE or HIT knob): heard once on the
 * selected track as PLAY plays it, stopped or playing (seq.c seq_preview: over the music, as a key). Not a removal */
static void place_heard(uint32_t note, uint32_t vel)
{
    pv_wvel = (uint8_t)vel;
    pv_want = (uint8_t)note;
    pv_req = (uint8_t)(song.sel + 1u);
}
/* a step's velocity as seq.c seq_step plays it: ACC 127, else its VEL (0: 96) */
static uint32_t st_vel(const step_t *st) { return st->flags & SF_ACCENT ? 127u : st->vel ? st->vel : 96u; }
/* .. lane l's hit at step i, if there: its lane's ACC 127 */
static void hit_heard(uint32_t i, uint32_t l)
{
    const step_t *st = ui_st(TSEL, i);
    if ((step_lanes(st) >> l) & 1u)
        place_heard(DRUM_LANE_NOTE[l], (step_accents(st) >> l) & 1u ? 127u : st_vel(st));
}
/* (Dah) the DRUM grid not armed (the keys play the kit): the lane of the last key played picked, the grid shows it
 * (its row lit while the key is held: ui_graph.c gd_held) */
static void key_lane(uint32_t pressed)
{
    uint32_t m = keys_lanes(TSEL, pressed), l = 0;
    if (!m)
        return;
    while (m >> l > 1u)                                 /* (several at once: the highest) */
        l++;
    ui.lane = (uint8_t)l;
}
static void grid_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u)
        cursor_move(ui.cursor + steps);
    else if (slot == 1u)
        ui.lane = (uint8_t)clamp((int32_t)ui.lane + (steps > 0 ? 1 : -1), 0, NLANE - 1);
    else if (slot == 2u)
        grid_hit(TSEL, ui.cursor, ui.lane, steps > 0);
    else
        grid_acc(TSEL, ui.cursor, ui.lane, steps > 0);
    if (slot >= 2u && steps > 0)                        /* (HIT, ACC on: heard) */
        hit_heard(ui.cursor, ui.lane);
}

/* the keys on the grid (presses): a white key toggles the selected lane at its step of the page (its accent
 * while ACC is held) and puts the cursor there; a lane key selects the lane (seq.c plays it); the page keys */
static void grid_keys(uint32_t pressed)
{
    uint32_t k, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        if (!((pressed >> k) & 1u))
            continue;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            if (chain_busy()) { ui_message("STOP TO EDIT"); continue; }
            if (i >= len)
                continue;                               /* past LEN: no step there */
            if (detail_on()) {                          /* DETAIL (1.5.1): the key picks the step, no hit toggled */
                cursor_set((int32_t)i);
                continue;
            }
            {   /* a hit (an accent) there goes when the key is let go, unless it was held for a lock (lock_keys) */
                const step_t *st = ui_st(TSEL, i);
                uint64_t b = i - ed_base() < NSTEP ? (uint64_t)1 << (i - ed_base()) : 0u;
                uint32_t acc = (uint32_t)black_held(GK_ACC);
                if (((acc ? step_accents(st) : step_lanes(st)) >> ui.lane) & 1u) {
                    ui.plk_rm |= b;
                    ui.plk_acc = acc ? ui.plk_acc | b : ui.plk_acc & ~b;
                    ui.plk_lane = ui.lane;
                    ui.plk_trk = song.sel;
                } else {
                    if (acc)
                        grid_acc(TSEL, i, ui.lane, 1);
                    else
                        grid_hit(TSEL, i, ui.lane, 1);
                    hit_heard(i, ui.lane);              /* (placed: heard, as PLAY plays it) */
                }
            }
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

/* ------------------------------------------------- parameter locks --- */
/* 1.1 (Discussions #75, #53): on SEQ > STEP, hold a step and turn KNOB 1..4: the step gets a lock of the parameter
 * that knob edits on HOME (Dah: HOME's four, whatever page was shown last; the
 * cards show those four while a step is held, a locked value bright, the sound's own DIM). The step held: on the
 * DRUM grid its white keys (several at once: each gets the lock; a press on a hit takes it away only when let go,
 * and not at all when it was held for a lock), on the roll the step being entered (its note keys held). [EDIT]
 * tapped meanwhile clears the held steps' locks (EDIT alone still clears the step, its locks with it). Not while a
 * song plays (STOP TO EDIT). A lock is a motion record (motion.c), a point on SEQ > AUTOMATION's lanes */
static uint64_t lock_held(void)
{
    const track_t *t = TSEL;
    uint32_t len = (uint32_t)t->p[P_SLEN], keys = fm1_in.notes & ~kb_layer;
    if (ui.home || ui.menu || ui.confirm || ui.layer || name_on() || !song.seq_mode || cur_page()->graph != GR_ROLL ||
        !keys || detail_on())                           /* (DETAIL: its knobs are the step's own fields, sd_turn) */
        return 0;
    if (!grid_on())                                     /* the roll: the step being entered */
        return ui.entry_open && !live_rec_sel() && ui.cursor < len ? (uint64_t)1 << (ui.cursor - ed_base()) : 0u;
    if (!grid_write())                                  /* (Dah: the grid not armed: its keys play, none is held) */
        return 0;
    return ed_keys(keys, len);
}

/* each pass, before the grid's keys: a hit pressed on goes once its key is let go (plk_rm, grid_keys) */
static void lock_keys(void)
{
    uint64_t gone = ui.plk_rm & ~lock_held();
    uint32_t i;
    for (i = 0; gone && i < NSTEP; i++)
        if ((gone >> i) & 1u) {
            if ((ui.plk_acc >> i) & 1u)
                grid_acc(&trk[ui.plk_trk % NTRK], ed_base() + i, ui.plk_lane, 0);
            else
                grid_hit(&trk[ui.plk_trk % NTRK], ed_base() + i, ui.plk_lane, 0);
        }
    ui.plk_rm &= ~gone;
}

/* KNOB k turned s with the steps `held` held: each gets (or changes) its lock of lock_id(k), from its lock or the
 * sound's own value */
static void lock_turn(uint32_t k, int32_t s, uint64_t held)
{
    track_t *t = TSEL;
    uint32_t id = lock_id(k), i;
    const param_desc_t *d;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (id == 0xFFu)
        return;                                         /* (an empty card: nothing to lock) */
    ui.plk_rm &= ~held;                                 /* (held for a lock: its hit stays) */
    d = track_desc(t, id);
    s = accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
    for (i = 0; i < NSTEP; i++) {
        int16_t v;
        if (!((held >> i) & 1u))
            continue;
        if (!motion_lock_get(t, ed_base() + i, id, &v))
            v = motion_base_value(t, id);
        if (motion_set_lock(t, ed_base() + i, id, (int16_t)param_turn(d, v, s)) == 2)
            break;                                      /* (full: ui_notices says so) */
    }
    ui.auto_id = (uint8_t)(id + 1u);                    /* (Dah: STEP's lane shows it now) */
    ui.auto_trk = (uint8_t)song.sel;
}
/* (Dah) SELECT on STEP's piano roll: the lane's parameter at the cursor step, as a lock there */
static int lane_turn(int32_t s)
{
    track_t *t = TSEL;
    uint32_t id = lane_id(t);
    const param_desc_t *d;
    int pt;
    if (id == 0xFFu)
        return 0;
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return 1;
    }
    d = track_desc(t, id);
    (void)motion_set_lock(t, ui.cursor, id,
                          (int16_t)param_turn(d, lane_value(t, ui.cursor, id, &pt), accel(EN_SELECT, s, d->max - d->min)));
    ui.auto_id = (uint8_t)(id + 1u);
    ui.auto_trk = (uint8_t)song.sel;
    return 1;
}

/* NUDGE (1.2, #162; core.h step_nudge): the steps `held` move by one sixteenth of a step (-8..+7) a detent: SEQ > STEP
 * with steps held (as for a lock: the grid's step keys, the roll's step being entered) and the PRESETS knob; let go,
 * PRESETS moves the cursor again (MENU > STEP PREVIEW: it sounds). SEQ > DETAIL's KNOB 3 is the other way. They play
 * with the track's QUANTIZE OFF (ON, the default, plays every step on its start: the message says so) */
static void nudge_turn(uint64_t held, int32_t steps)
{
    uint32_t i;
    int32_t v = 0;
    char b[16] = "NUDGE ";
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    ui.plk_rm &= ~held;                                 /* (a grid key held for it: its hit stays) */
    for (i = 0; i < NSTEP; i++)
        if ((held >> i) & 1u) {
            step_t *st = ed_st(TSEL, ed_base() + i);
            v = clamp(step_nudge(st) + (steps > 0 ? 1 : -1), -8, 7);
            step_set_nudge(st, v);
        }
    if (TSEL->p[P_SQNT]) {
        ui_message("QUANTIZE IS ON");
        return;
    }
    ev_nudge_fmt(b + 6, v);
    ui_message(b);
}

/* SEQ > DETAIL (1.5.1, Discussion #199; ui.c detail_on): KNOB 1 CHANCE (0..100 %, 1.5.5: then the conditions 1:2 ..
 * 4:4, FILL, !FILL, ui_events.c ev_chance_turn), 2 RATCH (x1..x4), 3 NUDGE (-8..+7
 * sixteenths), 4 VEL (1..127) of the cursor step, the step's own fields (ui_events.c ev_sval,
 * ev_sput: one storage, nothing new stored). On the DRUM grid the white
 * keys held pick several steps: each moves by the turn. A step that plays nothing takes none (sd_takes: NO NOTE) */
static uint64_t sd_held(void)                           /* the grid's step keys held on DETAIL (bit = step), 0 none */
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], keys = fm1_in.notes & ~kb_layer;
    if (!keys || !detail_on() || !grid_on() || ui.layer || name_on())
        return 0;
    return ed_keys(keys, len);
}
static uint64_t sd_steps(void)
{
    uint64_t m = sd_held();
    return m ? m : (uint64_t)1 << (ui.cursor - ed_base());
}
static void sd_turn(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t k = SD_KIND[slot & 3u], i, n = 0;
    uint64_t m = sd_steps();
    int32_t d = k == EVK_CHANCE ? accel(EN_K1 + slot, steps, 100) : k == EVK_VEL ? accel(EN_K1 + slot, steps, 126) : steps;
    for (i = 0; i < NSTEP; i++)
        n += ((m >> i) & 1u) && sd_takes(ui_st(t, ed_base() + i), k);
    if (!n) {
        ui_message("NO NOTE");
        return;
    }
    for (i = 0; i < NSTEP; i++) {
        int32_t v;
        step_t *st;
        if (!((m >> i) & 1u) || !sd_takes(ui_st(t, ed_base() + i), k))
            continue;
        st = ed_st(t, ed_base() + i);
        v = ev_sval(st, k);
        v = k == EVK_CHANCE ? ev_chance_turn(v, steps, d) : k == EVK_RATCH ? clamp(v + d, 1, 4) :
            k == EVK_VEL ? clamp(v + d, 1, 127) : clamp(v + d, -8, 7);
        ev_sput(st, k, v);
    }
    if (k == EVK_NUDGE && t->p[P_SQNT])
        ui_message("QUANTIZE IS ON");                   /* (kept: it plays once QUANTIZE is OFF) */
    else if (k == EVK_VEL && (ui_st(t, ui.cursor)->flags & SF_ACCENT))
        ui_message("ACC PLAYS 127");                    /* (kept: it plays once the ACC goes) */
}
/* DETAIL's EDIT: the steps' four back to 100 % / x1 / 0 / 96 (their notes stay) */
static void sd_reset(void)
{
    track_t *t = TSEL;
    uint64_t m = sd_steps();
    uint32_t i, k, n = 0;
    for (i = 0; i < NSTEP; i++)
        for (k = 0; k < 4u; k++)
            n += ((m >> i) & 1u) && ev_sval(ui_st(t, ed_base() + i), SD_KIND[k]) != ev_sdefv(SD_KIND[k]);
    if (!n) {
        ui_message("NOTHING TO RESET");
        return;
    }
    for (i = 0; i < NSTEP; i++)
        for (k = 0; (m >> i) & 1u && k < 4u; k++)
            ev_sput(ed_st(t, ed_base() + i), SD_KIND[k], ev_sdefv(SD_KIND[k]));
    ui_message("STEP RESET");
}

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = ed_st(TSEL, ui.cursor);
    uint32_t i;
    if (detail_on()) {                                    /* DETAIL (1.5.1): CHANCE RATCH NUDGE VEL */
        follow_hold();
        sd_turn(slot, steps);
        return;
    }
    if (drum_track(TSEL)) {
        grid_edit(slot, steps);
        return;
    }
    if (slot)
        follow_hold();                                    /* (1.6, #242: editing the cursor's step: it stays on the screen) */
    switch (slot) {
    case 0:                                               /* STEP: the cursor */
        cursor_move(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            place_heard(last_note, st_vel(st));
            break;
        }
        {                                                 /* (overlaps) the notes starting there (none: all,
                                                           * struck there now), each carried on after it as it moves */
            uint32_t c = st_cont(st), all = c == (1u << st->n) - 1u, was;
            for (i = 0; i < st->n; i++) {
                if (!all && ((c >> i) & 1u))
                    continue;
                was = st->note[i];
                st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
                (void)note_run(TSEL, ui.cursor, was, st->note[i]);   /* (overlaps: carried on as it moves) */
                st = ed_st(TSEL, ui.cursor);
            }
            if (st_kind(st) != ST_NOTE || all)
                st->time = ST_NOTE;
            last_note = st->note[0];
        }
        place_heard(st->note[0], st_vel(st));            /* (the new note heard) */
        break;
    case 2:                                               /* TIME; a note's LEN (1.5: ui.c note_set_len; FINE LEN:
                                                           * turned slowly a tenth a detent, fast a whole step) */
        if (step_on(st)) {
            uint32_t was = note_len10(TSEL, ui.cursor), n;
            int32_t d = acc_rate(EN_K1 + slot, steps, 4) > 1u ? steps * 10 : steps;
            n = note_set_len10(TSEL, ui.cursor, (uint32_t)clamp((int32_t)was + d, 1, LEN_MAX * 10));
            if (steps > 0 && n <= was)
                ui_message(ui.cursor + (n + 9u) / 10u < (uint32_t)clamp(TSEL->p[P_SLEN], 1, LEN_MAX) ? "NOTE AHEAD" : "END OF PATTERN");
            break;
        }
        st->time = (uint8_t)clamp((int32_t)st_kind(st) + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

/* (Dah) SONG's VIEW (OCT+ held): KNOB 1 GRID (where PLAY starts), 2 ZOOM, 3 WINDOW, 4 FOLLOW */
static void song_view_knob(uint32_t slot, int32_t steps)
{
    {
        if (slot == 0u) {                               /* GRID: a bar a detent; the view on it */
            song_lay_t L;
            uint32_t r = 0;
            int32_t g;
            song_layout(&L);
            if (chain.running && !chain_seek_req && song_playu(&L) >= 0)   /* (Dah: playing: from the bar playing) */
                sgw.gu = (uint32_t)song_playu(&L) - (uint32_t)song_playu(&L) % SG_BARU;
            g = (int32_t)sgw.gu + steps * (int32_t)SG_BARU;
            g = clamp(g, 0, L.u0[L.n] ? (int32_t)L.u0[L.n] - 1 : 0);
            sgw.gu = (uint32_t)(g - g % (int32_t)SG_BARU);
            while (r + 1u < L.n && sgw.gu >= L.u0[r + 1u])
                r++;
            chain_from_row = (uint8_t)r;                /* (song_chain.c: PLAY from there) */
            chain_from_u = sgw.gu - L.u0[r];
            sgw.at = song_ux(&L, sgw.gu) - SG_W / 2;
            if (sgw.at < 0)
                sgw.at = 0;
            sgw.nofollow = !chain.running;              /* (stopped: the view on the GRID) */
            if (chain.running && !chain_seek_req) {     /* (Dah) playing: the song jumps there (stopped, from it) */
                chain_seek_req = 1;
                transport_req = 2;
            }
        } else if (slot == 2u) {                        /* WINDOW: the view, a bar a detent (FOLLOW off) */
            sgw.at = (sgw.nofollow ? sgw.at : sgv.off) + steps * (int32_t)sg_zoom();
            if (sgw.at < 0)
                sgw.at = 0;
            sgw.nofollow = 1;
        } else if (slot == 1u) {
            int32_t z = (int32_t)sg_zoom() + (steps > 0 ? 1 : -1) * ((int32_t)sg_zoom() / 4 > 1 ? (int32_t)sg_zoom() / 4 : 1);
            int32_t bar = (sgv.off + SG_W / 2) / (int32_t)sg_zoom();   /* (the middle stays where it is) */
            sgw.zoom = (uint8_t)clamp(z, 2, 96);
            sgw.at = bar * (int32_t)sg_zoom() - SG_W / 2;
            if (sgw.at < 0)
                sgw.at = 0;
        } else
            sgw.nofollow = steps < 0;                   /* FOLLOW */
        ui.force = 1;
    }
}
/* (Dah) STEP's VIEW (OCT+ held, a melodic track): KNOB 1 MOVE (a quarter of the view a detent), 2 ZOOM (8 16 32 64
 * steps), 4 FOLLOW */
static void roll_view_knob(uint32_t slot, int32_t steps)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], n = prv_cols();
    if (slot == 0u) {
        int32_t a = (int32_t)prv_base(TSEL) + (steps > 0 ? 1 : -1) * (int32_t)(n / 4u);
        prv.at = (int16_t)clamp(a, 0, len > n ? (int32_t)(len - n) : 0);
        prv.nofollow = 1;
    } else if (slot == 1u) {
        uint32_t mid = prv_base(TSEL) + n / 2u;
        prv.zoom = (uint8_t)clamp((int32_t)prv.zoom + (steps > 0 ? -1 : 1), 0, 3);   /* (right: closer) */
        n = prv_cols();
        prv.at = (int16_t)(mid > n / 2u ? mid - n / 2u : 0u);
    } else if (slot == 3u)
        prv.nofollow = steps < 0;
    ui.force = 1;
}
/* (Dah) the previous, the next pattern (every track; past the last a new one), the patterns' list over the panel ~1 s */
static void pattern_turn_ui(int32_t s)
{
    uint32_t was = ps_num(song.sel);
    if (chain_busy()) {
        ui_message("STOP THE SONG");
        return;
    }
    {                                                   /* (Dah: every detent a pattern; past the last: asked once) */
        int32_t k;
        for (k = s > 0 ? s : -s; k-- > 0 && !ui.confirm;)
            pattern_turn(s > 0 ? 1 : -1);
    }
    tl_ms = fm1_ms | 1u;
    tl_pat = 1;
    if (ps_num(song.sel) != was)
        tl_dy = clamp(tl_dy + (s > 0 ? 17 : -17), -68, 68);
}
static void home_view_knob(uint32_t slot, int32_t steps)   /* (Dah) HOME's VIEW: the same, on the tracks' rack */
{
    uint32_t len = 1, n = hv_cols(), i;
    for (i = 0; i < trk_count(); i++)                   /* (MOVE: as far as the longest track) */
        if ((uint32_t)clamp(trk[i].p[P_SLEN], 1, LEN_MAX) > len)
            len = (uint32_t)clamp(trk[i].p[P_SLEN], 1, LEN_MAX);
    if (slot == 0u) {
        int32_t a = (int32_t)hv_base(TSEL, 1) + (steps > 0 ? 1 : -1) * (int32_t)(n / 2u > 4u ? n / 2u : 4u);
        hvw.at = (int16_t)clamp(a, 0, len > n ? (int32_t)(len - n) : 0);
        hvw.nofollow = 1;
    } else if (slot == 1u) {
        uint32_t mid = hv_base(TSEL, 1) + hv_cols() / 2u;
        hvw.zoom = (uint8_t)clamp((int32_t)hvw.zoom + (steps > 0 ? -1 : 1), 0, 3);   /* (right: closer) */
        n = hv_cols();
        hvw.at = (int16_t)(mid / n * n);
    } else if (slot == 2u) {                            /* (Dah) KNOB 3: the selected track's own pattern, "-" none */
        uint8_t nums[PS_NUM];
        uint32_t k = song.sel % NTRK, cnt = ps_list_all(nums);
        int32_t at = -1;
        if (chain_busy()) {
            ui_message("STOP THE SONG");
            return;
        }
        if (!((seq_off >> k) & 1u))
            for (i = 0; i < cnt; i++)
                if (nums[i] == ps_num(k)) at = (int32_t)i;
        at = clamp(at + (steps > 0 ? 1 : -1), -1, (int32_t)cnt - 1);
        if (at < 0)
            seq_off |= (uint8_t)(1u << k);
        else {
            seq_off &= (uint8_t)~(1u << k);
            if (pattern_go(k, nums[at]))
                ui_message("MEMORY FULL");
        }
    } else if (slot == 3u)
        hvw.nofollow = steps < 0;
    ui.force = 1;
}
static int home_view_page(void) { return ui.home && !ui.menu && !ui.confirm && home_tracks(); }
static int roll_view_page(void)
{
    return !ui.home && !ui.menu && !ui.confirm && ((cur_page()->graph == GR_ROLL && !drum_track(TSEL) && !detail_on()) ||
                                                   cur_page()->graph == GR_EVENTS);   /* (Dah: AUTOMATION, the roll's VIEW) */
}
static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->graph == GR_EVENTS) {                         /* AUTOMATION (ui_events.c): lane slot's point at the cursor */
        au_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG && sgw.on) {             /* (Dah) VIEW: GRID (where PLAY starts), ZOOM, WINDOW, FOLLOW */
        song_view_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG && sg_pend) {            /* (Dah: a section pending: moved away, dropped) */
        sg_pend = 0;
        ui.force = 1;
    }
    if (pg->graph == GR_SONG) {                       /* KNOB 1 SECTION, 2 TRACK, 3 PAT (A..D, -), 4 REPS */
        chain_row_t *r;
        sgw.touch = ui.frame;                           /* (Dah: the view on the section picked, a while) */
        if (slot == 0u) {
            ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + steps, 0,
                chain_config.count < CHAIN_ROWS ? chain_config.count : CHAIN_ROWS - 1u);
            if (ui.song_row < chain_config.count) {     /* (Dah) the GRID (where PLAY, RESTART start) on it: the last
                                                         * of the two moved wins (playing: the next PLAY's) */
                chain_from_row = ui.song_row;
                chain_from_u = 0;
            }
            return;
        }
        if (slot == 1u) {
            int32_t l = ui.song_trk >= trk_count() ? (int32_t)trk_count() : ui.song_trk;   /* (the project's
                                                       * tracks, then SEC: NTRK) */
            l = clamp(l + steps, 0, (int32_t)trk_count());
            ui.song_trk = (uint8_t)(l >= (int32_t)trk_count() ? NTRK : l);
            return;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (ui.song_row >= chain_config.count) {     /* + ADD: a turn adds the section (KNOB 3: only that) */
            song_add();
            if (slot == 2u) return;
        }
        r = &chain_config.row[ui.song_row];
        if (slot == 2u && ui.song_trk >= NTRK)            /* SEC: KNOB 3 the section's CUT (right ON, left OFF) */
            r->repeat = (uint8_t)(steps > 0 ? r->repeat | CHAIN_CUT : r->repeat & ~CHAIN_CUT);
        else if (slot == 2u) {                            /* "-", then the track's patterns */
            uint8_t nums[PS_NUM];
            uint32_t k = ui.song_trk % NTRK, n = ps_list(k, nums), i;
            int32_t at = -1;
            for (i = 0; i < n; i++)
                if (nums[i] == r->slot[k]) at = (int32_t)i;
            at = clamp(at + steps, -1, (int32_t)n - 1);
            r->slot[k] = at < 0 ? (uint8_t)CHAIN_SILENT : nums[at];
        }
        else
            r->repeat = (uint8_t)((r->repeat & CHAIN_CUT) | (uint32_t)clamp((int32_t)CHAIN_REP(*r) + steps, 1, 16));
        return;
    }
    if (chain_busy() && (pg->scope == SC_STEP || pg->graph == GR_STEPS ||
        0)) {
        ui_message("STOP TO EDIT"); return;
    }
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
#if FELUCCA_SLICE
    if (pg->graph == GR_SLICES && slot < 2u) {           /* SLICES: KNOB 1 the marker, 2 moves it (ui_slice.c) */
        if (slice_page_ok())                              /* (the engine changed before ui_draw left the page) */
            slice_knob(slot, steps);
        return;
    }
#endif
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;   /* OCT+ does it (act_do) */
        return;
    }
    if (pg->graph == GR_PATTERN || pg->graph == GR_FILE) {   /* (Dah) KNOB 1 the pattern (every track on it), the
                                                         * project (the list glides) */
        if (slot == 0u)
            pg->graph == GR_PATTERN ? pt_move(steps) : file_move(steps);
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u) {
            uint32_t was = ui.uslot;
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            if (ui.uslot != was)                        /* (Dah: the slots glide to it) */
                lu_dy = clamp(lu_dy + (ui.uslot > was ? 13 : -13), -52, 52);   /* (26 px a row: graph_user) */
        }
        return;
    }
    if (fop_page(pg->graph)) {                           /* FM6's operators: KNOB 1 OP, 2..4 its bytes (ui_fm6op.c) */
        fop_knob(slot, steps);
        return;
    }
    if (dln_page(pg->graph)) {                           /* DRUM's lanes: KNOB 1 LANE, 2..4 its values (ui_drumlane.c) */
        dln_knob(slot, steps);
        return;
    }
    if (xp_page(pg->graph)) {                            /* LFO DRAW, CURVE (ui_lfodraw.c) */
        xp_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_CHORD && TSEL->p[P_CHRD] == CH_MATCH && slot >= 2u) {   /* (Dah) MATCH: KNOB 3 COLOR, 4 NOTES */
        trkx_t *x = &trkx[song.sel % NTRK];
        uint32_t v = slot == 2u ? x->rsv & 3u : (x->rsv >> 2) & 3u;
        v = (uint32_t)clamp((int32_t)v + (steps > 0 ? 1 : -1), 0, 3);
        x->rsv = (uint8_t)(slot == 2u ? (x->rsv & ~3u) | v : (x->rsv & ~0xCu) | v << 2);
        ui.force = 1;
        return;
    }
    if (pg->fam == FAM_FX && str_eq(pg->title, "DIST") && slot == 0u) {   /* (Dah) DIST: KNOB 1 its IN gain */
        track_t *ft = fx_tsel();
        int16_t *mp = fx_mst(ft) ? &mtrk.p[P_PAN] : 0;   /* (the MASTER's: in its chain, mtrk's P_PAN) */
        int32_t v = (mp ? *mp : trkx[song.sel % NTRK].din) + accel(EN_K1, steps, 127);
        v = clamp(v, -64, 63);
        if (mp) *mp = (int16_t)v; else trkx[song.sel % NTRK].din = (int8_t)v;
        ui.force = 1;
        return;
    }
    if (pg->fam == FAM_FX && pg->id[0] == P_DKSRC && slot == 3u && !mx_master) {   /* (Dah) SIDECHAIN: KNOB 4 a
                                                         * second source (OFF, the other tracks) */
        trkx_t *x = &trkx[song.sel % NTRK];
        int32_t v = x->sc2;
        do
            v = clamp(v + (steps > 0 ? 1 : -1), 0, (int32_t)trk_count());
        while (v && v == (int32_t)song.sel + 1 && v < (int32_t)trk_count() && steps > 0);
        if (v == (int32_t)song.sel + 1)                 /* (not itself) */
            v = steps > 0 ? x->sc2 : v - 1;
        x->sc2 = (uint8_t)v;
        ui.force = 1;
        return;
    }
    if (pg->fam == FAM_FX && str_eq(pg->title, "COMP")) {   /* (Dah) COMP: KNOB 1 AMOUNT, 2 RELEASE (trkx; the MASTER's: GLO) */
        const track_t *ft = fx_tsel();
        if (slot == 0u)
            fx_cset(ft, clamp((int32_t)fx_camt(ft) + accel(EN_K1, steps, 126), 1, 127), -1);   /* (1: it stays in) */
        else if (slot == 1u)
            fx_cset(ft, -1, clamp((int32_t)fx_crel(ft) + accel(EN_K2, steps, 127), 0, 127));
        else                                            /* IN, OUT */
            fx_cput(ft, slot - 2u, fx_cget(ft, slot - 2u) + accel(EN_K1 + slot, steps, 127));
        ui.force = 1;
        return;
    }
    if (eq_page(pg) && eq_knob(slot, steps))           /* (Dah) EQ: the band, its TYPE, a cut's SLOPE */
        return;
    if (pg->id[0] == P_FXORD && pg->scope == SC_TRACK) {   /* (Dah) CHAIN: KNOB 1 moves the effect, 2 its TYPE, 3 its
                                                         * MIX (the effect: SELECT's), 4 nothing */
        uint8_t u[FXU_N_ALL];
        track_t *ft = fx_tsel();
        uint32_t n = fx_list(ft, u);
        if (slot == 0u)                                 /* (Dah) KNOB 1 the effect, in the list (no window) */
            fl_fx_move(steps > 0 ? 1 : -1);
        else if (slot == 2u)
            fx_chain_type(ft, steps);
        else if (slot == 1u)
            fx_chain_move(ft, steps);
        else if (ch_cur < n && u[ch_cur] == FXU_COMP)   /* (COMP's MIX: its AMOUNT) */
            fx_cset(ft, clamp((int32_t)fx_camt(ft) + steps, 1, 127), -1);
        else if (ch_cur < n && FXU_MIX[u[ch_cur]] != 0xFFu) {   /* MIX: the effect's presence (it stays in) */
            uint32_t id = FXU_MIX[u[ch_cur]];
            const param_desc_t *md = track_desc(ft, id);
            int32_t w = param_turn(md, ft->p[id], accel(EN_K3, steps, md->max - md->min));
            ft->p[id] = (int16_t)(w < 1 && id != P_DOUT ? 1 : w);
            if (!fx_mst(ft))
                motion_capture(ft, id, ft->p[id]);
        }
        ui.force = 1;
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    if (pg->fam == FAM_FX && v <= 0 &&
        (vp == &fx_tsel()->p[P_SLCR] || vp == &fx_tsel()->p[P_ITYPE] || vp == &fx_tsel()->p[P_DIST] ||
         vp == &fx_tsel()->p[P_DKSRC] || vp == &fx_tsel()->p[P_IMIX] || vp == &fx_tsel()->p[P_CHOR] ||
         vp == &fx_tsel()->p[P_DLY] || vp == &fx_tsel()->p[P_REV])) {                     /* (Dah) an effect's own page: it stays in the chain (CHAIN's
                                                         * REC takes it out), its type or amount at its least */
        v = 1;
        ui_message("REMOVE IT IN CHAIN");
    }
    *vp = (int16_t)v;
    if (vp >= TSEL->p && vp < TSEL->p + P_COUNT) {      /* (Dah: a track's value, a send on a global page too) */
        if (motion_param((uint32_t)(vp - TSEL->p))) {   /* (Dah: STEP's automation lane shows the last one turned) */
            ui.auto_id = (uint8_t)((uint32_t)(vp - TSEL->p) + 1u);
            ui.auto_trk = (uint8_t)song.sel;
        }
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        ml_knob(TSEL, (uint32_t)(vp - TSEL->p));        /* (MIDI LEARN: picked) */
    }
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col();
    if (!c--)
        return;
    if (cur_page()->graph == GR_USER && !act_ready())   /* (Dah) MY PRESETS: what cannot be done (LOAD, DELETE on an
                                                         * empty slot) greyed, OCT+ nothing */
        return;
    if (cur_page()->graph == GR_SONG) {                 /* (Dah) a section to add or to delete: done; else the VIEW is
                                                         * OCT+ held (ui_input) */
        uint32_t r = sg_prow, n = chain_config.count;
        if (sg_pend == 1u && !chain_busy() && n < CHAIN_ROWS) {
            if (r >= n)
                song_add();
            else {
                for (; n > r; n--)
                    chain_config.row[n] = chain_config.row[n - 1u];
                chain_config.count++;
            }
            ui_message("SECTION ADDED");
        } else if (sg_pend == 2u && !chain_busy() && r < n) {
            for (; r + 1u < chain_config.count; r++)
                chain_config.row[r] = chain_config.row[r + 1u];
            chain_config.count--;
            if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
            ui_message("SECTION DELETED");
        } else if (!sg_pend)
            ui_message("HOLD OCT+: VIEW");
        sg_pend = 0;
        ui.force = 1;
        return;
    }
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        if (c == 2u && up_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* DELETE: the dialog first */
        else if (c == 1u && up_used(ui.uslot))
            confirm_open(CF_LOAD_USER, ui.uslot);         /* (Dah) LOAD too: the track's sound goes */
        else if (c != 3u)
            up_ui(c - 1u, ui.uslot);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (up_used(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    if (cur_page()->graph == GR_PATTERN) {              /* (Dah) PATTERN: COPY, NEW, DELETE (the dialog) */
        ui.act = 0;
        if (chain_busy())                               /* (the song owns the tracks' timing meanwhile) */
            ui_message("STOP TO EDIT");
        else if (c == 3u)
            confirm_open(CF_DEL_PAT, ps_num(song.sel));
        else if (c == 2u && !ps_free_all())             /* (Dah) NEW: asked, then what it starts from (CF_NEW_FROM) */
            ui_message("32 PATTERNS AT MOST");
        else if (c == 2u)
            confirm_open(CF_NEW_PAT, ps_free_all() | 0x80u);
        else if (c)
            pt_do(c + 1u);
        return;
    }
    if (cur_page()->graph == GR_FILE) {                 /* FILE: OPEN, COPY (its name), NEW; the one open is
                                                         * saved first, so nothing is lost: no dialog */
        ui.act = 0;
        if (transport_busy() || chain_busy())
            ui_message("STOP FIRST");
        else if (c == 1u)                               /* (KNOB 2 OPEN, 3 COPY, 4 NEW; DELETE: OCT- held) */
            (void)project_open(ui.fsel);
        else if (c == 2u)
            name_open(NK_ARCH_SAVE, 0);
        else if (!project_new_file())
            file_enter();
        return;
    }
}

/* OCT- / OCT+ where they answer (the dialogs, the menu, action pages): on release, and only a press
 * that began there; both down together (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint8_t oct_up_used;                             /* (Dah) OCT+ held with a knob turned (the VIEW): no tap */
static uint8_t oct_dn_used;                             /* (Dah) OCT- held for an action (SONG: a section out): no tap */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord, where;                  /* (Dah) where: pressed where they answer, a bit each */
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap, ch = 0;
    uint32_t p = ((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1;
    down |= (uint8_t)p;
    where = (uint8_t)(here ? where | p : where & ~p);
    if (now == 3u && !chord)
        ch = 4u;                                        /* (Dah) both down: once, the knob ASSIGN */
    if (now == 3u)
        chord = 1;
    tap = down & ~now & 3u;
    if (oct_up_used && (tap & 2u))
        tap &= ~2u;
    if (oct_dn_used && (tap & 1u))
        tap &= ~1u;
    if (!(now & 2u))
        oct_up_used = 0;
    if (!(now & 1u))
        oct_dn_used = 0;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    /* bits 0, 1: a tap pressed where they answer (and let go there: here); bits 4, 5: pressed elsewhere (Dah: the
     * octave, on its release); bit 2: ASSIGN */
    return ((tap & where) & (here ? 3u : 0u)) | (tap & ~where & 3u) << 4 | ch;
}

/* SEQ step entry = step recording (1.2, Discussion #133: only with the track armed and the transport stopped), acid
 * style, into the cursor step (the cursor stays: KNOB 1 / PRESETS move it; EDIT clears the step and moves on). (Dah) The
 * keys of one gesture (from the first key down to the last one up) decide what the step is:
 *   one key alone (one note: CHRD off, MONO, a kit) on a step holding that note alone: the note goes out (the step a
 *     REST, its tail with it), the key writes nothing more;
 *   one key on a step holding other notes: they are replaced by its note (theirs go with their tails, st_cut);
 *   on an empty step, a REST, a TIE: its note written;
 *   two keys or more held together (or a CHRD key's chord): the step is exactly their notes (POLY, up to 4; MONO the
 *     last key's), never a removal: E taken out by its key, G pressed while E is still held: E and G.
 * With CHRD on a key writes what it sounds, as live recording does: POLY its chord, MONO the chord's root. (CHORD ENTRY
 * ADD no longer changes step recording)
 * (Dah) Inside notes going on (POLY: the cursor step carries notes sounding from a step before, a TIE or a NOTE step
 * with some going on, core.h st_cont), the chord changes there, a note at a time: each note of the gesture (each key,
 * each note of a CHRD key's chord, several keys together each by itself, a gesture mixing both doing both) toggles:
 *   a note going on there: it ends at the step before (from here on out, its TIEs made explicit without it: entry_end),
 *     the others going on;
 *   a note starting there: out, with its tail (st_cut);
 *   a new note: it starts there beside them and lasts as long as the notes it joins (to where the longest of them
 *     ended when the gesture began: ent.run), each going on its own (entry_add); the step full: "4 NOTES MAX" */
static struct {
    uint8_t keys, n, note[4];
    uint8_t cn, cx[4];                                  /* the notes going on into the step */
    uint16_t run;                                       /* .. the longest one's steps from it */
    step_t orig;                                        /* the step as the gesture found it */
} ent __attribute__((section(".pool")));
/* the notes the cursor step carries on from the step before (POLY), into x (their number) */
static uint32_t entry_carried(track_t *t, const step_t *o, uint8_t *x)
{
    uint8_t q[4];
    uint32_t qn, n = 0, k, j, c;
    if (!ui.cursor || trk_vmode(t) != V_POLY || drum_track(t) || st_kind(o) == ST_REST)
        return 0;
    qn = pat_at(t, ui.cursor - 1u, q);
    for (k = 0, c = st_kind(o) == ST_TIE ? 15u : st_cont(o); k < (st_kind(o) == ST_TIE ? qn : o->n) && k < 4u; k++) {
        uint32_t y = st_kind(o) == ST_TIE ? q[k] : o->note[k];
        for (j = 0; ((c >> k) & 1u) && j < qn && q[j] != y; j++)
            ;
        if (((c >> k) & 1u) && j < qn)
            x[n++] = (uint8_t)y;
    }
    return n;
}
/* note x going on into step i ends at the step before: every step carrying it on from i out without it */
static void entry_end(track_t *t, uint32_t i, uint32_t x)
{
    uint8_t q[4];
    uint32_t qn = pat_at(t, i - 1u, q), n = note_run(t, i, x, x), j;
    for (j = i; j < i + n; j++)
        (void)pat_cont(t, j, x, 0, q, &qn);
}
/* note x starts at step i beside the notes going on there, for l steps (each going on); 0: the step full */
static int entry_add(track_t *t, uint32_t i, uint32_t x, uint32_t l)
{
    uint8_t q[4], po[4], w[4];
    uint32_t len = ui_len(t), qn = pat_at(t, i - 1u, q), np, j, c;
    step_t *s = ed_st(t, i);
    np = pat_after(s, q, qn, po);                       /* (what the TIE after it carries on without x) */
    st_explicit(s, q, qn);
    if (s->n >= 4u) {
        st_canon(s, q, qn);
        return 0;
    }
    c = st_cont(s);
    s->note[s->n++] = (uint8_t)x;
    st_set_cont(s, c);                                  /* (x starts here) */
    memcpy(w, q, 4);
    qn = pat_after(s, w, qn, q);
    for (j = i + 1u; j < i + l && j < len; j++) {
        const step_t *u = ui_st(t, j);
        if (u->hit || (st_kind(u) == ST_REST && u->n) || !pat_cont(t, j, x, 1, q, &qn))
            break;
    }
    if (j == i + 1u && j < len)
        st_hold_off(ed_st(t, j), po, np);
    return 1;
}
static void entry_inside(track_t *t)
{
    uint32_t i = ui.cursor, len = ui_len(t), k, j, c, full = 0;
    *ed_st(t, i) = ent.orig;                            /* (the gesture's earlier keys undone at the step; after it
                                                         * each note's edit is the same again) */
    for (k = 0; k < ent.n; k++) {
        const step_t *s = ui_st(t, i);
        for (j = 0; j < ent.cn && ent.cx[j] != ent.note[k]; j++)
            ;
        if (j < ent.cn) {
            entry_end(t, i, ent.note[k]);
            continue;
        }
        for (j = 0; j < s->n && j < 4u && s->note[j] != ent.note[k]; j++)
            ;
        if (j < s->n && j < 4u && st_kind(s) == ST_NOTE) {
            uint8_t q[4];
            uint32_t qn;
            st_cut(t, ed_st, i, len, 1u << j);
            qn = pat_at(t, i, q);
            if (i + 1u < len)                           /* (the TIE st_cut made explicit: a TIE again, as it was) */
                st_canon(ed_st(t, i + 1u), q, qn);
        }
    }
    for (k = 0; k < ent.n; k++) {
        for (j = 0; j < ent.cn && ent.cx[j] != ent.note[k]; j++)
            ;
        for (c = 0; j == ent.cn && c < ent.orig.n && c < 4u && ent.orig.note[c] != ent.note[k]; c++)
            ;
        if (j == ent.cn && (st_kind(&ent.orig) != ST_NOTE || c == ent.orig.n || c == 4u) &&
            !entry_add(t, i, ent.note[k], ent.run))
            full = 1;                                   /* (a new note: not one going on, nor one starting here) */
    }
    if (full)
        ui_message("4 NOTES MAX");
}
static void entry_write(track_t *t, int single)
{
    step_t *st = ed_st(t, ui.cursor);
    const step_t *o = &ent.orig;
    uint32_t m = st_starts(o), k, j;
    if (ent.cn) {
        entry_inside(t);
        return;
    }
    *st = *o;                                           /* (the gesture's earlier keys undone: their notes are in ent) */
    if (m)
        st_cut(t, ed_st, ui.cursor, ui_len(t), m);      /* (the notes starting there out, with their tails) */
    if (single && m) {
        for (k = 0; !((m >> k) & 1u); k++)
            ;
        if (m == 1u << k && o->note[k] == ent.note[0])
            return;                                     /* that note alone: out */
    }
    if (st_kind(st) != ST_NOTE) {
        st->n = 0;
        st->time = ST_NOTE;
    }
    for (k = 0; k < ent.n; k++) {                      /* (Dah: MONO too: its chords feed the ARP; without it the voice
                                                         * plays one of them) */
        for (j = 0; j < st->n && st->note[j] != ent.note[k]; j++)
            ;
        if (j == st->n && st->n < 4u)
            st->note[st->n++] = ent.note[k];
        else if (j < st->n)                             /* (there, going on from the step before: it starts here) */
            st_set_cont(st, st_cont(st) & ~(1u << j));
    }
}
static void entry_note(track_t *t, uint32_t note)     /* one key (or MIDI note) of the gesture onto the cursor step */
{
    uint8_t ch[CHORD_MAX];
    int32_t r;
    uint16_t mask;
    uint32_t n, i, j;
    if (!ui.entry_open) {
        ui.entry_open = 1;
        follow_hold();                                /* (1.6, #242) */
        ent.keys = ent.n = 0;
        ent.orig = *ed_st(t, ui.cursor);
        ent.cn = (uint8_t)entry_carried(t, &ent.orig, ent.cx);
        for (i = 0, ent.run = 1; i < ent.cn; i++)
            if ((j = note_run(t, ui.cursor, ent.cx[i], ent.cx[i])) > ent.run)
                ent.run = (uint16_t)j;
    }
    n = chord_make(t, note, ch, &r, &mask);        /* (CHRD OFF, a kit: the note alone; MONO: the root; Dah: MONO keys add as POLY's) */
    ent.keys++;
    for (i = 0; i < n; i++) {
        for (j = 0; j < ent.n && ent.note[j] != ch[i]; j++)
            ;
        if (j == ent.n && ent.n < 4u)
            ent.note[ent.n++] = ch[i];
    }
    entry_write(t, ent.keys == 1u && n == 1u);
    last_note = (uint8_t)note;
}
/* (Dah) MIDI IN's notes too (midi_ent: the selected track's), one gesture with the keys while any is held, and
 * 0.6 s after the last one: a controller's ARP (its notes one by one) writes its chord, not note after note */
static uint32_t ent_mt __attribute__((section(".pool")));
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    uint32_t k, note;
    for (k = 0; k < 27u; k++) {
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note != KB_SILENT)
            entry_note(t, note);
    }
    for (; midi_ent_r != midi_ent_w; midi_ent_r++, ent_mt = fm1_ms)
        entry_note(t, midi_ent[midi_ent_r & 7u]);
    if (ui.entry_open && !(fm1_in.notes & ~kb_layer) && !midi_owners[song.sel % NTRK] && fm1_ms - ent_mt > 600u)
        ui.entry_open = 0;                              /* (Dah: the cursor stays: KNOB 1 moves it) */
}

/* Discussions #92 / #94: the PRESETS knob by page (the menu, a dialog, NAME and the layers: ui_input before this).
 *   the list / action pages USER,
 *     PROJECT, SONG: as their KNOB 1 (the SEQ pages: a sound load there would drop the track's motion: never).
 *     1.2: with PATTERN, CHANCE and the old AUTOMATION gone, KNOB 1 is the cursor or the row on every SEQ page, so
 *     the PRESETS knob is no longer a second cursor where KNOB 1 was something else; TOOLS and SLICES: nothing
 *   HOME, PRESETS and every other page (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, CLOCK, ...): the selected track's
 *     sound, one list over every engine, then the used user presets (preset_step). A sound load keeps the steps.
 *     Up to 1.0.3 it did that on HOME and PRESETS only */
static void presets_turn(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t g = pg->graph;
    uint64_t held = lock_held();
    if (held) {                                           /* STEP with steps held: their NUDGE (1.2) */
        nudge_turn(held, s);
        return;
    }
    if (ui.home) {
        preset_step(s);
    } else if (pg->id[0] == P_FXORD && pg->fam == FAM_FX) {   /* (Dah) CHAIN: its list */
        edit_param(0, s);
    } else if (g == GR_PATTERN || g == GR_FILE) {          /* (Dah) the list (its knobs are the actions) */
        g == GR_PATTERN ? pt_move(s) : file_move(s);
    } else if (g == GR_USER || g == GR_FILE || g == GR_PATTERN || g == GR_SONG) {
        edit_param(0, s);                                 /* KNOB 1's (STEP: STOP TO EDIT while a song plays) */
        ui.hot_col = 0;
        ui.hot_t = 40;
    } else if (g != GR_SLICES) {     /* (Dah) the sound, on every page (its list shown ~1 s) */
        preset_step(s);
    }
}

/* (Dah) CHAIN: OCT+ opens the page of the effect picked; on + ADD, adds one */
static int8_t fx_back = -1;                             /* (Dah) the track a send was opened from (its MASTER's page) */
static uint8_t fx_back_row;
static void chain_open(void)
{
    static const uint8_t ID[FXU_N_ALL] = {P_DIST, P_ITYPE, P_SLCR, P_EQLO, P_DKSRC, PS_TRK | P_CHOR, PS_TRK | P_DLY,
                                          PS_TRK | P_REV, 0xFF};   /* (COMP: by its title) */
    uint8_t l[FXU_N_ALL];
    uint32_t n = fx_list(fx_tsel(), l), u, i;
    if (ch_cur < n && fx_send(l[ch_cur]) && !mx_master) {   /* (Dah) a track's send: the effect is the MASTER's, its
                                                         * settings there (its return, in the MASTER's chain) */
        uint32_t want = l[ch_cur];
        static char why[32];
        fx_back = (int8_t)(song.sel % NTRK);            /* (Dah: FX on its page again: back to this track's chain) */
        fx_back_row = ch_cur;
        mx_master = 1;
        if (!song.playing && !chain_busy() && help_on()) {   /* (Dah) said (HELP, stopped): where one is now and why */
            str_cpy(why, FXU_N[want % FXU_N_ALL], 12);
            str_cpy(why + str_len(why), " IS SHARED\nBY ALL TRACKS", 26);
            cards_say("ON THE MASTER", why);
            ui.msg_t = (uint8_t)(ui.msg_t + 30u);       /* (HELP's line under it: ~0.5 s more) */
        }
        n = fx_list(fx_tsel(), l);
        for (i = 0; i < n; i++)
            if (l[i] == want)
                ch_cur = (uint8_t)i;
        ui.force = 1;
    }
    if (ch_cur >= n) {                                  /* (Dah: + ADD asks by itself, fl_fx_at; OCT+ there nothing) */
        fl_fx_at();
        return;
    }
    u = l[ch_cur];
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_FX &&
            (u == FXU_COMP ? str_eq(PAGES[i].title, "COMP") : !u ? str_eq(PAGES[i].title, "DIST") : PAGES[i].id[0] == ID[u])) {
            ui.page = (uint8_t)i;
            ui.fam_last[FAM_FX] = ui.page;
            page_entered();
            return;
        }
}

/* (Dah) FX tapped: from elsewhere, CHAIN; on CHAIN, the effect picked's page (+ ADD: asked); on an effect's page, its
 * second when it has one ("EQ" -> "EQ 2"), else back to CHAIN */
static int fx_has_next(void)
{
    char t[16];
    uint32_t i = ui.page + 1u;
    if (ui.home || cur_page()->fam != FAM_FX || cur_page()->id[0] == P_FXORD || i >= NPAGES || PAGES[i].fam != FAM_FX ||
        !page_visible(i))
        return 0;
    str_cpy(t, PAGES[ui.page].title, 12);
    str_cpy(t + str_len(t), " 2", 3);
    return str_eq(PAGES[i].title, t);
}
static void fx_go_chain(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !(PAGES[i].fam == FAM_FX && PAGES[i].id[0] == P_FXORD); i++)
        ;
    ui.page = (uint8_t)(i < NPAGES ? i : page_first(FAM_FX));
    ui.fam_last[FAM_FX] = ui.page;
    ui.home = 0;
    page_entered();
}
static void fx_tap(void)
{
    if (ui.home || cur_page()->fam != FAM_FX) {
        fx_back = -1;
        fx_go_chain();
    } else if (fx_back >= 0 && mx_master && cur_page()->id[0] != P_FXORD && !fx_has_next()) {
        mx_master = 0;                                  /* (Dah) a track's send opened on the MASTER: FX back to the
                                                         * track's chain, its send picked */
        track_select((uint32_t)fx_back);
        ch_cur = fx_back_row;
        fx_back = -1;
        fx_go_chain();
        ui.force = 1;
    }
    else if (ui.home || cur_page()->fam != FAM_FX)
        fx_go_chain();
    else if (cur_page()->id[0] == P_FXORD)
        chain_open();
    else if (fx_has_next()) {
        ui.page++;
        ui.fam_last[FAM_FX] = ui.page;
        page_entered();
    } else
        fx_go_chain();
}
/* MENU > SELECT LIST: SELECT moves down the list of the page shown (its KNOB 1, as PRESETS does there):
 * FILE's projects, PATTERN's slots, USER's presets, PRESETS' sounds, SONG's sections, STEP's
 * cursor. On HOME and on a page with no list (ENV, LFO, ARP, SCL ..) the tempo (Dah) */
static int sel_is_list(void)                           /* (Dah) the page shown has a list SELECT moves down */
{
    uint32_t g = cur_page()->graph;
    return g == GR_FILE || g == GR_PATTERN;            /* (Dah: MIXER: SELECT the pattern, ALGORITHM the track) */   /* (Dah: MIXER, its strips and the MASTER; FILE, PATTERN,
                                                     * whose knobs are the actions; where SELECT did what KNOB 1 does,
                                                     * the tempo again) */
}
static void sel_list(int32_t s)
{
    if (!ui.home && cur_page()->graph == GR_TRK) {      /* (Dah) MIXER: SELECT along the tracks, then the MASTER */
        if (s < 0 && !mx_master && !song.sel)       /* (the MASTER at the left) */
            mx_master = 1;
        else if (s > 0 && mx_master)
            mx_master = 0;
        else if (!mx_master)
            track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, (int32_t)trk_count() - 1));
        ui.force = 1;
        return;
    }
    if (sel_is_list())
        presets_turn(s);
}

/* (Dah) SAVE held: the project open saved over itself, no dialog (as FILE's OPEN and NEW save it first): "SAVED" and its
 * name; none open yet (new music): FILE's COPY, the NAME page, saved as a new project. Stopped only (STOP FIRST: a flash
 * write silences the audio) */
static void save_quick(void)
{
    char m[16];
    if (pfs_busy())
        return;
    if (!pfs_there(pfs_cur())) {
        name_open(NK_ARCH_SAVE, 0);
        return;
    }
    if (project_save_cur()) {
        ui_message("SAVE ERROR");
        return;
    }
    (void)pfs_name(pfs_cur(), m);
    ui_big("SAVED", m);                                 /* (Dah) big, its name under it */
}
/* HOME / REC / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static int layer_knobs_quiet(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static void fx_map_sync(void);
static void fx_assign(int32_t s);
static void fx_default(void);
static int layer_play(void);
static void seq_presets(int32_t s);                    /* (Dah: ui_layer.c, the SEQ layer's QUANTIZE) */
static void seq_row(int32_t s);                        /* (.. its row: QUANTIZE / CLEAR ALL) */
static void tl_clear_all(track_t *t);                  /* (Dah: ui_tools.c, its CLEAR ALL) */
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static void layer_oct_open(uint32_t pressed);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);
static void layer_lock_input(uint32_t pressed);

/* a page button let go (they act on release; a layer's own button: layer_gesture). 1: it acted (EDIT on STEP, USER,
 * PROJECT, AUTOMATION) instead of opening a page */
static int page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_EDIT && detail_on()) {                    /* DETAIL (1.5.1): the step's CHANCE RATCH NUDGE VEL back */
        if (chain_busy())
            ui_message("STOP TO EDIT");
        else
            sd_reset();
        return 1;
    }
    if (b == B_EDIT && !ui.home && cur_page()->graph == GR_EVENTS) {   /* (Dah) AUTOMATION: as STEP's, EDIT clears the
                                                         * lane's point at the cursor, the cursor on */
        au_clear();
        return 1;
    }
    if (b == B_EDIT && !ui.home && cur_page()->graph == GR_FILE) {   /* FILE: EDIT renames the project open */
        if (ui.fsel && ui.fsel != pfs_cur())
            ui_message("OPEN IT TO RENAME");
        else
            name_open(NK_FILE_RENAME, 0);
        return 1;
    }
    if (b == B_EDIT && !ui.home && cur_page()->graph == GR_USER) {
        name_rename();                                  /* SAVE > USER / PROJECT: EDIT renames the slot */
        return 1;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)        /* (GLO SEQ SAVE ..: HOME is ui_input's, home_tap) */
        if (FAM_BTN[f] == b && b != B_HOME) {
            open_family(f);
            return 0;
        }
    return 0;
}

/* a track whose sound's sample is missing (engines.c snd_missing: an empty user slot, a built-in set this build lacks;
 * it plays a sine): the no-file icon and NO SAMPLE (ui.c MSG_NO_SAMPLE). Once per track and source: at power-on, a sound
 * or project load, SRC / SET turned, a slot erased; again after another source or a project load. One a frame; after
 * a message already showing (LOADED) as the second message. Not on notes: nothing in the audio path */
static void sample_notice(void)
{
    static char why[40];
    uint32_t k, code;
    const char *nm;
    for (k = 0; k < NTRK; k++) {
        code = 0;
        if (!(nm = snd_missing(&trk[k], &code))) {
            snd_said[k] = 0;
            continue;
        }
        if (snd_said[k] == code)
            continue;
        if (ui.msg_t)                                   /* (Dah: after the one showing, whole) */
            return;
        snd_said[k] = (uint8_t)code;
        str_cpy(why, nm, 16);                           /* (Dah) which one, where, and what is heard instead */
        str_cpy(why + str_len(why), " ON TRACK ", 12);
        why[str_len(why) + 1u] = 0;
        why[str_len(why)] = (char)('1' + k);
        str_cpy(why + str_len(why), "\nA SINE INSTEAD", 18);
        cards_say(MSG_NO_SAMPLE, why);
        return;
    }
}

/* 1.5.5 (#208): MIDI IN notes since the last frame (seq.c midi_seen): their tracks light (midi_lit), and with MENU > MIDI >
 * AUTO SELECT ON a track that is not selected becomes the selected one, but not while the selected track has had notes
 * within MF_HOLD_MS (two tracks played together, a chord across tracks: the selection stays) nor within MF_HOLD_MS of the
 * last such switch (no flicker), nor with MIDI IN SEL (every note is the selected track's), the menu or a layer up, nor
 * while the selected track is being edited (mf_busy). The lowest such track if several. 1: it switched */
#define MF_HOLD_MS 250u
/* every pass's start (the menu, NAME, SCREEN OFF too): the notes since the last pass light their tracks and are this
 * pass's (mf_seen: SCREEN WAKE +MIDI, AUTO SELECT); a pass that returns before midi_follow drops them (no AUTO SELECT
 * later from a note played in the menu, NAME or the dark) */
static void midi_take(void)
{
    uint32_t k, now = fm1_ms | 1u;
    fm1_irq_off();
    mf_seen = midi_seen;
    midi_seen = 0;
    fm1_irq_on();
    for (k = 0; k < NTRK; k++)
        if ((mf_seen >> k) & 1u)
            midi_lit_ms[k] = now;
}
/* an edit of the selected track in progress: a step held for a lock (STEP, the DRUM grid), a step being entered on the
 * roll, a grid hit waiting for its key's release, a dialog */
static int mf_busy(void)
{
    return lock_held() || (ui.entry_open && (fm1_in.notes & ~kb_layer)) || ui.plk_rm || ui.confirm;
}
static int midi_follow(void)
{
    static uint32_t sw_ms;                              /* the last switch, fm1_ms | 1 (0 none) */
    uint32_t seen = mf_seen, k, now = fm1_ms | 1u, cand, lit;
    mf_seen = 0;
    if (!seen)
        return 0;
    lit = midi_lit_ms[song.sel];                        /* (the selected track's last note: midi_take lit it now if */
                                                        /* this pass has one, and then nothing switches) */
    cand = seen & ~(1u << song.sel);
    k = 0;
    if (cand && !((seen >> song.sel) & 1u) && (ui_rec_prefs & (PREF_MIDI_FOLLOW >> 8)) && song.g[G_ROUTE] != ROUTE_SEL &&
        !ui.menu && !ui.layer && !mf_busy() && (!lit || now - lit >= MF_HOLD_MS) &&
        (!sw_ms || now - sw_ms >= MF_HOLD_MS)) {
        while (!((cand >> k) & 1u))
            k++;
        track_select(k);
        sw_ms = now;
        k = 1;
    } else
        k = 0;
    return (int)k;
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    static uint32_t midi_t, midi_last;
    sample_notice();
    ml_poll();                                          /* (MIDI LEARN: a CC came for the picked parameter) */
    fine_prune();                                       /* (FINE LEN: none left where no note ends) */
    if (motion_full) { motion_full = 0; ui_message("AUTOMATION FULL"); }
    if (midi_follow())                                  /* (AUTO SELECT took the track: no "MIDI IN -> Tn") */
        midi_hint = 0;
    if (midi_hint) {                                    /* MIDI notes into a track that is not selected */
        uint32_t h = midi_hint;
        midi_hint = 0;
        if (!ui.msg_t && (h != midi_last || fm1_ms - midi_t > 4000u)) {
            char b[4] = {'T', (char)('0' + h), 0, 0};
            ui_say("MIDI IN -> ", b);
            midi_last = h;
            midi_t = fm1_ms;
        }
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    if ((pressed || notes) && msig && ui.msg_t) {       /* (Dah) a message's window: any button or key away with it
                                                         * (what it does, done too) */
        ui.msg_t = 0;
        ui.msg2[0] = 0;
    }
    uint32_t home, rec, save, play;
    uint32_t oct, oct_plain;
    uint32_t lay, combo = 0, lytap, lkeys, glo, kq = 0;
    int32_t s, sel = 0, ks[4] = {0, 0, 0, 0}, qs = 0;
    static uint32_t lock_ms;                            /* BPM LOCK: the last locked SELECT turn (fm1_ms | 1; 0 none) */
    pg_service();                                       /* (long patterns: the pages edited back, the ISR's ring filled) */
    midi_take();                                        /* (MIDI IN notes: lights, SCREEN WAKE, AUTO SELECT's) */
    midi_pc_poll();                                     /* MIDI Program Change (before fm6_poll: a bank voice) */
    fm6_poll();                                         /* FM6: PTCH turned -> its patch */
#if !FELUCCA_FM4
    for (k = 0; k < NTRK; k++)                          /* a DIGITAL sound any other way (the paths convert it */
        if (trk[k].eng_req == ENGI_DIGITAL)             /* already): FM6 (fm4_convert.c) */
            fm4_track(&trk[k]);
#endif
    perf_latch_on = fx_latch & 1u;                      /* (MENU > FX LATCH; a settings load sets it too) */
    fx_map_sync();                                      /* (the FX layer's key map: ui_layer.c, a settings load too) */
    fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (MENU > USB LEVEL: fx.c, audio.c) */
    scope_mix = (ui_prefs2 & (PREF_SCOPE_MIX >> 16)) != 0u;   /* (MENU > SCOPE: fx.c, audio.c) */
    rp_apply();                                         /* (MENU > CLICK, CLICK LEVEL, COUNT-IN: click.c, seq.c) */
    if (!ui.menu)
        usb_serial_apply();                             /* (MENU > USB SERIAL: when the menu has closed) */
    settings_later_poll();                              /* (#197: a knob's settings written once the view is left or idle) */
    follow_poll();                                      /* (1.6, #242: SEQ's roll follows the play position) */
    if (scr_input(pressed, notes))                      /* MENU > SCREEN OFF: dark, or the wake gesture: swallowed */
        return;                                         /* (before the buttons' timers: no tap or hold after it) */
    if (mom.n && !mom_down()) {                         /* MOMENTARY: LFO let go (or the menu, a layer ..): back */
        mom_restore();
        ui_message("BACK");
    }
    if (mom.n && ((pressed >> panel.btn[B_OCTUP]) & 1u)) {   /* .. OCT+ with LFO held: kept (no octave) */
        mom.n = 0;
        pressed &= ~(1u << panel.btn[B_OCTUP]);
        ui_message("KEPT");
    }
    if (ui.menu && !ui.confirm && (pressed & ~(1u << panel.btn[B_HOME] | 1u << panel.btn[B_PLAY] |
                                               1u << panel.btn[B_REC] | 1u << panel.btn[B_OCTDN] |
                                               1u << panel.btn[B_OCTUP]))) {
        menu_close();                                   /* (Dah) the menu as a held layer's lock: another button but
                                                         * PLAY, REC and OCT- / OCT+ closes it, and acts as always */
        if (ui.home_t0)
            ui.home_t0 |= 2u;                           /* (HOME still held: no tap, no hold) */
    }
    home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    play = btn_hold(&play_t0, B_PLAY, now, !ui.menu && !ui.confirm && !name_on());   /* (Dah: tap on release, held LIVE) */
    rec = btn_hold(&ui.rec_t0, B_REC, now, 0);          /* (a tap; held, the REC layer: ui_layer.c) */
    if (ui.menu && !ui.confirm && ((pressed >> panel.btn[B_REC]) & 1u)) {   /* (Dah) REC in the menu: as in a layer, it */
        rec_tap();                                      /* arms / disarms (on the press: no REC layer here) */
        ui.rec_t0 |= 2u;
        pressed &= ~(1u << panel.btn[B_REC]);
    }
    save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* (held: no tap) */
    if (((pressed >> panel.btn[B_OCTDN]) & 1u) && ((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u) && ui.confirm)
        ui.save_t0 |= 2u;                               /* (Dah: OCT- cancels the dialog; SAVE let go is no tap then) */
    kb_master = mx_master;                              /* (Dah: the MASTER picked: the keys silent, seq.c) */
    if (mx_master && !ui.menu && !ui.confirm) {         /* .. FX, PLAY and FX's OCT+ OCT- REC only; ALGO leaves it */
        uint32_t ok = 1u << panel.btn[B_FX] | 1u << panel.btn[B_PLAY];
        if (!ui.home && cur_page()->fam == FAM_FX)
            ok |= 1u << panel.btn[B_OCTUP] | 1u << panel.btn[B_OCTDN] | 1u << panel.btn[B_REC];
        if ((pressed & ~ok) || notes)
            cards_say("MASTER: FX ONLY", "TURN \x02" "ALGORITHM\nFOR A TRACK");
        pressed &= ok;
        notes = 0;
        if (ui.home_t0) ui.home_t0 |= 2u;               /* (held: no tap, no hold) */
        if (ui.save_t0) ui.save_t0 |= 2u;
        if (ui.rec_t0 && !(ok >> panel.btn[B_REC] & 1u)) ui.rec_t0 |= 2u;
        home = save = BT_NONE;
        if (!(ok >> panel.btn[B_REC] & 1u)) rec = BT_NONE;
    }
    layer_lock_input(pressed);                          /* (#83: a button closes a locked layer) */
    layer_arm(pressed, now);
    if (((pressed >> panel.btn[B_REC]) & 1u) && ui.ly == LAYER_REC)
        ui.rec_t0 |= 2u;                                /* (REC is its layer's button now: its tap is layer_tap's) */
    layer_oct_open(pressed);                            /* (OCT± with a SET layer's button down: it opens now) */
    oct_plain = !(ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open() || ui.ml.on || ka_chain_page() ||
                  ka.st);                               /* (Dah: the octave, where nothing else takes OCT- / OCT+) */
    oct = oct_taps(pressed, !oct_plain);                /* (on their release; a tap where its press began) */
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: its own) */
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    glo = lay && ui.ly == LAYER_GLO;                    /* GLO held: SELECT is the tempo, BPM LOCK or not (#58) */
    if (!layer_allowed()) {
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
        perf_latched = 0;                               /* (FX LATCH: the latched ones and the macros off) */
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    }
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly) &            /* (REC + PLAY: PLAY plays and REC let go soon is still */
                  ~(ui.ly == LAYER_REC ? 1u << panel.btn[B_PLAY] : 0u)) != 0u;   /* a tap, it arms: as before) */
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        if ((s = panel_enc(EN_PRESET)) != 0 && ui.ly == LAYER_SEQ) {   /* (Dah) SEQ: the QUANTIZE amount */
            qs = s;
            combo = 1;
        } else if (s && ui.ly == LAYER_FX) {            /* (a stray turn would load another sound) */
            fx_assign(s);                               /* FX: the white key held takes the next effect */
            combo = 1;
        }
        if (ui.ly == LAYER_FX && ((pressed >> panel.btn[B_EDIT]) & 1u))
            fx_default();                               /* .. EDIT: its default back */
        if ((s = panel_enc(EN_ALGO)) != 0 && glo) {     /* (Dah) GLO + ALGORITHM: the pattern, every track's (its list
                                                         * ~1 s, + NEW at the end); else: another track */
            pattern_turn_ui(s);
            combo = 1;
        }
        if (ui.ly == LAYER_SEQ && (s = panel_enc(EN_SELECT)) != 0) {   /* (Dah) SEQ + SELECT: the pattern, as
                                                         * everywhere (its selector) */
            pattern_turn_ui(s);
            combo = 1;
        }
        if (glo && (sel = panel_enc(EN_SELECT)) != 0)   /* GLO + SELECT: the tempo, a combo (OCT- puts it back) */
            combo = 1;
    } else if ((kq = (uint32_t)layer_knobs_quiet()) != 0) {   /* a layer letting go: KNOB 1..4 are nobody's (#39) */
        for (k = 0; k < 4u; k++)
            if (panel_enc(EN_K1 + k) != 0)
                combo = 1;                              /* (with the button let go this frame: no tap) */
    }
    lytap = layer_gesture(now, combo);
    layer_show();
    layer_keys(lkeys);
    if (qs)
        seq_presets(qs);
    for (k = 0; k < 4u; k++)
        if (ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (home == BT_TAP && ui.menu) {                    /* HOME (pressed, or held) closes the menu, from ABOUT too; */
        menu_close();                                   /* the release of the hold that opened it is no tap */
        home = BT_NONE;                                 /* (Dah: back where it was opened) */
    } else if (home == BT_HOLD) {                       /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;                                /* (at the tab and row it was left at) */
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (rec == BT_TAP && !ui.menu && !ui.confirm && !name_on())   /* REC pressed in another layer (REC alone: */
        rec_tap();                                      /* its layer's tap); not a dialog or NAME (the menu: above) */
    if (midi_tp) {                                      /* (Dah) a controller's PLAY / STOP / REC (midi_control.c) */
        uint32_t m = midi_tp;
        midi_tp = 0;
        if (m == 1u && !song.playing && !chain_busy() && !seq_counting()) {
            transport_req = 1;
            if (live_on())
                live_mode = 1;                          /* (started from LIVE, as PLAY) */
        }
        else if (m == 2u && (song.playing || chain_busy() || seq_counting()))
            transport_req = 2;
        else if (m == 3u && !ui.confirm && !name_on())
            rec_tap();
    }
    if (ui.menu) {                                      /* (Dah) PLAY plays and stops, as in a layer (HOME: above) */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        if ((pressed >> panel.btn[B_PLAY]) & 1u) {
            if (song.playing || chain_busy() || seq_counting())
                transport_req = 2;
            else
                transport_req = 1;
        }
        if (play_t0)
            play_t0 |= 2u;                              /* (acted on the press: no tap) */
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        if (play_t0)
            play_t0 |= 2u;
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || chain_busy() || seq_counting()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD && !name_on())                  /* (Dah) SAVE held: the project open saved (save_quick) */
        save_quick();
    if (save == BT_TAP && !ui.confirm)                  /* SAVE acts on release (a hold: nothing) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (ui.confirm == CF_PICK_FX) {                 /* (Dah) ADD's picker: SELECT (or KNOB 1) the effect */
            int32_t st = panel_enc(EN_SELECT) + panel_enc(EN_K1);
            uint32_t cur, n = fl_pick_rows(&cur);
            if (st && n) {
                fxp_cur = (uint8_t)clamp((int32_t)cur + st, 0, (int32_t)n - 1);
                ui.force = 1;
            }
        }
        if (ui.confirm == CF_NEW_FROM) {                /* (Dah) NEW's start: SELECT (or KNOB 1) the row */
            int32_t st = panel_enc(EN_SELECT) + panel_enc(EN_K1);
            uint8_t nums[PS_NUM];
            if (st) {
                np_cur = (uint8_t)clamp((int32_t)np_cur + st, 0, (int32_t)ps_list_all(nums));
                ui.force = 1;
            }
        }
        if (oct & 2u) {
            uint32_t kind = ui.confirm;
            ui.confirm = 0;
            ui.force = 1;
            if (kind == CF_OVR_USER) {
                name_open(NK_USER_SAVE, ui.confirm_trk);
            } else if (kind == CF_ERASE_USER) {
                up_ui(1u, ui.confirm_trk);
            } else if (kind == CF_LOAD_USER) {          /* (Dah) MY PRESETS: LOAD, answered */
                up_ui(0u, ui.confirm_trk);
            } else if (kind == CF_DEL_PAT) {       /* SAVE > PATTERN, REC */
                pt_delete();
            } else if (kind == CF_DEL_TRK) {           /* (Dah) MIXER / HOME, OCT- held: the track picked gone */
                track_delete(ui.confirm_trk % NTRK);
            } else if (kind == CF_ADD_TRACK) {     /* one track more: its power-on sound, no steps */
                track_add();
            } else if (kind == CF_PICK_FX) {       /* (Dah) the one picked (SELECT), added last, its page open */
                fx_chain_add(fx_tsel());
                chain_open();
                ui_message("EFFECT ADDED");
            } else if (kind == CF_NEW_PAT) {           /* (Dah) NEW: from what? (EMPTY or a copy: the selector) */
                np_cur = 0;
                confirm_open(CF_NEW_FROM, 0);
                ui.pg_down = 0;
                enc_drop();
                return;
            } else if (kind == CF_NEW_FROM) {      /* (Dah) EMPTY: a new one; a pattern: it, then its copy */
                uint8_t nums[PS_NUM];
                uint32_t n = ps_list_all(nums);
                if (chain_busy())
                    ui_message("STOP TO EDIT");
                else if (np_cur && np_cur <= n && nums[np_cur - 1u] != ps_num(song.sel) && pattern_go_all(nums[np_cur - 1u]))
                    ui_message("MEMORY FULL");
                else
                    pt_do(np_cur ? 2u : 3u);
            } else if (kind == CF_NEW_PAT) {       /* (Dah) a new, empty pattern, every track on it */
                int r = pattern_new_all(0);
                if (r)
                    ui_message(r == 2 ? "NO MORE PATTERNS" : "MEMORY FULL");
                else {
                    tl_ms = fm1_ms | 1u;            /* (its list again, the new one in the middle) */
                    tl_pat = 1;
                }
            } else if (kind == CF_DEL_ARCH) {
                if (!project_delete(ui.confirm_trk)) file_enter();
            } else if (kind == CF_CLEAR_PAT) {     /* (Dah) the SEQ layer's CLEAR TRACK, CLEAR PATTERN (every track's) */
                uint32_t k;
                for (k = 0; !chain_busy() && k < trk_count(); k++)
                    if (k == song.sel || (ui.confirm_trk & 0x80u))
                        tl_clear_all(&trk[k]);
            } else if (kind == CF_INIT_SOUND) {
                if (!chain_busy()) { set_engine(TSEL->eng_req); ui_message("SOUND INIT"); }
            } else if (kind == CF_CLEAN_SOUND) {
                if (!chain_busy()) { clean_engine(); ui_message("CLEAN INIT"); }
            } else if (kind == CF_FACTORY) {           /* MENU > FACTORY RESET, answered: erased, restarted */
                factory_reset();
            }                                           /* (Dah: CLEAR T1 SEQUENCE / TRACK, SONG ORDER, the last track
                                                         * off: SAVE > TOOLS', gone) */
        } else if ((oct & 1u) || (ui.confirm == CF_ADD_TRACK && panel_enc(EN_ALGO) < 0) ||
                   (ui.confirm == CF_NEW_PAT && (panel_enc(EN_SELECT) < 0 || panel_enc(EN_ALGO) < 0))) {
            if (ui.confirm == CF_PICK_FX && ch_cur)   /* (Dah: no effect added: off
                                                         * + ADD, the last effect) */
                ch_cur--;   /* (Dah: ADD TRACK: ALGO
                                                         * turned back cancels too) */
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer; SEQ's */
        layer_tap(lytap);                               /* on every page since 1.2, its tap the SEQ pages) */
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        home_tap();
    if (play == BT_TAP) {                               /* (Dah) PLAY acts on release: held, LIVE MODE */
        if (song.playing || chain_busy() || seq_counting())   /* (a count-in: PLAY stops it) */
            transport_req = 2;
        else if (!ui.home && cur_page()->graph == GR_SONG)
            chain_play_ui();                            /* (PLAY elsewhere plays the pattern) */
        else {
            transport_req = 1;
            if (live_on())
                live_mode = 1;                          /* (started from LIVE: LIVE MODE) */
        }
    } else if (play == BT_HOLD) {
        live_mode = (uint8_t)!live_mode;
        if (!live_mode) {                               /* (off: every track unmuted, the LIVE volumes bypassed) */
            for (id = 0; id < NTRK; id++) {
                live_mreq[id] = 0;
                live_cut[id] = 0;
                trk[id].p[P_MUTE] = 0;
            }
        }
        ui_message(live_mode ? "LIVE MODE ON" : "LIVE MODE OFF");
        ui.force = 1;
    }
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if (layer_play())                           /* (GLO held: RESTART) */
                play_t0 |= 2u;                          /* (no tap after) */
            break;                                      /* (Dah: else its tap, on release: above) */
        case B_SEQ:
        case B_REC:                                     /* tap: above; held, its layer (ui_layer.c) */
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            (void)both;                                 /* (Dah: on release, below: OCT- and OCT+ together are ASSIGN) */
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (ka.st && (ui.home || ui.menu || ui.confirm || ui.layer || name_on() || ui.page != ka.pg)) {
        ka.st = 0;                                      /* (Dah) ASSIGN left behind: it stops (its frames go) */
        ui.force = 1;
    }
    if (oct & 4u && !ui.menu && !ui.confirm && !name_on()) {   /* (Dah) OCT- and OCT+ together: ASSIGN a HOME knob */
        ka_start();
    } else if (ka.st && (oct & 0x33u)) {
        ka_oct(oct | oct >> 4);
    } else if (oct & 0x30u) {                           /* (Dah) the octave, on release (pressed where nothing else
                                                         * takes OCT- / OCT+) */
        song.octave += oct & 0x10u ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
    } else if ((oct & 2u) && fx_on_add() && !ui.confirm) {   /* (Dah) CHAIN on + ADD: which effect (the picker) */
        fxp_cur = 0;
        oct_up_used = 1;                                /* (its release: no octave) */
        confirm_open(CF_PICK_FX, 0);
    } else if (act_cols() && (oct & 2u)) {              /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it (Dah: no more BACK to HOME) */
        if (sg_pend) {                                  /* (Dah) SONG: the section to add / delete: not */
            sg_pend = 0;
            ui.force = 1;
            ui_message("CANCELLED");
        }
        ui.act = 0;
    } else if (ui.ml.on && oct) {                          /* MIDI LEARN: OCT- clears the picked CC, OCT+ done */
        ml_oct(oct);
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: the keys are the grid's) */
#if FELUCCA_SLICE
    if (notes && song.grid != 4u && slice_page_on())    /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif
    lock_keys();                                        /* (a grid key on a hit let go: the hit goes) */
    if (!(song.seq_mode && cur_page()->graph == GR_ROLL && !song.grid && (song.rec >> song.sel) & 1u &&
          !live_rec_sel() && !chain_busy()))
        midi_ent_r = midi_ent_w;                        /* (Dah) MIDI notes step-enter on STEP, armed and stopped only */
    if (song.grid == 4u) {
        live_keys(notes);
    } else if (song.grid == 3u) {
        song_keys(notes);
    } else if (song.grid) {
        grid_keys(notes);
    } else if (song.seq_mode && cur_page()->graph == GR_ROLL) {   /* STEP */
        if (live_rec_sel() || !((song.rec >> song.sel) & 1u)) {   /* armed and playing: the keys record live, not
                                                         * into the cursor step too; not armed (1.2, #133): they
                                                         * only play, being on STEP writes nothing */
            ui.entry_open = 0;
            if (drum_track(TSEL))
                key_lane(notes);                        /* (Dah) the DRUM grid: the lane played picked */
        }
        else if (!chain_busy())
            seq_entry(notes);                           /* armed and stopped: step recording at the cursor */
        else if (notes)
            ui_message("STOP TO EDIT");
    }

    /* #102: the knobs a layer took above are not read again here. The TIMER5 scan (10 kHz) counts detents while this
     * pass runs, so one landing after the layer's read went to the page as well: KNOB 1..4 edited the layer's control
     * and the page under it (HOME's sound, motion recorded), PRESETS could load a sound. They stay for the next pass */
    kq |= lay;
    {                                                   /* (Dah) OCT+ held 0.5 s on SONG, on STEP (a melodic track): the
                                                         * VIEW (OCT+ blinking; let go: closed, no octave) */
        static uint32_t up_ms;
        if (sg_pend && (ui.home || cur_page()->graph != GR_SONG))
            sg_pend = 0;                                /* (Dah: SONG left: the section pending dropped) */
        int down = ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) && !lay && !ui.menu && !ui.confirm, held;
        uint8_t so, ro, ho;
        if (!down)
            up_ms = 0;
        else if (!up_ms)
            up_ms = fm1_ms | 1u;
        held = down && (int32_t)(fm1_ms - up_ms) >= 500;   /* (fm1_ms | 1 may be fm1_ms + 1: no wrap) */
        so = (uint8_t)(held && !ui.home && cur_page()->graph == GR_SONG);
        ro = (uint8_t)(held && roll_view_page());
        ho = (uint8_t)(held && home_view_page());       /* (Dah: HOME's tracks, the same VIEW) */
        if (so | ro | ho)
            oct_up_used = 1;                            /* (the VIEW opened: its release no octave) */
        {                                               /* (Dah) SONG: OCT- held 0.5 s, a section added */
            static uint32_t dn_ms;
            int dn = ((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) && !lay && !ui.home && !ui.menu && !ui.confirm &&
                     (cur_page()->graph == GR_SONG || cur_page()->graph == GR_TRK || cur_page()->graph == GR_FILE) &&
                     !((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u);
            dn |= ((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) && !lay && ui.home && !ui.menu && !ui.confirm &&
                  !((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u);   /* (HOME too: the track deleted) */
            if (!dn)
                dn_ms = 0;
            else if (!dn_ms)
                dn_ms = fm1_ms | 1u;
            else if (!oct_dn_used && (int32_t)(fm1_ms - dn_ms) >= 500) {
                oct_dn_used = 1;
                if ((ui.home && !live_on()) || cur_page()->graph == GR_TRK) {   /* (Dah) MIXER, HOME (not LIVE): OCT- held, the track picked
                                                         * deleted (asked; any one, the ones after it one down) */
                    if (song.playing || chain_busy()) ui_message("STOP FIRST");
                    else if (trk_count() <= 1u) ui_message("ONE TRACK AT LEAST");
                    else if (!ui.home && mx_master) ui_message("NOT THE MASTER");
                    else confirm_open(CF_DEL_TRK, song.sel);
                } else if (cur_page()->graph == GR_FILE) {   /* (Dah) FILE: OCT- held, the project picked deleted (asked) */
                    if (transport_busy() || chain_busy()) ui_message("STOP FIRST");
                    else if (pfs_there(ui.fsel)) confirm_open(CF_DEL_ARCH, ui.fsel);
                    else ui_message("NO PROJECT");
                } else if (chain_busy()) ui_message("STOP TO EDIT");
                else if (chain_config.count >= CHAIN_ROWS) ui_message("SONG FULL");
                else {                                  /* (shown blinking: OCT+ adds it) */
                    sg_pend = 1;
                    sg_prow = (uint8_t)ui.song_row;
                    ui_message("ADD? OCT+ YES");
                    ui.force = 1;
                }
            }
        }
        if (so != sgw.on || ro != prv.on || ho != hvw.on) {
            if (hvw.on && !ho)                          /* (Dah) HOME's VIEW closed: the rows follow the play again
                                                         * (a MOVE in it held them only while it was open) */
                hvw.nofollow = 0;
            if (prv.on && !ro)                          /* (Dah) STEP's too: the roll follows the play and the cursor */
                prv.nofollow = 0;
            sgw.on = so;
            prv.on = ro;
            hvw.on = ho;
            ui.force = 1;
        }
    }
    if (chain_seek_req && !transport_busy()) {          /* (Dah) SONG's GRID turned while it played: on from there */
        chain_play_ui();
        chain_seek_req = 0;
    }
    if (!lay && (s = panel_enc(EN_PRESET)) != 0) {
        if (live_on())
            live_vol(100 - (int32_t)live_cut[song.sel % NTRK] + accel(EN_PRESET, s, 100));   /* (Dah) LIVE: the volume */
        else if (mx_master)
            cards_say("MASTER: FX ONLY", "TURN \x02" "ALGORITHM\nFOR A TRACK");              /* (Dah: no sound to load there) */
        else if (detail_on() && !chain_busy())
            cursor_move(ui.cursor + s);                 /* (DETAIL: the cursor, its KNOB 1 CHANCE) */
        else if (lock_held())
            nudge_turn(lock_held(), s);                 /* (STEP, steps held: their NUDGE) */
        else
            preset_step(s);                             /* (Dah) PRESETS: the sound, on every page (its list ~1 s);
                                                         * the lists: SELECT (MENU > SELECT LIST), KNOB 1 */
    }
    if (!lay && (s = panel_enc(EN_ALGO)) != 0) {   /* ALGORITHM: the selected track, on every page; past the
                                                      * project's last: ADD TRACK? (the dialog) */
        uint32_t was = mx_master ? NTRK : song.sel;
        tl_ms = fm1_ms | 1u;                            /* (Dah) the tracks' list over the panel ~1 s (ui_graph.c) */
        tl_pat = 0;
        if (s > 0 && mx_master) {                       /* (Dah) the MASTER, left of track 1, on every page */
            mx_master = 0;
            track_select(0);
            ui.force = 1;
        } else if (s < 0 && !mx_master && !song.sel) {
            mx_master = 1;
            ui_message("MASTER: FX");
            ui.force = 1;
        } else if (s > 0 && song.sel + 1u >= trk_count() && trk_count() < NTRK && !ui.confirm && !name_on() && !ui.menu)
            confirm_open(CF_ADD_TRACK, trk_count());
        else if (!mx_master)
            track_select((uint32_t)clamp((int32_t)song.sel + s, 0, (int32_t)trk_count() - 1));
        if ((mx_master ? NTRK : song.sel) != was) {
            tl_dy = clamp(tl_dy + (s > 0 ? 17 : -17), -68, 68);
            if (!mx_master && !song.playing && !chain_busy() && !(ui_idle & 2u))   /* (Dah) stopped: the track picked heard, as a sound
                                                         * picked (seq.c seq_preview) */
                pv_req = (uint8_t)(song.sel + 1u);
        }
    }
    if ((s = glo ? sel : panel_enc(EN_SELECT)) != 0) {   /* SELECT knob = global tempo; */
        if (!glo && !lay && ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u)) {   /* (Dah) OCT+ held + SELECT: the tempo,
                                                         * on every page (OCT+'s release then no octave) */
            oct_up_used = 1;
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;
        } else if (!glo && sel_mode() < SEL_MODW && roll_on() && !lay && lane_turn(s)) {
            /* (Dah: STEP's piano roll: SELECT edits the automation lane at the cursor) */
        } else if (!glo && sel_mode() >= SEL_MODW) {         /* MENU > SELECT MODW / VEL (1.5.6, #84): it plays */
            sel_play(sel_mode(), s);
        } else if (!glo && !ui.home && sel_is_list() && (sel_mode() == SEL_PAGE || cur_page()->graph == GR_FILE ||
                                                         cur_page()->graph == GR_PATTERN)) {   /* (Dah: FILE, PATTERN: their
                                                         * list SELECT's, whatever MENU > SELECT says) */   /* MENU > SELECT LIST
                                                         * (1.5.5's PAGE, #188, turned the pages): down the page's list
                                                         * (sel_list); Dah: a page with no list (ENV, LFO, ARP, SCL ..):
                                                         * the tempo, as on HOME. Not while a layer's button is down or
                                                         * letting go, LFO is held (MOMENTARY) or steps are held */
            for (k = (uint32_t)(s > 0 ? s : -s); k && !lay && !kq && !mom_down() && !lock_held(); k--)
                sel_list(s > 0 ? 1 : -1);
        } else if (!glo && !lay) {                      /* (Dah) SELECT: the pattern, every track's, on any page without
                                                         * a list (its list ~1 s); the tempo: GLO + SELECT */
            pattern_turn_ui(s);
        } else if (glo || !(ui_prefs & PREF_BPM_LOCK)) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
        } else {                                        /* MENU > BPM LOCK ON (#58): only with GLO held (and on HOME >
                                                         * CLOCK, GLO's F4 TAP); a turn burst says so once */
            if (!lock_ms || fm1_ms - lock_ms > 1000u)
                ui_message("BPM LOCKED");
            lock_ms = fm1_ms | 1u;
        }
    }
    for (k = 0; k < 4u && !kq; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        uint64_t held;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ka.st) {                                    /* (Dah) ASSIGN: the knob names, it edits nothing */
            ka_knob(k);
            continue;
        }
        if (sgw.on || prv.on || hvw.on) {                /* (Dah) OCT+ held: SONG's, STEP's, HOME's VIEW */
            if (hvw.on)
                home_view_knob(k, s);
            else if (sgw.on)
                song_view_knob(k, s);
            else
                roll_view_knob(k, s);
            oct_up_used = 1;
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (mx_master && (ui.home || (pg->fam != FAM_FX && pg->graph != GR_TRK))) {   /* (Dah) the MASTER: FX's pages */
            cards_say("MASTER: PRESS FX", "TURN \x02" "ALGORITHM\nFOR A TRACK");
            continue;
        }
        if (mom_down())                                 /* (LFO + a knob, MOMENTARY or not: LFO's release opens no page) */
            ui.pg_down &= (uint16_t)~(1u << panel.btn[B_LFO]);
        if ((held = lock_held()) != 0u) {               /* a step held on STEP: KNOB k locks (parameter locks) */
            lock_turn(k, s, held);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (mom_down() && mom_turn(k, s)) {             /* MOMENTARY: LFO held (no automation) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            (pg->graph == GR_USER && k == 0u)
            || pg->graph == GR_SONG || (pg->graph == GR_SLICES && k < 2u) || fop_page(pg->graph) || dln_page(pg->graph) ||
            (xp_page(pg->graph) && xp_field(k) != XP_NONE)) {   /* (not an empty
                                                                                                          * column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
            motion_capture(home_trk(k), (uint32_t)(vp - home_trk(k)->p), *vp);   /* (LEVELS: that track's, as MIXER) */
            ml_knob(home_trk(k), (uint32_t)(vp - home_trk(k)->p));
        } else {
            edit_param(k, s);
        }
    }
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot). A piano key cancels at
 * any time: the keys are not part of what is taught, so one works whatever the buttons' table says */
#define SETUP_IDLE_MS 30000u
#define SETUP_GIVE_UP(t0) (fm1_ms - (t0) > SETUP_IDLE_MS || fm1_in.notes != 0u)
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "calibration title centred");
        GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS,
                       "header icon on its title's line");
        cv_icon_mid(x, 12, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, HEAD_MY, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
    draw_text_box(0, 206, 240, &AF_S, "ANY KEY: CANCEL", T_DIM, 1);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
#if defined(FELUCCA_WEB) && FELUCCA_WEB
    ui_message("NOT IN THE EMULATOR");          /* (Dah: its waits run on the audio's clock, stopped meanwhile) */
    return;
#endif
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (SETUP_GIVE_UP(t0))
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (SETUP_GIVE_UP(t0))
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (SETUP_GIVE_UP(t0))
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
