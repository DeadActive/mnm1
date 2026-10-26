/* SPDX-License-Identifier: GPL-3.0-only
 * Expanded pattern/kit/full-sound library. Include after project.c.
 * SINGLE WRITER; caller stops transport and serializes flash access. Prepare
 * functions only read/validate; apply functions only copy validated records.
 * Caller must guard apply/reset voices under its IRQ policy. No implicit format,
 * migration, current-project fallback, writes to legacy regions, or live resets.
 */
#ifndef MNM_LIBRARY_H
#define MNM_LIBRARY_H
#include "mm_store.h"
#include "mm_records.h"
enum { MMLIB_NOT_READY=-20, MMLIB_EXISTS=-21, MMLIB_EMPTY=-22 };
typedef struct {uint8_t used,model;char name[9];} mmlib_sound_meta;
static mms_store mmlib_store;
static uint8_t mmlib_ready, mmlib_prepared;
static unsigned mmlib_pattern_slot, mmlib_kit_slot;
static mmlib_sound_meta mmlib_sounds[MMR_SOUNDS];
/* Shared scratch: non-reentrant, about 5 KiB, never a 14 KiB sound cache. */
static union {mmr_pattern_t pattern;mmr_sounds_t sounds;} mmlib_work;
static mmr_kits_t mmlib_kits;
static int library_ready(void){return mmlib_ready && mmlib_store.loaded;}
static int mmlib_error(int rc)
{
    if(!mmlib_store.loaded)mmlib_ready=0;
    mmlib_prepared=0;return rc;
}
static int mmlib_read(unsigned id,void *out,unsigned bytes)
{
    int n=mms_read_object(&mmlib_store,id,out,bytes);
    return n==(int)bytes?0:n<0?mmlib_error(n):MMS_CORRUPT;
}
static int mmlib_read_kits(unsigned group)
{
    int rc=mmlib_read(8+group,&mmlib_kits,sizeof mmlib_kits);
    return rc?rc:mmr_kits_valid(&mmlib_kits)?0:MMS_CORRUPT;
}
static int mmlib_read_sounds(unsigned group)
{
    int rc=mmlib_read(12+group,&mmlib_work.sounds,sizeof mmlib_work.sounds);
    return rc?rc:mmr_sounds_valid(&mmlib_work.sounds)?0:MMS_CORRUPT;
}
static void mmlib_metadata(unsigned group)
{
    for(unsigned i=0;i<32;++i){
        const mmr_sound_t *s=&mmlib_work.sounds.sound[i];
        mmlib_sound_meta *m=&mmlib_sounds[group*32+i];
        memset(m,0,sizeof *m);
        if(s->used){m->used=1;m->model=s->p[P_MODEL];memcpy(m->name,s->name,sizeof m->name);}
    }
}
/* Read-only boot. Presence of all eight group objects is the completion marker:
 * migration publishes them together, and normal saves never remove objects. */
static int library_boot(mms_io io)
{
    mmlib_ready=mmlib_prepared=0;memset(mmlib_sounds,0,sizeof mmlib_sounds);
    int rc=mms_init(&mmlib_store,io);if(rc)return rc;
    rc=mms_load(&mmlib_store);if(rc)return rc;
    for(unsigned id=8;id<16;++id)if(!mmlib_store.committed.ref[id].present)return MMLIB_NOT_READY;
    uint8_t kits_used[8];
    for(unsigned group=0;group<4;++group){
        rc=mmlib_read_kits(group);if(rc)return rc;
        for(unsigned i=0;i<2;++i)kits_used[group*2+i]=mmlib_kits.kit[i].used;
        rc=mmlib_read_sounds(group);if(rc)return rc;mmlib_metadata(group);
    }
    for(unsigned id=0;id<8;++id)if(mmlib_store.committed.ref[id].present){
        rc=mmlib_read(id,&mmlib_work.pattern,sizeof mmlib_work.pattern);if(rc)return rc;
        if(!mmr_pattern_valid(&mmlib_work.pattern)||!kits_used[mmlib_work.pattern.kit])return MMS_CORRUPT;
    }
    mmlib_ready=1;return 0;
}
static int library_format_authorized(mms_io io,int authorization)
{
    if(authorization!=MMS_VERIFIED_BACKUP && authorization!=MMS_OWNER_WAIVER)return MMS_APPROVAL_REQUIRED;
    mmlib_ready=mmlib_prepared=0;memset(mmlib_sounds,0,sizeof mmlib_sounds);
    int rc=mms_init(&mmlib_store,io);if(rc)return rc;
    return mms_format(&mmlib_store,authorization); /* still NOT_READY until migration */
}
static const mmlib_sound_meta *library_sound_metadata(unsigned slot)
{
    return library_ready()&&slot<MMR_SOUNDS?&mmlib_sounds[slot]:0;
}
static int mmlib_commit(void)
{
    int rc=mms_commit(&mmlib_store);return rc?mmlib_error(rc):0;
}
static int library_save_pattern(unsigned slot,unsigned kit)
{
    mmlib_prepared=0;
    if(slot>=8||kit>=8)return MMS_INVALID;
    if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_kits(kit/2);if(rc)return rc;
    if(!mmr_kit_capture(&mmlib_kits.kit[kit%2],trk,chain.name)||
       !mmr_pattern_capture(&mmlib_work.pattern,trk,song.g,&mo.s,&chain.cfg,song.sel,kit,chain.name))return MMS_INVALID;
    mms_abort(&mmlib_store);
    rc=mms_stage(&mmlib_store,8+kit/2,&mmlib_kits,sizeof mmlib_kits);
    if(!rc)rc=mms_stage(&mmlib_store,slot,&mmlib_work.pattern,sizeof mmlib_work.pattern);
    if(rc){mms_abort(&mmlib_store);return mmlib_error(rc);}
    rc=mmlib_commit();if(!rc){mmlib_pattern_slot=slot;mmlib_kit_slot=kit;}return rc;
}
static int library_prepare_pattern(unsigned slot)
{
    mmlib_prepared=0;
    if(slot>=8)return MMS_INVALID;
    if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read(slot,&mmlib_work.pattern,sizeof mmlib_work.pattern);
    if(rc)return rc;
    if(!mmr_pattern_valid(&mmlib_work.pattern))return MMS_CORRUPT;
    unsigned kit=mmlib_work.pattern.kit;
    rc=mmlib_read_kits(kit/2);if(rc)return rc;
    if(!mmlib_kits.kit[kit%2].used)return MMLIB_EMPTY;
    mmlib_pattern_slot=slot;mmlib_kit_slot=kit;mmlib_prepared=1;return 0;
}
/* No flash access below: enter the caller's IRQ guard and reset voices only
 * AFTER prepare succeeded. All subsequent apply operations are infallible. */
static int library_apply_pattern(void)
{
    if(!library_ready()||mmlib_prepared!=1)return MMLIB_NOT_READY;
    mmr_kit_apply(trk,&mmlib_kits.kit[mmlib_kit_slot%2]);
    mmr_pattern_apply(trk,song.g,&mo.s,&chain.cfg,&mmlib_work.pattern);
    song.sel=mmlib_work.pattern.selected;
    proj_name_get(chain.name,mmlib_work.pattern.name);
    chain.from=(uint8_t)(mmlib_pattern_slot+1);
    for(unsigned t=0;t<NTRK;++t)trk[t].model=(uint8_t)trk[t].p[P_MODEL];
    mmlib_prepared=0;return 0;
}
static int library_load_pattern(unsigned slot)
{
    int rc=library_prepare_pattern(slot);return rc?rc:library_apply_pattern();
}
static int library_save_kit(unsigned slot)
{
    mmlib_prepared=0;if(slot>=8)return MMS_INVALID;if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_kits(slot/2);if(rc)return rc;
    if(!mmr_kit_capture(&mmlib_kits.kit[slot%2],trk,chain.name))return MMS_INVALID;
    mms_abort(&mmlib_store);rc=mms_stage(&mmlib_store,8+slot/2,&mmlib_kits,sizeof mmlib_kits);
    return rc?mmlib_error(rc):mmlib_commit();
}
static int library_prepare_kit(unsigned slot)
{
    mmlib_prepared=0;if(slot>=8)return MMS_INVALID;if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_kits(slot/2);if(rc)return rc;if(!mmlib_kits.kit[slot%2].used)return MMLIB_EMPTY;
    mmlib_kit_slot=slot;mmlib_prepared=2;return 0;
}
static int library_apply_kit(void)
{
    if(!library_ready()||mmlib_prepared!=2)return MMLIB_NOT_READY;
    mmr_kit_apply(trk,&mmlib_kits.kit[mmlib_kit_slot%2]);
    for(unsigned t=0;t<NTRK;++t)trk[t].model=(uint8_t)trk[t].p[P_MODEL];
    mmlib_prepared=0;return 0;
}
static int library_load_kit(unsigned slot)
{
    int rc=library_prepare_kit(slot);return rc?rc:library_apply_kit();
}
static int library_save_sound(unsigned slot,const track_t *t,const char *name)
{
    mmlib_prepared=0;if(slot>=128||!t)return MMS_INVALID;if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_sounds(slot/32);if(rc)return rc;
    if(!mmr_sound_capture(&mmlib_work.sounds.sound[slot%32],t,name))return MMS_INVALID;
    mms_abort(&mmlib_store);rc=mms_stage(&mmlib_store,12+slot/32,&mmlib_work.sounds,sizeof mmlib_work.sounds);
    if(!rc)rc=mmlib_commit();if(!rc)mmlib_metadata(slot/32);return rc?mmlib_error(rc):0;
}
static int library_delete_sound(unsigned slot)
{
    mmlib_prepared=0;if(slot>=128)return MMS_INVALID;if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_sounds(slot/32);if(rc)return rc;
    memset(&mmlib_work.sounds.sound[slot%32],0,sizeof(mmr_sound_t));
    mms_abort(&mmlib_store);rc=mms_stage(&mmlib_store,12+slot/32,&mmlib_work.sounds,sizeof mmlib_work.sounds);
    if(!rc)rc=mmlib_commit();if(!rc)mmlib_metadata(slot/32);return rc?mmlib_error(rc):0;
}
static int library_prepare_sound(unsigned slot)
{
    mmlib_prepared=0;if(slot>=128)return MMS_INVALID;if(!library_ready())return MMLIB_NOT_READY;
    int rc=mmlib_read_sounds(slot/32);if(rc)return rc;
    if(!mmlib_work.sounds.sound[slot%32].used)return MMLIB_EMPTY;
    mmlib_kit_slot=slot;mmlib_prepared=3;return 0;
}
static int library_apply_sound(track_t *t)
{
    if(!t)return MMS_INVALID;if(!library_ready()||mmlib_prepared!=3)return MMLIB_NOT_READY;
    mmr_sound_apply(t,&mmlib_work.sounds.sound[mmlib_kit_slot%32]);
    t->model=(uint8_t)t->p[P_MODEL];mmlib_prepared=0;return 0;
}
static int library_load_sound(unsigned slot,track_t *t)
{
    if(!t)return MMS_INVALID;int rc=library_prepare_sound(slot);return rc?rc:library_apply_sound(t);
}
/* Convert checked legacy project RAM; never load it into the live engine. A
 * checksum-valid but semantically invalid record aborts migration explicitly. */
static int mmlib_project_kit(mmr_kit_t *kit,const project_t *p)
{
    memset(kit,0,sizeof *kit);kit->used=1;
    char name[NAME_LEN+1];proj_name_get(name,p->name);mmr_name_copy(kit->name,sizeof kit->name,name);
    for(unsigned t=0;t<NTRK;++t){
        mmr_sound_t *s=&kit->sound[t];s->used=1;s->preset_ref=p->pre[t];s->edited=p->pre_ed[t];
        for(unsigned id=0;id<P_COUNT;++id)if(mmr_sound_param(id)){
            int v=id<P_NSTORED?p->t[t].p[id]:proj_xp_get(&p->t[t],id);
            if(v< -128||v>127)return MMS_CORRUPT;s->p[id]=(uint8_t)v;
        }
        for(unsigned j=0;j<MMR_KIT_N;++j)kit->config[t][j]=p->t[t].p[MMR_KIT_PARAMS[j]];
    }
    return mmr_kit_valid(kit)?0:MMS_CORRUPT;
}
static int mmlib_project_pattern(mmr_pattern_t *out,const project_t *p,unsigned kit)
{
    memset(out,0,sizeof *out);out->version=MMR_VERSION;out->used=1;out->kit=(uint8_t)kit;out->selected=p->sel;
    memcpy(out->name,p->name,sizeof out->name);memcpy(out->g,p->g,sizeof out->g);
    out->motion=p->motion;out->song=p->song;
    for(unsigned t=0;t<NTRK;++t){
        memcpy(out->step[t],p->t[t].step,sizeof out->step[t]);
        for(unsigned i=0;i<NSTEP;++i)out->step[t][i].rsv=0;
        for(unsigned j=0;j<MMR_PATTERN_N;++j)out->timing[t][j]=p->t[t].p[MMR_PATTERN_PARAMS[j]];
    }
    return mmr_pattern_valid(out)?0:MMS_CORRUPT;
}
static int library_migrate_legacy(void)
{
    mmlib_prepared=0;
    if(!mmlib_store.loaded)return MMLIB_NOT_READY;
    for(unsigned id=0;id<16;++id)if(mmlib_store.committed.ref[id].present)return MMLIB_EXISTS;
    mms_abort(&mmlib_store);int rc=0;
    for(unsigned group=0;group<4;++group){
        memset(&mmlib_kits,0,sizeof mmlib_kits);mmlib_kits.version=MMR_VERSION;
        for(unsigned i=0;i<2;++i){
            unsigned slot=group*2+i;
            if(slot<4 && proj_ok(&proj_slot[slot])){
                rc=mmlib_project_kit(&mmlib_kits.kit[i],&proj_slot[slot]);if(rc)goto fail;
                rc=mmlib_project_pattern(&mmlib_work.pattern,&proj_slot[slot],slot);if(rc)goto fail;
                rc=mms_stage(&mmlib_store,slot,&mmlib_work.pattern,sizeof mmlib_work.pattern);if(rc)goto fail;
            }
        }
        rc=mms_stage(&mmlib_store,8+group,&mmlib_kits,sizeof mmlib_kits);if(rc)goto fail;
    }
    for(unsigned group=0;group<4;++group){
        memset(&mmlib_work.sounds,0,sizeof mmlib_work.sounds);mmlib_work.sounds.version=MMR_VERSION;
        for(unsigned i=0;i<32;++i){
            unsigned slot=group*32+i;const upr_slot_t *old=&upr.s[slot];
            if(upr.magic!=UPR_MAGIC || old->used!=1 || old->model>=MM_MODEL_FIRST)continue;
            mmr_sound_t *s=&mmlib_work.sounds.sound[i];s->used=1;s->preset_ref=(uint16_t)(0x8000u|slot);
            for(unsigned id=0;id<P_COUNT;++id)if(mmr_sound_param(id))s->p[id]=(uint8_t)TP[id].def;
            s->p[P_MODEL]=old->model;
            for(unsigned k=0;k<8;++k)s->p[P_E0+k]=(uint8_t)old->k[k];
            if(!mmr_sound_valid(s)){rc=MMS_CORRUPT;goto fail;}
        }
        rc=mms_stage(&mmlib_store,12+group,&mmlib_work.sounds,sizeof mmlib_work.sounds);if(rc)goto fail;
    }
    rc=mmlib_commit();if(rc)return rc;
    return library_boot(mmlib_store.io);
fail:
    mms_abort(&mmlib_store);return mmlib_error(rc);
}
#endif
