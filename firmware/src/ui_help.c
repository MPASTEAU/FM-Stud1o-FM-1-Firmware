/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* HELP (1.5, Discussion #156; MENU > SYSTEM > HELP, OFF by default): a short, visual hint per page and per layer, key
 * caps and two or three words, never a screen of text.
 *   a page   entered (a page button, HOME): its hint in the footer's first row for HELP_FRAMES (~2 s; in place of the
 *            steps or the OCT+ / OCT- hint, which come back after), ui_draw.c draw_foot
 *   a layer  while its map shows: a second footer row under the layer's own (ui_layer.c draw_layer)
 * A hint is "[KEYCAP]WORDS" up to three times (Dah: never a knob, SELECT, ALGORITHM or PRESETS) (gfx.c kc_tag names the keycaps); help_parse splits it for cv_key_row.
 * Pages by their title (a page added later has none until it is written here); HOME, HOME LEVELS and the DRUM grid by
 * what they are. Every hint fits its row with every word (tests/ui_test.c test_help: no word dropped). The setting and
 * the timer: ui.c help_on, help_start (PREF_HELP, ui.help_t) */

/* (Dah) never a knob (K1..K4: the cards say what they do), never SELECT, ALGORITHM or PRESETS: the keys and the
 * buttons only; a page with nothing else to say has no hint (the footer keeps its room) */
static const char H_EDIT[] = "[EDIT]HOLD: ENGINES";
static const char H_SAVE[] = "[SAVE]HOLD: SAVE THE PROJECT";
static const struct { const char *title, *hint; } HELP_PAGE[] = {
    {"ENV", "[ENV]ENV DEST"},
    {"ENV DEST", "[ENV]ENV"},
    {"LFO", "[LFO]HOLD: MOMENTARY"},
    {"LFO 2", "[LFO]LFO DEST"},
    {"LFO DEST", "[LFO]LFO"},
    {"CHAIN", "[OCT+]ITS PAGE"},
    {"INSERT", "[FX]DRY / WET"},
    {"INSERT 2", "[FX]SLICER"},
    {"SLICER", "[FX]EQ"},
    {"COMP", "[FX]NEXT"},
    {"DELAY 2", "[FX]NEXT"},
    {"SCL", "[SCL]CHORDS"},
    {"CHORD", "[KEYS]ONE FINGER[SCL]SCL"},
    {"EDIT 1", H_EDIT},
    {"EDIT 2", H_EDIT},
    {"FILTER", "[EDIT]VOICE"},
    {"LANES", "[EDIT]HOLD: MUTES"},
    {"LANES 2", "[EDIT]HOLD: MUTES"},
    {"SLICES", "[OCT+]SPLIT, JOIN"},
    {"VOICE 3", "[EDIT]EDIT 1"},
    {"SONG", "[REC]DELETE"},
    {"ARP", "[KEYS]CHORD[ARP]ARP 2"},
    {"ARP 2", "[ARP]ARP"},
    {"STEP", "[KEYS]WRITE[OCT+]HOLD: VIEW"},   /* (1.5.1: SEQ to DETAIL; Dah: EDIT no longer clears: OCT+ held the VIEW) */
    {"MIXER", "[HOME]CLOCK"},
    {"FILE", H_SAVE},                                   /* (Dah) the SAVE pages: the quick save (ui_input.c save_quick) */
    {"PATTERN", H_SAVE},
    {"MY PRESETS", H_SAVE},
};
#define H_HOME "[OCT+]+SELECT: BPM[HOME]HOLD: MENU"   /* (Dah: the tempo, OCT+ held and SELECT turned) */
#define H_LEVELS "[HOME]MIXER"
#define H_LIVE "[PLAY]HOLD: LIVE MODE[PRESETS]VOLUME"   /* (Dah) LIVE: the PRESETS knob its volume */
#define H_GRID "[REC]STEPS[SEQ]RATCH"
/* a layer's (LAYER_*): what its footer (LAYERS[].foot) does not say, after it where it fits */
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
        return live_on() ? H_LIVE : home_levels() ? H_LEVELS : H_HOME;
    if (grid_on() && !detail_on())
        return H_GRID;
    t = cur_page()->title;
    if (env_note(cur_page(), TSEL))
        return 0;                                       /* (Dah: ENV on a kit, FM6: its note says it) */
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
