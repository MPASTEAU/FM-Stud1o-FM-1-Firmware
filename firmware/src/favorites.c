/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1                              /* (the settings' bytes below: settings_persist.c) */
/* (Dah) The favourites are gone (the PRESETS page, its FAV, the stars): the struct stays, the settings' layout (PER4,
 * settings_persist.c) with it: row 13's bytes 30, 31, rows 14 (MIDI LEARN), 15 (the UI's settings) are read and
 * written; the favourite bits (rows 0..13, user, filter) kept as saved, unread (firmware before shows them again) */
typedef struct {
    uint8_t factory[16][32]; /* (was: engine 0..15, preset 0..255) */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
/* MENU > DISPLAY > COLOR's 11th value, CUSTOM (1.6, #244, Discussion #239): a palette of the user's, five RGB565 colours (BG
 * SURF TEXT THEME ACCENT, 16 bits little endian) in favorites.factory[15][0..9], flags in [12] (bit 0: one is stored, bit 1:
 * it is the palette in use), set from the editor (PALETTE_SET, editor_palette.c) and saved with the settings. settings.palette
 * stays a built-in palette (the one in use before, or after): firmware before 1.6 shows that and keeps the bytes. The value
 * is in COLOR only while one is stored. Zero (every older setting) = none */
#define ui_cpal (&favorites.factory[15][0])
#define ui_cpal_flags (favorites.factory[15][12])
#define CPAL_STORED 1u
#define CPAL_ACTIVE 2u
static uint32_t cpal_on(void) { return (ui_cpal_flags & (CPAL_STORED | CPAL_ACTIVE)) == (CPAL_STORED | CPAL_ACTIVE); }
static uint32_t palette_n(void) { return NPALETTES + ((ui_cpal_flags & CPAL_STORED) ? 1u : 0u); }     /* COLOR's values */
static uint32_t palette_cur(void) { return cpal_on() ? NPALETTES : settings.palette % NPALETTES; }
/* (Dah) its settings outside row 15 (full): row 13 (SLICE's favourites, no engine has 224 presets) bytes 30, 31 */
#define DAH_MIG (favorites.factory[13][30])            /* the one-time changes done: bit 0 SELECT LIST, 1 CHORD ADD, 2 the
                                                        * key map's bytes freed, 3 2ND CORE ON */
#define DAH_CUR (favorites.factory[13][31])            /* the project open (projfs.c PFS_CUR), its id, 0 none */
static const char *palette_name(uint32_t v) { return v == NPALETTES ? "CUSTOM" : UI_PALETTES[v % NPALETTES].name; }
/* the palette in use into gfx.c (the settings changed under it: a start, an import, a restore, a STYLE change) */
static void palette_apply(void)
{
#if defined(FELUCCA_DEMO) && FELUCCA_DEMO
    if (favorites.factory[15][13] != 'H') {         /* the first start: HOME's TRACKS once, the channel rack (ui.c ui_home_view
                                                     * HV_TRACKS; byte 13: free; saved with the settings, so a view picked
                                                     * later stays) */
        favorites.factory[15][13] = 'H';
        favorites.factory[15][26] = 0u;             /* (HV_TRACKS: 0 since TRACKS came first) */
    }
    if (!(DAH_MIG & 4u)) {                          /* (Dah) once: bytes 16, 17 of row 15 are the FX layer's key map
                                                     * (ui_layer.c fx_keys, 14..23): the open project and a mark were kept
                                                     * there by mistake: moved out (DAH_CUR), the keys' map back */
        DAH_MIG |= 4u;
        if (favorites.factory[15][17] == 'P') {
            DAH_CUR = favorites.factory[15][16];
            favorites.factory[15][16] = favorites.factory[15][17] = 0;
            DAH_MIG |= 1u;
        }
    }
    if (!(DAH_MIG & 1u)) {                          /* .. once, SELECT on the lists (MENU > SELECT LIST, ui.c sel_mode, bit 4
                                                     * of byte 24): the tempo on HOME, GLO + SELECT and HOME > CLOCK */
        DAH_MIG |= 1u;
        favorites.factory[15][24] = (uint8_t)((favorites.factory[15][24] & ~0x30u) | 0x10u);
    }
    if (!(DAH_MIG & 2u)) {                          /* .. once, CHORD ENTRY ADD: step recording adds each key to the cursor
                                                     * step and stays on it (bit 2 of byte 24; KNOB 1 / PRESETS move on) */
        DAH_MIG |= 2u;
        favorites.factory[15][24] |= 0x04u;
    }
#endif
#if FELUCCA_CPU2
    if (!(DAH_MIG & 8u)) {                          /* (Dah) once: MENU > SYSTEM > 2ND CORE ON, the default (a new FM-1,
                                                     * the settings reset, settings saved before); OFF stays OFF after */
        DAH_MIG |= 8u;
        favorites.factory[15][24] |= 0x80u;         /* (ui.c PREF_CPU2_ON, ui_prefs2's bit 7) */
    }
#endif
    if (cpal_on()) {
        palette_custom_load(ui_cpal);
        palette_set(NPALETTES);
    } else {
        palette_set(settings.palette % NPALETTES);
    }
}

/* the five colours into the settings' bytes (stored; not yet the palette in use: palette_choose / palette_apply) */
static void cpal_store(const uint16_t *c)
{
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        ui_cpal[2u * i] = (uint8_t)c[i];
        ui_cpal[2u * i + 1u] = (uint8_t)(c[i] >> 8);
    }
    ui_cpal_flags |= CPAL_STORED;
}
