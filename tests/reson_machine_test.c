/* SPDX-License-Identifier: GPL-3.0-only
 * Restored Hortator FX resonators: tuning, decay, glide, caps and target-safe arithmetic. */
#include "drum_host.h"
#include "../firmware/src/mm_reson.h"
#include <assert.h>

static const dmodel_t RESON_MODELS[] = {DM_FXSTR_DEF, DM_FXPIPE_DEF, DM_FXCHORD_DEF, DM_FXMODAL_DEF};
static int32_t reson_audio[44100 * 3];
static uint32_t model_id(uint32_t mode)
{
    uint32_t i;
    for (i = 0; i < NMODELS; i++)
        if (DMODELS[i].render == RESON_MODELS[mode].render) return i;
    assert(!"resonator model missing from DMODELS");
    return 0;
}
static track_t *setup(uint32_t mode)
{
    uint32_t i;
    track_t *t;
    host_init();
    t = &trk[1];
    drum_set_model(t, model_id(mode));
    for (i = 0; i < 8u; i++) t->p[P_E0 + i] = RESON_MODELS[mode].edit[i].def;
    t->p[P_FXG] = 0;
    t->p[P_E3] = 0;
    t->p[P_E4] = 0;
    t->p[P_E6] = 127;
    t->nt = 0;
    dv_init(&t->v[0], 127);
    fx_trigger(t, &t->v[0]);
    return t;
}
static void block(track_t *t, int32_t impulse, int32_t *out)
{
    memset(fx_in_buf, 0, sizeof fx_in_buf);
    memset(out, 0, CTL * sizeof *out);
    fx_in_buf[0] = impulse;
    fx_in_on = impulse != 0;
    trk_model(t)->render(t, &t->v[0], out, CTL);
}
static double energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double e = 0;
    while (a < b) { e += (double)x[a] * x[a]; a++; }
    return e;
}
static double pitch(const int32_t *x, uint32_t n, uint32_t expected)
{
    uint32_t lag, best = 0, i;
    double score = -1;
    for (lag = expected * 9 / 10; lag <= expected * 11 / 10; lag++) {
        double xy = 0, xx = 0, yy = 0;
        for (i = 256; i + lag < n; i++) {
            xy += (double)x[i] * x[i + lag];
            xx += (double)x[i] * x[i]; yy += (double)x[i + lag] * x[i + lag];
        }
        double c = xy / sqrt(xx * yy + 1);
        if (c > score) { score = c; best = lag; }
    }
    return 44100.0 / best;
}
static void test_division(void)
{
    uint32_t rng = 1, i;
    for (i = 0; i < 20000; i++) {
        rng = rng * 1664525u + 1013904223u;
        uint32_t d = rng | 1u;
        rng = rng * 1664525u + 1013904223u;
        uint64_t n = ((uint64_t)(rng % d) << 32) | (rng ^ 0x5a987123u);
        assert(fxres_div(n, d) == (uint32_t)(n / d));
    }
    for (i = 24; i <= 96; i += 3) {
        px_svf_t a, b;
        uint32_t q;
        for (q = 256; q <= 1048576; q *= 4) {
            px_svf_coef(&a, qnote_inc((int32_t)i), q);
            fxres_svf_coef(&b, qnote_inc((int32_t)i), q);
            assert(!memcmp(&a, &b, sizeof a));
        }
    }
}
static void test_modal_coefficients(void)
{
    uint32_t i;
    for (i = 0; i < 32u; i++) {
        px_modal_blk_t a = {0}, b = {0};
        px_modal_t ma = {0}, mb = {0};
        uint32_t f = qnote_inc(24 + (int32_t)i * 2);
        int32_t structure = (int32_t)(i * 2113u), tone = (int32_t)((31u - i) * 2113u);
        uint32_t decay = (i * 29u) & 127u;
        px_modal_block_q(&a, &ma, f, structure, tone, (int32_t)decay * 516, RS_T60_MS10[decay], 5000);
        fxres_modal_coeff(&b, &mb, f, structure, tone, (int32_t)decay * 516, RS_T60_MS10[decay], 5000);
        assert(!memcmp(&a, &b, sizeof a) && !memcmp(&ma, &mb, sizeof ma));
    }
}
static void test_extremes(void)
{
    uint32_t m, p, i, rng = 1;
    int32_t y[CTL];
    for (m = 0; m < 4u; m++) {
        track_t *t = setup(m);
        for (p = 0; p < 16u; p++) {
            for (i = 0; i < 8u; i++) {
                const param_desc_t *d = &RESON_MODELS[m].edit[i];
                t->p[P_E0 + i] = ((p >> (i % 4)) & 1u) ? d->max : d->min;
            }
            for (i = 0; i < 256; i++) {
                uint32_t j;
                rng = rng * 1664525u + 1013904223u;
                t->nt = (int8_t)((rng >> 24) % 128u - 60);
                block(t, (int32_t)(rng >> 13) - 262144, y);
                for (j = 0; j < CTL; j++) assert(abs(y[j]) < (1 << 20));
            }
        }
    }
}
static void test_sound(void)
{
    uint32_t m, i;
    for (m = 0; m < 4; m++) {
        track_t *t = setup(m);
        for (i = 0; i + CTL <= 44100 * 3; i += CTL)
            block(t, i ? 0 : 65536, reson_audio + i);
        double e = energy(reson_audio, 0, 8192), tail = energy(reson_audio, 44100 * 2, 44100 * 3 / CTL * CTL);
        printf("%s energy %.0f tail %.0f active %u\n", RESON_MODELS[m].name, e, tail, t->v[0].active);
        assert(e > 10000 && tail < e * 0.02 && !t->v[0].active);
        for (i = 0; i < 44100 * 3 / CTL * CTL; i++) assert(abs(reson_audio[i]) < (1 << 20));
        if (m < 2) {
            double hz = pitch(reson_audio, 8192, 169);
            printf("%s fundamental %.2f Hz\n", RESON_MODELS[m].name, hz);
            assert(fabs(hz / 261.625565 - 1) < 0.02);
        }
    }
}
static void test_glide_and_mix(void)
{
    int32_t y[CTL];
    track_t *t = setup(0);
    fxres_store_t a, b;
    uint32_t i;
    block(t, 65536, y);
    memcpy(&a, &fxb[1][RS_LEN], sizeof a);
    t->nt = 12;
    block(t, 0, y);
    memcpy(&b, &fxb[1][RS_LEN], sizeof b);
    assert(b.r.kq == a.r.kq + RS_KGLIDE && b.r.len[0] < a.r.len[0]);
    t->p[P_E6] = 0;
    block(t, 12345, y);
    for (i = 0; i < CTL; i++) assert(y[i] == (i ? 0 : 12345));
    t->p[P_E7] = 0;
    block(t, 12345, y);
    for (i = 0; i < CTL; i++) assert(y[i] == 0);
}
static void test_caps(void)
{
    uint32_t i;
    int32_t out[CTL];
    setup(0);
    for (i = 0; i < 4; i++) trk[i].p[P_MODEL] = (int16_t)model_id(0);
    assert(fxres_refused(&trk[4], fxstr_render));
    assert(!fxres_refused(&trk[2], fxpipe_render));
    trk[0].p[P_MODEL] = trk[1].p[P_MODEL] = (int16_t)model_id(2);
    assert(fxres_refused(&trk[2], fxchord_render));
    trk[0].p[P_MODEL] = trk[1].p[P_MODEL] = (int16_t)model_id(3);
    assert(fxres_refused(&trk[2], fxmodal_render));
    trk[4].p[P_MODEL] = (int16_t)model_id(0);
    trk[4].v[0].active = 1;
    memset(out, 0, sizeof out);
    fxstr_render(&trk[4], &trk[4].v[0], out, CTL);
    assert(!trk[4].v[0].active);
    for (i = 0; i < CTL; i++) assert(out[i] == 0);
}
/* Diagnostic only: compile without sanitizers at -O2 and run --cost.
 * Full mix includes source voices, filters, routing, sends and sleeping tracks.
 * Hardware overload shedding is intentionally not simulated by render_mix. */
static void cost_configure(const uint32_t *models, uint32_t count, int sends, int serial) {
    uint32_t i,j;
    bench_reset();
    for(i=0;i<NTRK;i++) {
        track_t *t=&trk[i];
        for(j=0;j<P_COUNT;j++) t->p[j]=TP[j].def;
        drum_set_model(t,DM_MMGND);
        memset(t->step,0,sizeof t->step);
        t->p[P_MUTE]=1;
    }
    for(i=0;i<count;i++) {
        track_t *t=&trk[i];
        drum_set_model(t,models[i]);
        assert((uint32_t)t->p[P_MODEL]==models[i]);
        t->p[P_MUTE]=0;
        t->p[P_AHOLD]=t->p[P_ADEC]=127;
        t->p[P_FXG]=0;
        t->p[P_OUT]=BUS_AB;
        t->p[P_IN]=serial?IN_NEIB:IN_AB;
        t->p[P_DIST]=sends?60:0;
        t->p[P_CHOR]=t->p[P_DLY]=t->p[P_REV]=sends?60:0;
        t->p[P_SLCR]=sends?1:0;
        if (DMODELS[models[i]].fx) t->p[P_E1]=127;
        else {
            drum_play(t,127,0);
            t->gate_n=GATE_HELD;
        }
    }
    song.g[G_BPM]=120;
}
static double cost_run(const char *name,const uint32_t *models,uint32_t count,int sends,int serial) {
    cost_configure(models,count,sends,serial);
    render_mix(0,0,44100u/CTL*CTL);
    uint64_t a=instr_now();
    render_mix(0,0,44100u/CTL*CTL);
    double c=(double)(instr_now()-a)/(44100u/CTL*CTL);
    printf("%s: %.1f insn/sample active",name,c);
    uint32_t i;
    for(i=0;i<count;i++) assert(trk[i].v[0].active == (count != 6 || models[0] != DM_FXTHRU));
    for(i=0;i<count;i++)printf(" %s:%u",N_MODEL[models[i]],trk[i].v[0].active);
    puts("");return c;
}
static void test_cost(void) {
    const uint32_t typical[]={DM_MMSAW,DM_FXMODAL};
    const uint32_t two[]={DM_MMSAW,DM_FXMODAL,DM_FXMODAL};
    const uint32_t four[]={DM_MMSAW,DM_MMPLS,DM_FXMODAL,DM_FXCHORD,DM_FXMODAL,DM_FXCHORD};
    const uint32_t light[]={DM_MMSAW,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU};
    const uint32_t idle[]={DM_FXTHRU,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU,DM_FXTHRU};
    int repeat;
    for(repeat=0;repeat<3;repeat++) {
        cost_run("NEIB synth+MODAL",typical,2,0,1);
        cost_run("NEIB synth+2MODAL",two,3,0,1);
        cost_run("AB two synths+2MODAL+2CHORD",four,6,0,0);
        cost_run("AB two synths+2MODAL+2CHORD+sends",four,6,1,0);
        cost_run("NEIB synth+5THRU",light,6,0,1);
        cost_run("6THRU no source",idle,6,0,1);
    }
}

static void test_wide_helpers(void)
{
    uint32_t rng=0x871349u;
    for(unsigned i=0;i<20000;i++) {
        rng=rng*1664525u+1013904223u;int32_t a=(int32_t)rng;
        rng=rng*1664525u+1013904223u;int32_t b=(int32_t)rng;
        unsigned shift=i%64;
        int64_t product=(int64_t)a*b;
        assert(fxres_mul(a,b,shift)==(product>>shift));
        assert(fxres_umul32((uint32_t)a,(uint32_t)b,shift)==(((uint64_t)(uint32_t)a*(uint32_t)b)>>shift));
        uint64_t x=((uint64_t)(uint32_t)a<<32)|(uint32_t)b,y=((uint64_t)(uint32_t)b<<32)|(uint32_t)a;
        assert(fxres_umul(x,y,shift)==((x*y)>>shift));
        assert(fxres_mulz15(a,b)==(int32_t)((product+((product>>63)&32767))>>15));
        assert(fxres_sin((uint32_t)a)==px_sin((uint32_t)a));
        assert(fxres_fasttan((uint32_t)a%2143260078u)==px_fasttan((uint32_t)a%2143260078u));
        assert(fxres_exp2(a>>12)==px_exp2(a>>12));
        assert(fxres_stiff((uint32_t)a%65537)==px_stiff((uint32_t)a%65537));
    }
}

/* Fixed pre-scheduling-change PCM fingerprints, including KEYF glide and cut. */
static void test_fingerprint(int print_only)
{
    static const uint32_t expected[4] = {0xcfc2d3aeu,0xfdcbc3d6u,0xb6a6c63eu,0x38b44c93u};
    unsigned m;
    for(m=0;m<4;m++) {
        uint32_t hash=2166136261u,rng=73;unsigned config,b,i;int32_t y[CTL];
        for(config=0;config<16;config++) {
            track_t *t=setup(m);
            for(i=0;i<8;i++) {
                const param_desc_t *d=&RESON_MODELS[m].edit[i];
                rng=rng*1664525u+1013904223u;
                t->p[P_E0+i]=d->min+(int32_t)(rng%(unsigned)(d->max-d->min+1));
            }
            t->p[P_E5]=127;t->p[P_E6]=127;
            for(b=0;b<512;b++) {
                if(!(b%64)){t->nt=(int8_t)((b/64%3)*12-12);t->lpit=(int16_t)(b%23);}
                if(b==384){fxres_store_t state;memcpy(&state,&fxb[1][RS_LEN],sizeof state);state.r.kill=1;memcpy(&fxb[1][RS_LEN],&state,sizeof state);}
                block(t,b<32?(int32_t)(rng>>17)-16384:0,y);
                rng=rng*1664525u+1013904223u;
                for(i=0;i<CTL;i++)hash=(hash^(uint32_t)y[i])*16777619u;
            }
        }
        if(print_only)printf("%08x\n",hash);else assert(hash==expected[m]);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--fingerprint")) { test_fingerprint(1); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--cost")) { test_cost(); return 0; }
    test_wide_helpers(); test_fingerprint(0);
    test_division(); test_modal_coefficients(); test_extremes(); test_sound(); test_glide_and_mix(); test_caps();
    puts("reson_machine_test: all passed");
    return 0;
}
