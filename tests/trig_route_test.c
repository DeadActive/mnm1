/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include <assert.h>
static void fresh(unsigned model)
{
    ui_host_init();transport_req=0;song.playing=0;song.solo=0;
    memset(route_owner,0,sizeof route_owner);
    for(unsigned k=0;k<NTRK;k++){
        drum_set_model(&trk[k],model);if(mmen_is_model(model))mmen_default(&trk[k]);
        trk[k].perf=(perf_options_t){0};trk[k].p[P_MUTE]=trk[k].p[P_CHOKE]=trk[k].p[P_ARPM]=0;
        trk[k].p[P_NLEN]=64;trk[k].p[P_AHOLD]=127;trk[k].p[P_ADEC]=127;
    }
}
static void chain3(int cycle)
{
    trk[0].perf.route_flags=2;trk[1].perf.route_flags=3;
    trk[2].perf.route_flags=cycle?1:0;
}
static int gated(unsigned k){return dv_gate_open(&trk[k],&trk[k].v[0]);}
static void forwarding(unsigned model)
{
    fresh(model);chain3(1);assert(route_mask(0)==7);
    unsigned age=dvage;drum_play(&trk[0],113,-7);assert(dvage==age+3);
    for(unsigned k=0;k<3;k++){
        assert(trk[k].nt==-7&&trk[k].trig_nt==-7&&trk[k].v[0].vel==113);
        assert(gated(k));assert(route_owner[k]==1);
    }
    assert(!trk[3].v[0].active);
    age=dvage;assert(drum_play_routes((1u<<NTRK)-1,97,9,0)==(1u<<NTRK)-1);
    assert(dvage==age+NTRK);
    for(unsigned k=0;k<NTRK;k++)assert(trk[k].nt==9&&trk[k].v[0].vel==97);
    fresh(model);age=dvage;drum_play(&trk[4],77,3);assert(dvage==age+1);
    for(unsigned k=0;k<NTRK;k++)assert(!!trk[k].v[0].active==(k==4));
}
static void releases(unsigned model)
{
    fresh(model);chain3(0);drum_play(&trk[0],100,0);
    assert(trk[1].gate_n==0&&trk[2].gate_n==0);
    acid_gate_off(&trk[0]);for(unsigned k=0;k<3;k++)assert(!gated(k));
    fresh(model);chain3(0);drum_play(&trk[0],100,0);
    trk[0].perf.route_flags=4;trk[1].perf.route_flags=0;
    drum_play_masked(&trk[0],100,12,ST_NOTEOFF);
    for(unsigned k=0;k<3;k++)assert(!gated(k));
    fresh(model);chain3(0);drum_play(&trk[0],100,0);
    trk[1].perf.route_flags=0;drum_play(&trk[1],111,5);
    assert(route_owner[1]==2);acid_gate_off(&trk[0]);
    assert(!gated(0)&&gated(1)&&!gated(2));assert(trk[1].trig_nt==5);
    fresh(model);drum_set_model(&trk[0],DM_K909);trk[0].perf.route_flags=2;
    drum_play(&trk[0],100,0);assert(gated(1));
    unsigned gate=trk[1].gate_n;assert(gate&&gate!=GATE_HELD);
    seq_tick(&trk[1],gate);assert(!gated(1));
}
static void flags(unsigned model)
{
    fresh(model);chain3(1);
    for(unsigned k=0;k<3;k++)trk[k].trig_nt=(int8_t)(k*3-5);
    drum_play_masked(&trk[0],99,22,ST_NOPITCH);
    for(unsigned k=0;k<3;k++)assert(trk[k].nt==(int)k*3-5&&trk[k].trig_nt==(int)k*3-5);
    unsigned age=dvage;uint32_t times[3],envelopes[3];
    for(unsigned k=0;k<3;k++){
        trk[k].v[0].t=123+k;times[k]=trk[k].v[0].t;
        trk[k].fenv=12345+k;envelopes[k]=trk[k].fenv;
        trk[k].gate_n=789+k;
    }
    drum_play_masked(&trk[0],70,11,ST_NOAMP|ST_NOFLT|ST_NOLFO);
    assert(dvage==age);
    for(unsigned k=0;k<3;k++){
        assert(trk[k].nt==11&&trk[k].v[0].t==times[k]);
        assert(trk[k].fenv==(int32_t)envelopes[k]&&trk[k].gate_n==789+k);
        assert(gated(k));
    }
    drum_play_masked(&trk[0],70,55,ST_NOTEOFF);
    assert(dvage==age);
    for(unsigned k=0;k<3;k++)assert(!gated(k)&&trk[k].trig_nt==11&&!trk[k].gate_n);
}
int main(void)
{
    unsigned models[]={DM_MMSAW,DM_M2_GSIN};
    for(unsigned i=0;i<2;i++){forwarding(models[i]);releases(models[i]);flags(models[i]);}
    puts("trig_route_test: passed");return 0;
}
