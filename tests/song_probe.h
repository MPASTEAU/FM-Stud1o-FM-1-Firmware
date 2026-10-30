/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* A song played block by block and hashed (tests/ui_test.c test_song_lanes): what every track's sequencer does (its step,
 * its sounding notes, LEN / DIV, the automated REV) and the song's row and repeats, every audio block, until the song
 * stops. Rows of one slot each, as every song before lanes (1.2) had: the same source compiles against the firmware of
 * before (src/ of 1.2 before lanes, chain_row_t {slot, repeat}), which gave SONG_PROBE_GOLDEN. Included after the UI
 * sources and ui_test.c's helpers (ui_power_on, project_save) */
#ifndef NTRK_V10
#define NTRK_V10 NTRK                              /* (the firmware of before: four tracks) */
#endif
#ifdef CHAIN_SILENT
static chain_row_t probe_row(uint32_t s, uint32_t r)  /* (tracks 5..8 silent, the song of before) */
{
    chain_row_t w = chain_row_of(s, r);
    for (uint32_t k = NTRK_V10; k < NTRK; k++)
        w.slot[k] = CHAIN_SILENT;
    return w;
}
#define PROBE_ROW(s, r) probe_row((s) + 1u, r)       /* (the slots A, B: patterns 1, 2 (pat_keep)) */
#define PROBE_START() (pat_park(), song_round = 0)   /* (Dah: to its end, as before)*/
#define PROBE_SAVE(s) pat_keep((s) + 1u)
#define PROBE_POLL() chain_poll()                     /* (the main loop readies the next section) */
#else
#define PROBE_START()
#define PROBE_SAVE(s) project_save(s)
#define PROBE_POLL()
#define PROBE_ROW(s, r) ((chain_row_t){s, r})
#endif
#define SONG_PROBE_GOLDEN 0x8E72200Du              /* (Dah: a TIE into another note held till it starts; before: 0x676A420D,
                                                    * the firmware before lanes, bddbbd8), 11026 blocks */
#define SONG_PROBE_BLOCKS 11026u
static uint32_t song_probe_mix(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t song_probe(uint32_t *blocks)
{
    static const int16_t LEN[2][NTRK_V10] = {{16, 12, 7, 5}, {8, 16, 10, 3}}, DIV[2][NTRK_V10] = {{2, 1, 2, 3}, {1, 2, 0, 2}};
    uint32_t s, k, i, h = 2166136261u, n = 0;
    ui_power_on();
    PROBE_START();
    for (s = 0; s < 2u; s++) {
        for (k = 0; k < NTRK_V10; k++) {
            track_t *t = &trk[k];
            track_defaults_steps(t);
            motion_clear(t);
            for (i = 0; i < NSTEP; i += 1u + (k + s) % 3u) {
                t->step[i].note[0] = (uint8_t)(36u + 12u * s + 5u * k + i % 7u);
                t->step[i].n = 1;
                t->step[i].time = ST_NOTE;
                t->step[i].vel = (uint8_t)(70u + i);
            }
            t->step[1].time = ST_TIE;
            t->p[P_SLEN] = LEN[s][k];
            t->p[P_SDIV] = DIV[s][k];
            t->p[P_SGATE] = (int16_t)(40 + 20 * (int16_t)k);
            motion_set_event(t, (uint32_t)(2u + s), P_REV, (int16_t)(20 + 30 * (int16_t)s + (int16_t)k));
        }
        PROBE_SAVE(s);
    }
    chain_config.count = 3;
    chain_config.row[0] = PROBE_ROW(0, 2);
    chain_config.row[1] = PROBE_ROW(1, 1);
    chain_config.row[2] = PROBE_ROW(0, 1);
    if (chain_prepare())
        return 0;
    while (n < 40000u && (n < 4u || chain.running)) {
        events_block(32);
        PROBE_POLL();
        n++;
        h = song_probe_mix(h, (uint32_t)chain.running | (uint32_t)chain.row << 1 | (uint32_t)chain.remaining << 8);
        for (k = 0; k < NTRK_V10; k++) {
            const track_t *t = &trk[k];
            h = song_probe_mix(h, t->seq_idx | (uint32_t)t->seq_n << 8 | (uint32_t)t->seq_notes[0] << 16);
            h = song_probe_mix(h, (uint32_t)t->p[P_SLEN] | (uint32_t)t->p[P_SDIV] << 8 | (uint32_t)(uint16_t)t->p[P_REV] << 16);
        }
    }
    *blocks = n;
#ifdef CHAIN_SILENT
    song_round = 1;
#endif
    return h;
}
