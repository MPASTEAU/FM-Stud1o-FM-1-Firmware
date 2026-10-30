/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HELP (1.5, Discussion #156; MENU > SYSTEM > HELP, OFF by default): a short, visual hint per page and per layer, key
 * caps and two or three words, never a screen of text.
 *   a page   entered (a page button, HOME): its hint in the footer's first row for HELP_FRAMES (~2 s; in place of the
 *            steps or the OCT+ / OCT- hint, which come back after), ui_draw.c draw_foot
 *   a layer  while its map shows: a second footer row under the layer's own (ui_layer.c draw_layer)
 * A hint is "[KEYCAP]WORDS" up to three times (gfx.c kc_tag names the keycaps); help_parse splits it for cv_key_row.
 * Pages by their title (a page added later has none until it is written here); HOME, HOME LEVELS and the DRUM grid by
 * what they are. Every hint fits its row with every word (tests/ui_test.c test_help: no word dropped). The setting and
 * the timer: ui.c help_on, help_start (PREF_HELP, ui.help_t) */

static const char H_EDIT[] = "[K1-4]SOUND[EDIT]HOLD: ENGINES";
static const char H_LANES[] = "[K1-4]LANE LEVELS[EDIT]HOLD: MUTES";
static const char H_FMOP[] = "[K1]OPERATOR[SAVE]+ OCT-: UNDO";
static const struct { const char *title, *hint; } HELP_PAGE[] = {
    {"ENV", "[K1-4]A D S R[ENV]ENV DEST"},
    {"ENV DEST", "[K1-4]ENV AMOUNTS[ENV]ENV"},
    {"LFO", "[K1-4]SHAPE[LFO]HOLD: MOMENTARY"},
    {"LFO 2", "[K1]SYNC TO BPM[LFO]LFO DEST"},
    {"LFO DRAW", "[K1]POINT[K2]VALUE[K3]SHAPE"},   /* (HA HA HE HU) */
    {"LFO DEST", "[K1-4]LFO AMOUNTS[LFO]LFO"},
    {"CHAIN", "[K1]UNIT[K2]MOVE IT[OCT+]ITS PAGE"},
    {"DIST", "[K1]IN[K2]OUT[K3]COLOR"},
    {"INSERT", "[K1]TYPE[FX]DRY / WET"},
    {"INSERT 2", "[K1]DRY / WET[FX]SLICER"},
    {"SLICER", "[K1]ON[K2]PATTERN[FX]EQ"},
    {"EQ", "[K1]FILTER[K2]TYPE[K3]FREQ"},           /* (Dah: four, flat BELLs at first) */
    {"SIDECHAIN", "[K1]SOURCE[K2]DEPTH[K3]RELEASE"},
    {"COMP", "[K1]AMOUNT[K2]RELEASE[FX]NEXT"},
    {"DELAY", "[K1]SEND[K2]TIME[K3]FEEDBACK"},
    {"DELAY 2", "[K1]LEVEL[FX]NEXT"},
    {"REVERB", "[K1]SEND[K2]ROOM SPRING HALL"},
    {"CHORUS", "[K1]SEND[K2]RATE[K3]DEPTH"},
    {"SCL", "[K2]SCALE[K3]QUANTIZE[SCL]CHORDS"},
    {"CHORD", "[K1]CHORD[KEYS]ONE FINGER[SCL]SCL"},
    {"EDIT 1", H_EDIT},
    {"EDIT 2", H_EDIT},
    {"FILTER", "[K1]LP BP HP[K2]CUTOFF[EDIT]VOICE"},
    {"LANES", H_LANES},
    {"LANES 2", H_LANES},
    {"DESIGN", "[K1]LANE[K2]VOICE[K3]TUNE"},
    {"DESIGN 2", "[K1]LANE[K4]STACK A LANE"},
    {"SYN TONE", "[K1]SYN LANE[K2]LEVEL[K3]PITCH"},
    {"SYN TONE 2", "[K2]BEND[K3]SWEEP[K4]SHAPE"},
    {"SYN NOISE", "[K1]SYN LANE[K2]LEVEL[K3]CUT"},
    {"SYN NOISE 2", "[K2]FILTER[K3]CLAP[K4]SPACE"},
    {"SYN METAL", "[K2]LEVEL[K3]BAND[K4]DECAY"},
    {"SLICES", "[K1]SLICE[K2]MOVE[OCT+]SPLIT, JOIN"},
    {"OPERATOR", H_FMOP},
    {"OP ENV", H_FMOP},
    {"OPERATOR 2", H_FMOP},
    {"VOICE", "[K1]POLY, MONO[K2]GLIDE"},
    {"VOICE 2", "[K2]DETUNE[K3]PAN[K4]MUTE"},
    {"VOICE 3", "[K1]STEREO SPREAD[EDIT]EDIT 1"},
    {"SONG", "[K1]SECTION[K3]PATTERN[REC]DELETE"},
    {"MY PRESETS", "[K2]LOAD[K3]DELETE[K4]SAVE"},
    {"FILE", "[PRESETS]PROJECT[K1]OPEN[K4]DELETE"},   /* (HA HA HE HU: the projects; REC deletes, EDIT renames) */
    {"PATTERN", "[PRESETS]PATTERN[K1]OPEN[K4]DELETE"},   /* (HA HA HE HU: a track's patterns; REC deletes) */
    {"ARP", "[K1]MODE[KEYS]CHORD[ARP]ARP 2"},
    {"ARP 2", "[K3]LATCH[K4]ORDER[ARP]ARP"},
    {"STEP", "[KEYS]WRITE[K1]STEP[EDIT]CLEAR"},   /* (1.5.1: SEQ to DETAIL; Dah: EDIT clears the step) */
    {"DETAIL", "[PRESETS]STEP[K1]CHANCE, 1:4, FILL"},   /* (1.5.5: the conditions past 100 %) */
    {"AUTOMATION", "[K1]STEP[K2]LANE[K3]VALUE"},
    {"MIXER", "[K4]MUTE[ALGO]TRACK[HOME]CLOCK"},
};
#define H_HOME "[ALGO]+ TRACK[HOME]HOLD: MENU"
#define H_LEVELS "[K1-4]LEVELS[HOME]MIXER"
#define H_GRID "[REC]STEPS[K2]LANE[SEQ]RATCH"
/* a layer's (LAYER_*): what its footer row (LAYERS[].foot) does not say */
static const char *const HELP_LAYER[LAYER_N] = {
    "", "[KEYS]BLACK: MUTES[FX]2 TAPS: LOCK", "", "",
    "", "", "",                                         /* (Dah: GLO, EDIT: none, the panel takes the room) */
};

/* the hint of the page shown, 0 = none */
static const char *help_page(void)
{
    uint32_t i;
    const char *t;
    if (ui.home)
        return home_levels() ? H_LEVELS : H_HOME;
    if (grid_on() && !detail_on())
        return H_GRID;
    t = cur_page()->title;
    if (env_note(cur_page(), TSEL))
        return 0;                                       /* (Dah: ENV on a kit, FM6: its note says it) */
    if (str_eq(t, "VOICE 2") && page_id(cur_page(), 1u) == 0xFFu)
        return "[K3]PAN[K4]MUTE";                       /* (Dah: DETUNE empty, not UNISON: params.c pg_unused) */
    if (str_eq(t, "VOICE") && page_id(cur_page(), 1u) == 0xFFu)
        return "[K1]POLY, MONO";                        /* (Dah: SLICE: no GLIDE) */
    for (i = 0; i < NELEM(HELP_PAGE); i++)
        if (str_eq(HELP_PAGE[i].title, t))
            return HELP_PAGE[i].hint;
    return 0;
}
/* "[K1]SLOT[OCT+]SAVE" -> kh[] (at most 3), their words in b (bn bytes); the number of hints */
static uint32_t help_parse(const char *s, khint_t *kh, char *b, uint32_t bn)
{
    uint32_t n = 0, i = 0, len;
    int32_t id;
    while (s && n < 3u && (id = kc_tag(s, &len)) >= 0) {
        s += len;
        kh[n].key = (uint8_t)id;
        kh[n++].act = b + i;
        while (*s && *s != '[' && i + 2u < bn)
            b[i++] = *s++;
        b[i++] = 0;
    }
    return n;
}
/* a hint as a row of key hints from x 8 to 232 at y (the footer's rows: 2, 21); 0 = nothing to draw */
static int help_row(const char *s, int32_t y)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b);
    if (n)
        cv_key_row(8, 232, y, kh, n, 7u, T_BG);
    return n != 0u;
}
