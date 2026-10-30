/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* Dah: the panels of the pages that showed the scope for want of their own: what each page sets, drawn.
 *   FILTER    the response over ten octaves: LP / BP / HP, the cutoff's place, the resonance's peak (a sketch in dB, not
 *             the SVF's exact curve)
 *   ENV DEST  the envelope's three amounts as bars from the middle (FLT PIT SHP), ESYNC said
 *   VOICE     the track's voices (the budget's slots: its notes), the stereo field (PAN, SPREAD, DETUNE) and GLIDE
 *   ARP       the notes it plays from a C chord over its octaves, sixteen of them, the gate their length (ARP 2: the
 *             swing's late steps, the probability's faint ones)
 *   DLY       the echoes: the dry hit, then each repeat at TIME, FDBK lower each time, COLR darker, MIX their height
 *   REVERB    the reflections (ROOM close, SPRING a wobble, HALL sparse) and the tail, SIZE long, DAMP dark
 *   CHORUS    its two voices moving: RATE and DEPTH
 *   CLOCK     a bar of sixteen steps, the beats taller, SWING's late ones, the step playing
 * On the 100 px scale (graph_ht, GOY) as the other curves; integer only. Included by ui_graph.c */

/* 2^(oct / 256) for |oct| <= 8 octaves, Q16 (linear between octaves: a sketch's accuracy) */
static int32_t gm_exp2(int32_t oct)
{
    int32_t e = oct >> 8, f = oct & 255, b = 65536 + ((f * 45426) >> 8);
    e = clamp(e, -15, 14);
    return e >= 0 ? b << e : b >> -e;
}
static int32_t gm_y(int32_t db10, int32_t top, int32_t bot)  /* +18 dB at top .. -36 dB at bot (db10: dB x 10) */
{
    return top + (180 - clamp(db10, -360, 180)) * (bot - top) / 540;
}
static void graph_filter(const track_t *t, uint16_t c)
{
    int32_t type = clamp(t->p[P_FTYPE], 0, 2), xc = PANEL_X0 + t->p[P_E4] * PANEL_W / 127, x, py = 0;
    int32_t peak = t->p[P_E5] * 180 / 127, top = 8, bot = graph_ht - 8;   /* the resonance's peak, dB x 10 */
    cv_rect(PANEL_X0, gm_y(0, top, bot), PANEL_W, 1, T_RAISE);   /* 0 dB */
    cv_rect(xc, top, 1, bot - top, T_GRID);                       /* the cutoff */
    for (x = 0; x < PANEL_W; x++) {
        int32_t oct = (PANEL_X0 + x - xc) * 10 * 256 / PANEL_W, ao = oct < 0 ? -oct : oct;   /* octaves from it, Q8 */
        int32_t bump = peak * 256 / (256 + (ao * ao >> 6)), db, y;   /* the peak, narrowing away from the cutoff */
        if (type == 0)
            db = (oct > 0 ? -120 * oct / 256 : 0) + bump;              /* LP: -12 dB / octave above */
        else if (type == 2)
            db = (oct < 0 ? 120 * oct / 256 : 0) + bump;               /* HP */
        else
            db = -60 * ao / 256 + bump;                                /* BP: -6 dB / octave each side */
        y = gm_y(db, top, bot);
        if (x)
            cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, c, 2);
        py = y;
    }
}
/* bars from the middle, each with its name: v -64..63 */
static void gm_bibar(int32_t y, const char *name, int32_t v, uint16_t c)
{
    int32_t mid = PANEL_X0 + 32 + (PANEL_W - 32) / 2, w = v * ((PANEL_W - 32) / 2) / 64;
    cv_text_on(PANEL_X0, y - 4, &AF_S, name, T_MID, T_SURF);
    cv_rect(PANEL_X0 + 32, y + 2, PANEL_W - 32, 1, T_RAISE);
    cv_rect(mid, y - 4, 1, 13, T_GRID);
    if (w > 0)
        cv_rect(mid + 1, y - 2, w, 9, c);
    else if (w < 0)
        cv_rect(mid + w, y - 2, -w, 9, c);
}
static void graph_envdest(const track_t *t, uint16_t c)
{
    gm_bibar(graph_ht * 22 / 100, "FLT", t->p[P_ED_FLT], c);
    gm_bibar(graph_ht * 48 / 100, "PIT", t->p[P_ED_PIT], c);
    gm_bibar(graph_ht * 74 / 100, "SHP", t->p[P_ED_SHP], c);
    if (t->p[P_ESYNC])
        cv_text_r(PANEL_X0 + PANEL_W, graph_ht - 12, &AF_S, "ENV IN NOTES", T_MID, T_SURF);
}
static void graph_voice(const track_t *t, uint16_t c)
{
    const page_t *pg = cur_page();
    uint32_t i, n = 0, m = ui_vmode(t);
    int32_t sw = (PANEL_W - 7 * 4) / 8, ly = graph_ht * 62 / 100, pan = t->p[P_PAN];
    int32_t spr = t->p[P_SPRD] > 0 && (m == V_POLY || m == V_UNISON) ? t->p[P_SPRD] : 0;   /* (what plays: params.c pg_unused) */
    int32_t dt = pg_unused(pg, P_DETUNE) ? 0 : t->p[P_DETUNE];
    for (i = 0; i < NVOICE; i++) {                    /* the slots: the voices sounding, their notes */
        const voice_t *v = &t->v[i];
        int32_t x = PANEL_X0 + (int32_t)i * (sw + 4);
        if (v->active) {
            char nb[6];
            cv_rrect(x, 4, sw, 18, 3, c, T_SURF);
            note_name(nb, v->note);
            cv_text_in(x, 4 + CAP_IN(S, 18), sw, &AF_S, nb, T_INK, c);
            n++;
        } else {
            cv_rrect(x, 4, sw, 18, 3, T_RAISE, T_SURF);
        }
    }
    (void)n;
    cv_text_on(PANEL_X0, ly - 16, &AF_S, "L", T_DIM, T_SURF);   /* the stereo field: where the voices sit */
    cv_text_r(PANEL_X0 + PANEL_W, ly - 16, &AF_S, "R", T_DIM, T_SURF);
    cv_rect(PANEL_X0, ly, PANEL_W, 1, T_RAISE);
    cv_rect(PANEL_X0 + PANEL_W / 2, ly - 4, 1, 9, T_GRID);
    for (i = 0; i < 4u; i++) {                        /* four voices: PAN, SPREAD apart, DETUNE their width */
        int32_t p = clamp(pan + (i & 1u ? spr : -spr) * (int32_t)(1u + i / 2u) / 4, -64, 63);
        int32_t x = PANEL_X0 + (p + 64) * (PANEL_W - 6) / 127, w = 4 + dt / 24;
        cv_rrect(x - w / 2 + 3, ly - 3, w, 7, 3, c, T_SURF);
    }
    if (!ENGINES[t->eng_req % NENGINES]->oneshot && !pg_unused(pg, P_GLIDE)) {   /* GLIDE: from one note to the next,
                                                         * its time the slope (a kit, SLICE: none) */
        int32_t gy0 = graph_ht - 8, gy1 = graph_ht - 22, g = t->p[P_GLIDE], gx = PANEL_X0 + 60, gw = g * 100 / 127;
        cv_text_on(PANEL_X0, gy1 - 2, &AF_S, "GLD", T_MID, T_SURF);
        cv_rect(gx, gy0, 30, 2, c);
        cv_line_t(gx + 30, gy0, gx + 30 + gw, gy1, c, 2);
        cv_rect(gx + 30 + gw, gy1, 30, 2, c);
    }
}
/* (Dah) the place in the list (len notes) step i of the arp's cycle plays, as seq.c arp_pick (RND, WALK: in order, a
 * picture: they draw anew each time) */
static uint32_t gm_arp_at(uint32_t m, uint32_t i, uint32_t len)
{
    uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, k = i % cyc;
    switch (m) {
    case AM_DN: return len - 1u - i % len;
    case AM_UPDN: return k < len ? k : cyc - k;
    case AM_DNUP: return k < len ? len - 1u - k : k - (len - 1u);
    case AM_CONV: case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG) k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY: return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB: return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    default: return i % len;
    }
}
static uint32_t gm_arp_cycle(uint32_t m, uint32_t len)   /* the steps of one whole cycle */
{
    if (m == AM_UPDN || m == AM_DNUP || m == AM_PINKY || m == AM_THUMB)
        return len > 1u ? 2u * len - 2u : 1u;
    return m == AM_CHORD || m == AM_REPEAT ? 1u : len;
}
/* ARP: what it plays from the keys held now, one cycle of it as a little roll (each note its row, the gate its length;
 * ARP 2: the swing's late steps, PROB's faint), the step playing in TEXT. No key held: a C chord's, faint, and how */
static void graph_arp(const track_t *t, uint16_t c)
{
    uint32_t list[64], cnt, len, m = (uint32_t)t->p[P_AMODE], cyc, i, j, nrow = 0, playing;
    uint8_t row[128];
    int32_t top = 6, bot = graph_ht - 6, cw, rh, gate = clamp(t->p[P_AGATE], 1, 127), demo = 0;
    if (!m) {
        panel_note("ARP OFF", "[K1] MODE", 0);
        return;
    }
    len = arp_list(t, list, &cnt);
    if (!len) {                                         /* nothing held: a C chord over its octaves, as an example */
        uint32_t o;
        demo = 1;
        for (o = 0; o < (uint32_t)clamp(t->p[P_AOCT], 1, 4) && len < 60u; o++) {
            list[len++] = 60u + 12u * o; list[len++] = 64u + 12u * o; list[len++] = 67u + 12u * o;
        }
        if (m == AM_REPEAT)
            len = 1;
    }
    if (m == AM_UP8 && len < 64u) {                     /* UP+8: the first note again over the top */
        list[len] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
        len++;
    }
    cyc = gm_arp_cycle(m, len);
    if (cyc > 32u)
        cyc = 32u;
    memset(row, 0xFF, sizeof row);
    for (i = 0; i < 128u; i++)                          /* the rows: the notes of the list, low to high */
        for (j = 0; j < len; j++)
            if (list[j] == i) {
                row[i] = (uint8_t)nrow++;
                break;
            }
    cw = PANEL_W / (int32_t)(cyc < 4u ? 4u : cyc);
    rh = nrow ? (bot - top) / (int32_t)nrow : 1;
    if (rh > 12) rh = 12;
    playing = !demo && t->arp_idx ? (t->arp_idx - 1u) % cyc : 0xFFu;   /* (arp_fire: idx counted first) */
    for (i = 0; i <= cyc; i++)
        cv_rect(PANEL_X0 + (int32_t)i * cw, top, 1, bot - top, i % 4u ? T_GRID : T_RAISE);
    for (i = 0; i < cyc; i++) {
        int32_t x = PANEL_X0 + (int32_t)i * cw + 1, w = (cw - 2) * gate / 127;
        uint16_t col = demo ? ux_mix(c, T_SURF, 50) : i == playing ? T_TEXT : c;
        if (i & 1u)
            x += (cw - 2) * t->p[P_ASWING] / 200;      /* SWING: the off-steps later */
        for (j = 0; j < len; j++)
            if (m == AM_CHORD || m == AM_REPEAT ? 1 : j == gm_arp_at(m, i, len)) {
                int32_t y = bot - (int32_t)(row[list[j] & 127u] + 1u) * rh;
                cv_rect(x, y + 1, w < 2 ? 2 : w, rh - 2 > 2 ? rh - 2 : 2, col);
                if (m != AM_CHORD)
                    break;
            }
    }
    if (demo) {                                         /* (Dah) in the middle, on a patch of the panel over the example */
        int32_t w = text_w(&AF_M, "HOLD KEYS") + 16, y = (top + bot) / 2 - 10;
        cv_rrect(120 - w / 2, y, w, 20, 4, T_SURF, T_SURF);
        GFX_HOOK_ALIGN(0, y, 240, y + 20, AL_HV, "arp hold keys centred");
        cv_text_in(0, y + CAP_IN(M, 20), 240, &AF_M, "HOLD KEYS", T_TEXT, T_SURF);
    }
}
/* the delay's TIME in quarters x 24 (N_DTIME's order) */
static const uint16_t GM_DT24[13] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384, 18, 9, 36};
static void graph_dly(uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    int32_t dt = GM_DT24[(uint32_t)song.g[G_DTIME] % 13u], win = dt * 6 > 96 ? dt * 6 : 96;   /* the window: a bar, or six repeats */
    int32_t fb = clamp(song.g[G_DFDBK], 0, 120), a = song.g[G_DMIX] * 1000 / 127, k, bot = graph_ht - 6, top = 6;
    cv_rect(PANEL_X0, bot, pw, 1, T_RAISE);
    cv_rect(PANEL_X0, top, 3, bot - top, T_TEXT);    /* the dry hit */
    for (k = 1; k * dt < win && k < 64; k++) {
        int32_t x = PANEL_X0 + k * dt * pw / win, h = a * (bot - top) / 1000;
        cv_rect(x, bot - h, 3, h, ux_mix(c, T_RAISE, clamp((k - 1) * song.g[G_DCOLOR] / 16, 0, 90)));   /* COLR: darker on */
        a = a * fb / 100;
        if (a > 1000)
            a = 1000;
        if (a < 8)
            break;
    }
}
static void graph_reverb(uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    int32_t type = clamp(song.g[G_RTYPE], 0, 2), size = song.g[G_RSIZE], damp = song.g[G_RDAMP];
    int32_t bot = graph_ht - 6, top = 10, len = pw * (24 + size) / 151, x, i;
    static const uint8_t ER[3][6] = {{6, 10, 15, 19, 26, 31}, {4, 7, 9, 12, 14, 17}, {12, 22, 38, 50, 64, 80}};
    cv_rect(PANEL_X0, bot, pw, 1, T_RAISE);
    cv_rect(PANEL_X0, top, 3, bot - top, T_TEXT);    /* the sound */
    for (x = 0; x < len; x++) {                       /* the tail: (1 - x / len)^2, darker with DAMP along it */
        int32_t r = len - x, h = (bot - top) * 7 / 10 * r / len * r / len;
        if (type == 1)                                /* SPRING: its wobble */
            h = h * (100 + (sine_i((uint32_t)x * 0x6000000u) * 25 >> 15)) / 100;
        cv_rect(PANEL_X0 + 6 + x, bot - h, 1, h, ux_mix(c, T_RAISE, clamp(x * damp / len * 90 / 127, 0, 90)));
    }
    for (i = 0; i < 6; i++) {                          /* the first reflections */
        int32_t ex = PANEL_X0 + 6 + ER[type][i] * len / 100, eh = (bot - top) * (90 - i * 10) / 100;
        cv_rect(ex, bot - eh, 2, eh, c);
    }
}
static void graph_chorus(uint16_t c, int32_t pw)   /* (Dah: pw wide from the left) */
{
    int32_t x, py0 = 0, py1 = 0, cy = graph_ht / 2, a = song.g[G_CDEPTH] * (graph_ht * 38 / 100) / 127;
    uint32_t inc = (0xFFFFFFFFu / (uint32_t)pw) * (uint32_t)(1 + song.g[G_CRATE] / 16);
    cv_rect(PANEL_X0, cy, pw, 1, T_RAISE);
    for (x = 0; x < pw; x++) {                   /* its two voices: a quarter of a cycle apart */
        int32_t y0 = cy - (sine_i((uint32_t)x * inc) * a >> 15), y1 = cy - (sine_i((uint32_t)x * inc + 0x40000000u) * a >> 15);
        if (x) {
            cv_line_t(PANEL_X0 + x - 1, py0, PANEL_X0 + x, y0, c, 2);
            cv_line_t(PANEL_X0 + x - 1, py1, PANEL_X0 + x, y1, ux_mix(c, T_SURF, 45), 2);
        }
        py0 = y0;
        py1 = y1;
    }
}
static void graph_clock(uint16_t c)
{
    int32_t cw = PANEL_W / 16, bot = graph_ht - 8, i, sw = song.g[G_SWING];
    uint32_t ph = song.playing ? trk[song.sel % NTRK].seq_idx % 16u : 99u;
    cv_rect(PANEL_X0, bot, PANEL_W, 1, T_RAISE);
    for (i = 0; i < 16; i++) {                        /* a bar: its sixteen steps, the beats taller, SWING's late ones */
        int32_t x = PANEL_X0 + i * cw + cw / 2 - 2 + (i & 1 ? cw * sw / 200 : 0);
        int32_t h = i == 0 ? graph_ht * 70 / 100 : i % 4 == 0 ? graph_ht * 50 / 100 : graph_ht * 25 / 100;
        cv_rrect(x, bot - h, 5, h, 2, (uint32_t)i == ph ? T_TEXT : i % 4 == 0 ? c : ux_mix(c, T_SURF, 40), T_SURF);
    }
}
/* SIDECHAIN, live: the track it watches (its number at the top), their level over the last
 * ~3 s scrolling left: before the dip faint, after it in the colour, the dip itself a line from the top (ACCENT), each
 * kick a mark under it. Nothing sounding yet: the dip's shape over four kicks, faint (DEPTH, REL) */
#define DV_N DVR_N                                     /* (fx.c dvr_*) */
static void sound_name(const track_t *t, char *b);     /* ui_draw.c */
static uint32_t duck_watch(const track_t *t)            /* this track */
{
    return 1u << (trk_index(t) % NTRK);
}
/* (Dah) the track's sound now, thin, across the panel's top (the EQ's: fx_eq.c eq_ring) */
static void fx_wave_line(int32_t top, uint16_t c)
{
    uint32_t w = eq_wp;
    int32_t wy = top + 10, pw = wy, x;
    (void)c;
    for (x = 0; x < (int32_t)PANEL_W; x++) {
        int32_t v = eq_ring[(w + EQ_RN - PANEL_W + (uint32_t)x) & (EQ_RN - 1u)], y = wy - v * 9 / 32768;
        if (x)
            cv_line_t(PANEL_X0 + x - 1, pw, PANEL_X0 + x, y, ux_mix(T_TEXT, T_SURF, 45), 1);
        pw = y;
    }
}
/* (Dah) an effect's page's live picture: the effect u on track t watched (fx.c dv_*, dvr_*); 1 while something
 * sounded in the ring's last ~0.6 s */
static uint8_t fxv_fresh;                               /* (fxv_draw: the scale from scratch) */
static int fxv_take(const track_t *t, uint32_t u)
{
    uint32_t k, w = dvr_w;
    if (dv_unit != u) {                                 /* (another effect's page: a fresh picture) */
        dv_watch = 0;
        dv_unit = (uint8_t)u;
        fxv_fresh = 1;
        memset(dvr_pre, 0, sizeof dvr_pre);
        memset(dvr_post, 0, sizeof dvr_post);
        memset(dvr_att, 0, sizeof dvr_att);
    }
    dv_watch = (uint8_t)duck_watch(t);
    for (k = 1; k <= 110u; k++)
        if (dvr_pre[(w - k) % DV_N] | dvr_post[(w - k) % DV_N])
            return 1;
    return 0;
}
#define FXV_LW 70                                       /* the effects' pages: the shape's width at the left .. */
#define FXV_RX (PANEL_X0 + FXV_LW + 8)                  /* .. the sound's at the right, the most of it */
#define FXV_RW ((int32_t)PANEL_W - FXV_LW - 8)
#define FXV_LINE 1u                                     /* fxv_draw: the effect's dip (dv att) a line from the top */
#define FXV_AREA 2u                                     /* .. that dip as an area from the top */
#define FXV_CUT 4u                                      /* .. what it took away (before less after) as an area */
#define FXV_OWN 8u                                      /* .. before and after each on its own scale */
#define FXV_SEND 16u                                    /* .. after: this track's own return, from its sound and the
                                                         * bus's settings (the bus mixes every track's: dv_unit's) */
/* (Dah) a send's return for this track alone, in the ring's columns (DVR_K x CTL samples), from what it sends (dvr_pre):
 * CHORUS at once, DELAY its repeats (TIME, FEEDBACK, MIX), REVERB a tail (SIZE); into ui_tmp (/ 2), the newest last */
static void fxv_send(const track_t *t, uint32_t e)   /* (e: the ring's write place, as the picture reads it) */
{
    uint32_t x, k, cols = DVR_K * CTL;
    int32_t sq = (dv_unit == DV_CHO ? t->p[P_CHOR] : dv_unit == DV_DLY ? t->p[P_DLY] : t->p[P_REV]) * 258;   /* Q15 */
    int32_t env = 0, d = (int32_t)(delay_samples() / cols);
    for (x = 0; x < DV_N && x < 512u; x++) {            /* (the whole ring, oldest first: the tails from before) */
        uint32_t w = (e + x) % DV_N;
        int32_t v = mulq15(dvr_pre[w], sq);
        if (dv_unit == DV_DLY) {                        /* the repeats: each D columns back, FEEDBACK down each time */
            int32_t g = song.g[G_DMIX] * 258, o = 0;
            for (k = 1; k < 24u && d > 0 && (int32_t)(k * (uint32_t)d) <= (int32_t)x; k++) {
                int32_t p = mulq15(mulq15(dvr_pre[(e + x - k * (uint32_t)d) % DV_N], sq), g);
                o = p > o ? p : o;
                g = mulq15(g, song.g[G_DFDBK] * 230);
                if (g < 64)
                    break;
            }
            v = o;
        } else if (dv_unit == DV_REV) {                 /* the tail: ~0.3 .. 3.8 s to -60 dB with SIZE */
            int32_t len = (300 + song.g[G_RSIZE] * 3500 / 127) * 44 / (int32_t)cols;   /* (columns) */
            env -= env * 7 / (len > 7 ? len : 7);
            v = v / 2 > env ? v / 2 : env;
            env = v;
        }
        ui_tmp[x] = (int16_t)(v >> 1);
    }
}
#define FXV_Q(w) ((mode & FXV_SEND) ? (int32_t)ui_tmp[((w) + DV_N - e) % DV_N] * 2 : (int32_t)dvr_post[w])
static void fxv_draw(int32_t x0, int32_t wd, int32_t top, int32_t bot, uint16_t c, uint32_t mode)   /* (the newest wd) */
{
    int32_t x, fp = 4000, fq = 4000, band = (mode & (FXV_LINE | FXV_AREA | FXV_CUT)) ? 10 : 0, cy, hh;
    uint32_t e = dvr_w;
    uint16_t dry = ux_mix(T_TEXT, T_SURF, 45);
    if (mode & FXV_SEND)
        fxv_send(TSEL, e);
    for (x = 0; x < wd; x++) {                          /* (the scale: the loudest shown) */
        uint32_t w = (e + DV_N - (uint32_t)wd + (uint32_t)x) % DV_N;
        fp = dvr_pre[w] > fp ? dvr_pre[w] : fp;
        fq = FXV_Q(w) > fq ? FXV_Q(w) : fq;
    }
    if (!(mode & FXV_OWN))
        fp = fq = fp > fq ? fp : fq;
    {                                                   /* (the scale held: up at once, down after ~3 s, slowly) */
        static int32_t hp0, hq0;
        static uint32_t tp0, tq0;
        if (fxv_fresh) {
            hp0 = hq0 = 0;
            fxv_fresh = 0;
        }
        if (fp >= hp0) { hp0 = fp; tp0 = ui.frame; } else if (ui.frame - tp0 > 200u) hp0 -= (hp0 - fp) / 24 + 1;
        if (fq >= hq0) { hq0 = fq; tq0 = ui.frame; } else if (ui.frame - tq0 > 200u) hq0 -= (hq0 - fq) / 24 + 1;
        fp = hp0;
        fq = hq0;
    }
    fp += fp / 10;
    fq += fq / 10;
    cy = (top + band + bot) / 2;                        /* the sound mirrored round its middle, under the work's band */
    hh = (bot - top - band) / 2;
    if (band)
        cv_rect(x0, top + band - 2, wd, 1, T_RAISE);
    cv_rect(x0, cy, wd, 1, T_RAISE);
    for (x = 0; x < wd; x++) {                          /* oldest at the left */
        uint32_t w = (e + DV_N - (uint32_t)wd + (uint32_t)x) % DV_N;
        int32_t hp = clamp((int32_t)dvr_pre[w] * hh / fp, 0, hh);
        int32_t hq = clamp(FXV_Q(w) * hh / fq, 0, hh);
        int32_t ha = (int32_t)(dvr_att[w] & 32767u) * (band - 3) / 32767;
        if ((mode & FXV_CUT) && dvr_pre[w] > FXV_Q(w))   /* (what it took away, of what came in) */
            ha = (int32_t)(dvr_pre[w] - FXV_Q(w)) * (band - 3) / (dvr_pre[w] | 1);
        if (band && ha > 0)
            cv_rect(x0 + x, top, 1, ha, T_ACCENT);      /* the effect's work, down from the top */
        if (hp >= hq) {                                 /* the dry and the wet, each its colour: the taller behind */
            if (hp) cv_rect(x0 + x, cy - hp, 1, 2 * hp + 1, dry);
            if (hq) cv_rect(x0 + x, cy - hq, 1, 2 * hq + 1, c);
        } else {
            cv_rect(x0 + x, cy - hq, 1, 2 * hq + 1, c);
            if (hp) cv_rect(x0 + x, cy - hp, 1, 2 * hp + 1, dry);
        }
        if (dvr_att[w] & 32768u)
            cv_rect(x0 + x, bot + 2, 2, 4, T_TEXT);                    /* a kick */
    }

}
/* the right half of an effect's page: its two labels (before, after), the sound going by */
static void fxv_split(const track_t *t, uint32_t u, uint16_t c, const char *pre, const char *post, uint32_t mode)
{
    int32_t x;
    (void)fxv_take(t, u);
    x = cv_text_on(FXV_RX, 2 + CAP_IN(S, 12), &AF_S, pre, ux_mix(T_TEXT, T_SURF, 45), T_SURF);   /* (the dry's colour) */
    cv_text_on(x + 6, 2 + CAP_IN(S, 12), &AF_S, post, c, T_SURF);
    fxv_draw(FXV_RX, FXV_RW, 16, graph_ht - 4, c, mode);
}
static void graph_duck(const track_t *t, uint16_t c)
{
    int32_t top = 18, bot = graph_ht - 8, x, i;
    uint32_t m = duck_watch(t);
    if (!t->p[P_DKSRC]) {
        panel_note("SIDECHAIN OFF", "[K1] THE TRACK THAT PUMPS IT", 0);
        dv_watch = 0;
        return;
    }
    (void)m;
    int live = fxv_take(t, DV_DUCK);
    {                                                   /* (Dah) where it comes from: the track, its sound's name */
        uint32_t s = (uint32_t)clamp(t->p[P_DKSRC], 1, NTRK) - 1u;
        char d[2] = {(char)('1' + s), 0}, nm[16];
        x = PANEL_X0;
        x += cv_text_on(x, 2 + CAP_IN(S, 12), &AF_S, "FROM", T_MID, T_SURF) + 6;
        cv_rrect(x, 2, 12, 12, 3, ux.trk[s], T_SURF);
        cv_text_in(x, 2 + CAP_IN(S, 12), 12, &AF_S, d, T_INK, ux.trk[s]);
        sound_name(&trk[s], nm);
        x = cv_text_on(x + 16, 2 + CAP_IN(S, 12), &AF_S, nm, T_TEXT, T_SURF);
        s = trkx[trk_index(t) % NTRK].sc2;
        if (s && s <= NTRK) {                           /* (and the second source) */
            d[0] = (char)('0' + s);
            x += 8;
            cv_rrect(x, 2, 12, 12, 3, ux.trk[s - 1u], T_SURF);
            cv_text_in(x, 2 + CAP_IN(S, 12), 12, &AF_S, d, T_INK, ux.trk[s - 1u]);
        }
    }
    if (!live) {                                        /* nothing sounding: the shape, faint */
        int32_t py = top, env = 0, hold = 0, beat = PANEL_W / 4;
        int32_t rel = 23790 / (20 + (t->p[P_DKREL] + 64) * 3), depth = t->p[P_DKAMT];
        for (x = 0; x < PANEL_W; x++) {
            int32_t lvl, y;
            if (x % beat == 0) {
                env = 32767;
                hold = 3;
                cv_rect(PANEL_X0 + x, bot + 2, 3, 4, T_MID);
            }
            for (i = 0; i < 14; i++)
                if (hold > 0) hold--; else env = (env * (32768 - rel)) >> 15;
            lvl = 32767 - ((env >> 8) * (depth * 258) >> 7);
            y = bot - (lvl * (bot - top)) / 32767;
            if (x)
                cv_line_t(PANEL_X0 + x - 1, py, PANEL_X0 + x, y, ux_mix(c, T_SURF, 50), 2);
            py = y;
        }
        return;
    }
    fxv_draw(PANEL_X0, (int32_t)PANEL_W, top, bot, c, FXV_LINE);
}
#include "ui_graph_fx.c"                           /* (Dah) the FX family's: CHAIN DIST EQ SENDS */
/* the page's own panel when it has one here (1), else 0 (the scope) */
static int graph_more(const page_t *pg, const track_t *t, uint16_t c)
{
    uint32_t id = pg->id[0];
    if (!(pg->scope == SC_TRACK && id == P_DKSRC))
        dv_watch = 0;                                   /* (the ISR's picture: only for DUCK's page) */
    if (pg->scope == SC_TRACK && id == P_FTYPE) graph_filter(t, c);
    else if (pg->scope == SC_TRACK && id == P_ED_FLT) graph_envdest(t, c);
    else if (pg->scope == SC_TRACK && (id == P_VOICE || id == P_ALLOC || id == P_SPRD)) graph_voice(t, c);
    else if (pg->scope == SC_TRACK && (id == P_AMODE || id == P_ASWING)) graph_arp(t, c);
    else if (pg->scope == SC_TRACK && id == P_DKSRC) graph_duck(t, c);
    else if (pg->scope == SC_GLOBAL && (id == (PS_TRK | P_DLY) || id == G_DMIX)) {   /* (Dah: FX's: the shape at the
                                                         * left, the track and the bus's return at the right) */
        graph_dly(c, FXV_LW);
        fxv_split(t, DV_DLY, c, "DRY", "ECHO", FXV_SEND);
    } else if (pg->scope == SC_GLOBAL && id == (PS_TRK | P_REV)) {
        graph_reverb(c, FXV_LW);
        fxv_split(t, DV_REV, c, "DRY", "TAIL", FXV_SEND);
    } else if (pg->scope == SC_GLOBAL && id == (PS_TRK | P_CHOR)) {
        graph_chorus(c, FXV_LW);
        fxv_split(t, DV_CHO, c, "DRY", "WET", FXV_SEND);
    }
    else if (pg->scope == SC_GLOBAL && id == G_BPM) graph_clock(c);
    else return 0;
    return 1;
}
