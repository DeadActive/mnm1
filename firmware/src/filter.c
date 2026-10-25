/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE; MNM-1: 2026 */
/* FILTER (MNM-1): a resonant high-pass at BASE into a resonant low-pass at BASE + WDTH, each its own
 * Q (HPQ, LPQ), on each track's sound before DIST (fx.c mix_part). Both follow the note: BASE 0 puts the high-pass two
 * octaves under it, BASE and WDTH an octave every 8 (a synth machine's pitch; any other machine C2's). An AD envelope
 * (ATK, DEC) from each note adds BOFS to BASE and WOFS to WDTH (64 at the top: 8 octaves). A stage off its range (the
 * high-pass under 30 Hz, the low-pass over 16 kHz) is left out (no cost, its state cleared). The stages are Simper's
 * trapezoidal SVF (2-pole, 12 dB / oct), 64-bit products: a track's block reaches +-2^19
 * and Q 127 rings ~7x. The cutoffs are set once per 32-sample block. After the last voice it rings out, then the track
 * goes idle (fring 0): a block with no input under +-64 (~ -78 dB) is the end. Coefficients Q24 (flt_coef), the input
 * x 8 inside. */
#define FLT_SMAX (1 << 28)                               /* state clamp (64-bit products: no overflow below it) */
#define FLT_OMAX 524287                                  /* the track's range: mix_part's level product fits */
#define FLT_ENV1 (1 << 24)
#define FLT_IN 3                                         /* the input x 8 inside the filter (precision; headroom) */
#define FLT_OCT 3589                                     /* an octave in cutoff units (Q8 of the 30 Hz .. 16 kHz table) */
#define FLT_TOP (127 << 8)
static const dmodel_t *trk_model(const track_t *t);   /* drum_core.c */

typedef struct { int32_t a1, a2, a3, k; } flt_coef_t;   /* Q24, Q24, Q24; k: damping Q12 */

/* the trapezoidal SVF's coefficients (as dsp.c tsvf_coef) at Q24, from SVF_G24 (cut 0..127 << 8, reso 0..127):
 * at Q12 / Q13 the low cutoffs' a3 = g^2 a1 rounds to 0 and the filter holds a constant for ever */
static void flt_coef(flt_coef_t *c, int32_t cut, int32_t reso)
{
    int32_t i;
    int64_t g;
    uint32_t den;
    cut = clamp(cut, 0, FLT_TOP);
    i = cut >> 8;
    g = SVF_G24[i];
    if (i < 127)
        g += (((int64_t)SVF_G24[i + 1] - g) * (cut & 255)) >> 8;
    c->k = 8192 - reso * 7600 / 127;                     /* damping 2.0 .. ~0.15 (Q12), as tsvf_coef */
    den = (uint32_t)((1 << 24) + ((g * (g + ((int64_t)c->k << 12))) >> 24));
    c->a1 = (int32_t)(((uint64_t)1 << 48) / den);
    c->a2 = (int32_t)(((int64_t)c->a1 * g) >> 24);
    c->a3 = (int32_t)(((int64_t)c->a2 * g) >> 24);
}

/* the DECAY knob -> TIME_MS_X10 / ENV_EXP (1 ms .. 10 s over 0..127): 22..105 = 5 ms .. 2 s, ~99 % back to CUT */
static uint32_t flt_dec_idx(int32_t v) { return 22u + (uint32_t)clamp(v, 0, 127) * 83u / 127u; }

static void trk_filter_hit(track_t *t, uint32_t vel)     /* drum_hit: the envelope from 0 at ATTACK (0: the top) */
{
    t->fatk = t->p[P_FATK] > 0;
    t->fenv = t->fatk ? 0 : FLT_ENV1;
    t->fvel = (uint8_t)(vel > 127u ? 127u : vel);
}

/* the note the filter follows, 1/16 semitone (MIDI): a synth machine's pitch (its TUNE too), else (a drum, an FX
 * machine: its first knob is no TUNE) C2 */
static int32_t flt_key16(const track_t *t)
{
    const dmodel_t *m = trk_model(t);
    return m->mm && !m->fx && t->mm_p16 ? t->mm_p16 + t->p[P_E0] * 16 : 36 * 16;
}

/* the cutoffs this block (units of the 30 Hz .. 16 kHz table, Q8; off the table: that stage out): the high-pass two
 * octaves under the note at BASE 0, an octave every 8 of BASE + BOFS x the envelope x the velocity; the low-pass WDTH +
 * WOFS x the same over it */
static void flt_cuts_at(const track_t *t, int32_t ev, int32_t *hc, int32_t *lc)   /* ev: the envelope x velocity, Q15 */
{
    int32_t b = clamp(t->p[P_FCUT], 0, 127) * 256 + ((clamp(t->p[P_FENV], -64, 63) * 256 * ev) >> 15);
    int32_t w = clamp(t->p[P_FWDTH], 0, 127) * 256 + ((clamp(t->p[P_FWOFS], -64, 63) * 256 * ev) >> 15);
    int32_t x = (flt_key16(t) - 1104) * 32 + b * 3;     /* octaves from A4, 6144 an octave */
    x *= FLT_OCT;
    *hc = 6726 + (x >= 0 ? x / 6144 : -((-x + 6143) / 6144));   /* (floored: an octave of note or BASE exactly FLT_OCT) */
    *lc = *hc + (w > 0 ? w : 0) * FLT_OCT / 2048;
}
static void flt_cuts(const track_t *t, int32_t *hc, int32_t *lc)
{
    flt_cuts_at(t, mulq15(t->fenv >> 9, t->fvel * 258), hc, lc);
}

/* one 2-pole SVF stage over b (its state s0 s1), the high-pass (hp) or the low-pass out */
#define FLT_STAGE(C, S0, S1, OUT_HP)                                                                         \
    do {                                                                                                     \
        int32_t v3 = x - S1, v1, v2;                                                                         \
        v1 = (int32_t)(((int64_t)(C).a1 * S0 + (int64_t)(C).a2 * v3 + (1 << 23)) >> 24);                    \
        v2 = S1 + (int32_t)(((int64_t)(C).a2 * S0 + (int64_t)(C).a3 * v3 + (1 << 23)) >> 24);                \
        S0 = clamp(2 * v1 - S0, -FLT_SMAX, FLT_SMAX);                                                        \
        S1 = clamp(2 * v2 - S1, -FLT_SMAX, FLT_SMAX);                                                        \
        x = OUT_HP ? clamp(x - (int32_t)(((int64_t)(C).k * v1) >> 12) - v2, -FLT_SMAX, FLT_SMAX) : v2;       \
    } while (0)

/* the stages in use over b: 1 both, 2 the high-pass only, 3 the low-pass only; returns its output's peak |y| and
 * whether any input came in */
static __attribute__((noinline)) int32_t flt_run(track_t *t, int32_t *b, uint32_t n, uint32_t use, int32_t hc, int32_t lc,
                                                 int32_t *pin)
{
    flt_coef_t h, l;
    uint32_t i;
    int32_t s0 = t->fs[0], s1 = t->fs[1], s2 = t->fs[2], s3 = t->fs[3], pk = 0, in = 0;
    if (use != 3u)
        flt_coef(&h, hc, clamp(t->p[P_FRESO], 0, 127));
    if (use != 2u)
        flt_coef(&l, lc, clamp(t->p[P_FLPQ], 0, 127));
    for (i = 0; i < n; i++) {
        int32_t x = b[i] * (1 << FLT_IN), y;
        if (use != 3u)
            FLT_STAGE(h, s0, s1, 1);
        if (use != 2u)
            FLT_STAGE(l, s2, s3, 0);
        y = clamp((x + (1 << (FLT_IN - 1))) >> FLT_IN, -FLT_OMAX, FLT_OMAX);
        in |= b[i];
        b[i] = y;
        y = y < 0 ? -y : y;
        pk = y > pk ? y : pk;
    }
    t->fs[0] = s0; t->fs[1] = s1; t->fs[2] = s2; t->fs[3] = s3;
    *pin = in;
    return pk;
}
#undef FLT_STAGE

/* the envelope one block on: ATTACK up to the top (linear, 1 ms .. 10 s), then DECAY */
static void flt_env_step(track_t *t)
{
    if (t->fatk) {
        t->fenv += (int32_t)ENV_LIN[clamp(t->p[P_FATK], 0, 127)];
        if (t->fenv >= FLT_ENV1) {
            t->fenv = FLT_ENV1;
            t->fatk = 0;
        }
        return;
    }
    t->fenv -= (int32_t)(((int64_t)t->fenv * ENV_EXP[flt_dec_idx(t->p[P_FDEC])]) >> 16);
}

/* track t's block b through its filter; 1 while it sounds or rings. Both stages out: 0, the state cleared */
static __attribute__((noinline)) uint32_t trk_filter(track_t *t, int32_t *b, uint32_t n)
{
    int32_t hc, lc, in = 0, pk;
    uint32_t use, ring;
    flt_cuts(t, &hc, &lc);
    use = hc > 0 ? (lc < FLT_TOP ? 1u : 2u) : (lc < FLT_TOP ? 3u : 0u);
    if (use != t->fmode) {                               /* stages in or out: those out from silence */
        if (use == 0u || use == 3u)
            t->fs[0] = t->fs[1] = 0;
        if (use == 0u || use == 2u)
            t->fs[2] = t->fs[3] = 0;
        t->fmode = (uint8_t)use;
    }
    flt_env_step(t);
    if (!use) {
        t->fring = 0;
        return 0;
    }
    pk = flt_run(t, b, n, use, hc, lc, &in);
    ring = in || pk > 64;
    if (!ring)
        t->fs[0] = t->fs[1] = t->fs[2] = t->fs[3] = 0;   /* (a rounding dead band can hold a small offset for ever) */
    t->fring = (uint8_t)ring;
    return ring;
}

/* EQ (EFFECTS' EQF, EQG): a peak at EQF, +-36 dB (EQG 64: 0, out), Q ~0.7: x + (G - 1) x the band-pass */
static const int32_t EQ_G12[73] = {
    -4031, -4023, -4014, -4004, -3993, -3981, -3966, -3951, -3933, -3913, -3891, -3866, -3838, -3806, -3771, -3731,
    -3686, -3636, -3580, -3517, -3447, -3368, -3279, -3179, -3067, -2942, -2801, -2643, -2465, -2266, -2043, -1793,
    -1512, -1196, -842, -445, 0, 500, 1061, 1690, 2396, 3188, 4077, 5074, 6193, 7448, 8857, 10437, 12210, 14200, 16433,
    18938, 21748, 24901, 28440, 32410, 36864, 41862, 47470, 53762, 60821, 68742, 77630, 87602, 98791, 111345, 125431,
    141236, 158969, 178866, 201190, 226239, 254344,
};
static __attribute__((noinline)) void track_eq(track_t *t, int32_t *b, uint32_t n)
{
    flt_coef_t c;
    uint32_t i;
    int32_t s0 = t->eq_s[0], s1 = t->eq_s[1], g = EQ_G12[(clamp(t->p[P_EQG], 0, 127) - 64) * 36 / 64 + 36];
    flt_coef(&c, clamp(t->p[P_EQF], 0, 127) << 8, 40);
    for (i = 0; i < n; i++) {
        int32_t x = b[i] * (1 << FLT_IN), v3 = x - s1, v1, v2;
        v1 = (int32_t)(((int64_t)c.a1 * s0 + (int64_t)c.a2 * v3 + (1 << 23)) >> 24);
        v2 = s1 + (int32_t)(((int64_t)c.a2 * s0 + (int64_t)c.a3 * v3 + (1 << 23)) >> 24);
        s0 = clamp(2 * v1 - s0, -FLT_SMAX, FLT_SMAX);
        s1 = clamp(2 * v2 - s1, -FLT_SMAX, FLT_SMAX);
        {
            int64_t e = x + (((((int64_t)g * c.k) >> 12) * v1) >> 12);   /* k v1: the band-pass at unity */
            x = e > FLT_SMAX ? FLT_SMAX : e < -FLT_SMAX ? -FLT_SMAX : (int32_t)e;
        }
        b[i] = clamp((x + (1 << (FLT_IN - 1))) >> FLT_IN, -FLT_OMAX, FLT_OMAX);
    }
    t->eq_s[0] = s0;
    t->eq_s[1] = s1;
}

/* SRR (the EFFECTS page): the track's sample rate reduced, a sample held for 1 + SRR x 0.25 samples
 * (127: ~1.4 kHz), the fraction carried (no jitter of its own) */
static void track_srr(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t i, step = 65536u * 4u / (4u + (uint32_t)clamp(t->p[P_SRR], 0, 127)), ph = t->srr_ph;
    int32_t x = t->srr_x;
    for (i = 0; i < n; i++) {
        ph += step;
        if (ph >= 65536u) {
            ph -= 65536u;
            x = b[i];
        }
        b[i] = x;
    }
    t->srr_ph = ph;
    t->srr_x = x;
}

/* the track's inserts before DIST (fx.c mix_part): the FILTER while the track sounds or the filter rings, the EQ
 * (EQG off 64), then SRR; they keep the track on. Out of line, so mix_part stays straight: a condition there was laid
 * out as a cold block jumping back, which tests/target_budget.py reads as a loop around the whole mix */
static __attribute__((noinline)) uint32_t trk_inserts(track_t *t, int32_t *b, uint32_t n, uint32_t snd)
{
    if (snd || t->fring)
        snd |= trk_filter(t, b, n);
    if (snd && t->p[P_EQG] != 64)
        track_eq(t, b, n);
    else if (!snd)
        t->eq_s[0] = t->eq_s[1] = 0;
    if (snd && t->p[P_SRR])
        track_srr(t, b, n);
    return snd;
}
