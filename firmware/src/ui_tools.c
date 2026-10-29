/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca: the SEQ layer's actions (ui_layer.c: SEQ held, on every page). Included by ui_layer.c. (Dah) Only two, on
 * the selected track (the tools of 1.2 .. 1.5.5 gone: REVERSE, SHIFT, RANDOM, COOK, BEAT, EUCLID, DOUBLE, HALF and
 * the DRUM lane's):
 *   QUANTIZE   the PRESETS knob its amount (the track's P_SQNT: OFF 25 % 50 % 75 % ON, as it plays at once, seq.c
 *              seq_nudge); OCT+ APPLY writes the timing as it plays into the steps for good (each NUDGE scaled, ON: all
 *              gone) and QUANTIZE OFF (the steps play as written). One undo (SAVE held)
 *   CLEAR ALL  F3: the dialog (CLEAR PATTERN n?), OCT+ empties the pattern: every step and its automation (events and
 *              locks); its number, LEN and place in the song stay. One undo (SAVE held)
 * KNOB 1..4 LEN DIV SWING GATE (ui_layer.c); the layer's OCT- puts back the track as it opened (tl_open) */
/* the track as the layer opened (OCT-): in the pool (RAM's .bss is tight), taken by layer_opened */
static struct { step_t step[NSTEP]; motion_store_t mo; } tl_open __attribute__((section(".pool")));
static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }

/* the undo copy of t as it is now (ui.c load_begin, but the automation is copied and stays): one action */
static void tl_undo_take(track_t *t)
{
    uint32_t i = trk_index(t);
    undo.trk = (uint8_t)(i + 1u);
    undo.what = UNDO_PAT;
    undo.keep = 0;                                      /* (the next load or action takes its own copy) */
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    memcpy(undo.fm6, fm6_patch[i], FP_SIZE);
    undo.fm6_slot = fm6_slot[i];
    undo.from = pat_from[i];
    undo.from_h = pat_from_h[i];
    motion_snapshot_track(t, &undo.motion_backup);
}

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
    edit_undo_take(t, 0x3000u, UNDO_PAT);
    for (i = 0; i < len; i++) {
        int32_t v = step_nudge(&t->step[i]);
        if (!v)
            continue;
        step_set_nudge(&t->step[i], t->p[P_SQNT] == 1 ? 0 : v * (5 - t->p[P_SQNT]) / 4);
        n++;
    }
    t->p[P_SQNT] = 0;
    motion_undo_done(t);
    str_cpy(b, "QUANTIZED ", sizeof b);
    fmt_int(b + str_len(b), (int32_t)n);
    str_cpy(b + str_len(b), " STEPS", sizeof b - str_len(b));
    ui_message(b);
    return 1;
}
/* CLEAR ALL (the dialog's OCT+): every step of track t and its automation gone, the pattern kept; one undo */
static void tl_clear_all(track_t *t)
{
    uint32_t i;
    tl_undo_take(t);
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
    motion_clear(t);
    t->nheld = 0;
    ui_message("PATTERN CLEARED");
    str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
    ui.force = 1;
}
