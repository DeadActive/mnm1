/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include <assert.h>
static step_t steps[2][NTRK][NSTEP];
static seq_options_t options[2][NTRK];
static pattern_options_t global[2];
static void setup(unsigned div,unsigned firstmask,unsigned secondmask,int loop)
{
    host_init_live();transport_req=0;song.g[G_BPM]=137;
    memset(steps,0,sizeof steps);
    for(unsigned s=0;s<2;s++) {
        global[s]=(pattern_options_t){.swing=75};
        chain.src[s].options=&global[s];chain.src[s].seq=options[s];
        for(unsigned k=0;k<NTRK;k++) {
            seq_options_default(&options[s][k]);memset(options[s][k].swing,0,8);
            options[s][k].swing[0]=s?secondmask:firstmask;
            chain.src[s].step[k]=steps[s][k];chain.src[s].model[k]=trk[k].model;
            chain.src[s].timing[k][0]=3;chain.src[s].timing[k][1]=div;
            chain.src[s].timing[k][2]=chain.src[s].timing[k][3]=0;
            trk[k].p[P_ARPM]=0;
        }
    }
    chain.cfg.count=2;chain.cfg.loop=loop;
    chain.cfg.row[0]=(chain_row_t){0,2};chain.cfg.row[1]=(chain_row_t){1,1};
    chain.armed=1;seq_start();
}
static void tick(void)
{
    chain.cut=0;chain_tick(1);
    for(unsigned k=0;k<NTRK;k++)seq_tick(&trk[k],1);
}
static void test_chain(unsigned div,unsigned firstmask,unsigned secondmask,int loop)
{
    setup(div,firstmask,secondmask,loop);
    uint32_t first=0,second=0,rem=0,cycle=0;
    for(unsigned i=0;i<9;i++) {
        cycle+=div_period(div,rem);rem=div_rem_next(div,rem);
        if(i==2)first=cycle;if(i==5)second=cycle;
    }
    unsigned delay0=step_delay(&trk[0],div_period(div,0),0);
    for(unsigned now=1;now<=cycle;now++) {
        tick();
        if(now<first)assert(chain.row==0&&chain.left==2);
        if(now>=first&&now<second)assert(chain.row==0&&chain.left==1);
        if(now>=second&&now<cycle)assert(chain.row==1&&chain.left==1);
        if(delay0&&now<delay0)assert(trk[0].seq_cnt==0xFFFFFFFFu);
        if(now==first) {
            /* The repeat boundary counts once but does not force the delayed next onset. */
            if(firstmask&1)assert(trk[0].seq_idx==2&&trk[0].seq_cnt==2);
        }
        if(now==second)assert(trk[0].seq_cnt==((secondmask&1)?0xFFFFFFFFu:0u));
    }
    if(loop)assert(chain.running&&chain.row==0&&chain.left==2);
    else assert(!chain.running&&!song.playing);
}
static void block_boundaries(void)
{
    for(unsigned div=10;div<=11;div++) {
        setup(div,5,3,1);unsigned rem=0,ends[6],sum=0;
        for(unsigned i=0;i<18;i++) {
            sum+=div_period(div,rem);rem=div_rem_next(div,rem);
            if(i%3==2)ends[i/3]=sum;
        }
        for(unsigned now=32;now<=ends[5]+32;now+=32) {
            chain.cut=0;chain_tick(32);
            for(unsigned k=0;k<NTRK;k++)seq_tick(&trk[k],32);
            unsigned phase=0;while(phase<6&&now>=ends[phase])phase++;
            assert(chain.running);
            assert(chain.row==(phase%3==2?1:0));
            assert(chain.left==(phase%3==0?2:1));
        }
    }
}
int main(void)
{
    for(unsigned div=2;div<12;div++) {
        if(div>2&&div<10)continue;
        for(unsigned a=0;a<8;a++)for(unsigned b=0;b<8;b++) {
            test_chain(div,a,b,0);test_chain(div,a,b,1);
        }
    }
    block_boundaries();
    puts("song_swing_test: passed");return 0;
}
