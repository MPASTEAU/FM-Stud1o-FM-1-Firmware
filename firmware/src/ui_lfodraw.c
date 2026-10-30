/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* the track's extras (core.h trkx_t, saved in the projects' FUNY objects, project.c), LFO DRAW's page:
 *   LFO DRAW (LFO family, after LFO 2)  POINT (1..16)  VALUE (-64..63, the point picked)  SHAPE (a template drawn
 *            over all 16: SIN TRI SAW RAMP SQR PULSE WOB STEPS)  SMOOTH (STEP: each point held; LINE: joined)
 *            The panel: the 16 points as bars from the middle line, the point picked in the track's colour, the LFO's
 *            cycle over them (voice.c lfo_wave DRAW). A VALUE or SHAPE turned sets the LFO's WAVE to DRAW.
 * The other extra, CURVE (STEP: an automation event's value holds to the next, as always; RAMP: it moves in a line to
 * the next event of its parameter, motion.c motion_ramp_*), the track's (SEQ > AUTOMATION draws its lanes so; no knob
 * sets it since the lanes replaced the rows, Dah).
 * Not automatable, nor lockable. Included by ui_graph.c (after
 * ui_drumlane.c); the cards: ui_draw.c, the knobs: ui_input.c edit_param, the pages: params.c PAGES. */
enum { XP_POINT, XP_VALUE, XP_SHAPE, XP_SMOOTH, XP_NONE };
static const char *const N_XSHAPE[] = {"SIN", "TRI", "SAW", "RAMP", "SQR", "PULSE", "WOB", "STEPS"};
#define XP_NSHAPE ((uint32_t)NELEM(N_XSHAPE))
static uint8_t xp_point, xp_shape;                   /* the point picked, the last template drawn */

static int xp_page(uint32_t g) { return g == GR_LDRAW; }
static uint32_t xp_field(uint32_t slot) { return slot & 3u; }
static trkx_t *xp_x(void) { return &trkx[song.sel % NTRK]; }

static void xp_draw_on(void)                         /* the LFO plays the drawing */
{
    if (TSEL->p[P_LWAVE] != 5)
        TSEL->p[P_LWAVE] = 5;
}
static void xp_knob(uint32_t slot, int32_t steps)
{
    trkx_t *x = xp_x();
    if (!steps)
        return;
    switch (xp_field(slot)) {
    case XP_POINT:
        xp_point = (uint8_t)clamp((int32_t)xp_point + steps, 0, (int32_t)LDRAW_N - 1);
        break;
    case XP_VALUE:
        x->draw[xp_point % LDRAW_N] = (int8_t)clamp(x->draw[xp_point % LDRAW_N] + accel(EN_K1 + slot, steps, 127), -64, 63);
        xp_draw_on();
        break;
    case XP_SHAPE:
        xp_shape = (uint8_t)clamp((int32_t)xp_shape + (steps > 0 ? 1 : -1), 0, (int32_t)XP_NSHAPE - 1);
        ldraw_template(x->draw, xp_shape);
        xp_draw_on();
        break;
    case XP_SMOOTH:
        x->smooth = steps > 0;
        break;
    default:
        break;
    }
    ui.force = 1;
}
/* a card: 0 = DIM (nothing there) */
static int xp_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    const trkx_t *x = xp_x();
    int32_t v;
    *unit = "";
    *icon = ICON_AUTO;
    *ratio = -1;
    val[0] = 0;
    switch (xp_field(slot)) {
    case XP_POINT:
        *label = "POINT";
        fmt_int(val, xp_point + 1);
        *unit = "/16";
        *ratio = xp_point * 1000 / (LDRAW_N - 1u);
        return 1;
    case XP_VALUE:
        *label = "VALUE";
        v = x->draw[xp_point % LDRAW_N];
        val[0] = v > 0 ? '+' : 0;
        fmt_int(val + (v > 0), v);
        *ratio = (v + 64) * 1000 / 127;
        return TSEL->p[P_LWAVE] == 5;
    case XP_SHAPE:
        *label = "SHAPE";
        str_cpy(val, N_XSHAPE[xp_shape % XP_NSHAPE], 8);
        *ratio = xp_shape * 1000 / (XP_NSHAPE - 1u);
        return 1;
    case XP_SMOOTH:
        *label = "SMOOTH";
        str_cpy(val, x->smooth ? "LINE" : "STEP", 8);
        *ratio = x->smooth ? 1000 : 0;
        return 1;
    default:
        *label = "";
        return 0;
    }
}
