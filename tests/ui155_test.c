/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 1.5.5, fixes and UI, on the real UI sources (with the stubs of tests/ui_test.c):
 *   PAN        (#199) a bipolar value (-64..63, F_BIPCT) reads -100..+100 %: +100 at the top (was +98), the stored
 *              range and the sound unchanged; the MIXER strip's number the same
 *   (and SELECT PAGE, PROJECT ERASE, CLEAN INIT; OCT- on the press as in 1.5.1 (1.5.6: #205 taken
 *   out), next to the SEQ layer's OCT+, FILL)
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* ---------------------------------------------------------------- PAN --- */
static int fmt_is(const param_desc_t *d, int32_t v, const char *want)
{
    char val[12];
    const char *unit;
    param_format(d, v, val, &unit);
    if (!str_eq(val, want))
        printf("  %s %d: \"%s\", want \"%s\"\n", d->label, (int)v, val, want);
    return str_eq(val, want) && str_eq(unit, "%");
}
static int pan_range(void)
{
    int bad = 0, ok = 1;
    int32_t v, last = -1000;
    char val[12];
    const char *unit;
    ok &= fmt_is(&TP[P_PAN], 63, "+100") && fmt_is(&TP[P_PAN], -64, "-100") && fmt_is(&TP[P_PAN], 0, "0");
    ok &= fmt_is(&TP[P_PAN], 32, "+50") && fmt_is(&TP[P_PAN], -32, "-50") && fmt_is(&TP[P_PAN], 1, "+1") &&
          fmt_is(&TP[P_PAN], -1, "-1");
    for (v = -64; v <= 63; v++) {                       /* every value: rising, never twice the same number */
        int32_t n = 0, i;
        param_format(&TP[P_PAN], v, val, &unit);
        for (i = val[0] == '+' || val[0] == '-'; val[i]; i++)
            n = n * 10 + (val[i] - '0');
        if (val[0] == '-')
            n = -n;
        ok &= n > last && n >= -100 && n <= 100;
        last = n;
    }
    bad += check("PAN (#199): -64..63 read -100..+100 %, +100 at the top (was +98), rising, each value its own", ok);
    ok = fmt_is(&TP[P_M1AMT], 63, "+100") && fmt_is(&TP[P_ED_FLT], -64, "-100") &&
         TP[P_PAN].min == -64 && TP[P_PAN].max == 63 && TP[P_PAN].def == 0;
    bad += check("  every bipolar value the same (MOD AMT, ENV FLT); the stored range -64..63 as before", ok);
    return bad;
}

/* a HELP hint fits its footer row with every word (as tests/ui_test.c test_help) */
static int help_fits(const char *s)
{
    khint_t kh[3];
    char b[48];
    uint32_t n = help_parse(s, kh, b, sizeof b), i;
    int32_t w = 6 * ((int32_t)n - 1);
    int ok = n >= 2u && n <= 3u;
    for (i = 0; i < n; i++) {
        ok &= kh[i].key < KC_COUNT && kh[i].act[0] != 0;
        w += kh_w(kh[i].key, kh[i].act);
    }
    if (!ok || w > 224) printf("  help: \"%s\" %u hints, %d px of 224\n", s, n, w);
    return ok && w <= 224;
}

/* --------------------------------------------------- SELECT = PAGE --- */
static int title_is(const char *t) { return !ui.home && str_eq(cur_page()->title, t); }
static int select_page(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint8_t before[32];
    uint32_t i;
    ui_power_on();
    memcpy(before, favorites.factory[15], 32);
    ok = menu_get(MI_SELECT) == 0u && str_eq(MI_NAME[MI_SELECT], "SELECT") && MI_TAB[MI_SELECT] == MTAB_CONTROL &&
         MI_SELECT < MI_VALUES && str_eq(menu_vname(MI_SELECT, 0), "PATTERN") && str_eq(menu_vname(MI_SELECT, 1), "LIST") &&
         mtab_rows(MTAB_CONTROL) <= MTAB_ROWS;
    go_title("ENV"); frame();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 2);
    ok &= ui.confirm == CF_NEW_PAT && ps_num(0) == 1u;  /* (Dah: past the last: NEW PATTERN 2? asked) */
    press(B_OCTUP);
    ok &= ui.confirm == CF_NEW_FROM;                    /* (Dah: then from what: EMPTY first) */
    press(B_OCTUP);
    ok &= song.g[G_BPM] == b0 && ps_num(0) == 2u && title_is("ENV");
    turn(EN_SELECT, -1);
    ok &= ps_num(0) == 1u;
    bad += check("SELECT (#188): MENU > CONTROL > SELECT, BPM by default (Dah: SELECT the pattern, OCT+ + SELECT the tempo)", ok);

    menu_put(MI_SELECT, 1);
    ok = menu_get(MI_SELECT) == 1u && favorites.factory[15][24] == (uint8_t)(before[24] | 0x10u);
    for (i = 0; i < 32u; i++)
        ok &= i == 24u || favorites.factory[15][i] == before[i];
    {   /* saved with the settings; settings from before it read BPM */
        persist_t p;
        settings_export(&p);
        ui_prefs2 = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_SELECT) == 1u;
        p.favorites.factory[15][24] &= (uint8_t)~0x10u;
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_SELECT) == 0u;
        menu_put(MI_SELECT, 1);
    }
    bad += check("  PAGE: a bit of its own (MENU's 1.2 flags, bit 4), kept with the settings; 0 (older settings) = BPM", ok);

    b0 = song.g[G_BPM];                                 /* (LIST, 1.5.5's PAGE: the page's list, not the pages) */
    go_title("ENV"); frame();
    turn(EN_SELECT, 1);
    ok = title_is("ENV") && song.g[G_BPM] == b0 && ps_num(0) == 2u;   /* (Dah: no list: the pattern) */
    go_title("SONG"); frame();                          /* (Dah) where SELECT did KNOB 1's (SONG, PATTERN, FILE ..):
                                                         * the pattern too */
    turn(EN_SELECT, -1);
    ok &= title_is("SONG") && song.g[G_BPM] == b0 && ps_num(0) == 1u;
    bad += check("  LIST (Dah): the pattern where SELECT did what KNOB 1 does (SONG, PATTERN, FILE ..), and with no list (ENV)", ok);

    go_home(); frame();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 1);
    ok = ui.home && song.g[G_BPM] == b0 && ps_num(0) == 2u;
    bad += check("  LIST: on HOME SELECT is the pattern (Dah)", ok);

    go_title("ENV"); frame();
    b0 = song.g[G_BPM];
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 + 3 && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= title_is("ENV");
    btn_down(B_LFO); frame();                           /* LFO held (MOMENTARY): no page */
    turn(EN_SELECT, 1);
    ok &= title_is("ENV");
    btn_up(B_LFO); frame();
    bad += check("  LIST: GLO + SELECT is the tempo; nothing with LFO held", ok);

    menu_put(MI_SELECT, 0);                             /* (Dah: CLOCK's page and its hint gone) */
    return bad;
}

/* ---------------------------------------------- OCT- on the press (1.5.6) --- */
/* 1.5.6: 1.5.5's "OCT- held + a knob = default" (#205) is taken out to keep OCT- as it was: the octave moves when OCT-
 * is pressed (as in 1.5.1), and a knob turned with OCT- held is an ordinary turn. EDIT 2's hint is EDIT 1's again */
static int oct_press(void)
{
    int bad = 0, ok;
    track_t *t;
    int16_t v;
    uint32_t mv;
    ui_power_on();
    t = TSEL;
    go_title("ENV"); frame();
    song.octave = 0;
    btn_down(B_OCTDN); frame();
    ok = song.octave == 0;                              /* (Dah: nothing on the press) */
    btn_up(B_OCTDN); frame();
    ok &= song.octave == -1;                            /* (on the release) */
    press(B_OCTDN);
    ok &= song.octave == -2;
    press(B_OCTUP);
    ok &= song.octave == -1;
    btn_down(B_OCTDN); frame();
    btn_down(B_OCTUP); frame(); btn_up(B_OCTUP); btn_up(B_OCTDN); frame(); frame();
    ok &= song.octave == -1 && ka.st == 1u;             /* (both: ASSIGN, the octave as it was) */
    press(B_OCTDN);
    ok &= !ka.st && song.octave == -1;                  /* (OCT-: ASSIGN off) */
    for (v = 0; v < 5; v++) press(B_OCTDN);
    ok &= song.octave == -3;                            /* (stops at -3) */
    song.octave = 0;
    bad += check("OCT- (Dah): the octave down on its release, OCT+ up, both together ASSIGN (OCT- leaves it), -3 the floor", ok);

    t->p[P_ATK] = 50;
    btn_down(B_OCTDN); frame();
    turn(EN_K1, 2);
    ok = t->p[P_ATK] == 52 && !str_eq(ui.msg, "ATK DEFAULT");
    btn_up(B_OCTDN); frame();
    ok &= song.octave == -1;
    song.octave = 0;
    go_title("EDIT 2");
    ok &= str_eq(help_page(), "[EDIT]HOLD: ENGINES");   /* (Dah: no knob in a hint) */
    bad += check("  OCT- held + KNOB: an ordinary turn (no default), the octave as pressed; EDIT 2's hint as EDIT 1's", ok);

    hold(B_HOME);
    ok = ui.menu != 0;
    mv = menu_get(ui.menu_sel);
    press(B_OCTDN);
    ok &= ui.menu != 0 && song.octave == 0;             /* MENU: OCT- steps the setting back (as before), no octave */
    menu_put(ui.menu_sel, mv);                          /* (the setting as it was) */
    press(B_HOME);
    ok &= ui.menu == 0;
    bad += check("  the menu: OCT- is the menu's as before (the setting back), the octave untouched", ok);
    return bad;
}

/* ------------------------------------------- SAVE > PATTERN --- */
/* the track's patterns (pstore.c): KNOB 1 OPEN, 2 COPY, 3 NEW, 4 DELETE (the dialog), OCT+ does it; REC deletes too */
static int project_erase_ui(void)
{
    int bad = 0, ok;
    char a[24], b[24];
    ui_power_on();
    song.playing = 0;
    song.sel = 0;
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    go_page(GR_PATTERN); frame();
    ok = act_cols() == 14u && str_eq(act_name(0), "PATTERN") && str_eq(act_name(1), "COPY") && str_eq(act_name(2), "NEW") &&
         str_eq(act_name(3), "DELETE");
    turn(EN_K2, 1);
    ok &= ui.act == 2u && act_ready();
    press(B_OCTUP);
    ok &= ps_num(0) == 2u && ps_num(1) == 2u && ps_exists(0, 1) && trk[0].step[0].n == 1u && trk[0].step[0].note[0] == 60u;
    turn(EN_K3, 1);
    press(B_OCTUP);                                     /* (Dah: NEW asks, then what it starts with: EMPTY first) */
    ok &= ui.confirm == CF_NEW_PAT;
    press(B_OCTUP);
    ok &= ui.confirm == CF_NEW_FROM && np_cur == 0u;
    press(B_OCTUP);
    ok &= !ui.confirm && ps_num(0) == 3u && ps_num(1) == 3u && trk[0].step[0].time != ST_NOTE && ps_exists(0, 2);
    turn(EN_K1, -1);                                    /* (KNOB 1: the one before, every track on it at once) */
    ok &= ps_num(0) == 2u && ps_num(1) == 2u && trk[0].step[0].note[0] == 60u;
    turn(EN_K1, 1);
    ok &= ps_num(0) == 3u;
    turn(EN_K4, 1);
    press(B_OCTUP);
    confirm_text(a, b);
    ok &= ui.confirm == CF_DEL_PAT && str_eq(a, "DELETE PATTERN 3?");
    press(B_OCTDN);
    ok &= !ui.confirm && ps_exists(0, 3);
    bad += check("SAVE > PATTERN (Dah): KNOB 1 the pattern (every track on it), 2 COPY, 3 NEW (empty), 4 DELETE (the dialog)", ok);

    press(B_REC);
    ok = ui.confirm == CF_DEL_PAT && !song.rec;
    confirm_text(a, b);
    ok &= str_eq(a, "DELETE PATTERN 3?");
    press(B_OCTDN);
    ok &= !ui.confirm && ps_exists(0, 3);
    bad += check("  REC asks \"DELETE PATTERN 3?\" (no recording armed), OCT- keeps it", ok);

    press(B_REC);
    press(B_OCTUP);
    ok = !ui.confirm && !ps_exists(0, 3) && ps_num(0) != 3u && str_eq(ui.msg, "PATTERN DELETED");
    pattern_go(0, 2);
    ok &= trk[0].step[0].n == 1u && trk[0].step[0].note[0] == 60u;
    pattern_del(0, 1);
    pattern_del(0, 2);
    ok &= ps_num(0) == 1u && trk[0].step[0].time != ST_NOTE && !ps_exists(0, 2);
    bad += check("  OCT+ deletes it, the track on another; the last one: emptied", ok);

    turn(EN_K1, 1);
    ok = !act_ready();                                  /* (OPEN: the one played already) */
    ok &= help_page() == H_SAVE;                        /* (Dah: the quick save's, SAVE held: no knob, no PRESETS) */
    bad += check("  OPEN on the one played: nothing to do; the HELP hint the quick save's ([SAVE] HOLD)", ok);
    return bad;
}

/* --------------------------------------------------------- CLEAN INIT --- */
/* a sound with everything added: sends, INSERT, LFO / ENV amounts, MOD, GLIDE, MONO, its E1..E8 off their defaults */
static void dress(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(30 + i);
    t->p[P_ITYPE] = 2; t->p[P_IMIX] = 90;
    t->p[P_LD_FLT] = 20; t->p[P_LD_PIT] = -9; t->p[P_LD_AMP] = 40; t->p[P_ED_FLT] = 33; t->p[P_ED_SHP] = -12;
    t->p[P_M1SRC] = 1; t->p[P_M1DST] = 2; t->p[P_M1AMT] = 50; t->p[P_M3AMT] = -20;
    t->p[P_GLIDE] = 60; t->p[P_VOICE] = V_MONO; t->p[P_SPRD] = 70;
    t->p[P_ATK] = 80; t->p[P_SUS] = 10; t->p[P_REL] = 100;
    t->p[P_LEVEL] = 77; t->p[P_PAN] = -30;
}
static int clean_is(const track_t *t, uint32_t e, int16_t lvl, int16_t pan)
{
    const engine_t *en = ENGINES[e];
    uint32_t i;
    int ok = t->eng_req == e && t->p[P_LEVEL] == lvl && t->p[P_PAN] == pan;
    for (i = 0; i < 4u; i++)
        ok &= t->p[P_DIST + i] == 0;
    ok &= t->p[P_ITYPE] == 0 && t->p[P_LD_FLT] == 0 && t->p[P_LD_PIT] == 0 && t->p[P_LD_AMP] == 0 && t->p[P_ED_FLT] == 0 &&
          t->p[P_ED_SHP] == 0 && t->p[P_M1SRC] == 0 && t->p[P_M1DST] == 0 && t->p[P_M1AMT] == 0 && t->p[P_M3AMT] == 0 &&
          t->p[P_GLIDE] == 0 && t->p[P_VOICE] == V_POLY && t->p[P_SPRD] == 0;
    for (i = 0; i < 8u; i++)
        ok &= t->p[P_E0 + i] == (e == ENGI_FM6 && i == 7u ? (int16_t)FM6_OWN : en->edit[i].def);
    if (e == ENGI_DRUM)
        ok &= t->p[P_ATK] == en->presets[0].env[0] && t->p[P_SUS] == en->presets[0].env[2] &&
              t->p[P_REL] == en->presets[0].env[3];
    else
        ok &= t->p[P_ATK] == 0 && t->p[P_SUS] == 127 && t->p[P_REL] == CLEAN_REL;
    if (e == ENGI_FM6) {
        uint8_t v[FP_SIZE + 1u];
        fm6_unpack(FM6_INIT, v);
        fm6_sanitize(v);
        ok &= !memcmp(v, fm6_patch[trk_index(t)], FP_SIZE) && fm6_slot[trk_index(t)] == FM6_OWN;
    }
    return ok;
}
/* a note on track t after the load: it sounds (some energy, below full scale), and ends after its release */
static int clean_sounds(track_t *t, uint32_t note)
{
    int32_t o[2u * CTL];
    uint32_t b, i, mq = song.master_q12, act = 0;
    uint64_t e = 0;
    int32_t pk = 0;
    song.master_q12 = 4096;
    for (b = 0; b < 256u && (b < 1u || t->xf_on || t->eng_req != t->engine); b++)
        mix_block(o, CTL);
    trk_note_on(t, note, 100);
    for (b = 0; b < 96u; b++) {
        memset(o, 0, sizeof o);
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            e += (uint64_t)a;
            if (a > pk) pk = a;
        }
    }
    trk_note_off(t, note);
    for (b = 0; b < 1500u; b++)
        mix_block(o, CTL);
    for (i = 0; i < NVOICE; i++)
        act += t->v[i].active;
    song.master_q12 = mq;
    if (!(e > 0u && pk < 32767 && !act))
        printf("  %s: energy %llu peak %d voices left %u\n", ENGINES[t->engine % NENGINES]->name, (unsigned long long)e,
               (int)pk, act);
    return e > 0u && pk < 32767 && !act;
}
static int clean_init(void)
{
    int bad = 0, ok = 1, ok3 = 1;
    uint32_t r, e;
    track_t *t;
    char a[24], b[24];
    for (r = 0; r < NENG_SHOWN; r++) {                 /* every engine one can pick: EDIT + F5, CLEAN (Dah: TOOLS gone) */
        step_t st[NSTEP];
        ui_power_on();
        song.playing = 0;
        t = TSEL;
        e = eng_vis(r);
        set_engine(e);
        dress(t);
        my_steps(t);
        memcpy(st, t->step, sizeof st);
        go_home(); frame();
        lay_combo(B_EDIT, white(LY_CLEAN));
        key_up(white(LY_CLEAN)); btn_up(B_EDIT); frame();
        ok &= ui.confirm == CF_CLEAN_SOUND;
        press(B_OCTUP);
        ok &= !ui.confirm && msg_is("CLEAN INIT") && !memcmp(st, t->step, sizeof st);
        if (!clean_is(t, e, 77, -30)) {
            printf("  CLEAN INIT on %s: not clean\n", ENGINES[e]->name);
            ok = 0;
        }
        ok3 &= clean_sounds(t, e == ENGI_DRUM ? 36u : 60u);
    }
    bad += check("CLEAN INIT (#190), every engine: its E1..E8 defaults, no sends, INSERT OFF, no LFO / ENV / MOD amounts, POLY,", ok);
    bad += check("  each engine's clean sound plays a note (heard, below full scale) and ends after its release", ok3);

    ui_power_on();
    t = TSEL;
    set_engine(ENGI_FM6);
    dress(t);
    go_home(); frame();
    lay_combo(B_EDIT, white(LY_INIT));
    key_up(white(LY_INIT)); btn_up(B_EDIT); frame();
    ok = ui.confirm == CF_INIT_SOUND;
    press(B_OCTUP);
    ok &= msg_is("SOUND INIT") && t->p[P_E7] == 0 && t->p[P_DLY] == ENGINES[ENGI_FM6]->presets[0].fx[2] - 1;
    bad += check("  EDIT + E5: INIT as before (the first preset, its sends)", ok);

    ui_power_on();
    go_home(); frame();
    lay_combo(B_EDIT, white(LY_CLEAN));
    ok = ui.confirm == CF_CLEAN_SOUND && LY_CLEAN == LY_INIT + 1u && LY_CLEAN < 16u;
    key_up(white(LY_CLEAN)); btn_up(B_EDIT); frame();
    confirm_text(a, b);
    ok &= str_eq(a, "CLEAN INIT SOUND?") && str_eq(b, "NO FX, NO MOD");
    dress(TSEL);
    press(B_OCTUP);
    ok &= msg_is("CLEAN INIT") && clean_is(TSEL, TSEL->eng_req, 77, -30);
    bad += check("  EDIT + the key after INIT (F5): the dialog \"CLEAN INIT SOUND?\"", ok);
    return bad;
}

/* OCT- (on the press again in 1.5.6) next to 1.5.5's other OCT- / layer users: the SEQ layer's OCT+ (QUANTIZE's APPLY)
 * and FILL (GLO + E4) leave the octave alone; a plain OCT- after
 * them still lowers it. (MIDI LEARN's OCT- clear: tests/midi_control_test.c checks the octave stays) */
static int oct_layers(void)
{
    int bad = 0, ok;
    ui_power_on();
    go_page(GR_ROLL); frame();
    song.octave = 0;
    btn_down(B_SEQ); frame();
    btn_down(B_OCTUP); frame(); btn_up(B_OCTUP); frame();   /* (Dah: open; its first row LEN: OCT+ nothing) */
    lys.sq_row = 2;                                     /* (Dah: the QUANTIZE row, KNOB 1's) */
    btn_down(B_OCTUP); frame(); btn_up(B_OCTUP); frame();   /* (Dah) SEQ: OCT+ APPLY (QUANTIZE ON, nothing nudged) */
    ok = song.octave == 0;                              /* (Dah: no APPLY any more: OCT+ nothing there, no octave) */
    btn_up(B_SEQ); frames(400);
    if (ui.lock) {                                      /* (Dah: held open, it stays: SEQ again closes it) */
        btn_down(B_SEQ); frame(); btn_up(B_SEQ); frames(400);
    }
    bad += check("OCT+ in the SEQ layer: QUANTIZE's APPLY; no octave", ok);

    btn_down(B_GLO); frames(100);
    key_down(key_at(0, 6)); frames(50);                /* E4: FILL while held */
    ok = seq_fill == 1u && ui.layer == LAYER_GLO;
    key_up(key_at(0, 6)); frames(50);
    ok &= !seq_fill;
    btn_up(B_GLO); frames(400);
    if (ui.lock) {                                      /* (Dah: held open, it stays: GLO again closes it) */
        btn_down(B_GLO); frame(); btn_up(B_GLO); frames(400);
    }
    ok &= song.octave == 0;
    press(B_OCTDN);
    ok &= song.octave == -1;
    song.octave = 0;
    bad += check("  FILL (GLO + E4) as before, the octave untouched; a plain OCT- after it the octave down", ok);
    return bad;
}

/* (Dah) every menu alike: MENU (HOME held) one list, KNOB 1 the row (across the old tabs), KNOB 2 its value; another
 * button closes it and acts (a page button: its page), PLAY plays in it, HOME closes it; the held layers' lists the
 * same (KNOB 1 the row, gliding); no knob, SELECT, ALGORITHM or PRESETS in any footer or help */
static int knob_named(const char *h)
{
    return strstr(h, "[K1") || strstr(h, "[K2") || strstr(h, "[K3") || strstr(h, "[K4");
}
static int menus_alike(void)
{
    int bad = 0, ok;
    uint32_t i, n = 0;
    ui_power_on();
    go_home(); frame();
    hold(B_HOME);
    ui.menu_sel = MI_HOME;                              /* (DISPLAY's last row) */
    turn(EN_K1, 1);
    ok = ui.menu == 1 && ui.menu_sel == MI_HOLD && menu_tab() == MTAB_CONTROL;   /* (on into CONTROL's first) */
    turn(EN_K1, -1);
    ok &= ui.menu_sel == MI_HOME;
    ui.menu_sel = MI_LEDS;
    i = menu_get(MI_LEDS);
    turn(EN_K2, 1);
    ok &= menu_get(MI_LEDS) == (i + 1u) % menu_n(MI_LEDS) || menu_get(MI_LEDS) == menu_n(MI_LEDS) - 1u;
    ok &= menu_nvis() + 8u == MI_COUNT;                /* (every row but CLICK .. TUNE and T1..T4 CHANNEL: no tabs) */
    for (i = 0; i < menu_nvis(); i++)
        n += menu_pos(menu_vis(i)) == i;
    ok &= n == menu_nvis() && menu_vis(menu_nvis() - 1u) == MI_FACTORY;   /* (FACTORY RESET the last) */
    bad += check("MENU: one list (no tabs), KNOB 1 the row across DISPLAY into CONTROL, KNOB 2 the value", ok);
    press(B_PLAY);
    ok = ui.menu == 1 && (song.playing || transport_req == 1u);
    press(B_PLAY); frames(100);
    press(B_LFO);
    ok &= ui.menu == 0 && !ui.home && cur_page()->fam == FAM_LFO;
    hold(B_HOME);
    ok &= ui.menu == 1;
    press(B_HOME);
    ok &= ui.menu == 0;
    bad += check("  PLAY plays in it; another button closes it and acts (LFO: its page); HOME closes it", ok);
    {   /* FACTORY RESET: the last row; OCT+ asks (the dialog, the menu closed), OCT- cancels, OCT+ answers */
        char a[24], b[24];
        hold(B_HOME);
        ui.menu_sel = MI_FACTORY;
        ok = ui.menu == 1 && oct_leds() == OCT_BREATH && str_eq(menu_label(MI_FACTORY), "FACTORY RESET");
        press(B_OCTUP);
        confirm_text(a, b);
        ok &= ui.menu == 0 && ui.confirm == CF_FACTORY && str_eq(a, "FACTORY RESET?") && str_eq(b, "ERASES ALL BUT SAMPLES");
        frame();
        press(B_OCTDN);
        ok &= !ui.confirm && ui.menu == 0;
        hold(B_HOME);
        ui.menu_sel = MI_FACTORY;
        press(B_OCTUP);
        ok &= ui.confirm == CF_FACTORY;
        press(B_OCTUP);                                 /* (no flash here: factory_reset erases nothing) */
        ok &= !ui.confirm && host_reboots == 0u;
        bad += check("  FACTORY RESET (the last row): OCT+ the dialog, OCT- cancels it, OCT+ answers it", ok);
    }
    ok = 1;
    for (i = 0; i < NELEM(HELP_PAGE); i++)
        ok &= !knob_named(HELP_PAGE[i].hint) && !strstr(HELP_PAGE[i].hint, "[PRESETS]") &&
              !strstr(HELP_PAGE[i].hint, "[ALGO") && !strstr(HELP_PAGE[i].hint, "[SELECT]");
    for (i = 0; i < LAYER_N; i++)
        ok &= !knob_named(HELP_LAYER[i]) && LAYERS[i].foot[0].key != KC_K14 && LAYERS[i].foot[1].key != KC_K14;
    ok &= !strstr(H_HOME, "[ALGO") && FX_KEY_FOOT[0].key != KC_PRESETS && FX_LATCH_FOOT[1].key != KC_K14;
    bad += check("  no knob, SELECT, ALGORITHM or PRESETS in a footer or a HELP hint", ok);
    go_page(GR_ROLL); frame();
    btn_down(B_REC); frames(900); btn_up(B_REC); frames(100);
    ok = ui.layer == LAYER_REC && lys.sq_row == 0u;
    turn(EN_K1, 1);
    ok &= lys.sq_row == 1u && (mr.dy != 0 || (ui_prefs & PREF_ANIM_OFF));
    turn(EN_K1, 5);
    ok &= lys.sq_row == 2u;                             /* (a step a turn, as the MENU's) */
    for (i = 0; i < 5u; i++) turn(EN_K1, 1);
    ok &= lys.sq_row == 5u;                             /* (the last, REC MODE: it stops) */
    press(B_LFO);
    ok &= !ui.layer;
    bad += check("  REC's list: KNOB 1 the row (gliding), stops at the last; another button closes it", ok);
    return bad;
}

/* (Dah) REC's rule (step recording: armed, stopped; live: armed, playing, QUANTIZE OFF nudged too): one key alone on a
 * step holding that note alone takes it out (with its tail), onto other notes it replaces them, keys together (or held
 * while another of them is) make the step exactly their notes; on an empty step the note is written */
static int has_note(const step_t *st, uint32_t note)
{
    uint32_t j;
    for (j = 0; j < st->n && st->note[j] != note; j++)
        ;
    return st_kind(st) == ST_NOTE && j < st->n;
}
static int only(const step_t *st, uint32_t a, uint32_t b)   /* exactly a (and b; 0 none) */
{
    return st_kind(st) == ST_NOTE && st->n == (b ? 2u : 1u) && has_note(st, a) && (!b || has_note(st, b)) && !st_cont(st);
}
static track_t *rule_setup(void)
{
    track_t *t;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    press(B_REC);
    return t;
}
static int rec_keeps(void)
{
    int bad = 0, ok = 1;
    uint32_t i, kE = 7, kF = 8, kG = 10, kC = 5, E, F, G, C, done;
    track_t *t = rule_setup();
    E = kb_map(t, kE); F = kb_map(t, kF); G = kb_map(t, kG); C = kb_map(t, kC);
    ok &= song.rec == 1u && !song.playing && E != F && E != G;
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    tap_k(kE);
    ok &= st_kind(&t->step[3]) == ST_REST && !t->step[3].n;
    bad += check("REC on STEP (stopped): E alone, E played: removed (a REST)", ok);
    t->step[3] = (step_t){{(uint8_t)C, (uint8_t)E, (uint8_t)G}, 3, ST_NOTE, 0, 100};
    tap_k(kE);
    bad += check("  a chord C E G, E played: E alone", only(&t->step[3], E, 0));
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    tap_k(kF);
    bad += check("  E, F played: F", only(&t->step[3], F, 0));
    track_defaults_steps(t);
    key_down(kC); key_down(kE); frame(); key_up(kC); key_up(kE); frame();
    ok = only(&t->step[3], C, E);
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    key_down(kC); key_down(kE); frame(); key_up(kC); key_up(kE); frame();
    ok &= only(&t->step[3], C, E);
    bad += check("  C and E pressed together: C E (an empty step, a step holding E: never a removal)", ok);
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    key_down(kE); frame();
    ok = st_kind(&t->step[3]) == ST_REST;
    key_down(kG); frame();
    ok &= only(&t->step[3], E, G);
    key_up(kE); key_up(kG); frame();
    ok &= only(&t->step[3], E, G);
    bad += check("  E held: removed; G pressed while E is held: E G; all let go: E G", ok);
    track_defaults_steps(t);                            /* a long E (3 steps), then a C going on from it */
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    t->step[4].time = ST_TIE; t->step[5].time = ST_TIE;
    tap_k(kE);
    ok = st_kind(&t->step[3]) == ST_REST && st_kind(&t->step[4]) == ST_REST && st_kind(&t->step[5]) == ST_REST;
    t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
    t->step[4].time = ST_TIE; t->step[5] = (step_t){{(uint8_t)E, (uint8_t)C}, 2, ST_NOTE, 0, 100};
    st_set_cont(&t->step[5], 1u);                       /* (E going on, C starting) */
    tap_k(kF);
    ok &= only(&t->step[3], F, 0) && st_kind(&t->step[4]) == ST_REST && only(&t->step[5], C, 0);
    bad += check("  a long E taken out / replaced: its TIEs and its going on after it gone (C kept)", ok);
    /* live: QUANTIZE OFF, E a little early and a little late onto step 4 (index 3) holding E alone: removed */
    for (done = 0; done < 2u; done++) {
        uint32_t len, cur;
        t = rule_setup();
        song.rec = 1u << song.sel;
        t->p[P_SLEN] = 16;
        t->p[P_SQNT] = 0;
        t->step[3] = (step_t){{(uint8_t)E}, 1, ST_NOTE, 0, 100};
        transport_req = 1;
        blocks(2);
        len = 0;
        for (i = 0; i < 4000u; i++) {                   /* (to step 3, 80 % in: early for step 4; or step 4, 20 % in) */
            cur = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
            if (t->seq_idx == (done ? 3u : 2u) && t->seq_pos > (done ? cur / 5u : cur * 4u / 5u))
                break;
            blocks(1);
        }
        key_down(kE); frame(); blocks(2); key_up(kE); frame();
        len = st_kind(&t->step[3]) == ST_REST && !t->step[3].n;
        transport_req = 2; blocks(8); frame(); song.rec = 0;
        bad += check(done ? "REC playing, QUANTIZE OFF: E a little late onto a step holding E alone: removed"
                          : "REC playing, QUANTIZE OFF: E a little early onto a step holding E alone: removed", (int)len);
    }
    {   /* live: E then G held together onto an empty step: E G; across loops, notes on other steps build up */
        t = rule_setup();
        song.rec = 1u << song.sel;
        t->p[P_SLEN] = 16;
        transport_req = 1;
        blocks(2);
        for (i = 0; i < 4000u && t->seq_idx != 5u; i++)
            blocks(1);
        key_down(kE); frame(); key_down(kG); frame(); blocks(1); key_up(kE); key_up(kG); frame();
        for (i = 0; i < 4000u && t->seq_idx != 9u; i++)
            blocks(1);
        key_down(kC); frame(); blocks(1); key_up(kC); frame();
        transport_req = 2; blocks(8); frame(); song.rec = 0;
        bad += check("REC playing: E and G held together onto step 6: E G; then C on step 10: both kept",
                     has_note(&t->step[5], E) && has_note(&t->step[5], G) && has_note(&t->step[9], C));
    }
    return bad;
}

/* (Dah) inside a chord going on (step recording, POLY): a key on a note going on ends it at the step before (the
 * others go on), a new note starts there and lasts as long as the notes it joins; heard so through the sequencer */
static int goes(const step_t *s, uint32_t x)            /* NOTE step s carries x on from the step before */
{
    uint32_t k;
    for (k = 0; k < s->n && k < 4u && s->note[k] != x; k++)
        ;
    return st_kind(s) == ST_NOTE && k < s->n && ((st_cont(s) >> k) & 1u);
}
static int starts(const step_t *s, uint32_t x)
{
    return has_note(s, x) && !goes(s, x);
}
typedef struct { uint8_t ons, offs, on_at, off_at; } ce_t;
static void ce_listen(track_t *t, const uint32_t *nt, ce_t *h, uint32_t nn)   /* one loop of 16 from step 1 */
{
    uint32_t va[NVOICE], i, j, b, loops = 0, last = 0;
    uint8_t vg[NVOICE];
    memset(h, 0, nn * sizeof *h);
    transport_req = 1;
    blocks(1);
    for (i = 0; i < NVOICE; i++) {
        va[i] = t->v[i].age;
        vg[i] = 0;
    }
    for (b = 0; b < 40000u && loops < 1u; b++) {
        blocks(1);
        if (t->seq_idx < last)
            loops++;
        last = t->seq_idx;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint8_t g = (uint8_t)(v->active && v->gate), on = v->age != va[i], off = vg[i] && !g;
            for (j = 0; j < nn && nt[j] != v->note; j++)
                ;
            if (j < nn && on && !loops && !h[j].ons++)  /* (the loop's: the next one's start aside) */
                h[j].on_at = (uint8_t)last;
            if (j < nn && off && !on && !h[j].offs++)
                h[j].off_at = (uint8_t)(loops ? 16u + last : last);
            va[i] = v->age;
            vg[i] = g;
        }
    }
    transport_req = 2;
    blocks(8);
    frame();
}
static int chord_inside(void)
{
    int bad = 0, ok;
    uint32_t i, k, kE = 7, kC = 5, kG = 10, kA = 99, C, E, G, A, nt[4];
    ce_t h[4];
    track_t *t = rule_setup();
    C = kb_map(t, kC); E = kb_map(t, kE); G = kb_map(t, kG);
    for (k = 0; k < 27u; k++)
        if (kb_map(t, k) == E + 5u)
            kA = k;
    A = E + 5u;
    t->p[P_SLEN] = 16;
    t->step[0] = (step_t){{(uint8_t)C, (uint8_t)E, (uint8_t)G}, 3, ST_NOTE, 0, 100};
    for (i = 1; i < 16u; i++)
        t->step[i].time = ST_TIE;
    ok = kA < 27u && note_len(t, 0) == 16u;
    cursor_set(9); frame();
    tap_k(kE);
    ok &= st_kind(&t->step[8]) == ST_TIE && t->step[9].n == 2u && goes(&t->step[9], C) && goes(&t->step[9], G) &&
          !has_note(&t->step[9], E);
    for (i = 10; i < 16u; i++)
        ok &= st_kind(&t->step[i]) == ST_TIE;
    ok &= note_run(t, 0, C, C) == 16u && note_run(t, 0, E, E) == 9u && note_run(t, 0, G, G) == 16u;
    bad += check("inside a chord C E G (LEN 16), E pressed at step 10: E ends at 9, C G go on to 16", ok);
    tap_k(kA);
    ok = t->step[9].n == 3u && goes(&t->step[9], C) && goes(&t->step[9], G) && starts(&t->step[9], A) &&
         note_run(t, 9, A, A) == 7u && note_run(t, 0, C, C) == 16u;
    for (i = 10; i < 16u; i++)
        ok &= st_kind(&t->step[i]) == ST_TIE;
    bad += check("  then A pressed there: A starts at 10, going on with C G to 16", ok);
    nt[0] = C; nt[1] = E; nt[2] = G; nt[3] = A;
    ce_listen(t, nt, h, 4);
    ok = h[0].ons == 1u && h[0].on_at == 0u && h[2].ons == 1u && h[2].on_at == 0u && h[1].ons == 1u &&
         h[1].on_at == 0u && h[1].offs == 1u && (h[1].off_at == 8u || h[1].off_at == 9u) && h[3].ons == 1u &&
         h[3].on_at == 9u;
    ok &= h[3].offs == 1u && h[3].off_at >= 15u;       /* (C G: on into the next loop's C E G) */
    if (!ok)
        for (k = 0; k < 4u; k++)
            printf("  note %u: %u on (step %u), %u off (step %u)\n", nt[k], h[k].ons, h[k].on_at + 1u, h[k].offs, h[k].off_at + 1u);
    bad += check("  played: C G A E on once each (C E G at 1, A at 10), E off at 9's end, A at 16's", ok);
    cursor_set(9); frame();
    tap_k(kA);
    ok = t->step[9].n == 2u && !has_note(&t->step[9], A) && note_run(t, 0, C, C) == 16u;
    for (i = 10; i < 16u; i++)
        ok &= st_kind(&t->step[i]) == ST_TIE;
    tap_k(kG);
    ok &= t->step[9].n == 1u && goes(&t->step[9], C) && note_run(t, 0, G, G) == 9u && note_run(t, 0, C, C) == 16u;
    cursor_set(12); frame();
    key_down(kC); key_down(kE); frame(); key_up(kC); key_up(kE); frame();   /* (both at once: C ends, E comes in) */
    ok &= t->step[12].n == 1u && starts(&t->step[12], E) && note_run(t, 12, E, E) == 4u && note_run(t, 0, C, C) == 12u;
    for (i = 13; i < 16u; i++)
        ok &= st_kind(&t->step[i]) == ST_TIE;
    bad += check("  A again: out; G: ends at 9; C and E together at 13: C ends, E comes in to 16", ok);
    cursor_set(0); frame();
    tap_k(kC);
    ok = only(&t->step[0], C, 0) && note_run(t, 0, C, C) == 1u && note_run(t, 12, E, E) == 4u;
    bad += check("  on the chord's first step the rules as before: C alone pressed, C alone (the others' tails out)", ok);
    {   /* full: 4 notes going on, a fifth: 4 NOTES MAX */
        uint32_t kx = 99;
        track_defaults_steps(t);
        t->step[0] = (step_t){{(uint8_t)C, (uint8_t)E, (uint8_t)G, (uint8_t)A}, 4, ST_NOTE, 0, 100};
        for (i = 1; i < 16u; i++)
            t->step[i].time = ST_TIE;
        for (k = 0; k < 27u; k++)
            if (kb_map(t, k) != C && kb_map(t, k) != E && kb_map(t, k) != G && kb_map(t, k) != A && kb_map(t, k) != KB_SILENT)
                kx = k;
        cursor_set(5); frame();
        tap_k(kx);
        ok = msg_is("4 NOTES MAX") && st_kind(&t->step[5]) == ST_TIE && note_run(t, 0, C, C) == 16u;
        bad += check("  4 notes going on, a fifth pressed: 4 NOTES MAX, nothing changed", ok);
    }
    {   /* a long pattern: the chord over page 0's end (LEN 128, steps 61 .. 72), E ends at 66 */
        track_defaults_steps(t);
        t->p[P_SLEN] = 128;
        *ed_st(t, 60) = (step_t){{(uint8_t)C, (uint8_t)E, (uint8_t)G}, 3, ST_NOTE, 0, 100};
        for (i = 61; i < 72u; i++) {
            step_t *s = ed_st(t, i);
            memset(s, 0, sizeof *s);
            s->time = ST_TIE;
        }
        cursor_set(66); frame();
        tap_k(kE);
        tap_k(kA);
        ok = note_run(t, 60, C, C) == 12u && note_run(t, 60, E, E) == 6u && starts(ui_st(t, 66), A) &&
             note_run(t, 66, A, A) == 6u && st_kind(ui_st(t, 72)) != ST_TIE;
        bad += check("  a long pattern (LEN 128, a chord over page 0's end): E ends at 66, A from 67 to 72", ok);
    }
    press(B_REC);
    return bad;
}

/* (Dah) SAVE > FILE: KNOB 1 the projects' list, OPEN COPY NEW on KNOB 2..4; DELETE is OCT- held 0.5 s (the dialog;
 * NO PROJECT, STOP FIRST), its release no tap; the footer says it */
static int file_page(void)
{
    int bad = 0, ok;
    khint_t kh[4];
    uint32_t okm, n, i;
    char ha[16], hb[16];
    ui_power_on();
    go_page(GR_FILE);
    frame();
    ok = act_cols() == 14u && str_eq(act_name(0), "PROJECT") && str_eq(act_name(1), "OPEN") && str_eq(act_name(3), "NEW");
    turn(EN_K1, 1);
    ok &= ui.act == 0u;                                 /* (KNOB 1: the list, no action picked) */
    n = foot_keys(kh, &okm, ha, hb);
    for (i = 0; i < n && !(kh[i].key == KC_OCTDN && str_eq(kh[i].act, "(HOLD) DELETE")); i++)
        ;
    ok &= i < n;
    bad += check("SAVE > FILE: KNOB 1 the list, KNOB 2..4 OPEN COPY NEW; the footer OCT- (HOLD) DELETE", ok);
    btn_down(B_OCTDN);
    frames(700);
    btn_up(B_OCTDN);
    frame();
    ok = !ui.confirm && msg_is("NO PROJECT") && cur_page()->graph == GR_FILE;
    song.playing = 1;
    btn_down(B_OCTDN);
    frames(700);
    btn_up(B_OCTDN);
    frame();
    ok &= !ui.confirm && msg_is("STOP FIRST");
    song.playing = 0;
    bad += check("  OCT- held 0.5 s: DELETE asked (no project: NO PROJECT; playing: STOP FIRST), never without the dialog", ok);
    return bad;
}

/* (Dah) OCT- held 0.5 s deletes the track picked, any one (asked): on MIXER and on HOME; STOP FIRST, ONE TRACK AT LEAST */
static int del_track_hold(void)
{
    int bad = 0, ok;
    ui_power_on();
    song.ntrk = 4;
    trk_count_changed();
    track_select(0);
    go_home();
    frame();
    btn_down(B_OCTDN); frames(700); btn_up(B_OCTDN); frame();
    ok = ui.confirm == CF_DEL_TRK && ui.confirm_trk == 0u;
    press(B_OCTUP);                                     /* (yes) */
    ok &= !ui.confirm && trk_count() == 3u && msg_is("TRACK DELETED");
    bad += check("HOME: OCT- held asks DELETE TRACK 1? (the first: any one), OCT+ deletes it, 3 tracks", ok);
    song.playing = 1;
    btn_down(B_OCTDN); frames(560); btn_up(B_OCTDN); frame();
    ok = !ui.confirm && msg_is("STOP FIRST");
    song.playing = 0;
    song.ntrk = 1;
    trk_count_changed();
    btn_down(B_OCTDN); frames(560); btn_up(B_OCTDN); frame();
    ok &= !ui.confirm && msg_is("ONE TRACK AT LEAST");
    bad += check("  playing: STOP FIRST; one track: ONE TRACK AT LEAST", ok);
    return bad;
}

/* (Dah) REC's held menu: LOOP CUE (ON by default) and REC MODE (OVERDUB by default, REPLACE), bits 6 and 5 of the byte
 * MENU > LEDS's INV level is in (kept by LEDS), into seq.c (loop_cue, rec_replace) every frame */
static int rec_rows(void)
{
    int bad = 0, ok;
    ui_power_on();
    frame();
    ok = ly_rows(LAYER_REC) == 6u && rl_get(4) == 1u && rl_get(5) == 0u && loop_cue && !rec_replace &&
         str_eq(rl_name(5, 0), "OVERDUB") && str_eq(rl_name(5, 1), "REPLACE") && str_eq(REC_RL[4], "LOOP CUE");
    rec_set(4, 0);
    rec_set(5, 1);
    frame();
    ok &= rl_get(4) == 0u && rl_get(5) == 1u && !loop_cue && rec_replace;
    menu_put(MI_LEDS, LEDS_ROWS - 1u);                  /* (LEDS INV HI: its level beside them) */
    menu_put(MI_LEDS, 0);
    frame();
    ok &= rl_get(4) == 0u && rl_get(5) == 1u;
    bad += check("REC layer: LOOP CUE ON, REC MODE OVERDUB by default; OFF / REPLACE set, kept through MENU > LEDS", ok);
    ui_leds_inv &= (uint8_t)~REC_BITS;
    frame();
    return bad;
}

/* (Dah) GLO > SONG: the GRID (where PLAY / RESTART start) and the section picked, the last moved wins: the VIEW's GRID
 * moved into section 3, then section 2 picked: from 2's start; the GRID moved again: from it; playing, a section picked
 * moves it for the next PLAY (the song plays on) */
static int song_grid(void)
{
    int bad = 0, ok;
    uint32_t r, k;
    ui_power_on();
    chain_defaults(&chain_config);
    chain_config.count = 3;
    for (r = 0; r < 3u; r++) {
        for (k = 0; k < NTRK; k++)
            chain_config.row[r].slot[k] = k ? (uint8_t)CHAIN_SILENT : 1u;
        chain_config.row[r].repeat = 1;
    }
    trk[0].p[P_SLEN] = 64;                              /* (a section: four bars) */
    go_page(GR_SONG);
    frame();
    song_view_knob(0, 5);                               /* (VIEW: GRID five bars on: section 2, its 2nd bar) */
    ok = chain_from_row == 1u && chain_from_u == SG_BARU;
    ui.song_row = 0;
    turn(EN_K1, 2);                                     /* (section 3 picked) */
    ok &= ui.song_row == 2u && chain_from_row == 2u && chain_from_u == 0u;
    turn(EN_K1, -1);
    ok &= chain_from_row == 1u && chain_from_u == 0u;
    song_view_knob(0, -3);
    ok &= chain_from_row == 0u && chain_from_u == SG_BARU;   /* (the GRID last: it wins) */
    bad += check("SONG: the GRID and the section picked, the last moved decides where PLAY starts", ok);
    ok = !chain_prepare();
    blocks(40); frame();
    ok &= chain.running && chain.row == 0u;
    turn(EN_K1, 1);                                     /* (section 3: song_row 1 -> 2) */
    ok &= chain.running && chain.row == 0u && chain_from_row == 2u && chain_from_u == 0u;
    transport_req = 2;
    blocks(8); frame();
    chain.running = chain.armed = 0;
    ok &= !chain_prepare();
    blocks(8); frame();
    ok &= chain.running && chain.row == 2u;
    transport_req = 2;
    blocks(8); frame();
    chain.running = chain.armed = 0;
    bad += check("  playing: a section picked, the song plays on; the next PLAY from it", ok);
    return bad;
}

/* (Dah) SAVE held 0.7 s: the project open saved over itself (no project open here: the NAME page, FILE's COPY, a new
 * one); its release opens no SAVE page; playing: STOP FIRST */
static int save_hold(void)
{
    int bad = 0, ok;
    uint32_t pg;
    ui_power_on();
    go_home();
    frame();
    hold(B_SAVE);
    ok = name_on() && nm.kind == NK_ARCH_SAVE && ui.home;
    press(B_OCTDN);                                     /* (NAME cancelled) */
    ok &= !name_on() && ui.home;
    go_page(GR_ROLL);
    pg = ui.page;
    song.playing = 1;
    btn_down(B_SAVE); frames(750);
    ok &= msg_is("STOP FIRST") && !name_on();
    btn_up(B_SAVE); frame();
    ok &= ui.page == pg && !ui.home;
    song.playing = 0;
    press(B_SAVE);
    ok &= cur_page()->fam == FAM_SAVE;                  /* (a tap: the SAVE pages as before) */
    bad += check("SAVE held: the project open saved (none: the NAME page, a new one; playing: STOP FIRST), no page", ok);
    return bad;
}

/* FINE LEN (ui_input.c step_edit, ui.c note_set_len10): SEQ > STEP's KNOB 3 on a note, turned slowly a tenth a detent,
 * fast a whole step; the LEN card "1.3" */
static int fine_len(void)
{
    track_t *t;
    uint32_t i, a, b;
    ui_power_on();
    song.sel = 0;
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->p[P_VOICE] = V_POLY;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 0};
    go_page(GR_ROLL);
    ui.cursor = 0;
    frame();
    for (i = 0; i < 3u; i++) {
        memset(ui.enc_t, 0, sizeof ui.enc_t);          /* (a slow hand: a new turn each detent) */
        turn(EN_K3, 1);
    }
    a = note_len10(t, 0) * (fine_at(&motion, 0, 1) == 3u);
    for (i = 0; i < 6u; i++) {                         /* (a detent a frame: fast) */
        host_enc[panel.enc[EN_K3]] += panel.dir[EN_K3];
        frame();
    }
    b = note_len10(t, 0);
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    turn(EN_K3, -1);
    return check("FINE LEN: KNOB 3 slowly 1 -> 1.3 (its tenths on step 2), fast 1.4 then a step a detent (6.4), back 6.3",
                 a == 13u && b == 64u && note_len10(t, 0) == 63u &&
                 fine_at(&motion, 0, 6) == 3u);   /* (fast: the first detent of a turn a tenth, then a step each) */
}

int main(void)
{
    int bad = pan_range() + select_page() + oct_press() + project_erase_ui() + clean_init() + oct_layers() + menus_alike() + rec_keeps() + chord_inside() + file_page() + del_track_hold() + rec_rows() + song_grid() + save_hold() + fine_len();
    printf("%s\n", bad ? "1.5.5 UI TEST FAILED" : "1.5.5 ui tests passed");
    return bad != 0;
}
