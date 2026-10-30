/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAVE > PATTERN, the patterns (pstore.c; Dah: FL Studio's: a number is every track's), as a list:
 * KNOB 1 (or SELECT, PRESETS) goes from one to the next, every track playing it at once (as GLO + ALGORITHM anywhere);
 * KNOB 2 COPY (a new one, a copy of the one playing), KNOB 3 NEW (a new, empty one), OCT+ does it; KNOB 4 or REC DELETE
 * (the dialog). A row: its number, every track's steps in it, a line each. The memory the patterns take (ps_used of
 * PS_HEAP) on the last line. Included by ui_graph.c */
static uint8_t pt_sel;                                  /* the pattern picked (its number) */
static void pt_enter(void) { pt_sel = (uint8_t)ps_num(song.sel); }
static uint32_t pt_list(uint8_t *nums) { return ps_list_all(nums); }
static uint32_t pt_pick(void)                           /* the pattern picked, one of the track's */
{
    uint8_t nums[PS_NUM];
    uint32_t n = pt_list(nums), i;
    for (i = 0; i < n; i++)
        if (nums[i] == pt_sel)
            return pt_sel;
    pt_sel = (uint8_t)ps_num(song.sel);
    return pt_sel;
}
static void pt_move(int32_t steps)                      /* KNOB 1: up and down the list, the one there playing */
{
    uint8_t nums[PS_NUM];
    uint32_t n = pt_list(nums), i, cur = ps_num(song.sel);
    if (chain_busy()) {                                 /* (the song picks the patterns meanwhile) */
        ui_message("STOP THE SONG");
        return;
    }
    for (i = 0; i < n && nums[i] != cur; i++)
        ;
    i = (uint32_t)clamp((int32_t)i + (steps > 0 ? 1 : -1), 0, (int32_t)n - 1);
    if (nums[i] != cur)                                 /* (Dah) the rows glide to it (ui_graph.c graph_pattern) */
        pp_dy = clamp(pp_dy + (steps > 0 ? 17 : -17), -68, 68);
    if (nums[i] != cur && pattern_go_all(nums[i]))
        ui_message("MEMORY FULL");
    pt_sel = (uint8_t)ps_num(song.sel);
    ui.force = 1;
}
static uint32_t pt_mem_pct(void) { return (ps_used * 100u + PS_HEAP - 1u) / PS_HEAP; }
/* OCT+: the action picked (col 1 OPEN, 2 COPY, 3 NEW; Dah: KNOB 1..3, KNOB 4 DELETE: ui_input.c, the dialog) */
static void pt_do(uint32_t c)
{
    uint32_t k = song.sel % NTRK;
    int r = 0;
    if (c == 1u) {
        r = pattern_go_all(pt_pick());                  /* (Dah: every track to it, FL Studio's way) */
        if (!r) {
            char m[16] = "PATTERN ";
            fmt_int(m + 8, (int32_t)ps_num(k));
            ui_message(m);
        }
    } else {
        r = pattern_new_all(c == 2u);
        if (!r) {
            pt_sel = (uint8_t)ps_num(k);
            ui_message(c == 2u ? "COPIED" : "NEW PATTERN");
        } else if (r == 2) {
            ui_message("32 PATTERNS AT MOST");
            return;
        }
    }
    if (r == 1)
        ui_message("MEMORY FULL");
}
static int pt_ready(uint32_t c)                         /* column c (Dah: 0 the pattern, 1 COPY 2 NEW 3 DELETE) */
{
    return c == 0u || c == 3u ? 1 : ps_free_all() != 0u;
}
/* REC: delete the pattern picked (the dialog first; the last one: emptied) */
static void pt_delete(void)
{
    pattern_del_all(pt_pick());                         /* (Dah: on every track) */
    pt_sel = (uint8_t)ps_num(song.sel);
    ui_message("PATTERN DELETED");
}
