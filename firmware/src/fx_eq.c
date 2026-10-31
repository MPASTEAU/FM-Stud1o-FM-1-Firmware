/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the EQ, a unit of the track's chain (ORDER, fx.c fx_chain / mix_spread): four filters (eqc_t: trkx's, the
 * MASTER's eq_mc; flat BELLs at first), each a TYPE in a slot's two parameters (EQ_FID EQ_SID: its FREQ, its SLOPE or
 * GAIN): HIGH PASS and LOW PASS (6, 12 or 24 dB an octave, Butterworth), LOW SHELF and HIGH SHELF (Q 0.71), BELL
 * (Q 1); GAIN -15 .. +15 dB.
 * Five filter stages at most, as the five filters before bands (a 24 dB cut takes two, any other band one: eq_plan).
 * Each a trapezoidal SVF (Zavalishin; a 6 dB cut a one-pole), Q16 coefficients a block (EQ_G: tan(pi f / FS) of the
 * cutoff index, 30 Hz .. 10.25 kHz); mulq16 keeps a sample's full precision. A band at rest (a HIGH PASS at its bottom,
 * a LOW PASS at its top, a GAIN of 0) runs nothing; none running: the EQ runs nothing (in the chain or not: EQF_IN).
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
#define EQF_NEW 1u                                      /* (eqc_t f: filters, not the EQ before them) */
#define EQF_IN 2u                                       /* .. in the chain */
#define EQF_4 4u                                        /* .. always four (before: a count, 0..4, of slots in o) */
#define EQF_TONE 8u                                     /* (a track's) its BASS and TREBLE, from its four filters (tone_adopt) */
#define TONE_LO 24                                      /* BASS's and TREBLE's corners at first: 100 Hz, 5 kHz */
#define TONE_HI 103
static const uint8_t EQ_FID[EQ_NB] = {P_EQLO, P_EQHI, P_EQ1F, P_EQ2F};   /* each slot's FREQ */
static const uint8_t EQ_SID[EQ_NB] = {P_EQ3G, P_EQ3F, P_EQ1G, P_EQ2G};   /* .. its GAIN, a cut's SLOPE (< 0 6, 0 12,
                                                         * > 0 24 dB) */
static const uint8_t EQ_FLAT[EQ_NB] = {24, 57, 85, 113};   /* a flat BELL's FREQ: 100 Hz, 500 Hz, 2 kHz, 8 kHz */
typedef struct {
    int32_t s1[EQ_NS], s2[EQ_NS];                       /* each stage's two states */
    uint16_t g[EQ_NS], dq[EQ_NS];                       /* the block's g and 1 / (1 + g (g + k)), Q16 (a one-pole: g / (1 + g)) */
    uint8_t own[EQ_NS];                                 /* what each stage ran (another band, kind: from rest) */
    int32_t m1[EQ_NB], m2[EQ_NB], mg[EQ_NB];            /* each band's mix: bp, lp, out (a shelf's, a bell's), Q12 */
    uint32_t key[EQ_NB];                                /* what each band's coefficients were made of (0: none yet) */
    int32_t din, dc;                                    /* (fix) the block of one value din: what it gives, every sample */
    uint8_t on, fix;                                    /* fix: a block all din left every state as it was (eq_run) */
} eq_t;
static eq_t eq[NTRK] __attribute__((section(".pool")));
static eq_t eq_m[2];                                    /* (the MASTER's: left, right) */
static eqc_t eq_mc;                                     /* (the MASTER's filters: projfs.c keeps them) */

static eqc_t *eq_cfg(const track_t *t) { return t >= trk && t < trk + NTRK ? &trkx[t - trk].eq : &eq_mc; }
static uint32_t eq_slot(const eqc_t *c, uint32_t k) { return (c->o >> (2u * (k & 3u))) & 3u; }
static uint32_t eq_type(const eqc_t *c, uint32_t k) { uint32_t y = c->t[eq_slot(c, k)]; return y < EQT_N ? y : EQT_BELL; }
static uint32_t eq_slope(int32_t v) { return v < 0 ? 0u : v > 0 ? 2u : 1u; }   /* 6 12 24 dB */
/* t's four filters, filter k in slot k (none set yet: four BELLs) */
static int eq_trk(const track_t *t) { return t >= trk && t < trk + NTRK; }
static void eq_get(const track_t *t, eqc_t *c)
{
    *c = *eq_cfg(t);
    if (!(c->f & EQF_4))
        c->t[0] = c->t[1] = c->t[2] = c->t[3] = EQT_BELL;
    c->n = EQ_NB;
    c->o = 0 | 1u << 2 | 2u << 4 | 3u << 6;
    if (eq_trk(t)) {                                    /* (Dah) a track's: BASS and TREBLE, two shelves (tone_run) */
        c->n = 2;
        c->t[0] = EQT_LS;
        c->t[1] = EQT_HS;
    }
}
/* c's four filters (the slots in used kept, the others flat BELLs: p's FREQ EQ_FLAT, GAIN 0) */
static void eq_four(int16_t *p, eqc_t *c, uint32_t used)
{
    uint32_t j;
    for (j = 0; j < EQ_NB; j++)
        if (!((used >> j) & 1u)) {
            c->t[j] = EQT_BELL;
            p[EQ_FID[j]] = EQ_FLAT[j];
            p[EQ_SID[j]] = 0;
        }
    c->n = EQ_NB;
    c->o = 0 | 1u << 2 | 2u << 4 | 3u << 6;
    c->f |= EQF_NEW | EQF_4;
}
/* a stored EQ as four filters: one before filters (f without EQF_NEW: LOW CUT, HIGH CUT, the bells 1 2 3 in p's raw
 * values: a LOW CUT not at rest a HIGH PASS in slot 1, a HIGH CUT a LOW PASS in 2 (12 dB), each bell not at 0 dB a
 * BELL in a free slot; in the chain when one does something), or a count of them (f without EQF_4: those kept); the
 * rest flat BELLs. p may be raw */
static void eq_adopt(int16_t *p, const int16_t *raw, eqc_t *c)
{
    static const uint8_t FREE[4] = {2, 3, 1, 0};        /* (the bells' slots: the cuts' last) */
    int32_t lo = raw[P_EQLO], hi = raw[P_EQHI], b[3][2] = {{raw[P_EQ1F], raw[P_EQ1G]}, {raw[P_EQ2F], raw[P_EQ2G]},
                                                           {raw[P_EQ3F], raw[P_EQ3G]}};
    uint32_t k, j, used = 0;
    if (c->f & EQF_4)
        return;
    if (c->f & EQF_NEW) {                               /* (a count: its slots) */
        for (k = 0; k < c->n && k < EQ_NB; k++)
            used |= 1u << eq_slot(c, k);
        eq_four(p, c, used);
        return;
    }
    memset(c, 0, sizeof *c);
    for (j = 0; j < EQ_NB; j++)
        p[EQ_SID[j]] = 0;                               /* (12 dB, 0 dB) */
    if (lo > 0) {
        p[P_EQLO] = (int16_t)clamp(lo, 0, EQ_TOP);
        c->t[0] = EQT_HP;
        used |= 1u;
    }
    if (hi < EQ_TOP) {
        p[P_EQHI] = (int16_t)clamp(hi, 0, EQ_TOP);
        c->t[1] = EQT_LP;
        used |= 2u;
    }
    for (k = 0; k < 3u; k++)
        if (b[k][1]) {
            for (j = 0; j < EQ_NB && ((used >> FREE[j]) & 1u); j++)
                ;
            if (j == EQ_NB)
                break;
            j = FREE[j];
            used |= 1u << j;
            p[EQ_FID[j]] = (int16_t)clamp(b[k][0], 0, EQ_TOP);
            p[EQ_SID[j]] = (int16_t)clamp(b[k][1], -64, 63);
            c->t[j] = EQT_BELL;
        }
    eq_four(p, c, used);
    if (used)
        c->f |= EQF_IN;
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
#ifndef FM1_OPAQUE
#define FM1_OPAQUE(v) ((void)0)                         /* (the host: hal/fm1_cc.h's, not needed there) */
#endif
/* a k / 65536 (k < 2^31), one 64-bit product: dsp.c mulq16's value bit for bit (the floor of the same product) */
static inline int32_t eq_m16(int32_t a, uint32_t k) { return (int32_t)(((int64_t)a * (int32_t)k) >> 16); }
/* v m, m Q12 signed, as mulq16(v, |m|) << 4 negated for m < 0 (am = |m|, ng = 0 or -1: m's sign) */
static inline int32_t eq_mq(int32_t v, uint32_t am, int32_t ng) { return ((eq_m16(v, am) << 4) ^ ng) - ng; }
/* one stage over the block, its state and coefficients held in registers: c 0 a one-pole (lp: its low output, else
 * x minus it), 1 an SVF's high or low output (lp), 2 a shelf or bell, x + m1 bp + m2 lp (hs: then times mg). The
 * bands run one after the other over the whole block, each sample through them in the same order as before: the
 * same values bit for bit */
static __attribute__((noinline)) void eq_pass(eq_t *s, uint32_t f, uint32_t k, uint32_t c, uint32_t kh, uint32_t lp,
                                              int32_t *b, uint32_t n)
{
    int32_t s1 = s->s1[f], s2 = s->s2[f], m1 = s->m1[k], m2 = s->m2[k];
    uint32_t g = s->g[f], dq = s->dq[f], i, a1 = (uint32_t)(m1 < 0 ? -m1 : m1), a2 = (uint32_t)(m2 < 0 ? -m2 : m2);
    uint32_t ag = (uint32_t)s->mg[k];
    int32_t n1 = m1 < 0 ? -1 : 0, n2 = m2 < 0 ? -1 : 0;
    for (i = 0; i < n; i++) {
        int32_t x = b[i], h, v, bp, l;
        if (!c) {                                       /* (a one-pole) */
            v = eq_m16(x - s1, g);
            l = v + s1;
            s1 = l + v;
            b[i] = lp ? l : x - l;
            continue;
        }
        FM1_OPAQUE(dq);                                 /* (else its product grows to 64 x 64 bits) */
        h = eq_m16(x - (eq_m16(s1, kh) << 1) - eq_m16(s1, g) - s2, dq);
        v = eq_m16(h, g);
        bp = v + s1;
        s1 = bp + v;
        v = eq_m16(bp, g);
        l = v + s2;
        s2 = l + v;
        if (c == 1u)
            x = lp ? l : h;
        else {
            x += eq_mq(bp, a1, n1) + eq_mq(l, a2, n2);
            if (c == 3u)
                x = eq_m16(x, ag) << 4;
        }
        b[i] = x;
    }
    s->s1[f] = s1;
    s->s2[f] = s2;
}
static void eq_run(eq_t *s, const track_t *t, int32_t *b, uint32_t n)
{
    eqc_t c;
    uint8_t kd[EQ_NB], bk[EQ_NB], st[EQ_NB], own[EQ_NS] = {0};
    uint32_t nb, i, k, f;
    int32_t z, s1[EQ_NS], s2[EQ_NS];
    eq_get(t, &c);
    nb = eq_plan(t, &c, kd, bk, st);
    if (!nb) {
        if (s->on)
            memset(s, 0, sizeof *s);                    /* (flat again: from rest next time) */
        return;
    }
    for (k = 0; k < nb; k++) {                          /* each band's coefficients (when its values changed) */
        uint32_t j = eq_slot(&c, bk[k]), x = kd[k], g, key;
        int32_t v = t->p[EQ_SID[j]], a2, a;
        f = st[k];
        own[f] = (uint8_t)(1u + j * 32u + x * 2u);
        if (x == EQK_HP24 || x == EQK_LP24)
            own[f + 1u] = (uint8_t)(own[f] + 1u);
        key = 1u + (x | f << 4 | ((uint32_t)t->p[EQ_FID[j]] & 0xFFu) << 8 | ((uint32_t)v & 0xFFFFu) << 16);
        if (s->key[k] == key)
            continue;
        s->key[k] = key;
        s->fix = 0;
        g = eq_band_g(x, t->p[EQ_FID[j]], v);
        a2 = eq_gain_q12(v);
        a = eq_db_q12(v * 1500 / 64 / 2);
        if (x == EQK_HP6 || x == EQK_LP6)
            s->g[f] = (uint16_t)((g << 15) / ((65536u + g) >> 1));   /* (g / (1 + g)) */
        else if (x == EQK_HP24 || x == EQK_LP24) {
            eq_coef(s, f, g, EQ_KH_4A);
            eq_coef(s, f + 1u, g, EQ_KH_4B);
        } else
            eq_coef(s, f, g, x == EQK_BELL ? EQ_KH_BELL : EQ_KH_BUT);
        s->m2[k] = s->mg[k] = 0;
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
    for (k = nb; k < EQ_NB; k++)
        s->key[k] = 0;                                  /* (a band not running: its stages may be another's now) */
    for (f = 0; f < EQ_NS; f++)
        if (own[f] != s->own[f]) {
            s->s1[f] = s->s2[f] = 0;                    /* a stage starting, or another band's now: from rest */
            s->own[f] = own[f];
            s->fix = 0;
        }
    s->on = 1;
    for (i = 1, z = 0; i < n && !z; i++)
        z = b[i] - b[0];                                /* (z 0: the block one value, b[0]) */
    if (FX_SKIP && !z && s->fix && b[0] == s->din) {               /* (that block again: what it gave, the states as they are; */
        for (i = 0; i < n; i++)                         /* at 0 in, their rounding may hold them off 0) */
            b[i] = s->dc;
        return;
    }
    s->din = b[0];
    memcpy(s1, s->s1, sizeof s1);
    memcpy(s2, s->s2, sizeof s2);
    for (i = 0; i < n; i++)
        b[i] = clamp(b[i], -3000000, 3000000);
    for (k = 0; k < nb; k++) {
        uint32_t x = kd[k], lp = x >= EQK_LP6 && x <= EQK_LP24;
        f = st[k];
        if (x == EQK_HP6 || x == EQK_LP6)
            eq_pass(s, f, k, 0, 0, lp, b, n);
        else if (x == EQK_HP24 || x == EQK_LP24) {
            eq_pass(s, f, k, 1, EQ_KH_4A, lp, b, n);
            eq_pass(s, f + 1u, k, 1, EQ_KH_4B, lp, b, n);
        } else if (x <= EQK_LP24)
            eq_pass(s, f, k, 1, EQ_KH_BUT, lp, b, n);
        else
            eq_pass(s, f, k, x == EQK_HS ? 3u : 2u, x == EQK_BELL ? EQ_KH_BELL : EQ_KH_BUT, 0, b, n);
    }    if (!z) {                                           /* (a block of one value: did it change nothing, give one?) */
        for (i = 1; i < n && b[i] == b[0]; i++)
            ;
        s->fix = i == n && !memcmp(s1, s->s1, sizeof s1) && !memcmp(s2, s->s2, sizeof s2);
        s->dc = b[0];
    } else
        s->fix = 0;
}
/* (Dah) a track's EQ: BASS and TREBLE, a one-pole shelf each (the 6 dB cut's trapezoidal one-pole: x + (A - 1) lp,
 * x + (A - 1) (x - lp)), GAIN -15 .. +15 dB (slot 0's GAINA, 1's GAINB), its corner FREQ (FREQA, FREQB); a band at
 * 0 dB nothing, both: the EQ out of the chain (eq_units). A few instructions a sample (the four filters it replaces:
 * ~30 a filter). State in eq[]'s first two stages; a band starting: from rest */
static uint32_t tone_a(int32_t fidx)                    /* the one-pole's g / (1 + g), Q16 */
{
    uint32_t g = EQ_G[clamp(fidx, 0, EQ_TOP)];
    return (g << 15) / ((65536u + g) >> 1);
}
static __attribute__((noinline)) void track_eq(track_t *t, int32_t *b, uint32_t n)
{
    eq_t *s = &eq[(uint32_t)(t - trk) % NTRK];
    int32_t vl = t->p[EQ_SID[0]], vh = t->p[EQ_SID[1]], sl = s->s1[0], sh = s->s1[1];
    int32_t kl = eq_gain_q12(vl) - 4096, kh = eq_gain_q12(vh) - 4096;
    uint32_t al = tone_a(t->p[EQ_FID[0]]), ah = tone_a(t->p[EQ_FID[1]]), i;
    uint32_t ml = (uint32_t)(kl < 0 ? -kl : kl), mh = (uint32_t)(kh < 0 ? -kh : kh);
    int32_t nl = kl < 0 ? -1 : 0, nh = kh < 0 ? -1 : 0;
    if (!vl && !vh) {
        if (s->on)
            memset(s, 0, sizeof *s);                    /* (flat: from rest next time) */
        return;
    }
    s->on = 1;
    for (i = 0; i < n; i++) {                           /* (a band at 0 dB: k 0, its product 0; its state runs on) */
        int32_t x = clamp(b[i], -3000000, 3000000), l = eq_m16(x - sl, al) + sl, h = eq_m16(x - sh, ah) + sh;
        sl = l + (l - sl);
        sh = h + (h - sh);
        b[i] = x + eq_mq(l, ml, nl) + eq_mq(x - h, mh, nh);
    }
    s->s1[0] = sl;
    s->s1[1] = sh;
}
/* a track's four filters (as stored before) as BASS and TREBLE: the lowest band with a GAIN (a shelf, a bell; a cut:
 * -15 dB there) BASS, the highest TREBLE, the ones between dropped; one band alone: below 1 kHz BASS, else TREBLE.
 * Flat: the corners TONE_LO, TONE_HI. Once (EQF_TONE): a project, a song as loaded (project.c, main.c) */
static void tone_adopt(int16_t *p, eqc_t *c)
{
    int32_t f[EQ_NB], v[EQ_NB], lo = -1, hi = -1;
    uint32_t k, j;
    if (c->f & EQF_TONE)
        return;
    for (k = 0, j = 0; k < EQ_NB; k++) {
        uint32_t y = (c->f & EQF_4) && c->t[k] < EQT_N ? c->t[k] : EQT_BELL;
        int32_t fr = p[EQ_FID[k]], g = y <= EQT_LP ? (y ? fr < EQ_TOP : fr > 0) * -64 : p[EQ_SID[k]];   /* (a cut: -15 dB) */
        if (!g)
            continue;
        f[j] = fr;
        v[j++] = g;
    }
    for (k = 0; k < j; k++) {
        if (lo < 0 || f[k] < f[lo])
            lo = (int32_t)k;
        if (hi < 0 || f[k] > f[hi])
            hi = (int32_t)k;
    }
    if (lo == hi && lo >= 0) {                          /* (one band: by its frequency) */
        if (f[lo] < 71)
            hi = -1;
        else
            lo = -1;
    }
    for (k = 0; k < EQ_NB; k++) {
        p[EQ_FID[k]] = EQ_FLAT[k];
        p[EQ_SID[k]] = 0;
    }
    p[EQ_FID[0]] = (int16_t)(lo >= 0 ? f[lo] : TONE_LO);
    p[EQ_SID[0]] = (int16_t)(lo >= 0 ? v[lo] : 0);
    p[EQ_FID[1]] = (int16_t)(hi >= 0 ? f[hi] : TONE_HI);
    p[EQ_SID[1]] = (int16_t)(hi >= 0 ? v[hi] : 0);
    c->t[0] = EQT_LS;
    c->t[1] = EQT_HS;
    c->t[2] = c->t[3] = EQT_BELL;
    c->n = EQ_NB;
    c->o = 0 | 1u << 2 | 2u << 4 | 3u << 6;
    c->f = (uint8_t)(EQF_NEW | EQF_4 | EQF_TONE | (lo >= 0 || hi >= 0 ? EQF_IN : 0u));
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
