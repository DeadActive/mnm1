/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE; MNM-1: 2026 */
/* LFOs (MNM-1): three per track, each its own PAGE DEST TRIG WAVE / MULT SPD INTL DPTH.
 * PAGE and DEST pick the knob it moves: SYN AMP FLT EFX (the track's 8-knob pages, knob 1..8), LF1 LF2 LF3 (another LFO's
 * MULT SPD INTL DPTH, or its WAVE) or PTCH (the synth machine's pitch, eight ranges: a vibrato .. 16 octaves). Always on
 * the tempo: SPD x MULT, SPD 64 at MULT 2X one cycle a bar of 16 steps (a cycle = 2048 / (SPD x MULT) steps). TRIG:
 * FREE runs on; TRIG starts over at the track's notes; HOLD runs on, its output the value at the last note; ONE and
 * HALF start over at a note and stop after a cycle / half. INTL (interlaced phase): the wave and silence in
 * turn, INTL times a cycle each. LFOs on one knob add up. The waves are functions of the phase (2^32 a cycle) but RND
 * (a new value a cycle); the mirrored ones the same upside down. Output Q15, -32767 .. 32767; a block's knobs written
 * in t->p (their set values kept in t->lsave, put back at the block's end: lfo_restore), in the order LFO 1, 2, 3, so
 * an LFO on another LFO's knob moves that one in the same block. */
static uint32_t beat_samples(void);               /* fx.c: a quarter note, the clock's when following */

/* Calibrated LFOs retain raw controls and fractional Q8 modulation. The legacy
 * CTL cadence remains unchanged; each calibrated slice is exactly 16 samples. */
#define MML_SIN mm2_sinp
#include "mm_lfo.h"
#undef MML_SIN
static mml_state mmen_lfo_state;
static uint32_t mmen_lfo_rng = 0x2000u;
static uint8_t mmen_lfo_used;
static void mmen_lfo_reset(void)
{
    mmen_lfo_used = 0;
    mml_init(&mmen_lfo_state);
    mmen_lfo_rng = 0x2000u;
    memset(mmen_delta, 0, sizeof mmen_delta);
}
/* Cross modulation into a legacy track is deliberately outside the calibrated
 * model family. Remove it also before evaluating controls, so it cannot enable
 * a disabled legacy track's synthetic source LFO in the following block. */
static void mmen_lfo_filter(void)
{
    unsigned out = 0;
    for (unsigned i = 0; i < mmen_lfo_state.count; ++i) {
        mml_mod m = mmen_lfo_state.mod[i];
        if (mmen_is_model(trk[m.track].model)) mmen_lfo_state.mod[out++] = m;
    }
    mmen_lfo_state.count = (uint8_t)out;
}
static void mmen_lfo_prepare(uint32_t n)
{
    unsigned calibrated = 0;
    for (unsigned t = 0; t < NTRK; ++t)
        if (mmen_is_model(trk[t].model)) calibrated |= 1u << t;
    if (!calibrated) {
        /* Legacy-only playback pays six model checks, no buffers, tempo division
         * or source LFO work. Clear departed calibrated state exactly once. */
        if (mmen_lfo_used) mmen_lfo_reset();
        return;
    }
    mmen_lfo_used = 1;
    uint8_t raw[NTRK][3][8];
    unsigned slices = (n + 15u) / 16u;
    if (slices > (CTL + 15u) / 16u) slices = (CTL + 15u) / 16u;
    memset(raw, 0, sizeof raw);
    for (unsigned t = 0; t < NTRK; ++t)
        if (calibrated & (1u << t))
            for (unsigned l = 0; l < 3; ++l)
                for (unsigned p = 0; p < 8; ++p)
                    raw[t][l][p] = mm_control_raw(trk[t].p[MM_CONTROL_ID[42 + l * 8 + p]]);
    uint32_t quarter = beat_samples();
    uint32_t tempo10 = quarter ? (uint32_t)FS * 600u / quarter : (uint32_t)song.g[G_BPM] * 10u;
    mmen_lfo_filter();
    memset(mmen_delta, 0, sizeof mmen_delta);
    for (unsigned b = 0; b < slices; ++b) {
        mml_block(&mmen_lfo_state, raw, tempo10, &mmen_lfo_rng, (int)song.sel);
        mmen_lfo_filter();
        for (unsigned i = 0; i < mmen_lfo_state.count; ++i) {
            const mml_mod *m = &mmen_lfo_state.mod[i];
            mmen_delta[b][m->track][m->param] = m->value;
        }
    }
}


/* the knobs of the pages an LFO's PAGE names (knob 1..8 = DEST 0..7); PTCH has none (lfo_pitch) */
static const uint8_t LFO_PAGE_ID[LP_PTCH][8] = {
    {P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7},
    {P_AATK, P_AHOLD, P_ADEC, P_AREL, P_DIST, P_LEVEL, P_PAN, P_PORT},
    {P_FCUT, P_FWDTH, P_FRESO, P_FLPQ, P_FATK, P_FDEC, P_FENV, P_FWOFS},
    {P_EQF, P_EQG, P_SRR, P_DTIM, P_DLY, P_DFB, P_DBAS, P_DWID},
    {P_LFO1 + LF_PAGE, P_LFO1 + LF_DEST, P_LFO1 + LF_TRIG, P_LFO1 + LF_WAVE, P_LFO1 + LF_MULT, P_LFO1 + LF_SPD,
     P_LFO1 + LF_INTL, P_LFO1 + LF_DPTH},
    {P_LFO2 + LF_PAGE, P_LFO2 + LF_DEST, P_LFO2 + LF_TRIG, P_LFO2 + LF_WAVE, P_LFO2 + LF_MULT, P_LFO2 + LF_SPD,
     P_LFO2 + LF_INTL, P_LFO2 + LF_DPTH},
    {P_LFO3 + LF_PAGE, P_LFO3 + LF_DEST, P_LFO3 + LF_TRIG, P_LFO3 + LF_WAVE, P_LFO3 + LF_MULT, P_LFO3 + LF_SPD,
     P_LFO3 + LF_INTL, P_LFO3 + LF_DPTH},
};
/* PTCH's DESTs: the swing at DPTH 127, 1/16 semitone (params.c N_LPTCH: VIB 1/2 SEMI 2ST 5TH OCT 4OCT 16OC) */
static const uint16_t LFO_PTCH16[8] = {4, 8, 16, 32, 112, 192, 768, 3072};

static uint32_t lfo_base(uint32_t l) { return l == 0u ? (uint32_t)P_LFO1 : l == 1u ? (uint32_t)P_LFO2 : (uint32_t)P_LFO3; }
/* an LFO's own knob (a change may turn modulation on / off: midi_learn.c ml_set) */
static int lfo_own(uint32_t id) { return (id >= P_LFO1 && id < P_LFO2 + LF_N) || (id >= P_LFO3 && id <= P_LFO3_END); }

/* the knob LFO l of t moves (0xFF: none, PTCH, or one an LFO may not move: another LFO's PAGE DEST TRIG, the
 * engine's "-" knobs) */
static uint32_t lfo_dest_param(const track_t *t, uint32_t l)
{
    const int16_t *q = &t->p[lfo_base(l)];
    uint32_t pg = (uint32_t)clamp(q[LF_PAGE], 0, LP_N - 1), id;
    const param_desc_t *d;
    if (pg == LP_PTCH)
        return 0xFFu;
    id = LFO_PAGE_ID[pg][clamp(q[LF_DEST], 0, 7)];
    if (pg >= LP_LF1 && (id - lfo_base(pg - LP_LF1)) < LF_WAVE)
        return 0xFFu;
    d = track_desc(t, id);
    return !d->label || d->label[0] == '-' || d->max <= d->min ? 0xFFu : id;
}

/* the phase's step a sample: SPD x MULT, 2048 / (SPD x MULT) steps of a 16th a cycle */
static uint32_t lfo_inc(const int16_t *q)
{
    uint32_t r = (uint32_t)clamp(q[LF_SPD], 0, 127) << clamp(q[LF_MULT], 0, 6);
    return (uint32_t)(((uint64_t)r << 32) / (512u * beat_samples()));
}

/* the deterministic waves at phase ph (RND: 0, lfo_track draws it) */
static int32_t lfo_shape(uint32_t wave, uint32_t ph)
{
    int32_t x = (int32_t)(ph >> 16), y;               /* 0 .. 65535 */
    uint32_t w = wave >= LW_ITRI ? wave - LW_ITRI : wave;
    switch (w) {
    case LW_TRI:                                      /* up from 0, the top at 1/4, down, up again to 0 */
        x = (x + 16384) & 65535;
        y = x < 32768 ? x * 2 - 32767 : 32767 - (x - 32768) * 2;
        break;
    case LW_SINE:
        y = sine_i(ph);
        break;
    case LW_SQR:
        y = x < 32768 ? 32767 : -32767;
        break;
    case LW_SAW:                                      /* down; mirrored: up (a ramp) */
        y = 32767 - x;
        break;
    case LW_EXP: {                                    /* a fall, fast then slow */
        int32_t r = (65535 - x) >> 1, r2 = r * r >> 15;
        y = 2 * (r2 * r2 >> 15) - 32767;
        break;
    }
    default:
        return 0;
    }
    y = clamp(y, -32767, 32767);
    return wave >= LW_ITRI ? -y : y;
}

/* ------------------------------------------------------- runtime --- */
/* inside mix_block only: lfo_apply ... lfo_restore (song.lfo_in), a hit in between (drum_hit) re-applies its track */

static int32_t lfo_rnd(track_t *t)                    /* -32767 .. 32767 */
{
    t->lrng = t->lrng * 1664525u + 1013904223u;
    return clamp((int32_t)(t->lrng >> 16) - 32768, -32767, 32767);
}

/* LFO l of t: its phase n samples on (TRIG ONE / HALF: up to their end), its output */
static void lfo_run(track_t *t, uint32_t l, uint32_t n)
{
    const int16_t *q = &t->p[lfo_base(l)];
    lfo_state_t *st = &t->lfo[l];
    uint32_t tm = (uint32_t)clamp(q[LF_TRIG], 0, LT_HALF), wave = (uint32_t)clamp(q[LF_WAVE], 0, LW_COUNT - 1u);
    uint32_t old = st->ph, intl = (uint32_t)clamp(q[LF_INTL], 0, 127);
    int32_t v;
    if (!st->stop && n) {
        uint32_t end = tm == LT_HALF ? 0x80000000u : 0u;
        st->ph = old + lfo_inc(q) * n;
        if (tm == LT_ONE && st->ph < old) {           /* the cycle done: held at its end */
            st->ph = 0xFFFFFFFFu;
            st->stop = 1;
        } else if (tm == LT_HALF && old < end && (st->ph >= end || st->ph < old)) {
            st->ph = end - 1u;
            st->stop = 1;
        }
    }
    if (wave == LW_RND) {
        if (st->fresh || (n && st->ph < old))
            st->rnd = lfo_rnd(t);
        v = st->rnd;
    } else {
        v = lfo_shape(wave, st->ph);
    }
    if (intl && (((st->ph >> 16) * intl >> 15) & 1u))   /* INTL: the second of each pair silent */
        v = 0;
    st->run = v;
    if (tm != LT_HOLD || st->fresh)
        st->out = v;                                  /* HOLD: the value at the last note (lfo_hit) */
    st->fresh = 0;
}

/* the knob pid's place in this block's written knobs (saved the first time) */
static uint32_t lfo_slot(track_t *t, uint32_t pid)
{
    uint32_t k;
    for (k = 0; k < t->lnum && t->lpid[k] != pid; k++)
        ;
    if (k == t->lnum && k < 4u) {
        t->lpid[k] = (uint8_t)pid;
        t->lsave[k] = t->p[pid];
        t->lval[k] = 0;                               /* (the sum of the LFOs on it, until the end) */
        t->lnum++;
    }
    return k;
}

/* track t: its LFOs n samples on (0: none), the knobs they move written. An LFO at DPTH 0 is not run (its phase
 * waits) unless HOLD (its value at the next note) */
static void lfo_track(track_t *t, uint32_t n)
{
    if (mmen_is_model(t->model)) { t->lpit = 0; t->lon = 0; return; }
    uint32_t l, on = 0;
    int32_t pit = 0, sum[4] = {0, 0, 0, 0};
    for (l = 0; l < NLFO; l++) {
        const int16_t *q = &t->p[lfo_base(l)];
        int32_t dp = clamp(q[LF_DPTH], 0, 127), m;
        uint32_t pid, k;
        if (!dp)
            continue;
        on |= 1u << l;
        lfo_run(t, l, n);
        m = t->lfo[l].out * dp / 127;
        if (clamp(q[LF_PAGE], 0, LP_N - 1) == LP_PTCH) {
            pit += m * LFO_PTCH16[clamp(q[LF_DEST], 0, 7)] / 32767;
            continue;
        }
        pid = lfo_dest_param(t, l);
        if (pid == 0xFFu)
            continue;
        k = lfo_slot(t, pid);
        if (k < 4u) {
            const param_desc_t *d = track_desc(t, pid);
            sum[k] += m * (d->max - d->min) / 32767;
            t->p[pid] = (int16_t)clamp(t->lsave[k] + sum[k], d->min, d->max);
            t->lval[k] = t->p[pid];
        }
    }
    t->lpit = (int16_t)clamp(pit, -32767, 32767);
    t->lon = (uint8_t)on;
}

static void lfo_apply(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        lfo_track(&trk[i], n);
    song.lfo_in = 1;
}

static void lfo_restore_track(track_t *t)
{
    uint32_t k;
    for (k = t->lnum; k-- > 0u;)                      /* (last first: an LFO's knob moved by another one) */
        t->p[t->lpid[k]] = t->lsave[k];
    t->lnum = 0;
    t->lpit = 0;
}

static void lfo_restore(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        lfo_restore_track(&trk[i]);
    song.lfo_in = 0;
}

/* a note of t: TRIG ONE HALF start over, HOLD takes its value; inside the block the knobs are written again from there
 * (the trigger reads them next) */
static void lfo_hit(track_t *t)
{
    if (mmen_is_model(t->model)) {
        unsigned index = mmen_index(t);
        if (index < NTRK) {
            mmen_lfo_used = 1;
            uint8_t sound[71];
            mm_controls_get(t, sound);
            mml_trigger(&mmen_lfo_state, index, (const uint8_t (*)[8])(sound + 42));
        }
        return; /* graph/audition scratch tracks never advance the song LFOs */
    }
    uint32_t l, any = 0;
    for (l = 0; l < NLFO; l++) {
        uint32_t tm = (uint32_t)clamp(t->p[lfo_base(l) + LF_TRIG], 0, LT_HALF);
        if (tm == LT_FREE)
            continue;
        if (tm != LT_HOLD) {
            t->lfo[l].ph = 0;
            t->lfo[l].stop = 0;
        }
        t->lfo[l].fresh = 1;
        any = 1;
    }
    if (any && song.lfo_in) {
        lfo_restore_track(t);
        lfo_track(t, 0);
    }
}

/* PLAY: the LFOs but FREE start over, the random sequence too */
static void lfo_start(void)
{
    uint32_t i, l;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (mmen_is_model(t->model)) { lfo_hit(t); continue; }
        t->lrng = 0x2545F491u * (i + 1u);
        for (l = 0; l < NLFO; l++)
            if (t->p[lfo_base(l) + LF_TRIG] != LT_FREE) {
                memset(&t->lfo[l], 0, sizeof t->lfo[l]);
                t->lfo[l].fresh = 1;
            }
        if (song.lfo_in) {
            lfo_restore_track(t);
            lfo_track(t, 0);
        }
    }
}

/* the UI: the knob pid of t is moved now, to *val (its set value + every LFO on it, at their last block) */
static int lfo_live(const track_t *t, uint32_t pid, int32_t *val)
{
    if (mmen_is_model(t->model)) {
        unsigned index = mmen_index(t);
        if (index >= NTRK) return 0;
        for (unsigned p = 0; p < 71; ++p) if (MM_CONTROL_ID[p] == pid) {
            int32_t delta = mml_delta(&mmen_lfo_state, index, p);
            if (!delta) return 0;
            int32_t raw = ((int32_t)t->p[pid] + mm_control_bias(p)) * 256 + delta;
            *val = (clamp(raw, 0, 127 * 256) >> 8) - mm_control_bias(p);
            return 1;
        }
        return 0;
    }
    uint32_t l, hit = 0;
    int32_t sum = 0;
    const param_desc_t *d = track_desc(t, pid);
    for (l = 0; l < NLFO; l++) {
        int32_t dp = clamp(t->p[lfo_base(l) + LF_DPTH], 0, 127);
        if (dp && lfo_dest_param(t, l) == pid) {
            hit = 1;
            sum += t->lfo[l].out * dp / 127 * (d->max - d->min) / 32767;
        }
    }
    if (!hit)
        return 0;
    *val = clamp(t->p[pid] + sum, d->min, d->max);
    return 1;
}

static int32_t lfo_out(const track_t *t, uint32_t l)
{
    if (mmen_is_model(t->model)) {
        unsigned index = mmen_index(t);
        return index < NTRK ? mmen_lfo_state.voice[index][l < NLFO ? l : 0].value : 0;
    }
    return t->lfo[l < NLFO ? l : 0].out;
}
