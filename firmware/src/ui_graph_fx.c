/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the FX family's pictures (the track's sound after its chain live: fx_eq.c eq_ring, watched while one shows):
 *   the chain strip (FX's top, CHAIN's boxes): DIST INSERT SLICER EQ in ORDER, then DUCK and the sends; a unit doing
 *   something in the colour, at rest RAISE; CHAIN's unit picked outlined (ACCENT), the sound under the boxes;
 *   DIST: its curve (fx.c track_dist: the drive, the bias, the make-up) and the sound through it;
 *   SENDS: CHO DLY REV, each one's share and the sound going there, a level each.
 * Included by ui_graph_more.c */
static uint8_t ch_cur;                                  /* CHAIN: the row picked (an effect, or + ADD after them) */
static const char *const FXU_N[4] = {"DIST", "INSERT", "SLICER", "EQ"};
static uint32_t fx_unit_at(const track_t *t, uint32_t i) { return (fx_order(t) >> (2u * (i & 3u))) & 3u; }
static int fx_unit_on(const track_t *t, uint32_t u)       /* in the chain: doing something */
{
    return u == 0u ? t->p[P_DIST] != 0 : u == 1u ? t->p[P_ITYPE] != 0 && t->p[P_IMIX] != 0 : u == 2u ? t->p[P_SLCR] != 0
                                                                                                    : eq_units(t) != 0u;
}
/* the effects in the chain, in ORDER: their units (0 DIST 1 INSERT 2 SLICER 3 EQ) into u, the number */
static uint32_t fx_list(const track_t *t, uint8_t *u)
{
    uint32_t i, n = 0;
    for (i = 0; i < 4u; i++)
        if (fx_unit_on(t, fx_unit_at(t, i)))
            u[n++] = (uint8_t)fx_unit_at(t, i);
    return n;
}
/* the chain made u[0..n) (the units out of it after them, as they were): ORDER */
static void fx_set_list(track_t *t, const uint8_t *u, uint32_t n)
{
    uint32_t o = 0, m = 0, i, j, k;
    for (i = 0; i < n; i++)
        o |= (uint32_t)u[i] << (2u * m++);
    for (i = 0; i < 4u; i++) {
        uint32_t x = fx_unit_at(t, i), in = 0;
        for (j = 0; j < n; j++)
            in |= u[j] == x;
        if (!in)
            o |= x << (2u * m++);
    }
    for (k = 0; k < 24u; k++)
        if (FX_ORDER[k] == o)
            t->p[P_FXORD] = (int16_t)k;
}
static void fx_unit_set(track_t *t, uint32_t u, int on)   /* an effect in (its defaults) or out of the chain */
{
    switch (u) {
    case 0:
        t->p[P_DIST] = (int16_t)(on ? 40 : 0);
        break;
    case 1:
        t->p[P_ITYPE] = (int16_t)(on ? IT_SOFT : IT_OFF);
        if (on && !t->p[P_IMIX])
            t->p[P_IMIX] = 127;
        break;
    case 2:
        t->p[P_SLCR] = (int16_t)(on ? 1 : 0);
        break;
    default:
        t->p[P_EQLO] = (int16_t)(on ? 24 : 0);          /* (on: a low cut at ~100 Hz) */
        if (!on) {
            t->p[P_EQHI] = EQ_TOP;
            t->p[P_EQ1G] = t->p[P_EQ2G] = 0;
        }
        break;
    }
}
/* CHAIN's actions on the row picked: ADD (a free effect at the end), REMOVE, TYPE (another free one in its place), MOVE */
static void fx_chain_add(track_t *t)
{
    uint8_t u[4];
    uint32_t n = fx_list(t, u), x;
    for (x = 0; x < 4u && fx_unit_on(t, x); x++)
        ;
    if (n >= 4u || x >= 4u)
        return;
    fx_unit_set(t, x, 1);
    u[n++] = (uint8_t)x;
    fx_set_list(t, u, n);
    ch_cur = (uint8_t)(n - 1u);
}
static void fx_chain_remove(track_t *t)
{
    uint8_t u[4];
    uint32_t n = fx_list(t, u), i;
    if (ch_cur >= n)
        return;
    fx_unit_set(t, u[ch_cur], 0);
    for (i = ch_cur; i + 1u < n; i++)
        u[i] = u[i + 1u];
    fx_set_list(t, u, n - 1u);
    if (ch_cur && ch_cur >= n - 1u)
        ch_cur--;
}
static void fx_chain_type(track_t *t, int32_t dir)
{
    uint8_t u[4];
    uint32_t n = fx_list(t, u), x, k;
    if (ch_cur >= n)
        return;
    for (x = u[ch_cur], k = 0; k < 4u; k++) {           /* the next free one that way */
        x = (x + (dir > 0 ? 1u : 3u)) & 3u;
        if (!fx_unit_on(t, x))
            break;
    }
    if (k == 4u || x == u[ch_cur])
        return;
    fx_unit_set(t, u[ch_cur], 0);
    fx_unit_set(t, x, 1);
    u[ch_cur] = (uint8_t)x;
    fx_set_list(t, u, n);
}
static void fx_chain_move(track_t *t, int32_t dir)
{
    uint8_t u[4], a;
    uint32_t n = fx_list(t, u);
    int32_t j = (int32_t)ch_cur + (dir > 0 ? 1 : -1);
    if (ch_cur >= n || j < 0 || j >= (int32_t)n)
        return;
    a = u[ch_cur]; u[ch_cur] = u[j]; u[j] = a;
    fx_set_list(t, u, n);
    ch_cur = (uint8_t)j;
}
/* an effect's line in the list: what it does now */
static void fx_unit_info(const track_t *t, uint32_t u, char *b)
{
    switch (u) {
    case 0: fmt_int(b, t->p[P_DIST] * 100 / 127); str_cpy(b + str_len(b), " %", 3); break;
    case 1: str_cpy(b, N_ITYPE[clamp(t->p[P_ITYPE], 0, IT_N - 1)], 12); break;
    case 2: str_cpy(b, N_SLCR[clamp(t->p[P_SLCR], 0, 2)], 12); break;
    default: {
        uint32_t m = eq_units(t), c = 0;
        for (; m; m &= m - 1u) c++;
        fmt_int(b, (int32_t)c); str_cpy(b + str_len(b), c > 1u ? " PARTS" : " PART", 7);
        break;
    }
    }
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
/* the chain as boxes across at y, h tall (FX's top): its effects in ORDER, then DUCK (when on) or OUT */
static void fx_chain_strip(const track_t *t, int32_t y, int32_t h, int32_t cur, uint16_t c)
{
    uint8_t u[4];
    uint32_t n = fx_list(t, u), i;
    int32_t bw = n ? (PANEL_W - 48 - (int32_t)n * 8) / (int32_t)n : 0, x = PANEL_X0;
    (void)cur;
    if (bw > 52)
        bw = 52;
    for (i = 0; i < n; i++, x += bw + 8) {
        cv_rrect(x, y, bw, h, 3, c, T_SURF);
        cv_text_in(x, y + (h - AF_S_CAP_H) / 2 - AF_S_CAP_Y, bw, &AF_S, FXU_N[u[i]], T_INK, c);
        cv_rect(x + bw + 1, y + h / 2, 6, 1, T_MID);   /* the arrow on */
    }
    if (!n)
        cv_text_on(x, y + (h - AF_S_CAP_H) / 2 - AF_S_CAP_Y, &AF_S, "NO EFFECT", T_MID, T_SURF), x += 70;
    {
        int dk = t->p[P_DKSRC] != 0;
        cv_rrect(x, y, 40, h, 3, dk ? c : T_RAISE, T_SURF);
        cv_text_in(x, y + (h - AF_S_CAP_H) / 2 - AF_S_CAP_Y, 40, &AF_S, dk ? "DUCK" : "OUT", dk ? T_INK : T_MID,
                   dk ? c : T_RAISE);
    }
}
/* CHAIN: the list, an effect a row (its number, name, what it does), the sound flowing down a line at the left; the
 * row picked outlined; + ADD after them (four at most); the sound out at the right */
static void graph_chain(const track_t *t, uint16_t c)
{
    uint8_t u[4];
    uint32_t n = fx_list(t, u), i, rows = n < 4u ? n + 1u : 4u;
    int32_t rh = 22, y0 = 4, x0 = PANEL_X0 + 14, bw = 150;
    if (ch_cur >= rows)
        ch_cur = (uint8_t)(rows - 1u);
    cv_rect(PANEL_X0 + 5, y0 + 4, 2, (int32_t)rows * rh - 8, T_RAISE);   /* the sound's way down */
    for (i = 0; i < rows; i++) {
        int32_t y = y0 + (int32_t)i * rh;
        int sel = i == ch_cur;
        if (i < n) {
            char d[2] = {(char)('1' + i), 0}, inf[16];
            cv_rrect(PANEL_X0, y + 5, 12, 12, 6, sel ? T_ACCENT : c, T_SURF);
            cv_text_in(PANEL_X0, y + 5 + CAP_IN(S, 12), 12, &AF_S, d, T_INK, sel ? T_ACCENT : c);
            if (sel)
                cv_rrect(x0 - 2, y, bw + 4, rh - 2, 5, T_ACCENT, T_SURF);
            cv_rrect(x0, y + 2, bw, rh - 6, 4, sel ? c : T_RAISE, sel ? T_ACCENT : T_SURF);
            cv_text_on(x0 + 6, y + 2 + CAP_IN(S, rh - 6), &AF_S, FXU_N[u[i]], sel ? T_INK : T_TEXT, sel ? c : T_RAISE);
            fx_unit_info(t, u[i], inf);
            cv_text_r(x0 + bw - 6, y + 2 + CAP_IN(S, rh - 6), &AF_S, inf, sel ? T_INK : T_MID, sel ? c : T_RAISE);
        } else {                                        /* + ADD */
            if (sel)
                cv_rrect(x0 - 2, y, bw + 4, rh - 2, 5, T_ACCENT, T_SURF);
            cv_rrect(x0, y + 2, bw, rh - 6, 4, T_LINE, sel ? T_ACCENT : T_SURF);
            cv_rrect(x0 + 1, y + 3, bw - 2, rh - 8, 3, T_SURF, T_LINE);
            cv_text_in(x0, y + 2 + CAP_IN(S, rh - 6), bw, &AF_S, "+ ADD", sel ? T_TEXT : T_MID, T_SURF);
        }
    }
    {                                                   /* the sound out */
        int32_t wx = x0 + bw + 8, ww = PANEL_X0 + PANEL_W - wx;
        cv_text_in(wx, y0 + 2, ww, &AF_S, "OUT", T_MID, T_SURF);
        fx_wave(wx, ww, y0 + 50, 30, ux_mix(c, T_SURF, 70));
    }
    cv_text_on(PANEL_X0, graph_ht - 12, &AF_S, ch_cur < n ? "[OCT+] ITS PAGE [REC] REMOVE" : n < 4u ? "[OCT+] ADD AN EFFECT" : "",
               T_MID, T_SURF);
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
