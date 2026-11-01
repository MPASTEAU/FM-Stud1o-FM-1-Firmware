/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* the PROJECTS, as many as fit. Each one is a whole piece of music: its tracks (song.ntrk: their sounds,
 * FM6 patches, extras, MIDI IN channels), each track's patterns (pstore.c: the one it plays and the others in the
 * heap), the SONG, the globals, its name. It is kept in what was user sample slots USR2 and USR3 (eng_sample.c,
 * PFS_NSECT sectors from PFS_BASE) as one record over as many whole sectors as it needs:
 *   the header (magic, its id 1..PFS_IDS, its generation, its sectors, the payload's length and FNV-1a, the name, the
 *   header's own FNV), written last; then the payload (pfs_stream). A project saved again is written to free sectors
 *   first, the old record erased after: a power cut leaves one of the two (the newer generation wins on a scan).
 * The project open is saved by itself while the music is stopped and nothing changes (project.c autosave_poll), on
 * NEW and OPEN too; the FILE page (ui_graph.c graph_file) lists the projects with their share of the memory. Main loop
 * only, the transport stopped (a flash erase silences the audio). Included by project.c */
#define PFS_BASE 0xB4000u                          /* (USR2 and USR3: 0xB4000 .. 0xDBFFF) */
#define PFS_NSECT 40u
#define PFS_IDS 32u
#define PFS_PAY 64u                                /* the payload's offset in the first sector */
#define PFS_MAGIC 0x4A525048u                      /* "HPRJ" */
#define PFS_VER 9u                                 /* (9: patterns longer than a page: their pages' records in the
                                                    * heap, PS_PG; written only by a project holding one, else 8; 8: eq_mc with its flags; 7: the MASTER's EQ bands (eq_mc) after its chain; 6: the MASTER's DIST IN; 5: the MASTER's COMP IN OUT too; 4: the MASTER's chain after the SONG; 3: the extras' size after P_COUNT; 2: P_COUNT after it; 1: 114
                                                    * parameters; before 3 the extras were TRKX_V2 bytes) */
#define PFS_CUR DAH_CUR                           /* the project open, its id (0 none): kept with the settings (favorites.c) */
_Static_assert(PFS_BASE + PFS_NSECT * ST_SECTOR <= 0xDC000u, "the projects in USR2 and USR3");
typedef struct {
    uint32_t magic, gen, len, sum;
    uint8_t ver, id, nsect, ntrk;
    char name[PROJ_NAME_LEN];
    uint32_t hsum;
} pfs_hdr_t;
static struct {                                    /* the scan: each id's record (start sector + 1, 0 none), the sectors */
    uint8_t ok, start[PFS_IDS + 1u], nsect[PFS_IDS + 1u];
    uint32_t gen[PFS_IDS + 1u], gmax;
    uint64_t used;
} pfs;
static struct {                                    /* the payload, written (dry: counted) or read, 256 bytes at a time */
    uint32_t base, pos, sum, max;
    uint8_t dry, err, buf[256];
} pw;

static uint32_t pfs_off(uint32_t s) { return PFS_BASE + s * ST_SECTOR; }
static uint32_t pfs_fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static int pfs_head(uint32_t s, pfs_hdr_t *h)
{
    return !st_read(pfs_off(s), h, sizeof *h) && h->magic == PFS_MAGIC && (h->ver >= 1u && h->ver <= PFS_VER) && h->id && h->id <= PFS_IDS &&
           h->nsect && s + h->nsect <= PFS_NSECT && h->len && h->len + PFS_PAY <= h->nsect * ST_SECTOR &&
           h->hsum == pfs_fnv(2166136261u, h, sizeof *h - 4u);
}
/* the projects there: each id's newest record */
static void pfs_scan(void)
{
    uint32_t s = 0, i;
    pfs_hdr_t h;
    memset(&pfs, 0, sizeof pfs);
    while (s < PFS_NSECT) {
        if (!pfs_head(s, &h)) {
            s++;
            continue;
        }
        if (!pfs.start[h.id] || h.gen > pfs.gen[h.id]) {
            pfs.start[h.id] = (uint8_t)(s + 1u);
            pfs.nsect[h.id] = h.nsect;
            pfs.gen[h.id] = h.gen;
        }
        if (h.gen > pfs.gmax)
            pfs.gmax = h.gen;
        s++;                                            /* (Dah: sector by sector: a record written inside a stale one's
                                                         * sectors, after a power cut, is found too) */
    }
    for (i = 1; i <= PFS_IDS; i++) {                    /* (Dah) two records over the same sectors (a stale header a
                                                         * power cut left): the newer one only */
        uint32_t j;
        for (j = 1; j <= PFS_IDS && pfs.start[i]; j++) {
            uint32_t a0 = pfs.start[i] - 1u, b0 = pfs.start[j] - 1u;
            if (j == i || !pfs.start[j] || a0 + pfs.nsect[i] <= b0 || b0 + pfs.nsect[j] <= a0)
                continue;
            if (pfs.gen[i] < pfs.gen[j])
                pfs.start[i] = 0;
        }
    }
    for (i = 1; i <= PFS_IDS; i++)
        if (pfs.start[i])
            pfs.used |= (((uint64_t)1 << pfs.nsect[i]) - 1u) << (pfs.start[i] - 1u);
    pfs.ok = 1;
}
static void pfs_need(void) { if (!pfs.ok) pfs_scan(); }
static int pfs_there(uint32_t id) { pfs_need(); return id && id <= PFS_IDS && pfs.start[id]; }
static uint32_t pfs_count(void)
{
    uint32_t i, n = 0;
    pfs_need();
    for (i = 1; i <= PFS_IDS; i++)
        n += pfs.start[i] != 0;
    return n;
}
static uint32_t pfs_list(uint8_t *ids)                  /* the ids there, in order */
{
    uint32_t i, n = 0;
    pfs_need();
    for (i = 1; i <= PFS_IDS; i++)
        if (pfs.start[i])
            ids[n++] = (uint8_t)i;
    return n;
}
static int pfs_name(uint32_t id, char *b)
{
    pfs_hdr_t h;
    b[0] = 0;
    if (!pfs_there(id) || !pfs_head(pfs.start[id] - 1u, &h))
        return 0;
    proj_name_get(b, (const uint8_t *)h.name);
    return 1;
}
static uint32_t pfs_pct(uint32_t id) { return pfs_there(id) ? (pfs.nsect[id] * 100u + PFS_NSECT / 2u) / PFS_NSECT : 0u; }
static uint32_t pfs_mem_pct(void)
{
    uint32_t i, n = 0;
    pfs_need();
    for (i = 0; i < PFS_NSECT; i++)
        n += (uint32_t)(pfs.used >> i) & 1u;
    return (n * 100u + PFS_NSECT - 1u) / PFS_NSECT;
}
static uint32_t pfs_cur(void) { return PFS_CUR <= PFS_IDS ? PFS_CUR : 0u; }
static uint32_t pfs_free_id(void)
{
    uint32_t i;
    pfs_need();
    for (i = 1; i <= PFS_IDS; i++)
        if (!pfs.start[i])
            return i;
    return 0;
}

/* ------------------------------------------------------------------ the payload --- */
static void pw_put(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--) {
        if (pw.pos >= pw.max) {
            pw.err = 2;
            return;
        }
        pw.sum = (pw.sum ^ *b) * 16777619u;
        pw.buf[pw.pos & 255u] = *b++;
        pw.pos++;
        if (!(pw.pos & 255u) && !pw.dry && !pw.err && st_prog(pw.base + PFS_PAY + pw.pos - 256u, pw.buf, 256u))
            pw.err = 1;
    }
}
static void pw_b(uint32_t v) { uint8_t b = (uint8_t)v; pw_put(&b, 1); }
static void pw_flush(void)
{
    uint32_t n = pw.pos & 255u;
    if (n && !pw.dry && !pw.err && st_prog(pw.base + PFS_PAY + pw.pos - n, pw.buf, n))
        pw.err = 1;
}
/* the music as it is: version, globals, the selected track, the SONG; then each of the project's tracks: its sound
 * (parameters as stored, engine, preset), its FM6 patch, its extras, its MIDI IN channel, the number of the pattern it
 * plays and that pattern (pstore.c ps_encode); then the heap's patterns of those tracks */
static uint32_t pfs_ver_w(void)                         /* the version a save writes: 9 with pages, else 8 (as before) */
{
    uint32_t at = 0;
    while (at + 4u <= ps_used) {
        if (ps_heap[at + 1u] >> 6)
            return PFS_VER;
        at += 4u + ps_rlen(at);
    }
    return 8u;
}
static void pfs_stream(void)
{
    uint32_t k, i, nt = trk_count(), at = 0;
    pw_b(pfs_ver_w());
    pw_b(P_COUNT);
    pw_b(sizeof(trkx_t));
    pw_put(song.g, sizeof song.g);
    pw_b(song.sel < nt ? song.sel : 0u);
    pw_put(&chain_config, sizeof chain_config);
    for (i = 0; i < MST_N; i++)                         /* (4) the MASTER's chain */
        pw_b((uint32_t)(mtrk.p[MST_ID[i]] + 64));
    pw_put(&eq_mc, sizeof eq_mc);                       /* (7) its EQ's filters (8: and flags) */
    for (k = 0; k < nt; k++) {
        const track_t *t = &trk[k];
        uint8_t fm[FM6_PACKED], n2[2];
        uint32_t n;
        for (i = 0; i < P_COUNT; i++)
            pw_b((uint32_t)(clamp(motion_base_value(t, i), -64, 127) + 64));   /* (LEN over 127: its pattern holds it) */
        pw_b(t->eng_req);
        pw_b(t->preset);
        fm6_pack(fm6_patch[k], fm);
        pw_put(fm, sizeof fm);
        pw_put(&trkx[k], sizeof trkx[k]);
        pw_b(song.mch[k]);
        pw_b(ps_num(k));
        n = ps_encode(t, ps_buf);
        n2[0] = (uint8_t)n; n2[1] = (uint8_t)(n >> 8);
        pw_put(n2, 2);
        pw_put(ps_buf, n);
    }
    while (at + 4u <= ps_used) {                        /* (the heap: the project's tracks' records) */
        uint32_t n = 4u + ps_rlen(at);
        if (ps_heap[at] < nt)
            pw_put(ps_heap + at, n);
        at += n;
    }
}
/* the project open into id (a new record over free sectors; the old one erased after), named name. 0 done */
/* the first run of need free sectors in u from sector `from` on (then from 0): its start, -1 none. Each write starts
 * past the one before (its generation), so the saves of the project open spread over the free sectors (wear) */
static int32_t pfs_fit(uint64_t u, uint32_t need, uint32_t from)
{
    uint32_t pass, s, run;
    for (pass = 0; pass < 2u; pass++)
        for (s = pass ? 0u : from, run = 0; s < PFS_NSECT; s++) {
            run = (u >> s) & 1u ? 0u : run + 1u;
            if (run == need)
                return (int32_t)(s + 1u - need);
        }
    return -1;
}
static int pfs_write(uint32_t id, const char *name)
{
    pfs_hdr_t h;
    uint32_t len, need, s, i, old, from;
    int32_t at;
    pfs_need();
    for (i = 0; i < trk_count(); i++)
        (void)pg_sync(i);                               /* (long patterns: their pages' records as they are) */
    memset(&pw, 0, sizeof pw);                          /* the dry run: its size */
    pw.dry = 1; pw.max = PFS_NSECT * ST_SECTOR; pw.sum = 2166136261u;
    pfs_stream();
    len = pw.pos;
    need = (len + PFS_PAY + ST_SECTOR - 1u) / ST_SECTOR;
    old = pfs.start[id] ? pfs.start[id] : 0u;
    from = old ? old - 1u + pfs.nsect[id] : (pfs.gmax * 7u) % PFS_NSECT;
    at = need <= PFS_NSECT ? pfs_fit(pfs.used, need, from % PFS_NSECT) : -1;   /* free sectors */
    if (at < 0 && old) {                                /* no room beside the old one: over it (and the free after) */
        uint64_t u = pfs.used & ~((((uint64_t)1 << pfs.nsect[id]) - 1u) << (old - 1u));
        at = need <= PFS_NSECT ? pfs_fit(u, need, 0) : -1;
        if (at >= 0) {
            for (i = 0; i < pfs.nsect[id]; i++)
                (void)st_erase(pfs_off(old - 1u + i));
            pfs.used = u;
            pfs.start[id] = 0;
            old = 0;
        }
    }
    if (at < 0)
        return 2;                                       /* (MEMORY FULL) */
    s = (uint32_t)at;
    for (i = 0; i < need; i++)
        if (st_erase(pfs_off(s + i)))
            return 1;
    memset(&pw, 0, sizeof pw);
    pw.base = pfs_off(s); pw.max = need * ST_SECTOR - PFS_PAY; pw.sum = 2166136261u;
    pfs_stream();
    pw_flush();
    if (pw.err || pw.pos != len)
        return 1;
    memset(&h, 0, sizeof h);
    h.magic = PFS_MAGIC; h.ver = (uint8_t)pfs_ver_w(); h.id = (uint8_t)id; h.nsect = (uint8_t)need; h.ntrk = (uint8_t)trk_count();
    h.gen = pfs.gmax + 1u; h.len = len; h.sum = pw.sum;
    memcpy(h.name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    h.hsum = pfs_fnv(2166136261u, &h, sizeof h - 4u);
    if (st_prog(pfs_off(s), &h, sizeof h))
        return 1;
    if (old)
        (void)st_erase(pfs_off(old - 1u));              /* (the old record's header: gone) */
    pfs_scan();
    proj_wire_gen++;
    return 0;
}

static int pr_get(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--) {
        if (pw.pos >= pw.max)
            return 0;
        if (!(pw.pos & 255u) && st_read(pw.base + PFS_PAY + pw.pos, pw.buf, 256u))
            return 0;
        *b++ = pw.buf[pw.pos & 255u];
        pw.pos++;
    }
    return 1;
}
static uint32_t pr_b(void) { uint8_t b = 0; if (!pr_get(&b, 1)) pw.err = 1; return b; }
static int pfs_get_x(trkx_t *x, uint32_t w)             /* (Dah) a track's extras of w bytes (more or fewer than ours) */
{
    uint8_t b[64];
    memset(x, 0, sizeof *x);
    if (!pr_get(b, w)) return 0;
    memcpy(x, b, w < sizeof *x ? w : sizeof *x);
    return 1;
}
/* project id onto the tracks. 0 done, 2 damaged (the music as it was) */
static void pr_seek(uint32_t pos)                      /* (Dah) the payload read again from pos */
{
    pw.pos = pos;
    if (pos & 255u && st_read(pw.base + PFS_PAY + (pos & ~255u), pw.buf, 256u))
        pw.err = 1;
}
static int pfs_read_in(uint32_t id)
{
    pfs_hdr_t h;
    project_t *q = &proj_scratch;
    uint32_t k, i, nt, n, np, xw, ver;
    uint8_t cur[NTRK], mst[MST_N];
    eqc_t mqc = {0};                                     /* (Dah) the MASTER's EQ bands (7; before: as bands, eq_adopt) */
    uint32_t cur_at[NTRK], cur_n[NTRK];              /* (Dah: each track's pattern, where in the payload: read again
                                                      * after the sounds, straight onto the track, not through the heap,
                                                      * so a project whose heap is full opens) */
    if (!pfs_there(id) || !pfs_head(pfs.start[id] - 1u, &h))
        return 2;
    memset(&pw, 0, sizeof pw);                          /* its hash first, all of it */
    pw.base = pfs_off(pfs.start[id] - 1u); pw.max = h.len;
    for (i = 0, pw.sum = 2166136261u; i < h.len; i++) {
        uint8_t b;
        if (!pr_get(&b, 1)) break;
        pw.sum = (pw.sum ^ b) * 16777619u;
    }
    if (i != h.len || pw.sum != h.sum || h.ntrk < 1u || h.ntrk > NTRK)
        return 2;
    nt = h.ntrk;
    memset(&pw, 0, sizeof pw);
    pw.base = pfs_off(pfs.start[id] - 1u); pw.max = h.len;
    memset(q, 0, sizeof *q);                            /* the tracks past the project's: as at power-on */
    q->magic = PROJ_MAGIC; q->size = sizeof *q; q->parts = NPART; q->phys = PROJ_PHYS;
    chain_defaults(&q->chain);
    proj_x_defaults(q);
    for (k = 0; k < NTRK_V10; k++)
        q->t[k] = q->t[NTRK_V10], q->t[k].engine = (uint8_t)trk_def_engine(k);
    for (k = 0; k < NTRK_V10; k++)
        for (i = 0; i < P_COUNT; i++)
            q->t[k].p[i] = param_desc_of(trk_def_engine(k), i)->def;
    {
        uint32_t v = ver = pr_b();
        np = v == 1u ? PROJ_NP_W : v <= PFS_VER ? pr_b() : 0u;
        xw = v >= 3u ? pr_b() : TRKX_V2;                /* (the extras' size it wrote) */
        if (xw < TRKX_V2 || xw > 64u) return 2;
        if (np < 8u || np > P_COUNT || !pr_get(q->g, sizeof q->g)) return 2;
    }
    q->sel = (uint8_t)pr_b();
    if (!pr_get(&q->chain, sizeof q->chain) || !chain_valid(&q->chain)) return 2;
    if (ver >= 4u && !pr_get(mst, ver >= 6u ? MST_N : ver >= 5u ? MST_N5 : MST_N4)) return 2;
    if (ver >= 7u && !pr_get(&mqc, ver >= 8u ? sizeof mqc : 6u)) return 2;
    for (k = 0; k < nt; k++) {
        int16_t v[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++)
            v[i] = (int16_t)((int32_t)pr_b() - 64);
        q->t[k].engine = (uint8_t)pr_b();
        for (i = 0; i < P_COUNT; i++)
            def[i] = param_desc_of(q->t[k].engine % NENGINES, i)->def;
        params_by_count(q->t[k].p, v, np, def);         /* (an older count: the new ones their defaults) */
        q->t[k].preset = (uint8_t)pr_b();
        if (q->t[k].engine >= NENGINES || !pr_get(q->fm6[k], FM6_PACKED) || !pfs_get_x(&q->x[k], xw)) return 2;
        q->mch[k] = (uint8_t)pr_b();
        cur[k] = (uint8_t)pr_b();
        n = pr_b();
        n |= pr_b() << 8;
        cur_at[k] = pw.pos;
        cur_n[k] = n;
        if (n > PS_PAT_MAX || !pr_get(ps_buf, n)) return 2;
        if (k == 0u) {
            ps_used = 0;
            pgc_drop(NTRK);
            pgr_drop_all();
        }
    }
    while (!pw.err && pw.pos + 4u <= h.len) {           /* the heap's patterns */
        uint8_t r[4];
        if (!pr_get(r, 4)) return 2;
        n = (uint32_t)r[2] | (uint32_t)r[3] << 8;
        if (r[0] >= nt || !(r[1] & 63u) || (r[1] & 63u) > PS_NUM || n > PS_PAT_MAX || !pr_get(ps_buf, n) || ps_put(r[0], r[1], ps_buf, n))
            return 2;
    }
    if (pw.err) return 2;
    q->sum = proj_sum(q);
    proj_bound(q);
    if (project_restore_runtime(q)) return 2;
    for (i = 0; ver >= 4u && i < (ver >= 6u ? MST_N : ver >= 5u ? MST_N5 : MST_N4); i++)            /* (the MASTER's chain: restore_runtime emptied it) */
        mtrk.p[MST_ID[i]] = (int16_t)clamp((int32_t)mst[i] - 64, TP[MST_ID[i]].min, TP[MST_ID[i]].max);
    eq_mc = mqc;
    if (!(eq_mc.f & EQF_4)) {                           /* (Dah) its EQ stored before four filters: as four */
        int16_t raw[P_COUNT];
        memcpy(raw, mtrk.p, sizeof raw);
        for (i = 0; ver >= 4u && i < (ver >= 6u ? MST_N : ver >= 5u ? MST_N5 : MST_N4); i++)
            raw[MST_ID[i]] = (int16_t)((int32_t)mst[i] - 64);
        eq_adopt(mtrk.p, raw, &eq_mc);
    }
    song.ntrk = (uint8_t)nt;
    for (k = 0; k < NTRK; k++) {                        /* each track's pattern, read again, its number back */
        ps_cur[k] = k < nt ? (cur[k] && cur[k] <= PS_NUM ? cur[k] : 1u) : 1u;
        if (k >= nt)
            continue;
        pr_seek(cur_at[k]);
        if (!pw.err && pr_get(ps_buf, cur_n[k]))
            (void)ps_decode(&trk[k], ps_buf, cur_n[k]);
        ps_del(k, ps_cur[k]);                           /* (the one played lives on the track) */
        pg_motion_load(k);                              /* (its pages' automation) */
    }
    proj_name_get(proj_name, (const uint8_t *)h.name);
    PFS_CUR = (uint8_t)id;
    settings_later();
    trk_count_changed();                                /* (no arm, SONG lane, selection past the tracks) */
    return 0;
}
static int pfs_read(uint32_t id)                       /* 0 done; else nothing half loaded left in the heap */
{
    int r = pfs_read_in(id);
    if (r)
        ps_reset();
    return r;
}

/* ------------------------------------------------------------------ the actions --- */
static int pfs_busy(void)
{
    if (transport_busy() || chain_busy()) {
        ui_message("STOP FIRST");
        return 1;
    }
    return 0;
}
static void pfs_auto_name(uint32_t id, char *b)          /* "PROJECT 7" */
{
    str_cpy(b, "PROJECT ", 16);
    fmt_int(b + 8, (int32_t)id);
}
/* the project open saved (none open: a new one, PROJECT n). 0 done */
static int project_save_cur(void)
{
    uint32_t id = pfs_cur();
    int r;
    char nm[16];
    if (!id || !pfs_there(id)) {
        if (!(id = pfs_free_id())) return 2;
        if (!proj_name[0]) {
            pfs_auto_name(id, nm);
            str_cpy(proj_name, nm, sizeof proj_name);
        }
    }
    r = pfs_write(id, proj_name);
    if (!r && PFS_CUR != id) {
        PFS_CUR = (uint8_t)id;
        settings_later();
    }
    return r;
}
static int project_open(uint32_t id)                    /* OPEN: the one open saved first, then id */
{
    if (pfs_busy()) return 1;
    if (!pfs_there(id)) { ui_message("EMPTY PROJECT"); return 1; }
    if (id != pfs_cur() && project_save_cur()) { ui_message("SAVE ERROR"); return 2; }   /* (Dah: any failure: stop) */
    ps_reset();
    if (pfs_read(id)) { ui_message("PROJECT DAMAGED"); return 2; }
    ui_message("PROJECT OPEN");
    return 0;
}
static int project_new_file(void)                       /* NEW: the one open saved, an empty one (one track) saved as new */
{
    uint32_t id;
    char nm[16];
    if (pfs_busy()) return 1;
    if (project_save_cur() || !(id = pfs_free_id())) { ui_message("MEMORY FULL"); return 2; }
    chain_defaults(&chain_config);
    ps_reset();
    if (project_new()) return 2;
    chain_defaults(&chain_config);
    song.ntrk = 1;
    song.sel = 0;
    trk_count_changed();                                /* (Dah: no arm left on the tracks gone) */
    pfs_auto_name(id, nm);
    str_cpy(proj_name, nm, sizeof proj_name);
    PFS_CUR = (uint8_t)id;
    if (pfs_write(id, proj_name)) { ui_message("MEMORY FULL"); return 2; }
    settings_later();
    ui_message("NEW PROJECT");
    return 0;
}
static int project_copy(const char *name)               /* COPY: the music as a new project, open now. 0 done */
{
    uint32_t id;
    if (pfs_busy()) return 1;
    if (project_save_cur() || !(id = pfs_free_id())) { ui_message("MEMORY FULL"); return 2; }   /* (Dah: the id after the
                                                         * save, which may take one for the music open) */
    str_cpy(proj_name, name, sizeof proj_name);
    if (pfs_write(id, proj_name)) { ui_message("MEMORY FULL"); return 2; }
    PFS_CUR = (uint8_t)id;
    settings_later();
    ui_big("SAVED", proj_name);                         /* (Dah) big, as SAVE held: the new project's name under it */
    return 0;
}
static int project_rename_cur(const char *name)         /* EDIT on FILE: the project open renamed */
{
    if (pfs_busy()) return 1;
    str_cpy(proj_name, name, sizeof proj_name);
    if (project_save_cur()) { ui_message("MEMORY FULL"); return 2; }
    ui_message("RENAMED");
    return 0;
}
static int project_delete(uint32_t id)                  /* REC on FILE: a project gone (the open one: the music stays) */
{
    if (pfs_busy() || !pfs_there(id)) return 1;
    if (st_erase(pfs_off(pfs.start[id] - 1u))) { ui_message("DELETE ERROR"); return 2; }
    pfs_scan();
    proj_wire_gen++;
    if (PFS_CUR == id) {
        PFS_CUR = 0;
        settings_later();
        as.saved = 0;                                   /* (Dah: the music stays: autosave keeps it, a new project) */
    }
    ui_message("PROJECT DELETED");
    return 0;
}
