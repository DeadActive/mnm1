/* FILTER (filter.c), MNM-1's: a high-pass at BASE into a low-pass at BASE + WDTH, HPQ and LPQ, the
 * note followed (BASE 0: two octaves under it, 8 an octave), the AD envelope on BASE (BOFS) and WDTH (WOFS); the
 * tail, the extremes; the EQ (EFFECTS). Each test prints one line; any failure exits 1. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wl[SECS(4)], wr[SECS(4)];

/* a drum machine's note is C2 (65.4 Hz): BASE b puts the high-pass at 16.35 Hz x 2^(b / 8) */
static double hp_hz(int base) { return 65.406 / 4 * pow(2, base / 8.0); }

static void flt_set(track_t *t, int base, int wdth, int hpq, int lpq, int bofs, int wofs, int dec)
{
    t->p[P_FCUT] = (int16_t)base;
    t->p[P_FWDTH] = (int16_t)wdth;
    t->p[P_FRESO] = (int16_t)hpq;
    t->p[P_FLPQ] = (int16_t)lpq;
    t->p[P_FENV] = (int16_t)bofs;
    t->p[P_FWOFS] = (int16_t)wofs;
    t->p[P_FDEC] = (int16_t)dec;
}

/* the gain (dB) of a steady sine of f Hz, amplitude a, through track 0's filter (eq: its EQ) after 0.2 s to settle */
static double tone_db(int base, int wdth, int hpq, int lpq, double f, int32_t a, int eq)   /* (eq: unused) */
{
    static int32_t b[CTL];
    uint32_t blk, i, ph = 0;
    double in2 = 0, out2 = 0;
    track_t *t = &trk[0];
    host_init();
    flt_set(t, base, wdth, hpq, lpq, 0, 0, 40);
    for (blk = 0; blk < SECS(0.4) / CTL; blk++) {
        for (i = 0; i < CTL; i++, ph++)
            b[i] = (int32_t)(a * sin(2 * M_PI * f * ph / FS));
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                in2 += (double)b[i] * b[i];
        (void)eq;
        trk_filter(t, b, CTL);
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                out2 += (double)b[i] * b[i];
    }
    return 10 * log10((out2 + 1e-9) / in2);
}

/* the stages BASE / WDTH put in on a drum (0: none) */
static uint32_t flt_open_test(int base, int wdth)
{
    int32_t hc, lc;
    host_init();
    flt_set(&trk[0], base, wdth, 0, 0, 0, 0, 40);
    flt_cuts_at(&trk[0], 0, &hc, &lc);
    return hc > 0 || lc < FLT_TOP;
}

/* the EQ's gain (dB) at f Hz at EQG g (EQF 64) */
static double tone_db_eq(double f, int32_t a, int g)
{
    static int32_t b[CTL];
    uint32_t blk, i, ph = 0;
    double in2 = 0, out2 = 0;
    host_init();
    trk[0].p[P_EQF] = 64;
    trk[0].p[P_EQG] = (int16_t)g;
    for (blk = 0; blk < SECS(0.4) / CTL; blk++) {
        for (i = 0; i < CTL; i++, ph++)
            b[i] = (int32_t)(a * sin(2 * M_PI * f * ph / FS));
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                in2 += (double)b[i] * b[i];
        track_eq(&trk[0], b, CTL);
        if (blk >= SECS(0.2) / CTL)
            for (i = 0; i < CTL; i++)
                out2 += (double)b[i] * b[i];
    }
    return 10 * log10((out2 + 1e-9) / in2);
}

/* a hit at EQG 64 sounds the same whatever EQF is (the EQ off) */
static int eq_off_identical(void)
{
    static int32_t a[SECS(0.3)];
    uint32_t i, same = 1;
    host_init();
    old_set_model(&trk[0], DM_RIM);
    drum_hit(&trk[0], 127);
    render_mix(a, wr, SECS(0.3));
    host_init();
    old_set_model(&trk[0], DM_RIM);
    trk[0].p[P_EQF] = 10;
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.3));
    for (i = 0; i < SECS(0.3); i++)
        same &= wl[i] == a[i];
    return (int)same;
}

static void test_response(void)
{
    double a = 20000, lp = hp_hz(48), fh = hp_hz(48);   /* ~1 kHz */
    check("LP (BASE 0, WDTH 48: ~1 kHz): 250 Hz passes, 4 kHz >= 18 dB down",
          tone_db(0, 48, 0, 0, 250, a, 0) > -1.5 && tone_db(0, 48, 0, 0, 4000, a, 0) < -18);
    check("HP (BASE 48: ~1 kHz, WDTH 127): 4 kHz passes, 250 Hz >= 18 dB down",
          tone_db(48, 127, 0, 0, 4000, a, 0) > -1.5 && tone_db(48, 127, 0, 0, 250, a, 0) < -18);
    check("BASE 40 WDTH 16: a band between (10 dB over 2 octaves either side of its middle)",
          tone_db(40, 16, 0, 0, hp_hz(48), a, 0) > tone_db(40, 16, 0, 0, hp_hz(48) / 4, a, 0) + 10 &&
              tone_db(40, 16, 0, 0, hp_hz(48), a, 0) > tone_db(40, 16, 0, 0, hp_hz(48) * 4, a, 0) + 10);
    check("LPQ 127 raises the low-pass's gain at its cutoff by >= 12 dB; HPQ leaves it",
          tone_db(0, 48, 0, 127, lp, a, 0) > tone_db(0, 48, 0, 0, lp, a, 0) + 12 &&
              fabs(tone_db(0, 48, 127, 0, lp, a, 0) - tone_db(0, 48, 0, 0, lp, a, 0)) < 1);
    check("HPQ 127 raises the high-pass's gain at its cutoff by >= 12 dB",
          tone_db(48, 127, 127, 0, fh, a, 0) > tone_db(48, 127, 0, 0, fh, a, 0) + 12);
    check("BASE 0 WDTH 127 (the defaults): a drum's filter wide open, no stage in (bit for bit)",
          !flt_open_test(0, 127));
}

/* the note followed: a synth machine's pitch two octaves up moves both cutoffs two octaves (16 of BASE); BASE +8 one
 * octave; WDTH the low-pass alone */
static void test_keytrack(void)
{
    int32_t h0, l0, h1, l1, h2, l2;
    host_init();
    old_set_model(&trk[0], DM_MMSAW);
    trk[0].p[P_E0] = 0;
    flt_set(&trk[0], 20, 30, 0, 0, 0, 0, 40);
    trk[0].mm_p16 = 48 * 16;
    flt_cuts_at(&trk[0], 0, &h0, &l0);
    trk[0].mm_p16 = 72 * 16;
    flt_cuts_at(&trk[0], 0, &h1, &l1);
    check("keytrack: two octaves up the note, both cutoffs two octaves up", h1 - h0 == 2 * FLT_OCT && l1 - l0 == 2 * FLT_OCT);
    trk[0].p[P_FCUT] = 28;
    flt_cuts_at(&trk[0], 0, &h2, &l2);
    check("BASE +8: one octave up, the low-pass with it", h2 - h1 == FLT_OCT && l2 - l1 == FLT_OCT);
    trk[0].p[P_FWDTH] = 38;
    flt_cuts_at(&trk[0], 0, &h1, &l1);
    check("WDTH +8: the low-pass alone one octave up", h1 == h2 && l1 - l2 == FLT_OCT);
    trk[0].p[P_FCUT] = 0;
    trk[0].mm_p16 = 60 * 16;
    flt_cuts_at(&trk[0], 0, &h1, &l1);
    check("BASE 0 under C4: the high-pass at C2 (65 Hz, the table's units)",
          abs(h1 - (int32_t)(256 * 127 * log2(65.406 / 30) / log2(16000.0 / 30))) < 32);
}

/* every BASE x WDTH x Q extreme (both Qs), full-level noise and a full-level sine at the high-pass: bounded output,
 * the states never near their clamp (headroom) */
static void test_extremes(void)
{
    static const int V[3] = {0, 64, 127};
    static int32_t b[CTL];
    uint32_t ok = 1, c, w, r, blk, i, rng = 1;
    int32_t smax = 0;
    for (c = 0; c < 3u; c++)
        for (w = 0; w < 3u; w++)
            for (r = 0; r < 3u; r++) {
                track_t *t = &trk[0];
                double fc = hp_hz(V[c]);
                host_init();
                flt_set(t, V[c], V[w], V[r], V[r], 0, 0, 40);
                for (blk = 0; blk < 400u; blk++) {
                    for (i = 0; i < CTL; i++) {
                        rng = rng * 1664525u + 1013904223u;
                        b[i] = blk < 200u ? (int32_t)(rng >> 13) - 262144 - 262144 + 1
                                          : (int32_t)(524287 * sin(2 * M_PI * fc * (blk * CTL + i) / FS));
                    }
                    trk_filter(t, b, CTL);
                    for (i = 0; i < CTL; i++)
                        ok &= b[i] >= -524287 && b[i] <= 524287;
                    for (i = 0; i < 4u; i++) {               /* (two resonant stages in a row: the second's
                                                              * states reach further) */
                        ok &= abs(t->fs[i]) < FLT_SMAX;
                        smax = abs(t->fs[i]) > smax ? abs(t->fs[i]) : smax;
                    }
                }
            }
    printf("     extremes: the largest state %d (clamp %d)\n", smax, FLT_SMAX);
    check("extremes: every BASE x WDTH x Q bounded, the states inside their clamp", ok);
}

/* a track with a sustained noise source: HNOIS at DECAY 127, hit once */
static double hf_energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double e = 0;
    for (; a + 1 < b; a++)
        e += (double)(x[a + 1] - x[a]) * (x[a + 1] - x[a]);
    return e;
}

static void hit_render(int bofs, int wofs, int dec, uint32_t vel, uint32_t frames)
{
    host_init();
    old_set_model(&trk[0], DM_HNOIS);
    trk[0].p[P_E1] = 127;
    flt_set(&trk[0], bofs || wofs < 0 ? 20 : 0, bofs || wofs < 0 ? 40 : 30, 0, 20, bofs, wofs, dec);
    drum_hit(&trk[0], vel);
    render_mix(wl, wr, frames);
}

/* blocks until the envelope is below 1 %, the filter fed a steady signal (the envelope runs while it does) */
static uint32_t env_blocks(int dec)
{
    static int32_t b[CTL];
    uint32_t n = 0, i;
    host_init();
    flt_set(&trk[0], 20, 40, 0, 20, 40, 0, dec);
    trk_filter_hit(&trk[0], 127);
    while (trk[0].fenv > (1 << 24) / 100 && n < 100000u) {
        for (i = 0; i < CTL; i++)
            b[i] = (i & 1u) ? 1000 : -1000;
        trk_filter(&trk[0], b, CTL);
        n++;
    }
    return n;
}

static void test_envelope(void)
{
    double e0, e1, a, p;
    uint32_t n0, n1;
    hit_render(0, 40, 40, 127, SECS(0.6));
    e0 = hf_energy(wl, 0, SECS(0.01));
    e1 = hf_energy(wl, SECS(0.4), SECS(0.41));
    check("WOFS +40: brighter at the hit than after DEC", e0 > 3 * e1);
    hit_render(0, -40, 40, 127, SECS(0.6));
    e0 = hf_energy(wl, 0, SECS(0.01));
    e1 = hf_energy(wl, SECS(0.4), SECS(0.41));
    check("WOFS -40: darker at the hit than after DEC", e0 * 3 < e1);
    hit_render(30, 0, 40, 127, SECS(0.6));
    e0 = hf_energy(wl, 0, SECS(0.01));
    e1 = hf_energy(wl, SECS(0.4), SECS(0.41));
    check("BOFS +30: the band up at the hit (brighter than after DEC)", e0 > 3 * e1);
    hit_render(0, 30, 60, 127, SECS(0.02));         /* (BASE 0 WDTH 30: the low-pass well under the top) */
    a = hf_energy(wl, 0, SECS(0.01));
    hit_render(0, 30, 60, 96, SECS(0.02));
    p = hf_energy(wl, 0, SECS(0.01)) * (127.0 * 127) / (96.0 * 96);   /* the plain hit's level taken out */
    check("an accent sweeps further than a plain hit", a > 1.3 * p);
    n0 = env_blocks(0);                              /* the filter driven directly: a voice may end first */
    n1 = env_blocks(127);
    check("DEC: 0 vs 127 to 1 % are ~400x apart (250..600)", n0 && n1 > 250u * n0 && n1 < 600u * n0);
}

/* a resonant low-pass rings after the hit's voice has ended, then the track is silent and idle */
static void test_tail(void)
{
    uint32_t rang = 0, blk;
    host_init();
    old_set_model(&trk[0], DM_RIM);
    flt_set(&trk[0], 0, 60, 0, 127, 0, 0, 40);
    drum_hit(&trk[0], 127);
    for (blk = 0; blk < SECS(3) / CTL; blk++) {
        render_mix(wl + blk * CTL, wr + blk * CTL, CTL);
        rang |= !trk[0].v[0].active && !trk[0].dtail && trk[0].fring;
    }
    check("tail: the filter rings on after the voice, then the track is silent and idle",
          rang && !trk[0].fring && !trk[0].v[0].active && wl[SECS(3) - 1] == 0 && wl[SECS(2.5)] == 0);
}

/* a stage taken out while ringing (WDTH to 127: the low-pass out) and back: bounded; out, its state cleared; both
 * out, the filter idle */
static void test_stage_switch(void)
{
    uint32_t blk, ok = 1;
    host_init();
    old_set_model(&trk[0], DM_RIM);
    flt_set(&trk[0], 30, 30, 127, 127, 0, 0, 40);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.05));
    trk[0].p[P_FWDTH] = 127;
    for (blk = 0; blk < 20u; blk++) {
        render_mix(wl, wr, CTL);
        ok &= abs(wl[0]) <= 32767;
    }
    ok &= trk[0].fs[2] == 0 && trk[0].fs[3] == 0;
    trk[0].p[P_FCUT] = 0;                            /* both out */
    render_mix(wl, wr, CTL);
    ok &= trk[0].fs[0] == 0 && trk[0].fs[1] == 0 && !trk[0].fring;
    trk[0].p[P_FWDTH] = 30;
    for (blk = 0; blk < 20u; blk++) {
        render_mix(wl, wr, CTL);
        ok &= abs(wl[0]) <= 32767;
    }
    check("a stage out while ringing and back: bounded; out, its state cleared; both out, idle", ok);
}

/* a choke cuts the track while its filter rings: the ring ends, the track goes idle */
static void test_choke_ring_ends(void)
{
    host_init();
    old_set_model(&trk[0], DM_RIM);
    old_set_model(&trk[1], DM_RIM);
    trk[0].p[P_CHOKE] = trk[1].p[P_CHOKE] = 2;
    flt_set(&trk[0], 40, 10, 127, 127, 0, 0, 40);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.02));
    drum_hit(&trk[1], 127);
    render_mix(wl, wr, SECS(3));
    check("a choke while the filter rings: the ring ends, the track idle", !trk[0].fring && !trk[0].v[0].active);
}

/* every track a loud kick through a resonant high-pass swept by its envelope: the mix bounded */
static void test_all_resonant(void)
{
    uint32_t i, ok = 1;
    host_init();
    for (i = 0; i < NTRK; i++) {
        old_set_model(&trk[i], DM_K909);
        trk[i].p[P_LEVEL] = 127;
        flt_set(&trk[i], 30, 127, 127, 0, 63, 0, 60);
        drum_hit(&trk[i], 127);
    }
    render_mix(wl, wr, SECS(1));
    for (i = 0; i < SECS(1); i++)
        ok &= abs(wl[i]) <= 32767 && abs(wr[i]) <= 32767;
    check("every track a loud kick through HP HPQ 127, BOFS +63: the mix bounded", ok);
}

static void test_decay_display(void)
{
    char v[8];
    const char *u0, *u1;
    param_format(&TP[P_FDEC], 0, v, &u0);
    param_format(&TP[P_FDEC], 127, v, &u1);
    check("DEC shows ms at 0, s at 127", str_eq(u0, "ms") && str_eq(u1, "s"));
}

/* LFO 1 on BASE (PAGE FLT, DEST 1), a square: the noise's brightness alternates */
static void test_lfo_base(void)
{
    double lo = 1e300, hi = 0;
    uint32_t w, k;
    host_init();
    old_set_model(&trk[0], DM_HNOIS);
    trk[0].p[P_E1] = 127;
    flt_set(&trk[0], 30, 30, 0, 20, 0, 0, 40);
    trk[0].p[P_LFO1 + LF_PAGE] = LP_FLT;
    trk[0].p[P_LFO1 + LF_DEST] = 0;
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SQR;
    trk[0].p[P_LFO1 + LF_SPD] = 16;                  /* MULT 1X: a cycle 128 steps, a half well over 0.1 s */
    trk[0].p[P_LFO1 + LF_MULT] = 0;
    trk[0].p[P_LFO1 + LF_DPTH] = 40;
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(4));
    for (w = 0; w < 30u; w++) {                      /* 30 windows of 0.1 s: the LFO's two levels */
        double e = 0;
        for (k = SECS(0.1) * w; k + 1 < SECS(0.1) * (w + 1); k++)
            e += (double)(wl[k + 1] - wl[k]) * (wl[k + 1] - wl[k]);
        lo = e < lo ? e : lo;
        hi = e > hi ? e : hi;
    }
    check("LFO on FLT BASE: the cutoffs move (brightness 3x between the LFO's levels)", hi > 3 * lo);
}

/* BASE 127 + BOFS +63 + an LFO at full depth on it, and 0 / -64 / down: the knob in range, bounded */
static void test_lfo_extremes(void)
{
    static const int S[2] = {1, -1};
    uint32_t k, ok = 1, i;
    for (k = 0; k < 2u; k++) {
        host_init();
        old_set_model(&trk[0], DM_HNOIS);
        flt_set(&trk[0], S[k] > 0 ? 127 : 0, S[k] > 0 ? 0 : 127, 127, 127, S[k] > 0 ? 63 : -64, S[k] > 0 ? 63 : -64, 127);
        trk[0].p[P_LFO1 + LF_PAGE] = LP_FLT;
        trk[0].p[P_LFO1 + LF_WAVE] = (int16_t)(S[k] > 0 ? LW_SQR : LW_ISQR);
        trk[0].p[P_LFO1 + LF_DPTH] = 127;
        drum_hit(&trk[0], 127);
        for (i = 0; i < SECS(1) / CTL; i++) {
            render_mix(wl, wr, CTL);
            ok &= abs(wl[0]) <= 32767 && trk[0].p[P_FCUT] >= 0 && trk[0].p[P_FCUT] <= 127;
        }
    }
    check("BASE / BOFS / an LFO on BASE at their extremes: the knob in range, bounded", ok);
}

/* after a noise burst every BASE x WDTH x Q rings out and ends within 3 s (the track idle); never while the input
 * sounds */
static void test_ring_ends_sweep(void)
{
    static const int B[6] = {0, 20, 40, 60, 90, 127}, W[4] = {0, 16, 48, 127}, Q[4] = {0, 40, 80, 127};
    static int32_t b[CTL];
    uint32_t c, w, r, blk, i, rng = 7, stuck = 0, early = 0;
    for (c = 0; c < 6u; c++)
        for (w = 0; w < 4u; w++)
            for (r = 0; r < 4u; r++) {
                uint32_t ended = 0;
                host_init();
                flt_set(&trk[0], B[c], W[w], Q[r], Q[r], 0, 0, 40);
                for (blk = 0; blk < SECS(3.1) / CTL && !ended; blk++) {
                    uint32_t loud = blk < SECS(0.1) / CTL, on;
                    for (i = 0; i < CTL; i++) {
                        rng = rng * 1664525u + 1013904223u;
                        b[i] = loud ? (int32_t)(rng >> 14) - 131072 : 0;
                    }
                    on = trk_filter(&trk[0], b, CTL);
                    early += loud && !on && trk[0].fmode;
                    ended = !loud && !on;
                }
                if (!ended && stuck++ < 4)
                    printf("     BASE %d WDTH %d Q %d: still ringing after 3 s\n", B[c], W[w], Q[r]);
            }
    check("ring-out: every BASE x WDTH x Q ends within 3 s after its input stops, never while it sounds",
          !stuck && !early);
}

/* a held kick (K808 DECAY 127) through a high-pass, a band: no clicks (the filter never cleared mid-sound) */
static void test_hp_kick_no_clicks(void)
{
    uint32_t i, k;
    int32_t worst = 0;
    for (k = 0; k < 2u; k++) {
        host_init();
        old_set_model(&trk[0], DM_K808);
        trk[0].p[P_E1] = 127;
        flt_set(&trk[0], k ? 40 : 30, k ? 30 : 127, 0, 0, 0, 0, 40);
        drum_hit(&trk[0], 127);
        render_mix(wl, wr, SECS(2));
        for (i = SECS(0.2); i + 1 < SECS(2); i++)
            worst = abs(wl[i + 1] - wl[i]) > worst ? abs(wl[i + 1] - wl[i]) : worst;
    }
    check("K808 held through HP / a band: no clicks (sample steps < 64 after the attack)", worst < 64);
}

/* EQ: +36 dB at EQF lifts a sine there far over one 4 octaves off; -36 cuts it; 64 off (no change, bit for bit) */
static void test_eq(void)
{
    double a = 2000, fc = CUTOFF_HZ[64], up, far, cut;
    up = tone_db_eq(fc, a, 127);
    far = tone_db_eq(fc / 16, a, 127);
    cut = tone_db_eq(fc, a, 0);
    printf("     EQ: +36 dB at EQF %.1f dB (4 octaves under %.1f), -36: %.1f dB\n", up, far, cut);
    check("EQ: +36 at EQF lifts it >= 25 dB, >= 15 dB over 4 octaves under; -36 cuts it >= 25 dB",
          up > 25 && up > far + 15 && cut < -25);
    check("EQ: EQG 64 is off (trk_inserts skips it)", eq_off_identical());
}

/* the knobs motion records: the FILTER's, the EFFECTS' */
static void test_motion_params(void)
{
    check("motion records BASE WDTH HPQ LPQ ATK DEC BOFS WOFS, EQ, SRR, the delay",
          motion_param(P_FCUT) && motion_param(P_FWDTH) && motion_param(P_FRESO) && motion_param(P_FLPQ) &&
              motion_param(P_FATK) && motion_param(P_FDEC) && motion_param(P_FENV) && motion_param(P_FWOFS) &&
              motion_param(P_EQF) && motion_param(P_EQG) && motion_param(P_SRR) && motion_param(P_DTIM) &&
              motion_param(P_DLY) && motion_param(P_DWID));
}

int main(void)
{
    test_response();
    test_keytrack();
    test_extremes();
    test_envelope();
    test_tail();
    test_stage_switch();
    test_choke_ring_ends();
    test_all_resonant();
    test_decay_display();
    test_lfo_base();
    test_lfo_extremes();
    test_ring_ends_sweep();
    test_hp_kick_no_clicks();
    test_eq();
    test_motion_params();
    printf(fails ? "filter_test: %d FAILED\n" : "filter_test: all passed\n", fails);
    return fails ? 1 : 0;
}
