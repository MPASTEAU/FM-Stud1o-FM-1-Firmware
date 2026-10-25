/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; HA HA HE HU fork */
/* HA HA HE HU: SAVE > PATTERN, the selected track's patterns (pstore.c), as a list: KNOB 1 (or SELECT, PRESETS) picks one,
 * KNOB 2 OPEN (the track plays it), KNOB 3 COPY (a new one, a copy of the one it plays), KNOB 4 NEW (a new, empty one),
 * OCT+ does it; REC DELETE (the dialog). A row: its number, its steps and its size; the one the track plays "PLAYING".
 * The memory the patterns take (ps_used of PS_HEAP) on the last line. Included by ui_graph.c */
static uint8_t pt_sel;                                  /* the pattern picked (its number) */
static int pt_page(uint32_t g) { return g == GR_PATTERN; }
static void pt_enter(void) { pt_sel = (uint8_t)ps_num(song.sel); }
static uint32_t pt_list(uint8_t *nums) { return ps_list(song.sel, nums); }
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
static void pt_move(int32_t steps)                      /* KNOB 1: up and down the list */
{
    uint8_t nums[PS_NUM];
    uint32_t n = pt_list(nums), i, cur = pt_pick();
    for (i = 0; i < n && nums[i] != cur; i++)
        ;
    i = (uint32_t)clamp((int32_t)i + (steps > 0 ? 1 : -1), 0, (int32_t)n - 1);
    pt_sel = nums[i];
    ui.force = 1;
}
static uint32_t pt_steps(uint32_t num)                  /* its steps that strike (notes, hits; not the ties) */
{
    uint32_t i, c = 0, n;
    int32_t at;
    const uint8_t *b;
    if (num == ps_num(song.sel)) {
        for (i = 0; i < NSTEP; i++)
            c += TSEL->step[i].time == ST_NOTE && (TSEL->step[i].n || TSEL->step[i].hit);
        return c;
    }
    at = ps_find(song.sel % NTRK, num);
    if (at < 0)
        return 0;
    b = ps_heap + at + 4 + 11;
    for (n = ps_heap[at + 4 + 10], i = 0; i < n; i++, b += 10)
        c += ((b[5] >> 3) & 3u) == ST_NOTE && ((b[5] & 7u) || b[7]);
    return c;
}
static uint32_t pt_mem_pct(void) { return (ps_used * 100u + PS_HEAP - 1u) / PS_HEAP; }
/* OCT+: the action picked (col 1 OPEN, 2 COPY, 3 NEW; Dah: KNOB 1..3, KNOB 4 DELETE: ui_input.c, the dialog) */
static void pt_do(uint32_t c)
{
    uint32_t k = song.sel % NTRK;
    int r = 0;
    if (c == 1u) {
        r = pattern_go(k, pt_pick());
        if (!r) {
            char m[16] = "PATTERN ";
            fmt_int(m + 8, (int32_t)ps_num(k));
            ui_message(m);
        }
    } else {
        r = pattern_new(k, c == 2u);
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
static int pt_ready(uint32_t c)                         /* column c (Dah: 0 OPEN 1 COPY 2 NEW 3 DELETE) */
{
    return c == 0u ? pt_pick() != ps_num(song.sel) : c == 3u ? 1 : ps_free_num(song.sel) != 0u;
}
/* REC: delete the pattern picked (the dialog first; the last one: emptied) */
static void pt_delete(void)
{
    pattern_del(song.sel % NTRK, pt_pick());
    pt_sel = (uint8_t)ps_num(song.sel);
    ui_message("PATTERN DELETED");
}
