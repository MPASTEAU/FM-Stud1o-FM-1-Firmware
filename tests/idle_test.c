/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* An idle frame draws nothing (the LCD would flash what is drawn again): every held layer (FX GLO SCL EDIT SEQ REC,
 * SEQ on a synth and on a DRUM track) and the MENU, opened on SEQ > STEP (the roll, the grid) and on HOME, then
 * frames with nothing changing: no blit at all after the first few (the real UI sources with tests/ui_test.c's stubs) */
#define UI_TEST_NO_MAIN 1
static void idle_blit(unsigned x, unsigned y, unsigned w, unsigned h);
#define UI_BLIT_HOOK(x, y, w, h) idle_blit(x, y, w, h)
#include "ui_test.c"

static uint32_t nblit, logging;
static void idle_blit(unsigned x, unsigned y, unsigned w, unsigned h)
{
    nblit++;
    if (logging && nblit <= 6u)
        printf("  blit %u,%u %ux%u\n", x, y, w, h);
}
/* frames until it settles, then 30 idle ones: their blits */
static uint32_t idle_blits(void)
{
    uint32_t i;
    for (i = 0; i < 40u; i++)
        frame();
    nblit = 0;
    for (i = 0; i < 30u; i++)
        frame();
    return nblit;
}
int main(void)
{
    static const struct { uint8_t layer; const char *name; } L[] = {
        {LAYER_SEQ, "SEQ"}, {LAYER_GLO, "GLO"}, {LAYER_REC, "REC"}, {LAYER_SCL, "SCL"}, {LAYER_EDIT, "EDIT"}, {LAYER_FX, "FX"}};
    int bad = 0;
    uint32_t i, where, drum, k, n;
    char what[96];
    for (drum = 0; drum < 2u; drum++)
        for (where = 0; where < 2u; where++)
            for (i = 0; i <= NELEM(L); i++) {
                ui_power_on();
                for (k = 0; drum && k < NTRK && trk[k].eng_req != ENGI_DRUM; k++)
                    ;
                track_select(drum ? k % NTRK : 0u);
                if (where) go_page(GR_ROLL); else go_home();
                frames(200);
                if (i < NELEM(L)) {
                    btn_down(LAYERS[L[i].layer].btn);   /* (held open; let go, it stays: FX's held on) */
                    frames(800);
                    if (L[i].layer != LAYER_FX)
                        btn_up(LAYERS[L[i].layer].btn);
                } else
                    ui.menu = 1;
                ui.force = 1;
                n = idle_blits();
                if (i < NELEM(L) ? ui.layer != L[i].layer : ui.menu != 1)
                    n = 999;                            /* (closed meanwhile: not what is checked) */
                fm1_in.buttons = 0;
                snprintf(what, sizeof what, "%s on %s, a %s track: an idle frame draws nothing (%u blits in 30)",
                         i < NELEM(L) ? L[i].name : "MENU", where ? "SEQ > STEP" : "HOME", drum ? "DRUM" : "synth", n);
                bad += check(what, !n);
                if (n) {
                    logging = 1; nblit = 0; frame(); logging = 0;
                }
            }
    printf("%s\n", bad ? "IDLE TEST FAILED" : "idle test passed");
    return bad != 0;
}
