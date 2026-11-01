/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project format (firmware/src/project.c, -DPROJ_HOST part): FUN10, the only one FM Stud1o reads (the
 * older formats FUN1..FUN9 and their conversions are gone: no FM Stud1o user has one): the name, the FM6 patches, the
 * locks, a step out of range packed inside it; damaged, short and older records refused.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    int bad = 0, ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_LN0 &&
                 P_LN7 + 1 == P_LSYNC && P_LSYNC + 3 == P_SQNT && P_SQNT + 1 == P_SPRD && P_SPRD + 1 == P_ITYPE &&
                 P_ITYPE + 4 == P_IMIX && P_IMIX + 1 == P_FTYPE && P_FTYPE + 1 == P_ESYNC && P_ESYNC + 1 == P_FXORD && P_DKAMT + 1 == P_EQLO && P_DCOL + 1 == P_E0 && P_E0 == 116);
    bad += check("FUN10 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3840u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);

    {   /* the FUN10 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("FUN10 name at the end of the reserved tail, the FM6 patches before it, after the data (4 spare: 12 taken)",
                     PROJ_NAME_OFF == 3824u && PROJ_FM6_OFF == 3312u && 68u + NTRK_V10 * (PROJ_NP_W + 2u + NSTEP * 9u) +
                     sizeof(chain_v10_t) + sizeof(motion_store_v10_t) + 4u == PROJ_FM6_OFF);   /* (12 taken) */
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN10 no name: the tail all zero", ok && zero);
        bad += check("FUN10 zero tail: loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN10 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN10 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN10 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN10 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
    }

    {   /* FUN10: each track's FM6 patch travels with the project */
        project_t a, c;
        static project_store_t st, slot;
        uint32_t i, k;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < NTRK_V10; k++) {
            memcpy(a.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            a.t[k].engine = ENGI_FM6;
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == 0x46554E41u && proj_import(&c, &st, sizeof st);
        for (k = 0; k < NTRK_V10; k++)
            ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED) && c.t[k].engine == ENGI_FM6 &&
                  !memcmp(st.raw + PROJ_FM6_OFF + k * FM6_PACKED, FM6_FACTORY[k * 2u], FM6_PACKED);
        bad += check("FUN10: the four FM6 patches round trip (bytes at PROJ_FM6_OFF)", ok && !memcmp(c.name, "FM SONG", 7));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN10: a patch byte changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN10: one byte short: refused", !proj_import(&c, &st, sizeof st - 1u));
        slot = st;
        ((uint32_t *)slot.raw)[0] = 0x46554E39u;        /* ("FUN9", upstream's 1.1: not read) */
        bad += check("FUN10 size with FUN9's magic: refused", !proj_import(&c, &slot, sizeof slot));
        {   /* FUN10 holds parameter locks (bit 7 of the id): on a DRUM lane level and on an engine value */
            project_t f = a, g;
            f.t[3].engine = ENGI_DRUM; f.t[3].p[P_E3] = 30;
            f.motion.count = 4; f.motion.on = 8;
            f.motion.event[0] = (motion_event_t){3u << 6 | 2u, P_E3, 99};
            f.motion.event[1] = (motion_event_t){3u << 6 | 4u, P_REV, 50};
            f.motion.event[2] = (motion_event_t){3u << 6 | 5u, (uint8_t)(P_LN2 | MOTION_LOCK), 40};
            f.motion.event[3] = (motion_event_t){3u << 6 | 6u, (uint8_t)(P_E5 | MOTION_LOCK), -3};
            f.sum = proj_sum(&f);
            ok = proj_pack(&st, &f) && proj_import(&g, &st, sizeof st) && !memcmp(&f.motion, &g.motion, sizeof f.motion);
            bad += check("FUN10 round trip: locks on a lane level (P_LN2) and an engine value (P_E5) kept", ok &&
                         st.raw[68u + NTRK_V10 * (PROJ_NP_W + 2u + NSTEP * 9u) + sizeof(chain_v10_t) + 4u + 6u + 1u] ==
                         (P_LN2 | MOTION_LOCK) && g.motion.event[3].param == (P_E5 | MOTION_LOCK) && g.t[3].p[P_E3] == 30);
        }
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        for (i = 0; i < FM6_PACKED; i++) a.fm6[1][i] = (uint8_t)(i | 0x80u);
        a.sum = proj_sum(&a);
        bad += check("FUN10: a patch byte above 127 cannot be packed", !proj_pack(&st, &a));
        memcpy(a.fm6[1], FM6_INIT, FM6_PACKED);
        a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN10: a step out of range packs inside it (note, velocity 127; accents on hits): it loads",
                     ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                     c.t[0].step[3].hit == 0x05u && c.t[0].step[3].acc == 0x05u);
    }
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
