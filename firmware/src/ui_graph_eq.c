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
/* |H|^2 of one unit at frequency index fi (EQ_G's), in dB x 8 */
static int32_t eq_unit_db(uint32_t unit, int32_t fidx, int32_t fi, int32_t gain)
{
    uint32_t g = EQ_G[clamp(fidx, 0, EQ_TOP)];
    int64_t w = (int64_t)(((EQ_G[clamp(fi, 0, 127)] << 14) / g) << 2), a, kw, den, num;   /* (32-bit divides only) */
    int64_t k = unit < 2u ? 92682 : 65536;              /* Butterworth's k, a bell's (Q 1), Q16 */
    if (unit == 0u)
        w = (int64_t)(0xFFFFFFFFu / (uint32_t)(w ? w : 1));   /* (a high-pass: the low-pass at 1 / w) */
    w = w < 1024 ? 1024 : w > (64 << 16) ? (64 << 16) : w;
    a = 65536 - ((w * w) >> 16);
    kw = (k * w) >> 16;
    den = a * a + kw * kw;                              /* Q32 */
    if (unit < 2u)
        return -(eq_log2q8((uint64_t)den) - 32 * 256) * 771 / 256 * 8 / 256;   /* 10 log10 (1 / den) */
    {
        int64_t ak = (kw * eq_gain_q12(gain)) >> 12;
        num = a * a + ak * ak;
        return (eq_log2q8((uint64_t)num) - eq_log2q8((uint64_t)den)) * 771 / 256 * 8 / 256;
    }
}
static int32_t eq_curve_db(const track_t *t, int32_t fi)   /* the whole EQ at frequency index fi, dB x 8 */
{
    uint32_t on = eq_units(t);
    int32_t db = 0;
    if (on & 1u) db += eq_unit_db(0, t->p[P_EQLO], fi, 0);
    if (on & 2u) db += eq_unit_db(1, t->p[P_EQHI], fi, 0);
    if (on & 4u) db += eq_unit_db(2, t->p[P_EQ1F], fi, t->p[P_EQ1G]);
    if (on & 8u) db += eq_unit_db(3, t->p[P_EQ2F], fi, t->p[P_EQ2G]);
    if (on & 16u) db += eq_unit_db(4, t->p[P_EQ3F], fi, t->p[P_EQ3G]);
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
static void graph_eq(const track_t *t, uint16_t c)
{
    int32_t top = 4, bot = graph_ht - 10, mid = (top + bot) / 2, x, i, py = 0;
    uint32_t hot = ui.hot_t ? page_id(cur_page(), ui.hot_col) : 0xFFu;
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
    for (i = 0; i < 4; i++) {                           /* the decades: 100 Hz, 1 kHz, 10 kHz */
        static const uint8_t DEC[3] = {24, 71, 117};
        if (i < 3)
            cv_rect(PANEL_X0 + DEC[i] * PANEL_W / 127, top, 1, bot - top, T_GRID);
    }
    for (i = 0; i <= 127; i++) {                        /* the curve */
        int32_t db = clamp(eq_curve_db(t, i), -144, 144), y = mid - db * (bot - top) / 2 / 144;
        x = PANEL_X0 + i * (PANEL_W - 1) / 127;
        if (i)
            cv_line_t(PANEL_X0 + (i - 1) * (PANEL_W - 1) / 127, py, x, y, c, 2);
        py = y;
    }
    {                                                   /* the units' frequencies, under the curve */
        static const uint8_t ID[5] = {P_EQLO, P_EQHI, P_EQ1F, P_EQ2F, P_EQ3F};
        uint32_t on = eq_units(t), u;
        for (u = 0; u < 5u; u++) {
            int32_t fx = PANEL_X0 + clamp(t->p[ID[u]], 0, 127) * (PANEL_W - 1) / 127;
            int h = hot == ID[u] || (u >= 2u && hot == (u == 4u ? (uint32_t)P_EQ3G : (uint32_t)ID[u] + 1u));
            cv_rect(fx - 1, bot + 3, 3, 5, h ? T_ACCENT : (on >> u) & 1u ? c : T_RAISE);
        }
    }
}
