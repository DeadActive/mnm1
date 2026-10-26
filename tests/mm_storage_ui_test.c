/* SPDX-License-Identifier: GPL-3.0-only
 * Production expanded-storage bridge against simulated flash; no hardware I/O. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#include <assert.h>
static uint8_t snapshot[sizeof hflash];
static project_t old_projects[4];
static upr_rec_t old_users;
static track_t before;

static void sample(unsigned slot)
{
    smp_user_hdr_t h={0};
    h.magic=SMP_USER_MAGIC;h.version=1;h.nz=1;h.data_len=512;
    h.zone[0].n=64;h.zone[0].rate=1u<<16;h.zone[0].le=63;h.zone[0].hi=127;
    memset(hflash+SMP_USER_BASE+slot*SMP_USER_SIZE,0x31+slot,SMP_USER_SIZE);
    memcpy(hflash+SMP_USER_BASE+slot*SMP_USER_SIZE,&h,sizeof h);
}
static void seed_legacy(void)
{
    memset(hflash,0xFF,sizeof hflash);
    memset(proj_slot,0,sizeof proj_slot);
    ui_host_init();flash_ok=1;transport_req=0;song.playing=0;
    for(unsigned slot=0;slot<4;slot++){
        for(unsigned k=0;k<NTRK;k++){
            drum_set_model(&trk[k],DM_MMSAW);
            trk[k].p[P_E0]=(int16_t)(slot*5+k);
            memset(trk[k].step,0,sizeof trk[k].step);
            trk[k].step[slot+k].on=1;trk[k].step[slot+k].nt=(int8_t)slot;
        }
        char name[13]="PROJECT A";name[8]=(char)('A'+slot);name_set(chain.name,name);
        project_save(slot);assert(proj_ok(&proj_slot[slot]));old_projects[slot]=proj_slot[slot];
    }
    memset(&upr,0,sizeof upr);upr.magic=UPR_MAGIC;
    for(unsigned slot=0;slot<PRE_NUSER;slot++){
        drum_set_model(&trk[0],DM_CPEFM);
        trk[0].p[P_E0]=(int16_t)((int)(slot%49)-24);
        upr_set(slot,DM_CPEFM,trk[0].p);
    }
    old_users=upr;assert(!st_save(OBJ_UPRESET0+1,&upr,sizeof upr));
    for(unsigned k=0;k<SMP_USER_SLOTS;k++)sample(k);
    expanded_reset();
    for(unsigned slot=0;slot<4;slot++) {
        assert(!mmlib_project_kit(&mmlib_kits.kit[0],&proj_slot[slot]));
        assert(!mmlib_project_pattern(&mmlib_work.pattern,&proj_slot[slot],slot));
    }
}
static void test_boot_and_authorization(void)
{
    memcpy(snapshot,hflash,sizeof hflash);
    persist_boot();
    assert(!memcmp(snapshot,hflash,sizeof hflash));
    assert(!library_ready()&&!full_user.get&&!smp_user_reserved);
    assert(usr_nz[0]==1&&usr_nz[1]==1&&usr_nz[2]==1);
    assert(expanded_migrate(0)==MMS_APPROVAL_REQUIRED);
    song.playing=1;assert(expanded_migrate(MMS_OWNER_WAIVER)==MMS_INVALID);song.playing=0;
    transport_req=1;assert(expanded_migrate(MMS_OWNER_WAIVER)==MMS_INVALID);transport_req=0;
    chain.armed=1;assert(expanded_migrate(MMS_OWNER_WAIVER)==MMS_INVALID);chain.armed=0;
    assert(!memcmp(snapshot,hflash,sizeof hflash));
    int migration_rc=expanded_migrate(MMS_OWNER_WAIVER);
    if(migration_rc)fprintf(stderr,"migration rc %d\n",migration_rc);
    assert(!migration_rc);
    assert(library_ready()&&full_user.get&&smp_user_reserved);
    assert(!memcmp(snapshot,hflash,MMS_BASE));
    assert(!memcmp(snapshot+MMS_END,hflash+MMS_END,sizeof hflash-MMS_END));
    assert(usr_nz[0]==1&&!usr_nz[1]&&!usr_nz[2]);
    memcpy(snapshot,hflash,sizeof hflash);
    assert(expanded_migrate(MMS_OWNER_WAIVER)==MMLIB_EXISTS);
    assert(!memcmp(snapshot,hflash,sizeof hflash));
}
static void test_conversion(void)
{
    for(unsigned slot=0;slot<4;slot++){
        assert(!library_prepare_pattern(slot));
        const mmr_pattern_t *p=&mmlib_work.pattern;
        assert(!memcmp(p->name,old_projects[slot].name,NAME_LEN));
        assert(!memcmp(&p->motion,&old_projects[slot].motion,sizeof p->motion));
        for(unsigned k=0;k<NTRK;k++){
            assert(!memcmp(p->step[k],old_projects[slot].t[k].step,sizeof p->step[k]));
            const mmr_sound_t *s=&mmlib_kits.kit[slot%2].sound[k];
            for(unsigned id=0;id<P_COUNT;id++)if(mmr_sound_param(id))
                assert(mmr_decode(s->p[id],mmr_desc(s->p[P_MODEL],id))==old_projects[slot].t[k].p[id]);
        }
    }
    for(unsigned slot=0;slot<PRE_NUSER;slot++){
        preset_t meta;
        assert(preset_get(PRE_NFACT+slot,&meta)&&meta.model==old_users.s[slot].model&&meta.name[0]);
        assert(!preset_load(&trk[0],PRE_NFACT+slot));
        for(unsigned k=0;k<8;k++)assert(trk[0].p[P_E0+k]==old_users.s[slot].k[k]);
    }
    assert(preset_user_free()==-1);
}
static void simulated_restart(void)
{
    memcpy(snapshot,hflash,sizeof hflash);
    memset(proj_slot,0,sizeof proj_slot);memset(&upr,0,sizeof upr);
    ui_host_init();persist_boot();
    assert(!memcmp(snapshot,hflash,sizeof hflash));
    assert(library_ready()&&full_user.get&&smp_user_reserved);
    assert(usr_nz[0]==1&&!usr_nz[1]&&!usr_nz[2]);
    smp_user_scan(1);smp_user_scan(2);assert(!usr_nz[1]&&!usr_nz[2]);
}
static void test_full_sound_ui(void)
{
    uint8_t wanted[71],got[71];int16_t wanted_params[P_COUNT];preset_t meta;
    song.sel=0;transport_req=0;song.playing=0;
    preset_user_delete(17);assert(!preset_get(PRE_NFACT+17,&meta)&&preset_user_free()==17);
    assert(!preset_load(&trk[0],PRE_FULL_FIRST+1));
    trk[0].p[P_PAN]=-37;trk[0].p[P_MODR]=119;trk[0].p[P_PTCH]=105;
    mm_controls_get(&trk[0],wanted);memcpy(wanted_params,trk[0].p,sizeof wanted_params);
    memcpy(snapshot,hflash,sizeof hflash);
    song.playing=1;assert(preset_user_save(17,&trk[0])<0);song.playing=0;
    assert(!memcmp(snapshot,hflash,sizeof hflash));
    preset_list_open();pl_cursor(PRE_NFACT+17);pl_save();pl_close();
    assert(trk[0].pre==PRE_NFACT+17&&preset_get(PRE_NFACT+17,&meta));
    drum_set_model(&trk[0],DM_K808);
    trk[0].p[P_OUT]=BUS_EF;trk[0].p[P_MUTE]=1;trk[0].p[P_NOTE]=27;
    assert(!preset_load(&trk[0],PRE_NFACT+17));
    mm_controls_get(&trk[0],got);assert(!memcmp(wanted,got,sizeof got));
    for(unsigned id=0;id<P_COUNT;id++)if(mmr_sound_param(id))assert(trk[0].p[id]==wanted_params[id]);
    assert(trk[0].p[P_OUT]==BUS_EF&&trk[0].p[P_MUTE]&&trk[0].p[P_NOTE]==27);
    assert(trk[0].pre==PRE_NFACT+17&&!trk[0].pre_ed);
    drum_set_model(&trk[0],DM_MMSAW);trk[0].pre_ed=1;before=trk[0];
    preset_list_open();pl_move(PRE_NFACT+17);
    mm_controls_get(&trk[0],got);assert(!memcmp(wanted,got,sizeof got)&&ui.pr.pv);
    pl_close();
    assert(!memcmp(before.p,trk[0].p,sizeof before.p));
    assert(trk[0].pre==before.pre&&trk[0].pre_ed==before.pre_ed);
    simulated_restart();assert(!preset_load(&trk[0],PRE_NFACT+17));
    mm_controls_get(&trk[0],got);assert(!memcmp(wanted,got,sizeof got));
    memcpy(snapshot,hflash,sizeof hflash);song.playing=1;preset_user_delete(17);song.playing=0;
    assert(!memcmp(snapshot,hflash,sizeof hflash)&&preset_get(PRE_NFACT+17,&meta));
    preset_user_delete(17);assert(preset_user_free()==17&&!preset_get(PRE_NFACT+17,&meta));
    simulated_restart();assert(!preset_get(PRE_NFACT+17,&meta));
}
static unsigned failed_reads;
static int failed_read(void *ctx,uint32_t off,void *dst,uint32_t n)
{(void)ctx;(void)off;(void)dst;(void)n;failed_reads++;return -1;}
static void test_read_failure(void)
{
    assert(!preset_load(&trk[0],PRE_FULL_FIRST));
    before=trk[0];
    int (*read_fn)(void*,uint32_t,void*,uint32_t)=mmlib_store.io.read;
    mmlib_store.io.read=failed_read;
    memcpy(snapshot,hflash,sizeof hflash);
    assert(preset_load(&trk[0],PRE_NFACT+18)<0);
    assert(!memcmp(&before,&trk[0],sizeof before));
    assert(!library_ready());                       /* uncertain flash blocks further writes */
    mmlib_store.io.read=read_fn;
    assert(!library_boot(expanded_io()));
    mmlib_store.io.read=failed_read;
    preset_list_open();pl_move(PRE_NFACT+18);
    assert(!ui.pr.pv&&!memcmp(&before,&trk[0],sizeof before));
    assert(!strcmp(ui.msg,"READ ERROR"));
    assert(!memcmp(snapshot,hflash,sizeof hflash));
    mmlib_store.io.read=read_fn;pl_close();
}
static void page_for(unsigned id)
{
    for(unsigned p=0;p<NPAGES;p++)for(unsigned k=0;k<4;k++)
        if(PAGES[p].scope==SC_GLOBAL&&PAGES[p].id[k]==id){ui.page=p;return;}
    assert(0);
}
static void turn_action(unsigned id)
{
    page_for(id);
    for(unsigned k=0;k<4;k++)if(page_id(cur_page(),k)==id){edit_param(k,1);return;}
    assert(0);
}
static void test_kits_patterns(void)
{
    char name[13];
    transport_req=0;song.playing=0;chain_stop();
    for(unsigned k=0;k<NTRK;k++)drum_set_model(&trk[k],DM_MMSAW);
    song.act[G_KSLOT-G_COUNT]=8;name_set(chain.name,"KIT H");
    trk[0].p[P_E1]=43;
    ui.arm=0;memcpy(snapshot,hflash,sizeof hflash);
    turn_action(G_KSAVE);assert(ui.arm==G_KSAVE&&!memcmp(snapshot,hflash,sizeof hflash));
    turn_action(G_KSAVE);assert(!strcmp(ui.msg,"SAVED"));
    assert(kit_name(7,name)&&!strcmp(name,"KIT H"));
    trk[0].p[P_E1]=17;ui.arm=0;
    turn_action(G_KLOAD);assert(trk[0].p[P_E1]==17);
    turn_action(G_KLOAD);assert(trk[0].p[P_E1]==43);
    turn_action(G_KNAME);assert(nm.kind==NK_KIT_RENAME&&nm.slot==7);
    name_set(nm.s,"  H NAMED  ");nm.len=str_len(nm.s);nm.num=0;
    song.playing=1;name_ok();assert(nm.kind==NK_KIT_RENAME&&!strcmp(ui.msg,"STOP TO SAVE"));
    song.playing=0;name_ok();assert(!nm.kind&&kit_name(7,name)&&!strcmp(name,"H NAMED"));
    turn_action(G_KNAME);name_set(nm.s,"CANCELLED");name_close();
    assert(kit_name(7,name)&&!strcmp(name,"H NAMED"));
    name_set(chain.name,"OTHER TITLE");kit_save(7);
    assert(kit_name(7,name)&&!strcmp(name,"H NAMED"));
    for(unsigned slot=4;slot<8;slot++){
        trk[0].p[P_SLEN]=(int16_t)(slot+9);trk[0].p[P_E1]=(int16_t)(slot+30);
        memset(trk[0].step,0,sizeof trk[0].step);trk[0].step[slot].on=1;
        name_set(chain.name,"PATTERN E");chain.name[8]=(char)('A'+slot);
        project_save(slot);assert(!strcmp(ui.msg,"SAVED"));
        assert(project_name(slot,name)&&name[8]=='A'+slot);
        assert(lastp.slot==slot);
        assert(kit_name(7,name)&&!strcmp(name,"H NAMED"));
    }
    song.g[G_SLOT]=8;name_rename();assert(nm.kind==NK_RENAME&&nm.slot==7);
    name_set(nm.s,"PATTERN H2");nm.len=str_len(nm.s);name_ok();
    assert(project_name(7,name)&&!strcmp(name,"PATTERN H2"));
    simulated_restart();assert(lastp.set&&lastp.slot==7);
    project_boot();assert(song.g[G_SLOT]==8&&trk[0].p[P_SLEN]==16&&trk[0].step[7].on);
    transport_req=0;
    for(unsigned slot=4;slot<8;slot++){
        project_load(slot);transport_req=0;
        assert(trk[0].p[P_SLEN]==slot+9&&trk[0].step[slot].on);
        assert(song.g[G_SLOT]==slot+1&&!song.g[G_LOAD]&&!song.g[G_SAVE]);
    }
    /* A kit with too many expensive models can be stored, never activated. */
    for(unsigned k=0;k<NTRK;k++){trk[k].p[P_MODEL]=DM_ACID;trk[k].model=DM_ACID;for(unsigned j=0;j<8;j++)trk[k].p[P_E0+j]=DMODELS[DM_ACID].edit[j].def;}
    kit_save(6);assert(!strcmp(ui.msg,"SAVED"));
    for(unsigned k=0;k<NTRK;k++)drum_set_model(&trk[k],DM_MMSAW);
    before=trk[0];kit_load(6);
    assert(!strcmp(ui.msg,"MODEL LIMIT")&&!memcmp(&before,&trk[0],sizeof before));
    kit_save(6);assert(!strcmp(ui.msg,"SAVED"));
    before=trk[0];int (*rd)(void*,uint32_t,void*,uint32_t)=mmlib_store.io.read;
    mmlib_store.io.read=failed_read;kit_load(7);
    assert(!strcmp(ui.msg,"LOAD ERROR")&&!memcmp(&before,&trk[0],sizeof before));
    mmlib_store.io.read=rd;assert(!library_boot(expanded_io()));
    mmlib_store.io.read=failed_read;project_load(7);
    assert(!memcmp(&before,&trk[0],sizeof before));
    mmlib_store.io.read=rd;assert(!library_boot(expanded_io()));
    transport_req=0;
    memset(&chain.cfg,0,sizeof chain.cfg);chain.cfg.count=2;
    chain.cfg.row[0].slot=4;chain.cfg.row[0].repeat=1;
    chain.cfg.row[1].slot=7;chain.cfg.row[1].repeat=1;
    assert(!chain_prepare());
    assert(chain.src[4].step[0]==proj_memory.chain[4].step[0]);
    assert(chain.src[7].step[0]==expanded_chain_extra[1].step[0]);
    assert(chain.src[4].step[0][4].on&&chain.src[7].step[0][7].on);
    assert(chain.src[4].m==&proj_memory.chain[4].motion&&chain.src[7].m==&expanded_chain_extra[1].motion);
    failed_reads=0;mmlib_store.io.read=failed_read;chain_start();
    assert(chain.slot==4&&trk[0].p[P_SLEN]==13&&seq_steps(&trk[0])[4].on&&mo.src==chain.src[4].m);
    chain.row=1;chain_apply(0);
    assert(chain.slot==7&&trk[0].p[P_SLEN]==16&&seq_steps(&trk[0])[7].on&&mo.src==chain.src[7].m);
    assert(library_ready()&&!failed_reads);chain_stop();transport_req=0;mmlib_store.io.read=rd;
}
int main(void)
{
    seed_legacy();test_boot_and_authorization();test_conversion();simulated_restart();test_full_sound_ui();test_kits_patterns();test_read_failure();
    puts("mm_storage_ui_test: all passed");return 0;
}
