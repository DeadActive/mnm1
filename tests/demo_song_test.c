/* SPDX-License-Identifier: GPL-3.0-only */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#include "../firmware/src/demo_song.h"
#include <assert.h>
static uint8_t saved_flash[sizeof hflash];
static project_t saved_projects[4];
static upr_rec_t saved_users;
static uint64_t energy;
static void unchanged(void)
{
    assert(!memcmp(saved_flash,hflash,sizeof hflash));
    assert(!memcmp(saved_projects,proj_slot,sizeof saved_projects));
    assert(!memcmp(&saved_users,&upr,sizeof upr));
}
int main(void)
{
    memset(hflash,255,sizeof hflash);ui_host_init();flash_ok=1;
    memset(proj_slot,0,sizeof proj_memory);memset(&upr,0,sizeof upr);upr.magic=UPR_MAGIC;
    for(unsigned slot=0;slot<4;slot++){trk[0].step[slot].on=1;project_save(slot);assert(proj_ok(&proj_slot[slot]));}
    upr_set(3,DM_K808,trk[0].p);upr_write();
    memcpy(saved_flash,hflash,sizeof hflash);memcpy(saved_projects,proj_slot,sizeof saved_projects);saved_users=upr;
    song.playing=1;track_t before=trk[0];demo_song_load();assert(!memcmp(&before,&trk[0],sizeof before));song.playing=0;
    demo_song_load();unchanged();
    assert(chain.cfg.rsv[0]==1&&chain.cfg.count==8&&chain_valid(&chain.cfg));
    assert(!strcmp(chain.name,"AFTER HOURS")&&song.g[G_BPM]==112);
    assert(chain.cfg.options[0].mutes&&chain.cfg.options[3].transpose==5&&chain.cfg.options[4].offset==4);
    assert(trk[4].p[P_OUT]==BUS_OFF&&trk[5].p[P_IN]==IN_NEIB&&trk[5].p[P_OUT]==BUS_AB);
    for(unsigned k=0;k<NTRK;k++){
        assert(trk[k].model==DEMO_MODELS[k]&&seq_options_valid(&trk[k].seq));
        for(unsigned id=0;id<P_COUNT;id++){const param_desc_t*d=track_desc(&trk[k],id);assert(trk[k].p[id]>=d->min&&trk[k].p[id]<=d->max);}
        if(k<3)assert(trk[k].seq.scale_key==16);
    }
    assert(motion_valid(&DEMO_MOTION[0])&&motion_valid(&DEMO_MOTION[1]));
    assert(memcmp(DEMO_STEPS[0],DEMO_STEPS[1],sizeof DEMO_STEPS[0]));
    assert(memcmp(&DEMO_MOTION[0],&DEMO_MOTION[1],sizeof DEMO_MOTION[0]));
    assert(!demo_song_prepare());assert(chain.armed&&transport_req==1);unchanged();
    assert(demo_song_prepare()==2);
    for(unsigned p=0;p<2;p++)for(unsigned k=0;k<NTRK;k++){
        assert(chain.src[p].step[k]==DEMO_STEPS[p][k]);
        assert(chain.src[p].m==&DEMO_MOTION[p]&&chain.src[p].seq==DEMO_SEQ);
    }
    int32_t l[CTL],r[CTL];
    unsigned visited=0,audible=0,last_row=0,row_start=0,looped=0,rendered=0;
    const unsigned row_samples=8u*beat_samples();
    for(unsigned block=0;block<44100u*40u/CTL;block++){
        render_mix(l,r,CTL);rendered+=CTL;
        assert(chain.running&&chain.row<8&&chain.slot<2);
        if(chain.row!=last_row){
            unsigned elapsed=rendered-row_start;
            assert(elapsed+2u*CTL>=row_samples&&elapsed<=row_samples+2u*CTL);
            assert(chain.row==((last_row+1u)&7u));
            if(last_row==7&&chain.row==0)looped=1;
            last_row=chain.row;row_start=rendered;
        }
        visited|=1u<<chain.row;
        for(unsigned k=0;k<NTRK;k++){
            assert(trk[k].p[P_MUTE]==((chain.cfg.options[chain.row].mutes>>k)&1u));
            if(trk[k].peak>0)audible|=1u<<k;
        }
        for(unsigned i=0;i<CTL;i++){
            uint64_t sample=(uint64_t)((int64_t)l[i]*l[i])+(uint64_t)((int64_t)r[i]*r[i]);
            assert(UINT64_MAX-energy>=sample);energy+=sample;
        }
        if(looped)break;
    }
    assert(visited==255&&looped&&audible==((1u<<NTRK)-1u));
    assert(song.playing&&chain.running&&energy>1000000);
    unchanged();
    printf("demo arrangement: %u samples, all 8 rows + loop, all 6 tracks audible\n",rendered);
    chain.row=2;chain_apply(0);motion_step(&trk[3],0);
    assert(chain.slot==1&&mo.src==&DEMO_MOTION[1]&&trk[3].p[P_FWDTH]==38);
    chain.row=0;chain_apply(0);
    assert(trk[4].p[P_MUTE]&&trk[5].p[P_MUTE]&&!trk[0].p[P_MUTE]);
    chain.row=3;chain_apply(0);
    assert(song.seq.transpose==5&&!trk[4].p[P_MUTE]);
    trk[0].nt=trk[4].nt=0;
    assert(seq_note(&trk[0])==60&&seq_note(&trk[4])==65);
    chain.row=4;chain_apply(0);
    assert(song.seq.transpose==0&&seq_source_index(&trk[3],0)==4);
    assert(seq_step_at(&trk[3],12)==&DEMO_STEPS[0][3][0]);
    chain.row=7;chain_apply(0);assert(trk[4].p[P_MUTE]&&!trk[5].p[P_MUTE]);
    seq_stop();transport_req=0;unchanged();
    /* Explicit user SAVE persists the source flag; a later load can replay
     * immutable phrases without creating or overwriting any pattern slots. */
    project_save(2);init_all();transport_req=0;project_load(2);transport_req=0;
    assert(chain.cfg.rsv[0]==1&&chain.cfg.count==8);
    assert(!demo_song_prepare());seq_stop();transport_req=0;
    init_all();transport_req=0;assert(!chain.cfg.count&&!chain.cfg.rsv[0]);
    puts("demo_song_test: audible, valid, reloadable; saved library preserved");return 0;
}
