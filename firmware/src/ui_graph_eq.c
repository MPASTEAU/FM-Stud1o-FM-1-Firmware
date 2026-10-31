/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the EQ pages' panel (fx_eq.c): the track's sound live, its spectrum (30 Hz .. 16 kHz, log, 0 .. -60 dB) as faint
 * bars falling slowly, its waveform a thin line across the top; over them the EQ's curve (+-18 dB, the 0 dB line) in
 * the colour, each unit's frequency marked (the knob turned last in ACCENT). The curve from the filters' own response
 * (the SVF is the analogue one at a warped frequency: s = j tan(pi f / FS) / g), in integers: the log of |H|^2.
 * The spectrum: a 256-point FFT (Hann) of eq_ring, a frame at a time. Integers only. Included by ui_graph_more.c */
#define EQS_N 256u
#define EQS_LOG 8u
#define eqs_re ui_tmp                                   /* (Dah: fx.c's scratch, the frame's) */
#define eqs_im (ui_tmp + EQS_N)
_Static_assert(2u * EQS_N <= SCOPE_N, "the FFT in ui_tmp");
static int8_t eqs_bar[PANEL_W] __attribute__((section(".pool")));                         /* each column's level shown, dB x 2 (0 .. -120) */
static uint32_t eqs_wn;                                 /* eq_wn when last taken */
static uint8_t eqs_init;

/* log2 of x (> 0) in Q8 */
static int32_t eq_log2q8(uint64_t x)
{
    int32_t e = 63, f;
    if (!x)
        return -32768;
    while (!(x >> e))
        e--;
    f = (int32_t)((e >= 8 ? x >> (e - 8) : x << (8 - e)) & 255u);   /* the mantissa's next 8 bits */
    return e * 256 + f + ((f * (256 - f)) >> 9);        /* (log2 (1 + m) ~ m + m (1 - m) / 2.7) */
}
/* (Dah) |N / D|^2 in dB x 8: N = nr + j ni, D = dr + j di (Q16) */
static int32_t eq_q2db(int64_t nr, int64_t ni, int64_t dr, int64_t di)
{
    return (eq_log2q8((uint64_t)(nr * nr) + (uint64_t)(ni * ni)) - eq_log2q8((uint64_t)(dr * dr) + (uint64_t)(di * di))) *
           771 / 256 * 8 / 256;                         /* (10 log10 2 = 771 / 256) */
}
/* |H|^2 of a band of kind kd (eq_plan's), FREQ fidx, GAIN v, at frequency index fi (EQ_G's), in dB x 8; the analogue
 * prototype at s = j w, w = tan(pi f / FS) / g (the SVF's own warping) */
static int32_t eq_kind_db(uint32_t kd, int32_t fidx, int32_t v, int32_t fi)
{
    uint32_t g = eq_band_g(kd, fidx, v);
    int64_t w = (int64_t)(((EQ_G[clamp(fi, 0, 127)] << 14) / g) << 2), w2, a, kw, a2, aa;   /* (32-bit divides only) */
    if (kd <= EQK_HP24)
        w = (int64_t)(0xFFFFFFFFu / (uint32_t)(w ? w : 1));   /* (a high pass: the low pass at 1 / w) */
    w = w < 1024 ? 1024 : w > (64 << 16) ? (64 << 16) : w;
    w2 = (w * w) >> 16;
    a = 65536 - w2;
    kw = (92682 * w) >> 16;                             /* (Butterworth's k w) */
    a2 = (int64_t)eq_gain_q12(v) << 4;                  /* A^2, A, Q16 */
    aa = (int64_t)eq_db_q12(v * 1500 / 64 / 2) << 4;
    switch (kd) {
    case EQK_HP6: case EQK_LP6: return -(eq_log2q8((uint64_t)((1LL << 32) + w * w)) - 32 * 256) * 771 / 256 * 8 / 256;
    case EQK_HP12: case EQK_LP12: return eq_q2db(65536, 0, a, kw);
    case EQK_HP24: case EQK_LP24: return eq_q2db(65536, 0, a, (50160 * w) >> 16) + eq_q2db(65536, 0, a, (121094 * w) >> 16);
    case EQK_LS: return eq_q2db(a2 - w2, (kw * aa) >> 16, a, kw);   /* (s^2 + k A s + A^2) / (s^2 + k s + 1) */
    case EQK_HS: return eq_q2db(65536 - ((a2 * w2) >> 16), (kw * aa) >> 16, a, kw);   /* (A^2 s^2 + k A s + 1) / .. */
    default: return eq_q2db(a, (w * a2) >> 16, a, w);   /* a bell: (s^2 + A^2 s + 1) / (s^2 + s + 1) */
    }
}
/* (Dah) a track's BASS (k 0) or TREBLE (1): the one-pole shelf's |H|^2 (fx_eq.c track_eq), dB x 8, at frequency index
 * fi: (A + s) / (1 + s), (1 + A s) / (1 + s), s = j w, w = tan(pi f / FS) / tan(pi fc / FS) */
static int32_t tone_db(const track_t *t, uint32_t k, int32_t fi)
{
    int32_t v = t->p[EQ_SID[k]];
    int64_t w = (int64_t)(((EQ_G[clamp(fi, 0, 127)] << 14) / EQ_G[clamp(t->p[EQ_FID[k]], 0, EQ_TOP)]) << 2);
    int64_t a = (int64_t)eq_gain_q12(v) << 4;           /* (A, Q16) */
    if (!v)
        return 0;
    w = w < 64 ? 64 : w > (64 << 16) ? (64 << 16) : w;
    return k ? eq_q2db(65536, (a * w) >> 16, 65536, w) : eq_q2db(a, w, 65536, w);
}
/* the whole EQ at frequency index fi, dB x 8 (only band `one`, < EQ_NB) */
static int32_t eq_curve_db(const track_t *t, int32_t fi, uint32_t one)
{
    eqc_t c;
    uint8_t kd[EQ_NB], bk[EQ_NB], st[EQ_NB];
    uint32_t nb, k;
    int32_t db = 0;
    if (eq_trk(t))
        return (one >= EQ_NB || one == 0u ? tone_db(t, 0, fi) : 0) + (one >= EQ_NB || one == 1u ? tone_db(t, 1, fi) : 0);
    eq_get(t, &c);
    nb = eq_plan(t, &c, kd, bk, st);
    for (k = 0; k < nb; k++)
        if (one >= EQ_NB || bk[k] == one) {
            uint32_t j = eq_slot(&c, bk[k]);
            db += eq_kind_db(kd[k], t->p[EQ_FID[j]], t->p[EQ_SID[j]], fi);
        }
    return db;
}

/* the FFT of eqs_re / eqs_im in place (radix 2, a halving each stage: X / N) */
static void eqs_fft(void)
{
    uint32_t i, j, s, m, k;
    for (i = 1, j = 0; i < EQS_N; i++) {                /* bit reversal */
        uint32_t b = EQS_N >> 1;
        for (; j & b; b >>= 1)
            j ^= b;
        j |= b;
        if (i < j) {
            int16_t r = eqs_re[i], q = eqs_im[i];
            eqs_re[i] = eqs_re[j]; eqs_im[i] = eqs_im[j];
            eqs_re[j] = r; eqs_im[j] = q;
        }
    }
    for (s = 1; s <= EQS_LOG; s++) {
        m = 1u << s;
        for (k = 0; k < m / 2u; k++) {
            uint32_t ph = k * (1024u / m);              /* SINE's index of -2 pi k / m */
            int32_t wr = SINE[(ph + 256u) & 1023u], wi = -SINE[ph & 1023u];
            for (i = k; i < EQS_N; i += m) {
                int32_t xr = eqs_re[i + m / 2u], xi = eqs_im[i + m / 2u];
                int32_t tr = (int32_t)(((int64_t)xr * wr - (int64_t)xi * wi) >> 15), ti = (int32_t)(((int64_t)xr * wi + (int64_t)xi * wr) >> 15);
                int32_t ur = eqs_re[i], ui_ = eqs_im[i];
                eqs_re[i] = (int16_t)((ur + tr) >> 1); eqs_im[i] = (int16_t)((ui_ + ti) >> 1);
                eqs_re[i + m / 2u] = (int16_t)((ur - tr) >> 1); eqs_im[i + m / 2u] = (int16_t)((ui_ - ti) >> 1);
            }
        }
    }
}
/* the spectrum now into eqs_bar (each column the loudest bin under it; falling 1.5 dB a frame at most) */
static void eqs_take(void)
{
    uint32_t i, w = eq_wp, x;
    for (i = 0; i < EQS_N; i++) {
        int32_t hann = (32768 - SINE[(i * (1024u / EQS_N) + 256u) & 1023u]) >> 1;   /* (1 - cos) / 2, Q15 */
        eqs_re[i] = (int16_t)((eq_ring[(w + EQ_RN - EQS_N + i) & (EQ_RN - 1u)] * hann) >> 15);
        eqs_im[i] = 0;
    }
    eqs_fft();
    {
        /* (Dah) each column's level, dB x 8: in the FFT's upper halves, unread past it (ui_tmp's) */
#define EQS_COL(x) ui_tmp[(x) < 128 ? 128 + (x) : 384 + (x) - 128]
        _Static_assert(PANEL_W <= 256u, "the columns in the FFT's upper halves");
        for (x = 0; x < PANEL_W; x++) {
            uint32_t f0 = CUTOFF_HZ[x * 127u / PANEL_W], f1 = CUTOFF_HZ[(x + 1u) * 127u / PANEL_W];
            uint32_t b0 = f0 * EQS_N / FS, b1 = f1 * EQS_N / FS, b;
            int64_t best = 0;
            int32_t db;
            if (b1 <= b0 + 1u) {                        /* (Dah) below a bin a column: each bin round it a bell
                                                         * (a gaussian, ~half a bin wide) on its frequency, summed */
                static const uint16_t GW[36] = {32767, 32512, 31759, 30542, 28917, 26953, 24734, 22345, 19874, 17402,
                    15002, 12732, 10638, 8750, 7086, 5650, 4435, 3427, 2607, 1952, 1440, 1045, 747, 526, 364, 248, 167,
                    110, 72, 46, 29, 18, 11, 7, 4, 2};   /* exp(-(d / 16)^2 / 0.5), Q15: d in 1/16 bins */
                int32_t q = (int32_t)((f0 + f1) / 2u * EQS_N * 16u / FS), ib = q >> 4, k;
                int64_t acc = 0;
                for (k = ib - 2; k <= ib + 3; k++) {
                    int32_t d = q - k * 16;
                    d = d < 0 ? -d : d;
                    if (k >= 1 && k < (int32_t)(EQS_N / 2u) && d < 36)
                        acc += (((int64_t)eqs_re[k] * eqs_re[k] + (int64_t)eqs_im[k] * eqs_im[k]) * GW[d]) >> 15;
                }
                db = acc ? (eq_log2q8((uint64_t)acc) - 26 * 256) * 771 / 256 * 8 / 256 : -480;
            } else {
                for (b = b0; b < b1 && b < EQS_N / 2u; b++) {
                    int64_t p = (int64_t)eqs_re[b] * eqs_re[b] + (int64_t)eqs_im[b] * eqs_im[b];
                    if (b && p > best)
                        best = p;
                }
                db = best ? (eq_log2q8((uint64_t)best) - 26 * 256) * 771 / 256 * 8 / 256 : -480;   /* (a full-scale
                                                         * sine of the ring: ~ 2^26 a bin's power: 0 dB) */
            }
            EQS_COL(x) = (int16_t)clamp(db, -480, 0);
        }
        for (x = 0; x < PANEL_W; x++) {                 /* (Dah: smoothed, five columns) */
            int32_t a = x > 1 ? x - 2 : 0, z = x + 2 < PANEL_W ? x + 2 : PANEL_W - 1, i, s = 0, db;
            for (i = a; i <= z; i++)
                s += EQS_COL(i);
            db = s / (z - a + 1) / 4;
            eqs_bar[x] = (int8_t)(db > eqs_bar[x] - 3 ? db : eqs_bar[x] - 3);
        }
    }
}
/* (Dah) the EQ page: the filter picked (KNOB 1), its knobs' ids (KNOB 1, 2: 0xFF) */
static uint32_t eq_pick(const eqc_t *e) { return eq_ui_band < e->n ? eq_ui_band : e->n ? e->n - 1u : 0u; }
static uint32_t eq_ui_id(uint32_t c)
{
    eqc_t e;
    eq_get(fx_tsel(), &e);
    return c < 2u || !e.n ? 0xFFu : (c == 2u ? EQ_FID : EQ_SID)[eq_slot(&e, eq_pick(&e))];
}
static void graph_eq(const track_t *t, uint16_t c)
{
    int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, top = 4, bot = full - 6, mid = (top + bot) / 2, x, i;
    int32_t cy[128];
    eqc_t e;
    uint32_t sel, k, on = eq_units(t);
    eq_get(t, &e);
    sel = eq_pick(&e);
    cv_oy = 0;                                          /* (the panel's height, every pass's) */
    if (!eqs_init) {                                    /* (silence to start with) */
        for (x = 0; x < PANEL_W; x++)
            eqs_bar[x] = -120;
        eqs_init = 1;
    }
    if (eq_wn != eqs_wn) {
        eqs_wn = eq_wn;
        eqs_take();
    } else
        for (x = 0; x < PANEL_W; x++)                   /* silent: the bars fall */
            eqs_bar[x] = (int8_t)(eqs_bar[x] > -120 + 3 ? eqs_bar[x] - 3 : -120);
    for (x = 0; x < PANEL_W; x++) {                     /* the spectrum, its top a line (Dah) */
        int32_t h = (120 + eqs_bar[x]) * (bot - top) / 120;
        if (h > 0) {
            cv_rect(PANEL_X0 + x, bot - h, 1, h, ux_mix(c, T_SURF, 72));
            cv_rect(PANEL_X0 + x, bot - h, 1, 1, ux_mix(c, T_SURF, 40));
        }
    }
    fx_wave_line(top, c);                               /* the waveform, thin, across the top */
    cv_rect(PANEL_X0, mid, PANEL_W, 1, T_RAISE);        /* 0 dB */
    for (i = 0; i < 3; i++) {                           /* the decades: 100 Hz, 1 kHz, 10 kHz */
        static const uint8_t DEC[3] = {24, 71, 117};
        cv_rect(PANEL_X0 + DEC[i] * PANEL_W / 127, top, 1, bot - top, T_GRID);
    }
    for (i = 0; i <= 127; i++) {                        /* the curve: every filter */
        int32_t db = clamp(eq_curve_db(t, i, EQ_NB), -144, 144);
        cy[i] = mid - db * (bot - top) / 2 / 144;
        x = PANEL_X0 + i * (PANEL_W - 1) / 127;
        if (i)
            cv_line_t(PANEL_X0 + (i - 1) * (PANEL_W - 1) / 127, cy[i - 1], x, cy[i], c, 2);
    }
    for (k = 0; e.n && k <= e.n; k++) {                 /* each filter a dot on the curve at its frequency (the picked
                                                         * one last, larger, ACCENT), its number over it */
        uint32_t b = k < e.n ? k : sel, j = eq_slot(&e, b);
        int32_t f = clamp(t->p[EQ_FID[j]], 0, 127), fx = PANEL_X0 + f * (PANEL_W - 1) / 127, r = b == sel ? 4 : 2;
        int32_t fy = clamp(cy[f], top + r + 1, bot - r - 1);
        char nm[2] = {(char)('1' + b), 0};
        if (k < e.n && k == sel)
            continue;
        cv_rrect(fx - r - 1, fy - r - 1, 2 * r + 3, 2 * r + 3, r + 1, T_SURF, T_SURF);
        cv_rrect(fx - r, fy - r, 2 * r + 1, 2 * r + 1, r, b == sel ? T_ACCENT : (on >> b) & 1u ? T_TEXT : T_MID, T_SURF);
        x = clamp(fx - 3, PANEL_X0, PANEL_X0 + PANEL_W - 8);
        cv_text_on(x, fy - r - 3 - AF_S_CAP_H - AF_S_CAP_Y >= top ? fy - r - 3 - AF_S_CAP_H - AF_S_CAP_Y : fy + r + 3 - AF_S_CAP_Y,
                   &AF_S, nm, b == sel ? T_ACCENT : T_MID, T_SURF);
    }
}
/* (Dah) the EQ page's keys (ui_input.c edit_param): t's four filters, set */
static eqc_t *eq_own(track_t *t)
{
    eqc_t *e = eq_cfg(t);
    if (eq_trk(t))
        tone_adopt(t->p, e);                            /* (Dah: a track's BASS and TREBLE) */
    else if (!(e->f & EQF_4))
        eq_four(t->p, e, 0);
    return e;
}
static void eq_set(track_t *t, uint32_t id, int32_t v)
{
    t->p[id] = (int16_t)v;
    if (!fx_mst(t))
        (void)motion_capture(t, id, t->p[id]);
}
/* KNOB 1 the filter (1..4), 2 its TYPE, 4 a cut's SLOPE (a GAIN: as any parameter). 1 done */
static int eq_knob(uint32_t slot, int32_t steps)
{
    track_t *t = fx_tsel();
    eqc_t *e = eq_own(t);
    uint32_t k = eq_pick(e), j = eq_slot(e, k), y = eq_type(e, k);
    int32_t d = steps > 0 ? 1 : -1, v = t->p[EQ_SID[j]];
    if (slot == 0u)
        eq_ui_band = (uint8_t)clamp((int32_t)k + d, 0, eq_trk(t) ? 1 : EQ_NB - 1);
    else if (slot == 1u && eq_trk(t))
        return 1;                                       /* (Dah: a track's: BASS a low shelf, TREBLE a high one) */
    else if (slot == 1u) {
        uint32_t ny = (uint32_t)clamp((int32_t)y + d, 0, EQT_N - 1);
        if (ny == y)
            return 1;
        e->t[j] = (uint8_t)ny;
        if ((ny <= EQT_LP) != (y <= EQT_LP))            /* (a cut now 12 dB; a shelf, a bell flat) */
            eq_set(t, EQ_SID[j], 0);
    } else if (slot == 3u && y <= EQT_LP) {
        int32_t s = (int32_t)eq_slope(v) + d;
        if (s > 2 || s < 0)
            return 1;
        if (s == 2 && eq_stages(t, e) >= EQ_NS)
            ui_message("EQ FULL: NO 24 dB");
        else
            eq_set(t, EQ_SID[j], s - 1);
    } else
        return 0;
    ui.force = 1;
    return 1;
}
