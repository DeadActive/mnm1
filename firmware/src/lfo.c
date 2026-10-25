/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE; MNM-1: 2026 */
/* LFOs (MNM-1): three per track, each its own PAGE DEST TRIG WAVE / MULT SPD INTL DPTH.
 * PAGE and DEST pick the knob it moves: SYN AMP FLT EFX (the track's 8-knob pages, knob 1..8), LF1 LF2 LF3 (another LFO's
 * MULT SPD INTL DPTH, or its WAVE) or PTCH (the synth machine's pitch, eight ranges: a vibrato .. 16 octaves). Always on
 * the tempo: SPD x MULT, SPD 64 at MULT 2X one cycle a bar of 16 steps (a cycle = 2048 / (SPD x MULT) steps). TRIG:
 * FREE runs on; TRIG starts over at the track's notes; HOLD runs on, its output the value at the last note; ONE and
 * HALF start over at a note and stop after a cycle / half. INTL (the SidStation's interlace): the wave and silence in
 * turn, INTL times a cycle each. LFOs on one knob add up. The waves are functions of the phase (2^32 a cycle) but RND
 * (a new value a cycle); the mirrored ones the same upside down. Output Q15, -32767 .. 32767; a block's knobs written
 * in t->p (their set values kept in t->lsave, put back at the block's end: lfo_restore), in the order LFO 1, 2, 3, so
 * an LFO on another LFO's knob moves that one in the same block. */
static uint32_t beat_samples(void);               /* fx.c: a quarter note, the clock's when following */

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

static int32_t lfo_out(const track_t *t, uint32_t l) { return t->lfo[l < NLFO ? l : 0].out; }
