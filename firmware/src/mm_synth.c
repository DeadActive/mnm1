/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 (Hortator fork): 2026 */
/* The synth machines (MNM-1): every track one mono voice, a machine (the oscillator section,
 * its 8 SYNTH knobs) into the track's AMP envelope (ATTACK HOLD DECAY RELEASE, PORTAMENTO: track parameters, the AMP
 * page), then the track's FILTER, SRR, DIST and sends (fx.c). Gated models (as ACID): the sequencer, the keys and a
 * hit's countdown open and close the gate (seq.c acid_step); a slid-into step or a legato key glides (mm_glide).
 * AMP: ATTACK rises linearly (1 ms .. 10 s, ENV_LIN), HOLD keeps it full (1 ms .. 8 s; 127 GATE: while the gate is
 * open), DECAY falls (5 ms .. 4 s to -60 dB, DECAY_K; 127 INF: stays), the gate closing RELEASEs from anywhere.
 * Pitch: the note in 1/16 semitone (the step's, the key's) glides at PORT (0 = jumps; a slide without PORT ~60 ms);
 * TUNE and each machine's FINE are added per block, so an LFO on them moves the voice at once. Phases start at the
 * voice's random seed (free-running oscillators: no two notes start alike). */
enum { MS_ATK, MS_HOLD, MS_DEC, MS_REL };
#define MM_LIFE (44100u * 30u)                          /* a gate still open after 30 s closes (the backstop) */
#define MM_P16_MAX (128 * 16 - 1)

static inline uint32_t mm_inc(int32_t p16) { return PITCH_INC[clamp(p16, 0, MM_P16_MAX)]; }
static inline int32_t mm_note16(const track_t *t) { return seq_note(t) * 16 + seq_multi_pitch(t)%16; }
/* the glide's per-block factor (ENV_EXP: ~99 % of the way in its time): PORT 1..127 = ~4 ms .. ~3 s; 0: a slide's
 * ~60 ms */
static int32_t mm_glide_k(int32_t port)
{
    return ENV_EXP[port > 0 ? 20 + clamp(port, 0, 127) * 90 / 127 : 56];
}

static void mm_start(track_t *t, dvoice_t *v)
{
    mm_t *m = &v->ms.mm;
    uint32_t i, r = (uint32_t)v->rng;
    m->tgt = mm_note16(t);
    m->p16 = t->p[P_PORT] > 0 && t->mm_p16
        ? t->mm_p16 + seq_multi_pitch(t) - t->multi_pitch_ref : m->tgt;
    t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);
    m->gk = mm_glide_k(t->p[P_PORT]);
    m->amp = 0;
    m->stage = MS_ATK;
    m->gate = 1;
    for (i = 0; i < 6u; i++) {
        r = r * 1664525u + 1013904223u;
        m->ph[i] = r;
    }
    m->rng = r | 1u;
    t->mm_p16 = m->tgt;
}

static void mm_glide(track_t *t, dvoice_t *v)            /* a slide or a legato key: a new target, nothing restarts */
{
    mm_t *m = &v->ms.mm;
    m->tgt = mm_note16(t);
    /* Preserve the current audible pitch when changing its MULTI reference. */
    m->p16 += seq_multi_pitch(t) - t->multi_pitch_ref;
    t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);
    t->trig_nt=t->nt;
    m->gk = mm_glide_k(t->p[P_PORT]);
    v->t = 0;
    t->mm_p16 = m->tgt;
}

static void mm_gate(track_t *t, dvoice_t *v, int open)
{
    mm_t *m = &v->ms.mm;
    (void)t;
    m->gate = (uint8_t)(open != 0);
    if (!open)
        m->stage = MS_REL;
    else if (m->stage == MS_REL)
        m->stage = MS_ATK;                               /* the attack goes on from where the release is */
}

/* the pitch this block (1/16 semitone over MIDI 0): the note's glide one block on, + TUNE + fine (1/16 units) */
static int32_t mm_pitch(const track_t *t, dvoice_t *v, int32_t fine16)
{
    mm_t *m = &v->ms.mm;
    int32_t d = m->tgt - m->p16;
    if (d) {
        int32_t s = (int32_t)(((int64_t)d * m->gk) >> 16);
        m->p16 += s ? s : d > 0 ? 1 : -1;
    }
    return m->p16 + seq_multi_pitch(t)-t->multi_pitch_ref + t->p[P_E0] * 16 + fine16 + t->lpit;   /* (lpit: the LFOs' PTCH, lfo.c) */
}

/* the AMP envelope n samples on (amp[i], Q24, per sample); the voice ends once released (or decayed) under ENV_END */
static void mm_amp_block(track_t *t, dvoice_t *v, int32_t *ampv, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = t->p;
    int32_t amp = m->amp;
    uint32_t i, ainc = ENV_LIN[clamp(p[P_AATK], 0, 127)] >> 5;   /* ENV_LIN is per CTL (32 samples) */
    uint32_t hinf = p[P_AHOLD] >= 127, dinf = p[P_ADEC] >= 127;
    uint32_t dkk = DECAY_K[clamp(p[P_ADEC], 0, 127)], rkk = DECAY_K[clamp(p[P_AREL], 0, 127)];
    if (!ainc)
        ainc = 1;
    for (i = 0; i < n; i++) {
        switch (m->stage) {
        case MS_ATK:
            amp += (int32_t)ainc;
            if (amp >= ENV1) {
                amp = ENV1;
                m->stage = MS_HOLD;
                m->hold = hinf ? 0u : TIME_MS_X10[clamp(p[P_AHOLD], 0, 126)] * 441u / 100u;
            }
            break;
        case MS_HOLD:
            if (!hinf) {
                if (m->hold)
                    m->hold--;
                else
                    m->stage = MS_DEC;
            }
            break;
        case MS_DEC:
            if (!dinf)
                amp = mulq16(amp, dkk);
            break;
        default:
            amp = mulq16(amp, rkk);
            break;
        }
        ampv[i] = amp;
    }
    m->amp = amp;
    v->t += n;
    if (v->t >= MM_LIFE && m->stage != MS_REL)
        m->stage = MS_REL;
    if (m->stage >= MS_DEC && amp < ENV_END && (m->stage == MS_REL || !dinf))
        dm_end(v);
}

/* the machine's block buf (Q15, +-32767 full scale) through the AMP envelope into out: velocity, VOICE_FS */
static void mm_out(track_t *t, dvoice_t *v, const int32_t *buf, int32_t *out, uint32_t n)
{
    int32_t ampv[CTL], g = mulq15(vel_gain(v), VOICE_FS), y = 0;
    uint32_t i;
    mm_amp_block(t, v, ampv, n);
    for (i = 0; i < n; i++) {
        y = mulq15((int32_t)(((int64_t)buf[i] * ampv[i]) >> 24), g);
        out[i] += y;
    }
    v->last = v->active ? y : 0;
}

static inline int32_t mm_noise(mm_t *m)                  /* white noise, Q15 */
{
    m->rng ^= m->rng << 13;
    m->rng ^= m->rng >> 17;
    m->rng ^= m->rng << 5;
    return (int32_t)m->rng >> 16;
}
static inline uint32_t mm_detune(uint32_t inc, int32_t q16)   /* inc x (1 + q16 / 65536) */
{
    return inc + (uint32_t)(((int64_t)inc * q16) >> 16);
}
/* a one-pole low-pass one sample on (64-bit: the difference reaches +-2^17) */
static inline int32_t mm_lp(int32_t lp, int32_t x, int32_t k) { return lp + (int32_t)(((int64_t)(x - lp) * k) >> 15); }
/* a one-pole low-pass's coefficient (Q15) from a 0..127 knob: 127 open */
static inline int32_t mm_tone_k(int32_t v) { v = clamp(v, 0, 127) + 1; return v * v * 2 - 1; }

/* ---- SWSAW (SWAVE-SAW): up to 5 detuned saws (the centre one at its own level), a square an octave down, a tone
 * low-pass. TUNE DTUN NUM SUB, FINE TONE CNTR OCT */
static void mmsaw_trigger(track_t *t, dvoice_t *v) { mm_start(t, v); }
static void mmsaw_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    static const int8_t SIDE[5] = {0, -2, 2, -1, 1};      /* the saws' detune: the centre, the outer pair, the inner */
    static const int16_t NORM[6] = {0, 32767, 26000, 21000, 18000, 16000};   /* ~1 / sqrt(N), Q15 */
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL];
    uint32_t i, k, nv = (uint32_t)clamp(p[2], 1, 5), inc[5], isub;
    int32_t pc = mm_pitch(t, v, p[4] / 4) + clamp(p[7], -2, 2) * 192, dt = clamp(p[1], 0, 127);
    int32_t cg = clamp(p[6], 0, 127) * 258, sg = clamp(p[3], 0, 127) * 258, tk = mm_tone_k(p[5]), lp = m->s[0];
    uint32_t base = mm_inc(pc);
    for (k = 0; k < nv; k++)
        inc[k] = mm_detune(base, SIDE[k] * dt * 10 + (int32_t)k * 3);   /* (+3k: never two in lock) */
    isub = base >> 1;
    for (i = 0; i < n; i++) {
        int32_t x = mulq15(osc_saw(m->ph[0], inc[0]), cg);
        m->ph[0] += inc[0];
        for (k = 1; k < nv; k++) {
            x += osc_saw(m->ph[k], inc[k]);
            m->ph[k] += inc[k];
        }
        x = (int32_t)(((int64_t)x * NORM[nv]) >> 15);
        if (sg) {
            x += mulq15(osc_pulse(m->ph[5], isub, 0x80000000u), sg) >> 1;
            m->ph[5] += isub;
        }
        lp = mm_lp(lp, x, tk);
        buf[i] = lp;
    }
    m->s[0] = lp;
    mm_out(t, v, buf, out, n);
}
static const char *const N_MMOCT[] = {"-2", "-1", "0", "+1", "+2"};
#define DM_MMSAW_DEF {"SWSAW", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DTUN", F_INT, 0, 127, 40), \
    PD("NUM", F_INT, 1, 5, 3), PD("SUB", F_PCT, 0, 127, 0), PD("FINE", F_INT, -64, 63, 0), PD("TONE", F_PCT, 0, 127, 127), \
    PD("CNTR", F_PCT, 0, 127, 100), {"OCT", F_INT, -2, 2, 0, 0, 0}}, mmsaw_trigger, mmsaw_render, 1, mm_glide, mm_gate, 1}

/* ---- SWPLS (SWAVE-PULS): a pulse with its width swept by a triangle (PWM), a second detuned pulse, a sub square.
 * TUNE PW PWMD PWMS, SUB FINE DTUN MIX2 */
static void mmpls_trigger(track_t *t, dvoice_t *v) { mm_start(t, v); }
static void mmpls_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL];
    uint32_t i, inc = mm_inc(mm_pitch(t, v, p[5] / 4)), inc2 = mm_detune(inc, clamp(p[6], 0, 127) * 12 + 7);
    uint32_t isub = inc >> 1, linc = LFO_INC[clamp(p[3], 0, 127)] / CTL;
    int32_t pwd = clamp(p[2], 0, 127) * 410, m2 = clamp(p[7], 0, 127) * 258, sg = clamp(p[4], 0, 127) * 258;
    int64_t pw0 = 2147483648LL + (int64_t)(clamp(p[1], 0, 127) - 64) * 30000000;
    for (i = 0; i < n; i++) {
        int64_t pw = pw0 + (int64_t)osc_tri(m->ph[4]) * pwd;
        uint32_t w;
        int32_t x;
        pw = pw < 130000000LL ? 130000000LL : pw > 4165000000LL ? 4165000000LL : pw;
        w = (uint32_t)pw;
        m->ph[4] += linc;
        x = osc_pulse(m->ph[0], inc, w);
        m->ph[0] += inc;
        if (m2) {
            x += mulq15(osc_pulse(m->ph[1], inc2, w), m2);
            m->ph[1] += inc2;
            x = mulq15(x, 23170);                        /* two pulses: -3 dB */
        }
        if (sg) {
            x += mulq15(osc_pulse(m->ph[5], isub, 0x80000000u), sg) >> 1;
            m->ph[5] += isub;
        }
        buf[i] = x;
    }
    mm_out(t, v, buf, out, n);
}
#define DM_MMPLS_DEF {"SWPLS", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("PW", F_INT, 0, 127, 64), \
    PD("PWMD", F_PCT, 0, 127, 40), PD("PWMS", F_LFOHZ, 0, 127, 40), PD("SUB", F_PCT, 0, 127, 0), \
    PD("FINE", F_INT, -64, 63, 0), PD("DTUN", F_INT, 0, 127, 20), PD("MIX2", F_PCT, 0, 127, 0)}, mmpls_trigger, \
    mmpls_render, 1, mm_glide, mm_gate, 1}

/* ---- SID (SID-6581): two oscillators as the chip's: naive waves (TRI SAW PULS NOIS, and TRI AND SAW as the chip's
 * combined wave), oscillator 2 an interval away; SYNC: 2 restarts 1; RING: 2's top bit flips 1's triangle; BITS: the
 * output's resolution. TUNE WAVE PW O2, O2LV SYNC RING BITS */
enum { SW_TRI, SW_SAW, SW_PULS, SW_NOIS, SW_TS };
static const char *const N_MMONOFF[] = {"OFF", "ON"};
static inline int32_t mmsid_wave(uint32_t w, uint32_t ph, uint32_t pw, int32_t *nz, mm_t *m, uint32_t ring)
{
    int32_t tri = (int32_t)((ph & 0x80000000u ? ~ph : ph) >> 15) - 32768;
    if (ring)
        tri = ~tri;                                      /* RING: oscillator 2's top bit turns the triangle over (the
                                                          * chip's XOR), within its range */
    switch (w) {
    case SW_TRI:
        return tri;
    case SW_SAW:
        return (int32_t)(ph >> 16) - 32768;
    case SW_PULS:
        return ph < pw ? 32767 : -32768;
    case SW_NOIS:
        if (((ph >> 27) ^ (uint32_t)nz[1]) & 1u) {      /* a new value 32 times a cycle (the LFSR's clock) */
            nz[1] = (int32_t)(ph >> 27);
            nz[0] = mm_noise(m);
        }
        return nz[0];
    default:                                             /* TRI AND SAW: the bits ANDed, as the chip's DAC sees them */
        return (int32_t)(((uint32_t)(tri + 32768) & (ph >> 16)) & 0xFFFFu) - 16384;
    }
}
static void mmsid_trigger(track_t *t, dvoice_t *v) { mm_start(t, v); }
static void mmsid_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL], pc = mm_pitch(t, v, 0), lv2 = clamp(p[4], 0, 127) * 258;
    uint32_t i, w = (uint32_t)clamp(p[1], 0, 4), inc = mm_inc(pc), inc2 = mm_inc(pc + clamp(p[3], -24, 24) * 16);
    uint32_t pw = 0x80000000u + (uint32_t)((clamp(p[2], 0, 127) - 64) * 31000000);
    uint32_t sync = p[5] != 0, ring = p[6] != 0, mask = ~((1u << (16u - (uint32_t)clamp(p[7], 4, 16))) - 1u);
    for (i = 0; i < n; i++) {
        uint32_t o2 = m->ph[1], n2 = o2 + inc2;
        int32_t x;
        m->ph[1] = n2;
        if (sync && n2 < o2)
            m->ph[0] = 0;                                /* hard sync: the chip's, aliasing and all */
        x = mmsid_wave(w, m->ph[0], pw, &m->s[0], m, ring && (n2 & 0x80000000u));
        m->ph[0] += inc;
        if (lv2)
            x = (x + mulq15(mmsid_wave(w, n2, pw, &m->s[2], m, 0), lv2)) >> 1;
        buf[i] = (int32_t)((uint32_t)(x + 32768) & mask) - 32768;
    }
    mm_out(t, v, buf, out, n);
}
static const char *const N_SIDW[] = {"TRI", "SAW", "PULS", "NOIS", "T&S"};
#define DM_MMSID_DEF {"SID", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PE("WAVE", N_SIDW, 2), PD("PW", F_INT, 0, 127, 48), \
    PD("O2", F_SEMI, -24, 24, 12), PD("O2LV", F_PCT, 0, 127, 0), PE("SYNC", N_MMONOFF, 0), PE("RING", N_MMONOFF, 0), \
    {"BITS", F_INT, 4, 16, 12, 0, 0}}, mmsid_trigger, mmsid_render, 1, mm_glide, mm_gate, 1}

/* ---- FM+ (FM+ STAT): a sine carrier phase-modulated by a sine with feedback; the index's own envelope (MDEC; 127
 * holds) by MENV. TUNE RATIO INDX FDBK, MDEC MENV FINE CRAT */
static const uint8_t MMFM_RATIO[16] = {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 176, 208, 0};   /* x 16 */
static const char *const N_MMFMR[] = {"0.5", "1", "1.5", "2", "2.5", "3", "3.5", "4", "5", "6", "7", "8", "9",
                                      "11", "13", "16"};
static const char *const N_MMFMC[] = {"0.5", "1", "2", "3"};
static void mmfm_trigger(track_t *t, dvoice_t *v)
{
    mm_start(t, v);
    v->ms.mm.s[0] = ENV1;                                /* the index's envelope */
    v->ms.mm.ph[0] = v->ms.mm.ph[1] = 0;                 /* FM from phase 0: the same attack every note */
}
static void mmfm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL], depth, menv = clamp(p[5], 0, 127) * 258, fb = clamp(p[3], 0, 127) * 400;
    uint32_t i, j, base = mm_inc(mm_pitch(t, v, 0)), ic, im, rt = (uint32_t)clamp(p[1], 0, 15);
    uint32_t kn = DECAY_K[clamp(p[4], 0, 126)];
    static const uint8_t CR[4] = {1, 2, 4, 6};          /* x 2 */
    ic = (uint32_t)(((uint64_t)base * CR[clamp(p[7], 0, 3)]) >> 1);
    im = mm_detune((uint32_t)(((uint64_t)base * (rt == 15u ? 256u : MMFM_RATIO[rt])) >> 4), clamp(p[6], -64, 63) * 20);
    depth = mulq15(clamp(p[2], 0, 127) * 258, 32767 - menv + mulq15(menv, env_q15(m->s[0])));   /* Q15 */
    if (p[4] < 127) {
        for (j = n; j > 1u; j >>= 1)
            kn = (uint32_t)(((uint64_t)kn * kn) >> 16);
        m->s[0] = mulq16(m->s[0], kn);
    }
    for (i = 0; i < n; i++) {
        int32_t md = sine_i(m->ph[1] + (uint32_t)((m->s[1] + m->s[2]) * fb));   /* feedback: the last two averaged */
        uint32_t off = (uint32_t)(((int64_t)md * depth) << 3);   /* depth 1.0: +-2 cycles (2^33) */
        m->s[2] = m->s[1];
        m->s[1] = md >> 1;
        m->ph[1] += im;
        buf[i] = sine_i(m->ph[0] + off);
        m->ph[0] += ic;
    }
    mm_out(t, v, buf, out, n);
}
#define DM_MMFM_DEF {"FM+", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PE("RATIO", N_MMFMR, 1), PD("INDX", F_PCT, 0, 127, 40), \
    PD("FDBK", F_PCT, 0, 127, 0), {"MDEC", F_INT, 0, 127, 60, 0, 0}, PD("MENV", F_PCT, 0, 127, 64), \
    PD("FINE", F_INT, -64, 63, 0), PE("CRAT", N_MMFMC, 1)}, mmfm_trigger, mmfm_render, 1, mm_glide, mm_gate, 1}

/* ---- DPRO (DPRO-WAVE): a wave scanned through 8 single cycles (SINE TRI SAW SQR PULS ORGAN VOX SYNC), the scan's
 * own triangle LFO, a sine wavefolder, a second oscillator detuned. TUNE POS SCAN SDEP, FOLD FINE DTUN MIX2 */
static inline int32_t mmdp_shape(uint32_t s, uint32_t ph, uint32_t inc)
{
    switch (s) {
    case 0:
        return sine_i(ph);
    case 1:
        return osc_tri(ph);
    case 2:
        return osc_saw(ph, inc);
    case 3:
        return osc_pulse(ph, inc, 0x80000000u);
    case 4:
        return osc_pulse(ph, inc, 0x40000000u);
    case 5:                                              /* drawbars 1, 2, 3 */
        return (sine_i(ph) * 18 + sine_i(ph * 2u) * 9 + sine_i(ph * 3u) * 6) >> 5;
    case 6:                                              /* a formant-ish ring: 1 x 3 */
        return mulq15(sine_i(ph), sine_i(ph * 3u));
    default: {                                           /* a hard-synced saw at 2.5 x, windowed */
        uint32_t q = (uint32_t)(((uint64_t)ph * 5u) >> 1);
        return mulq15((int32_t)(q >> 16) - 32768, 32767 - (int32_t)(ph >> 17));
    }
    }
}
static void mmdp_trigger(track_t *t, dvoice_t *v) { mm_start(t, v); }
static void mmdp_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL], pos, f, m2 = clamp(p[7], 0, 127) * 258, fold = clamp(p[4], 0, 127);
    uint32_t i, s, inc = mm_inc(mm_pitch(t, v, p[5] / 4)), inc2 = mm_detune(inc, clamp(p[6], 0, 127) * 12 + 5);
    m->ph[4] += LFO_INC[clamp(p[2], 0, 127)] * (n / CTL ? n / CTL : 1u);   /* the scan's LFO, per block */
    pos = clamp(p[1], 0, 127) * (7 * 256) / 127 + (osc_tri(m->ph[4]) * clamp(p[3], 0, 127) >> 11);
    pos = clamp(pos, 0, 7 * 256 - 1);
    s = (uint32_t)pos >> 8;
    f = pos & 255;
    for (i = 0; i < n; i++) {
        int32_t a = mmdp_shape(s, m->ph[0], inc), x = a + (((mmdp_shape(s + 1u, m->ph[0], inc) - a) * f) >> 8);
        m->ph[0] += inc;
        if (m2) {
            int32_t b = mmdp_shape(s, m->ph[1], inc2);
            x = (x + mulq15(b + (((mmdp_shape(s + 1u, m->ph[1], inc2) - b) * f) >> 8), m2)) * 3 >> 2;
            m->ph[1] += inc2;
        }
        if (fold)                                        /* sin(pi/2 x drive): x 1 .. x 9 */
            x = sine_i((uint32_t)((x * (256 + fold * 16)) >> 8) << 15);
        buf[i] = x;
    }
    mm_out(t, v, buf, out, n);
}
#define DM_MMDP_DEF {"DPRO", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("POS", F_INT, 0, 127, 36), \
    PD("SCAN", F_LFOHZ, 0, 127, 30), PD("SDEP", F_PCT, 0, 127, 0), PD("FOLD", F_PCT, 0, 127, 0), \
    PD("FINE", F_INT, -64, 63, 0), PD("DTUN", F_INT, 0, 127, 16), PD("MIX2", F_PCT, 0, 127, 0)}, mmdp_trigger, \
    mmdp_render, 1, mm_glide, mm_gate, 1}

/* ---- GND (GND-SIN / NOIS): a plain wave (SINE TRI NOIS, DIGI: noise held a cycle at a time, pitched), a pitch
 * envelope (PDEP up to 4 octaves, PDEC), noise mixed in through a low-pass (COLR). Kicks, toms, hats and zaps.
 * TUNE WAVE COLR PDEP, PDEC FINE NOIS */
static const char *const N_GNDW[] = {"SINE", "TRI", "NOIS", "DIGI"};
static void mmgnd_trigger(track_t *t, dvoice_t *v)
{
    mm_start(t, v);
    v->ms.mm.s[0] = ENV1;                                /* the pitch envelope */
    v->ms.mm.ph[0] = 0;
}
static void mmgnd_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL], lp = m->s[1], tk = mm_tone_k(p[2]), ng = clamp(p[6], 0, 127) * 258;
    int32_t pe = (int32_t)(((int64_t)clamp(p[3], 0, 127) * 768 / 127 * env_q15(m->s[0])) >> 15);
    uint32_t i, j, w = (uint32_t)clamp(p[1], 0, 3), inc = mm_inc(mm_pitch(t, v, p[5] / 4) + pe);
    uint32_t kn = DECAY_K[clamp(p[4], 0, 127)];
    for (j = n; j > 1u; j >>= 1)
        kn = (uint32_t)(((uint64_t)kn * kn) >> 16);
    m->s[0] = mulq16(m->s[0], kn);
    for (i = 0; i < n; i++) {
        uint32_t o = m->ph[0];
        int32_t x, nz = mm_noise(m);
        m->ph[0] = o + inc;
        if (w == 0u)
            x = sine_i(o);
        else if (w == 1u)
            x = osc_tri(o);
        else if (w == 2u)
            x = nz;
        else {
            if (m->ph[0] < o)
                m->s[2] = nz;                            /* DIGI: a new value each cycle */
            x = m->s[2];
        }
        if (w == 2u || ng) {
            lp = mm_lp(lp, w == 2u ? x : nz, tk);
            x = w == 2u ? lp : x + mulq15(lp, ng);
        }
        buf[i] = x;
    }
    m->s[1] = lp;
    mm_out(t, v, buf, out, n);
}
#define DM_MMGND_DEF {"GND", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PE("WAVE", N_GNDW, 0), PD("COLR", F_PCT, 0, 127, 100), \
    PD("PDEP", F_PCT, 0, 127, 0), PD("PDEC", F_INT, 0, 127, 30), PD("FINE", F_INT, -64, 63, 0), \
    PD("NOIS", F_PCT, 0, 127, 0), PD("-", F_INT, 0, 0, 0)}, mmgnd_trigger, mmgnd_render, 1, mm_glide, mm_gate, 1}

/* ---- VO-6: a saw or a pulse (and breath noise) through two formant band-passes, morphed through A E I O U (VOWL),
 * shifted (FORM), their width (RES); a 5.5 Hz vibrato. TUNE VOWL FORM BRTH, SRC RES FINE VIB */
static const int16_t MMVO_F1[5] = {16527, 14870, 11376, 15244, 11924};   /* 730 530 270 570 300 Hz, cutoff units */
static const int16_t MMVO_F2[5] = {18603, 21311, 22445, 17252, 17433};   /* 1090 1840 2290 840 870 Hz */
static const char *const N_VOSRC[] = {"SAW", "PULS"};
static void mmvo_trigger(track_t *t, dvoice_t *v) { mm_start(t, v); }
static void mmvo_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    mm_t *m = &v->ms.mm;
    const int16_t *p = &t->p[P_E0];
    int32_t buf[CTL], pos = clamp(p[1], 0, 127) * (4 * 256) / 127, k = pos >> 8, f = pos & 255, sh;
    int32_t f1, f2, br = clamp(p[3], 0, 127) * 200, vib, s0 = m->fs[0], s1 = m->fs[1], s2 = m->fs[2], s3 = m->fs[3];
    uint32_t i, inc, pul = p[4] != 0;
    if (k >= 4) {
        k = 3;
        f = 256;
    }
    m->ph[3] += HZ(5.5) * n;
    vib = (sine_i(m->ph[3]) * clamp(p[7], 0, 127) * 16 / 127) >> 15;
    inc = mm_inc(mm_pitch(t, v, p[6] / 4 + vib));
    sh = clamp(p[2], -64, 63) * CUT_ST / 4;              /* FORM: +-16 semitones */
    f1 = MMVO_F1[k] + (((MMVO_F1[k + 1] - MMVO_F1[k]) * f) >> 8) + sh;
    f2 = MMVO_F2[k] + (((MMVO_F2[k + 1] - MMVO_F2[k]) * f) >> 8) + sh;
    dsvf_coef(&m->f[0], f1, 64 + clamp(p[5], 0, 127) / 2);
    dsvf_coef(&m->f[1], f2, 64 + clamp(p[5], 0, 127) / 2);
    for (i = 0; i < n; i++) {
        int32_t x = pul ? osc_pulse(m->ph[0], inc, 0x50000000u) : osc_saw(m->ph[0], inc), b1, b2, h;
        m->ph[0] += inc;
        if (br)
            x += mulq15(mm_noise(m), br);
        x >>= 1;
        dsvf_tick(&m->f[0], x, &s0, &s1, &b1, &h);
        dsvf_tick(&m->f[1], x, &s2, &s3, &b2, &h);
        buf[i] = ((b1 * m->f[0].k) >> 11) + ((b2 * m->f[1].k * 5) >> 14);   /* each band-pass at unity, F2 -4 dB */
    }
    m->fs[0] = s0; m->fs[1] = s1; m->fs[2] = s2; m->fs[3] = s3;
    mm_out(t, v, buf, out, n);
}
#define DM_MMVO_DEF {"VO-6", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("VOWL", F_INT, 0, 127, 0), \
    PD("FORM", F_INT, -64, 63, 0), PD("BRTH", F_PCT, 0, 127, 10), PE("SRC", N_VOSRC, 0), PD("RES", F_INT, 0, 127, 64), \
    PD("FINE", F_INT, -64, 63, 0), PD("VIB", F_PCT, 0, 127, 20)}, mmvo_trigger, mmvo_render, 1, mm_glide, mm_gate, 1}
