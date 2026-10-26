/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Ported from Hortator 0.23.3 by DEADACTIVE, with its tuning and decay laws.
 * FX adapters use the existing per-track fxb buffer; no additional pool memory. */
#ifndef MNM_MM_RESON_H
#define MNM_MM_RESON_H
#include "reson_tables.h"
enum { RS_OFF, RS_STRNG, RS_PIPE, RS_CHORD, RS_MODAL, RS_NMODEL };
typedef struct {                 /* RESON runtime (reson.c): per line (STRNG / PIPE 1, CHORD 4) */
    uint32_t len[4];             /* the line's delay, Q8 samples (the loop filters' delay taken off) */
    int32_t g[4];                /* loop gain, Q15 (PIPE: negative) */
    int32_t lp[4], lr[4], apx[4], apy[4];   /* damping low-pass and its step's remainder; the all-pass' x, y[n-1] */
    int32_t k, a;                /* damping coefficient (Q15), all-pass coefficient (Q15, <= 0) */
    uint16_t tap[4];             /* POS: the second pickup tap (0 = none) */
    uint16_t w, seg, quiet, peak;   /* write position, segment length, quiet blocks, last block's line peak */
    uint8_t ns, model, ring, kill;  /* lines, the model running, ringing, fade out this block */
    uint8_t hold;                /* after a cut: no input until the track's next hit (its declick tail excites nothing) */
    int8_t knt;                  /* KEYF: the last hit's note (offset from C4) */
    int32_t kq;                  /* KEYF: the pitch offset playing, 1/256 semitone (glides to knt x KEYF while it rings) */
} fxres_t;

/* Isolate wide products from callers' memory operations on the target CPU.
 * Consume each product before returning; no 64-bit divide or extra state. */
static __attribute__((noinline)) int64_t fxres_mul(int32_t a,int32_t b,unsigned shift)
{ return ((int64_t)a*b)>>shift; }
static __attribute__((noinline)) uint64_t fxres_umul32(uint32_t a,uint32_t b,unsigned shift)
{ return ((uint64_t)a*b)>>shift; }
static uint64_t fxres_umul(uint64_t a,uint64_t b,unsigned shift)
{
    uint64_t low=fxres_umul32((uint32_t)a,(uint32_t)b,0);
    uint32_t high=(uint32_t)(a>>32)*(uint32_t)b+(uint32_t)a*(uint32_t)(b>>32);
    return (low+((uint64_t)high<<32))>>shift;
}
static __attribute__((noinline)) int32_t fxres_mulz15(int32_t a,int32_t b)
{
    int64_t product=(int64_t)a*b;
    return (int32_t)((product+((product>>63)&32767))>>15);
}
/* Local variants of phys_dsp.c's MIT polynomial and modal helpers. Its full
 * copyright and permission notice is retained in phys_dsp.c. Only multiply
 * placement changes; integer rounding and modal accumulation stay identical. */
static int32_t fxres_sin(uint32_t ph)
{
    uint32_t q = ph >> 30, r = ph & 0x3FFFFFFFu;
    int32_t t, t2, p;
    if (q & 1u)
        r = 0x40000000u - r;
    t = (int32_t)r;
    t2 = fxres_mul(t, t, 30);
    p = 172272;
    p = 5026995 - fxres_mul(t2, p, 30);
    p = 85569306 - fxres_mul(t2, p, 30);
    p = 693598668 - fxres_mul(t2, p, 30);
    p = 1686629713 - fxres_mul(t2, p, 30);
    p = fxres_mul(t, p, 30);
    return q & 2u ? -p : p;
}
static uint32_t fxres_exp2(int32_t x)
{
    int32_t i = x >> 16;
    int64_t f = (int64_t)(x & 0xFFFF) << 14, p = 16377;   /* Q30 */
    if (i < -16)
        return 0;
    if (i > 14)
        return 0xFFFFFFFFu;
    p = 165394 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = 1431680 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = 10327387 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = 59597083 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = 257941248 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = 744261118 + fxres_mul((int32_t)f,(int32_t)p,30);
    p = (1 << 30) + fxres_mul((int32_t)f,(int32_t)p,30);                     /* 2^frac, Q30 */
    return i >= -14 ? (uint32_t)(p >> (14 - i)) : (uint32_t)(p >> 28) >> (-14 - i);
}
static int32_t fxres_fasttan(uint32_t f)
{
    int64_t x = f >> 1, x2 = fxres_mul((int32_t)x,(int32_t)x,31);              /* Q31 */
    int64_t p = 169584874 + fxres_mul(935957068,(int32_t)x2,31);    /* a + b f^2, Q24 */
    p = 52707179 + fxres_mul((int32_t)x2,(int32_t)p,31);                     /* pi + ..., Q24 */
    return (int32_t)fxres_mul((int32_t)x,(int32_t)p,27);
}
static int32_t fxres_stiff(int32_t s)
{
    if (s < 16384)
        return -((16384 - s) >> 2);
    if (s < 19661)
        return 0;
    if (s < 58982)
        return (int32_t)fxres_mul(s-19661,109227,16);   /* / 0.6 */
    s = (s - 58982) * 10;                                /* (s - 0.9) / 0.1 */
    s = fxres_mul(s, s, 16);
    return 98304 - (px_sin(((uint32_t)s << 15) + 0x40000000u) >> 15);   /* 1.5 - cos(pi s) / 2 */
}
static void fxres_modal_run_in(const px_modal_blk_t *B, px_modal_t *M, const int32_t *in, int32_t *out, uint32_t n)
{
    uint32_t i, k, nm = B->n;
    for (i = 0; i < n; i++) {
        int64_t acc = 0;
        const px_mode_t *c = B->m;
        int32_t (*st)[2] = M->s, xi = in[i];
        for (k = 0; k < nm; k++, c++, st++) {
            int32_t s1 = st[0][0], s2 = st[0][1], t, v;
            t = fxres_mul(c->gh, xi - fxres_mul(c->rpg, s1, 28) - s2, 30);
            v = s1 + t;
            st[0][0] = v + t;
            t = fxres_mul(c->g, v, 28);
            st[0][1] = s2 + t + t;
            acc += fxres_mul(c->a,v,0);
        }
        out[i] = (int32_t)(acc >> 30);
    }
}
/* Exact quotient without the target's unavailable 64-bit division runtime. */
static uint32_t fxres_div(uint64_t num, uint32_t den)
{
    uint32_t r = (uint32_t)(num >> 32), q = (uint32_t)num, i;
    if (!den || r >= den)
        return UINT32_MAX;
    for (i = 0; i < 32u; i++) {
        uint32_t carry = r >> 31;
        r = (r << 1) | (q >> 31);
        q <<= 1;
        if (carry || r >= den) { r -= den; q |= 1u; }
    }
    return q;
}
/* Modal coefficient adaptation of phys_dsp.c (MIT; Electrosmith / Emilie Gillet), whose notice remains there. */
static void fxres_svf_coef(px_svf_t *c, uint32_t f, uint32_t q8)
{
    int32_t g = fxres_fasttan(f);
    uint32_t r = fxres_div(((uint64_t)1 << 36) + q8 - 1u, q8);
    uint64_t den = ((uint64_t)1 << 28) + fxres_umul32(r,(uint32_t)g,28) + fxres_umul32((uint32_t)g,(uint32_t)g,28);
    uint32_t d24 = (uint32_t)((den + 15u) >> 4);
    uint32_t h = fxres_div((uint64_t)1 << 54, d24);
    c->g = g;
    c->rpg = (int32_t)(r + (uint32_t)g);
    c->gh = (int32_t)fxres_umul32(h,(uint32_t)g,28);
}
static __attribute__((noinline)) void fxres_modal_coeff(px_modal_blk_t *B, px_modal_t *M, uint32_t f0, int32_t structure,
                                                       int32_t brightness, int32_t damping, uint32_t t60, int32_t pos)
{
    int32_t stiff = fxres_stiff(structure), st2 = stiff, stretch = 65536, ql;
    uint32_t i, phs = 0;
    uint64_t harm, q8 = fxres_umul32(33648942u,t60,16);   /* Q8 per cycle (Q32): pi FS 256 / (ln 1000 10000) . T60 */
    {
        int32_t bb=(int32_t)fxres_mul((int32_t)fxres_mul(brightness,65536-(int32_t)fxres_mul(structure,19661,16),16),
                                     65536-(int32_t)fxres_mul(damping,19661,16),16);
        ql=(int32_t)fxres_mul((int32_t)fxres_mul(bb,131072-bb,16),55706,16)+9830;
    }
    f0 = (uint32_t)fxres_umul32(f0,fxres_div((uint64_t)1 << 32,
                     (uint32_t)(65536 + stiff + fxres_mul(stiff, stiff < 0 ? 60948 : 64225, 16))),16);
    harm = f0;
    for (i = 0; i < PX_NMODE; i++) {
        uint64_t mf = fxres_umul(harm,(uint32_t)stretch,16);
        int32_t att, amp;
        if (mf >= 2143260078u)
            break;
        att = 65536 - (int32_t)(mf >> 15);               /* 1 - 2 f */
        amp = fxres_mul(fxres_sin(phs + 0x40000000u), 16384, 16);   /* cos(2 pi pos i) / 4 */
        {
            px_svf_t c;
            fxres_svf_coef(&c, (uint32_t)mf, (uint32_t)(256u + fxres_umul(mf,q8,32)));   /* (as px_modal_block) */
            B->m[i].g = c.g;
            B->m[i].rpg = c.rpg;
            B->m[i].gh = c.gh;
            B->m[i].a = fxres_mul(amp, att, 18);
        }
        stretch += st2;
        st2 = fxres_mul(st2, st2 < 0 ? 60948 : 64225, 16);
        harm += f0;
        q8 = fxres_umul(q8,(uint32_t)ql,16);
        phs += (uint32_t)pos << 16;
    }
    B->n = i;
    for (; i < PX_NMODE; i++)
        M->s[i][0] = M->s[i][1] = 0;
    for (i = 0; i < B->n; i++) {
        M->s[i][0] = px_clamp(M->s[i][0], -(1 << 28), 1 << 28);
        M->s[i][1] = px_clamp(M->s[i][1], -(1 << 28), 1 << 28);
    }
}

#define RS_LEN 1352u                                  /* STRNG down to C1 (1348.6 samples) */
#define RS_SEG 338u                                   /* CHORD: 4 lines, the lowest string C3 (337.2) */
#define RS_GMAX 32700                                 /* the loop gain's cap (0.998): never self-oscillating */
#define RS_FLOOR 32u                                  /* the line's level (int16, -60 dB of its full scale) under which,
                                                         * for a whole loop, the ring is over: it fades out (a power of 2:
                                                         * the block's OR of |v| is under it exactly when every |v| is) */
#define RS_FINE 6144                                  /* R.TUN at full DEPTH: 2 octaves, in 1/256 semitone */
#define RS_MAXTRK 4u                                  /* RESON on 4 tracks at most (CPU; 2 of them CHORD), by user decision */
#define rs_buf fxb
#define RS_MAXMODAL 2u                                /* MODAL on 2 tracks at most (its 12 modes: CPU) */
#define RS_KGLIDE 256                                 /* KEYF: a retune under a ring moves a semitone a block at most (an
                                                         * octave in 12 blocks, 8.7 ms), the lines read per sample between
                                                         * the blocks' lengths (reson_line_glide): the ring never jumps */
#define RS_MODAL_IN 6                                 /* MODAL: the track's scale -> the modes' Q22 (<< this) */

static const int8_t RS_CHORD_IV[20u][4] = {   /* semitones above TUNE, one note per line (N_RCHORD) */
    {0, 12, 24, 36}, {0, 7, 12, 19}, {0, 5, 12, 17}, {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12},
    {0, 3, 6, 12}, {0, 4, 8, 12}, {0, 4, 7, 9}, {0, 3, 7, 9}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10},
    {0, 3, 6, 10}, {0, 3, 6, 9}, {0, 5, 7, 10}, {0, 4, 7, 14}, {0, 5, 10, 15}, {0, 1, 2, 3},
};

/* p >> sh rounded toward zero: the loop's errors never feed energy one way (a floor's bias, held by a gain just
 * under 1, kept a DC offset or a small cycle alive for ever), so a ring always decays to an all-zero line */
static inline int32_t rs_mz(int32_t p, int32_t sh) { return (p + ((p >> 31) & ((1 << sh) - 1))) >> sh; }

/* KEYF (P_E5): the ring follows the last hit's note (drum_core.c drum_play: rs.knt) from C4, by KEYF (127 = 100 %).
 * The offset this block plays -> r->kq (1/256 semitone): a silent line takes it at once, a ringing one glides to it
 * (RS_KGLIDE); 1 = it moved under a ring of lines (STRNG / PIPE / CHORD, not fading): they glide (reson_glide; MODAL's
 * modes follow block by block). KEYF 0: always 0, nothing else changes */
static __attribute__((noinline)) uint32_t reson_key(track_t *t, fxres_t *r)   /* (out of line: reson_block's loops stay as they were,
                                                                     * tests/target_budget.py) */
{
    int32_t kf = clamp(t->p[P_E5], 0, 127), to = kf ? r->knt * kf * 256 / 127 : 0, d = to - r->kq;
    if (!d)
        return 0;
    if (!r->ring) {
        r->kq = to;
        return 0;
    }
    r->kq += clamp(d, -RS_KGLIDE, RS_KGLIDE);
    return !r->kill && r->ns && r->model != RS_MODAL;
}

static uint32_t rs_chord(const track_t *t) { return (uint32_t)clamp(t->p[P_E3], 0, 127) * 20u / 128u; }

#ifdef DRUM_HOST
static px_modal_blk_t trk_modal_blk;                  /* (host tests: the last MODAL block) */
#endif
/* MODAL: the track's sound b (before DIST) into the 12 modes (their states in the track's rs_buf line); ring in the
 * track's scale; returns the block's peak in the lines' scale (|ring| OR'd, >> 2: the RS_FLOOR test and the meter as
 * STRNG's) */
static __attribute__((noinline)) uint32_t reson_modal(track_t *t, fxres_t *r, px_modal_t *M, const int32_t *b, int32_t *ring, uint32_t n)
{
    px_modal_blk_t K;
    int32_t x[CTL], y[CTL], base = (clamp(t->p[P_E0], 24, 96) << 8) + t->lpit * 16 + r->kq, ink = r->kill || r->hold ? 0 : -1;
    uint32_t i, peak = 0, f0;
    base = clamp(base, 0, 127 * 256);
    f0 = (uint32_t)fxres_umul32(qnote_inc(base >> 8),fxres_exp2(((base & 255) << 16) / (12 * 256)),16);
    fxres_modal_coeff(&K, M, f0, clamp(t->p[P_E3], 0, 127) * 516, clamp(t->p[P_E2], 0, 127) * 516,
                     clamp(t->p[P_E1], 0, 127) * 516, RS_T60_MS10[clamp(t->p[P_E1], 0, 127)],
                     clamp(t->p[P_E4], 0, 127) * 129);   /* DECAY: the ring time as STRNG's */
#ifdef DRUM_HOST
    trk_modal_blk = K;
#endif
    for (i = 0; i < n; i++)
        x[i] = (b[i] & ink) * (1 << RS_MODAL_IN);     /* the track's scale -> Q22 */
    fxres_modal_run_in(&K, M, x, y, n);
    for (i = 0; i < n; i++) {
        int32_t v = clamp(y[i] >> 4, -131071, 131071);   /* Q20 -> the track's scale */
        ring[i] = v;
        peak |= (uint32_t)(v ^ (v >> 31));
    }
    r->ns = 0;
    r->seg = RS_LEN;
    return peak >> 2;
}

static int32_t rs_period(int32_t nq)                  /* note in 1/256 semitone -> the loop period, Q8 samples */
{
    int32_t i, f;
    nq = clamp(nq, 0, 128 * 256 - 1);
    i = nq >> 4;
    f = nq & 15;
    return (int32_t)RS_PER[i] - (int32_t)(((RS_PER[i] - RS_PER[i + 1]) * (uint32_t)f) >> 4);
}

static void reson_clear(track_t *t, fxres_t *r)                   /* silence: the line and the loop filters */
{
    memset(rs_buf[t - trk], 0, sizeof rs_buf[0]);
    memset(r->lp, 0, sizeof r->lp);
    memset(r->lr, 0, sizeof r->lr);
    memset(r->apx, 0, sizeof r->apx);
    memset(r->apy, 0, sizeof r->apy);
    r->quiet = r->peak = r->w = 0;                    /* w: a CHORD line is shorter than STRNG's */
    r->ring = r->kill = 0;
}

/* this block's lines for model m: lengths from TUNE (+ the chord, + the fine offset, + KEYF's), the gains from DECAY, the
 * damping from TONE, the stiffness from STRCT, the pickup tap from POS */
static inline __attribute__((always_inline)) void reson_setup(track_t *t, fxres_t *r, uint32_t m)   /* (in reson_block, as measured) */
{
    uint32_t s, seg = m == RS_CHORD ? RS_SEG : RS_LEN;
    int32_t base = (clamp(t->p[P_E0], 24, 96) << 8) + t->lpit * 16 + r->kq, dk = (int32_t)RS_DK[clamp(t->p[P_E1], 0, 127)];
    int32_t k = RS_TONE_K[clamp(t->p[P_E2], 0, 127)], a, comp;
    if (m == RS_CHORD && base < (48 << 8))
        base = 48 << 8;                               /* CHORD: the lowest string C3 */
    a = m == RS_CHORD ? 0 : -(int32_t)RS_AP_MAX[clamp(base >> 8, 0, 127)] * clamp(t->p[P_E3], 0, 127) / 127;
    comp = ((32768 - k) << 8) / k + ((32768 - a) << 8) / (32768 + a);   /* the loop filters' delay, Q8 */
    r->ns = (uint8_t)(m == RS_CHORD ? 4u : 1u);
    r->seg = (uint16_t)seg;
    r->k = k;
    r->a = a;
    for (s = 0; s < r->ns; s++) {
        int32_t per = rs_period(base + (m == RS_CHORD ? RS_CHORD_IV[rs_chord(t)][s] * 256 : 0));
        int32_t len = clamp((m == RS_PIPE ? per / 2 : per) - comp, 2 << 8, (int32_t)(seg - 2u) << 8);
        int64_t e = fxres_mul(len+comp,dk,16);   /* the loop's decay, log2 units Q16 */
        int32_t g = e >= (15 << 16) ? 0 : (int32_t)(RS_EXP2N[(e >> 8) & 255] >> (e >> 16));
        g = g > RS_GMAX ? RS_GMAX : g;
        r->len[s] = (uint32_t)len;
        r->g[s] = m == RS_PIPE ? -g : g;
        r->tap[s] = (uint16_t)(t->p[P_E4] > 0 ? ((uint32_t)len >> 8) * (uint32_t)clamp(t->p[P_E4], 0, 127) / 256u + 1u : 0u);
    }
}

/* KEYF's glide: line s for this block (as reson_block's loop) with its delay and pickup tap moving per sample from the
 * last block's (l0, t0, Q8 / samples) to this block's: the read slides, never jumps. Returns the line's peak (OR) */
static __attribute__((noinline)) uint32_t reson_line_glide(fxres_t *r, int16_t *ln, uint32_t s, const int32_t *b, int32_t *ring,
                                                           uint32_t n, uint32_t l0, uint32_t t0)
{
    uint32_t i, w = r->w, seg = r->seg, peak = 0;
    int32_t dq = (int32_t)l0, dd = ((int32_t)r->len[s] - dq) / (int32_t)n, tq = (int32_t)t0 << 8;
    int32_t td = (((int32_t)r->tap[s] << 8) - tq) / (int32_t)n;
    int32_t lp = r->lp[s], lr = r->lr[s], apx = r->apx[s], apy = r->apy[s], g = r->g[s], k = r->k, a = r->a, ink = r->kill || r->hold ? 0 : 1;
    for (i = 0; i < n; i++) {
        uint32_t li, rp, tap;
        int32_t x0, d, y, v;
        dq += dd;                                     /* (between the two lengths: >= 2 samples, < seg - 1) */
        tq += td;
        li = (uint32_t)dq >> 8;
        rp = w >= li ? w - li : w + seg - li;
        x0 = ln[rp];
        d = x0 + rs_mz((ln[rp ? rp - 1u : seg - 1u] - x0) * (dq & 255), 8);
        {
            int32_t e = (d - lp) * k + lr, st = e >> 15;
            lr = e - (st * 32768);
            lp += st;
        }
        if (a) {
            y = fxres_mulz15(lp-apy,a)+apx;
            apy = y;
        } else {
            y = apx;
        }
        apx = lp;
        v = clamp(((b[i] >> 2) & -ink) + rs_mz(rs_mz(y, 1) * g, 14), -32767, 32767);
        tap = (uint32_t)tq >> 8;
        ring[i] += tap ? v - ln[w >= tap ? w - tap : w + seg - tap] : v;
        ln[w] = (int16_t)v;
        peak |= (uint32_t)(v ^ (v >> 31));
        if (++w == seg)
            w = 0;
    }
    r->lp[s] = lp;
    r->lr[s] = lr;
    r->apx[s] = apx;
    r->apy[s] = apy;
    return peak;
}

/* KEYF moved the pitch under a ring: this block's lines (reson_setup) read gliding from the last block's lengths and
 * taps; returns their peak (OR) */
static __attribute__((noinline)) uint32_t reson_glide(track_t *t, fxres_t *r, int16_t *buf, const int32_t *b, int32_t *ring, uint32_t n)
{
    uint32_t s, peak = 0, l0[4], t0[4];
    for (s = 0; s < r->ns; s++) {
        l0[s] = r->len[s];
        t0[s] = r->tap[s];
    }
    reson_setup(t, r, r->model);
    for (s = 0; s < r->ns; s++)
        peak |= reson_line_glide(r, buf + s * r->seg, s, b, ring, n, l0[s], t0[s]);
    return peak;
}

/* track t's block b (its rendered sound) through RESON, in place: b = dry + (ring - dry) x MIX. A model change
 * or a cut (kill) fades the old ring out over the block, then clears it. Returns non-zero while it rings. */
static __attribute__((noinline)) uint32_t reson_block(track_t *t, fxres_t *r, px_modal_t *M, uint32_t m, int32_t *b, uint32_t n)   /* (measured on its own:
                                                         * tests/target_budget.py) */
{
    uint32_t i, s, peak = 0, gl;
    int32_t ring[CTL], mix = t->p[P_E6] * 258, fade = 32767, fstep;
    int16_t *buf = rs_buf[t - trk];
    if (m != r->model) {
        if (r->ring && r->model)
            r->kill = 1;                              /* the old ring fades out with its own lines */
        else {
            reson_clear(t, r);
            memset(M, 0, sizeof *M);
            r->model = (uint8_t)m;
        }
    }
    if (r->hold && !r->ring)
        return 0;                                     /* after a cut, until the next hit: nothing to excite it */
    if (!r->model) {                                  /* OFF and quiet */
        r->model = (uint8_t)m;
        return 0;
    }
    fstep = r->kill ? 32767 / (int32_t)n + 1 : 0;
    for (i = 0; i < n; i++)
        ring[i] = 0;
    gl = reson_key(t, r);                                /* KEYF moved the pitch under a ring: the lines glide */
    if (r->model == RS_MODAL)
        peak = reson_modal(t, r, M, b, ring, n);
    else if (gl)
        peak = reson_glide(t, r, buf, b, ring, n);
    else
        reson_setup(t, r, r->model);
    for (s = gl ? r->ns : 0u; s < r->ns; s++) {
        int16_t *ln = buf + s * r->seg;
        uint32_t li = r->len[s] >> 8, w = r->w, seg = r->seg, tap = r->tap[s];
        uint32_t rp = w >= li ? w - li : w + seg - li, tp = w >= tap ? w - tap : w + seg - tap;   /* read, pickup tap */
        int32_t lp = r->lp[s], lr = r->lr[s], apx = r->apx[s], apy = r->apy[s], g = r->g[s], k = r->k, a = r->a, ink = r->kill || r->hold ? 0 : 1;
        int32_t fr = (int32_t)(r->len[s] & 255u), x1 = ln[rp ? rp - 1u : seg - 1u];   /* the older neighbour */
        for (i = 0; i < n; i++) {                     /* |d - lp| <= 65534; the all-pass output reaches ~2.94 x full scale
                                                         * (|y| < 2^17: (y >> 1) * g fits 32 bits) */
            int32_t x0 = ln[rp], d = x0 + rs_mz((x1 - x0) * fr, 8), y, v;
            x1 = x0;                                  /* (the write position is li >= 2 samples ahead: never read) */
            {                                         /* damping: the step's remainder carried (error feedback):
                                                         * lp follows d exactly, no dead band to hold a value */
                int32_t e = (d - lp) * k + lr, st = e >> 15;   /* |d - lp| <= 65534: e fits 32 bits */
                lr = e - (st * 32768);
                lp += st;
            }
            if (a) {                                  /* STRCT: the all-pass (exact product: dropping bits before
                                                         * it fed a small endless cycle); at 0 a plain sample of delay */
                y = fxres_mulz15(lp-apy,a)+apx;
                apy = y;
            } else {
                y = apx;
            }
            apx = lp;
            v = clamp(((b[i] >> 2) & -ink) + rs_mz(rs_mz(y, 1) * g, 14), -32767, 32767);
            ring[i] += tap ? v - ln[tp] : v;
            ln[w] = (int16_t)v;
            peak |= (uint32_t)(v ^ (v >> 31));          /* >= the largest |v| (the floor test only) */
            if (++w == seg)
                w = 0;
            if (++rp == seg)
                rp = 0;
            if (++tp == seg)
                tp = 0;
        }
        r->lp[s] = lp;
        r->lr[s] = lr;
        r->apx[s] = apx;
        r->apy[s] = apy;
    }
    r->w = (uint16_t)((r->w + n) % r->seg);
    for (i = 0; i < n; i++) {
        int32_t o = r->ns == 4u ? ring[i] : r->ns ? ring[i] * 4 : ring[i];   /* back to the track's scale (CHORD: 4
                                                         * lines summed; MODAL: already) */
        if (r->kill) {
            o = (int32_t)fxres_mul(o,fade,15);
            fade = fade > fstep ? fade - fstep : 0;
        }
        b[i] += (((o - b[i]) >> 5) * mix) >> 10;     /* |o - b| < 2^20: 32-bit */
    }
    r->peak = (uint16_t)peak;
    r->quiet = (uint16_t)(peak < RS_FLOOR ? r->quiet + 1u : 0u);
    r->ring = (uint8_t)(r->quiet <= (r->ns ? r->seg / n : 64u) + 2u);   /* (MODAL: 64 quiet blocks) */
    if (!r->ring && !r->kill) {                       /* quiet for a whole loop: fade the rest out next block (a residue
                                                         * of the loop's rounding must not end as a step) */
        r->ring = r->kill = 1;
        return 1;
    }
    if (r->kill) {
        reson_clear(t, r);
        memset(M, 0, sizeof *M);
        r->model = (uint8_t)m;
    }
    return r->ring;
}

/* Keep runtime in the unused tail of the same FX row. memcpy avoids alignment and aliasing assumptions. */
typedef struct { fxres_t r; px_modal_t modal; } fxres_store_t;
_Static_assert(RS_LEN * sizeof(int16_t) + sizeof(fxres_store_t) <= sizeof fxb[0], "resonator fits the FX buffer");
static const dmodel_t *trk_model(const track_t *t);
static void fxstr_render(track_t *, dvoice_t *, int32_t *, uint32_t);
static void fxpipe_render(track_t *, dvoice_t *, int32_t *, uint32_t);
static void fxchord_render(track_t *, dvoice_t *, int32_t *, uint32_t);
static void fxmodal_render(track_t *, dvoice_t *, int32_t *, uint32_t);
typedef void (*fxres_render_fn)(track_t *, dvoice_t *, int32_t *, uint32_t);
static uint32_t fxres_kind(fxres_render_fn f)
{
    return f == fxstr_render ? RS_STRNG : f == fxpipe_render ? RS_PIPE :
           f == fxchord_render ? RS_CHORD : f == fxmodal_render ? RS_MODAL : RS_OFF;
}
/* The UI and preset loaders can refuse a configuration before changing any sound. */
static const char *fxres_refused(const track_t *t, fxres_render_fn f)
{
    uint32_t i, all = 0, chords = 0, modals = 0, m = fxres_kind(f);
    if (!m) return 0;
    for (i = 0; i < NTRK; i++) {
        uint32_t q = &trk[i] == t ? RS_OFF : fxres_kind(trk_model(&trk[i])->render);
        all += q != RS_OFF;
        chords += q == RS_CHORD;
        modals += q == RS_MODAL;
    }
    if (all >= RS_MAXTRK) return "RESON: 4 TRACKS MAX";
    if (m == RS_CHORD && chords >= 2u) return "CHORD: 2 TRACKS MAX";
    if (m == RS_MODAL && modals >= RS_MAXMODAL) return "MODAL: 2 TRACKS MAX";
    return 0;
}
/* A loaded or remotely edited project also respects the caps, in track order. */
static int fxres_rank_ok(const track_t *t, uint32_t m)
{
    uint32_t i, all = 0, same = 0;
    for (i = 0; i < (uint32_t)(t - trk); i++) {
        uint32_t q = fxres_kind(trk_model(&trk[i])->render);
        all += q != RS_OFF;
        same += q == m;
    }
    return all < RS_MAXTRK && ((m != RS_CHORD && m != RS_MODAL) || same < 2u);
}
static void fxres_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n, uint32_t mode)
{
    fxres_store_t state;
    int32_t y[CTL];
    uint32_t i, k = fx_k(t), input_on;
    if (fx_scratch(t, v)) return;
    if (!fxres_rank_ok(t, mode)) { dm_end(v); return; }
    if (!n) return;
    if (n > CTL) n = CTL;
    memcpy(&state, &fxb[k][RS_LEN], sizeof state);
    state.r.knt = seq_note(t)-60;
    for (i = 0; i < n; i++)
        y[i] = clamp(fx_inp(&t->p[P_E0], fx_in_buf[i]), -131071, 131071);
    reson_block(t, &state.r, &state.modal, mode, y, n);
    memcpy(&fxb[k][RS_LEN], &state, sizeof state);
    input_on = fx_in_on;
    if (state.r.ring) fx_in_on = 1; /* A long quiet part of the delay must not make the voice sleep. */
    fx_out(t, v, y, out, n);
    fx_in_on = input_on;
    if (!state.r.ring && !input_on) dm_end(v);
}
static void fxstr_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n) { fxres_render(t, v, out, n, RS_STRNG); }
static void fxpipe_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n) { fxres_render(t, v, out, n, RS_PIPE); }
static void fxchord_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n) { fxres_render(t, v, out, n, RS_CHORD); }
static void fxmodal_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n) { fxres_render(t, v, out, n, RS_MODAL); }
#define FXRES_DEF(label, render_fn) {label, 1, 0, {PD("TUNE", F_INT, 24, 96, 60), \
    PD("DEC", F_INT, 0, 127, 90), PD("TONE", F_INT, 0, 127, 100), PD("STRCT", F_INT, 0, 127, 40), \
    PD("POS", F_INT, 0, 127, 32), PD("KEYF", F_PCT, 0, 127, 127), PD("MIX", F_PCT, 0, 127, 96), \
    PD("INP", F_INT, 0, 127, 64)}, fx_trigger, render_fn, 1, mm_glide, mm_gate, 1, 1}
#define DM_FXSTR_DEF FXRES_DEF("STRNG", fxstr_render)
#define DM_FXPIPE_DEF FXRES_DEF("PIPE", fxpipe_render)
#define DM_FXCHORD_DEF FXRES_DEF("CHORD", fxchord_render)
#define DM_FXMODAL_DEF FXRES_DEF("MODAL", fxmodal_render)
#undef rs_buf
#endif
