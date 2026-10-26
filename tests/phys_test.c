/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* PHYS: MEMB (the drum model: the head's modes, BEND, POS, retrigger, voices, level) and RESON MODAL. */
#include "drum_host.h"
#include <math.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
static int32_t buf[44100 * 3];

static double mode_hz(const px_mode_t *m)           /* g = tan(pi f) (Q28) -> Hz */
{
    return atan((double)m->g / (1 << 28)) / M_PI * 44100.0;
}
static void memb_on(track_t *t)
{
    old_set_model(t, DM_MEMB);
}
static px_modal_blk_t memb_blk(track_t *t, uint32_t vel)   /* the first block of a hit: its modes */
{
    px_modal_blk_t K;
    drum_hit(t, vel);
    render_track(t, buf, CTL);
    K = memb_body[t - trk].k;
    return K;
}

static void test_memb_modes(void)
{
    px_modal_blk_t K;
    uint32_t i;
    int ok = 1;
    double f0;
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E3] = 0;                              /* HEAD: ideal */
    trk[0].p[P_E5] = 0;                              /* no BEND */
    K = memb_blk(&trk[0], 96);
    f0 = mode_hz(&K.m[0]);
    check("MEMB: TUNE 0 is A2 (110 Hz, within 1 %)", fabs(f0 / 110.0 - 1) < 0.01);
    for (i = 1; i < K.n; i++)
        ok &= fabs(mode_hz(&K.m[i]) / f0 - PX_MEMB_IDEAL[i] / 65536.0) < 0.005 * PX_MEMB_IDEAL[i] / 65536.0 + 0.002;
    check("MEMB HEAD 0: the ideal head's mode ratios", ok && K.n == PX_NMEMB);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E3] = 127;
    trk[0].p[P_E5] = 0;
    K = memb_blk(&trk[0], 96);
    for (i = 1, ok = 1; i < K.n; i++)
        ok &= fabs(mode_hz(&K.m[i]) / mode_hz(&K.m[0]) - PX_MEMB_LOADED[i] / 65536.0) < 0.01 * PX_MEMB_LOADED[i] / 65536.0;
    check("MEMB HEAD 127: the loaded head's (harmonic) ratios", ok);
}

static void test_memb_bend_pos(void)
{
    px_modal_blk_t K;
    uint32_t i;
    int ok = 1;
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E5] = 127;                            /* BEND 12 semitones */
    K = memb_blk(&trk[0], 96);
    check("MEMB BEND: the strike starts an octave up", fabs(mode_hz(&K.m[0]) / 220.0 - 1) < 0.03);
    render_track(&trk[0], buf, 44100 / 5);           /* 200 ms */
    check("MEMB BEND: back on the note within 200 ms (1 %)", fabs(mode_hz(&memb_body[0].k.m[0]) / 110.0 - 1) < 0.01);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E4] = 0;                              /* POS: the centre */
    K = memb_blk(&trk[0], 96);
    for (i = 0; i < K.n; i++)
        ok &= PX_MEMB_M[i] ? K.m[i].a == 0 : K.m[i].a != 0;
    check("MEMB POS centre: only the circular modes (m = 0) sound", ok);
}

static void test_memb_voice(void)
{
    uint32_t i, n = 0;
    int32_t pk_t = 0, pk_m = 0;
    host_init();
    check("MEMB: a heavy voice (weight 2), one body per track", DMODELS[DM_MEMB].weight == 2 && DMODELS[DM_MEMB].voices == 1);
    memb_on(&trk[0]);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 4410);
    drum_hit(&trk[0], 127);                          /* re-struck while ringing */
    check("MEMB retrigger: the head rings on (no declick tail added)", trk[0].dtail == 0 && memb_body[0].m.s[0][0] != 0);
    for (i = 0; i < 5u; i++) {
        memb_on(&trk[i]);
        drum_hit(&trk[i], 127);
    }
    for (i = 0; i < 5u; i++)
        n += trk[i].v[0].active;
    check("MEMB on 5 tracks: at most 4 sound (the cap of 8, weight 2)", n <= 4u);
    host_init();                                     /* level: within -6 .. +3 dB of a default TOM */
    old_set_model(&trk[0], DM_TOM);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 22050);
    for (i = 0; i < 22050u; i++)
        pk_t = buf[i] > pk_t ? buf[i] : -buf[i] > pk_t ? -buf[i] : pk_t;
    host_init();
    memb_on(&trk[0]);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 22050);
    for (i = 0; i < 22050u; i++)
        pk_m = buf[i] > pk_m ? buf[i] : -buf[i] > pk_m ? -buf[i] : pk_m;
    printf("     MEMB peak %d, TOM peak %d\n", pk_m, pk_t);
    check("MEMB level: a default hit within -6 .. +3 dB of a default TOM", pk_m * 2 >= pk_t && pk_m <= pk_t * 141 / 100);
}

static void test_memb_accent(void)
{
    px_modal_blk_t a, b;
    host_init();
    memb_on(&trk[0]);
    a = memb_blk(&trk[0], 96);
    host_init();
    memb_on(&trk[0]);
    b = memb_blk(&trk[0], 127);
    check("MEMB accent: a harder, brighter strike", b.ex.g > a.ex.g);
}

static void modal_kick(track_t *t)                    /* a kick through RESON MODAL at full MIX */
{
    old_set_model(t, DM_K909);
}
static void test_modal_sustained(void)
{
    uint32_t i, k;
    int32_t pk = 0;
    host_init();
    old_set_model(&trk[0], DM_S909);                 /* (broadband: body and noise ring the modes) */
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    trk[0].p[P_SDIV] = 3;                            /* 1/32 */
    song.g[G_BPM] = 240;
    transport_req = 1;
    for (k = 0; k < 44100u * 10u / CTL; k++) {        /* 10 s of hats into a ringing body */
        render_mix(0, 0, CTL);
        for (i = 0; i < CTL; i++)
            pk = mixo[2 * i] > pk ? mixo[2 * i] : -mixo[2 * i] > pk ? -mixo[2 * i] : pk;
    }
    printf("     MODAL sustained by 1/32 snares, DECAY 127: master peak %d\n", pk);
    check("MODAL fed for 10 s at DECAY 127: bounded (the master never clips past full scale)", pk > 1000 && pk <= 32767);
}

static double mode_t60(const px_mode_t *m)           /* the mode's ring time (s): Q = 1 / r, T60 = Q ln 1000 / (pi f) */
{
    double r = (double)((int64_t)m->rpg - m->g) / (1 << 28), f = mode_hz(m);
    return r > 0 && f > 0 ? log(1000.0) / (r * M_PI * f) : 1e9;
}
/* final review: a ringing MEMB re-struck at another velocity: no jump in the ringing body's level */
static void test_memb_retrigger_vel(void)
{
    static const uint8_t V[2][2] = {{127, 96}, {96, 127}};
    uint32_t c, i;
    for (c = 0; c < 2u; c++) {
        int32_t nat = 0, step;
        host_init();
        memb_on(&trk[0]);
        trk[0].p[P_E1] = 100;
        trk[0].p[P_E2] = 0;
        trk[0].p[P_E6] = 0;
        drum_hit(&trk[0], V[c][0]);
        render_track(&trk[0], buf, 13216);           /* 0.3 s (whole blocks) */
        for (i = 13216u - 64u; i < 13216u; i++)
            nat = abs(buf[i] - buf[i - 1]) > nat ? abs(buf[i] - buf[i - 1]) : nat;
        drum_hit(&trk[0], V[c][1]);
        render_track(&trk[0], buf + 13216, CTL);
        step = abs(buf[13216] - buf[13215]);
        printf("     MEMB re-struck %u -> %u: step %d (natural %d)\n", V[c][0], V[c][1], step, nat);
        check(c ? "MEMB re-struck 96 -> 127: no level jump" : "MEMB re-struck 127 -> 96: no level jump", step <= 2 * nat + 64);
    }
}

/* final review: MEMB's modes damped at every knob extreme, and a hit at DECAY 127 ends by the 6 s lifetime */
static void test_memb_stable(void)
{
    static const int16_t LO[7] = {-24, 0, 0, 0, 0, 0, 0}, HI[7] = {24, 127, 127, 127, 127, 127, 127};
    uint32_t c, k, b, bad = 0;
    for (c = 0; c < 128u; c++) {
        host_init();
        memb_on(&trk[0]);
        for (k = 0; k < 7u; k++)
            trk[0].p[P_E0 + k] = ((c >> k) & 1u) ? HI[k] : LO[k];
        drum_hit(&trk[0], 127);
        render_track(&trk[0], buf, CTL);
        for (k = 0; k < memb_body[0].k.n; k++)
            bad += memb_body[0].k.m[k].rpg <= memb_body[0].k.m[k].g;
    }
    check("MEMB: every mode damped at every knob extreme", bad == 0);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    for (b = 0; b < 44100u * 7u / CTL && trk[0].v[0].active; b++)
        render_track(&trk[0], buf, CTL);
    check("MEMB DECAY 127: the voice ends by its 6 s lifetime", !trk[0].v[0].active);
}

/* Pre-scheduling-change PCM fingerprints: MEMB parameter extremes/retriggers. */
static void test_memb_fingerprint(int print_only)
{
    static const uint32_t expected[4]={0xe163772bu,0x40df35c4u,0x70754d7fu,0xf444de93u};
    for(unsigned group=0;group<4;group++) {
        uint32_t hash=2166136261u,rng=0x12478u;int32_t out[CTL];
        for(unsigned config=0;config<16;config++) {
            host_init();memb_on(&trk[0]);
            for(unsigned i=0;i<8;i++) {
                const param_desc_t *d=&DMODELS[DM_MEMB].edit[i];
                rng=rng*1664525u+1013904223u;
                trk[0].p[P_E0+i]=group==0?d->def:group==1?d->min:group==2?d->max:d->min+(int32_t)(rng%(unsigned)(d->max-d->min+1));
            }
            drum_play(&trk[0],96,(int32_t)config-8);
            for(unsigned b=0;b<1024;b++) {
                if(b==128||b==256)drum_play(&trk[0],b==128?1:127,(int32_t)config-8);
                render_track(&trk[0],out,CTL);
                for(unsigned i=0;i<CTL;i++)hash=(hash^(uint32_t)out[i])*16777619u;
            }
        }
        if(print_only)printf("%08x\n",hash);else check("MEMB PCM identical across safe multiply scheduling",hash==expected[group]);
    }
}

int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--fingerprint")){test_memb_fingerprint(1);return 0;}
    test_memb_fingerprint(0);
    {
        uint32_t rng=0x43561u;int ok=1;
        for(unsigned i=0;i<20000;i++) {
            rng=rng*1664525u+1013904223u;int32_t a=(int32_t)rng;
            rng=rng*1664525u+1013904223u;int32_t b=(int32_t)rng;
            unsigned shift=i%64;
            int32_t qla,qlb,bright=(int32_t)((uint32_t)a%65537u),damp=(int32_t)((uint32_t)b%65537u);
            uint64_t qa=px_memb_q(bright,damp,&qla),qb=px_modal_q(0,bright,damp,&qlb);
            ok &= qa==qb&&qla==qlb;
            uint64_t x=((uint64_t)(uint32_t)a<<32)|(uint32_t)b,y=((uint64_t)(uint32_t)b<<32)|(uint32_t)a;
            ok &= px_memb_mul(a,b,shift)==(int32_t)(((int64_t)a*b)>>shift);
            ok &= px_memb_umul(x,y,shift)==((x*y)>>shift);
        }
        static const uint64_t edges[]={0,1,2,3,0x7fffffffu,0x80000000u,0xffffffffu,UINT64_MAX,0x100000001ull};
        for(unsigned a=0;a<sizeof(edges)/sizeof(edges[0]);a++)
            for(unsigned b=0;b<sizeof(edges)/sizeof(edges[0]);b++)
                for(unsigned shift=0;shift<64;shift++)
                    ok &= px_memb_umul(edges[a],edges[b],shift)==((edges[a]*edges[b])>>shift);
        check("MEMB isolated products retain exact signed/unsigned rounding",ok);
    }
    test_memb_modes();
    test_memb_bend_pos();
    test_memb_voice();
    test_memb_accent();
    if(argc!=2||strcmp(argv[1],"--memb-only"))test_modal_sustained();
    test_memb_retrigger_vel();
    test_memb_stable();
    printf(fails ? "phys_test: %d FAILED\n" : "phys_test: all passed\n", fails);
    return fails ? 1 : 0;
}
