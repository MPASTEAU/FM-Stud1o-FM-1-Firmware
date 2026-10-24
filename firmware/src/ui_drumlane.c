/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; HA HA HE HU fork */
/* DRUM's lane pages (EDIT family, a DRUM track only, after LANES 2): each lane designed on its own (eng_drum.c dl_*, the
 * values in the track's patch). KNOB 1 is the lane (LANE 1..8, the grid's: ui.lane, one choice for SEQ > STEP and both
 * pages), KNOB 2..4 its values:
 *   DESIGN    LANE  VOICE (KIT: the kit's own; a drum of Felucca's own kit or a piece of a model kit)  TUNE (+-24
 *             semitones)  DECAY
 *   DESIGN 2  LANE  TONE  SNAP  STACK (OFF, or the lane struck with it)
 * DECAY TONE SNAP move the lane from what the kit and EDIT 1 / 2 give it (0: as they are, -50..+49). The panel: the
 * eight lanes, each with its VOICE (and its STACK), the lane picked in the accent. A detent writes one value (dl_set:
 * the audio ISR takes it at its next block); each knob gesture is one undo (SAVE held: the sound before it, ui.c
 * edit_undo_take). Saved with projects and user presets in the track's patch (no new format, no new parameter ids:
 * nothing here is automatable or lockable). Included by ui_graph.c (after icons.c); the cards: ui_draw.c, the knobs:
 * ui_input.c edit_param. */
enum { DN_LANE = DL_NF };                            /* (the fields after the lane's own: dl_*) */
static const uint8_t DN_PAGE[2][4] = {
    {DN_LANE, DL_VOICE, DL_TUNE, DL_DECAY},          /* DESIGN */
    {DN_LANE, DL_TONE, DL_SNAP, DL_STACK},           /* DESIGN 2 */
};

static int dln_page(uint32_t g) { return g == GR_DLANE || g == GR_DLANE2; }
static int dln_ok(void) { return TSEL->eng_req % NENGINES == ENGI_DRUM; }
static uint32_t dln_field(uint32_t slot) { return DN_PAGE[cur_page()->graph == GR_DLANE2][slot & 3u]; }
static uint32_t dln_lane(void) { return ui.lane % NLANE; }

/* field f of the selected lane = v: one undo per knob gesture */
static void dln_write(uint32_t f, uint32_t v)
{
    uint32_t tr = song.sel % NTRK, l = dln_lane();
    track_t *t = TSEL;
    if (dl_get(tr, l, f) == v)
        return;
    edit_undo_take(t, 0x2000u | l << 3 | f, UNDO_SOUND);   /* (a knob turned on, this value: one copy) */
    dl_set(tr, l, f, v);
    motion_undo_done(t);
}

/* KNOB slot turned by steps on a lane page */
static void dln_knob(uint32_t slot, int32_t steps)
{
    uint32_t f = dln_field(slot);
    if (!dln_ok() || !steps)
        return;
    if (f == DN_LANE) {
        ui.lane = (uint8_t)clamp((int32_t)dln_lane() + steps, 0, NLANE - 1);
        return;
    }
    dln_write(f, (uint32_t)clamp((int32_t)dl_get(song.sel, dln_lane(), f) + accel(EN_K1 + slot, steps, DL_MAX[f]), 0,
                                 DL_MAX[f]));
}

/* a signed offset: "+12", "0", "-7" */
static void dln_signed(char *s, int32_t v)
{
    s[0] = v > 0 ? '+' : 0;
    fmt_int(s + (v > 0), v);
}

/* card slot of a lane page: its label, value (12), unit, gauge (0..1000, -1 none), icon; 0 = shown DIM (at its default).
 * dln_card_at: as if the field held vo (>= 0; MENU > LARGE's face, ui_draw.c span_dln) */
static int dln_card_at(uint32_t slot, int32_t vo, const char **label, char *val, const char **unit, int32_t *ratio,
                       uint32_t *icon)
{
    static const char *const LBL[DL_NF] = {"VOICE", "TUNE", "DECAY", "TONE", "SNAP", "STACK"};
    uint32_t f = dln_field(slot), l = dln_lane(), v;
    *unit = "";
    *icon = ICON_AUTO;
    if (f == DN_LANE) {
        *label = "LANE";
        fmt_int(val, (int32_t)l + 1);
        *unit = drum_lane_abbr(TSEL, l);
        *ratio = (int32_t)l * 1000 / (NLANE - 1);
        return 1;
    }
    v = vo >= 0 ? (uint32_t)vo : dl_get(song.sel, l, f);
    *label = LBL[f];
    *ratio = (int32_t)(v * 1000u / DL_MAX[f]);
    switch (f) {
    case DL_VOICE:
        dl_voice_name(v, val);
        *ratio = -1;
        break;
    case DL_TUNE:
        dln_signed(val, (int32_t)v - DL_DEF[DL_TUNE]);
        *unit = "ST";
        break;
    case DL_STACK:
        str_cpy(val, v ? drum_lane_abbr(TSEL, v - 1u) : "OFF", 12);
        *ratio = -1;
        break;
    default:                                         /* DECAY TONE SNAP */
        dln_signed(val, (int32_t)v - DL_DEF[f]);
        break;
    }
    return v != DL_DEF[f];
}
static int dln_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    return dln_card_at(slot, -1, label, val, unit, ratio, icon);
}
