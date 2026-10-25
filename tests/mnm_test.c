/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 (Hortator fork): 2026 */
/* MNM-1 host checks: the synth machines (mm_synth.c), the AMP envelope, PORT and SLIDE, the ARP (arp.c), the DUAL filter
 * and SRR (filter.c), the gates at STOP, the power-on kit's cost. Each check prints one line; any failure exits 1.
 *   mnm_test [OUTDIR]     with OUTDIR: also WAV demos there (each machine, the kit, ARP, PORT, the filter) */
#include "drum_host.h"
#include <sys/stat.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wl[SECS(8)], wr[SECS(8)], blk[CTL];
static const uint32_t MM[] = {DM_MMSAW, DM_MMPLS, DM_MMSID, DM_MMFM, DM_MMDP, DM_MMGND, DM_MMVO};
#define NMM (sizeof MM / sizeof MM[0])

static int32_t peak_of(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t p = 0;
    for (; a < b; a++)
        p = abs(x[a]) > p ? abs(x[a]) : p;
    return p;
}
static double rms_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t n = b - a;
    for (; a < b; a++)
        s += (double)x[a] * x[a];
    return n ? sqrt(s / n) : 0;
}
static uint32_t end_of(const int32_t *x, uint32_t frames)
{
    while (frames > 0 && x[frames - 1] == 0)
        frames--;
    return frames;
}
/* the frequency over [a, b) by its rising zero crossings (Hz) */
static double freq_of(const int32_t *x, uint32_t a, uint32_t b)
{
    uint32_t i, n = 0, f0 = 0, f1 = 0;
    for (i = a + 1; i < b; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            if (!n)
                f0 = i;
            f1 = i;
            n++;
        }
    return n > 1 ? (double)(n - 1) * FS / (f1 - f0) : 0;
}

/* one track through the device's chain, its gate counted as on the device (seq_tick, stopped too): the voices, then
 * the FILTER and SRR (filter.c trk_inserts); no LFO, no mix */
static void render_chain(track_t *t, int32_t *dst, uint32_t frames)
{
    uint32_t f, i, snd;
    for (f = 0; f < frames; f += CTL) {
        drum_block_begin();
        seq_tick(t, CTL);
        snd = track_render(t, blk, CTL);
        trk_inserts(t, blk, CTL, snd);
        for (i = 0; i < CTL && f + i < frames; i++)
            dst[f + i] = blk[i];
    }
}

/* track 0 alone on machine mi, its AMP / FILTER / ARP at their defaults (the power-on kit's other tracks silent) */
static track_t *solo(uint32_t mi)
{
    uint32_t i, k;
    host_init_live();
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            if (i < P_E0 || i > P_E7)
                trk[k].p[i] = TP[i].def;
        trk[k].p[P_MUTE] = k != 0u;
    }
    drum_set_model(&trk[0], mi);
    return &trk[0];
}

static void test_machines(void)
{
    uint32_t i, ok = 1, oke = 1;
    for (i = 0; i < NMM; i++) {
        track_t *t = solo(MM[i]);
        double r;
        uint32_t e;
        drum_hit(t, 127);                            /* a hit's gate: LEN of a 1/16 at 120 BPM (~94 ms) */
        render_chain(t, wl, SECS(1));
        r = rms_of(wl, SECS(0.01), SECS(0.08));
        e = end_of(wl, SECS(1));
        printf("     %-5s rms %6.0f peak %6d, ends at %3.0f ms\n", N_MODEL[MM[i]], r, peak_of(wl, 0, SECS(1)), e * 1000.0 / FS);
        ok &= r > 1500.0 && peak_of(wl, 0, SECS(1)) <= 2 * VOICE_FS;
        oke &= e < SECS(0.4) && !t->v[0].active;
    }
    check("every machine sounds at its defaults, bounded (under 2 x a voice's full scale)", ok);
    check("every machine: the hit's gate closes after LEN, the RELEASE ends the voice (under 0.4 s)", oke);
}

static void test_amp(void)
{
    track_t *t = solo(DM_MMGND);                     /* a plain sine */
    double a, b, h, f;
    t->p[P_AATK] = 64;                               /* ~100 ms (TIME_MS_X10) */
    drum_play(t, 127, 0);                            /* a held note: no countdown (as a key held) */
    render_chain(t, wl, SECS(2));
    h = peak_of(wl, SECS(0.045), SECS(0.055));
    f = peak_of(wl, SECS(0.19), SECS(0.2));
    printf("     ATK %.0f ms: at 50 ms %.2f of the top\n", TIME_MS_X10[64] / 10.0, h / f);
    check("AMP ATTACK: linear, half way at half its time", h / f > 0.4 && h / f < 0.6);
    a = rms_of(wl, SECS(0.3), SECS(0.4));
    b = rms_of(wl, SECS(1.9), SECS(2.0));
    check("AMP HOLD GATE, DECAY INF: the note holds while its gate is open", t->v[0].active && fabs(a - b) < 0.05 * a);
    acid_gate_off(t);
    render_chain(t, wl, SECS(1));
    check("AMP RELEASE: the gate closes, the voice ends", !t->v[0].active && end_of(wl, SECS(1)) < SECS(0.2));
    t = solo(DM_MMGND);
    t->p[P_AHOLD] = 0;
    t->p[P_ADEC] = 40;                               /* ~41 ms to -60 dB */
    drum_play(t, 127, 0);
    render_chain(t, wl, SECS(1));
    check("AMP HOLD 1 ms, DECAY: the note decays and ends with its gate still open", !t->v[0].active &&
          end_of(wl, SECS(1)) < SECS(0.25));
    t = solo(DM_MMGND);
    t->p[P_AHOLD] = 80;                              /* ~35 ms */
    t->p[P_ADEC] = 127;
    drum_play(t, 127, 0);
    render_chain(t, wl, SECS(0.5));
    check("AMP HOLD timed, DECAY INF: stays at the top after the hold (until the gate closes)", t->v[0].active &&
          rms_of(wl, SECS(0.4), SECS(0.5)) > 0.95 * rms_of(wl, SECS(0.1), SECS(0.2)));
    {
        char v[8];
        const char *u;
        param_format(&TP[P_AHOLD], 127, v, &u);
        a = str_eq(v, "GATE");
        param_format(&TP[P_ADEC], 127, v, &u);
        check("AMP: HOLD 127 reads GATE, DEC 127 INF", a && str_eq(v, "INF"));
    }
}

static void test_port_slide(void)
{
    track_t *t = solo(DM_MMGND);
    double f0, f1, f2;
    drum_play(t, 127, 0);                            /* C4 */
    render_chain(t, wl, SECS(0.3));
    t->p[P_PORT] = 90;
    drum_play(t, 127, 12);                           /* C5, gliding from C4 */
    render_chain(t, wl, SECS(2));
    f0 = freq_of(wl, 0, SECS(0.02));
    f1 = freq_of(wl, SECS(1.5), SECS(2));
    printf("     PORT 90: %.1f Hz at the start, %.1f Hz after 1.5 s (C4 261.6, C5 523.3)\n", f0, f1);
    check("PORT: a new note starts from the last one's pitch and glides to its own", f0 < 330.0 && fabs(f1 - 523.25) < 5.0);
    t = solo(DM_MMGND);
    drum_play(t, 127, 0);
    render_chain(t, wl, SECS(0.1));
    drum_play(t, 127, 12);
    render_chain(t, wl, SECS(0.1));
    f2 = freq_of(wl, 0, SECS(0.1));
    check("PORT 0: the note jumps", fabs(f2 - 523.25) < 5.0);
    /* SLIDE on the sequencer: step 1 slides into step 2, no new voice; step 3 (no slide) restarts */
    t = solo(DM_MMGND);
    t->step[0].on = t->step[0].slide = 1;
    t->step[1].on = 1;
    t->step[1].nt = 7;
    t->step[2].on = 1;
    t->p[P_SLEN] = 4;
    t->p[P_NLEN] = 120;
    transport_req = 1;
    render_mix(0, 0, CTL);
    {
        uint32_t age0 = t->v[0].age, age1, age2, step = div_period(2, 0);
        render_mix(0, 0, step / CTL * CTL + CTL * 2u);
        age1 = t->v[0].age;
        f1 = t->v[0].ms.mm.tgt / 16 - 60;
        render_mix(0, 0, step / CTL * CTL);
        age2 = t->v[0].age;
        check("SLIDE: the slid-into step glides (the same voice, its note the target); the next step restarts",
              age0 && age1 == age0 && f1 == 7 && age2 != age1);
    }
    transport_req = 2;
    render_mix(0, 0, CTL);
}

static void test_arp(void)
{
    static const int32_t UP[6] = {0, 4, 7, 0, 4, 7}, UPDN2[8] = {0, 4, 7, 12, 16, 19, 16, 12};
    track_t *t = solo(DM_MMGND);
    uint32_t k, ok = 1, step = div_period(2, 0), ages = 0, last = 0;
    t->p[P_ARPM] = 1;                                /* UP */
    t->p[P_ARPS] = 2;                                /* 1/16 */
    t->p[P_ARPC] = 2;                                /* MAJ */
    drum_play(t, 127, 0);                            /* a held note (a key) */
    for (k = 0; k < 6u; k++) {
        int32_t nt = t->v[0].ms.mm.tgt / 16 - 60;
        ok &= nt == UP[k];
        ages += t->v[0].age != last;
        last = t->v[0].age;
        render_chain(t, wl, step / CTL * CTL + CTL);
        render_chain(t, wl, 0);
    }
    check("ARP UP, MAJ, 1 octave, 1/16: C E G C E G, a new note each", ok && ages == 6u);
    acid_gate_off(t);
    render_chain(t, wl, SECS(0.5));
    check("ARP: the run ends with the note that started it (its gate)", !t->arp_on && !t->v[0].active);
    t->p[P_ARPM] = 3;                                /* UP-DOWN over 2 octaves */
    t->p[P_ARPR] = 2;
    t->arp_on = 1;
    for (k = 0, ok = 1; k < 8u; k++)
        ok &= arp_offset(t, k) == UPDN2[k];
    check("ARP UPDN, 2 octaves: C E G C' E' G' E' C' (the ends not repeated)", ok);
    t->p[P_ARPM] = 2;
    t->p[P_ARPR] = 1;
    check("ARP DOWN: G E C", arp_offset(t, 0) == 7 && arp_offset(t, 1) == 4 && arp_offset(t, 2) == 0);
    t = solo(DM_K909);
    t->p[P_ARPM] = 1;
    drum_play(t, 127, 0);
    check("ARP on a drum (no gate): nothing runs", !t->arp_on);
}

/* the energy of the first difference over [a, b): the high frequencies' share */
static double hf_of(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    for (a++; a < b; a++)
        s += (double)(x[a] - x[a - 1]) * (x[a] - x[a - 1]);
    return s;
}
static double lf_of(const int32_t *x, uint32_t a, uint32_t b)   /* .. and of a 64-sample moving sum: the lows */
{
    double s = 0, m = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        m += x[i] - (i >= a + 64u ? x[i - 64u] : 0);
        if (i >= a + 64u)
            s += m * m;
    }
    return s;
}

static void test_dual_srr(void)
{
    track_t *t = solo(DM_MMGND);
    double hf0, hf1, lf0, lf1;
    uint32_t i, same = 0;
    t->p[P_E0 + 1] = 2;                              /* WAVE NOIS */
    t->p[P_E0 + 2] = 127;                            /* COLR open */
    drum_play(t, 127, 0);
    render_chain(t, wl, SECS(0.5));
    hf0 = hf_of(wl, SECS(0.1), SECS(0.5));
    lf0 = lf_of(wl, SECS(0.1), SECS(0.5));
    t->p[P_FWDTH] = 20;                              /* DUAL: BASE 0, the low-pass ~100 Hz */
    render_chain(t, wl, SECS(0.5));
    hf1 = hf_of(wl, SECS(0.1), SECS(0.5));
    check("DUAL: a narrow WIDTH low-passes (high frequencies 20 dB down)", hf1 < hf0 / 100.0);
    t->p[P_FCUT] = 100;                              /* BASE ~4 kHz: the high-pass */
    t->p[P_FWDTH] = 27;
    render_chain(t, wl, SECS(0.5));
    lf1 = lf_of(wl, SECS(0.1), SECS(0.5));
    check("DUAL: a high BASE high-passes (the lows 20 dB down)", lf1 < lf0 / 100.0);
    t->p[P_FCUT] = 0;
    t->p[P_FWDTH] = 127;
    {
        int32_t hc, lc;
        flt_cuts_at(t, 0, &hc, &lc);
        check("BASE 0 WDTH 127 under the note: the low-pass out (no cost)", lc >= FLT_TOP);
    }
    t->p[P_SRR] = 127;
    render_chain(t, wl, SECS(0.5));
    for (i = SECS(0.1); i < SECS(0.5); i++)
        same += wl[i] == wl[i - 1];
    check("SRR 127: each sample held ~32 samples (~1.4 kHz)", same > (SECS(0.4) * 9u) / 10u);
}

/* the worst FX case: T1 noise into five tracks of one FX machine (NEIB, a chain), every one busy (bench.c bench_kit) */
static void test_fx_cost(void)
{
    static const uint32_t FX[] = {DM_FXTHRU, DM_FXREV, DM_FXCHO, DM_FXDYN, DM_FXRING, DM_FXPHS, DM_FXFLG};
    uint32_t f;
    double worst = 0;
    for (f = 0; f < sizeof FX / sizeof FX[0]; f++) {
        uint64_t i0;
        double c;
        bench_reset();
        bench_setup(BENCH_MODELS + 2u * FX[f]);
        render_mix(0, 0, SECS(0.3));
        i0 = instr_now();
        render_mix(wl, wr, SECS(1));
        c = i0 ? (double)(instr_now() - i0) / SECS(1) : 0;
        printf("     T1 noise into 5 x %-5s: %4.0f host instructions / sample\n", N_MODEL[FX[f]], c);
        worst = c > worst ? c : worst;
        transport_req = 2;
        render_mix(0, 0, CTL);
    }
    check("cost: five FX tracks of any machine in a chain, all busy, under the FM-1's 100 % (~3300)", worst < 3300.0);
}

/* the demo: a groove for the power-on kit (16 steps, 'x' a note, 'X' accented, '~' slide into the next; notes from
 * NT per track): the kick, the CLING snare on 2 and 4 (heard through the FLANGER on T3), the bass, the arp into the
 * reverb; the FX tracks (T3, T6) at GATE ON need no steps */
static const char *const DEMO[NTRK] = {
    "X...x...x...x..x", "....X.......X..x", "................", "x.xX..x.x.x~x..x", "x.......x.......",
    "................",
};
static const int8_t DEMO_NT[NTRK][16] = {
    {0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5}, {0},
    {-12, 0, -12, 0, 0, 0, -9, 0, -12, 0, -7, -5, 0, 0, 0, -12}, {0, 0, 0, 0, 0, 0, 0, 0, 3}, {0},
};
static void demo_kit(void)
{
    uint32_t i, k;
    host_init_live();
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 16u; k++) {
            trk[i].step[k].on = DEMO[i][k] == 'x' || DEMO[i][k] == 'X' || DEMO[i][k] == '~';
            trk[i].step[k].acc = DEMO[i][k] == 'X';
            trk[i].step[k].slide = DEMO[i][k] == '~';
            trk[i].step[k].nt = DEMO_NT[i][k];
        }
    trk[4].p[P_SLEN] = 16;
    song.g[G_BPM] = 120;
}

/* track k on machine mi, every other parameter at its default (the power-on kit's sound gone) */
static void fresh_track(uint32_t k, uint32_t mi)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (i < P_E0 || i > P_E7)
            trk[k].p[i] = TP[i].def;
    drum_set_model(&trk[k], mi);
}

/* the FX tracks and the buses (fx.c, mm_fx.c): NEIB, an insert, a send and return, GATE TRIG, the sleep */
static void test_routing(void)
{
    uint32_t k, same = 1;
    double dry, wet;
    /* a plain sine on T1 (OUT AB), THRU on T2 by NEIB: T2 gets T1's block after its LEVEL */
    host_init_live();
    for (k = 0; k < NTRK; k++) {
        fresh_track(k, k == 1u ? DM_FXTHRU : DM_MMGND);
        trk[k].p[P_OUT] = BUS_AB;
        trk[k].p[P_IN] = IN_NEIB;
        trk[k].p[P_FXG] = 0;
        trk[k].p[P_MUTE] = k > 1u;
        trk[k].p[P_LEVEL] = 112;                     /* 0 dB */
        trk[k].p[P_E0] = k == 1u ? 64 : -12;         /* (the sine an octave down, quiet enough for the limiter) */
    }
    drum_play(&trk[0], 127, 0);
    render_mix(wl, wr, SECS(0.2));
    for (k = 0; k < CTL; k++)
        same &= abs(trk_post[1][k] - trk_post[0][k]) <= 4;
    check("NEIB: THRU (INP 64, x 1) on T2 at 0 dB passes T1's sound (within the level's rounding)",
          same && trk[1].v[0].active);
    trk[0].p[P_LEVEL] = trk[1].p[P_LEVEL] = 90;     /* (under the master limiter) */
    trk[1].p[P_MUTE] = 1;
    render_mix(wl, wr, SECS(0.2));
    dry = rms_of(wl, SECS(0.1), SECS(0.2));          /* the source alone */
    trk[1].p[P_MUTE] = 0;
    trk[0].p[P_OUT] = BUS_OFF;                       /* the source's own output off: heard through the FX only */
    render_mix(wl, wr, SECS(0.2));
    wet = rms_of(wl, SECS(0.1), SECS(0.2));
    printf("     NEIB: the source alone %.0f, through the FX alone %.0f\n", dry, wet);
    check("NEIB, the source's OUT OFF: only the FX track heard (the source's sound at the FX track's LEVEL once more)",
          fabs(wet - dry * LEVEL_Q12[90] / 4096.0) < 0.1 * dry && wet > 100.0);
    /* an insert: T2 FX IN AB OUT AB replaces the bus (T1 on AB is in it); the FX at INP 0: silence */
    trk[0].p[P_OUT] = BUS_AB;
    trk[1].p[P_IN] = IN_AB;
    trk[1].p[P_E0] = 0;                              /* THRU's INP: 0 */
    render_mix(wl, wr, SECS(0.2));
    check("insert (IN AB, OUT AB): the FX's output takes the bus's place (INP 0: silent)",
          peak_of(wl, SECS(0.1), SECS(0.2)) < 64);
    /* a send and return: the source on CD (heard dry), the FX IN CD OUT AB adds its output */
    trk[0].p[P_OUT] = BUS_CD;
    trk[1].p[P_IN] = IN_CD;
    trk[1].p[P_OUT] = BUS_AB;
    trk[1].p[P_E0] = 64;
    render_mix(wl, wr, SECS(0.2));
    check("send / return (source on CD, FX IN CD OUT AB): dry and wet both heard",
          fabs(rms_of(wl, SECS(0.1), SECS(0.2)) - dry * (1.0 + LEVEL_Q12[90] / 4096.0)) < 0.1 * dry);
    /* GATE TRIG: nothing passes until the FX track's note; its AMP then opens */
    trk[0].p[P_OUT] = BUS_OFF;
    trk[1].p[P_IN] = IN_NEIB;
    trk[1].p[P_FXG] = 1;
    dv_cut(&trk[1], &trk[1].v[0]);
    trk[1].dtail = 0;
    render_mix(wl, wr, SECS(0.2));
    printf("     GATE TRIG, no note: peak %d (T1 %s, T2 %s)\n", peak_of(wl, SECS(0.1), SECS(0.2)),
           trk[0].v[0].active ? "on" : "-", trk[1].v[0].active ? "on" : "-");
    check("GATE TRIG: without a note on the FX track nothing passes", peak_of(wl, SECS(0.1), SECS(0.2)) < 64);
    drum_play(&trk[1], 127, 0);
    render_mix(wl, wr, SECS(0.2));
    check("GATE TRIG: its note opens its AMP (GATE: held while the note is)", rms_of(wl, SECS(0.1), SECS(0.2)) > 100.0);
    /* the sleep: GATE ON, the input gone: the FX voice ends, and starts again with the input */
    trk[1].p[P_FXG] = 0;
    acid_gate_off(&trk[0]);
    render_mix(wl, wr, SECS(1));
    check("an FX at GATE ON with no input sleeps (its voice ends: no cost)", !trk[1].v[0].active);
    drum_play(&trk[0], 127, 0);
    render_mix(wl, wr, SECS(0.1));
    check("... and wakes with its input", trk[1].v[0].active && rms_of(wl, SECS(0.05), SECS(0.1)) > 100.0);
}

/* the FX machines on noise (NEIB from T1): bounded, each passing its input; REVERB's tail and GATE */
static void test_fx_machines(void)
{
    static const uint32_t FX[] = {DM_FXTHRU, DM_FXREV, DM_FXCHO, DM_FXDYN, DM_FXRING, DM_FXPHS, DM_FXFLG};
    uint32_t f, ok = 1, k;
    for (f = 0; f < sizeof FX / sizeof FX[0]; f++) {
        host_init_live();
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_MUTE] = k > 1u;
        fresh_track(0, DM_MMGND);
        trk[0].p[P_E0 + 1] = 2;                      /* NOIS */
        trk[0].p[P_E0 + 2] = 127;
        trk[0].p[P_OUT] = BUS_OFF;
        fresh_track(1, FX[f]);
        trk[1].p[P_IN] = IN_NEIB;
        trk[1].p[P_OUT] = BUS_AB;
        trk[1].p[P_FXG] = 0;
        drum_play(&trk[0], 127, 0);
        render_mix(wl, wr, SECS(0.5));
        printf("     %-5s rms %6.0f peak %6d\n", N_MODEL[FX[f]], rms_of(wl, SECS(0.1), SECS(0.5)), peak_of(wl, 0, SECS(0.5)));
        ok &= rms_of(wl, SECS(0.1), SECS(0.5)) > 300.0 && peak_of(wl, 0, SECS(0.5)) <= 32767;
    }
    check("every FX machine passes its input (noise by NEIB) at its defaults, bounded", ok);
    host_init_live();                                /* REVERB: a tail after a short note; GATE cuts it */
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = k > 1u;
    fresh_track(0, DM_MMGND);
    trk[0].p[P_OUT] = BUS_OFF;
    trk[0].p[P_AHOLD] = 70;                          /* ~25 ms, then ~50 ms down */
    trk[0].p[P_ADEC] = 40;
    fresh_track(1, DM_FXREV);
    trk[1].p[P_IN] = IN_NEIB;
    trk[1].p[P_OUT] = BUS_AB;
    trk[1].p[P_FXG] = 0;
    trk[1].p[P_E0 + 3] = 127;                        /* MIX: wet */
    drum_play(&trk[0], 127, 0);
    render_mix(wl, wr, SECS(1));
    printf("     REVERB: during the note %.0f, the tail at 0.4 .. 0.6 s %.0f\n", rms_of(wl, SECS(0.02), SECS(0.06)),
           rms_of(wl, SECS(0.4), SECS(0.6)));
    check("REVERB: a tail rings on after a short note (0.4 .. 0.6 s: within 30 dB of the note)",
          rms_of(wl, SECS(0.4), SECS(0.6)) > rms_of(wl, SECS(0.02), SECS(0.06)) / 32.0);
    trk[1].p[P_E0 + 2] = 10;                         /* GATE ~110 ms */
    drum_play(&trk[0], 127, 0);
    render_mix(wl, wr, SECS(1));
    check("REVERB GATE: the tail cut after the gate time", peak_of(wl, SECS(0.4), SECS(0.6)) < 64);
}

static void test_stop_and_cost(void)
{
    uint32_t k, on = 0;
    double c;
    uint64_t i0;
    demo_kit();
    transport_req = 1;
    render_mix(wl, wr, SECS(2));
    for (k = 0; k < NTRK; k++)
        on += trk[k].v[0].active;
    transport_req = 2;
    render_mix(wl, wr, SECS(2));
    for (k = 0; k < NTRK; k++)
        on += (trk[k].v[0].active ? 100u : 0u) +       /* (an FX at GATE ON sleeps once its input is gone) */
              (dv_gate_open(&trk[k], &trk[k].v[0]) && !trk_model(&trk[k])->fx ? 100u : 0u);
    printf("     STOP: before %u voices; after:", on);
    for (k = 0; k < NTRK; k++)
        printf(" T%u %s%s", k + 1u, trk[k].v[0].active ? "on" : "-", dv_gate_open(&trk[k], &trk[k].v[0]) ? "+gate" : "");
    printf("\n");
    check("STOP: every gate closes, every voice ends (no drone)", on > 0u && on < 100u);
    demo_kit();
    transport_req = 1;
    render_mix(wl, wr, SECS(1));
    i0 = instr_now();
    render_mix(wl, wr, SECS(4));
    c = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    printf("     the power-on kit playing the demo (6 tracks: 4 machines, the FLANGER and REVERB tracks, ARP): %.0f host instructions / sample "
           "(the FM-1: ~3300 = 100 %%; the stock Felucca reference 1566)\n", c);
    check("cost: the power-on kit's demo within the stock Felucca reference (1566, ~47 % CPU)", !i0 || c <= 1566.0);
    check("the demo's mix is bounded (the master limiter: no wrap)", peak_of(wl, 0, SECS(4)) <= 32767);
}

/* any machine, any knobs, AMP, FILTER, SRR at random: bounded, and ends once its gate closes */
static void test_fuzz(void)
{
    uint32_t n, k, ok = 1, oke = 1, r = 12345u;
    for (n = 0; n < 300u; n++) {
        track_t *t = solo(MM[n % NMM]);
        const dmodel_t *m = trk_model(t);
        for (k = 0; k < 8u; k++) {
            r = r * 1664525u + 1013904223u;
            t->p[P_E0 + k] = (int16_t)(m->edit[k].min + (int32_t)((r >> 8) % (uint32_t)(m->edit[k].max - m->edit[k].min + 1)));
        }
        r = r * 1664525u + 1013904223u;
        t->p[P_AATK] = (int16_t)((r >> 8) % 100u);
        t->p[P_AHOLD] = (int16_t)((r >> 15) % 128u);
        t->p[P_ADEC] = (int16_t)((r >> 22) % 128u);
        r = r * 1664525u + 1013904223u;
        t->p[P_AREL] = (int16_t)((r >> 8) % 110u);
        t->p[P_FCUT] = (int16_t)((r >> 15) % 128u);
        t->p[P_FWDTH] = (int16_t)((r >> 22) % 128u);
        r = r * 1664525u + 1013904223u;
        t->p[P_FRESO] = (int16_t)((r >> 8) % 128u);
        t->p[P_FENV] = (int16_t)((r >> 15) % 128u) - 64;
        t->p[P_SRR] = (int16_t)((r >> 22) % 128u);
        t->p[P_PORT] = (int16_t)((r >> 25) % 128u);
        drum_hit(t, 1u + n % 127u);
        render_chain(t, wl, SECS(7));
        ok &= peak_of(wl, 0, SECS(7)) <= 4 * VOICE_FS;
        if (t->v[0].active || t->fring) {
            oke = 0;
            printf("     %s ATK %d HOLD %d DEC %d REL %d: still sounding after 7 s\n", m->name, t->p[P_AATK],
                   t->p[P_AHOLD], t->p[P_ADEC], t->p[P_AREL]);
        }
    }
    check("fuzz (300): any machine, knobs, AMP, FILTER, SRR, PORT: bounded", ok);
    check("fuzz: every note ends once its gate has closed (its RELEASE, the filter's ring)", oke);
}

/* ---- WAV demos */
static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v, sr = FS, br = FS * 4;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}
static void write_mix(const char *dir, const char *name, double secs)
{
    char path[512];
    uint32_t i, frames = SECS(secs), done = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    wav_hdr(f, frames);
    transport_req = 1;
    while (done < frames) {
        uint32_t n = frames - done < SECS(1) ? frames - done : SECS(1);
        render_mix(wl, wr, n);
        for (i = 0; i < n; i++) {
            int16_t s[2] = {(int16_t)clamp(wl[i], -32768, 32767), (int16_t)clamp(wr[i], -32768, 32767)};
            fwrite(s, 2, 2, f);
        }
        done += n;
    }
    fclose(f);
    transport_req = 2;
    render_mix(wl, wr, CTL);
    printf("mnm: %s\n", path);
}
static void write_demos(const char *dir)
{
    static const char *const MEL = "x.x.xx.x~x.x.xx.";
    static const int8_t MEL_NT[16] = {0, 0, 3, 0, 7, 10, 0, 7, 5, 3, 0, 0, 12, 10, 0, 7};
    uint32_t i, k;
    char name[64];
    mkdir(dir, 0755);
    demo_kit();
    write_mix(dir, "mnm_kit_demo.wav", 16);
    for (i = 0; i < NMM; i++) {                      /* each machine (its kit sound, else its defaults on T4): a phrase */
        uint32_t kt = 3;
        for (k = 0; k < NTRK; k++)
            if (KIT_DEF[k][0] == MM[i]) {
                kt = k;
                break;
            }
        demo_kit();
        if (KIT_DEF[kt][0] != MM[i])
            drum_set_model(&trk[kt], MM[i]);
        trk[kt].p[P_OUT] = BUS_AB;
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_MUTE] = k != kt;
            memset(trk[k].step, 0, sizeof trk[k].step);
        }
        trk[kt].p[P_SLEN] = 16;
        trk[kt].p[P_ARPM] = 0;
        for (k = 0; k < 16u; k++) {
            trk[kt].step[k].on = MEL[k] != '.';
            trk[kt].step[k].slide = MEL[k] == '~';
            trk[kt].step[k].nt = (int8_t)(MEL_NT[k] + (MM[i] == DM_MMGND ? 0 : MM[i] == DM_MMSAW ? -12 : 0));
        }
        snprintf(name, sizeof name, "mnm_machine_%s.wav", N_MODEL[MM[i]]);
        write_mix(dir, name, 4);
    }
    demo_kit();                                      /* PORT on the lead */
    for (k = 0; k < NTRK; k++)                       /* (the bass, T4) */
        trk[k].p[P_MUTE] = k != 3u && k != 0u;
    trk[3].p[P_PORT] = 70;
    write_mix(dir, "mnm_port_bass.wav", 8);
    demo_kit();                                      /* the ARP's modes, a bar each, on a held chord track */
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = k != 4u && k != 5u && k != 0u;   /* the arp (T5) and its reverb */
    trk[4].p[P_ARPR] = 3;
    trk[4].p[P_ARPS] = 3;                            /* 1/32 */
    write_mix(dir, "mnm_arp.wav", 8);
    demo_kit();                                      /* the CLING snare (T2 through the FLANGER on T3), and dry */
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = k != 1u && k != 2u;
    for (k = 0; k < 16u; k++)
        trk[1].step[k].on = k == 4u || k == 12u || k == 14u || k == 15u;
    write_mix(dir, "mnm_sophie_cling.wav", 8);
    trk[1].p[P_OUT] = BUS_AB;
    trk[2].p[P_MUTE] = 1;
    write_mix(dir, "mnm_sophie_cling_dry.wav", 8);
}

/* ---- MNM-1 0.4: the three LFOs (lfo.c) */
static void test_lfos(void)
{
    uint32_t i, ok = 1, w;
    track_t *t;
    /* the waves: TRI from 0 up to its top at 1/4; SINE; SQR high then low; SAW down; EXP down; mirrored upside down */
    ok &= abs(lfo_shape(LW_TRI, 0)) <= 1 && lfo_shape(LW_TRI, 0x40000000u) >= 32760 && lfo_shape(LW_TRI, 0xC0000000u) <= -32760;
    ok &= lfo_shape(LW_SQR, 0x10000000u) == 32767 && lfo_shape(LW_SQR, 0x90000000u) == -32767;
    ok &= lfo_shape(LW_SAW, 0) > 32700 && lfo_shape(LW_SAW, 0xFFFF0000u) < -32700;
    ok &= lfo_shape(LW_EXP, 0) > 32700 && lfo_shape(LW_EXP, 0x40000000u) < 0;   /* (fast, then slow) */
    for (w = LW_TRI; w < LW_RND; w++)
        for (i = 0; i < 64u; i++)
            ok &= lfo_shape(w + LW_ITRI, i * 0x04000000u) == -lfo_shape(w, i * 0x04000000u);
    check("LFO waves: TRI SINE SQR SAW EXP, the mirrored five upside down", ok);
    /* the tempo: SPD 64 at MULT 2X one cycle a bar of 16 steps (120 BPM: 88200 samples); each MULT step doubles */
    host_init();
    song.g[G_BPM] = 120;
    t = &trk[0];
    lfo_set(t, 0, LP_AMP, 6, LW_TRI, 64, 1, 64);
    {
        double cyc = (double)lfo_inc(&t->p[P_LFO1]) * 88200.0 / 4294967296.0;
        t->p[P_LFO1 + LF_MULT] = 2;
        printf("     LFO: SPD 64 x 2X: %.4f cycles a bar; x 4X: %.4f\n", cyc,
               (double)lfo_inc(&t->p[P_LFO1]) * 88200.0 / 4294967296.0);
        check("LFO tempo: SPD 64 at 2X one cycle a bar (within 0.1 %), 4X two",
              fabs(cyc - 1.0) < 0.001 && fabs((double)lfo_inc(&t->p[P_LFO1]) * 88200.0 / 4294967296.0 - 2.0) < 0.002);
    }
    /* TRIG: FREE runs on through notes; TRIG starts over at a note; HOLD keeps the value at the note; ONE stops after a
     * cycle, HALF after half */
    host_init();
    t = &trk[0];
    lfo_set(t, 0, LP_AMP, 6, LW_SAW, 64, 4, 64);
    lfo_set(t, 1, LP_AMP, 6, LW_SAW, 64, 4, 64);
    lfo_set(t, 2, LP_AMP, 6, LW_SAW, 64, 4, 64);
    t->p[P_LFO2 + LF_TRIG] = LT_TRIG;
    t->p[P_LFO3 + LF_TRIG] = LT_HOLD;
    render_mix(wl, wr, SECS(0.3));
    {
        uint32_t ph0 = t->lfo[0].ph;
        int32_t held;
        drum_hit(t, 100);
        ok = t->lfo[0].ph == ph0 && t->lfo[1].ph == 0u;
        render_mix(wl, wr, CTL);                     /* (HOLD takes its value in the block after the note) */
        held = t->lfo[2].out;
        render_mix(wl, wr, SECS(0.1));
        check("LFO TRIG: FREE runs on through a note, TRIG starts over at it", ok && t->lfo[1].ph != 0u);
        check("LFO HOLD: its output the value at the note, until the next", t->lfo[2].out == held && t->lfo[2].run != held);
    }
    host_init();
    t = &trk[0];
    lfo_set(t, 0, LP_AMP, 6, LW_SAW, 127, 6, 64);   /* fast: a cycle well inside 0.2 s */
    lfo_set(t, 1, LP_AMP, 6, LW_SAW, 127, 6, 64);
    t->p[P_LFO1 + LF_TRIG] = LT_ONE;
    t->p[P_LFO2 + LF_TRIG] = LT_HALF;
    drum_hit(t, 100);
    render_mix(wl, wr, SECS(0.5));
    check("LFO ONE: one cycle then held at its end; HALF: half a cycle then held",
          t->lfo[0].stop && t->lfo[0].ph == 0xFFFFFFFFu && t->lfo[0].out < -32000 && t->lfo[1].stop &&
              t->lfo[1].ph == 0x7FFFFFFFu && abs(t->lfo[1].out) < 300);
    /* INTL: the wave and silence in turn, INTL times a cycle each */
    {
        uint32_t zero = 0, nz = 0;
        host_init();
        t = &trk[0];
        lfo_set(t, 0, LP_AMP, 6, LW_SQR, 64, 3, 64);   /* a cycle 4 steps (0.5 s) */
        t->p[P_LFO1 + LF_INTL] = 4;
        for (i = 0; i < 4000u; i++) {
            lfo_apply(CTL);
            zero += t->lfo[0].out == 0;
            nz += t->lfo[0].out != 0;
            lfo_restore();
        }
        check("LFO INTL: the output silent about half the time", zero > 1500u && nz > 1500u);
    }
    /* an LFO on another LFO's DPTH (LF2, DEST 8): it moves that LFO's reach in the same block; another LFO's PAGE DEST
     * TRIG: never */
    host_init();
    t = &trk[0];
    lfo_set(t, 0, LP_LF2, 7, LW_SQR, 64, 2, 127);
    lfo_set(t, 1, LP_AMP, 6, LW_SQR, 64, 2, 20);
    {
        int32_t mx = 0;
        for (i = 0; i < 3000u; i++) {
            lfo_apply(CTL);
            mx = t->p[P_LFO2 + LF_DPTH] > mx ? t->p[P_LFO2 + LF_DPTH] : mx;
            lfo_restore();
        }
        ok = mx > 100 && t->p[P_LFO2 + LF_DPTH] == 20;
        t->p[P_LFO1 + LF_DEST] = 0;                  /* LF2's PAGE */
        ok &= lfo_dest_param(t, 0) == 0xFFu;
        t->p[P_LFO1 + LF_DEST] = 2;                  /* .. TRIG */
        ok &= lfo_dest_param(t, 0) == 0xFFu;
        check("LFO on LFO 2's DPTH: moves it, put back after the block; another LFO's PAGE / TRIG: not a DEST", ok);
    }
    /* two LFOs on one knob add up */
    host_init();
    t = &trk[0];
    t->p[P_PAN] = 0;
    lfo_set(t, 0, LP_AMP, 6, LW_SQR, 64, 2, 32);
    lfo_set(t, 1, LP_AMP, 6, LW_SQR, 64, 2, 32);
    lfo_apply(CTL);
    {
        int32_t one = t->lfo[0].out * 32 / 127 * 127 / 32767;
        check("two LFOs on PAN add up (each its own share)", t->p[P_PAN] == clamp(2 * one, -64, 63) && t->lnum == 1u);
    }
    lfo_restore();
    check("(after the block: PAN back at its base)", t->p[P_PAN] == 0);
    /* PTCH: a synth machine's pitch; 1 OCT up at the LFO's top */
    host_init();
    t = &trk[0];
    old_set_model(t, DM_MMSAW);
    lfo_set(t, 0, LP_PTCH, 5, LW_SQR, 64, 2, 127);
    lfo_apply(CTL);
    check("LFO PTCH (OCT): the machine's pitch an octave off at the top (1/16 semitones)", abs(t->lpit) == 192);
    lfo_restore();
    check("(after the block: no pitch offset)", t->lpit == 0);
    /* DPTH 0 on all three: the sound bit for bit as with no LFO touched */
    {
        uint32_t h0 = 0, h1 = 0, k;
        for (k = 0; k < 2u; k++) {
            uint32_t h = 2166136261u;
            host_init();
            demo_kit();
            if (k)
                for (i = 0; i < NTRK; i++) {
                    lfo_set(&trk[i], 0, LP_FLT, 0, LW_SQR, 100, 3, 0);
                    lfo_set(&trk[i], 2, LP_PTCH, 7, LW_RND, 100, 5, 0);
                }
            transport_req = 1;
            render_mix(wl, wr, SECS(2));
            for (i = 0; i < SECS(2); i++)
                h = (h ^ (uint32_t)wl[i]) * 16777619u;
            if (k)
                h1 = h;
            else
                h0 = h;
        }
        check("LFOs at DPTH 0: the sound bit for bit as without", h0 == h1);
    }
}

/* ---- MNM-1 0.4: the tracks' delays (fx.c track_delay): a click in, left only (as mix_part hands it: ping-pong's
 * sides already swapped there); the first echo's place, the peaks left / right of the first and the second echo */
static uint32_t td_echo(uint32_t dtim, int dsnd, int dfb, int32_t *l1, int32_t *r1, int32_t *l2, int32_t *r2)
{
    static int32_t bl[CTL], br[CTL];
    uint32_t b, i, at = 0, d = dtim * 22050u / 64u;
    track_t *t = &trk[0];
    host_init();
    song.g[G_BPM] = 120;
    t->p[P_DTIM] = (int16_t)dtim;
    t->p[P_DLY] = (int16_t)dsnd;
    t->p[P_DFB] = (int16_t)dfb;
    t->p[P_DBAS] = 0;
    t->p[P_DWID] = 127;
    *l1 = *r1 = *l2 = *r2 = 0;
    for (b = 0; b < SECS(1.2) / CTL; b++) {
        for (i = 0; i < CTL; i++) {
            td_in_l[i] = b == 0u && i < 4u ? 300000 : 0;
            td_in_r[i] = 0;
            bl[i] = br[i] = 0;
        }
        track_delay(t, 0, bl, br, CTL, b == 0u);
        for (i = 0; i < CTL; i++) {
            uint32_t s0 = b * CTL + i;
            int32_t m = abs(bl[i]) > abs(br[i]) ? abs(bl[i]) : abs(br[i]);
            if (!at && m > 20000)
                at = s0;
            if (s0 + 200u > d && s0 < d + 200u) {
                *l1 = abs(bl[i]) > *l1 ? abs(bl[i]) : *l1;
                *r1 = abs(br[i]) > *r1 ? abs(br[i]) : *r1;
            } else if (s0 + 200u > 2u * d && s0 < 2u * d + 200u) {
                *l2 = abs(bl[i]) > *l2 ? abs(bl[i]) : *l2;
                *r2 = abs(br[i]) > *r2 ? abs(br[i]) : *r2;
            }
        }
    }
    return at;
}

static void test_delays(void)
{
    int32_t l1, r1, l2, r2;
    uint32_t at;
    at = td_echo(4, -40, 0, &l1, &r1, &l2, &r2);    /* 4 / 256 note at 120 BPM: 1378 samples (one cell a sample) */
    check("delay: DTIM 4 at 120 BPM, the echo 1378 samples on (+-2)", at >= 1376u && at <= 1382u);
    check("delay: DFB 0, one echo only", l2 < 300 && r2 < 300);
    at = td_echo(64, -40, 0, &l1, &r1, &l2, &r2);   /* a beat: 22050 samples (a tape, fewer cells a second) */
    printf("     delay: DTIM 64, the first echo at %u (a beat: 22050)\n", at);
    check("delay: DTIM 64, the echo a beat on (within 1 %)", at > 21830u && at < 22270u);
    td_echo(8, -40, 64, &l1, &r1, &l2, &r2);
    printf("     delay: DSND -40 echoes L %d R %d, then L %d R %d\n", l1, r1, l2, r2);
    check("delay: DSND under 0 keeps the sides (a left click echoes left, again left)", l1 > 20000 && r1 < 2000 &&
          l2 > l1 / 4 && r2 < 2000);
    td_echo(8, 40, 64, &l1, &r1, &l2, &r2);
    printf("     delay: DSND +40 echoes L %d R %d, then L %d R %d\n", l1, r1, l2, r2);
    check("delay: DSND over 0 ping-pongs (the first echo left, the second right)", l1 > 20000 && r1 < 2000 &&
          r2 > l1 / 4 && l2 < 2000);
    {                                                /* the feedback: DFB 32 halves each echo, a long loop goes quiet
                                                      * and asleep; DFB 127 grows into the clip, bounded */
        static int32_t bl[CTL], br[CTL];
        uint32_t b, i, ok = 1;
        track_t *t = &trk[0];
        host_init();
        song.g[G_BPM] = 120;
        t->p[P_DTIM] = 4;
        t->p[P_DLY] = -63;
        t->p[P_DFB] = 127;
        t->p[P_DWID] = 127;
        for (b = 0; b < SECS(3) / CTL; b++) {
            for (i = 0; i < CTL; i++) {
                td_in_l[i] = b == 0u && i < 64u ? 400000 : 0;
                td_in_r[i] = td_in_l[i];
                bl[i] = br[i] = 0;
            }
            track_delay(t, 0, bl, br, CTL, b == 0u);
            for (i = 0; i < CTL; i++)
                ok &= abs(bl[i]) <= 32767 * 16 && abs(br[i]) <= 32767 * 16;
        }
        check("delay: DFB 127 grows into the clip, bounded", ok && td[0].on);
        t->p[P_DFB] = 32;
        for (b = 0; b < SECS(3) / CTL; b++) {
            for (i = 0; i < CTL; i++) {
                td_in_l[i] = td_in_r[i] = 0;
                bl[i] = br[i] = 0;
            }
            track_delay(t, 0, bl, br, CTL, 0);
        }
        check("delay: DFB 32, no input: it dies out and sleeps (no DC held)", !td[0].on);
    }
}

/* ---- MNM-1 0.5: PARAMETER LOCKS and PARAMETER SLIDE (motion.c) */
static void plk_to_step(uint32_t s, uint32_t into)  /* render until track 1 is `into` samples into step s */
{
    uint32_t guard = 0;
    while ((trk[0].seq_idx != s || trk[0].seq_pos < into) && guard++ < 200000u)
        render_mix(0, 0, CTL);
}
static void plk_setup(void)                         /* T1 SWSAW, trigs on 0 4 8 12 of 16, SUB at 50, the rest muted */
{
    uint32_t k;
    host_init();
    old_set_model(&trk[0], DM_MMSAW);
    memset(trk[0].step, 0, sizeof trk[0].step);
    trk[0].p[P_SLEN] = 16;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[0].p[P_E3] = 50;
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_MUTE] = 1;
}

static void test_plocks(void)
{
    int32_t v;
    plk_setup();
    motion_add(0, 4, P_E3, 100);
    motion_add(0, 6, P_E3, 10);                      /* a lock with no trig */
    transport_req = 1;
    plk_to_step(4, 100);
    check("LOCK: step 4's lock plays there (100)", trk[0].p[P_E3] == 100);
    plk_to_step(5, 100);
    check("LOCK: .. and lasts over a step with no trig", trk[0].p[P_E3] == 100);
    plk_to_step(6, 100);
    check("LOCK: a lock with no trig sets the knob at its step (10)", trk[0].p[P_E3] == 10);
    plk_to_step(8, 100);
    check("LOCK: an unlocked trig plays the kit's value (50)", trk[0].p[P_E3] == 50);
    transport_req = 2;
    render_mix(wl, wr, CTL * 4);
    check("LOCK: STOP puts the kit's value back", trk[0].p[P_E3] == 50);
    plk_setup();                                     /* PARAMETER SLIDE: 0 at step 4 (SLIDE) to 120 at step 12 */
    motion_add(0, 4, P_E3, 0);
    motion_add(0, 12, P_E3, 120);
    trk[0].step[4].pslide = 1;
    transport_req = 1;
    plk_to_step(8, 0);
    v = trk[0].p[P_E3];
    printf("     SLIDE: at step 8 (half way) %d\n", (int)v);
    check("SLIDE: half way (step 8, an unlocked trig: no stop) about half way (60 +-8)", v >= 52 && v <= 68);
    plk_to_step(11, 2000);
    check("SLIDE: nearly there just before step 12", trk[0].p[P_E3] >= 105 && trk[0].p[P_E3] <= 120);
    plk_to_step(12, 100);
    check("SLIDE: at step 12 its lock exactly (120)", trk[0].p[P_E3] == 120);
    transport_req = 2;
    render_mix(wl, wr, CTL * 4);
    plk_setup();                                     /* a SLIDE step's knob locked nowhere else: no glide */
    motion_add(0, 4, P_E3, 0);
    trk[0].step[4].pslide = 1;
    transport_req = 1;
    plk_to_step(6, 100);
    check("SLIDE: a knob locked once doesn't slide (holds its lock)", trk[0].p[P_E3] == 0);
    transport_req = 2;
    render_mix(wl, wr, CTL * 4);
    plk_setup();                                     /* the edit API (the LOCK mode's) */
    lock_turn(0, 5, P_E3, 7);
    check("lock_turn: from the kit's value (50 + 7)", lock_find(0, 5, P_E3) >= 0 && mo.s.ev[lock_find(0, 5, P_E3)].value == 57);
    lock_turn(0, 5, P_E3, -100);
    check("lock_turn: clamped to the knob's range", mo.s.ev[lock_find(0, 5, P_E3)].value == track_desc(&trk[0], P_E3)->min);
    lock_turn(0, 5, P_E2, 3);
    check("lock_count_step: two locks on step 5", lock_count_step(0, 5) == 2u);
    lock_drop(0, 5, 0xFFu);
    check("lock_drop: the step's locks gone", lock_count_step(0, 5) == 0u && mo.s.count == 0u);
}

int main(int argc, char **argv)
{
    test_machines();
    test_amp();
    test_port_slide();
    test_arp();
    test_dual_srr();
    test_routing();
    test_fx_machines();
    test_fx_cost();
    test_stop_and_cost();
    test_lfos();
    test_delays();
    test_plocks();
    test_fuzz();
    if (argc > 1)
        write_demos(argv[1]);
    if (fails) {
        printf("mnm_test: %d FAILED\n", fails);
        return 1;
    }
    printf("mnm_test: all passed\n");
    return 0;
}
