/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Long patterns (LEN up to LEN_MAX = 256: pages of NSTEP, page 0 on the track, the others in the pattern store, played
 * through the audio ISR's ring that the main loop fills: pstore.c pg_*, song_chain.c pgr), on the real sequencer, pattern
 * store, projects and SONG (persistence_test.c's NOR flash):
 *   playback   LEN 128 / 256: each step's note-on at its sample across the pages, two loops, the main loop only every
 *              ~23 ms; nothing late (pg_late)
 *   a held note 200 steps of TIEs over four pages: one note-on, one note-off at its end
 *   editing    page 3 (step 200) edited stopped (the UI's ed_st), then heard; NOTE LEN over a page's end
 *   recording  live recording held across steps 62 .. 70: the NOTE, its TIEs on the next page, played back
 *   automation an event on step 150: it sounds there; pattern 2 and back keeps it
 *   projects   saved (PFS 9) and opened again: the pages' steps, LEN, their automation; a project without one: PFS 8
 *   SONG       a section on a 128-step pattern: its page 1 heard, then the next section on time
 *   LEN        shrunk to 64 (step 100 not heard), grown back (heard again)
 *   patterns   a copy (NEW from a copy) has the pages, a deletion takes them */
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
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
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
    printf("longpat: %-72s %s\n", what, ok ? "ok" : "FAIL");
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

/* LEN len: a note every 4 steps, each its own pitch; two loops with the main loop every ~23 ms */
static int t_play(uint32_t len)
{
    track_t *t = setup(len);
    uint32_t i, j = 0, ok = 1, n = 0;
    char what[96];
    for (i = 0; i < len; i += 4u)
        put(t, i, 30u + i / 4u);
    pg_service();
    ok &= ps_find(0, PS_PG(1, 1)) >= 0 && (len <= 128u || ps_find(0, PS_PG(1, 3)) >= 0) && t->step[0].note[0] == 30u;
    play();
    ev_start(t);
    run(t, 2u * len * S, 1024u);
    for (j = 0; j < nev; j++) {
        uint32_t k;
        if (!ev[j].on)
            continue;
        k = n % (len / 4u) * 4u;                         /* (the step it must be) */
        ok &= ev[j].note == 30u + k / 4u && at_step(ev[j].at, (n / (len / 4u)) * len + k);
        n++;
    }
    stop(t);
    snprintf(what, sizeof what, "LEN %u: %u note-ons, each on its step over every page, two loops; none late", len, n);
    return check(what, ok && n == 2u * len / 4u && !pg_late);
}
/* a note held over 200 steps (a NOTE, 199 TIEs: four pages) */
static int t_held(void)
{
    track_t *t = setup(256);
    uint32_t j, ons = 0, offs = 0, on_at = 0, off_at = 0;
    put(t, 10, 60);
    note_set_len(t, 10, 200);
    pg_service();
    play();
    ev_start(t);
    run(t, 256u * S, 1024u);
    for (j = 0; j < nev; j++)
        if (ev[j].note == 60u) {
            if (ev[j].on) ons++, on_at = ev[j].at;
            else offs++, off_at = ev[j].at;
        }
    stop(t);
    return check("a note of 200 steps (steps 11..210 TIEs over 4 pages): one note-on at step 11, one note-off at its end",
                 note_len(t, 10) == 200u && ons == 1u && offs == 1u && at_step(on_at, 10) && off_at >= 209u * S &&
                 off_at <= 211u * S + 2u * CTL && !pg_late);
}
/* page 3 edited stopped (step 201), then played; a NOTE LEN across a page's end */
static int t_edit(void)
{
    int bad = 0;
    track_t *t = setup(256);
    uint32_t j, hit = 0, other = 0;
    put(t, 200, 77);
    pg_service();
    bad += check("page 3 edited stopped: its record (pattern 1, page 3), the track's page 0 untouched",
                 ps_find(0, PS_PG(1, 3)) >= 0 && ps_find(0, PS_PG(1, 1)) < 0 && t->step[200 % NSTEP].time == ST_REST &&
                 ui_st(t, 200)->note[0] == 77u);
    play();
    ev_start(t);
    run(t, 202u * S, 1024u);
    for (j = 0; j < nev; j++)
        if (ev[j].on) {
            hit += ev[j].note == 77u && at_step(ev[j].at, 200);
            other += ev[j].note != 77u;
        }
    stop(t);
    bad += check("  .. then played: heard on step 201, alone", hit == 1u && !other && !pg_late);
    play();                                              /* (playing: a step just ahead, on page 1, edited) */
    ev_start(t);
    run(t, 66u * S, 1024u);
    put(t, 72, 88);
    run(t, 8u * S, 1024u);
    for (j = 0, hit = 0; j < nev; j++)
        hit += ev[j].on && ev[j].note == 88u && at_step(ev[j].at, 72);
    stop(t);
    bad += check("playing step 67, step 73 (in the ring already) edited: heard at once", hit == 1u && !pg_late);
    put(t, 60, 61);
    j = note_set_len(t, 60, 10);
    pg_service();
    bad += check("a NOTE's LEN over page 0's end: steps 62..70 TIEs, on page 1 too", j == 10u && ui_st(t, 69)->time == ST_TIE &&
                 ui_st(t, 64)->time == ST_TIE && ui_st(t, 70)->time == ST_REST && note_len(t, 60) == 10u);
    return bad;
}
/* live recording: a key held from step 63 into step 71 (pressed just after 62's start, let go early in 70) */
static track_t *rt;
static void rec_keys(uint32_t at)
{
    if (at >= 62u * S + S / 8u && at < 62u * S + S / 8u + CTL)
        input_on(rt, 50, 100);
    if (at >= 70u * S + S / 8u && at < 70u * S + S / 8u + CTL)
        input_off(rt, 50);
}
static int t_rec(void)
{
    int bad = 0;
    track_t *t = rt = setup(128);
    uint32_t j, i, ok = 1, ons = 0, on_at = 0, off_at = 0;
    play();
    ev_start(t);
    run(t, CTL, 0);
    song.rec = 1;                                        /* (armed, playing: no count-in) */
    blk_hook = rec_keys;
    run(t, 80u * S, 1024u);
    blk_hook = 0;
    song.rec = 0;
    stop(t);
    pg_service();
    for (i = 63; i < 70u; i++)
        ok &= ui_st(t, i)->time == ST_TIE;
    bad += check("recorded across page 0's end: step 63 the NOTE, 64..70 TIEs (page 1), 71 not",
                 ui_st(t, 62)->time == ST_NOTE && ui_st(t, 62)->note[0] == 50u && ok && ui_st(t, 70)->time != ST_TIE &&
                 ps_find(0, PS_PG(1, 1)) >= 0);
    play();
    ev_start(t);
    run(t, 72u * S, 1024u);
    for (j = 0; j < nev; j++)
        if (ev[j].note == 50u) {
            if (ev[j].on) ons++, on_at = ev[j].at;
            else off_at = ev[j].at;
        }
    stop(t);
    bad += check("  .. played back: one note-on on step 63 (+ its recorded NUDGE), held to step 70's end",
                 ons == 1u && on_at > 62u * S && on_at < 62u * S + S / 4u + 2u * CTL &&
                 off_at >= 69u * S && off_at <= 70u * S + 2u * CTL && !pg_late);
    return bad;
}
/* automation on page 2 */
static int t_motion(void)
{
    int bad = 0;
    track_t *t = setup(160);
    int16_t before, after;
    t->p[P_REV] = 0;
    trkx[trk_index(t) % NTRK].home[0] = P_REV + 1;     /* (Dah: automation on HOME's knobs only: REV ASSIGN'd there) */
    put(t, 150, 70);
    bad += check("an automation event on step 151 (page 2): stored, page 0's records as before",
                 !motion_set_event(t, 150, P_REV, 100) && motion_find(t, 150, P_REV) >= 0 && motion.event[0].place == MPLACE(0, 150) &&
                 motion_find(t, 150 % NSTEP, P_REV) < 0);
    pg_service();
    play();
    ev_start(t);
    run(t, 149u * S + S / 2u, 1024u);
    before = t->p[P_REV];
    run(t, S, 1024u);
    after = t->p[P_REV];
    stop(t);
    bad += check("  .. it plays there (REV 0 before step 151, 100 on it)", before == 0 && after == 100);
    bad += check("  .. pattern 2 and back: the event and the page's note still there",
                 !pattern_go(0, 2) && motion_find(t, 150, P_REV) < 0 && ui_st(t, 150)->time == ST_REST && !pattern_go(0, 1) &&
                 motion_find(t, 150, P_REV) >= 0 && ui_st(t, 150)->note[0] == 70u && t->p[P_SLEN] == 160);
    return bad;
}
/* a project with long patterns, saved and opened again */
static int t_project(void)
{
    int bad = 0;
    track_t *t = setup(192);
    uint32_t id;
    pfs_hdr_t h;
    put(t, 70, 71);
    put(t, 130, 72);
    put(t, 190, 73);
    (void)motion_set_lock(t, 130, P_REV, 90);
    trk[1].p[P_SLEN] = 32;
    trk[1].step[3] = (step_t){{40}, 1, ST_NOTE, 0, 0};
    bad += check("NEW from a copy: pattern 2 with the pages", !pattern_new(0, 1) && ps_num(0) == 2u && ui_st(t, 190)->note[0] == 73u);
    put(t, 100, 74);                                     /* (pattern 2's own) */
    pg_service();
    bad += check("  .. its own pages (pattern 1's untouched)", ps_find(0, PS_PG(1, 1)) >= 0 && ps_find(0, PS_PG(2, 1)) >= 0);
    bad += check("saved: a project of PFS 9", !project_save_cur() && (id = pfs_cur()) != 0u &&
                 pfs_head(pfs.start[id] - 1u, &h) && h.ver == 9u);
    memset(trk, 0, sizeof trk);                          /* (the music gone, the flash kept) */
    host_tracks_init();
    motion.count = 0;
    ps_reset();
    bad += check("opened again: LEN 192, the pages' steps, page 2's lock, track 2 as it was",
                 !pfs_read(id) && t->p[P_SLEN] == 192 && ps_num(0) == 2u && ui_st(t, 70)->note[0] == 71u &&
                 ui_st(t, 100)->note[0] == 74u && ui_st(t, 190)->note[0] == 73u && motion_find(t, 130, P_REV) >= 0 &&
                 trk[1].p[P_SLEN] == 32 && trk[1].step[3].note[0] == 40u);
    bad += check("  .. pattern 1 too (its pages, its automation)", !pattern_go(0, 1) && ui_st(t, 100)->time == ST_REST &&
                 ui_st(t, 130)->note[0] == 72u && motion_find(t, 130, P_REV) >= 0);
    bad += check("  .. deleted: pattern 2's pages gone, pattern 1's kept",
                 (pattern_del(0, 2), ps_find(0, PS_PG(2, 1)) < 0 && ps_find(0, PS_PG(1, 1)) >= 0));
    t = setup(64);
    put(t, 5, 50);
    bad += check("a project without a long pattern: PFS 8, as before", !project_save_cur() && (id = pfs_cur()) != 0u &&
                 pfs_head(pfs.start[id] - 1u, &h) && h.ver == 8u);
    return bad;
}
/* SONG: section 1 on track 1's pattern 1 (LEN 128: step 101 on page 1), section 2 its pattern 2 (LEN 16) */
static int t_song(void)
{
    track_t *t = setup(128);
    uint32_t j, a = 0, b = 0, c = 0, k;
    put(t, 0, 80);
    put(t, 100, 81);
    (void)pattern_new(0, 0);                              /* (pattern 2, empty) */
    t->p[P_SLEN] = 16;
    put(t, 0, 90);
    (void)pattern_go(0, 1);
    chain_config.count = 2;
    for (k = 0; k < NTRK; k++)
        chain_config.row[0].slot[k] = chain_config.row[1].slot[k] = CHAIN_SILENT;
    chain_config.row[0].slot[0] = 1;
    chain_config.row[1].slot[0] = 2;
    chain_config.row[0].repeat = chain_config.row[1].repeat = 1;
    (void)chain_prepare();
    ev_start(t);
    run(t, 128u * S + 4u * S, 1024u);
    for (j = 0; j < nev; j++)
        if (ev[j].on) {
            a += ev[j].note == 80u && at_step(ev[j].at, 0);
            b += ev[j].note == 81u && at_step(ev[j].at, 100);
            c += ev[j].note == 90u && at_step(ev[j].at, 128);
        }
    stop(t);
    return check("SONG: a 128-step section (its step 101 on page 1 heard), the next one on step 129", a == 1u && b == 1u &&
                 c == 1u && !pg_late && t->p[P_SLEN] == 128);
}
/* LEN 128 -> 64 -> 128 */
static int t_len(void)
{
    track_t *t = setup(128);
    uint32_t j, a = 0, b = 0;
    put(t, 0, 80);
    put(t, 100, 81);
    t->p[P_SLEN] = 64;
    pg_service();
    play();
    ev_start(t);
    run(t, 130u * S, 1024u);
    for (j = 0; j < nev; j++)
        a += ev[j].on && ev[j].note == 81u;
    stop(t);
    t->p[P_SLEN] = 128;
    play();
    ev_start(t);
    run(t, 102u * S, 1024u);
    for (j = 0; j < nev; j++)
        b += ev[j].on && ev[j].note == 81u && at_step(ev[j].at, 100);
    stop(t);
    return check("LEN 128 -> 64: step 101 not heard (kept); -> 128 again: heard", !a && b == 1u && ps_find(0, PS_PG(1, 1)) >= 0);
}
/* NEW from a copy and DELETE on every track (SAVE > PATTERN) */
static int t_copy(void)
{
    track_t *t = setup(256);
    put(t, 255, 99);
    return check("PATTERN COPY (every track): the copy's pages; DELETE: they go",
                 !pattern_new_all(1) && ps_num(0) == 2u && ui_st(t, 255)->note[0] == 99u && ps_find(0, PS_PG(1, 3)) >= 0 &&
                 (pattern_del_all(2), ps_find(0, PS_PG(2, 3)) < 0) && ps_num(0) == 1u && ui_st(t, 255)->note[0] == 99u);
}

int main(void)
{
    int bad = 0;
    reset();
    S = div_samples(2);
    bad += t_play(128);
    bad += t_play(256);
    bad += t_play(100);
    bad += t_held();
    bad += t_edit();
    bad += t_rec();
    bad += t_motion();
    bad += t_project();
    bad += t_song();
    bad += t_len();
    bad += t_copy();
    if (bad)
        printf("LONG PATTERN TEST FAILED (%d)\n", bad);
    else
        printf("longpat: all ok\n");
    return bad != 0;
}
