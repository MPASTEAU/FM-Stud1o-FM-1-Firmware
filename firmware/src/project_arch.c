/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; HA HA HE HU fork */
/* HA HA HE HU: PROJECTS, a project is a whole piece of music: its sounds, its four patterns (the PATTERN slots A..D,
 * storage.c OBJ_PROJECT0.. and their FUNX / FUNY), the music on the tracks and the SONG. The patterns' slots are the
 * project open now; SAVE > FILE keeps ARCH_N projects in what was user sample slot USR2 (eng_sample.c, 0xB4000..0xC7FFF:
 * ARCH_SECT sectors each), as one record:
 *   sector 0, 0..255: the header (magic, the payload's length and FNV-1a, the name, the header's own FNV), written last:
 *   a save cut short leaves no header (the project reads empty; the one before it is gone too: one copy, the device's
 *   flash has no room for two)
 *   from 256: the payload: version, name, globals, the selected track, the SONG; the SOUNDS (each track's parameters,
 *   engine and preset, its FM6 patch, its extras, its MIDI IN channel); then the patterns A..D (each: there or not)
 *   and the music on the tracks (W), each as its differences from the SOUNDS (the parameters that differ, a patch or
 *   extras of its own), its steps that are not empty (index + FUN10's 9 bytes), its automation and locks, its name.
 * A project of four full patterns on eight tracks may not fit (ARCH_SIZE): the save is refused before anything is
 * erased (a dry run counts it first). OPEN writes the patterns back to the slots A..D (each its three objects) and puts
 * the music on the tracks (project_restore_runtime); NEW clears the slots and the tracks (project_new). Main loop only,
 * transport stopped (a flash erase silences the audio). Included by project.c (at its end). */
#define ARCH_N 4u
#define ARCH_SECT 5u
#define ARCH_SIZE (ARCH_SECT * ST_SECTOR)
#define ARCH_BASE 0xB4000u                         /* (USR2) */
#define ARCH_PAY 256u                              /* the payload's offset in a project's area */
#define ARCH_MAGIC 0x4A525046u                     /* "FPRJ" */
#define ARCH_VER 1u
#define ARCH_HDR (P_COUNT + 2u)                    /* a track's sound: its parameters (+64), engine, preset */
#define ARCH_TRK (ARCH_HDR + NSTEP * 9u)           /* proj_pack_trk's bytes */
#define ARCH_CUR (favorites.factory[15][16])       /* the project open (1..ARCH_N), 0 none: kept with the settings */
_Static_assert(ARCH_BASE + ARCH_N * ARCH_SIZE <= 0xC8000u, "the projects in USR2");
typedef struct {
    uint32_t magic, len, sum;
    uint8_t ver, rsv[3];
    char name[PROJ_NAME_LEN];
    uint32_t hsum;
} arch_hdr_t;
static struct {                                    /* the SOUNDS while a project is written or read */
    uint8_t hdr[NTRK][ARCH_HDR];
    uint8_t fm6[NTRK][FM6_PACKED];
    trkx_t x[NTRK];
    uint8_t mch[NTRK];
} arch_snd __attribute__((section(".pool")));
static uint8_t arch_trk[ARCH_TRK] __attribute__((section(".pool")));   /* a track's bytes */
static struct {                                    /* the stream: written (dry: counted) or read, 256 bytes at a time */
    uint32_t base, pos, sum, max;
    uint8_t dry, err, buf[256];
} arw;

static uint32_t arch_off(uint32_t k) { return ARCH_BASE + (k % ARCH_N) * ARCH_SIZE; }
static uint32_t arch_fnv(uint32_t h, const uint8_t *p, uint32_t n)
{
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

/* ------------------------------------------------------------------ writing --- */
static void arw_flush(void)
{
    uint32_t n = arw.pos & 255u;
    if (!n || arw.dry || arw.err)
        return;
    if (st_prog(arw.base + ARCH_PAY + (arw.pos - n), arw.buf, n))
        arw.err = 1;
}
static void arw_put(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--) {
        if (arw.pos >= arw.max) {
            arw.err = 2;                            /* (too big) */
            return;
        }
        arw.sum = (arw.sum ^ *b) * 16777619u;
        arw.buf[arw.pos & 255u] = *b++;
        arw.pos++;
        if (!(arw.pos & 255u) && !arw.dry && !arw.err && st_prog(arw.base + ARCH_PAY + arw.pos - 256u, arw.buf, 256u))
            arw.err = 1;
    }
}
static void arw_b(uint32_t v) { uint8_t b = (uint8_t)v; arw_put(&b, 1); }
static const uint8_t *arch_empty_step(void)        /* an empty step's 9 bytes (proj_pack_trk's) */
{
    static uint8_t e[9];
    static proj_trk_t t;
    static int ok;
    if (!ok) {
        memset(&t, 0, sizeof t);
        t.step[0].time = ST_REST;
        (void)proj_pack_trk(arch_trk, &t);
        memcpy(e, arch_trk + ARCH_HDR, 9);
        ok = 1;
    }
    return e;
}
/* project q as one pattern: its differences from the SOUNDS, its steps, automation, name */
static void arch_put_pattern(const project_t *q)
{
    uint32_t t, i, n;
    const uint8_t *e = arch_empty_step();
    for (t = 0; t < NTRK; t++) {
        if (!proj_pack_trk(arch_trk, &q->t[t])) {
            arw.err = 3;
            return;
        }
        for (i = 0, n = 0; i < ARCH_HDR; i++)
            n += arch_trk[i] != arch_snd.hdr[t][i];
        arw_b(n);
        for (i = 0; i < ARCH_HDR; i++)
            if (arch_trk[i] != arch_snd.hdr[t][i]) {
                arw_b(i);
                arw_b(arch_trk[i]);
            }
        n = memcmp(q->fm6[t], arch_snd.fm6[t], FM6_PACKED) != 0;
        arw_b(n | (memcmp(&q->x[t], &arch_snd.x[t], sizeof(trkx_t)) != 0) << 1);
        if (n)
            arw_put(q->fm6[t], FM6_PACKED);
        if (memcmp(&q->x[t], &arch_snd.x[t], sizeof(trkx_t)))
            arw_put(&q->x[t], sizeof(trkx_t));
        for (i = 0, n = 0; i < NSTEP; i++)
            n += memcmp(arch_trk + ARCH_HDR + i * 9u, e, 9) != 0;
        arw_b(n);
        for (i = 0; i < NSTEP; i++)
            if (memcmp(arch_trk + ARCH_HDR + i * 9u, e, 9)) {
                arw_b(i);
                arw_put(arch_trk + ARCH_HDR + i * 9u, 9);
            }
    }
    arw_b(q->motion.count);
    arw_b(q->motion.on);
    for (i = 0; i < q->motion.count; i++)
        arw_put(&q->motion.event[i], sizeof q->motion.event[i]);
    arw_put(q->name, PROJ_NAME_LEN);
}
/* the whole project into area k (dry: counted only); its length, 0 = failed (arw.err) */
static uint32_t arch_stream(uint32_t k, uint32_t dry, const char *name)
{
    uint32_t s, t;
    char nm[PROJ_NAME_LEN];
    memset(&arw, 0, sizeof arw);
    arw.base = arch_off(k);
    arw.max = ARCH_SIZE - ARCH_PAY;
    arw.sum = 2166136261u;
    arw.dry = (uint8_t)dry;
    memset(nm, 0, sizeof nm);
    memcpy(nm, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    arw_b(ARCH_VER);
    arw_put(nm, PROJ_NAME_LEN);
    arw_put(song.g, sizeof song.g);
    arw_b(song.sel);
    arw_put(&chain_config, sizeof chain_config);
    project_capture(&proj_scratch);                     /* the music on the tracks: the SOUNDS */
    for (t = 0; t < NTRK; t++) {
        if (!proj_pack_trk(arch_trk, &proj_scratch.t[t]))
            return 0;
        memcpy(arch_snd.hdr[t], arch_trk, ARCH_HDR);
        memcpy(arch_snd.fm6[t], proj_scratch.fm6[t], FM6_PACKED);
        arch_snd.x[t] = proj_scratch.x[t];
        arch_snd.mch[t] = proj_scratch.mch[t];
    }
    arw_put(&arch_snd, sizeof arch_snd);
    for (s = 0; s < 4u && !arw.err; s++) {               /* the patterns A..D */
        if (!project_used(s) || !proj_import(&proj_scratch, &proj_slot[s], sizeof proj_slot[s])) {
            arw_b(0);
            continue;
        }
        (void)proj_x_apply(&proj_scratch, PX_OBJ0 + s, proj_tag(&proj_slot[s]));
        arw_b(1);
        arch_put_pattern(&proj_scratch);
    }
    project_capture(&proj_scratch);                     /* the music on the tracks, last (OPEN puts it on last) */
    arch_put_pattern(&proj_scratch);
    arw_flush();
    return arw.err ? 0u : arw.pos;
}

/* ------------------------------------------------------------------ reading --- */
static int arw_get(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--) {
        if (arw.pos >= arw.max)
            return 0;
        if (!(arw.pos & 255u) && st_read(arw.base + ARCH_PAY + arw.pos, arw.buf, 256u))
            return 0;
        *b++ = arw.buf[arw.pos & 255u];
        arw.pos++;
    }
    return 1;
}
static uint32_t arw_gb(void) { uint8_t b = 0; if (!arw_get(&b, 1)) arw.err = 1; return b; }
/* area k's header: 1 = a project there (*h) */
static int arch_head(uint32_t k, arch_hdr_t *h)
{
    if (st_read(arch_off(k), h, sizeof *h) || h->magic != ARCH_MAGIC || h->ver != ARCH_VER || !h->len ||
        h->len > ARCH_SIZE - ARCH_PAY || h->hsum != arch_fnv(2166136261u, (const uint8_t *)h, (sizeof(arch_hdr_t) - 4u)))
        return 0;
    return 1;
}
static int arch_used(uint32_t k) { arch_hdr_t h; return arch_head(k, &h); }
static int arch_name(uint32_t k, char *b)          /* its name ("" none); 0 = empty */
{
    arch_hdr_t h;
    b[0] = 0;
    if (!arch_head(k, &h))
        return 0;
    proj_name_get(b, (const uint8_t *)h.name);
    return 1;
}
/* one pattern from the stream into q (the SOUNDS, then its differences) */
static int arch_get_pattern(project_t *q)
{
    uint32_t t, i, n, f;
    const uint8_t *e = arch_empty_step();
    for (t = 0; t < NTRK && !arw.err; t++) {
        memcpy(arch_trk, arch_snd.hdr[t], ARCH_HDR);
        for (i = 0; i < NSTEP; i++)
            memcpy(arch_trk + ARCH_HDR + i * 9u, e, 9);
        for (n = arw_gb(); n--;) {
            i = arw_gb();
            if (i >= ARCH_HDR) return 0;
            arch_trk[i] = (uint8_t)arw_gb();
        }
        f = arw_gb();
        memcpy(q->fm6[t], arch_snd.fm6[t], FM6_PACKED);
        q->x[t] = arch_snd.x[t];
        q->mch[t] = arch_snd.mch[t];
        if ((f & 1u) && !arw_get(q->fm6[t], FM6_PACKED)) return 0;
        if ((f & 2u) && !arw_get(&q->x[t], sizeof(trkx_t))) return 0;
        for (n = arw_gb(); n--;) {
            i = arw_gb();
            if (i >= NSTEP || !arw_get(arch_trk + ARCH_HDR + i * 9u, 9)) return 0;
        }
        if (!proj_unpack_trk(&q->t[t], arch_trk, P_COUNT, 1))
            return 0;
    }
    memset(&q->motion, 0, sizeof q->motion);
    q->motion.count = (uint8_t)arw_gb();
    q->motion.on = (uint8_t)arw_gb();
    if (q->motion.count > MOTION_MAX) return 0;
    for (i = 0; i < q->motion.count; i++)
        if (!arw_get(&q->motion.event[i], sizeof q->motion.event[i])) return 0;
    if (!arw_get(q->name, PROJ_NAME_LEN)) return 0;
    for (t = 0; t < NTRK; t++)
        for (i = 0; i < LDRAW_N; i++)
            q->x[t].draw[i] = (int8_t)clamp(q->x[t].draw[i], -64, 63);
    return !arw.err && motion_valid(&q->motion);
}

/* ------------------------------------------------------------------ the actions --- */
static int arch_busy(void)
{
    if (transport_busy() || chain_busy()) {
        ui_message("STOP FIRST");
        return 1;
    }
    return 0;
}
/* SAVE: the project as it is into area k, named name (""; the music's own). 0 done */
static int project_arch_save(uint32_t k, const char *name)
{
    arch_hdr_t h;
    uint32_t len, i;
    if (arch_busy())
        return 1;
    if (!name[0])
        name = proj_name;
    if (!arch_stream(k, 1, name)) {                     /* the dry run: it fits? */
        ui_message(arw.err == 2 ? "PROJECT TOO BIG" : "SAVE ERROR");
        return 2;
    }
    for (i = 0; i < ARCH_SECT; i++)
        if (st_erase(arch_off(k) + i * ST_SECTOR)) {
            ui_message("SAVE ERROR");
            return 2;
        }
    len = arch_stream(k, 0, name);
    if (!len) {
        ui_message("SAVE ERROR");
        return 2;
    }
    memset(&h, 0, sizeof h);
    h.magic = ARCH_MAGIC; h.len = len; h.sum = arw.sum; h.ver = ARCH_VER;
    memcpy(h.name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    h.hsum = arch_fnv(2166136261u, (const uint8_t *)&h, (sizeof(arch_hdr_t) - 4u));
    if (st_prog(arch_off(k), &h, sizeof h)) {
        ui_message("SAVE ERROR");
        return 2;
    }
    str_cpy(proj_name, name, sizeof proj_name);
    ARCH_CUR = (uint8_t)(k % ARCH_N + 1u);
    settings_later();
    proj_wire_gen++;
    ui_message("PROJECT SAVED");
    return 0;
}
/* OPEN: area k's project: its patterns into the slots A..D, its music on the tracks. 0 done */
static int project_arch_open(uint32_t k)
{
    arch_hdr_t h;
    uint32_t s, i;
    uint8_t ver, nm[PROJ_NAME_LEN];
    int16_t g[G_COUNT];
    uint8_t sel;
    if (arch_busy())
        return 1;
    if (!arch_head(k, &h)) {
        ui_message("EMPTY PROJECT");
        return 1;
    }
    memset(&arw, 0, sizeof arw);                          /* its hash, all of it, before anything is written */
    arw.base = arch_off(k);
    arw.max = h.len;
    for (i = 0; i < h.len; i++) {
        uint8_t b;
        if (!arw_get(&b, 1)) break;
        arw.sum = (i ? arw.sum : 2166136261u);
        arw.sum = (arw.sum ^ b) * 16777619u;
    }
    if (i != h.len || arw.sum != h.sum) {
        ui_message("PROJECT DAMAGED");
        return 2;
    }
    memset(&arw, 0, sizeof arw);
    arw.base = arch_off(k);
    arw.max = h.len;
    ver = (uint8_t)arw_gb();
    if (ver != ARCH_VER || !arw_get(nm, sizeof nm) || !arw_get(g, sizeof g)) goto bad;
    sel = (uint8_t)arw_gb();
    if (!arw_get(&chain_config, sizeof chain_config) || !chain_valid(&chain_config)) {
        chain_defaults(&chain_config);
        goto bad;
    }
    if (!arw_get(&arch_snd, sizeof arch_snd)) goto bad;
    for (s = 0; s < 5u; s++) {                          /* A..D, then the music on the tracks */
        project_t *q = &proj_scratch;
        uint32_t there = s < 4u ? arw_gb() : 1u;
        memset(q, 0, sizeof *q);
        q->magic = PROJ_MAGIC; q->size = sizeof *q;
        memcpy(q->g, g, sizeof q->g);
        q->sel = sel < NTRK ? sel : 0u;
        q->parts = NPART; q->phys = PROJ_PHYS;
        q->chain = chain_config;
        if (there && !arch_get_pattern(q)) goto bad;
        q->sum = proj_sum(q);
        if (s == 4u)
            break;
        if (!there) {                                   /* (an empty slot there) */
            memset(&proj_slot[s], 0, sizeof proj_slot[s]);
#if FELUCCA_FLASH
            if (flash_ok) (void)st_save(OBJ_PROJECT0 + s, &proj_wire, 0);
#endif
            continue;
        }
        if (!proj_pack(&proj_wire, q)) goto bad;
#if FELUCCA_FLASH
        if (flash_ok && st_save(OBJ_PROJECT0 + s, &proj_wire, sizeof proj_wire)) goto bad;
#endif
        memcpy(&proj_slot[s], &proj_wire, sizeof proj_wire);
        (void)proj_x_save(q, PX_OBJ0 + s, proj_tag(&proj_slot[s]));
    }
    if (project_restore_runtime(&proj_scratch)) goto bad;
    proj_name_get(proj_name, nm);
    ARCH_CUR = (uint8_t)(k % ARCH_N + 1u);
    settings_later();
    proj_wire_gen++;
    ui_message("PROJECT OPEN");
    return 0;
bad:
    proj_wire_gen++;
    ui_message("PROJECT DAMAGED");
    return 2;
}
/* DELETE: area k's header erased */
static int project_arch_delete(uint32_t k)
{
    if (arch_busy())
        return 1;
    if (st_erase(arch_off(k))) {
        ui_message("DELETE ERROR");
        return 2;
    }
    if (ARCH_CUR == k % ARCH_N + 1u) {
        ARCH_CUR = 0;
        settings_later();
    }
    ui_message("PROJECT DELETED");
    return 0;
}
/* NEW: an empty project: the slots A..D cleared, the tracks as at power-on (project_new), none open */
static int project_arch_new(void)
{
    uint32_t s;
    if (arch_busy())
        return 1;
    for (s = 0; s < 4u; s++) {
        memset(&proj_slot[s], 0, sizeof proj_slot[s]);
#if FELUCCA_FLASH
        if (flash_ok) (void)st_save(OBJ_PROJECT0 + s, &proj_wire, 0);
#endif
    }
    chain_defaults(&chain_config);
    if (project_new())
        return 2;
    chain_defaults(&chain_config);
    ARCH_CUR = 0;
    settings_later();
    proj_wire_gen++;
    return 0;
}
static uint32_t project_arch_cur(void) { return ARCH_CUR ? ARCH_CUR - 1u : 0xFFu; }
