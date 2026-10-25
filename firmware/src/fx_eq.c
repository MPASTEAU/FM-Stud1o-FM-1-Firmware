/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the EQ, a unit of the track's chain (ORDER, fx.c fx_chain / mix_spread): LOW CUT and HIGH CUT (12 dB an octave,
 * Butterworth), BAND 1 and BAND 2 (bells: FREQ, GAIN -15 .. +15 dB, Q 1). Each one a trapezoidal SVF (Zavalishin),
 * Q16 coefficients a block (EQ_G: tan(pi f / FS) of the cutoff index, 30 Hz .. 10.25 kHz); mulq16 keeps a sample's
 * full precision. A unit flat (LOW CUT 0, HIGH CUT at its top, GAIN 0) runs nothing; all four: the EQ runs nothing.
 * A bell: x + (A - 1) k bp (k bp: unity at its frequency). The track's output after the chain is watched for the EQ
 * page's spectrum (eq_watch, eq_ring). Included by fx.c */
#define EQ_TOP 118                                      /* the cutoff index at the top: 10.25 kHz (HIGH CUT: off) */
static const uint32_t EQ_G[128] = {                     /* (past EQ_TOP: the EQ page's curve only) */
    140, 147, 155, 162, 171, 179, 188, 198, 208, 219, 230, 241, 254, 266, 280, 294, 309, 325, 341, 358, 377, 396, 416,
    437, 459, 482, 507, 532, 559, 588, 617, 649, 681, 716, 752, 790, 831, 873, 917, 963, 1012, 1063, 1117, 1174, 1234,
    1296, 1362, 1431, 1503, 1580, 1660, 1744, 1832, 1925, 2023, 2125, 2233, 2347, 2466, 2591, 2722, 2860, 3005, 3158,
    3318, 3487, 3664, 3850, 4046, 4251, 4467, 4695, 4933, 5184, 5448, 5726, 6018, 6325, 6647, 6987, 7344, 7719, 8114,
    8530, 8968, 9428, 9913, 10424, 10962, 11529, 12126, 12756, 13420, 14120, 14859, 15640, 16465, 17337, 18259, 19236,
    20270, 21367, 22532, 23769, 25085, 26488, 27984, 29583, 31295, 33133, 35109, 37240, 39546, 42048, 44774, 47757,
    51037, 54664, 58700, 63225, 68342, 74185, 80938, 88851, 98281, 109752, 124068, 142526};
static const uint16_t EQ_DB_Q12[17] = {4096, 4596, 5157, 5786, 6492, 7284, 8173, 9170, 10289, 11544, 12953, 14533,
                                       16306, 18296, 20529, 23034, 25844};   /* 10 ^ (dB / 20), 0 .. 16 dB */
#define EQ_KH_BUT 46341u                                /* k / 2, Q16: Butterworth's k = 1.414 */
#define EQ_KH_BELL 32768u                               /* .. a bell's, Q 1 */
typedef struct {
    int32_t s1[4], s2[4];                               /* each filter's two states */
    uint16_t g[4], dq[4];                               /* the block's g and 1 / (1 + g (g + k)), Q16 */
    int32_t am1[2];                                     /* the bells' A - 1, Q12 */
    uint8_t on;                                         /* the filters running (a bit each: LOW HIGH BAND 1 BAND 2) */
} eq_t;
static eq_t eq[NTRK] __attribute__((section(".pool")));

/* GAIN -64 .. 63 -> the linear gain, Q12 (-15 .. +15 dB) */
static int32_t eq_gain_q12(int32_t g)
{
    int32_t d10 = (g < 0 ? -g : g) * 150 / 64, i = d10 / 10, a;
    a = EQ_DB_Q12[i] + (EQ_DB_Q12[i + 1] - EQ_DB_Q12[i]) * (d10 % 10) / 10;
    return g < 0 ? (4096 * 4096) / a : a;
}
static void eq_coef(eq_t *s, uint32_t f, int32_t idx, uint32_t kh)
{
    uint32_t g = EQ_G[clamp(idx, 0, EQ_TOP)];          /* (< 65536: mulq16's) */
    uint32_t den = 65536u + ((g * ((g + 2u * kh) >> 4)) >> 12);   /* (32 bits: g < 2^16, the sum >> 4 < 2^14) */
    s->g[f] = (uint16_t)g;
    s->dq[f] = (uint16_t)(0xFFFF0000u / den);            /* (65535 x 65536 / den: no 64-bit divide on the target) */
}
/* which units of the EQ do something (eq_t on's bits) */
static uint32_t eq_units(const track_t *t)
{
    return (t->p[P_EQLO] > 0) | (t->p[P_EQHI] < EQ_TOP) << 1 | (t->p[P_EQ1G] != 0) << 2 | (t->p[P_EQ2G] != 0) << 3;
}
/* one SVF step: x in, its high, band and low outputs */
static inline void eq_svf(eq_t *s, uint32_t f, int32_t x, uint32_t kh, int32_t *hp, int32_t *bp, int32_t *lp)
{
    int32_t h = mulq16(x - (mulq16(s->s1[f], kh) << 1) - mulq16(s->s1[f], s->g[f]) - s->s2[f], s->dq[f]);
    int32_t v = mulq16(h, s->g[f]), b = v + s->s1[f], l;
    s->s1[f] = b + v;
    v = mulq16(b, s->g[f]);
    l = v + s->s2[f];
    s->s2[f] = l + v;
    *hp = h; *bp = b; *lp = l;
}
static __attribute__((noinline)) void track_eq(track_t *t, int32_t *b, uint32_t n)
{
    eq_t *s = &eq[(uint32_t)(t - trk) % NTRK];
    uint32_t on = eq_units(t), i, f;
    if (!on) {
        if (s->on)
            memset(s, 0, sizeof *s);                    /* (flat again: from rest next time) */
        return;
    }
    for (f = 0; f < 4u; f++)
        if (((on & ~s->on) >> f) & 1u)
            s->s1[f] = s->s2[f] = 0;                    /* a unit starting: from rest */
    s->on = (uint8_t)on;
    if (on & 1u) eq_coef(s, 0, t->p[P_EQLO], EQ_KH_BUT);
    if (on & 2u) eq_coef(s, 1, t->p[P_EQHI], EQ_KH_BUT);
    if (on & 4u) { eq_coef(s, 2, t->p[P_EQ1F], EQ_KH_BELL); s->am1[0] = eq_gain_q12(t->p[P_EQ1G]) - 4096; }
    if (on & 8u) { eq_coef(s, 3, t->p[P_EQ2F], EQ_KH_BELL); s->am1[1] = eq_gain_q12(t->p[P_EQ2G]) - 4096; }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -3000000, 3000000), h, bp, l;
        if (on & 1u) { eq_svf(s, 0, x, EQ_KH_BUT, &h, &bp, &l); x = h; }
        if (on & 2u) { eq_svf(s, 1, x, EQ_KH_BUT, &h, &bp, &l); x = l; }
        for (f = 0; f < 2u; f++)
            if ((on >> (2u + f)) & 1u) {
                int32_t a = s->am1[f], kb;
                eq_svf(s, 2u + f, x, EQ_KH_BELL, &h, &bp, &l);
                kb = bp;                                /* (k = 1: k bp) */
                x += a >= 0 ? mulq16(kb, (uint32_t)a) << 4 : -(mulq16(kb, (uint32_t)-a) << 4);
            }
        b[i] = x;
    }
}

/* the EQ page's spectrum: the watched track's output after its chain (fx.c mix_part / mix_spread), its last EQ_RN
 * samples (a quarter, 16 bits); eq_wn counts the blocks written (the page redraws on a new one) */
#define EQ_RN 256u
static int16_t eq_ring[EQ_RN] __attribute__((section(".pool")));
static volatile uint16_t eq_wp;
static volatile uint32_t eq_wn;
static volatile uint8_t eq_watch;                       /* the track watched + 1, 0 none (ui_graph_eq.c) */
static __attribute__((noinline)) void eq_tap_put(const int32_t *b, uint32_t n);
static inline void eq_tap(const track_t *t, const int32_t *b, uint32_t n)
{
    if (eq_watch == (uint32_t)(t - trk) + 1u)
        eq_tap_put(b, n);                               /* (off the ISR's inlined path) */
}
static __attribute__((noinline)) void eq_tap_put(const int32_t *b, uint32_t n)
{
    uint32_t i, w = eq_wp;
    for (i = 0; i < n; i++, w = (w + 1u) & (EQ_RN - 1u))
        eq_ring[w] = (int16_t)clamp(b[i] >> 3, -32767, 32767);
    eq_wp = (uint16_t)w;
    eq_wn++;
}
