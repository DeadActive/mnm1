/* SPDX-License-Identifier: GPL-3.0-only
 * Independent filter keytracking through legacy cutoffs and the live adapter. */
#include "ui_host.h"
#include <assert.h>
static void legacy(void)
{
    host_init_live();track_t *t=&trk[0];drum_set_model(t,DM_MMSAW);
    t->p[P_E0]=0;t->p[P_FCUT]=16;t->p[P_FWDTH]=16;
    t->p[P_FENV]=t->p[P_FWOFS]=0;
    int32_t reference_hp,reference_lp;
    t->mm_p16=60*16;t->perf.route_flags=0;flt_cuts_at(t,0,&reference_hp,&reference_lp);
    for(unsigned flags=0;flags<4;flags++) {
        int32_t h0,l0,h1,l1,h2,l2;
        t->perf.route_flags=(uint8_t)(flags<<3);t->mm_p16=48*16;
        flt_cuts_at(t,0,&h0,&l0);t->mm_p16=72*16;flt_cuts_at(t,0,&h1,&l1);
        assert((flags&1)?h0==h1&&h0==reference_hp:h1-h0==2*FLT_OCT);
        assert((flags&2)?l0==l1&&l0==reference_lp:l1-l0==2*FLT_OCT);
        t->p[P_E0]=12;flt_cuts_at(t,0,&h2,&l2);
        assert((flags&1)?h2==h1:h2-h1==FLT_OCT);
        assert((flags&2)?l2==l1:l2-l1==FLT_OCT);
        t->p[P_E0]=0;
    }
    /* Legacy drum tracking retains C2; OFF deliberately uses the C4 reference. */
    drum_set_model(t,DM_K909);t->p[P_FCUT]=16;t->p[P_FWDTH]=16;
    t->p[P_FENV]=t->p[P_FWOFS]=0;t->perf.route_flags=0;
    int32_t h,l;flt_cuts_at(t,0,&h,&l);
    assert(reference_hp-h==2*FLT_OCT&&reference_lp-l==2*FLT_OCT);
    t->perf.route_flags=PERF_HP_OFF|PERF_LP_OFF;flt_cuts_at(t,0,&h,&l);
    assert(h==reference_hp&&l==reference_lp);
}
typedef struct {uint32_t hp,lp;} coefficients;
static coefficients render(track_t *t,unsigned flags,int note,int tune)
{
    t->p[P_E7]=(int16_t)tune;
    drum_play(t,100,note-60);
    /* Change after triggering: the render's prepare must refresh the flags too. */
    t->perf.route_flags=(uint8_t)(flags<<3);
    int32_t out[CTL]={0};assert(track_render(t,out,CTL));
    mma_track_t *s=&mmen_state[0];assert(s->keytrack_off==flags);
    return (coefficients){s->cached_hp_g,s->cached_lp_g};
}
static void calibrated(unsigned model)
{
    host_init_live();track_t *t=&trk[0];drum_set_model(t,model);mmen_default(t);
    uint8_t raw[71];mm_controls_get(t,raw);
    raw[MMA_BASE]=32;raw[MMA_WDTH]=16;raw[MMA_BOF]=raw[MMA_WOF]=64;
    raw[MMA_HPQ]=raw[MMA_LPQ]=32;raw[MMA_ATK]=0;raw[MMA_HOLD]=raw[MMA_DEC]=127;
    for(unsigned l=0;l<3;l++)raw[MMA_LFO+8*l+7]=64;
    mm_controls_set(t,raw);t->p[P_MUTE]=0;
    coefficients reference=render(t,0,60,64);
    for(unsigned flags=0;flags<4;flags++) {
        coefficients low=render(t,flags,48,64),high=render(t,flags,72,64);
        assert((flags&1)?low.hp==high.hp&&low.hp==reference.hp:high.hp>low.hp);
        assert((flags&2)?low.lp==high.lp&&low.lp==reference.lp:high.lp>low.lp);
        coefficients tuned=render(t,flags,72,96);
        assert((flags&1)?tuned.hp==high.hp:tuned.hp>high.hp);
        assert((flags&2)?tuned.lp==high.lp:tuned.lp>high.lp);
    }
    /* Switching back on invalidates/recomputes the cached stage coefficients. */
    coefficients off=render(t,3,48,64),on=render(t,0,48,64);
    assert(off.hp>on.hp&&off.lp>on.lp);
}
int main(void)
{
    assert(sizeof(mma_track_t)==936);
    legacy();calibrated(DM_M2_GSIN);calibrated(DM_M2_GNOI);
    puts("filter_tracking_test: passed");return 0;
}
