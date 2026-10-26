/* SPDX-License-Identifier: GPL-3.0-only
 * Differential oracle copied from MONO2K by Inigo Hernandez,
 * revision 78d7cdfd676dd8a88a06e6b794fbcfb5fa2b1537, LFO section.
 * Reference code intentionally retains source names and control flow. */
#include <assert.h>
#include <stdio.h>
#include "../firmware/src/mm_lfo.h"
#define M2K_TRACKS 6
#define P_COUNT 71
#define P_LFO 42
enum { L_TRCK,L_DEST,L_WAVE,L_TRIG,L_MULT,L_SPD,L_PHAS,L_DPTH };
typedef struct {
    uint32_t lph[3]; int32_t lval[3],lhold[3],lrnd[3]; uint8_t ldone[3];
    uint8_t eff[71]; int32_t mod[71];
} trk_t;
static trk_t trk[6];
static struct { int lfo_view; int32_t lfo[6][3]; } mon;
static uint32_t rng=0x2000;
static uint32_t rnd32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int32_t sinp(uint32_t p) {return mml_sine(p);}
static int32_t P8(const trk_t *t,int p) {
    int32_t v=t->eff[p]*256+t->mod[p];return v<0?0:v>32512?32512:v;
}
static int32_t P7(const trk_t *t,int p) {return P8(t,p)>>8;}
static int32_t PB8(const trk_t *t,int p) {return P8(t,p)-16384;}
static int32_t PN(const trk_t *t,int p,int n) {return (P7(t,p)*n)>>7;}
static int32_t lfo_shape(int w, uint32_t ph, int32_t *rnd)
{
    int32_t x = (int32_t)(ph >> 16);
    switch (w) {
    case 0: return x < 32768 ? x * 2 - 32768 : (65535 - x) * 2 - 32768;
    case 1: return sinp(ph);
    case 2: return ph < 0x80000000u ? 32767 : -32767;
    case 3: return 32767 - x;                                   /* saw, falling */
    case 4: {                                                   /* exp, unipolar (y * y to 2^32: unsigned) */
        uint32_t y = 65535u - (uint32_t)x;
        y = (y * y) >> 16;
        y = (y * y) >> 16;
        return (int32_t)(y >> 1);
    }
    case 5: return x >> 1;                                      /* ramp, unipolar */
    default: return *rnd;
    }
}

static uint32_t bpm10;                 /* tempo in 1/10 bpm, set by the sequencer */

/* the LFOs' modulation: at most three a track, so only what they touch is set, and cleared the block after */
typedef struct { uint8_t t, p; int32_t v; } modq_t;
static modq_t modq[M2K_TRACKS * 3], modp[M2K_TRACKS * 3];
static int modn, modpn;

static void mod_add(int t, int p, int32_t v)
{
    for (int i = 0; i < modn; i++) if (modq[i].t == t && modq[i].p == p) { modq[i].v += v; return; }
    modq[modn++] = (modq_t){(uint8_t)t, (uint8_t)p, v};
}

static void mod_apply(void)
{
    for (int i = 0; i < modpn; i++) trk[modp[i].t].mod[modp[i].p] = 0;
    for (int i = 0; i < modn; i++) { trk[modq[i].t].mod[modq[i].p] = modq[i].v; modp[i] = modq[i]; }
    modpn = modn;
    modn = 0;
}

static void lfo_block(int ti)
{
    trk_t *t = &trk[ti];
    for (int l = 0; l < 3; l++) {
        const int b = P_LFO + 8 * l;
        int dest = P7(t, b + L_DEST);
        if (dest == 0 || dest > P_COUNT) { t->lval[l] = 0; mon.lfo[ti][l] = 0; continue; }
        int wave = PN(t, b + L_WAVE, 7), trig = PN(t, b + L_TRIG, 5);
        uint32_t mult = 1u << PN(t, b + L_MULT, 8);
        uint32_t inc = (uint32_t)P7(t, b + L_SPD) * mult * bpm10 * 10u;    /* about SPD * MULT * bpm / 15360 Hz */
        uint32_t p = t->lph[l];
        if (!t->ldone[l]) {
            t->lph[l] += inc;
            if (t->lph[l] < p) t->lrnd[l] = (int32_t)(rnd32() >> 16) - 32768;
            if (trig >= 3) {               /* ONE, HALF: a cycle or half of one from the trig's phase (PHAS), then held */
                uint32_t s = (uint32_t)t->eff[b + L_PHAS] << 25, a = p - s, z = t->lph[l] - s;
                if (trig == 3 && z < a) { t->ldone[l] = 1; t->lph[l] = s - 1u; }
                if (trig == 4 && z >= 0x80000000u && a < 0x80000000u) { t->ldone[l] = 1; t->lph[l] = s + 0x80000000u; }
            }
        }
        int32_t depth = PB8(t, b + L_DPTH) >> 8;    /* -64..63 */
        if (!depth && ti != mon.lfo_view) { t->lval[l] = 0; mon.lfo[ti][l] = 0; continue; }   /* its phase runs on; no
                                                                                             * shape for nothing */
        int32_t v = lfo_shape(wave, t->lph[l], &t->lrnd[l]);
        if (trig == 2) v = t->lhold[l];
        t->lval[l] = v;
        mon.lfo[ti][l] = v;
        if (depth) mod_add(PN(t, b + L_TRCK, M2K_TRACKS), dest - 1, (v * depth) >> 6);
    }
}

static void lfo_trig(trk_t *t)
{
    for (int l = 0; l < 3; l++) {
        const int b = P_LFO + 8 * l;
        int trig = (t->eff[b + L_TRIG] * 5) >> 7;
        if (trig == 0) continue;
        if (trig == 2) {
            t->lhold[l] = lfo_shape((t->eff[b + L_WAVE] * 7) >> 7, t->lph[l], &t->lrnd[l]);
            continue;
        }
        t->lph[l] = (uint32_t)t->eff[b + L_PHAS] << 25;
        t->ldone[l] = 0;
    }
}


static uint32_t test_rng=123;
static uint32_t next_random(void) {test_rng=test_rng*1664525u+1013904223u;return test_rng;}
static void compare(const mml_state *s,uint32_t seed) {
    assert(seed==rng);
    for(int t=0;t<6;++t) {
        for(int l=0;l<3;++l) {
            const mml_voice *v=&s->voice[t][l];
            assert(v->phase==trk[t].lph[l]); assert(v->value==trk[t].lval[l]);
            assert(v->hold==trk[t].lhold[l]); assert(v->random==trk[t].lrnd[l]);
            assert(v->done==trk[t].ldone[l]);
        }
        for(int p=0;p<71;++p) assert(mml_delta(s,t,p)==trk[t].mod[p]);
    }
}
static void differential(void) {
    mml_state s; uint8_t raw[6][3][8]; uint32_t seed;
    for(int scenario=0;scenario<5;++scenario) {
        mml_init(&s); memset(trk,0,sizeof(trk)); memset(raw,0,sizeof(raw));
        seed=rng=0x2000; modn=modpn=0;
        for(int b=0;b<4000;++b) {
            for(int t=0;t<6;++t) for(int l=0;l<3;++l) {
                for(int p=0;p<8;++p) {
                    unsigned v=next_random()>>25;
                    if(scenario==0) v=p==L_DPTH?64:p==L_SPD?32:0;
                    if(scenario==1) v=127;
                    if(scenario==2) v=p==L_DPTH?0:127;
                    if(scenario==3 && p==L_DEST) v=43+((t+l+b/100)%24);
                    raw[t][l][p]=(uint8_t)v;
                }
                if(scenario==1 || scenario==2) raw[t][l][L_DEST]=71;
                memcpy(trk[t].eff+42+l*8,raw[t][l],8);
            }
            bpm10=(b%4==0)?600:(b%4==1)?1200:(b%4==2)?2400:3000;
            mon.lfo_view=(b%8)-1;
            for(int t=0;t<6;++t) if(b%13==t) {lfo_trig(&trk[t]);mml_trigger(&s,(unsigned)t,raw[t]);}
            for(int t=0;t<6;++t) lfo_block(t);
            mod_apply();
            mml_block(&s,raw,bpm10,&seed,mon.lfo_view);
            compare(&s,seed);
        }
    }
}
static void semantics(void) {
    mml_state s;uint8_t raw[6][3][8]={0};uint32_t seed=0x2000;
    mml_init(&s);
    for(int t=0;t<6;++t)for(int l=0;l<3;++l)raw[t][l][L_DPTH]=64;
    raw[0][0][L_DEST]=71;raw[0][0][L_TRCK]=127;raw[0][0][L_WAVE]=40;
    raw[0][0][L_DPTH]=0; /* negative maximum square depth */
    mml_block(&s,raw,1200,&seed,-1);
    assert(mml_delta(&s,5,70)==-32767);assert(mml_delta(&s,0,70)==0);
    raw[0][0][L_DPTH]=127;mml_block(&s,raw,1200,&seed,-1);
    assert(mml_delta(&s,5,70)==(32767*63>>6));
    raw[0][0][L_DEST]=0;mml_block(&s,raw,1200,&seed,-1);assert(s.count==0);
    /* Zero depth still advances phase, disabled destination freezes it. */
    raw[0][0][L_SPD]=127;raw[0][0][L_MULT]=127;raw[0][0][L_DPTH]=64;
    raw[0][0][L_DEST]=1;mml_block(&s,raw,1200,&seed,-1);assert(s.voice[0][0].phase!=0);
    uint32_t phase=s.voice[0][0].phase;raw[0][0][L_DEST]=127;
    mml_block(&s,raw,1200,&seed,-1);assert(s.voice[0][0].phase==phase);
    /* ONE and HALF stop at the source's exact phase endpoints. */
    raw[0][0][L_DEST]=1;raw[0][0][L_PHAS]=97;raw[0][0][L_TRIG]=80;
    mml_trigger(&s,0,raw[0]);
    for(int b=0;b<100;++b)mml_block(&s,raw,1200,&seed,-1);
    assert(s.voice[0][0].done);assert(s.voice[0][0].phase==((97u<<25)-1u));
    raw[0][0][L_TRIG]=127;mml_trigger(&s,0,raw[0]);
    for(int b=0;b<100;++b)mml_block(&s,raw,1200,&seed,-1);
    assert(s.voice[0][0].done);assert(s.voice[0][0].phase==((97u<<25)+0x80000000u));
    raw[0][0][L_TRIG]=64;raw[0][0][L_DPTH]=127;
    mml_trigger(&s,0,raw[0]);int32_t held=s.voice[0][0].hold;
    mml_block(&s,raw,1200,&seed,-1);assert(s.voice[0][0].value==held);
    for(unsigned d=1;d<=71;++d)assert(mml_destination[d]==d-1);
}
int main(void) {
    differential();semantics();
    printf("LFO: 20000 donor differential blocks and trigger/routing/depth semantics passed; state=%zu bytes\n",sizeof(mml_state));
    return 0;
}
