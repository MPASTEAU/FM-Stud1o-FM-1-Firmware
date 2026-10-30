/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* 1.5.6, on the real sources (with the stubs of tests/ui_test.c):
 *   KITS      (#216) DRUM KIT DEEP TIGHT PERC (9..11) from the five kits' pieces, three DRUM presets after DRUM KIT; the
 *             lanes' names, the 8 lanes play, saved; an older KIT range reads them as 77
 *   LEDS      (#212) MENU > LEDS INV LO / INV HI appended: INV with its lit LEDs at the mid level (LO the darker), saved as
 *             INV plus a level byte, older settings as before
 *   SELECT    (#84) MENU > SELECT MODW / VEL after BPM / PAGE: the selected track's mod wheel (CC1 out) or
 *             the keys' velocity (sounding, sent); GLO + SELECT the tempo; PAGE's bit as 1.5.5, saved; CLOCK's hint
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* -------------------------------------------------------------- KITS --- */
/* (#216) DEEP TIGHT PERC: appended kit values 9..11 and DRUM presets 1..3 (the first, DRUM KIT, as it was); each lane one
 * of the five kits' (DEEP's kick tuned down); the lanes' names; they play; a project keeps them; what older firmware
 * makes of them (its KIT range ends at 77: a stored 9..11 lands there) */
static int kits(void)
{
    static const uint8_t SRC[3][DV_NLANE] = {{DK_77, DK_80, DK_77, DK_80, DK_80, DK_77, DK_80, DK_10},
                                            {DK_55, DK_66, DK_80, DK_55, DK_55, DK_55, DK_66, DK_80},
                                            {DK_10, DK_77, DK_77, DK_66, DK_66, DK_66, DK_77, DK_66}};
    int bad = 0, ok;
    uint32_t k, l;
    const param_desc_t *d = &ENG_DRUM.edit[0];
    param_desc_t old = *d;
    track_t *t;
    int32_t s;
    static int32_t kbuf[2 * CTL];
    ok = DK_DEEP == 9 && DK_TIGHT == 10 && DK_PERC == 11 && DK_COUNT == 12 && DV_NKIT == 8 && d->max == DK_PERC &&
         str_eq(d->names[DK_DEEP], "DEEP") && str_eq(d->names[DK_TIGHT], "TIGHT") && str_eq(d->names[DK_PERC], "PERC");
    for (k = 0; k < 3u; k++)
        for (l = 0; l < DV_NLANE; l++) {
            const dv_kit_t *a = &DV_KIT[5u + k][l], *b = &DV_KIT[SRC[k][l] - DK_80][l];
            if (k == 0u && l == DV_KICK)
                ok &= a->p16 == b->p16 - 32 && a->run == b->run && !memcmp(a->v, b->v, sizeof a->v);   /* (-2 st) */
            else
                ok &= !memcmp(a, b, sizeof *a);
        }
    bad += check("KITS (#216): DEEP TIGHT PERC appended (9..11), each lane one of the five kits' (DEEP's kick 2 st down)", ok);

    ok = ENG_DRUM.npresets >= 4u && str_eq(ENG_DRUM.presets[0].name, "DRUM KIT") && ENG_DRUM.presets[0].e[0] == DK_STD &&
         str_eq(ENG_DRUM.presets[1].name, "DEEP KIT") && ENG_DRUM.presets[1].e[0] == DK_DEEP &&
         str_eq(ENG_DRUM.presets[2].name, "TIGHT KIT") && ENG_DRUM.presets[2].e[0] == DK_TIGHT &&
         str_eq(ENG_DRUM.presets[3].name, "PERC KIT") && ENG_DRUM.presets[3].e[0] == DK_PERC;
    for (k = 1; k < 4u; k++)
        ok &= preset_cat(ENGI_DRUM, k) == CAT_DRUM;
    bad += check("  DRUM's presets: DRUM KIT first as before, then DEEP KIT, TIGHT KIT, PERC KIT (category DRUM)", ok);

    ui_power_on();
    t = &trk[0];
    host_preset(t, ENGI_DRUM, 3);
    t->engine = t->eng_req;
    ok = t->p[P_E0] == DK_PERC && str_eq(drum_lane_name(t, DV_TOM), "CONGA") && str_eq(drum_lane_name(t, DV_RIM), "CLAVE");
    ok &= drum_gm(t->p, 45, &s) == DV_KTYPE(8u, DV_TOM);
    t->p[P_E0] = DK_DEEP;
    ok &= str_eq(drum_lane_name(t, DV_BELL), "CYM") && drum_gm(t->p, 36, &s) == DV_KTYPE(6u, DV_KICK);
    t->p[P_E0] = DK_TIGHT;
    ok &= str_eq(drum_lane_name(t, DV_BELL), "BELL");
    for (k = DK_DEEP; k <= DK_PERC; k++) {              /* each plays its 8 lanes */
        uint32_t i, live = 0;
        t->p[P_E0] = (int16_t)k;
        for (i = 0; i < 8u; i++)
            trk_note_on(t, DRUM_LANE_NOTE[i], 110);
        events_block(CTL);
        mix_block(kbuf, CTL);
        for (i = 0; i < DV_NLANE; i++)
            live += drum_kit_of(t)[i].v.live;
        ok &= live == 8u;
        trk_all_off(t);
    }
    bad += check("  the lanes' names (DEEP: CYM; PERC: CONGA CLAVE), the GM notes on the kit's pieces, the 8 lanes play", ok);

    t->p[P_E0] = DK_TIGHT;
    song.playing = 0;
    ok = project_save_as(2, "KITS") == 0;
    t->p[P_E0] = DK_STD;
    project_load(2);
    ok &= t->p[P_E0] == DK_TIGHT;
    old.max = DK_77;                                    /* (1.5.1's KIT: STD .. 77) */
    for (k = DK_DEEP; k <= DK_PERC; k++)
        ok &= param_fit(&old, (int32_t)k) == DK_77 && param_fit(d, (int32_t)k) == (int32_t)k;
    bad += check("  a project keeps the kit; a 1.5.1-range KIT reads 9..11 as 77 (the README's downgrade note)", ok);
    return bad;
}

/* -------------------------------------------------------------- LEDS --- */
/* (#212) MENU > LEDS INV LO / INV HI: appended values (0..3 as before); INV's lit ones at the mid level (HI ~12.5 %, LO
 * ~8 %: the darker mid), the active ones dark as INV; saved as INV in the zoom field (older firmware: plain INV) with
 * the level in a spare settings byte; older settings read as before */
static int leds_lit(const uint8_t *a, uint32_t id) { uint8_t q = led_pos[id]; return q != 0xFF && ((a[q >> 3] >> (q & 7u)) & 1u); }
static int leds(void)
{
    int bad = 0, ok;
    uint32_t id, v, key = 14u + 5u, btn;
    ui_power_on();
    ui_leds();                                          /* (led_pos: a made-up matrix, every LED its own place) */
    for (id = 0; id < 41u; id++)
        led_pos[id] = (uint8_t)((id / 4u) << 3 | (1u + id % 4u));
    btn = panel.btn[B_SEQ];
    ok = menu_n(MI_LEDS) == 6u && str_eq(menu_vname(MI_LEDS, 3), "INV") && str_eq(menu_vname(MI_LEDS, 4), "INV LO") &&
         str_eq(menu_vname(MI_LEDS, 5), "INV HI") && str_eq(menu_vname(MI_LEDS, 0), "OFF") && menu_get(MI_LEDS) == 2u;
    for (v = 0; v < 6u; v++) {
        menu_put(MI_LEDS, v);
        ok &= menu_get(MI_LEDS) == v && (settings_leds == LEDS_INV) == (v >= 3u) && leds_inv() == (v > 3u ? v - 3u : 0u);
    }
    bad += check("LEDS (#212): OFF DIM LO DIM HI INV, then INV LO INV HI appended; each its own value", ok);

    menu_put(MI_LEDS, 3);                               /* INV: the idle key and button lit */
    frame(); ui_leds();
    ok = leds_lit(fm1_led, key) && leds_lit(fm1_led, btn) && !leds_lit(fm1_led_mid, key) && !host_dim_lo;
    menu_put(MI_LEDS, 5);                               /* INV HI: at the mid level, not lit */
    frame(); ui_leds();
    ok &= !leds_lit(fm1_led, key) && leds_lit(fm1_led_mid, key) && !leds_lit(fm1_led, btn) && leds_lit(fm1_led_mid, btn) &&
          !host_dim_lo;
    menu_put(MI_LEDS, 4);                               /* INV LO: the same, the darker mid level */
    frame(); ui_leds();
    ok &= !leds_lit(fm1_led, key) && leds_lit(fm1_led_mid, key) && host_dim_lo;
    key_down(5); frame(); ui_leds();                    /* a key held: dark in every INV */
    ok &= !leds_lit(fm1_led, key) && !leds_lit(fm1_led_mid, key);
    key_up(5); frame();
    menu_put(MI_LEDS, 2);
    frame(); ui_leds();
    ok &= !leds_lit(fm1_led_mid, key) && !host_dim_lo;
    bad += check("  INV: idle lit; INV HI / LO: idle at the mid level (LO the darker one), a held key dark; DIM HI as before", ok);

    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        menu_put(MI_LEDS, 5);
        settings_export(&p);
        ok = p.zoom == (LEDS_TAG | LEDS_INV) && leds_from_stored(p.zoom) == LEDS_INV && leds_stored_ok(p.zoom) &&
             p.favorites.factory[15][25] == 2u;          /* (older firmware: INV, the byte kept unread) */
        menu_put(MI_LEDS, 2);
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_LEDS) == 5u;
        p.favorites.factory[15][25] = 0;                 /* (a setting of before: INV) */
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_LEDS) == 3u;
        p.favorites.factory[15][25] = 7;                 /* (an unknown level: INV) */
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_LEDS) == 3u;
        p.favorites.factory[15][25] = 1;                 /* (a level with DIM HI: DIM HI) */
        p.zoom = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_LEDS) == 2u && !leds_inv();
        menu_put(MI_LEDS, 2);
    }
    bad += check("  saved: INV in the LEDS field (older firmware: INV), the level in a spare byte; older settings as before", ok);
    return bad;
}

/* ------------------------------------------------------------ SELECT --- */
/* (#84) MENU > CONTROL > SELECT MODW / VEL after BPM / PAGE: MODW the selected track's mod wheel (the matrix's MODW, CC1
 * out on its channel), VEL the keys' velocity (sounding, MIDI OUT, recorded); GLO + SELECT the tempo; the bits (PAGE's
 * as 1.5.5's), saved; older settings BPM */
static int select_play(void)
{
    int bad = 0, ok;
    track_t *t;
    int16_t b0;
    uint32_t mo, pk;
    ui_power_on();
    ok = menu_n(MI_SELECT) == 4u && str_eq(menu_vname(MI_SELECT, 0), "PATTERN") && str_eq(menu_vname(MI_SELECT, 1), "LIST") &&
         str_eq(menu_vname(MI_SELECT, 2), "MODW") && str_eq(menu_vname(MI_SELECT, 3), "VEL") && menu_get(MI_SELECT) == 0u;
    menu_put(MI_SELECT, 1);
    ok &= ui_prefs2 == 0x10u && sel_mode() == SEL_PAGE;     /* (PAGE: 1.5.5's bit) */
    menu_put(MI_SELECT, 2);
    ok &= ui_prefs2 == 0x40u && sel_mode() == SEL_MODW;
    menu_put(MI_SELECT, 3);
    ok &= ui_prefs2 == 0x50u && sel_mode() == SEL_VEL;
    {
        persist_t p = {0};
        p.magic = PERSIST_MAGIC; p.panel = panel;
        settings_export(&p);
        menu_put(MI_SELECT, 0);
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_SELECT) == 3u;
        p.favorites.factory[15][24] = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && menu_get(MI_SELECT) == 0u;
    }
    bad += check("SELECT (#84): BPM PAGE MODW VEL; PAGE bit 4 as 1.5.5, MODW / VEL bit 6 too; saved, older settings BPM", ok);

    menu_put(MI_SELECT, SEL_MODW);
    go_home(); frame();
    t = TSEL;
    t->mw = 0;
    b0 = song.g[G_BPM];
    usb.config = 1;
    mo = mo_w;
    turn(EN_SELECT, 3);
    ok = t->mw == 3u && str_eq(ui.msg, "MODW 3") && song.g[G_BPM] == b0 && ui.home;
    ok &= mo_w == mo + 1u && midi_out_q[mo % MQ] == (0x0Bu | (0xB0u | trk_midi_ch(song.sel)) << 8 | 1u << 16 | 3u << 24);
    turn(EN_SELECT, -5);
    ok &= t->mw == 0u;                                  /* (stops at 0) */
    turn(EN_SELECT, 127);
    ok &= t->mw == 127u;
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 2);
    ok &= song.g[G_BPM] == b0 + 2 && t->mw == 127u;     /* GLO + SELECT: the tempo */
    btn_up(B_GLO); frames(400);
    song.g[G_BPM] = b0;
    t->mw = 0;
    bad += check("  MODW: the track's mod wheel 0..127 (shown), CC1 out on its channel; GLO + SELECT tempo", ok);

    menu_put(MI_SELECT, SEL_VEL);
    go_home(); frame();
    ok = kb_vel == 100u;
    turn(EN_SELECT, -40);
    ok &= kb_vel == 60u && str_eq(ui.msg, "VEL 60") && song.g[G_BPM] == b0;
    mo = mo_w;
    key_down(white(3)); frame();
    pk = midi_out_q[mo % MQ];
    ok &= mo_w == mo + 1u && (pk & 0xF0FFu) == 0x9009u && pk >> 24 == 60u && TSEL->m_vel == 60u;
    key_up(white(3)); frame();
    turn(EN_SELECT, -100);
    ok &= kb_vel == 1u;
    turn(EN_SELECT, 200);
    ok &= kb_vel == 127u;
    kb_vel = 100;
    bad += check("  VEL: the keys' velocity 1..127 (100 at power-on), shown, sounding and sent; GLO + SELECT as ever", ok);

    menu_put(MI_SELECT, SEL_MODW);
    menu_put(MI_SELECT, 0);                             /* (Dah: CLOCK's page and its hint gone) */
    return bad;
}

int main(void)
{
    int bad = kits() + leds() + select_play();
    printf("%s\n", bad ? "1.5.6 TEST FAILED" : "1.5.6 tests passed");
    return bad != 0;
}
