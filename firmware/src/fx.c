/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses, stereo dry mix. */
#define MM2_MUL_OUT 1
#include "mm_busfx.h"
#include "mm_delay.h"
#define DLY_LEN 16384u           /* 0.37 s (MNM-1: the shared delay is PERFORM's THROW only; its RAM the tracks' own) */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
#define TD_LEN 4096u             /* a track's delay: cells a channel (MNM-1, track_delay) */
static int16_t td_buf[NTRK][2][TD_LEN] __attribute__((section(".pool")));
static int16_t tape_buf[MMB_TAPE_CELLS];   /* RAM has room; preserve the pool's 8 KiB safety margin. */
static mmd_state mmtd[NTRK];
static uint8_t mmtd_profile[NTRK];
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441 + 1) / 2];    /* all 997 int16 (upstream's 498 words left ap[996] stale: rev_clear
                                         * missed it, and ROOM played it back after a switch as a click) */
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    mmb_tape_t tape;
    mmb_crunch_t crunch;
    mmb_hiss_t hiss;
    mmb_reverb_t reverb;
    uint8_t bus_ready;
} bfx;
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;   /* BASS+ (spk_bass) */
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
} fx;

/* SPRING (G_RTYPE 1, upstream 1.0.2): one spring of a spring tank, mono like the other buses, in the ROOM's own
 * buffers (no RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass)
 * -> SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* MENU > USB LEVEL FIXED (upstream 1.0.2, #42: record over USB with the speaker turned down): the mix goes to
 * master_out at the full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what
 * the DAC gets (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings (MENU SPEAKER EQ): 0 FLAT, 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the
                                        * small speaker): 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;
/* METRO (GLOBAL 3/3, a device setting: project.c keeps it in the settings record's lowcut bits 5..6; not .noinit, so
 * only the flash record sets it): 0 OFF, 1 LOW, 2 MID, 3 HIGH. While playing, seq.c grids_tick marks each quarter of
 * Grids' clock (gclk.cnt % 4 == 0; bar: the bar's first, % 16): due = 1 + how far the clock had run past the quarter
 * when the block's advance (adv) ended. metro_block starts the click at that point mapped onto the block, a block less
 * a sample after it (own clock: always n - 1 samples, 0.7 ms): in the block whose start fires the beat's steps, and
 * exactly a beat after the click before */
static volatile uint8_t fx_metro;
static struct { uint32_t ph, inc, due, adv; int32_t env; uint8_t bar; } metro;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+ (upstream 1.0.2): what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is
 * clipped at its own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and
 * added. The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB). Upstream's text; its state lives in fx (no new globals). */
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    fx.sb_lp1 += ((m - fx.sb_lp1) * 692) >> 15;
    fx.sb_lp2 += ((fx.sb_lp1 - fx.sb_lp2) * 692) >> 15;
    fx.sb_lp3 += ((fx.sb_lp2 - fx.sb_lp3) * 692) >> 15;
    fx.sb_lp4 += ((fx.sb_lp3 - fx.sb_lp4) * 692) >> 15;
    a = fx.sb_lp4 < 0 ? -fx.sb_lp4 : fx.sb_lp4;
    if (a > fx.sb_env)
        fx.sb_env += (a - fx.sb_env) >> 2;
    else if (fx.sb_env > 0)
        fx.sb_env -= (fx.sb_env >> 11) + 1;
    t = clamp(fx.sb_lp4 * 8, -fx.sb_env, fx.sb_env);
    fx.sb_h1 += (t - fx.sb_h1) >> 5;
    u = t - fx.sb_h1;
    fx.sb_h2 += (u - fx.sb_h2) >> 5;
    u -= fx.sb_h2;
    fx.sb_hl += (u - fx.sb_hl) >> 3;
    return fx.sb_hl * 3;
}

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)                    /* a quarter note: the clock's tempo when following, else BPM */
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
/* length of one division (N_DIV id) in samples at the song tempo: ids 0..5 a quarter / DEN, the slow ids 6..9
 * (1/2 1/1 2BAR 4BAR) a quarter x 2, 4, 8, 16 (upstream 1.0.2) */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* beats = 1 / DEN; original ids fixed, slow rates append */
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */

static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v would be a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.sp / 4u; i++)
        rev_u.sp[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

static int32_t stereo_wet[CTL];
static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */

/* process the three buses for one block; sends in, wet stereo-equal out */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet,
                     uint32_t n)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6, rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r;
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
        wet[i] = y;
    }
    for (i = 0; i < n; i++) stereo_wet[i] = 0;
    rt = clamp(song.g[G_RTYPE], 0, 2);
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;   /* its buffers are cleared, the new */
        for (i = 0; i < n; i++)                                     /* one starts from silence */
            t[i] = 0;
        if (fx.rtype == 1)
            rev_spring(rev_in, t, n);
        else if (!fx.rtype)
            rev_room(rev_in, t, n);
        for (i = 0; i < n; i++, g -= d)
            wet[i] += mulq16(t[i], (uint32_t)g);
        rev_clear();
        mmb_reverb_init(&bfx.reverb, rev_comb);
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt == 2) {
        for (i = 0; i + 16u <= n; i += 16u) {
            int32_t l[16] = {0}, r[16] = {0}, send[16];
            uint32_t j;
            for (j = 0; j < 16; j++) send[j] = clamp(rev_in[i + j], -32767, 32767);
            mmb_reverb_block(&bfx.reverb, song.g[G_RSIZE], song.g[G_RDAMP], send, l, r);
            for (j = 0; j < 16; j++) {
                wet[i + j] += (l[j] + r[j]) / 2;
                stereo_wet[i + j] = (l[j] - r[j]) / 2;
            }
        }
    } else if (rt == 1)
        rev_spring(rev_in, wet, n);
    else
        rev_room(rev_in, wet, n);
}

/* one block of the whole mix (shared with the host tests): events -> each drum track -> dist -> SLICER
 * -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static void midi_drop(void);                             /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];
/* MNM-1 routing: each track onto one of the three stereo buses AB CD EF (P_OUT; OFF: none, no
 * sends either), the buses summed to the output (its 3xSTEREO + AB=MIX on the FM-1's one stereo out). An FX machine's
 * input (P_IN, mm_fx.c): NEIB, the track before's block after its LEVEL (trk_post, mono, before PAN), or a bus as it
 * stands when the FX track is mixed (the tracks before it, in index order; the COMP source, mixed first, always in it);
 * with its OUT the bus it takes (an insert), its output replaces that bus */
static int32_t bus_l[3][CTL], bus_r[3][CTL], trk_post[NTRK][CTL];
static uint8_t trk_post_on;                              /* trk_post holds a sounding block (bit per track) */
static int32_t fx_in_buf[CTL];                           /* an FX track's input this block (mm_fx.c reads it) */
static uint32_t fx_in_on;                                /* .. anything in it */

/* EFFECTS' delay (MNM-1): each track its own, stereo, after its PAN (DSND: the send; under 0 the
 * stereo image kept, over 0 the sides swapped and the echoes crossing over: ping-pong), its time DTIM in 1/256 notes
 * (64 a beat), its feedback DFB (64: unity; above: growing into the clip), a one-pole high-pass DBAS and a low-pass DWID
 * over it in the loop. A tape: TD_LEN cells a channel, written at a rate that makes the loop the delay's length (a
 * cell an input sample up to 93 ms; longer: fewer cells a second, each the average of its samples, lower fidelity the
 * longer, as a tape slowed down); a new DTIM glides the rate there (the pitch bends, no click). The output onto the
 * track's bus. Asleep (no cost) once its input and output stayed quiet a whole loop and more */
static const uint16_t TD_POLE[128] = {   /* one-pole coefficient (Q15) at the 30 Hz .. 16 kHz table's cutoffs */
    140, 147, 154, 162, 170, 179, 188, 197, 207, 218, 229, 240, 253, 265, 279, 293,
    307, 323, 339, 356, 374, 393, 413, 434, 456, 479, 503, 528, 554, 582, 612, 642,
    674, 708, 744, 781, 820, 861, 904, 949, 997, 1046, 1098, 1153, 1210, 1271, 1334, 1400,
    1469, 1542, 1618, 1698, 1782, 1869, 1961, 2057, 2158, 2264, 2374, 2490, 2611, 2737, 2870, 3008,
    3153, 3305, 3463, 3629, 3801, 3982, 4170, 4367, 4572, 4786, 5009, 5241, 5483, 5736, 5998, 6271,
    6555, 6849, 7156, 7474, 7803, 8145, 8499, 8866, 9245, 9637, 10041, 10459, 10890, 11333, 11789, 12258,
    12739, 13233, 13738, 14255, 14783, 15322, 15871, 16428, 16995, 17568, 18149, 18735, 19325, 19919, 20514, 21110,
    21705, 22298, 22886, 23468, 24044, 24609, 25165, 25707, 26236, 26748, 27244, 27721, 28177, 28612, 29025, 29415,
};
static struct {
    uint32_t wp, rate, dc;       /* the write head (cells, Q16), the cells a sample (Q16), the loop's cells (Q16) */
    int32_t acc[2], hp[2], lp[2];   /* a cell's sum; the loop filters' states */
    uint32_t quiet;              /* samples it stayed quiet */
    uint8_t on;                  /* awake */
} td[NTRK];
static int32_t td_in_l[CTL], td_in_r[CTL];

/* track k's delay one block: its input (in: anything in td_in_*) mixed in, its echoes added to bl / br */
static __attribute__((noinline)) void track_delay(const track_t *t, uint32_t k, int32_t *bl, int32_t *br, uint32_t n,
                                                  uint32_t in)
{
    uint32_t i, d = (uint32_t)clamp(t->p[P_DTIM], 1, 127) * beat_samples() / 64u, rt, dt, pk = 0;
    int32_t fb = clamp(t->p[P_DFB], 0, 127) * 256, ping = t->p[P_DLY] > 0;
    int32_t ah = TD_POLE[clamp(t->p[P_DBAS], 0, 127)], al = TD_POLE[clamp(t->p[P_DBAS] + t->p[P_DWID], 0, 127)];
    int16_t *cl = td_buf[k][0], *cr = td_buf[k][1];
    if (!td[k].on) {
        if (!in)
            return;
        td[k].on = 1;
        td[k].quiet = 0;
    }
    d = d < 2u ? 2u : d;
    if (d <= TD_LEN - 2u) {
        rt = 1u << 16;
        dt = d << 16;
    } else {
        rt = (uint32_t)(((uint64_t)(TD_LEN - 2u) << 16) / d);
        dt = (TD_LEN - 2u) << 16;
    }
    if (!td[k].rate) {                                    /* woken: at once */
        td[k].rate = rt;
        td[k].dc = dt;
    }
    td[k].rate += (uint32_t)((int32_t)(rt - td[k].rate) >> 4);   /* a new time glides (tape) */
    td[k].dc += (uint32_t)((int32_t)(dt - td[k].dc) >> 4);
    {                                                     /* (the state in locals: the loop is the cost) */
        uint32_t wp = td[k].wp, rate = td[k].rate, dc = td[k].dc;
        int32_t a0 = td[k].acc[0], a1 = td[k].acc[1], h0 = td[k].hp[0], h1 = td[k].hp[1], l0 = td[k].lp[0];
        int32_t l1 = td[k].lp[1], *xl = td_in_l, *xr = td_in_r;   /* (ping-pong's input: swapped by mix_part) */
        for (i = 0; i < n; i++) {                         /* the input into the cell being written; the echoes read
                                                           * between cells */
            uint32_t rp = wp - dc, c0 = (rp >> 16) & (TD_LEN - 1u), c1 = (c0 + 1u) & (TD_LEN - 1u), nx = wp + rate;
            int32_t f = (int32_t)(rp & 0xFFFFu) >> 1, y0, y1;
            y0 = cl[c0] + (cl[c1] - cl[c0]) * f / 32768;
            y1 = cr[c0] + (cr[c1] - cr[c0]) * f / 32768;
            a0 += xl[i] / 16;                             /* (toward zero throughout: a loop dying out reaches 0, no
                                                           * DC held, none handed on to an FX's input) */
            a1 += xr[i] / 16;
            if ((nx ^ wp) >> 16) {                        /* a cell full: the average of its samples, and the loop at
                                                           * the cells' rate: the cell a loop back through the high-pass
                                                           * and the low-pass (states Q8 over the cells: no dead band
                                                           * holding a DC), the feedback */
                uint32_t c = (wp >> 16) & (TD_LEN - 1u), r = (c - (dc >> 16)) & (TD_LEN - 1u);
                int32_t w0, w1;
                h0 += (int32_t)((int64_t)(cl[r] * 256 - h0) * ah / 32768);
                h1 += (int32_t)((int64_t)(cr[r] * 256 - h1) * ah / 32768);
                l0 += (int32_t)((int64_t)(cl[r] * 256 - h0 - l0) * al / 32768);
                l1 += (int32_t)((int64_t)(cr[r] * 256 - h1 - l1) * al / 32768);
                w0 = (int32_t)((int64_t)l0 * fb / (16384 * 256));
                w1 = (int32_t)((int64_t)l1 * fb / (16384 * 256));
                if (rate != 1u << 16) {
                    a0 = (int32_t)((int64_t)a0 * rate / 65536);
                    a1 = (int32_t)((int64_t)a1 * rate / 65536);
                }
                cl[c] = (int16_t)clamp(a0 + (ping ? w1 : w0), -32767, 32767);   /* ping-pong: the echoes cross over */
                cr[c] = (int16_t)clamp(a1 + (ping ? w0 : w1), -32767, 32767);
                a0 = a1 = 0;
            }
            wp = nx;
            bl[i] += y0 * 16;
            br[i] += y1 * 16;
            pk |= (uint32_t)((y0 < 0 ? -y0 : y0) | (y1 < 0 ? -y1 : y1));
        }
        td[k].wp = wp;
        td[k].acc[0] = a0;
        td[k].acc[1] = a1;
        td[k].hp[0] = h0;
        td[k].hp[1] = h1;
        td[k].lp[0] = l0;
        td[k].lp[1] = l1;
    }
    if (in || pk > 16u)                               /* (cells: 16 ~ -66 dB) */
        td[k].quiet = 0;
    else if ((td[k].quiet += n) > d + 4096u) {           /* a whole loop quiet: asleep (its cells near silence) */
        td[k].on = 0;
        td[k].rate = 0;
    }
}

/* the FX track t's input this block into fx_in_buf (mono, the track's own scale) */
static void fx_prepare(const track_t *t, uint32_t n)
{
    uint32_t i, k = (uint32_t)(t - trk), in = (uint32_t)clamp(t->p[P_IN], 0, 3);
    fx_in_on = 0;
    if (in == IN_NEIB) {
        if (k == 0u || !((trk_post_on >> (k - 1u)) & 1u)) {
            for (i = 0; i < n; i++)
                fx_in_buf[i] = 0;
            return;
        }
        for (i = 0; i < n; i++)
            fx_in_buf[i] = trk_post[k - 1u][i];
    } else {
        for (i = 0; i < n; i++)
            fx_in_buf[i] = (bus_l[in - 1u][i] + bus_r[in - 1u][i]) >> 1;
    }
    if (trk_model(t)->mm >= 2u) {
        if (in == IN_NEIB && k && trk_model(&trk[k-1])->mm >= 2u)
            for (i = 0; i < n; i++) fx_in_buf[i] = t->model == DM_M2_FXR ? mmen_post[k-1][i] : mmen_pre[k-1][i];
        for (i = 0; i < n; i++) mmen_carrier[i] = k >= 2u && ((trk_post_on >> (k-2u)) & 1u) ?
            (trk_model(&trk[k-2])->mm >= 2u ? mmen_pre[k-2][i] : mm2_sat16(trk_post[k-2][i])) : 0;
    }
    for (i = 0; i < n; i++)
        fx_in_on |= (uint32_t)fx_in_buf[i];
}

/* Calibrated delay shares the legacy track's 16 KiB allocation. */
static void calibrated_delay(track_t *t, uint32_t k, int32_t *bl, int32_t *br,
                             const int32_t *left, const int32_t *right, uint32_t n)
{
    uint32_t off, j;
    for (off = 0; off + 16u <= n; off += 16u) {
        int32_t l[16], r[16], wl[16], wr[16];
        for (j = 0; j < 16; j++) {
            l[j] = left ? clamp(left[off+j], -32767, 32767) : 0;
            r[j] = right ? clamp(right[off+j], -32767, 32767) : 0;
        }
        mmd_process(&mmtd[k], (uint8_t *)td_buf[k], l, r, wl, wr,
                    song.g[G_BPM], t->p[P_DTIM], t->p[P_DLY]+64,
                    t->p[P_DFB], t->p[P_DBAS], t->p[P_DWID]);
        if (bl && br) for (j = 0; j < 16; j++) {
            bl[off+j] += wl[j]; br[off+j] += wr[j];
        }
    }
}

/* one track into the dry mix and the sends; a track with no voice sounding costs the LFO tick and a cleared
 * buffer only (after the DIST tail has run out). src: the COMP source (NTRK = off). The source's block (after
 * DIST / SLICER, before LEVEL and MUTE) feeds the compressor; a muted source (trk_silent: SOLO too) is heard by it
 * only (ghost key) with GHOST KEEP, by nobody with MUTE; with HIDE the source is never heard in the mix (G_CGHOST). A DUCK track is multiplied by the compressor's gain before its level, pan and sends. */
/* Only independent calibrated renders run concurrently. Routing, compressor,
 * delays and inserts keep their serial order and shared scratch ownership. */
static int (*cal_parallel_begin)(void (*)(void *), void *);
static void (*cal_parallel_end)(void);
static int32_t cal_pcm[NTRK][CTL];
static uint8_t cal_sounding[NTRK], cal_cached, cal_worker_mask;
static uint32_t cal_frames;
static void cal_render_mask(uint32_t mask)
{
    for (unsigned k=0;k<NTRK;k++) if (mask & (1u<<k)) {
        memset(mmen_left[k],0,sizeof mmen_left[k]);
        memset(mmen_right[k],0,sizeof mmen_right[k]);
        memset(mmen_reverb_send[k],0,sizeof mmen_reverb_send[k]);
        cal_sounding[k]=(uint8_t)track_render(&trk[k],cal_pcm[k],cal_frames);
    }
}
static void cal_render_worker(void *context)
{
    (void)context;
    cal_render_mask(cal_worker_mask);
}
static void cal_render_parallel(uint32_t n)
{
    unsigned mask=0, worker=0, main_cost=0, worker_cost=0;
    cal_cached=0;
    if (!cal_parallel_begin || !cal_parallel_end) return;
    for (unsigned k=0;k<NTRK;k++) {
        unsigned m=trk[k].model;
        /* SID reads the preceding track's same-block pitch; FX reads routing.
         * Keep both serial, including modulation that changes their inputs. */
        if (!mmen_is_model(m) || m==MM_MODEL_FIRST+MM2_SID ||
            trk[k].p[P_MODEL]!=(int16_t)m || DMODELS[m].fx) continue;
        if (k+1u<NTRK && trk[k+1u].model==MM_MODEL_FIRST+MM2_SID) continue;
        unsigned cost=m==MM_MODEL_FIRST+MM2_ENS?2:1;
        mask|=1u<<k;
        if (worker_cost<main_cost) {worker|=1u<<k;worker_cost+=cost;}
        else main_cost+=cost;
    }
    if (!worker) return;
    cal_frames=n;cal_worker_mask=(uint8_t)worker;
    if (!cal_parallel_begin(cal_render_worker,0)) return;
    cal_render_mask(mask & ~worker);
    cal_parallel_end();
    cal_cached=(uint8_t)mask;
}
static void mix_part(track_t *t, uint32_t n, uint32_t src)
{
    int32_t *b = part_buf, side[CTL] = {0};
    uint32_t cal = trk_model(t)->mm >= 2u;
    uint32_t i, is_src = (uint32_t)(t - trk) == src, snd, k = (uint32_t)(t - trk), out = (uint32_t)clamp(t->p[P_OUT], 0, 3);
    if (mmtd_profile[k] != cal + 1u) {
        memset(&td[k], 0, sizeof td[k]);
        if (cal) mmd_init(&mmtd[k], (uint8_t *)td_buf[k]);
        else memset(td_buf[k], 0, sizeof td_buf[k]);
        mmtd_profile[k] = (uint8_t)(cal + 1u);
    }
    if (cal && !(cal_cached & (1u<<k))) {
        memset(mmen_left[k], 0, sizeof mmen_left[k]);
        memset(mmen_right[k], 0, sizeof mmen_right[k]);
        memset(mmen_reverb_send[k], 0, sizeof mmen_reverb_send[k]);
    }
    if (trk_model(t)->fx)
        fx_prepare(t, n);                               /* its input, before it renders (mm_fx.c) */
    if (cal_cached & (1u<<k)) {
        memcpy(b,cal_pcm[k],n*sizeof *b);
        snd=cal_sounding[k];
    } else snd = track_render(t, b, n);
    if (!cal) snd = trk_inserts(t, b, n, snd);                   /* RESON, then FILTER (filter.c), before DIST */
    if (cal) for (i = 0; i < n; i++) side[i] = (mmen_left[k][i] - mmen_right[k][i]) / 2;
    if (snd)
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (cal) calibrated_delay(t, k, out < BUS_OFF ? bus_l[out] : 0,
                                  out < BUS_OFF ? bus_r[out] : 0, 0, 0, n);
        if (!cal && td[k].on && out < BUS_OFF)                  /* its delay's echoes ring on */
            track_delay(t, k, bus_l[out], bus_r[out], n, 0);
        if ((pf.mute >> (t - trk)) & 1u)
            perf_mute_idle((uint32_t)(t - trk));        /* perform.c: a white key in the FX layer */
        if (is_src) {
            comp_block(0, n);                           /* silence: the detector decays */
            t->gfade = trk_silent(t) || song.g[G_CGHOST] == CG_HIDE ? 32767u : 0u;   /* nothing sounds: at once */
        }
        return;
    }
    {
        int32_t lvl = cal ? 4096 : LEVEL_Q12[t->p[P_LEVEL] & 127], pan = cal ? 0 : t->p[P_PAN], *bl, *br, *post = trk_post[k];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = 0, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t ds = (t->p[P_DLY] < 0 ? -t->p[P_DLY] : t->p[P_DLY]) * 512, dsw = t->p[P_DLY] > 0;   /* the track's
                                                         * delay's send, Q15; ping-pong: the sides swapped */
        int32_t xmax = c > ds ? c : ds;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        if (!cal) track_dist(t, b, n);
        slicer_track_stereo(t, b, cal ? side : 0, n);                          /* slicer.c: before the level, pan and sends */
        if ((pf.mute >> (t - trk)) & 1u) {
            int32_t gain = pf.mg[k];
            perf_mute(k, b, n);
            if (cal) { pf.mg[k] = gain; perf_mute(k, side, n); }
        }       /* perform.c: a white key in the FX layer */
        if (is_src) {
            int sil = trk_silent(t), hide = sil || song.g[G_CGHOST] == CG_HIDE;
            comp_block(sil && song.g[G_CGHOST] == CG_MUTE ? 0 : b, n);   /* GHOST MUTE: muted keys nothing */
            if (hide || t->gfade) {                     /* ghost key: the compressor hears it, the mix does not;
                                                         * a mute / unmute / GHOST change fades over 5 ms */
                int32_t g = t->gfade, to = hide ? 32767 : 0;
                if (g == 32767 && to == 32767)
                    return;                             /* silent in the mix the whole block (a fade's last block
                                                         * still mixes its faded samples) */
                for (i = 0; i < n; i++) {
                    g = g < to ? (g + 150 < to ? g + 150 : to) : (g - 150 > to ? g - 150 : to);
                    b[i] = mmc_mul(b[i], 32767 - g, 15);
                    if (cal) side[i] = mmc_mul(side[i], 32767 - g, 15);
                }
                t->gfade = (uint16_t)g;
            }
        } else if (src < NTRK && t->p[P_DUCK]) {
            for (i = 0; i < n; i++) {
                b[i] = clamp(mmc_mul(b[i], comp.gain[i], 16), -(1 << 19), 1 << 19);
                if (cal) side[i] = mmc_mul(side[i], comp.gain[i], 16);
            }
        }
        if (out == BUS_OFF)
            c = d = r = ds = 0;                         /* OUT OFF: on no bus, no sends (heard through an FX only) */
        bl = bus_l[out < BUS_OFF ? out : 0];
        br = bus_r[out < BUS_OFF ? out : 0];
        if (trk_model(t)->fx && out < BUS_OFF && t->p[P_IN] == (int16_t)(out + 1u))
            for (i = 0; i < n; i++)
                bl[i] = br[i] = 0;                      /* an insert: its output takes the bus's place */
        for (i = 0; i < n; i++) {
            int32_t x = cal ? b[i] : clamp(((b[i] >> 2) * lvl) >> 10, -524287, 524287), a = x < 0 ? -x : x;   /* pre-shift: 8 loud
                                                         * voices; the clamp: x * gl (Q12) fits (COMP makeup) */
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            post[i] = x;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            if (out < BUS_OFF) {
                bl[i] += ((x * gl) >> 12) + side[i];
                br[i] += ((x * gr) >> 12) - side[i];
                if (cal) send_r[i] += mmen_reverb_send[k][i];
            }
            if (cal) {
                mmen_left[k][i] = x + side[i];
                mmen_right[k][i] = x - side[i];
            }
            if (ds && !cal) {
                int32_t sl = mulq15(xs, ds);
                td_in_l[i] = (sl * (dsw ? gr : gl)) >> 12;
                td_in_r[i] = (sl * (dsw ? gl : gr)) >> 12;
            }
        }
        if (cal) calibrated_delay(t, k, out < BUS_OFF ? bl : 0, out < BUS_OFF ? br : 0,
                                  out < BUS_OFF ? mmen_left[k] : 0, out < BUS_OFF ? mmen_right[k] : 0, n);
        if (!cal && (ds || (td[k].on && out < BUS_OFF))) {
            if (!ds)
                for (i = 0; i < n; i++)
                    td_in_l[i] = td_in_r[i] = 0;
            track_delay(t, k, bl, br, n, ds != 0);
        }
        trk_post_on |= (uint8_t)(1u << k);
        t->peak = pk;
    }
}

/* METRO's click, added after the FX layer (PERFORM's effects leave it alone), scaled by the MASTER level mg (the
 * volume knob turns it down with the mix): a sine, 1 kHz (1.6 kHz on the bar's
 * first), from phase 0 (no step), decaying 1/128 a sample (2.9 ms: -40 dB at 13 ms), ended below 1 LSB (~28 ms at
 * HIGH). LVL: the start amplitude (Q8 in env) for a peak of about -24 / -15 / -6 dBFS at the output (the decay before
 * the first peak and the soft clip counted). noinline: the audio ISR's loop code stays as it was (target_budget.py) */
static __attribute__((noinline)) void metro_block(int32_t *l, int32_t *r, uint32_t n, int32_t mg)
{
    static const int32_t LVL[4] = {0, 2230, 6330, 19400};
    uint32_t i, s = n;
    if (metro.due) {
        uint32_t p = metro.due - 1u, a = metro.adv;
        metro.due = 0;
        if (fx_metro)                                   /* following a clock, the advance maps onto the block */
            s = p < a ? (a - 1u - p) * n / a : 0u;
    }
    for (i = 0; i < n; i++) {
        int32_t x;
        if (i == s) {
            metro.ph = 0;
            metro.inc = metro.bar ? 155826478u : 97391549u;   /* 1.6 kHz, 1 kHz: f * 2^32 / FS */
            metro.env = LVL[fx_metro & 3u] << 8;
        }
        if (!metro.env)
            continue;
        x = (((sine_i(metro.ph) * (metro.env >> 8)) >> 15) * mg) >> 12;   /* MASTER_FULL = unity */
        metro.ph += metro.inc;
        metro.env -= metro.env >> 7;
        if (metro.env < 256)
            metro.env = 0;
        l[i] += x;
        r[i] += x;
    }
}

static uint32_t comp_was = NTRK;                 /* the COMP source of the last block (NTRK = off) */
static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int perf;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    if (safe_start) {                                 /* safe start: silence, no track / model / FX code */
        for (i = 0; i < n; i++)
            out[2u * i] = out[2u * i + 1u] = 0;
        midi_drop();                                    /* MIDI in dropped, the ring kept empty: a full ring holds
                                                         * every USB packet back (usb_app.c), the installer's SysEx too */
        return;
    }
    for (i = 0; i < n; i++) {
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
        bus_l[0][i] = bus_r[0][i] = bus_l[1][i] = bus_r[1][i] = bus_l[2][i] = bus_r[2][i] = 0;
    }
    trk_post_on = 0;
    ctlmod_begin();
    lfo_apply(n);                                       /* LFOs: the modulated knobs for this block (lfo.c) */
    events_block(n);
    mmen_lfo_prepare(n);
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
    drum_block_begin();
    cal_render_parallel(n);
    {
        uint32_t src = comp_src();
        if (src != comp_was) {                          /* another source, or OFF: a fresh detector (comp_was:
                                                         * file scope, before mix_block, = NTRK at start) */
            comp_reset();
            comp_was = src;
        }
        if (src < NTRK)
            mix_part(&trk[src], n, src);                /* the source first: its block keys the others */
        for (i = 0; i < NTRK; i++)
            if (i != src)
                mix_part(&trk[i], n, src);
    }
    for (i = 0; i < n; i++) {                           /* the three buses to the output */
        mix_l[i] = bus_l[0][i] + bus_l[1][i] + bus_l[2][i];
        mix_r[i] = bus_r[0][i] + bus_r[1][i] + bus_r[2][i];
    }
    lfo_restore();                                      /* the knobs as set again */
    ctlmod_end();
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);  /* THROW */
    if (!bfx.bus_ready) {
        mmb_tape_init(&bfx.tape, tape_buf);
        mmb_crunch_init(&bfx.crunch);
        mmb_hiss_init(&bfx.hiss);
        mmb_reverb_init(&bfx.reverb, rev_comb);
        bfx.bus_ready = 1;
    }
    fx_buses(send_c, send_d, send_r, wet, n);
    for (i = 0; i + 16u <= n; i += 16u) {
        if (song.g[G_TAPE] || song.g[G_ADPC] || song.g[G_HISS] || song.g[G_DRIVE]) {
            uint32_t j;
            for (j = i; j < i + 16u; j++) {
                mix_l[j] = clamp(mix_l[j] + wet[j] + stereo_wet[j], -32767, 32767);
                mix_r[j] = clamp(mix_r[j] + wet[j] - stereo_wet[j], -32767, 32767);
                wet[j] = stereo_wet[j] = 0;
            }
            mmb_tape_block(&bfx.tape, song.g[G_TAPE], mix_l + i, mix_r + i);
            mmb_crunch_block(&bfx.crunch, song.g[G_ADPC], mix_l + i, mix_r + i);
            mmb_hiss_block(&bfx.hiss, song.g[G_HISS], song.playing, mix_l + i, mix_r + i);
            if (song.g[G_DRIVE]) {
                int32_t drive = mmb_t_driv[song.g[G_DRIVE] & 127];
                for (j = i; j < i + 16u; j++) {
                    mix_l[j] = mmb_limit((mix_l[j] * drive) >> 8);
                    mix_r[j] = mmb_limit((mix_r[j] * drive) >> 8);
                }
            }
        }
    }
    for (i = 0; i < n; i++) {                           /* the master level */
        mix_l[i] = (clamp((mix_l[i] + wet[i] + stereo_wet[i]) >> 2, -524287, 524287) * mg) >> 10;
        mix_r[i] = (clamp((mix_r[i] + wet[i] - stereo_wet[i]) >> 2, -524287, 524287) * mg) >> 10;   /* fits Q12 */
    }
    if (perf)
        perf_block(mix_l, mix_r, n);                    /* the FX layer's effects (perform.c) */
    if(multi.envactive&&song.multi.mode!=MULTI_SPLIT){
        for(i=0;i<n;i++){
            int gain=multi_env_sample(&multi,&song.multi);
            if(gain<32767){mix_l[i]=mulq15(mix_l[i],gain);mix_r[i]=mulq15(mix_r[i],gain);}
        }
    }
    if (metro.due | metro.env)
        metro_block(mix_l, mix_r, n, mg);               /* METRO: after the FX layer, at the MASTER level */
    for (i = 0; i < n; i++) {
        int32_t l = mix_l[i], r = mix_r[i];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
