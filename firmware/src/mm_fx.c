/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 (Hortator fork): 2026 */
/* The FX machines (MNM-1): THRU REVERB CHORUS DYNAMIX RINGMOD PHASER
 * FLANGER. An FX machine makes no sound: it processes its input (P_IN: NEIB, the track before, or a bus; fx.c
 * fx_prepare fills fx_in_buf) and its output runs through its own track: FILTER, SRR, DIST, LEVEL, PAN, the sends, the
 * LFOs. GATE (P_FXG) ON: it always passes (a voice of its own, started by track_render); TRIG:
 * its notes open its AMP envelope (ATK HOLD DEC REL) and nothing passes between them. The track's path is mono: no stereo
 * WID parameters. Each FX track keeps its state in fxs[] and a 4096-sample delay
 * memory (fxb[], the input >> 4 as 16 bit), cleared when the machine on the track changes. INP: input gain, 64 = x 1. */
#define FXB_LEN 4096u
#define FXB_SH 4                                         /* the delay memory holds the signal >> 4 */
typedef struct {
    int32_t z[16];                                       /* filter / allpass / comb states */
    uint32_t ph, w;                                      /* the LFO's phase, the write position */
    int32_t env, gain;                                   /* DYNAMIX: the level, the gain */
    uint32_t gate;                                       /* REVERB: samples the gate stays open */
    uint32_t quiet;                                      /* blocks with no input and a quiet output (FX_SLEEP: ends) */
    uint8_t model;                                       /* the machine the state is for (another: cleared) */
} fxst_t;
static fxst_t fxs[NTRK];
static int32_t fx_in_buf[CTL];                           /* (fx.c: the input fx_prepare filled) */
static uint32_t fx_in_on;                                /* (fx.c: .. anything in it) */
static int16_t fxb[NTRK][FXB_LEN] __attribute__((section(".pool")));

static inline uint32_t fx_k(const track_t *t) { return (uint32_t)(t - trk); }   /* NTRK and over: the scratch track */
/* into the delay memory: >> 4 rounded toward 0 (a floor would hold a feedback loop at a constant of up to ~20 LSB for
 * ever: a limit cycle; toward 0 every loop decays to 0) */
static inline int16_t fx_s16(int32_t x) { x /= 1 << FXB_SH; return (int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x); }
static inline int32_t fx_mul(int32_t x, int32_t q15)   /* rounded: a decaying loop reaches 0 (no -1 held for ever) */
{
    return (int32_t)(((int64_t)x * q15 + 16384) >> 15);
}
static inline int32_t fx_mix(int32_t dry, int32_t wet, int32_t mix) { return dry + fx_mul(wet - dry, mix * 258); }
static inline int32_t fx_inp(const int16_t *p, int32_t x) { return fx_mul(x, clamp(p[7], 0, 127) * 512); }   /* INP */

/* a delay read d samples back (Q8, between samples), from the memory of track k at write position w */
static inline int32_t fx_tap(uint32_t k, uint32_t w, int32_t dq8)
{
    uint32_t di = (uint32_t)dq8 >> 8, f = (uint32_t)dq8 & 255u;
    int32_t a = fxb[k][(w - di) & (FXB_LEN - 1u)], b = fxb[k][(w - di - 1u) & (FXB_LEN - 1u)];
    return (a + (((b - a) * (int32_t)f) >> 8)) << FXB_SH;
}

static void fx_trigger(track_t *t, dvoice_t *v)
{
    uint32_t k = fx_k(t);
    mm_start(t, v);
    v->x[0] = (int32_t)(t->p[P_FXG] == 0);              /* started for GATE ON (track_render) */
    if (k < NTRK && fxs[k].model != t->model) {         /* another machine: from silence */
        memset(&fxs[k], 0, sizeof fxs[k]);
        memset(fxb[k], 0, sizeof fxb[k]);
        fxs[k].model = t->model;
        fxs[k].gain = 1 << 16;
    }
}

/* the FX's block y into out: GATE ON as it is, TRIG through the AMP envelope (a voice GATE ON started closes when
 * GATE turns TRIG) */
#define FX_SLEEP 64u                                     /* ~46 ms of no input and an output under -78 dB: the voice
                                                         * ends (track_render starts it again with the input) */
static void fx_out(track_t *t, dvoice_t *v, const int32_t *y, int32_t *out, uint32_t n)
{
    uint32_t i, k = fx_k(t);
    int32_t pk = 0;
    for (i = 0; i < n; i++)
        pk |= y[i] > 64 || y[i] < -64;
    if (k < NTRK) {
        fxs[k].quiet = fx_in_on || pk ? 0u : fxs[k].quiet + 1u;
        if (fxs[k].quiet >= FX_SLEEP) {
            fxs[k].quiet = 0;
            dm_end(v);                                   /* idle: no cost until the input sounds again */
            return;
        }
    }
    if (!t->p[P_FXG]) {
        for (i = 0; i < n; i++)
            out[i] += y[i];
        v->last = n ? y[n - 1u] : 0;
        return;
    }
    if (v->x[0]) {
        v->x[0] = 0;
        mm_gate(t, v, 0);
    }
    {
        int32_t ampv[CTL];
        mm_amp_block(t, v, ampv, n);
        for (i = 0; i < n; i++)
            out[i] += (int32_t)(((int64_t)y[i] * ampv[i]) >> 24);
        v->last = v->active && n ? (int32_t)(((int64_t)y[n - 1u] * ampv[n - 1u]) >> 24) : 0;
    }
}

/* the scratch track (the engine view): no input, nothing to show */
static int fx_scratch(const track_t *t, dvoice_t *v)
{
    if (fx_k(t) < NTRK)
        return 0;
    dm_end(v);
    return 1;
}

/* ---- THRU: the input, its gain. INP */
static void fxthru_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    int32_t y[CTL];
    uint32_t i;
    if (fx_scratch(t, v))
        return;
    for (i = 0; i < n; i++)
        y[i] = fx_mul(fx_in_buf[i], clamp(t->p[P_E0], 0, 127) * 512);
    fx_out(t, v, y, out, n);
}
#define DM_FXTHRU_DEF {"THRU", 1, 0, {PD("INP", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0)}, fx_trigger, fxthru_render, 1, mm_glide, mm_gate, 1, 1}

/* a modulated delay (FLANGER, CHORUS): base b samples, the triangle LFO SPD swinging it by depth (Q15 of b), FB fed
 * back (bipolar), taps 1 or 3 (120 degrees apart), the feedback low-passed at lp (Q15, 32767 open) */
static void fx_moddelay(track_t *t, dvoice_t *v, int32_t *out, uint32_t n, int32_t b, int32_t depth, uint32_t taps,
                        int32_t lp)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t k = fx_k(t), i, j, w;
    fxst_t *s;
    int32_t y[CTL], fb = clamp(p[4], -64, 63) * 510, mix = clamp(p[3], 0, 127), dq[3], d1[3], dd[3];
    if (fx_scratch(t, v))
        return;
    s = &fxs[k];
    s->ph += LFO_INC[clamp(p[2], 0, 127)];
    for (j = 0; j < taps; j++) {                         /* each tap's delay at the block's end, ramped to from now */
        int32_t tri = osc_tri(s->ph + j * 0x55555555u);
        d1[j] = clamp((b << 8) + (int32_t)(((int64_t)tri * depth >> 15) * b >> 7), 512, (int32_t)(FXB_LEN - 4u) << 8);
        dq[j] = s->z[8 + j] ? s->z[8 + j] : d1[j];      /* (0: the first block, no ramp) */
        dd[j] = (d1[j] - dq[j]) / (int32_t)n;
    }
    w = s->w;
    for (i = 0; i < n; i++) {
        int32_t x = fx_inp(p, fx_in_buf[i]), wet = 0;
        for (j = 0; j < taps; j++) {
            dq[j] += dd[j];
            wet += fx_tap(k, w, dq[j]);
        }
        wet = taps == 3u ? wet / 3 : wet;
        s->z[0] += fx_mul(wet - s->z[0], lp);            /* the feedback's low-pass */
        fxb[k][w & (FXB_LEN - 1u)] = fx_s16(x + fx_mul(s->z[0], fb));
        w++;
        y[i] = fx_mix(x, wet, mix);
    }
    s->w = w;
    for (j = 0; j < taps; j++)
        s->z[8 + j] = d1[j];
    fx_out(t, v, y, out, n);
}

/* ---- FLANGER: DEL DEP SPD MIX, FB INP. DEL 0.2 .. 9 ms, DEP up to the whole of it; FB bipolar */
static void fxflg_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    fx_moddelay(t, v, out, n, 8 + clamp(p[0], 0, 127) * 3, clamp(p[1], 0, 127) * 232, 1u, 32767);
}
#define DM_FXFLG_DEF {"FLANG", 1, 0, {PD("DEL", F_INT, 0, 127, 20), PD("DEP", F_PCT, 0, 127, 60), \
    PD("SPD", F_LFOHZ, 0, 127, 30), PD("MIX", F_PCT, 0, 127, 64), PD("FB", F_BIPCT, -64, 63, 40), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, fxflg_render, 1, \
    mm_glide, mm_gate, 1, 1}

/* ---- CHORUS: DEL DEP SPD MIX, FB LP INP. 3 taps 120 degrees apart, DEL 5 .. 30 ms, LP on the feedback */
static void fxcho_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    fx_moddelay(t, v, out, n, 220 + clamp(p[0], 0, 127) * 8, clamp(p[1], 0, 127) * 64, 3u, mm_tone_k(p[5]));
}
#define DM_FXCHO_DEF {"CHRS", 1, 0, {PD("DEL", F_INT, 0, 127, 40), PD("DEP", F_PCT, 0, 127, 50), \
    PD("SPD", F_LFOHZ, 0, 127, 30), PD("MIX", F_PCT, 0, 127, 64), PD("FB", F_BIPCT, -64, 63, 0), \
    PD("LP", F_PCT, 0, 127, 127), PD("-", F_INT, 0, 0, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, fxcho_render, \
    1, mm_glide, mm_gate, 1, 1}

/* ---- PHASER: CNTR DEP SPD MIX, FB INP. Six first-order all-passes swept by a triangle around CNTR (DEP octaves),
 * the chain's output fed back (FB, bipolar): notches where the chain is half a turn off the dry signal (MIX 50 %) */
static void fxphs_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t k = fx_k(t), i, j;
    fxst_t *s;
    int32_t y[CTL], fb = clamp(p[4], -64, 63) * 450, mix = clamp(p[3], 0, 127), c, g, a;
    if (fx_scratch(t, v))
        return;
    s = &fxs[k];
    s->ph += LFO_INC[clamp(p[2], 0, 127)];
    c = clamp((clamp(p[0], 0, 127) << 8) + (int32_t)(((int64_t)osc_tri(s->ph) * clamp(p[1], 0, 127)) >> 9), 0, 120 << 8);
    g = (int32_t)SVF_G24[c >> 8];                        /* tan(pi fc / fs), Q24 */
    a = (int32_t)(((int64_t)(QONE - g) << 15) / (QONE + g));   /* the all-pass coefficient, Q15 */
    for (i = 0; i < n; i++) {
        int32_t x = fx_inp(p, fx_in_buf[i]), u = x + fx_mul(s->z[12], fb);
        for (j = 0; j < 6u; j++) {                       /* y = a (u - y1) + u1 */
            int32_t yy = fx_mul(u - s->z[2 * j + 1], a) + s->z[2 * j];
            s->z[2 * j] = u;
            s->z[2 * j + 1] = yy;
            u = yy;
        }
        s->z[12] = clamp(u, -(1 << 21), 1 << 21);
        y[i] = fx_mix(x, u, mix);
    }
    fx_out(t, v, y, out, n);
}
#define DM_FXPHS_DEF {"PHASR", 1, 0, {PD("CNTR", F_INT, 0, 127, 64), PD("DEP", F_PCT, 0, 127, 60), \
    PD("SPD", F_LFOHZ, 0, 127, 30), PD("MIX", F_PCT, 0, 127, 64), PD("FB", F_BIPCT, -64, 63, 30), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, fxphs_render, 1, \
    mm_glide, mm_gate, 1, 1}

/* ---- RINGMOD: WAVE EXT MIX TUNE, INP. The input times a carrier at the track's note (+ TUNE): a sine (WAVE 0)
 * morphing to a triangle (127); EXT how much of the input is ring-modulated, MIX dry / wet */
static void fxring_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t i, inc;
    int32_t y[CTL], wv = clamp(p[0], 0, 127) * 258, ext = clamp(p[1], 0, 127), mix = clamp(p[2], 0, 127);
    mm_t *m = &v->ms.mm;
    if (fx_scratch(t, v))
        return;
    m->tgt = (60 + t->nt) * 16;                          /* the note (GATE ON: drum_play keeps the voice) */
    inc = mm_inc(mm_pitch(t, v, 0) - t->p[P_E0] * 16 + clamp(p[3], -24, 24) * 16);
    for (i = 0; i < n; i++) {
        int32_t x = fx_inp(p, fx_in_buf[i]), sn = sine_i(m->ph[0]), car = sn + fx_mul(osc_tri(m->ph[0]) - sn, wv);
        int32_t rm = fx_mul(x, car);
        m->ph[0] += inc;
        y[i] = fx_mix(x, fx_mix(x, rm, ext), mix);
    }
    fx_out(t, v, y, out, n);
}
#define DM_FXRING_DEF {"RING", 1, 0, {PD("WAVE", F_INT, 0, 127, 0), PD("EXT", F_PCT, 0, 127, 127), \
    PD("MIX", F_PCT, 0, 127, 127), PD("TUNE", F_SEMI, -24, 24, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, fxring_render, 1, mm_glide, mm_gate, 1, 1}

/* log2 of x > 0 in Q8 (the integer part by the top bit, the fraction linear: within 0.09 of an octave) */
static int32_t fx_log2q8(uint32_t x)
{
    int32_t e = 0;
    if (!x)
        return -(32 << 8);
    while (x >= 2u << 15) {
        x >>= 1;
        e++;
    }
    while (x < 1u << 15) {
        x <<= 1;
        e--;
    }
    return ((e + 15) << 8) + (int32_t)((x - (1u << 15)) >> 7);
}

/* ---- DYNAMIX: ATK REL THRS MIX, RAT GAIN RMS INP. A compressor: the level followed (ATK 0.5 .. 100 ms, REL 50 ms
 * .. 5 s; RMS smooths the detector, 0 = peak), above THRS (-60 .. 0 dB) reduced by RAT (1:1 .. 1:255, 127 a limiter),
 * GAIN makeup up to +24 dB; the gain set per block, ramped */
static void fxdyn_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t k = fx_k(t), i, ka, kr, km;
    fxst_t *s;
    int32_t y[CTL], mix = clamp(p[3], 0, 127), lvl, over, red, g1, dg, r;
    if (fx_scratch(t, v))
        return;
    s = &fxs[k];
    ka = 65536u * 10u / (TIME_MS_X10[clamp(p[0], 0, 127) * 64 / 127] * 441u / 100u + 1u);
    kr = 65536u * 10u / (TIME_MS_X10[54 + clamp(p[1], 0, 127) * 73 / 127] * 441u / 100u + 1u);
    km = 65536u - (uint32_t)clamp(p[6], 0, 127) * 500u;  /* RMS: the detector's own smoothing */
    for (i = 0; i < n; i++) {
        int32_t x = fx_inp(p, fx_in_buf[i]), a = x < 0 ? -x : x;
        s->z[0] += (int32_t)(((int64_t)(a - s->z[0]) * (int64_t)(65536u - km + 1u)) >> 16);   /* RMS-ish */
        a = p[6] ? s->z[0] : a;
        s->env += (int32_t)(((int64_t)(a - s->env) * (a > s->env ? ka : kr)) >> 16);
        y[i] = x;
    }
    /* dB in Q8 over full scale (2^19): 6.02 dB an octave */
    lvl = (fx_log2q8((uint32_t)(s->env > 0 ? s->env : 0)) - (19 << 8)) * 602 / 100;
    over = lvl - (clamp(p[2], 0, 127) * 60 / 127 - 60) * 256;
    r = 1 + clamp(p[4], 0, 127) * clamp(p[4], 0, 127) / 64;
    red = over > 0 ? over - over / r : 0;
    g1 = (int32_t)pow2_q16(clamp((clamp(p[5], 0, 127) * 24 * 256 / 127 - red) * 8164 / 65536, -3000, 1000));
    dg = (g1 - s->gain) / (int32_t)n;
    for (i = 0; i < n; i++) {
        s->gain += dg;
        y[i] = fx_mix(y[i], (int32_t)(((int64_t)y[i] * s->gain) >> 16), mix);
    }
    s->gain = g1;
    fx_out(t, v, y, out, n);
}
#define DM_FXDYN_DEF {"DYNMX", 1, 0, {PD("ATK", F_INT, 0, 127, 20), PD("REL", F_INT, 0, 127, 40), \
    PD("THRS", F_INT, 0, 127, 80), PD("MIX", F_PCT, 0, 127, 127), PD("RAT", F_INT, 0, 127, 40), \
    PD("GAIN", F_INT, 0, 127, 20), PD("RMS", F_INT, 0, 127, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, \
    fxdyn_render, 1, mm_glide, mm_gate, 1, 1}

/* ---- REVERB: DEC DAMP GATE MIX, HP LP INP. A gated plate: four damped combs (887 .. 1109
 * samples) into two all-passes, DEC their feedback, DAMP their damping, HP / LP on the wet signal; GATE (127 off)
 * closes the tail GATE x 10 ms after the input last sounded */
static const uint16_t FXR_LEN[6] = {887, 953, 1031, 1109, 67, 47};
static void fxrev_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t k = fx_k(t), i, j;
    fxst_t *s;
    int32_t y[CTL], mix = clamp(p[3], 0, 127), fb = 22938 + clamp(p[0], 0, 127) * 72, dmp = 32767 - clamp(p[1], 0, 127) * 206;
    int32_t hpk = mm_tone_k(p[4]) >> 3, lpk = mm_tone_k(p[5]);   /* HP 0: off (its low-pass ~DC) */
    uint32_t gt = clamp(p[2], 0, 127) >= 127 ? 0xFFFFFFFFu : (uint32_t)clamp(p[2], 0, 126) * 441u + 441u;
    if (fx_scratch(t, v))
        return;
    s = &fxs[k];
    for (i = 0; i < n; i++) {
        int32_t x = fx_inp(p, fx_in_buf[i]), wet = 0, base = 0, u;
        uint32_t w = s->w + i;
        if (x > 2048 || x < -2048)
            s->gate = gt;                                /* the input sounds: the gate open again */
        for (j = 0; j < 4u; j++) {                       /* the combs: a damped feedback */
            uint32_t pos = (uint32_t)base + w % FXR_LEN[j];
            int32_t c = fxb[k][pos] << FXB_SH;
            s->z[j] += fx_mul(c - s->z[j], dmp);
            fxb[k][pos] = fx_s16(x / 4 + fx_mul(s->z[j], fb));
            wet += c;
            base += FXR_LEN[j];
        }
        u = wet / 2;
        for (j = 4; j < 6u; j++) {                       /* the all-passes, g = 0.5 */
            uint32_t pos = (uint32_t)base + w % FXR_LEN[j];
            int32_t d = fxb[k][pos] << FXB_SH, o = d - (u >> 1);
            fxb[k][pos] = fx_s16(u + (o >> 1));
            u = o;
            base += FXR_LEN[j];
        }
        s->z[4] += fx_mul(u - s->z[4], lpk);              /* LP, then HP (the low-pass's difference from its own LP) */
        s->z[5] += fx_mul(s->z[4] - s->z[5], hpk);
        u = s->z[4] - s->z[5];
        if (s->gate != 0xFFFFFFFFu) {                    /* GATE: the tail cut, a 3 ms fade */
            if (s->gate)
                s->gate--;
            else
                s->z[6] = s->z[6] > 256 ? s->z[6] - 256 : 0;
            if (s->gate)
                s->z[6] = 32767;
            u = fx_mul(u, s->z[6]);
        }
        y[i] = fx_mix(x, u, mix);
    }
    s->w += n;
    fx_out(t, v, y, out, n);
}
#define DM_FXREV_DEF {"REVRB", 1, 0, {PD("DEC", F_INT, 0, 127, 80), PD("DAMP", F_INT, 0, 127, 40), \
    PD("GATE", F_INT, 0, 127, 127), PD("MIX", F_PCT, 0, 127, 64), PD("HP", F_INT, 0, 127, 0), \
    PD("LP", F_PCT, 0, 127, 127), PD("-", F_INT, 0, 0, 0), PD("INP", F_INT, 0, 127, 64)}, fx_trigger, fxrev_render, 1, \
    mm_glide, mm_gate, 1, 1}
