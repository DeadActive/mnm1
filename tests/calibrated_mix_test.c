/* SPDX-License-Identifier: GPL-3.0-only
 * Calibrated track adapter through the actual routing/mixer/control-block path. */
#include "drum_host.h"
#include <assert.h>
static int32_t left[4096], right[4096];
static void fresh(void)
{
    unsigned k;
    host_init_live();
    for (k=0;k<NTRK;k++) {
        drum_set_model(&trk[k],DM_MMGND);
        memset(trk[k].step,0,sizeof trk[k].step);
        trk[k].p[P_CHOR]=trk[k].p[P_REV]=trk[k].p[P_DLY]=0;
        trk[k].p[P_SLCR]=0; trk[k].p[P_OUT]=BUS_AB;
    }
    song.g[G_CSRC]=0;
    song.g[G_TAPE]=song.g[G_ADPC]=song.g[G_HISS]=song.g[G_DRIVE]=0;
}
static track_t *voice(unsigned k,unsigned model)
{
    track_t *t=&trk[k];
    drum_set_model(t,model);
    if(mmen_is_model(model))mmen_default(t);
    t->p[P_AHOLD]=127; t->p[P_AREL]=0;
    t->p[P_CHOR]=t->p[P_REV]=t->p[P_DLY]=0;
    t->p[P_SPU]=0; t->p[P_MUTE]=0;
    t->p[P_IN]=IN_NEIB;
    drum_play(t,100,0); t->gate_n=GATE_HELD;
    return t;
}
static uint64_t energy(const int32_t *a,unsigned n)
{
    uint64_t e=0;
    for(unsigned i=0;i<n;i++)e+=(int64_t)a[i]*a[i];
    return e;
}
/* Compare with a cloned adapter, not the mixer's rewritten output arrays. */
static void test_single_chain(void)
{
    fresh(); track_t *t=voice(0,DM_M2_GSIN);
    t->p[P_AATK]=17; t->p[P_DIST]=83; t->p[P_FCUT]=22;
    t->p[P_FWDTH]=90; t->p[P_FLPQ]=27; t->p[P_LEVEL]=81;
    t->p[P_MVOL]=93; t->p[P_PAN]=-23;
    for(unsigned b=0;b<80;b++) {
        mma_track_t reference=mmen_state[0];
        int16_t storage[2048],zero[16]={0}; uint8_t raw[71];
        int32_t l[CTL],r[CTL],ds[16],rs[16];
        memcpy(storage,fxb[0],sizeof storage);
        reference.machine.delay=storage; reference.mod_buffer=storage+1024;
        reference.mod_q8=0;
        mm_controls_get(t,raw);
        for(unsigned off=0;off<CTL;off+=16)
            assert(mma_render(&reference,raw,song.g[G_BPM],0,0,zero,zero,l+off,r+off,ds,rs));
        render_mix(0,0,CTL);
        for(unsigned i=0;i<CTL;i++) {
            /* Mid/side reconstruction may round by one PCM unit. */
            assert(abs(bus_l[BUS_AB][i]-l[i])<=1);
            assert(abs(bus_r[BUS_AB][i]-r[i])<=1);
            assert(bus_l[BUS_AB][i]==mmen_left[0][i]);
            assert(bus_r[BUS_AB][i]==mmen_right[0][i]);
        }
    }
    puts("calibrated chain: one AMP/filter/distortion/VOL/pan/LEVEL pass");
}
static void test_stereo_and_gain(void)
{
    fresh(); track_t *t=voice(0,DM_M2_ENS);
    t->p[P_E5]=110; t->p[P_E6]=127;
    render_mix(left,right,4096);
    assert(energy(left,4096)>100000 && memcmp(left,right,sizeof left));
    fresh(); t=voice(0,DM_M2_GSIN);t->p[P_PAN]=-64;
    render_mix(0,0,4096);
    assert(energy(bus_l[0],CTL)>10000 && energy(bus_r[0],CTL)<100);
    /* VOL clips before LEVEL: swapping their raw values is not interchangeable. */
    fresh();t=voice(0,DM_M2_GSIN);t->p[P_MVOL]=127;t->p[P_LEVEL]=50;
    render_mix(left,0,4096);
    fresh();t=voice(0,DM_M2_GSIN);t->p[P_MVOL]=50;t->p[P_LEVEL]=127;
    render_mix(right,0,4096);
    assert(energy(left,4096)>1000 && energy(right,4096)>1000 && memcmp(left,right,sizeof left));
}
static void test_fx_taps(void)
{
    const unsigned models[]={DM_M2_FXC,DM_M2_FXD,DM_M2_FXR};
    for(unsigned m=0;m<3;m++) {
        fresh(); voice(0,DM_M2_GSIN);voice(1,DM_M2_SAW);voice(2,models[m]);
        trk[0].p[P_OUT]=trk[1].p[P_OUT]=BUS_OFF;
        trk[1].p[P_MVOL]=23;
        if(models[m]==DM_M2_FXR)trk[2].p[P_E1]=127;
        render_mix(left,right,4096);
        assert(energy(left,4096)>1000);
        for(unsigned i=0;i<CTL;i++) {
            assert(fx_in_buf[i]==(models[m]==DM_M2_FXR?mmen_post[1][i]:mmen_pre[1][i]));
            assert(mmen_carrier[i]==mmen_pre[0][i]);
        }
    }
}
static void test_presets_and_release(void)
{
    for(unsigned id=0;id<24;id++) {
        fresh(); assert(!preset_load(&trk[0],PRE_FULL_FIRST+id));
        if(DMODELS[trk[0].model].fx)continue;
        trk[0].p[P_DLY]=trk[0].p[P_SPU]=0;
        drum_play(&trk[0],100,0);
        render_mix(left,right,4096);
        assert(energy(left,4096)+energy(right,4096)>1000);
    }
    fresh();track_t *t=voice(0,DM_M2_GSIN);
    render_mix(left,0,4096);assert(energy(left,4096)>1000);
    DMODELS[t->model].gate(t,&t->v[0],0);
    render_mix(0,0,44100);
    assert(!t->v[0].active && !mmen_state[0].sounding);
    fresh();t=voice(0,DM_M2_GSIN);
    t->p[P_AHOLD]=0;t->p[P_ADEC]=1;
    drum_play(t,100,0);t->gate_n=GATE_HELD;
    render_mix(0,0,44100);
    assert(!t->v[0].active && !mmen_state[0].sounding);
}
static void test_delay_profiles(void)
{
    fresh();voice(0,DM_M2_GSIN);voice(1,DM_M2_GSIN);
    trk[0].p[P_DLY]=-63;trk[0].p[P_DTIM]=1;
    render_mix(0,0,4096);
    assert(mmtd_profile[0]==2 && mmtd_profile[1]==2);
    uint8_t other[MMD_BYTES];memcpy(other,td_buf[1],sizeof other);
    drum_set_model(&trk[0],DM_MMSAW);
    trk[0].p[P_DLY]=0;
    render_mix(0,0,CTL);
    assert(mmtd_profile[0]==1 && !memcmp(other,td_buf[1],sizeof other));
    for(unsigned i=0;i<sizeof td_buf[0]/sizeof(int16_t);i++)assert(((int16_t*)td_buf[0])[i]==0);
    memset(td_buf[0],0x55,sizeof td_buf[0]);
    drum_set_model(&trk[0],DM_M2_GSIN);
    render_mix(0,0,CTL);
    assert(mmtd_profile[0]==2);
    for(unsigned i=0;i<sizeof td_buf[0];i++)assert(((uint8_t*)td_buf[0])[i]==127);
    fresh();voice(0,DM_MMSAW);render_mix(left,0,4096);assert(energy(left,4096)>1000);
}
static void cost(void)
{
    const unsigned models[]={DM_M2_GSIN,DM_M2_SAW,DM_M2_ENS,DM_M2_FMD,DM_M2_PUL,DM_M2_WAV};
    fresh();for(unsigned k=0;k<NTRK;k++)voice(k,models[k]);
    render_mix(0,0,44100u/CTL*CTL);
    uint64_t start=instr_now();
    render_mix(0,0,44100u/CTL*CTL);
    double instructions=(double)(instr_now()-start)/(44100u/CTL*CTL);
    for(unsigned k=0;k<NTRK;k++)assert(trk[k].v[0].active);
    printf("six calibrated sources: %.1f instructions/sample (compile -O2 without sanitizers)\n",instructions);
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--cost")){cost();return 0;}
    test_single_chain();test_stereo_and_gain();test_fx_taps();test_presets_and_release();test_delay_profiles();
    puts("calibrated_mix_test: all passed");return 0;
}
