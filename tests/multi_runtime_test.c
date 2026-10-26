/* SPDX-License-Identifier: GPL-3.0-only */
#include "drum_host.h"
#include "../firmware/src/multi_runtime.h"
#include <assert.h>
static void holds(void)
{
    multi_runtime_t s={0};
    for(unsigned i=0;i<128;i++)multi_hold(&s,i,i,1,0);
    assert(s.count==128&&multi_last(&s,0,127)==127&&multi_last(&s,0,60)==60);
    for(unsigned i=0;i<128;i++)multi_hold(&s,i,127,1,1);
    assert(s.count==128);
    multi_hold(&s,7,90,1,0);assert(multi_last(&s,0,127)==7&&s.count==128);
    for(unsigned i=0;i<128;i++)multi_hold(&s,i,0,0,0);
    assert(s.count==128&&s.held[7]==2);
    for(unsigned i=0;i<128;i++)multi_hold(&s,i,0,0,1);
    assert(!s.count&&multi_last(&s,0,127)==-1);
    multi_hold(&s,128,100,1,0);multi_hold(&s,60,100,1,2);assert(!s.count);
    multi_options_t c={0};
    for(unsigned split=0;split<128;split++)for(unsigned first=0;first<6;first++)for(unsigned note=0;note<128;note++){
        c.mode=MULTI_SPLIT;c.split_key=split;c.first_upper=first;
        unsigned lower=(1u<<first)-1;
        assert(multi_tracks(&c,note)==(note<split?lower:63u^lower));
    }
    c.mode=MULTI_ALL;assert(multi_tracks(&c,0)==63&&multi_tracks(&c,127)==63);
    c.mode=MULTI_OFF;assert(!multi_tracks(&c,60));
}
static void envelope(void)
{
    multi_runtime_t s={0};multi_options_t c={0};c.sustain=64;
    assert(multi_env_sample(&s,&c)==32767);multi_env_on(&s);
    unsigned last=0;while(s.envstage==MULTI_ENV_ATTACK){multi_env_sample(&s,&c);assert(s.env>=last);last=s.env;}
    assert(s.env==MULTI_ENV_ONE);
    while(s.envstage==MULTI_ENV_DECAY){multi_env_sample(&s,&c);assert(s.env<=last);last=s.env;}
    assert(s.env==64u*MULTI_ENV_ONE/127u&&s.gate);
    multi_env_off(&s);last=s.env;
    for(unsigned i=0;i<44100;i++){multi_env_sample(&s,&c);assert(s.env<=last);last=s.env;}
    assert(!s.env&&s.envactive&&!s.gate&&multi_env_sample(&s,&c)==0);
    multi_env_on(&s);c.sustain=127;
    for(unsigned i=0;i<1000;i++)multi_env_sample(&s,&c);
    assert(s.env==MULTI_ENV_ONE&&multi_env_sample(&s,&c)==32767);
    c.release=127;multi_env_off(&s);last=s.env;
    for(unsigned i=0;i<44100;i++)multi_env_sample(&s,&c);
    assert(s.env==last);
    c.release=0;multi_env_on(&s);multi_env_sample(&s,&c);multi_env_off(&s);
    for(unsigned i=0;i<10000;i++)multi_env_sample(&s,&c);
    assert(!s.env);
}
static void pitch_clock(void)
{
    multi_options_t c={0};multi_runtime_t s={0};s.target16=72*16;
    multi_pitch_tick(&s,&c,0);assert(!s.pitch16);multi_pitch_tick(&s,&c,32);assert(s.pitch16==s.target16);
    c.portamento=127;s.pitch16=60*16;s.target16=s.pitch16+1;
    multi_pitch_tick(&s,&c,32);assert(s.pitch16<s.target16&&s.pitch_fraction>0);
    for(unsigned i=0;i<100000;i++)multi_pitch_tick(&s,&c,32);
    assert(s.pitch16==s.target16&&!s.pitch_fraction);
    s.target16=48*16;int old=s.pitch16;
    for(unsigned i=0;i<100000;i++){multi_pitch_tick(&s,&c,32);assert(s.pitch16<=old&&s.pitch16>=s.target16);old=s.pitch16;}
    assert(s.pitch16==s.target16);
    multi_runtime_t a={0},b={0};a.pitch16=b.pitch16=60*16;a.target16=b.target16=72*16;c.portamento=64;
    for(unsigned i=0;i<100;i++){multi_pitch_tick(&a,&c,32);multi_pitch_tick(&b,&c,16);multi_pitch_tick(&b,&c,16);assert(abs(a.pitch16-b.pitch16)<=3);}
    for(unsigned timing=0;timing<8;timing++){c.timing=timing;assert(multi_quantum(&c,22050)==(timing?22050u*(1u<<(timing-1))/4u:0));}
    const uint32_t qs[]={1,3,5512,352800,0xFFFFFFFEu};
    for(unsigned q=0;q<5;q++)for(unsigned i=0;i<100;i++){
        uint32_t quantum=qs[q],n=0xFFFFFFFFu-i;s.phase=i*913u;
        uint64_t expected=(uint64_t)(s.phase%quantum)+n;
        assert(multi_clock_tick(&s,n,quantum)==(expected>=quantum));assert(s.phase==expected%quantum);
    }
    assert(!multi_clock_tick(&s,32,0)&&!s.phase);
}
int main(void){holds();envelope();pitch_clock();puts("multi_runtime_test: all passed");return 0;}
