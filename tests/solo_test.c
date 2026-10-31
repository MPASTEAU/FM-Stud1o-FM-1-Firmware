/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* SOLO and MUTE take a track out completely (fx.c trk_off): a demo song (the emulator's build, -DDAH_DEMO_SONG=N)
 *   - every track muted: after the ramp nothing into the mix nor the buses (their sends 0), no voice left, the CPU
 *     estimate down; the reverb's tail rings on;
 *   - track 1 soloed: the output as with tracks 2..8 muted, sample for sample (unless it ducks from one: a track
 *     soloed out still ducks; a muted one does not);
 *   - the steps (second differences) around a mute and a solo's end: no larger than the song's own (no click). */
#include "../web/emu/felucca_web.c"

static int32_t outv[3][44100 * 20 * 2];
static const char *self;
#define OV(how) ((how) == 1u ? 1u : (how) == 3u ? 2u : 0u)   /* (3: the song as it is, nothing off) */
static uint32_t run(uint32_t how, uint32_t *bad_sends, uint32_t *cpu_on, uint32_t *cpu_off)
{
    uint32_t k, b = 0, t, i, n = 0;
    web_nor_erase();
    web_boot();
    rng_state = 0x1234567u;
    transport_req = 1;
    *bad_sends = 0;
    for (k = 0; k < 20u * FS; k += 128u, b++) {
        uint32_t s = b * 128u / FS, at = b * 128u % FS < 128u;
        if (at && s == 6u) {                            /* 6 s: off */
            for (t = 0; t < NTRK; t++)
                if (how == 0u || (how == 2u && t))
                    trk[t].p[P_MUTE] = 1;
            if (how == 1u)
                mix_solo = 1u;                          /* track 1 alone (MIXER's SOLO; ui_layer.c: perf_solo) */
        }
        if (at && s == 12u) {                           /* 12 s: on again */
            for (t = 0; t < NTRK; t++)
                trk[t].p[P_MUTE] = 0;
            mix_solo = 0;
        }
        web_render(128);
        if (s >= 3u && s < 6u)
            *cpu_on = song.cpu_q8;
        if (s >= 9u && s < 12u)
            *cpu_off = song.cpu_q8;
        if (how == 0u && b * 128u >= 6u * FS + FS / 20u && s < 12u) {   /* (50 ms after: the ramp's long done) */
            for (i = 0; i < CTL; i++)
                *bad_sends += send_c[i] != 0 || send_d[i] != 0 || send_r[i] != 0;
            for (t = 0; t < NTRK; t++)
                for (i = 0; i < NVOICE; i++)
                    *bad_sends += trk[t].v[i].active;
        }
        for (t = 0; t < 128u && n < sizeof outv[0] / 4u - 1u; t++) {
            outv[OV(how)][n++] = (int32_t)(out_l[t] * 8388608.0f);
            outv[OV(how)][n++] = (int32_t)(out_r[t] * 8388608.0f);
        }
    }
    return n;
}
static int32_t step_max(const int32_t *o, uint32_t from, uint32_t to)
{
    int32_t m = 0, d;
    uint32_t i;
    for (i = from + 4u; i < to; i++) {
        d = o[i] - 2 * o[i - 2u] + o[i - 4u];          /* (the second difference: a step, not the wave's own slope) */
        d = d < 0 ? -d : d;
        m = d > m ? d : m;
    }
    return m;
}
/* each run in a process of its own (the firmware's state from power-on): ./solo_test MODE FILE runs one */
static uint32_t child(uint32_t how, const char *path, uint32_t *bad, uint32_t *on, uint32_t *off)
{
    char cmd[512];
    uint32_t n = 0;
    FILE *f;
    snprintf(cmd, sizeof cmd, "\"%s\" %u \"%s\"", self, how, path);
    if (system(cmd))
        return 0;
    f = fopen(path, "rb");
    if (!f || fread(bad, 4, 1, f) != 1 || fread(on, 4, 1, f) != 1 || fread(off, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 ||
        fread(outv[OV(how)], 4, n, f) != n)
        n = 0;
    if (f)
        fclose(f);
    return n;
}
int main(int argc, char **argv)
{
    uint32_t bad, on, off, n, ok = 1, on2, off2;
    int32_t song_step, sw, dk;
    char p0[600], p1[600];
    self = argv[0];
    if (argc == 3) {
        uint32_t how = (uint32_t)atoi(argv[1]);
        FILE *f = fopen(argv[2], "wb");
        n = run(how, &bad, &on, &off);
        fwrite(&bad, 4, 1, f); fwrite(&on, 4, 1, f); fwrite(&off, 4, 1, f); fwrite(&n, 4, 1, f);
        fwrite(outv[OV(how)], 4, n, f);
        fclose(f);
        return 0;
    }
    snprintf(p0, sizeof p0, "%s.0.raw", argv[0]);
    snprintf(p1, sizeof p1, "%s.1.raw", argv[0]);
    child(0, p0, &bad, &on, &off);
    printf("solo: every track muted: %u sends / voices left after the ramp; CPU %u%% -> %u%%  %s\n", bad, on * 100u / 256u,
           off * 100u / 256u, !bad && off < on ? "ok" : "FAIL");
    ok &= !bad && off < on;
    child(3, p1, &bad, &on2, &off2);
    song_step = step_max(outv[2], 2u * 2u * FS, 2u * 20u * FS);   /* (the song's own, nothing off) */
    sw = step_max(outv[0], 2u * 6u * FS - 2u * 256u, 2u * 6u * FS + 2u * 4096u);
    printf("solo: the mute's steps %d (the song's %d)  %s\n", sw, song_step, sw <= song_step ? "ok" : "FAIL");
    ok &= sw <= song_step;
    n = child(2, p0, &bad, &on2, &off2);
    child(1, p1, &bad, &on, &off);
    web_nor_erase();
    web_boot();
    dk = trk[0].p[P_DKSRC] || trkx[0].sc2;              /* (a DUCK source soloed out still ducks it: not as muted) */
    printf("solo: track 1 soloed = tracks 2..8 muted, sample for sample: %s; CPU %u%% -> %u%%\n",
           dk ? "(its DUCK: skipped)" : n && !memcmp(outv[0], outv[1], n * 4u) ? "ok" : "FAIL", on * 100u / 256u,
           off * 100u / 256u);
    ok &= (dk || (n && !memcmp(outv[0], outv[1], n * 4u))) && off < on;
    sw = step_max(outv[1], 2u * 12u * FS - 2u * 256u, 2u * 12u * FS + 2u * 4096u);
    printf("solo: the solo's end: steps %d (the song's %d)  %s\n", sw, song_step, sw <= song_step ? "ok" : "FAIL");
    ok &= sw <= song_step;
    remove(p0);
    remove(p1);
    return ok ? 0 : 1;
}
