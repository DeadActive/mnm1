/* SPDX-License-Identifier: GPL-3.0-only
 * Full host engine with in-memory new-store flash only. Legacy flash inaccessible. */
#include "ui_host.h"
#include "../firmware/src/mm_library.h"
#include <assert.h>
static uint8_t libflash[MMS_END-MMS_BASE],empty_flash[sizeof libflash],ready_flash[sizeof libflash];
static unsigned writes,reads,fail_write,fail_read;
static track_t live_before[NTRK];
static song_t song_before;
static motion_store_t motion_before;
static chain_config_t chain_before;
static project_t projects_before[4];
static upr_rec_t users_before;
static void flash_bounds(uint32_t off,unsigned n){assert(off>=MMS_BASE&&off<MMS_RESERVE&&n<=MMS_RESERVE-off);}
static int lib_read(void *ctx,uint32_t off,void *dst,uint32_t n)
{
    (void)ctx;flash_bounds(off,n);if(++reads==fail_read)return -1;
    memcpy(dst,libflash+(off-MMS_BASE),n);return 0;
}
static int lib_erase(void *ctx,uint32_t off)
{
    (void)ctx;flash_bounds(off,MMS_SECTOR);assert(!(off%MMS_SECTOR));if(++writes==fail_write)return -1;
    memset(libflash+(off-MMS_BASE),255,MMS_SECTOR);return 0;
}
static int lib_program(void *ctx,uint32_t off,const void *src,uint32_t n)
{
    (void)ctx;flash_bounds(off,n);assert(n<=256&&(off%256)+n<=256);if(++writes==fail_write)return -1;
    for(unsigned i=0;i<n;++i){uint8_t *p=&libflash[off-MMS_BASE+i],v=((const uint8_t *)src)[i];assert((*p&v)==v);*p&=v;}return 0;
}
static mms_io libio={0,lib_read,lib_program,lib_erase};
static void io_reset(void){writes=reads=fail_write=fail_read=0;}
static void live_snapshot(void)
{
    memcpy(live_before,trk,sizeof trk);song_before=song;motion_before=mo.s;chain_before=chain.cfg;
}
static void live_unchanged(void)
{
    assert(!memcmp(live_before,trk,sizeof trk));assert(!memcmp(&song_before,&song,sizeof song));
    assert(!memcmp(&motion_before,&mo.s,sizeof mo.s));assert(!memcmp(&chain_before,&chain.cfg,sizeof chain.cfg));
}
static void defaults(void)
{
    host_init_live();
    for(unsigned k=0;k<NTRK;++k){
        drum_set_model(&trk[k],DM_MMGND);
        for(unsigned id=0;id<P_COUNT;++id)trk[k].p[id]=mmr_desc(DM_MMGND,id)->def;
        trk[k].model=DM_MMGND;trk[k].p[P_MODEL]=DM_MMGND;
    }
}
static void legacy_fixture(void)
{
    defaults();memset(proj_slot,0,sizeof proj_slot);
    for(unsigned slot=0;slot<4;++slot){
        song.sel=slot;song.g[G_BPM]=100+slot;
        for(unsigned k=0;k<NTRK;++k){
            trk[k].p[P_PAN]=(int16_t)((int)slot*10-20);trk[k].p[P_E0]=(int16_t)slot;
            trk[k].p[P_SLEN]=(int16_t)(16+slot);trk[k].step[slot].on=1;
            trk[k].pre=preset_first_of(DM_MMGND);trk[k].pre_ed=slot&1;
        }
        strcpy(chain.name,"LEGACY");chain.name[6]=(char)('1'+slot);chain.name[7]=0;
        project_save(slot);assert(proj_ok(&proj_slot[slot]));
    }
    memset(&upr,0,sizeof upr);upr.magic=UPR_MAGIC;
    for(unsigned slot=0;slot<128;++slot){
        upr.s[slot].used=1;upr.s[slot].model=DM_MMGND;
        for(unsigned i=0;i<8;++i)upr.s[slot].k[i]=(int8_t)DMODELS[DM_MMGND].edit[i].def;
        upr.s[slot].k[0]=(int8_t)(slot%12);
    }
    memcpy(projects_before,proj_slot,sizeof proj_slot);users_before=upr;live_snapshot();
}
static void boot_format_migrate(void)
{
    io_reset();memset(libflash,255,sizeof libflash);
    assert(library_boot(libio)==MMS_CORRUPT);assert(!writes&&!library_ready());
    assert(library_format_authorized(libio,0)==MMS_APPROVAL_REQUIRED);assert(!writes);
    assert(library_format_authorized(libio,MMS_OWNER_WAIVER)==0);assert(!library_ready());
    assert(library_save_pattern(0,0)==MMLIB_NOT_READY);
    io_reset();assert(library_boot(libio)==MMLIB_NOT_READY);assert(!writes);
    memcpy(empty_flash,libflash,sizeof libflash);
    assert(library_migrate_legacy()==0);assert(library_ready());live_unchanged();
    assert(!memcmp(projects_before,proj_slot,sizeof proj_slot));assert(!memcmp(&users_before,&upr,sizeof upr));
    unsigned before=writes;assert(library_migrate_legacy()==MMLIB_EXISTS);assert(writes==before);
    memcpy(ready_flash,libflash,sizeof libflash);
    io_reset();assert(library_boot(libio)==0);assert(!writes);
    for(unsigned slot=0;slot<4;++slot){
        assert(library_load_pattern(slot)==0);const project_t *p=&projects_before[slot];
        assert(song.sel==p->sel&&!memcmp(song.g,p->g,sizeof song.g));
        for(unsigned k=0;k<NTRK;++k){
            assert(!memcmp(trk[k].p,p->t[k].p,sizeof trk[k].p));
            assert(!memcmp(trk[k].step,p->t[k].step,sizeof trk[k].step));
            assert(preset_rec_enc(trk[k].pre)==p->pre[k]&&trk[k].pre_ed==p->pre_ed[k]);
        }
        assert(!memcmp(&mo.s,&p->motion,sizeof mo.s)&&!memcmp(&chain.cfg,&p->song,sizeof chain.cfg));
    }
    for(unsigned slot=0;slot<128;++slot){
        assert(library_sound_metadata(slot)->used);assert(library_load_sound(slot,&trk[0])==0);
        for(unsigned id=0;id<P_COUNT;++id)if(mmr_sound_param(id)){
            int expected=id==P_MODEL?DM_MMGND:id>=P_E0&&id<=P_E7?users_before.s[slot].k[id-P_E0]:TP[id].def;
            assert(trk[0].p[id]==expected);
        }
        assert(preset_rec_enc(trk[0].pre)==(0x8000u|slot));
    }
}
static void saves_and_groups(void)
{
    assert(library_load_pattern(0)==0);
    trk[0].p[P_PAN]=31;strcpy(chain.name,"NEWKIT");assert(library_save_kit(7)==0);
    assert(library_load_kit(6)==MMLIB_EMPTY);
    trk[0].p[P_PAN]=-31;assert(library_save_kit(6)==0);
    assert(library_load_kit(7)==0&&trk[0].p[P_PAN]==31);
    assert(library_load_kit(6)==0&&trk[0].p[P_PAN]==-31);
    trk[0].p[P_E0]=9;assert(library_save_pattern(7,7)==0);
    trk[0].p[P_E0]=1;assert(library_load_pattern(7)==0&&trk[0].p[P_E0]==9);
    trk[0].p[P_PAN]=-64;assert(library_save_sound(127,&trk[0],"LAST")==0);
    assert(!strcmp(library_sound_metadata(127)->name,"LAST"));
    trk[0].p[P_PAN]=63;assert(library_save_sound(126,&trk[0],"NEXT")==0);
    assert(library_load_sound(127,&trk[1])==0&&trk[1].p[P_PAN]==-64);
    assert(library_delete_sound(126)==0);assert(!library_sound_metadata(126)->used);
    assert(library_load_sound(126,&trk[1])==MMLIB_EMPTY);
    assert(library_load_sound(127,&trk[1])==0&&trk[1].p[P_PAN]==-64);
    unsigned before=writes;
    assert(library_save_pattern(8,0)==MMS_INVALID&&library_save_pattern(0,8)==MMS_INVALID);
    assert(library_load_pattern(8)==MMS_INVALID&&library_save_kit(8)==MMS_INVALID&&library_load_kit(8)==MMS_INVALID);
    assert(library_save_sound(128,&trk[0],"X")==MMS_INVALID&&library_delete_sound(128)==MMS_INVALID);
    assert(library_load_sound(128,&trk[0])==MMS_INVALID);assert(!library_sound_metadata(128));assert(writes==before);
}
static void failed_loads(void)
{
    memcpy(libflash,ready_flash,sizeof libflash);io_reset();assert(library_boot(libio)==0);
    reads=0;assert(library_prepare_pattern(0)==0);unsigned count=reads;
    for(unsigned fail=1;fail<=count;++fail){
        io_reset();assert(library_boot(libio)==0);live_snapshot();reads=0;fail_read=fail;
        assert(library_load_pattern(0)==MMS_IO);live_unchanged();assert(!library_ready());
    }
    io_reset();assert(library_boot(libio)==0);live_snapshot();
    assert(library_load_pattern(7)==MMS_NOT_FOUND);live_unchanged();
    /* Prepare can be followed by a short caller-controlled IRQ apply window. */
    assert(library_prepare_pattern(0)==0);reads=0;assert(library_apply_pattern()==0);assert(!reads);
    assert(library_apply_pattern()==MMLIB_NOT_READY);
}
static void pattern_save_cuts(void)
{
    memcpy(libflash,ready_flash,sizeof libflash);io_reset();assert(library_boot(libio)==0);
    assert(library_load_pattern(0)==0);trk[0].p[P_E0]=9;song.g[G_BPM]=211;
    writes=0;assert(library_save_pattern(0,0)==0);unsigned total=writes;
    for(unsigned cut=1;cut<=total;++cut){
        memcpy(libflash,ready_flash,sizeof libflash);io_reset();assert(library_boot(libio)==0);
        assert(library_load_pattern(0)==0);trk[0].p[P_E0]=9;song.g[G_BPM]=211;
        live_snapshot();writes=0;fail_write=cut;assert(library_save_pattern(0,0)!=0);live_unchanged();
        io_reset();assert(library_boot(libio)==0);assert(library_load_pattern(0)==0);
        assert(trk[0].p[P_E0]==0&&song.g[G_BPM]==100);
        trk[0].p[P_E0]=9;song.g[G_BPM]=211;assert(library_save_pattern(0,0)==0);
        io_reset();assert(library_boot(libio)==0);assert(library_load_pattern(0)==0);
        assert(trk[0].p[P_E0]==9&&song.g[G_BPM]==211);
        assert(library_load_pattern(1)==0&&song.g[G_BPM]==101); /* other kit in same object survives */
    }
    /* Semantic corruption with correct flash CRC must still be rejected before
     * any live parameters are changed. */
    memcpy(libflash,ready_flash,sizeof libflash);io_reset();assert(library_boot(libio)==0);
    assert(mmlib_read(0,&mmlib_work.pattern,sizeof mmlib_work.pattern)==0);
    mmlib_work.pattern.kit=255;
    assert(mms_stage(&mmlib_store,0,&mmlib_work.pattern,sizeof mmlib_work.pattern)==0);
    assert(mms_commit(&mmlib_store)==0);live_snapshot();
    assert(library_load_pattern(0)==MMS_CORRUPT);live_unchanged();
    assert(library_boot(libio)==MMS_CORRUPT&&!library_ready());live_unchanged();
    printf("  atomic pattern+kit failure/reboot/retry at all %u boundaries\n",total);
}
static void migration_cuts(void)
{
    memcpy(libflash,empty_flash,sizeof libflash);io_reset();assert(library_boot(libio)==MMLIB_NOT_READY);
    writes=0;assert(library_migrate_legacy()==0);unsigned total=writes;
    for(unsigned cut=1;cut<=total;++cut){
        memcpy(libflash,empty_flash,sizeof libflash);io_reset();assert(library_boot(libio)==MMLIB_NOT_READY);
        live_snapshot();writes=0;fail_write=cut;assert(library_migrate_legacy()!=0);live_unchanged();
        io_reset();assert(library_boot(libio)==MMLIB_NOT_READY);assert(!writes);
        assert(library_migrate_legacy()==0);assert(library_ready());live_unchanged();
        assert(!memcmp(projects_before,proj_slot,sizeof proj_slot));assert(!memcmp(&users_before,&upr,sizeof upr));
    }
    printf("  migration failure/reboot/retry at all %u erase/program boundaries\n",total);
    /* Empty legacy collections still publish initialized groups, no live fallback. */
    memset(proj_slot,0,sizeof proj_slot);memset(&upr,0,sizeof upr);
    memcpy(libflash,empty_flash,sizeof libflash);io_reset();assert(library_boot(libio)==MMLIB_NOT_READY);
    live_snapshot();assert(library_migrate_legacy()==0);live_unchanged();
    assert(library_load_pattern(0)==MMS_NOT_FOUND&&library_load_kit(0)==MMLIB_EMPTY);
    for(unsigned i=0;i<128;++i)assert(!library_sound_metadata(i)->used);
}
int main(void)
{
    legacy_fixture();boot_format_migrate();saves_and_groups();failed_loads();pattern_save_cuts();migration_cuts();
    printf("library: migration/full records/group preservation/read failures/bounds passed; scratch=%zu metadata=%zu\n",sizeof mmlib_work+sizeof mmlib_kits,sizeof mmlib_sounds);
    return 0;
}
