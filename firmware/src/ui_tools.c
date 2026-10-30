/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca: the SEQ layer's actions (ui_layer.c: SEQ held, on every page). Included by ui_layer.c. (Dah) Only two, on
 * the selected track (the tools of 1.2 .. 1.5.5 gone: REVERSE, SHIFT, RANDOM, COOK, BEAT, EUCLID, DOUBLE, HALF and
 * the DRUM lane's):
 *   QUANTIZE   the PRESETS knob its amount (the track's P_SQNT: OFF 25 % 50 % 75 % ON, as it plays at once, seq.c
 *              seq_nudge); OCT+ APPLY writes the timing as it plays into the steps for good (each NUDGE scaled, ON: all
 *              gone) and QUANTIZE OFF (the steps play as written)
 *   CLEAR ALL  F3: the dialog (CLEAR PATTERN n?), OCT+ empties the pattern: every step and its automation (events and
 *              locks); its number, LEN and place in the song stay
 * KNOB 1..4 LEN DIV SWING GATE (ui_layer.c) */
static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }

/* QUANTIZE: the amount turned (PRESETS; OFF 25 % 50 % 75 % ON, the ends held), said */
static const uint8_t QZ_ORD[5] = {0, 2, 3, 4, 1};       /* (P_SQNT: 0 OFF, 1 ON, 2..4 25 % .. 75 %) */
static uint32_t qz_rank(const track_t *t)
{
    uint32_t o;
    for (o = 0; o < 5u && QZ_ORD[o] != (uint32_t)t->p[P_SQNT]; o++)
        ;
    return o < 5u ? o : 0u;
}
static void qz_turn(track_t *t, int32_t s)
{
    t->p[P_SQNT] = (int16_t)QZ_ORD[clamp((int32_t)qz_rank(t) + (s > 0 ? 1 : -1), 0, 4)];
    ui_say("QUANTIZE ", N_QNT[clamp(t->p[P_SQNT], 0, 4)]);
}
/* the steps inside LEN with a NUDGE (a note step) */
static uint32_t qz_nudged(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < tl_len(t); i++)
        n += step_nudge(&t->step[i]) != 0 && t->step[i].time == ST_NOTE;
    return n;
}
/* OCT+ APPLY: each NUDGE as it plays now written, QUANTIZE OFF; 0 = nothing to do (said why) */
static int qz_apply(track_t *t)
{
    uint32_t len = tl_len(t), i, n = 0;
    char b[24];
    if (!t->p[P_SQNT]) {
        ui_message("QUANTIZE IS OFF");
        return 0;
    }
    if (!qz_nudged(t)) {
        ui_message("NOTHING TO QUANTIZE");
        return 0;
    }
    for (i = 0; i < len; i++) {
        int32_t v = step_nudge(&t->step[i]);
        if (!v)
            continue;
        step_set_nudge(&t->step[i], t->p[P_SQNT] == 1 ? 0 : v * (5 - t->p[P_SQNT]) / 4);
        n++;
    }
    t->p[P_SQNT] = 0;
    str_cpy(b, "QUANTIZED ", sizeof b);
    fmt_int(b + str_len(b), (int32_t)n);
    str_cpy(b + str_len(b), " STEPS", sizeof b - str_len(b));
    ui_message(b);
    return 1;
}
/* CLEAR ALL (the dialog's OCT+): every step of track t and its automation gone, the pattern kept */
static void tl_clear_all(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    motion_clear(t);
    t->nheld = 0;
    ui_message("PATTERN CLEARED");
    ui.force = 1;
}
