/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca SEQ > AUTOMATION (included by ui.c). (Dah) The selected track's four HOME knobs (ASSIGN's, else the
 * engine's four) as four lanes, each its curve over the steps (ui.c lane_value: a lock, else the last automation event
 * at or before the step, else the sound's own value; CURVE RAMP, trkx[].curve: joined as motion.c plays it) and a dot
 * where a step holds a point; the ruler and the step playing as HOME's (ui_graph.c graph_auto).
 * The piano roll's controls: KNOB 1 STEP (the cursor), 2 LANE, 3 VALUE (the lane's point at the cursor: an automation
 * event, motion_set_event; a lock there becomes one; AUTOMATION FULL past the 128), 4 CURVE
 * STEP / RAMP; OCT+ held the roll's VIEW (MOVE ZOOM FOLLOW, the same prv state); EDIT the lane's point at the cursor out,
 * the cursor on (STEP's EDIT CLEAR); REC arms as on STEP (armed and playing, VALUE records where it plays, motion_capture,
 * as HOME's knobs). Left of each lane its value at the cursor
 * A parameter motion cannot record: its lane greyed, "--". Not while a song plays (STOP TO EDIT). The steps' CHANCE,
 * RATCH, NUDGE, VEL: SEQ > DETAIL (their helpers below) */
static int32_t accel(uint32_t role, int32_t s, int32_t range);

enum { EVK_CHANCE = 2, EVK_RATCH = 3, EVK_NUDGE = 5, EVK_VEL = 7 };   /* a step's own fields (DETAIL's kinds) */
/* a step's CHANCE (kind EVK_CHANCE), RATCH (EVK_RATCH), NUDGE (EVK_NUDGE) or VEL (EVK_VEL): its value, its default
 * (100 %, x1, 0, 96), at it, set (VEL 96 is stored as 0, as a step that never had one) */
static int32_t ev_sval(const step_t *s, uint32_t k)
{
    return k == EVK_CHANCE ? (int32_t)step_chance_v(s) : k == EVK_RATCH ? (int32_t)step_ratchet(s) :
           k == EVK_VEL ? (s->vel ? s->vel : 96) : step_nudge(s);
}
static int32_t ev_sdefv(uint32_t k) { return k == EVK_CHANCE ? 100 : k == EVK_RATCH ? 1 : k == EVK_VEL ? 96 : 0; }
static int ev_sdef(const step_t *s, uint32_t k) { return ev_sval(s, k) == ev_sdefv(k); }
static void ev_sput(step_t *s, uint32_t k, int32_t v)
{
    if (k == EVK_CHANCE)
        step_set_chance_v(s, (uint32_t)v);
    else if (k == EVK_RATCH)
        step_set_ratchet(s, (uint32_t)v);
    else if (k == EVK_VEL)
        s->vel = (uint8_t)(v == 96 ? 0 : v);
    else
        step_set_nudge(s, v);
}
/* 1.5.5 (#209): CHANCE's value turned on (s detents, d of them accelerated): 0..100 % accelerated, then past 100 the
 * conditions one a detent (core.h step_chance_v: 101..); a fast turn stops at 100 on the way in and out */
static const char COND_NAME[NCOND][6] = {"1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "!FILL"};
static int32_t ev_chance_turn(int32_t v, int32_t s, int32_t d)
{
    int32_t to = v > 100 || (v == 100 && s > 0) ? v + s : v + d;
    if ((v < 100 && to > 100) || (v > 100 && to < 100))
        to = 100;
    return clamp(to, 0, (int32_t)CHANCE_V_MAX);
}
/* CHANCE's value as text: "50" (unit "%"), a condition's name ("1:4", "FILL"; unit ""); b holds 8 */
static const char *ev_chance_fmt(char *b, int32_t v)
{
    if (v > 100) {
        str_cpy(b, COND_NAME[(uint32_t)(v - 101) % NCOND], 8);
        return "";
    }
    fmt_int(b, v);
    return "%";
}
/* a NUDGE that plays: the track's QUANTIZE OFF, a note step, not ratcheted (seq.c seq_tick) */
static int ev_nudge_plays(const track_t *t, const step_t *s)
{
    return t->p[P_SQNT] != 1 && st_kind(s) == ST_NOTE && (s->n || s->hit) && step_ratchet(s) == 1u;
}
/* "+3" "-8" "0" (b holds 4) */
static void ev_nudge_fmt(char *b, int32_t v)
{
    fmt_int(b + (v > 0), v);
    if (v > 0)
        b[0] = '+';
}
/* SEQ > DETAIL (1.5.1, #199; ui_input.c sd_turn, ui_draw.c, ui_graph.c graph_detail): KNOB 1..4 are these fields of
 * the cursor step, one storage. A step takes them where they play: a NOTE step (notes or
 * hits); CHANCE a TIE too (a failed roll cuts the held note). A REST or an empty step none (nothing to hear) */
static const uint8_t SD_KIND[4] = {EVK_CHANCE, EVK_RATCH, EVK_NUDGE, EVK_VEL};
static int sd_takes(const step_t *s, uint32_t k) { return step_on(s) || (k == EVK_CHANCE && st_kind(s) == ST_TIE); }


/* ---- (Dah) the page: four lanes ---- */
static uint8_t au_lane;                                 /* the lane picked (KNOB 2), 0..3 */
static void cursor_move(int32_t c);                     /* (ui_input.c: the roll's cursor) */
static uint32_t au_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, LEN_MAX); }
static uint32_t au_cur(void) { return ui.cursor < au_len(TSEL) ? ui.cursor : au_len(TSEL) - 1u; }
static uint32_t au_raw(uint32_t k)                       /* HOME's KNOB k's id on the selected track (home_param's) */
{
    uint32_t a = trkx[song.sel % NTRK].home[k & 3u];
    return a && a <= P_COUNT ? a - 1u : ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u];
}
static uint32_t au_id(uint32_t k)                        /* .. lane k's parameter; 0xFF: none motion records */
{
    uint32_t id = au_raw(k);
    const param_desc_t *d;
    if (id >= P_COUNT || !motion_param(id))
        return 0xFFu;
    d = track_desc(TSEL, id);
    return d && d->max > d->min ? id : 0xFFu;
}
static const char *au_label(uint32_t k)                  /* lane k's name ("" none) */
{
    uint32_t id = au_raw(k);
    const param_desc_t *d = id < P_COUNT ? track_desc(TSEL, id) : 0;
    return d && d->label ? d->label : "";
}
static int au_point(const track_t *t, uint32_t si, uint32_t id) { return id != 0xFFu && motion_find(t, si, id) >= 0; }
/* the value lane id draws at step si: lane_value's; CURVE RAMP: an event's value in a line to the next event (none:
 * to the pass's start, its step 1 event or the sound's own value at LEN), a lock its step alone (motion_ramp_from) */
static int32_t au_drawn(const track_t *t, uint32_t si, uint32_t id, int *pt)
{
    uint32_t i, k = trk_index(t), len = au_len(t), ps = 0, ns = len, pf = 0, nf = 0;
    int16_t v = lane_value(t, si, id, pt), lv, pv = 0, nv = 0, fv = motion_base_value(t, id);
    const param_desc_t *d = track_desc(t, id);
    if (!trkx[k % NTRK].curve || d->fmt == F_ENUM || motion_lock_get(t, si, id, &lv))
        return v;
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        uint32_t st = MP_STEP(e);
        if (MP_TRK(e) != k || e->param != id || st >= len)
            continue;                                   /* (automation only: no MOTION_LOCK) */
        if (st <= si && (!pf || st >= ps)) ps = st, pv = e->value, pf = 1;
        if (st > si && (!nf || st < ns)) ns = st, nv = e->value, nf = 1;
        if (st == 0u) fv = e->value;
    }
    if (!pf)
        return v;
    if (!nf)
        nv = fv, ns = len;
    return ns > ps ? pv + ((int32_t)nv - pv) * (int32_t)(si - ps) / (int32_t)(ns - ps) : pv;
}
static int au_del_ready(void) { return au_point(TSEL, au_cur(), au_id(au_lane)); }   /* REC would delete */
static void au_enter(void)
{
    au_lane &= 3u;
    if (ui.cursor >= au_len(TSEL))
        cursor_set((int32_t)au_len(TSEL) - 1);
}
/* KNOB slot as on the roll: 1 STEP (the cursor), 2 LANE, 3 VALUE (the lane's point at the cursor: an automation event,
 * a lock there becomes one), 4 CURVE STEP / RAMP (trkx[].curve) */
static void au_knob(uint32_t slot, int32_t s)
{
    track_t *t = TSEL;
    uint32_t id = au_id(au_lane), c = au_cur();
    const param_desc_t *d;
    int pt, r;
    ui.hot_col = (uint8_t)slot;
    ui.hot_t = 40;
    if (slot == 0u) {
        cursor_move((int32_t)c + s);
        return;
    }
    if (slot == 1u) {
        au_lane = (uint8_t)clamp((int32_t)au_lane + (s > 0 ? 1 : -1), 0, 3);
        return;
    }
    if (slot == 3u) {
        trkx[song.sel % NTRK].curve = s > 0;
        return;
    }
    if (id == 0xFFu) {
        ui_message("NOT AUTOMATABLE");
        return;
    }
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    d = track_desc(t, id);
    if (rec_on(t) && song.playing) {                    /* armed, playing: recorded where it plays (as HOME's knobs) */
        t->p[id] = (int16_t)param_turn(d, t->p[id], accel(EN_K1 + slot, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
        if (motion_capture(t, id, t->p[id]) == 2)
            ui_message("AUTOMATION FULL");
        return;
    }
    r = motion_set_event(t, c, id, (int16_t)param_turn(d, lane_value(t, c, id, &pt),
                                                         accel(EN_K1 + slot, s, d->fmt == F_ENUM ? 0 : d->max - d->min)));
    if (r == 2)
        ui_message("AUTOMATION FULL");
}
static void au_clear(void)                              /* EDIT: the lane's point at the cursor (an event or a lock)
                                                         * out, the cursor on (as STEP's EDIT) */
{
    track_t *t = TSEL;
    uint32_t c = au_cur();
    if (chain_busy()) {
        ui_message("STOP TO EDIT");
        return;
    }
    if (au_del_ready()) {
        motion_delete_event(t, c, au_id(au_lane));
        ui_message("POINT CLEARED");
    } else
        ui_message("NO POINT HERE");
    cursor_set((int32_t)c + 1);
}
