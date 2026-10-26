/* SPDX-License-Identifier: GPL-3.0-only
 * Regression tests for the MONO2K-derived stereo delay adapter. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../firmware/src/mm_delay.h"
static uint8_t buffer[MMD_BYTES];
static mmd_state state;
static int32_t input_l[16], input_r[16], left[16], right[16];
static void block(int bpm,int time,int send,int feedback)
{
    mmd_process(&state,buffer,input_l,input_r,left,right,bpm,time,send,feedback,0,127);
    for(int i=0;i<16;++i) { assert(abs(left[i])<131072); assert(abs(right[i])<131072); }
}
static void reset(int bpm,int time)
{
    memset(input_l,0,sizeof(input_l)); memset(input_r,0,sizeof(input_r));
    mmd_init(&state,buffer);
    for(int b=0;b<1000;++b) block(bpm,time,0,0);
    assert(state.sleeping); assert(state.time_q8==mmd_target(time,bpm));
}
static void codec_and_laws(void)
{
    /* G.711 endpoints and both signed zero codes, independent expected values. */
    assert(mmd_decode(0)==-32124); assert(mmd_decode(128)==32124);
    assert(mmd_decode(127)==0); assert(mmd_decode(255)==0);
    assert(mmd_decode(254)==8); assert(mmd_decode(126)==-8);
    for(int x=-100000;x<=100000;++x) {
        int y=mmd_decode(mmd_encode(x,0));
        assert(abs(y)<=32124);
        if(abs(x)<1024) assert(abs(mmd_decode(mmd_encode(x,1024)))<=abs(x));
    }
    for(int v=0;v<128;++v) {
        double hp=500*pow(2,(v-64)/11.5), lp=900*pow(2,(v-64)/11.5);
        int h=(int)round(32767*(1-exp(-2*3.141592653589793*fmin(hp,10000)/22050)));
        int l=lp>10000 ? 32767 : (int)round(32767*(1-exp(-2*3.141592653589793*lp/22050)));
        assert(mmd_dly_hp[v]==h); assert(mmd_dly_lp[v]==l);
    }
    assert(mmd_target(0,120)==167*256);
    assert(mmd_target(64,120)==11192*256);
    assert(mmd_target(64,60)==22217*256);
    assert(mmd_target(64,240)==5680*256); /* rounded half-rate sample */
    /* Squared send and unity feedback scaling, including maximum over-unity. */
    assert(mmd_mac15(16000,32*32*8,0)==4000);
    assert(mmd_mac15(16000,64*512,0)==16000);
    assert(mmd_mac15(16000,127*512,0)==31750);
}
static void impulse(int ping)
{
    reset(120,0);
    int first=-1; long energy[2]={0,0};
    input_l[0]=input_l[1]=16000;
    for(int b=0;b<100;++b) {
        block(120,0,ping?127:0,0);
        memset(input_l,0,sizeof(input_l));
        /* Captured from the unmodified donor delay_block at full left pan,
         * unity level, input pair 16000, DTIM/DFB/DBAS=0, DWID=127.
         * This segment begins at sample 336, immediately after echo onset. */
        if (!ping && b==21) {
            static const int32_t donor[16]={7951,-46,-47,-47,-47,-47,-47,-47,
                                           -47,-47,-47,-46,-46,-46,-46,-46};
            for(int i=0;i<16;++i) assert(left[i]==donor[i]);
        }
        for(int i=0;i<16;++i) {
            if(first<0 && (left[i]||right[i])) first=b*16+i;
            energy[0]+=abs(left[i]); energy[1]+=abs(right[i]);
        }
    }
    assert(first==334);
    assert(energy[ping?1:0]>10000); assert(energy[ping?0:1]==0);
    for(int b=0;b<3000 && !state.sleeping;++b) block(120,0,64,0);
    assert(state.sleeping);
    for(int i=0;i<16;++i) assert(left[i]==0 && right[i]==0);
}
static void stereo_and_feedback(void)
{
    reset(120,0); input_l[0]=input_l[1]=12000; input_r[0]=input_r[1]=-6000;
    long l=0,r=0;
    for(int b=0;b<35;++b) {
        block(120,0,0,0); memset(input_l,0,sizeof(input_l)); memset(input_r,0,sizeof(input_r));
        for(int i=0;i<16;++i) { l+=abs(left[i]);r+=abs(right[i]); }
    }
    assert(l>r*18/10 && l<r*22/10);
    reset(120,0); input_l[0]=input_l[1]=16000;
    long first[2]={0},second[2]={0};
    for(int b=0;b<60;++b) {
        block(120,0,127,48); memset(input_l,0,sizeof(input_l));
        for(int i=0;i<16;++i) {
            int n=b*16+i;
            if(n>=334 && n<400) {first[0]+=abs(left[i]);first[1]+=abs(right[i]);}
            if(n>=668 && n<734) {second[0]+=abs(left[i]);second[1]+=abs(right[i]);}
        }
    }
    assert(first[1]>1000 && first[0]==0); assert(second[0]>1000);
    for(int b=0;b<10000 && !state.sleeping;++b) block(120,0,64,48);
    assert(state.sleeping);
}
static void stress(void)
{
    reset(120,127);
    unsigned random=1;
    for(int b=0;b<50000;++b) {
        for(int i=0;i<16;++i) {
            random=random*1664525u+1013904223u;
            input_l[i]=(int32_t)(random>>16)-32768;
            input_r[i]=-input_l[i]-1;
        }
        block(b<20000?1:b<40000?1000:120,b%128,(b/100)%128,127);
        assert(state.phase<65536); assert(state.count<400);
        assert(state.hr[0]<32768 && state.lr[1]<32768);
    }
    /* Time changes converge, and muting send retains current mode and tail. */
    memset(input_l,0,sizeof(input_l)); memset(input_r,0,sizeof(input_r));
    unsigned mode=state.pingpong;
    for(int b=0;b<3000;++b) block(240,64,64,0);
    assert(state.time_q8==mmd_target(64,240)); assert(state.pingpong==mode);
}
int main(void)
{
    codec_and_laws(); impulse(0); impulse(1); stereo_and_feedback(); stress();
    printf("delay port: codec/laws/stereo/timing/feedback/sleep/stress passed; state=%zu, buffer=%u\n",sizeof(state),MMD_BYTES);
    return 0;
}
