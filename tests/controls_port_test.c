/* SPDX-License-Identifier: GPL-3.0-only
 * Full raw sound mapping checks using production core types and exported sounds. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Embedded section placement has no meaning in this standalone host test. */
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#include "../firmware/src/mm_controls.h"
#include "../firmware/src/mm_sound_bank.h"
static void roundtrip(void)
{
    track_t t;
    uint8_t raw[71],back[71],used[P_COUNT]={0};
    for(unsigned i=0;i<71;++i) {
        assert(MM_CONTROL_ID[i]<P_COUNT);assert(!used[MM_CONTROL_ID[i]]);
        used[MM_CONTROL_ID[i]]=1;
    }
    for(int v=0;v<128;++v) {
        memset(&t,0,sizeof(t));
        for(unsigned p=0;p<P_COUNT;++p)t.p[p]=1234;
        memset(raw,v,sizeof(raw));mm_controls_set(&t,raw);mm_controls_get(&t,back);
        assert(!memcmp(raw,back,71));
        assert(t.p[P_PAN]==v-64 && t.p[P_DLY]==v-64);
        assert(t.p[P_FENV]==v-64 && t.p[P_FWOFS]==v-64);
        for(unsigned p=0;p<P_COUNT;++p)if(!used[p])assert(t.p[p]==1234);
        for(unsigned i=8;i<71;++i) {
            const param_desc_t *d=mm_control_desc(MM_CONTROL_ID[i]);
            assert(d && d->label && d->min<=t.p[MM_CONTROL_ID[i]] && d->max>=t.p[MM_CONTROL_ID[i]]);
            assert(d->max-d->min==127);
        }
    }
    /* Independent source-index checks catch reversible but miswired maps. */
    for(unsigned i=0;i<71;++i)raw[i]=(uint8_t)i;
    mm_controls_set(&t,raw);
    for(unsigned i=0;i<8;++i)assert(t.p[P_E0+i]==(int)i);
    assert(t.p[P_AATK]==8 && t.p[P_MVOL]==13 && t.p[P_LEVEL]==37);
    assert(t.p[P_FRESO]==18 && t.p[P_FLPQ]==19 && t.p[P_FATK]==20);
    assert(t.p[P_EQF]==24 && t.p[P_DTIM]==27 && t.p[P_DWID]==31);
    assert(t.p[P_BITS]==32 && t.p[P_SPU]==35 && t.p[P_PTCH]==36);
    assert(t.p[P_ARPM]==38 && t.p[P_ARPS]==39 && t.p[P_ARPR]==40 && t.p[P_NLEN]==41);
    for(unsigned l=0;l<3;++l) {
        unsigned base=l==0?P_LFO1:l==1?P_LFO2:P_LFO3, source=42+l*8;
        assert(t.p[P_L1TRK+l]==(int)source);
        assert(t.p[base+LF_DEST]==(int)source+1);
        assert(t.p[base+LF_WAVE]==(int)source+2);
        assert(t.p[base+LF_TRIG]==(int)source+3);
        assert(t.p[base+LF_MULT]==(int)source+4);
        assert(t.p[base+LF_SPD]==(int)source+5);
        assert(t.p[P_L1PHS+l]==(int)source+6);
        assert(t.p[base+LF_DPTH]==(int)source+7);
    }
    assert(t.p[P_MODT]==66 && t.p[P_MODR]==67 && t.p[P_MODD]==68 && t.p[P_MODM]==69 && t.p[P_WBNK]==70);
    assert(!mm_control_desc(P_MODEL) && !mm_control_desc(P_E0));
    assert(!mm_control_desc(P_LFO1+LF_PAGE) && !mm_control_desc(P_LFO1+LF_INTL));
    assert(!mm_control_desc(P_COUNT) && !mm_control_desc(999));
    /* Damaged persistent values clamp without signed wraparound. */
    t.p[P_PAN]=-32768;t.p[P_AREL]=32767;mm_controls_get(&t,back);
    assert(back[14]==0 && back[11]==127);
    memset(raw,255,71);mm_controls_set(&t,raw);mm_controls_get(&t,back);
    for(unsigned i=0;i<71;++i)assert(back[i]==127);
}
static void presets(void)
{
    track_t t={0};uint8_t back[71];
    for(unsigned i=0;i<MM_SOUND_COUNT;++i) {
        mm_controls_set(&t,MM_SOUNDS[i].raw);mm_controls_get(&t,back);
        assert(!memcmp(back,MM_SOUNDS[i].raw,71));
        for(unsigned p=8;p<71;++p) {
            const param_desc_t *d=mm_control_desc(MM_CONTROL_ID[p]);
            assert(t.p[MM_CONTROL_ID[p]]>=d->min && t.p[MM_CONTROL_ID[p]]<=d->max);
        }
    }
}
static void defaults_and_remap(void)
{
    /* Donor kit_default, common block through gate, before per-track LFO setup. */
    static const uint8_t expected[34]={0,0,64,64,64,64,64,0,
        0,127,0,0,0,32,64,64,64,64,0,64,64,32,0,127,
        0,0,0,0,64,100,0,64,0,14};
    track_t t={0};uint8_t raw[71];
    for(unsigned track=0;track<6;++track)for(unsigned machine=0;machine<19;++machine) {
        for(unsigned i=0;i<8;++i)t.p[P_E0+i]=100+i;
        t.p[P_MUTE]=1;t.p[P_MODEL]=55;t.p[P_NOTE]=60;t.p[P_LFO1+LF_PAGE]=7;
        mm_controls_init(&t,track,machine);mm_controls_get(&t,raw);
        for(unsigned i=0;i<8;++i)assert(raw[i]==100+i);
        for(unsigned i=0;i<34;++i)
            assert(raw[i+8]==(i==1 && machine>=11 && machine<=13 ? 127 : expected[i]));
        for(unsigned l=0;l<3;++l) {
            unsigned b=42+8*l;
            assert(((unsigned)raw[b]*6>>7)==track);
            assert(raw[b+1]==0 && raw[b+2]==0 && raw[b+3]==0 && raw[b+4]==0);
            assert(raw[b+5]==32 && raw[b+6]==0 && raw[b+7]==64);
        }
        assert(raw[66]==0 && raw[67]==40 && raw[68]==64 && raw[69]==0 && raw[70]==0);
        assert(t.p[P_MUTE]==1 && t.p[P_MODEL]==55 && t.p[P_NOTE]==60 && t.p[P_LFO1+LF_PAGE]==7);
    }
    for(unsigned from=0;from<6;++from)for(unsigned to=0;to<6;++to)for(unsigned v=0;v<128;++v) {
        memset(raw,v,71);mm_controls_remap(raw,from,to);
        for(unsigned p=0;p<71;++p) {
            int self=(p==42 || p==50 || p==58) && ((v*6)>>7)==from && from!=to;
            assert(raw[p]==(self ? mm_control_track(to) : v));
        }
    }
}
int main(void)
{
    (void)trk;(void)song;(void)fm1_ms;
    roundtrip();presets();defaults_and_remap();
    puts("controls: all 128 values, 120 full sounds, descriptors, defaults and self-target remapping passed");
    return 0;
}
