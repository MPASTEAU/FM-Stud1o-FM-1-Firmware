/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Overlapping notes (core.h st_cont: each note of a NOTE step starts there or goes on from the step before), on the real
 * sequencer, pattern store and projects (persistence_test.c's NOR flash), a POLY track recorded live (REC + PLAY):
 *   recording  A held from step 1, B pressed at step 4 while A is held, A let go at step 8, B at step 12 (early in the
 *              step after: rec_release): A on 1..8, B on 4..12, each its own; the steps as the model says them
 *   playback   one note-on and one note-off each, A's off at step 8's end, B's at 12's (no retrigger of A at step 4,
 *              none of B at step 9); two loops
 *   UI         NOTE LEN of each (ui.c note_len), the roll's bars (ui_graph.c pr_walk / pr_next), B shortened to 3
 *              steps (note_set_len): A untouched, heard as such
 *   pages      the same across page 0's end (LEN 128, steps 61 .. 72)
 *   projects   saved (PFS) and opened again: the notes going on kept (pstore.c PS_GOES), played back the same
 *   MONO       the same keys on a MONO track: B ends A, as before */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t irq_start_race, irq_races;
static void fm1_irq_off(void)
{
    if (irq_start_race) {                         /* audio serviced PLAY immediately before main masks it */
        irq_start_race = 0;
        events_block(CTL);
        irq_races++;
    }
}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{ (void)x; (void)y; (void)w; (void)h; (void)c; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1;
static uint32_t erases, as_erases;               /* (as_erases: the PROJECTS' sectors, 0xB4000 .. 0xDBFFF: projfs.c) */
static uint32_t pfs_er[40];                      /* (each of those sectors' erases) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    if (off >= 0xB4000u && off < 0xDC000u) {
        as_erases++;
        pfs_er[(off - 0xB4000u) / 4096u]++;
    }
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
static void fm1_wdt_feed(void) {}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"

enum { ED_UI_STATE = 34, ED_UI_SET, ED_UI_PALETTES, ED_FAV_GET, ED_FAV_SET };
static uint8_t ed_rep[64];                        /* the reply's bytes (MENU_SET) */
static uint32_t ed_repn;
static void ed_b(uint32_t n) { if (ed_repn < sizeof ed_rep) ed_rep[ed_repn++] = (uint8_t)(n & 127u); }
static void ed_v(int32_t n) { ed_b((uint32_t)(n + 8192)); ed_b((uint32_t)(n + 8192) >> 7); }
static int32_t ed_rv(const uint8_t *a) { return (int32_t)(a[0] | (uint32_t)a[1] << 7) - 8192; }
static void ed_str(const char *s, uint32_t n) { (void)s; (void)n; }
#include "../firmware/src/editor_preferences.c"
#include "../firmware/src/editor_menu.c"


static int check(const char *what, int ok)
{
    printf("overlap: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reset(void)
{
    uint32_t k;
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&chain, 0, sizeof chain);
    chain_defaults(&chain_config);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    motion.count = 0;
    motion.on = 0;
    ps_reset();
    fm1_ms = 0;
    transport_req = 0;
    flash_ok = 1;
    pfs.ok = 0;
    pg_late = 0;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SSWING] = 0;
        trk[k].p[P_SDIV] = 2;
        trk[k].p[P_SGATE] = 64;
        trk[k].p[P_VOICE] = V_POLY;
        trk[k].p[P_AMODE] = 0;
    }
}

/* ---- the note-ons and offs of track t, at the sample of the block they come in ---- */
typedef struct { uint32_t at; uint8_t note, on; } ev_t;
static ev_t ev[8192];
static uint32_t nev, now, va[NVOICE];
static uint8_t vg[NVOICE];
static void ev_start(const track_t *t)
{
    uint32_t i;
    nev = now = 0;
    for (i = 0; i < NVOICE; i++) {
        va[i] = t->v[i].age;
        vg[i] = t->v[i].active && t->v[i].gate;
    }
}
/* the audio ISR's blocks for `samples`, the main loop (pg_service, as ui_input calls it) every svc samples (0 never);
 * hook: called before each block with the sample (recording's keys) */
static void (*blk_hook)(uint32_t at);
static void run(const track_t *t, uint32_t samples, uint32_t svc)
{
    uint32_t e, i;
    for (e = 0; e < samples; e += CTL) {
        if (blk_hook)
            blk_hook(now);
        events_block(CTL);
        now += CTL;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint8_t g = (uint8_t)(v->active && v->gate);
            if (v->age != va[i]) {
                if (nev < 8192u) ev[nev++] = (ev_t){now, v->note, 1};
                va[i] = v->age;
            } else if (vg[i] && !g && nev < 8192u)
                ev[nev++] = (ev_t){now, v->note, 0};
            vg[i] = g;
        }
        if (svc && now % svc < CTL)
            pg_service();
    }
}
static uint32_t S;                                       /* a step: 1/16 at 120 BPM */
static int at_step(uint32_t at, uint32_t i) { return at >= i * S + CTL && at < i * S + 2u * CTL; }
static void put(track_t *t, uint32_t i, uint32_t note)   /* step i (the UI's way): a note */
{
    step_t *s = ed_st(t, i);
    memset(s, 0, sizeof *s);
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)note;
    s->vel = 100;
}
static track_t *setup(uint32_t len)
{
    track_t *t;
    reset();
    t = &trk[0];
    t->p[P_SLEN] = (int16_t)len;
    return t;
}
static void play(void) { transport_req = 1; }
static void stop(const track_t *t) { transport_req = 2; run(t, CTL, 0); }



/* the keys: A (60) pressed early in step a0, let go early in a1 (its last step a1 - 1), B (64) likewise b0 .. b1 */
static track_t *rt;
static uint32_t a0, a1, b0, b1;
#define NA 60u
#define NB 64u
static int at_in(uint32_t at, uint32_t i) { return at >= i * S + S / 8u && at < i * S + S / 8u + CTL; }
static void rec_keys(uint32_t at)
{
    if (at_in(at, a0)) input_on(rt, NA, 100);
    if (at_in(at, b0)) input_on(rt, NB, 100);
    if (at_in(at, a1)) input_off(rt, NA);
    if (at_in(at, b1)) input_off(rt, NB);
}
static void record(track_t *t, uint32_t a, uint32_t ae, uint32_t b, uint32_t be)
{
    rt = t;
    a0 = a, a1 = ae, b0 = b, b1 = be;
    play();
    ev_start(t);
    run(t, CTL, 0);
    song.rec = 1;                                        /* (armed, playing: no count-in) */
    blk_hook = rec_keys;
    run(t, (be + 2u) * S, 1024u);
    blk_hook = 0;
    song.rec = 0;
    stop(t);
    pg_service();
}
/* played from step 1 for `loops` loops of LEN len: note x's ons and offs, the first on / off */
typedef struct { uint32_t ons, offs, on_at, off_at; } nt_t;
static nt_t heard[2];
static void listen(track_t *t, uint32_t len, uint32_t loops)
{
    uint32_t j;
    memset(heard, 0, sizeof heard);
    play();
    ev_start(t);
    run(t, loops * len * S, 1024u);
    stop(t);
    for (j = 0; j < nev; j++) {
        nt_t *h = ev[j].note == NA ? &heard[0] : ev[j].note == NB ? &heard[1] : 0;
        if (!h)
            continue;
        if (ev[j].on) {
            if (!h->ons++) h->on_at = ev[j].at;
        } else if (!h->offs++)
            h->off_at = ev[j].at;
    }
}
/* note x heard from step i0 to the end of step i1 - 1: one on, one off a loop (the off as a TIE ends: the step's end) */
static int span(const nt_t *h, uint32_t i0, uint32_t i1, uint32_t loops)
{
    return h->ons == loops && h->offs == loops && h->on_at > i0 * S && h->on_at < i0 * S + S / 4u + 2u * CTL &&
           h->off_at + 2u * CTL >= i1 * S && h->off_at <= i1 * S + 2u * CTL;
}
static int has(const step_t *s, uint32_t x, int cont)    /* NOTE step s holds x, going on (1) or starting (0) */
{
    uint32_t k;
    for (k = 0; k < s->n && k < 4u; k++)
        if (s->note[k] == x)
            return st_kind(s) == ST_NOTE && (int)((st_cont(s) >> k) & 1u) == cont;
    return 0;
}
static int bars(const track_t *t, uint32_t i, uint32_t len, uint32_t want)   /* the roll's bars at step i: A 1, B 2 */
{
    pr_set_t ps;
    uint32_t j, got = 0;
    pr_walk(t, i, len, &ps);
    pr_next(&ps, ui_st(t, i), i);
    for (j = 0; j < ps.n; j++)
        got |= ps.note[j] == NA ? 1u : ps.note[j] == NB ? 2u : 4u;
    return got == want;
}
/* A from step a+1 to a+8, B from a+4 to a+12 (LEN len), recorded, checked, played */
static int t_overlap(uint32_t len, uint32_t a, const char *tag)
{
    int bad = 0;
    track_t *t = setup(len);
    uint32_t i, ok = 1;
    char what[128];
    record(t, a, a + 8u, a + 3u, a + 12u);
    for (i = a + 1u; i < a + 3u; i++)
        ok &= st_kind(ui_st(t, i)) == ST_TIE;
    for (i = a + 4u; i < a + 8u; i++)
        ok &= st_kind(ui_st(t, i)) == ST_TIE;
    for (i = a + 9u; i < a + 12u; i++)
        ok &= st_kind(ui_st(t, i)) == ST_TIE;
    snprintf(what, sizeof what, "%srecorded: A's NOTE, TIEs, A on + B, TIEs, B on alone, TIEs, then nothing", tag);
    bad += check(what, ui_st(t, a)->n == 1u && has(ui_st(t, a), NA, 0) && ui_st(t, a + 3u)->n == 2u &&
                       has(ui_st(t, a + 3u), NA, 1) && has(ui_st(t, a + 3u), NB, 0) && ui_st(t, a + 8u)->n == 1u &&
                       has(ui_st(t, a + 8u), NB, 1) && ok && st_kind(ui_st(t, a + 12u)) != ST_TIE &&
                       !(st_kind(ui_st(t, a + 12u)) == ST_NOTE && ui_st(t, a + 12u)->n) && st_kind(ui_st(t, a + 8u)) == ST_NOTE);
    snprintf(what, sizeof what, "%s  .. NOTE LEN: A 8, B 9; the roll: A at %u, A + B at %u and %u, B at %u, none at %u", tag,
             a + 2u, a + 4u, a + 8u, a + 9u, a + 13u);
    bad += check(what, note_len(t, a) == 8u && note_len(t, a + 3u) == 9u && bars(t, a + 1u, len, 1u) && bars(t, a + 3u, len, 3u) &&
                       bars(t, a + 7u, len, 3u) && bars(t, a + 8u, len, 2u) && bars(t, a + 12u, len, 0u));
    listen(t, len, 2u);
    snprintf(what, sizeof what, "%s  .. played (2 loops): A on %u..%u, B on %u..%u, one on / off each a loop", tag, a + 1u,
             a + 8u, a + 4u, a + 12u);
    bad += check(what, span(&heard[0], a, a + 8u, 2u) && span(&heard[1], a + 3u, a + 12u, 2u) && !pg_late);
    return bad;
}
/* the roll's editing: B (from step 4) shortened to 3 steps, then A to 2, then B back to 9 */
static int t_edit(void)
{
    int bad = 0;
    track_t *t = setup(16);
    record(t, 0, 8, 3, 12);
    bad += check("NOTE LEN: B (step 4) 9 -> 3: B's own, A still 8 (B gone from 7..12)",
                 note_set_len(t, 3, 3) == 3u && note_len(t, 3) == 3u && note_len(t, 0) == 8u && bars(t, 6u, 16, 1u) &&
                 bars(t, 5u, 16, 3u) && bars(t, 8u, 16, 0u));
    listen(t, 16, 1u);
    bad += check("  .. played: A on 1..8, B on 4..6", span(&heard[0], 0, 8, 1u) && span(&heard[1], 3, 6, 1u));
    bad += check("A (step 1) 8 -> 2, B 3 -> 9: each its own again", note_set_len(t, 0, 2) == 2u && note_len(t, 0) == 2u &&
                 note_set_len(t, 3, 9) == 9u && note_len(t, 3) == 9u && bars(t, 2u, 16, 0u) && bars(t, 3u, 16, 2u));
    listen(t, 16, 1u);
    bad += check("  .. played: A on 1..2, B on 4..12", span(&heard[0], 0, 2, 1u) && span(&heard[1], 3, 12, 1u));
    return bad;
}
/* saved into a project (PFS), the music gone, opened again: as recorded */
static int t_project(uint32_t len, uint32_t a)
{
    int bad = 0;
    track_t *t = setup(len);
    uint32_t id;
    record(t, a, a + 8u, a + 3u, a + 12u);
    bad += check("saved: a project", !project_save_cur() && (id = pfs_cur()) != 0u);
    memset(trk, 0, sizeof trk);                          /* (the music gone, the flash kept) */
    host_tracks_init();
    t->p[P_VOICE] = V_POLY;
    motion.count = 0;
    ps_reset();
    bad += check("  .. opened again: A going on beside B, B alone after, NOTE LEN 8 / 9",
                 !pfs_read(id) && t->p[P_SLEN] == (int16_t)len && has(ui_st(t, a + 3u), NA, 1) && has(ui_st(t, a + 3u), NB, 0) &&
                 has(ui_st(t, a + 8u), NB, 1) && note_len(t, a) == 8u && note_len(t, a + 3u) == 9u);
    pg_service();
    listen(t, len, 1u);
    bad += check("  .. played: A and B each their own", span(&heard[0], a, a + 8u, 1u) && span(&heard[1], a + 3u, a + 12u, 1u));
    return bad;
}
/* MONO: B ends A (one note a step), as before */
static int t_mono(void)
{
    track_t *t = setup(16);
    t->p[P_VOICE] = V_MONO;
    record(t, 0, 8, 3, 12);
    return check("MONO: B (step 4) ends A's hold: A 1..3, B 4..12, no notes going on",
                 note_len(t, 0) == 3u && ui_st(t, 3)->n == 1u && has(ui_st(t, 3), NB, 0) && note_len(t, 3) == 9u &&
                 !st_cont(ui_st(t, 8)));
}

/* (Dah) REC MODE REPLACE: recording on through three passes of the pattern: pass 1 plays A, pass 2 plays B (its first note
 * wipes the take: A gone, every page), pass 3 plays nothing (B stays); OVERDUB keeps both. On a DRUM track lane by lane:
 * B on another lane keeps A, on A's lane wipes it. len: the pattern (128: A on page 1, its record gone too); a, b: their steps */
static uint32_t rp_len, rp_a, rp_b, rp_na, rp_nb;
static void rp_keys(uint32_t at)
{
    if (at_in(at, rp_a)) input_on(rt, rp_na, 100);
    if (at_in(at, rp_a + 1u)) input_off(rt, rp_na);
    if (at_in(at, rp_len + rp_b)) input_on(rt, rp_nb, 100);
    if (at_in(at, rp_len + rp_b + 1u)) input_off(rt, rp_nb);
}
static int rp_has(const track_t *t, uint32_t i, uint32_t x)
{
    const step_t *s = ui_st(t, i);
    uint32_t k;
    if (drum_track(t))
        return st_kind(s) == ST_NOTE && ((s->hit >> drum_lane(x)) & 1u);
    for (k = 0; k < s->n && k < 4u; k++)
        if (s->note[k] == x)
            return st_kind(s) == ST_NOTE;
    return 0;
}
static int t_replace(uint32_t len, uint32_t a, uint32_t b, int drum, int replace)
{
    track_t *t = setup(len);
    char what[128];
    int ok;
    if (drum) {
        set_engine_of(t, ENGI_DRUM);
        t->engine = t->eng_req;
    }
    rt = t; rp_len = len; rp_a = a; rp_b = b;
    rp_na = drum ? DRUM_LANE_NOTE[0] : NA; rp_nb = drum ? DRUM_LANE_NOTE[drum == 2 ? 0 : 1] : NB;   /* (drum 2: one lane) */
    rec_replace = (uint8_t)replace;
    play();
    ev_start(t);
    run(t, CTL, 0);
    song.rec = 1;
    blk_hook = rp_keys;
    run(t, 3u * len * S, 1024u);                     /* (three passes: A, B, none) */
    blk_hook = 0;
    song.rec = 0;
    stop(t);
    pg_service();
    rec_replace = 0;
    ok = rp_has(t, b, rp_nb) && (replace && drum != 1 ? !rp_has(t, a, rp_na) : rp_has(t, a, rp_na));   /* (Dah: DRUM REPLACE
                                                     * wipes the lane played only: another lane's A stays) */
    snprintf(what, sizeof what, "REC MODE %s%s (LEN %u): pass 1 A on %u, pass 2 B on %u, pass 3 none: %s", replace ? "REPLACE" : "OVERDUB",
             drum == 1 ? " on DRUM, two lanes" : drum ? " on DRUM, one lane" : "", len, a + 1u, b + 1u,
             replace && drum != 1 ? "B alone" : "A and B");
    return check(what, ok);
}

int main(void)
{
    int bad = 0;
    reset();
    S = div_samples(2);
    bad += t_overlap(16, 0, "");
    bad += t_overlap(128, 60, "LEN 128, over page 0's end: ");
    bad += t_edit();
    bad += t_project(16, 0);
    bad += t_project(128, 60);
    bad += t_mono();
    bad += t_replace(16, 2, 6, 0, 1);
    bad += t_replace(16, 2, 6, 0, 0);
    bad += t_replace(128, 100, 5, 0, 1);
    bad += t_replace(128, 100, 5, 0, 0);
    bad += t_replace(16, 2, 6, 1, 1);
    bad += t_replace(16, 2, 6, 2, 1);
    bad += t_replace(128, 100, 5, 2, 1);
    bad += t_replace(128, 100, 5, 1, 1);
    if (bad)
        printf("OVERLAP TEST FAILED (%d)\n", bad);
    else
        printf("overlap: all ok\n");
    return bad != 0;
}
