/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (SCL > CHORD: P_CHRD, P_VOIC): one key, MIDI note or arp input plays a chord. Included by seq.c
 * after kb_map; runs in the audio ISR with the rest of the note input.
 *   CHRD OFF; DIA3 / DIA7: the triad / seventh of the track's scale (ROOT, SCALE) built on the key, every tone
 *        in key (a key outside the scale takes the scale note below it, as QNT SNAP does; CHR builds on the
 *        major scale of ROOT); MAJ MIN DOM7 MAJ7 MIN7 SUS4 POW: that shape on the key, whatever the scale.
 *   VOIC CLOSE (root position); OPEN (the second tone an octave up: 1-5-3); INV1 / INV2 (the lowest tone, then
 *        the next, an octave up); +OCT (the root an octave down added; a seventh drops its fifth). At most
 *        CHORD_MAX notes, notes outside 0..127 left out.
 * The key is mapped first (kb_map: QNT SNAP / WHITE, TRN, the octave), the chord is built on the note it gives.
 * MONO / LEGATO / UNISON parts play the chord's root only; a kit (the DRUM engine, slices: an
 * engine that maps the keys itself) ignores CHRD. Every source keeps the notes it started (kb_chord for a
 * key, mchord for a MIDI note) and its release ends exactly those, so CHRD / VOIC changed while it is held
 * leave nothing hanging. A note several sources hold sounds once and ends with the last of them. */
enum { CH_OFF, CH_DIA3, CH_DIA7, CH_MAJ, CH_MIN, CH_DOM7, CH_MAJ7, CH_MIN7, CH_SUS4, CH_POW,
       CH_COLOR, CH_MATCH };                         /* (Dah: COLOR and MATCH, appended) */
enum { VC_CLOSE, VC_OPEN, VC_INV1, VC_INV2, VC_BASS, VC_AUTO };
/* Dah:
 *   CHRD COLOR  the scale's chord on the key as DIA7 does, each degree its usual colour: the tonic add9 (not a seventh),
 *               the dominant (the fifth degree) a 7 with its flat ninth in a minor key (the raised third: harmonic),
 *               the others their seventh
 *   CHRD MATCH  the keys held together make the chord: one key, its note alone; two or more, the chord they belong to
 *               (the least notes added; its root among them, the lowest best; in the track's scale best) completed by
 *               the key pressed last with the tones missing, each placed nearest the notes held (chord_match)
 *   VOIC AUTO   voice leading: of the chord's inversions an octave either way, the one nearest the chord before (each
 *               tone to its nearest, the bass moving less), close position for the first */
#define CHORD_MAX 4u
#define MCHORD_N 24u                     /* MIDI notes held as chords at once (more: their root alone) */

static const int8_t CHORD_SHAPE[CH_POW - CH_MAJ + 1][CHORD_MAX] = {
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1},
};

static uint8_t kb_chord[27][CHORD_MAX], kb_chn[27];   /* the notes key k started (kb_chn 0: none) */
typedef struct { uint8_t id, ch, src, n, note[CHORD_MAX]; } mchord_t;   /* id: track + 1, 0 = free */
static mchord_t mchord[MCHORD_N];
/* the last chord played per track (the CHORD page shows it): its root, notes, and the shape's tones above the
 * root (bit i = i semitones, 0..11) */
static struct { uint8_t root, n, note[CHORD_MAX]; uint16_t mask; uint8_t gen; } chord_last[NTRK];

/* 1 = the track's keys are a kit: no chords */
static int chord_kit(const track_t *t)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    return e->oneshot || (e->keys && e->keys(t, 0) >= 0);
}

/* the tones of the chord (semitones above *root, ascending, iv[0] = 0) -> their number; DIA may move *root
 * down onto the scale */
static uint32_t chord_tones(const track_t *t, uint32_t mode, int32_t *root, int32_t *iv)
{
    uint32_t n = 0, i;
    if (mode >= CH_MAJ && mode <= CH_POW) {
        for (i = 0; i < CHORD_MAX && CHORD_SHAPE[mode - CH_MAJ][i] >= 0; i++)
            iv[n++] = CHORD_SHAPE[mode - CH_MAJ][i];
        return n;
    }
    {
        uint32_t mask = scale_mask(t), deg[12], c = 0, at = 0, guard = 12;
        int32_t r = t->p[P_ROOT];
        if (mask == 0xFFFu)
            mask = SCALE_MASK[1];                       /* CHR: no key of its own, the major scale of ROOT */
        while (guard-- && !((mask >> (uint32_t)((*root - r + 120) % 12)) & 1u))
            (*root)--;                                  /* outside the scale: the scale note below */
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (i == (uint32_t)((*root - r + 120) % 12))
                    at = c;
                deg[c++] = i;
            }
        for (i = 0; i < (mode == CH_DIA3 ? 3u : 4u); i++) {   /* every other note of the scale */
            uint32_t k = at + 2u * i;
            iv[n++] = (int32_t)deg[k % c] + 12 * (int32_t)(k / c) - (int32_t)deg[at];
        }
        if (mode == CH_COLOR && n == 4u) {              /* (Dah) each degree its colour */
            int minor = (mask >> 3) & 1u && !((mask >> 4) & 1u);   /* (the key's third) */
            if (at == 0u) {                             /* the tonic: add9 */
                uint32_t k = at + 8u;
                iv[3] = (int32_t)deg[k % c] + 12 * (int32_t)(k / c) - (int32_t)deg[at];
            } else if (deg[at] == 7u && minor) {        /* the minor key's dominant: 7 b9 (harmonic) */
                iv[1] = 4; iv[2] = 10; iv[3] = 13;
            }
        }
        return n;
    }
}

/* the chord on note `root` as track t plays it now -> out (ascending), the number of notes (1..CHORD_MAX);
 * *rp its root, *maskp its tones above the root (bit i: i semitones; 0 = no chord). The root alone when CHRD
 * is OFF, on a kit, or for MONO / LEGATO / UNISON (their chord's root) */
static uint32_t chord_make(const track_t *t, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    int32_t iv[CHORD_MAX + 1u], r = (int32_t)root, x;
    uint32_t mode = (uint32_t)t->p[P_CHRD], n, i, j, m = 0;
    uint16_t mask = 0;
    *rp = r;
    *maskp = 0;
    if (!mode || mode > CH_COLOR || chord_kit(t)) {   /* (MATCH: chord_match, by the key) */
        out[0] = (uint8_t)root;
        return 1;
    }
    n = chord_tones(t, mode, &r, iv);
    for (i = 0; i < n; i++)
        mask |= (uint16_t)(1u << (iv[i] % 12));
    *rp = r;
    *maskp = mask;
    if (trk_vmode(t) != V_POLY) {                       /* one voice: the root */
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    switch (t->p[P_VOIC]) {
    case VC_OPEN:                                       /* 1-5-3(-7): the second tone an octave up */
        if (n >= 3u)
            iv[1] += 12;
        break;
    case VC_INV2:
    case VC_INV1:
        for (j = 0; j < (t->p[P_VOIC] == VC_INV2 ? 2u : 1u); j++) {   /* the lowest tone an octave up */
            x = iv[0] + 12;
            for (i = 1; i < n; i++)
                iv[i - 1u] = iv[i];
            iv[n - 1u] = x;
        }
        break;
    case VC_AUTO: {                                     /* (Dah) voice leading: the inversion nearest the chord before */
        uint32_t k = trk_index(t) % NTRK, pn = chord_last[k].n, inv, best = 0, bo = 0;
        int32_t bc = 0x7FFFFFFF, o;
        if (pn < 2u || n < 2u)
            break;
        for (inv = 0; inv < n; inv++)
            for (o = -12; o <= 12; o += 12) {
                int32_t cost = 0, lo = 1000;
                for (i = 0; i < n; i++) {
                    int32_t v = r + iv[i] + (i < inv ? 12 : 0) + o, d = 1000;
                    for (j = 0; j < pn; j++) {
                        int32_t e = v - (int32_t)chord_last[k].note[j];
                        e = e < 0 ? -e : e;
                        d = e < d ? e : d;
                    }
                    cost += d;
                    lo = v < lo ? v : lo;
                }
                cost += (lo > (int32_t)chord_last[k].note[0] ? lo - chord_last[k].note[0] : chord_last[k].note[0] - lo) / 2;
                if (cost < bc) {
                    bc = cost;
                    best = inv;
                    bo = (uint32_t)(o + 12);
                }
            }
        for (i = 0; i < n; i++)
            iv[i] += (i < best ? 12 : 0) + (int32_t)bo - 12;
        break;
    }
    case VC_BASS:                                       /* the root an octave down; a seventh drops its fifth */
        if (n == CHORD_MAX) {
            iv[2] = iv[3];
            n--;
        }
        for (i = n; i > 0; i--)
            iv[i] = iv[i - 1u];
        iv[0] = -12;
        n++;
        break;
    default:
        break;
    }
    for (i = 1; i < n; i++)                             /* ascending */
        for (j = i; j > 0 && iv[j - 1u] > iv[j]; j--) {
            x = iv[j];
            iv[j] = iv[j - 1u];
            iv[j - 1u] = x;
        }
    for (i = 0; i < n && m < CHORD_MAX; i++) {          /* inside 0..127, each note once */
        x = r + iv[i];
        if (x < 0 || x > 127 || (m && out[m - 1u] == (uint8_t)x))
            continue;
        out[m++] = (uint8_t)x;
    }
    if (!m) {
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    return m;
}

/* (Dah) MATCH: the chords it knows (tones above the root, a bit each), the simplest first */
static const uint16_t MATCH_T[] = {
    0x091u, 0x089u, 0x491u, 0x891u, 0x489u, 0x0A1u, 0x085u, 0x049u, 0x449u, 0x111u, 0x095u, 0x08Du, 0x291u, 0x289u,
};   /* maj min 7 maj7 m7 sus4 sus2 dim m7b5 aug add9 madd9 6 m6 */
/* key `key` pressed on track t with MATCH: the notes the other keys of t sound now and it make the chord; out: the key's
 * note, then the tones missing (each near the notes held). The number of notes */
static uint32_t chord_match(track_t *t, uint32_t key, uint8_t *out)
{
    uint32_t j, i, held[32], nh = 0, pcs, lo = key, scale = scale_mask(t), best_r = 0, best_m = 0, n = 1, k = trk_index(t);
    int32_t best = 0x7FFFFFFF, sum = (int32_t)key;
    out[0] = (uint8_t)key;
    for (j = 0; j < 27u; j++)                           /* the notes the other keys of the track sound */
        if (kb_chn[j] && kb_trk[j] == k)
            for (i = 0; i < kb_chn[j] && nh < 32u; i++) {
                held[nh++] = kb_chord[j][i];
                sum += kb_chord[j][i];
                lo = kb_chord[j][i] < lo ? kb_chord[j][i] : lo;
            }
    if (!nh || chord_kit(t) || trk_vmode(t) != V_POLY)
        return 1;                                       /* (one key: its note alone) */
    pcs = 1u << (key % 12u);
    for (i = 0; i < nh; i++)
        pcs |= 1u << (held[i] % 12u);
    for (j = 0; j < 12u; j++)                           /* each root, each chord: all the notes in it, the fewest added */
        for (i = 0; i < NELEM(MATCH_T); i++) {
            uint32_t m = ((uint32_t)MATCH_T[i] << j | (uint32_t)MATCH_T[i] >> (12u - j)) & 0xFFFu, x, c;
            int32_t sc;
            if ((pcs & m) != pcs)
                continue;
            for (x = m & ~pcs, c = 0; x; x &= x - 1u)
                c++;
            sc = (int32_t)c * 10 + (int32_t)i;          /* (the simpler chord first) */
            if (!((pcs >> j) & 1u))
                sc += 8;                                /* its root not played */
            else if (j != lo % 12u)
                sc += 2;                                /* its root not the lowest */
            if (scale != 0xFFFu && (m & ~(scale << ((uint32_t)t->p[P_ROOT] % 12u) | scale >> (12u - (uint32_t)t->p[P_ROOT] % 12u)) & 0xFFFu))
                sc += 3;                                /* out of the track's scale */
            if (sc < best) {
                best = sc;
                best_r = j;
                best_m = m;
            }
        }
    if (best == 0x7FFFFFFF)
        return 1;                                       /* (no chord holds them: the note alone) */
    {
        int32_t mid = sum / (int32_t)(nh + 1u);
        uint32_t miss = best_m & ~pcs;
        for (j = 0; j < 12u && n < CHORD_MAX; j++)      /* the missing tones, each its octave nearest the notes held */
            if ((miss >> j) & 1u) {
                int32_t v = mid - ((mid - (int32_t)j) % 12 + 12) % 12;
                if (mid - v > 6)
                    v += 12;
                if (v >= 0 && v <= 127)
                    out[n++] = (uint8_t)v;
            }
    }
    chord_last[k % NTRK].root = (uint8_t)(lo - (lo % 12u) + best_r);   /* (the CHORD page: its name) */
    chord_last[k % NTRK].mask = (uint16_t)(((best_m >> best_r) | (best_m << (12u - best_r))) & 0xFFFu);
    chord_last[k % NTRK].n = (uint8_t)n;
    for (i = 0; i < CHORD_MAX; i++)
        chord_last[k % NTRK].note[i] = i < n ? out[i] : 0u;
    chord_last[k % NTRK].gen++;
    return n;
}

/* chord_make for a note played now: a chord is kept in chord_last (the CHORD page shows it) */
static uint32_t chord_build(track_t *t, uint32_t root, uint8_t *out)
{
    int32_t r;
    uint16_t mask;
    uint32_t m = chord_make(t, root, out, &r, &mask), i, k = trk_index(t);
    if (m > 1u) {
        chord_last[k].root = (uint8_t)clamp(r, 0, 127);
        chord_last[k].mask = mask;
        chord_last[k].n = (uint8_t)m;
        for (i = 0; i < CHORD_MAX; i++)
            chord_last[k].note[i] = i < m ? out[i] : 0u;
        chord_last[k].gen++;
    }
    return m;
}

/* a chord's name from its root and tones (bit i: i semitones above the root): "C", "Cm7", "F#m7b5", "Gsus4",
 * "A5" -> b (12 bytes) */
static void chord_name(char *b, uint32_t root, uint32_t mask)
{
    uint32_t mi = (mask >> 3) & 1u, ma = (mask >> 4) & 1u, b5 = (mask >> 6) & 1u, p5 = (mask >> 7) & 1u;
    uint32_t s5 = (mask >> 8) & 1u, d7 = (mask >> 9) & 1u, m7 = (mask >> 10) & 1u, M7 = (mask >> 11) & 1u;
    const char *q, *x = "";
    str_cpy(b, N_NOTE[root % 12u], 12);
    if (mi && !ma && b5 && !p5)
        q = d7 ? "dim7" : m7 ? "m7b5" : "dim";
    else if (ma && !mi && s5 && !p5)
        q = m7 ? "+7" : M7 ? "+M7" : "aug";
    else if (ma && !mi)
        q = m7 ? "7" : M7 ? "maj7" : d7 ? "6" : "";
    else if (mi)
        q = m7 ? "m7" : M7 ? "mM7" : d7 ? "m6" : "m";
    else if ((mask >> 5) & 1u) {                        /* no third: suspended */
        q = m7 ? "7" : "";
        x = "sus4";
    } else if ((mask >> 2) & 1u) {
        q = m7 ? "7" : "";
        x = "sus2";
    } else
        q = "5";
    str_cpy(b + str_len(b), q, 12 - str_len(b));
    str_cpy(b + str_len(b), x, 12 - str_len(b));
}

/* the MIDI note (ch, src) of track id - 1 that plays a chord, 0 = none */
static mchord_t *mchord_of(uint32_t ch, uint32_t src, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id && mchord[i].ch == ch && mchord[i].src == src)
            return &mchord[i];
    return 0;
}

/* a MIDI-held chord of track id - 1 sounds note */
static int mchord_held(uint32_t id, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id)
            for (j = 0; j < mchord[i].n; j++)
                if (mchord[i].note[j] == note)
                    return 1;
    return 0;
}

static void mchord_forget(uint32_t track)               /* panic: the track's MIDI chords are gone */
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == track + 1u)
            mchord[i].id = 0;
}
