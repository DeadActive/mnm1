/* SPDX-License-Identifier: GPL-3.0-only
 * Real core types and lfo.c, with the unrelated engine/mixer dependencies stubbed. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#include "../firmware/src/mm_controls.h"
#include "../firmware/src/mm_lfo.h"
#undef MML_SIN
#define CTL 32
#define FS 44100
#define MM_MODEL_FIRST 63u
static int32_t mmen_delta[2][NTRK][71];
static unsigned desc_calls;
static uint32_t quarter=22050, beat_calls;
static int32_t clamp(int32_t x,int32_t lo,int32_t hi) {return x<lo?lo:x>hi?hi:x;}
static int mmen_is_model(unsigned m) {return m>=63 && m<82;}
static unsigned mmen_index(const track_t *t) {for(unsigned i=0;i<NTRK;++i)if(t==&trk[i])return i;return NTRK;}
static int32_t mm2_sinp(uint32_t phase) {return mml_sine(phase);}
static int32_t sine_i(uint32_t phase) {return mml_sine(phase);}
static uint32_t beat_samples(void) {++beat_calls;return quarter;}
static const param_desc_t *track_desc(const track_t *t,uint32_t p)
{
    static const param_desc_t generic=PD("TEST",F_INT,0,127,0);
    (void)t;(void)p;++desc_calls;return &generic;
}
#include "../firmware/src/lfo.c"
static void init(void)
{
    memset(trk,0,sizeof(trk));memset(&song,0,sizeof(song));
    song.g[G_BPM]=120;song.sel=0;quarter=22050;
    for(unsigned t=0;t<NTRK;++t) {
        trk[t].model=t==1?0:MM_MODEL_FIRST;
        mm_controls_init(&trk[t],t,0);
    }
    /* Legacy controls use actual legacy enum values, independently of raw source. */
    memset(trk[1].p+P_LFO1,0,LF_N*sizeof(int16_t));
    memset(trk[1].p+P_LFO2,0,LF_N*sizeof(int16_t));
    memset(trk[1].p+P_LFO3,0,LF_N*sizeof(int16_t));
    mmen_lfo_reset();desc_calls=0;
}
static void sequential_and_trigger(void)
{
    init();
    trk[0].p[P_L1TRK]=mm_control_track(2);trk[0].p[P_LFO1+LF_DEST]=1;
    trk[0].p[P_LFO1+LF_SPD]=127;trk[0].p[P_LFO1+LF_MULT]=127;
    trk[0].p[P_LFO1+LF_TRIG]=30;trk[0].p[P_LFO1+LF_DPTH]=127;
    trk[0].p[P_L1PHS]=96;
    int16_t saved[NTRK][P_COUNT];for(unsigned t=0;t<NTRK;++t)memcpy(saved[t],trk[t].p,sizeof saved[t]);
    lfo_hit(&trk[0]);assert(mmen_lfo_state.voice[0][0].phase==(96u<<25));
    mml_state reference=mmen_lfo_state;uint32_t random=mmen_lfo_rng;
    uint8_t raw[6][3][8]={0},sound[71];
    for(unsigned t=0;t<NTRK;++t)if(mmen_is_model(trk[t].model)) {
        mm_controls_get(&trk[t],sound);memcpy(raw[t],sound+42,24);
    }
    quarter=14700; /* External clock 180 BPM overrides configured 120 BPM. */
    mmen_lfo_prepare(32);
    for(unsigned b=0;b<2;++b) {
        mml_block(&reference,raw,1800,&random,0);
        for(unsigned t=0;t<NTRK;++t)for(unsigned p=0;p<71;++p)
            assert(mmen_delta[b][t][p]==mml_delta(&reference,t,p));
    }
    assert(mmen_delta[0][2][0]!=mmen_delta[1][2][0]);
    assert(!memcmp(&reference,&mmen_lfo_state,sizeof reference));
    assert(random==mmen_lfo_rng);
    for(unsigned t=0;t<NTRK;++t)assert(!memcmp(saved[t],trk[t].p,sizeof saved[t]));
    assert(desc_calls==0); /* Calibrated controls never went through legacy enum clamps. */
    lfo_hit(&trk[0]);assert(mmen_lfo_state.voice[0][0].phase==(96u<<25));
    mml_state before=mmen_lfo_state;track_t scratch=trk[0];lfo_hit(&scratch);
    assert(!memcmp(&before,&mmen_lfo_state,sizeof before));assert(lfo_out(&scratch,0)==0);
    int32_t value;assert(lfo_live(&trk[2],P_E0,&value));
    assert(value==clamp(mml_delta(&mmen_lfo_state,2,0),0,127*256)>>8);
    assert(!lfo_live(&scratch,P_E0,&value));
}
static void mixed_legacy(void)
{
    init();
    track_t *t=&trk[1];
    t->p[P_E0]=64;t->p[P_LFO1+LF_PAGE]=LP_SYN;t->p[P_LFO1+LF_DEST]=0;
    t->p[P_LFO1+LF_WAVE]=LW_SQR;t->p[P_LFO1+LF_DPTH]=127;
    t->p[P_LFO1+LF_SPD]=64;t->p[P_LFO1+LF_MULT]=1;
    trk[0].p[P_LFO1+LF_DEST]=71;trk[0].p[P_LFO1+LF_TRIG]=127;
    trk[0].p[P_LFO1+LF_WAVE]=127;trk[0].p[P_LFO1+LF_MULT]=127;
    trk[0].p[P_LFO1+LF_DPTH]=127;
    int16_t saved[NTRK][P_COUNT];for(unsigned i=0;i<NTRK;++i)memcpy(saved[i],trk[i].p,sizeof saved[i]);
    lfo_apply(32);assert(t->p[P_E0]==127);assert(song.lfo_in);
    assert(!memcmp(saved[0],trk[0].p,sizeof saved[0]));
    mmen_lfo_prepare(32);lfo_restore();assert(!song.lfo_in);
    for(unsigned i=0;i<NTRK;++i)assert(!memcmp(saved[i],trk[i].p,sizeof saved[i]));
    for(unsigned b=0;b<2;++b)for(unsigned p=0;p<71;++p)assert(mmen_delta[b][1][p]==0);
    /* A cross-track source targeting a legacy LFO DEST must never wake that
     * track's disabled source evaluator via previous-block modulation. */
    trk[0].p[P_L1TRK]=mm_control_track(1);trk[0].p[P_LFO1+LF_DEST]=44;
    trk[0].p[P_LFO1+LF_WAVE]=40;trk[0].p[P_LFO1+LF_TRIG]=0;
    for(unsigned b=0;b<20;++b)mmen_lfo_prepare(32);
    for(unsigned l=0;l<3;++l)assert(mmen_lfo_state.voice[1][l].phase==0);
    assert(mml_delta(&mmen_lfo_state,1,43)==0);
    /* Reset wipes both slices and all phase; start keeps source FREE running. */
    mmen_lfo_state.voice[0][0].phase=123;lfo_start();assert(mmen_lfo_state.voice[0][0].phase==123);
    mmen_lfo_reset();assert(mmen_lfo_rng==0x2000);
    for(unsigned b=0;b<2;++b)for(unsigned i=0;i<NTRK;++i)for(unsigned p=0;p<71;++p)assert(mmen_delta[b][i][p]==0);
}
static void legacy_fast_path(void)
{
    init();
    for(unsigned t=0;t<NTRK;++t)trk[t].model=0;
    track_t saved[NTRK];memcpy(saved,trk,sizeof saved);
    beat_calls=0;
    for(unsigned b=0;b<100;++b)mmen_lfo_prepare(32);
    assert(!beat_calls && !mmen_lfo_used);
    assert(!memcmp(saved,trk,sizeof saved));
    /* Exercise a live calibrated cross-track modulation, then remove the last
     * calibrated source/destination: phase, sparse output and both slices clear. */
    trk[0].model=MM_MODEL_FIRST;trk[2].model=MM_MODEL_FIRST;
    trk[0].p[P_L1TRK]=mm_control_track(2);trk[0].p[P_LFO1+LF_DEST]=1;
    trk[0].p[P_LFO1+LF_WAVE]=40;trk[0].p[P_LFO1+LF_DPTH]=127;
    mmen_lfo_prepare(32);assert(mmen_delta[1][2][0]!=0 && mmen_lfo_used);
    for(unsigned t=0;t<NTRK;++t)trk[t].model=0;
    memcpy(saved,trk,sizeof saved);beat_calls=0;mmen_lfo_prepare(32);
    mml_state zero={0};assert(!memcmp(&zero,&mmen_lfo_state,sizeof zero));
    assert(!mmen_lfo_used && !beat_calls && mmen_lfo_rng==0x2000);
    for(unsigned b=0;b<2;++b)for(unsigned t=0;t<NTRK;++t)for(unsigned p=0;p<71;++p)
        assert(mmen_delta[b][t][p]==0);
    for(unsigned b=0;b<100;++b)mmen_lfo_prepare(32);
    assert(!beat_calls && !memcmp(saved,trk,sizeof saved));
    /* A trigger followed by a model change before prepare also gets cleared. */
    trk[0].model=MM_MODEL_FIRST;trk[0].p[P_LFO1+LF_TRIG]=30;trk[0].p[P_L1PHS]=33;
    lfo_hit(&trk[0]);assert(mmen_lfo_used && mmen_lfo_state.voice[0][0].phase);
    trk[0].model=0;mmen_lfo_prepare(32);assert(!mmen_lfo_used);
    assert(!memcmp(&zero,&mmen_lfo_state,sizeof zero));
}
int main(void)
{
    (void)fm1_ms;(void)lfo_own;(void)mm2_sinp;
    sequential_and_trigger();mixed_legacy();legacy_fast_path();
    puts("LFO integration: two sequential slices, external tempo, trigger/scratch, cross-track, legacy isolation and restoration passed");
    return 0;
}
