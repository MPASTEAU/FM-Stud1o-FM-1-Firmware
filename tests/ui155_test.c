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
    ok &= !help_page();                                 /* (Dah: its hint named knobs and PRESETS only: none) */
    bad += check("  OPEN on the one played: nothing to do; no HELP hint (knobs and PRESETS are never named)", ok);
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
    ok &= lys.sq_row == 3u;                             /* (the last: it stops) */
    press(B_LFO);
    ok &= !ui.layer;
    bad += check("  REC's list: KNOB 1 the row (gliding), stops at the last; another button closes it", ok);
    return bad;
}

/* (Dah) REC never takes a note out: step recording (armed, stopped; CHORD ENTRY ADD and HOLD) and live recording
 * (armed, playing) of a key whose note is on the step already: it stays (recorded again), the step a note step */
static int has_note(const step_t *st, uint32_t note)
{
    uint32_t j;
    for (j = 0; j < st->n && st->note[j] != note; j++)
        ;
    return st_kind(st) == ST_NOTE && j < st->n;
}
static int rec_keeps(void)
{
    int bad = 0, ok = 1;
    uint32_t add, i, k = 7, note, held = 0;
    track_t *t;
    for (add = 0; add < 2u; add++) {
        ui_power_on();
        menu_put(MI_CHADD, add);
        t = TSEL;
        t->p[P_VOICE] = V_POLY;
        track_defaults_steps(t);
        note = kb_map(t, k);
        t->step[3] = (step_t){{(uint8_t)note}, 1, ST_NOTE, 0, 100};
        go_page(GR_ROLL);
        cursor_set(3);
        frame();
        press(B_REC);
        ok &= song.rec == 1u && !song.playing;
        tap_k(k);
        ok &= has_note(&t->step[3], note) && t->step[3].n == 1u;
        tap_k(k);
        ok &= has_note(&t->step[3], note) && t->step[3].n == 1u;
    }
    bad += check("REC on STEP (stopped, ADD and HOLD): a key whose note is on the step keeps it, twice", ok);
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    t->p[P_SLEN] = 16;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    note = kb_map(t, k);
    for (i = 0; i < 16u; i++)
        t->step[i] = (step_t){{(uint8_t)note}, 1, ST_NOTE, 0, 100};
    go_page(GR_ROLL);
    frame();
    song.rec = 1u << song.sel;
    transport_req = 1;
    for (i = 0; i < 500u; i++) {                        /* (8 s: four loops, the key struck again and again) */
        blocks(6);
        if (i % 5u == 0u) { key_down(k); held = 1; }
        else if (held && i % 5u == 2u) { key_up(k); held = 0; }
        frame();
    }
    if (held) key_up(k);
    transport_req = 2;
    blocks(8); frame();
    song.rec = 0;
    ok = !song.playing;
    for (i = 0; i < 16u; i++)
        ok &= has_note(&t->step[i], note);
    bad += check("REC playing: the same note struck over the steps that hold it: every step keeps it", ok);
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

int main(void)
{
    int bad = pan_range() + select_page() + oct_press() + project_erase_ui() + clean_init() + oct_layers() + menus_alike() + rec_keeps() + file_page();
    printf("%s\n", bad ? "1.5.5 UI TEST FAILED" : "1.5.5 ui tests passed");
    return bad != 0;
}
