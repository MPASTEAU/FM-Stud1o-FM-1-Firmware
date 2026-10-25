/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: a drum kit of 8 lanes (drum_voice.c, Felucca's own): KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM,
 * BELL. Before 1.0 it was PHYS's MODEL DRUM; projects and user presets saved then load as this engine
 * (drum_from_phys in core.h).
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other notes as
 * their octave of 36..47), so GM patterns and MIDI parts play as they did on SAMPLE PERC (retired: its sounds
 * load as this engine, core.h drum_from_perc).
 *
 * Parameters: KIT the kit: STD (Felucca's own: TOM RIM BELL on lanes 6..8, other GM notes their congas, claves,
 * cymbals) or a model kit (80 10 66 55 77, 1.5.6 DEEP TIGHT PERC: every lane that kit's own piece, drum_voice.c DV_KIT; KICK does not
 * apply). Its stored values 1..3 were HAND CYM H+CYM (STD with CONGA CLAVE / CYM on lanes 6..8) until 1.0.4: they
 * play 66 10 77 now (DK_PLAYS), and whatever stores them lands there (params.c enum_orig, param_fit: the knob
 * steps over them, the editor's SET, projects, user presets, motion). KICK the kick (PUNCH, ROUND). TUNE (64: as
 * designed, +-12 semitones), TONE, DECY and SNAP (each lane's extra: the kick's drive, the snare's snappiness,
 * the clap's spread, the hats' noise, the toms' bend, the rim's drive, the bell's strike) move every lane from its designed value. ACC
 * is the accent at full velocity (velocity scales it), DRV a soft clip on every hit (x1..x4, level kept).
 * Each lane has its LEVEL (P_LN0..P_LN7, EDIT > LANES / LANES 2, #97: square law, 100 % the kit as designed, the
 * default, so every older sound plays as it did), on the voice amplitude after the knee (a quieter lane is cleaner).
 * A hit keeps the drum and the variant it was struck with; the knobs move it while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (drum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST PIT / FLT / SHP, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the part's engine memory (engines.c eng_mem; drum_kit_of: the parameters, coefficients, voice and metal
 * source of each lane). */
#include "drum_voice.c"

/* 1.5.6 (Discussion #216): DEEP TIGHT PERC (9..11), kits put together from the five's pieces (drum_voice.c DV_KIT),
 * appended. Firmware before 1.5.6 clamps 9..11 to 8 (77) when it loads them (param_fit) */
enum { DK_STD, DK_HAND, DK_CYM, DK_HCYM, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP, DK_TIGHT, DK_PERC, DK_COUNT };   /* (stored values) */
/* the kit a stored KIT value plays: HAND CYM H+CYM (retired after 1.0.4) -> 66 (a conga on TOM), 10 (a cymbal on
 * BELL), 77 (claves on RIM, a cymbal on BELL) */
static const uint8_t DK_PLAYS[DK_COUNT] = {DK_STD, DK_66, DK_10, DK_77, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP,
                                           DK_TIGHT, DK_PERC};
static uint32_t drum_kit_plays(int32_t v) { return DK_PLAYS[clamp(v, 0, DK_COUNT - 1)]; }
/* the model kits' pieces where a lane plays another than its own (the lane's name): 1 TOM -> CONGA, 2 RIM -> CLAVE,
 * 4 BELL -> CYM */
static const uint8_t DK_SWAP[DV_NKIT] = {0, 4, 1, 0, 2 | 4, 4, 0, 1 | 2};
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};

typedef struct {
    dv_param_t key;              /* the parameters the coefficients were set up with */
    dv_coef_t c;
    dv_voice_t v;
    dv_metal_t mb;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
    uint8_t slot;                /* SYNTH: the layer set struck (SYN 1..5: 0..4) */
    uint8_t syn[DS_NF];          /* .. its values the coefficients were set up with */
    uint8_t pg;                  /* .. read at this patch generation (eng_fm6.c fm6_pgen) */
    int32_t last, off;           /* (Dah) its last sample; the step a hit over its own tail leaves, fading (no click) */
} drum_lane_t;

ENG_MEM_FITS("DRUM", sizeof(drum_lane_t) * DV_NLANE);
/* 1.4.1: each part's lanes muted (bit l: lane l), the EDIT layer's black keys 1..8 (ui_layer.c EQA_DRUM): a channel
 * mute. The lane plays on as always (hits, keys, MIDI: its voice, the CPU, the choke) and drum_render drops its output,
 * so unmuted it sounds as if it never was. Performance state: not saved, not recorded, no undo; a sound load into the
 * track (ui.c load_begin) and a project load (project_restore_runtime) clear it, power-off too */
static uint8_t drum_mute[NPART];

/* 1..3 named as the kit they play: aliases, never shown or offered (EDITOR_PROTOCOL.md: retired values) */
static const char *const N_DRUM_KIT[] = {"STD", "66", "10", "77", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC"};
static const char *const N_DRUM_KICK[] = {"PUNCH", "ROUND"};

/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*, or a model kit's lane: DV_KTYPE) and its semitones; KICK picks the kick; a model kit
 * plays its own piece on the note's lane */
static uint32_t drum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)DRUM_GM[n - 35u][0];
    uint32_t kit = drum_kit_plays(p[P_E0]);           /* STD, the model kits */
    *st = DRUM_GM[n - 35u][1];
    if (kit >= DK_80)
        return DV_KTYPE(kit - DK_80 + 1u, DV_TYPE_LANE[t]);
    return t == DVT_PUNCH && p[P_E6] > 0 ? DVT_ROUND : t;
}

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth plays the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a note strikes (KIT-proof: a kit plays its own piece inside the lane) */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* KIT's swaps of lanes 6..8 (1 TOM -> CONGA, 2 RIM -> CLAVE, 4 BELL -> CYM) */
static uint32_t drum_swaps(const track_t *t)
{
    uint32_t kit = drum_kit_plays(t->p[P_E0]);
    return kit >= DK_80 ? DK_SWAP[kit - DK_80] : 0u;
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CONGA";
    if (l == DV_RIM && (kit & 2u))
        return "CLAVE";
    return (kit & 4u) && l == DV_BELL ? "CYM" : N[l];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CG";
    if (l == DV_RIM && (kit & 2u))
        return "CL";
    return (kit & 4u) && l == DV_BELL ? "CY" : N[l];
}

/* ------------------------------------------------- the lanes designed (HA HA HE HU) --- */
/* EDIT > DESIGN / DESIGN 2 (ui_drumlane.c): each lane's own VOICE, TUNE, DECAY, TONE, SNAP and STACK, on top of the
 * kit and the EDIT knobs. They live in the track's patch (eng_fm6.c fm6_patch, what an FM6 track plays; a DRUM track
 * has no use for it), so projects, user presets (up_fm6.c), undo and the editor's backup carry them as they are:
 * field n = lane x DL_NF + f at byte n / 11 x 21 + n % 11 of the 155-byte voice (an operator's rates, levels and
 * breakpoints: bytes the FM6 formats keep whole up to 99), marked by the name DL_SIG. A patch without the mark: every
 * lane at its default (the kit as it was). Loading a factory DRUM sound clears them (eng_fm6.c drum_track_loaded).
 *   VOICE  0 KIT (the kit's own piece), 1..DVT_COUNT one of Felucca's own drums, then the model kits' pieces (kit by
 *          kit, lane by lane: "80 BD" .. "PERC CB"), then (phase 2) SYN 1..5: the layered voice (drum_voice.c
 *          DVT_SYNTH) playing that layer set (below; lanes may share one, each with its own TUNE DECAY TONE SNAP)
 *   TUNE   semitones, DL_DEF 24 = 0 (-24..+24, held in the drum's range)
 *   DECAY TONE SNAP  50 = as the kit and the EDIT knobs make it; each step 64 / 50 of the knob's (-64..+63)
 *   STACK  0 OFF, 1..8: a hit on this lane strikes that lane too (voice.c trk_note_on; one level, never itself)
 * The layer sets (phase 2, EDIT > SYN TONE .. SYN METAL, ui_drumlane.c): DS_NSET = 5 sets of DS_NF = 17 values in the
 * same voice, each value a whole byte inside the range the FM6 formats keep (fm6_max: what fm6_pack, fm6_unpack and
 * fm6_sanitize leave as it is), so nothing is packed and a set needs no format either. The bytes free after the
 * lanes' 48 (0..10 21..31 42..52 63..73 84..87) and the name (145..154, DL_SIG):
 *   up to 99   op 4's 88..94, op 5's 105..115, every operator's OL (16 + 21 k) and FF (19 + 21 k), the pitch
 *              envelope 126..133, the LFO's 137..140: 42 bytes
 *   smaller    per operator FC 0..31, DET 0..14, RS KVS 0..7, LC RC AMS 0..3, MODE 0..1; ALG 0..31 (134), TRNSP 0..48
 *              (144); FB LPMS 0..7, LFW 0..5, OKS LKS 0..1 (unused)
 * Set k (0..4) takes operator k's ten smaller bytes and OL FF, six of the pool's 30, and one of five for SWEEP (its 15
 * values: op 5's FC DET, ALG, TRNSP, 140): DS_AT. 8 of the 17 need more than 32 values (the levels, PITCH CUT BEND, the
 * noise's and the metal's decays) and 40 of the 42 bytes go to them; so five sets, not eight (an open and a closed
 * hat share one: their lanes' DECAY apart). Marked by 139 = 83 and 119 (op 5's AMS) = 2: a patch with lanes of before
 * (its bytes there whatever the FM6 voice had) reads SYN 1..5 as DV_SYN_DEF until the first write (eng_fm6.c dl_set)
 * puts them in. Projects, user presets, undo and the editor's backup carry the sets with the lanes, as they are. */
enum { DL_VOICE, DL_TUNE, DL_DECAY, DL_TONE, DL_SNAP, DL_STACK, DL_NF };
#define DL_NVOICE (1u + DVT_COUNT + DV_NKIT * DV_NLANE)   /* (VOICE values before SYN 1..5) */
#define DL_NAME 145u                     /* the voice's name (fm6_core.c FP_NAME) */
static const uint8_t DL_DEF[DL_NF] = {0, 24, 50, 50, 50, 0};
static const uint8_t DL_MAX[DL_NF] = {DL_NVOICE + DS_NSET - 1u, 48, 99, 99, 99, DV_NLANE};
static const char DL_SIG[10] = {'D', 'R', 'U', 'M', ' ', 'L', 'A', 'N', 'E', 'S'};
static uint8_t fm6_patch[NTRK][155u + 1u];   /* (eng_fm6.c: the tracks' patches, FP_SIZE + 1; defined there) */
static volatile uint8_t fm6_pgen[NTRK];      /* (eng_fm6.c: +1 after each write of fm6_patch[t]; defined there) */
/* set k's value f: its byte in the 155-byte voice (fm6_core.c FP_*: op k at 21 k; OL 16, FF 19, FC 18, DET 20, RS 13,
 * KVS 15, LC 11, RC 12, AMS 14, MODE 17) */
static const uint8_t DS_AT[DS_NSET][DS_NF] = {
    /* TLVL TPIT TBEND TSWP TDEC TDRV TWAV NLVL NFLT NCUT NRES NDEC NCLP NSPC MLVL MTON MDEC */
    {16, 19, 88, 123, 18, 13, 17, 89, 14, 90, 11, 91, 15, 12, 92, 20, 93},
    {37, 40, 94, 125, 39, 34, 38, 105, 35, 106, 32, 107, 36, 33, 108, 41, 109},
    {58, 61, 110, 134, 60, 55, 59, 111, 56, 112, 53, 113, 57, 54, 114, 62, 115},
    {79, 82, 121, 144, 81, 76, 80, 124, 77, 126, 74, 127, 78, 75, 128, 83, 129},
    {100, 103, 130, 140, 102, 97, 101, 131, 98, 132, 95, 133, 99, 96, 137, 104, 138},
};
#define DS_MARK_AT 139u                  /* (FP_LPMD = 83, FP_OP x 5 + FP_AMS = 2: the sets are in) */
#define DS_MARK2_AT 119u
#define DS_MARK 83u
#define DS_MARK2 2u

static uint32_t dl_byte(uint32_t l, uint32_t f)
{
    uint32_t n = (l % DV_NLANE) * DL_NF + f;
    return n / 11u * 21u + n % 11u;
}
/* track tr's patch holds lanes */
static int dl_on(uint32_t tr) { return !memcmp(fm6_patch[tr % NTRK] + DL_NAME, DL_SIG, sizeof DL_SIG); }
/* lane l's field f on track tr */
static uint32_t dl_get(uint32_t tr, uint32_t l, uint32_t f)
{
    return dl_on(tr) ? fm6_patch[tr % NTRK][dl_byte(l, f)] : DL_DEF[f];
}
/* DECAY TONE SNAP: the knob offset (-64..+63) of value v */
static int32_t dl_off(uint32_t v) { return ((int32_t)v - 50) * 64 / 50; }
/* track tr's patch holds the layer sets (and lanes) */
static int ds_on(uint32_t tr)
{
    const uint8_t *p = fm6_patch[tr % NTRK];
    return dl_on(tr) && p[DS_MARK_AT] == DS_MARK && p[DS_MARK2_AT] == DS_MARK2;
}
/* set k's value f on track tr (each within DS_MAX) */
static uint32_t ds_get(uint32_t tr, uint32_t k, uint32_t f)
{
    uint32_t v;
    k %= DS_NSET;
    f %= DS_NF;
    v = ds_on(tr) ? fm6_patch[tr % NTRK][DS_AT[k][f]] : DV_SYN_DEF[k][f];
    return v > DS_MAX[f] ? DS_MAX[f] : v;
}
/* .. all of set k into s (DS_NF) */
static void ds_read(uint32_t tr, uint32_t k, uint8_t *s)
{
    uint32_t f;
    for (f = 0; f < DS_NF; f++)
        s[f] = (uint8_t)ds_get(tr, k, f);
}
/* VOICE v's layer set (SYN 1..5: 0..4), -1 for any other */
static int32_t dl_syn(uint32_t v) { return v >= DL_NVOICE && v < DL_NVOICE + DS_NSET ? (int32_t)(v - DL_NVOICE) : -1; }
/* lane l of track tr plays a layer set: which (0..4), else -1 */
static int32_t dl_syn_of(uint32_t tr, uint32_t l) { return dl_syn(dl_get(tr, l, DL_VOICE)); }
/* VOICE v (1..): the drum it plays (DVT_*, or a model kit's piece: DV_KTYPE; SYN 1..5: DVT_SYNTH) */
static uint32_t dl_role(uint32_t v)
{
    if (dl_syn(v) >= 0)
        return DVT_SYNTH;
    v -= 1u;
    if (v < DVT_COUNT)
        return v;
    v -= DVT_COUNT;
    return DV_KTYPE(v / DV_NLANE % DV_NKIT + 1u, v % DV_NLANE);
}
/* VOICE v's name (s holds 12): "KIT", "PUNCH", "80 BD" */
static void dl_voice_name(uint32_t v, char *s)
{
    static const char *const OWN[DVT_COUNT] = {"PUNCH", "ROUND", "SNARE", "CLAP", "HAT CL", "HAT OP", "TOM", "CONGA",
                                               "RIM", "CLAVE", "BELL", "CYM"};
    static const char *const AB[DV_NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t k;
    if (!v) {
        str_cpy(s, "KIT", 12);
        return;
    }
    if (dl_syn(v) >= 0) {
        str_cpy(s, "SYN 1", 12);
        s[4] = (char)('1' + dl_syn(v));
        return;
    }
    if (v - 1u < DVT_COUNT) {
        str_cpy(s, OWN[v - 1u], 12);
        return;
    }
    k = v - 1u - DVT_COUNT;
    str_cpy(s, N_DRUM_KIT[DK_80 + k / DV_NLANE % DV_NKIT], 12);
    k %= DV_NLANE;
    s[str_len(s) + 1u] = 0;
    s[str_len(s)] = ' ';
    str_cpy(s + str_len(s), AB[k], 4);
}
/* the note a hit of `note` strikes with it (its lane's STACK), 0 = none */
static uint32_t drum_stack_note(const track_t *t, uint32_t note);

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(s->note[i]);
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return s->flags & SF_ACCENT ? step_lanes(s) : s->acc & step_lanes(s); }

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = drum_lane(s->note[i]);
        if (s->note[i] == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}

static uint32_t drum_stack_note(const track_t *t, uint32_t note)
{
    uint32_t l = drum_lane(note), s = dl_get((uint32_t)(t - trk), l, DL_STACK);
    return s && s - 1u != l ? DRUM_LANE_NOTE[(s - 1u) % NLANE] : 0u;
}

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drum_lane_t *)ENG_MEM(t - trk) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drum_note_on(track_t *t, voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = drum_gm(t->p, v->note, &st);
    lane = drum_lane(v->note);
    if (dl_get((uint32_t)(t - trk), lane, DL_VOICE))     /* the lane's own VOICE (DESIGN) */
        role = dl_role(dl_get((uint32_t)(t - trk), lane, DL_VOICE));
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    if (L->role != role || L->v.type != dv_run_type(role)) {   /* another drum on this lane: from rest */
        dv_init(&L->v, role);
        L->key.type = 0xFF;
        L->role = (uint8_t)role;
    }
    L->st = (int8_t)st;
    if (role == DVT_SYNTH) {                             /* the layer set it strikes, read again at the next block */
        L->slot = (uint8_t)dl_syn_of((uint32_t)(t - trk), lane);
        L->pg = (uint8_t)(fm6_pgen[(uint32_t)(t - trk) % NTRK] + 1u);
    }
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here, not from drum_amp:
                                                          * a key-off before the first block (zero-length MIDI
                                                          * notes) would end a fresh voice at env 0 in env_tick */
    dv_trigger(&L->v);
    if (lane == DV_HATC && K[DV_HATO].v.live)            /* a closed hat chokes the open one */
        dv_choke(&K[DV_HATO].v);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->v.live && !L->v.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static struct { int16_t drv; int32_t g, mk; } drum_drv[NTRK];   /* (Dah) DRV's gain and make-up, set when it moves */
static __attribute__((noinline, cold)) void drum_drv_set(uint32_t tr, int32_t drv)
{
    drum_drv[tr].drv = (int16_t)drv;
    drum_drv[tr].g = 4096 + drv * 3 * 4096 / 127;
    drum_drv[tr].mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * drum_drv[tr].g) >> 12));
}
static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drum_lane_t *L = drum_lane_of(t, v);
    dv_param_t k;
    int32_t y[CTL], mb[CTL], acc = clamp(p[P_E5] * v->vel * 4, 0, 65536), drv = p[P_E7], g = 0, mk = 0;   /* acc Q16 */
    int32_t lv = clamp(p[P_LN0 + ((uint32_t)v->s[0] & (DV_NLANE - 1u))], 0, 127);
    uint32_t i, r;
    vmod_t ml;                                           /* (only its amplitude ramp is read: voice_amp) */
    if (!L)
        return;
    if (lv < 127) {                                      /* the lane's LEVEL (square law) on the block's amplitude
                                                          * ramp, not per sample; 100 %: the ramp as it was */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);       /* Q15, 127: 32508 (not used), 64: 8256 */
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    if (n > CTL)
        n = CTL;
    r = L->role;
    dv_default(&k, r);                                   /* the knobs move every lane from its design (64), */
    {                                                    /* then the lane's own DESIGN values (0 at their defaults) */
        uint32_t tr = (uint32_t)(t - trk) % NTRK, l = (uint32_t)v->s[0] & (DV_NLANE - 1u), on = dl_on(tr);
        int32_t dd = on ? dl_off(fm6_patch[tr][dl_byte(l, DL_DECAY)]) : 0;
        int32_t dt = on ? dl_off(fm6_patch[tr][dl_byte(l, DL_TONE)]) : 0;
        int32_t ds = on ? dl_off(fm6_patch[tr][dl_byte(l, DL_SNAP)]) : 0;
        int32_t st = on ? ((int32_t)fm6_patch[tr][dl_byte(l, DL_TUNE)] - DL_DEF[DL_TUNE]) * 16 : 0;
        k.decay = (uint8_t)clamp(k.decay + p[P_E3] - 64 + dd, 0, 127);
        k.tone = (uint8_t)clamp(k.tone + p[P_E2] - 64 + dt, 0, 127);
        k.extra = (uint8_t)clamp(k.extra + p[P_E4] - 64 + ds, 0, 127);
        k.tune = (int16_t)(L->st * 16 + (p[P_E1] - 64) * 3 + st);   /* +-12 semitones, the lane's +-24 */
    }
    k.accent = (uint8_t)clamp(acc >> 9, 0, 127);         /* (Q16 -> 0..127) */
    if (r == DVT_SYNTH) {                                /* SYNTH: its layer set, read when the patch was written */
        uint32_t tr = (uint32_t)(t - trk) % NTRK;
        if (L->pg != fm6_pgen[tr]) {
            uint8_t sv[DS_NF];
            L->pg = fm6_pgen[tr];
            ds_read(tr, L->slot, sv);
            if (memcmp(sv, L->syn, DS_NF)) {
                memcpy(L->syn, sv, DS_NF);
                L->key.type = 0xFF;                      /* (set up again) */
            }
        }
    }
    if (((const uint32_t *)&k)[0] != ((const uint32_t *)&L->key)[0] ||
        ((const uint32_t *)&k)[1] != ((const uint32_t *)&L->key)[1]) {
        if (r == DVT_SYNTH)                              /* (a parameter moved) */
            dv_setup_synth(&L->c, &k, L->syn);
        else
            dv_setup(&L->c, &k);
        L->key = k;
        if (dv_has_metal(&L->c))
            dv_metal_tune(&L->mb, &L->c);
    }
    if (dv_wants_metal(&L->c, &L->v))                    /* (the metal source only while the voice takes it: SYNTH
                                                          * reads mb then only) */
        dv_metal_run(&L->mb, mb, n);
    if (L->v.trig && L->v.live)                          /* (Dah) struck again while ringing: the tail stops where it
                                                          * was, the new hit from rest: the step between, faded over
                                                          * ~1.5 ms, not a click */
        L->off += L->last;
    else if (!L->v.live && !L->v.trig)
        L->off = 0;
    dv_run(&L->c, &L->v, mb, y, n);
    if (L->off)
        for (i = 0; i < n; i++) {
            y[i] += L->off;
            L->off = (L->off * 61) / 64;
        }
    L->last = n ? y[n - 1u] : L->last;
    if ((drum_mute[(uint32_t)(t - trk) % NPART] >> ((uint32_t)v->s[0] & (DV_NLANE - 1u))) & 1u)
        return;                                          /* (a muted lane rings on unheard) */
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        uint32_t tr = (uint32_t)(t - trk) % NTRK;
        if (drum_drv[tr].drv != drv || !drum_drv[tr].g)
            drum_drv_set(tr, drv);
        g = drum_drv[tr].g;
        mk = drum_drv[tr].mk;
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV}; every kit suggests the BEAT pattern (GM notes) */
static const preset_t DRUM_PRESETS[] = {
    {"DRUM KIT", DRUM_KIT_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), PAT(12)},   /* (core.h: SAMPLE PERC's too) */
    /* 1.5.6 (Discussion #216): the new kits, appended */
    {"DEEP KIT", {DK_DEEP, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 28), PAT(12)},
    {"TIGHT KIT", {DK_TIGHT, 64, 64, 56, 64, 100, 0, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8), PAT(12)},
    {"PERC KIT", {DK_PERC, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24), PAT(12)},
    /* HA HA HE HU (drum designer phase 2): the layered kits, their lanes designed (DRUM_DESIGN below), appended */
    {"LAYER KIT", {DK_STD, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 16, 30), PAT(12)},
    {"HARD KIT", {DK_STD, 64, 64, 64, 64, 110, 0, 18}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 14), PAT(12)},
};

/* A factory kit's lanes designed (HA HA HE HU): a preset of DRUM_PRESETS with one loads it into the track's patch
 * (eng_fm6.c drum_track_loaded; any other factory kit: no lanes, as before). Per lane VOICE TUNE DECAY TONE SNAP STACK
 * (DL_*), the five layer sets (DS_*). A user preset or a project made from it carries its own copy (the patch) */
typedef struct {
    uint8_t preset;                                      /* its index in DRUM_PRESETS */
    uint8_t lane[DV_NLANE][DL_NF];
    uint8_t set[DS_NSET][DS_NF];
} dl_design_t;
#define DL_S(k) (DL_NVOICE + (k))                        /* VOICE SYN k + 1 */
static const dl_design_t DRUM_DESIGN[] = {
    {4, /* LAYER KIT: a deep swept kick with a click, a 185 Hz snare whose noise claps 3 times, a wide clap, hats of the
         * metal over high noise (the closed one short, the open one long: one set, DECAY apart), a crash on BELL;
         * TOM and RIM the kit's */
     {{DL_S(0), 24, 50, 50, 50, 0}, {DL_S(1), 24, 50, 50, 50, 0}, {DL_S(2), 24, 50, 50, 50, 0},
      {DL_S(3), 24, 18, 50, 50, 0}, {DL_S(3), 24, 90, 50, 50, 0}, {0, 24, 50, 50, 50, 0}, {0, 24, 50, 50, 50, 0},
      {DL_S(4), 24, 50, 50, 50, 0}},
     /* TLVL TPIT TBEND TSWP TDEC TDRV TWAV NLVL NFLT NCUT NRES NDEC NCLP NSPC MLVL MTON MDEC */
     {{95, 17, 40, 8, 25, 2, 0, 32, 2, 75, 0, 18, 0, 0, 0, 7, 30},      /* kick 44 Hz from +40 st, a 3.5 ms click */
      {85, 42, 10, 6, 16, 2, 0, 95, 1, 72, 0, 62, 2, 0, 0, 7, 30},      /* snare 185 Hz, band noise at 3.5 kHz x3 */
      {0, 42, 0, 5, 10, 0, 0, 85, 1, 60, 1, 65, 3, 1, 0, 7, 30},        /* clap: 1.4 kHz, 4 strikes 8 ms apart */
      {0, 60, 0, 0, 5, 0, 0, 55, 2, 92, 0, 45, 0, 0, 92, 11, 55},       /* hats: the metal at 9 kHz, noise above */
      {0, 60, 0, 0, 5, 0, 0, 40, 2, 78, 0, 88, 0, 0, 62, 5, 97}}},      /* crash: 4.7 kHz, 0.8 s */
    {5, /* HARD KIT: a short hard kick (more drive, a band click), a 196 Hz snare with a metal sizzle and 4 claps, a
         * tight resonant clap, brighter hats, a long crash */
     {{DL_S(0), 24, 50, 50, 50, 0}, {DL_S(1), 24, 50, 50, 50, 0}, {DL_S(2), 24, 50, 50, 50, 0},
      {DL_S(3), 24, 15, 50, 50, 0}, {DL_S(3), 24, 95, 50, 50, 0}, {0, 24, 50, 50, 50, 0}, {0, 24, 50, 50, 50, 0},
      {DL_S(4), 24, 50, 50, 50, 0}},
     {{99, 20, 44, 7, 21, 5, 0, 45, 1, 70, 1, 22, 0, 0, 0, 7, 30},      /* kick 52 Hz, drive x6, a 3 kHz click */
      {62, 43, 12, 5, 15, 3, 0, 78, 2, 70, 0, 60, 3, 0, 22, 8, 40},     /* snare 196 Hz, noise above 3 kHz x4, metal */
      {0, 42, 0, 5, 10, 0, 0, 88, 1, 64, 2, 58, 4, 0, 0, 7, 30},        /* clap: 1.8 kHz Q 2.5, 5 strikes */
      {0, 60, 0, 0, 5, 0, 0, 62, 2, 95, 1, 40, 0, 0, 85, 12, 48},       /* hats: 9.8 kHz */
      {0, 60, 0, 0, 5, 0, 0, 42, 2, 82, 0, 92, 0, 0, 65, 7, 99}}},      /* crash: 6 kHz, 0.95 s */
};
/* the lanes designed of factory preset pi, 0 = none */
static const dl_design_t *drum_design(uint32_t pi)
{
    uint32_t i;
    for (i = 0; i < NELEM(DRUM_DESIGN); i++)
        if (DRUM_DESIGN[i].preset == pi)
            return &DRUM_DESIGN[i];
    return 0;
}

static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", F_ENUM, 0, DK_COUNT - 1, DK_STD, N_DRUM_KIT, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECY", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 100, 0, 0},
        {"KICK", F_ENUM, 0, 1, 0, N_DRUM_KICK, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
    .mem = sizeof(drum_lane_t) * DV_NLANE,
};
