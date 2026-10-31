/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A note or a hit placed on SEQ sounds once on the selected track, as PLAY will play it (ui_input.c place_heard, seq.c
 * seq_preview), stopped and playing; a removal is silent; a fast turn holds one note at a time:
 *   DRUM grid   a white key on an empty step (armed): that lane's hit (its kit note, the step's velocity; ACC 127);
 *               the same key again (the hit off): nothing; KNOB 3 HIT on: the lane's hit; playing: over the music
 *   the roll    KNOB 2 NOTE: the new note (the step's VEL), a fast turn: the last note only; step entry (armed,
 *               stopped): the key's note sounds (the keyboard's own)
 *   played      the DRUM grid not armed (the keys play the kit): the lane of the key played picked, its row lit
 *               while the key is held (drawn again then and when it is let go)
 * The real UI sources with tests/ui_test.c's stubs */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t sounding(const track_t *t, uint32_t note)   /* voices of note gated on */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && t->v[i].note == note;
    return n;
}
static uint32_t any_gated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}
static void settle(void) { uint32_t i; for (i = 0; i < 40u; i++) { blocks(12); frame(); } }   /* (~0.6 s) */

static int place_drum(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t note;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    track_select(0);
    open_family(FAM_SEQ);
    song.rec = 1u;                                      /* (armed: the keys are the steps) */
    frame();
    tap_key(key_at(1, DV_SNARE));                       /* (the SNARE lane picked: it plays, as always) */
    settle();
    note = DRUM_LANE_NOTE[DV_SNARE];
    t->step[4].vel = 0;
    tap_key(key_at(0, 4));
    blocks(2);
    ok = t->step[4].hit == 1u << DV_SNARE && pv_note == note && pv_on == 1u && sounding(t, note) >= 1u &&
         t->v[0].vel <= 127u;
    bad += check("DRUM grid (stopped): a white key places the SNARE on step 5, the SNARE sounds (its kit note, VEL 96)", ok);
    settle();
    ok = !any_gated(t);
    tap_key(key_at(0, 4));                              /* (off: the hit goes) */
    blocks(2);
    ok &= !t->step[4].hit && !any_gated(t) && !pv_on;
    bad += check("  the same key again: the hit off, nothing sounds", ok);
    ui.lane = DV_KICK;
    cursor_set(6);
    turn(EN_K3, 1);
    blocks(2);
    ok = t->step[6].hit == 1u << DV_KICK && pv_note == DRUM_LANE_NOTE[DV_KICK] && sounding(t, DRUM_LANE_NOTE[DV_KICK]);
    settle();
    turn(EN_K3, -1);
    blocks(2);
    ok &= !t->step[6].hit && !any_gated(t);
    bad += check("  KNOB 3 HIT on: the KICK sounds; off: nothing", ok);
    fm1_in.notes |= 1u << key_at(1, GK_ACC);            /* (ACC held: placed accented, 127) */
    frame();
    tap_key(key_at(0, 9));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    frame();
    ok = ((t->step[9].acc >> ui.lane) & 1u) && pv_wvel == 127u && pv_note == DRUM_LANE_NOTE[ui.lane];
    fm1_in.notes = 0;
    frame();
    settle();
    bad += check("  ACC held: the hit placed accented sounds at 127", ok);
    menu_put(MI_COUNTIN, 0);                            /* (no count-in: armed) */
    transport_req = 1;                                  /* playing: over the music */
    blocks(4); frame();
    ui.lane = DV_SNARE;
    tap_key(key_at(0, 12));
    blocks(2);
    ok = song.playing && t->step[12].hit == 1u << DV_SNARE && pv_on == 1u && pv_note == DRUM_LANE_NOTE[DV_SNARE] &&
         sounding(t, DRUM_LANE_NOTE[DV_SNARE]);
    transport_req = 2;
    blocks(8); frame();
    bad += check("  playing: a hit placed sounds at once, over the music", ok);
    return bad;
}
static int place_roll(void)
{
    int bad = 0, ok;
    track_t *t = TSEL;
    uint32_t i, n0;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    t->step[2] = (step_t){{60}, 1, ST_NOTE, 0, 80};
    go_page(GR_ROLL);
    cursor_set(2);
    frame();
    settle();
    turn(EN_K2, 3);
    blocks(2);
    ok = t->step[2].note[0] == 63u && pv_note == 63u && pv_wvel == 80u && sounding(t, 63);
    bad += check("the roll: KNOB 2 NOTE 60 -> 63, the new note sounds (the step's VEL 80)", ok);
    for (i = 0; i < 12u; i++) {                         /* (a fast turn: one note at a time) */
        host_enc[panel.enc[EN_K2]] += panel.dir[EN_K2];
        frame();
        blocks(1);
    }
    n0 = any_gated(t);
    ok = t->step[2].note[0] == 75u && pv_note == 75u && sounding(t, 75) && n0 == 1u;
    bad += check("  a fast turn (12 notes): the last one only sounding, never a stack", ok);
    settle();
    ok = !any_gated(t);
    track_defaults_steps(t);
    cursor_set(5);
    press(B_REC);                                       /* (armed, stopped: step entry) */
    key_down(7);
    frame();
    blocks(2);
    ok &= song.rec && t->step[5].n == 1u && t->step[5].note[0] == kb_map(t, 7) && sounding(t, kb_map(t, 7));
    key_up(7);
    frame();
    bad += check("  step entry (armed, stopped): the key's note written and sounding", ok);
    return bad;
}
static int played_lane(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t k, ks = 27, kk = 27, rows;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    track_select(0);
    open_family(FAM_SEQ);
    frames(300);
    for (k = 0; k < 27u; k++) {                         /* (the keys of the SNARE and of the KICK) */
        if (kb_map(t, k) == DRUM_LANE_NOTE[DV_SNARE] && ks == 27u) ks = k;
        if (kb_map(t, k) == DRUM_LANE_NOTE[DV_KICK] && kk == 27u) kk = k;
    }
    ok = !song.rec && !song.grid && grid_on() && ks < 27u && kk < 27u && ui.lane == 0u;
    key_down(ks);
    host_blit_rows = 0;
    frame();
    ok &= ui.lane == DV_SNARE && gd_held(t) == 1u << DV_SNARE && host_blit_rows > 0u;
    host_blit_rows = 0;
    frame();
    ok &= !host_blit_rows;                              /* (held: drawn once) */
    key_up(ks);
    frame();
    ok &= !gd_held(t) && ui.lane == DV_SNARE && host_blit_rows > 0u;
    bad += check("DRUM grid not armed: the SNARE's key picks its lane, its row lit while held, then as picked", ok);
    key_down(kk);
    frame();
    ok = ui.lane == DV_KICK && gd_held(t) == 1u << DV_KICK;
    key_up(kk);
    frame();
    song.rec = 1u;                                      /* (armed: the keys are the steps, no lane played) */
    frame();
    ok &= song.grid == 1u && !gd_held(t);
    bad += check("  the KICK's key next: the KICK's lane; armed: the keys are the steps (no lane lit)", ok);
    return bad;
}
int main(void)
{
    int bad = place_drum() + place_roll() + played_lane();
    printf("%s\n", bad ? "PLACE TEST FAILED" : "place test passed");
    return bad != 0;
}
