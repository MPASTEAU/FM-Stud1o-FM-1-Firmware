/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A track deleted (ui.c track_delete: MIXER or HOME, OCT- held, answered), any one: track 1 of 4, on the real sequencer,
 * pattern store, motion store, SONG and projects (persistence_test.c's NOR flash):
 *   moved     tracks 2..4 now 1..3: their sounds, patterns (the one on the track and one in the heap), their automation
 *             (track 3's event now track 2's), the SONG's lanes, SIDECHAIN SRC (track 4's on track 3: now 3's on 2;
 *             track 2's on the deleted one: OFF), the armed / solo bits
 *   plays     the SONG (two sections, patterns 1 and 2) after it: each track's note-ons and offs at the samples they
 *             had before, as the track it was
 *   projects  saved, the music gone, opened again: the same */
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
    printf("trackdel: %-72s %s\n", what, ok ? "ok" : "FAIL");
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

/* every track's note-ons / offs over `samples` (the SONG playing), into tv[k] */
typedef struct { uint32_t n; ev_t e[512]; } tev_t;
static tev_t tv[NTRK];
static void run_all(uint32_t samples)
{
    static uint32_t age[NTRK][NVOICE];
    static uint8_t gt[NTRK][NVOICE];
    uint32_t s, k, i;
    memset(tv, 0, sizeof tv);
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++) {
            age[k][i] = trk[k].v[i].age;
            gt[k][i] = trk[k].v[i].active && trk[k].v[i].gate;
        }
    for (s = 0; s < samples; s += CTL) {
        events_block(CTL);
        if (s % 1024u < CTL) {
            pg_service();
            chain_poll();
        }
        for (k = 0; k < NTRK; k++)
            for (i = 0; i < NVOICE; i++) {
                const voice_t *v = &trk[k].v[i];
                uint8_t g = (uint8_t)(v->active && v->gate);
                if (v->age != age[k][i]) {
                    if (tv[k].n < 512u) tv[k].e[tv[k].n++] = (ev_t){s, v->note, 1};
                    age[k][i] = v->age;
                } else if (gt[k][i] && !g && tv[k].n < 512u)
                    tv[k].e[tv[k].n++] = (ev_t){s, v->note, 0};
                gt[k][i] = g;
            }
    }
    transport_req = 2;
    events_block(CTL);
    chain.running = chain.armed = 0;
}
static uint32_t S;
static void put(track_t *t, uint32_t i, uint32_t note)
{
    step_t *s = &t->step[i];
    memset(s, 0, sizeof *s);
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)note;
    s->vel = 100;
}
static uint32_t song_play(void)                  /* the SONG from the top, both sections: tv; 0 done */
{
    uint32_t rc;
    chain_from_row = 0;
    if ((rc = chain_prepare()) != 0)
        return rc;
    run_all(2u * 16u * S + 4u * CTL);
    return 0;
}
static int same(const tev_t *a, const tev_t *b)
{
    return a->n == b->n && a->n > 4u && !memcmp(a->e, b->e, a->n * sizeof a->e[0]);
}

int main(void)
{
    int bad = 0, ok;
    static tev_t before[NTRK];
    uint32_t k, i, n;
    uint8_t ev_trk = 9;
    reset();
    S = div_samples(2);
    song.ntrk = 4;
    for (k = 0; k < 4u; k++) {                    /* each track: pattern 1 and 2, notes of its own */
        trk[k].p[P_SLEN] = 16;
        for (i = k; i < 16u; i += 4u)
            put(&trk[k], i, 40u + 10u * k + i / 4u);
        ok = !pattern_new(k, 0);
        for (i = (k + 2u) % 4u; i < 16u; i += 4u)
            put(&trk[k], i, 45u + 10u * k + i / 4u);
        (void)pattern_go(k, 1);
    }
    (void)motion_set_event(&trk[2], 6, P_DLY, 20);   /* track 3's automation */
    trk[3].p[P_DKSRC] = 3;                       /* track 4 ducks on track 3 */
    trk[1].p[P_DKSRC] = 1;                       /* track 2 on track 1 (the one deleted) */
    chain_defaults(&chain_config);
    chain_config.count = 2;
    for (k = 0; k < NTRK; k++) {
        chain_config.row[0].slot[k] = k < 4u ? 1u : (uint8_t)CHAIN_SILENT;
        chain_config.row[1].slot[k] = k < 4u ? 2u : (uint8_t)CHAIN_SILENT;
    }
    chain_config.row[0].repeat = chain_config.row[1].repeat = 1;
    song.rec = 1u << 2;                          /* track 3 armed */
    bad += check("4 tracks, two patterns each, the SONG of both: played", !song_play() && tv[1].n > 4u && tv[3].n > 4u);
    memcpy(before, tv, sizeof tv);
    track_select(0);
    track_delete(0);
    for (i = 0; i < motion.count; i++)
        if (motion.event[i].param == P_DLY)
            ev_trk = (uint8_t)MP_TRK(&motion.event[i]);
    n = trk_count();
    ok = n == 3u && ps_exists(0, 2) && ps_exists(2, 2) && !ps_exists(3, 2) && ev_trk == 1u && song.rec == 1u << 1 &&
         trk[2].p[P_DKSRC] == 2 && trk[0].p[P_DKSRC] == 0 && chain_config.row[1].slot[2] == 2u &&
         chain_config.row[0].slot[3] == (uint8_t)CHAIN_SILENT && trk[0].step[1].note[0] == 50u && song.sel == 0u;
    bad += check("track 1 deleted: 2..4 now 1..3 (steps, patterns, automation, SONG lanes, SIDECHAIN SRC, armed)", ok);
    ok = !song_play();
    for (k = 0; k < 3u; k++)
        ok &= same(&tv[k], &before[k + 1u]);
    ok &= !tv[3].n;
    bad += check("  the SONG after it: each track's notes at the samples they had, as the track it was", ok);
    pfs.ok = 0;
    ok = !pfs_write(1, "DEL");
    for (k = 0; k < NTRK; k++)                   /* (the music gone) */
        track_defaults_steps(&trk[k]);
    ps_reset();
    motion.count = 0;
    chain_defaults(&chain_config);
    song.ntrk = 1;
    ok &= !pfs_read(1) && trk_count() == 3u && trk[2].p[P_DKSRC] == 2 && !song_play();
    for (k = 0; k < 3u; k++)
        ok &= same(&tv[k], &before[k + 1u]);
    bad += check("  saved as a project, the music gone, opened: three tracks playing the same", ok);
    track_select(2);
    track_delete(2);                             /* (the last) */
    ok = trk_count() == 2u && song.sel == 1u && !ps_exists(2, 2) && trk[1].p[P_DKSRC] == 0;
    bad += check("  the last one deleted: two tracks, its patterns gone, the SIDECHAIN on it OFF", ok);
    printf("%s\n", bad ? "TRACK DELETE TEST FAILED" : "track delete test passed");
    return bad != 0;
}
