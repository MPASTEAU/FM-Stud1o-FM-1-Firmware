/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H", "DRAW"};   /* (HA HA HE HU: DRAW, ui_lfodraw.c) */
/* LFO 2 (1.2, #154). SYNC: OFF (RATE), or one cycle per note value of the tempo, longest first (LSYNC_DIV: its N_DIV);
 * TRIG: NOTE restarts it at PHS on a fresh phrase (as before), FREE never; POL: BI -1..+1 (as before), UNI 0..+1 */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT",   /* (append-only: seq.c AM_*) */
                                       "DNUP", "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
/* the delay's TIME (G_DTIME): N_DIV's ten (their values kept), then 1.5.1's dotted ones appended (stored values:
 * append-only). A list of its own: ARP RATE, SEQ DIV and LFO SYNC keep N_DIV's ten (fx.c delay_samples) */
static const char *const N_DTIME[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR",
                                      "1/8D", "1/16D", "1/4D"};
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
/* QUANTIZE (P_SQNT): OFF the steps' NUDGE as recorded, ON on the grid (as always: values 0 1); HA HA HE HU appends a
 * strength: 25 % .. 75 % of the way to the grid (seq.c seq_nudge) */
static const char *const N_QNT[] = {"OFF", "ON", "25%", "50%", "75%"};
/* ANALOG's filter TYPE (1.5, #104; eng_analog.c): the SVF's low-pass (as always), band-pass, high-pass output */
static const char *const N_FTYPE[] = {"LP", "BP", "HP"};
/* ENV SYNC (1.5, #175; voice.c esync_coef): ATK DEC REL as note values of the tempo, shortest first. 0..127 split
 * evenly over these 25 (param_format's F_INT name list: value v is ESYNC_NAMES[v * 25 / 128]); ESYNC_UNITS: each in
 * 1/384 of a bar of 4/4 (a quarter 96), 0 = the shortest time ATK / DEC / REL 0 has. 0-terminated (param_format) */
#define ESYNC_N 25u
static const char *const ESYNC_NAMES[ESYNC_N + 1u] = {
    "0", "1/64T", "1/64", "1/32T", "1/64D", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
    "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1", "1/1D", "2BAR", "3BAR", "4BAR", 0};
static const uint16_t ESYNC_UNITS[ESYNC_N] = {0, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72,
                                              96, 128, 144, 192, 256, 288, 384, 576, 768, 1152, 1536};
static const param_desc_t ESYNC_DESC[3] = {   /* ATK DEC REL with ENV SYNC on: the range and defaults as ever */
    {"ATK", F_INT, 0, 127, 10, ESYNC_NAMES, 0},
    {"DEC", F_INT, 0, 127, 70, ESYNC_NAMES, 0},
    {"REL", F_INT, 0, 127, 60, ESYNC_NAMES, 0},
};
static uint32_t esync_idx(int32_t v) { return (uint32_t)(v & 127) * ESYNC_N >> 7; }   /* (param_format's split) */
/* seq.c kb_map; 1 = SNAP (stored projects: the former ON); 3 = SEQ: SNAP, and the sequencer's notes snap too as
 * they play (seq.c seq_step; the steps keep what was written). Append-only: older projects hold 0..2 */
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "SEQ", "SEQ+T"};
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW",
                                     "COLOR", "MATCH"};   /* (Dah: append-only, chord.c) */
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT", "AUTO"};   /* VC_CLOSE .. VC_AUTO (Dah) */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
static const char *const N_MIDI_INPUT[] = {"USB", "TRS"};
/* MIDI IN (seq.c midi_base): 1..4 -> parts, 5..16 ignored / all -> selected / (1.2, append-only) 5..8, 9..12, 13..16 -> parts
 * / (1.5.5, #202) TRACK: each track its own channel (song.mch, MENU > MIDI > T1..T4 CHANNEL) */
static const char *const N_ROUTE[] = {"CH1-8", "SEL", "CH5-8", "CH9-12", "CH13-16", "TRACK"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_RTYPE[] = {"ROOM", "SPRING", "HALL"};   /* G_RTYPE: the reverb bus's model (fx.c) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
/* the INSERT's types (1.5, fx.c IT_*; append-only: stored). Its three values A B C mean what the type says (ins_desc) */
static const char *const N_ITYPE[] = {"OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR"};
/* HA HA HE HU: ORDER, DIST (D) INSERT (I) SLICER (S) in a row (fx.c FX_ORDER); DUCK's source (fx.c duck_part) */
/* (Dah) ORDER: DIST (D), INSERT (I), SLICER (S), EQ (E), SIDECHAIN (C): the 24 of D I S E first (C last: as they were),
 * then the rest (fx.c FX_ORDER, the same order) */
static const char *const N_FXORD[] = {
    "DISEC", "DSIEC", "IDSEC", "ISDEC", "SDIEC", "SIDEC", "EDISC", "EDSIC", "EIDSC", "EISDC", "ESDIC", "ESIDC",
    "DEISC", "DESIC", "DIESC", "DSEIC", "IEDSC", "IESDC", "IDESC", "ISEDC", "SEDIC", "SEIDC", "SDEIC", "SIEDC",
    "CDEIS", "CDESI", "CDIES", "CDISE", "CDSEI", "CDSIE", "CEDIS", "CEDSI", "CEIDS", "CEISD", "CESDI", "CESID",
    "CIDES", "CIDSE", "CIEDS", "CIESD", "CISDE", "CISED", "CSDEI", "CSDIE", "CSEDI", "CSEID", "CSIDE", "CSIED",
    "DCEIS", "DCESI", "DCIES", "DCISE", "DCSEI", "DCSIE", "DECIS", "DECSI", "DEICS", "DESCI", "DICES", "DICSE",
    "DIECS", "DISCE", "DSCEI", "DSCIE", "DSECI", "DSICE", "ECDIS", "ECDSI", "ECIDS", "ECISD", "ECSDI", "ECSID",
    "EDCIS", "EDCSI", "EDICS", "EDSCI", "EICDS", "EICSD", "EIDCS", "EISCD", "ESCDI", "ESCID", "ESDCI", "ESICD",
    "ICDES", "ICDSE", "ICEDS", "ICESD", "ICSDE", "ICSED", "IDCES", "IDCSE", "IDECS", "IDSCE", "IECDS", "IECSD",
    "IEDCS", "IESCD", "ISCDE", "ISCED", "ISDCE", "ISECD", "SCDEI", "SCDIE", "SCEDI", "SCEID", "SCIDE", "SCIED",
    "SDCEI", "SDCIE", "SDECI", "SDICE", "SECDI", "SECID", "SEDCI", "SEICD", "SICDE", "SICED", "SIDCE", "SIECD"};

static const char *const N_DKSRC[] = {"OFF", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};   /* (Dah: eight) */
/* CRUSH: BITS 1..16 and RATE (the held sample's rate, fx.c INS_HOLD) over the range in 16 even parts (0-terminated) */
static const char *const N_IBITS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                      "16", 0};
static const char *const N_IRATE[] = {"689", "919", "1.1k", "1.4k", "1.8k", "2.2k", "2.8k", "3.7k", "4.4k", "5.5k",
                                      "7.4k", "8.8k", "11k", "15k", "22k", "44k", 0};
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels).
 * Append-only (stored values keep their meaning): S&H SLEW and DEPTH (#132) came after EXPR and E8; VEL and TIME
 * (1.5.6, #213: at the note) after DEPTH */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "S&H", "SLEW"};
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "DIST", "CHO", "DLY", "REV", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8", "DEPTH", "VEL", "TIME"};
static const char *const N_ENGNAME[] = {"ANALOG", FELUCCA_FM4 ? "DIGITAL" : "-", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS",
                                             "DRUM", "NOISE", "FM6",
#if FELUCCA_SLICE
                                             "SLICE",
#endif
#if FELUCCA_GROWL
                                             "GROWL",      /* 14 (HA HA HE HU): DESC of G_ENGSEL lists every engine */
#endif
};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("REL", F_BIPCT, -64, 63, 0),   /* (P_DKREL, HA HA HE HU: DUCK's release; 0 ~210 ms; nothing else reads it) */
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DRIVE", F_PCT, 0, 127, 0),       /* (Dah: DIST's drive; its IN gain the track's extras') */
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* the OP ENV / OP LEVEL values of DIGITAL (ids 61..80): with FELUCCA_FM4 its operator envelopes; without it
 * inert, on no page and in no editor layout, read only when a DIGITAL sound converts (fm4_convert.c). Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_CHRD] = PE("CHRD", N_CHRD, 0),
    [P_VOIC] = PE("VOIC", N_VOIC, 0),
/* the DRUM engine's lane levels (eng_drum.c, #97): 100 % = the kit as designed (the sound before 1.1) */
#define LANE(k, l) [P_LN0 + (k)] = PD(l, F_PCT, 0, 127, 127)
    LANE(0, "KICK"), LANE(1, "SNARE"), LANE(2, "CLAP"), LANE(3, "HATCL"),
    LANE(4, "HATOP"), LANE(5, "TOM"), LANE(6, "RIM"), LANE(7, "BELL"),
#undef LANE
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),
    [P_LPOL] = PE("POL", N_LPOL, 0),
    [P_SQNT] = PE("QNTZ", N_QNT, 1),              /* ON: every step on its start, as before 1.2; 2..4 a strength */
    [P_SPRD] = PD("SPRD", F_PCT, 0, 127, 0),      /* #148: 0 = every voice on PAN, as before (fx.c mix_spread) */
    /* the INSERT (1.5, fx.c track_insert): TYPE OFF = as before. A B C as the editor protocol names them; the device
     * shows the type's own labels (ins_desc) */
    [P_ITYPE] = PE("INSRT", N_ITYPE, 0),
    [P_IA] = PD("INS A", F_PCT, 0, 127, 64),
    [P_IB] = PD("INS B", F_PCT, 0, 127, 96),
    [P_IC] = PD("INS C", F_PCT, 0, 127, 96),
    [P_IMIX] = PD("MIX", F_PCT, 0, 127, 127),
    [P_FTYPE] = PE("TYPE", N_FTYPE, 0),           /* 1.5 (#104): ANALOG's filter, LP = as before (eng_analog.c) */
    [P_ESYNC] = PE("ESYNC", N_ONOFF, 0),          /* 1.5 (#175): OFF = ATK DEC REL in ms, as before (voice.c esync) */
    [P_FXORD] = PE("ORDER", N_FXORD, 0),          /* D I S: as before */
    [P_DKSRC] = PE("SRC", N_DKSRC, 0),            /* OFF: as before (Dah: SIDECHAIN's source) */
    [P_DKAMT] = PD("DEPTH", F_PCT, 0, 127, 100),
    /* Dah: the EQ (fx_eq.c): LOW CUT OFF (0) / 32 Hz .., HIGH CUT .. 10.2 kHz / OFF (118), the bells' FREQ and GAIN
     * (-15 .. +15 dB, 0: off); param_format names them */
    [P_EQLO] = PD("LOCUT", F_CUTOFF, 0, 118, 0),
    [P_EQHI] = PD("HICUT", F_CUTOFF, 0, 118, 118),
    [P_EQ1F] = PD("FREQ1", F_CUTOFF, 0, 118, 40),
    [P_EQ1G] = PD("GAIN1", F_BIPCT, -64, 63, 0),
    [P_EQ2F] = PD("FREQ2", F_CUTOFF, 0, 118, 90),
    [P_EQ2G] = PD("GAIN2", F_BIPCT, -64, 63, 0),
    [P_EQ3G] = PD("GAIN3", F_BIPCT, -64, 63, 0),
    [P_EQ3F] = PD("FREQ3", F_CUTOFF, 0, 118, 110),
    [P_DOUT] = PD("OUT", F_BIPCT, -64, 63, 0),      /* (Dah) DIST's output gain (param_format: dB) */
    [P_DCOL] = PD("COLOR", F_BIPCT, -64, 63, 0),    /* .. its tone: darker / brighter */
};

/* the INSERT's A B C as its TYPE means them (TP's ranges and defaults): the drives DRIVE TONE LEVEL, CRUSH BITS RATE
 * LPF, the swept ones RATE DEPTH FDBK; OFF TP's own */
#define PU(l, f, df, n, u) {l, f, 0, 127, df, n, u}
static const param_desc_t INS_DESC[3][3] = {
    {PU("DRIVE", F_PCT, 64, 0, 0), PU("TONE", F_CUTOFF, 96, 0, 0), PU("LEVEL", F_DB, 96, 0, 0)},
    {PU("BITS", F_INT, 64, N_IBITS, 0), PU("RATE", F_INT, 96, N_IRATE, "Hz"), PU("LPF", F_CUTOFF, 96, 0, 0)},
    {PU("RATE", F_LFOHZ, 64, 0, 0), PU("DEPTH", F_PCT, 96, 0, 0), PU("FDBK", F_PCT, 96, 0, 0)},
};
#undef PU
static const param_desc_t *ins_desc(int32_t type, uint32_t k)
{
    if (type <= 0 || type >= (int32_t)NELEM(N_ITYPE))
        return &TP[P_IA + k];
    return &INS_DESC[type < 5 ? 0 : type == 5 ? 1 : 2][k];   /* (IT_SOFT .. IT_FUZZ, IT_CRUSH, the swept ones) */
}

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),     /* cents; edited on MENU > AUDIO TUNE (1.2: menu_items.c MI_TUNE) */
    [G_DTIME] = PE("TIME", N_DTIME, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    /* G_MIDI, G_SYNC, G_INFO: the old SYSTEM page's (gone in 1.2), on no page: MIDI's value was never read (its card
     * showed the USB state), SYNC was a placeholder, CPU a reading; the USB state and the CPU load are MENU > SYSTEM >
     * INFO's now (ui_menu.c). Kept: the ids and G_COUNT are fixed by the formats and the protocol */
    [G_MIDI] = PE("MIDI", N_MIDI_INPUT, 0),
    [G_SYNC] = PE("SYNC", N_DASH, 0),
    /* MIDI IN's routing, edited on MENU > MIDI > MIDI IN (1.2: menu_items.c MI_MIDIIN; up to 1.1.5 GLO > SYSTEM ROUT),
     * stored with the project as before. (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_ROUTE] = PE("ROUT", N_ROUTE, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    /* the reverb's model on the REVERB page: the id of the old GM drum channel (G_DRCH, inert since 1.0) */
    [G_RTYPE] = PE("TYPE", N_RTYPE, 2),         /* HALL at power-on since 1.2 (ROOM before; projects keep theirs) */
    /* inert: they set the GM drum part (level, reverb send), which is gone (drums are the DRUM engine
     * on any part). On no page; kept so the ids and G_COUNT, which the project format and the
     * editor protocol depend on, do not move */
    [G_DRLVL] = PD("COMP", F_PCT, 0, 127, 0),       /* (Dah) the MASTER's COMPRESSOR: AMOUNT (0 off), RELEASE */
    [G_DRREV] = PD("REL", F_PCT, 0, 127, 50),
};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    if (id >= P_IA && id <= P_IC)                     /* the INSERT's values as its TYPE names them */
        return ins_desc(t->p[P_ITYPE], id - P_IA);
    if (t->p[P_ESYNC] && (id == P_ATK || id == P_DEC || id == P_REL))   /* ENV SYNC: note values (1.5, #175) */
        return &ESYNC_DESC[id == P_ATK ? 0 : id == P_DEC ? 1 : 2];
    return &TP[id];
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a retired F_ENUM value kept as an alias, so stored values stay valid: SAMPLE SET and GRAIN SRC 1, once
 * TRANH, and 4, once PERC (a SAMPLE sound of it loads as DRUM: core.h drum_from_perc), play PIANO
 * (tools/gen_samples.py SMP_SET_ORIG); DRUM KIT 1..3, once HAND CYM H+CYM, play 66 10 77 (eng_drum.c DK_PLAYS).
 * It shows the original's name; knobs step over it and the editor's SET lands on the original. -> the value v
 * stands for */
static int32_t enum_orig(const param_desc_t *d, int32_t v)
{
    if (d->names == N_DRUM_KIT)
        return v >= 0 && v < DK_COUNT ? (int32_t)DK_PLAYS[v] : v;
    return d->names == SMP_ALL_NAMES && v >= 0 && v < SMP_NSETS ? SMP_SET_ORIG[v] : v;
}

/* a stored value as the parameter takes it (projects, user presets, motion): inside d's range, and a retired
 * DRUM KIT (1..3) as the kit it plays, so KIT never holds one again. (SAMPLE / GRAIN's aliases keep their
 * number: they are what the sound was saved with, and play the original anyway.) FM6's SLOT is never stored as a
 * bank slot: 9..40 (1.4.1's B1..B32; 1.0.2's B1..B27 were 8..34) is 8, OWN, and the patch comes with what is loaded */
static int32_t param_fit(const param_desc_t *d, int32_t v)
{
    v = clamp(v, d->min, d->max);
    if (d->names == N_FM6_PATCH)
        return v > (int32_t)FM6_OWN ? (int32_t)FM6_OWN : v;
    return d->names == N_DRUM_KIT ? enum_orig(d, v) : v;
}

/* v is a value a knob passes over: an alias, or one of FM6's bank slots without a voice */
static int enum_skip(const param_desc_t *d, int32_t v)
{
    return enum_orig(d, v) != v || (d->names == N_FM6_PATCH && !fm6_slot_ok(v));
}
/* a knob moved an F_ENUM from `from` to v: past any value it skips in that direction; with none left to the end, the
 * last one it does not skip on the way from `from` (else `from`) */
static int32_t enum_step(const param_desc_t *d, int32_t from, int32_t v)
{
    int32_t dir = v > from ? 1 : -1, w = v;
    while (w != from && enum_skip(d, w))
        w = w + dir > d->max || w + dir < d->min ? from : w + dir;
    while (w == from && v != from && enum_skip(d, v))
        v -= dir;
    return w == from ? v : w;
}

/* #48: the note divisions in the order of their length, longest first (triplets between their neighbours), on the
 * knobs and the gauges; the stored values (N_DIV, N_SLDIV indices: projects, presets, the editor protocol) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t SLDIV_ORDER[6] = {0, 3, 1, 4, 2, 5};              /* 1/8 8T 1/16 16T 1/32 32T */
static const uint8_t DTIME_ORDER[13] = {9, 8, 7, 6, 12, 0, 10, 1, 11, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4D 1/4 1/8D
                                                                                      * 1/8 1/16D 8T 1/16 16T 1/32 */
static const uint8_t *enum_order(const param_desc_t *d)
{
    return d->names == N_DIV ? DIV_ORDER : d->names == N_SLDIV ? SLDIV_ORDER : d->names == N_DTIME ? DTIME_ORDER : 0;
}
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order, past aliases (enum_step) */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    if (d->names == ESYNC_NAMES) {                    /* ENV SYNC: a note value a step (the first value showing it) */
        int32_t k = clamp((int32_t)esync_idx(v) + steps, 0, ESYNC_N - 1);
        return (k * 128 + ESYNC_N - 1) / ESYNC_N;
    }
    return enum_step(d, v, clamp(v + steps, d->min, d->max));
}

static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    if ((d == &TP[P_EQLO] && v <= 0) || (d == &TP[P_EQHI] && v >= 118)) {   /* (Dah) the EQ's cuts at rest (a band at
                                                         * 0 dB: its cards, ui_draw.c eq_band_off) */
        str_cpy(val, "OFF", 6);
        return;
    }
    if (d == &TP[P_EQ1G] || d == &TP[P_EQ2G] || d == &TP[P_EQ3G] || d == &TP[P_DOUT]) {         /* (Dah) the bells' GAIN: dB, a tenth */
        int32_t t = v * 150 / 64;
        fmt_fix(val + (t > 0), t, 1);
        if (t > 0)
            val[0] = '+';
        *unit = "dB";
        return;
    }
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:                                   /* -64..63 as -100..+100 % (1.5.5, #199: each side over its own
                                                     * range; up to 1.5.1 / 64 both ways, the top read +98) */
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 63);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK };   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_MOD, GR_PATS, GR_SONG, GR_TOOLS, GR_CHORD, GR_SLICES, GR_EVENTS,
       GR_FMOP, GR_FMOP2, GR_FMENV, GR_INS, GR_DLANE, GR_DLANE2, GR_DSYN, GR_DSYN2, GR_DSYN3, GR_DSYN4, GR_DSYN5,
       GR_LDRAW, GR_FILE, GR_PATTERN };   /* (GR_PATTERN: a track's patterns, ui_pattern.c) */   /* (HA HA HE HU: LFO DRAW, ui_lfodraw.c; FILE, the projects: projfs.c) */   /* (GR_DLANE..: DRUM's lane
                                 * pages, ui_drumlane.c; GR_DSYN..: a SYN lane's layer set) */   /* (GR_STEPS: SEQ TOOLS' knobs; GR_FMOP..: FM6's operator pages, ui_fm6op.c) */

#define PS_TRK 0x80u                     /* (Dah) on a global page: this id is a track parameter (the selected track's) */
typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ESYNC}},   /* (P_ED_FX: nothing reads it);
                                                                             * 1.5: ESYNC, ENV's times as note values */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, 0xFF}},   /* 1.2: SYNC TRIG POL */
    {"LFO DRAW", FAM_LFO, SC_TRACK, GR_LDRAW, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (HA HA HE HU) POINT VALUE SHAPE SMOOTH */
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, {0xFF, P_M1SRC, P_M1DST, P_M1AMT}},   /* KNOB 1: the slot (mod_ui_slot) */
    /* (Dah) FX: CHAIN first, its effects as a list (KNOB 1 the row, KNOB 2 its type, KNOB 3 moves it; OCT+ its page, or
     * adds one; REC takes it out); then a page each, its own picture live (ui_graph_fx.c). (FX's overview page and its
     * assignable knobs, P_FXK1..4, are gone: the parameters stay, their default the STEP lanes' DST CHO DLY REV) */
    {"CHAIN", FAM_FX, SC_TRACK, GR_NONE, {P_FXORD, 0xFF, 0xFF, 0xFF}},
    {"DIST", FAM_FX, SC_TRACK, GR_NONE, {0xFF, P_DIST, P_DCOL, P_DOUT}},   /* (Dah) IN (trkx din) DRIVE COLOR OUT */
    {"INSERT", FAM_FX, SC_TRACK, GR_INS, {P_ITYPE, P_IA, P_IB, P_IC}},   /* 1.5: the track's INSERT (fx.c) */
    {"INSERT 2", FAM_FX, SC_TRACK, GR_INS, {P_IMIX, 0xFF, 0xFF, 0xFF}},  /* .. its dry / wet */
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},
    {"EQ", FAM_FX, SC_TRACK, GR_NONE, {P_EQLO, P_EQHI, P_EQ1F, P_EQ1G}},   /* (Dah) the EQ (fx_eq.c): its spectrum */
    {"EQ 2", FAM_FX, SC_TRACK, GR_NONE, {P_EQ2F, P_EQ2G, P_EQ3F, P_EQ3G}},   /* BAND 2, BAND 3 */
    {"SIDECHAIN", FAM_FX, SC_TRACK, GR_NONE, {P_DKSRC, P_DKAMT, P_DKREL, 0xFF}},   /* (HA HA HE HU, fx.c duck_part) */
    {"COMP", FAM_FX, SC_TRACK, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (Dah) the COMPRESSOR: AMOUNT RELEASE (trkx, fx_comp.c) */
    /* (Dah) the send effects, as the others: the track's SEND (PS_TRK: a track parameter on a global page) and the
     * buses' own settings, shared by every track (were GLO's) */
    {"CHORUS", FAM_FX, SC_GLOBAL, GR_NONE, {PS_TRK | P_CHOR, G_CRATE, G_CDEPTH, 0xFF}},
    {"DELAY", FAM_FX, SC_GLOBAL, GR_NONE, {PS_TRK | P_DLY, G_DTIME, G_DFDBK, G_DCOLOR}},
    {"DELAY 2", FAM_FX, SC_GLOBAL, GR_NONE, {G_DMIX, 0xFF, 0xFF, 0xFF}},
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, {PS_TRK | P_REV, G_RTYPE, G_RSIZE, G_RDAMP}},   /* TYPE: ROOM / SPRING / HALL */
    {"SONG", FAM_GLO, SC_GLOBAL, GR_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},        /* GLO's first (1.2; up to 1.1.5 SEQ's); HA HA HE HU: then
                                                                               * the global FX, DLY REVERB CHORUS (FX: the track's) */
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, {P_CHRD, P_VOIC, 0xFF, 0xFF}},   /* SCL again: the chord keys (chord.c) */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"FILTER", FAM_EDIT, SC_TRACK, GR_NONE, {P_FTYPE, P_E4, P_E5, P_E7}},   /* ANALOG only (1.5, #104): TYPE CUT RES KTR */
    {"LANES", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN0, P_LN1, P_LN2, P_LN3}},   /* DRUM only: the lane levels */
    {"LANES 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_LN4, P_LN5, P_LN6, P_LN7}},
    {"DESIGN", FAM_EDIT, SC_TRACK, GR_DLANE, {0xFF, 0xFF, 0xFF, 0xFF}},     /* DRUM only: KNOB 1 the lane, then its */
    {"DESIGN 2", FAM_EDIT, SC_TRACK, GR_DLANE2, {0xFF, 0xFF, 0xFF, 0xFF}},  /* VOICE TUNE DECAY / TONE SNAP STACK */
    {"SYN TONE", FAM_EDIT, SC_TRACK, GR_DSYN, {0xFF, 0xFF, 0xFF, 0xFF}},    /* the lane picked plays SYN 1..5: */
    {"SYN TONE 2", FAM_EDIT, SC_TRACK, GR_DSYN2, {0xFF, 0xFF, 0xFF, 0xFF}}, /* KNOB 1 the lane, then its layer set's */
    {"SYN NOISE", FAM_EDIT, SC_TRACK, GR_DSYN3, {0xFF, 0xFF, 0xFF, 0xFF}},  /* LEVEL PITCH DECAY / BEND SWEEP SHAPE / */
    {"SYN NOISE 2", FAM_EDIT, SC_TRACK, GR_DSYN4, {0xFF, 0xFF, 0xFF, 0xFF}},   /* LEVEL CUT DECAY / FILTER CLAP SPACE */
    {"SYN METAL", FAM_EDIT, SC_TRACK, GR_DSYN5, {0xFF, 0xFF, 0xFF, 0xFF}},  /* / LEVEL TONE DECAY (HA HA HE HU) */
    {"SLICES", FAM_EDIT, SC_TRACK, GR_SLICES, {0xFF, 0xFF, 0xFF, 0xFF}},   /* SLICE only: the slices by hand (ui_slice.c) */
    {"OPERATOR", FAM_EDIT, SC_TRACK, GR_FMOP, {0xFF, 0xFF, 0xFF, 0xFF}},    /* FM6 only: KNOB 1 the operator, then its */
    {"OP ENV", FAM_EDIT, SC_TRACK, GR_FMENV, {0xFF, 0xFF, 0xFF, 0xFF}},     /* bytes of the track's patch (ui_fm6op.c): */
    {"OPERATOR 2", FAM_EDIT, SC_TRACK, GR_FMOP2, {0xFF, 0xFF, 0xFF, 0xFF}}, /* CRS FINE LEVEL / STG RATE LEVEL / MODE DTUN VEL */
    {"OP1 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"OP2 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"OP3 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"OP4 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"OP LEVEL", FAM_EDIT, SC_TRACK, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"VOICE 3", FAM_EDIT, SC_TRACK, GR_NONE, {P_SPRD, 0xFF, 0xFF, 0xFF}},   /* #148: SPREAD */
    /* 1.2 (Discussion #153): a family's pages go round in this order, a tap of its button each (ui.c open_family) */
    /* SAVE's taps (1.5, #173): USER (SAVE from another page: the quick save, SAVE OCT+ OCT+), PRESETS (the engines and
     * sounds one tap on; up to 1.4.1 the last), PHRASES, PROJECT, TOOLS */
    {"FILE", FAM_SAVE, SC_GLOBAL, GR_FILE, {0xFF, 0xFF, 0xFF, 0xFF}},       /* (HA HA HE HU) SAVE's first: the projects
                                                                             * (projfs.c): the projects */
    {"PATTERN", FAM_SAVE, SC_TRACK, GR_PATTERN, {0xFF, 0xFF, 0xFF, 0xFF}},  /* (HA HA HE HU) the selected track's patterns:
                                                                             * PATTERN, OPEN, COPY, NEW (ui_pattern.c) */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"PHRASES", FAM_SAVE, SC_GLOBAL, GR_PATS, {0xFF, 0xFF, 0xFF, 0xFF}},    /* pattern loader: PAT LOAD (ui.c pat_load);
                                                                             * the tap after PRESETS (up to 1.1.5 SEQ's) */
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_TOOLS, {G_CLRSEQ, G_INITSND, 0xFF, 0xFF}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"DETAIL", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},   /* 1.5.1 (#199): the cursor step's CHANCE RATCH NUDGE VEL
                                                             * (ui.c detail_on, ui_input.c sd_turn) */
    {"AUTOMATION", FAM_SEQ, SC_TRACK, GR_EVENTS, {0xFF, 0xFF, 0xFF, 0xFF}},  /* the locks, events, CHANCE and RATCH as a
                                                                             * list, PLAY and CLEAR (ui_events.c) */
    /* HOME's pages (1.2): HOME tapped on HOME the MIXER (up to 1.1.5 GLO's; LEVEL PAN REV MUTE), again HOME (ui.c
     * home_tap; Dah: CLOCK gone, its SYNC and SWING MENU > AUDIO's, the tempo OCT+ held + SELECT) */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
/* (Dah) the parameter a page's knob c edits: FX's what P_FXK1..4 name, the others their own */
static uint32_t fx_knob_id(const track_t *t, uint32_t c)
{
    static const uint8_t ID[4] = {P_DIST, P_CHOR, P_DLY, P_REV};   /* (the STEP lanes': DST CHO DLY REV) */
    (void)t;
    return ID[c & 3u];
}
static uint32_t page_id(const page_t *pg, uint32_t c)
{
    return pg->graph == GR_FX && pg->scope == SC_TRACK ? fx_knob_id(TSEL, c) : pg->id[c & 3u];
}
static uint8_t mod_ui_slot;      /* the MOD page: the matrix slot (0..3) KNOB 2..4 edit */

static track_t *fx_tsel(void);                         /* (Dah) ui.c: FX's track, or the MASTER (fx.c mtrk) */
static uint8_t mx_master;                               /* (ui.c: the MASTER picked) */
static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->id[slot];
    track_t *ft = pg->fam == FAM_FX ? fx_tsel() : TSEL;
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->fam == FAM_FX && pg->scope == SC_GLOBAL && (pg->id[0] & PS_TRK)) {   /* (Dah) CHORUS DELAY REVERB: on the
                                                         * MASTER (their returns) their own settings, from KNOB 1
                                                         * (DELAY's MIX the fourth); on a track its send alone */
        static const uint8_t RET[3][4] = {{G_CRATE, G_CDEPTH, 0xFF, 0xFF}, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX},
                                          {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF}};
        uint32_t k = pg->id[0] == (PS_TRK | P_CHOR) ? 0u : pg->id[0] == (PS_TRK | P_DLY) ? 1u : 2u;
        if (mx_master)
            id = RET[k][slot & 3u];
        else if (slot)
            id = 0xFFu;
        if (id == 0xFFu) {
            *valp = 0;
            return 0;
        }
    }
    if (pg->scope == SC_GLOBAL && (id & PS_TRK)) {   /* (Dah) a send effect's SEND: the track's */
        *valp = &ft->p[id & ~PS_TRK];
        return track_desc(ft, id & ~PS_TRK);
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    if (pg->graph == GR_MOD)                          /* SRC DST AMT of the slot shown */
        id += 3u * mod_ui_slot;
    if (pg->graph == GR_FX)                           /* (Dah) FX: what its knobs drive */
        id = fx_knob_id(TSEL, slot);
    *valp = &ft->p[id];
    return track_desc(ft, id);
}
