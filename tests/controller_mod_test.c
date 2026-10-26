/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include "../firmware/src/controller_mod.h"
#include <assert.h>
static void fresh(unsigned model)
{
    ctlmod_reset();ui_host_init();ctlmod_reset();
    drum_set_model(&trk[0],model);if(mmen_is_model(model))mmen_default(&trk[0]);
    memset(&trk[0].perf,0,sizeof trk[0].perf);
    for(unsigned l=0;l<3;l++)trk[0].p[lfo_base(l)+LF_DPTH]=0;
}
static void test_sources(void)
{
    fresh(DM_MMSAW);track_t *t=&trk[0];t->p[P_DIST]=10;
    t->perf.dest[0]=P_DIST+1;t->perf.amount[0]=63;
    int temp=ctlmod_note_begin(t,64,0,0);assert(temp&&t->p[P_DIST]==74);
    ctlmod_note_end(t,temp);assert(t->p[P_DIST]==10);
    t->perf.amount[0]=-64;temp=ctlmod_note_begin(t,127,0,0);assert(t->p[P_DIST]==0);
    ctlmod_note_end(t,temp);
    t->perf.amount[0]=63;t->perf.dest[1]=P_DIST+1;t->perf.amount[1]=-64;
    ctlmod_begin();assert(t->p[P_DIST]==10&&ctlmod_state[0].count==1);ctlmod_end();
    memset(&t->perf,0,sizeof t->perf);t->perf.dest[2]=P_DIST+1;t->perf.amount[2]=63;
    temp=ctlmod_note_begin(t,96,7,ST_NOAMP);assert(t->p[P_DIST]==77);ctlmod_note_end(t,temp);
    temp=ctlmod_note_begin(t,50,-60,ST_NOPITCH);assert(t->p[P_DIST]==77&&ctlmod_state[0].velocity==50);
    ctlmod_note_end(t,temp);
    assert(!ctlmod_note_begin(t,1,67,ST_NOTEOFF)&&ctlmod_state[0].key==67&&ctlmod_state[0].velocity==50);
    ctlmod_begin();assert(t->p[P_DIST]==77);ctlmod_end();assert(t->p[P_DIST]==10);
    track_t scratch=*t;assert(!ctlmod_note_begin(&scratch,127,0,0)&&scratch.p[P_DIST]==10);
}
static void test_lock_lfo(void)
{
    fresh(DM_MMSAW);track_t *t=&trk[0];t->p[P_E0]=0;
    t->perf.dest[0]=P_E0+1;t->perf.amount[0]=20;
    t->p[P_LFO1+LF_PAGE]=LP_SYN;t->p[P_LFO1+LF_DEST]=0;
    t->p[P_LFO1+LF_WAVE]=LW_SQR;t->p[P_LFO1+LF_DPTH]=20;
    ctlmod_begin();assert(t->p[P_E0]==15);lfo_apply(CTL);
    assert(t->lnum&&ctlmod_base_slot(t,P_E0)&&*ctlmod_base_slot(t,P_E0)==0);
    *ctlmod_base_slot(t,P_E0)=-10;ctlmod_refresh(t);
    assert(t->lsave[0]==5); /* lock base -10 plus controller15, before LFO */
    ctlmod_note_begin(t,63,0,0);assert(t->lsave[0]==-3);
    lfo_restore();ctlmod_end();assert(t->p[P_E0]==-10);
    /* Disabled controller routing retains existing LFO base refresh behavior. */
    memset(&t->perf,0,sizeof t->perf);lfo_apply(CTL);t->lsave[0]=-5;
    ctlmod_refresh(t);assert(t->lsave[0]==-5);lfo_restore();assert(t->p[P_E0]==-5);
}
static void test_disabled_and_motion(void)
{
    fresh(DM_MMSAW);track_t *t=&trk[0];
    t->p[P_LFO1+LF_PAGE]=LP_SYN;t->p[P_LFO1+LF_DEST]=0;
    t->p[P_LFO1+LF_WAVE]=LW_SQR;t->p[P_LFO1+LF_DPTH]=20;
    ctlmod_begin();lfo_apply(CTL);track_t prior=*t;
    assert(!ctlmod_note_begin(t,70,5,ST_NOLFO));
    assert(!memcmp(&prior,t,sizeof prior));lfo_restore();ctlmod_end();
    t->p[P_E0]=0;t->perf.dest[0]=P_E0+1;t->perf.amount[0]=20;
    memset(&mo,0,sizeof mo);mo.s.on=1;mo.s.count=1;
    mo.s.ev[0]=(motion_event_t){0,0,P_E0,-10};
    ctlmod_begin();lfo_apply(CTL);motion_step(t,0);
    assert(*ctlmod_base_slot(t,P_E0)==-10&&mo.base[0][P_E0]==0);
    assert(t->lsave[0]==-2); /* velocity70 produces +8, lock supplies -10 */
    motion_restore(0);assert(*ctlmod_base_slot(t,P_E0)==0);
    lfo_restore();ctlmod_end();assert(t->p[P_E0]==0);
}
static void test_pressed_input(void)
{
    fresh(DM_MMSAW);track_t *t=&trk[0];t->p[P_DIST]=0;
    t->perf.dest[0]=P_DIST+1;t->perf.amount[0]=63;
    t->perf.dest[2]=P_PAN+1;t->perf.amount[2]=63;t->p[P_PAN]=-64;
    ctlmod_input(t,41,-12); /* manual ARP: input arrives without a hit */
    assert(t->p[P_DIST]==0&&t->p[P_PAN]==-64&&!ctlmod_state[0].applied);
    ctlmod_begin();assert(t->p[P_DIST]==41);
    int pan=t->p[P_PAN];arp_self=1;
    assert(!ctlmod_note_begin(t,100,24,0));
    assert(t->p[P_DIST]==41&&t->p[P_PAN]==pan&&ctlmod_state[0].key==48);
    ctlmod_end();
    int temp=ctlmod_note_begin(t,127,48,0); /* generated child outside a block */
    assert(temp&&t->p[P_DIST]==41&&t->p[P_PAN]==pan);
    ctlmod_note_end(t,temp);arp_self=0;assert(!t->p[P_DIST]&&t->p[P_PAN]==-64);
    ctlmod_begin();lfo_apply(CTL);track_t before=*t;
    ctlmod_input(t,41,-12);assert(!memcmp(&before,t,sizeof before));
    ctlmod_input(t,80,0);assert(t->p[P_DIST]==80&&ctlmod_state[0].key==60);
    lfo_restore();ctlmod_end();assert(!t->p[P_DIST]);
}
static void test_learned_cc_base(void)
{
    for(unsigned with_lfo=0;with_lfo<2;with_lfo++){
        fresh(DM_MMSAW);track_t *t=&trk[0];t->p[P_DIST]=10;
        t->perf.dest[0]=P_DIST+1;t->perf.amount[0]=10;
        if(with_lfo){
            unsigned found=0;
            for(unsigned page=0;page<LP_PTCH;page++)for(unsigned dest=0;dest<8;dest++)
                if(LFO_PAGE_ID[page][dest]==P_DIST){t->p[P_LFO1+LF_PAGE]=page;t->p[P_LFO1+LF_DEST]=dest;found=1;}
            assert(found);t->p[P_LFO1+LF_WAVE]=LW_SQR;t->p[P_LFO1+LF_DPTH]=10;
        }
        memset(&mo,0,sizeof mo);mo.s.on=1;mo.s.count=1;
        mo.s.ev[0]=(motion_event_t){0,0,P_DIST,30};
        ctlmod_begin();lfo_apply(CTL);motion_step(t,0);
        assert(mo.base[0][P_DIST]==10&&*motion_slot(t,P_DIST)==30);
        ml_set(t,P_DIST,80);
        assert(*motion_slot(t,P_DIST)==80&&mo.base[0][P_DIST]==80);
        if(with_lfo){
            unsigned found=0;for(unsigned j=0;j<t->lnum;j++)if(t->lpid[j]==P_DIST){assert(t->lsave[j]==100);found=1;}
            assert(found);
        }else assert(t->p[P_DIST]==100);
        lfo_restore();ctlmod_end();assert(t->p[P_DIST]==80);
        motion_restore(0);assert(t->p[P_DIST]==80);
        /* UI runs between audio blocks: audition snapshots the edited base,
         * including when motion owns a separate persistent base. */
        song.sel=0;preset_list_open();pl_move(ui.pr.cur==0?1:0);
        assert(ui.pr.pv&&ui.pr.sv_k[P_DIST]==80);
        assert(ui.pr.sv_perf.dest[0]==P_DIST+1&&ui.pr.sv_perf.amount[0]==10);
        pl_close();assert(trk[0].p[P_DIST]==80&&trk[0].perf.dest[0]==P_DIST+1);
    }
}
static void test_calibrated(void)
{
    fresh(DM_M2_GSIN);track_t *t=&trk[0];t->p[P_MVOL]=20;
    t->perf.dest[0]=P_MVOL+1;t->perf.amount[0]=10;
    int32_t delta[71]={0};delta[MMA_VOL]=3*256+128;
    ctlmod_begin();int base=t->p[P_MVOL];assert(base==40);
    lfo_apply(CTL);assert(t->p[P_MVOL]==base);
    mma_track_t *s=mmen_prepare(t);mma_modulation(s,delta);
    uint8_t raw[71];mm_controls_get(t,raw);mma_parameters(s,raw);
    assert(raw[MMA_VOL]==base&&s->eff[MMA_VOL]==base&&mma_p8(s,MMA_VOL)==base*256+3*256+128);
    ctlmod_refresh(t);assert(t->p[P_MVOL]==base);
    lfo_restore();ctlmod_end();assert(t->p[P_MVOL]==20);
}
int main(void)
{
    test_sources();test_lock_lfo();test_disabled_and_motion();test_pressed_input();test_learned_cc_base();test_calibrated();
    printf("controller_mod_test: passed, state %zu bytes\n",sizeof ctlmod_state+sizeof ctlmod_active);return 0;
}
