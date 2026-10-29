/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI: the panel (Y_GRAPH .. Y_GRAPH + H_GRAPH) between the cards and the footer: a SURF area
 * (rounded, on BG) holding the page graphs (ADSR, LFO, steps, piano roll, drum grid, scale, FX, SLICER,
 * MOD, lists: presets, user presets, patterns, project slots, song), the HOME oscilloscope, the
 * instrument diagrams; the MIXER page draws four SURF columns instead. Each graph is redrawn only when
 * graph_signature() changes. The look:
 * curves THEME (2 px), guides and empty marks RAISE, captions MID / DIM, the active thing ACCENT,
 * bars rounded; a list's selected row is a THEME bar with INK text. */
#define PANEL_X0 10                                  /* the graphs' inner area: x 10..230 */
#define PANEL_W 220
#define GOY 11                                       /* graphs drawn on a 100 px scale sit at y 11..111 */
/* the height ADSR and LFO are drawn on: 100 px, or (MENU > LARGE's strip: draw_graph) the strip's ~ 56 */
static int32_t graph_ht = 100;
static uint32_t graph_arch_sig(void);              /* (HA HA HE HU: SAVE > FILE's projects, below) */
#include "ui_fm6op.c"                                /* EDIT > OPERATOR, OP ENV, OPERATOR 2: FM6's operators (their charts below) */
#include "ui_drumlane.c"                             /* EDIT > DESIGN, DESIGN 2, SYN TONE .. SYN METAL: DRUM's lanes
                                                     * (the list below) */
#include "ui_lfodraw.c"                              /* (HA HA HE HU) LFO DRAW, CURVE: the track's extras */
#include "ui_pattern.c"                              /* (HA HA HE HU) SAVE > PATTERN: a track's patterns */

/* Matches voice.c: attack is linear, decay and release are exponential
 * (env += (target - env) * k each tick, ~99 % after the set time). Time
 * axis is the parameter value (the times themselves are exponential). */
static void graph_adsr(const track_t *t, uint16_t c)
{
    const page_t *pg = cur_page();
    int32_t a = 4 + t->p[pg->id[0]] * 50 / 127, d = 6 + t->p[pg->id[1]] * 50 / 127, r = 6 + t->p[pg->id[3]] * 60 / 127;
    int32_t top = 6 * graph_ht / 100, bot = 88 * graph_ht / 100, sus = t->p[pg->id[2]] * 1000 / 127;   /* 0..1000 */
    int32_t x0 = 12, x1 = x0 + a, x3 = 226 - r, i, px, py;
    int32_t e = 32768;                                                  /* exp(-4.6 u), Q15 */
#define EGY(lvl) (bot - (lvl) * (bot - top) / 1000)
    cv_rect(PANEL_X0, bot + 2, PANEL_W, 1, T_RAISE);
    cv_line_t(x0, bot, x1, top, c, 2);                                  /* attack: linear */
    px = x1;
    py = top;
    for (i = 1; i <= d; i++) {                                          /* decay: exponential to SUS */
        int32_t lvl;
        e = (e * (32768 - 150733 / d)) >> 15;           /* k^d = exp(-4.6) */
        lvl = sus + ((1000 - sus) * e >> 15);
        cv_line_t(px, py, x1 + i, EGY(lvl), c, 2);
        px = x1 + i;
        py = EGY(lvl);
    }
    cv_line_t(px, py, x3, EGY(sus), c, 2);                               /* sustain */
    px = x3;
    py = EGY(sus);
    e = 32768;
    for (i = 1; i <= r; i++) {                                          /* release: exponential to 0 */
        e = (e * (32768 - 150733 / r)) >> 15;
        cv_line_t(px, py, x3 + i, EGY(sus * e >> 15), c, 2);
        px = x3 + i;
        py = EGY(sus * e >> 15);
    }
#undef EGY
}

/* (Dah) a note of the track sounding (its release too): the LFO's bar shown, moving */
static int lfo_live(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return 1;
    return 0;
}
static void graph_lfo(const track_t *t, uint16_t c)
{
    int32_t x, cy = graph_ht / 2, a = 38 * graph_ht / 100, py = cy, uni = t->p[P_LPOL] != 0;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25;
    int32_t z = uni ? cy + a : cy;                   /* the zero line: POL UNI (LFO 2) draws 0..+1 over it */
    cv_rect(PANEL_X0, z, PANEL_W, 1, T_RAISE);
    for (x = 0; x < PANEL_W; x++) {                  /* one cycle (Dah; lfo_wave only reads the track) */
        int32_t w = lfo_wave((track_t *)t, ph + (uint32_t)x * (0xFFFFFFFFu / PANEL_W)), y;
        if (t->p[P_LWAVE] == 4)
            w = (int32_t)((x / 10 * 2654435761u) >> 16) - 32768;
        y = uni ? z - (w + 32768) * a / 32768 : cy - w * a / 32768;
        if (x)
            cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
    if (lfo_live(t))                                    /* (Dah) where it is now: a note sounding only */
        cv_rect(PANEL_X0 + (int32_t)((t->lfo_ph - ph) / (0xFFFFFFFFu / PANEL_W)), cy - a - 4, 1, 2 * a + 8, T_ACCENT);
}

/* HA HA HE HU: LFO DRAW's panel: the 16 points as bars from the middle line (the point picked in the colour, the others
 * fainter, a mark under it), the drawn cycle over them (as the LFO plays it: lfo_wave DRAW, stepped or joined; TEXT
 * while WAVE plays it, MID while not) */
static void graph_ldraw(const track_t *t, uint16_t c)
{
    const trkx_t *x = &trkx[(uint32_t)(t - trk) % NTRK];
    int32_t cy = graph_ht / 2, a = 40 * graph_ht / 100, cw = PANEL_W / (int32_t)LDRAW_N, i, py = cy;
    uint16_t dim = ux_mix(T_SURF, c, 30);
    cv_rect(PANEL_X0, cy, PANEL_W, 1, T_RAISE);
    for (i = 0; i < (int32_t)LDRAW_N; i++) {
        int32_t x0 = PANEL_X0 + i * cw + 1, y = cy - x->draw[i] * a / 64, top = y < cy ? y : cy, h = y < cy ? cy - y : y - cy;
        if (h)
            cv_rect(x0, top, cw - 2, h, (uint32_t)i == xp_point % LDRAW_N ? c : dim);
        if ((uint32_t)i == xp_point % LDRAW_N)
            cv_rect(x0, cy + a + 4, cw - 2, 2, T_TEXT);
    }
    for (i = 0; i < PANEL_W; i++) {                  /* (the drawing, whatever WAVE plays) */
        int32_t y = cy - ldraw_at(x, (uint32_t)i * (0xFFFFFFFFu / PANEL_W)) * a / 32768;
        if (i)
            cv_line_t(PANEL_X0 + i - 1, py, PANEL_X0 + i, y, t->p[P_LWAVE] == 5 ? T_TEXT : T_MID, 2);
        py = y;
    }
    if (t->p[P_LWAVE] == 5 && lfo_live(t))           /* (Dah) where the LFO is now: a bar, a note sounding only */
        cv_rect(PANEL_X0 + (int32_t)(t->lfo_ph / (0xFFFFFFFFu / PANEL_W)), cy - a - 2, 1, 2 * a + 4, T_ACCENT);
}
/* step bar x of step i of a 16-step row: 4 groups, as the footer (9 px bars, 13 px apart, 4 px between groups),
 * the row centred: x 12 .. 228 */
static int32_t bar_x(uint32_t i) { return 12 + (int32_t)(i % 16u) * 13 + (int32_t)(i % 16u / 4u) * 4; }

/* STEP page (a melodic track): a piano roll of the cursor's 16-step page. PR_ROWS semitone rows of PR_RH px,
 * the highest on top; on the left the C rows named (S, DIM) and a keyboard strip (white keys RAISE, black keys a
 * shorter DIM; a key held on the track: its row ACCENT). Rows in the track's scale (SCL ROOT / SCALE; CHR: the white
 * keys) are tinted LANE, a C row closes with a RAISE line; step lines GRID, every 4th RAISE. A note is a 9 x 3 bar in
 * its column (an accent: TEXT, the row's full height; the cursor step's notes: ACCENT), chords stacked, lane hits as
 * their notes (a RATCH step's bars in its 2..4 parts, 1 px apart); a TIE carries the bars of the note before through
 * its column; a REST is empty; a slide is a TEXT diagonal from the end of its bar into the next note (not from a
 * RATCH step: it never slides out). Notes outside
 * the view: a 1 px mark on the edge. The cursor: a TEXT column frame; the playhead: a 1 px ACCENT line. The view
 * (proll.lo, the bottom row) fits the notes of the page, else centres on the cursor's note, and follows in steps (a
 * third of the way per frame); graph_signature() holds it, so a page that does not change is not drawn again. */
static int32_t pr_rows = 19, pr_rh = 5;             /* (Dah) as the panel's height allows (graph_roll) */
#define PR_ROWS pr_rows                             /* semitones shown (the ruler under them) */
#define PR_RH pr_rh                                 /* px per row */
#define PR_Y0 18                                    /* the top row (canvas y; Dah: the ruler over it, as HOME's) */
#define PR_X0 35                                    /* the first step column */
#define PR_CW 12                                    /* px per step */
#define PR_KX 24                                    /* the keyboard strip, 9 px */
static struct {
    uint8_t lo, trk, init, moving;                    /* lo: the note of the bottom row; moving: on its way */
    uint32_t frame;
} proll;
static const uint8_t KEY_BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
/* (Dah) STEP's VIEW (OCT+ held: KNOB 1 MOVE, 2 ZOOM, 4 FOLLOW): 8 16 32 or 64 steps across; where they start (-1: the
 * cursor's page, as before); FOLLOW: playing, the step playing in the middle */
static struct { uint8_t on, zoom, nofollow; int16_t at; } prv = {0, 1, 0, -1};
static uint32_t prv_cols(void) { return 8u << (prv.zoom & 3u); }
static uint32_t prv_base(const track_t *t)
{
    uint32_t len = (uint32_t)t->p[P_SLEN], n = prv_cols(), b;
    if (song.playing && !prv.nofollow && t->seq_idx < len)
        b = t->seq_idx > n / 2u ? t->seq_idx - n / 2u : 0u;
    else if (prv.at >= 0)
        b = (uint32_t)prv.at;
    else
        b = (ui.cursor / n) * n;
    if (len > n && b + n > len)
        b = len - n;
    return len > n ? b : 0u;
}
/* (Dah) HOME's VIEW, the tracks' rack (OCT+ held: KNOB 1 MOVE, 2 ZOOM, 4 FOLLOW): 8 16 32 or 64 steps across, where
 * they start (-1: the page playing, or the cursor's); FOLLOW: playing, each track's page playing */
static struct { uint8_t on, zoom, nofollow; int16_t at; } hvw = {0, 1, 0, -1};
static uint32_t hv_cols(void) { return 8u << (hvw.zoom & 3u); }
static uint32_t hv_base(const track_t *t, int sel)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = hv_cols(), b;
    if (song.playing && !hvw.nofollow)
        b = (t->seq_idx % len) / n * n;
    else if (hvw.at >= 0)
        b = (uint32_t)hvw.at;
    else
        b = sel ? ui.cursor / n * n : 0u;
    if (b >= len)
        b = (len - 1u) / n * n;
    return b;
}
static int32_t prv_x(uint32_t i) { return PR_X0 + (int32_t)(i * 195u / prv_cols()); }   /* (column i's left: x 35 .. 230) */

static int32_t pr_note(const step_t *st, uint32_t j)   /* the step's note j (0..3), then its lane hits (-1: none) */
{
    if (j < 4u)
        return j < st->n ? st->note[j] : -1;
    return (st->hit >> (j - 4u)) & 1u ? DRUM_LANE_NOTE[j - 4u] : -1;
}
static uint32_t pr_src(const track_t *t, uint32_t si, uint32_t len)   /* the step whose notes sound at si (NSTEP: none) */
{
    uint32_t k;
    for (k = 0; k < len && seq_steps(t)[si].time == ST_TIE; k++)
        si = (si + len - 1u) % len;
    return step_on(&seq_steps(t)[si]) ? si : NSTEP;
}
/* the rows lit by keys held on the selected track (bit r: row r from the top) */
static uint32_t pr_held(void)
{
    uint32_t k, i, m = 0;
    for (k = 0; k < 27u; k++)
        if (kb_chn[k] && kb_trk[k] == song.sel)
            for (i = 0; i < kb_chn[k]; i++) {
                int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - kb_chord[k][i];
                if (r >= 0 && r < PR_ROWS)
                    m |= 1u << r;
            }
    return m;
}
/* once a frame: the view toward the page's notes (centred; once there it stays while they fit); at once on a redraw
 * forced, a gap in the frames, another track or MENU > ANIM OFF */
static void pr_follow(const track_t *t)
{
    uint32_t i, j, len = (uint32_t)t->p[P_SLEN], base = prv_base(t), nc = prv_cols();
    int32_t lo = 127, hi = -1, tgt, d, cur = proll.lo;
    int snap = ui.force || !proll.init || proll.trk != song.sel || ui.frame != proll.frame + 1u || (ui_prefs & PREF_ANIM_OFF);
    if (proll.init && proll.frame == ui.frame)
        return;
    for (i = 0; i < nc && base + i < len; i++) {
        uint32_t s = pr_src(t, base + i, len);
        for (j = 0; s < NSTEP && j < 4u + NLANE; j++) {
            int32_t n = pr_note(&seq_steps(t)[s], j);
            if (n < 0)
                continue;
            lo = n < lo ? n : lo;
            hi = n > hi ? n : hi;
        }
    }
    if (hi < 0)
        lo = hi = last_note;
    if (hi - lo <= PR_ROWS - 1)                     /* all fit: centred, then kept while they fit */
        tgt = !snap && !proll.moving && lo >= cur && hi <= cur + PR_ROWS - 1 ? cur : (lo + hi + 1) / 2 - PR_ROWS / 2;
    else {                                            /* too wide: the cursor's (lowest) note in the middle */
        const step_t *st = &seq_steps(t)[ui.cursor];
        tgt = (st->n ? st->note[0] : (lo + hi) / 2) - PR_ROWS / 2;
    }
    tgt = clamp(tgt, 0, 128 - PR_ROWS);
    d = tgt - cur;
    proll.lo = (uint8_t)(snap ? tgt : cur + (d / 3 ? d / 3 : d > 0 ? 1 : d < 0 ? -1 : 0));
    proll.moving = proll.lo != tgt;
    proll.trk = (uint8_t)song.sel;
    proll.frame = ui.frame;
    proll.init = 1;
}
static int32_t pr_row_y(int32_t n) { return PR_Y0 + ((int32_t)proll.lo + PR_ROWS - 1 - n) * PR_RH; }
/* a bar w px wide in `hits` equal parts, 1 px apart (a RATCH step; 1: the bar) */
static void pr_split(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint32_t hits)
{
    int32_t k, pw = (w - (int32_t)hits + 1) / (int32_t)hits;
    if (hits < 2u) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (k = 0; k < (int32_t)hits; k++)
        cv_rect(x + k * (pw + 1), y, pw, h, c);
}
/* the bars of step s in column x (in its RATCH parts, hits; a TIE's carried bars: 1): a 1 px edge mark for a note
 * out of view; first: row y of the step's first note */
static void pr_bars(const step_t *st, int32_t x, int32_t w, uint16_t c, int acc_all, int32_t *first, uint32_t hits)
{
    uint32_t j;
    int32_t ybot = PR_Y0 + PR_ROWS * PR_RH;
    for (j = 0; j < 4u + NLANE; j++) {
        int32_t n = pr_note(st, j), y;
        int acc;
        if (n < 0)
            continue;
        acc = acc_all || (j >= 4u && ((st->acc >> (j - 4u)) & 1u));
        y = pr_row_y(n);
        if (*first < -1000)
            *first = y;
        if (y < PR_Y0)
            pr_split(x, PR_Y0, w, 1, c, hits);
        else if (y >= ybot)
            pr_split(x, ybot - 1, w, 1, c, hits);
        else if (acc)
            pr_split(x, y, w, PR_RH, c == T_ACCENT ? c : T_TEXT, hits);
        else
            pr_split(x, y + 1, w, PR_RH - 2, c, hits);
    }
}
static void graph_roll(const track_t *t, uint16_t c)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN], base = prv_base(t), nc = prv_cols(), ncol, mask = scale_mask(t), held = pr_held();
    uint64_t locks = motion_lock_steps(trk_index(t));
    int32_t r, ybot, gw, cw = (int32_t)(195u / nc), full = cv_split ? cv_split_h : (int32_t)cv_h;
    pr_rh = full >= 140 ? 6 : 5;                    /* (Dah: the roll to the panel's foot, its ruler over it) */
    pr_rows = (full - PR_Y0 - 4) / pr_rh;
    pr_rows = pr_rows < 19 ? 19 : pr_rows > 24 ? 24 : pr_rows;
    ybot = PR_Y0 + PR_ROWS * PR_RH;
    char nb[8];
    ncol = base < len ? (len - base < nc ? len - base : nc) : 0u;
    gw = prv_x(ncol) - PR_X0 + 1;
    for (r = 0; r < PR_ROWS; r++) {                 /* rows: the lane, the key, the C names */
        int32_t n = (int32_t)proll.lo + PR_ROWS - 1 - r, y = PR_Y0 + r * PR_RH;
        uint32_t pc = (uint32_t)n % 12u;
        int in = mask == 0xFFFu ? !KEY_BLACK[pc] : (int)((mask >> ((uint32_t)(n - t->p[P_ROOT] + 120) % 12u)) & 1u);
        if (in)
            cv_rect(PR_X0, y, gw, PR_RH, T_LANE);
        if ((held >> r) & 1u)
            cv_rect(PR_KX, y, 9, PR_RH - 1, T_ACCENT);
        else
            cv_rect(PR_KX, y, KEY_BLACK[pc] ? 5 : 9, PR_RH - 1, KEY_BLACK[pc] ? T_DIM : T_RAISE);
        if (pc == 0u) {
            cv_rect(PR_X0, y + PR_RH - 1, gw, 1, T_RAISE);
            note_name(nb, (uint32_t)n);
            GFX_HOOK_ALIGN(0, y, 23, y + PR_RH, AL_V, "piano roll C label on its row");
            GFX_HOOK_ALIGN(0, y, 23, y + PR_RH, AL_R | AL_PASS, "piano roll C labels' right edge");
            {   /* its ink ends at x 23, a column before the keys (by the advance, C1's 1 ended 2 px short) */
                int32_t b[4];
                text_ink(&AF_S, nb, b);
                cv_text_on(23 - b[2], y - 5, &AF_S, nb, T_DIM, T_SURF);   /* (the widest, "C-1", inside the panel) */
            }
        }
    }
    for (i = 0; i <= ncol; i++) {                     /* step lines, the beats brighter (zoomed out: the beats only) */
        uint32_t si = base + i;
        if (cw >= 4 || si % 4u == 0u)
            cv_rect(prv_x(i), PR_Y0, 1, ybot - PR_Y0, si % 16u == 0u ? T_MID : si % 4u ? T_GRID : T_RAISE);
    }
    {                                                 /* (Dah) the ruler on top, as HOME's: the whole pattern a line,
                                                       * the view on it a bar, the step playing a mark; a tick a step
                                                       * (the beats taller); the step playing's number on it, last */
        int32_t x0 = PR_X0 + (int32_t)(base * 195u / (len ? len : 1u)), x1 = PR_X0 + (int32_t)((base + ncol) * 195u / (len ? len : 1u));
        cv_rect(PR_X0, 1, 195, 1, T_LINE);
        cv_rect(x0, 0, x1 - x0 > 2 ? x1 - x0 : 2, 3, T_MID);
        if (song.playing && t->seq_idx < len)
            cv_rect(PR_X0 + (int32_t)(t->seq_idx * 195u / len), 0, 2, 3, T_ACCENT);
        for (i = 0; i < ncol; i++) {
            uint32_t si = base + i;
            int32_t x = prv_x(i);
            if (si % 4u == 0u)
                cv_rect(x, 9, 1, 3, si % 16u == 0u ? T_MID : T_RAISE);
            else if (cw >= 4)
                cv_rect(x, 11, 1, 1, T_GRID);
        }
    }
    for (i = 0; i < ncol; i++) {
        uint32_t si = base + i;
        int32_t x = prv_x(i);
        if (si == ui.cursor)
            cv_frame(x, PR_Y0 - 2, cw + 1, ybot - PR_Y0 + 4, T_TEXT);
        if ((locks >> si) & 1u)                       /* a parameter lock: a mark over the step */
            cv_rect(x + 1, PR_Y0 - 3, cw - 1, 1, T_ACCENT);
        if (step_detailed(&seq_steps(t)[si]))         /* 1.5.1: CHANCE / RATCH / NUDGE set: a mark over it (DETAIL) */
            cv_rect(x + cw / 2 - 2, 12, cw - 1 < 5 ? cw - 1 : 5, 2, c);
    }
    if (song.playing && t->seq_idx >= base && t->seq_idx < base + ncol) {   /* (Dah) the step playing: its column lit,
                                                         * step by step (as HOME's), the notes over it */
        int32_t x = prv_x(t->seq_idx - base);
        cv_rect(x + 1, PR_Y0, prv_x(t->seq_idx - base + 1u) - x - 1, ybot - PR_Y0, ux_mix(T_SURF, T_TEXT, 22));
    }
    for (i = 0; i < ncol; i++) {                      /* the notes */
        uint32_t si = base + i, s = pr_src(t, si, len), nx = (si + 1u) % len;
        const step_t *st, *ns = &seq_steps(t)[nx];
        int32_t x = prv_x(i), y = -10000, w = prv_x(i + 1u) - x;
        uint16_t col;
        if (s == NSTEP)
            continue;                                 /* a REST, empty, or a TIE after one */
        st = &seq_steps(t)[s];
        col = s == ui.cursor || si == ui.cursor ? T_ACCENT : c;
        if (s == si)
            pr_bars(st, x + (w > 5 ? 2 : 1), w > 5 ? w - 3 : w - 1 > 0 ? w - 1 : 1, col, (st->flags & SF_ACCENT) != 0u, &y,
                    step_ratchet(st));
        else                                          /* a TIE: the bars on through the gap before */
            pr_bars(st, x - 1, w, col, (st->flags & SF_ACCENT) != 0u, &y, 1u);
        if ((st->flags & SF_SLIDE) && step_ratchet(st) == 1u &&   /* (a ratcheted step never slides out) */
            ns->time == ST_NOTE && ns->n && y >= PR_Y0 && y < ybot) {
            int32_t ny = clamp(pr_row_y(ns->note[0]), PR_Y0, ybot - PR_RH);
            cv_line(x + w - 2, y + 2, x + w + 2, ny + 2, T_TEXT);   /* the slide into the next note */
        }
    }
    if (song.playing && t->seq_idx >= base && t->seq_idx < base + ncol) {   /* (Dah) the step playing: its number on the ruler */
        int32_t w, ib[4], nx;
        fmt_int(nb, (int32_t)t->seq_idx + 1);          /* (Dah) its number on the ruler, centred on its column */
        w = text_w(&AF_S, nb);
        text_ink(&AF_S, nb, ib);
        nx = clamp((2 * prv_x(t->seq_idx - base) + cw - (ib[0] + ib[2])) / 2, PR_X0, 230 - w);
        cv_rect(nx - 1, 4, w + 2, 10, T_SURF);
        cv_text_on(nx, 4 - AF_S_CAP_Y, &AF_S, nb, T_ACCENT, T_SURF);
    }
}
/* SEQ > STEP on a DRUM track: the grid, 8 lanes x the 16 steps of the page shown. Lanes by their two-letter
 * names (BD SD CP CH OH TM RS CB; CG CL CY on the other kits). A hit is a rounded square (accented: the
 * accent; a RATCH step's: its 2..4 parts as bars), an empty step a dot (brighter on the beats and on the selected lane); the selected lane is
 * underlaid RAISE, the cursor framed in TEXT; a TEXT bar over the step playing. 1.4.1: a muted lane (the EDIT layer's
 * black keys, eng_drum.c drum_mute): its name DIM, the MUTE icon after it, its hits DIM */
#define GD_RH 14                                    /* (Dah) the ruler on top, as HOME's */
#define GD_X0 40                                    /* the first step's column (12 px each) */
static int32_t gd_lp = 14;                          /* (Dah) a lane's pitch: the panel shared by the eight */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t l, i, len = (uint32_t)t->p[P_SLEN], mute = drum_mute[trk_index(t) % NPART];
    uint32_t base = (song.playing && !prv.nofollow && t->seq_idx < len ? t->seq_idx / 16u : ui.bank) * 16u;   /* (Dah:
                                                         * playing, the page playing, as HOME follows it) */
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == base / 16u ? t->seq_idx % 16u : 0xFFu;
    uint64_t locks = motion_lock_steps(trk_index(t));
    int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, y0 = GD_RH + 2, hs;
    (void)c;
    gd_lp = clamp((full - y0 - 2) / (int32_t)NLANE, 14, 20);
    hs = gd_lp - 5;                                 /* a hit: a square in its lane's middle */
    {   /* (Dah) the ruler, as HOME's: the whole pattern a line, this page of 16 on it a bar, the step playing a mark;
         * a tick a step, the beats taller; marks: a lock (ACCENT), a step's DETAIL (THEME); the step playing's number */
        int32_t x0 = GD_X0 + (int32_t)(base * 192u / (len ? len : 1u));
        int32_t x1 = GD_X0 + (int32_t)((base + 16u < len ? base + 16u : len) * 192u / (len ? len : 1u));
        cv_rect(GD_X0, 1, 192, 1, T_LINE);
        cv_rect(x0, 0, x1 - x0 > 2 ? x1 - x0 : 2, 3, T_MID);
        if (song.playing && t->seq_idx < len)
            cv_rect(GD_X0 + (int32_t)(t->seq_idx * 192u / len), 0, 2, 3, T_ACCENT);
        for (i = 0; i < 16u && base + i < len; i++) {
            int32_t x = GD_X0 + (int32_t)i * 12;
            cv_rect(x + 5, i % 4u == 0u ? 8 : 10, 1, i % 4u == 0u ? 3 : 1, i % 4u == 0u ? T_MID : T_GRID);
            if ((locks >> (base + i)) & 1u)
                cv_rect(x + 1, 12, 8, 1, T_ACCENT);
            if (step_detailed(&seq_steps(t)[base + i]))
                cv_rect(x + 3, 13, 4, 2, T_THEME);
        }
    }
    for (l = 0; l < NLANE; l++) {
        int32_t ly = y0 + (int32_t)l * gd_lp, lh = gd_lp - 1, hy = ly + (lh - hs) / 2;
        int sel = l == ui.lane, mu = (mute >> l) & 1u;
        uint16_t lc = ux.drum[l], row = sel ? ux_mix(T_SURF, lc, 16) : T_SURF, hc = mu ? T_DIM : lc, ac = mu ? T_DIM : T_ACCENT;
        if (sel) {                                   /* (Dah) the lane picked as HOME's track: tinted, framed in its
                                                         * colour, a band of it at the left */
            cv_rect(3, ly, 234, lh, row);
            cv_frame(3, ly, 234, lh, lc);
            cv_rect(3, ly, 3, lh, lc);
        }
        GFX_HOOK_ALIGN(0, ly, 0, ly + lh, AL_V, "drum lane label on its hits' line");
        cv_text_on(10, ly + CAP_IN(S, lh), &AF_S, drum_lane_abbr(t, l), mu ? T_DIM : sel ? T_TEXT : lc, row);
        if (mu) {                                    /* (27 .. 39: between the name and the first hit) */
            GFX_HOOK_ALIGN(0, ly, 0, ly + lh, AL_V, "drum lane mute icon centred up/down");
            cv_icon_in(27, ly, 0, lh, 12, ICON_MUTE, T_DIM, row);
        }
        for (i = 0; i < 16u && base + i < len; i++) {
            const step_t *st = &seq_steps(t)[base + i];
            uint32_t b = 1u << l;
            int32_t x = GD_X0 + (int32_t)i * 12;
            uint16_t under = row;
            if (i == ph) {                           /* the step playing: its column lit (as HOME's) */
                under = ux_mix(row, T_TEXT, 22);
                cv_rect(x - 1, ly + (sel ? 1 : 0), 12, lh - (sel ? 2 : 0), under);
            }
            if ((step_lanes(st) & b) && step_ratchet(st) > 1u)
                pr_split(x, hy, 10, hs, (step_accents(st) & b) ? ac : hc, step_ratchet(st));
            else if (step_lanes(st) & b)
                cv_rrect(x, hy, 10, hs, 2, (step_accents(st) & b) ? ac : hc, under);
            else
                cv_rect(x + 4, ly + lh / 2 - 1, 2, 2, i % 4u == 0u || sel ? T_MID : T_DIM);
            if (sel && base + i == ui.cursor)          /* the cursor: a 1 px frame */
                cv_frame(x - 1, hy - 1, 12, hs + 2, T_TEXT);
        }
    }
    if (ph != 0xFFu) {                               /* (Dah) the step playing's number on the ruler, centred on it */
        char nb[4];
        int32_t w, ib[4], nx;
        fmt_int(nb, (int32_t)t->seq_idx + 1);
        w = text_w(&AF_S, nb);
        text_ink(&AF_S, nb, ib);
        nx = clamp((2 * (GD_X0 + (int32_t)ph * 12) + 10 - (ib[0] + ib[2])) / 2, GD_X0, 232 - w);
        cv_rect(nx - 1, 3, w + 2, 11, T_SURF);
        cv_text_on(nx, 4 - AF_S_CAP_Y, &AF_S, nb, T_ACCENT, T_SURF);
    }
}
/* SEQ > DETAIL (1.5.1, Discussion #199): the cursor's 16 steps as four lanes, the knobs' order top to bottom, each
 * named on the left (the lane of the knob just turned in the accent): CHANCE a bar as high as the chance, RATCH the
 * step's bar in its 2..4 parts (as the roll draws a ratchet), NUDGE a tick left / right of the step's middle, VEL a bar
 * as high as the velocity (an ACC step's full, TEXT). Off its default a value is bright (THEME, the cursor's ACCENT),
 * at it faint (RAISE: 100 %, x1, 0, a VEL never set); DIM where it does not play (a NUDGE with QUANTIZE ON or on a
 * ratchet, a VEL under ACC). A step that plays nothing: a dot (a value left on it DIM); past LEN nothing. The cursor
 * framed through the lanes, the step playing a line */
#define SD_X0 36                                    /* the first step column (12 px each) */
static void graph_detail(const track_t *t, uint16_t c)
{
    static const char *const NAME[4] = {"CHN", "RAT", "NDG", "VEL"};
    static const uint8_t WT[4] = {28, 18, 18, 28};   /* (Dah) the lanes' shares of the panel under the ruler */
    uint32_t i, l, len = (uint32_t)t->p[P_SLEN], ncol;
    uint32_t base = (song.playing && !prv.nofollow && t->seq_idx < len ? t->seq_idx / 16u : ui.bank) * 16u;   /* (Dah:
                                                         * playing, the page playing, as HOME follows it) */
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == base / 16u ? t->seq_idx % 16u : 0xFFu;
    int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, LY[4], LH[4], avail, y = GD_RH + 4;
    avail = full - y - 4 - 3 * 8;
    for (l = 0; l < 4u; l++) {
        LH[l] = avail * WT[l] / 92;
        LY[l] = y;
        y += LH[l] + 8;
    }
    ncol = base < len ? (len - base < 16u ? len - base : 16u) : 0u;
    {   /* (Dah) the ruler, as HOME's: the whole pattern a line, this page on it a bar, the step playing a mark; ticks */
        int32_t x0 = SD_X0 + (int32_t)(base * 192u / (len ? len : 1u));
        int32_t x1 = SD_X0 + (int32_t)((base + ncol) * 192u / (len ? len : 1u));
        cv_rect(SD_X0, 1, 192, 1, T_LINE);
        cv_rect(x0, 0, x1 - x0 > 2 ? x1 - x0 : 2, 3, T_MID);
        if (ph != 0xFFu)
            cv_rect(SD_X0 + (int32_t)(t->seq_idx * 192u / len), 0, 2, 3, T_ACCENT);
        for (i = 0; i < ncol; i++)
            cv_rect(SD_X0 + (int32_t)i * 12 + 6, i % 4u == 0u ? 8 : 10, 1, i % 4u == 0u ? 3 : 1, i % 4u == 0u ? T_MID : T_GRID);
    }
    if (ph != 0xFFu)                                 /* the step playing: its column lit (as HOME's) */
        cv_rect(SD_X0 + (int32_t)ph * 12, GD_RH + 1, 13, full - GD_RH - 3, ux_mix(T_SURF, T_TEXT, 14));
    for (l = 0; l < 4u; l++) {
        int32_t yb = LY[l] + LH[l];
        cv_rect(SD_X0, yb, (int32_t)ncol * 12 + 1, 1, T_GRID);   /* the lane's floor */
        cv_text_on(8, LY[l] + LH[l] / 2 - 8, &AF_S, NAME[l], ui.hot_t && ui.hot_col == l ? T_ACCENT : T_MID, T_SURF);
    }
    for (i = 0; i < ncol; i++) {
        uint32_t si = base + i;
        const step_t *st = &seq_steps(t)[si];
        int32_t x = SD_X0 + (int32_t)i * 12;
        uint16_t hi = si == ui.cursor ? T_ACCENT : c;
        if (si == ui.cursor)
            cv_frame(x, GD_RH + 1, 13, full - GD_RH - 3, T_TEXT);
        for (l = 0; l < 4u; l++) {
            uint32_t k = SD_KIND[l];
            int32_t v = ev_sval(st, k), y = LY[l], h = LH[l], set = v != ev_sdefv(k), bh, ry = y + (h - 8) / 2;
            uint16_t col;
            if (!sd_takes(st, k) && !set) {           /* nothing plays there */
                cv_rect(x + 5, y + h - 3, 2, 2, T_DIM);
                continue;
            }
            col = !sd_takes(st, k) ? T_DIM : set ? hi : T_RAISE;
            if (k == EVK_CHANCE && v > 100) {         /* 1.5.5: a condition: a:b as b cells up the lane, the a-th lit; */
                int32_t c = v - 100, j, nb = c < 10 ? COND_B[c - 1] : 0;   /* FILL a frame, !FILL a frame crossed */
                for (j = 0; j < nb; j++)
                    cv_rect(x + 2, y + h - 6 - 7 * j, 9, 5, j == COND_A[c - 1] - 1 ? col : T_GRID);
                if (!nb)
                    cv_frame(x + 2, y + 2, 9, h - 2, col);
                if (c == (int32_t)COND_NFILL)
                    cv_rect(x + 2, y + h / 2, 9, 1, col);
            } else if (k == EVK_CHANCE) {
                bh = v * h / 100;
                cv_rect(x + 2, y + h - (bh < 2 ? 2 : bh), 9, bh < 2 ? 2 : bh, col);
            } else if (k == EVK_RATCH) {
                pr_split(x + 2, ry, 9, 8, col, (uint32_t)v);
            } else if (k == EVK_NUDGE) {
                if (set && (t->p[P_SQNT] || !ev_nudge_plays(t, st)))
                    col = T_DIM;
                cv_rect(x + 6, y + 2, 1, h - 4, T_GRID);
                if (!set)
                    cv_rect(x + 5, y + h / 2 - 1, 3, 3, col);
                else
                    cv_rect(x + 5 + v * 5 / 8, y + 3, 3, h - 6, col);
            } else if (st->flags & SF_ACCENT) {       /* VEL: an ACC step plays 127 */
                cv_rect(x + 2, y, 9, h, sd_takes(st, k) ? T_TEXT : T_DIM);
            } else {
                bh = v * h / 127;
                cv_rect(x + 2, y + h - (bh < 2 ? 2 : bh), 9, bh < 2 ? 2 : bh, col);
            }
        }
    }
    if (ph != 0xFFu) {                               /* (Dah) the step playing's number on the ruler, centred on it */
        char nb[4];
        int32_t w, ib[4], nx;
        fmt_int(nb, (int32_t)t->seq_idx + 1);
        w = text_w(&AF_S, nb);
        text_ink(&AF_S, nb, ib);
        nx = clamp((2 * (SD_X0 + (int32_t)ph * 12) + 13 - (ib[0] + ib[2])) / 2, SD_X0, 232 - w);
        cv_rect(nx - 1, 3, w + 2, 11, T_SURF);
        cv_text_on(nx, 4 - AF_S_CAP_Y, &AF_S, nb, T_ACCENT, T_SURF);
    }
}
/* SCL: the 12 keys as rounded bars (black keys high, white keys low): in the scale THEME, the root the
 * accent, out of the scale RAISE */
/* (Dah) the notes the track sounds now: bit k = pitch class k (its voices with their gate on, the arp's, MONO's) */
static uint32_t trk_sounding(const track_t *t)
{
    uint32_t i, m = 0;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate)
            m |= 1u << (t->v[i].note % 12u);
    return m;
}
static void graph_scale(const track_t *t, uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint32_t i, mask = scale_mask(t), on = trk_sounding(t);
    for (i = 0; i < 12u; i++) {
        uint32_t deg = (i + 12u - (uint32_t)t->p[P_ROOT]) % 12u;
        int32_t x = 13 + (int32_t)i * 18;
        uint16_t col = (mask >> deg) & 1u ? (deg == 0u ? T_ACCENT : c) : T_RAISE;
        cv_rrect(x + 3, BLACK[i] ? 6 : 38, 10, 52, 3, (on >> i) & 1u ? T_TEXT : col, T_SURF);   /* (Dah: sounding: TEXT) */
    }
}
/* CHORD (SCL 2): the chord's name ("Cm7": the last one the track played; before any, the one on its ROOT from
 * C4) over the keys from the C below its lowest note (two octaves, three for a wide one): its notes THEME, its
 * root's ACCENT, the others RAISE. OFF or a kit: what to do instead */
static void panel_note(const char *a, const char *b, const char *c);
static void panel_alert(uint32_t id, const char *a);
static void graph_chord(const track_t *t, uint16_t c)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    uint8_t nn[MATCH_MAX];
    uint32_t n, i, j, k = trk_index(t), lo, cnt, w;
    int32_t r;
    uint16_t mask;
    char b[12];
    const char *cap = "LAST";
    if (!t->p[P_CHRD]) {
        panel_note("CHORD KEYS OFF", "[K1] DIA3: IN-KEY CHORDS", 0);   /* (the title says what they are) */
        return;
    }
    if (chord_kit(t)) {
        panel_note("NO CHORDS ON KITS", 0, 0);
        return;
    }
    if (chord_last[k].n) {
        r = chord_last[k].root;
        mask = chord_last[k].mask;
        n = chord_last[k].n;
        for (i = 0; i < n; i++)
            nn[i] = chord_last[k].note[i];
    } else {
        n = chord_make(t, (uint32_t)(60 + t->p[P_ROOT]), nn, &r, &mask);
        cap = "ON ROOT";
    }
    if (trk_vmode(t) != V_POLY)
        cap = "ROOT ONLY";                              /* MONO / LEGATO / UNISON */
    chord_name(b, (uint32_t)r, mask);
    cv_text_on(14, 8, &AF_M, b, T_TEXT, T_SURF);
    GFX_HOOK_ALIGN(0, 0, 0, 8 + AF_M.asc, AL_B, "chord caption on the name's baseline");
    cv_text_r(226, 8 + AF_M.asc - AF_S.asc, &AF_S, cap, T_MID, T_SURF);   /* on the name's baseline */
    lo = nn[0] - nn[0] % 12u;
    cnt = nn[n - 1u] - lo < 24u ? 24u : 36u;
    w = 216u / cnt;
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_CELLS | AL_N(cnt), "chord keys centred");
    for (i = 0; i < cnt; i++) {
        uint32_t note = lo + i, on = 0;
        int32_t x = 13 + (int32_t)(i * w);              /* (216 px of keys and their gaps: x 13 .. 227) */
        for (j = 0; j < n; j++)
            on |= nn[j] == note;
        for (j = 0; j < NVOICE; j++)                    /* (Dah) sounding now: TEXT */
            if (t->v[j].active && t->v[j].gate && t->v[j].note == note)
                on = 2;
        cv_rrect(x, BLACK[note % 12u] ? 36 : 62, (int32_t)w - 2, 48, w > 6u ? 3 : 2,
                 !on ? T_RAISE : on == 2u ? T_TEXT : note % 12u == (uint32_t)r % 12u ? T_ACCENT : c, T_SURF);
    }
}
/* INSERT pages (1.5, fx.c track_insert): the drives and CRUSH as their curve, input (-1.5 .. +1.5 of a voice's full
 * scale) across, output up, dry and wet blended by MIX (fx.c ins_shape: the same shape; the dry diagonal RAISE); the
 * swept ones as two cycles of their sweep, as deep as DEPTH (FLANG the squared triangle, CHOR a sine, PHASR a
 * triangle). OFF: the dry diagonal, dim */
static void graph_insert(const track_t *t, uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    int32_t x, cy = graph_ht / 2, a = 38 * graph_ht / 100, py = cy, ty = clamp(t->p[P_ITYPE], 0, IT_N - 1);
    int32_t m = ins_mixq(t->p[P_IMIX]), g = 4096 + t->p[P_IA] * t->p[P_IA] * 3, lvl = LEVEL_Q12[t->p[P_IC] & 127];
    uint32_t sh = ins_bits_sh(t->p[P_IA]);
    cv_rect(PANEL_X0, cy, pw, 1, T_RAISE);
    cv_line(PANEL_X0 + pw / 2, cy - a, PANEL_X0 + pw / 2, cy + a, T_RAISE);
    if (ty == IT_OFF || ty >= IT_PHASER || !m)
        cv_line(PANEL_X0 + pw / 6, cy + a, PANEL_X0 + pw * 5 / 6, cy - a, ty ? T_RAISE : T_DIM);
    if (ty == IT_OFF)
        return;
    for (x = 0; x < pw; x++) {
        int32_t y, w;
        if (ty >= IT_PHASER) {                       /* the sweep, two cycles */
            uint32_t ph = (uint32_t)x * (0xFFFFFFFFu / ((uint32_t)pw / 2u)), tr = pf_tri(ph);
            w = ty == IT_CHORUS ? osc_sine(ph) * 2 : ty == IT_FLANGER ? (int32_t)((tr * tr) >> 16) * 2 - 65536 : (int32_t)tr * 2 - 65536;
            y = cy - (w >> 1) * a / 32768 * t->p[P_IB] / 127;
        } else {                                     /* the curve: v in, dry v, wet shaped */
            int32_t v = (x - pw / 2) * 49152 / (pw / 2), o;
            o = ty == IT_CRUSH ? ins_shape(IT_CRUSH, v, sh) : (ins_shape((uint32_t)ty, ((v >> 3) * g) >> 9, 0) * lvl) >> 12;
            w = v + mulq16(o - v, (uint32_t)m << 1);
            y = cy - clamp(w, -49152, 49152) * a / 49152;
        }
        if (x)
            cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
}
/* SLICER page: the pattern's 16 steps, a 'x' step a full bar; a '.' step: GATE a bar as high as it stays
 * open (DEPTH), STUT hatched (it repeats the last 'x'); the step playing underlined. Grey when OFF. */
static void graph_slicer(const track_t *t, uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    uint32_t i, pat = sl_pattern(t), mode = (uint32_t)t->p[P_SLCR], cur = sl[t - trk].idx;
    int32_t open = 70 - t->p[P_SLDEPTH] * 70 / 127;      /* px a closed GATE step keeps */
    uint16_t col = mode == SL_OFF ? T_DIM : c;
    for (i = 0; i < 16u; i++) {
        int32_t x = pw >= (int32_t)PANEL_W ? bar_x(i) : PANEL_X0 + (int32_t)i * pw / 16, y, bw = pw >= (int32_t)PANEL_W ? 9 : pw / 16 - 2;
        if ((pat >> i) & 1u) {
            cv_rrect(x, 8, bw, 72, 2, mode == SL_OFF ? T_RAISE : col, T_SURF);
        } else if (mode == SL_STUT) {
            for (y = 8; y < 80; y += 4)
                cv_rect(x, y, bw, 1, col);
        } else {
            cv_rrect(x, 77, bw, 3, 1, T_RAISE, T_SURF);
            if (open > 3)
                cv_rrect(x, 80 - open, bw, open, 2, T_RAISE, T_SURF);
        }
        if (mode != SL_OFF && i == cur)
            cv_rect(x, 84, bw, 2, T_ACCENT);
    }
}
/* MOD page: the four slots as rows "1 LFO > CUT +50%" (M, a list's rows: the source to x 78, the arrow 84..98, the
 * destination of up to 5 characters 106..160, the amount from 172), the one KNOB 2..4 edit selected, slots that do
 * nothing (SRC, DST or AMT at 0) dim */
static void graph_mod(const track_t *t, uint16_t c)
{
    uint32_t k;
    for (k = 0; k < NMSLOT; k++) {
        const int16_t *p = &t->p[P_M1SRC + 3u * k];
        int32_t y = 14 + (int32_t)k * 24, on = p[0] && p[1] && p[2], sel = k == mod_ui_slot, x;
        uint16_t bg = sel ? T_THEME : T_SURF, col = sel ? T_INK : on ? c : T_DIM;
        char b[8];
        const char *unit;
        if (sel)
            cv_rrect(6, y, 228, 17, 4, T_THEME, T_SURF);
        b[0] = (char)('1' + k);
        b[1] = 0;
        GFX_HOOK_ALIGN(0, y, 0, y + 17, AL_V, "mod row text centred up/down");
        cv_text_on(14, y + CAP_IN(M, 17), &AF_M, b, sel ? T_INK : T_MID, bg);
        GFX_HOOK_ALIGN(0, y, 0, y + 17, AL_V, "mod row text centred up/down");
        cv_text_on(30, y + CAP_IN(M, 17), &AF_M, N_MSRC[clamp(p[0], 0, MS_N - 1)], col, bg);
        cv_line(84, y + 8, 98, y + 8, col);              /* an arrow */
        cv_line(94, y + 4, 98, y + 8, col);
        cv_line(94, y + 12, 98, y + 8, col);
        cv_text_on(106, y + CAP_IN(M, 17), &AF_M, mod_dst_name(t, p[1]), col, bg);
        param_format(&TP[P_M1AMT], p[2], b, &unit);
        GFX_HOOK_ALIGN(0, y, 0, y + 17, AL_V, "mod row text centred up/down");
        x = cv_text_on(172, y + CAP_IN(M, 17), &AF_M, b, col, bg) + 2;
        GFX_HOOK_ALIGN(0, 0, 0, y + CAP_IN(M, 17) + AF_M.asc, AL_B, "mod row unit on its amount's baseline");
        cv_text_on(x, y + CAP_IN(M, 17) + AF_M.asc - AF_S.asc, &AF_S, unit, sel ? T_INK : T_DIM, bg);   /* (a unit: S) */
    }
}

static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *st = &seq_steps(t)[i];
        h = (h ^ (st->note[0] + st->n * 128u + st->time * 1024u + st->flags * 4096u + st->note[1] * 65536u)) *
            16777619u;
        h = (h ^ (st->hit | (uint32_t)st->acc << 8 | (uint32_t)st->note[2] << 16 | (uint32_t)st->note[3] << 24)) * 16777619u;
        h = (h ^ (st->vel | (uint32_t)st->probability << 8)) * 16777619u;   /* (1.5.1: DETAIL, the marks) */
    }
    return h;
}

static uint32_t str_hash(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* One shared, reduced sample display. Decode only in the UI/main loop with a
 * private IMA state, 512 samples per frame; no audio voice state is touched.
 * Cache follows the sample zone, selected note and user-slot generation. */
#define SAMPLE_WAVE_COLS 96u
static struct {
    int16_t lo[SAMPLE_WAVE_COLS], hi[SAMPLE_WAVE_COLS];
    const smp_zone_t *zone;
    uint32_t key, gen, pos;
    int32_t pred, index, peak;
    uint8_t ready;
} sample_wave;

static uint32_t sample_wave_zone(const track_t *t)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu;
    if (si < SMP_NSETS) {
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (last_note >= SMP_ZONES[set->z0 + i].lo && last_note <= SMP_ZONES[set->z0 + i].hi) zi = set->z0 + i;
        if (zi == 0xFFFFu && set->nz) zi = set->z0;
    } else {
        uint32_t k = si - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (last_note >= usr_zone[k][i].lo && last_note <= usr_zone[k][i].hi) zi = 0x8000u | k << 5 | i;
        if (zi == 0xFFFFu && usr_nz[k]) zi = 0x8000u | k << 5;
    }
    return zi;
}

static void sample_wave_tick(const track_t *t)
{
    uint32_t zi = sample_wave_zone(t), key = zi + ((uint32_t)last_note << 16), i;
    if (key != sample_wave.key || sample_wave.gen != smp_user_gen || !sample_wave.zone) {
        sample_wave.key = key; sample_wave.gen = smp_user_gen;
        sample_wave.zone = zi == 0xFFFFu ? 0 : smp_zone(zi);
        sample_wave.pos = 0; sample_wave.pred = sample_wave.index = 0;
        sample_wave.peak = 1; sample_wave.ready = 0;
        for (i = 0; i < SAMPLE_WAVE_COLS; i++) sample_wave.lo[i] = sample_wave.hi[i] = 0;
    }
    const smp_zone_t *z = sample_wave.zone;
    if (!z || !z->n || sample_wave.ready) return;
    for (i = 0; i < 512u && sample_wave.pos < z->n; i++) {
        uint32_t pos = sample_wave.pos, b = SMP_DATA[z->off + (pos >> 1)];
        uint32_t code = (pos & 1u) ? b >> 4 : b & 15u;
        int32_t step = IMA_STEP[sample_wave.index], d = step >> 3;
        if (code & 4u) d += step;
        if (code & 2u) d += step >> 1;
        if (code & 1u) d += step >> 2;
        sample_wave.pred = clamp(sample_wave.pred + ((code & 8u) ? -d : d), -32768, 32767);
        sample_wave.index = clamp(sample_wave.index + IMA_IDX[code & 7u], 0, 88);
        uint32_t col = pos * SAMPLE_WAVE_COLS / z->n;
        int32_t v = sample_wave.pred, a = v < 0 ? -v : v;
        if (v < sample_wave.lo[col]) sample_wave.lo[col] = (int16_t)v;
        if (v > sample_wave.hi[col]) sample_wave.hi[col] = (int16_t)v;
        if (a > sample_wave.peak) sample_wave.peak = a;
        sample_wave.pos++;
    }
    sample_wave.ready = sample_wave.pos == z->n;
}

static void graph_sample(uint16_t c)
{
    uint32_t i;
    const smp_zone_t *z = sample_wave.zone;
    cv_rect(PANEL_X0, 48, PANEL_W, 1, T_RAISE);
    for (i = 0; i < SAMPLE_WAVE_COLS; i++) {
        int32_t x = 12 + (int32_t)i * 216 / (SAMPLE_WAVE_COLS - 1u);
        cv_line(x, 48 - sample_wave.hi[i] * 38 / sample_wave.peak,
                x, 48 - sample_wave.lo[i] * 38 / sample_wave.peak, c);
    }
    if (z->looped && TSEL->p[P_E3]) {
        int32_t left = 12 + (int32_t)(z->ls * 216u / z->n), right = 12 + (int32_t)(z->le * 216u / z->n);
        cv_line(left, 4, left, 90, T_MID); cv_line(right, 4, right, 90, T_MID);
    }
}

#if FELUCCA_SLICE
/* SLICES (ui_slice.c): the waveform from the marker before the selected one to the one after it (the selected
 * slice tinted, its marker the accent, outside the slices DIM), under it the whole material with every marker and
 * the view; then the selected slice's length and the source */
static void graph_slices(void)
{
    uint32_t a, b, len, n = slice_count(), j = slice_sel(), i, src, div, ma, mb;
    char t[20], u[12];
    if (!slice_src(&src, &div) || !n) {             /* (it plays a sine: eng_slice.c) */
        panel_alert(ICON_X_NOFILE, "SAMPLE NOT FOUND");
        return;
    }
    slice_view(&a, &b, &len);
    slice_env(a, b);
    ma = slice_mark(0);
    mb = slice_mark(n);
#define SPX(p) (12 + (int32_t)((uint32_t)((p) - a) * SP_COLS / (b - a)))   /* p in [a, b] */
    if (j < n) {                                     /* the selected slice */
        uint32_t s0 = slice_mark(j), s1 = slice_mark(j + 1u);
        int32_t x0 = SPX(s0 > a ? s0 : a), x1 = SPX(s1 < b ? s1 : b);
        if (x1 > x0)
            cv_rect(x0, 6, x1 - x0, 80, T_TINT);
    }
    for (i = 0; i < SP_COLS; i++) {
        uint32_t p = a + (uint32_t)((uint32_t)i * (b - a) / SP_COLS);
        cv_line(12 + (int32_t)i, 46 - sp.hi[i] * 38 / 64, 12 + (int32_t)i, 46 - sp.lo[i] * 38 / 64,
                p >= ma && p < mb ? T_THEME : T_DIM);
    }
    for (i = 0; i <= n; i++) {                       /* the markers in the view (the selected one last: on top) */
        uint32_t p = slice_mark(i);
        if (i != j && p >= a && p <= b)
            cv_rect(SPX(p), 4, 1, 84, T_MID);
    }
    {
        uint32_t p = slice_mark(j);
        if (p >= a && p <= b)
            cv_rect(SPX(p) > 226 ? 226 : SPX(p), 2, 2, 88, T_ACCENT);
    }
#undef SPX
    cv_rrect(12, 96, (int32_t)SP_COLS, 3, 1, T_RAISE, T_SURF);   /* the whole: every marker, the view */
    for (i = 0; i <= n; i++)
        cv_rect(12 + (int32_t)((uint32_t)slice_mark(i) * (SP_COLS - 1u) / len), 94, 1, 7, i == j ? T_ACCENT : T_MID);
    {
        int32_t x0 = 12 + (int32_t)((uint32_t)a * SP_COLS / len), x1 = 12 + (int32_t)((uint32_t)b * SP_COLS / len);
        cv_rect(x0, 103, x1 - x0 < 2 ? 2 : x1 - x0, 2, T_THEME);
    }
    str_cpy(t, "LEN ", sizeof t);
    slice_time(u, j < n ? slice_mark(j + 1u) - slice_mark(j) : len - mb);
    str_cpy(t + 4, u, sizeof t - 4);
    str_cpy(t + str_len(t), " S", sizeof t - str_len(t));
    cv_text(12, 104, &AF_M, t, T_MID);                  /* the slice's detail (M): its capitals rows 108 .. 118 */
    cv_text_r(228, 104, &AF_M, N_SLC_SRC[src], src && src != SLC_SRC_PIANO ? T_THEME : T_DIM, T_SURF);
}
#endif

/* WHEEL: the nine drawbars as rounded bars over RAISE slots (the bars of the knob just turned: the accent),
 * their footages under them */
static void graph_wheel(const track_t *t, uint16_t c)
{
    uint32_t k;
    static const char *const names[9] = {"16", "5.3", "8", "4", "2.7", "2", "1.6", "1.3", "1"};
    for (k = 0; k < 9u; k++) {
        int32_t x = 11 + (int32_t)k * 25, level = drw_level(t->p, k);
        if (t->p[P_E4] && k == 8u) level = 0; /* DSP's percussion cancels the 1-foot bar. */
        int hot = ui.hot_t && cur_page()->id[0] == P_E0 &&
                  (ui.hot_col == 0u || ui.hot_col == (k < 2u ? 1u : k < 4u ? 2u : 3u));
        cv_rrect(x + 7, 2, 4, 73, 2, T_RAISE, T_SURF);
        if (level) cv_rrect(x + 3, 75 - level * 8, 12, level * 8, 3, hot ? T_ACCENT : c, T_SURF);
        else cv_rrect(x + 3, 73, 12, 2, 1, hot ? T_ACCENT : T_DIM, T_SURF);
        GFX_HOOK_ALIGN(x + 7, 0, x + 11, 0, AL_H, "wheel footage under its drawbar");
        cv_text_in(x + 3, 82, 12, &AF_S, names[k], T_MID, T_SURF);   /* under the bar, by its ink */
    }
}
/* The FM charts' parts: an operator box 21 x 17 (rows 23 px apart, columns 24), junction dots, Manhattan routes */
#define FM_BH 17
static void fm_dot(int32_t x, int32_t y, uint16_t c) { cv_rect(x - 1, y - 1, 3, 3, c); }
/* a Manhattan route, 1 px: down from (x0, y0) to the jog row jy, across to x1, down to y1 */
static void fm_route(int32_t x0, int32_t y0, int32_t x1, int32_t jy, int32_t y1, uint16_t c)
{
    cv_rect(x0, y0, 1, jy - y0, c);
    cv_rect(x0 < x1 ? x0 : x1, jy, (x0 < x1 ? x1 - x0 : x0 - x1) + 1, 1, c);
    cv_rect(x1, jy, 1, y1 - jy, c);
}
/* a feedback loop on the operator box at (x, y): out of its right side, up, back in on top with an arrow */
static void fm_loop(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 11, y + 5, 5, 1, c);
    cv_rect(x + 15, y - 4, 1, 9, c);
    cv_rect(x, y - 4, 15, 1, c);
    cv_rect(x, y - 4, 1, 4, c);
    cv_line(x - 2, y - 3, x - 1, y - 2, c); cv_line(x + 2, y - 3, x + 1, y - 2, c);
}
/* the output bus under the carriers (the leftmost at cl, the rightmost at cr) at y bus, an arrow out on the right */
static void fm_bus(int32_t cl, int32_t cr, int32_t bus)
{
    cv_rect(cl, bus, cr + 24 - cl, 1, T_MID);
    cv_line(cr + 20, bus - 3, cr + 23, bus, T_MID); cv_line(cr + 20, bus + 3, cr + 23, bus, T_MID);
}

#ifndef FM6_CHART_HOOK
#define FM6_CHART_HOOK(kind, a, b) ((void)0)   /* ui_render.c (host tests): the chart's parts one by one (its lint) */
#endif
enum { FMH_BOX, FMH_ROUTE, FMH_CAR, FMH_BUS, FMH_FB, FMH_LABEL, FMH_END };
/* FM6's 32 algorithms as charts. FM6_CELL: per operator 1..6 its cell, the column in 24 px steps (bits 0..2) and
 * the row above the output bus (bits 4..5; row 0 = the carriers). The routes, the carriers and the feedback
 * operator are fm6_core.c's FM6_ALG itself (fm6_routes, fm6_car_ops, fm6_fb_op). ui_test.c checks the cells
 * against them (a modulator one row above what it modulates, the carriers on the bottom row and only they, one
 * operator per cell) and the routes against the 32 algorithms written out; ui_render.c lints every chart as drawn
 * (no route through a box or touching another route, no two boxes overlapping, all inside the panel). The cells:
 * a search for the fewest and shortest bends, the operators numbered left to right. */
static const uint8_t FM6_CELL[32][6] = {
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x31}, {0x00, 0x10, 0x02, 0x12, 0x22, 0x32}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x21},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x21}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12}, {0x00, 0x10, 0x01, 0x11, 0x02, 0x12},
    {0x00, 0x10, 0x01, 0x11, 0x12, 0x22}, {0x00, 0x10, 0x01, 0x12, 0x11, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x13, 0x23},
    {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x20, 0x01, 0x11, 0x12}, {0x00, 0x10, 0x03, 0x12, 0x13, 0x14},
    {0x00, 0x10, 0x02, 0x11, 0x12, 0x13}, {0x00, 0x10, 0x01, 0x11, 0x20, 0x21}, {0x00, 0x10, 0x02, 0x12, 0x21, 0x22},
    {0x01, 0x10, 0x11, 0x21, 0x12, 0x22}, {0x01, 0x12, 0x10, 0x20, 0x11, 0x21}, {0x01, 0x10, 0x12, 0x11, 0x21, 0x31},
    {0x00, 0x10, 0x20, 0x01, 0x02, 0x11}, {0x00, 0x01, 0x10, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x10, 0x02, 0x03, 0x12},
    {0x00, 0x10, 0x01, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x11, 0x02, 0x03, 0x12}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x13},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x13}, {0x00, 0x01, 0x11, 0x02, 0x12, 0x13}, {0x00, 0x01, 0x11, 0x03, 0x13, 0x14},
    {0x00, 0x10, 0x01, 0x11, 0x21, 0x02}, {0x00, 0x01, 0x02, 0x12, 0x03, 0x13}, {0x00, 0x01, 0x02, 0x12, 0x22, 0x03},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x14}, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05}};
/* algorithm a's routes: m[i] bit j: operator j + 1 modulates operator i + 1. FM6_ALG runs the operators sixth
 * first, each reading a bus (or none) and writing one (or adding to it): who wrote a bus modulates its reader */
static void fm6_routes(uint32_t a, uint8_t *m)
{
    const uint8_t *f = FM6_ALG[a & 31u];
    uint8_t bus[4] = {0, 0, 0, 0};
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        uint32_t op = 5u - k, in = (f[k] >> 4) & 3u, o = f[k] & 3u;
        m[op] = in ? bus[in] : 0u;
        bus[o] = (uint8_t)(((f[k] & FM6_OADD) ? bus[o] : 0u) | 1u << op);
    }
}
/* the operator with feedback in algorithm a (0..5: operator 1..6) */
static uint32_t fm6_fb_op(uint32_t a)
{
    uint32_t k;
    for (k = 0; k < 5u && (FM6_ALG[a & 31u][5u - k] & 0xC0u) != 0xC0u; k++)
        ;
    return k;
}
/* the carriers of algorithm a: bit k operator k + 1 (fm6_carriers counts the sixth first) */
static uint32_t fm6_car_ops(uint32_t a)
{
    uint32_t c = fm6_carriers(a), r = 0, k;
    for (k = 0; k < 6u; k++)
        r |= (c >> (5u - k) & 1u) << k;
    return r;
}
/* the algorithm FM6 plays on track t (0..31): ALG, or the patch's at PAT */
static uint32_t fm6_alg_of(const track_t *t)
{
    return t->p[P_E0] >= 1 && t->p[P_E0] <= 32 ? (uint32_t)t->p[P_E0] - 1u : fm6_patch[(t - trk) % NTRK][FP_ALG] & 31u;
}
/* FM6's EDIT pages: the algorithm, drawn as graph_fm draws DIGITAL's, "ALG 05" top left; SLOT on a bank voice (B1..B32,
 * 1.4.1): its name top right. Carriers THEME with INK
 * numerals, modulators RAISE with THEME ones; an operator at output level 0: a DIM numeral (a carrier on RAISE).
 * Routes THEME, the feedback loop MID (DIM at feedback 0), the output bus MID. The knob just turned in ACCENT:
 * ALG and PTCH the label, FB the loop, MLVL the routes, MRAT MEG VMOD the modulators, DTUN the carriers.
 * sel < 6 (the OPERATOR pages, ui_fm6op.c): that operator's box ACCENT with an INK numeral, its frequency top right */
static void graph_fm6(const track_t *t, uint16_t c, uint32_t sel)
{
    uint32_t alg = fm6_alg_of(t), k, j, car = fm6_car_ops(alg), top = 0, hi = 0, fbop = fm6_fb_op(alg), hotset = 0;
    uint32_t hot = ui.hot_t ? page_id(cur_page(), ui.hot_col) : 0u;
    const uint8_t *pt = fm6_patch[(t - trk) % NTRK];
    int32_t x[6], y[6], bus, cl = 240, cr = 0, fb = clamp(pt[FP_FB] + t->p[P_E1], 0, 7);
    uint8_t m[6], fan[6] = {0, 0, 0, 0, 0, 0};
    uint16_t rc = hot == P_E2 ? T_ACCENT : c;
    char b[7] = {'A', 'L', 'G', ' ', (char)('0' + (alg + 1u) / 10u), (char)('0' + (alg + 1u) % 10u), 0};
    if (hot == P_E3 || hot == P_E4 || hot == P_E5)
        hotset = 63u & ~car;
    else if (hot == P_E6)
        hotset = car;
    fm6_routes(alg, m);
    for (k = 0; k < 6u; k++) {
        uint32_t col = FM6_CELL[alg][k] & 7u, row = FM6_CELL[alg][k] >> 4;
        hi = col > hi ? col : hi;
        top = row > top ? row : top;
        for (j = 0; j < 6u; j++)
            fan[j] += (uint8_t)(m[k] >> j & 1u);
    }
    bus = 50 + (27 + 23 * (int32_t)top) / 2;         /* the chart centred on the 100 px scale */
    for (k = 0; k < 6u; k++) {
        x[k] = 120 + (int32_t)(FM6_CELL[alg][k] & 7u) * 24 - (int32_t)hi * 12;
        y[k] = bus - 6 - FM_BH - 23 * (int32_t)(FM6_CELL[alg][k] >> 4);
        if (car >> k & 1u) { cl = x[k] < cl ? x[k] : cl; cr = x[k] > cr ? x[k] : cr; }
    }
    for (k = 0; k < 6u; k++) {                       /* into k: its sources down to its jog row, across, down */
        for (j = 0; j < 6u; j++)
            if (m[k] >> j & 1u) {
                FM6_CHART_HOOK(FMH_ROUTE, j, k);
                fm_route(x[j], y[j] + FM_BH, x[k], y[k] - 3, y[k], rc);
                if (fan[j] > 1u) fm_dot(x[j], y[k] - 3, rc);          /* one operator modulating several */
                if (m[k] & (m[k] - 1u)) fm_dot(x[k], y[k] - 3, rc);   /* several modulating one */
            }
        if (car >> k & 1u) {
            FM6_CHART_HOOK(FMH_CAR, k, 0);
            cv_rect(x[k], y[k] + FM_BH, 1, bus - y[k] - FM_BH, T_MID);
            if (x[k] != cl) fm_dot(x[k], bus, T_MID);
        }
    }
    FM6_CHART_HOOK(FMH_BUS, 0, 0);
    fm_bus(cl, cr, bus);
    FM6_CHART_HOOK(FMH_FB, fbop, 0);
    fm_loop(x[fbop], y[fbop], hot == P_E1 ? T_ACCENT : fb ? T_MID : T_DIM);
    for (k = 0; k < 6u; k++) {
        uint32_t on = pt[(5u - k) * FP_OP + FP_OL] != 0, fill_c = (car >> k & 1u) && on;
        uint16_t f = hotset >> k & 1u ? T_ACCENT : c, fill = k == sel ? T_ACCENT : fill_c ? f : T_RAISE;
        char n[2] = {(char)('1' + k), 0};
        FM6_CHART_HOOK(FMH_BOX, k, 0);
        cv_rrect(x[k] - 10, y[k], 21, FM_BH, 3, fill, T_SURF);
        GFX_HOOK_ALIGN(x[k] - 10, y[k], x[k] + 11, y[k] + FM_BH, AL_HV, "FM6 operator number in its box");
        cv_text_in(x[k] - 10, y[k] + CAP_IN(S, FM_BH), 21, &AF_S, n, fill_c || k == sel ? T_INK : on ? f : T_DIM, fill);
    }
    FM6_CHART_HOOK(FMH_LABEL, 0, 0);
    cv_text(10, 4 - GOY, &AF_S, b, hot == P_E0 || hot == P_E7 ? T_ACCENT : T_MID);
    if (sel < 6u) {                                  /* the operator picked: "OP3 x1.50" */
        char fq[16] = {'O', 'P', (char)('1' + sel), ' ', 0};
        fop_freq(pt + (5u - sel) * FP_OP, fq + 4);
        cv_text_r(228, 4 - GOY, &AF_S, fq, T_TEXT, T_SURF);
    } else if (fm6_bank_slot(fm6_slot[(t - trk) % NTRK]) >= 0) {   /* SLOT B1..B32: the bank voice's name */
        char nm[11];
        for (k = 0; k < 10u; k++)
            nm[k] = (char)pt[FP_NAME + k];
        for (nm[10] = 0; k && nm[k - 1] == ' '; k--)
            nm[k - 1] = 0;
        cv_text_r(228, 4 - GOY, &AF_S, nm, T_TEXT, T_SURF);
    }
    FM6_CHART_HOOK(FMH_END, alg, 0);
}

/* OP ENV (ui_fm6op.c): the picked operator's envelope as the patch has it: from L4 to L1 at R1, L2 at R2, L3 at R3, held
 * there while the key is down (a RAISE mark where it is let go), back to L4 at R4. A stage's width grows as its rate
 * falls (4..48 px: 99 is a step, 0 the longest), the levels 0..99 linearly (dB). The curve THEME, the stage KNOB 3 / 4
 * edit (STAGE) ACCENT with a dot at its target; "OP3" top left, the stage's rate and level top right */
static void graph_fm6env(const track_t *t, uint16_t c)
{
    static const char *const STG[4] = {"ATK", "DEC", "SUS", "REL"};
    const uint8_t *op = fop_bytes((uint32_t)(t - trk));
    int32_t top = 24, bot = 88, x[6], y[6], k;
    uint32_t sg = fop_stg & 3u;
    char b[24] = {'O', 'P', (char)('1' + fop_op % 6u), 0};
#define FEGY(l) (bot - (int32_t)(l) * (bot - top) / 99)
#define FEGW(r) (4 + (99 - (int32_t)(r)) * (99 - (int32_t)(r)) * 44 / 9801)
    x[0] = 12;
    y[0] = FEGY(op[FP_L1 + 3]);
    for (k = 0; k < 3; k++) {
        x[k + 1] = x[k] + FEGW(op[FP_R1 + k]);
        y[k + 1] = FEGY(op[FP_L1 + k]);
    }
    x[5] = 228;
    x[4] = x[5] - FEGW(op[FP_R1 + 3]);
    y[4] = y[3];
    y[5] = y[0];
    cv_rect(PANEL_X0, bot + 2, PANEL_W, 1, T_RAISE);
    cv_rect(x[4], top, 1, bot - top + 1, T_RAISE);   /* the key let go */
    for (k = 0; k < 5; k++)                          /* (3: the hold, no stage of its own) */
        cv_line_t(x[k], y[k], x[k + 1], y[k + 1], k != 3 && (uint32_t)(k < 3 ? k : 3) == sg ? T_ACCENT : c, 2);
    k = sg < 3u ? (int32_t)sg + 1 : 5;
    fm_dot(x[k], y[k], T_ACCENT);
#undef FEGY
#undef FEGW
    cv_text(10, 4 - GOY, &AF_S, b, T_MID);
    str_cpy(b, STG[sg], sizeof b);
    str_cpy(b + str_len(b), "  R ", 5);
    fmt_int(b + str_len(b), op[FP_R1 + sg]);
    str_cpy(b + str_len(b), "  L ", 5);
    fmt_int(b + str_len(b), op[FP_L1 + sg]);
    cv_text_r(228, 4 - GOY, &AF_S, b, T_TEXT, T_SURF);
}

#if FELUCCA_FM4
/* DIGITAL's eight algorithms as FM charts, read from src/eng_digital.c's switch (alg) (ui_test.c checks
 * these tables against it). FM_CELL: per operator 1..4 its grid cell, the column in 24 px steps (bits 0..2)
 * and the row above the output bus (bits 4..5; row 0 = the carriers). FM_MOD: per destination operator d a
 * nibble at bit 4 d of the operators modulating it (bit k = operator k + 1). Operator 4 feeds itself (FB). */
static const uint8_t FM_CELL[8][4] = {
    {0x00, 0x10, 0x20, 0x30}, {0x01, 0x11, 0x20, 0x22}, {0x01, 0x10, 0x20, 0x12}, {0x01, 0x12, 0x10, 0x20},
    {0x00, 0x10, 0x02, 0x12}, {0x00, 0x02, 0x04, 0x12}, {0x00, 0x02, 0x04, 0x14}, {0x00, 0x02, 0x04, 0x06}};
static const uint16_t FM_MOD[8] = {0x0842, 0x00C2, 0x004A, 0x0806, 0x0802, 0x0888, 0x0800, 0x0000};
/* Modulators above what they modulate, the carriers on the bottom row over the output bus (an arrow out).
 * Carriers THEME with INK numerals, modulators RAISE with THEME ones; an operator at LEVEL 0: DIM numeral.
 * Routes by IDX: DIM at 0, MID, THEME from 64; op 4's feedback loop MID (DIM at FB 0). The knob just turned
 * (an operator's ratio or level, IDX, FB): ACCENT. Junction dots where routes split or merge. */
static void graph_fm(const track_t *t, uint16_t c)
{
    uint32_t alg = (uint32_t)t->p[P_E0] & 7u, k, j, mods = FM_MOD[alg], car = 0, hop = 9;
    uint32_t hot = ui.hot_t ? page_id(cur_page(), ui.hot_col) : 0u;
    int32_t x[4], y[4], lo = 7, hi = 0, top = 0, bus, cl = 240, cr = 0, idx = t->p[P_E4];
    uint16_t ec = hot == P_E4 ? T_ACCENT : !idx ? T_DIM : idx < 64 ? T_MID : c;
    if (hot >= P_E1 && hot <= P_E3) hop = hot - P_E1 + 1u;
    else if (hot >= P_FM1_ATK && hot < P_E0) hop = (hot - P_FM1_ATK) / 5u;
    for (k = 0; k < 4u; k++) {
        int32_t col = FM_CELL[alg][k] & 7, row = FM_CELL[alg][k] >> 4;
        lo = col < lo ? col : lo; hi = col > hi ? col : hi; top = row > top ? row : top;
    }
    bus = 50 + (27 + 23 * top) / 2;                  /* the chart centred on the 100 px scale */
    for (k = 0; k < 4u; k++) {
        x[k] = 120 + (FM_CELL[alg][k] & 7) * 24 - (lo + hi) * 12;
        y[k] = bus - 6 - FM_BH - 23 * (FM_CELL[alg][k] >> 4);
        if (FM_CELL[alg][k] < 0x10) { car |= 1u << k; cl = x[k] < cl ? x[k] : cl; cr = x[k] > cr ? x[k] : cr; }
    }
    for (k = 0; k < 4u; k++) {                       /* into k: sources down to its jog row, across, down */
        uint32_t m = (mods >> (4u * k)) & 15u;
        for (j = 0; j < 4u; j++) if (m >> j & 1u) {
            uint32_t fan = mods & (0x1111u << j);
            fm_route(x[j], y[j] + FM_BH, x[k], y[k] - 3, y[k], ec);
            if (fan & (fan - 1u)) fm_dot(x[j], y[k] - 3, ec);   /* one operator modulating several */
        }
        if (m & (m - 1u)) fm_dot(x[k], y[k] - 3, ec);           /* several modulating one */
        if (car >> k & 1u) {
            cv_rect(x[k], y[k] + FM_BH, 1, bus - y[k] - FM_BH, T_MID);
            if (x[k] != cl) fm_dot(x[k], bus, T_MID);
        }
    }
    fm_bus(cl, cr, bus);                             /* the output bus and its arrow */
    fm_loop(x[3], y[3], hot == P_E6 ? T_ACCENT : t->p[P_E6] ? T_MID : T_DIM);   /* op 4's feedback */
    for (k = 0; k < 4u; k++) {
        char b[2] = {(char)('1' + k), 0};
        uint32_t on = t->p[P_FM1_LEVEL + k * 5u] != 0, fill_c = (car >> k & 1u) && on;
        uint16_t f = k == hop ? T_ACCENT : c, fill = fill_c ? f : T_RAISE;
        cv_rrect(x[k] - 10, y[k], 21, FM_BH, 3, fill, T_SURF);
        GFX_HOOK_ALIGN(x[k] - 10, y[k], x[k] + 11, y[k] + FM_BH, AL_HV, "DIGITAL operator number in its box");
        cv_text_in(x[k] - 10, y[k] + CAP_IN(S, FM_BH), 21, &AF_S, b, fill_c ? T_INK : on ? f : T_DIM, fill);
    }
}
#endif
/* Save-bank validity is expensive (CRC/import). Refresh it once per redraw or
 * when a slot changed (project_slots_key), rather than parsing four projects on every UI frame. */
static char graph_pname[4][13];                       /* .. and the slots' names ("" none) */
static uint16_t graph_plen[4][NTRK];                  /* .. each track's pattern length (project_info: 1/48 quarters) */
static uint32_t graph_pden[4][NTRK];                  /* .. and its picture for SONG (project.c proj_density: 2 bits a cell) */
static uint8_t graph_pcell[4][NTRK];                  /* .. in this many cells (min(LEN, 16)) */
static uint32_t graph_pname_sig;
static uint32_t project_slots_key(void);           /* (project.c) */
static int graph_project_used(uint32_t slot)
{
    static uint32_t key, frame;
    static uint8_t mask, ready;
    uint32_t k = project_slots_key();                  /* (HA HA HE HU: read again when a slot changed, not twice a second:
                                                        * 1.8 ms each time on SONG) */
    if (!ready || k != key || (ui.force && frame != ui.frame)) {
        uint32_t i; mask = 0;
        for (i = 0; i < 4u; i++)
            mask |= (uint8_t)((project_info(i, graph_pname[i], graph_plen[i], graph_pden[i], graph_pcell[i]) != 0) << i);
        graph_pname_sig = fnv(fnv(2166136261u, graph_pname, sizeof graph_pname), graph_plen, sizeof graph_plen);
        graph_pname_sig = fnv(fnv(graph_pname_sig, graph_pden, sizeof graph_pden), graph_pcell, sizeof graph_pcell);
        ready = 1; key = k; frame = ui.frame;
    }
    return (mask >> (slot & 3u)) & 1u;
}
static const char *graph_project_name(uint32_t slot)  /* (after graph_project_used) */
{
    return graph_pname[slot & 3u];
}

/* SONG's timeline (graph_song): the sections left to right, each as wide as it plays: REPS x its clock track's pattern
 * (song_chain.c chain_clock: the first track not silent there; project_info: that track's LEN x DIV in its slot, what
 * chain_tick counts; a slot not saved, or every track silent: one bar). A short song is stretched over the strip
 * (fitted); a section under SG_MINW gets SG_MINW and the rest is shared out again; when the sections would get under
 * SG_FITBAR px a bar the song scrolls at SG_BAR px a bar, the view following the cursor's section and, playing, the
 * playing one. + ADD's slot (count < CHAIN_ROWS) at the end. Content px: 0 .. the width. Four lanes, T1 at the top */
#define SG_X0 24                                       /* the strip: x 24 .. 234 (the lanes' cushions left of it) */
#define SG_W 210
#define SG_RH 14                                       /* (Dah) the ruler on top, as HOME's: the whole song, the bars */
#define SG_Y (SG_RH + 2)                               /* the lanes: under it, to the panel's foot */
static int32_t sg_lh = 11;                             /* (Dah) a lane's height: the panel shared by the eight (graph_song) */
#define SG_LH sg_lh                                    /* (its number, the pattern's picture), SG_LG between two */
#define SG_LG 2                                        /* (Dah: airier) */
#define SG_H (NTRK * (SG_LH + SG_LG) - SG_LG)         /* (HA HA HE HU: the eight lanes at once, thin) */
#define SG_LY(k) (SG_Y + (int32_t)(k) * (SG_LH + SG_LG))   /* lane k's top */
#define SG_GAP 2                                       /* between two blocks (in a block's width) */
#define SG_MINW 16                                     /* a block's least width, its gap included: a letter in it */
#define SG_ADDW 20                                     /* + ADD's slot */
#define SG_BAR 6                                       /* scrolled: px a bar (Dah: a song too long to fit at it scrolls at
                                                         * it, a constant zoom, the playhead in the middle) */
#define SG_FITBAR SG_BAR                               /* fitted: at least this many px a bar (Dah: never smaller than
                                                         * scrolled: no song squeezed) */
#define SG_BARU 192u                                   /* a 4/4 bar in 1/48 quarters */
#define SG_RY 0                                        /* (Dah) the ruler's top: over the lanes */
typedef struct {
    uint32_t n, add, fit;                              /* the rows, + ADD shown, fitted (no scrolling) */
    uint32_t u0[CHAIN_ROWS + 1u];                      /* each row's start in 1/48 quarters; u0[n]: the song's length */
    int32_t x[CHAIN_ROWS + 2u];                        /* each row's left edge (content px); x[n]: + ADD's; x[n + 1]: the end */
} song_lay_t;
static struct { int32_t off; uint32_t frame; uint8_t init, cur, play, last; } sgv;   /* the view (scrolled) */

/* a * b / c without a 64-bit divide: a and c halved alike until a * b fits */
static uint32_t sg_muldiv(uint32_t a, uint32_t b, uint32_t c)
{
    while (b && a > 0xFFFFFFFFu / b) { a >>= 1; c >>= 1; }
    return c ? a * b / c : 0u;
}
/* HA HA HE HU: track k's pattern s as SONG draws it (pstore.c ps_info): its length (1/48 quarters), its picture, its
 * cells; 0 = none there. Read again when a pattern changed (the heap, a track's steps or timing) */
static struct { uint32_t gen; uint8_t ok[NTRK][33], nc[NTRK][33]; uint16_t len[NTRK][33]; uint32_t den[NTRK][33]; } sgc
    __attribute__((section(".pool")));
static uint32_t sg_gen(void)
{
    uint32_t k, h = ps_gen * 2654435761u + trk_count();
    for (k = 0; k < trk_count(); k++)
        h = fnv(h ^ steps_sig(&trk[k]), chain.running ? chain.timing[k] : &trk[k].p[P_SLEN], 4u * sizeof(int16_t)) +
            ps_num(k);                                  /* (Dah: a song playing sets the tracks' timing section by
                                                         * section: their own, not that, or every picture read again) */
    return h;
}
static int sg_pat(uint32_t k, uint32_t s, uint32_t *len, uint32_t *den, uint32_t *nc)
{
    uint32_t g = sg_gen();
    uint8_t c;
    k %= NTRK;
    *len = *den = *nc = 0;
    if (!s || s > 32u || !ps_exists(k, s))
        return 0;
    if (g != sgc.gen) {
        memset(sgc.ok, 0, sizeof sgc.ok);
        sgc.gen = g;
    }
    if (!sgc.ok[k][s]) {
        sgc.len[k][s] = (uint16_t)ps_info(k, s, &sgc.den[k][s], &c);
        sgc.nc[k][s] = c;
        sgc.ok[k][s] = 1;
    }
    *len = sgc.len[k][s];
    *den = sgc.den[k][s];
    *nc = sgc.nc[k][s];
    return 1;
}
static uint32_t song_unit(const chain_row_t *r)        /* one pass of a section: its clock track's (1/48 quarters) */
{
    uint32_t k = chain_clock(r), len, den, nc;
    return sg_pat(k, r->slot[k], &len, &den, &nc) && len ? len : SG_BARU;
}
static void song_layout(song_lay_t *L)
{
    uint32_t i, n = chain_config.count <= CHAIN_ROWS ? chain_config.count : CHAIN_ROWS, forced = 0, nf = 0, uf = 0, chg = 1;
    uint32_t wb;
    L->n = n;
    L->add = n < CHAIN_ROWS;
    L->u0[0] = 0;
    for (i = 0; i < n; i++)
        L->u0[i + 1u] = L->u0[i] + CHAIN_REP(chain_config.row[i]) * song_unit(&chain_config.row[i]);
    wb = SG_W - (L->add ? SG_ADDW : 0u);
    while (chg) {                                      /* fitted: shared by length, SG_MINW the least */
        chg = 0; nf = 0; uf = 0;
        for (i = 0; i < n; i++)
            if ((forced >> i) & 1u) nf++;
            else uf += L->u0[i + 1u] - L->u0[i];
        if (nf * SG_MINW > wb)
            break;
        for (i = 0; i < n; i++)
            if (!((forced >> i) & 1u) && sg_muldiv(L->u0[i + 1u] - L->u0[i], wb - nf * SG_MINW, uf) < SG_MINW) {
                forced |= 1u << i;
                chg = 1;
            }
    }
    L->fit = 0;                                        /* (Dah) never fitted: the song at its zoom, scrolling */
    (void)forced;
    if (L->fit) {
        uint32_t nb = 0, ub = 0;
        for (i = 0; i <= n; i++) {
            L->x[i] = (int32_t)(nb * SG_MINW + (uf ? sg_muldiv(ub, wb - nf * SG_MINW, uf) : 0u));
            if (i < n && ((forced >> i) & 1u)) nb++;
            else if (i < n) ub += L->u0[i + 1u] - L->u0[i];
        }
    } else {
        L->x[0] = 0;
        for (i = 0; i < n; i++) {
            uint32_t w = ((L->u0[i + 1u] - L->u0[i]) * sg_zoom() + SG_BARU / 2u) / SG_BARU;   /* (Dah: VIEW's ZOOM) */
            L->x[i + 1u] = L->x[i] + (int32_t)(w > SG_MINW ? w : SG_MINW);
        }
    }
    L->x[n + 1u] = L->x[n] + (L->add ? SG_ADDW : 0);
}
static int32_t song_ux(const song_lay_t *L, uint32_t u)   /* 1/48 quarters -> content px (the blocks' scale) */
{
    uint32_t i = 0;
    while (i + 1u < L->n && u >= L->u0[i + 1u])
        i++;
    if (!L->n)
        return 0;
    return L->x[i] + (int32_t)sg_muldiv(u - L->u0[i], (uint32_t)(L->x[i + 1u] - L->x[i]), L->u0[i + 1u] - L->u0[i]);
}
static uint32_t song_cur(void) { return ui.song_row < chain_config.count ? ui.song_row : chain_config.count; }
/* the playhead, content px: the playing section's repeats played and its clock track's step in its pattern (-1: none) */
static int32_t song_playx(const song_lay_t *L)
{
    uint32_t r = chain.row, len, idx, reps;
    const track_t *t = &trk[chain.clk % NTRK];
    if (!chain.running || r >= L->n)
        return -1;
    len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    idx = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx % len;
    reps = CHAIN_REP(chain.config.row[r]) ? CHAIN_REP(chain.config.row[r]) : 1u;
    return L->x[r] + (int32_t)sg_muldiv((reps - (chain.remaining <= reps ? chain.remaining : reps)) * len + idx,
                                        (uint32_t)(L->x[r + 1u] - L->x[r] - SG_GAP), reps * len);
}
static int32_t song_playu(const song_lay_t *L)   /* (Dah) the playhead in 1/48 quarters (-1: none) */
{
    uint32_t r = chain.row, len, idx, reps;
    const track_t *t = &trk[chain.clk % NTRK];
    if (!chain.running || r >= L->n)
        return -1;
    len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    idx = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_idx % len;
    reps = CHAIN_REP(chain.config.row[r]) ? CHAIN_REP(chain.config.row[r]) : 1u;
    return (int32_t)(L->u0[r] + sg_muldiv((reps - (chain.remaining <= reps ? chain.remaining : reps)) * len + idx,
                                          L->u0[r + 1u] - L->u0[r], reps * len));
}
/* once a frame: the view toward the cursor's block and the playing one (both when they fit, else the one that moved
 * last), a block's margin round them; halving the way with MENU > ANIM ON, at once on a redraw forced, a gap in
 * the frames or ANIM OFF. Returns the view's left edge */
static int32_t song_follow(const song_lay_t *L)
{
    uint32_t c = song_cur(), p = chain.running && chain.row < L->n ? chain.row : 0xFFu;
    int32_t a = L->x[c], b = c < L->n ? L->x[c + 1u] - SG_GAP : L->x[c] + SG_ADDW, tgt = sgv.off, d, m, end;
    int fresh = !sgv.init || ui.frame != sgv.frame + 1u, snap = fresh || ui.force || (ui_prefs & PREF_ANIM_OFF);
    if (sgv.init && sgv.frame == ui.frame)
        return sgv.off;
    if (chain.running && !sgw.run) {                  /* (Dah) the song starts: FOLLOW on, the grid followed at once */
        sgw.nofollow = 0;
        sgw.touch = ui.frame - 1000u;
    }
    sgw.run = chain.running;
    if (chain_from_row < L->n && !chain_seek_req)      /* (Dah) the GRID: where PLAY starts (the song playing on
                                                         * does not move it; turned, it does) */
        sgw.gu = L->u0[chain_from_row] + chain_from_u;
    if (fresh) {                                     /* the page (re)entered: the playing row first */
        sgv.off = 0;
        sgv.last = p != 0xFFu;
    } else if (p != sgv.play) {
        sgv.last = 1;
    } else if (c != sgv.cur) {
        sgv.last = 0;
    }
    if (p != 0xFFu) {
        int32_t pa = L->x[p], pb = L->x[p + 1u] - SG_GAP;
        if ((pb > b ? pb : b) - (pa < a ? pa : a) <= SG_W) {
            a = pa < a ? pa : a;
            b = pb > b ? pb : b;
        } else if (sgv.last) {
            a = pa;
            b = pb;
        }
    }
    m = (SG_W - (b - a)) / 2;
    m = m > SG_MINW ? SG_MINW : m < 0 ? 0 : m;
    if (a - m < tgt) tgt = a - m;
    if (b + m > tgt + SG_W) tgt = b - a + 2 * m > SG_W ? a - m : b + m - SG_W;   /* (Dah: wider than the view: its start) */
    end = L->x[L->n + 1u] - SG_W;
    if (sgw.nofollow) {                               /* (Dah) VIEW: FOLLOW off: where MOVE put it */
        tgt = sgw.at;
        snap = 1;
    } else if (p != 0xFFu && song_playx(L) >= 0 && ui.frame - sgw.touch > 90u) {   /* (Dah) playing: the playhead stays
                                                         * in the middle once there, the song going by under it; a
                                                         * section picked (KNOB 1, 2): its block for 3 s, then this */
        tgt = song_playx(L) - SG_W / 2;
        snap = 1;
    }
    tgt = L->fit || end < 0 ? 0 : tgt < 0 ? 0 : tgt > end ? end : tgt;
    d = tgt - sgv.off;
    sgv.off = snap ? tgt : sgv.off + (d / 3 ? d / 3 : d > 0 ? 1 : d < 0 ? -1 : 0);
    sgv.cur = (uint8_t)c;
    sgv.play = (uint8_t)p;
    sgv.frame = ui.frame;
    sgv.init = 1;
    return sgv.off;
}

/* MENU > LARGE's strip under the tall cards (ui.c LK_TALL): HOME's scope, ENV's ADSR, LFO's wave,
 * MIXER's tracks drawn small; on the other pages the page's title and number (their charts need the whole panel) */
enum { SK_NONE, SK_SCOPE, SK_ADSR, SK_LFO, SK_TRK, SK_TITLE };
static uint32_t strip_kind(void)
{
    uint32_t g;
    if (large_kind() != LK_TALL)
        return SK_NONE;
    if (ui.home)
        return SK_SCOPE;
    g = cur_page()->graph;
    return g == GR_ADSR ? SK_ADSR : g == GR_LFO ? SK_LFO : g == GR_TRK ? SK_TRK : SK_TITLE;
}
/* the page's title and its number in its family ("ENV DEST 2/2", EDIT: the engine's "OSC 1/2"); the piano roll: its
 * scale ("C MIN"); ti holds 20 (the footer, the strip) */
static void page_title(char *ti)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i, n = 0, k = 0;
    const char *pt = pg->scope == SC_ENGINE ? e->page_title[pg->id[0] != P_E0] : 0;   /* EDIT: the engine's */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == pg->fam && page_visible(i)) {
            n++;
            if (i == ui.page)
                k = n;
        }
    str_cpy(ti, pt ? pt : grid_on() && !detail_on() ? "GRID" : pg->title, 12);
    if (n > 1) {
        str_cpy(ti + str_len(ti), " ", 4);
        fmt_int(ti + str_len(ti), (int32_t)k);
        str_cpy(ti + str_len(ti), "/", 4);
        fmt_int(ti + str_len(ti), (int32_t)n);
    }
    if (pg->graph == GR_ROLL && !drum_track(t) && !detail_on()) {   /* the piano roll: its tinted rows' scale ("C MIN") */
        str_cpy(ti, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u], 4);
        str_cpy(ti + str_len(ti), " ", 4);
        str_cpy(ti + str_len(ti), N_SCALE[clamp(t->p[P_SCALE], 0, (int32_t)(sizeof N_SCALE / sizeof N_SCALE[0]) - 1)], 8);
    }
}
/* the strip's title: L centred (M when L lacks a glyph or is too wide) */
static void graph_title(void)
{
    char ti[20];
    const aafont_t *f = &AF_L;
    int32_t y;
    page_title(ti);
    if (!large_face_has(ti) || text_w(f, ti) > 220)
        f = &AF_M;
    y = f == &AF_L ? CAP_IN(L, LG_H_GRAPH) : CAP_IN(M, LG_H_GRAPH);
    GFX_HOOK_ALIGN(0, 0, 240, LG_H_GRAPH, AL_HV, "large strip title centred");
    cv_text_in(0, y, 240, f, ti, T_THEME, T_SURF);
}

static uint32_t au_sig(void);
static uint32_t graph_signature(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint32_t h = 2166136261u + graph_h() * 7u, i;   /* (Dah: the panel's height too) */
    if (tl_on() || pl_on())
        return h ^ ui.frame * 2654435761u;           /* (Dah) the tracks', the sounds' list gliding */
    if (ui.home)
        return h ^ ui.frame / 2u;                    /* scope: every other frame */
    h ^= (uint32_t)pg->graph * 131u + TSEL->eng_req + song.sel * 7777u + ui.page * 1291u;
    h ^= ka.st * 0x9E37u + ka.src * 131u + ka.dst * 7919u + (uint32_t)ka_blink() * 3u;   /* (Dah: ASSIGN's panel) */
    h ^= (uint32_t)tl_dy * 40503u + (uint32_t)pp_dy * 2246822519u + (uint32_t)lu_dy * 3266489917u + (uint32_t)lf_dy * 668265263u + (uint32_t)ch_dy * 374761393u;   /* (Dah: a list gliding: each step drawn) */
    if (strip_kind() == SK_TITLE)                    /* MENU > LARGE: the page's title (its engine, its number) */
        return h ^ 0x5A17u;
    if (pg->graph == GR_NONE || pg->graph == GR_ARP || pg->fam == FAM_FX || pg->graph == GR_SCALE || pg->graph == GR_CHORD ||
        ((pg->graph == GR_LFO || pg->graph == GR_LDRAW) && lfo_live(TSEL))) h ^= ui.frame / 2u;   /* (Dah: the LFO's
                                                         * bar moves while a note sounds) */
    if (pg->graph == GR_LFO || pg->graph == GR_LDRAW)
        h ^= (uint32_t)lfo_live(TSEL) * 0x2F1Du;
    if (pg->graph == GR_ROLL && (song.playing || prv.on))   /* (Dah: the playhead gliding; the VIEW) */
        h ^= ui.frame * 2654435761u + prv.zoom * 7u + (uint32_t)prv.at * 131u + prv.nofollow * 3u;   /* (Dah: FX's: live) */
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h ^= (uint32_t)TSEL->preset * 7u + (uint32_t)song.g[G_SLOT] * 13u + TSEL->user * 257u + up_gen * 7919u + ui.uslot * 104729u;
    if (pg->graph == GR_CHORD) {                     /* the last chord played */
        h = (h ^ (chord_last[song.sel].root + 131u * chord_last[song.sel].mask)) * 16777619u;
        for (i = 0; i < CHORD_MAX; i++)
            h = (h ^ chord_last[song.sel].note[i]) * 16777619u;
        h ^= (uint32_t)t->engine * 389u;             /* (MONO and kits follow the sounding engine) */
    }
    if (pg->graph == GR_SONG) {
        h ^= ui.song_row * 40503u + chain_config.count * 7919u + ui.song_trk * 3571u;
        h ^= sg_pend ? (sg_pend * 131u + sg_prow * 7u + ((ui.frame / 6u) & 1u)) * 2246822519u : 0u;   /* (Dah: blinking) */
        h = fnv(h, chain_config.row, sizeof chain_config.row);
        h ^= chain.running ? (chain.row + 1u) * 104729u + chain.remaining * 1299709u : 0u;
        h ^= sg_gen();                               /* (HA HA HE HU: the patterns) */
        {                                            /* the timeline: its view (following, ui_graph.c song_follow), the playhead */
            song_lay_t L;
            song_layout(&L);
            h = (h ^ (uint32_t)song_follow(&L)) * 16777619u;
            h = (h ^ (uint32_t)(song_playx(&L) + 1)) * 16777619u;
            h = (h ^ sgw.gu) * 16777619u;              /* (Dah: the GRID) */
            h ^= (ui_prefs & PREF_LARGE) ? 0x2545F491u : 0u;
        }
    }
    if (pg->graph == GR_MOD)
        h ^= (mod_ui_slot + 1u) * 40503u;
    if (pg->graph == GR_SLCR && t->p[P_SLCR])        /* the SLICER's step playing */
        h ^= (sl[song.sel].idx + 1u) * 2654435761u;
    if (pg->graph == GR_PATTERN)                     /* (HA HA HE HU) the track's patterns, the one picked, the memory */
        h ^= (ps_used + 1u) * 2654435761u + (pt_sel + 1u) * 7919u + (ps_num(song.sel) + 1u) * 104729u + steps_sig(t) * 31u;
    if (pg->graph == GR_FILE)                        /* (HA HA HE HU) the projects (graph_arch_*), the one picked */
        h ^= graph_arch_sig() * 31u + (ui.fsel + 1u) * 7919u + pfs_cur() * 104729u + pfs_mem_pct() * 613u;
    if (pg->graph == GR_SLOTS) {                     /* (a checksum over each slot) */
        for (i = 0; i < 4u; i++)
            h ^= (uint32_t)graph_project_used(i) << (20u + i);
        h += graph_pname_sig + project_current() * 7919u;   /* (HA HA HE HU: the one OPEN) */
    }
    if (pg->graph == GR_EVENTS)                      /* AUTOMATION: the lanes, the view, the cursor */
        h = (h ^ au_sig()) * 16777619u;
#if FELUCCA_SLICE
    if (pg->graph == GR_SLICES && slice_page_ok()) h ^= slice_sig();
#endif
    if (pg->scope == SC_ENGINE && (ENGINES[t->eng_req % NENGINES] == &ENG_WHEEL || t->eng_req % NENGINES == ENGI_FM6 ||
                                   (FELUCCA_FM4 && t->eng_req % NENGINES == ENGI_DIGITAL)))
        h ^= (ui.hot_t ? ui.hot_col + 1u : 0u) * 65537u;
    if ((pg->scope == SC_ENGINE || fop_page(pg->graph)) && t->eng_req % NENGINES == ENGI_FM6)   /* the patch (PAT's algorithm,
                                                     * levels, FB; the operator pages: the operator, the stage) */
        h ^= (fm6_pgen[(t - trk) % NTRK] + 1u) * 2246822519u + (fop_op + 1u) * 40503u + (fop_stg + 1u) * 7919u;
    if (xp_page(pg->graph))                          /* (HA HA HE HU) LFO DRAW: the track's points, the one picked */
        h ^= fnv(2166136261u, &trkx[(t - trk) % NTRK], sizeof(trkx_t)) * 31u + (xp_point + 1u) * 7919u + (xp_shape + 1u) * 131u +
             (uint32_t)t->p[P_LWAVE] * 104729u;
    if (dln_page(pg->graph))                         /* DESIGN: the lanes' values, the lane picked */
        h ^= (fm6_pgen[(t - trk) % NTRK] + 1u) * 2246822519u + (ui.lane + 1u) * 40503u + (uint32_t)t->eng_req * 389u;
    if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE) h ^= sample_wave.pos * 13u + sample_wave.key;
    if (pg->graph == GR_ROLL) {
        uint32_t ph = song.playing ? t->seq_idx : 0xFFFFu;
        if (ph / 16u != ui.bank)
            ph = 0xFFFFu;                            /* the roll shows the cursor's bank only */
        h ^= steps_hash(t) + ph * 31u + ui.cursor * 7919u + ui.lane * 104723u + ui.bank * 613u +
             drum_mute[trk_index(t) % NPART] * 2654435761u;
        {                                            /* the parameter locks' marks (the roll, the grid) */
            uint64_t lk = motion_lock_steps(trk_index(t));
            h = (h ^ (uint32_t)lk ^ (uint32_t)(lk >> 32) * 2654435761u) * 16777619u;
        }
        if (detail_on())                             /* DETAIL: QUANTIZE (NUDGE's DIM), the lane named in the accent */
            h = (h ^ (0xD1u + (uint32_t)t->p[P_SQNT] * 31u + (ui.hot_t ? ui.hot_col + 1u : 0u) * 977u)) * 2654435761u;
        else if (!drum_track(t)) {                   /* the roll: its view and the keys held */
            pr_follow(t);
            h = (h ^ (proll.lo + 1u)) * 16777619u;
            h = (h ^ pr_held()) * 16777619u;
        }
    }
    return h;
}
/* 4-letter engine tags of the preset list */
static const char *eng_abbr(const char *name)
{
    static const char *const A[][2] = {{"ANALOG", "ANLG"},
#if FELUCCA_FM4
                                       {"DIGITAL", "DGTL"},
#endif
                                       {"PHASE", "PHAS"}, {"LOFI", "LOFI"},
                                       {"SAMPLE", "SMPL"}, {"VOICE", "VOCL"}, {"TRIO", "TRIO"}, {"WHEEL", "WHEL"},
                                       {"GRAIN", "GRAN"}, {"PHYS", "PHYS"}, {"DRUM", "DRUM"},
                                       {"NOISE", "NOIS"}, {"FM6", "FM6"}, {"SLICE", "SLCE"}, {"GROWL", "GRWL"}};
    uint32_t i;
    for (i = 0; i < sizeof A / sizeof A[0]; i++)
        if (str_eq(name, A[i][0]))
            return A[i][1];
    return name;
}

/* a list row (17 px), in M as every list's (PRESETS, USER, PROJECT, AUTOMATION, MOD, the MENU): the selected
 * one a THEME bar with INK text; tag at x 14 (the widest, DRUM, to x 57), free text from x LIST_NX to x1 */
#define LIST_Y(k) (3 + 17 * (int32_t)(k))
#define LIST_TY CAP_IN(M, 16)                           /* a row's text: its capitals rows 2 .. 12 of the row */
#define LIST_NX 64
static void list_row(int32_t y, int sel, const char *tag, uint16_t tc, const char *name, uint16_t nc, int32_t x1)
{
    uint16_t bg = sel ? T_THEME : T_SURF;
    if (sel)
        cv_rrect(6, y, 228, 16, 4, T_THEME, T_SURF);
    GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
    cv_text_on(14, y + LIST_TY, &AF_M, tag, sel ? T_INK : tc, bg);
    if (name[0])
        GFX_HOOK_ALIGN(0, y, 0, y + 16, AL_V, "list row text centred up/down");
    cv_free_text(LIST_NX, y + LIST_TY, &AF_M, name, sel ? T_INK : nc, bg, x1 - LIST_NX);
}
/* (Dah) a gliding list's row: its text only (the bar stays in the middle, the row under it in INK) */
/* (Dah) a gliding list: rows' (y, tag, its colour, name) under a bar fixed in the middle: drawn plain, the bar over
 * them, then again in INK inside the bar only (the text clip) */
static int32_t glide(int32_t *dy)                       /* (Dah) a step of the glide: a quarter of the way, a px at least */
{
    if (!cv_py)                                         /* (a split panel's next canvases: the same step) */
        *dy -= *dy / 4 ? *dy / 4 : *dy > 0 ? 1 : *dy < 0 ? -1 : 0;
    return *dy;
}
/* an empty list: a title and a hint, centred */
static void note_line(int32_t y, const char *s, uint16_t fg)   /* centred S; "[K2] ADD PATTERN": a key hint */
{
    uint32_t n;
    int32_t id = kc_tag(s, &n);
    if (id < 0) {
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "panel note centred");
        cv_text_in(0, y, 240, &AF_S, s, fg, T_SURF);
        return;
    }
    s += n;
    while (*s == ' ')
        s++;
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "panel note hint centred");
    cv_text_on(cv_keycap(HALF_UP(240 - kh_ink((uint32_t)id, s)), y + 1, (uint32_t)id, T_KEY, T_INK, T_SURF) + KH_GAP, y,
               &AF_S, s, fg, T_SURF);
}
static void panel_note(const char *a, const char *b, const char *c)
{
    int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, bh = c ? 67 : b ? 45 : 19, y = (full - bh) / 2 - cv_oy;
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "panel note centred");   /* (Dah: the lines centred up and down in the panel) */
    cv_text_in(0, y, 240, &AF_M, a, T_TEXT, T_SURF);
    if (b) note_line(y + 30, b, T_MID);
    if (c) note_line(y + 52, c, T_DIM);
}
/* the same title led by an icon (16 px, the accent's: the header message's, ui_draw.c draw_head) in the middle of
 * the panel: the title's capitals centred up and down in it, the icon's ink on them; across, the pair centred by
 * its ink */
#define PA_GAP 6                                        /* icon ink -> title ink (setup_title's) */
static void panel_alert(uint32_t id, const char *a)
{
    int32_t bi[4], bt[4], x, y = CAP_IN(M, (int32_t)graph_h());
    icon_ink(16, id, bi);
    text_ink(&AF_M, a, bt);
    x = HALF_UP(240 - (bi[2] - bi[0] + PA_GAP + bt[2] - bt[0])) - bi[0];
    GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "panel alert centred");
    GFX_HOOK_ALIGN(0, y + AF_M_CAP_Y, 0, y + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS, "panel alert icon on its title's line");
    cv_icon_in(x, y + AF_M_CAP_Y, 0, AF_M_CAP_H, 16, id, T_ACCENT, T_SURF);
    GFX_HOOK_ALIGN(0, 0, 0, (int32_t)graph_h(), AL_V | AL_PASS, "panel alert title in the panel's middle");
    cv_text_on(x + bi[2] + PA_GAP - bt[0], y, &AF_M, a, T_TEXT, T_SURF);
}

/* (Dah) ALGORITHM turned: the MASTER and the project's tracks, the selected one in the middle (its colour, its
 * sound's name), gliding to it */
static void sound_name(const track_t *t, char *b);     /* ui_draw.c */
/* (Dah) ASSIGN: what it asks, the whole panel; then the four HOME knobs of the sound, the one to turn blinking */
static void graph_assign(void)
{
    char t[24];
    if (ka.st == 1u) {
        panel_note("ASSIGN A HOME KNOB", "TURN THE KNOB TO TAKE", "[OCT-] LEAVE");
        return;
    }
    if (ka.st == 2u) {
        str_cpy(t, track_desc(TSEL, ka.src)->label, 12);
        panel_note(t, "[OCT+] OK", "[OCT-] LEAVE");
        return;
    }
    {   /* its four HOME knobs: what each one turns now */
        int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, y = full / 2 - 4, k, on = ka_blink();
        str_cpy(t, ka.dst < 4u ? "OK?" : ka.st == 3u ? "ON WHICH KNOB?" : "RESET WHICH KNOB?", sizeof t);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H, "assign title centred");
        cv_text_in(0, y - 34, 240, &AF_M, t, T_TEXT, T_SURF);
        for (k = 0; k < 4; k++) {
            uint32_t a = trkx[song.sel % NTRK].home[k], id = a && a <= P_COUNT ? a - 1u : ENGINES[TSEL->eng_req % NENGINES]->knob[k];
            int32_t x = 8 + k * 57, pk = (uint32_t)k == ka.dst;
            char kn[3] = {'K', (char)('1' + k), 0};
            if (pk)                                     /* (Dah) the one picked: shown with what it will turn */
                id = ka.st == 3u ? ka.src : ENGINES[TSEL->eng_req % NENGINES]->knob[k];
            cv_rrect(x, y, 53, 40, 4, on || pk ? (pk ? T_ACCENT : T_THEME) : T_RAISE, T_SURF);
            cv_rrect(x + 2, y + 2, 49, 36, 3, T_RAISE, on || pk ? (pk ? T_ACCENT : T_THEME) : T_RAISE);
            GFX_HOOK_ALIGN(x, y + 2, x + 53, y + 18, AL_H, "assign knob name centred");
            cv_text_in(x, y + 4 - AF_S_CAP_Y + 2, 53, &AF_S, kn, T_MID, T_RAISE);
            cv_free_text(x + 4, y + 22 - AF_S_CAP_Y, &AF_S, track_desc(TSEL, id)->label, T_TEXT, T_RAISE, 45);
        }
        note_line(y + 52, ka.dst < 4u ? "[OCT+] OK" : "[OCT-] LEAVE", ka.dst < 4u ? T_TEXT : T_DIM);
    }
}
/* (Dah) SAVE's tall lists (34 px a row): the frame from the top down to the middle, there the rows go by under it,
 * at the end on down to the foot (as HOME's steps); p the place gliding (cur's, dy back). The view's top, the frame's y
 * (from the room's top, H tall) */
/* (Dah) a list's scrollbar at the right (as the full-screen selectors'): the room y0 .. y1, the content ch tall, the
 * view's top vt */
static void list_bar(int32_t y0, int32_t y1, int32_t ch, int32_t vt)
{
    int32_t th = y1 - y0, bh, by;
    if (ch <= th)
        return;
    bh = th * th / ch;
    bh = bh < 8 ? 8 : bh;
    by = y0 + (int32_t)((int64_t)vt * (th - bh) / (ch - th));
    cv_rect(232, y0, 2, th, T_LINE);
    cv_rect(231, by, 4, bh, T_MID);
}
static int32_t tall_view(int32_t n, int32_t cur, int32_t dy, int32_t h, int32_t *fy, int32_t rh)   /* (rh: a row's
                                                         * height, 2 px between two) */
{
    int32_t p = cur * (rh + 2) - dy, last = n * (rh + 2) - 2 - h, vt = p - (h - rh) / 2;
    vt = vt > last ? last : vt;
    vt = vt < 0 ? 0 : vt;
    *fy = p - vt;
    return vt;
}
static void pat_rows(int32_t dy, int add);
/* the EDIT layer (ui_layer.c): the sound loaded, as a list's selected row: "03" (its place in KNOB 2's list,
 * the engine's sounds) or "U07", the name */
static void engine_sound_row(int32_t y)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t total, cur = eng_list_pos(&total), u = user_of(TSEL);
    char tag[6], nm[13];
    if (u < UP_SLOTS) {
        up_slot_label(tag, u);
        up_name(u, nm);
    } else {
        tag[0] = (char)('0' + (cur + 1u) / 10u % 10u);
        tag[1] = (char)('0' + (cur + 1u) % 10u);
        tag[2] = 0;
        str_cpy(nm, e->npresets ? e->presets[TSEL->preset % e->npresets].name : "", sizeof nm);
    }
    list_row(y, 1, tag, T_DIM, nm, T_TEXT, 232);
    (void)total;
}
/* user preset slots around the selected one: "U07  NAME" / EMPTY */
/* (Dah) SAVE's lists as PATTERN's: tall rows (32 px, 34 apart) gliding through a frame fixed in the panel's middle
 * (y0 .. y1 the room: under a line on top, over one at the foot); the row k of n, its tag (M), name (M), a note at the
 * right (S); the one in the frame on RAISE, its tag in the theme */
#define BR_H 24                                        /* (Dah) MY PRESETS', FILE's rows: 24 px, 26 apart */
typedef struct { char tag[6]; char nm[20]; char note[12]; uint16_t tc, nc; } bigrow_t;
typedef void (*bigrow_fn)(int32_t k, bigrow_t *r);   /* (Dah: a row made when drawn: no table in RAM) */
static void big_rows(bigrow_fn fill, int32_t n, int32_t cur, int32_t dy, int32_t y0, int32_t y1)
{
    int32_t fy, vt = tall_view(n, cur, dy, y1 - y0, &fy, BR_H), k;   /* (Dah: taller than a list's, less than PATTERN's) */
    cv_rrect(4, y0 + fy - 1, 222, BR_H + 2, 5, T_THEME, T_SURF);   /* (as PATTERN's: the frame down to the middle, the
                                                         * rows by; Dah: the one in it a little bigger, as CHAIN's) */
    cv_rrect(6, y0 + fy + 1, 218, BR_H - 2, 4, T_RAISE, T_THEME);
    int16_t c0 = cv_cy0, c1 = cv_cy1;
    cv_cy0 = (int16_t)clamp(y0 + CV_OY, 0, (int32_t)cv_h);   /* (Dah) the rows cut by the room's edges: shown, fainter
                                                         * (as the selectors'), clipped to it */
    cv_cy1 = (int16_t)clamp(y1 + CV_OY, cv_cy0, (int32_t)cv_h);
    cv_scroll = 1;
    for (k = 0; k < n; k++) {
        int32_t y = y0 + k * (BR_H + 2) - vt;
        int mid = y > y0 + fy - BR_H / 2 && y < y0 + fy + BR_H / 2, whole = y >= y0 && y + BR_H <= y1;
        uint16_t bg = mid ? T_RAISE : T_SURF;
        bigrow_t rw = {{0}, {0}, {0}, 0, 0};
        if (y + BR_H <= y0 || y >= y1)
            continue;
        fill(k, &rw);
        if (!whole) {                                   /* (cut: small, faint) */
            cv_text_on(16, y + CAP_IN(S, BR_H), &AF_S, rw.tag, ux_mix(T_SURF, T_MID, 55), T_SURF);
            cv_free_text(56, y + CAP_IN(S, BR_H), &AF_S, rw.nm, ux_mix(T_SURF, T_MID, 55), T_SURF, 160);
            continue;
        }
        if (y + CV_OY >= 0 && y + BR_H + CV_OY <= (int32_t)cv_h)   /* (a split panel: in this canvas whole) */
            GFX_HOOK_ALIGN(0, y, 0, y + BR_H, AL_V, "save list row text centred");   /* (the one in the frame in M, the
                                                         * others S: a slight zoom) */
        if (mid)
            cv_text_on(12, y + CAP_IN(M, BR_H), &AF_M, rw.tag, T_THEME, bg);
        else
            cv_text_on(16, y + CAP_IN(S, BR_H), &AF_S, rw.tag, rw.tc ? rw.tc : T_MID, bg);
        if (mid)
            cv_free_text(54, y + CAP_IN(M, BR_H), &AF_M, rw.nm, rw.nc ? rw.nc : T_TEXT, bg, rw.note[0] ? 112 : 162);
        else
            cv_free_text(56, y + CAP_IN(S, BR_H), &AF_S, rw.nm, rw.nc ? rw.nc : T_MID, bg, rw.note[0] ? 110 : 160);
        if (rw.note[0])
            cv_text_r(216, y + CAP_IN(S, BR_H), &AF_S, rw.note, T_MID, bg);
    }
    list_bar(y0, y1, n * (BR_H + 2) - 2, vt);          /* (Dah: the scrollbar, as the selectors') */
    cv_scroll = 0;
    cv_cy0 = c0;
    cv_cy1 = c1;
}
static void user_row(int32_t k, bigrow_t *r)            /* (Dah) MY PRESETS' row k */
{
    up_slot_label(r->tag, (uint32_t)k);
    if (up_used((uint32_t)k))
        up_name((uint32_t)k, r->nm);
    else {
        str_cpy(r->nm, "EMPTY", sizeof r->nm);         /* (Dah: said, not "--") */
        r->nc = T_DIM;
    }
}
static void graph_user(void)
{
    big_rows(user_row, UP_SLOTS, (int32_t)ui.uslot, glide(&lu_dy) * 2, 18, (int32_t)graph_h() - 4);
    {   /* (Dah) on top, what SAVE puts in a slot: the selected track's sound ("TRACK 1  ACID") */
        char nm[16], tl[10] = "TRACK ";
        int32_t x;
        tl[6] = (char)('1' + song.sel % NTRK);
        tl[7] = 0;
        sound_name(TSEL, nm);
        GFX_HOOK_ALIGN(0, 4, 0, 14, AL_V, "user presets: the track's sound line");
        x = cv_text_on(14, 4 - AF_S_CAP_Y, &AF_S, tl, ux.trk[song.sel % NTRK], T_SURF) + 8;
        cv_free_text(x, 4 - AF_S_CAP_Y, &AF_S, nm[0] ? nm : "--", T_TEXT, T_SURF, 226 - x);
    }
}
/* DRUM's lane pages (ui_drumlane.c): the lanes around the one picked, each with its lane name, its VOICE (KIT: the kit's
 * own piece, by name) and its STACK ("+ CP") */
static void graph_dlanes(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk) % NTRK, cur = ui.lane % NLANE;
    int32_t ln = (int32_t)list_n() < NLANE ? (int32_t)list_n() : NLANE, row, first = clamp((int32_t)cur - ln / 2, 0, NLANE - ln);
    for (row = 0; row < ln; row++) {
        uint32_t l = (uint32_t)(first + row), v = dl_get(tr, l, DL_VOICE), s = dl_get(tr, l, DL_STACK);
        char nm[24];
        if (v)
            dl_voice_name(v, nm);
        else
            str_cpy(nm, drum_lane_name(t, l), sizeof nm);
        if (s) {
            str_cpy(nm + str_len(nm), " + ", 4);
            str_cpy(nm + str_len(nm), drum_lane_abbr(t, s - 1u), 4);
        }
        list_row(LIST_Y(row), l == cur, drum_lane_abbr(t, l), T_MID, nm, v || s ? T_TEXT : T_MID, 232);
    }
}
/* SEQ > AUTOMATION (Dah; ui_events.c): the selected track's HOME knobs, four lanes to the panel's foot, the one picked
 * (KNOB 2) as HOME's track row (tinted, framed, a band at its left); at the left each lane's value at the cursor (its
 * name small over it; none motion records: DIM, "--"), over the steps its curve (held, or joined with CURVE RAMP), a
 * dot where a step holds a point (a lock: ACCENT). The steps as STEP's roll shows them (prv: its VIEW, MOVE ZOOM
 * FOLLOW), its ruler on top (the pattern a line, the view a bar, the step playing a mark and its number), the step
 * playing's column lit, the cursor framed through the lanes */
#define AU_RH 14                                    /* the ruler */
#define AU_X0 60                                    /* the steps: x 60 .. 232 */
#define AU_W 172
static int32_t au_x(uint32_t i) { return AU_X0 + (int32_t)(i * (uint32_t)AU_W / prv_cols()); }
/* the view: its first step p0, n columns shown, the column playing ph (0xFF none) */
static void au_view(const track_t *t, uint32_t *p0, uint32_t *n, uint32_t *ph)
{
    uint32_t len = au_len(t), nc = prv_cols();
    *p0 = prv_base(t);
    *n = *p0 < len ? (len - *p0 < nc ? len - *p0 : nc) : 0u;
    *ph = song.playing && t->seq_idx >= *p0 && t->seq_idx < *p0 + *n ? t->seq_idx - *p0 : 0xFFu;
}
/* what the page shows (graph_signature): the track's motion, the view, the cursor, the lane, the four ids, CURVE, the
 * knob just turned */
static uint32_t au_sig(void)
{
    const track_t *t = TSEL;
    uint32_t p0, n, ph, k, h;
    au_view(t, &p0, &n, &ph);
    h = motion_sig(t) ^ (p0 * 131u + n * 7u + ph * 7919u + au_cur() * 104729u + au_lane * 613u + prv_cols() * 31u);
    for (k = 0; k < 4u; k++)
        h = (h ^ (au_raw(k) * 4u + k)) * 16777619u;
    return h ^ (trkx[song.sel % NTRK].curve ? 0x51EDu : 0u) ^ (ui.hot_t ? ui.hot_col + 1u : 0u) * 2654435761u;
}
/* lane k's value at the cursor at x 8 .. AU_X0 - 4 (its unit in S after it; too wide: in S, then no unit), its name over
 * it when the lane has room */
static void au_value(const track_t *t, uint32_t k, int32_t ly, int32_t lh, uint16_t bg, int sel)
{
    uint32_t id = au_id(k);
    const char *nm = au_label(k), *unit = "";
    const aafont_t *f = &AF_M;
    char val[16];
    int32_t w = AU_X0 - 12, two = lh >= 26, y, x;
    int pt = 0;
    uint16_t vc;
    if (two)
        cv_text_fit(8, ly + 4 - AF_S_CAP_Y, &AF_S, nm[0] && nm[0] != '-' ? nm : "--", sel ? T_THEME : T_MID, bg, w);
    if (id == 0xFFu) {
        str_cpy(val, "--", sizeof val);
        vc = T_DIM;
    } else {
        int16_t v = lane_value(t, au_cur(), id, &pt);
        param_format(track_desc(t, id), v, val, &unit);
        vc = sel && ui.hot_t && ui.hot_col == 2u ? T_ACCENT : pt ? T_TEXT : T_MID;
    }
    if (text_w(f, val) + (unit[0] ? 2 + text_w(&AF_S, unit) : 0) > w)
        f = &AF_S;
    if (text_w(f, val) + (unit[0] ? 2 + text_w(&AF_S, unit) : 0) > w)
        unit = "";
    y = two ? ly + 4 + AF_S_CAP_H + 4 : ly + (lh - (f == &AF_M ? AF_M_CAP_H : AF_S_CAP_H)) / 2;
    x = cv_text_fit(8, y - (f == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), f, val, vc, bg, w);
    if (unit[0])
        cv_text_on(x + 2, y - (f == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y) + f->asc - AF_S.asc, &AF_S, unit, T_MID, bg);
}
static void graph_auto(void)
{
    const track_t *t = TSEL;
    uint32_t p0, n, ph, i, k, len = au_len(t), cur = au_cur(), nc = prv_cols();
    int32_t full = cv_split ? cv_split_h : (int32_t)cv_h, y0 = AU_RH + 3, ybot = full - 4, lh, gap = 3, cw = AU_W / (int32_t)nc;
    au_view(t, &p0, &n, &ph);
    lh = (ybot - y0 - 3 * gap) / 4;
    {   /* the ruler: the pattern a line, the view on it a bar, the step playing a mark; a tick a step (zoomed out: the
         * beats), the beats taller */
        int32_t x0 = AU_X0 + (int32_t)(p0 * (uint32_t)AU_W / len), x1 = AU_X0 + (int32_t)((p0 + n) * (uint32_t)AU_W / len);
        cv_rect(AU_X0, 1, AU_W, 1, T_LINE);
        cv_rect(x0, 0, x1 - x0 > 2 ? x1 - x0 : 2, 3, T_MID);
        if (song.playing && t->seq_idx < len)
            cv_rect(AU_X0 + (int32_t)(t->seq_idx * (uint32_t)AU_W / len), 0, 2, 3, T_ACCENT);
        for (i = 0; i < n; i++) {
            int32_t xc = (au_x(i) + au_x(i + 1u)) / 2, beat = (p0 + i) % 4u == 0u;
            if (beat || cw >= 4)
                cv_rect(xc, beat ? 8 : 10, 1, beat ? 3 : 1, beat ? T_MID : T_GRID);
        }
    }
    for (k = 0; k < 4u; k++) {
        uint32_t id = au_id(k);
        int32_t ly = y0 + (int32_t)k * (lh + gap), a = lh - 10, px = 0, py = 0;
        int sel = k == au_lane;
        const param_desc_t *d = id != 0xFFu ? track_desc(t, id) : 0;
        uint16_t row = sel ? ux_mix(T_SURF, T_THEME, 16) : T_SURF;
        if (sel) {                                    /* the lane picked as HOME's track: tinted, framed, a band */
            cv_rect(3, ly, 234, lh, row);
            cv_frame(3, ly, 234, lh, T_THEME);
            cv_rect(3, ly, 3, lh, T_THEME);
        }
        au_value(t, k, ly, lh, row, sel);
        cv_rect(AU_X0, ly + 1, au_x(n) - AU_X0, lh - 2, d ? T_LANE : T_RAISE);
        if (!d)
            continue;
        for (i = 1; i < n; i++)                       /* the beats */
            if ((p0 + i) % 4u == 0u)
                cv_rect(au_x(i), ly + 1, 1, lh - 2, T_RAISE);
        if (ph != 0xFFu)                              /* the step playing: its column lit (as HOME's) */
            cv_rect(au_x(ph) + 1, ly + 1, au_x(ph + 1u) - au_x(ph) - 1 > 1 ? au_x(ph + 1u) - au_x(ph) - 1 : 1, lh - 2,
                    ux_mix(T_LANE, T_TEXT, 22));
        for (i = 0; i < n; i++) {                     /* the curve: held (a step, a line), or joined (RAMP) */
            int pt;
            int32_t v = au_drawn(t, p0 + i, id, &pt), xl = au_x(i), xr = au_x(i + 1u) - 1, xc = (xl + xr) / 2;
            int32_t y = ly + 4 + a - (enum_rank(d, v) - d->min) * a / (d->max - d->min);
            if (xr < xl)
                xr = xl;
            if (trkx[song.sel % NTRK].curve && d->fmt != F_ENUM) {
                if (i)
                    cv_line_t(px, py, xc, y, T_THEME, 2);
                else
                    cv_rect(xl, y, xc - xl + 1, 2, T_THEME);
                if (i + 1u == n)
                    cv_rect(xc, y, xr - xc + 1, 2, T_THEME);
                px = xc;
            } else {
                if (i && y != py)
                    cv_rect(xl, y < py ? y : py, 2, (y < py ? py - y : y - py) + 2, T_THEME);
                cv_rect(xl, y, xr - xl + 1, 2, T_THEME);
            }
            py = y;
        }
        for (i = 0; i < n; i++) {                     /* the points over it */
            int pt;
            int16_t lv;
            int32_t v = au_drawn(t, p0 + i, id, &pt), xc = (au_x(i) + au_x(i + 1u) - 1) / 2;
            int32_t y = ly + 4 + a - (enum_rank(d, v) - d->min) * a / (d->max - d->min);
            if (au_point(t, p0 + i, id))
                cv_rect(xc - 1, y - 1, 4, 4, motion_lock_get(t, p0 + i, id, &lv) ? T_ACCENT : T_TEXT);
        }
    }
    if (cur >= p0 && cur < p0 + n)                    /* the cursor: a column framed through the lanes */
        cv_frame(au_x(cur - p0), y0 - 1, au_x(cur - p0 + 1u) - au_x(cur - p0) + 1, 4 * lh + 3 * gap + 2, T_TEXT);
    if (ph != 0xFFu) {                                /* the step playing's number on the ruler, centred on it */
        char nb[4];
        int32_t w, ib[4], nx;
        fmt_int(nb, (int32_t)(p0 + ph) + 1);
        w = text_w(&AF_S, nb);
        text_ink(&AF_S, nb, ib);
        nx = clamp((au_x(ph) + au_x(ph + 1u) - (ib[0] + ib[2])) / 2, AU_X0, 234 - w);
        cv_rect(nx - 1, 3, w + 2, 11, T_SURF);
        cv_text_on(nx, 4 - AF_S_CAP_Y, &AF_S, nb, T_ACCENT, T_SURF);
    }
}
/* SAVE > PATTERN: the track's patterns, four rows round the one picked: its number (the one played in the track's colour),
 * its notes and size; the memory the patterns take, on the last line */
/* (Dah) pattern num's steps, every track's a line (x0, y0, w x h): a step that strikes a block in the track's colour,
 * its TIEs after it the same, dimmer; the track's length a hairline (bg: the row's) */
static void pt_mini(uint32_t num, int32_t x0, int32_t y0, int32_t w, int32_t h, uint16_t bg)
{
    uint32_t k, i, nt = trk_count(), len, n, pos;
    int32_t lh = h / (int32_t)(nt ? nt : 1u), dw = w / NSTEP > 1 ? w / NSTEP - 1 : 1;
    for (k = 0; k < nt; k++) {
        const step_t *s = ps_st;
        uint16_t tc = ux.trk[k];
        int32_t y = y0 + (int32_t)k * lh, dh = lh > 2 ? lh - 1 : 1;
        int on = 0;
        if (num == ps_num(k)) {
            s = trk[k].step;
            len = (uint32_t)clamp(trk[k].p[P_SLEN], 1, NSTEP);
        } else if (ps_exists(k, num) && (n = ps_fetch(k, num)) >= 12u && (pos = 10u, ps_steps_get(ps_buf, n, &pos, ps_st)))
            len = (uint32_t)clamp((int32_t)(ps_buf[0] | ps_buf[1] << 8), 1, NSTEP);
        else
            continue;                                   /* (none: an empty line) */
        cv_rect(x0, y + dh / 2, w * (int32_t)len / NSTEP, 1, T_LINE);
        for (i = 0; i < len; i++) {
            int32_t x = x0 + (int32_t)i * w / NSTEP;
            if (s[i].time == ST_NOTE && (s[i].n || s[i].hit)) {
                cv_rect(x, y, dw, dh, tc);
                on = 1;
            } else if (s[i].time == ST_TIE && on)
                cv_rect(x - 1, y, dw + 1, dh, ux_mix(bg, tc, 50));
            else
                on = 0;
        }
    }
}
/* (Dah) the patterns as rows of 34 px, the one playing in the middle (framed), dy px on its way (gliding); add: + NEW after
 * the last (SELECT turned past it makes one) */
/* (Dah) a list just turned (SELECT / GLO + ALGORITHM the patterns, PRESETS the sounds, ALGORITHM the tracks), the
 * whole screen ~1 s: its name and place on top, the one picked in the middle, large and framed, the others smaller and
 * fainter the further they are (a fisheye). Drawn in two halves (the canvas holds 142 rows) */
enum { FL_PAT, FL_SND, FL_TRK, FL_FX, FL_PICK };
static uint32_t fl_pick_rows(uint32_t *cur);            /* (ui_graph_fx.c: ADD's picker, the free effects) */
static void fl_pick_label(uint32_t i, char *tag, char *nm);
static uint32_t fl_fx_rows(uint32_t *cur);              /* (ui_graph_fx.c: CHAIN's effects) */
static void fl_fx_label(uint32_t i, char *tag, char *nm);
static const char *fl_fx_note(uint32_t i);
#define FL_TOP 34                                      /* the title's band (its name in L, its place in S) */
static const uint8_t FL_H[5] = {44, 30, 24, 20, 18};   /* a row's height by its distance from the one picked */
static uint32_t fl_kind(void) { return ui.confirm == CF_PICK_FX ? FL_PICK : tl_on() ? (tl_pat == 2u ? FL_FX : tl_pat ? FL_PAT : FL_TRK) : FL_SND; }
/* the list: how many rows, the one picked (its index) */
static uint32_t fl_count(uint32_t kind, uint32_t *cur, uint8_t *nums)
{
    uint32_t n, i;
    if (kind == FL_PAT) {
        n = ps_list_all(nums);
        for (i = *cur = 0; i < n; i++)
            if (nums[i] == ps_num(song.sel)) *cur = i;
        return n + (n < PS_NUM);                        /* (+ NEW after the last) */
    }
    if (kind == FL_FX)
        return fl_fx_rows(cur);
    if (kind == FL_PICK)
        return fl_pick_rows(cur);
    if (kind == FL_TRK) {
        *cur = mx_master ? 0u : song.sel + 1u;          /* (the MASTER first, then the tracks, + ADD TRACK) */
        return 1u + trk_count() + (trk_count() < NTRK);
    }
    *cur = preset_all_pos(&n);
    return n;
}
static void fl_row(uint32_t kind, uint32_t i, uint32_t n, const uint8_t *nums, int32_t y, int32_t h, uint32_t dq, int sel)
{
    char tag[6] = {0}, nm[16] = {0};
    const aafont_t *f = dq < 384u ? &AF_M : &AF_S;      /* (dq: its distance from the middle, 1/256 rows) */
    uint16_t ink = dq < 384u ? T_TEXT : dq < 640u ? T_MID : T_DIM, tc = T_MID, bg = T_BG;
    int32_t ty = y + (f == &AF_M ? CAP_IN(M, h) : CAP_IN(S, h)), nx = 52;
    (void)sel;                                          /* (Dah: the frame stays in the middle, draw_list_full) */
    if (kind == FL_PAT) {
        uint8_t tmp[PS_NUM];
        (void)n;
        if (i >= ps_list_all(tmp)) {                    /* + NEW */
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "full list row on its middle");
            cv_text_on(14, ty, f, "+ NEW", sel ? T_THEME : ink, bg);
            return;
        }
        fmt_int(tag, nums[i]);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "full list row on its middle");
        cv_text_on(14, ty, f, tag, sel ? T_THEME : ink, bg);
        pt_mini(nums[i], 44, y + (sel ? 5 : 3), 178, h - (sel ? 10 : 6), bg);
        return;
    }
    if (kind == FL_FX || kind == FL_PICK) {
        if (kind == FL_FX)
            fl_fx_label(i, tag, nm);
        else
            fl_pick_label(i, tag, nm);
        nx = kind == FL_PICK ? 14 : 40;
    } else if (kind == FL_TRK) {
        if (i == 0u) {
            str_cpy(tag, "M", sizeof tag);
            str_cpy(nm, "MASTER", sizeof nm);
        } else if (i - 1u < trk_count()) {
            tag[0] = (char)('0' + i);
            sound_name(&trk[i - 1u], nm);
            tc = ux.trk[(i - 1u) % NTRK];
        } else {
            str_cpy(tag, "+", sizeof tag);
            str_cpy(nm, "ADD TRACK", sizeof nm);
        }
        nx = 40;
    } else {
        uint32_t k, e = preset_all_at(i, &k);
        if (e == NENGINES) {
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {
            str_cpy(tag, eng_abbr(ENGINES[e]->name), sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
    }
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "full list row on its middle");
    cv_text_on(14, ty, f, tag, kind == FL_TRK && i ? tc : kind == FL_SND ? (dq < 640u ? T_MID : T_DIM) : sel ? T_THEME : ink,
               bg);                                    /* (Dah: a sound's engine greyed, its name the text) */
    if (14 + (int32_t)text_w(f, tag) + 8 > nx)          /* (a long tag in M: the name after it) */
        nx = 14 + (int32_t)text_w(f, tag) + 8;
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "full list row on its middle");
    if (kind == FL_FX && fl_fx_note(i)[0]) {           /* (Dah) a track's send: the note small and grey after it */
        int32_t ex = cv_text_on(nx, ty, f, nm, ink, bg) + 6;
        int32_t sy = ty + (f == &AF_M ? AF_M_CAP_Y + AF_M_CAP_H : AF_S_CAP_Y + AF_S_CAP_H) - AF_S_CAP_Y - AF_S_CAP_H;
        cv_free_text(ex, sy, &AF_S, fl_fx_note(i), T_MID, bg, 222 - ex);
        return;
    }
    if ((kind == FL_SND || kind == FL_TRK) && dq < 128u && text_w(&AF_B, nm) <= 222 - nx) {
        const char *q = nm;
        while (*q && glyph_at(&AF_B, fold(&AF_B, (uint8_t)*q)) >= 0)
            q++;
        if (!*q) {                                      /* (Dah) the sound / track in the middle: M's face larger (B) */
            cv_text_on(nx, y + CAP_IN(B, h), &AF_B, nm, ink, bg);
            return;
        }
    }
    cv_free_text(nx, ty, f, nm, ink, bg, 222 - nx);
}
static uint32_t fl_sig(void)                           /* what the full list shows */
{
    uint8_t nums[PS_NUM];
    uint32_t cur, kind = fl_kind(), n = fl_count(kind, &cur, nums);
    return kind * 7919u + cur * 131u + n * 104729u + ps_gen * 31u + ux.gen * 3u + TSEL->eng_req * 977u +
           TSEL->preset * 1009u + trk_count() * 15485863u + mx_master * 2u;
}
/* (Dah) the list's place on screen, 1/256 rows, gliding to the one picked (a third of the way a frame) */
static int32_t fl_pos, fl_tgt;
static uint32_t fl_last = 0xFFu;
static int fl_moving(void) { return fl_pos != fl_tgt; }
static int32_t fl_h(int32_t dq)                         /* a row's height at its distance (1/256 rows), between FL_H's */
{
    int32_t k = dq >> 8, fr = dq & 255;
    if (k >= 4)
        return FL_H[4];
    return FL_H[k] + (FL_H[k + 1] - FL_H[k]) * fr / 256;
}
static void draw_list_full(int fresh)
{
    uint8_t nums[PS_NUM];
    uint32_t kind = fl_kind(), cur, n = fl_count(kind, &cur, nums), half;
    int32_t mid = FL_TOP + (240 - FL_TOP) / 2, c0, f, d, top0;
    char t[24], pl[12] = {0};
    fl_tgt = (int32_t)cur * 256;
    if (fresh || kind != fl_last || fl_pos < 0 || fl_pos > (int32_t)n * 256)
        fl_pos = fl_tgt;
    else if (fl_pos != fl_tgt) {
        d = (fl_tgt - fl_pos) / 3;
        if (d > -24 && d < 24)
            d = fl_tgt > fl_pos ? 24 : -24;
        fl_pos = (d > 0 ? fl_pos + d > fl_tgt : fl_pos + d < fl_tgt) ? fl_tgt : fl_pos + d;
    }
    fl_last = kind;
    c0 = fl_pos >> 8;
    f = fl_pos & 255;
    {   /* the row at c0: its top, the middle moved by the way it has gone towards the next */
        int32_t h0 = fl_h(f), h1 = fl_h(256 - f);
        top0 = mid - f * ((h0 + h1) / 2 + 1) / 256 - h0 / 2;
    }
    str_cpy(t, kind == FL_PAT ? "Pattern" : kind == FL_TRK ? "Track" : kind == FL_FX ? "Effect" :
               kind == FL_PICK ? "Add effect" : "Preset", sizeof t);
    if (kind != FL_PICK) {                              /* (its place after it: "1/N", small) */
        fmt_int(pl, (int32_t)cur + 1);
        str_cpy(pl + str_len(pl), "/", 2);
        fmt_int(pl + str_len(pl), (int32_t)n);
    }
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, T_BG);
        cv_oy = -120 * (int32_t)half;
        {   /* (Dah) the list's name large and white, its place small and grey, on one baseline */
            int32_t x = cv_text(10, CAP_IN(L, FL_TOP), &AF_L, t, T_TEXT);   /* (L: large and bold) */
            int32_t by = CAP_IN(L, FL_TOP) + AF_L_CAP_Y + AF_L_CAP_H - AF_S_CAP_Y - AF_S_CAP_H;
            if (pl[0])
                cv_text(x + 6, by, &AF_S, pl, T_MID);
        }
        if (n) {                                        /* (Dah) the frame fixed in the middle, the rows gliding through
                                                         * it: an outline only, round (left of the scrollbar) */
            cv_rrect(4, mid - FL_H[0] / 2, 224, FL_H[0], -8, T_THEME, T_BG);
            cv_rrect(6, mid - FL_H[0] / 2 + 2, 220, FL_H[0] - 4, -6, T_BG, T_THEME);
        }
        if (!n) {
            GFX_HOOK_ALIGN(0, mid - 10, 0, mid + 10, AL_V, "full list row on its middle");
            cv_text(14, mid - 10 + CAP_IN(S, 20), &AF_S, "NOTHING IN THIS LIST", T_MID);
        } else {
            int32_t i, y = top0;
            for (i = c0; i >= 0; i--) {                 /* the row at c0 and those above it */
                int32_t dq = fl_pos - i * 256, h = fl_h(dq < 0 ? -dq : dq);
                if (i < c0)
                    y -= h + 1;
                if (y < FL_TOP)
                    break;
                if ((uint32_t)i < n)
                    fl_row(kind, (uint32_t)i, n, nums, y, h, (uint32_t)(dq < 0 ? -dq : dq), (uint32_t)i == cur);
            }
            y = top0 + fl_h(f) + 1;
            for (i = c0 + 1; (uint32_t)i < n && y < (kind == FL_PICK ? 220 : 240); i++) {   /* under it (the picker:
                                                         * its keys at the foot) */
                int32_t dq = i * 256 - fl_pos, h = fl_h(dq);
                if (kind == FL_PICK && y + h > 220)
                    break;
                fl_row(kind, (uint32_t)i, n, nums, y, h, (uint32_t)dq, (uint32_t)i == cur);
                y += h + 1;
            }
        }
        if (n > 1u) {                                   /* (Dah) the scrollbar at the right: where in the list */
            int32_t ty0 = FL_TOP + 2, th = 240 - 4 - ty0, bh = th / (int32_t)n > 10 ? th / (int32_t)n : 10, by;
            by = ty0 + (int32_t)(fl_pos * (th - bh) / ((int32_t)(n - 1u) * 256));
            cv_rect(234, ty0, 2, th, T_LINE);
            cv_rect(233, by, 4, bh, T_MID);
        }
        if (kind == FL_PICK) {                          /* (its keys, at the foot) */
            static const khint_t KH[2] = {{KC_OCTDN, "NO"}, {KC_OCTUP, "ADD"}};   /* (OCT- first, as on the panel) */
            cv_key_left(10, 240 - 4 - KC_H, KH, 2, 3u, T_BG);
        }
        cv_oy = 0;
        cv_blit(0, 120u * half);
    }
}
static void pat_rows(int32_t dy, int add)
{
    uint8_t nums[PS_NUM];
    int32_t n = (int32_t)pt_list(nums), at = 0, i, k, rows, vt, fy;
    uint32_t cur = ps_num(song.sel);
    char b[4];
    int32_t avail = (int32_t)graph_h() - (add ? 0 : 18);   /* (Dah: the panel's height; the PATTERN page: its memory
                                                         * line under them) */
    for (i = 0; i < n; i++)
        if (nums[i] == cur) at = i;
    rows = n + (add && n < (int32_t)PS_NUM);
    vt = tall_view(rows, at, dy, avail - 2, &fy, 32);
    cv_rrect(4, 1 + fy, 222, 34, 5, T_THEME, T_SURF);   /* (the frame: down to the middle, the rows by, on down; Dah:
                                                         * the one in it a little bigger, as CHAIN's) */
    cv_rrect(6, 3 + fy, 218, 30, 4, T_RAISE, T_THEME);
    for (k = 0; k < rows; k++) {
        int32_t y = 2 + k * 34 - vt;
        int mid = y > 2 + fy - 17 && y < 2 + fy + 17, whole = y >= 0 && y + 32 <= avail;
        uint16_t bg = mid ? T_RAISE : T_SURF;
        if (y + 32 <= 0 || y >= avail)
            continue;
        if (k == n) {                                   /* + NEW */
            if (!whole)
                continue;
            GFX_HOOK_ALIGN(0, y, 0, y + 32, AL_V, "pattern row number centred");
            cv_text_on(14, y + CAP_IN(M, 32), &AF_M, "+", mid ? T_THEME : T_DIM, bg);
            cv_text_on(36, y + CAP_IN(M, 32), &AF_M, "NEW", mid ? T_THEME : T_DIM, bg);
            continue;
        }
        if (!whole) {                                   /* (Dah) cut by an edge: its number small, faint, clipped */
            int16_t c0 = cv_cy0, c1 = cv_cy1;
            fmt_int(b, (int32_t)nums[k]);
            cv_cy0 = (int16_t)clamp(CV_OY, 0, (int32_t)cv_h);
            cv_cy1 = (int16_t)clamp(avail + CV_OY, cv_cy0, (int32_t)cv_h);
            cv_scroll = 1;
            cv_text_on(16, y + CAP_IN(S, 32), &AF_S, b, ux_mix(T_SURF, T_MID, 55), bg);
            cv_scroll = 0;
            cv_cy0 = c0;
            cv_cy1 = c1;
        } else {
            fmt_int(b, (int32_t)nums[k]);
            GFX_HOOK_ALIGN(0, y, 0, y + 32, AL_V, "pattern row number centred");
            if (mid)
                cv_text_on(12, y + CAP_IN(M, 32), &AF_M, b, T_THEME, bg);
            else
                cv_text_on(16, y + CAP_IN(S, 32), &AF_S, b, T_MID, bg);
        }
        if (mid)
            pt_mini(nums[k], 34, y + 3, 186, 27, bg);   /* (its steps a little bigger) */
        else
            pt_mini(nums[k], 38, y + 4, 178, 25, bg);
    }
    list_bar(0, avail, rows * 34 - 2, vt);              /* (Dah: the scrollbar, as the selectors') */
}
static void graph_pattern(void)
{
    pat_rows(glide(&pp_dy) * 2, 0);                     /* (Dah: gliding, as the lists) */
    cv_rect(8, (int32_t)graph_h() - 18, 224, 18, T_SURF);   /* (the rows cut over the memory line; they lie in x 8 ..
                                                         * 232: the panel's round corners untouched) */
    {
        char m[32] = "MEMORY ";                         /* (Dah: the memory; which one of how many: KNOB 1's card) */
        fmt_int(m + str_len(m), (int32_t)pt_mem_pct());
        str_cpy(m + str_len(m), " %", 4);
        int32_t my = (int32_t)graph_h() - 14;           /* (Dah: at the panel's foot) */
        GFX_HOOK_ALIGN(0, my, 0, my + 10, AL_V, "pattern memory line");   /* (its "/": 2 px higher) */
        cv_text_on(14, my - AF_S_CAP_Y, &AF_S, m, T_DIM, T_SURF);
    }
}
/* HA HA HE HU: SAVE > FILE's projects (projfs.c), read again when one changed (proj_wire_gen: a save, an open, a delete):
 * four rows round the one picked: the open one's id in the accent and "OPEN", each one's name and share of the memory */
static struct { uint8_t ok, gen, n, ids[32], pct[32]; char name[32][13]; } gfile;
static void graph_file_read(void)
{
    uint32_t i;
    if (gfile.ok && gfile.gen == proj_wire_gen_ui())
        return;
    gfile.ok = 1;
    gfile.gen = (uint8_t)proj_wire_gen_ui();
    gfile.n = (uint8_t)pfs_list(gfile.ids);
    for (i = 0; i < gfile.n; i++) {
        pfs_name(gfile.ids[i], gfile.name[i]);
        gfile.pct[i] = (uint8_t)pfs_pct(gfile.ids[i]);
    }
}
static uint32_t graph_arch_sig(void)
{
    graph_file_read();
    return fnv(gfile.n + 1u, gfile.name, sizeof gfile.name) + fnv(7u, gfile.pct, sizeof gfile.pct);
}
static void file_row(int32_t k, bigrow_t *r)            /* (Dah) FILE's row k */
{
    uint32_t cur = pfs_cur();
    fmt_int(r->tag, (int32_t)gfile.ids[k]);
    str_cpy(r->nm, gfile.name[k][0] ? gfile.name[k] : "UNTITLED", 14);
    fmt_int(r->note, gfile.pct[k]);
    str_cpy(r->note + str_len(r->note), gfile.ids[k] == cur ? "%  OPEN" : "%", 8);
    r->tc = gfile.ids[k] == cur ? T_ACCENT : 0;
}
static void graph_file(void)
{
    uint32_t i, at = 0, first, cur = pfs_cur();
    char m[24] = "MEMORY ";                             /* (Dah) the memory the projects take, on the last line */
    graph_file_read();
    fmt_int(m + 7, (int32_t)pfs_mem_pct());
    str_cpy(m + str_len(m), " %", 4);
    uint32_t rn = list_n() - 1u;                         /* (Dah: the panel's rows, the memory line under them) */
    int32_t my = (int32_t)graph_h() - 14;
    GFX_HOOK_ALIGN(0, my, 0, my + 10, AL_V, "file memory line");
    cv_text_on(14, my - AF_S_CAP_Y, &AF_S, m, T_DIM, T_SURF);
    if (!gfile.n) {
        list_row(LIST_Y(0), 0, "", T_MID, "NO PROJECT YET", T_DIM, 232);
        return;
    }
    for (i = 0; i < gfile.n; i++)
        if (gfile.ids[i] == ui.fsel) at = i;
    {   /* (Dah) as PATTERN's: tall rows gliding through the frame (lf_dy); the memory line under them */
        big_rows(file_row, (int32_t)gfile.n, (int32_t)at, glide(&lf_dy) * 2, 2, my - 4);
    }
    (void)first; (void)rn;
}
/* project slots: the name (none: USED) / EMPTY, the selected one filled; (HA HA HE HU) the one the music is from: its
 * letter in the accent, "OPEN" after its name */
static void graph_slots(void)
{
    uint32_t i, cur = project_current();
    for (i = 0; i < 4u; i++) {
        int32_t y = 12 + (int32_t)i * 26;
        char b[4];
        int sel = (int32_t)i + 1 == song.g[G_SLOT], used = graph_project_used(i);
        const char *n = graph_project_name(i);
        char nm[24];
        b[0] = (char)('A' + i);
        b[1] = 0;
        str_cpy(nm, !used ? "EMPTY" : n[0] ? n : "UNTITLED", 14);
        if (used && i == cur)
            str_cpy(nm + str_len(nm), "  (OPEN)", 9);
        list_row(y, sel, b, i == cur && used ? T_ACCENT : T_MID, nm, used ? T_TEXT : T_DIM, 232);
    }
}
/* MIXER page: four SURF columns, one under each card: the track number on its cushion (in the accent:
 * the selected track) with a REC / ARM / MUTE badge (P_MUTE, KNOB 1), the sound's short name (a MUTE badge
 * when armed and muted), the LEVEL knob (dB inside) with the output meter beside it, then the PAN and REV
 * knobs with their values. Knobs: a 270 degree ring, the track RAISE, the value arc THEME (the knob just
 * turned: ACCENT; muted: DIM); PAN from 12 o'clock. Each column is its own canvas with its own signature:
 * while the transport runs only the meters move. */
#define TS_MY 39                                     /* meter slot: column rows 39 .. 70 */
#define TS_MH 32
#define KB_X 4                                       /* LEVEL knob: box 4..37 x 37..70, centre (21, 54) */
#define KB_Y 37
#define KS_Y 86                                      /* PAN / REV knobs: boxes 86..105, centres x 15 and 42 */
static struct {
    uint32_t col[NTRK];
    uint8_t meter[NTRK];
} ts;

static int32_t meter_px(int32_t a)                   /* |sample| (Q15) -> px: 60 dB over the slot */
{
    int32_t lg = 0, v;
    if (a < 64)
        return 0;
    while ((a >> lg) > 1)
        lg++;
    v = lg * 8 + (((a << 3) >> lg) & 7);             /* 8 log2(a): 48 (-54 dB) .. 128 (+6 dB) */
    return clamp((v - 48) * (TS_MH - 2) / 80, 0, TS_MH - 2);
}

/* a knob's ring: 270 degrees (7:30 .. 4:30 o'clock), 2 px, anti-aliased, from the quadrant mask of its size
 * (tools/gen_aa_keycaps.py: coverage and angle; the other three quadrants mirror it, no trigonometry here).
 * Box top-left (x, y), outer radius r. Angles: 1/1024 turn from 12 o'clock, clockwise, -384 .. 384; the value
 * arc is lo .. hi (in vc), the rest of the ring the track (tr); bg lies under the ring */
#define KA_END 384
static int32_t ka_end = KA_END;                        /* (Dah: HOME's knobs open wider at the bottom, their name there) */
static void knob_arc(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t lo, int32_t hi,
                     uint16_t tr, uint16_t vc, uint16_t bg)
{
    uint16_t rt[16];                                   /* (Dah: a copy: the second ramp may take the first's slot of
                                                         * the cache, the ring then flickering between the two) */
    const uint16_t *rv;
    int32_t i, j, m;
    memcpy(rt, ramp(tr, bg), sizeof rt);
    rv = ramp(vc, bg);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t k = (uint32_t)(j * r + i), a = (cov[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t q = ang[k];
            if (!a)
                continue;
            for (m = 0; m < 4; m++) {                /* top right, bottom right, bottom left, top left */
                int32_t s = m == 0 ? q : m == 1 ? 512 - q : m == 2 ? q - 512 : -q;
                int32_t px = m < 2 ? x + r + i : x + r - 1 - i, py = (m == 0 || m == 3 ? y + r - 1 - j : y + r + j) + CV_OY;
                if (s < -ka_end || s > ka_end || (uint32_t)px >= cv_w || (uint32_t)py >= cv_h)
                    continue;
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = (s >= lo && s <= hi ? rv : rt)[a];
            }
        }
}
/* value v of lo..hi as an arc: from the start, or (bipolar, lo < 0) from 12 o'clock with a 1 px nub at 0 */
static void knob(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t v, int32_t lo,
                 int32_t hi, uint16_t vc)
{
    int32_t a0, a1;
    if (lo < 0) {
        int32_t s = v * KA_END / (v < 0 ? -lo : hi);
        a0 = (s < 0 ? s : 0) - 8;
        a1 = (s > 0 ? s : 0) + 8;
    } else {
        a0 = -KA_END;
        a1 = v > lo ? -KA_END + (v - lo) * 2 * KA_END / (hi - lo) : -KA_END - 1;
    }
    knob_arc(x, y, r, cov, ang, a0, a1, T_RAISE, vc, T_SURF);
}

static uint32_t trk_level(uint32_t c) { return (uint32_t)trk[c].p[P_LEVEL] & 127u; }   /* LEVEL 0..127 */

static void trk_short_name(uint32_t c, char *b)      /* the track's sound, b holds 13 */
{
    const track_t *t = &trk[c];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
    else
        str_cpy(b, e->name, 13);
}

/* MENU > LARGE: the four tracks in the strip under the tall cards (57 x LG_H_GRAPH each): the cushion (the selected
 * one the accent), REC / ARM / MUTE as on the full mixer, the sound's name, LEVEL as a gauge (THEME: the cards'),
 * the output meter under it (MID); a muted track DIM */
#define TSS_GY 40                                    /* the LEVEL gauge, 3 px */
#define TSS_MY 48                                    /* the meter, 3 px */
static void track_strip(uint32_t c, uint32_t sel, uint32_t st, uint32_t mute, uint32_t arm, uint32_t hot, uint32_t lvl,
                        const char *b, uint32_t lit)
{
    int32_t gw = CARD_W - 10, m = ts.meter[c] * gw / (TS_MH - 2), fx = (int32_t)lvl * gw / 127;
    uint16_t vc = mute ? T_DIM : T_THEME;
    cv_begin(CARD_W, LG_H_GRAPH, T_BG);
    cv_rrect(0, 0, CARD_W, LG_H_GRAPH, 5, T_SURF, T_BG);
    cv_icon_on(4, 4, 16, trk_icon(c, sel), lit ? (sel ? T_TEXT : T_ACCENT) : sel ? T_ACCENT : T_MID, T_SURF);
    if (st)
        GFX_HOOK_ALIGN(0, 4, 0, 20, AL_V, "mixer badge on the cushion's line");
    if (st == 1u || st == 2u)
        cv_keycap(53 - kc_w(st == 1u ? KC_REC : KC_ARM), 5, st == 1u ? KC_REC : KC_ARM, st == 1u ? T_REC : T_ACCENT,
                  T_INK, T_SURF);
    else if (st)
        cv_keycap(53 - kc_w(KC_MUTE), 5, KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
    if (mute && arm)                                 /* armed and muted: MUTE in place of the name */
        GFX_HOOK_ALIGN(0, 22 + AF_S_CAP_Y, 0, 22 + AF_S_CAP_Y + AF_S_CAP_H, AL_V, "mixer MUTE badge on the name's line");
    if (mute && arm)
        cv_keycap(5, 22 + AF_S_CAP_Y + HALF_UP(AF_S_CAP_H - KC_H), KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
    else
        cv_free_text(5, 22, &AF_S, b, mute ? T_DIM : sel ? T_TEXT : T_MID, T_SURF, CARD_W - 10);
    cv_rrect(5, TSS_GY, gw, 3, 1, ux.style ? T_LINE : T_BG, T_SURF);
    cv_rrect(5, TSS_GY, fx < 3 ? 3 : fx, 3, 1, hot == 1u ? T_ACCENT : vc, T_BG);
    cv_rrect(5, TSS_MY, gw, 3, 1, T_RAISE, T_SURF);
    if (m)
        cv_rrect(5, TSS_MY, m < 3 ? 3 : m, 3, 1, T_MID, T_RAISE);
    cv_blit((uint32_t)CARD_X(c % 4u), LG_Y_GRAPH);
}
/* (Dah) a level meter's colour at fill f of n (its height): green, then orange towards the top, red at the end (near
 * 0 dB), every palette alike (a meter reads as one) */
#define MTR_GREEN 0x366Au
#define MTR_ORANGE 0xFCA0u
#define MTR_RED 0xF145u
static uint16_t meter_col(int32_t f, int32_t n)
{
    int32_t p = n > 0 ? f * 100 / n : 0;
    if (ux.mono || ux.trk[0] == ux.trk[1])              /* (GREY, MONO: no hues; lighter to the top) */
        return ux_mix(T_MID, T_TEXT, p);
    if (p < 60)
        return MTR_GREEN;
    if (p < 85)
        return ux_mix(MTR_GREEN, MTR_ORANGE, (p - 60) * 4);
    return ux_mix(MTR_ORANGE, MTR_RED, (p - 85) * 100 / 15);
}
/* an upright meter at x, w wide, from y (top) h tall, filled f from its foot; muted: DIM */
static void meter_up(int32_t x, int32_t y, int32_t w, int32_t h, int32_t f, int mute, uint16_t bg)
{
    int32_t i;
    cv_rrect(x, y, w, h, 2, T_LINE, bg);
    for (i = 0; i < f && i < h; i++)
        cv_rect(x, y + h - 1 - i, w, 1, mute ? T_DIM : meter_col(i, h));
}
/* HA HA HE HU: the MIXER as eight strips side by side (MX_PITCH apart), every track at once: the cushion in the track's
 * colour (a muted one DIM; REC/ARM a dot), then (Dah) the panel's height for a fader (the LEVEL, a thumb on it) beside
 * the output meter (green to red), under them the PAN a dot on a line and the REV send a bar. No name, no dB: the
 * footer names the track, the cards above give the selected one's values. The selected strip RAISE, ringed in its
 * colour; the value just turned TEXT. Each strip redrawn only when it changed (ts.col) */
#define MX_PITCH ((int32_t)(232u / (trk_count() + 1u > 5u ? trk_count() + 1u : 5u)))   /* (Dah: the project's tracks and
                                                        * the MASTER share the width: 9 x 25 .. 5 x 46) */
#define MX_W (MX_PITCH - 1)
#define MX_FX (MX_W / 2 - 9)                           /* the fader's left; the meter at MX_FX + 11 */
#define MX_X0 4
#define MX_FY 22                                       /* the fader: from 22 to MX_LOW above the panel's foot */
#define MX_LOW 44                                      /* (Dah) under it: the PAN (L .. R), the WIDTH a knob */
#define MX_FH ((int32_t)graph_h() - MX_FY - MX_LOW)
static void mixer_strip(uint32_t c)
{
    track_t *t = &trk[c];
    uint32_t sel = c == song.sel && !mx_master, lvl = trk_level(c), arm = (song.rec >> c) & 1u, sig;
    uint32_t mute = t->p[P_MUTE] != 0 || (perf_solo && !((perf_solo >> c) & 1u));   /* (Dah: another one SOLO: grey) */
    uint32_t hot = sel && ui.hot_t ? ui.hot_col + 1u : 0u, lit = (uint32_t)midi_lit(c);
    int32_t pk = t->peak, m, pan = clamp(t->p[P_PAN], -64, 63), rv = clamp(t->p[P_SPRD], -64, 127), f, mh, x;   /* (rv: WIDTH) */
    uint16_t bg = sel ? T_RAISE : T_SURF, tc = mute ? T_DIM : ux.trk[c % NTRK];
    char b[16], v[8], d[2] = {(char)('1' + c), 0};
    t->peak = 0;
    trk_short_name(c, b);
    b[3] = 0;                                          /* (three letters: a strip is 28 px) */
    m = mute ? 0 : meter_px(pk);
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;
    ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
    sig = str_hash(1u + sel + mute * 2u + arm * 4u + (uint32_t)song.playing * 8u + hot * 16u + lit * 128u, b) +
          lvl * 7919u + ts.meter[c] * 131u + (uint32_t)(pan + 128) * 104729u + (uint32_t)(rv + 128) * 1299709u + tc * 3u;
    if (!ui.force && sig == ts.col[c])
        return;
    ts.col[c] = sig;
    cv_begin(MX_W, (int32_t)graph_h(), T_BG);
    if (sel) {
        cv_rrect(0, 0, MX_W, (int32_t)graph_h(), 4, tc, T_BG);
        cv_rrect(1, 1, MX_W - 2, (int32_t)graph_h() - 2, 3, bg, tc);
    } else {
        cv_rrect(0, 0, MX_W, (int32_t)graph_h(), 4, bg, T_BG);
    }
    cv_rect(MX_W / 2 - 7, 4, 14, 13, lit ? T_TEXT : tc);  /* the cushion (Dah: square, as HOME's) */
    GFX_HOOK_ALIGN(MX_W / 2 - 7, 4, MX_W / 2 + 7, 17, AL_HV, "mixer strip digit centred");
    cv_text_in(MX_W / 2 - 7, 4 + CAP_IN(S, 13), 14, &AF_S, d, T_INK, lit ? T_TEXT : tc);
    if (arm)                                           /* REC (recording) / ARM: a dot */
        cv_rrect(MX_W - 6, 3, 4, 4, 2, song.playing ? T_REC : T_ACCENT, bg);
    (void)v;
    f = (int32_t)lvl * MX_FH / 127;                    /* LEVEL: the fader */
    cv_rrect(MX_FX + 2, MX_FY, 7, MX_FH, 3, T_LINE, bg);
    if (f > 2)
        cv_rrect(MX_FX + 2, MX_FY + MX_FH - f, 7, f, 3, tc, T_LINE);
    cv_rrect(MX_FX, MX_FY + MX_FH - f - 1 < MX_FY ? MX_FY : MX_FY + MX_FH - f - 1, 11, 3, 1, hot == 1u ? T_TEXT : T_MID, bg);
    mh = ts.meter[c] * MX_FH / (TS_MH - 2);            /* the output's meter */
    meter_up(MX_FX + 13, MX_FY, 7, MX_FH, mh, mute, bg);   /* (Dah: green to orange to red) */
    {                                                  /* (Dah) PAN: L and R, the side it leans to lit, a bar
                                                        * from the middle to it; WIDTH: a knob */
        int32_t h = (int32_t)graph_h(), kr = KNOB_SMALL_R, ky = h - 2 * kr - 3, ph = AF_S_CAP_H + 4, py = ky - 3 - ph;
        int32_t gx = 2, gw = MX_W - 4, mid = gx + gw / 2, px = gx + 1 + (pan + 64) * (gw - 3) / 127;
        uint16_t pc = hot == 2u ? T_TEXT : tc;
        cv_rrect(gx, py, gw, ph, 3, T_LINE, bg);        /* a pill: L .. R, filled from its middle */
        if (px != mid)
            cv_rect(px < mid ? px : mid, py + 1, px < mid ? mid - px : px - mid, ph - 2, ux_mix(T_LINE, pc, 45));
        cv_rect(mid, py + 1, 1, ph - 2, T_MID);
        cv_rect(px, py, 2, ph, pc);                    /* (where it stands) */
        cv_text(gx + 2, py + 2 - AF_S_CAP_Y, &AF_S, "L", pan < -2 ? T_TEXT : T_MID);
        cv_text(gx + gw - 2 - text_w(&AF_S, "R"), py + 2 - AF_S_CAP_Y, &AF_S, "R", pan > 2 ? T_TEXT : T_MID);
        {                                              /* WIDTH (Dah: as FL's STEREO SEPARATION): from 12 o'clock,
                                                        * separated to the left, merged (MONO) to the right */
            int32_t a = rv >= 0 ? -rv * KA_END / 127 : -rv * KA_END / 64;
            knob_arc(MX_W / 2 - kr, ky, kr, KNOB_SMALL_COV, KNOB_SMALL_ANG, a < 0 ? a : 0, a > 0 ? a : 0,
                     T_LINE, hot == 3u ? T_TEXT : tc, bg);
            cv_rect(MX_W / 2 - 1, ky + 1, 2, 3, T_MID);   /* (its middle: normal) */
            cv_rect(MX_W / 2 - 1, ky + kr - 1, 2, 2, rv ? tc : T_DIM);   /* (its hub) */
        }
    }
    cv_blit((uint32_t)(MX_X0 + (int32_t)(c + 1u) * MX_PITCH), graph_y());   /* (Dah: after the MASTER's) */
}
/* (Dah) the MASTER's strip before the tracks: M, the output's meter, its COMPRESSOR's reduction (ACCENT, from the top) */
static int32_t mx_mpk;
static void mixer_master(uint32_t c)
{
    int32_t pk = master_pk, m = meter_px(pk), mh, gh;
    uint16_t bg = mx_master ? T_RAISE : T_SURF;
    static uint32_t last;
    uint32_t sig;
    master_pk = 0;
    if (m < mx_mpk - 1)
        m = mx_mpk - 1;
    mx_mpk = m < 0 ? 0 : m;
    gh = clamp(cmp_master.gr * MX_FH / (18 * 8), 0, MX_FH);
    sig = (uint32_t)mx_mpk * 131u + (uint32_t)gh * 7919u + mx_master * 3u + (uint32_t)song.g[G_MCOMP] * 104729u;
    if (!ui.force && sig == last)
        return;
    last = sig;
    cv_begin(MX_W, (int32_t)graph_h(), T_BG);
    if (mx_master) {
        cv_rrect(0, 0, MX_W, (int32_t)graph_h(), 4, T_TEXT, T_BG);
        cv_rrect(1, 1, MX_W - 2, (int32_t)graph_h() - 2, 3, bg, T_TEXT);
    } else
        cv_rrect(0, 0, MX_W, (int32_t)graph_h(), 4, bg, T_BG);
    cv_rect(MX_W / 2 - 7, 4, 14, 13, T_TEXT);
    cv_text_in(MX_W / 2 - 7, 4 + CAP_IN(S, 13), 14, &AF_S, "M", T_INK, T_TEXT);
    mh = mx_mpk * MX_FH / (TS_MH - 2);
    meter_up(MX_FX + 2, MX_FY, 7, MX_FH, mh, 0, bg);   /* the output */
    cv_rrect(MX_FX + 13, MX_FY, 7, MX_FH, 2, T_LINE, bg);   /* the reduction, from the top */
    if (gh > 1)
        cv_rect(MX_FX + 14, MX_FY + 1, 5, gh, T_ACCENT);
    cv_blit((uint32_t)(MX_X0 + (int32_t)c * MX_PITCH), graph_y());
}
static void draw_tracks(void)
{
    uint32_t c, sk = strip_kind() == SK_TRK;
    if (!sk) {
        if (ui.force) {                                /* the BG between the strips only, they cover the rest (#181) */
            lcd_fill(0, graph_y(), MX_X0, (int32_t)graph_h(), T_BG);
            for (c = 0; c < NTRK; c++)
                ts.meter[c] = 0;
            for (c = 0; c <= trk_count(); c++) {       /* (Dah: the strips share the width; after the last, to the edge) */
                uint32_t x = (uint32_t)(MX_X0 + (int32_t)c * MX_PITCH + MX_W);
                if (x < 240u)
                    lcd_fill(x, graph_y(), c < trk_count() ? (uint32_t)(MX_PITCH - MX_W) : 240u - x, (int32_t)graph_h(), T_BG);
            }
        }
        for (c = 0; c < trk_count(); c++)
            mixer_strip(c);
        mixer_master(0);                               /* (Dah) the MASTER, at the left */
        return;
    }
    if (ui.force) {                                  /* the BG beside the columns only, they cover the rest (#181: */
        draw_gaps(graph_y(), graph_h());             /* the whole panel cleared first flashed on every sound load and
                                                      * track change); LINE: divided as the cards above */
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    for (c = trk_bank(); c < trk_bank() + 4u; c++) {   /* (HA HA HE HU: the selected track's bank of four) */
        track_t *t = &trk[c];
        uint32_t sel = c == song.sel, lvl = trk_level(c), lit = (uint32_t)midi_lit(c);
        uint32_t mute = t->p[P_MUTE] != 0 || (perf_solo && !((perf_solo >> c) & 1u));
        uint32_t arm = (song.rec >> c) & 1u, st = arm ? (song.playing ? 1u : 2u) : mute ? 3u : 0u, sig;
        uint32_t hot = sel && ui.hot_t ? ui.hot_col + 1u : 0u;   /* the knob just turned (hot_col + 1): 1 LEVEL, 2 PAN,
                                                                * 3 REV, 4 MUTE (its badge) */
        int32_t pk = t->peak, m, pan = clamp(t->p[P_PAN], -64, 63), rv = clamp(t->p[P_SPRD], 0, 127);   /* (separated only) */
        uint16_t vc;
        ux_tint(ux.trk[c]);                            /* (HA HA HE HU) each column in its track's colour */
        vc = mute ? T_DIM : T_THEME;
        char b[16];
        t->peak = 0;
        trk_short_name(c, b);
        m = mute ? 0 : meter_px(pk);
        if (m < ts.meter[c] - 1)
            m = ts.meter[c] - 1;                     /* falls ~2 dB a frame */
        ts.meter[c] = (uint8_t)(m < 0 ? 0 : m);
        sig = str_hash(1u + sel + st * 2u + (mute && arm) * 16u + hot * 32u, b) + lvl * 7919u +
              ts.meter[c] * 131u + (uint32_t)(pan + 128) * 104729u + (uint32_t)(rv + 128) * 1299709u + mute * 3u + sk * 0x9E37u +
              lit * 15485863u;                         /* (1.5.5: MIDI IN lights the cushion) */
        if (!ui.force && sig == ts.col[c])
            continue;
        ts.col[c] = sig;
        if (sk) {
            track_strip(c, sel, st, mute, arm, hot, lvl, b, lit);
            continue;
        }
        cv_begin(CARD_W, (int32_t)graph_h(), T_BG);
        cv_rrect(0, 0, CARD_W, (int32_t)graph_h(), 5, T_SURF, T_BG);
        cv_icon_on(4, 5, 16, trk_icon(c, sel), lit ? (sel ? T_TEXT : T_ACCENT) : sel ? T_ACCENT : T_MID, T_SURF);
        if (st)
            GFX_HOOK_ALIGN(0, 5, 0, 21, AL_V, "mixer badge on the cushion's line");
        if (st == 1u || st == 2u)                    /* REC (recording) / ARM (armed, stopped) */
            cv_keycap(53 - kc_w(st == 1u ? KC_REC : KC_ARM), 6, st == 1u ? KC_REC : KC_ARM, st == 1u ? T_REC : T_ACCENT,
                      T_INK, T_SURF);
        else if (st)                                 /* muted (K4 just turned: lit) */
            cv_keycap(53 - kc_w(KC_MUTE), 6, KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
        if (mute && arm)                             /* armed and muted: MUTE in place of the name */
            GFX_HOOK_ALIGN(0, 21 + AF_S_CAP_Y, 0, 21 + AF_S_CAP_Y + AF_S_CAP_H, AL_V,
                           "mixer MUTE badge on the name's line");
        if (mute && arm)                             /* (on the name's capitals) */
            cv_keycap(5, 21 + AF_S_CAP_Y + HALF_UP(AF_S_CAP_H - KC_H), KC_MUTE, hot == 4u ? T_ACCENT : T_KEY, T_INK, T_SURF);
        else
            cv_free_text(5, 21, &AF_S, b, mute ? T_DIM : sel ? T_TEXT : T_MID, T_SURF, CARD_W - 10);
        {   /* LEVEL: the knob, its dB inside (OFF at 0), and the meter of the output */
            char v[8];
            knob(KB_X, KB_Y, KNOB_BIG_R, KNOB_BIG_COV, KNOB_BIG_ANG, (int32_t)lvl, 0, 127, hot == 1u ? T_ACCENT : vc);
            if (lvl) {
                int32_t d = LEVEL_DB_X10[lvl];
                fmt_int(v, (d + (d < 0 ? -5 : 5)) / 10);
                GFX_HOOK_ALIGN(KB_X, 0, KB_X + 2 * KNOB_BIG_R, 0, AL_H, "mixer level text centred across");
                cv_text_in(KB_X, KB_Y + KNOB_BIG_R + 5, 2 * KNOB_BIG_R, &AF_S, "dB", T_DIM, T_SURF);
            } else {
                str_cpy(v, "OFF", sizeof v);
            }
            GFX_HOOK_ALIGN(KB_X, KB_Y, KB_X + 2 * KNOB_BIG_R, KB_Y + 2 * KNOB_BIG_R, AL_HV, "mixer level value in its knob");
            cv_text_in(KB_X, KB_Y + CAP_IN(S, 2 * KNOB_BIG_R), 2 * KNOB_BIG_R, &AF_S, v, hot == 1u ? T_ACCENT : lvl ? vc : T_DIM, T_SURF);
            cv_rrect(45, TS_MY, 4, TS_MH, 2, T_RAISE, T_SURF);
            if (ts.meter[c])
                cv_rrect(45, TS_MY + TS_MH - 1 - ts.meter[c], 4, ts.meter[c] + 1, ts.meter[c] >= 4 ? 2 : 0, T_MID, T_RAISE);
        }
        {   /* PAN (from the centre) and the REV send: captions, knobs, values */
            char v[8];
            uint16_t pc = hot == 2u ? T_ACCENT : vc, rc = hot == 3u ? T_ACCENT : vc;
            GFX_HOOK_ALIGN(15 - KNOB_SMALL_R, 0, 15 + KNOB_SMALL_R, 0, AL_H, "mixer caption over its knob");
            cv_text_in(15 - KNOB_SMALL_R, 72, 2 * KNOB_SMALL_R, &AF_S, "PAN", T_DIM, T_SURF);   /* (by their ink) */
            GFX_HOOK_ALIGN(42 - KNOB_SMALL_R, 0, 42 + KNOB_SMALL_R, 0, AL_H, "mixer caption over its knob");
            cv_text_in(42 - KNOB_SMALL_R, 72, 2 * KNOB_SMALL_R, &AF_S, "REV", T_DIM, T_SURF);
            knob(15 - KNOB_SMALL_R, KS_Y, KNOB_SMALL_R, KNOB_SMALL_COV, KNOB_SMALL_ANG, pan, -64, 63, pc);
            knob(42 - KNOB_SMALL_R, KS_Y, KNOB_SMALL_R, KNOB_SMALL_COV, KNOB_SMALL_ANG, rv, 0, 127, rc);
            v[0] = '+';                              /* the cards' numbers, without the % */
            fmt_int(pan > 0 ? v + 1 : v, pan * 100 / (pan > 0 ? 63 : 64));   /* (+100 at the top: param_format) */
            GFX_HOOK_ALIGN(15 - KNOB_SMALL_R, 0, 15 + KNOB_SMALL_R, 0, AL_H, "mixer value under its knob");
            cv_text_in(15 - KNOB_SMALL_R, KS_Y + 17, 2 * KNOB_SMALL_R, &AF_S, v, pc, T_SURF);
            fmt_int(v, (rv * 100 + 63) / 127);
            GFX_HOOK_ALIGN(42 - KNOB_SMALL_R, 0, 42 + KNOB_SMALL_R, 0, AL_H, "mixer value under its knob");
            cv_text_in(42 - KNOB_SMALL_R, KS_Y + 17, 2 * KNOB_SMALL_R, &AF_S, v, rc, T_SURF);
        }
        cv_blit((uint32_t)CARD_X(c % 4u), Y_GRAPH);
    }
    ux_tint(ux.trk[song.sel % NTRK]);                  /* (the panel's colour back: draw_graph's) */
}
/* HOME > TRACKS (1.3, Discussions #112 / #134: MENU > DISPLAY > HOME TRACKS): the four tracks as rows in the panel, one
 * under the other (29 px, 2 apart), in place of the scope; the cards above stay HOME's four (the selected track's). A row:
 * the track's cushion at the left; on its first line the engine's icon and the sound's name (MENU > LARGE: the name in M,
 * no icon), a MUTE badge at its right when muted; under it the 16 steps of the page of the track's pattern that plays
 * (stopped: the selected track's cursor page, the others' first): a note a bar (accented: taller, the accent), a TIE after
 * a note the bar carried on from the one before, DRUM hits a pad (two hits or more: two pads), an empty step or a REST a
 * stub (a TIE with nothing to carry: brighter); the step playing on a raised cell. LEN > 16: a pip per page right of the
 * steps (the page shown THEME). At the right the track's output meter. The selected track's row is the selection (THEME,
 * its ink INK, as a list's selected row); a muted track's steps DIM. Each row is its own canvas with its own signature
 * (ts.col, the MIXER's: the two never show together); a meter that moved alone redraws its slot only. LINE: the rows on
 * BG, 1 px rules between them */
#define TR_H 29                                      /* a row; rows TR_PITCH apart from Y_GRAPH, 3 .. 237 across */
#define TR_PITCH 31
#define TR_W 234
#define TR_NY(lg) ((lg) ? 2 : 4)                     /* the name line's capitals from row 4 (LARGE, M: 2) */
#define TR_SB 26                                     /* the steps: bars standing on row 25 */
#define TR_BW 9                                      /* a step's bar, 11 px apart, 4 more between groups of 4 */
#define TR_PX 216                                    /* the page pips */
#define TR_MX 226                                    /* the meter: 4 x 22 from row 4 */
#define TR_MY 4
#define TR_MH 22
static int32_t tr_bx(uint32_t i) { return 24 + (int32_t)i * 11 + (int32_t)(i / 4u) * 4; }
static void tr_meter(int32_t x, int32_t y, uint32_t c, uint16_t slot, uint16_t fill, uint16_t under)
{
    int32_t m = ts.meter[c] * TR_MH / (TS_MH - 2);
    cv_rrect(x, y, 4, TR_MH, 2, slot, under);
    if (m)
        cv_rrect(x, y + TR_MH - m, 4, m, m >= 4 ? 2 : 0, fill, slot);
}
static void home_track_row(uint32_t c)
{
    track_t *t = &trk[c];
    const step_t *s = seq_steps(t);
    uint32_t sel = c == song.sel, mute = t->p[P_MUTE] != 0, large = (ui_prefs & PREF_LARGE) != 0u, lit = (uint32_t)midi_lit(c);
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), np = (len + 15u) / 16u, pg, p0, ph, i, sig, carry = 0;
    int32_t y = Y_GRAPH + (int32_t)(c & 3u) * TR_PITCH, m, x, xr = 222, ch = large ? AF_M_CAP_H : AF_S_CAP_H;
    uint16_t bg = sel ? T_THEME : T_SURF, half = ux_mix(T_THEME, T_INK, 35);
    uint16_t cn = sel ? (mute ? ux_mix(T_THEME, T_INK, 55) : T_INK) : mute ? T_DIM : T_THEME;   /* a note */
    uint16_t ca = sel || mute ? cn : T_ACCENT, cs = sel ? half : T_RAISE;   /* accented; a stub */
    uint16_t cp = sel ? ux_mix(T_THEME, T_INK, 20) : T_RAISE, cpl = sel ? T_INK : mute ? T_MID : T_TEXT;   /* playing */
    char b[16];
    pg = song.playing ? t->seq_idx / 16u : sel ? ui.cursor / 16u : 0u;
    if (pg >= np)
        pg = np - 1u;
    p0 = pg * 16u;
    ph = song.playing && t->seq_idx / 16u == pg ? t->seq_idx % 16u : 0xFFu;
    trk_short_name(c, b);
    m = mute ? 0 : meter_px(t->peak);
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;                         /* (falls as the MIXER's) */
    sig = str_hash(1u + sel + mute * 2u + large * 4u + t->eng_req * 8u, b) + steps_hash(t) * 31u + pg * 7919u +
          ph * 104729u + len * 1299709u + lit * 15485863u;   /* (1.5.5: MIDI IN lights the cushion) */
    if (!ui.force && sig == ts.col[c]) {
        if (m != ts.meter[c]) {                      /* the meter alone: its slot */
            ts.meter[c] = (uint8_t)m;
            cv_begin(4, TR_MH, bg);
            tr_meter(0, 0, c, cs, sel ? T_INK : T_MID, bg);
            cv_blit(3 + TR_MX, (uint32_t)(y + TR_MY));
        }
        return;
    }
    ts.col[c] = sig;
    ts.meter[c] = (uint8_t)m;
    cv_begin(TR_W, TR_H, T_BG);
    cv_rrect(0, 0, TR_W, TR_H, 5, bg, T_BG);
    cv_icon_mid(4, TR_H / 2, 16, trk_icon(c, 1), lit ? (sel ? half : T_ACCENT) : sel ? T_INK : mute ? T_DIM : T_MID, bg);
    if (mute) {                                      /* MUTE: its badge at the name line's end */
        xr -= kc_w(KC_MUTE);
        GFX_HOOK_ALIGN(0, TR_NY(large), 0, TR_NY(large) + ch, AL_V, "tracks row badge on the name's line");
        cv_keycap(xr, TR_NY(large) + (ch - KC_H) / 2, KC_MUTE, sel ? T_INK : T_KEY, sel ? T_THEME : T_INK, bg);
        xr -= 6;
    }
    x = 24;
    if (FELUCCA_ICONS && !large) {                   /* the engine's icon on the name's capitals */
        uint32_t ei = engine_icon(ENGINES[t->eng_req % NENGINES]->name);
        if (ei != ICON_NONE)
            GFX_HOOK_ALIGN(0, TR_NY(large), 0, TR_NY(large) + ch, AL_V, "tracks row icon on the name's line");
        x += cv_icon_in(x, TR_NY(large), 0, ch, 12, ei, sel ? T_INK : T_MID, bg) + 5;
    }
    cv_free_text(x, TR_NY(large) - (large ? AF_M_CAP_Y : AF_S_CAP_Y), large ? &AF_M : &AF_S, b, sel ? T_INK : mute ? T_DIM : T_TEXT,
                 bg, xr - x);
    for (i = p0; i-- > 0u;)                          /* a TIE at the page's start: carried from a note before it? */
        if (s[i].time != ST_TIE) {
            carry = s[i].time == ST_NOTE && s[i].n;
            break;
        }
    for (i = 0; i < 16u && p0 + i < len; i++) {
        const step_t *st = &s[p0 + i];
        int32_t bx = tr_bx(i);
        uint16_t under = i == ph ? cp : bg;
        if (i == ph)                                 /* the step playing: a raised cell */
            cv_rrect(bx - 1, 15, TR_BW + 2, 13, 2, cp, bg);
        if (st->time == ST_TIE && carry) {           /* carried on: joined to the bar before */
            int32_t x0 = i ? tr_bx(i - 1u) + TR_BW : bx - 2;
            cv_rect(x0, TR_SB - 7, bx + 2 - x0, 7, i == ph ? cpl : cn);
            cv_rrect(bx, TR_SB - 7, TR_BW, 7, 2, i == ph ? cpl : cn, under);
        } else if (st->time == ST_NOTE && st->n) {   /* notes: a bar, accented taller */
            int acc = (st->flags & SF_ACCENT) != 0u;
            cv_rrect(bx, TR_SB - (acc ? 10 : 7), TR_BW, acc ? 10 : 7, 2, i == ph ? cpl : acc ? ca : cn, under);
            carry = 1;
            continue;
        } else if (st->time == ST_NOTE && st->hit) { /* DRUM hits: a pad, two for two hits or more */
            uint16_t pc = i == ph ? cpl : (st->acc & st->hit) ? ca : cn;
            cv_rrect(bx, TR_SB - 4, TR_BW, 4, 1, pc, under);
            if (st->hit & (st->hit - 1u))
                cv_rrect(bx, TR_SB - 9, TR_BW, 4, 1, pc, under);
        } else {                                     /* empty, a REST, a TIE carrying nothing */
            cv_rect(bx, TR_SB - 2, TR_BW, 2, i == ph ? (sel ? T_INK : T_MID) : st->time == ST_TIE ? (sel ? T_INK : T_MID) : cs);
        }
        carry = st->time == ST_TIE && carry;
    }
    if (np > 1u)                                     /* LEN > 16: the pages, top to bottom */
        for (i = 0; i < np; i++)
            cv_rect(TR_PX, TR_SB - 3 * (int32_t)(np - i) + 1, 3, 2, i == pg ? (sel ? T_INK : T_THEME) : cs);
    tr_meter(TR_MX, TR_MY, c, cs, sel ? T_INK : T_MID, bg);
    cv_blit(3, (uint32_t)y);
}
/* HA HA HE HU: HOME's TRACKS as a channel rack, the eight tracks at once: the track's cushion in its colour (a muted
 * one DIM), the steps of the page playing (the selected track's: the cursor's) in the track's colour (accented TEXT,
 * a tie fainter, empty a stub; the step playing its column lit), the output meter. Each row redrawn only when it
 * changed (ts.col), its meter alone otherwise.
 * (Dah) The selected track stands out: its row tinted with its colour, framed in it, a band of it at the left, its
 * number on it; the others' numbers in their colour, their steps a little fainter. Square, the rows filling the panel
 * to its foot, each with the 1 px gap under it */
#define RK_SW (RK_MX - 3 - RK_SX)                      /* (Dah) the steps' width, spread over it */
#define RK_SX 21                                       /* the steps: x 21 .. 222 (Dah: no pattern number, the header says it) */
#define RK_MX 226                                      /* the meter: upright, x 226 .. 229, the row's height */
#define RK_MW 4
/* (Dah) track t's view: its first step shown (p0) and the column playing (ph, 0xFF none). FOLLOW, playing: the step
 * playing goes from the left to the middle, the steps then go by under it, at the track's end on to the right (as
 * STEP's); else HOME's VIEW (hv_base) */
static void rk_view(const track_t *t, int sel, uint32_t *p0, uint32_t *ph)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), nc = hv_cols();
    if (song.playing && !hvw.nofollow) {
        uint32_t si = t->seq_idx % len;
        *p0 = si > nc / 2u ? si - nc / 2u : 0u;
        if (len > nc && *p0 + nc > len)
            *p0 = len - nc;
        if (len <= nc)
            *p0 = 0;
        *ph = si - *p0;
    } else {
        *p0 = hv_base(t, sel);
        *ph = song.playing && t->seq_idx >= *p0 && t->seq_idx < *p0 + nc ? t->seq_idx - *p0 : 0xFFu;
    }
}
#define RK_RH 14                                       /* (Dah) the step ruler over the rows */
static int rk_sel(void) { return !mx_master && song.sel < trk_count() ? (int)song.sel : -1; }
/* row c's top (panel y) and height (the gap under it not counted) */
static void rk_geo(uint32_t c, int32_t *y, int32_t *h)
{
    int32_t tot = (int32_t)graph_h() - RK_RH, p = tot / (int32_t)NTRK, yy = (int32_t)c * p;
    *y = (int32_t)graph_y() + RK_RH + yy;
    *h = p - 1;                                         /* (the rows alike; what is left under the last: BG) */
}
/* the output meter, upright: rising from the row's foot (x0: its left in the canvas) */
static void rack_meter_at(int32_t x0, int32_t rh, int32_t m, uint16_t bg)
{
    int32_t hh = rh - 4;
    meter_up(x0, 2, RK_MW, hh, m * hh / (TS_MH - 2), 0, bg);
}
/* (Dah) a row's drawing, shifted ox px to the left into a canvas of w (a whole row: 0, TR_W; a column of it: the
 * step's): what falls outside it is clipped. The state the steps carry (a TIE, a chord's lines) is walked from the
 * view's start whatever is drawn */
typedef struct {
    const step_t *s;
    uint32_t c, len, p0, ph, nc, sel, mute, off;
    int32_t rh;
    uint16_t tc0, tc, bg;
} rk_row_t;
static void rk_paint(const rk_row_t *R, int32_t ox)
{
    const step_t *s = R->s, *src = 0;
    uint32_t i, carry = 0, len = R->len, p0 = R->p0, nc = R->nc;
    int32_t rh = R->rh, sp = RK_SW / (int32_t)nc, cw, top = 2, sh = rh - 4;
    uint16_t bg = R->bg, tc = R->tc, tc0 = R->tc0;
    char d[2] = {(char)('1' + R->c), 0};
    if (R->sel) {                                      /* (Dah) the selected: framed in its colour, a band of it at
                                                         * the left, its corners round */
        cv_rect(-ox, 0, TR_W, rh, T_BG);
        cv_rrect(-ox, 0, TR_W, rh, -5, tc0, T_BG);
        cv_rrect(-ox + 3, 1, TR_W - 4, rh - 2, -4, bg, tc0);
    } else
        cv_rect(-ox, 0, TR_W, rh, bg);                 /* (the others square, as the rest) */
    if (ox < 20) {   /* the number: the selected's on its colour (INK), the others' in their colour on the row */
        int32_t cx = 5 - ox, cyy = (rh - 13) / 2;
        uint16_t nc0 = R->mute || R->off ? T_DIM : tc0;
        if (R->sel)
            cv_rect(cx, cyy, 13, 13, nc0);
        GFX_HOOK_ALIGN(cx, cyy, cx + 13, cyy + 13, AL_HV, "rack cushion digit centred");
        cv_text_in(cx, cyy + CAP_IN(S, 13), 13, &AF_S, d, R->sel ? T_INK : nc0, R->sel ? nc0 : bg);
    }
    for (i = 1; i <= p0; i++) {                        /* a TIE at the view's start: carried from a note before it? */
        const step_t *q = &s[(p0 + len - i) % len];
        if (q->time != ST_TIE) {
            carry = q->time == ST_NOTE && (q->n || q->hit);
            if (carry && s[p0 % len].time == ST_TIE)
                src = q;                                /* (Dah: its notes, every one, carried into the view) */
            break;
        }
    }
    {   /* (Dah) a melodic track: a mini piano roll, each note at its height between the view's lowest and highest
         * (a chord: its notes), a TIE on at the height carried; DRUM: its hits as blocks */
        int32_t lo = 127, hi = 0, cy[4] = {0, 0, 0, 0}, ncy = 1, j, nh = 3;
        int roll = !drum_track(&trk[R->c % NTRK]);
        cy[0] = top + sh / 2 - 1;
        for (i = 0; roll && i <= nc; i++) {             /* (i == nc: the chord carried in) */
            const step_t *q = i < nc ? (p0 + i < len ? &s[p0 + i] : 0) : src;
            for (j = 0; q && j < q->n && j < 4; j++) {
                lo = q->note[j] < lo ? q->note[j] : lo;
                hi = q->note[j] > hi ? q->note[j] : hi;
            }
        }
        if (roll && src && src->n && hi >= lo) {      /* (Dah) a chord held into the view: each of its lines */
            for (j = 0; j < src->n && j < 4; j++)
                cy[j] = hi > lo ? top + sh - nh - (src->note[j] - lo) * (sh - nh) / (hi - lo) : top + sh / 2 - 1;
            ncy = j;
        }
        for (i = 0; i < nc && p0 + i < len; i++) {
            const step_t *st = &s[p0 + i];
            int32_t bx = RK_SX + (int32_t)i * RK_SW / (int32_t)nc - ox;
            uint16_t under = bg;
            cw = RK_SX + (int32_t)(i + 1u) * RK_SW / (int32_t)nc - ox - bx - (sp > 3 ? 2 : 1);   /* (Dah: the steps
                                                         * spread over the row's width) */
            if (i == R->ph) {                          /* the step playing: its column lit (Dah: no ring) */
                under = ux_mix(bg, T_TEXT, 22);
                cv_rect(bx - 1, top - 1, cw + 2, sh + 2, under);
            }
            if (st->time == ST_NOTE && (st->n || st->hit)) {
                int acc = (st->flags & SF_ACCENT) || (st->acc & st->hit);
                if (roll && st->n && hi >= lo) {
                    for (j = 0; j < st->n && j < 4; j++) {
                        int32_t ny = hi > lo ? top + sh - nh - (st->note[j] - lo) * (sh - nh) / (hi - lo) : top + sh / 2 - 1;
                        cv_rect(bx, ny, cw, nh, acc && !R->mute ? T_TEXT : tc);
                        cy[j] = ny;
                    }
                    ncy = j;
                } else
                    cv_rrect(bx, top + 1, cw, sh - 2, 2, acc && !R->mute ? T_TEXT : tc, under);
                carry = 1;
                continue;
            }
            if (st->time == ST_TIE && carry) {         /* carried on: fainter (the roll: each note at its height) */
                for (j = 0; j < (roll ? ncy : 1); j++)
                    cv_rect(bx - 1, roll ? cy[j] : top + sh / 2 - 1, cw + 2, roll ? nh : 2, ux_mix(under, tc, 55));
            } else
                cv_rect(bx, top + sh - 1, cw, 1, T_LINE);
            carry = st->time == ST_TIE && carry;
        }
    }
}
static struct { uint32_t base[NTRK]; uint16_t p0[NTRK]; uint8_t ph[NTRK]; } rkc;   /* (Dah) each row as drawn */
static void rack_row(uint32_t c)
{
    track_t *t = &trk[c];
    rk_row_t R;
    uint32_t i, sig, base;
    int32_t y, m;
    char b[16];
    R.s = seq_steps(t);
    R.c = c;
    R.sel = (int)c == rk_sel();
    R.mute = t->p[P_MUTE] != 0;
    R.len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    R.nc = hv_cols();
    R.off = (seq_off >> (c % NTRK)) & 1u && !chain.running;   /* (Dah: on no pattern: its steps dim) */
    R.tc0 = ux.trk[c % NTRK];
    R.tc = R.mute || R.off ? T_DIM : R.sel ? R.tc0 : ux_mix(T_SURF, R.tc0, 72);
    R.bg = R.sel ? ux_mix(T_SURF, R.tc0, 16) : T_SURF;
    rk_geo(c, &y, &R.rh);
    rk_view(t, (int)R.sel, &R.p0, &R.ph);              /* (Dah: where, how many steps across) */
    trk_short_name(c, b);
    m = R.mute ? 0 : meter_px(t->peak);
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;
    base = str_hash(1u + R.sel + R.mute * 2u + t->eng_req * 8u, b) + steps_hash(t) * 31u + R.p0 * 7919u +
           R.len * 1299709u + R.tc * 3u + R.bg * 5u + R.nc * 4099u + R.off * 7u + (uint32_t)(y * 131 + R.rh) * 2654435761u;
    sig = base + R.ph * 104729u;
    if (!ui.force && sig != ts.col[c] && base == rkc.base[c] && R.p0 == rkc.p0[c]) {
        /* (Dah) only the step playing moved: its column and the one it left, nothing else sent */
        uint32_t cols[2] = {rkc.ph[c], R.ph}, k;
        for (k = 0; k < 2u; k++) {
            int32_t x0, x1;
            if (cols[k] >= R.nc)
                continue;
            x0 = RK_SX + (int32_t)cols[k] * RK_SW / (int32_t)R.nc - 1;
            x1 = RK_SX + (int32_t)(cols[k] + 1u) * RK_SW / (int32_t)R.nc;
            cv_begin((uint32_t)(x1 - x0), (uint32_t)R.rh, R.bg);
            cv_oy = 0;
            rk_paint(&R, x0);
            cv_blit((uint32_t)(3 + x0), (uint32_t)y);
        }
        ts.col[c] = sig;
        rkc.ph[c] = (uint8_t)R.ph;
    }
    if (!ui.force && sig == ts.col[c]) {
        if (m != ts.meter[c]) {                        /* the meter alone */
            ts.meter[c] = (uint8_t)m;
            cv_begin(RK_MW, (uint32_t)R.rh - 2u, R.bg);   /* (Dah: inside the row, its frame untouched) */
            cv_oy = 0;
            meter_up(0, 1, RK_MW, R.rh - 4, m * (R.rh - 4) / (TS_MH - 2), 0, R.bg);
            cv_blit(3 + RK_MX, (uint32_t)y + 1u);
        }
        return;
    }
    ts.col[c] = sig;
    rkc.base[c] = base;
    rkc.p0[c] = (uint16_t)R.p0;
    rkc.ph[c] = (uint8_t)R.ph;
    ts.meter[c] = (uint8_t)m;
    cv_begin(TR_W, (uint32_t)R.rh + 1u, T_BG);         /* (its gap under it, BG) */
    rk_paint(&R, 0);
    rack_meter_at(RK_MX, R.rh, ts.meter[c], R.bg);
    cv_blit(3, (uint32_t)y);
    (void)i;
}
/* (Dah) a row past the project's tracks: the first says how to add one, the others empty (BG) */
static void rack_spare(uint32_t c)
{
    int32_t y, rh;
    uint32_t sig;
    rk_geo(c, &y, &rh);
    sig = 0x5A5Au + (uint32_t)(y * 131 + rh) * 2654435761u + (c == trk_count()) * 7u;
    if (!ui.force && sig == ts.col[c])
        return;
    ts.col[c] = sig;
    cv_begin(TR_W, (uint32_t)rh + 1u, T_BG);
    if (c == trk_count()) {
        cv_rrect(0, 0, TR_W, rh, 3, T_LINE, T_BG);
        cv_rrect(1, 1, TR_W - 2, rh - 2, 2, T_BG, T_LINE);
        GFX_HOOK_ALIGN(0, 0, 0, rh, AL_V, "rack add row on its line");
        cv_free_hint(18, CAP_IN(S, rh), "[ALGO] TURN RIGHT: ADD A TRACK", T_DIM, T_BG, TR_W - 24);   /* (Dah: what to do, said) */
    }
    cv_blit(3, (uint32_t)y);
}
/* (Dah) the step ruler over the rows, the selected track's view: on top the whole track (a line) with the window shown
 * on it (a bar: where the view is, how much of it) and the step playing (a mark); under it a tick a step, longer a
 * beat; the step playing's number (the accent) over its column, nothing else written */
static void rack_ruler(void)
{
    static uint32_t rsig;
    const track_t *t = &trk[rk_sel() >= 0 ? (uint32_t)rk_sel() : 0u];
    uint32_t p0, ph, i, nc = hv_cols(), len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), sig, vn;
    int32_t x0, x1;
    char b[4];
    rk_view(t, 1, &p0, &ph);
    sig = p0 * 7u + ph * 131u + nc * 4099u + len * 104729u + (uint32_t)graph_y() * 3u + ux.gen;
    if (!ui.force && sig == rsig)
        return;
    rsig = sig;
    cv_begin(TR_W, RK_RH, T_BG);
    vn = p0 + nc < len ? nc : len - p0;                /* the overview: the track, the window on it */
    cv_rect(RK_SX, 1, RK_SW, 1, T_LINE);
    x0 = RK_SX + (int32_t)p0 * RK_SW / (int32_t)len;
    x1 = RK_SX + (int32_t)(p0 + vn) * RK_SW / (int32_t)len;
    cv_rect(x0, 0, x1 - x0 > 2 ? x1 - x0 : 2, 3, T_MID);
    if (ph != 0xFFu)
        cv_rect(RK_SX + (int32_t)(p0 + ph) * RK_SW / (int32_t)len, 0, 2, 3, T_ACCENT);
    for (i = 0; i < nc && p0 + i < len; i++) {         /* the ticks */
        int32_t x = RK_SX + (int32_t)i * RK_SW / (int32_t)nc, beat = (p0 + i) % 4u == 0u;
        cv_rect(x, RK_RH - (beat ? 3 : 1), 1, beat ? 3 : 1, beat ? T_MID : T_LINE);
    }
    if (ph != 0xFFu) {                                 /* the step playing: its number, where it lands */
        int32_t bx = RK_SX + (int32_t)ph * RK_SW / (int32_t)nc, sp = RK_SW / (int32_t)nc, w, ib[4];
        int32_t cw = RK_SX + (int32_t)(ph + 1u) * RK_SW / (int32_t)nc - bx - (sp > 3 ? 2 : 1);
        int32_t cx2 = 2 * bx + cw;                      /* (twice the lit column's middle, as the rows draw it) */
        fmt_int(b, (int32_t)(p0 + ph) + 1);
        w = text_w(&AF_S, b);
        text_ink(&AF_S, b, ib);                         /* (Dah: its ink centred on the column) */
        x0 = clamp((cx2 - (ib[0] + ib[2])) / 2, 0, TR_W - w);
        cv_rect(x0 - 1, 4, w + 2, RK_RH - 4, T_BG);
        cv_text(x0, 4 - AF_S_CAP_Y, &AF_S, b, T_ACCENT);
    }
    cv_blit(3, graph_y());
}
static void draw_home_tracks(void)
{
    uint32_t c;
    if (ui.force) {                                    /* the BG either side of the rows, they cover the rest (#181) */
        int32_t p = ((int32_t)graph_h() - RK_RH) / (int32_t)NTRK, used = RK_RH + p * (int32_t)NTRK;
        lcd_fill(0, (int32_t)graph_y(), 3, (int32_t)graph_h(), T_BG);
        lcd_fill(3 + TR_W, (int32_t)graph_y(), 240 - 3 - TR_W, (int32_t)graph_h(), T_BG);
        lcd_fill(3, (int32_t)graph_y() + used, TR_W, (int32_t)graph_h() - used, T_BG);   /* (under the last row) */
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
    }
    rack_ruler();
    for (c = 0; c < NTRK; c++)
        if (c < trk_count())
            rack_row(c);
        else
            rack_spare(c);
}
static void draw_home_tracks_old(void)
{
    uint32_t c;
    if (ui.force) {                                  /* the BG round the rows only, they cover the rest (#181) */
        lcd_fill(0, Y_GRAPH, 3, (int32_t)graph_h(), T_BG);
        lcd_fill(3 + TR_W, Y_GRAPH, 240 - 3 - TR_W, (int32_t)graph_h(), T_BG);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
        for (c = 1; c < 4u; c++) {                   /* the 2 rows between two rows; LINE: a rule on the first */
            uint32_t y = (uint32_t)(Y_GRAPH + (int32_t)c * TR_PITCH - 2);
            if (ux.style) {
                lcd_fill(3, y, TR_W, 1, T_RULE);
                GFX_HOOK_RULE(3, y, TR_W, 1);
            }
            lcd_fill(3, ux.style ? y + 1u : y, TR_W, ux.style ? 1u : 2u, T_BG);
        }
    }
    for (c = trk_bank(); c < trk_bank() + 4u; c++)   /* (HA HA HE HU: the selected track's bank of four) */
        home_track_row(c);
}
/* oscilloscope of the output, triggered on a rising zero crossing: a RAISE centre line, the trace 2 px (in silence
 * a flat line on it; MENU > SCREEN OFF keeps it from staying on the panel for hours) */
static void graph_scope(uint16_t c)
{
    int16_t *snap = ui_tmp;                          /* (Dah: fx.c's scratch) */
    uint32_t w = scope_w, i, trig = 0;
    int32_t fh = cv_split ? cv_split_h : (int32_t)cv_h, cy = fh / 2, py = cy, x, peak = 1500, a = fh >= (int32_t)H_GRAPH ? 46 : cy - 6;   /* (LARGE: the strip) */
    for (i = 0; i < SCOPE_N; i++) {
        snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (snap[i] > peak)
            peak = snap[i];
        else if (-snap[i] > peak)
            peak = -snap[i];
    }
    for (i = 1; i < SCOPE_N - 240u; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0) {
            trig = i;
            break;
        }
    cv_rect(PANEL_X0, cy, PANEL_W, 1, T_RAISE);
    for (x = 0; x < PANEL_W; x++) {
        int32_t y = cy - snap[trig + (uint32_t)x] * a / peak;    /* auto-scaled */
        if (x)
            cv_line_t(PANEL_X0 - 1 + x, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
}

/* SONG is a playing order of the four stored project patterns. Letters match
 * PROJECT A..D; loading a project still restores its sound, SONG borrows steps. */
/* SONG as a timeline (song_layout), four tall lanes (T1 at the top, each led by its track's cushion, the selected
 * lane's the accent's) over a thin bar ruler: in a lane, the sections that play the same slot one after the other
 * make one block (RAISE, the slot's letter top-left; a slot not saved only outlined, DIM). In a block the picture of
 * the track's pattern in that slot (project.c proj_density, read with the slot: graph_pden): a bar a cell, as tall as
 * the cell is busy (a mean over its steps: rests 0, ties 1, notes / hits 2, accents 3; an empty cell a faint
 * baseline), the pattern looped over the section as it plays and a notch between the section's passes (its REPS). A
 * silent section ("-") is an empty lane with a faint line. The sections' boundaries are faint lines across the lanes
 * (seen between the blocks). The cell selected (KNOB 1 the section, KNOB 2 the lane) is THEME, its bars and letter
 * INK (a slot not saved: outlined THEME 2 px; "-": a THEME outline in the gap); + ADD's slot dashed after the last
 * section. Playing: the playhead across the lanes down to the ruler (ACCENT; INK on THEME), the playing section's
 * blocks filled up to it (RAISE -> ACCENT). The ruler: a tick a bar, longer every 4 with the bar's number. Under it
 * one line (M: the selected cell's detail): the selected cell's project name (NOT SAVED, SILENT; on + ADD the hint, S as
 * every key hint) and, right, the song's length in bars where it fits. MENU > LARGE: the letters in M too (the ruler's
 * numbers stay S) */
#define SG_PAD 1                                       /* a block's inner margin: the letter's, the picture's */
#define SG_PH (SG_LH - 2 * SG_PAD)                     /* the picture's height (8 px: 3 levels) */
#define SG_TX 5                                        /* the lanes' cushions (16 px) */
#define SG_IY 119                                      /* the line under the ruler: its capitals end on row 118 */
static void sg_dash(int32_t bx, int32_t bw, int32_t lo, int32_t hi, uint16_t c)   /* + ADD: dashed, clipped lo .. hi */
{
    int32_t k;
    for (k = 0; k < bw; k++)
        if (!((k >> 1) & 1) && bx + k >= lo && bx + k < hi) {
            cv_rect(bx + k, SG_Y, 1, 1, c);
            cv_rect(bx + k, SG_Y + SG_H - 1, 1, 1, c);
        }
    for (k = 0; k < SG_H; k++)
        if (!((k >> 1) & 1)) {
            if (bx >= lo) cv_rect(bx, SG_Y + k, 1, 1, c);
            if (bx + bw <= hi) cv_rect(bx + bw - 1, SG_Y + k, 1, 1, c);
        }
}
/* an outline of w x h at x, y, n px thick, clipped to lo .. hi (its ends only where not clipped) */
static void sg_frame(int32_t bx, int32_t bw, int32_t y, int32_t h, uint32_t n, int32_t lo, int32_t hi, uint16_t c)
{
    int32_t x0 = bx > lo ? bx : lo, x1 = bx + bw < hi ? bx + bw : hi;
    uint32_t k;
    if (x1 <= x0)
        return;
    for (k = 0; k < n; k++) {
        cv_rect(x0, y + (int32_t)k, x1 - x0, 1, c);
        cv_rect(x0, y + h - 1 - (int32_t)k, x1 - x0, 1, c);
        if (x0 == bx) cv_rect(bx + (int32_t)k, y, 1, h, c);
        if (x1 == bx + bw) cv_rect(bx + bw - 1 - (int32_t)k, y, 1, h, c);
    }
}
/* one lane's drawing state: the screen spans of the cell selected and of the playing section's part played */
typedef struct { int32_t c0, c1, p0, p1; uint16_t pf; } sg_fill_t;
static uint16_t sg_fill_at(const sg_fill_t *F, int32_t x)   /* the block's fill under column x */
{
    return x >= F->c0 && x < F->c1 ? T_THEME : x >= F->p0 && x < F->p1 ? F->pf : T_RAISE;
}
/* the pattern's number at x, its capitals from the lane's top; 0: no room before x1 */
static int32_t sg_letter(int32_t x, int32_t x1, int32_t y, uint32_t s, uint16_t ink, uint16_t bg)
{
    const aafont_t *f = &AF_S;                         /* (HA HA HE HU: S, the lanes are thin; a number now) */
    char a[4];
    fmt_int(a, (int32_t)s);
    int32_t w = text_w(f, a);
    if (x + w + SG_PAD > x1)
        return 0;
    cv_text_on(x, y + HALF_UP(SG_LH - AF_S_CAP_H) - AF_S_CAP_Y, f, a, ink, bg);   /* (Dah: in the lane's middle) */
    return w;
}
/* the picture of slot s's track k over sections i .. j - 1 (columns x0 .. x1 on screen), but for the letters'
 * columns (skip0 .. skip1, skip2 .. skip3) */
static void sg_picture(const song_lay_t *L, int32_t off, uint32_t k, uint32_t i, uint32_t j, uint32_t s, int32_t x0,
                       int32_t x1, const sg_fill_t *F, const int32_t *skip)
{
    int32_t y = SG_LY(k), yb = y + SG_PAD + SG_PH, x;
    uint32_t q, plen, n, den;
    (void)sg_pat(k, s, &plen, &den, &n);
    if (!plen || !n)
        return;
    for (q = i; q < j; q++) {
        int32_t sx = SG_X0 + L->x[q] - off, wt = L->x[q + 1u] - L->x[q] - SG_GAP, a, b;
        uint32_t reps = CHAIN_REP(chain_config.row[q]) ? CHAIN_REP(chain_config.row[q]) : 1u, unit = song_unit(&chain_config.row[q]);
        uint32_t tot = reps * unit, pc = 0xFFFFFFFFu, pl = 0xFFFFFFFFu, pp = 0xFFFFFFFFu, gaps, notch;
        if (wt < 1)
            continue;
        a = sx > x0 ? sx : x0;
        b = q + 1u == j ? sx + wt : sx + wt + SG_GAP;  /* (inside a run: on to the next section) */
        b = b < x1 ? b : x1;
        gaps = sg_muldiv((uint32_t)wt, plen, tot * n) >= 3u;   /* a cell 3 px or more: a gap between two */
        notch = reps > 1u && (uint32_t)wt / reps >= 4u;        /* the passes far enough apart: a notch between */
        for (x = sx; x < b; x++) {
            uint32_t r = (uint32_t)(x - sx), u = sg_muldiv(r < (uint32_t)wt ? r : (uint32_t)wt - 1u, tot, (uint32_t)wt);
            uint32_t pos = u % plen, cell = pos * n / plen, pass = u / unit, h;
            int edge = cell != pc || u / plen != pl, cut = notch && pass != pp && x > sx;
            uint16_t fill, bar;
            pc = cell;
            pl = u / plen;
            pp = pass;
            if (x < a || (x >= skip[0] && x < skip[1]) || (x >= skip[2] && x < skip[3]))
                continue;
            fill = sg_fill_at(F, x);
            bar = fill == T_THEME ? T_INK : T_THEME;
            if (cut) {                                 /* a pass begins: the notch */
                cv_rect(x, y + SG_PAD, 1, SG_PH, T_SURF);
                continue;
            }
            if (gaps && edge && x > sx)
                continue;
            h = (den >> (2u * cell)) & 3u;
            if (h)
                cv_rect(x, yb - (int32_t)(h * SG_PH / 3u), 1, (int32_t)(h * SG_PH / 3u), bar);
            else
                cv_rect(x, yb - 1, 1, 1, ux_mix(fill, bar, 30));
        }
    }
}
/* lane k's block of sections i .. j - 1 on slot s; c: the cursor's section when it is in this lane, else past it;
 * sp: the playhead on screen (-100: none) */
static void sg_block(const song_lay_t *L, int32_t off, uint32_t k, uint32_t i, uint32_t j, uint32_t s, uint32_t c,
                     int32_t sp)
{
    int32_t hi = SG_X0 + SG_W, y = SG_LY(k), bx = SG_X0 + L->x[i] - off, bw = L->x[j] - L->x[i] - SG_GAP, lx, w;
    int32_t x0 = bx > SG_X0 ? bx : SG_X0, x1 = bx + bw < hi ? bx + bw : hi, skip[4] = {0, 0, 0, 0};
    uint32_t ql, qd, qn;
    int saved = sg_pat(k, s, &ql, &qd, &qn), cur = c >= i && c < j;
    sg_fill_t F = {0, 0, 0, 0, 0};
    uint32_t p = chain.running && chain.row >= i && chain.row < j ? chain.row : 0xFFu;
    if (x1 <= x0)
        return;
    if (!saved) {                                      /* not saved: an outline, the letter DIM */
        if (cur) {
            int32_t cx = SG_X0 + L->x[c] - off, cw = L->x[c + 1u] - L->x[c] - SG_GAP;
            sg_frame(bx, bw, y, SG_LH, 1, SG_X0, hi, T_DIM);
            sg_frame(cx, cw, y, SG_LH, 2, SG_X0, hi, T_THEME);
            sg_letter((cx > SG_X0 ? cx : SG_X0) + SG_PAD, x1, y, s, T_THEME, T_SURF);
            return;
        }
        sg_frame(bx, bw, y, SG_LH, 1, SG_X0, hi, T_DIM);
        sg_letter(x0 + SG_PAD, x1, y, s, T_DIM, T_SURF);
        return;
    }
    cv_rrect(x0, y, x1 - x0, SG_LH, x0 != bx || x1 != bx + bw ? 0 : 2, T_RAISE, T_SURF);
    if (p != 0xFFu && sp >= SG_X0) {                   /* playing here: the part played */
        int32_t a = SG_X0 + L->x[p] - off;
        F.pf = ux_mix(T_RAISE, T_ACCENT, 35);
        F.p0 = a > x0 ? a : x0;
        F.p1 = sp + 2 < x1 ? sp + 2 : x1;              /* (its right corners under the playhead) */
        if (F.p1 > F.p0) {                             /* (no cell of its own: the letter may cross its end) */
            int32_t e = F.p0 == bx && ux.style != ST_LINE && F.p1 - F.p0 > 2 ? 2 : 0;   /* the block's rounded end */
            cv_rect(F.p0 + e, y, F.p1 - F.p0 - e, SG_LH, F.pf);
            if (e) {
                cv_rect(F.p0, y + 2, 1, SG_LH - 4, F.pf);
                cv_rect(F.p0 + 1, y + 1, 1, SG_LH - 2, F.pf);
            }
        }
    }
    if (cur) {                                         /* the cell selected */
        int32_t cx = SG_X0 + L->x[c] - off, cw = L->x[c + 1u] - L->x[c] - SG_GAP;
        F.c0 = cx > SG_X0 ? cx : SG_X0;
        F.c1 = cx + cw < hi ? cx + cw : hi;
        if (F.c1 > F.c0)
            cv_rrect(F.c0, y, F.c1 - F.c0, SG_LH, j - i == 1u && F.c0 == cx && F.c1 == cx + cw ? 2 : 0, T_THEME, T_SURF);
    }
    lx = x0 + SG_PAD;                                  /* the letters: the block's, the cell's (when it starts later) */
    if (!(F.c1 > F.c0 && lx >= F.c0 && lx < F.c1) && (w = sg_letter(lx, F.c1 > F.c0 && F.c0 > lx ? F.c0 : x1, y, s, T_TEXT, sg_fill_at(&F, lx)))) {
        skip[0] = lx - 1;
        skip[1] = lx + w + 2;
    }
    if (F.c1 > F.c0 && (w = sg_letter(F.c0 + SG_PAD, F.c1, y, s, T_INK, T_THEME))) {
        skip[2] = F.c0 + SG_PAD - 1;
        skip[3] = F.c0 + SG_PAD + w + 2;
    }
    sg_picture(L, off, k, i, j, s, x0, x1, &F, skip);
}
static void graph_song(void)
{
    song_lay_t L;
    const aafont_t *fl = &AF_M;                       /* the line: the selected cell's detail and the length, M */
    int32_t off, px, sp, hi = SG_X0 + SG_W, bx, x0, ly = SG_IY - (AF_M_CAP_Y + AF_M_CAP_H);
    uint32_t i, j, k, c = song_cur(), lane = ui.song_trk, total, bars;   /* (lane NTRK: SEC, no lane picked) */
    char b[24], a[12];
    sg_lh = ((int32_t)graph_h() - SG_Y - 2) / (int32_t)NTRK - SG_LG;   /* (Dah: the eight lanes fill the panel, to
                                                         * its foot: drawn in two canvases, draw_graph_in) */
    if (sg_lh < 11)
        sg_lh = 11;
    song_layout(&L);
    off = song_follow(&L);
    px = song_playx(&L);
    sp = px >= 0 ? SG_X0 + px - off : -100;
    total = L.u0[L.n];
    bars = (total + SG_BARU - 1u) / SG_BARU;
    for (k = 0; k < trk_count(); k++) {                /* (Dah) the lanes as HOME's rows: the one picked tinted with
                                                         * its colour, framed in it, a band of it at the left, its number
                                                         * on it; the others' numbers in their colour */
        char d[2] = {(char)('1' + k), 0};
        uint16_t tc = ux.trk[k];
        int32_t cyy = SG_LY(k) + HALF_UP(SG_LH - 13);
        if (k == lane) {
            cv_rect(3, SG_LY(k), 234, SG_LH, ux_mix(T_SURF, tc, 16));
            cv_frame(3, SG_LY(k), 234, SG_LH, tc);
            cv_rect(3, SG_LY(k), 3, SG_LH, tc);
            cv_rect(SG_TX + 1, cyy, 13, 13, tc);
        }
        GFX_HOOK_ALIGN(SG_TX + 1, cyy, SG_TX + 14, cyy + 13, AL_HV, "song lane number centred");
        cv_text_in(SG_TX + 1, cyy + CAP_IN(S, 13), 13, &AF_S, d, k == lane ? T_INK : tc, k == lane ? tc : T_SURF);
    }
    for (i = 1; i < L.n; i++) {                       /* the sections' boundaries, faint, behind the blocks */
        int32_t x = SG_X0 + L.x[i] - off - 1;
        if (x >= SG_X0 && x < hi)
            cv_rect(x, SG_Y, 1, SG_H, T_LINE);
    }
    for (i = 0; i < L.n; i++) {                       /* (HA HA HE HU) a CUT section: an ACCENT bar at its start */
        int32_t x = SG_X0 + L.x[i] - off - (i ? 2 : 0);
        if ((chain_config.row[i].repeat & CHAIN_CUT) && x >= SG_X0 && x + 2 <= hi)
            cv_rect(x, SG_Y, 2, SG_H, T_ACCENT);
    }
    for (k = 0; k < trk_count(); k++)                    /* the lanes: a block per run of one slot, "-" a faint line */
        for (ux_tint(ux.trk[k]), i = 0; i < L.n; i = j) {   /* (HA HA HE HU: each lane in its track's colour) */
            uint32_t s = chain_config.row[i].slot[k];
            for (j = i + 1u; j < L.n && chain_config.row[j].slot[k] == s; j++)
                ;
            if (s != CHAIN_SILENT) {
                sg_block(&L, off, k, i, j, s, k == lane || lane >= NTRK ? c : CHAIN_ROWS, sp);
            } else {
                int32_t sx = SG_X0 + L.x[i] - off, ex = SG_X0 + L.x[j] - off - SG_GAP;
                sx = sx > SG_X0 ? sx : SG_X0;
                ex = ex < hi ? ex : hi;
                if (ex > sx)
                    cv_rect(sx, SG_LY(k) + SG_LH / 2, ex - sx, 1, T_LINE);
                if ((k == lane || lane >= NTRK) && c >= i && c < j)   /* the cursor on a silent cell: its outline */
                    sg_frame(SG_X0 + L.x[c] - off, L.x[c + 1u] - L.x[c] - SG_GAP, SG_LY(k), SG_LH, 1, SG_X0, hi, T_THEME);
            }
        }
    ux_tint(ux.trk[song.sel % NTRK]);
    if (sgw.gu && sgw.gu < L.u0[L.n]) {                /* (Dah) the GRID: where PLAY starts, a line and a mark on top */
        int32_t gx = SG_X0 + song_ux(&L, sgw.gu) - off;
        if (gx >= SG_X0 && gx < hi) {
            for (k = 0; k < (uint32_t)SG_H; k += 3u)
                cv_rect(gx, SG_Y + (int32_t)k, 1, 2, T_TEXT);
            cv_rect(gx - 2, SG_Y, 5, 2, T_TEXT);
        }
    }
    if (L.add) {                                       /* + ADD */
        bx = SG_X0 + L.x[L.n] - off;
        x0 = bx > SG_X0 ? bx : SG_X0;
        if ((bx + SG_ADDW < hi ? bx + SG_ADDW : hi) > x0) {
            int32_t x1 = bx + SG_ADDW < hi ? bx + SG_ADDW : hi;
            sg_dash(bx, SG_ADDW, SG_X0, hi, c == L.n ? T_THEME : T_DIM);
            if (text_w(fl, "+") <= x1 - x0 - 4) {
                GFX_HOOK_ALIGN(x0, SG_Y, x1, SG_Y + SG_H, AL_HV, "song + ADD centred");
                if (fl == &AF_M) cv_text_in(x0, SG_Y + CAP_IN(M, SG_H), x1 - x0, &AF_M, "+", c == L.n ? T_THEME : T_DIM, T_SURF);
                else cv_text_in(x0, SG_Y + CAP_IN(S, SG_H), x1 - x0, &AF_S, "+", c == L.n ? T_THEME : T_DIM, T_SURF);
            }
        }
    }
    if (L.n) {                                         /* the ruler: a tick a bar, every 4th longer with its number */
        int32_t r0 = SG_X0 + L.x[0] - off, r1 = SG_X0 + L.x[L.n] - off - SG_GAP, last = -100;
        int32_t lx[24];                                /* the numbers, drawn after the ticks */
        uint32_t bar, lb[24], nl = 0;
        r0 = r0 > SG_X0 ? r0 : SG_X0;
        r1 = r1 < hi ? r1 : hi;
        {                                              /* (Dah) the overview, as HOME's: the whole song a line, the
                                                         * view on it a bar, the playhead a mark */
            int32_t wt = L.x[L.n + 1u] > SG_W ? L.x[L.n + 1u] : SG_W, v0 = SG_X0 + off * SG_W / wt;
            int32_t v1 = SG_X0 + (off + SG_W < wt ? off + SG_W : wt) * SG_W / wt;
            cv_rect(SG_X0, 1, SG_W, 1, T_LINE);
            cv_rect(v0, 0, v1 - v0 > 2 ? v1 - v0 : 2, 3, T_MID);
            if (px >= 0)
                cv_rect(SG_X0 + px * SG_W / wt, 0, 2, 3, T_ACCENT);
        }
        if (sg_pend && ((ui.frame / 6u) & 1u)) {       /* (Dah) the section to add (a bar where it goes), to delete
                                                         * (framed), blinking */
            uint32_t r = sg_prow < L.n ? sg_prow : L.n;
            int32_t ax = SG_X0 + L.x[r] - off;
            if (sg_pend == 1u)
                cv_rect(clamp(ax - 2, SG_X0, hi - 3), SG_Y, 3, SG_H, T_ACCENT);
            else if (r < L.n) {
                int32_t bx2 = SG_X0 + L.x[r + 1u] - off - SG_GAP;
                ax = ax < SG_X0 ? SG_X0 : ax;
                bx2 = bx2 > hi ? hi : bx2;
                if (bx2 > ax) {
                    cv_frame(ax, SG_Y - 1, bx2 - ax, SG_H + 2, T_REC);
                    cv_frame(ax + 1, SG_Y, bx2 - ax - 2, SG_H, T_REC);
                }
            }
        }
        if (sp >= SG_X0 && sp < hi - 1) {              /* the playhead, across the lanes (INK on the cell selected) */
            for (k = 0; k < trk_count(); k++) {
                uint32_t s = c < L.n ? chain_config.row[c].slot[k] : CHAIN_SILENT;
                uint32_t ql, qd, qn;
                int on = k == lane && s != CHAIN_SILENT && sg_pat(k, s, &ql, &qd, &qn) && sp >= SG_X0 + L.x[c] - off &&
                         sp + 2 <= SG_X0 + L.x[c + 1u] - off - SG_GAP;
                cv_rect(sp, SG_LY(k), 2, SG_LH + (k + 1u < trk_count() ? SG_LG : 0), on ? T_INK : T_ACCENT);
            }
        }
        for (i = 0; i < L.n; i++) {
            uint32_t u1 = L.u0[i + 1u], w = (uint32_t)(L.x[i + 1u] - L.x[i]);
            bx = SG_X0 + L.x[i] - off;
            if (bx >= hi || bx + (int32_t)w < SG_X0)
                continue;
            bar = (L.u0[i] + SG_BARU - 1u) / SG_BARU;  /* (from the view's left edge) */
            if (bx < SG_X0) {
                uint32_t uv = L.u0[i] + sg_muldiv((uint32_t)(SG_X0 - bx), u1 - L.u0[i], w);
                bar = uv / SG_BARU > bar ? uv / SG_BARU : bar;
            }
            for (; bar * SG_BARU < u1 || (i + 1u == L.n && bar * SG_BARU == u1); bar++) {
                int32_t x = SG_X0 + song_ux(&L, bar * SG_BARU) - off, lg = !(bar % 4u);
                if (i + 1u == L.n && bar * SG_BARU == u1) x = r1 - 1;   /* (the song's end: its last section's edge) */
                if (x > hi - 1)
                    break;
                if (x < SG_X0)
                    continue;
                cv_rect(x, SG_RH - (lg ? 3 : 1), 1, lg ? 3 : 1, lg ? T_MID : T_LINE);   /* (a tick a bar, longer every 4) */
                if (lg && bar * SG_BARU < total && nl < 24u) {
                    lx[nl] = x;
                    lb[nl++] = bar + 1u;
                }
            }
        }
        (void)lx; (void)lb; (void)last;
        if (sp >= SG_X0 && sp < hi) {                  /* (Dah) the bar playing: its number on the playhead, as HOME's */
            int32_t pu = song_playu(&L), w, ib[4];
            fmt_int(a, pu >= 0 ? pu / (int32_t)SG_BARU + 1 : 1);
            w = text_w(&AF_S, a);
            text_ink(&AF_S, a, ib);
            x0 = clamp((2 * sp + 2 - (ib[0] + ib[2])) / 2, SG_X0, hi - w);
            cv_rect(x0 - 1, 4, w + 2, SG_RH - 4, T_SURF);
            cv_text(x0, 4 - AF_S_CAP_Y, &AF_S, a, T_ACCENT);
        }
    }
    /* (Dah: no line of the cell's pattern nor the song's length: the PAT card and the ruler say them) */
    x0 = hi;
    (void)bars; (void)b; (void)fl;
    (void)ly;                                          /* (Dah: the lanes take the panel; + ADD's slot says itself) */
}
#include "ui_graph_more.c"                         /* (Dah) FILTER ENV DEST VOICE ARP DLY REVERB CHORUS CLOCK */
static void draw_graph_in(void);
/* HA HA HE HU: the panel (curves, scope, roll, grid) in the selected track's colour; MIXER and SONG colour each track */
static void draw_graph(void)
{
    ux_tint(ux.trk[song.sel % NTRK]);
    draw_graph_in();
    ux_untint();
}
static void draw_graph_in(void)
{
    const page_t *pg = cur_page();
    const track_t *t = TSEL;
    uint16_t c = ACC;
    uint32_t sig, gh;
    if (!ui.home && pg->graph == GR_TRK && !pl_on() && !tl_on()) {
        draw_tracks();
        return;
    }
    {                                                /* (Dah) a list over the panel gone: all of it again */
        static uint8_t ov;
        uint8_t now = (uint8_t)(pl_on() || tl_on());
        if (ov && !now)
            ui.force = 1;
        ov = now;
        if (now)
            goto lists;
    }
    if (ui.home && home_tracks()) {                  /* MENU > HOME TRACKS (1.3) */
        draw_home_tracks();
        return;
    }
lists:
    if (!ui.home && pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE) sample_wave_tick(t);
    sig = graph_signature();
    if (!ui.force && sig == ui.graph_sig)
        return;
    ui.graph_sig = sig;
    gh = graph_h() < CV_MAX / 240u ? graph_h() : CV_MAX / 240u;   /* (Dah: HOME's panel taller than the canvas: its
                                                         * graphs and lists to the canvas' foot, BG under it) */
    {   /* (Dah) the panel taller than the canvas (HOME's look, every page): drawn in canvases of gh rows, one under
         * the other, each the whole graph moved up (cv_py) and clipped to it: the panel to its foot, no more RAM */
    uint32_t tot = graph_h(), y0;
    for (y0 = 0; y0 < tot; y0 += gh) {
    uint32_t ph = tot - y0 < gh ? tot - y0 : gh;
    cv_begin(240, ph, T_BG);
    cv_split = tot > gh;
    cv_split_h = (int32_t)tot;
    cv_py = -(int32_t)y0;
    cv_rrect(3, 0, 234, (int32_t)tot, 5, T_SURF, T_BG);   /* the panel (MENU > LARGE: the strip) */
    graph_ht = 100 + ((int32_t)tot > H_GRAPH && strip_kind() == SK_NONE ? (int32_t)tot - H_GRAPH : 0);   /* (Dah:
                                                      * the panel taller, the 100 px scale's graphs stretched with it) */
    cv_bg = T_SURF;                                  /* (text drawn with cv_text lands on it) */
    cv_oy = GOY;                                     /* graphs on a 100 px scale */
    if (ka.st) {                                     /* (Dah) ASSIGN: the panel its own */
        cv_oy = 0;
        graph_assign();
    } else if (strip_kind() != SK_NONE) {            /* MENU > LARGE: the strip */
        uint32_t k = strip_kind();
        cv_oy = k == SK_ADSR || k == SK_LFO ? 3 : 0;
        graph_ht = LG_H_GRAPH - 6;
        if (k == SK_SCOPE) graph_scope(c);
        else if (k == SK_ADSR) graph_adsr(t, c);
        else if (k == SK_LFO) graph_lfo(t, c);
        else graph_title();
        graph_ht = 100;
    } else if (ui.home) {                            /* (Dah: PRESETS turned: its list full screen, ui_draw.c
                                                      * draw_list_full) */
        cv_oy = 0;
        graph_scope(c);
    } else {
        if (pg->fam == FAM_FX)
            t = fx_tsel();
        dv_watch = 0;                                /* (Dah: SIDECHAIN COMP DIST's pages watch again, fxv_take) */                           /* (Dah: the MASTER's chain, MIXER's MASTER picked) */
        eq_watch = pg->fam == FAM_FX && pg->scope == SC_TRACK ? (uint8_t)(fx_mst(t) ? NTRK + 1u : trk_index(t) % NTRK + 1u) : 0u;   /* (Dah: the
                                                         * FX pages' sound live, fx_eq.c) */
        switch (pg->graph) {
        case GR_ADSR:
            graph_adsr(t, c);
            break;
        case GR_LFO:
            graph_lfo(t, c);
            break;
        case GR_LDRAW:
            graph_ldraw(t, c);
            break;
        case GR_ROLL:
            cv_oy = 0;
            if (detail_on())                          /* 1.5.1 (#199) */
                graph_detail(t, c);
            else if (drum_track(t))
                graph_grid(t, c);
            else
                graph_roll(t, c);
            break;
        case GR_SCALE:
            graph_scale(t, c);
            break;
        case GR_CHORD:
            cv_oy = 0;
            graph_chord(t, c);
            break;
        case GR_SLCR:                                /* (Dah) its pattern at the left, the sound gated at the right */
            if (fx_mst(t))
                graph_slicer(t, c, PANEL_W);
            else {
                graph_slicer(t, c, FXV_LW);
                fxv_split(t, DV_SLCR, c, "IN", "OUT", FXV_CUT);
            }
            break;
        case GR_INS:                                 /* (Dah) its shape at the left, the sound through it at the right */
            if (fx_mst(t))
                graph_insert(t, c, PANEL_W);
            else {
                graph_insert(t, c, FXV_LW);
                fxv_split(t, DV_INS, c, "DRY", "WET", 0);
            }
            break;
        case GR_MOD:
            cv_oy = 0;
            graph_mod(t, c);
            break;
        case GR_SLOTS:
            cv_oy = 0;
            graph_slots();
            break;
        case GR_FILE:
            cv_oy = 0;
            graph_file();
            break;
        case GR_PATTERN:
            cv_oy = 0;
            graph_pattern();
            break;
        case GR_USER:
            cv_oy = 0;
            graph_user();
            break;
        case GR_SONG:
            cv_oy = 0;
            graph_song();
            break;
        case GR_EVENTS:
            cv_oy = 0;
            graph_auto();
            break;
#if FELUCCA_SLICE
        case GR_SLICES:
            cv_oy = 0;
            graph_slices();
            break;
#endif
        case GR_FMOP:
        case GR_FMOP2:
            if (fop_ok()) graph_fm6(t, c, fop_op % 6u);   /* the chart, the operator picked */
            break;
        case GR_FMENV:
            if (fop_ok()) graph_fm6env(t, c);
            break;
        case GR_DLANE:
        case GR_DLANE2:
        case GR_DSYN:
        case GR_DSYN2:
        case GR_DSYN3:
        case GR_DSYN4:
        case GR_DSYN5:
            cv_oy = 0;
            if (dln_ok()) graph_dlanes(t);
            break;
        default:
            if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_WHEEL) graph_wheel(t, c);
            else if (pg->scope == SC_ENGINE && ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && sample_wave.ready) graph_sample(c);
            else if (pg->scope == SC_ENGINE && t->eng_req % NENGINES == ENGI_FM6) graph_fm6(t, c, 6u);   /* EDIT 1 and 2 */
#if FELUCCA_FM4
            else if ((pg->scope == SC_ENGINE || pg->id[0] == P_FM1_LEVEL) && t->eng_req % NENGINES == ENGI_DIGITAL)
                graph_fm(t, c);                      /* (OP LEVEL too: the levels on the chart) */
#endif
            else if (graph_fxfam(pg, t, c) || graph_more(pg, t, c)) { /* (Dah: the page's own: ui_graph_more.c) */ }
            else { cv_oy = 0; graph_scope(c); }
            break;
        }
    }
    cv_oy = 0;
    cv_blit(0, graph_y() + y0);
    }
    cv_py = 0;
    cv_split = 0;
    }
    graph_ht = 100;
}
