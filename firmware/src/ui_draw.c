/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI drawing: the header, the four knob cards, the footer (steps + engine / preset / page)
 * and the frame; the panel between the cards and the footer is ui_graph.c. The layout:
 * flat SURF cards and panels on BG,
 * rounded corners, no rules (MENU > STYLE LINE: no SURF, 1 px rules between the areas instead: draw_gaps), colours from the theme tokens only (gfx.c T_*).
 * Type, by role: M (15 px) what is read: values, titles, messages, every list's rows, the
 * selected item's detail; S (12 px) what describes it: labels, units, key hints (a keycap's word), the footer, map
 * and layer cells, graph annotations, document text; L (28 px) the one big readout of a screen. Where M cannot fit
 * (a card value with its unit wider than the card, a dialog question or a header message too long) it is set in S;
 * free text (names) is ellipsised at its size. */
static void draw_menu(void);
static int name_on(void);                              /* NAME (ui_name.c) */
static uint64_t lock_held(void);                       /* parameter locks: the steps held on STEP (ui_input.c) */
static void name_draw(void);
#include "ui_help.c"                                   /* 1.5: HELP's hints (draw_foot, ui_layer.c draw_layer) */

/* --------------------------------------------------------- drawing --- */
#define COL_W CARD_W                                  /* a card: 57 x 44 at x 3 + 59 c, y 28 */
#define COL_H CARD_H

static int32_t batt_level(void)                         /* thresholds 531 / 561 / 591 on ADC ch3 */
{
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
/* A USB host powers us; there is no separate charger status line.
 * 4 means external power (a steady bolt inside the battery), otherwise its level. */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 4;
    return batt_level();
}

/* REC: filled, the selected track armed; outlined, another track armed */
static void draw_rec_mark(int32_t x, uint16_t bg)
{
    if (song.rec)
        cv_icon_mid(x, H_HEAD / 2, 16, (song.rec >> song.sel) & 1u ? ICON_X_REC : ICON_X_REC_O, T_REC, bg);
}

/* the battery: a 16 px Fukiai icon. The stock thresholds give 0..3 bars of 3; the font has 0..4 bars of 4:
 * each level takes the nearest share, 0 -> battery_0 (empty), 1 (1/3) -> battery_1 (1/4), 2 (2/3) ->
 * battery_3 (3/4), 3 (full) -> battery_4 (battery_2 is not used); USB power: battery_charging.
 * Colours as the drawn battery had them: one bar left the accent, empty MID (its outline), else THEME */
static uint32_t batt_icon(int32_t lvl)
{
    static const uint8_t I[5] = {ICON_X_BAT0, ICON_X_BAT1, ICON_X_BAT3, ICON_X_BAT4, ICON_X_BAT_CHG};
    return I[clamp(lvl, 0, 4)];
}
static void draw_battery(int32_t bx)
{
    int32_t lvl = batt_shown();
    cv_icon_mid(bx, H_HEAD / 2, 24, batt_icon(lvl), lvl == 1 ? T_ACCENT : lvl == 0 ? T_MID : T_THEME, T_BG);   /* 24 px: the glyph is wide and short */
}

/* ---------------------------------------------------- rolling digits --- */
/* A number that changes rolls its changed digits like a slot machine; only the drawing: the value, the gauge,
 * the hot colour and the sound change at once. Up: the old digit leaves upward and the new one comes from below;
 * down: the reverse. ROLL_FRAMES UI frames (ui.frame, ~135 ms) on an integer ease-out (ROLL_EASE: cubic, over
 * half-way in a quarter of the time, no overshoot); the ones digit first, each place one frame later, all ending
 * together on a frame equal to the static render. Clipped to the value strip, which alone is redrawn while it
 * rolls (a card: rows ROLL_Y.., the header: the BPM's columns). Characters other than digits never roll.
 * A change during a roll retargets it: it restarts from the value it was going to. Shown at once (no roll):
 * a change within ROLL_SNAP frames of the last one (a fast turn reads better as plain numbers), another
 * shape (length, sign, a non-digit, a name or an enum, a value set in S), another label or unit, a track /
 * engine / palette change (ui.roll[].sig), ui.force (a page change), a card's first draw; MENU > ANIM OFF (#46). */
#define ROLL_FRAMES 9u
#define ROLL_SNAP 3u                                    /* frames (~45 ms) */
#define ROLL_BPM 4u                                     /* ui.roll[]: the four cards, then the header BPM */
#define ROLL_Y 18                                       /* a card's value strip: rows 18..34, its figures (21..31) in the middle, */
#define ROLL_H 17                                       /* the label (MONO's chip: to row 17) above it, the gauge (38) below */
static const uint8_t ROLL_EASE[17] = {0, 45, 84, 118, 147, 172, 193, 210, 223, 234, 242, 247, 251, 253, 254, 255, 255};

static int roll_digit(char c) { return c >= '0' && c <= '9'; }
/* +1 / -1: b rolls in from a (same length, digits where a has digits, the rest equal and one of + - .),
 * the sign of b - a; 0: b is shown at once */
static int roll_dir(const char *a, const char *b)
{
    uint32_t i;
    int d = 0, neg = 0;
    for (i = 0; a[i] || b[i]; i++) {
        if (i + 1u >= sizeof ui.roll[0].from || roll_digit(a[i]) != roll_digit(b[i]))
            return 0;
        if (!roll_digit(a[i]) && (a[i] != b[i] || (a[i] != '-' && a[i] != '+' && a[i] != '.')))
            return 0;
        neg |= a[i] == '-';
        if (!d && a[i] != b[i])
            d = b[i] > a[i] ? 1 : -1;
    }
    return neg ? -d : d;
}
/* field k now shows b instead of a: roll (a retarget mid-roll), or snap */
static void roll_note(uint32_t k, const char *a, const char *b, int snap)
{
    uint32_t el = (uint8_t)(ui.frame - ui.roll[k].t0);
    int d = roll_dir(a, b);
    ui.roll[k].t0 = (uint8_t)ui.frame;
    ui.roll[k].from[0] = 0;
    if (snap || !d || el < ROLL_SNAP || (ui_prefs & PREF_ANIM_OFF))
        return;
    str_cpy(ui.roll[k].from, a, sizeof ui.roll[k].from);
    ui.roll[k].dir = (int8_t)d;
}
/* s (M) at x, y with field k's roll: each character at its pen in s (the digits are tabular and never kerned,
 * so the old value has the same pens); a rolling digit has moved o of the strip's h rows (ROLL_EASE, one frame
 * later per place from the right), clipped to the strip. The roll's last frame clears it: the static text. */
static int32_t roll_text(uint32_t k, int32_t x, int32_t y, const char *s, uint16_t fg)
{
    const aafont_t *f = &AF_M;
    uint32_t e = (uint8_t)(ui.frame - ui.roll[k].t0) + 1u, i, p = 0;
    const char *a = ui.roll[k].from;
    int32_t cy = k < ROLL_BPM ? ROLL_Y : 0, h = k < ROLL_BPM ? ROLL_H : H_HEAD;
    uint16_t bg = k < ROLL_BPM ? T_SURF : T_BG;
    char t[8], ch[2] = {0, 0};
    if (e >= ROLL_FRAMES)
        ui.roll[k].from[0] = 0;
    if (!a[0])
        return cv_text_on(x, y, f, s, fg, bg);
    str_cpy(t, s, sizeof t);
    for (i = 0; s[i]; i++)
        p += (uint32_t)roll_digit(s[i]);
    cv_cy0 = (int16_t)(cy + cv_oy);                     /* the strip (the static characters lie inside it) */
    cv_cy1 = (int16_t)(cy + cv_oy + h);
    cv_scroll = 1;                                      /* (the lint: rolling digits are cut on purpose) */
    for (i = 0; s[i]; i++) {
        int32_t px, ny = y;
        t[i] = 0;
        px = x + text_w(f, t);                          /* (its pen: no digit kerns) */
        t[i] = s[i];
        p -= (uint32_t)roll_digit(s[i]);
        if (s[i] != a[i]) {                             /* p: the places right of it */
            int32_t o = 0, dir = ui.roll[k].dir;
            if (e > p)                                  /* (e < ROLL_FRAMES: the LUT index stays in 1..16) */
                o = (int32_t)((ROLL_EASE[((e - p) * 32u / (ROLL_FRAMES - p) + 1u) >> 1] * (uint32_t)h + 128u) >> 8);
            ch[0] = a[i];
            cv_text_on(px, y - dir * o, f, ch, fg, bg);
            ny = y + dir * (h - o);
        }
        ch[0] = s[i];
        cv_text_on(px, ny, f, ch, fg, bg);
    }
    cv_cy0 = 0;
    cv_cy1 = (int16_t)cv_h;
    cv_scroll = 0;
    return x + text_w(f, s);
}

/* the header (y 0..24), three calm slots (1.2):
 *   left    the selected track's cushion (ink 4..20), the PROJECT slot letter of its pattern ("1 A", head_slot), the REC
 *           mark attached (filled: it is armed; outlined: another). A message centred hides the letter (its room)
 *   centre  the play state (stop, play, the song's disc), the metronome and the BPM (BPM LOCK ON: a lock right of
 *           it), the group's ink centred on x 120 (head_geo). The metronome swings on the click's beats (seq.c click_beat_now): mirrored
 *           on the bar's 2nd and 4th, the accent's on its 1st; stopped, still and MID
 *   right   the octave, or the song row while a song plays (ending at 208), and the battery (ink 216..236; its bolt
 *           says USB power: no separate USB icon)
 * A message or a layer's label replaces the centre and the octave, centred on x 120, up to
 * 166 px; while the BPM is being changed (a SELECT turn: bpm_t) or in the GLO layer the centre stays and the message
 * goes right of the BPM (over the right slot). Every element's ink centred on row 12 (H_HEAD / 2; ui_test.c
 * test_head_centres): icons by cv_icon_mid, text by its capitals' band (CAP_IN; the odd row left over above it, as
 * everywhere): M (BPM, the song row, the octave, a message: capitals rows 6..16) from y 2, S (OCT, a key hint: rows 7..15)
 * from y 4 */
#define HEAD_SY CAP_IN(S, H_HEAD)
#define HEAD_MY CAP_IN(M, H_HEAD)
#define HEAD_TRK_X 4                                   /* the track cushion's cell */
#define HEAD_REC_X 20                                  /* the REC mark's cell (ink 23..33), after the slot letter if any */
#define HEAD_SLOT_X 23                                 /* the slot letter's pen (ui_draw.c head_slot) */
#define HEAD_BAT_X 214                                 /* the battery's cell */
#define HEAD_GRP_R 208                                 /* the octave / song row ends here */
#define HEAD_MSG_W 166                                 /* a message centred on x 120: 37..203 (after the REC mark) */
#define HEAD_GAP 4                                     /* ink to ink: the metronome -> the BPM -> the lock */
#define HEAD_PLAY_GAP 6                                /* the play state -> the metronome */
/* the header's centre (1.5.6): the play state, the metronome and the BPM (BPM LOCK ON: its lock) as one group, its
 * ink (gx0 .. gx1, past it) centred on x 120 (before: the BPM's alone, the rest hanging off to the left). The play
 * state's slot (px0 .. px1) is as wide as the wider of stop and play, so the group stays put on PLAY / STOP (the
 * narrower ink right-aligned in it, its gap to the metronome kept; a song's disc reaches a few px further left). The
 * digits are placed by as many 0s as the BPM has digits (tabular figures: the same advance), so nothing moves while
 * the tempo changes, only with a digit more or less (or BPM LOCK turned). bx: the pen of the BPM, b: its ink; the
 * metronome's ink (mx0 .. mx1, past it); the BPM's rolling strip (sx0 .. sx1: between the metronome's cell, either
 * way round, and the lock's) */
typedef struct { int32_t bx, b[4], mx0, mx1, sx0, sx1, px0, px1, gx0, gx1; } head_geo_t;
/* HA HA HE HU: the track rail, the eight tracks left in the header while no message shows (head_rail_on): a cell each
 * (RAIL_W x 16, RAIL_P apart from x 3, the MASTER's M first, 3 .. 16, then 18 .. 96), the track's colour bright while it sounds, faint while silent, a well
 * when muted; the selected one ringed (TEXT), an armed one ringed REC. The play group then starts at RAIL_GX (not
 * centred on 120) and the cushion, the slot letter and the REC mark of before give way to it */
#define RAIL_W 9
#define RAIL_P 10                                      /* (Dah: the MASTER's M first, then the tracks) */
#define RAIL_GX 104
#define RAIL_MW 13                                     /* (Dah) the MASTER's M: from x 3 */
#define RAIL_T0 (3 + RAIL_MW + 2)                      /* .. the tracks after it */
static int head_rail_on(void) { return !(ui.msg_t || ui.layer); }
static uint32_t rail_sounding(void)                    /* bit k: track k has a voice sounding */
{
    uint32_t k, i, m = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active && trk[k].v[i].env_out > 64) {
                m |= 1u << k;
                break;
            }
    return m;
}
static uint32_t rail_muted(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < NTRK; k++)
        if (trk[k].p[P_MUTE] || (perf_solo && !((perf_solo >> k) & 1u)))
            m |= 1u << k;
    return m;
}
static void head_rail(void)
{
    uint32_t k, snd = rail_sounding(), mu = rail_muted(), n = trk_count();
    {                                                  /* (Dah) the MASTER: an M, grey; picked (ALGORITHM left of 1): lit */
        int32_t x = 3;
        if (mx_master)
            cv_rrect(x - 1, 2, RAIL_MW + 2, 20, 4, T_TEXT, T_BG);
        cv_rrect(x, 4, RAIL_MW, 16, 3, mx_master ? T_TEXT : T_RAISE, mx_master ? T_TEXT : T_BG);
        GFX_HOOK_ALIGN(x, 4, x + RAIL_MW, 20, AL_HV, "rail digit centred");
        cv_text_in(x, 4 + CAP_IN(S, 16), RAIL_MW, &AF_S, "M", mx_master ? T_INK : T_MID, mx_master ? T_TEXT : T_RAISE);
    }
    if (n < NTRK) {                                    /* (HA HA HE HU) the project's tracks, then a "+": ALGORITHM on adds one */
        int32_t x = RAIL_T0 + (int32_t)n * RAIL_P;
        cv_rrect(x, 4, RAIL_W, 16, 3, T_LINE, T_BG);
        cv_rrect(x + 1, 5, RAIL_W - 2, 14, 2, T_BG, T_LINE);
        GFX_HOOK_ALIGN(x, 4, x + RAIL_W, 20, AL_HV, "rail digit centred");
        cv_text_in(x, 4 + CAP_IN(S, 16), RAIL_W, &AF_S, "+", T_DIM, T_BG);
    }
    for (k = 0; k < n; k++) {
        int32_t x = RAIL_T0 + (int32_t)k * RAIL_P, y = 4;
        int sel = k == song.sel && !mx_master, arm = (song.rec >> k) & 1u, on = (snd >> k) & 1u, m = (mu >> k) & 1u;
        uint16_t fill = m ? T_RAISE : on ? ux.trk[k] : ux_mix(ux.bg, ux.trk[k], 38);
        char d[2] = {(char)('1' + k), 0};
        if (sel || arm)
            cv_rrect(x - 1, y - 2, RAIL_W + 2, 20, 4, arm ? T_REC : T_TEXT, T_BG);
        cv_rrect(x, y, RAIL_W, 16, 3, fill, sel || arm ? (arm ? T_REC : T_TEXT) : T_BG);
        GFX_HOOK_ALIGN(x, y, x + RAIL_W, y + 16, AL_HV, "rail digit centred");
        cv_text_in(x, y + CAP_IN(S, 16), RAIL_W, &AF_S, d, m ? T_DIM : on ? T_INK : ux.trk[k], fill);
    }
}
static void head_geo(head_geo_t *g, const char *bpm)
{
    int32_t ib[4], lb[4], pb[4], c = 16, pw, lw = 0;
    uint32_t sz = 16;
    char z[8] = "0000000";
    z[str_len(bpm) < 7u ? str_len(bpm) : 7u] = 0;
    text_ink(&AF_M, z, g->b);
    icon_ink(16, ICON_TEMPO, ib);
    if (icon_cell(&sz, ICON_TEMPO) >= 0)
        c = (int32_t)sz;
    icon_ink(16, ICON_X_STOP, pb);                     /* the play state's slot: the wider of stop and play */
    pw = pb[2] - pb[0];
    icon_ink(16, ICON_X_PLAY, pb);
    if (pb[2] - pb[0] > pw)
        pw = pb[2] - pb[0];
    icon_ink(16, ICON_X_LOCK, lb);
    if (ui_prefs & PREF_BPM_LOCK)
        lw = HEAD_GAP + lb[2] - lb[0];
    g->gx0 = head_rail_on() ? RAIL_GX : HALF_UP(240 - (pw + HEAD_PLAY_GAP + (ib[2] - ib[0]) + HEAD_GAP + (g->b[2] - g->b[0]) + lw));
    g->px0 = g->gx0;
    g->px1 = g->px0 + pw;
    g->mx0 = g->px1 + HEAD_PLAY_GAP;
    g->mx1 = g->mx0 + (ib[2] - ib[0]);
    g->bx = g->mx1 + HEAD_GAP - g->b[0];
    g->gx1 = g->bx + g->b[2] + lw;
    g->sx0 = g->mx1 + (ib[0] > c - ib[2] ? ib[0] : c - ib[2]);
    g->sx1 = g->bx + g->b[2] + HEAD_GAP - lb[0];
}
/* the metronome with its ink at x0 (beat: click_beat_now; the 2nd and 4th mirrored, the same ink box; the 1st the
 * accent's). Mirrored by cv_flip: the cell's alpha drawn right to left, no second glyph */
static void head_metro(int32_t x0, int32_t beat)
{
    int32_t ib[4], c = 16;
    uint32_t sz = 16;
    icon_ink(16, ICON_TEMPO, ib);
    if (icon_cell(&sz, ICON_TEMPO) >= 0)
        c = (int32_t)sz;
    cv_flip = (uint8_t)(beat > 0 && (beat & 1));
    cv_icon_mid(x0 - (cv_flip ? c - ib[2] : ib[0]), H_HEAD / 2, 16, ICON_TEMPO, beat == 0 ? T_ACCENT : T_MID, T_BG);
    cv_flip = 0;
}
/* the width cv_free_hint(x, .., s, .., maxw) draws (its word, keycap and the ellipsised rest) */
static int32_t head_hint_w(const char *s, int32_t maxw)
{
    uint32_t n, i;
    int32_t id = kc_tag(s, &n), w = 0;
    char b[48];
    for (i = 0; id < 0 && i < 7u && s[i] && s[i] != ' '; i++)
        ;
    if (id < 0 && i && s[i] == ' ' && (id = kc_tag(s + i + 1u, &n)) >= 0) {
        char wd[8] = {0};
        uint32_t j;
        for (j = 0; j < i; j++)
            wd[j] = s[j];
        w = text_w(&AF_S, wd) + KH_GAP;
        s += i + 1u;
    }
    if (id >= 0 && kc_w((uint32_t)id) + KH_GAP < maxw - w) {
        w += kc_w((uint32_t)id) + KH_GAP;
        s += n;
        while (*s == ' ')
            s++;
        if (!*s)
            return w - KH_GAP;
    }
    text_fit(b, sizeof b, s, &AF_S, maxw - w);
    return w + text_w(&AF_S, b);
}
/* a key hint: a keycap first, or after the first word ("[SAVE] HOLD TO UNDO", "HOLD [GLO] QUICK"; cv_free_hint) */
static int head_is_hint(const char *s)
{
    uint32_t n, i;
    if (kc_tag(s, &n) >= 0)
        return 1;
    for (i = 0; i < 7u && s[i] && s[i] != ' '; i++)
        ;
    return i && s[i] == ' ' && kc_tag(s + i + 1u, &n) >= 0;
}
/* a message / a layer's label at x, or centred on x 120 (x < 0), at most maxw px with its
 * icon (ui.c MSG_NOFILE: the accent's) and a locked layer's lock. A message in M (as the panel's titles), a key hint
 * (a layer's label, "[SAVE] HOLD TO UNDO") in S as every keycap's word; a message M cannot hold in maxw (right of the
 * BPM; a long name) in S, ellipsised there if it must be */
static void head_msg(int32_t x, int32_t maxw)
{
    const char *m = ui.msg_t ? ui.msg : layer_head();
    int lock = !ui.msg_t && layer_locked();   /* #83: locked open (a double tap): the lock after its name */
    int icon = m[0] == MSG_NOFILE[0];
    int32_t lw = lock ? 15 : 0, iw = icon ? 16 + KH_GAP : 0;   /* the lock: its cell 6 px on, its ink to 15 */
    int big = !head_is_hint(m + icon) && text_w(&AF_M, m + icon) <= maxw - lw - iw;
    if (x < 0)
        x = 120 - HALF_UP(iw + (big ? text_w(&AF_M, m + icon) : head_hint_w(m + icon, maxw - lw - iw)) + lw);
    if (icon)
        x += cv_icon_mid(x, H_HEAD / 2, 16, ICON_X_NOFILE, T_ACCENT, T_BG) + KH_GAP;
    if (big) {
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        x = cv_text(x, HEAD_MY, &AF_M, m + icon, T_TEXT);
    } else {
        x = cv_free_hint(x, HEAD_SY, m + icon, T_TEXT, T_BG, maxw - lw - iw);   /* (may start with a keycap) */
    }
    if (lock)
        cv_icon_mid(x + 6, H_HEAD / 2, 16, ICON_X_LOCK, T_THEME, T_BG);
}
/* the slot letter after the track's cushion (1.2): the PROJECT slot of the pattern the selected track plays or edits.
 * A song playing: its section's slot for the track ("-" silent there); else the slot the working pattern came from
 * (ui.c pat_from: the last PROJECT load or save), with a mark ("A*") once it was edited since (its steps, LEN DIV SWING
 * GATE, QUANTIZE or automation: ui.c pat_from_sig); none at power-on, after a PHRASES pattern or the editor's restore.
 * b: the letter (b[1] '*' or 0); 0 = none */
static int head_slot(char *b)
{
    uint32_t k = song.sel % NTRK, s;
    b[1] = b[2] = 0;
    s = chain.running ? chain.lane[k] : ps_num(k);   /* (HA HA HE HU) the track's pattern, its number; a song: its lane's */
    if (s == CHAIN_SILENT) {
        b[0] = '-';
        return 1;
    }
    b[0] = (char)(s < 10u ? '0' + s : '0' + s / 10u);
    b[1] = (char)(s < 10u ? 0 : '0' + s % 10u);
    return 1;
}
/* HA HA HE HU: the pattern worked on, at the right while the rail shows: the selected track's slot letter (A..D; a song
 * playing: its lane's) on a badge in the track's colour, "*" after it when edited since; no slot: "-" outlined */
#define HEAD_PAT_W 24                                  /* (Dah: "P" and the number: which pattern, plainly) */
#define HEAD_PAT_X (HEAD_GRP_R - HEAD_PAT_W)
static void head_pattern(int slot, const char *sl)
{
    uint16_t tc = ux.trk[song.sel % NTRK];
    char l[3] = {slot ? sl[0] : '-', slot ? sl[1] : 0, 0};
    int on = slot && sl[0] != '-';
    if (on)
        cv_rrect(HEAD_PAT_X, 3, HEAD_PAT_W, 18, 4, tc, T_BG);
    else
    {
        cv_rrect(HEAD_PAT_X, 3, HEAD_PAT_W, 18, 4, T_LINE, T_BG);
        cv_rrect(HEAD_PAT_X + 1, 4, HEAD_PAT_W - 2, 16, 3, T_BG, T_LINE);
    }
    {                                                   /* (Dah) a small P, then the number: "P1" (two digits: all small) */
        const aafont_t *nf = l[1] ? &AF_S : &AF_M;
        int32_t w = text_w(&AF_S, "P") + 1 + text_w(nf, l), x = HEAD_PAT_X + (HEAD_PAT_W - w) / 2;
        int32_t sy = 3 + CAP_IN(M, 18) + AF_M_CAP_Y + AF_M_CAP_H - AF_S_CAP_Y - AF_S_CAP_H;   /* (S on M's baseline) */
        cv_text_on(x, sy, &AF_S, "P", on ? T_INK : T_MID, on ? tc : T_BG);
        cv_text_on(x + text_w(&AF_S, "P") + 1, l[1] ? sy : 3 + CAP_IN(M, 18), nf, l, on ? T_INK : T_MID, on ? tc : T_BG);
    }
}
/* the octave, or the song row while a song plays, its right end at HEAD_GRP_R (the rail: left of the pattern's badge,
 * the song row without its disc: the play state shows it) */
static void head_group(void)
{
    char b[8];
    int32_t x;
    if (chain.running && head_rail_on()) {
        fmt_int(b, (int32_t)chain.row + 1);
        x = HEAD_PAT_X - 4 - 4 - text_w(&AF_M, b);
    } else if (chain.running) {
        fmt_int(b, (int32_t)chain.row + 1);
        x = HEAD_GRP_R - (16 + 4 + text_w(&AF_M, b));
        x += cv_icon_mid(x, H_HEAD / 2, 16, ICON_X_SONG, T_MID, T_BG);   /* SONG: the disc */
    } else {
        str_cpy(b, song.octave > 0 ? "+" : "", sizeof b);
        fmt_int(b + str_len(b), song.octave);
        if (head_rail_on()) {                          /* (HA HA HE HU: the rail's BPM further right, no room for OCT:
                                                        * the signed number alone) */
            x = HEAD_PAT_X - 4 - 4 - text_w(&AF_M, b);
        } else {
            x = HEAD_GRP_R - (text_w(&AF_S, "OCT") + 4 + text_w(&AF_M, b));
            GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
            x = cv_text(x, HEAD_SY, &AF_S, "OCT", T_MID);
        }
    }
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(x + 4, HEAD_MY, &AF_M, b, T_THEME);
}
/* (Dah) HOME's header: where one is, plainly, left to right: the page, the pattern, the track (M, 1..8 on its colour)
 * and its sound (the engine, the preset); an armed REC at the right. A message, a layer or the tempo being changed:
 * the header of the pages (draw_head) */
static void sound_name(const track_t *t, char *b);
/* (Dah) the battery, upright and small: its level filled from the foot (an estimate from ADC ch3 between 510 and
 * 620; 20 % or less the accent); USB power: full, THEME. Its right end at x1; returns its left */
static int32_t head_batt(int32_t x1, int32_t hh)
{
    char b[8];
    int32_t bh = 11, bw = 6, by = (hh - bh) / 2 + 1, bx = x1 - bw, pct, f;
    int usbp = batt_shown() == 4;
    uint16_t c;
    pct = clamp(((int32_t)song.batt_raw - 510) * 100 / 110, 0, 100);
    c = usbp ? T_THEME : pct <= 20 ? T_ACCENT : T_TEXT;
    cv_rect(bx + 2, by - 2, 2, 2, c);                   /* its cap */
    cv_frame(bx, by, bw, bh, c);
    f = usbp ? bh - 4 : (bh - 4) * pct / 100;
    if (f > 0)
        cv_rect(bx + 2, by + bh - 2 - f, bw - 4, f, c);
    (void)b;
    return bx;                                          /* (Dah: no share written, the battery alone) */
}
/* (Dah) HOME's header, one size of text (S), lower (HOME_HEAD_H): the page, the pattern, the track (M, 1..8 on its
 * colour) and its sound; the tempo while it is changed, a message over the rest; an armed REC and the battery right */
static void sound_name(const track_t *t, char *b);
#define HL_MS 250u                                      /* (Dah) the header's load read this often */
static struct { uint8_t voices, cpu; uint32_t t; } hld;
static void head_home(void)
{
    char pn[16], sl[3], num[4] = {0}, ch[2], bp[12], ti[20];
    int32_t lw;
    uint32_t k = song.sel % NTRK, sig;
    uint16_t tc = mx_master ? T_TEXT : ux.trk[k];
    const char *en = mx_master ? "MASTER" : ENGINES[TSEL->eng_req % NENGINES]->name;
    int32_t x, xr, hh = HOME_HEAD_H, ty = CAP_IN(S, HOME_HEAD_H);
    if (mx_master)
        str_cpy(pn, "FX", sizeof pn);
    else
        sound_name(TSEL, pn);
    if (fm1_ms - hld.t >= HL_MS) {                       /* (the load, a few times a second: no flicker) */
        hld.t = fm1_ms;
        hld.voices = (uint8_t)voices_busy();
        hld.cpu = (uint8_t)(song.cpu_q8 * 100u / 256u > 99u ? 99u : song.cpu_q8 * 100u / 256u);
    }
    (void)head_slot(sl);
    if ((seq_off >> k) & 1u && !chain.running)          /* (the track on no pattern) */
        sl[0] = '-', sl[1] = 0;
    num[0] = 'P';
    num[1] = sl[0];
    num[2] = sl[1];
    ch[0] = mx_master ? 'M' : (char)('1' + k);
    ch[1] = 0;
    str_cpy(bp, "BPM ", sizeof bp);
    fmt_int(bp + 4, song.g[G_BPM]);
    sig = 0x484F4D45u ^ (str_hash(k * 7u + mx_master * 3u, pn) + str_hash(1u, en) * 31u + str_hash(2u, num) * 131u +
                         (uint32_t)song.rec * 977u + tc * 13u +
                         (ui.msg_t && (song.playing || chain_busy()) ? str_hash(5u, ui.msg) * 7u + 1u : 0u) +
                         (uint32_t)song.g[G_BPM] * 2654435761u + (ui.bpm_t != 0) * 3u + (uint32_t)batt_shown() * 40503u +
                         (uint32_t)song.batt_raw / 4u * 104729u + hld.voices * 15485863u + hld.cpu * 7919u +
                         (ui.home ? 0u : ui.page + 1u) * 3141592653u);
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, (uint32_t)hh, T_BG);
    xr = head_batt(236, hh) - 8;
    {   /* (Dah) the tempo: its figures (the accent while it is changed), BPM after them */
        int32_t w = text_w(&AF_S, bp + 4);
        GFX_HOOK_ALIGN(0, 0, 0, hh, AL_V, "home header text on its middle");
        xr = cv_text_r(xr, ty, &AF_S, "BPM", T_MID, T_BG) - 3;
        cv_text(xr - w, ty, &AF_S, bp + 4, ui.bpm_t ? T_ACCENT : T_TEXT);
        xr -= w + 8;
    }
    if (ui.home)                                        /* (the page's name: what the left side needs) */
        str_cpy(ti, "HOME", sizeof ti);
    else
        page_title(ti);
    lw = 6 + text_w(&AF_S, ti) + 8 + text_w(&AF_S, "PAT") + 4 + text_w(&AF_S, num + 1) + 8 + 14 + 4;
    {   /* (Dah) the load: the voices sounding of the budget ("6/8"), the audio's CPU ("42%"), read every HL_MS; the
         * accent when the budget is full or the CPU over 85 %. Not where the page's name leaves no room for it */
        char lv[8], lc[8];
        int32_t w;
        fmt_int(lv, (int32_t)hld.voices);
        str_cpy(lv + str_len(lv), "/", 2);
        fmt_int(lv + str_len(lv), NVOICE);
        fmt_int(lc, (int32_t)hld.cpu);
        str_cpy(lc + str_len(lc), "%", 2);
        w = text_w(&AF_S, lv);
        if (xr - text_w(&AF_S, lc) - 6 - w - (song.rec ? 14 : 0) >= lw) {
            GFX_HOOK_ALIGN(0, 0, 0, hh, AL_V, "home header text on its middle");
            xr = cv_text_r(xr, ty, &AF_S, lc, hld.cpu > 85u ? T_ACCENT : T_MID, T_BG) - 6;
            cv_text(xr - w, ty, &AF_S, lv, hld.voices >= NVOICE ? T_ACCENT : T_MID);
            xr -= w + 8;
        }
    }
    if (song.rec) {                                     /* REC: a dot, filled the selected track armed, a ring another */
        cv_rrect(xr - 8, (hh - 8) / 2, 8, 8, 4, T_REC, T_BG);
        if (!((song.rec >> song.sel) & 1u))
            cv_rrect(xr - 6, (hh - 8) / 2 + 2, 4, 4, 2, T_BG, T_REC);
        xr -= 14;
    }
    GFX_HOOK_ALIGN(0, 0, 0, hh, AL_V, "home header text on its middle");
    x = cv_text(6, ty, &AF_S, ti, T_MID) + 8;            /* the page (its place in its family: "DIST 2/5") */
    if (ui.msg_t && (song.playing || chain_busy())) {   /* (Dah) playing: a message small in the header (no window) */
        const char *m = ui.msg[0] == MSG_NOFILE[0] ? ui.msg + 1 : ui.msg;
        cv_free_hint(x, ty, m, T_ACCENT, T_BG, xr - x);
        cv_blit(0, Y_HEAD);
        return;
    }
    GFX_HOOK_ALIGN(0, 0, 0, hh, AL_V, "home header text on its middle");
    x = cv_text(x, ty, &AF_S, "PAT", T_MID) + 4;       /* the pattern: "PAT 1" */
    x = cv_text(x, ty, &AF_S, num + 1, sl[0] == '-' ? T_MID : T_THEME) + 8;
    cv_rect(x, (hh - 14) / 2, 14, 14, tc);              /* the track (square) */
    GFX_HOOK_ALIGN(x, (hh - 14) / 2, x + 14, (hh - 14) / 2 + 14, AL_HV, "header track cushion digit centred");
    cv_text_in(x, (hh - 14) / 2 + CAP_IN(S, 14), 14, &AF_S, ch, T_INK, tc);
    x += 14 + 7;
    (void)en;                                           /* (its sound: in the footer, home_foot) */
    cv_blit(0, Y_HEAD);
}
/* (Dah) HOME's footer, a band at the screen's foot: the shortcuts left (keycaps: OCT+ held the VIEW, SELECT the
 * pattern), the selected track's preset right (its name alone) */
static int fx_has_next(void);                           /* (ui_input.c: an effect with a second page) */
static void home_foot(void)
{
    static uint32_t fsig;
    char pn[16];
    int32_t y0 = 240 - HOME_FOOT_H, x, w;
    uint32_t sig;
    static const khint_t KH[4][2] = {{{KC_OCTUP, "VIEW"}, {KC_SELECT, "PAT"}},   /* HOME */
                                     {{KC_FX, "OPEN"}, {KC_REC, "DEL"}},         /* FX > CHAIN: the effect picked */
                                     {{KC_FX, "CHAIN"}, {KC_SELECT, "FX"}},      /* an effect's page (FX: back) */
                                     {{KC_FX, "NEXT"}, {KC_SELECT, "FX"}}};      /* .. with a second page */
    uint32_t hk = ui.home ? 0u : cur_page()->id[0] == P_FXORD ? 1u : fx_has_next() ? 3u : 2u;
    if (mx_master)
        str_cpy(pn, "FX", sizeof pn);
    else
        sound_name(TSEL, pn);
    sig = str_hash(77u + mx_master, pn) + (uint32_t)ux.gen * 31u + hk * 7919u;
    if (!ui.force && sig == fsig)
        return;
    fsig = sig;
    lcd_fill(0, (uint32_t)y0 - 2u, 240, 2, T_BG);
    cv_begin(240, HOME_FOOT_H, T_BG);
    x = cv_key_hint(4, (HOME_FOOT_H - KC_H) / 2, KH[hk][0].key, KH[hk][0].act, 1, T_BG) + 8;
    x = cv_key_hint(x, (HOME_FOOT_H - KC_H) / 2, KH[hk][1].key, KH[hk][1].act, 1, T_BG) + 10;
    w = text_w(&AF_S, pn);                              /* (the preset's name alone) */
    if (w > 236 - x)
        w = 236 - x;
    GFX_HOOK_ALIGN(0, 0, 0, HOME_FOOT_H, AL_V, "home footer text on its middle");
    cv_free_text(236 - w, CAP_IN(S, HOME_FOOT_H), &AF_S, pn, T_TEXT, T_BG, w);
    cv_blit(0, (uint32_t)y0);
}
static void draw_head(void)
{
    static int16_t metro_x = -1;                       /* the metronome's ink as drawn (-1: hidden), its beat */
    static int8_t metro_beat;
    char b[16];
    head_geo_t g;
    int32_t beat = -1;                                 /* the metronome stands still (its swing removed in 1.1.5) */
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u;   /* 2 the selected track armed, 1 another */
    char sl[3];
    int slot = head_slot(sl);
    uint32_t sig;
    if (home_bare()) {                                  /* (Dah) HOME: its own, plain */
        head_home();
        return;
    }
    sig = (uint32_t)song.playing * 3u + rec * 5u + (uint32_t)(song.octave + 8) * 11u + song.sel * 13131u +
                   (ui.msg_t ? str_hash(7u, ui.msg) : ui.layer * 7919u + (uint32_t)layer_locked() * 3u) + (uint32_t)song.g[G_BPM] * 101u + (ui.bpm_t != 0) * 31u +
                   (uint32_t)seq_counting() * 7u + (ui_prefs & PREF_BPM_LOCK) * 4099u +
                   (uint32_t)batt_shown() * 7777u + (chain.running ? (chain.row + 1u) * 104729u : 0u) +
                   (slot ? str_hash(11u, sl) : 0u) + (uint32_t)midi_lit(song.sel) * 15485863u +   /* (1.5.5) */
                   (head_rail_on() ? (rail_sounding() * 2654435761u) ^ (rail_muted() * 40503u) ^ song.rec * 977u : 0u) +   /* (the rail) */
                   trk_count() * 3141592653u + mx_master * 2246822519u;
    int msg = ui.msg_t || ui.layer;
    int centre = !msg || ui.bpm_t || ui.layer == LAYER_GLO;   /* the BPM being changed: it stays, the message right */
    int32_t rx = HEAD_REC_X;
    if (song.g[G_BPM] != ui.roll_bpm) {
        char a[8];
        fmt_int(a, ui.roll_bpm);
        fmt_int(b, song.g[G_BPM]);
        roll_note(ROLL_BPM, a, b, ui.force);
        ui.roll_bpm = song.g[G_BPM];
    } else if (ui.force) {
        ui.roll[ROLL_BPM].from[0] = 0;
    }
    if (!centre)                                       /* (a message over the BPM: no roll there to finish; CLOCK's */
        ui.roll[ROLL_BPM].from[0] = 0;                 /* BPM knob turned while one shows) */
    fmt_int(b, song.g[G_BPM]);
    head_geo(&g, b);
    if (!ui.force && sig == ui.head_sig) {
        if (ui.roll[ROLL_BPM].from[0] && centre) {     /* rolling: the BPM's strip only */
            cv_begin((uint32_t)(g.sx1 - g.sx0), H_HEAD, T_BG);
            roll_text(ROLL_BPM, g.bx - g.sx0, HEAD_MY, b, ui.bpm_t ? T_ACCENT : T_THEME);
            cv_blit((uint32_t)g.sx0, Y_HEAD);
        }
        if (metro_x >= 0 && beat != metro_beat) {      /* a beat: the metronome's cell only (either way round) */
            int32_t cx = g.mx0 - (g.sx0 - g.mx1);
            metro_beat = (int8_t)beat;
            cv_begin((uint32_t)(g.sx0 - cx), H_HEAD, T_BG);
            head_metro(g.mx0 - cx, beat);
            cv_blit((uint32_t)cx, Y_HEAD);
        }
        return;
    }
    ui.head_sig = sig;
    metro_x = -1;
    cv_begin(240, H_HEAD, T_BG);
    if (head_rail_on())                                /* (HA HA HE HU: the eight tracks) */
        head_rail();
    else
        cv_icon_mid(HEAD_TRK_X, H_HEAD / 2, 16, trk_icon(song.sel, 1), midi_lit(song.sel) ? T_TEXT : T_ACCENT, T_BG);   /* (1.5.5:
                                                       * a MIDI IN note into it, #208: lit) */
    if (slot && !head_rail_on() && (centre || !msg)) { /* the slot letter (not under a message centred: its room) */
        char l[2] = {sl[0], 0};
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        rx = cv_text(HEAD_SLOT_X, HEAD_MY, &AF_M, l, sl[0] == '-' ? T_MID : T_THEME);
        if (sl[1])                                     /* edited since: the mark, up by the letter's top */
            rx = cv_text(rx, HEAD_MY, &AF_M, "*", T_MID);
        rx += 1;                                       /* (the REC mark's cell after it, its ink 3 px on) */
    }
    if (!head_rail_on())
        draw_rec_mark(rx, T_BG);
    else
        head_pattern(slot, sl);                        /* (HA HA HE HU: the pattern, top right) */
    if (centre) {
        int32_t ib[4], lx = g.bx + g.b[2];             /* lx: the right end of the BPM (and its lock) */
        uint32_t id = song.playing ? (chain.running ? ICON_X_SONG : ICON_X_PLAY) : seq_counting() ? ICON_X_PLAY : ICON_X_STOP;
        icon_ink(16, id, ib);                          /* the play state: the song's disc, play (counting in: the
                                                        * accent's, about to play), stop */
        cv_icon_mid(g.px1 - ib[2], H_HEAD / 2, 16, id, /* (right-aligned in its slot) */
                    song.playing ? T_THEME : seq_counting() ? T_ACCENT : T_MID, T_BG);
        head_metro(g.mx0, beat);
        metro_x = (int16_t)g.mx0;
        metro_beat = (int8_t)beat;
        if (!ui.roll[ROLL_BPM].from[0])
            GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        roll_text(ROLL_BPM, g.bx, HEAD_MY, b, ui.bpm_t ? T_ACCENT : T_THEME);
        if (ui_prefs & PREF_BPM_LOCK) {                /* BPM LOCK ON (#58): the lock right of the BPM */
            icon_ink(16, ICON_X_LOCK, ib);
            cv_icon_mid(lx + HEAD_GAP - ib[0], H_HEAD / 2, 16, ICON_X_LOCK, T_MID, T_BG);
            lx += HEAD_GAP + ib[2] - ib[0];
        }
        if (msg) {                                     /* right of the BPM, over the right slot */
            int32_t x = lx + 9 > 142 ? lx + 9 : 142;
            head_msg(x, 236 - x);
        }
    } else {
        head_msg(-1, HEAD_MSG_W);
    }
    if (!msg && (chain.running || song.octave))
        head_group();
    if (!(msg && centre))
        draw_battery(HEAD_BAT_X);
    cv_blit(0, Y_HEAD);
}
/* LINE: 1 px dividers in the BG gaps between the strips, not around them: under the header, above and
 * below the panel, and between the four columns over rows y .. y + h (the cards; the mixer's strips too) */
#define RULE_X0 CARD_X(0)
#define RULE_W (CARD_X(3) + CARD_W - CARD_X(0))
static void lcd_rule(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    lcd_fill(x, y, w, h, T_RULE);
    GFX_HOOK_RULE(x, y, w, h);
}
/* The gaps are filled around the rules, never over them (#181: a rule filled BG, then drawn again, flashed on every
 * redraw). Rows y0 .. y1 - 1 across the screen; LINE: the rule on its middle row */
static void gap_rows(uint32_t y0, uint32_t y1)
{
    uint32_t r = (y0 + y1) / 2u;
    if (!ux.style) {
        lcd_fill(0, y0, 240, y1 - y0, T_BG);
        return;
    }
    lcd_fill(0, y0, 240, r - y0, T_BG);
    lcd_fill(0, r, RULE_X0, 1, T_BG);
    lcd_rule(RULE_X0, r, RULE_W, 1);
    lcd_fill(RULE_X0 + RULE_W, r, 240u - RULE_X0 - RULE_W, 1, T_BG);
    lcd_fill(0, r + 1u, 240, y1 - r - 1u, T_BG);
}
/* the BG beside the four columns (the cards; MIXER's strips) over rows y .. y + h - 1; LINE: the dividers */
static void draw_gaps(uint32_t y, uint32_t h)
{
    uint32_t i;
    lcd_fill(0, y, (uint32_t)CARD_X(0), h, T_BG);
    for (i = 0; i < 4u; i++) {                          /* right of each column */
        uint32_t x = (uint32_t)(CARD_X(i) + CARD_W), w = i < 3u ? (uint32_t)CARD_X(i + 1u) - x : 240u - x;
        if (ux.style && i < 3u) {
            lcd_rule(x, y, 1, h);
            lcd_fill(x + 1u, y, w - 1u, h, T_BG);
        } else {
            lcd_fill(x, y, w, h, T_BG);
        }
    }
}
/* full redraw: the strips (header, cards, panel, footer) cover the rest; only the BG between them is filled */
static void draw_frame(void)
{
    uint32_t ch = card_h(), gy = graph_y(), gh = graph_h();   /* (MENU > LARGE: tall cards, a strip) */
    gap_rows(head_h(), card_y());
    gap_rows(card_y() + ch, gy);
    gap_rows(gy + gh, home_bare() ? 240u - HOME_FOOT_H : foot_free() ? Y_FOOT + FOOT_GAIN : Y_FOOT);
    draw_gaps(card_y(), ch);
}

#ifndef CARD_HOOK
#define CARD_HOOK(v, u) ((void)0)                       /* host: a card value set in S (M too wide with its unit) */
#endif
/* MOTION on the cards (#63: values that move on their own): the selected track's motion-driven parameters
 * (motion.c motion_mask: PLAY ON and an event for the id), once per draw_columns; card_mot_next is set just
 * before a draw_column of a track parameter (card_mot_of) and taken by it; card_mot: the cards drawn with it. */
static uint32_t card_mot_mask[(P_COUNT + 31u) / 32u];
static uint8_t card_mot_next, card_mot;
static void card_mot_of(const int16_t *vp)
{
    uintptr_t id = ((uintptr_t)vp - (uintptr_t)TSEL->p) / sizeof *vp;   /* (not the track's: huge) */
    card_mot_next = (uint8_t)(id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u));
}

/* #251, MENU > LARGE: a tall card sets its value in one face over everything its knob can show (L where every value
 * fits L, else M, else S), so turning it never changes the value's size. card_span (what the knob shows: fn's text
 * of each value lo..hi) is set just before a draw_column (card_span_of / card_span_by) and taken by it; none: the
 * value's own face. The face is worked out when what the card is of changes (span_face's key), not every frame. */
typedef struct card_span {
    void (*fn)(const struct card_span *sp, int32_t v, char *val);   /* value v as the card shows it (val: 16) */
    const void *p;                                       /* fn's: a param_desc_t, a name list .. */
    int32_t lo, hi, ctx;
} card_span_t;
static card_span_t card_span;
static void span_param(const card_span_t *sp, int32_t v, char *val)
{
    const char *u;
    uint8_t named = fmt_named;                           /* (param_format sets it: the card's own, kept) */
    param_format((const param_desc_t *)sp->p, v, val, &u);
    fmt_named = named;
}
static void span_names(const card_span_t *sp, int32_t v, char *val)
{
    str_cpy(val, ((const char *const *)sp->p)[v], 16);
}
static void span_mod_dst(const card_span_t *sp, int32_t v, char *val)   /* MOD's DST: the track's (its engine's) names */
{
    str_cpy(val, mod_dst_name((const track_t *)sp->p, v), 16);
}
static void span_fop(const card_span_t *sp, int32_t v, char *val)       /* FM6's operator pages: card ctx's byte = v */
{
    const char *l, *u;
    int32_t r;
    uint32_t ic;
    (void)fop_card_at((uint32_t)sp->ctx, v, &l, val, &u, &r, &ic);
}
static void span_dln(const card_span_t *sp, int32_t v, char *val)       /* DRUM's lane pages: card ctx's value = v */
{
    const char *l, *u;
    int32_t r;
    uint32_t ic;
    (void)dln_card_at((uint32_t)sp->ctx, v, &l, val, &u, &r, &ic);
}
static void span_fxu(const card_span_t *sp, int32_t v, char *val)       /* (Dah) CHAIN's UNIT: the units' names */
{
    (void)sp;
    str_cpy(val, FXU_N[(uint32_t)v % FXU_N_ALL], 16);
}
static void span_src(const card_span_t *sp, int32_t v, char *val)       /* (Dah) SIDECHAIN's SOURCE 2 */
{
    (void)sp;
    str_cpy(val, N_DKSRC[(uint32_t)v % (NTRK + 1u)], 4);
}
static void span_fxmv(const card_span_t *sp, int32_t v, char *val)      /* (Dah) CHAIN's MOVE: the place, of 4 */
{
    (void)sp;
    val[0] = (char)('1' + v % 9); val[1] = '/'; val[2] = '9'; val[3] = 0;
}
static void span_mcol(const card_span_t *sp, int32_t v, char *val)      /* (Dah) MATCH's COLOR */
{
    static const char *const MCN[4] = {"FREE", "SCALE", "MAJOR", "MINOR"};
    (void)sp;
    str_cpy(val, MCN[v & 3], 8);
}
static void span_mnot(const card_span_t *sp, int32_t v, char *val)      /* (Dah) MATCH's NOTES */
{
    (void)sp;
    val[0] = (char)('0' + v); val[1] = 0;
}
static void span_fxrow(const card_span_t *sp, int32_t v, char *val)     /* (Dah) CHAIN's EFFECT: a row, or ADD */
{
    (void)sp;
    if (v >= 9) {
        str_cpy(val, "ADD", 4);
        return;
    }
    val[0] = (char)('1' + v); val[1] = '/'; val[2] = '9'; val[3] = 0;
}
static void card_span_by(void (*fn)(const card_span_t *, int32_t, char *), const void *p, int32_t lo, int32_t hi, int32_t ctx)
{
    card_span.fn = fn;
    card_span.p = p;
    card_span.lo = lo;
    card_span.hi = hi;
    card_span.ctx = ctx;
}
static void card_span_of(const param_desc_t *d)          /* a parameter: its whole range */
{
    card_span_by(span_param, d, d->min, d->max, (int32_t)d->fmt);
}
/* the face of a tall card's value s (0 L, 1 M, 2 S): L where the sparse L face has its glyphs and it fits with 3 px
 * either side, M where it fits with 2 (52 px: "THUMB"), else S (cut by cv_text_fit when even S is too wide) */
static uint32_t tall_face(const char *s)
{
    return large_face_has(s) && ink_w(&AF_L, s) <= COL_W - 6 ? 0u : ink_w(&AF_M, s) <= COL_W - 4 ? 1u : 2u;
}
static uint32_t span_face(uint32_t c, const card_span_t *sp, const char *label)
{
    static uint32_t key[4], face[4] = {3u, 3u, 3u, 3u};   /* (3: none yet) */
    uint32_t k = str_hash((uint32_t)(uintptr_t)sp->fn * 31u + (uint32_t)(uintptr_t)sp->p * 7u + (uint32_t)sp->lo * 131u +
                          (uint32_t)sp->hi * 8191u + (uint32_t)sp->ctx * 524287u + song.sel + TSEL->eng_req * 4u, label);
    int32_t v;
    char t[16];
    c &= 3u;
    if (k == key[c] && face[c] <= 2u)
        return face[c];
    key[c] = k;
    face[c] = 0;
    for (v = sp->lo; v <= sp->hi && face[c] < 2u; v++) {   /* (a few hundred values at most, once) */
        uint32_t f;
        sp->fn(sp, v, t);
        if ((f = tall_face(t)) > face[c])
            face[c] = f;
    }
    return face[c];
}

/* MENU > LARGE, a tall card (ui.c LK_TALL; 57 x LG_CARD_H): which knob it is (a "K1" pill in M, the keycap's colours,
 * the accent while it turns, DIM when the knob does nothing here) and the icon at the right of it; the label in M
 * centred (S when M is too wide); the value centred in L, its figures twice the height of M (M, then S, when L is too
 * wide or the sparse L face lacks a glyph: gen_aa_font.py L_CHARS; #251: for any value the knob shows, sp: one face
 * for all of them, span_face); the unit (S) centred under it; the gauge. The value snaps (no rolling digits). MONO,
 * the knob just turned: the label inverted as on the small cards. */
#ifndef LARGE_HOOK
#define LARGE_HOOK(s, why) ((void)0)                    /* host: a tall card's value and its face (0 L, 1 M: a glyph not
                                                         * in L, 2 M: L too wide, 3 S) */
#endif
#define LG_MY 4                                         /* the K pill: rows 4..21 */
#define LG_MH 18
#define LG_LY 24                                        /* the label's line: M capitals rows 28..38 */
#define LG_VB 49                                        /* the value's band: L capitals rows 49..69 */
#define LG_UY 73                                        /* the unit's line (S capitals rows 76..84) */
#define LG_GY 92                                        /* the gauge, 3 px */
static void draw_column_tall(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                             uint32_t icon, int hot, int mot, int32_t kid, const card_span_t *sp)
{
    uint16_t lc = hot ? T_ACCENT : mot ? T_THEME : T_MID;
    const aafont_t *lf = &AF_M, *vf = &AF_L;
    int32_t ly = LG_LY, vy, lw = 0, mw;
    char k[3] = {'K', (char)('1' + c), 0};
    uint32_t ic = mot ? ICON_X_MOTION : icon, isz = 16u;
    cv_begin(COL_W, LG_CARD_H, T_BG);
    cv_rrect(0, 0, COL_W, LG_CARD_H, 4, T_SURF, T_BG);
    {   /* the knob: "K1" .. "K4" on a pill */
        uint16_t fill = hot ? T_ACCENT : label[0] || val[0] ? T_KEY : T_DIM;
        mw = ink_w(&AF_M, k) + 10;
        cv_rrect(5, LG_MY, mw, LG_MH, 5, fill, T_SURF);
        GFX_HOOK_ALIGN(5, LG_MY, 5 + mw, LG_MY + LG_MH, AL_HV, "large card knob pill");
        cv_text_in(5, LG_MY + CAP_IN(M, LG_MH), mw, &AF_M, k, T_INK, fill);
    }
    if (!label[0] && !val[0]) {                         /* a knob that does nothing on this page */
        cv_blit((uint32_t)CARD_X(c), Y_LABEL);
        return;
    }
    if (FELUCCA_ICONS && ic != ICON_NONE && label[0] && (ic - ICON_TRK < NTRK || icon_cell(&isz, ic) >= 0)) {
        GFX_HOOK_ALIGN(0, LG_MY, 0, LG_MY + LG_MH, AL_V, "large card icon on its pill's line");
        cv_icon_in(COL_W - 5 - 16, LG_MY, 16, LG_MH, 16, ic, mot ? (hot ? T_ACCENT : T_THEME) : lc, T_SURF);
    }
    if (label[0]) {
        if ((lw = ink_w(lf, label)) > COL_W - 6) {      /* M too wide: S, on the same capitals' middle */
            lf = &AF_S;
            ly = LG_LY + AF_M_CAP_Y + HALF_UP(AF_M_CAP_H - AF_S_CAP_H) - AF_S_CAP_Y;
            lw = ink_w(lf, label);
        }
        if (hot && T_ACCENT == T_MID) {                 /* MONO: the label inverted, the chip 2 px round its ink */
            int32_t cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
            int32_t x0 = HALF_UP(COL_W - lw) - 2;
            cv_rrect(x0, cy - 3, lw + 4, chh + 6, 3, T_ACCENT, T_SURF);
            GFX_HOOK_ALIGN(x0, cy - 3, x0 + lw + 4, cy + chh + 3, AL_HV, "large card label chip (MONO hot)");
            cv_text_in(0, ly, COL_W, lf, label, T_INK, T_ACCENT);
        } else {
            GFX_HOOK_ALIGN(0, 0, COL_W, 0, AL_H, "large card label centred");
            cv_text_in(0, ly, COL_W, lf, label, lc, T_SURF);
        }
    }
    if (kid >= 0) {                                     /* "[OCT+]": the keycap, centred */
        GFX_HOOK_ALIGN(0, LG_VB, COL_W, LG_VB + AF_L_CAP_H, AL_HV, "large card keycap centred");
        cv_keycap(HALF_UP(COL_W - kc_w((uint32_t)kid)), LG_VB + HALF_UP(AF_L_CAP_H - KC_H), (uint32_t)kid,
                  vc == T_ACCENT || vc == T_DIM ? vc : T_KEY, T_INK, T_SURF);
    } else if (val[0]) {
        uint32_t f = tall_face(val), fs = sp->fn ? span_face(c, sp, label) : 0u;
        int why;
        if (fs > f)
            f = fs;                                     /* (the value's own where it needs a smaller one) */
        vf = f == 0u ? &AF_L : f == 1u ? &AF_M : &AF_S;
        why = f == 2u ? 3 : f == 1u ? (!large_face_has(val) ? 1 : 2) : 0;
        LARGE_HOOK(val, why);
        vy = vf == &AF_L ? LG_VB - AF_L_CAP_Y : vf == &AF_M ? LG_VB + HALF_UP(AF_L_CAP_H - AF_M_CAP_H) - AF_M_CAP_Y
                                                          : LG_VB + HALF_UP(AF_L_CAP_H - AF_S_CAP_H) - AF_S_CAP_Y;
        if (vf == &AF_S && ink_w(vf, val) > COL_W - 4) {   /* (a value always fits: the lint reports one cut) */
            cv_text_fit(2, vy, vf, val, vc, T_SURF, COL_W - 4);
        } else {
            GFX_HOOK_ALIGN(0, LG_VB, COL_W, LG_VB + AF_L_CAP_H, AL_HV, "large card value centred");
            cv_text_in(0, vy, COL_W, vf, val, vc, T_SURF);
        }
    }
    if (unit[0]) {
        GFX_HOOK_ALIGN(0, 0, COL_W, 0, AL_H, "large card unit centred");
        cv_text_in(0, LG_UY, COL_W, &AF_S, unit, T_MID, T_SURF);
    }
    if (ratio >= 0) {
        int32_t gw = COL_W - 10, fx = ratio * gw / 1000;
        cv_rrect(5, LG_GY, gw, 3, 1, ux.style ? T_LINE : T_BG, T_SURF);
        cv_rrect(5, LG_GY, fx < 3 ? 3 : fx, 3, 1, hot ? T_ACCENT : vc == T_DIM ? T_DIM : T_THEME, T_BG);
    }
    cv_blit((uint32_t)CARD_X(c), Y_LABEL);
}

/* one card = one knob: [icon] LABEL / value unit / gauge on a SURF card, redrawn only when it changed.
 * The value is M, or S when M is too wide (engine names, long ENUMs); the unit S after it. The gauge is
 * a 3 px rounded bar: BG track, THEME fill. The knob just turned (hot): icon, label, value and gauge in
 * the accent. ratio: 0..1000 for the gauge, -1 = no gauge. icon: ICON_* (icons.c), ICON_AUTO = by label;
 * a label too long to share the card with its icon goes without it. A parameter MOTION drives (card_mot_next):
 * the motion icon in place of its own, in the theme colour; a long label moves it left, up to the card's edge;
 * a label with no room for it at all (or FELUCCA_ICONS=0): the label in the theme colour.
 * vc: the value's colour (T_THEME, T_DIM inactive, T_ACCENT the knob just turned) */
/* (Dah) HOME's card: a knob. Its name on top (TEXT), the ring under it in the track's colour (THEME, tinted: its whole
 * travel faint, the value's arc full, a TEXT mark where it stands), the value in the middle (TEXT), its unit under it
 * when it fits; the knob just turned: its mark and name in the accent. 57 x HOME_CARD_H at the card's place */
#define SQ_W 72                                         /* (Dah) a value too wide for its knob: drawn here first */
#define SQ_H 14
#define sq_px (cv_px + CV_MAX - SQ_W * SQ_H)              /* (the canvas' far end: no RAM of its own) */
static int32_t sq_w;
#define HK_END 320                                      /* HOME's ring: 225 degrees, open at the bottom for the name */
static const int16_t KC_SIN[65] = {0, 101, 201, 301, 401, 501, 601, 700, 799, 897, 995, 1092, 1189, 1285, 1380, 1474,
    1567, 1660, 1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967, 3035, 3102,
    3166, 3229, 3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822, 3857, 3889, 3920, 3948, 3973, 3996,
    4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095, 4096};   /* sin, Q12, a quarter turn in 64 */
static int32_t kc_sin(int32_t a)                        /* a: 1/1024 turn */
{
    int32_t q = ((a % 1024) + 1024) % 1024, i = q % 256, s = q < 512 ? 1 : -1;
    if ((q / 256) & 1)
        i = 256 - i;
    return s * KC_SIN[i / 4];
}
static void knob_card(uint32_t c, const char *label, const char *val, const char *unit, int32_t ratio, int hot)
{
    char key[48], nm[20];
    int32_t r = KNOB_HOME_R, bx = (COL_W - 2 * r) / 2, by = 1, cx = bx + r, cy = by + r, a1, mr = r - 1, e = HK_END;
    uint16_t tc = T_THEME;
    const aafont_t *vf = &AF_M;
    uint32_t n;
    str_cpy(nm, label, 12);                             /* the name, its unit after it ("ATK ms"; too wide: the name
                                                         * alone) */
    if (unit[0] && !str_eq(unit, label) && text_w(&AF_S, label) + text_w(&AF_S, " ") + text_w(&AF_S, unit) <= COL_W - 4) {
        str_cpy(nm + str_len(nm), " ", 2);
        str_cpy(nm + str_len(nm), unit, 7);
    }
    str_cpy(key, "@", 2);                               /* (its own: the cards of the pages differ) */
    str_cpy(key + 1, nm, 20);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), val, 14);
    n = str_len(key);
    key[n] = (char)('A' + (tc & 15u));
    key[n + 1] = (char)('A' + ((tc >> 4) & 15u));
    key[n + 2] = (char)('A' + ((tc >> 8) & 15u));
    key[n + 3] = (char)('A' + (tc >> 12));
    key[n + 4] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio * 90 / 1000));
    key[n + 5] = (char)('0' + hot);
    key[n + 6] = 0;
    if (!ui.force && str_eq(key, ui.col[c]))
        return;
    str_cpy(ui.col[c], key, sizeof ui.col[c]);
    ui.roll[c].from[0] = 0;                             /* (no rolling digits here) */
    if (text_w(&AF_M, val) > 2 * (r - 4) && text_w(&AF_S, val) > 2 * (r - 4)) {   /* (Dah) too wide even in S: drawn
                                                         * apart first, then narrowed into the ring (sq_*) */
        sq_w = (int32_t)text_w(&AF_S, val) + 2;
        if (sq_w > SQ_W)
            sq_w = SQ_W;
        cv_begin((uint32_t)sq_w, SQ_H, T_SURF);
        cv_text(1, 0, &AF_S, val, T_TEXT);
        memmove(sq_px, cv_px, (size_t)sq_w * SQ_H * sizeof cv_px[0]);
    }
    cv_begin(COL_W, HOME_CARD_H, T_BG);
    cv_rrect(0, 0, COL_W, HOME_CARD_H, 4, T_SURF, T_BG);
    a1 = ratio < 0 ? -e - 1 : -e + clamp(ratio, 0, 1000) * 2 * e / 1000;
    ka_end = e;
    knob_arc(bx, by, r, KNOB_HOME_COV, KNOB_HOME_ANG, -e, a1, ux_mix(T_SURF, tc, 30), tc, T_SURF);
    ka_end = KA_END;
    if (ratio >= 0) {                                   /* the mark where it stands */
        int32_t mx = cx + kc_sin(a1) * mr / 4096, my = cy - kc_sin(a1 + 256) * mr / 4096;
        uint16_t mc = hot ? T_ACCENT : T_TEXT;          /* (a round dot, 4 px) */
        cv_rect(mx - 1, my - 2, 2, 4, mc);
        cv_rect(mx - 2, my - 1, 4, 2, mc);
    }
    if (text_w(vf, val) > 2 * (r - 4))
        vf = &AF_S;
    {
        int32_t vh = vf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H, ty = cy - 2 - vh / 2, ly = by + 2 * r - AF_S_CAP_H - 1;
        int32_t mw = 2 * (r - 4), w = text_w(vf, val), vy = ty - (vf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y);
        if (w <= mw) {                                  /* the value, in the middle */
            GFX_HOOK_ALIGN(cx - r, ty, cx + r, ty + vh, AL_H, "knob card value centred");
            cv_text_in(cx - r, vy, 2 * r, vf, val, T_TEXT, T_SURF);
        } else {                                        /* (Dah) too wide even in S: narrowed into the ring (never
                                                         * cut, never over it): each column two of the text's blended */
            int32_t xx, yy, x0 = cx - mw / 2;
            for (yy = 0; yy < SQ_H && vy + yy < (int32_t)cv_h; yy++)
                for (xx = 0; xx < mw; xx++) {
                    int32_t s0 = xx * sq_w / mw, s1 = (2 * xx + 1) * sq_w / (2 * mw);
                    uint16_t a0 = swap16(sq_px[yy * sq_w + s0]), a1 = swap16(sq_px[yy * sq_w + (s1 < sq_w ? s1 : s0)]);
                    if (vy + yy >= 0)
                        cv_px[(uint32_t)(vy + yy) * cv_w + (uint32_t)(x0 + xx)] = swap16(ux_mix(a0, a1, 50));
                }
        }
        if (text_w(&AF_S, nm) <= COL_W - 4) {          /* the name under it, in the ring's opening (under its ends:
                                                         * the card's width) */
            uint32_t nl = str_len(label) < 12u ? str_len(label) : 11u;   /* (Dah: the name TEXT, its unit MID) */
            int32_t nx = 2 + (COL_W - 4 - text_w(&AF_S, nm)) / 2;
            if (nl == str_len(nm))                      /* (no unit: the name alone, centred; with one, the pair) */
                GFX_HOOK_ALIGN(2, ly, COL_W - 2, ly + AF_S_CAP_H, AL_H, "knob card label centred");
            nm[nl] = 0;
            nx = cv_text(nx, ly - AF_S_CAP_Y, &AF_S, nm, hot ? T_ACCENT : T_TEXT);
            if (unit[0] && !str_eq(unit, label) && text_w(&AF_S, label) + text_w(&AF_S, " ") + text_w(&AF_S, unit) <= COL_W - 4)
                cv_text(nx + text_w(&AF_S, " "), ly - AF_S_CAP_Y, &AF_S, unit, T_MID);
        } else
            cv_free_text(2, ly - AF_S_CAP_Y, &AF_S, nm, hot ? T_ACCENT : T_TEXT, T_SURF, COL_W - 4);
    }
    cv_blit((uint32_t)CARD_X(c), card_y());
}
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                        int32_t ratio, uint32_t icon)
{
    char key[48];
    int hot = c == ui.hot_col && ui.hot_t, named = fmt_named, mot = card_mot_next && label[0], strip, snap;
    uint32_t kind = large_kind();                       /* MENU > LARGE: tall (draw_column_tall), or the label in M */
    uint8_t sig;
    uint16_t lc = hot ? T_ACCENT : T_MID;
    int32_t x, lx = 5, uw, room = COL_W - 8;
    const aafont_t *vf = &AF_M;
    uint32_t n, kn;
    int32_t kid = kc_tag(val, &kn);
    card_span_t sp = card_span;                         /* (#251: what the knob shows, this card only) */
    card_span.fn = 0;
    fmt_named = 0;                                      /* (params.c: a name, for this card only) */
    card_mot_next = 0;                                  /* (the same: MOTION, this card only) */
    if (home_bare()) {                                  /* (Dah) HOME: knobs */
        (void)vc; (void)icon; (void)sp; (void)kn; (void)kid; (void)lx; (void)uw; (void)room; (void)sig; (void)snap;
        (void)strip; (void)x; (void)lc; (void)vf; (void)n; (void)named; (void)mot; (void)kind;
        knob_card(c, label, val, str_eq(unit, label) ? "" : unit, ratio, hot);
        return;
    }
    card_mot = (uint8_t)((card_mot & ~(1u << c)) | (uint32_t)mot << c);
    if (str_eq(unit, label))
        unit = "";                                      /* "BPM 124 BPM", "USB OFF USB": the label says it */
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    str_cpy(key, label, 12);                            /* cache key: texts + colour + gauge */
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), val, 14);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), unit, 8);
    n = str_len(key);
    /* every RGB565 bit: status colours can change with identical text */
    key[n] = (char)('A' + (vc & 15u));
    key[n + 1] = (char)('A' + ((vc >> 4) & 15u));
    key[n + 2] = (char)('A' + ((vc >> 8) & 15u));
    key[n + 3] = (char)('A' + (vc >> 12));
    key[n + 4] = (char)(' ' + (ratio < 0 ? 0 : 1 + ratio * (COL_W - 10) / 1000));   /* the gauge's own pixel (as
                                                        * drawn: gw; ratio / 20 missed a pixel inside a step, #63 soak) */
    key[n + 5] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
    key[n + 6] = (char)('0' + hot + 2 * mot + 4 * (int)kind);
    key[n + 7] = 0;
    uw = unit[0] ? text_w(&AF_S, unit) + 3 : 0;
    if (text_w(vf, val) + uw > room) {
        vf = &AF_S;
        CARD_HOOK(val, unit);                           /* (host: the values set in S) */
    }
    sig = (uint8_t)str_hash(str_hash(song.sel + TSEL->eng_req * 4u + ux.gen * 64u, label), unit);
    snap = ui.force || sig != ui.roll[c].sig;           /* what the value is of: label, unit, track, engine, palette */
    if (kind == LK_TALL)
        ui.roll[c].from[0] = 0;                         /* (a tall card: no rolling digits) */
    strip = !snap && str_eq(key, ui.col[c]);
    if (strip && !ui.roll[c].from[0])
        return;
    if (strip) {                                        /* rolling: the value strip only */
        cv_begin(COL_W, ROLL_H, T_SURF);
        cv_oy = -ROLL_Y;
    } else {
        char ov[16];                                    /* the value drawn before (in the cache key) */
        const char *k = ui.col[c];
        uint32_t i = 0;
        while (*k && *k != '|')
            k++;
        while (*k && k[1] && k[1] != '|' && i + 1u < sizeof ov)
            ov[i++] = *++k;
        ov[i] = 0;
        ui.roll[c].sig = sig;
        if (!str_eq(ov, val))
            roll_note(c, ov, val, snap || named || vf != &AF_M || kid >= 0 || kind == LK_TALL);
        else if (snap)
            ui.roll[c].from[0] = 0;
        str_cpy(ui.col[c], key, sizeof ui.col[c]);
        if (kind == LK_TALL) {
            draw_column_tall(c, label, val, unit, vc, ratio, icon, hot, mot, kid, &sp);
            return;
        }
        cv_begin(COL_W, COL_H, T_BG);
        cv_rrect(0, 0, COL_W, COL_H, 4, T_SURF, T_BG);
    }
    if (label[0] || val[0]) {
        if (!strip && label[0]) {
            /* MENU > LARGE on a list / graph page or a layer (LK_LABEL): the label in M where it fits the card (its
             * capitals rows 5..15, the icon on them when it fits too), else in S as without LARGE */
            const aafont_t *lf = kind == LK_LABEL && 5 + text_w(&AF_M, label) <= COL_W - 2 ? &AF_M : &AF_S;
            int32_t lw = text_w(lf, label), ix = 5, ly = lf == &AF_M ? 1 : 3;
            int32_t cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
            uint32_t ic = mot ? ICON_X_MOTION : icon;
            if (mot && ix + ICON_CELL + ICON_GAP + lw > COL_W - 2)
                ix = COL_W - 2 - lw - ICON_GAP - ICON_CELL;   /* a long label: MOTION's icon moves left */
            if (FELUCCA_ICONS && ic != ICON_NONE && ix >= 1 && ix + ICON_CELL + ICON_GAP + lw <= COL_W - 2) {
                GFX_HOOK_ALIGN(ix, cy, ix, cy + chh, AL_V, "card icon on its label's line");
                lx = ix + cv_icon_in(ix, cy, 0, chh, ICON_CELL, ic,   /* its ink on the label's line */
                                     mot ? (hot ? T_ACCENT : T_THEME) : lc, T_SURF) + ICON_GAP;
            }
            else if (mot)                               /* the label from x 19 (5 + 12 + 2) */
                lc = hot ? T_ACCENT : T_THEME;          /* no room for MOTION's icon: the label says it */
            if (hot && T_ACCENT == T_MID) {             /* MONO: no colour tells the hot knob: its label inverted, */
                int32_t b[4], w;                        /* the chip 2 px round its ink (M: 3 above, 2 below) */
                text_ink(lf, label, b);
                w = b[2] - b[0] + 4;
                if (w > COL_W + 1 - lx - b[0]) w = COL_W + 1 - lx - b[0];
                cv_rrect(lx + b[0] - 2, cy - 3, w, lf == &AF_M ? chh + 5 : 15, 3, T_ACCENT, T_SURF);
                GFX_HOOK_ALIGN(lx + b[0] - 2, cy - 3, lx + b[0] - 2 + w, lf == &AF_M ? cy + chh + 2 : 18, AL_HV,
                               "card label chip (MONO hot)");
                cv_text_fit(lx, ly, lf, label, T_INK, T_ACCENT, COL_W - 2 - lx);
            } else
                cv_text_fit(lx, ly, lf, label, lc, T_SURF, COL_W - 2 - lx);
        }
        if (kid >= 0)                                   /* "[OCT+]": the keycap (accent: it would act; DIM: it would not) */
            x = cv_keycap(5, 20, (uint32_t)kid, vc == T_ACCENT || vc == T_DIM ? vc : T_KEY, T_INK, T_SURF);
        else if (vf == &AF_M) {
            if (!ui.roll[c].from[0] && val[0])
                GFX_HOOK_ALIGN(0, ROLL_Y, COL_W, ROLL_Y + ROLL_H, AL_V, "card value in its roll window");
            x = roll_text(c, 5, 17, val, vc);
        } else
            x = cv_text_fit(5, 20, vf, val, vc, T_SURF, room - uw);
        if (unit[0]) {
            GFX_HOOK_ALIGN(0, 0, 0, vf == &AF_M ? 17 + AF_M.asc : 20 + AF_S.asc, AL_B, "card unit on its value's baseline");
            cv_text_on(x + 3, 20, &AF_S, unit, T_MID, T_SURF);     /* on the value's baseline (S) */
        }
        if (!strip && ratio >= 0) {
            int32_t gw = COL_W - 10, fx = ratio * gw / 1000;
            cv_rrect(5, 38, gw, 3, 1, ux.style ? T_LINE : T_BG, T_SURF);   /* (LINE: no card to cut it from) */
            cv_rrect(5, 38, fx < 3 ? 3 : fx, 3, 1, hot ? T_ACCENT : vc == T_DIM ? T_DIM : T_THEME, T_BG);
        }
    }
    cv_oy = 0;
    cv_blit((uint32_t)CARD_X(c), Y_LABEL + (strip ? ROLL_Y : 0));
}

/* an action's column (act_cols): picked, the OCT+ keycap (accent while it would do something); else "--" */
static void draw_act_column(uint32_t c, const char *label, uint16_t vc, uint32_t icon)   /* icon: ICON_AUTO = by label */
{
    int picked = act_col() == c + 1u;
    draw_column(c, label, picked ? "[OCT+]" : "--", "", picked ? (act_ready() ? T_ACCENT : T_DIM) : vc, -1, icon);
}

/* action pages: the footer's first row says what OCT+ and OCT- do ("OCT+ LOAD   OCT- BACK") */
static void foot_hint(char *a, char *b)
{
    uint32_t c = act_col();
    str_cpy(a, "OCT+ ", 8);
    str_cpy(a + 5, c ? act_name(c - 1u) : "--", 11);   /* (Dah: "HOLD VIEW") */
    str_cpy(b, ui.act && cur_page()->graph != GR_PATS ? "OCT- CANCEL" : "", 16);   /* (Dah: no BACK) */
}

/* SAVE > USER / PROJECT: EDIT renames the selected slot (ui_name.c); SEQ > AUTOMATION: EDIT deletes the row's
 * record (ui_events.c); 0 = not such a page, 1 an empty slot (PLAY, QUANTIZE, + ADD), 2 used */
/* (Dah) the list pages: REC deletes the row picked; 0 none, 1 nothing there to delete, 2 something */
static uint32_t foot_del(void)
{
    uint32_t g = ui.home ? GR_NONE : cur_page()->graph;
    if (g == GR_SONG)
        return 1u + (ui.song_row < chain_config.count);
    return 0;
}
static uint32_t foot_rename(void)
{
    uint32_t g = ui.home ? GR_NONE : cur_page()->graph;
    if (g == GR_EVENTS) {
        uint16_t rw[EV_ROWS];
        uint32_t k = ev_cur(rw, 0) >> 8;
        return k == EVK_TOP || k == EVK_QNT || k == EVK_ADD ? 1u : 2u;
    }
    if (g == GR_USER)
        return 1u + (uint32_t)up_used(ui.uslot);
    if (g == GR_SLOTS)
        return 1u + (uint32_t)graph_project_used((uint32_t)song.g[G_SLOT] - 1u);
    return 0;
}

/* the sound's name on the track: a user preset or the engine's preset (b holds 16) */
static void sound_name(const track_t *t, char *b)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    b[0] = 0;
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 16);
}

static void draw_foot_in(void);
/* HA HA HE HU: the footer's steps and engine in the selected track's colour, the page's name in its family's */
static void draw_foot(void)
{
    ux_tint(ux.trk[song.sel % NTRK]);
    draw_foot_in();
    ux_untint();
}
/* (Dah) the piano roll's footer row 1: the automation lane, not the steps (the roll shows them) */
static int lane_shown(const track_t *t)
{
    return !ui.home && !ui.menu && cur_page()->graph == GR_ROLL && !drum_track(t) && !detail_on();
}
/* the lane: the parameter's short name at the left, under each of the page's steps a bar of its value (a point, its
 * lock or event, bright; held from the one before, dim), the cursor's step framed; none yet: how to make one */
static void draw_lane(const track_t *t)
{
    uint32_t i, id = lane_id(t), len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    const param_desc_t *d;
    if (id == 0xFFu) {
        khint_t kh = {KC_OCTUP, "HOLD VIEW"};          /* (Dah: as SONG's, the same keycap row) */
        cv_key_row(8, 232, 2, &kh, 1, 3u, T_BG);
        return;
    }
    {   /* (Dah) the track's automated parameters, a lane each (4 at most), tiny: the one SELECT edits (its name at the
         * left) in the colour, the others MID */
        uint8_t ids[4];
        uint32_t r, nl = lane_ids(t, ids);
        int32_t ramp = trkx[trk_index(t) % NTRK].curve != 0u, lh = 16 / (int32_t)nl;
        cv_text_fit(4, 4, &AF_S, track_desc(t, id)->label, T_THEME, T_BG, 28);
        for (r = 0; r < nl; r++) {
            uint32_t pid = ids[r];
            int32_t px = -1, py = 0, y0 = 1 + (int32_t)r * lh, a = lh - 1;
            uint16_t col = pid == id ? T_THEME : T_MID;
            d = track_desc(t, pid);
            for (i = 0; i < 16u && base + i < len; i++) {
                int pt;
                int32_t v = lane_value(t, base + i, pid, &pt), x = 35 + (int32_t)i * 12 + 6;
                int32_t y = d->max > d->min ? y0 + a - (v - d->min) * a / (d->max - d->min) : y0 + a;
                if (px >= 0) {
                    if (ramp && pt)
                        cv_line(px, py, x, y, col);
                    else {
                        cv_line(px, py, x, py, col);
                        if (y != py)
                            cv_line(x, py, x, y, col);
                    }
                }
                if (pt)
                    cv_rect(x - 1, y, 2, 1, T_ACCENT);  /* a point */
                px = x;
                py = y;
            }
        }
        if (song.seq_mode && ui.cursor >= base && ui.cursor < base + 16u)
            cv_rect(35 + (int32_t)(ui.cursor - base) * 12 + 2, 17, 9, 1, T_TEXT);   /* the step SELECT edits */
    }
}
static void draw_foot_in(void)
{
    char s[48], pn[16], ti[20], ln[24];
    const track_t *t = TSEL;
    uint32_t sig;
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *ename = e->name, *help;
    int32_t x;
    sound_name(t, pn);
    if (ui.home)
        str_cpy(ti, "HOME", sizeof ti);
    else                                               /* page title + number in its family: "ENV DEST 2/2" */
        page_title(ti);
    str_cpy(s, ename, sizeof s);
    s[str_len(s) + 1u] = 0;
    s[str_len(s)] = (char)('1' + song.sel);
    str_cpy(s + str_len(s), pn, 16);
    str_cpy(s + str_len(s), ti, sizeof ti);
    {   /* step markers: the playhead only when it is in the shown bank, the cursor only in SEQ */
        uint32_t ph = song.playing && t->seq_idx / 16u == ui.bank ? t->seq_idx : 0xFFu;
        sig = str_hash(0x9E3779B9u, s) + ph * 97u + (song.seq_mode ? ui.cursor : 0xFFu) * 3001u + steps_hash(t) +
              (ui.home ? 0u : page_icon(pg)) * 7121u +
              ui.bank * 7u + (uint32_t)t->p[P_SLEN] * 13u;
        if (lane_shown(t)) {                          /* (Dah) STEP's automation lane: its parameter and values */
            uint32_t i, id = lane_id(t);
            int pt;
            sig += id * 50331653u;
            uint8_t ids[4];
            uint32_t nl = lane_ids(t, ids);
            for (i = 0; i < nl * 16u; i++)              /* (the lanes shown) */
                sig = sig * 31u + (uint32_t)(lane_value(t, ui.bank * 16u + i % 16u, ids[i / 16u], &pt) + 128) * 2u +
                      (uint32_t)pt + ids[i / 16u] * 7u;
        }
    }
    if (act_cols()) {                                  /* the hint, and whether OCT+ would act */
        char ha[16], hb[16];
        foot_hint(ha, hb);
        sig += str_hash(str_hash(act_ready() ? 7u : 3u, ha), hb) + foot_rename() * 7717u + foot_del() * 31u;
    }
    if (grid_on())
        sig += 0x51EDu + (uint32_t)black_held(GK_ACC) * 977u + grid_write() * 131u;
    help = ui.help_t && !ui.ml.on ? help_page() : 0;   /* 1.5, HELP ON: the page's hint for ~2 s after it was entered
                                                       * (not while MIDI LEARN shows its line) */
    if (help)
        sig = str_hash(sig + 0x4E1Bu, help);
    if (ui.ml.on && !act_cols()) {                        /* MIDI LEARN (midi_learn.c): what is picked, its CC */
        ml_line(ln);
        sig += str_hash(0x1EA2u, ln);
    }
    sig += (uint32_t)foot_free() * 0x2545F491u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, H_FOOT, T_BG);
    if (foot_free()) {
        /* (Dah: row 1 is the panel's: nothing here, only row 2 blitted) */
    } else if (help) {                                /* row 1: the hint in place of the steps or the OCT+ / OCT- hint */
        help_row(help, 2);
    } else if (act_cols()) {                          /* row 1: the OCT+ / OCT- hint in place of the steps */
        char ha[16], hb[16];
        khint_t kh[3];
        uint32_t rn = foot_rename(), dl = foot_del();
        foot_hint(ha, hb);                            /* "OCT+ LOAD", "OCT- BACK": keycaps and their words */
        uint32_t nk = hb[0] ? 2u : 1u, ok;            /* (Dah: OCT- shown only when it cancels) */
        /* (Dah) OCT- before OCT+ (as on the panel), packed at the left; what else after them */
        ok = act_ready() ? 1u : 0u;
        if (nk == 2u) {
            kh[0] = (khint_t){KC_OCTDN, hb + 5};
            kh[1] = (khint_t){KC_OCTUP, ha + 5};
            ok = 1u | ok << 1;
        } else
            kh[0] = (khint_t){KC_OCTUP, ha + 5};
        if (cur_page()->graph == GR_SONG && sg_pend) {   /* (Dah) SONG, a section pending: OCT+ does it, OCT- not */
            kh[0] = (khint_t){KC_OCTDN, "CANCEL"};
            kh[1] = (khint_t){KC_OCTUP, ha + 5};
            cv_key_left(8, 2, kh, 2u, 3u, T_BG);
        } else if (cur_page()->graph == GR_SONG) {    /* (Dah) SONG: OCT+ held the VIEW, REC adds, OCT- held deletes */
            kh[0] = (khint_t){KC_OCTUP, ha + 5};
            kh[1] = (khint_t){KC_REC, "ADD"};
            kh[2] = (khint_t){KC_OCTDN, "DEL"};             /* (held: the room for "HOLD" there is not) */
            cv_key_row(8, 232, 2, kh, 3u, 1u | 2u | (dl == 2u ? 4u : 0u), T_BG);
        } else if (dl) {                              /* (Dah) FILE, PATTERN: "REC DELETE" after them */
            kh[nk] = (khint_t){KC_REC, "DELETE"};
            cv_key_left(8, 2, kh, nk + 1u, ok | (dl == 2u ? 1u : 0u) << nk, T_BG);
        } else if (rn) {                              /* USER / PROJECT: "EDIT NAME" after them */
            kh[nk] = (khint_t){KC_EDIT, cur_page()->graph == GR_EVENTS ? "DELETE" : "NAME"};
            cv_key_left(8, 2, kh, nk + 1u, ok | (rn == 2u ? 1u : 0u) << nk, T_BG);
        } else {
            cv_key_left(8, 2, kh, nk, ok, T_BG);
        }
    } else if (ui.ml.on) {                               /* row 1: MIDI LEARN's line, OCT+ DONE at the right */
        khint_t kh = {KC_OCTUP, "DONE"};
        int32_t kw = kh_w(KC_OCTUP, "DONE");
        cv_free_text(8, 2, &AF_S, ln, T_ACCENT, T_BG, 232 - kw - 8 - 8);
        cv_key_row(232 - kw, 232, 2, &kh, 1, 1u, T_BG);
    } else if (grid_on() && !detail_on()) {          /* row 1: the page, and what the keys do (DETAIL: the steps) */
        char b[16];
        uint32_t len = (uint32_t)t->p[P_SLEN];
        str_cpy(b, "PAGE ", sizeof b);
        fmt_int(b + 5, (int32_t)ui.bank + 1);
        str_cpy(b + str_len(b), "/", 4);
        fmt_int(b + str_len(b), (int32_t)((len + 15u) / 16u));
        cv_text(8, 2, &AF_S, b, T_THEME);
        if (black_held(GK_ACC))
            cv_text_r(232, 2, &AF_S, "ACCENT", T_ACCENT, T_BG);
        else
            cv_key_hint(232 - kh_w(KC_KEYS, grid_write() ? "STEPS" : "PLAY"), 2, KC_KEYS, grid_write() ? "STEPS" : "PLAY", 1,
                        T_BG);                        /* the keys are the steps (Dah: armed; else they play) */
    } else if (lane_shown(t)) {
        draw_lane(t);
    }                                                 /* (Dah: no step strip elsewhere: the pages show the steps) */
    {   /* row 2: engine icon + name, sound, page title at the right with its icon before it (MIXER, PHRASES, SONG,
         * AUTOMATION); icons: their ink on the names' capitals */
        uint32_t pi = ui.home ? ICON_NONE : page_icon(pg), ei = FELUCCA_ICONS ? engine_icon(ename) : ICON_NONE;
        int32_t r = 232, tx, ex, need = text_w(&AF_S, pn), ew = text_w(&AF_S, ename);
        ex = 8 + (FELUCCA_ICONS ? 12 + 5 : 0) + (ew < 80 ? ew : 80);   /* (where the engine's name ends) */
        tx = r - text_w(&AF_S, ti) - (pi != ICON_NONE ? 16 : 0);
        if (!ui.home && pg->graph == GR_EVENTS && tx - 12 - (ex + 10) < need) {
            str_cpy(ti + 4, ti + 10, sizeof ti - 4);    /* #93: "AUTOMATION 6/6" -> "AUTO 6/6" where the sound's name */
            tx = r - text_w(&AF_S, ti) - (pi != ICON_NONE ? 16 : 0);   /* would be cut (MENU > LARGE) */
        }
        x = 8;
        if (FELUCCA_ICONS) {
            if (ei != ICON_NONE)
                GFX_HOOK_ALIGN(0, 19 + AF_S_CAP_Y, 0, 19 + AF_S_CAP_Y + AF_S_CAP_H, AL_V,
                               "footer icons on the names' line");
            x += cv_icon_in(x, 19 + AF_S_CAP_Y, 0, AF_S_CAP_H, 12, ei, T_MID, T_BG) + 5;
        }
        x = cv_text_fit(x, 19, &AF_S, ename, T_THEME, T_BG, 80);
        cv_free_text(x + 10, 19, &AF_S, pn, T_TEXT, T_BG, tx - 12 - (x + 10));
        if (pi != ICON_NONE) {
            GFX_HOOK_ALIGN(0, 19 + AF_S_CAP_Y, 0, 19 + AF_S_CAP_Y + AF_S_CAP_H, AL_V,
                           "footer icons on the names' line");
            cv_icon_in(tx, 19 + AF_S_CAP_Y, 0, AF_S_CAP_H, 12, pi, ui.home ? T_MID : ux.fam[pg->fam % 12u], T_BG);
        }
        cv_text_r(r, 19, &AF_S, ti, ui.home ? T_MID : ux.fam[pg->fam % 12u], T_BG);   /* (the page: its family's colour) */
    }
    if (foot_free())
        cv_blit_from(0, Y_FOOT, FOOT_GAIN);           /* (row 2 only: the panel above reaches down to it) */
    else
        cv_blit(0, Y_FOOT);
}
/* the EDIT layer's cards (ui_layer.c): ENG (the engine), No. (its sounds: KNOB 2's list), FAV, an empty card */
static void engine_columns(void)
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES;
    char val[8], u[8];
    fmt_int(val, (int32_t)cur + 1);
    str_cpy(u, "/", 8);
    fmt_int(u + 1, (int32_t)total);
    draw_column(0, "ENG", ENGINES[e]->name, "", VAL(0u), (int32_t)eng_rank(e) * 1000 / (NENG_SHOWN > 1 ? NENG_SHOWN - 1 : 1),
                engine_icon(ENGINES[e]->name));
    draw_column(1, "No.", val, u, VAL(1u), total > 1u ? (int32_t)(cur * 1000u / (total - 1u)) : 0, ICON_NONE);
    draw_column(2, "FAV", preset_favorite() ? "ON" : "OFF", "", VAL(2u), -1, ICON_X_STAR);
    draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
}
static void draw_columns_in(void);
/* HA HA HE HU: the four cards in the colour of where one is: a page's family (ux.fam), HOME the selected track's */
static void draw_columns(void)
{
    ux_tint(ui.home || ui.layer || cur_page()->fam == FAM_TRK ? ux.trk[song.sel % NTRK] : ux.fam[cur_page()->fam % 12u]);
    draw_columns_in();
    ux_untint();
}
/* (Dah) HOME's look: a message over the four knobs while it shows (the header keeps its own); a layer's "HOLD [X]
 * QUICK" said whole, on two lines. The knobs drawn again after it */
/* (Dah) HOME's look: a message in a window in the middle of the screen, large (what it says first in M, bold; the rest
 * in M, MID, on as many lines as it takes, two at most). The screen under it waits; all of it again once it is gone */
static int32_t pop_wrap(const char *b, int32_t w, char out[2][40])   /* b on two lines at most, by words */
{
    uint32_t i = 0, k, n = 0, last = 0;
    out[0][0] = out[1][0] = 0;
    while (b[i] && n < 2u) {
        char t[40];
        uint32_t j = i;
        for (k = 0; b[j] && k < 39u; k++, j++) {     /* as many words as fit */
            t[k] = b[j];
            t[k + 1u] = 0;
            if (b[j + 1u] == ' ' || !b[j + 1u]) {
                if (text_w(&AF_M, t) > w && last)
                    break;
                last = k + 1u;
            }
        }
        if (!last)
            last = k;
        str_cpy(out[n], b + i, last + 1u);
        i += last;
        while (b[i] == ' ')
            i++;
        n++;
        last = 0;
    }
    return (int32_t)n;
}
static uint32_t msig;                                   /* (Dah) the message window shown (0: none) */
static int msg_popup(void)
{
    uint32_t sig;
    int32_t W = 216, H, x0 = 12, y0, nl, ly;
    char ln[2][40];
    const char *a = ui.msg, *b = "";
    int two = (cards_b && str_eq(ui.msg, cards_a)) ||
              (ly_hint && ly_hint < 7u && ui.msg[0] == 'H' && ui.msg[4] == ' ' && ui.msg[5] == '[');
    if (!ui.msg_t || !(home_bare() || two) || ui.menu || ui.confirm || name_on() || ui.layer ||
        song.playing || chain_busy()) {                 /* (Dah: playing, none: the header says it, small) */   /* (Dah: HOME's
                                                         * look, every message; elsewhere the long ones: a layer's
                                                         * hint, a two-part message) */
        if (msig)                                       /* (gone: the page again, all of it) */
            ui.force = 1;
        msig = 0;
        return 0;
    }
    if (cards_b && str_eq(ui.msg, cards_a)) {          /* (a message of two parts: cards_say) */
        a = ui.msg;
        b = cards_b;
    } else if (ly_hint && ly_hint < 7u && ui.msg[0] == 'H' && ui.msg[4] == ' ' && ui.msg[5] == '[' &&
        ui.msg[str_len(ui.msg) - 1u] == 'K') {          /* ("HOLD [X] QUICK") */
        a = LY_LONG[ly_hint][0];
        b = LY_LONG[ly_hint][1];
    }
    nl = b[0] ? pop_wrap(b, W - 28, ln) : 0;
    H = 2 * 20 + AF_M_CAP_H + (nl ? 10 + 20 * nl - (20 - AF_M_CAP_H) : 0);   /* (the words' ink, 20 px over and under:
                                                         * centred both ways) */
    y0 = 120 - H / 2;
    sig = str_hash(str_hash(9u, a), b) + 1u;
    if (!ui.force && sig == msig)
        return 1;
    msig = sig;
    cv_begin((uint32_t)W, (uint32_t)H, T_BG);
    cv_rrect(0, 0, W, H, 8, T_THEME, T_BG);              /* (a window: its frame the colour, inside SURF) */
    cv_rrect(2, 2, W - 4, H - 4, 6, T_SURF, T_THEME);
    ly = 20;                                            /* (the first line's capitals from here) */
    if (a[0] == MSG_NOFILE[0])                          /* (an icon's byte first: the words) */
        a++;
    {
        uint32_t i, key = 0;
        for (i = 0; a[i]; i++)
            key |= a[i] == '[';
        if (key) {                                      /* a key in it: its keycap */
            int32_t ky = ly + AF_M_CAP_H / 2 - KC_H / 2;
            GFX_HOOK_ALIGN(0, ky, 0, ky + KC_H, AL_V | AL_PASS, "message window key line");
            cv_free_hint((W - head_hint_w(a, W - 28)) / 2, ky + CAP_IN(S, KC_H), a, T_TEXT, T_SURF, W - 28);   /* (centred) */
        } else
            cv_text_in(0, ly - AF_M_CAP_Y, W, &AF_M, a, T_TEXT, T_SURF);
    }
    if (nl) {
        int32_t k;
        for (k = 0; k < nl; k++)
            cv_text_in(0, ly + AF_M_CAP_H + 10 + 20 * k - AF_M_CAP_Y, W, &AF_M, ln[k], T_MID, T_SURF);
    }
    lcd_fill(0, (uint32_t)card_y(), 240, (uint32_t)(240 - (int32_t)card_y()), T_BG);   /* (the page under it, quiet) */
    cv_blit((uint32_t)x0, (uint32_t)y0);
    return 1;
}
static void draw_columns_in(void)
{
    uint32_t c;
    char val[12];
    const char *unit;

    (void)motion_mask(song.sel, card_mot_mask);       /* (PLAY OFF: no scan) */
    if (ui.home && hvw.on) {                            /* (Dah) HOME's VIEW: MOVE, ZOOM, FOLLOW */
        fmt_int(val, (int32_t)(hv_base(TSEL, 1) / 16u) + 1);
        draw_column(0, "MOVE", val, "BAR", VAL(0u), -1, ICON_AUTO);
        fmt_int(val, (int32_t)hv_cols());
        draw_column(1, "STEPS", val, "", VAL(1u), -1, ICON_AUTO);
        if ((seq_off >> (song.sel % NTRK)) & 1u)       /* (Dah) the track's pattern ("-": none) */
            str_cpy(val, "-", sizeof val);
        else
            fmt_int(val, (int32_t)ps_num(song.sel));
        draw_column(2, "PAT", val, "", VAL(2u), -1, ICON_AUTO);
        draw_column(3, "FOLLOW", hvw.nofollow ? "OFF" : "ON", "", hvw.nofollow ? T_DIM : VAL(3u), -1, ICON_AUTO);
        return;
    }
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            param_format(d, *vp, val, &unit);
            card_mot_of(vp);
            card_span_of(d);
            if (home_levels())                         /* 1.5: T1..T4 LEVEL, the track's cushion, muted: DIM */
                draw_column(c, "LEVEL", val, unit, trk[trk_bank() + c].p[P_MUTE] ? T_DIM : VAL(c), RATIO(d, *vp),
                            trk_icon(trk_bank() + c, 1));   /* (HA HA HE HU: the selected track's bank) */
            else
                draw_column(c, d->label, val, unit, VAL(c), RATIO(d, enum_rank(d, *vp)), param_icon(d, *vp));
        }
        return;
    }
    if (cur_page()->graph == GR_ROLL && prv.on) {   /* (Dah) STEP's VIEW: MOVE, ZOOM, FOLLOW */
        fmt_int(val, (int32_t)(prv_base(TSEL) / 16u) + 1);
        draw_column(0, "MOVE", val, "BAR", VAL(0u), -1, ICON_AUTO);
        fmt_int(val, (int32_t)prv_cols());
        draw_column(1, "STEPS", val, "", VAL(1u), -1, ICON_AUTO);   /* (the steps shown: the zoom; no unit) */
        draw_column(2, "", "", "", T_DIM, -1, ICON_AUTO);
        draw_column(3, "FOLLOW", prv.nofollow ? "OFF" : "ON", "", prv.nofollow ? T_DIM : VAL(3u), -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_SONG && sgw.on) {   /* (Dah) VIEW: GRID (PLAY's start), ZOOM, WINDOW, FOLLOW */
        fmt_int(val, (int32_t)(sgw.gu / SG_BARU) + 1);
        draw_column(0, "GRID", val, "", VAL(0u), -1, ICON_AUTO);
        fmt_int(val, (int32_t)(SG_W / sg_zoom()));
        draw_column(1, "BARS", val, "", VAL(1u), -1, ICON_AUTO);   /* (the bars shown: the zoom; no unit) */
        fmt_int(val, sgv.off / (int32_t)sg_zoom() + 1);
        draw_column(2, "WINDOW", val, "", VAL(2u), -1, ICON_AUTO);
        draw_column(3, "FOLLOW", sgw.nofollow ? "OFF" : "ON", "", sgw.nofollow ? T_DIM : VAL(3u), -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_SONG) {             /* SECTION, TRACK, its PAT (A..D, -), REPS (the section's) */
        uint32_t row = ui.song_row < CHAIN_ROWS ? ui.song_row : CHAIN_ROWS - 1u, k = ui.song_trk % NTRK, sl;
        int used = row < chain_config.count, sec = ui.song_trk >= NTRK;
        fmt_int(val, (int32_t)row + 1);
        draw_column(0, "SEC", val, "", VAL(0u), -1, ICON_X_SONG);
        if (sec) {                                     /* (HA HA HE HU) SEC: the whole section, KNOB 3 its CUT */
            draw_column(1, "TRACK", "ALL", "", VAL(1u), -1, ICON_X_SONG);
            str_cpy(val, used ? (chain_config.row[row].repeat & CHAIN_CUT ? "ON" : "OFF") : "--", sizeof val);
            draw_column(2, "CUT", val, "", used && (chain_config.row[row].repeat & CHAIN_CUT) ? VAL(2u) : T_DIM, -1,
                        ICON_AUTO);
        } else {
            val[0] = 'T';
            val[1] = (char)('1' + k);
            val[2] = 0;
            draw_column(1, "TRACK", val, "", VAL(1u), -1, trk_icon(k, 1));
            sl = used ? chain_config.row[row].slot[k] : CHAIN_SILENT;
            if (used && sl != CHAIN_SILENT) fmt_int(val, (int32_t)sl);   /* (HA HA HE HU: the pattern's number) */
            else str_cpy(val, used ? "-" : "--", sizeof val);
            draw_column(2, "PAT", val, "", used ? VAL(2u) : T_DIM, -1, ICON_X_PATTERN);
        }
        if (used) fmt_int(val, (int32_t)CHAIN_REP(chain_config.row[row]));
        else str_cpy(val, "--", sizeof val);
        draw_column(3, "REPS", val, "", used ? VAL(3u) : T_DIM, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_EVENTS) {              /* AUTOMATION (ui_events.c): ROW STEP PARAM VALUE */
        const track_t *t = TSEL;
        uint16_t rw[EV_ROWS];
        uint32_t n, c = ev_cur(rw, &n), r = ev_row(n), k = c >> 8, step, id;
        int16_t v;
        char nm[12];
        const param_desc_t *d;
        if (k == EVK_TOP) {                            /* PLAY: the counts, PLAY ON / OFF (OCT+: CLEAR) */
            draw_column(0, "ROW", "ALL", "", VAL(0u), -1, ICON_AUTO);
            fmt_int(val, (int32_t)(motion_count(t) - motion_lock_count(t)));
            draw_column(1, "EVENT", val, "", T_MID, -1, ICON_NONE);
            draw_column(2, "CURVE", trkx[song.sel % NTRK].curve ? "RAMP" : "STEP", "", VAL(2u), -1, ICON_NONE);   /* (HA HA HE HU:
                                                       * the events held or in lines; the locks counted in the list) */
            draw_column(3, "PLAY", motion_enabled(t) ? "ON" : "OFF", "", VAL(3u), -1, motion_icon());
            return;
        }
        if (k == EVK_QNT) {                            /* QUANTIZE (1.2): the steps with a NUDGE, QNTZ ON / OFF */
            draw_column(0, "ROW", "ALL", "", VAL(0u), -1, ICON_AUTO);
            (void)ev_nudged;
            draw_column(1, "GRID", ui.ev_grid ? "1/8" : "STEP", "", VAL(1u), -1, ICON_NONE);   /* (HA HA HE HU) */
            draw_column(2, "APPLY", "GO", "", VAL(2u), -1, ICON_NONE);   /* (KNOB 3 right) */
            draw_column(3, "QNTZ", N_QNT[clamp(t->p[P_SQNT], 0, 4)], "", VAL(3u), -1, ICON_QUANTIZE);
            return;
        }
        if (k == EVK_ADD) {
            ev_fix();
            str_cpy(val, "ADD", sizeof val);
        } else {                                       /* "3/7": the rows between QUANTIZE and + ADD */
            fmt_int(val, (int32_t)r - 1);
            str_cpy(val + str_len(val), "/", 4);
            fmt_int(val + str_len(val), (int32_t)n - 3);
        }
        step = ev_step_of(c);
        id = k == EVK_REC ? MOTION_ID(&motion.event[c & 0xFFu]) : k == EVK_ADD ? ui.ev_id :
             k == EVK_CHANCE ? EV_CHANCE : k == EVK_RATCH ? EV_RATCH : k == EVK_VEL ? EV_VEL : EV_NUDGE;
        draw_column(0, "ROW", val, "", VAL(0u), -1, ICON_AUTO);
        fmt_int(val, (int32_t)step + 1);
        draw_column(1, "STEP", val, "", k != EVK_ADD || step < (uint32_t)t->p[P_SLEN] ? VAL(1u) : T_DIM, -1, ICON_AUTO);
        if (!ev_id_ok(t, id)) {
            draw_column(2, "PARAM", "--", "", T_DIM, -1, ICON_AUTO);
            draw_column(3, "VALUE", "--", "", T_DIM, -1, ICON_AUTO);
            return;
        }
        ev_name(t, id, nm);
        draw_column(2, "PARAM", nm, "", k == EVK_REC || k == EVK_ADD ? VAL(2u) : T_MID, -1, ICON_AUTO);
        if (id == EV_NUDGE) {                          /* a step's NUDGE (1.2): "+3 /16", DIM where it does not play */
            const step_t *cs = &t->step[step % NSTEP];
            int32_t sv = k == EVK_ADD ? ev_sadd(EVK_NUDGE) : step_nudge(cs);
            ev_nudge_fmt(val, sv);
            draw_column(3, "VALUE", val, "/16", k != EVK_ADD && ev_nudge_plays(t, cs) ? VAL(3u) : T_DIM, (sv + 8) * 66,
                        ICON_AUTO);
            return;
        }
        if (id == EV_VEL) {                            /* a step's VEL (1.5): 1..127, DIM where it does not play; */
            const step_t *cs = &t->step[step % NSTEP];  /* + ADD: the rows shown or not */
            int32_t sv = ev_sval(cs, EVK_VEL);
            if (k == EVK_ADD) {
                draw_column(3, "ROWS", ui.ev_vel ? "SHOWN" : "HIDDEN", "", T_DIM, -1, ICON_AUTO);
                return;
            }
            fmt_int(val, sv);
            draw_column(3, "VALUE", val, "", ev_vel_plays(cs) ? VAL(3u) : T_DIM, sv * 1000 / 127, ICON_LEVEL);
            return;
        }
        if (id == EV_CHANCE || id == EV_RATCH) {       /* a step's CHANCE (the die) / RATCH; + ADD: what it adds */
            const step_t *cs = &t->step[step % NSTEP];
            uint32_t sk = ev_skind(id), sv = (uint32_t)(k == EVK_ADD ? ev_sadd(sk) : ev_sval(cs, sk));
            int plays = sk == EVK_CHANCE || (cs->time == ST_NOTE && (cs->n || cs->hit));   /* (RATCH does nothing on a
                                                                                            * REST, a TIE, empty: DIM) */
            const char *u = "";
            if (sk == EVK_CHANCE) {                    /* (1.5.5: a condition, "1:4") */
                u = ev_chance_fmt(val, (int32_t)sv);
            } else {
                val[0] = 'x';                          /* x1 .. x4 */
                val[1] = (char)('0' + sv);
                val[2] = 0;
            }
            draw_column(3, sk == EVK_CHANCE && sv > 100u ? "COND" : "VALUE", val, u, k != EVK_ADD && plays ? VAL(3u) : T_DIM,
                        sk == EVK_CHANCE ? (sv > 100u ? (int32_t)(sv - 100u) * 1000 / (int32_t)NCOND : (int32_t)sv * 10) :
                        ((int32_t)sv - 1) * 333, sk == EVK_CHANCE ? ICON_PROB : ICON_X_REPEAT);
            return;
        }
        v = k == EVK_REC ? motion.event[c & 0xFFu].value : motion_base_value(t, id);
        d = track_desc(t, id);
        param_format(d, v, val, &unit);
        draw_column(3, k == EVK_ADD ? "VALUE" : (motion.event[c & 0xFFu].param & MOTION_LOCK) ? "LOCK" : "AUTO", val, unit,
                    k != EVK_ADD ? VAL(3u) : T_DIM, RATIO(d, enum_rank(d, v)), param_icon(d, v));
        return;
    }
    if (cur_page()->graph == GR_TOOLS) {
        static const char *const labels[] = {"PAT", "SOUND", "TRACK", "SONG"};   /* (HA HA HE HU: TRACK, the last off) */
        static const uint8_t icons[] = {ICON_AUTO, ICON_AUTO, ICON_AUTO, ICON_X_SONG};
        for (c = 0; c < 4u; c++)                       /* (1.5.5: KNOB 2 on past INIT, CLEAN) */
            draw_act_column(c, c == 1u && ui.tl_clean && act_col() == 2u ? "CLEAN" : labels[c], VAL(c), icons[c]);
        return;
    }
    if (cur_page()->scope == SC_TRK && mx_master) {   /* (Dah) the MASTER: its COMPRESSOR */
        fmt_int(val, song.g[G_MCOMP] * 100 / 127);
        draw_column(0, "COMP", song.g[G_MCOMP] ? val : "OFF", song.g[G_MCOMP] ? "%" : "", song.g[G_MCOMP] ? VAL(0u) : T_DIM,
                    song.g[G_MCOMP] * 1000 / 127, ICON_AUTO);
        fmt_int(val, 20 + song.g[G_MREL] * 360 / 127);
        draw_column(1, "RELEASE", val, "ms", VAL(1u), song.g[G_MREL] * 1000 / 127, ICON_AUTO);
        draw_column(2, "", "", "", T_THEME, -1, ICON_AUTO);
        draw_column(3, "", "", "", T_THEME, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_TRK) {                 /* LEVEL PAN REV MUTE of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);   /* (0: OFF) */
        card_mot_of(&TSEL->p[P_LEVEL]);
        card_span_of(&TP[P_LEVEL]);
        draw_column(0, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VAL(0u) : T_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        card_mot_of(&TSEL->p[P_PAN]);
        card_span_of(&TP[P_PAN]);
        draw_column(1, "PAN", val, unit, VAL(1u), RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        if (t->p[P_SPRD] == 0 || t->p[P_SPRD] == 127) {   /* (Dah) WIDTH, its ends named */
            str_cpy(val, t->p[P_SPRD] ? "WIDE" : "MONO", 8);
            unit = "";
        } else
            param_format(&TP[P_SPRD], t->p[P_SPRD], val, &unit);
        card_mot_of(&TSEL->p[P_SPRD]);
        card_span_of(&TP[P_SPRD]);
        draw_column(2, "WIDTH", val, unit, VAL(2u), RATIO(&TP[P_SPRD], t->p[P_SPRD]), ICON_AUTO);
        {
            uint32_t solo = (mix_solo >> (song.sel % NTRK)) & 1u;
            draw_column(3, "STATE", solo ? "SOLO" : t->p[P_MUTE] ? "MUTE" : "ON", "", solo || t->p[P_MUTE] ? T_ACCENT : VAL(3u),
                        -1, ICON_AUTO);
        }
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[8];
        if (cur < total) fmt_int(val, (int32_t)cur + 1);
        else str_cpy(val, "--", 8);
        str_cpy(u, "/", 8);
        fmt_int(u + 1, (int32_t)total);
        draw_column(0, "No.", val, u, VAL(0u), -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[TSEL->eng_req]->name, "", VAL(1u), -1, engine_icon(ENGINES[TSEL->eng_req]->name));
        draw_column(2, "FAV", preset_favorite() ? "ON" : "OFF", "", VAL(2u), -1, ICON_X_STAR);
        draw_column(3, "LIST", list_name(list_mode()), "", VAL(3u), -1, ICON_X_FOLDER);
        return;
    }
    if (cur_page()->graph == GR_PATS) {                  /* PAT, then LOAD (a GO button) */
        uint32_t n = pat_count(), k = pat_pick();
        char tag[4], nm[13];
        pat_label(k, tag, nm);
        draw_column(0, "PAT", tag, "", VAL(0u), (int32_t)k * 1000 / (int32_t)(n > 1u ? n - 1u : 1u), ICON_X_PATTERN);
        draw_act_column(1, "LOAD", T_THEME, ICON_AUTO);
        draw_column(2, "", "", "", T_THEME, -1, ICON_AUTO);
        draw_column(3, "", "", "", T_THEME, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_USER) {                  /* SLOT, then three GO buttons */
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VAL(0u), (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_act_column(1, "LOAD", used ? T_THEME : T_DIM, ICON_AUTO);
        draw_act_column(2, "ERASE", used ? T_THEME : T_DIM, ICON_AUTO);
        draw_act_column(3, "SAVE", T_THEME, ICON_AUTO);
        return;
    }
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                /* SLICE POS, then SPLIT JOIN (ui_slice.c) */
        uint32_t n = slice_count(), j = slice_sel(), src, div, ok = slice_src(&src, &div) && src;
        char u[8];
        if (j < n) {
            fmt_int(val, (int32_t)j + 1);
            str_cpy(u, "/", 8);
            fmt_int(u + 1, (int32_t)n);
        } else {
            str_cpy(val, n ? "END" : "--", sizeof val);
            u[0] = 0;
        }
        draw_column(0, "SLICE", val, u, VAL(0u), -1, ICON_SLICE);
        slice_time(val, slice_mark(j));
        draw_column(1, "POS", val, "S", ok ? VAL(1u) : T_DIM, -1, ICON_AUTO);
        draw_act_column(2, "SPLIT", ok ? T_THEME : T_DIM, ICON_AUTO);
        draw_act_column(3, "JOIN", ok ? T_THEME : T_DIM, ICON_AUTO);
        return;
    }
#endif
    if (fop_page(cur_page()->graph)) {                   /* FM6: OP, then three of its bytes (ui_fm6op.c) */
        for (c = 0; c < 4u; c++) {
            const char *lb, *u;
            int32_t r;
            uint32_t ic;
            int on;
            if (!fop_ok()) {                             /* (the engine changed before ui_draw left the page) */
                draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
                continue;
            }
            on = fop_card(c, &lb, val, &u, &r, &ic);
            if (fop_field(c) != FO_OP && fop_field(c) != FO_STG)   /* (OP, STAGE: 1..6, 1..4) */
                card_span_by(span_fop, 0, 0, (int32_t)fm6_max(fop_byte(fop_field(c))), (int32_t)c);
            draw_column(c, lb, val, u, on ? VAL(c) : T_DIM, r, ic);
        }
        return;
    }
    if (xp_page(cur_page()->graph)) {                    /* (HA HA HE HU) LFO DRAW, CURVE: the track's extras (ui_lfodraw.c) */
        for (c = 0; c < 4u; c++) {
            const char *lb, *u;
            int32_t r;
            uint32_t ic;
            int on = xp_card(c, &lb, val, &u, &r, &ic);
            draw_column(c, lb, val, u, on ? VAL(c) : T_DIM, r, lb[0] ? ic : ICON_NONE);
        }
        return;
    }
    if (dln_page(cur_page()->graph)) {                   /* DRUM: LANE, then three of its values (ui_drumlane.c) */
        for (c = 0; c < 4u; c++) {
            const char *lb, *u;
            int32_t r;
            uint32_t ic;
            int on;
            if (!dln_ok()) {                             /* (the engine changed before ui_draw left the page) */
                draw_column(c, "", "", "", T_THEME, -1, ICON_NONE);
                continue;
            }
            on = dln_card(c, &lb, val, &u, &r, &ic);
            if (dln_field(c) != DN_LANE)
                card_span_by(span_dln, 0, 0, (int32_t)dln_max(dln_field(c)), (int32_t)c);
            draw_column(c, lb, val, u, on ? VAL(c) : T_DIM, r, ic);
        }
        return;
    }
    if (cur_page()->graph == GR_MOD) {                   /* SLOT, then that slot's SRC DST AMT */
        const track_t *t = TSEL;
        uint32_t id = P_M1SRC + 3u * mod_ui_slot;
        int32_t s = t->p[id], d = t->p[id + 1u], a = t->p[id + 2u];
        fmt_int(val, (int32_t)mod_ui_slot + 1);
        draw_column(0, "SLOT", val, "/4", VAL(0u), (int32_t)mod_ui_slot * 1000 / 3, mod_src_icon(MS_OFF));   /* (the mod icon) */
        card_span_by(span_names, N_MSRC, 0, MS_N - 1, 0);
        draw_column(1, "SRC", N_MSRC[clamp(s, 0, MS_N - 1)], "", s ? VAL(1u) : T_DIM, -1, mod_src_icon(s));
        card_span_by(span_mod_dst, t, 0, MD_N - 1, 0);
        draw_column(2, "DST", mod_dst_name(t, d), "", d ? VAL(2u) : T_DIM, -1, mod_dst_icon(t, d));
        param_format(&TP[id + 2u], a, val, &unit);
        card_span_of(&TP[id + 2u]);
        draw_column(3, "AMT", val, unit, a ? VAL(3u) : T_DIM, RATIO(&TP[id + 2u], a), mod_src_icon(MS_OFF));
        return;
    }
    if (detail_on()) {                                  /* DETAIL (1.5.1, #199): the cursor step's CHANCE RATCH NUDGE VEL;
                                                         * DIM where they do not play (ui_input.c sd_turn) */
        static const char *const SD_LABEL[4] = {"CHANCE", "RATCH", "NUDGE", "VEL"};
        const step_t *st = &seq_steps(TSEL)[ui.cursor];
        for (c = 0; c < 4u; c++) {
            uint32_t k = SD_KIND[c];
            int32_t v = ev_sval(st, k), r;
            int on = sd_takes(st, k);
            const char *u = "";
            if (k == EVK_CHANCE) {                     /* (1.5.5: a condition, "1:4" "FILL", its card COND) */
                u = ev_chance_fmt(val, v);
                r = v > 100 ? (v - 100) * 1000 / (int32_t)NCOND : v * 10;
            } else if (k == EVK_RATCH) {
                val[0] = 'x';
                fmt_int(val + 1, v);
                r = (v - 1) * 1000 / 3;
            } else if (k == EVK_NUDGE) {
                ev_nudge_fmt(val, v);
                u = v ? "/16" : "";
                r = (v + 8) * 1000 / 15;
                on &= ev_nudge_plays(TSEL, st);
            } else {
                fmt_int(val, v);
                u = st->flags & SF_ACCENT ? "ACC" : "";
                r = v * 1000 / 127;
                on &= !(st->flags & SF_ACCENT);
            }
            draw_column(c, k == EVK_CHANCE && v > 100 ? "COND" : SD_LABEL[c], val, u, on ? VAL(c) : T_DIM, r, ICON_AUTO);
        }
        return;
    }
    if (cur_page()->graph == GR_ROLL && lock_held()) {   /* a step held: what KNOB 1..4 lock (ui_input.c lock_turn) */
        uint64_t held = lock_held();
        uint32_t st = 0;
        while (!((held >> st) & 1u))
            st++;                                       /* (the first step held: its values) */
        for (c = 0; c < 4u; c++) {
            uint32_t id = lock_id(c);
            const param_desc_t *d;
            int16_t v;
            int on;
            if (id == 0xFFu) {
                draw_column(c, "", "", "", T_THEME, -1, ICON_AUTO);
                continue;
            }
            d = track_desc(TSEL, id);
            on = motion_lock_get(TSEL, st, id, &v);
            if (!on)
                v = motion_base_value(TSEL, id);      /* not locked: the sound's own, DIM */
            param_format(d, v, val, &unit);
            draw_column(c, d->label, val, unit, on ? VAL(c) : T_DIM, RATIO(d, enum_rank(d, v)), param_icon(d, v));
        }
        return;
    }
    if (cur_page()->scope == SC_STEP && drum_track(TSEL)) {   /* the grid: STEP LANE HIT ACC */
        const step_t *st = &seq_steps(TSEL)[ui.cursor];
        uint32_t b = 1u << ui.lane, on = (step_lanes(st) & b) != 0u, ac = (step_accents(st) & b) != 0u;
        char sn[8], sl[8];
        fmt_int(sn, (int32_t)ui.cursor + 1);
        str_cpy(sl, "/", 8);
        fmt_int(sl + 1, TSEL->p[P_SLEN]);
        draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
        draw_column(1, "LANE", drum_lane_name(TSEL, ui.lane), "", VAL(1u), -1, ICON_AUTO);
        draw_column(2, "HIT", on ? "ON" : "--", "", on ? VAL(2u) : T_DIM, -1, ICON_AUTO);
        draw_column(3, "ACC", ac ? "ON" : "--", "", ac ? VAL(3u) : T_DIM, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        const step_t *st = &seq_steps(TSEL)[ui.cursor];
        char u[8];
        uint32_t cnt = st->n, first = st->note[0], l;
        for (l = NLANE; l-- > 0;)                         /* (lane hits count as their notes) */
            if ((st->hit >> l) & 1u) {
                cnt++;
                if (!st->n)
                    first = DRUM_LANE_NOTE[l];
            }
        if (cnt) {
            note_name(val, first);
            u[0] = 0;
            if (cnt > 1) {
                str_cpy(u, "+", 8);
                fmt_int(u + 1, (int32_t)cnt - 1);
            }
        } else {
            str_cpy(val, "--", 12);
            u[0] = 0;
        }
        {
            static const char *const FLAG_N[4] = {"-", "ACC", "SLD", "A+S"};
            char sn[8], sl[8];
            fmt_int(sn, (int32_t)ui.cursor + 1);
            str_cpy(sl, "/", 8);
            fmt_int(sl + 1, TSEL->p[P_SLEN]);
            draw_column(0, "STEP", sn, sl, VAL(0u), -1, ICON_AUTO);
            draw_column(1, "NOTE", val, u, step_on(st) ? VAL(1u) : T_DIM, -1, ICON_AUTO);
            if (step_on(st)) {                            /* a note: its LEN (1.5, KNOB 3: the TIEs after it) */
                fmt_int(sn, (int32_t)note_len(TSEL, ui.cursor));
                draw_column(2, "LEN", sn, "", VAL(2u), -1, ICON_AUTO);
            } else {
                draw_column(2, "TIME", TIME_N[st->time % 3u], "", VAL(2u), -1, ICON_AUTO);
            }
            draw_column(3, "FLAG", FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], "",
                        VAL(3u), -1, ICON_AUTO);
        }
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (cur_page()->graph == GR_SLOTS && c == 1u) {   /* PATTERN: ERASE (1.5.5, #199) */
            draw_act_column(1, "ERASE", graph_project_used((uint32_t)song.g[G_SLOT] - 1u) ? T_THEME : T_DIM, ICON_AUTO);
            continue;
        }
        if (cur_page()->graph == GR_PATTERN) {          /* (Dah) PATTERN: the one playing (KNOB 1); COPY NEW DELETE */
            if (!c) {
                uint8_t nums[PS_NUM];
                char u[6] = "/";
                fmt_int(val, (int32_t)ps_num(song.sel));
                fmt_int(u + 1, (int32_t)pt_list(nums));
                draw_column(0, "PATTERN", val, u, VAL(0u), -1, ICON_AUTO);
            } else
                draw_act_column(c, act_name(c), pt_ready(c) ? T_THEME : T_DIM, ICON_AUTO);
            continue;
        }
        if (cur_page()->graph == GR_FILE) {             /* (Dah) FILE: OPEN COPY NEW DELETE (the memory: the list's) */
            int ok = c == 0u ? pfs_there(ui.fsel) && ui.fsel != pfs_cur() : c == 3u ? pfs_there(ui.fsel) : 1;
            draw_act_column(c, act_name(c), ok ? T_THEME : T_DIM, ICON_AUTO);
            continue;
        }
        if (cur_page()->graph == GR_CHORD && TSEL->p[P_CHRD] == CH_MATCH && c >= 2u) {   /* (Dah) MATCH: COLOR, NOTES */
            static const char *const MCN[4] = {"FREE", "SCALE", "MAJOR", "MINOR"};
            char nv[2] = {(char)('0' + match_notes(song.sel)), 0};
            if (c == 2u) {
                card_span_by(span_mcol, 0, 0, 3, 2);
                draw_column(2, "COLOR", MCN[match_color(song.sel)], "", VAL(2u), -1, ICON_AUTO);
            } else {
                card_span_by(span_mnot, 0, 3, 6, 3);
                draw_column(3, "NOTES", nv, "", VAL(3u), ((int32_t)match_notes(song.sel) - 3) * 1000 / 3, ICON_AUTO);
            }
            continue;
        }
        if (cur_page()->fam == FAM_FX && str_eq(cur_page()->title, "DIST") && c == 0u) {   /* (Dah) DIST: IN */
            int32_t gv = fx_mst(fx_tsel()) ? mtrk.p[P_PAN] : trkx[song.sel % NTRK].din;
            param_format(&TP[P_DOUT], gv, val, &unit);
            card_span_of(&TP[P_DOUT]);
            draw_column(0, "IN", val, unit, VAL(0u), (gv + 64) * 1000 / 127, ICON_AUTO);
            continue;
        }
        if (cur_page()->fam == FAM_FX && cur_page()->id[0] == P_DKSRC && c == 3u) {   /* (Dah) SIDECHAIN: SOURCE 2 */
            uint32_t s2 = trkx[song.sel % NTRK].sc2;
            card_span_by(span_src, 0, 0, NTRK, 0);
            draw_column(3, "SRC 2", N_DKSRC[s2 <= NTRK ? s2 : 0u], "", s2 ? ux.trk[(s2 - 1u) % NTRK] : VAL(3u),
                        (int32_t)s2 * 1000 / NTRK, ICON_AUTO);
            continue;
        }
        if (cur_page()->fam == FAM_FX && str_eq(cur_page()->title, "COMP")) {   /* (Dah) COMP: AMOUNT RELEASE, the GR */
            int32_t ca = (int32_t)fx_camt(fx_tsel()), cr = (int32_t)fx_crel(fx_tsel());
            if (c == 0u) {
                fmt_int(val, ca * 100 / 127);
                draw_column(0, "AMOUNT", val, "%", VAL(0u), ca * 1000 / 127, ICON_AUTO);
            } else if (c == 1u) {
                fmt_int(val, 20 + cr * 360 / 127);
                draw_column(1, "RELEASE", val, "ms", VAL(1u), cr * 1000 / 127, ICON_AUTO);
            } else {                                    /* IN, OUT: +-15 dB */
                int32_t gv = fx_cget(fx_tsel(), c - 2u);
                param_format(&TP[P_DOUT], gv, val, &unit);
                card_span_of(&TP[P_DOUT]);
                draw_column(c, c == 2u ? "IN" : "OUT", val, unit, VAL(c), (gv + 64) * 1000 / 127, ICON_AUTO);
            }
            continue;
        }
        if (cur_page()->id[0] == P_FXORD && cur_page()->fam == FAM_FX) {   /* (Dah) CHAIN: MOVE, TYPE, MIX (the effect:
                                                         * SELECT's) */
            uint8_t u[FXU_N_ALL];
            track_t *ft = fx_tsel();
            uint32_t n = fx_list(ft, u);
            char pl[6] = {(char)('1' + ch_cur % 9u), '/', (char)('0' + n), 0};
            int on = ch_cur < n && !fx_ret_fixed(ft, u[ch_cur % FXU_N_ALL]);   /* (Dah: a MASTER's return: nothing to
                                                         * move, change or mix here) */
            if (c == 1u && on) {
                card_span_by(span_fxu, 0, 0, FXU_N_ALL - 1, 0);
                draw_column(1, "TYPE", FXU_N[u[ch_cur % FXU_N_ALL]], "", VAL(1u), -1, ICON_AUTO);
            } else if (c == 0u && on) {
                card_span_by(span_fxmv, 0, 0, FXU_N_ALL - 1, 1);
                draw_column(0, "MOVE", pl, "", VAL(0u), n > 1u ? (int32_t)ch_cur * 1000 / (int32_t)(n - 1u) : -1, ICON_AUTO);
            } else if (c == 2u && on && FXU_MIX[u[ch_cur % FXU_N_ALL]] != 0xFFu) {   /* MIX */
                uint32_t mid = FXU_MIX[u[ch_cur % FXU_N_ALL]];
                const param_desc_t *md = track_desc(ft, mid);
                param_format(md, ft->p[mid], val, &unit);
                card_span_of(md);
                draw_column(2, "MIX", val, unit, VAL(2u), RATIO(md, ft->p[mid]), ICON_AUTO);
            } else
                draw_column(c, "", "", "", T_THEME, -1, ICON_AUTO);
            continue;
        }
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", T_THEME, -1, ICON_AUTO);
            continue;
        }
        if ((act_cols() >> c) & 1u) {
            draw_act_column(c, d->label, T_THEME, ICON_AUTO);
            continue;
        }
        param_format(d, *vp, val, &unit);
        if (((vp == &TSEL->p[P_EQ1F] || vp == &TSEL->p[P_EQ1G]) && !TSEL->p[P_EQ1G]) ||
            ((vp == &TSEL->p[P_EQ2F] || vp == &TSEL->p[P_EQ2G]) && !TSEL->p[P_EQ2G]) ||
            ((vp == &TSEL->p[P_EQ3F] || vp == &TSEL->p[P_EQ3G]) && !TSEL->p[P_EQ3G])) {
            if (!(ui.hot_t && ui.hot_col == c)) {       /* (Dah) an EQ band at 0 dB: OFF, FREQ and GAIN; its value while
                                                         * its knob turns, OFF again a moment after */
                str_cpy(val, "OFF", 4);
                unit = "";
            }
        }
        card_mot_of(vp);                                /* (a global: never) */
        card_span_of(d);
        if (vp == &TSEL->p[P_LRATE] && TSEL->p[P_LSYNC]) {   /* LFO 2 SYNC on: RATE does nothing, DIM (1.2) */
            draw_column(c, d->label, val, unit, T_DIM, RATIO(d, *vp), param_icon(d, *vp));
            continue;
        }
        if (vp == &song.g[G_SLOT]) {                   /* (HA HA HE HU) FILE: the project's letter */
            val[0] = (char)('A' + clamp(*vp, 1, 4) - 1);
            val[1] = 0;
            draw_column(c, "SLOT", val, "", VAL(c), RATIO(d, *vp), param_icon(d, *vp));
            continue;
        }
        draw_column(c, d->label, val, unit,
                    vp == &TSEL->p[P_DKSRC] && *vp > 0 ? ux.trk[(*vp - 1) % NTRK] : VAL(c),   /* (Dah: SIDECHAIN's source:
                                                         * its track's colour) */
                    d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, enum_rank(d, *vp)), param_icon(d, *vp));
    }
}


/* The power-on splash (main.c fm1_main, web/emu felucca_web.c; HA HA HE HU): on BG, the picture (build/gen/ui_splash.h,
 * tools/gen_splash_image.py: SPLASH_W x SPLASH_H) centred near the top, under it centred the name (FW_NAME) and the
 * version in AF_M, then "based on Felucca" in AF_S (MID): the firmware this one is built on, as its licence asks. The
 * screen is more than the canvas (CV_MAX, 240 x 124): drawn as two canvases of SPL_STRIP rows, each with the part of
 * the picture and the lines that fall in it. In the saved palette (settings_init before it) */
#include "ui_splash.h"
#define SPL_STRIP 120                               /* rows per canvas (two make the screen) */
#define SPL_PY 6                                    /* the picture's top */
#define SPL_NAME_Y (SPL_PY + 144 + 4)               /* the name's top, in L (Dah: a picture of 144: the layout's) */
#define SPL_VER_Y (SPL_NAME_Y + 37)
#define SPL_BASED_Y (SPL_VER_Y + 21)
static const char *const SPLASH_LINES[] = {FW_NAME, FELUCCA_VERSION, "based on Felucca"};
static uint32_t splash_ix(uint32_t i)              /* (Dah) pixel i's colour, the runs from the start (the tests') */
{
    uint32_t k = 0;
    for (; k < sizeof SPLASH_RLE; k++) {
        uint32_t n = (SPLASH_RLE[k] & 15u) + 1u;
        if (i < n)
            return SPLASH_RLE[k] >> 4;
        i -= n;
    }
    return 0;
}
static void draw_splash(void)
{
    uint32_t s, x, y = 0, px0 = (240u - SPLASH_W) / 2u, k = 0, left = 0, col = 0;
    for (s = 0; s < 240u / SPL_STRIP; s++) {
        int32_t y0 = (int32_t)(s * SPL_STRIP);
        cv_begin(240, SPL_STRIP, T_BG);
        for (; y < SPLASH_H && SPL_PY + (int32_t)y - y0 < SPL_STRIP; y++) {   /* the picture's rows in this strip, its runs
                                                     * on (Dah: ui_splash.h's SPLASH_RLE) */
            int32_t r = SPL_PY + (int32_t)y - y0;
            for (x = 0; x < SPLASH_W; x++) {
                if (!left && k < sizeof SPLASH_RLE) {
                    col = SPLASH_RLE[k] >> 4;
                    left = (SPLASH_RLE[k++] & 15u) + 1u;
                }
                if (left)
                    left--;
                if (r >= 0)
                    cv_px[(uint32_t)r * 240u + px0 + x] = SPLASH_PAL[col];
            }
        }
        if (SPL_NAME_Y >= y0 && SPL_BASED_Y + AF_S.h <= y0 + SPL_STRIP) {
            GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(3), "splash lines centred");
            cv_text_in(0, SPL_NAME_Y - y0, 240, &AF_L, SPLASH_LINES[0], T_TEXT, T_BG);   /* (Dah: the name large) */
            cv_text_in(0, SPL_VER_Y - y0, 240, &AF_M, SPLASH_LINES[1], T_THEME, T_BG);
            cv_text_in(0, SPL_BASED_Y - y0, 240, &AF_S, SPLASH_LINES[2], T_MID, T_BG);
        }
        cv_blit(0, (uint32_t)y0);
    }
    lcd_sync();
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held): over everything, the menu and the dialogs too */
static void draw_uboot(void)
{
    static uint8_t shown;
    char d[4] = {(char)('0' + ui.uboot % 10u), 0, 0, 0};
    if (!ui.force && shown == ui.uboot)
        return;
    shown = ui.uboot;
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    draw_text_box(0, 76, 240, &AF_M, "UPDATE MODE IN", T_TEXT, 1);
    draw_text_box(0, 102, 240, &AF_L, d, T_THEME, 1);
    {   /* the two keycaps held, and what letting go does */
        int32_t w = kc_w(KC_OCTDN) + 3 + kh_ink(KC_OCTUP, "LET GO TO CANCEL"), x = HALF_UP(240 - w);
        cv_begin(240, KC_H + 2, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(3), "update-mode keys centred");
        x = cv_keycap(x, 1, KC_OCTDN, T_KEY, T_INK, T_BG) + 3;
        cv_key_hint(x, 1, KC_OCTUP, "LET GO TO CANCEL", 1, T_BG);
        cv_blit(0, 149);
        lcd_sync();
    }
    ui.force = 0;
}

/* (Dah) a digit 0..9 as seven segments, w x h at x y, t thick, straight onto the LCD */
static void seg_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t d, uint16_t c)
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* a b c d e f g */
    uint32_t m = SEG[d % 10u];
    int32_t hh = (h - t) / 2;
    if (m & 1u) lcd_fill((uint32_t)(x + t), (uint32_t)y, (uint32_t)(w - 2 * t), (uint32_t)t, c);             /* a */
    if (m & 2u) lcd_fill((uint32_t)(x + w - t), (uint32_t)(y + t), (uint32_t)t, (uint32_t)(hh - t), c);    /* b */
    if (m & 4u) lcd_fill((uint32_t)(x + w - t), (uint32_t)(y + hh + t), (uint32_t)t, (uint32_t)(hh - t), c);   /* c */
    if (m & 8u) lcd_fill((uint32_t)(x + t), (uint32_t)(y + h - t), (uint32_t)(w - 2 * t), (uint32_t)t, c);   /* d */
    if (m & 16u) lcd_fill((uint32_t)x, (uint32_t)(y + hh + t), (uint32_t)t, (uint32_t)(hh - t), c);          /* e */
    if (m & 32u) lcd_fill((uint32_t)x, (uint32_t)(y + t), (uint32_t)t, (uint32_t)(hh - t), c);               /* f */
    if (m & 64u) lcd_fill((uint32_t)(x + t), (uint32_t)(y + hh), (uint32_t)(w - 2 * t), (uint32_t)t, c);     /* g */
}
/* the countdown (seq.c, MENU > COUNT-IN, REC's COUNT; Dah: 3 2 1 on the whole screen, the click from its first beat):
 * over the page until the sequencer starts, the beats left as one big digit; PLAY stops it. Drawn again on each beat */
#define CI_Y 88
static void draw_countin(void)
{
    static uint32_t shown;
    uint32_t left = cin_left, sig = cin_total * 64u + left + 1u;
    if (!ui.force && shown == sig)
        return;
    shown = sig;
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    seg_digit(70, H_HEAD + 18, 100, 160, 16, left ? left : 1u, T_REC);
    {   /* PLAY stops it */
        int32_t x = HALF_UP(240 - kh_ink(KC_PLAY, "STOP"));
        cv_begin(240, KC_H + 2, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "count-in key centred");
        cv_key_hint(x, 1, KC_PLAY, "STOP", 1, T_BG);
        cv_blit(0, 206);
        lcd_sync();
    }
    ui.force = 0;
}

/* the OCT- / OCT+ dialog: what it does (ui.confirm, ui.confirm_trk) on two lines, on a surface */
#define DLG_X 16
#define DLG_Y 62
#define DLG_W 208
#define DLG_H 116
static void confirm_text(char *a, char *b)
{
    uint32_t k = ui.confirm_trk;
    b[0] = 0;
    switch (ui.confirm) {
    case CF_CLEAR_TRK:
        str_cpy(a, "CLEAR TRACK 1?", 24);
        a[12] = (char)('1' + k % NTRK);
        break;
    case CF_OVR_PROJ:
        str_cpy(a, "OVERWRITE PATTERN A?", 24);   /* (HA HA HE HU: the slots are the patterns) */
        a[18] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b) || !b[0])          /* the project's name, else what SONG plays from it */
            str_cpy(b, "SONG PATTERN CHANGES", 24);
        break;
    case CF_DEL_ROW:
        str_cpy(a, "DELETE SECTION?", 24);
        break;
    case CF_DEL_PAT:                                 /* (HA HA HE HU) SAVE > PATTERN, REC */
        str_cpy(a, "DELETE PATTERN ", 24);
        fmt_int(a + str_len(a), (int32_t)k);
        str_cpy(a + str_len(a), "?", 4);
        str_cpy(b, "TRACK 1", 24);
        b[6] = (char)('1' + song.sel % NTRK);
        break;
    case CF_ADD_FX:                                  /* (Dah) FX > CHAIN: + ADD reached */
        str_cpy(a, "ADD AN EFFECT?", 24);
        str_cpy(b, "LAST IN THE CHAIN", 24);
        break;
    case CF_NEW_PAT:                                 /* (Dah) SELECT, GLO + ALGORITHM past the last pattern */
        str_cpy(a, "NEW PATTERN ", 24);
        fmt_int(a + str_len(a), (int32_t)k);
        str_cpy(a + str_len(a), "?", 4);
        str_cpy(b, "EMPTY, EVERY TRACK", 24);
        break;
    case CF_ADD_TRACK:                               /* (HA HA HE HU) ALGORITHM past the last track */
        str_cpy(a, "ADD TRACK 1?", 24);
        a[10] = (char)('1' + k % NTRK);
        str_cpy(b, track_def_name(k % NTRK), 24);
        break;
    case CF_DEL_TRACK:                               /* SAVE > TOOLS: the last track off */
        str_cpy(a, "REMOVE TRACK 1?", 24);
        a[13] = (char)('1' + k % NTRK);
        str_cpy(b, "ITS SOUND AND STEPS GO", 24);
        break;
    case CF_NEW_PROJ:                                /* (HA HA HE HU) SAVE > FILE's NEW */
        str_cpy(a, "NEW PROJECT?", 24);
        str_cpy(b, "UNSAVED CHANGES LOST", 24);
        break;
    case CF_OPEN_ARCH:                               /* (HA HA HE HU) SAVE > FILE: REC deletes */
    case CF_DEL_ARCH:
    case CF_OVR_ARCH:
        str_cpy(a, "DELETE PROJECT?", 24);
        if (!pfs_name(k, b) || !b[0])
            str_cpy(b, "UNTITLED", 24);
        break;
    case CF_CLEAR_SONG:
        str_cpy(a, "CLEAR SONG ORDER?", 24);
        break;
    case CF_INIT_SOUND:
        str_cpy(a, "INITIALIZE SOUND?", 24);
        break;
    case CF_CLEAN_SOUND:                             /* 1.5.5 (#190): the basic sound, nothing added */
        str_cpy(a, "CLEAN INIT SOUND?", 24);
        str_cpy(b, "NO FX, NO MOD", 24);
        break;
    case CF_CLEAR_MOTION:
        str_cpy(a, "CLEAR T1 AUTOMATION?", 24); a[7] = (char)('1' + k % NTRK);
        break;
    case CF_OVR_USER:
        str_cpy(a, "OVERWRITE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);                               /* the sound stored there */
        break;
    case CF_ERASE_PROJ:                              /* 1.5.5 (#199): the project's name under it */
        str_cpy(a, "ERASE PATTERN A?", 24);
        a[14] = (char)('A' + (k & 3u));
        (void)project_name(k & 3u, b);
        break;
    case CF_ERASE_USER:
        str_cpy(a, "ERASE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);
        break;
    case CF_LOAD_PAT: {
        char tag[4];
        str_cpy(a, "REPLACE T1 SEQUENCE?", 24);
        a[9] = (char)('1' + k % NTRK);
        pat_label(pat_pick(), tag, b);               /* with the pattern picked */
        break;
    }
    default:
        str_cpy(a, "CLEAR T1 SEQUENCE?", 24);     /* the header (T1..T4) is hidden */
        a[7] = (char)('1' + k % NTRK);
        break;
    }
}
static void draw_confirm(void)
{
    char a[24], b[24];
    const aafont_t *tf = &AF_M;
    confirm_text(a, b);
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    cv_begin(DLG_W, DLG_H, T_BG);                     /* a SURF card: warning, the question, the detail, two buttons */
    if (ux.style)                                     /* LINE: no card, a 1 px rule round it */
        cv_rrect(0, 0, DLG_W, DLG_H, 8, T_RULE, T_BG);
    cv_rrect(!!ux.style, !!ux.style, DLG_W - 2 * !!ux.style, DLG_H - 2 * !!ux.style, 8 - !!ux.style, T_SURF,
             ux.style ? T_RULE : T_BG);
    GFX_HOOK_ALIGN(0, 0, DLG_W, 0, AL_H, "dialog icon centred");
    cv_icon_in(0, 12, DLG_W, 0, 16, ui.confirm == CF_CLEAR_MOTION ? ICON_X_MOTION_DEL : ICON_X_WARN, T_ACCENT, T_SURF);
    if (text_w(tf, a) > DLG_W - 16)
        tf = &AF_S;
    GFX_HOOK_ALIGN(0, 0, DLG_W, 0, AL_H, "dialog question centred");
    cv_text_in(0, 34, DLG_W, tf, a, T_TEXT, T_SURF);
    if (b[0]) {
        char f[32];
        text_fit(f, sizeof f, b, &AF_M, DLG_W - 16);    /* a name: free text (M: what is selected, as the question) */
        GFX_HOOK_ALIGN(0, 0, DLG_W, 0, AL_H, "dialog detail centred");
        cv_text_flags(ink_in(&AF_M, f, DLG_W), 55, &AF_M, f, T_MID, T_SURF, 8u | (f[str_len(f) - 1u] == ELLIPSIS));
    }
    cv_rrect(10, 80, 90, 26, 6, T_RAISE, T_SURF);     /* OCT-: NO */
    GFX_HOOK_ALIGN(10, 80, 100, 106, AL_HV | AL_N(2), "dialog button NO");
    cv_key_hint(10 + HALF_UP(90 - kh_ink(KC_OCTDN, "NO")), 80 + HALF_UP(26 - KC_H), KC_OCTDN, "NO", 1, T_RAISE);   /* (ink centred) */
    cv_rrect(108, 80, 90, 26, 6, T_THEME, T_SURF);    /* OCT+: YES (on the THEME fill: an INK keycap, THEME label) */
    {
        int32_t x = 108 + HALF_UP(90 - kh_ink(KC_OCTUP, "YES"));
        GFX_HOOK_ALIGN(108, 80, 198, 106, AL_HV | AL_N(2), "dialog button YES");
        x = cv_keycap(x, 80 + HALF_UP(26 - KC_H), KC_OCTUP, T_INK, T_THEME, T_THEME);
        cv_text_on(x + KH_GAP, 80 + HALF_UP(26 - KC_H) - 1, &AF_S, "YES", T_INK, T_THEME);
    }
    cv_blit(DLG_X, DLG_Y);
}

/* the stored STYLE (ui_style: the menu, a settings import, a restore) into gfx.c's copy: the tokens again, all redrawn */
static void style_apply(void)
{
    if (ui_style > ST_LINE)                             /* 2, the retired PIXEL: LINE; unknown: FLAT */
        ui_style = ui_style == 2u ? ST_LINE : ST_FLAT;
    if (ux.style != ui_style) {
        ux.style = ui_style;
        palette_apply();
        ui.force = 1;
    }
}

static void ui_draw_page(uint32_t counting);
static void ui_draw(void)
{
    static uint8_t counting;
    style_apply();
    ui.frame++;
    if (counting != (uint8_t)seq_counting()) {          /* a count-in began or ended: the page drawn again after it */
        counting = (uint8_t)seq_counting();
        ui.force = 1;
    }
    if (!scr_frame())                                   /* MENU > SCREEN OFF: dark (or waking), nothing drawn */
        return;
    ui_draw_page(counting);
    scr_shown();
}
static void ui_draw_page(uint32_t counting)
{
    if (ui.uboot) {
        draw_uboot();
        draw_head();
        return;
    }
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (counting && !ui.confirm && !name_on()) {        /* the count-in: over the page (and a layer) */
        draw_countin();
        draw_head();
        return;
    }
    {   /* (Dah) a list just turned: the whole screen, ~1 s; gone: all of the page again */
        static uint8_t was;
        uint8_t on = (uint8_t)(((tl_on() || pl_on()) && !ui.confirm && !name_on()) || ui.confirm == CF_PICK_FX);   /* (Dah:
                                                         * ADD's picker too) */
        if (was && !on)
            ui.force = 1;
        if (on) {
            static uint32_t lsig;
            uint32_t sg = fl_sig();
            if (!was || ui.force || sg != lsig || fl_moving())   /* (gliding: every frame) */
                draw_list_full(!was);
            lsig = sg;
            was = on;
            ui.force = 0;
            return;
        }
        was = on;
    }
    if (ui.confirm) {                                   /* the OCT- / OCT+ dialog */
        if (ui.force) {
            draw_confirm();
            ui.force = 0;
        }
        draw_head();                                    /* recording remains visible over confirmations */
        return;
    }
    if (name_on()) {                                    /* NAME: a user preset's or a project's (ui_name.c) */
        name_draw();
        return;
    }
    if (ui.layer) {                                     /* a layer's map over the page (ui_layer.c) */
        if (ui.force)
            draw_frame();
        draw_head();
        draw_layer();
        if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {
            str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
            ui.msg2[0] = 0;
            ui.msg_t = 60;
        }
        if (ui.hot_t)
            ui.hot_t--;
        if (ui.bpm_t)
            ui.bpm_t--;
        ui.force = 0;
        return;
    }
    if (!ui.home && !page_visible(ui.page) && PAGES[ui.page].fam != FAM_FX) {   /* (Dah: an effect's page stays while
                                                         * shown, its effect out of the chain or not) an OP page of a track that is not DIGITAL (without
                                                         * FELUCCA_FM4: any track): EDIT 1; a SYN page of a lane that
                                                         * plays no layer set any more (an undo, the grid's lane): DESIGN */
        uint32_t i = 0;
        if (dln_syn_page(PAGES[ui.page].graph) && TSEL->eng_req % NENGINES == ENGI_DRUM)
            for (i = 0; i < NPAGES && PAGES[i].graph != GR_DLANE; i++)
                ;
        ui.page = (uint8_t)(i && i < NPAGES ? i : page_first(FAM_EDIT));
        page_entered();
    }
    cursor_fix();
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    if (msg_popup()) {                                  /* (Dah) a message: its window, the page waits under it */
        if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {
            str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
            ui.msg2[0] = 0;
            ui.msg_t = 60;
        }
        if (ui.bpm_t)
            ui.bpm_t--;
        if (ui.hot_t)
            ui.hot_t--;
        ui.force = 0;
        return;
    }
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
    if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {     /* the second message (ui_notices) */
        str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
        ui.msg2[0] = 0;
        ui.msg_t = 60;
    }
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.hot_t)
        ui.hot_t--;
    if (ui.help_t)
        ui.help_t--;
    felucca_dbg.stage = 6;
    if (!home_bare())                                   /* (Dah: HOME has its own, a band) */
        draw_foot();
    else
        home_foot();
    ui.force = 0;
}
