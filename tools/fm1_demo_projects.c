/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The demo songs (main.c felucca_demo, FELUCCA_DEMO: the emulator's power-on, none on the device: its flash is full) as
 * PROJECTS (projfs.c), for the device to get them over USB-MIDI with no program space: the firmware built for the host
 * as the emulator is (web/emu/felucca_web.c: hostsim.c's sources, the UI, main.c, storage and projects on a simulated
 * NOR), one build per song (-DDAH_DEMO_SONG=N). tools/fm1_demo_projects.py drives it:
 *   build IN OUT ID NAME   power on with song N (as the emulator does), save the music as project ID named NAME into the
 *                          projects' flash read from IN ("-": erased), OUT that flash (PFS_NSECT sectors); prints the
 *                          song's audio hash (its SONG from the top, 12 s)
 *   verify IMG ID          (built with another song, 0: the FX test) power on, restore IMG as the editor's backup
 *                          object 18 does (editor_backup.c ED_BACKUP_PUT: begin, 256-byte chunks, commit), OPEN project
 *                          ID as the FILE page does (project_open) and print its audio hash the same way, and its name
 *   hash                   the song this build powers on with: its audio hash only
 * The hashes of build and verify are equal when the project plays what the demo plays (every step, sound, mix, effect,
 * the SONG). Host only: nothing of it is in the firmware.
 *   cc -O1 -w -DDAH_DEMO_SONG=1 -Ibuild/gen -Ifirmware/src -o build/host/demo1 tools/fm1_demo_projects.c -lm */
#include "../web/emu/felucca_web.c"

/* (editor_backup.c's surroundings, as tests/backup_test.c has them: editor.c's packing and its transfer buffer) */
enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j, k;
        na--;
        k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512];
static int ed_flash_stop(void) { return transport_busy(); }
#define __attribute__(x)                         /* (as hostsim.c: no target sections; felucca_web.c undid it) */
#include "../firmware/src/editor_backup.c"

#define PFS_BYTES (PFS_NSECT * ST_SECTOR)

/* the SONG from its first section, 12 s of the output: an FNV-1a of the samples (the RNG as at power-on first) */
static uint32_t song_hash(void)
{
    uint32_t h = 2166136261u, k, i, on = 0;
    float peak = 0;
    rng_state = 0x1234567u;
    if (chain_prepare())
        return 0;
    for (k = 0; k < 12u * FS; k += 128u) {
        web_render(128);
        for (i = 0; i < 128u; i++) {
            int32_t s[2] = {(int32_t)(out_l[i] * 8388608.0f), (int32_t)(out_r[i] * 8388608.0f)};
            h = pfs_fnv(h, s, sizeof s);
            if (out_l[i] > peak) peak = out_l[i];
        }
        on += song.playing;
    }
    if (peak < 0.01f || on < 12u * FS / 128u * 9u / 10u)   /* (silent, or not playing: no hash) */
        return 0;
    return h;
}

static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }
static uint32_t put(const uint8_t *a, uint32_t n)       /* one PUT request: its rc */
{
    rep_n = 0;
    ed_backup_handle(ED_BACKUP_PUT, a, n);
    return rep_n == 3u ? rep[2] : 99u;
}
/* the image as the sender restores it (tools/fm1_demo_projects.py, web/fm1backup.js restoreBackup): 0 done */
static uint32_t restore(const uint8_t *img, uint32_t len)
{
    uint8_t a[12 + 300];
    uint32_t off, rc, i, n;
    a[0] = 0; a[1] = ED_BK_PFS;
    put32(a + 2, len);
    put32(a + 7, st_crc32(img, len));
    if ((rc = put(a, 12)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        a[0] = 1; a[1] = ED_BK_PFS;
        put32(a + 2, off);
        n = 7;
        for (i = 0; i < 256u; i += 7u) {                /* (7 bytes a group, their high bits first: bkPack) */
            uint32_t k = 256u - i < 7u ? 256u - i : 7u, j, m = 0;
            for (j = 0; j < k; j++)
                m |= (uint32_t)(img[off + i + j] >> 7) << j;
            a[n++] = (uint8_t)m;
            for (j = 0; j < k; j++)
                a[n++] = img[off + i + j] & 127u;
        }
        fm1_ms += 50u;                                  /* (the requests' pace) */
        if ((rc = put(a, n)) != 0)
            return 100u + rc;
    }
    a[0] = 2; a[1] = ED_BK_PFS;
    return put(a, 2);
}
/* the projects' flash up to its last used sector (whole sectors, as editor_backup.c's LIST gives it) */
static uint32_t pfs_used_len(void)
{
    uint32_t k, len = 0;
    pfs_scan();
    for (k = 1; k <= PFS_IDS; k++)
        if (pfs.start[k] && (pfs.start[k] - 1u + pfs.nsect[k]) * ST_SECTOR > len)
            len = (pfs.start[k] - 1u + pfs.nsect[k]) * ST_SECTOR;
    return len;
}

int main(int argc, char **argv)
{
    static uint8_t img[PFS_BYTES];
    FILE *f;
    web_nor_erase();
    if (argc == 2 && !strcmp(argv[1], "hash")) {
        web_boot();
        printf("hash %08x\n", song_hash());
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "build")) {
        uint32_t id = (uint32_t)atoi(argv[4]);
        if (strcmp(argv[2], "-")) {
            if (!(f = fopen(argv[2], "rb")) || fread(nor + PFS_BASE, 1, PFS_BYTES, f) != PFS_BYTES)
                return fprintf(stderr, "demo: cannot read %s\n", argv[2]), 2;
            fclose(f);
        }
        web_boot();                                     /* (the song, as at power-on; nothing played yet) */
        pfs.ok = 0;
        if (!flash_ok || !id || id > PFS_IDS || pfs_there(id) || pfs_write(id, argv[5]))
            return fprintf(stderr, "demo: project %u not written (%u sectors used)\n", id, pfs_used_len() / ST_SECTOR), 1;
        if (!(f = fopen(argv[3], "wb")) || fwrite(nor + PFS_BASE, 1, PFS_BYTES, f) != PFS_BYTES)
            return fprintf(stderr, "demo: cannot write %s\n", argv[3]), 2;
        fclose(f);
        printf("project %u \"%s\": %u sectors, %u tracks; projects %u sectors\n", id, argv[5], pfs.nsect[id],
               (unsigned)trk_count(), pfs_used_len() / ST_SECTOR);
        printf("hash %08x\n", song_hash());
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "verify")) {
        uint32_t id = (uint32_t)atoi(argv[3]), len, rc;
        char name[PROJ_NAME_LEN + 1];
        if (!(f = fopen(argv[2], "rb")))
            return fprintf(stderr, "demo: cannot read %s\n", argv[2]), 2;
        len = (uint32_t)fread(img, 1, PFS_BYTES, f);
        fclose(f);
        web_boot();                                     /* (another song in RAM: the project must replace it all) */
        if ((rc = restore(img, len)) != 0)
            return fprintf(stderr, "demo: restore rc %u\n", rc), 1;
        if (memcmp(nor + PFS_BASE, img, len) || !pfs_there(id) || project_open(id))
            return fprintf(stderr, "demo: project %u not there after the restore\n", id), 1;
        memset(name, 0, sizeof name);
        pfs_name(id, name);
        printf("name %s\n", name);
        printf("hash %08x\n", song_hash());
        return 0;
    }
    fprintf(stderr, "usage: %s build IN|- OUT ID NAME | verify IMG ID | hash\n", argv[0]);
    return 2;
}
