/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the FX family's pictures (the track's sound after its chain live: fx_eq.c eq_ring, watched while one shows):
 *   the chain strip (FX's top, CHAIN's boxes): DIST INSERT SLICER EQ in ORDER, then DUCK and the sends; a unit doing
 *   something in the colour, at rest RAISE; CHAIN's unit picked outlined (ACCENT), the sound under the boxes;
 *   DIST: its curve (fx.c track_dist: the drive, the bias, the make-up) and the sound through it;
 *   SENDS: CHO DLY REV, each one's share and the sound going there, a level each.
 * Included by ui_graph_more.c */
static uint8_t ch_cur;                                  /* CHAIN: the unit picked, its place in ORDER (0..3) */
static const char *const FXU_N[4] = {"DIST", "INS", "SLICE", "EQ"};
static uint32_t fx_unit_at(const track_t *t, uint32_t i) { return (fx_order(t) >> (2u * (i & 3u))) & 3u; }
static int fx_unit_on(const track_t *t, uint32_t u)
{
    return u == 0u ? t->p[P_DIST] != 0 : u == 1u ? t->p[P_ITYPE] != 0 && t->p[P_IMIX] != 0 : u == 2u ? t->p[P_SLCR] != 0
                                                                                                    : eq_units(t) != 0u;
}
/* the ORDER whose unit at place i and the one at place j swap places: its index (N_FXORD's) */
static int32_t fx_order_swap(const track_t *t, uint32_t i, uint32_t j)
{
    uint32_t o = fx_order(t), a = (o >> (2u * i)) & 3u, b = (o >> (2u * j)) & 3u, k;
    o &= ~(3u << (2u * i) | 3u << (2u * j));
    o |= b << (2u * i) | a << (2u * j);
    for (k = 0; k < 24u; k++)
        if (FX_ORDER[k] == o)
            return (int32_t)k;
    return t->p[P_FXORD];
}
static int32_t fx_peak(void)                            /* the sound's loudest of the last 128 samples (Q15) */
{
    uint32_t i, w = eq_wp;
    int32_t m = 0;
    for (i = 0; i < 128u; i++) {
        int32_t v = eq_ring[(w + EQ_RN - 1u - i) & (EQ_RN - 1u)];
        m = v < 0 ? (-v > m ? -v : m) : v > m ? v : m;
    }
    return m;
}
static void fx_wave(int32_t x0, int32_t w, int32_t cy, int32_t a, uint16_t col)   /* the sound, w samples across */
{
    uint32_t r = eq_wp;
    int32_t x, py = cy;
    for (x = 0; x < w; x++) {
        int32_t v = eq_ring[(r + EQ_RN - (uint32_t)w + (uint32_t)x) & (EQ_RN - 1u)], y = cy - v * a / 16384;
        y = clamp(y, cy - a, cy + a);
        if (x)
            cv_line_t(x0 + x - 1, py, x0 + x, y, col, 1);
        py = y;
    }
}
/* the chain as boxes from x0 over w px, h tall at y; cur: the place outlined (-1 none) */
static void fx_chain_strip(const track_t *t, int32_t y, int32_t h, int32_t cur, uint16_t c)
{
    int32_t bw = (PANEL_W - 3 * 8 - 40) / 4, i, x = PANEL_X0;
    for (i = 0; i < 4; i++, x += bw + 8) {
        uint32_t u = fx_unit_at(t, (uint32_t)i);
        int on = fx_unit_on(t, u);
        if (i == cur)
            cv_rrect(x - 2, y - 2, bw + 4, h + 4, 4, T_ACCENT, T_SURF);
        cv_rrect(x, y, bw, h, 3, on ? c : T_RAISE, i == cur ? T_ACCENT : T_SURF);
        cv_text_in(x, y + (h - AF_S_CAP_H) / 2 - AF_S_CAP_Y, bw, &AF_S, FXU_N[u], on ? T_INK : T_MID, on ? c : T_RAISE);
        cv_rect(x + bw + 1, y + h / 2, 6, 1, T_MID);   /* the arrow on */
    }
    {                                                   /* then DUCK (when on) and out */
        int dk = t->p[P_DKSRC] != 0;
        cv_rrect(x, y, 40, h, 3, dk ? c : T_RAISE, T_SURF);
        cv_text_in(x, y + (h - AF_S_CAP_H) / 2 - AF_S_CAP_Y, 40, &AF_S, dk ? "DUCK" : "OUT", dk ? T_INK : T_MID,
                   dk ? c : T_RAISE);
    }
}
static void graph_chain(const track_t *t, uint16_t c)
{
    int32_t h = graph_ht * 40 / 100, y = 8;
    uint32_t u = fx_unit_at(t, ch_cur & 3u);
    fx_chain_strip(t, y, h, ch_cur & 3u, c);
    fx_wave(PANEL_X0, PANEL_W, y + h + (graph_ht - 14 - y - h) / 2, (graph_ht - 14 - y - h) / 2 - 2, ux_mix(c, T_SURF, 70));
    cv_text_on(PANEL_X0, graph_ht - 12, &AF_S, FXU_N[u], T_TEXT, T_SURF);
    cv_text_r(PANEL_X0 + PANEL_W, graph_ht - 12, &AF_S, "[OCT+] ITS PAGE", T_MID, T_SURF);
}
static int32_t dist_curve(const track_t *t, int32_t x)   /* fx.c track_dist's, the filters left out */
{
    int32_t d = t->p[P_DIST], g = 4096 + d * d * 2, mk = 30000 - d * 120, bias = 2400;
    if (!d)
        return x;
    return mulq15(softclip((((x >> 2) * g) >> 10) + bias) - softclip(bias), mk);
}
static void graph_dist(const track_t *t, uint16_t c)
{
    int32_t cx = PANEL_X0 + PANEL_W / 2, cy = graph_ht / 2, a = graph_ht * 42 / 100, x, py = 0;
    cv_rect(PANEL_X0, cy, PANEL_W, 1, T_GRID);
    cv_rect(cx, cy - a, 1, 2 * a, T_GRID);
    cv_line_t(cx - a, cy + a, cx + a, cy - a, T_RAISE, 1);   /* clean */
    for (x = -a; x <= a; x++) {                          /* the curve: -1.5 .. +1.5 of a voice across */
        int32_t y = cy - clamp(dist_curve(t, x * 49152 / a) * a / 49152, -a, a);
        if (x > -a)
            cv_line_t(cx + x - 1, py, cx + x, y, c, 2);
        py = y;
    }
    fx_wave(PANEL_X0, cx - a - 6 - PANEL_X0, cy, a, ux_mix(c, T_SURF, 70));   /* the sound out, each side */
    fx_wave(cx + a + 6, PANEL_X0 + PANEL_W - cx - a - 6, cy, a, ux_mix(c, T_SURF, 70));
    {                                                    /* where the sound is on the curve now */
        int32_t p = clamp(fx_peak() * 4 * a / 49152, 0, a);
        if (p > 1)
            cv_rect(cx + p - 2, cy - clamp(dist_curve(t, p * 49152 / a) * a / 49152, -a, a) - 2, 5, 5, T_ACCENT);
    }
}
static void graph_sends(const track_t *t, uint16_t c)
{
    static const uint8_t ID[3] = {P_CHOR, P_DLY, P_REV};
    static const char *const NM[3] = {"CHORUS", "DLY", "REVERB"};
    int32_t cw = PANEL_W / 3, top = 6, bot = graph_ht - 16, pk = fx_peak(), i;
    uint32_t hot = ui.hot_t ? page_id(cur_page(), ui.hot_col) : 0xFFu;
    for (i = 0; i < 3; i++) {
        int32_t x = PANEL_X0 + i * cw + cw / 2 - 14, v = t->p[ID[i]], h = v * (bot - top) / 127;
        int32_t lv = clamp(pk * v / 127 * (bot - top) / 16384, 0, h);
        cv_rrect(x, top, 28, bot - top, 4, T_RAISE, T_SURF);
        if (h > 2)
            cv_rrect(x, bot - h, 28, h, 4, ux_mix(c, T_SURF, 45), T_RAISE);
        if (lv > 1)
            cv_rect(x + 6, bot - lv, 16, lv, hot == ID[i] ? T_ACCENT : c);   /* the sound going there now */
        cv_text_in(PANEL_X0 + i * cw, bot + 4, cw, &AF_S, NM[i], v ? T_TEXT : T_MID, T_SURF);
    }
}
#include "ui_graph_eq.c"
/* the FX family's own pages (1), else 0; the sound watched while one shows */
static int graph_fxfam(const page_t *pg, const track_t *t, uint16_t c)
{
    uint32_t id = pg->id[0];
    if (pg->fam != FAM_FX || pg->scope != SC_TRACK)
        return 0;
    if (id == P_FXORD) graph_chain(t, c);
    else if (id == P_DIST && pg->graph == GR_NONE) graph_dist(t, c);
    else if (id == P_EQLO || id == P_EQ2F) graph_eq(t, c);
    else if (id == P_CHOR) graph_sends(t, c);
    else return 0;
    return 1;
}
