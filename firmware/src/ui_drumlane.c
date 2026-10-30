/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM's lane pages (EDIT family, a DRUM track only, after LANES 2): each lane designed on its own (eng_drum.c dl_*, the
 * values in the track's patch). KNOB 1 is the lane (LANE 1..8, the grid's: ui.lane, one choice for SEQ > STEP and every
 * page here), KNOB 2..4 its values:
 *   DESIGN    LANE  VOICE (KIT: the kit's own; a drum of Felucca's own kit, a piece of a model kit, or SYN 1..5)  TUNE
 *             (+-24 semitones)  DECAY
 *   DESIGN 2  LANE  TONE  SNAP  STACK (OFF, or the lane struck with it)
 * DECAY TONE SNAP move the lane from what the kit and EDIT 1 / 2 give it (0: as they are, -50..+49).
 * Phase 2: the layer set of a lane whose VOICE is SYN 1..5 (drum_voice.c DVT_SYNTH, eng_drum.c ds_*), five more pages
 * shown only while the lane picked plays one (ui.c page_visible); on them KNOB 1 steps over the lanes that do, so the
 * page never goes away under it. A set is shared by the lanes that play it (each keeps its own TUNE DECAY TONE SNAP):
 *   SYN TONE     LANE  LEVEL  PITCH (16 Hz .. 5 kHz)  DECAY (its tau)
 *   SYN TONE 2   LANE  BEND (+0..48 semitones at the hit)  SWEEP (the bend's tau)  SHAPE (SINE / TRI, a drive 1..7)
 *   SYN NOISE    LANE  LEVEL  CUT (70 Hz .. 15 kHz)  DECAY
 *   SYN NOISE 2  LANE  FILTER (LP BP HP, a resonance 1..3)  CLAP (OFF, 2..8 strikes)  SPACE (their spacing)
 *   SYN METAL    LANE  LEVEL  TONE (the band, 2.6 .. 13 kHz)  DECAY
 * SHAPE and FILTER are two values each (WAVE and DRIVE, FILT and RESO): one knob steps through both. A layer at LEVEL 0
 * shows its other values DIM. The panel: the eight lanes, each with its VOICE (and its STACK), the lane picked in the
 * accent. A detent writes one value (dl_set, ds_set: the audio ISR takes it at its next block). Saved with projects and user presets in the track's patch
 * (no new format, no new parameter ids: nothing here is automatable or lockable). Included by ui_graph.c (after
 * icons.c); the cards: ui_draw.c, the knobs: ui_input.c edit_param. */
enum { DN_LANE = DL_NF, DN_S0, DN_SHAPE = DN_S0 + DS_NF, DN_FILT };   /* (the fields after the lane's own: dl_*; then
                                                     * the layer set's, DN_S0 + DS_*; then the two of two values) */
static const uint8_t DN_PAGE[7][4] = {
    {DN_LANE, DL_VOICE, DL_TUNE, DL_DECAY},          /* DESIGN */
    {DN_LANE, DL_TONE, DL_SNAP, DL_STACK},           /* DESIGN 2 */
    {DN_LANE, DN_S0 + DS_TLVL, DN_S0 + DS_TPIT, DN_S0 + DS_TDEC},    /* SYN TONE */
    {DN_LANE, DN_S0 + DS_TBEND, DN_S0 + DS_TSWP, DN_SHAPE},          /* SYN TONE 2 */
    {DN_LANE, DN_S0 + DS_NLVL, DN_S0 + DS_NCUT, DN_S0 + DS_NDEC},    /* SYN NOISE */
    {DN_LANE, DN_FILT, DN_S0 + DS_NCLP, DN_S0 + DS_NSPC},            /* SYN NOISE 2 */
    {DN_LANE, DN_S0 + DS_MLVL, DN_S0 + DS_MTON, DN_S0 + DS_MDEC},    /* SYN METAL */
};

static int dln_page(uint32_t g) { return g >= GR_DLANE && g <= GR_DSYN5; }
static int dln_syn_page(uint32_t g) { return g >= GR_DSYN && g <= GR_DSYN5; }
static uint32_t dln_lane(void) { return ui.lane % NLANE; }
/* the page can be shown: a DRUM track; a SYN page: the lane picked plays a layer set */
static int dln_ok(void)
{
    return TSEL->eng_req % NENGINES == ENGI_DRUM && (!dln_syn_page(cur_page()->graph) || dl_syn_of(song.sel, dln_lane()) >= 0);
}
static uint32_t dln_field(uint32_t slot)
{
    uint32_t g = cur_page()->graph;
    return DN_PAGE[dln_page(g) ? g - GR_DLANE : 0u][slot & 3u];
}
/* the selected lane's layer set (0..4; 0 when it plays none: the SYN pages are not shown then) */
static uint32_t dln_set(void)
{
    int32_t k = dl_syn_of(song.sel, dln_lane());
    return k < 0 ? 0u : (uint32_t)k;
}
/* field f's highest value */
static uint32_t dln_max(uint32_t f)
{
    return f < DL_NF ? DL_MAX[f] : f == DN_SHAPE ? 15u : f == DN_FILT ? 11u : f > DN_LANE ? DS_MAX[(f - DN_S0) % DS_NF] : 0u;
}
/* field f of the selected lane (SHAPE: WAVE x 8 + DRIVE, FILTER: FILT x 4 + RESO) */
static uint32_t dln_get(uint32_t f)
{
    uint32_t tr = song.sel, k = dln_set();
    if (f < DL_NF)
        return dl_get(tr, dln_lane(), f);
    if (f == DN_SHAPE)
        return ds_get(tr, k, DS_TWAV) * 8u + ds_get(tr, k, DS_TDRV);
    if (f == DN_FILT)
        return ds_get(tr, k, DS_NFLT) * 4u + ds_get(tr, k, DS_NRES);
    return ds_get(tr, k, (f - DN_S0) % DS_NF);
}

/* field f of the selected lane = v */
static void dln_write(uint32_t f, uint32_t v)
{
    uint32_t tr = song.sel % NTRK, l = dln_lane(), k = dln_set();
    if (dln_get(f) == v)
        return;
    if (f < DL_NF) {
        dl_set(tr, l, f, v);
    } else if (f == DN_SHAPE) {
        ds_set(tr, k, DS_TWAV, v / 8u);
        ds_set(tr, k, DS_TDRV, v % 8u);
    } else if (f == DN_FILT) {
        ds_set(tr, k, DS_NFLT, v / 4u);
        ds_set(tr, k, DS_NRES, v % 4u);
    } else {
        ds_set(tr, k, (f - DN_S0) % DS_NF, v);
    }
}

/* KNOB slot turned by steps on a lane page (KNOB 1 on a SYN page: the lanes that play a set) */
static void dln_knob(uint32_t slot, int32_t steps)
{
    uint32_t f = dln_field(slot);
    if (!dln_ok() || !steps)
        return;
    if (f == DN_LANE) {
        int32_t l = (int32_t)dln_lane(), dir = steps > 0 ? 1 : -1, n = steps * dir, j;
        if (!dln_syn_page(cur_page()->graph)) {
            ui.lane = (uint8_t)clamp(l + steps, 0, NLANE - 1);
            return;
        }
        while (n--) {
            for (j = l + dir; j >= 0 && j < NLANE && dl_syn_of(song.sel, (uint32_t)j) < 0; j += dir)
                ;
            if (j < 0 || j >= NLANE)
                break;
            l = j;
        }
        ui.lane = (uint8_t)l;
        return;
    }
    dln_write(f, (uint32_t)clamp((int32_t)dln_get(f) + accel(EN_K1 + slot, steps, (int32_t)dln_max(f)), 0,
                                 (int32_t)dln_max(f)));
}

/* a signed offset: "+12", "0", "-7" */
static void dln_signed(char *s, int32_t v)
{
    s[0] = v > 0 ? '+' : 0;
    fmt_int(s + (v > 0), v);
}
/* a frequency (p16): "49" HZ, "4.9" / "13" KHZ */
static void dln_hz(int32_t p16, char *s, const char **unit)
{
    uint32_t hz = (uint32_t)(((uint64_t)pitch_inc(clamp(p16, 0, 2047)) * FS + (1ull << 31)) >> 32);
    *unit = hz < 1000u ? "HZ" : "KHZ";
    if (hz < 1000u)
        fmt_int(s, (int32_t)hz);
    else if (hz < 10000u)
        fmt_fix(s, (int32_t)(hz / 100u), 1);
    else
        fmt_int(s, (int32_t)(hz / 1000u));
}
/* a time (us): "4.5" / "130" MS, "1.2" S */
static void dln_ms(uint32_t us, char *s, const char **unit)
{
    *unit = us < 1000000u ? "MS" : "S";
    if (us < 10000u)
        fmt_fix(s, (int32_t)(us / 100u), 1);
    else if (us < 1000000u)
        fmt_int(s, (int32_t)(us / 1000u));
    else
        fmt_fix(s, (int32_t)(us / 100000u), 1);
}

/* card slot of a lane page: its label, value (12), unit, gauge (0..1000, -1 none), icon; 0 = shown DIM (at its default;
 * on a SYN page: its layer at LEVEL 0). dln_card_at: as if the field held vo (>= 0; MENU > LARGE's face, ui_draw.c
 * span_dln) */
static int dln_card_at(uint32_t slot, int32_t vo, const char **label, char *val, const char **unit, int32_t *ratio,
                       uint32_t *icon)
{
    static const char *const LBL[DL_NF] = {"VOICE", "TUNE", "DECAY", "TONE", "SNAP", "STACK"};
    static const char *const SLBL[DS_NF] = {"LEVEL", "PITCH", "BEND", "SWEEP", "DECAY", "DRIVE", "WAVE", "LEVEL",
                                            "FILTER", "CUT", "RESO", "DECAY", "CLAP", "SPACE", "LEVEL", "TONE", "DECAY"};
    static const char *const FLT[3] = {"LP", "BP", "HP"};
    uint32_t f = dln_field(slot), l = dln_lane(), v, s, lvl;
    *unit = "";
    *icon = ICON_AUTO;
    if (f == DN_LANE) {
        *label = "LANE";
        fmt_int(val, (int32_t)l + 1);
        *unit = drum_lane_abbr(TSEL, l);
        *ratio = (int32_t)l * 1000 / (NLANE - 1);
        return 1;
    }
    v = vo >= 0 ? (uint32_t)vo : dln_get(f);
    *ratio = (int32_t)(v * 1000u / dln_max(f));
    if (f < DL_NF) {
        *label = LBL[f];
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
        default:                                     /* DECAY TONE SNAP */
            dln_signed(val, (int32_t)v - DL_DEF[f]);
            break;
        }
        return v != DL_DEF[f];
    }
    if (f == DN_SHAPE) {                             /* SINE / TRI, and the drive */
        *label = "SHAPE";
        str_cpy(val, v / 8u ? "TRI" : "SINE", 12);
        if (v % 8u) {
            s = str_len(val);
            val[s] = ' ', val[s + 1u] = (char)('0' + v % 8u), val[s + 2u] = 0;
        }
        *ratio = -1;
        f = DN_S0 + DS_TWAV;
    } else if (f == DN_FILT) {                       /* LP BP HP, and the resonance */
        *label = "FILTER";
        str_cpy(val, FLT[v / 4u % 3u], 12);
        if (v % 4u) {
            val[2] = ' ', val[3] = (char)('0' + v % 4u), val[4] = 0;
        }
        *ratio = -1;
        f = DN_S0 + DS_NFLT;
    } else {
        s = f - DN_S0;
        *label = SLBL[s];
        switch (s) {
        case DS_TPIT:
            dln_hz(ds_pitch(v), val, unit);
            break;
        case DS_NCUT:
            dln_hz(ds_cut(v), val, unit);
            break;
        case DS_MTON:
            dln_hz(ds_band(v), val, unit);
            break;
        case DS_TBEND:
            dln_signed(val, (int32_t)v);
            *unit = "ST";
            break;
        case DS_TSWP:
            dln_ms(DS_SWEEP[v % 15u], val, unit);
            break;
        case DS_TDEC:
            dln_ms(ds_tau(v * 99u / 31u, 64), val, unit);
            break;
        case DS_NDEC:
        case DS_MDEC:
            dln_ms(ds_tau(v, 64), val, unit);
            break;
        case DS_NCLP:
            if (v) {
                fmt_int(val, (int32_t)v + 1);
                *unit = "X";
            } else {
                str_cpy(val, "OFF", 12);
            }
            break;
        case DS_NSPC:
            fmt_fix(val, (int32_t)(DS_SPACE[v & 3u] * CTL * 10000u / FS), 1);
            *unit = "MS";
            break;
        default:                                     /* the levels */
            fmt_int(val, (int32_t)v);
            return v != 0u;
        }
    }
    s = f - DN_S0;                                   /* (DIM: its layer silent) */
    lvl = s < DS_NLVL ? DS_TLVL : s < DS_MLVL ? DS_NLVL : DS_MLVL;
    return ds_get(song.sel, dln_set(), lvl) != 0u;
}
static int dln_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    return dln_card_at(slot, -1, label, val, unit, ratio, icon);
}
