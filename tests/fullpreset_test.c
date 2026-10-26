/* SPDX-License-Identifier: GPL-3.0-only
 * Full factory sound metadata, application, audition rollback and save guard. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#include <assert.h>
int main(void) {
    uint32_t i;
    uint8_t want[71], got[71];
    int16_t before[P_COUNT];
    upr_rec_t users;
    memset(hflash, 0xFF, sizeof hflash);
    ui_host_init();
    persist_boot();
    settings_init();
    assert(PRE_NFACT == PRE_FULL_FIRST + MM_SOUND_COUNT);
    for(i=0;i<MM_SOUND_COUNT;i++) {
        const preset_t *meta = &PRE_FACTORY[PRE_FULL_FIRST+i];
        assert(meta->model == MM_MODEL_FIRST + MM_SOUNDS[i].machine);
        assert(!strcmp(meta->name, MM_SOUNDS[i].name));
        assert(PRE_UID[PRE_FULL_FIRST+i] == MM_SOUNDS[i].uid);
        assert(preset_bank_of(PRE_FULL_FIRST+i) == (i < 24 ? PB_SOURCE : PB_ARCHIVE));
        for (uint32_t j = 0; j < 8; j++) assert(meta->k[j] == MM_SOUNDS[i].raw[j]);
        trk[2].p[P_MUTE]=1; trk[2].p[P_OUT]=BUS_EF; trk[2].p[P_NOTE]=11;
        trk[2].step[7].on=1;
        assert(preset_load(&trk[2],PRE_FULL_FIRST+i)==0);
        assert(trk[2].pre == PRE_FULL_FIRST+i && !trk[2].pre_ed);
        memcpy(want,MM_SOUNDS[i].raw,sizeof want);
        mm_controls_remap(want,MM_SOUNDS[i].source_track,2);
        mm_controls_get(&trk[2],got);
        assert(!memcmp(got,want,sizeof got));
        assert(trk[2].p[P_MUTE]==1 && trk[2].p[P_OUT]==BUS_EF && trk[2].p[P_NOTE]==11 && trk[2].step[7].on);
        assert(preset_rec_dec(preset_rec_enc(PRE_FULL_FIRST+i))==PRE_FULL_FIRST+i);
    }
    song.sel=0;
    preset_touch(&trk[0]);
    uint16_t original_pre = trk[0].pre;
    memcpy(before,trk[0].p,sizeof before);
    preset_list_open(); pl_move(PRE_FULL_FIRST); pl_move(PRE_FULL_FIRST+27);
    pl_close();
    assert(!memcmp(before,trk[0].p,sizeof before));
    assert(trk[0].pre == original_pre && trk[0].pre_ed);
    assert(preset_load(&trk[0],PRE_FULL_FIRST)==0);
    memcpy(before,trk[0].p,sizeof before);
    preset_list_open(); pl_move(0); pl_close();
    assert(!memcmp(before,trk[0].p,sizeof before));
    users=upr;
    preset_list_open(); pl_save();
    assert(!memcmp(&users,&upr,sizeof users));
    assert(!strcmp(ui.msg,"FULL SOUND STORE NEEDED"));
    puts("fullpreset_test: all passed");
    return 0;
}
