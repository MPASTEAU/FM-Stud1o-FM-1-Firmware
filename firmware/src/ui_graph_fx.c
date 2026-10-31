/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the FX family's pictures (the track's sound after its chain live: fx_eq.c eq_ring, watched while one shows):
 *   the chain strip (FX's top, CHAIN's boxes): DIST INSERT SLICER EQ in ORDER, then DUCK and the sends; a unit doing
 *   something in the colour, at rest RAISE; CHAIN's unit picked outlined (ACCENT), the sound under the boxes;
 *   DIST: its curve (fx.c track_dist: the drive, the bias, the make-up) and the sound through it;
 *   SENDS: CHO DLY REV, each one's share and the sound going there, a level each.
 * Included by ui_graph_more.c */
static uint8_t ch_cur;                                  /* CHAIN: the row picked (an effect, or + ADD after them) */
static uint32_t fx_free_list(const track_t *t, uint8_t *f);   /* (Dah: ADD's picker, below) */
static uint8_t fxp_cur;
/* the effects: 0..4 in series in ORDER (DIST INSERT SLICER EQ SIDECHAIN), 5..7 the sends to GLO's buses (CHORUS DELAY
 * REVERB, after them: fixed). In the chain: doing something (a send above 0); none by default */
#define FXU_SER 5u
#define FXU_COMP 8u                                     /* (Dah) the COMPRESSOR: just before SIDECHAIN, fixed (fx_comp.c) */
#define FXU_N_ALL 9u
static const char *const FXU_N[FXU_N_ALL] = {"DIST", "INSERT", "SLICER", "EQ", "SIDECHAIN", "CHORUS", "DELAY", "REVERB",
                                             "COMP"};
static const char *const FXU_S[FXU_N_ALL] = {"DST", "INS", "SLC", "EQ", "SC", "CHO", "DLY", "REV", "CMP"};   /* (FX's strip) */
/* (Dah) CHAIN's KNOB 4, MIX: each effect's presence in the mix (DIST its OUT, INSERT its MIX, SLICER its DEPTH,
 * SIDECHAIN its DEPTH, a send its level; the EQ none) */
static const uint8_t FXU_MIX[FXU_N_ALL] = {P_DOUT, P_IMIX, P_SLDEPTH, 0xFF, P_DKAMT, P_CHOR, P_DLY, P_REV, 0xFF};
/* (Dah) the MASTER's chain (fx.c mtrk): DIST, EQ, COMP only; its COMPRESSOR GLO's G_MCOMP G_MREL, no sends (mst_x) */
static trkx_t mst_x;
static int fx_mst(const track_t *t) { return t == &mtrk; }
static trkx_t *fx_x(const track_t *t) { return fx_mst(t) ? &mst_x : &trkx[trk_index(t) % NTRK]; }
static int fx_unit_ok(const track_t *t, uint32_t u) { return !fx_mst(t) || u == 0u || u == 3u || u == FXU_COMP; }
/* (Dah) CHORUS DELAY REVERB: one each for the project (fx.c's buses). A track's are its sends (their amount, where they
 * take its sound); the MASTER's their returns, always in its chain, first: their own settings there */
static int fx_send(uint32_t u) { return u >= FXU_SER && u < FXU_COMP; }
static int fx_ret_fixed(const track_t *t, uint32_t u) { return fx_mst(t) && fx_send(u); }
static uint32_t fx_camt(const track_t *t) { return fx_mst(t) ? (uint32_t)song.g[G_MCOMP] : trkx[trk_index(t) % NTRK].rsv0; }
static void fx_cset(const track_t *t, int32_t amt, int32_t rel)   /* the COMPRESSOR's AMOUNT, RELEASE (-1: as it is) */
{
    if (fx_mst(t)) {
        if (amt >= 0) song.g[G_MCOMP] = (int16_t)amt;
        if (rel >= 0) song.g[G_MREL] = (int16_t)rel;
    } else {
        if (amt >= 0) trkx[trk_index(t) % NTRK].rsv0 = (uint8_t)amt;
        if (rel >= 0) trkx[trk_index(t) % NTRK].rsv1 = (uint8_t)rel;
    }
}
static int16_t *fx_cio(const track_t *t, uint32_t out, int8_t **x8)   /* (Dah) the COMPRESSOR's IN (0) / OUT (1) */
{
    *x8 = 0;
    if (fx_mst(t))
        return &mtrk.p[out ? P_IB : P_IA];
    *x8 = out ? &trkx[trk_index(t) % NTRK].cout : &trkx[trk_index(t) % NTRK].cin;
    return 0;
}
static int32_t fx_cget(const track_t *t, uint32_t out)
{
    int8_t *x8;
    int16_t *p = fx_cio(t, out, &x8);
    return p ? *p : *x8;
}
static void fx_cput(const track_t *t, uint32_t out, int32_t v)
{
    int8_t *x8;
    int16_t *p = fx_cio(t, out, &x8);
    v = clamp(v, -64, 63);
    if (p) *p = (int16_t)v; else *x8 = (int8_t)v;
}
static uint32_t fx_max(const track_t *t) { return fx_mst(t) ? 6u : 8u; }   /* (Dah: the MASTER's three returns and
                                                         * three of its own) */   /* (the effects it takes; Dah: eight at most) */   /* (the effects it takes) */
static uint32_t fx_crel(const track_t *t) { return fx_mst(t) ? (uint32_t)song.g[G_MREL] : trkx[trk_index(t) % NTRK].rsv1; }
static uint32_t fx_unit_at(const track_t *t, uint32_t i) { return (fx_order(t) >> (3u * (i % FXU_SER))) & 7u; }
static int fx_unit_on(const track_t *t, uint32_t u)
{
    switch (u) {
    case 0: return t->p[P_DIST] != 0;
    case 1: return t->p[P_ITYPE] != 0 && t->p[P_IMIX] != 0;
    case 2: return t->p[P_SLCR] != 0;
    case 3: return (eq_cfg(t)->f & EQF_IN) != 0u;       /* (Dah: flat, it is there) */
    case 4: return t->p[P_DKSRC] != 0;
    case 5: return fx_mst(t) || t->p[P_CHOR] != 0;     /* (Dah: the MASTER's returns: always) */
    case 6: return fx_mst(t) || t->p[P_DLY] != 0;
    case 7: return fx_mst(t) || t->p[P_REV] != 0;
    default: return fx_camt(t) != 0u;
    }
}
/* the effects in the chain as its list shows them: those in series in ORDER, each send where it takes the sound
 * (trkx tap: before the series one at place tap - 1; 0 after them all); into u, the number */
static uint32_t fx_list(const track_t *t, uint8_t *u)
{
    const trkx_t *x = fx_x(t);
    uint32_t i, j, n = 0, ns = 0;
    for (i = 0; i < FXU_SER; i++) {
        for (j = 0; j < 3u; j++)
            if (x->tap[j] == i + 1u && fx_unit_on(t, FXU_SER + j))
                u[n++] = (uint8_t)(FXU_SER + j);
        if (fx_unit_at(t, i) == 4u)
            continue;                                   /* (the SIDECHAIN: last, below) */
        if (fx_unit_on(t, fx_unit_at(t, i)))
            u[n++] = (uint8_t)fx_unit_at(t, i), ns++;
    }
    for (j = 0; j < 3u; j++) {                          /* the sends after them all (or placed past the last) */
        if (n > FXU_N_ALL - 1u)
            break;
        uint32_t in = 0, k;
        for (k = 0; k < n; k++)
            in |= u[k] == FXU_SER + j;
        if (!in && fx_unit_on(t, FXU_SER + j))
            u[n++] = (uint8_t)(FXU_SER + j);
    }
    if (fx_unit_on(t, FXU_COMP) && n < FXU_N_ALL)
        u[n++] = FXU_COMP;                              /* (the COMPRESSOR, then the SIDECHAIN: last, fixed) */
    if (fx_unit_on(t, 4u) && n < FXU_N_ALL)
        u[n++] = 4u;
    (void)ns;
    if (fx_mst(t)) {                                    /* (Dah) the MASTER: its returns first, then its own */
        uint8_t o[FXU_N_ALL];
        uint32_t m = 3u;
        o[0] = 5u; o[1] = 6u; o[2] = 7u;
        for (i = 0; i < n && m < FXU_N_ALL; i++)
            if (!fx_send(u[i]))
                o[m++] = u[i];
        for (i = 0; i < m; i++)
            u[i] = o[i];
        n = m;
    }
    return n;
}
/* the chain made the list u[0..n): ORDER from its series ones (those out of the chain after them, as they were), each
 * send placed before the series one after it in the list (none after it: after them all) */
static void fx_set_list(track_t *t, const uint8_t *u, uint32_t n)
{
    trkx_t *x = fx_x(t);
    uint32_t o = 0, m = 0, i, j, k;
    for (i = 0; i < n; i++)
        if (u[i] < FXU_SER)
            o |= (uint32_t)u[i] << (3u * m++);
    for (i = 0; i < n; i++)
        if (u[i] >= FXU_SER && u[i] < FXU_COMP) {
            uint32_t before = 0, after = 0;
            for (j = 0; j < i; j++)
                before += u[j] < FXU_SER;
            for (j = i + 1u; j < n; j++)
                after |= u[j] < FXU_SER && u[j] != 4u;   /* (the SIDECHAIN, last: not a place to tap before) */
            x->tap[u[i] - FXU_SER] = (uint8_t)(after ? before + 1u : 0u);
        }
    for (i = 0; i < FXU_SER; i++) {
        uint32_t y = fx_unit_at(t, i), in = 0;
        for (j = 0; j < n; j++)
            in |= u[j] == y;
        if (!in)
            o |= y << (3u * m++);
    }
    for (k = 0; k < 120u; k++)
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
    case 3:                                             /* (in or out: four flat BELLs; Dah: a track's, BASS and TREBLE flat) */
        memset(eq_cfg(t), 0, sizeof(eqc_t));
        if (fx_mst(t))
            eq_four(t->p, eq_cfg(t), 0);
        else {
            int32_t k;
            for (k = 0; k < (int32_t)EQ_NB; k++)
                t->p[EQ_SID[k]] = 0;
            tone_adopt(t->p, eq_cfg(t));
        }
        eq_cfg(t)->f |= on ? EQF_IN : 0u;
        eq_ui_band = 0;
        break;
    case 4:                                             /* (on: the first other track, at full depth) */
        t->p[P_DKSRC] = (int16_t)(on ? (trk_index(t) ? 1 : 2) : 0);
        if (on && !t->p[P_DKAMT])
            t->p[P_DKAMT] = 100;
        break;
    case 5:
        t->p[P_CHOR] = (int16_t)(on ? 40 : 0);
        break;
    case 6:
        t->p[P_DLY] = (int16_t)(on ? 40 : 0);
        break;
    case 7:
        t->p[P_REV] = (int16_t)(on ? 50 : 0);
        break;
    default:
        fx_cset(t, on ? 64 : 0, on ? 50 : -1);
        break;
    }
}
/* (Dah) CHAIN's list for the full screen list (SELECT): its rows (+ ADD after the effects), the one picked; a row's
 * number and name; SELECT a row a detent */
static uint32_t fl_fx_rows(uint32_t *cur)
{
    uint8_t u[FXU_N_ALL];
    track_t *t = fx_tsel();
    uint32_t n = fx_list(t, u), rows = n < fx_max(t) ? n + 1u : n;
    *cur = ch_cur < rows ? ch_cur : rows ? rows - 1u : 0u;
    return rows;
}
static void fl_fx_label(uint32_t i, char *tag, char *nm)
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(fx_tsel(), u);
    if (i < n) {
        tag[0] = (char)('1' + i);
        tag[1] = 0;
        str_cpy(nm, FXU_N[u[i]], 16);
        (void)0;                                       /* (Dah: a send: "send to master" beside it, fl_fx_note) */
    } else {
        str_cpy(tag, "+", 6);
        str_cpy(nm, "ADD AN EFFECT", 16);
    }
}
static void confirm_open(uint32_t kind, uint32_t trk);
/* (Dah) the row picked moved (SELECT, KNOB 1): on + ADD, an effect to add asked (as ADD TRACK); no, back off it */
static int fx_on_add(void)                              /* (Dah) the row picked, + ADD: OCT+ asks which (no dialog) */
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(fx_tsel(), u);
    return !ui.home && cur_page()->fam == FAM_FX && cur_page()->id[0] == P_FXORD && ch_cur >= n && n < fx_max(fx_tsel());
}
static void fl_fx_at(void) { ui.force = 1; }
static uint32_t fl_pick_rows(uint32_t *cur)             /* (Dah) ADD's picker: the free effects */
{
    uint8_t f[FXU_N_ALL];
    uint32_t n = fx_free_list(fx_tsel(), f);
    *cur = fxp_cur < n ? fxp_cur : 0u;
    return n;
}
static void fl_pick_label(uint32_t i, char *tag, char *nm)
{
    uint8_t f[FXU_N_ALL];
    uint32_t n = fx_free_list(fx_tsel(), f);
    tag[0] = 0;
    str_cpy(nm, i < n ? FXU_N[f[i]] : "", 16);
}
static const char *fl_fx_note(uint32_t i)             /* (Dah) a row's note in the full list: a track's sends */
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(fx_tsel(), u);
    return i < n && fx_send(u[i]) && !fx_mst(fx_tsel()) ? "send to master" : "";
}
static void fl_fx_move(int32_t s)
{
    uint32_t cur, rows = fl_fx_rows(&cur);
    ch_cur = (uint8_t)clamp((int32_t)cur + s, 0, rows ? (int32_t)rows - 1 : 0);
    if (ch_cur != cur)                                  /* (Dah) CHAIN's rows glide to it (graph_chain) */
        ch_dy = clamp(ch_dy + ((int32_t)ch_cur > (int32_t)cur ? 26 : -26), -104, 104);
    ui.force = 1;
    fl_fx_at();
}
/* CHAIN's actions on the row picked: ADD (a free effect, last: Dah), REMOVE, TYPE (another free one in its
 * place), MOVE (in series only: the sends are after them) */
/* (Dah) the effects not in t's chain yet, that it takes: their units into f, how many */
static uint32_t fx_free_list(const track_t *t, uint8_t *f)
{
    uint32_t x, n = 0;
    for (x = 0; x < FXU_N_ALL; x++)
        if (!fx_unit_on(t, x) && fx_unit_ok(t, x))
            f[n++] = (uint8_t)x;
    return n;
}
static void fx_chain_add_unit(track_t *t, uint32_t x);
static void fx_chain_add(track_t *t)
{
    uint8_t f[FXU_N_ALL];
    uint32_t n = fx_free_list(t, f);
    if (n)
        fx_chain_add_unit(t, f[fxp_cur < n ? fxp_cur : 0u]);
}
static void fx_chain_add_unit(track_t *t, uint32_t x)
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(t, u), i;
    if (x >= FXU_N_ALL || fx_unit_on(t, x) || n >= fx_max(t))
        return;
    fx_unit_set(t, x, 1);
    u[n++] = (uint8_t)x;                                /* (Dah) at the chain's end (the COMPRESSOR, whose place is the
                                                         * sound's end, stays after it) */
    fx_set_list(t, u, n);
    n = fx_list(t, u);
    for (i = 0; i < n; i++)
        if (u[i] == x)
            ch_cur = (uint8_t)i;
}
static void fx_chain_remove(track_t *t)
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(t, u), i;
    if (ch_cur >= n)
        return;
    if (fx_ret_fixed(t, u[ch_cur])) {                   /* (Dah: the MASTER's returns stay) */
        ui_message("ALWAYS ON THE MASTER");
        return;
    }
    fx_unit_set(t, u[ch_cur], 0);
    for (i = ch_cur; i + 1u < n; i++)
        u[i] = u[i + 1u];
    fx_set_list(t, u, n - 1u);
    if (ch_cur && ch_cur >= n - 1u)
        ch_cur--;
}
static void fx_chain_type(track_t *t, int32_t dir)
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(t, u), x, k, i, old;
    if (ch_cur >= n || fx_ret_fixed(t, u[ch_cur]))
        return;
    for (old = x = u[ch_cur], k = 0; k < FXU_N_ALL; k++) {   /* the next free one that way */
        x = (x + (dir > 0 ? 1u : FXU_N_ALL - 1u)) % FXU_N_ALL;
        if (!fx_unit_on(t, x) && fx_unit_ok(t, x))
            break;
    }
    if (k == FXU_N_ALL || x == old)
        return;
    fx_unit_set(t, old, 0);
    fx_unit_set(t, x, 1);
    u[ch_cur] = (uint8_t)x;
    fx_set_list(t, u, n);
    n = fx_list(t, u);
    for (i = 0; i < n; i++)
        if (u[i] == x)
            ch_cur = (uint8_t)i;
}
static void fx_chain_move(track_t *t, int32_t dir)
{
    uint8_t u[FXU_N_ALL], a;
    uint32_t n = fx_list(t, u);
    int32_t j = (int32_t)ch_cur + (dir > 0 ? 1 : -1);
    if (ch_cur >= n || j < 0 || j >= (int32_t)n || u[ch_cur] == FXU_COMP || u[j] == FXU_COMP || u[ch_cur] == 4u ||
        u[j] == 4u || fx_ret_fixed(t, u[ch_cur]) || fx_ret_fixed(t, u[j]))
        return;                                         /* (the COMPRESSOR and the SIDECHAIN stay last) */
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
    case 3: {
        uint32_t m = eq_units(t), c = 0;
        for (; m; m &= m - 1u) c++;
        if (!c) { str_cpy(b, "FLAT", 6); break; }
        fmt_int(b, (int32_t)c); str_cpy(b + str_len(b), c > 1u ? " FILTERS" : " FILTER", 9);
        break;
    }
    case FXU_COMP:
        fmt_int(b, (int32_t)fx_camt(t) * 100 / 127);
        str_cpy(b + str_len(b), " %", 3);
        break;
    case 4: {                                           /* FROM and the source's sound */
        char nm[16];
        sound_name(&trk[clamp(t->p[P_DKSRC], 1, NTRK) - 1], nm);
        str_cpy(b, N_DKSRC[clamp(t->p[P_DKSRC], 0, 8)], 4);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), nm, 10);
        break;
    }
    default: {
        static const uint8_t ID[3] = {P_CHOR, P_DLY, P_REV};
        if (u >= FXU_COMP)
            break;
        if (fx_mst(t)) {                                /* (Dah) the MASTER's return: the project's effect */
            str_cpy(b, "ALL TRACKS", 12);
            break;
        }
        str_cpy(b, "SEND ", 6);
        fmt_int(b + 5, t->p[ID[u - 5u]] * 100 / 127);
        str_cpy(b + str_len(b), " %", 3);
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
/* CHAIN: the list, an effect a row (its number, name, what it does), the sound flowing down a line at the left; the
 * row picked outlined; + ADD after them (four at most); the sound out at the right */
/* CHAIN (Dah): the whole chain at once, filling the panel: the row picked taller (its name, what it does, framed), the
 * others as tall as the room left lets them (up to 22 px; past nine rows a window round the one picked); each its
 * number in its cushion, the sound's way down at the left; a track's sends grey with M (the MASTER's effects); + ADD */
static void graph_chain(const track_t *t, uint16_t c)
{
    uint8_t u[FXU_N_ALL];
    uint32_t n = fx_list(t, u), rows = n < fx_max(t) ? n + 1u : n;
    int32_t avail = graph_ht + GOY - 4, x0 = PANEL_X0 + 16, bw = PANEL_W - 18, ph = 26, my, vt, dy = glide(&ch_dy), i;
    if (ch_cur >= rows)
        ch_cur = (uint8_t)(rows - 1u);
    vt = tall_view((int32_t)rows, (int32_t)ch_cur, dy, avail - 1, &my, ph - 2) - 1;   /* (Dah) as SAVE > PATTERN's: the
                                                         * frame from the top to the middle, the effects by, on down */
    {   /* the sound's way down, from the first to the last */
        int32_t y0 = -vt + 6, y1 = (int32_t)rows * ph - vt - 6;
        y0 = y0 < 1 ? 1 : y0;
        y1 = y1 > avail ? avail : y1;
        if (y1 > y0)
            cv_rect(PANEL_X0 + 5, y0, 2, y1 - y0, T_RAISE);
    }
    my += 1;
    cv_rrect(x0 - 2, my, bw + 4, ph - 1, -6, T_ACCENT, T_SURF);   /* (the frame: an outline only, as the selectors') */
    cv_rrect(x0, my + 2, bw, ph - 5, -4, T_SURF, T_ACCENT);
    for (i = 0; i < (int32_t)rows; i++) {
        int32_t y = i * ph - vt, sel = y > my - ph / 2 && y < my + ph / 2;
        int32_t in = sel ? 0 : 4, by = y + 2 + (sel ? 0 : 2), bh = ph - 5 - (sel ? 0 : 4);   /* (Dah: the one in the
                                                         * frame a little bigger, its name in M; the others smaller) */
        int32_t ty = by + CAP_IN(S, bh);
        if (y + ph <= 0 || y >= avail)
            continue;
        if (y < 0 || y + ph > avail) {                  /* (Dah) cut by an edge: shown, small and faint, clipped (as the
                                                         * selectors') */
            int16_t c0 = cv_cy0, c1 = cv_cy1;
            uint16_t fc = ux_mix(T_SURF, T_MID, 50);
            cv_cy0 = (int16_t)clamp(CV_OY, 0, (int32_t)cv_h);
            cv_cy1 = (int16_t)clamp(avail + CV_OY, cv_cy0, (int32_t)cv_h);
            cv_scroll = 1;
            cv_text_on(x0 + 10, ty, &AF_S, i < (int32_t)n ? FXU_N[u[i]] : "+ ADD", fc, T_SURF);
            cv_scroll = 0;
            cv_cy0 = c0;
            cv_cy1 = c1;
            continue;
        }
        if (i < (int32_t)n) {
            char d[2] = {(char)('1' + i), 0}, inf[16];
            int32_t cy = y + (ph - 12) / 2, nx = x0 + 6;
            int ms = fx_send(u[i]) && !fx_mst(t);       /* (Dah) a send: the MASTER's effect, greyed, "M" */
            uint16_t cc = ms || fx_mst(t) ? T_MID : c;   /* (Dah: the MASTER's chain grey: no track's colour) */
            cv_rrect(PANEL_X0, cy, 12, 12, 6, sel ? T_ACCENT : cc, T_SURF);
            cv_text_in(PANEL_X0, cy + CAP_IN(S, 12), 12, &AF_S, d, T_INK, sel ? T_ACCENT : cc);
            if (!sel)                                   /* (Dah: the one picked in the outline alone, no fill) */
                cv_rrect(x0 + in, by, bw - 2 * in, bh, 4, T_RAISE, T_SURF);
            if (sel) {
                int32_t mty = by + CAP_IN(M, bh);
                if (ms)
                    nx = cv_text_on(nx, mty, &AF_M, "M", T_THEME, T_SURF) + 6;
                cv_text_on(nx, mty, &AF_M, FXU_N[u[i]], ms ? T_MID : T_TEXT, T_SURF);
            } else {
                nx += in;
                if (ms)
                    nx = cv_text_on(nx, ty, &AF_S, "M", T_THEME, T_RAISE) + 5;
                cv_text_on(nx, ty, &AF_S, FXU_N[u[i]], ms ? T_MID : T_TEXT, T_RAISE);
            }
            fx_unit_info(t, u[i], inf);
            cv_text_r(x0 + bw - 6 - in, ty, &AF_S, inf, sel ? T_TEXT : T_MID, sel ? T_SURF : T_RAISE);
        } else {                                        /* + ADD */
            int32_t bx = x0 + in, bw2 = bw - 2 * in;   /* (Dah) a dashed row, as HOME's spare one: what to do, said */
            uint16_t dc = sel ? T_TEXT : T_DIM;
            dash_rrect(bx, by, bw2, bh, dc);
            GFX_HOOK_ALIGN(0, by, 0, by + bh, AL_V, "chain add row on its line");
            cv_free_hint(bx + 8, by + CAP_IN(S, bh), "TURN [K1] TO ADD AN EFFECT", sel ? T_TEXT : T_DIM, T_SURF, bw2 - 12);
        }
    }
}
static int32_t dist_curve(const track_t *t, int32_t x)   /* fx.c track_dist's, the filters left out */
{
    int32_t d = t->p[P_DIST], g = 4096 + d * d * 2, mk = 30000 - d * 120, bias = 2400;
    if (t->p[P_DOUT])
        mk = (mk * eq_gain_q12(t->p[P_DOUT])) >> 12;     /* (OUT) */
    if (!d)
        return x;
    return mulq15(softclip((((x >> 2) * g) >> 10) + bias) - softclip(bias), mk);
}
static void graph_dist(const track_t *t, uint16_t c, int32_t pw)   /* (Dah: pw wide from the left; the panel's: the
                                                         * sound out each side) */
{
    int32_t cx = PANEL_X0 + pw / 2, cy = graph_ht / 2, a = graph_ht * 42 / 100, x, py = 0;
    if (a > pw / 2 - 2)
        a = pw / 2 - 2;
    cv_rect(PANEL_X0, cy, pw, 1, T_GRID);
    cv_rect(cx, cy - a, 1, 2 * a, T_GRID);
    cv_line_t(cx - a, cy + a, cx + a, cy - a, T_RAISE, 1);   /* clean */
    for (x = -a; x <= a; x++) {                          /* the curve: -1.5 .. +1.5 of a voice across */
        int32_t y = cy - clamp(dist_curve(t, x * 49152 / a) * a / 49152, -a, a);
        if (x > -a)
            cv_line_t(cx + x - 1, py, cx + x, y, c, 2);
        py = y;
    }
    if (pw >= (int32_t)PANEL_W) {
        fx_wave(PANEL_X0, cx - a - 6 - PANEL_X0, cy, a, ux_mix(c, T_SURF, 70));   /* the sound out, each side */
        fx_wave(cx + a + 6, PANEL_X0 + PANEL_W - cx - a - 6, cy, a, ux_mix(c, T_SURF, 70));
    }
    {                                                    /* where the sound is on the curve now */
        int32_t p = clamp(fx_peak() * 4 * a / 49152, 0, a);
        if (p > 1)
            cv_rect(cx + p - 2, cy - clamp(dist_curve(t, p * 49152 / a) * a / 49152, -a, a) - 2, 5, 5, T_ACCENT);
    }
}
/* COMP: its curve (in across, out up, -36 .. 0 dB; the threshold marked), the reduction now (a bar falling from the
 * top, ACCENT), the sound out under it */
static void graph_comp(uint32_t amt, int32_t gr, uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    int32_t x0 = PANEL_X0, w = pw >= (int32_t)PANEL_W ? 150 : pw, top = 4, bot = graph_ht - 8, x, py = 0, h = bot - top;
    int32_t thr = -(int32_t)amt * 240 / 127, keep = 32 * 256 / (32 + (int32_t)amt);   /* (dB x 8) */
    cv_rect(x0, bot, w, 1, T_GRID);
    cv_line_t(x0, bot, x0 + w, top, T_RAISE, 1);       /* (clean) */
    for (x = 0; x <= w; x++) {
        int32_t in = -288 + x * 288 / w, out = in > thr ? thr + ((in - thr) * keep >> 8) : in;
        int32_t mk = ((-thr) * (256 - keep)) >> 9, y;
        out += mk;
        y = bot - clamp((out + 288) * h / 288, 0, h);
        if (x)
            cv_line_t(x0 + x - 1, py, x0 + x, y, c, 2);
        py = y;
    }
    cv_rect(x0 + (thr + 288) * w / 288, top, 1, h, T_MID);   /* the threshold */
    if (pw >= (int32_t)PANEL_W) {                       /* the reduction now: 0 .. 18 dB (split: the band's, right) */
        int32_t gx = x0 + w + 20, gh = clamp(gr * h / (18 * 8), 0, h);
        cv_rrect(gx, top, 14, h, 3, T_RAISE, T_SURF);
        if (gh > 1)
            cv_rect(gx + 2, top, 10, gh, T_ACCENT);
        cv_text_in(gx - 8, bot + 1 - AF_S_CAP_Y, 30, &AF_S, "GR", T_MID, T_SURF);
    }
}
#include "ui_graph_eq.c"
/* the FX family's own pages (1), else 0; the sound watched while one shows */
static int graph_fxfam(const page_t *pg, const track_t *t, uint16_t c)
{
    uint32_t id = pg->id[0];
    int32_t x;
    if (pg->fam != FAM_FX || pg->scope != SC_TRACK)
        return 0;
    if (str_eq(pg->title, "COMP")) {
        if (fx_mst(t))                                  /* (the MASTER's: its curve, the panel's) */
            graph_comp(fx_camt(t), cmp_master.gr, c, PANEL_W);
        else {                                          /* (Dah) the curve at the left, the sound at the right */
            char b[12];
            int32_t gr = 0, k;
            (void)fxv_take(t, DV_COMP);
            for (k = 1; k <= 60; k++) {                 /* (the deepest of the last ~0.35 s: readable) */
                int32_t a = (int32_t)(dvr_att[(dvr_w - (uint32_t)k) % DVR_N] & 32767u);
                gr = a > gr ? a : gr;
            }
            gr = gr * 180 / 32767;                      /* (dB x 10: the ring's 0 .. 18 dB) */
            graph_comp(fx_camt(t), cmp_trk[trk_index(t) % NTRK].gr, c, FXV_LW);
            fmt_fix(b, -gr, 1);
            if (b[0] == '.' || (b[0] == '-' && b[1] == '.')) {   /* ("0.0", "-0.5": a zero first) */
                int32_t z = b[0] == '-', m = (int32_t)str_len(b);
                for (; m >= z; m--)
                    b[m + 1] = b[m];
                b[z] = '0';
            }
            x = cv_text_on(FXV_RX, 2 + CAP_IN(S, 12), &AF_S, "GR", T_ACCENT, T_SURF);
            x = cv_text_on(x + 4, 2 + CAP_IN(S, 12), &AF_S, b, T_TEXT, T_SURF);
            cv_text_on(x + 3, 2 + CAP_IN(S, 12), &AF_S, "dB", T_MID, T_SURF);
            fxv_draw(FXV_RX, FXV_RW, 16, graph_ht - 4, c, FXV_AREA);
        }
        return 1;
    }
    if (id == P_FXORD) graph_chain(t, c);
    else if (str_eq(pg->title, "DIST")) {
        if (fx_mst(t))
            graph_dist(t, c, PANEL_W);
        else {                                          /* (Dah) the curve at the left, the sound in and out at the right */
            (void)fxv_take(t, DV_DIST);
            graph_dist(t, c, FXV_LW);
            x = cv_text_on(FXV_RX, 2 + CAP_IN(S, 12), &AF_S, "IN", ux_mix(T_TEXT, T_SURF, 40), T_SURF);
            cv_text_on(x + 6, 2 + CAP_IN(S, 12), &AF_S, "OUT", c, T_SURF);
            fxv_draw(FXV_RX, FXV_RW, 16, graph_ht - 4, c, 0u);
        }
    }
    else if (id == P_EQLO) graph_eq(t, c);
    else return 0;
    return 1;
}
