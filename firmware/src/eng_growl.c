/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GROWL: one wave, phase-modulated by a sine, into a resonant low-pass, for growling FM basses. This fork's.
 *
 *   WAVE   the carrier: SAW SQR TRI SIN (SAW and SQR naive: the modulation moves their edges, the aliasing is part of
 *          the growl)
 *   RATIO  the modulator's frequency over the note: 0.5 1 1.5 2 3 4 5 7
 *   FM     the modulation index: 0 none .. ~2 cycles of phase; the SHP routings move it (ENV DEST / LFO DEST SHP: the wob
 *          on the growl, not only on the filter)
 *   FB     the modulator's feedback onto itself: a sine towards a saw, then noise
 *   CUT RES  a trapezoidal low-pass (dsp.c tsvf) after the drive; the filter routings move CUT; it follows the key by half
 *   DRV    a soft clip in front of the filter, 1x .. ~9x (the level kept)
 *   SUB    a sine an octave below the note, after the filter (it stays clean)
 * Per voice: ph[0] the carrier, ph[1] the modulator, ph[2] the sub; s[0] s[1] the filter, s[2] the modulator's last
 * output (FB). Integer only, one table read per oscillator and sample. */
static const char *const N_GROWL_WAVE[] = {"SAW", "SQR", "TRI", "SIN"};
static const char *const N_GROWL_RATIO[] = {"0.5", "1", "1.5", "2", "3", "4", "5", "7"};
static const uint8_t GROWL_RATIO2[8] = {1, 2, 3, 4, 6, 8, 10, 14};   /* the ratio x 2 */

static void growl_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;                 /* every note from the same phase: the stabs hit alike */
    v->s[2] = 0;
}

static void growl_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0] & 3u, i;
    uint32_t inc = m->inc, minc = (inc >> 1) * GROWL_RATIO2[p[P_E1] & 7], sinc = inc >> 1;
    int32_t idx = clamp(p[P_E2] * 256 + (m->shape - (64 << 8)) * 2, 0, 32767);   /* Q15 */
    int32_t fb = p[P_E3] * 96;                          /* Q15: 0 .. ~0.37 */
    int32_t drv = 4096 + p[P_E6] * 256, mk = 0;          /* Q12: 1x .. ~9x */
    int32_t sub = p[P_E7] * 200;                         /* Q15 */
    int32_t cut = (p[P_E4] << 8) + m->cutoff + ((v->pitch16 - 60 * 16) >> 1);
    uint32_t cph = v->ph[0], mph = v->ph[1], sph = v->ph[2];
    int32_t ic1 = v->s[0], ic2 = v->s[1], mo = v->s[2];
    tsvf_t flt;
    tsvf_coef(&flt, clamp(cut, 0, 127 << 8), p[P_E5]);
    if (p[P_E6])
        mk = (int32_t)((16384u << 15) / (uint32_t)softclip((16384 * drv) >> 12));   /* the level kept (Q15) */
    for (i = 0; i < n; i++) {
        uint32_t ph;
        int32_t s, y, a;
        mo = sine_i(mph + ((uint32_t)mulq15(mo, fb) << 17));
        ph = cph + ((uint32_t)mulq15(mo, idx) << 18);
        switch (wave) {
        case 1:
            s = ph < 0x80000000u ? 24000 : -24000;
            break;
        case 2:
            s = osc_tri(ph);
            break;
        case 3:
            s = sine_i(ph);
            break;
        default:
            s = (int32_t)(ph >> 16) - 32768;
            s -= s >> 2;
            break;
        }
        cph += inc;
        mph += minc;
        sph += sinc;
        if (mk)
            s = (softclip((s * drv) >> 12) * mk) >> 15;
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);
        a = y < 0 ? -y : y;                              /* the resonance's peaks into a soft knee (as ANALOG) */
        if (a > 16000) {
            a = 16000 + (softclip((a - 16000) * 2) >> 1);
            y = y < 0 ? -a : a;
        }
        s = (y << 1) + (sub ? mulq15(sine_i(sph), sub) : 0);
        out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[0] = cph;
    v->ph[1] = mph;
    v->ph[2] = sph;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = mo;
}

/* {WAVE, RATIO, FM, FB, CUT, RES, DRV, SUB} */
static const preset_t GROWL_PRESETS[] = {
    {"FM GROWL", {0, 1, 70, 40, 72, 40, 90, 60}, {0, 60, 127, 12}, 0, 1, FX(50, 0, 0, 0), PAT(8)},
    {"VOWEL", {1, 3, 50, 20, 52, 70, 100, 40}, {0, 50, 110, 12}, 30, 1, FX(40, 0, 0, 0), PAT(8)},
    {"METAL", {3, 7, 100, 80, 100, 20, 70, 50}, {0, 70, 120, 15}, 0, 1, FX(30, 0, 10, 10), PAT(8)},
    {"SUB WAVE", {3, 0, 30, 0, 90, 0, 30, 100}, {0, 60, 127, 12}, 0, 1, FX(0, 0, 0, 0), PAT(8)},
    {"GRIT", {0, 2, 110, 100, 64, 50, 127, 50}, {0, 60, 127, 10}, 20, 1, FX(70, 0, 0, 0), PAT(8)},
};

static const engine_t ENG_GROWL = {
    .name = "GROWL",
    .page_title = {"FM", "TONE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 3, 0, N_GROWL_WAVE, 0},
        {"RATIO", F_ENUM, 0, 7, 1, N_GROWL_RATIO, 0},
        {"FM", F_PCT, 0, 127, 60, 0, 0},
        {"FB", F_PCT, 0, 127, 20, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 60, 0, 0},
        {"SUB", F_PCT, 0, 127, 40, 0, 0},
    },
    .presets = GROWL_PRESETS,
    .npresets = NELEM(GROWL_PRESETS),
    .note_on = growl_note_on,
    .render = growl_render,
    .knob = {P_E2, P_E3, P_E4, P_E6},
    .keep = 3,                                           /* the filter over a retrigger */
};
