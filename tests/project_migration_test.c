/* SPDX-License-Identifier: GPL-3.0-only
 * MNM3 flash migration and MNM4 round-trip through the real project/storage code. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#include <assert.h>

static mnm_project_v3_t legacy;
static project_t expected;

static void fixture(void)
{
    uint32_t k, i;
    bench_reset();
    flash_ok = 0;
    transport_req = 0;
    song.playing = 0;
    memset(&chain, 0, sizeof chain);
    memset(&mo, 0, sizeof mo);
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++) trk[k].p[i] = TP[i].def;
        drum_set_model(&trk[k], DM_MMSAW);
        trk[k].p[P_NOTE] = (int16_t)(k * 2);
        trk[k].pre = k == 1 ? PRE_NFACT + 7u : PRE_NONE;
        trk[k].pre_ed = k & 1u;
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &trk[k].step[i];
            memset(s, 0, sizeof *s);
            s->on = i & 1u; s->acc = (i >> 1) & 1u;
            s->nt = (int8_t)((int)(i % 25u) - 12);
            s->rat = i % 4u; s->cond = i % COND_MAX;
            s->slide = (i >> 2) & 1u; s->tie = (i >> 3) & 1u;
        }
    }
    mo.s.count = 2; mo.s.on = 3;
    mo.s.ev[0] = (motion_event_t){0, 1, P_E0, 42};
    mo.s.ev[1] = (motion_event_t){1, 5, P_PAN, 7};
    assert(motion_valid(&mo.s));
    chain.cfg.count = 2; chain.cfg.loop = 1;
    chain.cfg.row[0] = (chain_row_t){1, 3};
    chain.cfg.row[1] = (chain_row_t){3, 2};
    name_set(chain.name, "OLD SONG");
    song.sel = 4;
    project_save(0);
    expected = proj_slot[0];
    memset(&legacy, 0, sizeof legacy);
    legacy.magic = PROJ_V3_MAGIC; legacy.size = sizeof legacy;
    memcpy(legacy.g, expected.g, sizeof legacy.g);
    legacy.sel = expected.sel;
    for (k = 0; k < NTRK; k++) {
        memcpy(legacy.t[k].p, expected.t[k].p, sizeof legacy.t[k].p);
        memcpy(legacy.t[k].step, expected.t[k].step, sizeof legacy.t[k].step);
    }
    legacy.motion = expected.motion; legacy.song = expected.song;
    memcpy(legacy.name, expected.name, sizeof legacy.name);
    memcpy(legacy.pre, expected.pre, sizeof legacy.pre);
    memcpy(legacy.pre_ed, expected.pre_ed, sizeof legacy.pre_ed);
    legacy.sum = proj_hash(&legacy, sizeof legacy - 4u);
}
static void test_decode(void)
{
    project_t q;
    uint32_t i;
    assert(sizeof legacy == 3624u && sizeof q == 3828u + (G_COUNT - PROJ_V3_GLOBALS) * 2u);
    memset(&q, 0xA5, sizeof q);
    memcpy(&q, &legacy, sizeof legacy);
    assert(proj_decode(&q, sizeof legacy));
    assert(!memcmp(&q, &expected, sizeof q));
    assert(proj_decode(&q, sizeof q));
    q.t[3].p[P_PAN] ^= 1;
    assert(!proj_decode(&q, sizeof q));
    for (i = 0; i < sizeof legacy; i++) {
        memcpy(&q, &legacy, sizeof legacy);
        ((uint8_t *)&q)[i] ^= 1u;
        assert(!proj_decode(&q, sizeof legacy));
    }
    memcpy(&q, &legacy, sizeof legacy);
    assert(!proj_decode(&q, sizeof legacy - 1u));
    assert(!proj_decode(&q, sizeof legacy + 1u));
    assert(!proj_decode(&q, sizeof q));
}
static void test_flash(void)
{
    uint32_t k;
    project_t saved;
    memset(hflash, 0xFF, sizeof hflash);
    memset(proj_slot, 0xCC, sizeof proj_slot);
    flash_ok = 1;
    for (k = 0; k < 4; k++) assert(!st_save(OBJ_PROJECT0 + k, &legacy, sizeof legacy));
    for (k = 0; k < 4; k++) {
        proj_fetch(k);
        assert(!memcmp(&proj_slot[k], &expected, sizeof expected));
    }
    memset(trk[0].p, 0, sizeof trk[0].p);
    assert(proj_load(2, 0));
    for (k = 0; k < NTRK; k++) {
        assert(!memcmp(trk[k].p, expected.t[k].p, sizeof trk[k].p));
        assert(!memcmp(trk[k].step, expected.t[k].step, sizeof trk[k].step));
    }
    assert(!memcmp(&mo.s, &legacy.motion, sizeof mo.s));
    assert(!memcmp(&chain.cfg, &legacy.song, sizeof chain.cfg));
    assert(!strcmp(chain.name, "OLD SONG") && song.sel == 4);
    assert(trk[1].pre == PRE_NFACT + 7u && trk[1].pre_ed == 1);
    project_save(2);
    assert(st_load(OBJ_PROJECT0 + 2u, &saved, sizeof saved) == sizeof saved);
    assert(proj_ok(&saved));
    memset(&proj_slot[2], 0, sizeof proj_slot[2]);
    proj_fetch(2);
    assert(!memcmp(&saved, &proj_slot[2], sizeof saved));
    legacy.sum ^= 1;
    assert(!st_save(OBJ_PROJECT0, &legacy, sizeof legacy));
    proj_fetch(0);
    assert(!proj_ok(&proj_slot[0]));
    legacy.sum ^= 1;
    assert(!st_save(OBJ_PROJECT0, &legacy, sizeof legacy - 1u));
    proj_fetch(0);
    assert(!proj_ok(&proj_slot[0]));
}
int main(void)
{
    fixture(); test_decode(); test_flash();
    puts("project_migration_test: all passed");
    return 0;
}
