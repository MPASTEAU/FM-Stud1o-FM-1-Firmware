/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by MENU > MIDI > MIDI IN (G_ROUTE):
 * CH1-4 (0) channels 1..4 play parts 1..4, channels 5..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track; 1.2: CH5-8, CH9-12, CH13-16 as CH1-4 on those four channels (midi_base).
 * A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t in_key;                   /* the key (+ 1) whose notes go into input_on now (key_on), 0: none (MIDI IN) */
static uint8_t kb_vel = 100;             /* 1.5.6 (#84): the keys' velocity (MENU > SELECT VEL, ui_input.c sel_play) */
/* #191: where each track's held[i] came from, so the arp maps it again as it plays (arp_list: TRN, ROOT, SCALE, QNT as
 * they are now). held_key: the key + 1 (bits 0..4) and the octave buttons then + 3 (bits 5..7); 0: not a key's own
 * mapping (MIDI IN, the DRUM grid, an engine's own key map), held[i] plays as it is. held_dt: held[i] above the key's
 * note (a CHRD chord's other notes). Out of track_t: its size stays as the audio ISR's trk[] indexing wants it */
static uint8_t held_key[NTRK][16];
static int8_t held_dt[NTRK][16];
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */
static volatile uint8_t midi_seen;       /* 1.5.5 (#208): bit per track, a MIDI IN note-on played it since the UI last
                                          * looked (ui_input.c midi_follow: the track's light, AUTO SELECT;
                                          * ui.c scr_input: MENU > MIDI > SCREEN WAKE +MIDI) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

/* MENU > MIDI > MIDI IN (G_ROUTE, up to 1.1.5 GLO > SYSTEM ROUT): the first channel (0..15) of the block of four that
 * plays tracks 1..4, -1 with SEL, -2 with TRACK (each track its own: song.mch). Stored
 * 0 CH1-4, 1 SEL, then (1.2, Discussion #143) 2 CH5-8, 3 CH9-12, 4 CH13-16, (1.5.5, #202) 5 TRACK */
static int32_t midi_base(void)
{
    uint32_t r = (uint32_t)song.g[G_ROUTE];
    return r == ROUTE_SEL ? -1 : r == ROUTE_TRACK ? -2 : r ? (int32_t)(r - 1u) * 4 : 0;
}
/* MIDI channel 0..15 of track i (keys -> MIDI out): its channel in ROUT's block (SEL, CH1-4: 1..4); TRACK its own (OMNI,
 * OFF: as CH1-4) */
static uint32_t trk_midi_ch(uint32_t i)
{
    int32_t b = midi_base();
    if (b == -2)
        return mch_get(i) < 16u ? mch_get(i) : i % NTRK;
    return b > 0 ? (uint32_t)b + (i & 3u) : i % NTRK;   /* (CH1-8 1..8; a block of four: 5..8 as 1..4) */
}

static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10). The DRUM grid: white keys are steps, black keys 1..8 lanes, 9 ACC, 10 / 11 the page */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}
enum { GK_ACC = NLANE, GK_PGDN, GK_PGUP };       /* black keys 9..11 on the grid */
/* the DRUM grid: key k's lane note (black keys 1..8), KB_SILENT for the rest. Out of line (1.5): called from the audio
 * ISR's key loop only on a key change; inlined there, its % 12 and key_place's loop sat in the ISR's static cost
 * (tests/target_budget.py) for a path that runs once per key press */
static __attribute__((noinline)) uint8_t grid_key_note(uint32_t k)
{
    return key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
}
/* GLO > SONG (song.grid 3): key k's slot, the white keys A3 B3 C4 D4 (white places 2..5: keys 4 6 7 9 from F) A..D; -1 = a
 * key that plays. A table, not key_place (a loop); the audio ISR's key loop reads SONG_KEYS, the same keys */
#define SONG_KEYS ((1u << 27) - 1u)              /* (Dah: the white keys patterns 1 .. 16; no key sounds there) */
static int32_t song_key(uint32_t k)                  /* (Dah) the white keys, left to right: patterns 1 .. 16 */
{
    return k < 27u && !key_black(k) ? (int32_t)key_place(k) : -1;
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys and QNT SEQ's sequenced notes */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

enum { QN_OFF, QN_SNAP, QN_WHITE, QN_SEQ, QN_SEQT };   /* P_QUANT (SEQ+T, 1.5.6 #218: appended; older firmware loads SEQ) */

/* the engine's own key map (DRUM's GM map, slices) for key k, -1 none */
static int32_t kb_engine(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    return e->keys ? e->keys(t, k) : -1;
}

/* key k's note with the octave buttons at oct: QNT, ROOT, SCALE, TRN (KB_SILENT: a black key under WHITE). The keys
 * (kb_map) and the arp's latched keys (arp_list, #191: as the keys play now, the octave they were pressed in) */
static uint32_t kb_pitch(const track_t *t, uint32_t k, int32_t oct)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == QN_SNAP || t->p[P_QUANT] >= QN_SEQ)   /* SNAP (and SEQ, SEQ+T): every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * oct + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], wo;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        wo = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            wo--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * wo + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    int32_t n = kb_engine(t, k);
    return n >= 0 ? (uint32_t)n : kb_pitch(t, k, song.octave);
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i, k = in_key - 1u;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u) {
        /* #191: a key the track's scale keyboard maps (not the DRUM grid, not an engine's own map) is kept as that
         * key, so TRN, ROOT, SCALE and QNT reach the notes the arp holds (arp_list); the octave stays the one it was
         * pressed in. Anything else (MIDI IN) plays as it came, as before */
        uint8_t *hk = held_key[trk_index(t)];
        hk[t->nheld] = in_key && song.grid != 1u && kb_engine(t, k) < 0
                           ? (uint8_t)(in_key | (uint32_t)(song.octave + 3) << 5) : 0u;
        held_dt[trk_index(t)][t->nheld] = (int8_t)(hk[t->nheld] ? (int32_t)note - kb_note[k] : 0);
        t->held[t->nheld++] = (uint8_t)note;
    }
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    uint8_t *hk = held_key[trk_index(t)];
    int8_t *dt = held_dt[trk_index(t)];
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note) {
            hk[k] = hk[i];
            dt[k] = dt[i];
            t->held[k++] = t->held[i];
        }
    t->nheld = (uint8_t)k;
}

#define ND_NONE 0u                      /* track_t nd_late / nd_early: nothing latched (seq_tick; else its seq_pos + 1) */
static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing && !chain.running; }

#include "motion.c"

/* the ARP's modes (P_AMODE). Append-only: a stored value keeps its meaning. ORD (1.2, it played as UP before) plays
 * the held notes in the order they were pressed, whatever ARP 2's ORD switch says (that switch orders the list of the
 * other modes). 1.2 (Discussion #95), over the note list (the held notes low to high, or as played,
 * across OCT octaves), C E G held: DNUP G E C E; UP+8 C E G C' (the first note again an octave above the top);
 * CONV C G E (outside in); DIVG E G C (inside out); PINKY C G E G (the top note between the others); THUMB
 * C E C G (the bottom one); WALK a random step up or down the list (bouncing off its ends); CHORD all the held
 * notes together (the first 4), an octave higher each step through OCT */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static void arp_forget(track_t *t)               /* (its notes ended otherwise: trk_all_off) */
{
    t->arp_note = t->arp_ch[0] = t->arp_ch[1] = t->arp_ch[2] = 0;
}

/* CHORD's notes. Not inlined: they run once an arp step, and kept out of the audio ISR's loop over the tracks
 * (tests/target_budget.py) */
static __attribute__((noinline)) void arp_chord_off(track_t *t, int rec)   /* the other notes end */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (t->arp_ch[i]) {
            if (rec)
                rec_release(t, t->arp_ch[i]);
            trk_note_off(t, t->arp_ch[i]);
            t->arp_ch[i] = 0;
        }
}

static __attribute__((noinline)) void arp_chord(track_t *t, const uint32_t *list, uint32_t n)
{
    uint32_t i, nt, top = 0, o = t->arp_idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    if (n > 4u)
        n = 4u;
    for (i = 0; i < n; i++) {                       /* the held notes (sorted or as played), o octaves up */
        nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (nt == 127u && top++)
            continue;                               /* (notes clamped onto the top one: once) */
        if (!t->arp_note)
            t->arp_note = (uint8_t)nt;
        else
            t->arp_ch[i - 1u] = (uint8_t)nt;
        if (rec_on(t))
            rec_note(t, nt, 100);
        trk_note_on(t, nt, 100);
    }
}

static void arp_silence(track_t *t)              /* the sounding arp note(s) end */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, 0);
    trk_note_off(t, t->arp_note);
    t->arp_note = 0;
}

static void arp_off(track_t *t)                  /* the sounding arp note(s) end (a recorded one too) */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, rec_on(t));
    trk_note_off(t, t->arp_note);
    if (rec_on(t))
        rec_release(t, t->arp_note);
    t->arp_note = 0;
}

/* the place in the note list (len notes) the step arp_idx plays (not inlined, as arp_chord); UP+8 adds its note */
static __attribute__((noinline)) uint32_t arp_pick(track_t *t, uint32_t *list, uint32_t len)
{
    uint32_t i = t->arp_idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8 && len < 64u)                   /* UP+8: the first note again over the top */
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = t->arp_walk;
        if (!i)
            w = 0;                                  /* (from the bottom) */
        else if (w >= len)
            w = len - 1u;                           /* (notes let go) */
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        t->arp_walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

/* n: samples the arp advances by (the external clock's while it runs, 0 stopped); gate_n: samples its
 * gate counts down by (real time when the external transport stops: a tapped or latched note still ends) */
static volatile uint32_t beat_pos, beat_n;      /* samples into the beat, the beat of the bar (0..3) */

/* the metronome (click.c) and the count-in (1.1, Discussion #131). The click follows the song tempo's quarter notes
 * (BPM, or the external clock's 24 pulses), 4 to the bar, the first accented, whatever a track's DIV or SWING: a beat is
 * four 1/16 steps as the sequencer counts them (click_beat_len: 4 x div_samples(1/16), which may be a few samples
 * shorter than beat_samples, so the click never drifts from the 1/16 tracks it is recorded against). clk_pos and
 * clk_step count the 1/16 steps as seq_tick counts a 1/16 track's (and midi_clock_pulse rescales clk_pos with them when
 * an external clock's tempo moves): CLK_START at seq_start clicks the first beat in the block step 0 starts in, and
 * beat k in the block a 1/16 track's step 4k starts in (every 4th step; step 0 of 16, the bar's first, accented).
 * The count-in: with MENU > COUNT-IN on, PLAY from stop with a track armed (REC while stopped starts PLAY too) on
 * the internal clock counts cin_total beats first (1 or 2 bars, clicked whatever CLICK is), then
 * starts the sequencer at step 1 exactly that many beats later (song.playing stays 0 until then: nothing plays or
 * records; the keys sound). A note into an armed track in the count-in's last eighth (the second half of its last
 * beat) is recorded onto step 1 when it starts (cin_keep, cin_flush), held on or let go as it was. An external clock
 * never counts in (its Start starts). STOP (seq_stop) cancels it. */
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* samples into the 1/16 step, the step of the bar (0..15) */
static volatile uint8_t cin_left, cin_total;    /* the count-in's beats still to come (0: none), of how many */
static volatile uint32_t cin_pos;               /* samples into its beat playing */
static uint8_t cin_flush;                       /* the count-in ended in this block: record what was kept */
static uint8_t cin_note[NTRK][4], cin_vel[NTRK][4], cin_n[NTRK], cin_up[NTRK];   /* kept notes; cin_up bit k: let go */
static int seq_counting(void) { return cin_left != 0u; }
static uint32_t click_beat_len(void) { return 4u * div_samples(2); }   /* DIV 2: 1/16 */
static int click_wanted(void)                  /* CLICK REC: while a track is armed (not in a song: no recording) */
{
    return click_mode == CLICK_ON || (click_mode == CLICK_REC && song.rec && !chain.running);
}
/* the arp's note list (list, its length returned; 0: nothing to play): the held notes (sorted or as played) over OCT
 * octaves, *cnt of them. #191: the notes as they play now, a key's mapped again through kb_pitch (TRN, ROOT, SCALE,
 * QNT now, the octave it was pressed in; a chord's note as far above it), the rest (MIDI IN) as they came; each once
 * (two keys may meet on one note now), a key silent now (a black key, QNT WHITE since) left out. With nothing changed
 * since the keys were pressed, exactly the list of held[] as before */
static uint32_t arp_list(const track_t *t, uint32_t *list, uint32_t *cnt)
{
    uint32_t i, j, n = 0, x, s, last = 0, len = 0, o;
    const uint8_t *hk = held_key[trk_index(t)];
    const int8_t *dt = held_dt[trk_index(t)];
    for (i = 0; i < t->nheld; i++) {
        x = t->held[i];
        if ((s = hk[i]) != 0u) {
            x = kb_pitch(t, (s & 31u) - 1u, (int32_t)(s >> 5) - 3);
            if (x == KB_SILENT)
                continue;
            x = (uint32_t)clamp((int32_t)x + dt[i], 0, 127);
        }
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!t->p[P_AORDER] && t->p[P_AMODE] != AM_ORD)   /* (ORD: as pressed, held[] keeps that order) */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    if (n && t->p[P_AMODE] == AM_REPEAT) { list[0] = last; len = 1; } /* REPEAT: last played note, no octave traversal */
    return len;
}

/* an arp step starts (period: its length): the next note of the list. Not inlined: once a step, kept out of the audio
 * ISR's loop over the tracks (tests/target_budget.py) */
static __attribute__((noinline)) void arp_fire(track_t *t, uint32_t period)
{
    uint32_t cnt, list[64], len, j;
    if (!(len = arp_list(t, list, &cnt))) {        /* (every key silent now: QNT WHITE's black keys) */
        if (t->arp_note)
            arp_off(t);
        return;
    }
    t->arp_idx++;
    j = t->p[P_AMODE] == AM_CHORD ? 0u : arp_pick(t, list, len);
    if (t->arp_note)
        arp_off(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (t->p[P_AMODE] == AM_CHORD) {            /* CHORD: the first 4 notes, an octave up each step */
            arp_chord(t, list, cnt);
            return;
        }
        t->arp_note = (uint8_t)list[j];
        if (rec_on(t))
            rec_note(t, t->arp_note, 100);           /* recording: what the arp plays */
        trk_note_on(t, t->arp_note, 100);
    }
}

static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period;
    int32_t sw;
    if (t->arp_note) {
        if (t->arp_off <= gate_n)
            arp_off(t);
        else
            t->arp_off -= gate_n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_off(t);
        return;
    }
    period = div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    t->arp_pos = 0;
    arp_fire(t, period);
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

/* NUDGE (1.2, #162): a note `off` samples from the start of the step it goes into, in 1/NUDGE_DIV of the step (period),
 * rounded (step_set_nudge keeps -8..+7): the first note of a fresh step sets it; QUANTIZE (P_SQNT) decides whether it
 * plays (not inlined: its divide stays out of the audio ISR's loops) */
static __attribute__((noinline)) int32_t rec_nudge(int32_t off, uint32_t period)
{
    return (off * NUDGE_DIV + (off < 0 ? -(int32_t)period / 2 : (int32_t)period / 2)) / (int32_t)(period ? period : 1u);
}

/* (overlaps) a step's notes as they start or go on (core.h st_cont), against q (qn), the notes the pattern sounds before
 * it: what sounds after it (out; a note said to go on that is not in q is silent there) */
static uint32_t pat_after(const step_t *s, const uint8_t *q, uint32_t qn, uint8_t *out)
{
    uint32_t c, k, j, n = 0;
    if (st_kind(s) == ST_TIE) {
        for (k = 0; k < qn; k++)
            out[k] = q[k];
        return qn;
    }
    if (st_kind(s) != ST_NOTE)
        return 0;
    for (k = 0, c = st_cont(s); k < s->n && k < 4u; k++) {
        for (j = 0; (c >> k) & 1u && j < qn && q[j] != s->note[k]; j++)
            ;
        if (j < qn || !((c >> k) & 1u))
            out[n++] = s->note[k];
    }
    return n;
}
/* .. a TIE made a NOTE step of the notes q, each going on; a REST a NOTE step of none (its other fields stay) */
static void st_explicit(step_t *s, const uint8_t *q, uint32_t qn)
{
    uint32_t k;
    if (st_kind(s) == ST_TIE) {
        for (k = 0; k < 4u; k++)
            s->note[k] = (uint8_t)(k < qn ? q[k] : 0u);
        s->n = (uint8_t)qn;
        s->time = ST_NOTE;
        st_set_cont(s, 15u);
    } else if (st_kind(s) == ST_REST) {
        s->n = 0;
        s->time = ST_NOTE;
    }
}
/* .. a TIE after a step whose notes change: the notes q (qn) it carried on, each going on (none: a REST), so it does
 * not carry on what came in */
static void st_hold_off(step_t *s, const uint8_t *q, uint32_t qn)
{
    if (st_kind(s) != ST_TIE)
        return;
    st_explicit(s, q, qn);
    if (!qn)
        s->time = ST_REST;
}
/* .. back: only notes going on, all of q (qn), no hit: a TIE (as older firmware reads it); nothing left: a REST */
static void st_canon(step_t *s, const uint8_t *q, uint32_t qn)
{
    uint32_t k, j;
    if (st_kind(s) != ST_NOTE || s->hit)
        return;
    if (!s->n) {
        s->time = ST_REST;
        return;
    }
    if (s->n != qn || st_cont(s) != (1u << s->n) - 1u)
        return;
    for (k = 0; k < s->n; k++) {
        for (j = 0; j < qn && q[j] != s->note[k]; j++)
            ;
        if (j == qn)
            return;
    }
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
}
/* (overlaps) POLY live recording: each note held is a hold of its own. rp[track]: st the step each rh_note went into;
 * go bit k, it has been held into a step since (back to its own: the whole loop, it ends); cm bit k, rec_hold carried it
 * on into the step playing (let go before its middle: taken out again); pv / pvn the notes the pattern sounds at the
 * step playing (as its steps say, chance and gate aside: seq_enter), what a TIE there carries on */
static struct { uint8_t st[4], go, cm, pv[4], pvn, poly, pass, took, gi, gk, gn, gnote[4]; int8_t gnd; } rp[NTRK];   /* (Dah) pass: the loops since PLAY;
                                                     * took: the pass REPLACE last wiped for */
/* (Dah) the notes m (bits of its notes) of NOTE step idx out, with their tails: the TIE after it made explicit with
 * what goes on there (none: a REST), every later step carrying one of them on without it (none left, no hit: a REST),
 * up to the first step that carries none of them on, so nothing is left going on or tied from them. at: the pattern's
 * step i (seq_wr live, ed_st on the page); len: the pattern's length (no wrap, as a note's length) */
static __attribute__((noinline)) void st_cut(track_t *t, step_t *(*at)(track_t *, uint32_t), uint32_t idx, uint32_t len,
                                             uint32_t m)
{
    step_t *s = at(t, idx), *u;
    uint8_t x[4], q[4];
    uint32_t nx = 0, qn = 0, k, j, i, c, any, rest;
    if (st_kind(s) != ST_NOTE)
        return;
    for (k = 0, c = st_cont(s); k < s->n && k < 4u; k++)
        if ((m >> k) & 1u)
            x[nx++] = s->note[k];
        else if ((c >> k) & 1u)
            q[qn++] = s->note[k];
    for (k = s->n > 4u ? 4u : s->n; k--;)
        if ((m >> k) & 1u)
            st_del(s, k);
    if (!s->n && !s->hit)
        s->time = ST_REST;
    for (i = idx + 1u, rest = 1; i < len && nx; i++) {   /* (rest: the step before ends what it carried) */
        u = at(t, i);
        if (st_kind(u) == ST_TIE) {
            if (rest) {                                 /* (it carried them: what goes on there, or a REST) */
                st_hold_off(u, i == idx + 1u ? q : 0, i == idx + 1u ? qn : 0u);
                rest = st_kind(u) == ST_REST;
            }
            continue;
        }
        if (st_kind(u) != ST_NOTE)
            break;
        for (j = any = 0; j < nx; j++)
            for (k = 0; k < u->n && k < 4u; k++)
                if (u->note[k] == x[j] && ((st_cont(u) >> k) & 1u)) {
                    st_del(u, k);
                    any = 1;
                    break;
                }
        if (!u->n && !u->hit)
            u->time = ST_REST;
        rest = st_kind(u) == ST_REST;
        if (!any)
            break;
    }
}
/* .. the notes starting at NOTE step s (bits of its notes; none going on there) */
static uint32_t st_starts(const step_t *s)
{
    return st_kind(s) == ST_NOTE ? ((1u << (s->n > 4u ? 4u : s->n)) - 1u) & ~st_cont(s) : 0u;
}

static void rh_drop(track_t *t, uint32_t k)        /* hold k ends */
{
    uint32_t i = trk_index(t) % NTRK, j, lo = (1u << k) - 1u;
    for (j = k; j + 1u < t->rh_n; j++) {
        t->rh_note[j] = t->rh_note[j + 1u];
        rp[i].st[j] = rp[i].st[j + 1u];
    }
    rp[i].go = (uint8_t)((rp[i].go & lo) | (rp[i].go >> 1 & ~lo));
    rp[i].cm = (uint8_t)((rp[i].cm & lo) | (rp[i].cm >> 1 & ~lo));
    t->rh_n--;
}
/* (overlaps) a POLY note recorded into step idx (next: the one after the step playing): beside what sounds there, the
 * notes held into it going on (a TIE made explicit, the TIE after it too: it would carry the new note on), its own hold
 * from there. Not inlined: off the audio ISR's loops */
static __attribute__((noinline)) void rec_poly(track_t *t, step_t *s, uint32_t idx, uint32_t next, uint32_t note, uint32_t vel,
                                               int32_t off, uint32_t len)
{
    uint32_t i = trk_index(t) % NTRK, k, np;
    uint8_t po[4];
    np = next ? pat_after(s, rp[i].pv, rp[i].pvn, po) : rp[i].pvn;   /* (what it sounds now) */
    if (!next)
        memcpy(po, rp[i].pv, 4);
    if (len > 1u)
        st_hold_off(seq_wr(t, (idx + 1u) % len), po, np);
    st_explicit(s, rp[i].pv, rp[i].pvn);            /* (a TIE: the notes sounding there; the one after the step playing: the
                                                     * step playing's) */
    if (!s->n || st_cont(s) == (1u << s->n) - 1u) {  /* a fresh step: no note starts there yet */
        s->flags = 0;
        s->vel = 0;
        step_set_nudge(s, off);
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        k = s->n - 1u;
        s->note[k] = (uint8_t)note;
    }
    st_set_cont(s, st_cont(s) & ~(1u << k));        /* (one going on, struck again: it starts here) */
    if (vel > 110)
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    if (!next && rp[i].pvn < 4u) {                  /* (the step playing sounds it now) */
        for (k = 0; k < rp[i].pvn && rp[i].pv[k] != note; k++)
            ;
        if (k == rp[i].pvn)
            rp[i].pv[rp[i].pvn++] = (uint8_t)note;
    }
    if (!rp[i].poly)
        t->rh_n = 0;                                /* (a MONO hold before: over) */
    rp[i].poly = 1;
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n) {
        if (k == 4u)
            rh_drop(t, 0);                          /* (four held: the oldest ends) */
        k = t->rh_n++;
    }
    t->rh_note[k] = (uint8_t)note;
    rp[i].st[k] = (uint8_t)idx;
    rp[i].go &= (uint8_t)~(1u << k);
    rp[i].cm &= (uint8_t)~(1u << k);
}
/* (overlaps) the sequencer enters step idx (before playing it) with POLY notes held: each carried on there beside what
 * the step holds (the TIE after it made explicit when they were not there: let go before it, they end), but in its own
 * step, or a step that starts that note itself (the hold ends before it) */
static __attribute__((noinline)) void rec_hold_poly(track_t *t, uint32_t idx, uint32_t len)
{
    uint32_t i = trk_index(t) % NTRK, k, j, np, chg = 0, any = 0;
    uint8_t po[4];
    step_t *s;
    rp[i].cm = 0;
    for (k = 0; k < t->rh_n; k++)
        if (rp[i].st[k] == idx && ((rp[i].go >> k) & 1u))
            rh_drop(t, k--);                        /* (held the whole loop round: it is the loop) */
        else
            any |= rp[i].st[k] != idx;
    if (!any)
        return;
    s = seq_wr(t, idx);
    np = pat_after(s, rp[i].pv, rp[i].pvn, po);
    st_explicit(s, rp[i].pv, rp[i].pvn);
    for (k = 0; k < t->rh_n; k++) {
        if (rp[i].st[k] == idx)
            continue;
        for (j = 0; j < s->n && s->note[j] != t->rh_note[k]; j++)
            ;
        if (j == s->n && s->n < 4u) {
            s->note[s->n++] = t->rh_note[k];
            chg = 1;
        } else if (j == s->n || !((st_cont(s) >> j) & 1u)) {
            rh_drop(t, k--);                        /* (the step full, or the note starting there: it ends before) */
            continue;
        }
        st_set_cont(s, st_cont(s) | 1u << j);
        rp[i].go |= (uint8_t)(1u << k);
        rp[i].cm |= (uint8_t)(1u << k);
    }
    st_canon(s, rp[i].pv, rp[i].pvn);
    if (chg && len > 1u)
        st_hold_off(seq_wr(t, (idx + 1u) % len), po, np);
}
/* FINE LEN, recorded: a note let go in step `at` (the one playing), `from` samples after its start where it began (0:
 * carried into it): its tenths there as played (a whole step: none; recorded as played, QUANTIZE says how it plays) */
static __attribute__((noinline)) void rec_fine(track_t *t, uint32_t at, int32_t from)
{
    uint32_t cur = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx);
    int32_t f;
    if (t->seq_pos >= 0x7FFFFFFFu || !cur)
        return;
    f = ((int32_t)t->seq_pos - from) * 10 / (int32_t)cur;   /* (a tenth begun: the note sounds into it) */
    f += ((int32_t)t->seq_pos - from) * 10 % (int32_t)cur ? 1 : 0;
    (void)fine_raw(t, at, f < 1 ? 1u : f >= 10 ? 0u : (uint32_t)f);
}
static int32_t rec_from(const track_t *t, uint32_t at)   /* where step at's notes began: its NUDGE */
{
    return step_nudge(seq_at(t, at)) * (int32_t)div_samples((uint32_t)t->p[P_SDIV]) / NUDGE_DIV;
}
/* (overlaps) POLY note let go (its hold k): early in the step it was carried on into, not there (that step without it) */
static __attribute__((noinline)) void rec_release_poly(track_t *t, uint32_t note)
{
    uint32_t i = trk_index(t) % NTRK, j, k;
    step_t *s;
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n)
        return;
    if (((rp[i].cm >> k) & 1u) &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u) {
        s = seq_wr(t, t->seq_idx);
        st_explicit(s, rp[i].pv, rp[i].pvn);
        for (j = 0; j < s->n && (s->note[j] != note || !((st_cont(s) >> j) & 1u)); j++)
            ;
        st_del(s, j);
        st_canon(s, rp[i].pv, 0);                   /* (never all of what sounded before: it went on) */
        for (j = 0; j < rp[i].pvn && rp[i].pv[j] != note; j++)
            ;
        if (j < rp[i].pvn)
            rp[i].pv[j] = rp[i].pv[--rp[i].pvn];
    } else if ((rp[i].cm >> k) & 1u)
        rec_fine(t, t->seq_idx, 0);                 /* (FINE LEN: let go late in it) */
    else if (rp[i].st[k] == t->seq_idx)
        rec_fine(t, t->seq_idx, rec_from(t, t->seq_idx));   /* (in its own step) */
    rh_drop(t, k);
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one; QUANTIZE OFF: the step whose window it falls in).
 * (Dah) A key onto a step holding that note alone takes it out; onto other notes starting there it replaces them;
 * keys landing there together (while the first is held) make it their chord (rec_live, rp[].gi / gk / gnote; the ARP
 * adds as before); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * MONO / LEGATO / UNISON: a note recorded into another step ends the hold before. POLY (overlaps, rec_poly): each note
 * its own hold, beside the others: a note pressed while another is held starts beside it, the first going on */
/* (Dah) REC MODE REPLACE: a note played in a pass after the one the last went into: the take again, every note, TIE and
 * hit of the pattern gone first (the track's steps, the ring's; its pages: pg_service, rec_wipe_req) */
static volatile uint8_t rec_wipe_req;               /* bit per track (main loop: its pages' records) */
static __attribute__((noinline)) void rec_wipe(track_t *t)
{
    uint32_t k = trk_index(t) % NTRK, j, n;
    for (j = 0; j < NSTEP; j++) {
        memset(&t->step[j], 0, sizeof t->step[j]);
        t->step[j].time = ST_REST;
    }
    for (j = 0; j < PGR_N; j++)
        if (pgr[k].abs[j]) {
            memset(&pgr[k].st[j], 0, sizeof pgr[k].st[j]);
            pgr[k].st[j].time = ST_REST;
            pgr[k].dirty |= (uint16_t)(1u << j);
        }
    rec_wipe_req |= (uint8_t)(1u << k);
    for (j = n = 0; j < motion.count; j++)             /* (its FINE LENs too) */
        if (motion.event[j].param != MOTION_FINE || MP_TRK(&motion.event[j]) != k)
            motion.event[n++] = motion.event[j];
    for (j = n; j < motion.count; j++)
        memset(&motion.event[j], 0, sizeof motion.event[j]);
    motion.count = (uint8_t)n;
}
/* (Dah) REPLACE on a DRUM track: lane by lane. A pass's first hit of lane l wipes that lane only (its hits and accents;
 * a step left with nothing a REST), the other lanes stay: the hats played again, the kick kept. rp_lw[track]: the lanes
 * wiped this pass; rp_wl: those still to wipe on its pages (the main loop's: pg_service). In the pool (zeroed) */
static uint8_t rp_lw[NTRK] __attribute__((section(".pool")));
static uint8_t rp_wl[NTRK] __attribute__((section(".pool")));
static void lane_out(step_t *s, uint32_t m)
{
    s->hit &= (uint8_t)~m;
    s->acc &= (uint8_t)~m;
    if (!s->hit && !s->n && st_kind(s) == ST_NOTE) {
        memset(s, 0, sizeof *s);
        s->time = ST_REST;
    }
}
static __attribute__((noinline)) void rec_wipe_lane(track_t *t, uint32_t m)
{
    uint32_t k = trk_index(t) % NTRK, j;
    for (j = 0; j < NSTEP; j++)
        lane_out(&t->step[j], m);
    for (j = 0; j < PGR_N; j++)
        if (pgr[k].abs[j]) {
            lane_out(&pgr[k].st[j], m);
            pgr[k].dirty |= (uint16_t)(1u << j);
        }
    rp_wl[k] |= (uint8_t)m;
}
static uint8_t rec_live;                            /* (Dah) rec_note: a key played (input_on, the count-in), not the ARP */
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, k;
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), cur = step_samples(t, period, t->seq_idx);
    uint32_t next = t->seq_pos > cur / 2u, sounds;
    int32_t off = rec_nudge(t->seq_pos >= cur ? 0 : next ? -(int32_t)(cur - t->seq_pos) : (int32_t)t->seq_pos, period);
    step_t *s;
    if (next)
        idx = (idx + 1u) % len;
    {   /* (Dah) REPLACE: the first note of a new pass (one played just before its step 1: that pass) */
        uint32_t i = trk_index(t) % NTRK, p = (uint8_t)(rp[i].pass + (next && !idx));
        if (rec_replace && drum_track(t)) {           /* (DRUM: the lane played only) */
            uint32_t m = 1u << (drum_lane(note) & 7u);
            if (p != rp[i].took)
                rp_lw[i] = 0;
            if (!(rp_lw[i] & m)) {
                rec_wipe_lane(t, m);
                rp_lw[i] |= (uint8_t)m;
            }
        } else if (rec_replace && p != rp[i].took)
            rec_wipe(t);
        rp[i].took = (uint8_t)p;
    }
    s = seq_wr(t, idx);
    /* the step's notes play later in this pass (it is the next one, not played early yet, or the one playing whose
     * nudge has not come yet): what sounds now must not play again there (rskip) */
    sounds = next ? t->nd_fired != idx + 1u : t->nd_late != ND_NONE;
    if (drum_track(t)) {                            /* DRUM: into the grid, no holds (hits) */
        uint32_t l = drum_lane(note);
        if (st_kind(s) != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
            step_set_nudge(s, off);
        }
        s->time = ST_NOTE;
        if (note == DRUM_LANE_NOTE[l]) {            /* a lane's note: its hit */
            s->hit |= (uint8_t)(1u << l);
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else {                                    /* another GM drum (a low tom, a crash): a note */
            for (k = 0; k < s->n && s->note[k] != note; k++)
                ;
            if (k == s->n) {
                if (s->n < 4u)
                    s->n++;
                s->note[s->n - 1u] = (uint8_t)note;
            }
            if (vel > 110)
                s->flags |= SF_ACCENT;
        }
        if (vel > s->vel)
            s->vel = (uint8_t)vel;
        t->seq_active = 1;
        if (sounds) {
            if (t->rskip_idx != idx)
                t->rskip_n = 0;
            t->rskip_idx = (uint8_t)idx;
            if (t->rskip_n < NLANE)
                t->rskip[t->rskip_n++] = (uint8_t)note;
        }
        return;
    }
    if (sounds) {                                   /* it sounds now: the step must not trigger it again */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    t->seq_active = 1;
    if (rec_live) {   /* (Dah) the keys played: one onto a step holding that note alone takes it out (and records nothing
                       * more: no hold); onto other notes it replaces them; keys landing on that step together (while the
                       * first is held) make it exactly their notes. The notes replaced go with their tails (st_cut) */
        uint32_t i = trk_index(t) % NTRK, m = st_starts(s), j;
        if (rp[i].gk && rp[i].gi == idx + 1u) {         /* another key of the keys held there: the step is all of them */
            for (j = 0; j < rp[i].gn && rp[i].gnote[j] != note; j++)
                ;
            if (j == rp[i].gn && j < 4u)
                rp[i].gnote[rp[i].gn++] = (uint8_t)note;
            rp[i].gk++;
            off = rp[i].gnd;                            /* (the first key's time) */
            if (m)
                st_cut(t, seq_wr, idx, len, m);
            if (trk_vmode(t) == V_POLY) {
                for (j = 0; j < rp[i].gn; j++)
                    rec_poly(t, seq_wr(t, idx), idx, next, rp[i].gnote[j], vel, off, len);
                return;
            }
        } else {
            rp[i].gi = (uint8_t)(idx + 1u);
            rp[i].gk = 1;
            rp[i].gn = 1;
            rp[i].gnote[0] = (uint8_t)note;
            rp[i].gnd = (int8_t)off;
            if (m) {
                for (j = 0; !((m >> j) & 1u); j++)
                    ;
                if (m == 1u << j && s->note[j] == note) {   /* that note alone: out */
                    st_cut(t, seq_wr, idx, len, m);
                    if (trk_vmode(t) != V_POLY)
                        t->rh_n = 0;                    /* (a MONO hold elsewhere ends, as another note would end it) */
                    return;
                }
                st_cut(t, seq_wr, idx, len, m);         /* other notes: replaced */
            }
        }
        s = seq_wr(t, idx);
    }
    if (trk_vmode(t) == V_POLY) {
        rec_poly(t, s, idx, next, note, vel, off, len);
        return;
    }
    rp[trk_index(t) % NTRK].poly = 0;
    s->n = 1;                                       /* a fresh step of this note (one a step) */
    s->note[0] = (uint8_t)note;
    s->flags = vel > 110 ? SF_ACCENT : 0u;
    s->vel = (uint8_t)vel;
    step_set_nudge(s, off);
    s->time = ST_NOTE;
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k = trk_index(t), poly = rp[k % NTRK].poly;
    if (!t->rh_n)
        return;
    if (!((song.rec >> k) & 1u) || (!poly && t->rh_ties + 1u >= len)) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (poly) {
        rec_hold_poly(t, idx, len);
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* (recorded ahead into the step now starting) */
    s = seq_wr(t, idx);
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = s->acc = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, g = trk_index(t) % NTRK;
    for (i = 0; rp[g].gk && i < rp[g].gn; i++)          /* (Dah) one of the keys of a step's gesture let go */
        if (rp[g].gnote[i] == note) {
            rp[g].gk--;
            break;
        }
    if (rp[trk_index(t) % NTRK].poly) {
        rec_release_poly(t, note);
        return;
    }
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u)
        *seq_wr(t, t->rh_last) = t->rh_bak;         /* released early in it: not held into this step */
    else if (t->rh_ties && t->seq_idx == t->rh_last)
        rec_fine(t, t->rh_last, 0);                 /* (FINE LEN: late in it, its tenths) */
    else if (!t->rh_ties && t->seq_idx == t->rh_start)
        rec_fine(t, t->rh_start, rec_from(t, t->rh_start));
}

/* a note into armed track t in the count-in's last eighth: kept for step 1 (cin_flush); once each, 4 per track */
static __attribute__((noinline)) void cin_keep(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t), k;
    if (cin_left != 1u || cin_pos < click_beat_len() / 2u || !((song.rec >> i) & 1u) || t->p[P_AMODE])
        return;
    for (k = 0; k < cin_n[i] && cin_note[i][k] != note; k++)
        ;
    if (k == cin_n[i]) {
        if (k == 4u)
            return;
        cin_n[i]++;
    }
    cin_note[i][k] = (uint8_t)note;
    cin_vel[i][k] = (uint8_t)vel;
    cin_up[i] &= (uint8_t)~(1u << k);
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    if (cin_left)
        cin_keep(t, note, vel);
    if (rec_on(t) && !t->p[P_AMODE]) {             /* (ARP on: arp_tick records its notes) */
        rec_live = 1;
        rec_note(t, note, vel);
        rec_live = 0;
    }
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    if (cin_left) {                                 /* a kept note let go before step 1: recorded, then let go */
        uint32_t i = trk_index(t), k;
        for (k = 0; k < cin_n[i]; k++)
            if (cin_note[i][k] == note)
                cin_up[i] |= (uint8_t)(1u << k);
    }
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the white keys F3 .. G5 the map's (perf_map: the settings,
 * ui_layer.c; by default 1.1.5's ten, FLANGER, PHASER, REV / ECHO THROW), black keys 1..4 the track mutes */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_KEYS ? perf_map[p] : PF_N) : p < 4u ? PF_M1 + trk_bank() + p : PF_N;   /* (the bank's) */
}

/* a key of the FX layer down (FX held) / up: the effect it holds. kb_fx: the one it took (+ 1, 0 none), so a key
 * remapped while held lets go of what it holds; an effect two keys hold ends with the last of them. noinline: out
 * of keyboard_block's loop (the audio ISR) */
static uint8_t kb_fx[27];
static __attribute__((noinline)) void perf_key_press(uint32_t k, int down)
{
    uint32_t e, j;
    if (down) {
        e = perf_key(k);
        kb_fx[k] = (uint8_t)(e + 1u);
        perf_press(e, 1);
        return;
    }
    if (!kb_fx[k])
        return;
    e = kb_fx[k] - 1u;
    kb_fx[k] = 0;
    for (j = 0; j < 27u; j++)
        if (kb_fx[j] == e + 1u)
            return;
    perf_press(e, 0);
}

/* the map changed (ui_layer.c: PRESETS with a key held, EDIT, a settings load): a white key held takes its new
 * effect at once (the old one let go); FX LATCH: a latched effect of the key's becomes the new one */
static __attribute__((noinline)) void perf_key_remap(void)
{
    uint32_t k, e, o;
    perf_remap = 0;
    for (k = 0; k < 27u; k++) {
        if (!kb_fx[k] || key_black(k) || (o = kb_fx[k] - 1u) == (e = perf_key(k)))
            continue;
        if (!perf_latch_on) {
            perf_key_press(k, 0);
            perf_key_press(k, 1);
            continue;
        }
        kb_fx[k] = (uint8_t)(e + 1u);
        if (o < PF_N && ((perf_latched >> o) & 1u)) {
            perf_latched &= ~PF_BIT(o);
            if (e < PF_N) {
                perf_latched |= PF_BIT(e);
                perf_ord[e] = ++perf_seq;
            }
        }
    }
}

/* (Dah) MATCH: the chord is the keys held's, whatever their order: on each press and release it is made again
 * (chord.c chord_match) from the keys of the track held; the tones it adds belong to the key pressed last (`own`), the
 * others keep their own note only. Tones no longer in it let go, new ones start; the notes sounding still stay */
static uint8_t kb_age[27];
static uint8_t kb_ages;
static __attribute__((noinline)) void match_rechord(track_t *t)
{
    uint32_t j, i, a, own = 27u, n, mc = trk_midi_ch(trk_index(t));
    uint8_t out[MATCH_MAX];
    for (j = 0; j < 27u; j++)                           /* the key pressed last */
        if (kb_chn[j] && kb_trk[j] == trk_index(t) && (own == 27u || (uint8_t)(kb_age[j] - kb_age[own]) < 128u))
            own = j;
    if (own == 27u)
        return;
    n = chord_match(t, kb_note[own], out);
    for (j = 0; j < 27u; j++) {                         /* the tones the chord no longer has: out */
        if (!kb_chn[j] || kb_trk[j] != trk_index(t))
            continue;
        for (i = 1; i < kb_chn[j];) {                   /* (0: the key's own note) */
            uint32_t x = kb_chord[j][i], keep = 0;
            for (a = 1; a < n && !keep; a++)
                keep = out[a] == x && j == own;
            if (keep) {
                i++;
                continue;
            }
            kb_chord[j][i] = kb_chord[j][kb_chn[j] - 1u];
            kb_chn[j]--;
            if (!midi_local_held(t, x)) {
                input_off(t, x);
                midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
            }
        }
    }
    for (a = 1; a < n && kb_chn[own] < MATCH_MAX; a++) {   /* the new ones: in (the key pressed last's) */
        uint32_t x = out[a], have = 0;
        for (i = 0; i < kb_chn[own]; i++)
            have |= kb_chord[own][i] == x;
        if (have)
            continue;
        if (!midi_local_held(t, x)) {
            input_on(t, x, kb_vel);
            midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | kb_vel << 24);
        }
        kb_chord[own][kb_chn[own]++] = (uint8_t)x;
    }
}

/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n, i, mc = trk_midi_ch(trk_index(t)), vel = kb_vel;
    kb_chn[k] = 0;
    if (t->p[P_CHRD] == CH_MATCH) {                 /* (Dah) MATCH: the key's note, then the chord of all the keys */
        kb_chord[k][0] = kb_note[k];
        n = 1;
    } else
        n = chord_build(t, kb_note[k], kb_chord[k]);
    in_key = (uint8_t)(k + 1u);                     /* (the arp keeps the key: arp_add) */
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, vel);
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | vel << 24);
    }
    in_key = 0;
    kb_chn[k] = (uint8_t)n;
    kb_age[k] = ++kb_ages;
    if (t->p[P_CHRD] == CH_MATCH)
        match_rechord(t);
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
    if (t->p[P_CHRD] == CH_MATCH)
        match_rechord(t);                           /* (Dah) the keys still held: their chord */
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    uint32_t lay, fx, mute;
    ch = cur ^ kb_prev;                          /* keys also sound while entering steps */
    if (perf_remap)
        perf_key_remap();
    if (!ch)
        return;
    if (kb_boot_hold) {                           /* #137: under the splash; their release is silent too */
        for (k = 0; k < 27u; k++)
            if ((ch & cur) >> k & 1u)
                kb_note[k] = KB_SILENT;
        kb_prev = cur;
        return;
    }
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
    mute = song.grid == 2u || song.grid == 4u || kb_master ? ~0u : song.grid == 3u ? SONG_KEYS : 0u;   /* the keys that never sound here */
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_trk[k] = song.sel;
            if (kb_asleep) {                      /* (SCREEN OFF: the wake press is swallowed) */
                kb_note[k] = KB_SILENT;
                continue;
            }
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_key_press(k, 1);
                continue;
            }
            if ((mute >> k) & 1u)                 /* NAME (ui_name.c): every key types, none sounds; SONG: A..D */
                kb_note[k] = KB_SILENT;
            else if (song.grid == 1u)             /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = grid_key_note(k);
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_key_press(k, 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            key_off(k, &trk[kb_trk[k] % NTRK]);
        }
    }
    kb_prev = cur;
}

/* -------------------------------------------------------- sequencer --- */
static void live_reset(void)                       /* (Dah) LIVE: PLAY and STOP: its volumes back, the MUTEs asked done */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        live_cut[i] = 0;
        if (live_mreq[i]) {
            live_mreq[i] = 0;
            trk[i].p[P_MUTE] = (int16_t)!trk[i].p[P_MUTE];
        }
    }
}
static void seq_start(void)
{
    uint32_t i;
    live_reset();
    motion_begin();
    chain_start();
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->rat_left = 0;
        t->nd_late = t->nd_early = ND_NONE;
        t->nd_fired = 0;
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    chain_seek();                                  /* (Dah: a song from its GRID: into its section) */
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    clk_pos = CLK_START;                           /* the metronome's first beat with step 0 */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

/* (Dah) a step's note on / off: through the track's ARP when it runs (the sequence feeds it as the keys do); not
 * while it records (rec_on): the ARP then plays the keys only, or it would record over the steps feeding it what it
 * plays of them. seq_arp: the track's step notes went into its ARP (their note-off goes the same way) */
static uint8_t seq_arp __attribute__((section(".pool")));
static __attribute__((noinline)) uint32_t seq_chord_slide(track_t *t, uint8_t *nn, uint32_t m, uint32_t mute);
static void seq_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t b = 1u << (trk_index(t) & 7u);
    if (t->p[P_AMODE] && !rec_on(t)) {
        seq_arp |= (uint8_t)b;
        arp_add(t, note);
    } else {
        seq_arp &= (uint8_t)~b;
        trk_note_on(t, note, vel);
    }
}
static void seq_note_off(track_t *t, uint32_t note)
{
    if ((seq_arp >> (trk_index(t) & 7u)) & 1u)
        arp_remove(t, note);
    else
        trk_note_off(t, note);
}
static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        seq_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->seq_keep = 0;
    t->tie_over = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}
/* (overlaps) the gate of the step playing is over (seq_tick) with notes of it going on into the next step (seq_keep's
 * bits 0..3): the others end, those that went on into this one (bits 4..7) half a step later (as a TIE's notes); what
 * goes on stays till the next step. Not inlined: off the audio ISR's loop, only with a note going on */
static __attribute__((noinline)) void seq_gate_end(track_t *t)
{
    uint32_t i, k = 0, keep = t->seq_keep & 15u, late = (uint32_t)(t->seq_keep >> 4) & ~keep, nk = 0;
    for (i = 0; i < t->seq_n; i++) {
        uint32_t b = 1u << i;
        if (!((keep | late) & b)) {
            seq_note_off(t, t->seq_notes[i]);
            continue;
        }
        nk |= (keep & b ? 1u : 0u) << k;
        t->seq_notes[k++] = t->seq_notes[i];
    }
    t->seq_n = (uint8_t)k;
    t->seq_keep = (uint8_t)nk;                      /* (the late ones end at the next end: half a step on) */
    t->seq_off = late ? div_samples((uint32_t)t->p[P_SDIV]) / 2u : 0xFFFFFFFFu;
}

static void seq_stop(void)
{
    uint32_t i;
    song.playing = 0;
    live_reset();
    cin_left = 0;                                  /* (a count-in: cancelled, nothing recorded) */
    cin_flush = 0;
    memset(cin_n, 0, sizeof cin_n);
    if (song.g[G_CLOCK])                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        trk[i].rat_left = 0;
        trk[i].nd_late = trk[i].nd_early = ND_NONE;   /* (a nudged step not played yet: not after the stop) */
        trk[i].nd_fired = 0;
    }
    chain_stop();
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here); SEQ_REP: a RATCH repeat (seq_ratchet).
 * RATCH x2..x4 (core.h step_ratchet): a NOTE step plays all of it again in equal parts of its swung length, each
 * part retriggered with GATE of the part; the chance is rolled once for the whole step (a failed roll: no part),
 * a slide into it glides into the first part only, and it never slides or ties out (its last part ends at its
 * gate). x1, what every older pattern holds, is the step exactly as before. */
/* 1.5.5 (#209): a step's CONDITION (core.h step_cond). a:b plays on loop a of every b of its track: cond_loop counts the
 * passes through the track's LEN, 0 the first (seq_tick: each wrap to step 1 adds one, modulo COND_LOOPS, a multiple
 * of 2, 3 and 4). It starts at 0 on PLAY (and RESTART) and on every SONG section (each section's pattern from its
 * first loop: 4:4 plays on a section's 4th repeat); a pattern loaded or LEN changed while playing keeps counting. Each
 * track counts its own LEN. A step played early by its NUDGE (step 1 at the end of the loop before) is the next
 * loop's. FILL / !FILL: seq_fill, set by the UI while GLO + E4 is held (ui_layer.c). Read where the chance is rolled
 * (seq_play): a step whose condition fails plays as a failed roll (nothing, none of its locks). Every step with no
 * condition (every older pattern) plays as before, its roll untouched */
#define COND_LOOPS 12u
static volatile uint8_t seq_fill;
static const uint8_t COND_A[9] = {1, 2, 1, 2, 3, 1, 2, 3, 4}, COND_B[9] = {2, 2, 3, 3, 3, 4, 4, 4, 4};
static __attribute__((noinline)) int seq_cond_miss(const track_t *t, const step_t *s, uint32_t idx)
{
    uint32_t c = step_cond(s), loop;
    if (c >= COND_FILL)
        return (c == COND_FILL) != (seq_fill != 0u);
    if (!c)
        return 0;
    loop = t->cond_loop + (idx < t->seq_idx);       /* (an early play of step 1: the next loop's) */
    return loop % COND_B[c - 1u] != COND_A[c - 1u] - 1u;
}
/* the step does not play: its condition fails, or its chance's roll */
static int seq_miss(const track_t *t, const step_t *s, uint32_t idx)
{
    if (s->probability >= COND_FIRST)
        return seq_cond_miss(t, s, idx);
    return step_chance(s) < 100u && rng() % 100u >= step_chance(s);
}
#define SEQ_REP (1u << 16)
#define SEQ_ROLLED (1u << 17)   /* seq_tick rolled the chance already (before the step's parameter locks): it passed */
#define SEQ_MISS (1u << 18)     /* .. it failed: nothing plays */
/* (Dah) the step after idx is a note step of the notes t holds now (all of them, no more): the step after the loop's
 * end is its first, or at a SONG's section's end the next section's first (its pattern ready: chain_poll). A held
 * note tied up to it then goes on (no release, no new attack: a drone, a pad over the loop or the next section) */
static const step_t *seq_next_step(const track_t *t, uint32_t idx, uint32_t len)   /* (Dah) the step after idx (the
                                                         * loop's first, or the next section's), 0 none known */
{
    uint32_t k = (uint32_t)(t - trk) % NTRK;
    if (idx + 1u >= len && chain.running && chain.remaining <= 1u) {   /* (the section's end: its next's step 1) */
        uint32_t b = chain.cur ^ 1u, r = chain.row + 1u < chain.config.count ? chain.row + 1u : 0u;
        if (!chain.src_ok[b] || chain.src_row[b] != r || chain.config.row[r].slot[k] == CHAIN_SILENT)
            return 0;
        return &chain.source[b].step[k][0];
    }
    return seq_at(t, (idx + 1u) % len);
}
static __attribute__((noinline)) int seq_same_next(const track_t *t, uint32_t idx, uint32_t len)
{
    const step_t *nx = seq_next_step(t, idx, len);
    uint32_t i, j;
    if (!nx || st_kind(nx) != ST_NOTE || nx->hit || nx->n != t->seq_n || step_ratchet(nx) > 1u || step_chance(nx) < 100u)
        return 0;
    for (i = 0; i < nx->n; i++) {
        for (j = 0; j < t->seq_n && t->seq_notes[j] != nx->note[i]; j++)
            ;
        if (j == t->seq_n)
            return 0;
    }
    return 1;
}
/* (overlaps) the notes sounding (seq_notes, the first four) that the step after, nx, says go on (core.h st_cont: its
 * notes as they play, QNT SEQ's snap): bit k, seq_notes[k]. Not inlined: once a step, only before such a step */
static __attribute__((noinline)) uint32_t seq_goes_on(const track_t *t, const step_t *nx, uint32_t qseq, int32_t qtrn)
{
    uint32_t c = st_cont(nx), i, j, m = 0, x;
    for (i = 0; i < nx->n && i < 4u; i++) {
        if (!((c >> i) & 1u))
            continue;
        x = nx->note[i];
        if (qseq)
            x = (uint32_t)clamp(scale_snap(t, (int32_t)x + qtrn), 0, 127);
        for (j = 0; j < t->seq_n && j < 4u; j++)
            if (t->seq_notes[j] == x)
                m |= 1u << j;
    }
    return m;
}
static uint32_t seq_fine(const track_t *t, uint32_t idx, uint32_t period, uint32_t gate);
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t idx, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n && !(skip & SEQ_REP);
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = st_kind(seq_at(t, (idx + 1u) % len)) == ST_TIE;
    uint32_t hits = step_ratchet(s);
    uint8_t nn[4 + NLANE], vv[4 + NLANE];           /* the notes it plays: its notes, then its hits */
    uint32_t m = 0, sk = 0, sl, over, cm = skip & SEQ_REP ? 0u : st_cont(s), kin = 0, nz = 0, late = 0, fo;
    const step_t *nx;
    /* QNT SEQ: the step's notes (not the lane hits) snap to the scale as they play; never on a drum kit, the slices
     * or another engine that maps the keys itself. SEQ+T (#218): moved by TRN first, as the keys are. seq_notes keeps
     * the notes that sound: their note-offs match */
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t qseq = t->p[P_QUANT] >= QN_SEQ && !(e->keys && e->keys(t, 0) >= 0);
    int32_t qtrn = qseq && t->p[P_QUANT] == QN_SEQT ? t->p[P_TRANS] : 0;
    if (!(skip & SEQ_REP)) {
        t->rat_left = 0;
        if ((skip & SEQ_MISS) || (!(skip & SEQ_ROLLED) && seq_miss(t, s, idx))) {
            seq_release(t);
            return;
        }
    }
    if (st_kind(s) == ST_TIE) {
        if (t->seq_n) {
            uint32_t fine = next_tie ? 0u : seq_fine(t, idx, period, gate + period / 2u);
            t->seq_off = gate + period / 2u;
            nx = seq_next_step(t, idx, len);
            if (fine) {                             /* FINE LEN: what ends here ends at its tenths (no hold on, no
                                                     * slide out); what the step after says goes on, goes on */
                t->seq_off = fine;
                t->seq_hold = t->tie_over = 0;
                t->seq_keep = (uint8_t)(nx && st_cont(nx) ? seq_goes_on(t, nx, qseq, qtrn) : 0u);
                return;
            }
            if (nx && st_cont(nx) && !(s->flags & SF_SLIDE) && !next_tie) {   /* (overlaps) into a step that says
                                                         * which go on: those, the others end at the gate */
                t->seq_hold = t->tie_over = 0;
                t->seq_keep = (uint8_t)seq_goes_on(t, nx, qseq, qtrn);
                return;
            }
            uint32_t into = nx && st_kind(nx) == ST_NOTE && nx->n && !nx->hit;   /* (Dah) a note comes next */
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie ||   /* chains hold at any GATE / swing; (Dah) */
                          seq_same_next(t, idx, len) || into;   /* into the same notes (the loop's, the next section's):
                                                         * on; into others: held till they start (no gap) */
            t->tie_over = (uint8_t)(!(s->flags & SF_SLIDE) && !next_tie && into);
            t->seq_keep = 0;
        }
        return;
    }
    if (st_kind(s) == ST_REST || (!s->n && !s->hit)) {
        seq_release(t);
        return;
    }
    if (hits > 1u) {                                /* RATCH: the gate of a part, no slide or tie out */
        gate = step_samples(t, period, idx) / hits * (uint32_t)t->p[P_SGATE] / 128u;
        if (!(skip & SEQ_REP))
            t->rat_left = (uint8_t)(hits - 1u);
    }
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t x = s->note[i];
        if (qseq) {                                 /* QNT SEQ: onto the scale now; two notes snapping */
            x = (uint32_t)clamp(scale_snap(t, (int32_t)x + qtrn), 0, 127);   /* together play once */
            for (j = 0; j < m && nn[j] != x; j++)
                ;
            if (j < m)
                continue;
        }
        nn[m] = (uint8_t)x;
        vv[m] = (uint8_t)vel;
        sk |= ((skip >> i) & 1u) << m;
        if ((cm >> i) & 1u) {                       /* (overlaps) a note going on: sounding, it is not struck again;
                                                     * not sounding (cut before, the keys' own), it stays silent */
            for (j = 0; j < t->seq_n && t->seq_notes[j] != x; j++)
                ;
            if (j < t->seq_n)
                kin |= 1u << m;
            else
                nz |= 1u << m;
        }
        m++;
    }
    sk |= nz;
    for (i = 0; i < NLANE; i++)
        if ((s->hit >> i) & 1u) {
            nn[m] = DRUM_LANE_NOTE[i];
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel);
            sk |= ((skip >> (8u + i)) & 1u) << m;
            m++;
        }
    over = slide_in && t->tie_over;                 /* (Dah) held over, no SLIDE: the new notes take over, no glide */
    t->tie_over = 0;
    t->slide_glide = (uint8_t)(slide_in && !over);
    sl = slide_in && !over && !t->p[P_AMODE] && trk_vmode(t) == V_POLY ? seq_chord_slide(t, nn, m, nz) : 0u;   /* (Dah) a
                                                     * chord slid into: its voices glide over, not started again */
    if (!slide_in) {
        if (kin)                                    /* (overlaps) what does not go on ends */
            for (i = 0; i < t->seq_n; i++) {
                for (j = 0; j < m && (nn[j] != t->seq_notes[i] || !((kin >> j) & 1u)); j++)
                    ;
                if (j == m)
                    seq_note_off(t, t->seq_notes[i]);
            }
        else
            seq_release(t);
    } else if (!over)                                 /* (Dah) held over: the notes sounding already go on, no attack
                                                     * (a TIE into another step: they strike again, the gap only gone) */
        for (i = 0; i < m; i++)
            for (j = 0; j < t->seq_n; j++)
                if (nn[i] == t->seq_notes[j] && trk_vmode(t) == V_POLY && !t->p[P_AMODE])
                    sl |= 1u << i;
    sl |= kin;
    for (i = 0; i < m; i++)
        if (!(((sk | sl) >> i) & 1u))
            seq_note_on(t, nn[i], vv[i]);
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < m && nn[j] != t->seq_notes[i]; j++)
                ;
            if (j == m)
                seq_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < m; i++)
        if (!((sk >> i) & 1u)) {
            late |= ((kin >> i) & 1u) << t->seq_n;
            t->seq_notes[t->seq_n++] = nn[i];
        }
    t->seq_off = gate;
    t->seq_hold = hits == 1u && ((s->flags & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
    if (hits == 1u && !next_tie && !(skip & SEQ_REP) && (fo = seq_fine(t, idx, period, gate)) != 0u) {
        t->seq_off = fo;                            /* FINE LEN: its notes ending here (those that went on into it too)
                                                     * at its tenths, from where it plays (as the GATE); no slide out */
        t->seq_hold = 0;
        late = 0;
    }
    nx = !t->seq_hold && hits == 1u ? seq_next_step(t, idx, len) : 0;   /* (overlaps) the next step's: they go on */
    t->seq_keep = (uint8_t)((nx && st_cont(nx) ? seq_goes_on(t, nx, qseq, qtrn) : 0u) | (hits == 1u ? late & 15u : 0u) << 4);
}

/* (Dah) a POLY chord slid into (SLIDE on the step before): the old notes and the new ones each low to high, in pairs,
 * the voice of an old note moved to its new one, gliding (GLIDE, legato: no new attack); the bit of each new note so
 * played (the caller starts the others; the old notes left over are let go after: not among the new) */
static __attribute__((noinline)) uint32_t seq_chord_slide(track_t *t, uint8_t *nn, uint32_t m, uint32_t mute)
{
    uint8_t o[4], nw[4], oi[4], ni[4];
    uint32_t no = 0, nm = 0, i, j, done = 0;
    for (i = 0; i < t->seq_n && no < 4u; i++) {     /* the old ones not in the new chord */
        for (j = 0; j < m && nn[j] != t->seq_notes[i]; j++)
            ;
        if (j == m)
            o[no] = t->seq_notes[i], oi[no++] = (uint8_t)i;
    }
    for (j = 0; j < m && nm < 4u; j++) {            /* the new ones not sounding (overlaps: not a silent one going on) */
        for (i = 0; i < t->seq_n && nn[j] != t->seq_notes[i]; i++)
            ;
        if (i == t->seq_n && !((mute >> j) & 1u))
            nw[nm] = nn[j], ni[nm++] = (uint8_t)j;
    }
    for (i = 1; i < no; i++)                        /* low to high, both */
        for (j = i; j > 0 && o[j - 1u] > o[j]; j--) {
            uint8_t a = o[j]; o[j] = o[j - 1u]; o[j - 1u] = a;
            a = oi[j]; oi[j] = oi[j - 1u]; oi[j - 1u] = a;
        }
    for (i = 1; i < nm; i++)
        for (j = i; j > 0 && nw[j - 1u] > nw[j]; j--) {
            uint8_t a = nw[j]; nw[j] = nw[j - 1u]; nw[j - 1u] = a;
            a = ni[j]; ni[j] = ni[j - 1u]; ni[j - 1u] = a;
        }
    for (i = 0; i < no; i++) {                      /* (Dah) each old note to the nearest new one 2 semitones at most
                                                     * from it: that voice glides; a bigger leap starts again */
        uint32_t v, b = nm, j2;
        int32_t bd = 3;
        for (j2 = 0; j2 < nm; j2++) {
            int32_t dd = (int32_t)nw[j2] - (int32_t)o[i];
            dd = dd < 0 ? -dd : dd;
            if (!((done >> ni[j2]) & 1u) && dd < bd)
                bd = dd, b = j2;
        }
        if (b == nm)
            continue;
        for (v = 0; v < NVOICE; v++) {
            voice_t *vo = &t->v[v];
            if (vo->active && vo->gate && vo->note == o[i]) {
                vo->note = nw[b];
                vo->pitch16 = (int32_t)nw[b] * 16;
                {                                   /* (~0.3 s for any interval; GLIDE TIME: its own) */
                    int32_t d = vo->pitch16 - vo->pitch_cur;
                    glide_set(t, vo, 1);
                    if (!t->p[P_GLMODE] && vo->pitch_cur != vo->pitch16)
                        vo->gstep = (d < 0 ? -d : d) / 400 > 0 ? (d < 0 ? -d : d) / 400 : 1;
                }
                t->seq_notes[oi[i]] = nw[b];        /* (not let go after: it is the new note now) */
                done |= 1u << ni[b];
                break;
            }
        }
    }
    return done;
}

/* MENU > CONTROL > STEP PREVIEW (1.2, Discussion #169): the UI asks for a step of a track to sound once (aud_req: step
 * << 8 | track + 1; ui_input.c cursor_move); with the transport stopped it plays here as the sequencer would play it,
 * through the track's sound: its notes or hits, accent, QNT SEQ (not its chance, ratchet repeats or locks), for a 1/16
 * at the track's GATE (seq_tick lets it go, stopped too). A SLIDE or TIE after it does not hold it. Nothing is recorded:
 * the step is only read */
static volatile uint16_t aud_req;
static step_t aud_far;                              /* (long patterns) the step asked for, from NSTEP on: the UI's copy */
/* (Dah) PRESETS turned, stopped: the new sound heard, a note on the selected track (pv_req: track + 1) for PV_MS, the
 * last one let go first (a quick turn: each sound a moment, the last one whole); C4 (a DRUM kit: its kick). A note or a
 * hit placed on SEQ (pv_want: its note, pv_wvel its velocity, then pv_req; ui_input.c place_heard): that note, stopped
 * or playing (over the music, as a key played), let go at the track's GATE of a 1/16 (a fast turn: one at a time) */
#define PV_MS 350u
static volatile uint8_t pv_req, pv_want, pv_wvel;
static uint8_t pv_on, pv_note;
static uint32_t pv_ms, pv_len;
static __attribute__((noinline)) void seq_preview(void)
{
    uint32_t i = pv_req, w = pv_want;
    if (pv_on && (i || fm1_ms - pv_ms > pv_len)) {
        trk_note_off(&trk[(pv_on - 1u) % NTRK], pv_note);
        pv_on = 0;
    }
    if (!i)
        return;
    pv_req = 0;
    pv_want = 0;
    if ((song.playing && !w) || cin_left || --i >= NTRK)
        return;
    pv_note = (uint8_t)(w ? w : trk[i].eng_req % NENGINES == ENGI_DRUM ? DRUM_LANE_NOTE[0] : 60u);
    trk_note_on(&trk[i], pv_note, w ? pv_wvel : 100u);
    pv_len = w ? div_samples(2) * (uint32_t)trk[i].p[P_SGATE] / 128u / (FS / 1000u) : PV_MS;
    pv_on = (uint8_t)(i + 1u);
    pv_ms = fm1_ms;
}
static __attribute__((noinline)) void seq_audition(void)
{
    uint32_t r = aud_req, i = (r & 0xFFu) - 1u, idx = r >> 8;
    track_t *t;
    uint16_t at;
    aud_req = 0;
    if (song.playing || cin_left || i >= NTRK || idx >= LEN_MAX)
        return;
    t = &trk[i];
    at = t->seq_idx;
    t->seq_idx = (uint16_t)idx;
    seq_step(t, idx < NSTEP ? &t->step[idx] : &aud_far, idx, div_samples(2), SEQ_ROLLED);
    t->seq_idx = at;
    t->seq_hold = t->seq_keep = 0;
    t->rat_left = 0;
}

/* RATCH: the next part of the playing step once the sequencer is that far into it (seq_step played the first).
 * The step is read again: an edit to fewer parts (or another source in a song) ends the repeats */
static __attribute__((noinline)) void seq_ratchet(track_t *t, uint32_t period)
{
    const step_t *s = seq_at(t, t->seq_idx);
    uint32_t hits = step_ratchet(s);
    if (t->rat_left >= hits) {
        t->rat_left = 0;
        return;
    }
    if (t->seq_pos < (hits - t->rat_left) * (step_samples(t, period, t->seq_idx) / hits))
        return;
    t->rat_left--;
    seq_step(t, s, t->seq_idx, period, SEQ_REP);
}

/* the nudge of step s in 1/NUDGE_DIV of the step (NUDGE, core.h) when it plays off its start: the track's QUANTIZE OFF,
 * a NOTE step that plays something, x1 (a ratcheted step's parts divide the step: on its start); else 0 */
static int32_t seq_nudge(const track_t *t, const step_t *s)
{
    if (t->p[P_SQNT] == 1 || st_kind(s) != ST_NOTE || (!s->n && !s->hit) || step_ratchet(s) != 1u)
        return 0;
    if (t->p[P_SQNT] >= 2)                              /* 25 / 50 / 75 %: that much nearer the grid */
        return step_nudge(s) * (5 - t->p[P_SQNT]) / 4;
    return step_nudge(s);
}

/* the automation a track plays: its slot's while a song plays (song_chain.c lanes; 0: a silent lane, none) */
static const motion_store_t *chain_motion(const track_t *t)
{
    uint32_t s = chain.lane[trk_index(t)];
    return !chain.running ? ((seq_off >> trk_index(t)) & 1u ? 0 : &motion) : s != CHAIN_SILENT ? &chain.source[chain.cur].motion : 0;
}

/* FINE LEN (core.h MOTION_FINE): the note-off, in samples from where step idx plays, of the notes ending there when
 * it has tenths: that much of its (swung) length; 0 none (the GATE's, gate, as before: every older pattern). QUANTIZE
 * (P_SQNT) as for the NUDGE: ON the tenths not heard (whole steps, the GATE as ever), 25 / 50 / 75 % that much of the
 * way to the GATE's off, OFF as set. Not inlined: once a step, off the audio ISR's loop */
static __attribute__((noinline)) uint32_t seq_fine(const track_t *t, uint32_t idx, uint32_t period, uint32_t gate)
{
    const motion_store_t *m;
    uint32_t f, x;
    if (t->p[P_SQNT] == 1 || !(m = chain_motion(t)) || !(f = fine_at(m, trk_index(t), idx)))
        return 0;
    x = step_samples(t, period, idx) * f / 10u;
    if (t->p[P_SQNT] >= 2)
        x = (uint32_t)((int32_t)x + ((int32_t)gate - (int32_t)x) * (t->p[P_SQNT] - 1) / 4);
    return x ? x : 1u;
}

/* step idx plays: its chance (the one roll, as seq_step made it: a step that does not play applies no lock), the
 * automation and the locks before the notes (so a note-on reads them: eng_drum's KIT, ..), the notes live recording
 * already sounds left out (rskip), then its notes. On its start, or (NUDGE) at its start + its nudge */
static __attribute__((noinline)) void seq_play(track_t *t, uint32_t idx, uint32_t period)
{
    const step_t *s = seq_at(t, idx);
    const motion_store_t *m = chain_motion(t);
    uint32_t skip = SEQ_ROLLED, i, k;
    if (seq_miss(t, s, idx))
        skip = SEQ_MISS;
    if (m)
        motion_step(t, idx, m, skip == SEQ_ROLLED);
    if (t->rskip_n && t->rskip_idx == idx) {
        for (k = 0; k < t->rskip_n; k++) {
            for (i = 0; i < s->n; i++)
                if (s->note[i] == t->rskip[k])
                    skip |= 1u << i;
            for (i = 0; i < NLANE; i++)
                if (((s->hit >> i) & 1u) && DRUM_LANE_NOTE[i] == t->rskip[k])
                    skip |= 1u << (8u + i);
        }
        t->rskip_n = 0;
    }
    seq_step(t, s, idx, period, skip);
}

/* NUDGE (1.2, #162; QUANTIZE OFF): a step plays at its start + nudge x DIV / 16. When a step starts its own late play
 * (nd_late) and the next step's early one (nd_early: that much before the end of this one) are latched; an early
 * play marks the step (nd_fired), whose start then plays nothing. A late play falls before the next start (at most
 * 7/16 of the step, the shortest swung step is 0.6 of it); an early one never before the late one of the step before.
 * Not across the end of a song row's pattern (the next row's pattern is not there yet): such a step plays on its
 * start. QUANTIZE ON (the default), or every nudge 0 (every older pattern): each step plays on its start, as before.
 * seq_nd_due: before step seq_idx's end is checked, the latched plays due by now (seq_pos): this step's late one, then
 * the next step's early one (not inlined: off the audio ISR's loop, and only called with one latched) */
static __attribute__((noinline)) void seq_nd_due(track_t *t, uint32_t period, uint32_t len, uint32_t cur_len, uint32_t n)
{
    if (t->nd_late != ND_NONE && (t->seq_pos >= t->nd_late - 1u || t->seq_pos >= cur_len)) {
        t->nd_late = ND_NONE;                       /* (also when a shorter DIV / LEN left it past the end) */
        seq_play(t, t->seq_idx, period);
    }
    if (t->nd_early != ND_NONE && t->seq_pos >= t->nd_early - 1u && t->seq_pos != 0x7FFFFFFFu + n) {
        uint32_t nx = (t->seq_idx + 1u) % len;
        t->nd_early = ND_NONE;
        t->nd_fired = (uint16_t)(nx + 1u);
        seq_play(t, nx, period);
    }
}

/* NUDGE: step seq_idx has started: it plays now (or played early: nd_fired), or its late play is latched; and the
 * next step's early play (not inlined, as seq_nd_due) */
static __attribute__((noinline)) void seq_nd_start(track_t *t, uint32_t period, uint32_t len)
{
    uint32_t nx = (t->seq_idx + 1u) % len;
    int32_t d;
    t->nd_early = ND_NONE;
    if (t->nd_fired == t->seq_idx + 1u) {
        ;                                           /* (it played early) */
    } else {                                        /* late by its NUDGE */
        d = seq_nudge(t, seq_at(t, t->seq_idx));
        if (d > 0)
            t->nd_late = (uint32_t)d * period / NUDGE_DIV + 1u;
        else
            seq_play(t, t->seq_idx, period);
    }
    t->nd_fired = 0;
    if ((!chain.running || nx) && (d = seq_nudge(t, seq_at(t, nx))) < 0) {
        uint32_t at = step_samples(t, period, t->seq_idx), back = (uint32_t)-d * period / NUDGE_DIV;
        at = (at > back ? at - back : 0u) + 1u;
        t->nd_early = at < t->nd_late ? t->nd_late : at;   /* (never before this step's own late play) */
    }
}

/* step seq_idx starts (seq_tick; top: PLAY's or a SONG section's first): a loop's first counts it (seq_cond_miss), a
 * recorded note held ties into it (not inlined: once a step, off the audio ISR's loop) */
static __attribute__((noinline)) void seq_enter(track_t *t, uint32_t top, uint32_t len)
{
    uint32_t i = trk_index(t) % NTRK;
    uint8_t q[4];
    if (!t->seq_idx) {
        t->cond_loop = (uint8_t)(top || t->cond_loop + 1u >= COND_LOOPS ? 0u : t->cond_loop + 1u);
        rp[i].pass = (uint8_t)(top ? 0u : rp[i].pass + 1u);   /* (Dah: REC MODE REPLACE's passes) */
        if (top)
            rp[i].took = 0xFF;                       /* (the pass from PLAY takes again too) */
    }
    if (loop_cue && i == song.sel && ((song.rec >> i) & 1u) && !chain.running) {   /* (Dah) REC's LOOP CUE: the last
                                                     * three beats of the selected track's pattern a blip each, going
                                                     * down, its step 1 again a ping (sound only, at CLICK LEVEL) */
        uint32_t q = click_beat_len() / div_samples((uint32_t)t->p[P_SDIV]), r = len - t->seq_idx;
        q = q ? q : 1u;
        if (!t->seq_idx)
            click_req = top ? click_req : 6u;
        else if (!(r % q) && r / q <= 3u)
            click_req = (uint8_t)(6u - r / q);
    }
    if (top)
        rp[i].pvn = 0;                              /* (PLAY: nothing before step 1) */
    rec_hold(t, t->seq_idx, len);
    memcpy(q, rp[i].pv, 4);
    rp[i].pvn = (uint8_t)pat_after(seq_at(t, t->seq_idx), q, rp[i].pvn, rp[i].pv);   /* (overlaps: what it sounds) */
}

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period, len, top;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off > n)
            t->seq_off -= n;
        else if (t->seq_keep)
            seq_gate_end(t);                        /* (overlaps) some go on */
        else
            seq_release(t);
    }
    if (!song.playing)
        return;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    len = len ? len : 1u;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->nd_late | t->nd_early)
            seq_nd_due(t, period, len, cur_len, n);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        top = t->seq_pos >= 0x7FFFFFFFu;            /* (PLAY, a SONG section: its first step) */
        t->seq_pos = top ? (chain.running ? chain.carry : 0u) : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % len);
        if (!t->seq_idx) {                          /* (Dah) the pattern's start: the ARP again from its first note, on
                                                     * it (a RATE, MODE or pattern changed since stays in time) */
            t->arp_pos = 0xFFFFFFF;
            t->arp_idx = 0xFFFFFFFFu;
            t->arp_walk = 0;
            if (live_mreq[(uint32_t)(t - trk) % NTRK]) {   /* (Dah) LIVE: a MUTE asked for, on the pattern's start */
                live_mreq[(uint32_t)(t - trk) % NTRK] = 0;
                t->p[P_MUTE] = (int16_t)!t->p[P_MUTE];
            }
        }
        seq_enter(t, top, len);
        if (t->p[P_SQNT] == 1 && !t->nd_fired)
            seq_play(t, t->seq_idx, period);        /* QUANTIZE ON: every step on its start, as before 1.2 */
        else
            seq_nd_start(t, period, len);
    }
    if (mramp[(uint32_t)(t - trk) % NTRK].n)          /* (automation in lines, motion.c) */
        motion_ramp_block(t, t->seq_pos, step_samples(t, period, t->seq_idx));
    if (t->rat_left)
        seq_ratchet(t, period);
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-4 (0) channels 1..4 their parts (5..16 never get
 * here: midi_event drops them), SEL (1) every channel the selected track; CH5-8 .. CH13-16 (2..4) as CH1-4 on
 * their four channels (midi_base); TRACK (5, 1.5.5) the first track set to the channel, else the first set to OMNI, else
 * none (dropped as a channel outside a block) */
static uint8_t midi_route;                 /* the G_ROUTE events_block last saw (a change to a block: midi_route_block;
                                            * MIDI_ROUTE_AGAIN: TRACK's channels changed, the same again) */
#define MIDI_ROUTE_AGAIN 0xFFu
static int32_t midi_slot_any(uint32_t ch);
static int32_t midi_slot(uint32_t ch)      /* the track channel ch plays, -1 none (and with SEL; Dah: none past the
                                            * project's tracks) */
{
    int32_t k = midi_slot_any(ch);
    return k >= (int32_t)trk_count() ? -1 : k;
}
static int32_t midi_slot_any(uint32_t ch)
{
    int32_t b = midi_base(), omni = -1;
    uint32_t i;
    if (b != -2)
        return b >= 0 && ch - (uint32_t)b < (b ? 4u : NPART) ? (int32_t)(ch - (uint32_t)b) : -1;   /* (CH1-8
                                                * eight channels, the other blocks four: tracks 1..4) */
    for (i = 0; i < NPART; i++) {
        uint32_t c = mch_get(i);
        if (c == ch)
            return (int32_t)i;
        if (c == MCH_OMNI && omni < 0)
            omni = (int32_t)i;
    }
    return omni;
}
static int midi_in_block(uint32_t ch) { return midi_slot(ch) >= 0; }   /* ch is heard by a track (never with SEL) */
static track_t *midi_track(uint32_t ch)
{
    int32_t k = midi_slot(ch);
    return k >= 0 ? &trk[k] : TSEL;
}

#include "midi_control.c"
#include "midi_clock.c"

/* the count-in: its first beat now (accented) */
static __attribute__((noinline)) void cin_begin(void)
{
    cin_total = cin_left = (uint8_t)(4u * cin_bars);
    cin_pos = 0;
    memset(cin_n, 0, sizeof cin_n);
    click_req = 2;
}
/* n samples on: a beat ends; the last one starts the sequencer (in this block, before seq_tick: step 0 with it) */
static __attribute__((noinline)) void cin_block(uint32_t n)
{
    uint32_t b = click_beat_len();
    cin_pos += n;
    if (cin_pos < b)
        return;
    cin_pos -= b;
    cin_pos = cin_pos < b ? cin_pos : 0u;
    if (--cin_left) {
        click_req = (uint8_t)((cin_total - cin_left) % 4u ? 1u : 2u);
        return;
    }
    seq_start();
    cin_flush = 1;
}
/* the count-in's kept notes onto step 1, which has just started (events_block, after seq_tick): as if played now */
static __attribute__((noinline)) void cin_record(void)
{
    uint32_t i, k;
    cin_flush = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < cin_n[i]; k++)
            if (rec_on(t)) {
                rec_live = 1;
                rec_note(t, cin_note[i][k], cin_vel[i][k]);
                rec_live = 0;
                if ((cin_up[i] >> k) & 1u)
                    rec_release(t, cin_note[i][k]);
            }
        cin_n[i] = 0;
    }
}
/* the metronome's beat: adv samples on while the transport runs (click.c sounds it in this block) */
static void click_tick(uint32_t adv)
{
    uint32_t p = div_samples(2), beat = 0;
    if (clk_pos == CLK_START) {
        clk_pos = 0;
        clk_step = 0;
        beat = 2;
    } else {
        clk_pos += adv;
        while (clk_pos >= p) {                      /* (as seq_tick: the remainder kept) */
            clk_pos -= p;
            clk_step = (clk_step + 1u) & 15u;
            if (!(clk_step & 3u))
                beat = clk_step ? 1u : 2u;
        }
    }
    if (beat && click_wanted())
        click_req = (uint8_t)beat;
}
/* the beat of the bar the click is on (0..3: 0 the accented first), -1 stopped: the count-in's, else the playing
 * transport's (clk_step, internal or external clock). It moves in the block a beat clicks in (the header's metronome,
 * ui_draw.c, swings on it; CLICK OFF it moves all the same) */
static int32_t click_beat_now(void)
{
    if (cin_left)
        return (int32_t)((uint32_t)(cin_total - cin_left) % 4u);
    if (!song.playing)
        return -1;
    return clk_pos == CLK_START ? 0 : (int32_t)(clk_step >> 2);
}

/* everything that happens between two rendered blocks */
/* (Dah) each block before the parts: the tracks past the project's keep no voice (not rendered, they would never end
 * and hold the budget); the slicer's buffers taken and given back (slicer.c). Out of line: a few loops, once a block */
static __attribute__((noinline)) void parts_prepare(void)
{
    uint32_t i;
    for (i = trk_count() * NVOICE; i < NTRK * NVOICE; i++) {
        voice_t *v = &trk[i / NVOICE].v[i % NVOICE];
        if (v->active)
            v->active = v->gate = 0, v->stage = 0;
    }
    slicer_claim();
}
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    parts_prepare();
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (cin_left && !clock_mode)
        cin_block(n);
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else if (cin_left)
            ;                                       /* counting in already (the UI's PLAY stops it: seq_counting) */
        else if (cin_bars && song.rec && !song.playing && !chain.armed)
            cin_begin();                            /* recording from stop: the count-in first */
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    if (midi_route != (uint8_t)song.g[G_ROUTE]) {     /* ROUT changed: a block lets go of the other channels */
        midi_route = (uint8_t)song.g[G_ROUTE];
        if (midi_route != ROUTE_SEL)
            midi_route_block();
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        for (i = 0; i < NTRK; i++) {
            trk[i].rh_n = trk[i].rskip_n = 0;
            trk[i].seq_n = trk[i].seq_hold = trk[i].slide_glide = trk[i].rat_left = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            arp_forget(t);
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note)
                arp_silence(t);
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    if (aud_req)
        seq_audition();                               /* (STEP PREVIEW: a step sounds, stopped) */
    if (pv_req | pv_on)
        seq_preview();                                /* (Dah: a sound picked, heard) */
    chain_tick(seq_n);
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    if (cin_flush)
        cin_record();
    if (song.playing)
        click_tick(seq_n);
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], clock_mode ? seq_n : n, clock_mode && song.playing ? seq_n : n);
    beat_pos += clock_mode ? seq_n : n;               /* the beat the ARP LED flashes on (ui_leds) */
    if (beat_pos >= beat_samples()) {
        beat_pos -= beat_samples();
        beat_pos = beat_pos < beat_samples() ? beat_pos : 0u;
        beat_n = (beat_n + 1u) & 3u;
    }
    if (song.playing)
        song.tick++;
}
