/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include <assert.h>
static track_t *setup(unsigned length,unsigned division,unsigned amount,unsigned mask)
{
    host_init_live();transport_req=0;
    track_t *t=&trk[0];t->p[P_SLEN]=length;t->p[P_SDIV]=division;t->p[P_SRC]=0;
    t->p[P_ARPM]=0;memset(t->step,0,sizeof t->step);
    memset(t->seq.swing,mask,sizeof t->seq.swing);song.seq.swing=amount;
    seq_start();return t;
}
static void duration_cycles(void)
{
    for(unsigned len=1;len<=64;len+=len<5?1:19)
    for(unsigned div=0;div<10;div++)
    for(unsigned mask=0;mask<4;mask++) {
        track_t *t=setup(len,div,80,mask==0?0:mask==1?255:mask==2?170:73);
        song.g[G_BPM]=137;uint32_t straight=0,swung=0,rem=0;
        uint32_t first=step_delay(t,div_period(div,0),0);
        for(unsigned i=0;i<len*8;i++) {
            t->seq_rem=rem;uint32_t p=div_period(div,rem);
            uint32_t d=step_samples(t,p,i);assert(d>0);
            straight+=p;swung+=d;rem=div_rem_next(div,rem);
            assert(swung==straight+step_delay(t,div_period(div,rem),i+1)-first);
        }
    }
}
static void onset_times(void)
{
    for(unsigned mask=0;mask<4;mask++) {
        track_t *t=setup(3,2,75,mask==0?0:mask==1?255:mask==2?170:73);
        song.g[G_BPM]=137;unsigned elapsed=0,rem=0,straight=0;
        for(unsigned i=0;i<18;i++) {
            unsigned p=div_period(2,rem),due=straight+step_delay(t,p,i);
            if(!due)due=1;
            while(elapsed<due){seq_tick(t,1);elapsed++;if(elapsed<due)assert(t->seq_cnt==(i?i-1:0xFFFFFFFFu));}
            assert(t->seq_cnt==i&&t->seq_idx==i%3);
            straight+=p;rem=div_rem_next(2,rem);
        }
    }
}
static void legacy_and_record(void)
{
    track_t *t=setup(3,2,0,255);song.g[G_SWING]=40;t->p[P_SSWING]=20;
    for(unsigned c=0;c<20;c++){
        uint32_t p=div_period(2,t->seq_rem);
        int32_t sw=track_swing(t)*(int32_t)div_samples(2)/250;
        assert(step_samples(t,p,c)==p+(uint32_t)((c&1)?-sw:sw));
        t->seq_rem=div_rem_next(2,t->seq_rem);
    }
    t=setup(3,2,75,255);seq_tick(t,1);
    assert(t->seq_cnt==0xFFFFFFFFu);assert(rec_hit(t,100)==0);
    assert(t->rskip&&t->rskip_idx==0);
    unsigned d=step_delay(t,div_period(2,0),0);
    seq_tick(t,d-1);assert(t->seq_cnt==0&&!t->rskip);
    t->seq_pos=step_samples(t,div_period(2,t->seq_rem),0)/2+1;
    assert(rec_hit(t,100)==1);
}
static void ratchets(void)
{
    track_t *t=setup(3,2,75,170);drum_set_model(t,DM_K909);
    t->step[0].on=1;t->step[0].rat=3;
    seq_tick(t,1);unsigned age=dvage;
    unsigned span=step_samples(t,div_period(2,t->seq_rem),0);
    assert(t->rat_len==span&&t->rat_n==4);
    for(unsigned k=1;k<4;k++) {
        unsigned at=k*span/4;
        while(t->seq_pos<at)seq_tick(t,1);
        assert(dvage==age+k&&t->rat_k==k+1);
    }
}
int main(void)
{
    duration_cycles();onset_times();legacy_and_record();ratchets();
    puts("swing_mask_test: passed");return 0;
}
