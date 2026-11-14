/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots. The slots live in .noinit RAM: they
 * survive resets and UBOOT entry. With FELUCCA_FLASH (default) every save
 * also goes to flash through storage.c, and an
 * empty RAM slot is filled from flash on load.
 *
 * Format 7 ("FUN7", written) stores P_COUNT (byte 66) and maps an older count as user presets do: FUN7 of
 * 89 parameters (before the chord keys P_CHRD / P_VOIC) loads its engine values at today's P_E0..P_E7,
 * the chord keys OFF / CLOSE, and its motion events' ids from its P_E0 on move up with them (proj_motion_ids).
 * 68 + 4 x (91 + 2 + 64 x 9) + chain + motion = 3040 of the 3372 bytes before the name: 332 spare, room for
 * 83 more track parameters (4 bytes each).
 * Format 6 ("FUN6") adds a 36-byte song chain before the checksum.
 * Format 5 ("FUN5", read only) = format 4 with the drum grid: a step is 10 bytes (step_t: its lane hits and
 * their accents after the 8 bytes it was), and each track has 40 reserved bytes (lane[8][5], written 0: room
 * for per-lane sounds of the DRUM engine). 4 ("FUN4": PROJ_NP_V4 parameters, the modulation matrix), 3
 * ("FUN3": PROJ_NP_V3, the SLICER), 2 ("FUN2") and 1 ("FUN1": PROJ_NP_V2) are read and converted, mapped by
 * count as user presets are (the first np - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the
 * parameters added since take their defaults: the SLICER OFF, every matrix slot OFF). Their steps get no
 * hits; on a DRUM track their notes that are a lane's note become its hits (proj_grid: the same notes and
 * velocities play, the grid shows them as its own).
 * Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines
 * added since (SLICE 8, ..) were appended, no index moved.
 *
 * Track 4 was the GM drum part until 1.0 (no engine: its byte 0; its level and reverb send in the
 * globals G_DRLVL / G_DRREV, which are inert now). A project says which it has in `parts`: NPART
 * when written since, 0 before (a reserved byte, always written 0: the format and its size did not
 * change). proj_drums_to_part turns such a track 4 into a DRUM part with its default kit (the GM map, so
 * its drum steps still play drums; until 1.0.2 it was the SAMPLE engine's PERC set), keeping its steps, its
 * pattern and mix parameters (LEN DIV SWING GATE, PAN MUTE), its SLICER, and the drum level and reverb send
 * as LEVEL and REV.
 *
 * The byte `phys` (reserved, always 0, before 1.0) says what a PHYS track's MODEL means: 0 MODEL 2 was
 * DUST (dropped: it loads as MODAL bowed, eng_phys.c phys_legacy); 1 MODEL 4 was DRUM (the kit is the DRUM
 * engine since: such a track loads as DRUM, core.h drum_from_phys); 2 (PROJ_PHYS) as today. proj_phys.
 *
 * Format 8 ("FUN8", written since 1.0) = FUN7 with each track's FM6 patch (eng_fm6.c, the 128-byte packed
 * record, 4 x 128 bytes just before the name): a project is self-contained. On load an FM6 track's SLOT shows F n
 * when its patch is that factory one, else OWN (eng_fm6.c fm6_adopt; a stored 9..40, the B slots of the patch bank
 * before 1.0.3 or of 1.4.1's voice bank, is OWN too: the project has the patch).
 * It is 3584 bytes (FUN7: 3388); FUN7 is read (its tracks get the init patch). The retained cache (proj_slot,
 * .noinit) grew with it: after an update its slot 1 still starts with a FUN7 record, which is read; the other
 * slots fail their hash and come back from flash (persist_boot).
 *
 * Format 9 ("FUN9", written since 1.1) = FUN8 with 64 more bytes: the DRUM lane levels (P_LN0..P_LN7, #97) made
 * P_COUNT 99 (P_E0 91), and 68 + 4 x (99 + 2 + 576) + chain + motion = 3072 no longer fit before FUN8's patches
 * (3056). 3648 bytes, the patches at PROJ_FM6_OFF (3120): 48 spare, room for 12 more track parameters. FUN8 (3584,
 * its 91 parameters mapped by count: the lane levels 100 %) and FUN7 are read; the retained cache grew again (its
 * slot 1 still starts with the older record, read as above).
 *
 * Format 10 ("FUNA", written since 1.2; FUN10 in the docs) = FUN9 laid out the same way, 3840 bytes (a whole storage
 * object, storage.c ST_PAYLOAD_MAX), for four changes made together:
 *   - P_COUNT 104 (P_E0 96): LFO 2's SYNC TRIG POL, QUANTIZE and SPREAD (P_LSYNC .. P_SPRD) before the engine
 *     values. SPREAD (#148) came before FUN10's release, into the spare bytes below: a FUN10 of a 1.2 build before
 *     it holds 103 (byte 66) and loads by count (SPREAD 0, its engine values and their motion moved up by one);
 *   - 128 motion records (core.h MOTION_MAX, 64 before), 3 bytes each (place, id, the value as an int8_t: the values
 *     were always -64..127): the block is 4 + 384 = 388 bytes (FUN9's 260 held 64 of 4 bytes);
 *   - each step's NUDGE (core.h step_nudge, -8..+7) in bit 7 of its four note bytes (bit j in note j), bits every
 *     earlier format wrote 0: the step stays 9 bytes;
 *   - the song's sections (core.h chain_row_t): a slot per track (A..D or "-") and the repeats, 5 bytes a row: the
 *     chain is 4 + 16 x 5 = 84 bytes (FUN6..FUN9: 36, rows {slot, repeat}, chain_v9_t; they load as sections with the
 *     four tracks on the row's slot, song_chain.c chain_from_v9: the same song).
 * 68 + 4 x (104 + 2 + 576) + chain + motion = 3268; the patches at PROJ_FM6_OFF (3312): 44 spare, room for 11 more
 * track parameters. 1.5 took seven of them (no format change, FUN10 as it was): the INSERT (P_ITYPE .. P_IMIX, 96..100),
 * ANALOG's filter TYPE (P_FTYPE, 101) and ENV SYNC (P_ESYNC, 102), before the engine values (P_COUNT 111, P_E0 103; 16
 * spare, room for 4). A FUN10 of 1.2 .. 1.4 (104) loads by count: the INSERT OFF, TYPE LP, ENV SYNC OFF (as they
 * played), its engine values and their motion moved up by seven.
 * Firmware 1.2 .. 1.4 refuses a FUN10 of more parameters than its own (np > P_COUNT): a 1.5 project shows EMPTY there. FUN9 / FUN8 / FUN7 load as before (their 64 records of 4 bytes read by proj_motion_v9, every
 * step's nudge 0: the timing they always had; parameters mapped by count, the four new ones at their defaults:
 * SYNC OFF, TRIG NOTE, POL BI, QUANTIZE ON, SPREAD 0, which play as before 1.2), and so do FUN6..FUN1. Firmware before 1.2
 * refuses a FUN10 everywhere (its size and magic are no format it knows): a slot holding one shows EMPTY there (the
 * flash copy stays until that slot is saved over), a backup PUT of one is refused (rc 1), the retained RAM copy of
 * one fails its checks.
 *
 * Parameter locks (1.1, core.h MOTION_LOCK) are motion records with bit 7 of their id byte set (P_COUNT 99 < 128:
 * the bit is free): no byte moved for them, and every project before 1.1 has none. FUN9 holds them. A FUN8 / FUN7
 * record of a 1.1 development build may hold some: on load their ids move to today's positions as the others do,
 * the lock bit kept (proj_motion_ids). Firmware before 1.1 refuses a project that holds a lock (an id out of its
 * range), as with RATCH below; FUN9 it refuses anyway (a newer format).
 *
 * A step's RATCH (core.h SF_RATCH, the hits - 1, 0..3) is in bit 7 of its velocity byte (bit 0) and of its chance
 * byte (bit 1) of FUN7 / FUN8: bits every firmware before wrote 0, so every older project loads with its steps x1,
 * and no format or size changed. Firmware before RATCH refuses a project that holds a ratchet (those bytes out
 * of their range), as it would a newer format. FUN6..FUN1 have none (x1).
 *
 * DIGITAL (engine 1) was retired in 1.0 (fm4_convert.c): a track of it, in any format, loads as FM6 with the
 * patch converted from its values as the track's own (proj_fm4, on every import; the stored record keeps what it
 * holds until saved again). Its motion events on the EDIT or OP ENV values are dropped.
 *
 * SAMPLE's SET 4, PERC (the GM drum kit), was retired after 1.0.2: a SAMPLE track that selects it, in any
 * format, loads as the DRUM engine with its default kit (core.h drum_from_perc: the same GM key map, its steps
 * as they are, the rest of its sound kept; proj_perc, on every import as proj_fm4). Its motion events on the
 * EDIT values are dropped (SAMPLE's meanings, not DRUM's).
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#define PROJ_MAGIC 0x46554E41u                 /* "FUNA" (FUN10): 104 (1.5: 111) parameters, 128 motion records, nudges */
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* NPART; 0: track 4 is the old GM drum part (see the top) */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;
    proj_trk_t t[NTRK];
    chain_config_t chain;
    motion_store_t motion;
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t mch[NTRK];                         /* 1.5.5: MIDI IN TRACK's channels, as song.mch (PROJ_MIDI_OFF) */
    trkx_t x[NTRK];                            /* the tracks' extras (LFO DRAW, CURVE: the FUNY object) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
    uint32_t sum;
} project_t;
/* Serialized FUN7 keeps the retained cache's exact extent. Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 16 bytes of the
 * reserved tail were left for parameters added later. FUN9: the same, 3648 bytes (see the top). */
#define PROJ_STORE_SIZE 3840u                  /* FUN10: a whole storage object (storage.c ST_PAYLOAD_MAX) */
#define PROJ_NAME_OFF (PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN)
#define PROJ_FM6_OFF (PROJ_NAME_OFF - NTRK_V10 * FM6_PACKED)
/* 1.5.5 (#202): MIDI IN TRACK, the last 3 of FUN10's 16 spare bytes (just before the patches: the parameters a later
 * version adds grow from the other end, 13 bytes, 3 more track parameters). 24 bits LE: bits 5k..5k+4 track k's channel
 * as song.mch holds it (0..15 its distance up from channel k + 1, 16 OMNI, 17 OFF; 18..31 read as 0), bit 23 set: MIDI IN
 * is TRACK. Then the globals' G_ROUTE holds the block TRACK falls back to (proj_route_fallback: the block its four
 * channels make, else CH1-4), which is all a firmware before 1.5.5 reads (these bytes it never did): a TRACK project plays
 * there as that block. Every project before has them 0: channels 1..4, MIDI IN as its G_ROUTE says */
#define PROJ_MIDI_OFF (PROJ_FM6_OFF - 3u)
typedef union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE]; } project_store_t;
_Static_assert(G_COUNT == 27u, "FUN7 globals retain original IDs");
_Static_assert(sizeof(project_store_t) == 3840u, "FUN10 size");
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (format 1) */
project_store_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q &&
    q->sum == proj_sum(q) && chain_valid(&q->chain) && motion_valid(&q->motion); }

/* tracks 5..8 of a project stored with four (every format up to FUN10, a FUNX missing or stale): as at
 * power-on (their sounds, no steps: PROJ_DEF_SOUND, project_restore_runtime), silent in every SONG section, on
 * channels 5..8, the init FM6 patch; their motion records none (the caller's store had none) */
static void proj_x_defaults(project_t *q)
{
    uint32_t i, k;
    for (i = NTRK_V10; i < NTRK; i++) {
        proj_trk_t *d = &q->t[i];
        for (k = 0; k < P_COUNT; k++)
            d->p[k] = param_desc_of(trk_def_engine(i), k)->def;
        d->engine = (uint8_t)trk_def_engine(i);
        d->preset = PROJ_DEF_SOUND;
        for (k = 0; k < NSTEP; k++) {
            memset(&d->step[k], 0, sizeof d->step[k]);
            d->step[k].time = ST_REST;
        }
        memcpy(q->fm6[i], FM6_INIT, FM6_PACKED);
        q->mch[i] = 0;
        for (k = 0; k < CHAIN_ROWS; k++)
            q->chain.row[k].slot[i] = CHAIN_SILENT;
    }
    for (i = 0; i < NTRK; i++)                          /* (the extras of every track: their FUNY, when there is one) */
        trkx_defaults(&q->x[i]);
}

/* every track's engine one this firmware has: a project of another number is not one */
static int proj_engines_ok(const project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (q->t[t].engine >= NENGINES)
            return 0;
    return 1;
}
static int proj_unpack(project_t *q, const uint8_t *b);
/* n bytes of a stored project (FUN10, FM Stud1o's only one: the older formats and their conversions are gone, no
 * FM Stud1o user has a file of them) -> q; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    return n == PROJ_STORE_SIZE && ((const uint32_t *)b)[0] == PROJ_MAGIC && proj_unpack(q, b) && proj_engines_ok(q);
}

/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* MIDI IN TRACK (PROJ_MIDI_OFF): G_ROUTE TRACK is stored as the block its channels make (0 1 2 3 + 4n: CH1-4 .. CH13-16),
 * else CH1-4, and the TRACK bit */
static uint32_t proj_route_fallback(const uint8_t *mch)
{
    uint32_t k;
    for (k = 1; k < NTRK_V10; k++)
        if (mch[k] != mch[0])
            return 0;
    return mch[0] % 4u || mch[0] >= 16u ? 0u : mch[0] ? mch[0] / 4u + 1u : 0u;
}
static void proj_midi_pack(uint8_t *b, const project_t *q)
{
    uint32_t k, v = 0;
    for (k = 0; k < NTRK_V10; k++)
        v |= (uint32_t)(q->mch[k] < MCH_N ? q->mch[k] : 0u) << (5u * k);
    if (q->g[G_ROUTE] == ROUTE_TRACK) {
        int16_t r = (int16_t)proj_route_fallback(q->mch);
        memcpy(b + 8 + 2u * G_ROUTE, &r, 2);
        v |= 1u << 23;
    }
    b[PROJ_MIDI_OFF] = (uint8_t)v; b[PROJ_MIDI_OFF + 1u] = (uint8_t)(v >> 8); b[PROJ_MIDI_OFF + 2u] = (uint8_t)(v >> 16);
}
static void proj_midi_unpack(project_t *q, const uint8_t *b)
{
    uint32_t k, v = b[PROJ_MIDI_OFF] | (uint32_t)b[PROJ_MIDI_OFF + 1u] << 8 | (uint32_t)b[PROJ_MIDI_OFF + 2u] << 16, f;
    for (k = 0; k < NTRK_V10; k++) {
        f = (v >> (5u * k)) & 31u;
        q->mch[k] = (uint8_t)(f < MCH_N ? f : 0u);
    }
    if (v >> 23)
        q->g[G_ROUTE] = ROUTE_TRACK;
}

/* motion records (today's, eight tracks) <-> FUN10's (four: place a byte, track << 6 | step): those of
 * tracks base..base + 3 (to; their PLAY bits too), or all of a FUN10 as tracks base.. (from, appended to m) */
static void motion_to_v10(motion_store_v10_t *o, const motion_store_t *m, uint32_t base)
{
    uint32_t i, n = 0;
    memset(o, 0, sizeof *o);
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        uint32_t k = (uint32_t)m->event[i].place >> 6;
        if (k < base || k >= base + NTRK_V10)
            continue;
        {   /* (Dah: the ids as PROJ_NP_W counts them; the parameters it does not store, none) */
            uint32_t id = MOTION_ID(&m->event[i]);
            if ((id >= PROJ_NP_W - 8u && id < P_E0) || m->event[i].param == MOTION_FINE || m->event[i].param == MOTION_BEND)
                continue;                              /* (FINE LEN: FUN10 has none, whole steps there) */
            if (id >= P_E0)
                id -= P_E0 - (PROJ_NP_W - 8u);
            o->event[n].param = (uint8_t)((m->event[i].param & MOTION_LOCK) | id);
        }
        o->event[n].place = (uint8_t)((k - base) << 6 | (m->event[i].place & 63u));
        o->event[n++].value = m->event[i].value;
    }
    o->count = (uint8_t)n;
    o->on = (uint8_t)((m->on >> base) & ((1u << NTRK_V10) - 1u));
}
static int motion_from_v10(motion_store_t *m, const motion_store_v10_t *o, uint32_t base)
{
    uint32_t i;
    if (o->count > MOTION_MAX || (o->on & ~((1u << NTRK_V10) - 1u)))
        return 0;
    for (i = 0; i < o->count; i++) {
        if (m->count >= MOTION_MAX)
            return 0;
        m->event[m->count].place = (uint16_t)(((uint32_t)(o->event[i].place >> 6) + base) << 6 | (o->event[i].place & 63u));
        m->event[m->count].param = o->event[i].param;
        m->event[m->count++].value = o->event[i].value;
    }
    m->on |= (uint8_t)(o->on << base);
    return 1;
}

/* one track's parameters, engine, preset and steps as FUN10 packs them (np bytes of biased values, then the 64 steps of
 * 9 bytes): at b, the bytes written; 0 = a value FUN10 cannot hold */
static uint32_t proj_pack_trk(uint8_t *b, const proj_trk_t *t)
{
    uint32_t pos = 0, i;
    for (i = 0; i < PROJ_NP_W; i++) {                  /* (Dah: up to P_DKAMT, then the engine's: params_by_count's) */
        int32_t v = t->p[i < PROJ_NP_W - 8u ? i : P_E0 + i - (PROJ_NP_W - 8u)];
        if (v < -64 || v > 127) return 0;
        b[pos++] = (uint8_t)(v + 64);
    }
    b[pos++] = t->engine; b[pos++] = t->preset;
    for (i = 0; i < NSTEP; i++) {
        const step_t *s = &t->step[i];
        uint32_t r = step_ratchet(s) - 1u;          /* RATCH: bit 7 of the velocity and chance bytes (see the top) */
        uint32_t nd = (uint32_t)step_nudge(s) & 15u;   /* NUDGE (FUN10): bit j in bit 7 of note j */
        if (s->n > 4u || st_kind(s) > ST_REST || (s->flags & ~(3u | SF_RATCH | SF_NUDGE)) || s->probability >= COND_FIRST + NCOND)
            return 0;
        for (uint32_t j = 0; j < 4u; j++)           /* what proj_unpack checks, so a saved project always loads: */
            b[pos++] = (uint8_t)((s->note[j] > 127u ? 127u : s->note[j]) | (nd >> j & 1u) << 7);   /* notes and
                                                     * velocity 0..127, accents */
        b[pos++] = (uint8_t)(s->n | st_kind(s) << 3 | (s->flags & 3u) << 5);   /* only on hits (overlaps: FUN10 has no
                                                     * notes going on, each starts on its step) */
        b[pos++] = (uint8_t)((s->vel > 127u ? 127u : s->vel) | (r & 1u) << 7); b[pos++] = s->hit;
        b[pos++] = s->acc & s->hit;
        b[pos++] = (uint8_t)(s->probability | (r >> 1) << 7);
    }
    return pos;
}
/* .. back (np stored parameters, mapped by count); the bytes read, 0 = not one */
static uint32_t proj_unpack_trk(proj_trk_t *t, const uint8_t *b, uint32_t np)
{
    uint32_t pos = 0, i;
    int16_t values[P_COUNT], def[P_COUNT];
    for (i = 0; i < np; i++) { if (b[pos] > 191u) return 0; values[i] = (int16_t)b[pos++] - 64; }
    t->engine = b[pos++]; t->preset = b[pos++];
    if (t->engine >= NENGINES) return 0;
    for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(t->engine, i)->def;
    params_by_count(t->p, values, np, def);
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i]; uint32_t meta, nd = 0;
        for (uint32_t j = 0; j < 4u; j++) {         /* (FUN10: bit 7 of note j is bit j of the nudge) */
            nd |= (uint32_t)(b[pos] >> 7) << j;
            s->note[j] = b[pos++] & 127u;
        }
        meta = b[pos++];
        s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
        s->vel = b[pos] & 127u; meta |= (b[pos++] & 128u) << 1;      /* (RATCH bit 0 at bit 8 of meta) */
        s->hit = b[pos++]; s->acc = b[pos++];
        s->probability = b[pos] & 127u; meta |= (b[pos++] & 128u) << 2;   /* (bit 1 at bit 9) */
        if ((meta & 255u) > 127u || s->n > 4u || s->time > ST_REST || s->probability >= COND_FIRST + NCOND) return 0;
        if (s->acc & ~s->hit) return 0;
        step_set_ratchet(s, (meta >> 8) + 1u);
        step_set_nudge(s, (int32_t)(nd ^ 8u) - 8);
    }
    return pos;
}

/* A stable serialized schema: first header retains FUN6's fields; at byte66
 * np, format flags, then four byte-param tracks and nine-byte steps; FUN8: the patches at PROJ_FM6_OFF.
 * this is tracks 1..4 of the project, as Felucca stores it (the parts byte 4, not 0: no GM drum part); tracks
 * 5..8 are proj_pack_x's */
static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, n; uint32_t magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    chain_v10_t c10;
    motion_store_v10_t m10;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || P_COUNT > 127u) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel < NTRK_V10 ? q->sel : 0u; b[63] = q->parts ? NTRK_V10 : 0u;
    b[64] = q->phys; b[66] = PROJ_NP_W;
    for (t = 0; t < NTRK_V10; t++) {
        if (!(n = proj_pack_trk(b + pos, &q->t[t]))) return 0;
        pos += n;
    }
    chain_to_v10(&c10, &q->chain, 0);
    motion_to_v10(&m10, &q->motion, 0);
    if (pos + sizeof c10 + sizeof m10 > PROJ_MIDI_OFF) return 0;
    memcpy(b + pos, &c10, sizeof c10); pos += sizeof c10;
    memcpy(b + pos, &m10, sizeof m10);
    proj_midi_pack(b, q);
    memcpy(b + PROJ_FM6_OFF, q->fm6, NTRK_V10 * FM6_PACKED);
    {   /* the name (0-padded; stops at the first 0) */
        char nm[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, nm, proj_name_get(nm, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}

/* Tracks 5..8 ("FUNX", one storage object: storage.c OBJ_PROJX0 / OBJ_AUTOX): magic, size, tag (the hash
 * at the end of the FUN10 record it belongs to: a project saved again, renamed or erased leaves a stale X, ignored),
 * np, 3 reserved; the four tracks as FUN10's; the sections' slots of tracks 5..8 (CHAIN_ROWS x 4); their motion
 * records (FUN10's layout, tracks counted from 5); their MIDI IN channels; their FM6 patches; the hash */
#define PROJX_MAGIC 0x584E5546u               /* "FUNX" */
#define PROJX_SIZE 3840u                      /* (a whole storage object, storage.c ST_PAYLOAD_MAX) */
static uint32_t proj_tag(const project_store_t *a) { uint32_t t; memcpy(&t, a->raw + PROJ_STORE_SIZE - 4u, 4); return t; }
static int proj_pack_x(uint8_t *b, const project_t *q, uint32_t tag)
{
    uint32_t magic = PROJX_MAGIC, size = PROJX_SIZE, pos = 16u, t, i, n, sum;
    motion_store_v10_t m10;
    memset(b, 0, PROJX_SIZE);
    memcpy(b, &magic, 4); memcpy(b + 4, &size, 4); memcpy(b + 8, &tag, 4); b[12] = PROJ_NP_W;
    for (t = NTRK_V10; t < NTRK; t++) {
        if (!(n = proj_pack_trk(b + pos, &q->t[t]))) return 0;
        pos += n;
    }
    for (i = 0; i < CHAIN_ROWS; i++)
        for (t = NTRK_V10; t < NTRK; t++)
            b[pos++] = q->chain.row[i].slot[t];
    motion_to_v10(&m10, &q->motion, NTRK_V10);
    memcpy(b + pos, &m10, sizeof m10); pos += sizeof m10;
    for (t = NTRK_V10; t < NTRK; t++)
        b[pos++] = q->mch[t];
    for (t = NTRK_V10; t < NTRK; t++, pos += FM6_PACKED)
        memcpy(b + pos, q->fm6[t], FM6_PACKED);
    if (pos > PROJX_SIZE - 4u) return 0;
    sum = proj_hash(b, PROJX_SIZE - 4u); memcpy(b + PROJX_SIZE - 4u, &sum, 4);
    return 1;
}
/* n bytes of a FUNX at b for the FUN10 record tagged tag -> q's tracks 5..8 (their motion appended); 0 = none (q's
 * tracks 5..8 as proj_x_defaults sets them, its motion as it was) */
static int proj_unpack_x(project_t *q, const uint8_t *b, int n, uint32_t tag)
{
    uint32_t magic, size, tg, sum, np, pos = 16u, t, i, k, mc = q->motion.count, mo = q->motion.on;
    motion_store_v10_t m10;
    if (n != (int)PROJX_SIZE)
        return 0;
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&tg, b + 8, 4); memcpy(&sum, b + PROJX_SIZE - 4u, 4);
    np = b[12];
    if (magic != PROJX_MAGIC || size != PROJX_SIZE || tg != tag || sum != proj_hash(b, PROJX_SIZE - 4u) ||
        np < 8u || np > P_COUNT)
        return 0;
    for (t = NTRK_V10; t < NTRK; t++) {
        if (!(k = proj_unpack_trk(&q->t[t], b + pos, np))) goto bad;
        pos += k;
    }
    for (i = 0; i < CHAIN_ROWS; i++)
        for (t = NTRK_V10; t < NTRK; t++)
            q->chain.row[i].slot[t] = b[pos++];
    memcpy(&m10, b + pos, sizeof m10); pos += sizeof m10;
    if (!motion_from_v10(&q->motion, &m10, NTRK_V10)) goto bad;   /* (the store's records shared: past 128, none) */
    for (i = mc; i < q->motion.count; i++) {                       /* (its ids as FUN10's: by count) */
        uint32_t id = MOTION_ID(&q->motion.event[i]);
        if (id >= np) goto bad;
        if (np < P_COUNT && id >= np - 8u)
            q->motion.event[i].param = (uint8_t)((q->motion.event[i].param & MOTION_LOCK) | (id + P_COUNT - np));
    }
    for (t = NTRK_V10; t < NTRK; t++, pos++)
        q->mch[t] = b[pos] < MCH_N ? b[pos] : 0u;
    for (t = NTRK_V10; t < NTRK; t++, pos += FM6_PACKED)
        for (i = 0; i < FM6_PACKED; i++)
            q->fm6[t][i] = b[pos + i] & 0x7Fu;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion)) goto bad;
    q->sum = proj_sum(q);
    return 1;
bad:
    memset(&q->motion.event[mc], 0, (MOTION_MAX - mc) * sizeof q->motion.event[0]);
    q->motion.count = (uint8_t)mc;
    q->motion.on = (uint8_t)mo;
    proj_x_defaults(q);
    q->sum = proj_sum(q);
    return 0;
}
/* The tracks' extras ("FUNY", one storage object: storage.c OBJ_PROJY0 / OBJ_AUTOY): magic, size, tag (the FUN10
 * record's hash, as FUNX), the extras' size (a later firmware's longer ones: their head read), 3 reserved; NTRK trkx_t;
 * zeros; the hash */
#define PROJY_MAGIC 0x594E5546u               /* "FUNY" */
#define PROJY_SIZE 3840u
_Static_assert(16u + NTRK * sizeof(trkx_t) + 4u <= PROJY_SIZE, "FUNY");
static int proj_pack_y(uint8_t *b, const project_t *q, uint32_t tag)
{
    uint32_t magic = PROJY_MAGIC, size = PROJY_SIZE, sum;
    memset(b, 0, PROJY_SIZE);
    memcpy(b, &magic, 4); memcpy(b + 4, &size, 4); memcpy(b + 8, &tag, 4); b[12] = (uint8_t)sizeof(trkx_t);
    memcpy(b + 16, q->x, sizeof q->x);
    sum = proj_hash(b, PROJY_SIZE - 4u); memcpy(b + PROJY_SIZE - 4u, &sum, 4);
    return 1;
}
/* n bytes of a FUNY at b for the record tagged tag -> q's extras; 0 = none (q's as they were) */
static int proj_unpack_y(project_t *q, const uint8_t *b, int n, uint32_t tag)
{
    uint32_t magic, size, tg, sum, w, k, i;
    if (n != (int)PROJY_SIZE)
        return 0;
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&tg, b + 8, 4); memcpy(&sum, b + PROJY_SIZE - 4u, 4);
    w = b[12];
    if (magic != PROJY_MAGIC || size != PROJY_SIZE || tg != tag || sum != proj_hash(b, PROJY_SIZE - 4u) ||
        w < TRKX_V2 || 16u + NTRK * w + 4u > PROJY_SIZE)
        return 0;
    for (k = 0; k < NTRK; k++) {
        memset(&q->x[k], 0, sizeof q->x[k]);            /* (Dah: an older one's: tap and home 0, as before) */
        memcpy(&q->x[k], b + 16u + k * w, w < sizeof(trkx_t) ? w : sizeof(trkx_t));
        for (i = 0; i < LDRAW_N; i++)
            q->x[k].draw[i] = (int8_t)clamp(q->x[k].draw[i], -64, 63);
        q->x[k].smooth = q->x[k].smooth != 0;
        q->x[k].curve = q->x[k].curve != 0;
    }
    q->sum = proj_sum(q);
    return 1;
}

/* the motion of a FUN10 written with np parameters (np < P_COUNT) -> today's ids: the ids from that store's P_E0
 * (np - 8) on moved up with P_E0, as its values did (params_by_count); the ones below kept theirs; a lock's bit 7
 * (MOTION_LOCK) stays. 0: an id that store could not have named (np or more) */
static int proj_motion_ids(motion_store_t *m, uint32_t np)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        uint32_t id = MOTION_ID(&m->event[i]);
        if (id >= np) return 0;
        if (np < P_COUNT && id >= np - 8u)
            m->event[i].param = (uint8_t)((m->event[i].param & MOTION_LOCK) | (id + P_COUNT - np));
    }
    return 1;
}
/* a stored FUN10 (PROJ_STORE_SIZE bytes at b); then: tracks 1..4, tracks 5..8 as proj_x_defaults sets them */
static int proj_unpack(project_t *q, const uint8_t *b)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66], n;
    uint32_t end = PROJ_FM6_OFF;
    chain_v10_t c10;
    motion_store_v10_t m10;
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + PROJ_STORE_SIZE - 4u, 4);
    if (magic != PROJ_MAGIC || size != PROJ_STORE_SIZE || sum != proj_hash(b, PROJ_STORE_SIZE - 4u) ||
        np < 8u || np > P_COUNT || 68u + NTRK_V10 * (np + 2u + NSTEP * 9u) + sizeof c10 + sizeof m10 > end)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = NPART; q->phys = PROJ_PHYS;
    for (t = 0; t < NTRK_V10; t++) {
        if (!(n = proj_unpack_trk(&q->t[t], b + pos, np))) return 0;
        pos += n;
    }
    memcpy(&c10, b + pos, sizeof c10);
    if (!chain_from_v10(&q->chain, &c10)) return 0;
    pos += sizeof c10;
    memcpy(&m10, b + pos, sizeof m10);
    if (!motion_from_v10(&q->motion, &m10, 0) || !proj_motion_ids(&q->motion, np)) return 0;
    proj_midi_unpack(q, b);
    for (t = 0; t < NTRK_V10; t++)
        for (i = 0; i < FM6_PACKED; i++)
            q->fm6[t][i] = b[end + t * FM6_PACKED + i] & 0x7Fu;
    proj_x_defaults(q);
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion)) return 0;
    {
        char nm[PROJ_NAME_LEN + 1u];
        memcpy(q->name, nm, proj_name_get(nm, b + PROJ_NAME_OFF));
    }
    q->sum = proj_sum(q);
    return 1;
}

#ifndef PROJ_HOST
static project_t proj_scratch               /* decoded main-loop work, never audio ISR */
    __attribute__((section(".pool")));
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work; no retained expansion */
    project_store_t s;
    uint8_t raw[3840];                         /* (the staging of a backup object, up to a storage object: editor_backup.c) */
} proj_wire_u __attribute__((section(".pool")));
#define proj_wire (proj_wire_u.s)
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */
static uint32_t proj_wire_gen_ui(void) { return proj_wire_gen; }

static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > 4u) s[i].n = 4;
        if (st_kind(&s[i]) > ST_REST) s[i].time = ST_REST;
        s[i].time &= (uint8_t)(st_kind(&s[i]) == ST_NOTE ? 0xF3u : ST_KIND);   /* (notes going on: a NOTE step's) */
        if (st_kind(&s[i]) == ST_NOTE) st_set_cont(&s[i], st_cont(&s[i]));
        for (j = 0; j < 4u; j++) s[i].note[j] &= 127u;
        s[i].acc &= s[i].hit;
    }
}

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 3u | SF_RATCH | SF_NUDGE;
            s->vel &= 127u;
            if (s->probability >= COND_FIRST + NCOND) s->probability = 0;
        }
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)param_fit(d, q->t[t].p[i]);   /* (a retired KIT: the kit it plays) */
        }
    }
    q->sum = proj_sum(q);
}

#if FELUCCA_FLASH
/* slot from flash into RAM (format 7, or format 6 / 5 / 4 / 3 / 2 / 1 converted) */
static void proj_fetch(uint32_t slot)
{
    project_store_t *q = &proj_slot[slot & 3u];
    int n;
    proj_wire_gen++;
    n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire);
    if (!proj_import(&proj_scratch, &proj_wire, n))
        memset(q->raw, 0, 4);
    else {
        proj_bound(&proj_scratch);
        if (!proj_pack(q, &proj_scratch))
            memset(q->raw, 0, 4);
    }
}
#endif

#ifndef PROJ_FUNX
#define PROJ_FUNX 0                               /* (FUNX / FUNY written no more: projfs.c) */
#endif
#if FELUCCA_FLASH
#define PX_OBJ0 OBJ_PROJX0                       /* the FUNX objects (storage.c) */
#define PX_AUTO OBJ_AUTOX
#define PX_YOF(o) ((o) == OBJ_AUTOX ? (uint32_t)OBJ_AUTOY : OBJ_PROJY0 + ((o) - OBJ_PROJX0))   /* .. their FUNY (the extras) */
#else
#define PX_OBJ0 0u                               /* (no storage: no FUNX objects; proj_x_* do nothing) */
#define PX_AUTO 0u
#define PX_YOF(o) (o)
#endif
/* q's tracks 5..8 from the FUNX in storage object obj, when it belongs to the FUN10 record tagged tag
 * (st_buf holds it: no other buffer); 0 = none there (q as it was) */
static int proj_x_apply(project_t *q, uint32_t obj, uint32_t tag)
{
#if FELUCCA_FLASH && PROJ_FUNX                       /* (their flash is the PROJECTS' now, projfs.c) */
    st_hdr_t h;
    int r;
    if (!flash_ok)
        return 0;
    r = st_current(obj, &h) >= 0 && proj_unpack_x(q, st_buf, (int)h.len, tag);   /* (a stale X: the extras' defaults
                                                                                 * too, proj_x_defaults) */
    if (st_current(PX_YOF(obj), &h) >= 0)             /* (then the extras, st_buf free again) */
        (void)proj_unpack_y(q, st_buf, (int)h.len, tag);
    return r;
#else
    (void)q; (void)obj; (void)tag;
    return 0;
#endif
}
/* .. q's tracks 5..8 written as the FUNX of the FUN10 record tagged tag into obj (0 written, 1 failed; no flash: 0) */
static int proj_x_save(const project_t *q, uint32_t obj, uint32_t tag)
{
#if FELUCCA_FLASH && PROJ_FUNX
    if (!flash_ok)
        return 0;
    proj_wire_gen++;
    if (!proj_pack_y(proj_wire.raw, q, tag) || st_save(PX_YOF(obj), &proj_wire, PROJY_SIZE))   /* (the extras) */
        return 1;
    if (!proj_pack_x(proj_wire.raw, q, tag))
        return 1;
    return st_save(obj, &proj_wire, PROJX_SIZE) != 0;
#else
    (void)q; (void)obj; (void)tag;
    return 0;
#endif
}

static void project_capture(project_t *p)
{
    uint32_t i;
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    p->chain = chain_config;
    for (i = 0; i < NTRK; i++) {
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        fm6_pack(fm6_patch[i], p->fm6[i]);
    }
    p->motion = motion;
    {   /* (long patterns: FUN10 holds a page, LEN at most NSTEP and the first page's automation) */
        uint32_t j, n = 0;
        for (i = 0; i < NTRK; i++)
            if (p->t[i].p[P_SLEN] > (int16_t)NSTEP)
                p->t[i].p[P_SLEN] = NSTEP;
        for (j = 0; j < motion.count; j++)
            if (motion.event[j].place < (1u << 9))
                p->motion.event[n++] = motion.event[j];
        memset(p->motion.event + n, 0, (MOTION_MAX - n) * sizeof p->motion.event[0]);
        p->motion.count = (uint8_t)n;
    }
    memcpy(p->mch, song.mch, sizeof p->mch);
    memcpy(p->x, trkx, sizeof p->x);
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, p)) { ui_message("SAVE FORMAT ERROR"); return 2; }

#if FELUCCA_FLASH
    if (flash_ok) {
        if (st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire)) {
            ui_message("SAVE ERROR");
            return 2;
        }
        memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
        proj_name_get(proj_name, (const uint8_t *)p->name);
        proj_cur = (uint8_t)(slot & 3u);
        pat_from_set((slot & 3u) + 1u);
        if (proj_x_save(p, PX_OBJ0 + (slot & 3u), proj_tag(&proj_slot[slot & 3u]))) {   /* (tracks 5..8) */
            ui_message("SAVE ERROR 5-8");
            return 2;
        }
        ui_message("SAVED");
        return 0;
    }
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot & 3u);
    pat_from_set((slot & 3u) + 1u);
    ui_message("SAVED (RAM)");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""). Uses proj_scratch */
static int project_name(uint32_t slot, char *b) { return project_info(slot, b, 0, 0, 0); }
/* .. and, len48 not 0, the length of each track's pattern as SONG plays it (song_chain.c chain_tick: the clock track's
 * LEN steps of its DIV, clamped as chain_prepare does) in 1/48 quarter notes (a 4/4 bar: 192; 0 = an empty slot):
 * len48[0..NTRK-1] */
static uint32_t len48_of(int32_t len, int32_t div)
{
    uint32_t n = (uint32_t)clamp(len, TP[P_SLEN].min, TP[P_SLEN].max), d = (uint32_t)clamp(div, TP[P_SDIV].min, TP[P_SDIV].max);
    return n * (d < 6u ? 48u / DIV_DEN[d] : d < 10u ? 48u << (d - 5u) : 48u / DIV_DEN[d % 6u]);   /* (fx.c div_samples) */
}
/* SONG's picture of a pattern (ui_graph.c graph_song): its LEN steps in n = min(LEN, 16) cells (a cell: steps
 * c * LEN / n .. (c + 1) * LEN / n - 1), 2 bits a cell, cell c at bits 2c: a step weighs 0 (rest, empty, CHANCE 0),
 * 1 (a tie), 2 (a note or hit), 3 (accented); a cell's level is its steps' mean, rounded up (0..3) */
static uint32_t proj_density(const proj_trk_t *t, uint8_t *n)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), nc = len < 16u ? len : 16u, c, i, d = 0;
    for (c = 0; c < nc; c++) {
        uint32_t i0 = c * len / nc, i1 = (c + 1u) * len / nc, w = 0;
        for (i = i0; i < i1; i++) {
            const step_t *s = &t->step[i];
            if (st_kind(s) == ST_TIE)
                w += 1u;
            else if (st_kind(s) == ST_NOTE && (s->n || s->hit) && step_chance(s))
                w += (s->flags & SF_ACCENT) || s->acc ? 3u : 2u;
        }
        w = (w + (i1 - i0) - 1u) / (i1 - i0);
        d |= (w < 3u ? w : 3u) << (2u * c);
    }
    *n = (uint8_t)nc;
    return d;
}
/* .. and, den not 0, each track's picture (proj_density: den[k], its cells n[k]; 0 for an empty slot) */
static int project_info(uint32_t slot, char *b, uint16_t *len48, uint32_t *den, uint8_t *n)
{
    uint32_t k;
    b[0] = 0;
    if (len48)
        memset(len48, 0, NTRK * sizeof *len48);
    if (den) {
        memset(den, 0, NTRK * sizeof *den);
        memset(n, 0, NTRK);
    }
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t)))
        return 0;
    {   /* tracks 5..8 from the slot's FUNX, read again only when the record changed (its tag) */
        static struct {
            uint32_t tag, den[NTRK - NTRK_V10];
            uint16_t len48[NTRK - NTRK_V10];
            uint8_t ok, gen, n[NTRK - NTRK_V10];
        } xc[4];
        uint32_t s4 = slot & 3u, tag = proj_tag(&proj_slot[s4]);
        if (!xc[s4].ok || xc[s4].tag != tag || xc[s4].gen != proj_wire_gen) {   /* (gen: an X saved again under the
                                                                              * same A, its tracks 1..4 unchanged) */
            xc[s4].tag = tag;
            xc[s4].gen = proj_wire_gen;
            xc[s4].ok = 1;
            (void)proj_x_apply(&proj_scratch, PX_OBJ0 + s4, tag);
            for (k = NTRK_V10; k < NTRK; k++) {
                xc[s4].len48[k - NTRK_V10] = (uint16_t)len48_of(proj_scratch.t[k].p[P_SLEN], proj_scratch.t[k].p[P_SDIV]);
                xc[s4].den[k - NTRK_V10] = proj_density(&proj_scratch.t[k], &xc[s4].n[k - NTRK_V10]);
            }
        }
        proj_name_get(b, (const uint8_t *)proj_scratch.name);
        for (k = 0; len48 && k < NTRK; k++)
            len48[k] = k < NTRK_V10 ? (uint16_t)len48_of(proj_scratch.t[k].p[P_SLEN], proj_scratch.t[k].p[P_SDIV]) :
                       xc[s4].len48[k - NTRK_V10];
        for (k = 0; den && k < NTRK; k++) {
            if (k < NTRK_V10)
                den[k] = proj_density(&proj_scratch.t[k], &n[k]);
            else {
                den[k] = xc[s4].den[k - NTRK_V10];
                n[k] = xc[s4].n[k - NTRK_V10];
            }
        }
        return 1;
    }
    proj_name_get(b, (const uint8_t *)proj_scratch.name);
    for (k = 0; len48 && k < NTRK; k++)
        len48[k] = (uint16_t)len48_of(proj_scratch.t[k].p[P_SLEN], proj_scratch.t[k].p[P_SDIV]);
    for (k = 0; den && k < NTRK; k++)
        den[k] = proj_density(&proj_scratch.t[k], &n[k]);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    if (!proj_import(p, &proj_slot[slot & 3u], sizeof(project_store_t))) {
        ui_message("EMPTY SLOT");
        return 1;
    }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    {   /* (its tracks 5..8 go with it, tied to the renamed record) */
        uint32_t had = proj_x_apply(p, PX_OBJ0 + (slot & 3u), proj_tag(&proj_slot[slot & 3u]));
        proj_wire_gen++;
        if (!proj_pack(&proj_wire, p)) { ui_message("SAVE FORMAT ERROR"); return 2; }
#if FELUCCA_FLASH
        if (flash_ok && st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, sizeof proj_wire)) {
            ui_message("SAVE ERROR");
            return 2;
        }
#endif
        memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
        if (had && proj_x_save(p, PX_OBJ0 + (slot & 3u), proj_tag(&proj_slot[slot & 3u]))) {
            ui_message("SAVE ERROR 5-8");
            return 2;
        }
    }
    if (proj_cur == (slot & 3u))
        proj_name_get(proj_name, (const uint8_t *)p->name);
#if FELUCCA_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED (RAM)");
    return 0;
}
/* 1.5.5 (#199): a stored project erased (SAVE > PROJECT, KNOB 2 ERASE, the dialog): the slot empty. Written as a save
 * is (st_save: the other copy of the A/B pair erased and an empty record committed last, as a full backup's restore
 * writes an empty slot), so a power cut leaves the project there or the slot empty, never half of either. The music
 * playing is not touched: it keeps its name; were it loaded from or last saved to this slot it now has none (the
 * header's slot letter goes). 0 erased, 1 refused (playing, an empty slot), 2 failed (the slot as it was) */
static int project_erase(uint32_t slot)
{
    uint32_t k;
    slot &= 3u;
    if (transport_busy()) {                            /* (a flash erase silences the audio: project_save_as) */
        ui_message("STOP TO ERASE");
        return 1;
    }
    if (!project_used(slot)) {
        ui_message("EMPTY SLOT");
        return 1;
    }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + slot, &proj_wire, 0)) {
        ui_message("ERASE ERROR");
        return 2;
    }
    /* (its FUNX left as it is, no second erase: its tag no longer matches anything) */
#endif
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    if (proj_cur == slot)
        proj_cur = PROJ_NO_SLOT;
    for (k = 0; k < NTRK; k++)
        if (pat_from[k] == slot + 1u)
            pat_from[k] = 0;
#if FELUCCA_FLASH
    if (flash_ok) {
        char b[20] = "PROJECT A ERASED";
        b[8] = (char)('A' + slot);
        ui_message(b);
        return 0;
    }
#endif
    ui_message("ERASED (RAM)");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input) || !proj_engines_ok(input)) return 1;
    mom_back(0);                                  /* (ui_input.c MOMENTARY: the values as they were, then the load) */
    memset(drum_mute, 0, sizeof drum_mute);       /* (1.4.1: the DRUM lanes' mutes, performance state) */
    if (p != input) memcpy(p, input, sizeof *p);
    mst_defaults();                                     /* (Dah: the MASTER's chain: projfs.c puts a project's back) */
    chain_from_row = 0;                                 /* (SONG's GRID at the top) */
    mix_solo = perf_solo = 0;                           /* (MIXER's SOLO: the project's, none) */
    chain_from_u = 0;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    transport_req = 0;
    chain_config = p->chain;
    motion = p->motion;
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    ui.song_row = 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++)                          /* MIDI IN TRACK's channels (1.5.5; 0, channels 1..4, before) */
        song.mch[k] = (uint8_t)(p->mch[k] < MCH_N ? p->mch[k] : 0u);
    for (k = 0; k < NTRK; k++) {                        /* the extras, bounded */
        trkx[k] = p->x[k];
        for (i = 0; i < LDRAW_N; i++)
            trkx[k].draw[i] = (int8_t)clamp(trkx[k].draw[i], -64, 63);
        trkx[k].smooth = trkx[k].smooth != 0;
        trkx[k].curve = trkx[k].curve != 0;
    }
    midi_route = MIDI_ROUTE_AGAIN;                      /* (events_block: what the channels no longer hear let go) */
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine;                         /* (< NENGINES: proj_engines_ok) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range (param_fit) */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)param_fit(d, s->p[i]);
        }
        if (!(trkx[k].eq.f & EQF_4))                    /* (Dah) its EQ stored before four filters: as four */
            eq_adopt(t->p, s->p, &trkx[k].eq);
        tone_adopt(t->p, &trkx[k].eq);                  /* (Dah) .. and its four filters as BASS and TREBLE */
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset >= 0xFEu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
        {   /* the project's own FM6 patch, never reloaded from SLOT: F n if it is that factory patch, else OWN */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            fm6_set_patch(k, v);
            fm6_adopt(k);
        }
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    pat_from_set(0);                                    /* (project_load: its slot; the header's letter) */
    load_depth++;                                       /* (ui.c: these loads are inner ones) */
    for (k = 0; k < NTRK; k++) {                        /* the power-on sounds (PROJ_DEF_SOUND: NEW, tracks 5..8) */
        track_t *t = &trk[k];
        if (p->t[k].preset == PROJ_DEF_SOUND) {
            apply_preset_to(t, TRK_DEF[k][1]);
            track_defaults_steps(t);
        }
    }
    load_depth--;
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    memset(snd_said, 0, sizeof snd_said);               /* a missing sample is said again, after LOADED */
    return 0;
}

/* SAVE > FILE's NEW: an empty project, as at power-on: every track's power-on sound with no steps
 * (PROJ_DEF_SOUND), no automation, no song, the globals' defaults (the tempo kept), no name, from no slot. 1: refused */
static int project_new(void)
{
    project_t *p = &proj_scratch;
    uint32_t i;
    int16_t bpm = song.g[G_BPM];
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = GP[i].def;
    p->g[G_BPM] = bpm;
    p->parts = NPART;
    p->phys = PROJ_PHYS;
    chain_defaults(&p->chain);
    proj_x_defaults(p);                                 /* tracks 5..8, the extras */
    for (i = 0; i < NTRK_V10; i++) {                    /* .. and 1..4 the same way */
        p->t[i] = p->t[NTRK_V10];
        p->t[i].engine = (uint8_t)trk_def_engine(i);
        for (uint32_t k = 0; k < P_COUNT; k++)
            p->t[i].p[k] = param_desc_of(trk_def_engine(i), k)->def;
        memcpy(p->fm6[i], FM6_INIT, FM6_PACKED);
    }
    chain_defaults(&p->chain);
    p->sum = proj_sum(p);
    if (project_restore_runtime(p))
        return 1;
    proj_name[0] = 0;
    ui_message("NEW PROJECT");
    return 0;
}
static void project_load(uint32_t slot)
{
#if FELUCCA_FLASH
    if (flash_ok && !proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) proj_fetch(slot);
#endif
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) { ui_message("EMPTY SLOT"); return; }
    (void)proj_x_apply(&proj_scratch, PX_OBJ0 + (slot & 3u), proj_tag(&proj_slot[slot & 3u]));   /* (tracks 5..8) */
    proj_bound(&proj_scratch);                          /* a retained RAM slot of an older format: as from flash */
#if FELUCCA_FLASH
    ps_reset();                                         /* (Dah: a Felucca slot, its four tracks, not the project open's
                                                         * patterns; no project open then: autosave writes a new one) */
    song.ntrk = NTRK_V10;
    DAH_CUR = 0;
#endif
    if (!project_restore_runtime(&proj_scratch)) {
        proj_cur = (uint8_t)(slot & 3u);
        pat_from_set((slot & 3u) + 1u);
    }
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"
#if FELUCCA_FLASH && FELUCCA_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if FELUCCA_FLASH
static persist_t persist_saved __attribute__((section(".pool")));
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if FELUCCA_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n))
            persist_saved = p;
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_import(&proj_scratch, &proj_slot[i], sizeof(project_store_t)))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t)); }

/* Main loop only: no flash access or copies when the ISR changes rows. */
static uint32_t chain_prepare(void)
{
    if (transport_busy())
        return 2;
    if (!chain_valid(&chain_config) || !chain_config.count)
        return 1;
    chain.config = chain_config;                        /* (each track's patterns, pstore.c: the first two
                                                         * sections now, each next one while the one before plays) */
    if (chain_from_row >= chain.config.count)
        chain_from_row = 0, chain_from_u = 0;
    chain_fill(chain_from_row, 0);                      /* (Dah: from the GRID's section) */
    chain_fill(chain_from_row + 1u < chain.config.count ? chain_from_row + 1u : 0u, 1);   /* (one section: itself again) */
    RING_PUBLISH();
    chain.armed = 1;
    transport_req = 1;
    return 0;
}

static void settings_poll(void)
{
#if FELUCCA_FLASH
    persist_t p;
#if FELUCCA_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p)) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

/* MENU > FACTORY RESET (its dialog answered, ui_input.c): every sector Felucca keeps something in erased, then a restart,
 * the power-on of a new device: the four old project slots and the user presets' FM6 patches (0x97000..0x9FFFF, 0xFE000),
 * the PROJECTS and the user presets (0xB4000..0xDFFFF), the autosave and the FM6 voice bank (0xE5000..0xE7FFF), the
 * settings (0xFC000, 0xFD000: the MENU, the palette, the panel's table, MIDI LEARN's map, the project open). Not the
 * samples (the user slot USR1 0xA0000.., the sample pack in the image) nor the OTA staging. Stopped only (an erase
 * silences the audio); the sectors by index (/ 4096), a run each */
static __attribute__((noinline)) void factory_reset(void)
{
#if FELUCCA_FLASH
    static const uint8_t FR[8] = {0x97, 9, 0xB4, 44, 0xE5, 3, 0xFC, 3};
    uint32_t i, s;
    if (transport_busy()) {
        ui_message("STOP FIRST");
        return;
    }
    for (i = 0; i < 8u; i += 2u)
        for (s = 0; s < FR[i + 1u]; s++) {
            (void)st_erase((FR[i] + s) * ST_SECTOR);
            fm1_wdt_feed();
        }
    fm1_reboot();
#endif
}

/* The autosave (1.2, Discussion #130): the music as it is (what a project holds: every track's sound, steps, LEN DIV
 * SWING GATE, mix, the song, the automation and locks, the FM6 patches, the name) kept in its own storage object
 * (OBJ_AUTOSAVE, A/B at 0xE5000 / 0xE6000, never one of the four project slots), and put back at power-on
 * (autosave_boot) with MENU > SYSTEM > RESTORE LAST ON (the default). Flash wear: a 4 KiB erase per write, the two
 * sectors taking turns, so a write only when
 *   - something a project holds changed since the last one (autosave_sig: a hash of those values, no copy; not the
 *     selected track, nor the PROJECT page's own controls),
 *   - and it has stayed the same for AS_IDLE_MS (10 s) with the transport stopped, no key or button down, no voice
 *     sounding and no editor transfer (a backup or a sample: autosave_hold); an erase stops the audio for its
 *     ~40 ms, as every save does, so it never happens while anything plays,
 *   - and at least AS_GAP_MS (60 s) after the one before (AS_RETRY_MS after a failed write).
 * So at most one write a minute while editing, none while playing or idle: about 100,000 erase cycles per sector
 * give over 200,000 writes, a write a minute for 8 hours a day for over a year, far more with real pauses.
 * RESTORE LAST OFF writes none (and restores none). Never after a crash, a hang or a failed boot (main.c): the music
 * that was playing then could be what crashed. Checked every AS_POLL_MS; the main loop only (never the audio ISR) */
#define AS_POLL_MS 250u
#define AS_IDLE_MS 10000u
#define AS_GAP_MS 60000u
#define AS_RETRY_MS 30000u
static struct {
    uint32_t saved, seen;                        /* the signature written last (or loaded at power-on), seen last */
    uint32_t t, last, poll;                      /* fm1_ms: seen changed / busy, the last write, the last check */
    uint32_t writes;                             /* (written since power-on: the host tests count them) */
    uint8_t wrote, err;                          /* a write since power-on (last is valid); the last one failed */
} as __attribute__((section(".pool")));

static int project_save_cur(void);               /* (projfs.c, below) */
#if FELUCCA_FLASH
static int pfs_read(uint32_t id);
#endif
static uint32_t as_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

/* what an autosave would hold, hashed (no copy, no RAM): project_capture's values */
static uint32_t autosave_sig(void)
{
    uint32_t h = 2166136261u, i, j;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        for (j = 0; j < P_COUNT; j++) {
            int16_t v = motion_base_value(t, j);
            h = as_mix(h, &v, sizeof v);
        }
        h = as_mix(h, &t->eng_req, 1);
        h = as_mix(h, &t->preset, 1);
        h = as_mix(h, t->step, sizeof t->step);
        h = as_mix(h, fm6_patch[i], sizeof fm6_patch[i]);
    }
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_NAME && i != G_LOAD && i != G_SAVE)
            h = as_mix(h, &song.g[i], sizeof song.g[i]);
    h = as_mix(h, song.mch, sizeof song.mch);           /* (1.5.5: MIDI IN TRACK's channels, the project's) */
    h = as_mix(h, &motion, sizeof motion);
    h = as_mix(h, &chain_config, sizeof chain_config);
    h = as_mix(h, trkx, sizeof trkx);                   /* (the extras, the patterns, the tracks) */
    h = as_mix(h, ps_heap, ps_used);
    h = as_mix(h, ps_cur, sizeof ps_cur);
    h = as_mix(h, &song.ntrk, 1);
    return as_mix(h, proj_name, sizeof proj_name);
}

/* nothing going on that an erase (the audio stopped ~40 ms) would be heard in, or would race */
static int autosave_quiet(void)
{
    uint32_t k, i;
    if (transport_busy() || fm1_in.notes || fm1_in.buttons)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 0;
    return 1;
}

static void autosave_hold(void) { as.t = fm1_ms; }   /* the editor's transfers: their staging RAM is proj_wire */

/* main loop, every pass (after settings_poll) */
static void autosave_poll(void)
{
#if FELUCCA_FLASH
    uint32_t sig;
    if (!flash_ok || fm1_ms - as.poll < AS_POLL_MS)
        return;
    as.poll = fm1_ms;
    sig = autosave_sig();
    if (sig != as.seen || !autosave_quiet()) {
        as.seen = sig;
        as.t = fm1_ms;
        return;
    }
    if (sig == as.saved || (ui_prefs & PREF_RESTORE_OFF) || fm1_ms - as.t < AS_IDLE_MS ||
        (as.wrote && fm1_ms - as.last < (as.err ? AS_RETRY_MS : AS_GAP_MS)))
        return;
    as.last = fm1_ms;
    as.wrote = 1;
    project_capture(&proj_scratch);
    proj_wire_gen++;
    as.err = project_save_cur() != 0;               /* (the project open, saved in place; none: a new one) */
    if (!as.err) {
        as.saved = as.seen = autosave_sig();           /* (a new project's name given meanwhile: saved too) */
        as.writes++;
    }
#endif
}

/* power-on, after the defaults (main.c): the autosave becomes the music when `allowed` (no crash, hang or failed boot
 * before) and RESTORE LAST is ON; an autosave that does not load (none, torn, damaged, another format) is ignored.
 * The music now (restored or not) is what is saved: nothing is written until it changes. 1 = restored */
static int autosave_boot(int allowed)
{
    int rc = 0;
#if FELUCCA_FLASH
    if (flash_ok && allowed && !(ui_prefs & PREF_RESTORE_OFF) && pfs_cur() && pfs_there(pfs_cur())) {
        ps_reset();                                     /* (the project open last) */
        if (!pfs_read(pfs_cur())) {
            ui_message("RESTORED");
            rc = 1;
        }
    }
#else
    (void)allowed;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
    return rc;
}

#if FELUCCA_FLASH
#include "projfs.c"                               /* the PROJECTS, as many as fit (SAVE > FILE) */
#else
static uint32_t pfs_count(void) { return 0; }
static uint32_t pfs_list(uint8_t *ids) { (void)ids; return 0; }
static int pfs_there(uint32_t id) { (void)id; return 0; }
static int pfs_name(uint32_t id, char *b) { (void)id; b[0] = 0; return 0; }
static uint32_t pfs_pct(uint32_t id) { (void)id; return 0; }
static uint32_t pfs_mem_pct(void) { return 0; }
static uint32_t pfs_cur(void) { return 0; }
static int project_save_cur(void) { return 1; }
static int pfs_busy(void) { return transport_busy() || chain_busy() ? (ui_message("STOP FIRST"), 1) : 0; }
static int project_open(uint32_t id) { (void)id; ui_message("NO FLASH"); return 1; }
static int project_new_file(void) { chain_defaults(&chain_config); ps_reset(); song.ntrk = 1; song.sel = 0; return project_new(); }
static int project_copy(const char *name) { (void)name; return 1; }
static int project_rename_cur(const char *name) { str_cpy(proj_name, name, sizeof proj_name); return 0; }
static int project_delete(uint32_t id) { (void)id; return 1; }
#endif
#if FELUCCA_FLASH
_Static_assert(sizeof(project_store_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
