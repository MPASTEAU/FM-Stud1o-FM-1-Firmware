/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FINE LEN (core.h MOTION_FINE: a note's length to a tenth of a step, a record on its last step), on the real sequencer,
 * pattern store and projects (persistence_test.c's NOR flash), a POLY track at 120 BPM, 1/16, GATE 50 %:
 *   playback   LEN 2.3: one note-on at step 1, its off 2.3 steps on (at the block it falls in); 0.5 (one step); a NUDGE of
 *              +4/16 (the 0.5 from where it plays, the 2.3's end on its last step as the GATE's would be); LEN 128 across
 *              page 0's end (steps 63 .. 65: the ring); two overlapping notes ending on their own steps; SLIDE on its
 *              last step (no slide out); RATCH x2 (its GATE, as before)
 *   QUANTIZE   ON: as a whole note (the same note-ons and offs as without the record); 50 %: half way to the GATE's off
 *   UI         note_len10 / note_set_len10 (2.3 -> 3 whole: no record; 0.5; the LEN knob: tests/ui155_test.c), the
 *              record gone where no note ends any more (fine_prune)
 *   recording  a POLY note held over steps, let go late in its last: its tenths
 *   projects   saved (PFS) and opened again: the record kept, played the same */
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
    printf("len: %-76s %s\n", what, ok ? "ok" : "FAIL");
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




#define NA 60u
#define NB 64u
typedef struct { uint32_t ons, offs, on_at, off_at; } nt_t;
static nt_t heard[2];
static void listen(track_t *t, uint32_t samples)
{
    uint32_t j;
    memset(heard, 0, sizeof heard);
    play();
    ev_start(t);
    run(t, samples, 1024u);
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
/* heard once, on at `on` and off at `off` samples (each at the block after it: the ISR's events come a block at a time) */
static int heard_at(const nt_t *h, uint32_t on, uint32_t off)
{
    return h->ons == 1u && h->offs == 1u && h->on_at >= on && h->on_at <= on + 2u * CTL && h->off_at >= off &&
           h->off_at <= off + 3u * CTL;   /* (an off counted from a
                                                         * late play's block) */
}
static int same_events(const ev_t *a, uint32_t na, const ev_t *b, uint32_t nb)
{
    return na == nb && !memcmp(a, b, na * sizeof a[0]);
}
static ev_t ev0[8192];
static uint32_t nev0;

/* note x at step i, its length l10 tenths (note_set_len10), LEN len */
static track_t *one(uint32_t len, uint32_t i, uint32_t x, uint32_t l10)
{
    track_t *t = setup(len);
    t->p[P_SQNT] = 0;                                    /* QUANTIZE OFF: as set */
    put(t, i, x);
    if (l10)
        (void)note_set_len10(t, i, l10);
    pgc_flush_all();
    return t;
}

static int t_play(void)
{
    int bad = 0;
    track_t *t = one(16, 0, NA, 23);
    bad += check("LEN 2.3: NOTE, TIE, TIE, its tenths (3) on step 3, note_len10 23",
                 note_len10(t, 0) == 23u && st_kind(ui_st(t, 1)) == ST_TIE && st_kind(ui_st(t, 2)) == ST_TIE &&
                 st_kind(ui_st(t, 3)) != ST_TIE && fine_at(&motion, 0, 2) == 3u && motion.count == 1u);
    listen(t, 4u * S);
    bad += check("  .. played: on at 0, off at 2.3 steps", heard_at(&heard[0], 0, 2u * S + 3u * S / 10u));
    t = one(16, 0, NA, 5);
    bad += check("LEN 0.5: one step, its tenths 5", note_len10(t, 0) == 5u && st_kind(ui_st(t, 1)) != ST_TIE &&
                                                     fine_at(&motion, 0, 0) == 5u);
    listen(t, 2u * S);
    bad += check("  .. played: off at 0.5 step", heard_at(&heard[0], 0, S / 2u));
    t = one(16, 0, NA, 1);
    listen(t, 2u * S);
    bad += check("LEN 0.1: off at a tenth", note_len10(t, 0) == 1u && heard_at(&heard[0], 0, S / 10u));
    t = one(16, 0, NA, 5);
    step_set_nudge(ed_st(t, 0), 4);
    listen(t, 2u * S);
    bad += check("LEN 0.5, NUDGE +4/16: on a quarter step late, off half a step after", heard_at(&heard[0], S / 4u, S / 4u + S / 2u));
    t = one(16, 0, NA, 23);
    step_set_nudge(ed_st(t, 0), 4);
    listen(t, 4u * S);
    bad += check("LEN 2.3, NUDGE +4/16: on late, off at 2.3 steps (on its last step, as the GATE)",
                 heard_at(&heard[0], S / 4u, 2u * S + 3u * S / 10u));
    t = one(16, 0, NA, 23);
    ed_st(t, 2)->flags |= SF_SLIDE;
    put(t, 3, NB);
    listen(t, 5u * S);
    bad += check("LEN 2.3, SLIDE on its last step: off at 2.3 all the same, the next note struck",
                 heard_at(&heard[0], 0, 2u * S + 3u * S / 10u) && heard[1].ons == 1u);
    t = one(16, 0, NA, 5);
    step_set_ratchet(ed_st(t, 0), 2);
    listen(t, S);
    bad += check("LEN 0.5, RATCH x2: two parts at their GATE (its tenths not heard)", heard[0].ons == 2u && heard[0].offs == 2u &&
                                                                                        heard[0].off_at <= S / 4u + 2u * CTL);
    return bad;
}
/* A from step 1, 4.5 long, B from step 3, 1.2 long: each ends on its own step */
static int t_overlap(void)
{
    int bad = 0;
    track_t *t = one(16, 0, NA, 45);
    step_t *s = ed_st(t, 2);
    s->time = ST_NOTE;                                   /* (B pressed at step 3, A going on beside it) */
    s->n = 2;
    s->note[0] = NA;
    s->note[1] = NB;
    s->vel = 100;
    st_set_cont(s, 1u);
    (void)note_set_len10(t, 2, 12);
    bad += check("overlaps: A 4.5 beside B 1.2 (each its own step's tenths)", note_len10(t, 0) == 45u && note_len10(t, 2) == 12u);
    listen(t, 6u * S);
    bad += check("  .. played: A off at 4.5, B at 3.2", heard_at(&heard[0], 0, 4u * S + S / 2u) &&
                                                          heard_at(&heard[1], 2u * S, 3u * S + 2u * S / 10u));
    return bad;
}
/* LEN 128: a note at step 63, 2.3 long (its end on page 1, the ring) */
static int t_page(void)
{
    track_t *t = one(128, 62, NA, 23);
    int ok = note_len10(t, 62) == 23u && fine_at(&motion, 0, 64) == 3u;
    run(t, CTL, 0);
    play();
    ev_start(t);
    memset(heard, 0, sizeof heard);
    run(t, 66u * S, 1024u);
    stop(t);
    for (uint32_t j = 0; j < nev; j++)
        if (ev[j].note == NA) {
            nt_t *h = &heard[0];
            if (ev[j].on) { if (!h->ons++) h->on_at = ev[j].at; }
            else if (!h->offs++) h->off_at = ev[j].at;
        }
    return check("LEN 128: steps 63..65 (page 1's first two), off at 64.3 steps", ok && !pg_late &&
                 heard_at(&heard[0], 62u * S, 64u * S + 3u * S / 10u));
}
static int t_quantize(void)
{
    int bad = 0;
    track_t *t = one(16, 0, NA, 30);                     /* (whole: the reference) */
    t->p[P_SQNT] = 1;
    listen(t, 4u * S);
    memcpy(ev0, ev, sizeof ev);
    nev0 = nev;
    t = one(16, 0, NA, 23);
    t->p[P_SQNT] = 1;
    listen(t, 4u * S);
    bad += check("QUANTIZE ON: LEN 2.3 plays as 3 whole steps (every on / off the same)", same_events(ev, nev, ev0, nev0));
    t = one(16, 0, NA, 23);
    t->p[P_SQNT] = 3;
    listen(t, 4u * S);
    bad += check("QUANTIZE 50 %: half way from 2.3 to the GATE's end (3): off at 2.65", heard_at(&heard[0], 0, 2u * S + 65u * S / 100u));
    t = one(16, 0, NA, 5);
    t->p[P_SQNT] = 2;
    listen(t, 2u * S);
    bad += check("QUANTIZE 25 %: LEN 0.5 a quarter of the way to GATE 50 %: 0.5", heard_at(&heard[0], 0, S / 2u));
    t = one(16, 0, NA, 1);
    t->p[P_SQNT] = 4;
    listen(t, 2u * S);
    bad += check("QUANTIZE 75 %: LEN 0.1 three quarters of the way to GATE 50 %: 0.4", heard_at(&heard[0], 0, 4u * S / 10u - 1u));
    return bad;
}
static int t_ui(void)
{
    int bad = 0;
    track_t *t = one(16, 0, NA, 23);
    bad += check("2.3 -> 3: whole, no record; -> 0.5: one step, its record", note_set_len10(t, 0, 30) == 30u && !motion.count &&
                 note_len10(t, 0) == 30u && note_set_len10(t, 0, 5) == 5u && motion.count == 1u && fine_at(&motion, 0, 0) == 5u &&
                 st_kind(ui_st(t, 1)) != ST_TIE);
    (void)note_set_len10(t, 0, 46);
    {
        uint32_t k;
        for (k = 0; k < 5u; k++)
            step_clear(ed_st(t, k));
    }
    fine_prune();
    bad += check("the note gone (its steps cleared): its record too (fine_prune)", !motion.count);
    return bad;
}
/* REC + PLAY (POLY): A pressed early in step 1, let go 0.6 into step 3 */
static track_t *rt;
static void rec_keys(uint32_t at)
{
    if (at >= S / 8u && at < S / 8u + CTL) input_on(rt, NA, 100);
    if (at >= 2u * S + 6u * S / 10u && at < 2u * S + 6u * S / 10u + CTL) input_off(rt, NA);
}
static int t_record(void)
{
    track_t *t = setup(16);
    uint32_t l;
    t->p[P_SQNT] = 0;
    rt = t;
    play();
    ev_start(t);
    run(t, CTL, 0);
    song.rec = 1;
    blk_hook = rec_keys;
    run(t, 5u * S, 1024u);
    blk_hook = 0;
    song.rec = 0;
    stop(t);
    l = note_len10(t, 0);
    return check("recorded: held into step 3, let go 0.6 into it: LEN 2.6 or 2.7 (the tenth it was let go in)", l == 26u || l == 27u);
}
static int t_project(void)
{
    int bad = 0;
    track_t *t = one(128, 0, NA, 23);
    uint32_t id = 0;
    put(t, 70, NB);
    (void)note_set_len10(t, 70, 15);
    pgc_flush_all();
    bad += check("saved: a project", !project_save_cur() && (id = pfs_cur()) != 0u);
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    t->p[P_VOICE] = V_POLY;
    motion.count = 0;
    ps_reset();
    bad += check("  .. opened again: LEN 2.3 (page 0), 1.5 (page 1)",
                 !pfs_read(id) && note_len10(t, 0) == 23u && note_len10(t, 70) == 15u);
    t->p[P_SQNT] = 0;
    pg_service();
    listen(t, 4u * S);
    bad += check("  .. played: off at 2.3", heard_at(&heard[0], 0, 2u * S + 3u * S / 10u));
    return bad;
}

int main(void)
{
    int bad = 0;
    reset();
    S = div_samples(2);
    bad += t_play();
    bad += t_overlap();
    bad += t_page();
    bad += t_quantize();
    bad += t_ui();
    bad += t_record();
    bad += t_project();
    if (bad)
        printf("LEN TEST FAILED (%d)\n", bad);
    else
        printf("len: all ok\n");
    return bad != 0;
}
