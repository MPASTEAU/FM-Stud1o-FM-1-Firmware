/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Optional device preferences, advertised in INFO. No preset format changes. The font weight
 * (ED_UI_FONT) is gone: one weight; UI_SET of it answers rc 2 (not supported), UI_STATE 127. (Dah) The favourites
 * too (ED_UI_FAVORITES not advertised: the editor hides its stars): UI_SET 3, FAV_GET, FAV_SET rc 2, UI_STATE's filter
 * 127, its signature 0 */
enum { ED_UI_PALETTE = 1, ED_UI_FONT = 2, ED_UI_MONITOR = 4, ED_UI_FAVORITES = 8 };
static uint32_t ed_ui_caps(void)
{
    uint32_t caps = ED_UI_PALETTE;
#ifdef FELUCCA_MONITOR
    caps |= ED_UI_MONITOR;
#endif
    return caps;
}
static void ed_u28(uint32_t n)
{
    for (uint32_t i = 0; i < 4; i++) ed_b((n >> (i * 7)) & 127u);
}
static void ed_ui_state(void)
{
    ed_b(ed_ui_caps());
    ed_b(palette_cur());                               /* (1.6: NPALETTES is CUSTOM) */
    ed_b(127);                                         /* font: not supported */
#ifdef FELUCCA_MONITOR
    ed_b(settings.monitor);
#else
    ed_b(127);
#endif
    ed_b(127);                                         /* the favourites' filter: gone */
    ed_u28(0);                                         /* .. their signature */
    ed_u28(up_gen);
}
/* Writes use the same flash path as the panel. rc 3 means applied in RAM,
 * but not saved (absent flash or a failed write); rc 4 means queued until STOP.
 * Repeated unchanged writes
 * do not wear flash. */
static uint32_t ed_ui_save(void)
{
    settings_save();
#if FELUCCA_FLASH
    if (!flash_ok || persist_pending == 2u) return 3;
    if (persist_pending == 1u) return 4;
    if (palette_from_stored(persist_saved.palette) != settings.palette) return 3;
#ifdef FELUCCA_MONITOR
    if (persist_saved.monitor != settings.monitor) return 3;
#endif
#ifdef FELUCCA_FAVORITES
    if (memcmp(&persist_saved.favorites, &favorites, sizeof favorites)) return 3;
#endif
    return 0;
#else
    return 3;
#endif
}
static uint32_t ed_ui_set(const uint8_t *a, uint32_t n)
{
    if (n != 2u || a[0] > 3u) return 1;
    if (!(ed_ui_caps() & (1u << a[0]))) return 2;
    if (a[1] >= (a[0] == 0 ? palette_n() : a[0] == 2 ? 3u : 2u)) return 1;
    switch (a[0]) {
    case 0:
        palette_choose(a[1]); break;
#ifdef FELUCCA_MONITOR
    case 2:
        fm1_irq_off();
        settings.monitor = a[1]; monitor_mode = a[1]; monitor_event.valid = 0;
        fm1_irq_on();
        break;
#endif
    }
    ui.force = 1;
    return ed_ui_save();
}
static int ed_ui_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    switch (cmd) {
    case ED_UI_STATE:
        ed_ui_state(); return 1;
    case ED_UI_SET:
        ed_b(ed_ui_set(a, n));
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u);
        ed_ui_state(); return 1;
    case ED_UI_PALETTES:
        ed_b(palette_n());                             /* (1.6: CUSTOM last, while one is stored) */
        for (uint32_t i = 0; i < palette_n(); i++) ed_str(palette_name(i), 12);
        return 1;
    case ED_FAV_GET:                                   /* (Dah: the favourites gone: not supported) */
    case ED_FAV_SET:
        ed_b(2);
        return 1;
    default: return 0;
    }
}
