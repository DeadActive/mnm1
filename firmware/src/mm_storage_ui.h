/* SPDX-License-Identifier: GPL-3.0-only
 * Main-loop bridge for the expanded library. Reads precede IRQ-guarded apply;
 * destructive migration is explicit and never part of boot. */
#ifndef MNM_STORAGE_UI_H
#define MNM_STORAGE_UI_H
#include "mm_library.h"
static int expanded_pattern_name(uint32_t slot,char *b)
{
    const mmlib_pattern_meta *m=library_pattern_metadata(slot);
    name_set(b,m&&m->used?m->name:"");return m&&m->used;
}
static int kit_name(uint32_t slot,char *b)
{
    const mmlib_kit_meta *m=library_kit_metadata(slot);
    name_set(b,m&&m->used?m->name:"");return m&&m->used;
}
static void expanded_pattern_save(uint32_t slot)
{
    if(transport_busy()){ui_message("STOP TO SAVE");return;}
    int rc=library_save_pattern(slot,(unsigned)song.act[G_KSLOT-G_COUNT]-1);
    if(!rc){chain.from=(uint8_t)(slot+1);lastp.set=1;lastp.slot=(uint8_t)slot;settings_save();}
    project_save_error=rc?rc:settings_save_error;
    ui_message(rc?"SAVE ERROR":settings_save_error?"SAVED; BOOT ERROR":"SAVED");
}
static int expanded_kit_allowed(void)
{
    unsigned acid=0,reson=0,chord=0,modal=0;
    const mmr_kit_t *kit=&mmlib_kits.kit[mmlib_kit_slot%2];
    for(unsigned k=0;k<NTRK;k++){
        unsigned model=kit->sound[k].p[P_MODEL];
        unsigned kind=fxres_kind(DMODELS[model].render);
        acid+=model==DM_ACID;reson+=kind!=RS_OFF;chord+=kind==RS_CHORD;modal+=kind==RS_MODAL;
    }
    return acid<=ACID_MAXTRK&&reson<=RS_MAXTRK&&chord<=2&&modal<=RS_MAXMODAL;
}
static void expanded_reset_voices(void)
{
    multi_stop();ctlmod_reset();
    for(unsigned k=0;k<NTRK;k++){drum_cut(&trk[k]);fxs[k].model=255;}
    mmen_reset_all();mmen_lfo_reset();mmarp_reset();
}
static int expanded_pattern_load(uint32_t slot,int irq)
{
    if(chain_busy()){if(irq)ui_message("STOP TO LOAD");return 0;}
    int rc=library_prepare_pattern(slot);
    if(rc||!expanded_kit_allowed()){if(irq)ui_message(rc?"LOAD ERROR":"MODEL LIMIT");return 0;}
    transport_req=2;
    if(irq)fm1_irq_off();
    expanded_reset_voices();
    (void)library_apply_pattern();motion_forget();
    song.g[G_SLOT]=(int16_t)(slot+1);song.g[G_LOAD]=song.g[G_SAVE]=0;
    song.act[G_KSLOT-G_COUNT]=(int16_t)(mmlib_kit_slot+1);
    if(irq)fm1_irq_on();
    ui.force=1;if(irq)ui_message("LOADED");return 1;
}
static void kit_load(uint32_t slot)
{
    if(transport_busy()||chain_busy()){ui_message("STOP TO LOAD");return;}
    int rc=library_prepare_kit(slot);
    if(rc||!expanded_kit_allowed()){ui_message(rc?"LOAD ERROR":"MODEL LIMIT");return;}
    fm1_irq_off();expanded_reset_voices();(void)library_apply_kit();motion_forget();fm1_irq_on();
    song.act[G_KSLOT-G_COUNT]=(int16_t)(slot+1);
    ui.force=1;ui_message("LOADED");
}
static void kit_save(uint32_t slot)
{
    if(transport_busy()){ui_message("STOP TO SAVE");return;}
    ui_message(library_save_kit(slot)?"SAVE ERROR":"SAVED");
}
static void kit_rename(uint32_t slot,const char *name)
{
    if(transport_busy()){ui_message("STOP TO SAVE");return;}
    ui_message(library_rename_kit(slot,name)?"SAVE ERROR":"RENAMED");
}
static void expanded_pattern_rename(uint32_t slot,const char *name)
{
    if(transport_busy()){ui_message("STOP TO SAVE");return;}
    int rc=library_rename_pattern(slot,name);
    if(!rc&&chain.from==slot+1)name_set(chain.name,name);
    ui_message(rc?"SAVE ERROR":"RENAMED");
}
static seq_options_t expanded_chain_options[8][NTRK] __attribute__((section(".pool")));
static pattern_options_t expanded_chain_global[8];
/* Cache before PLAY: the audio interrupt never reads external flash. */
static uint32_t expanded_chain_prepare(void)
{
    if(transport_busy()||chain_busy())return 2;
    if(!chain.cfg.count||!chain_valid(&chain.cfg))return 1;
    unsigned used=0;
    for(unsigned row=0;row<chain.cfg.count;row++)used|=1u<<chain.cfg.row[row].slot;
    for(unsigned slot=0;slot<8;slot++)if(used&(1u<<slot)){
        if(library_prepare_pattern(slot))return 3+slot;
        expanded_chain_slot_t *cache=slot<6?&proj_memory.chain[slot]:&expanded_chain_extra[slot-6];
        const mmr_pattern_t *p=&mmlib_work.pattern;
        memcpy(cache->step,p->step,sizeof cache->step);cache->motion=p->motion;
        chain_src_t *d=&chain.src[slot];d->m=&cache->motion;
        memcpy(expanded_chain_options[slot],p->seq,sizeof p->seq);expanded_chain_global[slot]=p->options;
        d->seq=expanded_chain_options[slot];d->options=&expanded_chain_global[slot];
        for(unsigned k=0;k<NTRK;k++){
            d->step[k]=cache->step[k];d->model[k]=mmlib_kits.kit[p->kit%2].sound[k].p[P_MODEL];
            for(unsigned j=0;j<4;j++)for(unsigned x=0;x<MMR_PATTERN_N;x++)
                if(CHAIN_TIMING[j]==MMR_PATTERN_PARAMS[x])d->timing[k][j]=p->timing[k][x];
        }
    }
    mmlib_prepared=0;
    fm1_irq_off();chain.armed=1;fm1_irq_on();transport_req=1;return 0;
}
static void expanded_reset(void)
{
    memset(&full_user,0,sizeof full_user);
    smp_user_reserved=0;mmlib_ready=0;mmlib_prepared=0;mmlib_store.loaded=0;
}
static int expanded_get_sound(uint32_t slot,preset_t *out)
{
    const mmlib_sound_meta *m=library_sound_metadata(slot);
    if(!m||!m->used)return 0;
    memset(out,0,sizeof *out);out->model=m->model;
    if(m->name[0])memcpy(out->name,m->name,sizeof out->name);
    else upr_name(out->name,slot,m->model);
    return 1;
}
static int expanded_prepare_sound(uint32_t slot){return library_prepare_sound(slot);}
static void expanded_apply_sound(track_t *t)
{
    const mmlib_sound_meta *m=library_sound_metadata(mmlib_kit_slot);
    if(!m||mmlib_prepared!=3)return;
    if(t->model!=m->model || t->p[P_MODEL]!=m->model){
        drum_cut(t);unsigned k=mmen_index(t);mmen_ready[k]=0;
        if(k<NTRK)fxs[k].model=255;
    }
    (void)library_apply_sound(t);
}
static void expanded_attach(void)
{
    full_user.get=expanded_get_sound;
    full_user.prepare=expanded_prepare_sound;
    full_user.apply=expanded_apply_sound;
}
static int expanded_save_sound(uint32_t slot,const track_t *t)
{
    if(transport_busy()){ui_message("STOP TO SAVE");return -1;}
    char name[9];upr_name(name,slot,(uint32_t)t->p[P_MODEL]);
    int rc=library_save_sound(slot,t,name);
    if(rc)ui_message("SAVE ERROR");
    return rc;
}
static int expanded_delete_sound(uint32_t slot)
{
    if(transport_busy()){ui_message("STOP TO DELETE");return -1;}
    int rc=library_delete_sound(slot);
    if(rc)ui_message("DELETE ERROR");
    return rc;
}
#if FELUCCA_FLASH
static int expanded_read(void *ctx,uint32_t off,void *dst,uint32_t n)
{(void)ctx;return st_read(off,dst,n);}
static int expanded_program(void *ctx,uint32_t off,const void *src,uint32_t n)
{(void)ctx;return st_prog(off,src,n);}
static int expanded_erase(void *ctx,uint32_t off)
{(void)ctx;return st_erase(off);}
static mms_io expanded_io(void)
{mms_io io={0,expanded_read,expanded_program,expanded_erase};return io;}
static void expanded_boot(void)
{
    if(!flash_ok)return;
    int rc=library_boot(expanded_io());
    uint32_t a=0,b=0;
    if(!st_read(mms_address(MMS_CATALOG,0),&a,4) &&
       !st_read(mms_address(MMS_CATALOG,1),&b,4))
        smp_user_reserved=(uint8_t)(a==MMS_MAGIC||b==MMS_MAGIC);
    if(!rc || smp_user_reserved)expanded_attach();
}
static int expanded_migrate(int authorization)
{
    if(authorization!=MMS_VERIFIED_BACKUP&&authorization!=MMS_OWNER_WAIVER)return MMS_APPROVAL_REQUIRED;
    if(!flash_ok||transport_busy()||chain_busy())return MMS_INVALID;
    int boot=library_boot(expanded_io());
    if(boot==MMS_IO)return boot;
    if(library_ready())return MMLIB_EXISTS;
    if(!mmlib_store.loaded){
        if(smp_user_reserved)return MMS_CORRUPT; /* never reformat a damaged committed library */
        fm1_irq_off();
        smp_user_reserved=1;
        for(unsigned k=0;k<NTRK;k++)drum_cut(&trk[k]);
        usr_nz[1]=usr_nz[2]=0;slc_user_scan(1,0);slc_user_scan(2,0);
        fm1_irq_on();
        /* Invalidate both sample headers before erasing any sample payload.
         * A power cut can never make an interrupted migration play old audio. */
        const uint32_t zero=0;
        int rc=st_prog(SMP_USER_BASE+SMP_USER_SIZE,&zero,4);
        if(!rc)rc=st_prog(SMP_USER_BASE+2*SMP_USER_SIZE,&zero,4);
        if(rc){smp_user_reserved=0;return MMS_IO;}
        rc=library_format_authorized(expanded_io(),authorization);
        if(rc){smp_user_reserved=0;return rc;}
    }
    int rc=library_migrate_legacy();
    if(!rc){smp_user_reserved=1;expanded_attach();ui.force=1;}
    return rc;
}
#else
static void expanded_boot(void){}
#endif
#endif
