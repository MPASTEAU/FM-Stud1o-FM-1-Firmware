/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Four tracks, each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part; the SAMPLE engine's PERC set is retired (drum_from_perc).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

#ifndef FW_NAME
#define FW_NAME "FM Stud1o"/* this fork's name: the splash, ABOUT, the crash screen (Felucca's credits stay) */
#endif
#ifndef FW_SPLASH_MS
#define FW_SPLASH_MS 700u        /* the power-on splash's time on the screen (main.c; Felucca's: 400; Dah: as long as the LED sweep, ~0.7 s) */
#endif
#ifndef FELUCCA_PROF
#define FELUCCA_PROF 0           /* the audio ISR's cost by stage (fx.c prf_lap, console.c `prof`): src/felucca.c sets
                                  * it with the console */
#endif
#ifndef FELUCCA_DEMO
#define FELUCCA_DEMO 0           /* power-on with the dubstep demo (main.c felucca_demo): src/felucca.c and the browser
                                  * build set 1; the host tests keep Felucca's power-on */
#endif

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPART 8                  /* synth parts: tracks 1..8 (Felucca: 4). Parts 5..8 are light: no PHYS,
                                  * no GRAIN (engines.c NPART_BIG, eng_fits) */
#define PROJ_NP_W 114u           /* (Dah) the parameters FUN10 / FUNX store: up to P_DKAMT, the eight engine values */
#define NTRK_V10 4u              /* the tracks of every stored format up to FUN10 (Felucca's) */
#define NTRK NPART               /* tracks (the formats and the protocol count these): every track is a part */
#define NSTEP 64                 /* the steps of a page: the track's step[] (its pattern's first page), a stored record's */
#define LEN_MAX 256              /* (long patterns) LEN up to 4 pages of NSTEP: page 0 on the track, the others in the
                                  * pattern store (pstore.c PS_PG), played through a ring (pgr) the main loop fills */
#define NPAGE (LEN_MAX / NSTEP)
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 1          /* the SLICE engine (eng_slice.c), engine 13; FELUCCA_SLICE=0 builds without it */
#endif
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1           /* the second core (src/felucca.c): 1 = built in, MENU > 2ND CORE ON lets it render
                                  * some parts; 0 = none of it */
#endif
#define C1_NP (FELUCCA_CPU2 ? NPART : 1)   /* the copies of the state a part's render writes that parts shared before the
                                            * second core (voice.c SPREAD, eng_formant.c): one each with two cores,
                                            * one for all with one, as up to 1.6 */
#define C1_PI(t) ((uint32_t)((t) - trk) % C1_NP)   /* a part's copy */
#ifndef FELUCCA_GROWL
#define FELUCCA_GROWL (FELUCCA_SLICE && !FELUCCA_FM4)   /* GROWL (14, eng_growl.c): after SLICE, so with
                                                          * it; not with DIGITAL (its favourites take that row) */
#endif
#if FELUCCA_GROWL && !FELUCCA_SLICE
#error "FELUCCA_GROWL (engine 14) needs FELUCCA_SLICE (engine 13)"
#endif
#define NENGINES (13 + FELUCCA_SLICE + FELUCCA_GROWL)   /* SLICE (13), GROWL (14) last: the others keep their numbers */
#define ENGI_GROWL 14u
#define ENGI_DIGITAL 1u          /* reserved without FELUCCA_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define NENG_SHOWN (NENGINES - !FELUCCA_FM4)   /* the engines one can pick: PRESETS, the EDIT layer, the editor,
                                                * in the display order of engines.c ENGINE_ORDER */
#define UP_SLOTS 32u             /* user presets (upreset.c) */

/* ------------------------------------------------------- parameters --- */
/* the engine interface (Felucca-Engines, MIT: engines/, a git submodule): param_desc_t and F_*, voice_t,
 * vmod_t, preset_t with FX() / PAT(), engine_t, NELEM */
#include "../engines/src/felucca_engine.h"

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* the modulation matrix's 4 slots of SRC, DST, AMT (removed): inert, */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* kept for the formats / protocol; new common parameters go just */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* before P_E0 (user presets and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,   /* DRUM lane levels (eng_drum.c, #97): KICK .. BELL */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* 1.2 (#154): LFO SYNC (OFF, a note value), TRIG (NOTE, FREE), POL (BI, UNI) */
    P_SQNT,                                    /* 1.2 (#162): QUANTIZE, the steps' recorded / nudged timing (ON: none) */
    P_SPRD,                                    /* (#148): SPREAD, the voices of a POLY / UNISON track around PAN (fx.c) */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* 1.5 (#78, #177): the INSERT, its TYPE, three values and MIX (fx.c) */
    P_FTYPE,                                   /* 1.5 (#104): ANALOG's filter TYPE, LP (as before) / BP / HP (eng_analog.c) */
    P_ESYNC,                                   /* 1.5 (#175): ENV SYNC: ATK DEC REL as note values of the tempo (voice.c) */
    P_FXORD,                                   /* the order of DIST, INSERT and SLICER (fx.c mix_part) */
    P_DKSRC, P_DKAMT,                          /* DUCK, the sidechain: its source track, depth (fx.c); its release
                                                * is P_DKREL (P_ED_FX's byte) */
    P_EQLO, P_EQHI, P_EQ1F, P_EQ1G, P_EQ2F, P_EQ2G,   /* Dah: the EQ's four slots (fx_eq.c EQ_FID EQ_SID): FREQ, and */
    P_EQ3G, P_EQ3F,                            /* SLOPE or GAIN (were LOW CUT, HIGH CUT and three bells' FREQ GAIN) */
    P_DOUT, P_DCOL,                            /* Dah: DIST's OUT (-64..63: +-15 dB, 0 as before) and COLOR (0 as before) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};
#define P_DKREL P_ED_FX                  /* DUCK's release in the byte P_ED_FX kept (nothing read it) */

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-4 (channels 1..4 -> parts 1..4, 5..16 ignored), 1 SEL;
                                        * 1.2: 2..4 CH5-8 CH9-12 CH13-16 (seq.c midi_base); 1.5.5: 5 TRACK (song.mch) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, 2 HALL (1.2, the power-on default) (fx.c). Was
                                 * G_DRCH, the GM drum part's MIDI channel (inert since 1.0, never read); projects
                                 * of formats before FUN7 load it as ROOM (project.c proj_rtype_room) */
    G_DRLVL, G_DRREV,           /* (Dah) the MASTER's COMPRESSOR, AMOUNT and RELEASE (G_MCOMP G_MREL); were inert (label "-", on no page): the GM drum part they set is gone; kept
                                 * because the ids and G_COUNT are fixed by the formats and the protocol (only
                                 * the import of an old project reads them: proj_drums_to_part) */
    G_COUNT
};
#define G_MCOMP G_DRLVL                  /* (Dah) the MASTER's COMPRESSOR (fx_comp.c), in the ids the GM drums left */
#define G_MREL G_DRREV

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> KIT: STD HAND CYM H+CYM */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* SAMPLE SET 4 was PERC, the GM-mapped drum kit (tools/gen_samples.py); retired after 1.0.2: its index stays
 * (SET / GRAIN SRC 4 is an alias of PIANO, USR1..3 keep 5..7). A sound that selected it is the DRUM engine with
 * its default kit (eng_drum.c DRUM_PRESETS[0]: the same GM key map, so its patterns still play as drums): its E
 * values become the kit's, the rest of the sound (mix, sends, ..) stays. 1 = it was one (its engine is
 * ENGI_DRUM now); projects (project.c proj_perc), user presets (upreset.c up_migrate), factory preset 4 and
 * favourites (ui.c, settings_persist.c). Idempotent */
#define ENGI_SAMPLE 4u
#define SMP_SET_PERC 4u
#define DRUM_KIT_E {0, 64, 70, 64, 64, 100, 0, 0}   /* {KIT STD, TUNE, TONE, DECY, SNAP, ACC, KICK PUNCH, DRV} */
static int drum_from_perc(uint32_t engine, int16_t *e)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    uint32_t i;
    if (engine != ENGI_SAMPLE || e[0] != (int16_t)SMP_SET_PERC)
        return 0;
    for (i = 0; i < 8u; i++)
        e[i] = KIT[i];
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
/* voice_t, vmod_t, preset_t, engine_t: felucca_engine.h (above) */

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
/* (overlaps) the step's time: bits 0..1 ST_NOTE / ST_TIE / ST_REST (st_kind), bits 4..7 on a NOTE step its notes that go
 * on (st_cont: bit k, note k sounds on from the step before, a TIE of that note alone; the others start there). A TIE
 * still carries on every note sounding, as always: a note held from step 1 beside one pressed at step 4 is a NOTE, TIEs,
 * a NOTE of the two (the first going on), TIEs, a NOTE of the second going on alone. A note said to go on that does not
 * sound before (an edit cut it) stays silent, as a TIE after a REST. 0 in every older pattern */
#define ST_KIND 3u
#define ST_CONT_SH 4u
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST, its notes going on (ST_CONT_SH) */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent; 1.5.5: 102..112 a condition */
} step_t;
static uint32_t st_kind(const step_t *s) { return s->time & ST_KIND; }
static uint32_t st_cont(const step_t *s)          /* bit k: note k goes on (a NOTE step only, its n notes) */
{
    return (s->time & ST_KIND) == ST_NOTE ? (uint32_t)(s->time >> ST_CONT_SH) & ((1u << (s->n & 7u)) - 1u) & 15u : 0u;
}
static void st_set_cont(step_t *s, uint32_t m)
{
    s->time = (uint8_t)((s->time & ST_KIND) | (m & ((1u << (s->n & 7u)) - 1u) & 15u) << ST_CONT_SH);
}
static void st_del(step_t *s, uint32_t k)        /* note k out (the others down one, each going on as it did) */
{
    uint32_t c = st_cont(s), i, lo = (1u << k) - 1u;
    if (k >= s->n)
        return;
    for (i = k; i + 1u < s->n; i++)
        s->note[i] = s->note[i + 1u];
    s->note[--s->n] = 0;                         /* (a clean slot: a step emptied so is a clean REST / TIE) */
    st_set_cont(s, (c & lo) | (c >> 1 & ~lo));
}
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
/* 1.5.5 (#209): CONDITIONS, in the chance byte's free values (no format change): COND_FIRST + c - 1 is condition c
 * (1..NCOND): a:b (1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4) plays on loop a of every b of its track (seq.c seq_cond), FILL
 * while FILL is held (GLO + E4), !FILL while not. A step has a chance or a condition (one byte): a condition plays at
 * 100 %. Firmware before 1.5.5 refuses a project holding one (its proj_unpack wants 0..101): the slot reads empty there.
 * The knobs and the lists show the two as one value (step_chance_v): 0..100 %, then 101..100 + NCOND the conditions */
#define COND_FIRST 102u
#define NCOND 11u
#define COND_FILL 10u                    /* FILL, NOT FILL: the last two */
#define COND_NFILL 11u
#define CHANCE_V_MAX (100u + NCOND)
static uint32_t step_cond(const step_t *s)   /* its condition 1..NCOND, 0 none */
{
    return s->probability >= COND_FIRST && s->probability < COND_FIRST + NCOND ? s->probability - COND_FIRST + 1u : 0u;
}
static uint32_t step_chance(const step_t *s)
{
    return !s->probability ? 100u : s->probability <= 100u ? s->probability : s->probability == 101u ? 0u : 100u;
}
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
static uint32_t step_chance_v(const step_t *s) { uint32_t c = step_cond(s); return c ? 100u + c : step_chance(s); }
static void step_set_chance_v(step_t *s, uint32_t v)
{
    if (v > 100u)
        s->probability = (uint8_t)(COND_FIRST - 101u + (v < CHANCE_V_MAX ? v : CHANCE_V_MAX));
    else
        step_set_chance(s, v);
}
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)
{
    s->flags = (uint8_t)((s->flags & ~SF_RATCH) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
/* NUDGE (1.2, #162, D98): a step's timing, in 1/NUDGE_DIV of the track's DIV, -8..+7 (half a step early .. 7/16 late);
 * live recording keeps it, SEQ > DETAIL's KNOB 3 and STEP (steps held + PRESETS) set it, the track's QUANTIZE
 * OFF (P_SQNT) plays it (seq.c seq_tick).
 * Four bits of the flags no other field uses: 4 (bit 3 of the nudge, its sign) and 32 64 128 (bits 0..2). 0 = on the
 * step: every older pattern. A user preset's pattern keeps none (upreset.c up_pat_norm) */
#define NUDGE_DIV 16
#define SF_NUDGE (4u | 0xE0u)
static int32_t step_nudge(const step_t *s)
{
    uint32_t u = (uint32_t)(s->flags >> 5) | (uint32_t)(s->flags & 4u) << 1;
    return (int32_t)(u ^ 8u) - 8;
}
static void step_set_nudge(step_t *s, int32_t v)
{
    uint32_t u = (uint32_t)(v < -8 ? -8 : v > 7 ? 7 : v) & 15u;
    s->flags = (uint8_t)((s->flags & ~SF_NUDGE) | (u & 7u) << 5 | (u >> 1 & 4u));
}
#define MOTION_MAX 128u          /* 1.2 (FUN10): 64 before */
/* Four tracks x64 steps fit one byte. Values retain their signed parameter range (-64..127: an int8_t, 1.2).
 * param: the P_* id (< 128: P_COUNT is at most 127, project.c), bit 7 (MOTION_LOCK, 1.1) a parameter lock: the value
 * sounds on that step only and goes back after it (motion.c motion_step); without it an automation event, the
 * value holds until another one changes it. One record per (track, step, id), of either kind */
#define MOTION_LOCK 0x80u
#define MOTION_ID(e) ((uint32_t)(e)->param & 0x7Fu)
/* FUN11: eight tracks, so place is 16 bits, track << 6 | step (the same code, tracks 0..7). Long patterns: the page
 * (step >> 6, 0..3) in bits 9..10, so page 0's places are as before (and code reading place >> 6 as the track, the
 * editor's, sees page 0 only) */
typedef struct { uint16_t place; uint8_t param; int8_t value; } motion_event_t;
#define MPLACE(k, s) ((uint16_t)(((uint32_t)(s) >> 6) << 9 | (uint32_t)(k) << 6 | ((uint32_t)(s) & 63u)))
#define MP_TRK(e) (((uint32_t)(e)->place >> 6) & 7u)
#define MP_STEP(e) (((uint32_t)(e)->place >> 9) << 6 | ((uint32_t)(e)->place & 63u))
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 516u, "motion layout (FUN11)");
typedef struct { uint8_t place, param; int8_t value; } motion_event_v10_t;   /* frozen: FUN10's records */
typedef struct { uint8_t count, on, rsv[2]; motion_event_v10_t event[MOTION_MAX]; } motion_store_v10_t;
_Static_assert(sizeof(motion_store_v10_t) == 388u, "motion disk layout (FUN10)");
#define CHAIN_ROWS 16u
/* A song section (1.2, FUN10): the PROJECT slot each track plays (0..3, A..D; CHAIN_SILENT "-": that track plays
 * nothing there) and its repeats (1..16). Up to 1.1 a row was {slot, repeat}, all four tracks on that slot
 * (chain_v9_t, FUN6..FUN9): it loads as a section with the four on it (song_chain.c chain_from_v9) */
#define CHAIN_SILENT 0u                /* (a section names a pattern number per track, 1..PS_NUM; 0 "-") */
/* a section's repeat byte, bit 7 CUT: as it starts the FX tails go (the reverb and delay buses cleared,
 * every sounding voice faded out at once): the drop dry and alone (song_chain.c chain_cut). Bits 0..6 the repeats */
#define CHAIN_CUT 0x80u
#define CHAIN_REP(r) ((uint32_t)(r).repeat & 0x7Fu)
typedef struct { uint8_t slot[NTRK], repeat; } chain_row_t;
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;
typedef struct { uint8_t slot[NTRK_V10], repeat; } chain_row_v10_t;   /* frozen: FUN10's sections (four tracks) */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_v10_t row[CHAIN_ROWS];
} chain_v10_t;
typedef struct { uint8_t slot, repeat; } chain_row_v9_t;   /* frozen: the rows of FUN6..FUN9 */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_v9_t row[CHAIN_ROWS];
} chain_v9_t;
_Static_assert(sizeof(chain_config_t) == 148u && sizeof(chain_v10_t) == 84u && sizeof(chain_v9_t) == 36u,
               "song chain layouts (FUN11 / FUN10 / FUN6..FUN9)");

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[3];           /* CHORD: the other notes sounding with arp_note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* NUDGE (QUANTIZE OFF, seq.c seq_tick): a step plays at its start + its nudge. Latched as a step starts: */
    uint32_t nd_late;            /* the step playing now plays at this seq_pos + 1 (0: it played) */
    uint32_t nd_early;           /* the next step plays early, at this seq_pos + 1 of this one (0: on its start) */
    uint16_t nd_fired;           /* that step + 1 once it played early (its start then plays nothing), 0 = none */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST / INSERT tail) */
    uint8_t ins_run;             /* the INSERT still sounds (fx.c track_insert: its wet share above 0) */
    uint8_t cond_loop;           /* 1.5.5 (#209): the loop of its LEN playing (0..COND_LOOPS - 1, seq.c seq_tick) */
#if FELUCCA_CPU2
    /* the second core: a part's render state, its own, so the two cores' parts share nothing (fx.c mix_block); in
     * the pad that keeps sizeof(track_t) a multiple of 16 (14 bytes before) */
    uint8_t lfo_wrap;            /* the LFO's phase wrapped this block: its S&H is drawn after the parts (voice.c) */
    uint32_t esync_k[3];         /* ATK's step, DEC's and REL's coefficient (Q16) this block (voice.c esync_coef) */
    uint8_t spread_on;           /* SPREAD this block (fx.c mix_part, voice.c track_render) */
    uint8_t tie_over;            /* (Dah) the notes held over to the next step's (a TIE into another note): no glide */
    uint8_t seq_keep;            /* (overlaps) bits 0..3: seq_notes[k] goes on into the next step (its own TIE: no release
                                  * at the gate); 4..7: it went on into this one (ends half a step after the gate) */
    uint8_t rsv_sz[6];           /* (sizeof(track_t) a multiple of 16: below; Dah: 7 with its 13 parameters, tie_over) */
#else
    uint8_t tie_over;            /* (Dah) as above */
    uint8_t seq_keep;            /* (overlaps) as above */
    uint8_t rsv_sz[7];           /* (sizeof(track_t) a multiple of 16: below; Dah: 8) */
#endif
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    uint8_t mw;                  /* MENU > SELECT MODW: the mod wheel sent as CC1 (ui_input.c sel_play), 0..127 */
    uint8_t m_vel;               /* the latest note-on's velocity, before UNISON's scaling (voice.c: the voices' mvel) */
    uint8_t sp_alt;              /* SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
} track_t;
/* the audio ISR indexes trk[] with a multiply by a small immediate and a shift while the size is a multiple of 16; 1.5's
 * new track parameters (the INSERT, the filter TYPE, ENV SYNC) and state would make it no multiple (the constant
 * loaded first: +3 % on the ISR's static cost, target_budget.py): one pad, rsv_sz, keeps it */
_Static_assert(sizeof(track_t) % 16u == 0, "track_t: a multiple of 16 bytes (a cheap trk[] index on the target)");

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays; 3: SONG, A..D are its (seq.c song_key) */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t cpu_peak_q8;        /* (Dah) its highest half since MENU > INFO last read it, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
    uint8_t mch[NTRK];           /* 1.5.5 (#202): MIDI IN TRACK (G_ROUTE ROUTE_TRACK), each track's channel as stored: 0..15
                                  * the channel's distance up from the track's own (track i: channel (i + v) % 16, so 0, the
                                  * zeroed default, is T1..T4 on channels 1..4), MCH_OMNI (any channel no track has), MCH_OFF
                                  * (none); mch_get / mch_set. The project's, in FUN10's spare bytes (project.c PROJ_MIDI_OFF);
                                  * MENU > MIDI > T1..T4 CHANNEL */
    uint8_t ntrk;                /* the project's tracks, 1..NTRK (0: all, as every project before); the others
                                  * neither shown, nor played, nor stored (trk_count) */
} song_t;
#define ROUTE_SEL 1
#define ROUTE_TRACK 5                /* G_ROUTE: TRACK, each track on its own channel (song.mch) */
#define MCH_OMNI 16u
#define MCH_OFF 17u
#define MCH_N 18u                    /* channels 1..16, OMNI, OFF */

static track_t trk[NTRK];        /* the instrument: four parts */
static song_t song;
static inline uint32_t trk_count(void) { return song.ntrk && song.ntrk <= NTRK ? song.ntrk : NTRK; }
/* a track's extras (project.c "FUNY", ui_lfodraw.c): LFO WAVE DRAW's 16 points (-64..63, voice.c lfo_wave),
 * stepped or joined by lines (smooth), and how its automation moves between two events (curve: 0 STEP, as always; 1 RAMP,
 * motion.c motion_ramp) */
#define LDRAW_N 16u
/* (Dah) the EQ's filters (fx_eq.c): how many (0..4), filter k's slot (o's bits 2k, 2k + 1: its two parameters, EQ_FID
 * EQ_SID), each slot's TYPE (EQT_*); f: EQF_NEW (set since filters: none, a project's EQ before them, LOW CUT HIGH CUT
 * and three bells, made filters on load: eq_adopt), EQF_IN (the EQ in the chain) */
typedef struct { uint8_t n, o, t[4], f; } eqc_t;
typedef struct {
    int8_t draw[LDRAW_N];
    uint8_t smooth, curve, rsv0, rsv;   /* (Dah: rsv0 the COMPRESSOR's AMOUNT, 0 none (fx_comp.c); rsv MATCH's COLOR and
                                        * NOTES, chord.c) */
    uint8_t tap[3], rsv1;        /* (Dah) where CHORUS DELAY REVERB take the sound: 0 after the chain, k before its k-th;
                                  * rsv1 the COMPRESSOR's RELEASE */
    uint8_t home[4];             /* (Dah) HOME's knobs: a parameter id + 1 each (0: the engine's, as before; ui.c) */
    int8_t cin, cout;            /* (Dah) the COMPRESSOR's IN and OUT gains (-64..63: +-15 dB, 0 as before; fx_comp.c) */
    uint8_t sc2;                 /* (Dah) the SIDECHAIN's second source (a track + 1, 0 none; fx.c duck_part_sd) */
    int8_t din;                  /* (Dah) DIST's IN gain before its drive (-64..63: +-15 dB, 0 as before) */
    eqc_t eq;                    /* (Dah) the EQ's bands (fx_eq.c) */
} trkx_t;
#define TRKX_V2 20u                      /* (Dah) the extras' size before tap and home */
static trkx_t trkx[NTRK];
/* the bank of four tracks shown where four are (MIXER, HOME LEVELS, SONG's lanes, the layers' solos and
 * mutes on F3..B3 and the black keys 1..4): the selected track's, 1..4 or 5..8 */
static inline uint32_t trk_bank(void) { return (uint32_t)song.sel & ~3u; }
#define TSEL (&trk[song.sel])    /* the selected track */
/* MIDI IN TRACK: track i's channel 0..15, MCH_OMNI or MCH_OFF (song.mch), and set */
static inline uint32_t mch_get(uint32_t i)
{
    uint32_t v = song.mch[i % NTRK];
    return v < 16u ? (v + i) & 15u : v < MCH_N ? v : (i & 15u);
}
static inline void mch_set(uint32_t i, uint32_t ch) { song.mch[i % NTRK] = (uint8_t)(ch < 16u ? (ch - i) & 15u : ch < MCH_N ? ch : 0u); }

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
