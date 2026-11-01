/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Dah fork */
/* The at-rest skips (fx.c FX_SKIP: HALL, CHORUS, DELAY, the EQs' held blocks, master_out still) give what running
 * gives, sample for sample: a demo song (the emulator's build, -DDAH_DEMO_SONG=N) played, stopped (the tails ring out,
 * the skips start), played again mid-tail, tracks muted and back, stopped for 30 s; both channels of every sample
 * written to OUT (raw int32); a second argument picks the reverb model. run_tests.sh builds it with FX_SKIP 0 and 1 and compares the two files.
 *   cc -O1 -w -DDAH_DEMO_SONG=1 -DFX_SKIP=0 -DWEB_COST=0 -Ibuild/gen -Ifirmware/src -o skip0 tests/skip_test.c -lm */
#include "../web/emu/felucca_web.c"

int main(int argc, char **argv)
{
    uint32_t k, b = 0, t;
    FILE *f = argc > 1 ? fopen(argv[1], "wb") : 0;
    if (!f)
        return 2;
    web_nor_erase();
    web_boot();
    rng_state = 0x1234567u;
    if (argc > 2)
        song.g[G_RTYPE] = (int16_t)atoi(argv[2]);       /* (a reverb model: 0 ROOM, 1 SPRING, 2 HALL) */
    for (k = 0; k < 60u * FS; k += 128u, b++) {
        uint32_t s = b * 128u / FS;                     /* (seconds) */
        if (b * 128u % FS < 128u) {                     /* once a second: the script */
            if (s == 0u || s == 10u)
                transport_req = 1;                      /* play */
            if (s == 6u || s == 22u)
                transport_req = 2;                      /* stop: the tails, then rest */
            if (s == 13u)
                for (t = 1; t < NTRK; t++)
                    trk[t].p[P_MUTE] = 1;               /* one track alone */
            if (s == 17u)
                for (t = 1; t < NTRK; t++)
                    trk[t].p[P_MUTE] = 0;
        }
        web_render(128);
        for (t = 0; t < 128u; t++) {
            int32_t v[2] = {(int32_t)(out_l[t] * 8388608.0f), (int32_t)(out_r[t] * 8388608.0f)};
            fwrite(v, sizeof v, 1, f);
        }
    }
    fclose(f);
    return 0;
}
