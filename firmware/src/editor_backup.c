/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Complete musical archive: no raw addresses are accepted. Small objects are staged
 * in main-loop RAM and fully validated before the existing A/B commit path writes.
 * Sample restore uses SMP_BEGIN/WRITE/END and its existing CRC/header-last commit.
 * A disconnected sample restore can lose that sample; the exported file is retained.
 * Id 8 (the FM6 patch bank of 1.0..1.0.2) is listed empty since 1.0.3; a PUT of it from an older archive moves its
 * patches into the user presets restored before it (ids 6, 7), as the first boot after the update does (up_fm6.c).
 * Id 9 is the user presets' FM6 patches (up_fm6.c), appended in 1.0.3.
 * ids 10..13 the PROJECT slots' tracks 5..8 (their FUNX objects, project.c proj_pack_x; empty when the
 * slot is, or its X belongs to no record there), restored after their slot (ids 2..5): tagged again with the slot's
 * record as restored (an older format is packed again: another hash). Ids 14..17 the same of their extras (FUNY, LFO DRAW
 * and CURVE, proj_pack_y). Retired with the PROJECTS (projfs.c: their flash is the projects' now): a PUT of them is
 * refused (rc 1, the page skips them), and so is a GET (none lists them).
 * Id 18: the PROJECTS, the flash of projfs.c as it is, up to its last used sector (whole sectors, 0: none).
 * Read from the flash 256 bytes at a time; restored straight into it (a sector erased when its first chunk comes, the
 * ones after the image's end at the commit, its CRC read back), then scanned again. Restored before the settings
 * (the project open, PFS_CUR) and the live music (id 0: the project open is opened again, its eight tracks).
 * Ids 33, 34 (USR2, USR3: the projects' flash now) are listed empty.
 */
static const uint8_t ED_BK_IDS[14] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 18, 32, 33, 34};
#define ED_BK_PFS 18u
#define ED_BK_PFS_MAX (40u * ST_SECTOR)            /* (projfs.c PFS_NSECT sectors) */
#if FELUCCA_FLASH
_Static_assert(ED_BK_PFS_MAX == PFS_NSECT * ST_SECTOR, "the projects image");
#endif
#define ED_BK_N ((uint32_t)sizeof ED_BK_IDS)
#define ED_BK_MAX ((uint32_t)sizeof proj_wire_u.raw)
#define ED_BK_RAW (proj_wire_u.raw)  /* reuse the existing serialized main-loop scratch */
_Static_assert(sizeof proj_wire_u.raw >= sizeof(upf_t) && sizeof proj_wire_u.raw >= sizeof(fm6_bank_t), "backup staging");
static persist_t ed_bk_settings __attribute__((section(".pool")));
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id, ed_bk_gen, ed_bk_pfs_done;   /* (Dah: id 18 restored this session) */
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms, ed_bk_usb;
static void ed_bk_u32(uint32_t n) { for (uint32_t i = 0; i < 5u; i++) ed_b((n >> (i * 7u)) & 127u); }
static uint32_t ed_bk_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, mask = 0;
        for (uint32_t i = 0; i < k; i++) mask |= (uint32_t)(p[i] >> 7) << i;
        ed_b(mask);
        for (uint32_t i = 0; i < k; i++) ed_b(p[i]);
        p += k; n -= k;
    }
}
static const uint8_t *ed_bk_object(uint32_t id, uint32_t *len)
{
    *len = 0;
    if (id == 0u) { *len = sizeof(project_store_t); return ED_BK_RAW; }
    if (id == 1u) { *len = sizeof ed_bk_settings; return (const uint8_t *)&ed_bk_settings; }
    if (id >= 2u && id <= 5u) {
        if (project_used(id - 2u)) *len = sizeof proj_slot[0];
        return (const uint8_t *)&proj_slot[id - 2u];
    }
    if (id == 6u || id == 7u) {
        if (up_bank[id - 6u].magic == UP_BANK_MAGIC) *len = sizeof up_bank[0];
        return (const uint8_t *)&up_bank[id - 6u];
    }
    if (id == 8u)                                   /* the retired FM6 patch bank: always empty */
        return ED_BK_RAW;
    if (id == 9u) {                                 /* the user presets' FM6 patches */
        if (upf_valid(&upf)) *len = sizeof upf;
        return (const uint8_t *)&upf;
    }
#if FELUCCA_FLASH
    if (id == ED_BK_PFS) {                          /* (read by ed_bk_pfs_read: no pointer into it) */
        uint32_t k;
        pfs_need();
        for (k = 1; k <= PFS_IDS; k++)
            if (pfs.start[k] && (pfs.start[k] - 1u + pfs.nsect[k]) * ST_SECTOR > *len)
                *len = (pfs.start[k] - 1u + pfs.nsect[k]) * ST_SECTOR;
        return ed_smp_buf;
    }
#endif
    if (id >= 32u && id < 32u + SMP_USER_WRITABLE) {   /* (no USR3) */
        uint32_t k = id - 32u;
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
        if (usr_nz[k] && h->magic == SMP_USER_MAGIC && h->data_len <= SMP_USER_SIZE - SMP_USER_DATA)
            *len = SMP_USER_DATA + h->data_len;
        return smp_user_xip(k);
    }
    return 0;
}
#if FELUCCA_FLASH
static uint32_t ed_bk_pfs_crc(uint32_t len)          /* the projects' image as the flash holds it */
{
    uint32_t c = 0xFFFFFFFFu, off;
    for (off = 0; off < len; off += 256u) {
        if (st_read(PFS_BASE + off, ed_smp_buf, 256u)) return 0;
        c = st_crc32_add(c, ed_smp_buf, 256u);
        fm1_wdt_feed();
    }
    return ~c;
}
#endif
static uint32_t ed_bk_capture(void)
{
    if (ed_flash_stop()) return 3;
#if FELUCCA_FLASH
    if (flash_ok && pfs_cur() && project_save_cur()) return 4;   /* (the project open as it is now, in the image) */
#endif
    project_capture(&proj_scratch);
    if (!proj_pack((project_store_t *)ED_BK_RAW, &proj_scratch)) return 2;
    ed_bk_gen = ++proj_wire_gen;
#if FELUCCA_FLASH
    ed_bk_settings = persist_saved;                 /* fields absent from this build survive */
#else
    memset(&ed_bk_settings, 0, sizeof ed_bk_settings);
#endif
    settings_export(&ed_bk_settings);
    ed_bk_put = 0; ed_bk_valid = 1; ed_bk_usb = usb.resets;
    return 0;
}
static int ed_bk_panel_valid(const panel_t *p)
{
    uint32_t b = 0, e = 0;
    if (p->magic != PANEL_MAGIC) return 0;
    for (uint32_t i = 0; i < NB; i++) {
        if (p->btn[i] >= NB || (b & (1u << p->btn[i]))) return 0;
        b |= 1u << p->btn[i];
    }
    for (uint32_t i = 0; i < NE; i++) {
        if (p->enc[i] >= NE || (e & (1u << p->enc[i])) || (p->dir[i] != 1 && p->dir[i] != -1)) return 0;
        e |= 1u << p->enc[i];
    }
    return 1;
}
static uint32_t ed_bk_commit(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t obj;
#if FELUCCA_FLASH
    if (ed_bk_id == ED_BK_PFS) {                    /* the projects: written already; the rest erased */
        uint32_t off;
        pfs.ok = 0;
        proj_wire_gen++;
        if (ed_bk_pos != ed_bk_len) return 2;
        for (off = ed_bk_len; off < PFS_NSECT * ST_SECTOR; off += ST_SECTOR) {
            uint32_t w, blank = 1;
            if (st_read(PFS_BASE + off, &w, 4u)) return 4;
            blank = w == 0xFFFFFFFFu;                /* (a record starts its sector: a blank head, nothing there) */
            if (!blank && st_erase(PFS_BASE + off)) return 4;
            fm1_wdt_feed();
        }
        if (ed_bk_pfs_crc(ed_bk_len) != ed_bk_crc) return 2;
        pfs_scan();
        ed_bk_pfs_done = 1;
        if (!pfs_there(pfs_cur()))
            PFS_CUR = 0;
        ui.force = 1;
        return 0;
    }
#endif
    if (ed_bk_pos != ed_bk_len || st_crc32(raw, ed_bk_len) != ed_bk_crc) return 2;
    if (ed_bk_id == 0u || (ed_bk_id >= 2u && ed_bk_id <= 5u)) {
        if (ed_bk_len && !proj_import(&proj_scratch, raw, (int)ed_bk_len)) return 2;
        if (ed_bk_len) {                            /* an older format becomes FUN10 inside its ranges */
            proj_bound(&proj_scratch);
            if (!proj_pack((project_store_t *)raw, &proj_scratch)) return 2;
            ed_bk_len = sizeof(project_store_t);
        }
        if (ed_bk_id == 0u) {
            if (!ed_bk_len) return 2;
#if FELUCCA_FLASH
            if (flash_ok && pfs_there(pfs_cur()))      /* (the project open, all its tracks) */
                return pfs_read(pfs_cur()) ? 2u : 0u;
#endif
            return project_restore_runtime(&proj_scratch) ? 2u : 0u;
        }
        obj = OBJ_PROJECT0 + ed_bk_id - 2u;
    } else if (ed_bk_id == 1u) {
        const persist_t *p = (const persist_t *)raw;
        if (ed_bk_len != sizeof *p || p->magic != PERSIST_MAGIC || !palette_stored_ok(p->palette) ||
            p->lowcut > 2u || !leds_stored_ok(p->zoom) || !hold_stored_ok(p->bold) || p->favorites.filter > 1u || !ed_bk_panel_valid(&p->panel)) return 2;
        obj = OBJ_SETTINGS;
    } else if (ed_bk_id == 6u || ed_bk_id == 7u) {
        const up_bank_t *p = (const up_bank_t *)raw;
        if (ed_bk_len && (ed_bk_len != sizeof *p || p->magic != UP_BANK_MAGIC ||
            p->rsize != sizeof(up_rec_t) || p->nslot != UP_PER_BANK)) return 2;
        /* Unknown record versions remain inert bytes, preserving future/older bank data. */
        obj = OBJ_UPRESET0 + ed_bk_id - 6u;
    } else if (ed_bk_id == 8u) {                    /* the retired FM6 bank (always empty; FM Stud1o: an older archive's
                                                     * bank is not moved) */
        return 0;
    } else if (ed_bk_id == 9u) {                    /* the user presets' FM6 patches (0: none) */
        const upf_t *u = (const upf_t *)raw;
        if (ed_bk_len && (ed_bk_len != sizeof *u || !upf_valid(u) || (u->used >> (UP_SLOTS - 1u) >> 1))) return 2;
        if (ed_bk_len) memcpy(&upf, raw, sizeof upf); else upf_empty();
        sync_reload = 1; ui.force = 1;
        return upf_save() == 2 ? 4u : 0u;           /* (a failed write keeps the restored RAM copy) */
    } else return 1;
#if FELUCCA_FLASH
    if (!flash_ok || st_save(obj, raw, ed_bk_len)) return 4;
#else
    (void)obj;
#endif
    if (ed_bk_id >= 2u && ed_bk_id <= 5u) {
        memset(&proj_slot[ed_bk_id - 2u], 0, sizeof proj_slot[0]);
        if (proj_cur == ed_bk_id - 2u)
            proj_cur = PROJ_NO_SLOT;                     /* (another project there now: the music keeps its name) */
        if (ed_bk_len) memcpy(&proj_slot[ed_bk_id - 2u], raw, ed_bk_len);
    } else if (ed_bk_id == 1u) {
        memcpy(&ed_bk_settings, raw, sizeof ed_bk_settings);
        {
            uint8_t cur = DAH_CUR;                  /* (Dah: the project open is the device's, unless the projects came
                                                     * with the archive: else autosave would write over another one) */
            settings_import(&ed_bk_settings, sizeof ed_bk_settings);
            if (!ed_bk_pfs_done)
                DAH_CUR = cur;
        }
        panel_init(); settings_init(); palette_apply();
#if FELUCCA_FLASH
        persist_saved = ed_bk_settings; persist_pending = 0;
#endif
    } else {
        uint32_t b = ed_bk_id - 6u;
        memset(&up_bank[b], 0, sizeof up_bank[b]);
        if (ed_bk_len) memcpy(&up_bank[b], raw, ed_bk_len);
        up_bank_check(b, (int)ed_bk_len); up_gen++;
    }
    sync_reload = 1; ui.force = 1;
    return 0;
}
/* a project object's length: FUN10's (the only format read) */
static int proj_store_len(uint32_t len) { return len == sizeof(project_store_t); }
static uint32_t ed_bk_write(const uint8_t *a, uint32_t n)
{
    /* the arguments first: a malformed request never stops the transport */
    if (n < 2u || a[0] > 3u || a[1] > ED_BK_PFS || (a[1] >= 10u && a[1] < ED_BK_PFS)) return 1;   /* (10..17 retired) */
    if (a[0] == 0u) {
        if (n != 12u || a[6] > 15u || a[11] > 15u) return 1;
        uint32_t len = ed_bk_r32(a + 2);
        if ((len > ED_BK_MAX && a[1] != ED_BK_PFS) || (a[1] == 0u && !proj_store_len(len)) ||
            (a[1] == 1u && len != sizeof(persist_t)) ||
            (a[1] >= 2u && a[1] <= 5u && len && !proj_store_len(len)) ||
            ((a[1] == 6u || a[1] == 7u) && len && len != sizeof(up_bank_t)) ||
            (a[1] == 8u && len && len != sizeof(fm6_bank_t)) || (a[1] == 9u && len && len != sizeof(upf_t)) ||
            (a[1] == ED_BK_PFS && (len % ST_SECTOR || len > ED_BK_PFS_MAX || !FELUCCA_FLASH))) return 1;
        if (ed_flash_stop()) return 3;
        ed_bk_valid = 0; ed_bk_put = 1; ed_bk_id = a[1]; ed_bk_len = len; ed_bk_gen = ++proj_wire_gen;
        ed_bk_crc = ed_bk_r32(a + 7); ed_bk_pos = 0;
        ed_bk_usb = usb.resets; ed_bk_ms = fm1_ms;
#if FELUCCA_FLASH
        if (a[1] == ED_BK_PFS) {                    /* (none in the archive: no project, every sector freed) */
            if (!flash_ok) return 4;
            pfs.ok = 0;
            PFS_CUR = 0;                            /* (the sectors change under it: no project open meanwhile) */
        }
#endif
        return 0;
    }
    if (!ed_bk_put || ed_bk_id != a[1] || ed_bk_usb != usb.resets || fm1_ms - ed_bk_ms > 15000u ||
        ed_bk_gen != proj_wire_gen) {                 /* (a project save / load reused the staging RAM) */
        ed_bk_put = 0; return 5;
    }
    ed_bk_ms = fm1_ms;
    if (a[0] == 3u) { ed_bk_put = 0; return n == 2u ? 0u : 1u; }
    if (a[0] == 2u) {
        if (n != 2u) return 1;
        if (ed_flash_stop()) return 3;
        uint32_t rc = ed_bk_commit(); ed_bk_put = 0; return rc;
    }
    if (n < 9u || a[6] > 15u || ed_bk_r32(a + 2) != ed_bk_pos) return 1;
    uint32_t count = ed_unpack7(a + 7, n - 7u, ed_smp_buf, 256u);
    if (!count || count > ed_bk_len - ed_bk_pos) return 1;
    if (ed_flash_stop()) return 3;
#if FELUCCA_FLASH
    if (ed_bk_id == ED_BK_PFS) {                    /* straight into the flash, 256 bytes a chunk */
        uint32_t at = PFS_BASE + ed_bk_pos;
        if (count != 256u) return 1;
        pfs.ok = 0;
        if (!(ed_bk_pos % ST_SECTOR) && st_erase(at)) return 4;
        if (st_prog(at, ed_smp_buf, count)) return 4;
        ed_bk_pos += count;
        return 0;
    }
#endif
    memcpy(ED_BK_RAW + ed_bk_pos, ed_smp_buf, count); ed_bk_pos += count;
    return 0;
}
static int ed_backup_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_BACKUP_LIST) {
        uint32_t rc = n ? 1u : ed_bk_capture();
        ed_b(1); ed_b(rc); ed_b(rc ? 0u : ED_BK_N);
        if (!rc) for (uint32_t i = 0; i < ED_BK_N; i++) {
            uint32_t len;
            const uint8_t *p = ed_bk_object(ED_BK_IDS[i], &len);
#if FELUCCA_FLASH
            if (ED_BK_IDS[i] == ED_BK_PFS) { ed_b(ED_BK_PFS); ed_bk_u32(len); ed_bk_u32(ed_bk_pfs_crc(len)); continue; }
#endif
            ed_b(ED_BK_IDS[i]); ed_bk_u32(len); ed_bk_u32(st_crc32(p, len));
            fm1_wdt_feed();
        }
        return 1;
    }
    if (cmd == ED_BACKUP_GET) {
        uint32_t len = 0, off = n >= 6u ? ed_bk_r32(a + 1) : 0u;
        uint32_t off2;
        uint32_t count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        const uint8_t *p = n ? ed_bk_object(a[0], &len) : 0;
        uint32_t rc = (!ed_bk_valid || ed_bk_usb != usb.resets || (n && !a[0] && ed_bk_gen != proj_wire_gen)) ? 5u : transport_busy() ? 3u :
            n != 8u || a[5] > 15u || !p || !count || count > 256u || off > len || count > len - off ? 1u : 0u;
        off2 = off;
#if FELUCCA_FLASH
        if (!rc && a[0] == ED_BK_PFS) {             /* (the projects: from the flash) */
            if (st_read(PFS_BASE + off, ed_smp_buf, count)) rc = 4;
            p = ed_smp_buf;
            off2 = 0;
        }
#endif
        ed_b(n ? a[0] : 127u); ed_b(rc); ed_bk_u32(off); ed_b(rc ? 0u : count & 127u); ed_b(rc ? 0u : count >> 7);
        if (!rc) ed_bk_pack(p + off2, count);
        return 1;
    }
    if (cmd == ED_BACKUP_PUT) {
        uint32_t rc = ed_bk_write(a, n);
        ed_b(n ? a[0] : 127u); ed_b(n >= 2u ? a[1] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
