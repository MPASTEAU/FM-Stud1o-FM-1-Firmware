/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the EQ, a unit of the track's chain (ORDER, fx.c fx_chain / mix_spread): up to four bands (eqc_t: trkx's, the
 * MASTER's eq_mc), each a TYPE in a slot's two parameters (EQ_FID EQ_SID: its FREQ, its SLOPE or GAIN): HIGH PASS and
 * LOW PASS (6, 12 or 24 dB an octave, Butterworth), LOW SHELF and HIGH SHELF (Q 0.71), BELL (Q 1); GAIN -15 .. +15 dB.
 * Five filter stages at most, as the five filters before bands (a 24 dB cut takes two, any other band one: eq_plan).
 * Each a trapezoidal SVF (Zavalishin; a 6 dB cut a one-pole), Q16 coefficients a block (EQ_G: tan(pi f / FS) of the
 * cutoff index, 30 Hz .. 10.25 kHz); mulq16 keeps a sample's full precision. A band at rest (a HIGH PASS at its bottom,
 * a LOW PASS at its top, a GAIN of 0) runs nothing; all of them: the EQ runs nothing (out of the chain).
 * A shelf: x + k (A - 1) bp + (A^2 - 1) lp at g / sqrt A (the HIGH SHELF: A^2 times the low one of 1 / A, at g sqrt A);
 * a bell: x + (A^2 - 1) k bp (k bp: unity at its frequency). The track's output after the chain is watched for the EQ
 * page's spectrum (eq_watch, eq_ring). Included by fx.c */
#define EQ_TOP 118                                      /* the cutoff index at the top: 10.25 kHz (a LOW PASS: at rest) */
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
#define EQ_KH_4A 25080u                                 /* .. a 24 dB cut's two stages (Q 1.31, 0.54) */
#define EQ_KH_4B 60547u
enum { EQT_HP, EQT_LP, EQT_LS, EQT_HS, EQT_BELL, EQT_N };   /* a band's TYPE */
enum { EQK_HP6, EQK_HP12, EQK_HP24, EQK_LP6, EQK_LP12, EQK_LP24, EQK_LS, EQK_HS, EQK_BELL };   /* .. as it runs */
#define EQ_NB 4u                                        /* bands at most (the parameters' four slots) */
#define EQ_NS 5u                                        /* filter stages at most */
static const uint8_t EQ_FID[EQ_NB] = {P_EQLO, P_EQHI, P_EQ1F, P_EQ2F};   /* each slot's FREQ */
static const uint8_t EQ_SID[EQ_NB] = {P_EQ3G, P_EQ3F, P_EQ1G, P_EQ2G};   /* .. its GAIN, a cut's SLOPE (< 0 6, 0 12,
                                                         * > 0 24 dB) */
typedef struct {
    int32_t s1[EQ_NS], s2[EQ_NS];                       /* each stage's two states */
    uint16_t g[EQ_NS], dq[EQ_NS];                       /* the block's g and 1 / (1 + g (g + k)), Q16 (a one-pole: g / (1 + g)) */
    uint8_t own[EQ_NS];                                 /* what each stage ran (another band, kind: from rest) */
    int32_t m1[EQ_NB], m2[EQ_NB], mg[EQ_NB];            /* each band's mix: bp, lp, out (a shelf's, a bell's), Q12 */
    uint8_t on;
} eq_t;
static eq_t eq[NTRK] __attribute__((section(".pool")));
static eq_t eq_m[2];                                    /* (the MASTER's: left, right) */
static eqc_t eq_mc;                                     /* (the MASTER's bands: projfs.c keeps them) */

static eqc_t *eq_cfg(const track_t *t) { return t >= trk && t < trk + NTRK ? &trkx[t - trk].eq : &eq_mc; }
static uint32_t eq_slot(const eqc_t *c, uint32_t k) { return (c->o >> (2u * (k & 3u))) & 3u; }
static uint32_t eq_type(const eqc_t *c, uint32_t k) { uint32_t y = c->t[eq_slot(c, k)]; return y < EQT_N ? y : EQT_BELL; }
static uint32_t eq_slope(int32_t v) { return v < 0 ? 0u : v > 0 ? 2u : 1u; }   /* 6 12 24 dB */
/* t's bands (none yet, as at power-on: the two cuts, a HIGH PASS and a LOW PASS) */
static void eq_get(const track_t *t, eqc_t *c)
{
    *c = *eq_cfg(t);
    if (!c->n || c->n > EQ_NB) {
        c->n = 2;
        c->o = 1u << 2;
        c->t[0] = EQT_HP; c->t[1] = EQT_LP; c->t[2] = c->t[3] = EQT_BELL;
    }
}
/* the layout before bands (eqc_t n 0: LOW CUT, HIGH CUT, the bells 1 2 3 in p's raw values) as bands: the cuts a HIGH
 * PASS and a LOW PASS (12 dB), each bell not at 0 dB a BELL (a third only where a slot is free). p may be raw */
static void eq_adopt(int16_t *p, const int16_t *raw, eqc_t *c)
{
    int32_t lo = raw[P_EQLO], hi = raw[P_EQHI], b[3][2] = {{raw[P_EQ1F], raw[P_EQ1G]}, {raw[P_EQ2F], raw[P_EQ2G]},
                                                           {raw[P_EQ3F], raw[P_EQ3G]}};
    uint32_t k, j = 2;
    c->n = 2;
    c->o = 1u << 2;
    c->t[0] = EQT_HP; c->t[1] = EQT_LP; c->t[2] = c->t[3] = EQT_BELL;
    p[P_EQLO] = (int16_t)clamp(lo, 0, EQ_TOP);
    p[P_EQHI] = (int16_t)clamp(hi, 0, EQ_TOP);
    p[P_EQ3G] = p[P_EQ3F] = 0;                          /* (12 dB) */
    p[P_EQ1F] = 40; p[P_EQ2F] = 90; p[P_EQ1G] = p[P_EQ2G] = 0;
    for (k = 0; k < 3u && j < EQ_NB; k++)
        if (b[k][1]) {
            p[EQ_FID[j]] = (int16_t)clamp(b[k][0], 0, EQ_TOP);
            p[EQ_SID[j]] = (int16_t)clamp(b[k][1], -64, 63);
            c->o = (uint8_t)(c->o | j << (2u * c->n));
            c->n++;
            j++;
        }
}
/* band k doing something (at rest: nothing) */
static int eq_band_on(const track_t *t, const eqc_t *c, uint32_t k)
{
    uint32_t j = eq_slot(c, k), y = eq_type(c, k);
    int32_t f = t->p[EQ_FID[j]];
    return y == EQT_HP ? f > 0 : y == EQT_LP ? f < EQ_TOP : t->p[EQ_SID[j]] != 0;
}
static uint32_t eq_units(const track_t *t)              /* the bands doing something, a bit each (none: out of the chain) */
{
    eqc_t c;
    uint32_t k, m = 0;
    eq_get(t, &c);
    for (k = 0; k < c.n; k++)
        m |= (uint32_t)eq_band_on(t, &c, k) << k;
    return m;
}
static uint32_t eq_need(const track_t *t, const eqc_t *c, uint32_t k)   /* the stages band k takes */
{
    uint32_t y = eq_type(c, k);
    return y <= EQT_LP && eq_slope(t->p[EQ_SID[eq_slot(c, k)]]) == 2u ? 2u : 1u;
}
static uint32_t eq_stages(const track_t *t, const eqc_t *c)   /* .. all of them (at rest too: what the knobs allow) */
{
    uint32_t k, s = 0;
    for (k = 0; k < c->n; k++)
        s += eq_need(t, c, k);
    return s;
}
/* the bands that run, in order: each one's kind (EQK_*), band, first stage; returns how many (every band doing
 * something, a stage each; a 24 dB cut its second while one is left (EQ_NS), else at 12) */
static uint32_t eq_plan(const track_t *t, const eqc_t *c, uint8_t *kd, uint8_t *bk, uint8_t *st)
{
    uint32_t k, nb = 0, ns = 0, spare = EQ_NS;
    for (k = 0; k < c->n; k++)
        spare -= (uint32_t)eq_band_on(t, c, k);
    for (k = 0; k < c->n; k++) {
        uint32_t y = eq_type(c, k), d;
        if (!eq_band_on(t, c, k))
            continue;
        if (y <= EQT_LP) {
            d = eq_slope(t->p[EQ_SID[eq_slot(c, k)]]);
            if (d == 2u && !spare)
                d = 1u;
            spare -= d == 2u;
            kd[nb] = (uint8_t)(y * 3u + d);
        } else
            kd[nb] = (uint8_t)(EQK_LS + y - EQT_LS);
        bk[nb] = (uint8_t)k;
        st[nb++] = (uint8_t)ns;
        ns += kd[nb - 1u] == EQK_HP24 || kd[nb - 1u] == EQK_LP24 ? 2u : 1u;
    }
    return nb;
}
/* GAIN -64 .. 63 -> the linear gain, Q12 (-15 .. +15 dB) */
static int32_t eq_gain_q12(int32_t g)
{
    int32_t d10 = (g < 0 ? -g : g) * 150 / 64, i = d10 / 10, a;
    a = EQ_DB_Q12[i] + (EQ_DB_Q12[i + 1] - EQ_DB_Q12[i]) * (d10 % 10) / 10;
    return g < 0 ? (4096 * 4096) / a : a;
}
static int32_t eq_db_q12(int32_t d)                     /* d (a hundredth of a dB, -16 .. +16 dB) -> 10 ^ (d / 20), Q12 */
{
    int32_t a = clamp(d < 0 ? -d : d, 0, 1599), i = a / 100;
    a = EQ_DB_Q12[i] + (EQ_DB_Q12[i + 1] - EQ_DB_Q12[i]) * (a % 100) / 100;
    return d < 0 ? (4096 * 4096) / a : a;
}
/* a band's g: a shelf's moved by sqrt A (A = 10 ^ (dB / 40)), within mulq16's 16 bits */
static uint32_t eq_band_g(uint32_t kd, int32_t fidx, int32_t v)
{
    uint32_t g = EQ_G[clamp(fidx, 0, EQ_TOP)];
    if (kd == EQK_LS || kd == EQK_HS) {
        uint32_t sa = (uint32_t)eq_db_q12(v * 1500 / 64 / 4);
        g = kd == EQK_LS ? g * 4096u / sa : g * sa / 4096u;
    }
    return g < 1u ? 1u : g > 65535u ? 65535u : g;
}
static void eq_coef(eq_t *s, uint32_t f, uint32_t g, uint32_t kh)
{
    uint32_t den = 65536u + ((g * ((g + 2u * kh) >> 4)) >> 12);   /* (32 bits: g < 2^16, the sum >> 4 < 2^14) */
    s->g[f] = (uint16_t)g;
    s->dq[f] = (uint16_t)(0xFFFF0000u / den);            /* (65535 x 65536 / den: no 64-bit divide on the target) */
}
static inline int32_t eq_mq(int32_t v, int32_t m)       /* v m, m Q12 signed */
{
    return m >= 0 ? mulq16(v, (uint32_t)m) << 4 : -(mulq16(v, (uint32_t)-m) << 4);
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
static void eq_run(eq_t *s, const track_t *t, int32_t *b, uint32_t n)
{
    eqc_t c;
    uint8_t kd[EQ_NB], bk[EQ_NB], st[EQ_NB], own[EQ_NS] = {0};
    uint32_t nb, i, k, f;
    eq_get(t, &c);
    nb = eq_plan(t, &c, kd, bk, st);
    if (!nb) {
        if (s->on)
            memset(s, 0, sizeof *s);                    /* (flat again: from rest next time) */
        return;
    }
    for (k = 0; k < nb; k++) {                          /* each band's coefficients */
        uint32_t j = eq_slot(&c, bk[k]), g = eq_band_g(kd[k], t->p[EQ_FID[j]], t->p[EQ_SID[j]]), x = kd[k];
        int32_t v = t->p[EQ_SID[j]], a2 = eq_gain_q12(v), a = eq_db_q12(v * 1500 / 64 / 2);
        f = st[k];
        own[f] = (uint8_t)(1u + j * 32u + x * 2u);
        if (x == EQK_HP6 || x == EQK_LP6)
            s->g[f] = (uint16_t)((g << 15) / ((65536u + g) >> 1));   /* (g / (1 + g)) */
        else if (x == EQK_HP24 || x == EQK_LP24) {
            eq_coef(s, f, g, EQ_KH_4A);
            eq_coef(s, f + 1u, g, EQ_KH_4B);
            own[f + 1u] = (uint8_t)(own[f] + 1u);
        } else
            eq_coef(s, f, g, x == EQK_BELL ? EQ_KH_BELL : EQ_KH_BUT);
        if (x == EQK_LS) {
            s->m1[k] = ((a - 4096) * 92682) >> 16;      /* k (A - 1) */
            s->m2[k] = a2 - 4096;
        } else if (x == EQK_HS) {                       /* (A^2 (x + k (1 / A - 1) bp + (1 / A^2 - 1) lp)) */
            s->m1[k] = ((4096 * 4096 / a - 4096) * 92682) >> 16;
            s->m2[k] = 4096 * 4096 / a2 - 4096;
            s->mg[k] = a2;
        } else
            s->m1[k] = a2 - 4096;                       /* (a bell's) */
    }
    for (f = 0; f < EQ_NS; f++)
        if (own[f] != s->own[f]) {
            s->s1[f] = s->s2[f] = 0;                    /* a stage starting, or another band's now: from rest */
            s->own[f] = own[f];
        }
    s->on = 1;
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i], -3000000, 3000000), h, bp, l, v;
        for (k = 0; k < nb; k++) {
            f = st[k];
            switch (kd[k]) {
            case EQK_HP6: case EQK_LP6:
                v = mulq16(x - s->s1[f], s->g[f]);
                l = v + s->s1[f];
                s->s1[f] = l + v;
                x = kd[k] == EQK_LP6 ? l : x - l;
                break;
            case EQK_HP12: eq_svf(s, f, x, EQ_KH_BUT, &h, &bp, &l); x = h; break;
            case EQK_LP12: eq_svf(s, f, x, EQ_KH_BUT, &h, &bp, &l); x = l; break;
            case EQK_HP24:
                eq_svf(s, f, x, EQ_KH_4A, &h, &bp, &l);
                eq_svf(s, f + 1u, h, EQ_KH_4B, &h, &bp, &l);
                x = h;
                break;
            case EQK_LP24:
                eq_svf(s, f, x, EQ_KH_4A, &h, &bp, &l);
                eq_svf(s, f + 1u, l, EQ_KH_4B, &h, &bp, &l);
                x = l;
                break;
            case EQK_LS:
                eq_svf(s, f, x, EQ_KH_BUT, &h, &bp, &l);
                x += eq_mq(bp, s->m1[k]) + eq_mq(l, s->m2[k]);
                break;
            case EQK_HS:
                eq_svf(s, f, x, EQ_KH_BUT, &h, &bp, &l);
                x = eq_mq(x + eq_mq(bp, s->m1[k]) + eq_mq(l, s->m2[k]), s->mg[k]);
                break;
            default:                                    /* a bell (k = 1: k bp) */
                eq_svf(s, f, x, EQ_KH_BELL, &h, &bp, &l);
                x += eq_mq(bp, s->m1[k]);
                break;
            }
        }
        b[i] = x;
    }
}
static __attribute__((noinline)) void track_eq(track_t *t, int32_t *b, uint32_t n)
{
    eq_run(&eq[(uint32_t)(t - trk) % NTRK], t, b, n);
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
