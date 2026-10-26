/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the COMPRESSOR, an effect of a track's chain (just before its SIDECHAIN, so the pump stays: fx_chain, mix_spread;
 * trkx rsv0 AMOUNT, 0 out of the chain; rsv1 RELEASE) and of the MASTER (the mix before the limiter: G_MCOMP,
 * G_MREL). AMOUNT 1..127 sets it all: the threshold 0 .. -30 dB, the ratio 1 + AMOUNT / 32 (.. ~5:1), half the
 * reduction at the threshold given back (make-up). A peak follower (attack ~0.2 ms, RELEASE ~20 .. 380 ms); the gain
 * a block at a time from it (the log2 and back, integers), glided over the block. cmp_gr: the reduction, for the
 * pictures. IN drives it (before the follower), OUT sets the level after (-64..63: +-15 dB; trkx cin cout; the
 * MASTER's: mtrk's P_IA P_IB, its INSERT never runs). Included by fx.c */
typedef struct {
    int32_t env;                        /* the peak followed (the signal's scale) */
    int32_t g;                          /* the gain applied last block, Q12 */
    int32_t gr;                         /* the reduction there, dB x 8 (for the pictures) */
} comp_t;
static comp_t cmp_trk[NTRK], cmp_master;
static int32_t cmp_l2(uint32_t x)       /* log2 of x (> 0), Q8 */
{
    int32_t e = 31, f;
    if (!x)
        return -8192;
    while (!(x >> e))
        e--;
    f = (int32_t)((e >= 8 ? x >> (e - 8) : x << (8 - e)) & 255u);
    return e * 256 + f + ((f * (256 - f)) >> 9);
}
static int32_t cmp_x2(int32_t l)        /* 2 ^ (l / 256), Q12, l <= 2 * 256 */
{
    int32_t e = l >> 8, f = l & 255, m = 4096 + ((4096 * f) >> 8) - ((f * (256 - f)) >> 5);   /* (2^f ~ 1 + f - .) */
    return e >= 0 ? m << e : m >> -e;
}
/* the gain for this block (Q12) from the peak followed: AMOUNT amt */
static int32_t cmp_gain(comp_t *c, uint32_t amt)
{
    int32_t lv = cmp_l2((uint32_t)c->env) - 15 * 256;   /* (the level against a voice's full scale, 32768: 0) */
    int32_t thr = -(int32_t)amt * 5 * 256 / 127;        /* 0 .. -5 octaves (-30 dB) */
    int32_t keep = 32 * 256 / (32 + (int32_t)amt);      /* 1 / ratio, Q8 */
    int32_t red = lv > thr ? ((lv - thr) * (256 - keep)) >> 8 : 0;
    int32_t mk = ((-thr) * (256 - keep)) >> 9;          /* (half the reduction at the threshold back) */
    c->gr = red * 8 * 6 / 256;                          /* (log2 units -> dB x 8: x 6.02) */
    return cmp_x2(mk - red);
}
static void cmp_follow(comp_t *c, int32_t a, uint32_t rs)
{
    if (a > c->env)
        c->env += (a - c->env) >> 3;
    else
        c->env -= (c->env - a) >> rs;
}
/* a track's mono block b (and SPREAD's side d, 0 none) through its COMPRESSOR */
static __attribute__((noinline)) void comp_track(track_t *t, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk) % NTRK, amt = trkx[k].rsv0, rs = 10u + (uint32_t)trkx[k].rsv1 / 32u, i;
    comp_t *c = &cmp_trk[k];
    int32_t g0, g1, dg, gi, go;
    if (!amt) {
        c->g = 4096;
        c->gr = 0;
        return;
    }
    if (dv_unit == DV_COMP && ((dv_watch >> k) & 1u))
        dv_level(b, n, 0);                              /* (COMP's page: before, after, the reduction) */
    gi = trkx[k].cin ? eq_gain_q12(trkx[k].cin) : 4096;
    go = trkx[k].cout ? eq_gain_q12(trkx[k].cout) : 4096;
    for (i = 0; i < n; i++) {
        int32_t a = b[i] < 0 ? -b[i] : b[i];
        cmp_follow(c, gi == 4096 ? a : (int32_t)(((int64_t)a * gi) >> 12), rs);
    }
    g0 = c->g ? c->g : 4096;
    g1 = cmp_gain(c, amt);
    c->g = g1;
    gi = (int32_t)(((int64_t)gi * go) >> 12);           /* (IN and OUT round the reduction) */
    g0 = (int32_t)(((int64_t)g0 * gi) >> 12);
    g1 = (int32_t)(((int64_t)g1 * gi) >> 12);
    if (g1 < g0)
        g0 = g1;                                        /* (Dah: down at once, no transient through; up gliding) */
    dg = (g1 - g0) / (int32_t)n;
    for (i = 0; i < n; i++, g0 += dg) {
        b[i] = (int32_t)(((int64_t)b[i] * g0) >> 12);
        if (d)
            d[i] = (int32_t)(((int64_t)d[i] * g0) >> 12);
    }
    if (dv_unit == DV_COMP && ((dv_watch >> k) & 1u)) {
        int32_t a = c->gr * 32767 / (18 * 8);
        dv_level(b, n, 1);
        if (a > dv_att)
            dv_att = a > 32767 ? 32767 : a;
    }
}
/* the MASTER: the mix's left and right (their louder followed) */
static __attribute__((noinline)) void comp_master(int32_t *l, int32_t *r, uint32_t n, uint32_t amt, uint32_t rel,
                                                  int32_t cin, int32_t cout)
{
    comp_t *c = &cmp_master;
    uint32_t rs = 10u + rel / 32u, i;
    int32_t g0, g1, dg, gi = cin ? eq_gain_q12(cin) : 4096, go = cout ? eq_gain_q12(cout) : 4096;
    for (i = 0; i < n; i++) {
        int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
        a = (a > b ? a : b) >> 2;                       /* (the mix at a quarter: its level as a voice's) */
        cmp_follow(c, gi == 4096 ? a : (int32_t)(((int64_t)a * gi) >> 12), rs);
    }
    g0 = c->g ? c->g : 4096;
    g1 = cmp_gain(c, amt);
    c->g = g1;
    gi = (int32_t)(((int64_t)gi * go) >> 12);
    g0 = (int32_t)(((int64_t)g0 * gi) >> 12);
    g1 = (int32_t)(((int64_t)g1 * gi) >> 12);
    if (g1 < g0)
        g0 = g1;                                        /* (Dah: down at once, no transient through; up gliding) */
    dg = (g1 - g0) / (int32_t)n;
    for (i = 0; i < n; i++, g0 += dg) {
        l[i] = (int32_t)(((int64_t)l[i] * g0) >> 12);
        r[i] = (int32_t)(((int64_t)r[i] * g0) >> 12);
    }
}
