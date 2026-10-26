/* BENCH on the host (firmware/src/bench.c): `bench_host` checks the cases; `bench_host measure` prints each
 * case's cost in host instructions per sample (tools/fm1_bench.py compares it with the FM-1's console run).
 * The cases are the firmware's (drum_set_model, today's defaults), so the power-on here
 * is the firmware's too: host_init_live. */
#include "drum_host.h"                             /* (with firmware/src/bench.c) */

#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t render_hash(uint32_t frames, int32_t *peak)    /* the mix of the next frames, FNV-1a */
{
    static int32_t o[2 * CTL];
    uint32_t h = 2166136261u, f, i;
    *peak = 0;
    for (f = 0; f < frames; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h = (h ^ (uint32_t)o[i]) * 16777619u;
            if (abs(o[i]) > *peak)
                *peak = abs(o[i]);
        }
    }
    return h;
}

static double measure(uint32_t c)                    /* host instructions per sample, as the FM-1 run: 0.5 s
                                                      * to settle, then 2 s */
{
    uint64_t i0;
    host_init_live();
    bench_setup(c);
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(0, 0, SECS(2));
    return i0 ? (double)(instr_now() - i0) / SECS(2) : 0;
}

/* the audio ISR's side of the run (audio.c, checked by audio_isr_test): HOLD counts the halves held, LIVE times
 * a render of 1 ms */
static void fake_half(void)
{
    if (bench.mode == BENCH_HOLD) {
        bench.held++;
    } else if (bench.mode == BENCH_LIVE) {
        render_mix(0, 0, HALF_FRAMES);
        bench.halves++;
        bench.sum_us += 1000;
        bench.max_us = 1000;
    }
}

static char lines[BENCH_N + 4][96];
static uint32_t nlines, says, held_at_setup_ok = 1;
static void out(const char *s)
{
    if (nlines < BENCH_N + 4u)
        snprintf(lines[nlines], sizeof lines[0], "%s", s);
    nlines++;
}
static void say(const char *a, const char *b) { (void)a; (void)b; says++; }

/* the console's run: a start line, each case once in order (set up only after the ISR held 2 halves), done, then
 * (once the last line has had its time) the power-on state, the audio as usual */
static void test_runner(void)
{
    uint32_t ms = 0, c, ok = 1, restart = 0, steps = 0, held0;
    char want[96], nm[32];
    host_init_live();
    memset(&bench, 0, sizeof bench);
    nlines = says = 0;
    bench_start(out);
    while (!restart && steps++ < 2000000u) {
        uint32_t ph = bench.phase, at = bench.at;
        held0 = bench.held;
        restart = bench_task(ms, out, say);
        if (ph == BP_SETUP && bench.phase != BP_SETUP && held0 - at < 2u)
            held_at_setup_ok = 0;                    /* set up before the ISR held 2 halves */
        fake_half();
        ms += 6;
    }
    ok &= restart && nlines == BENCH_N + 2u;
    snprintf(want, sizeof want, "bench start %u period_us %u\r\n", (unsigned)BENCH_N,
             (unsigned)(HALF_FRAMES * 1000000u / FS));
    ok &= nlines > 0 && !strcmp(lines[0], want);
    for (c = 0; c < BENCH_N && c + 1u < nlines; c++) {
        bench_name(c, nm);
        snprintf(want, sizeof want, "bench %u %s halves %u sum_us %u max_us 1000 late 0\r\n", (unsigned)c, nm,
                 (unsigned)BENCH_MEAS, (unsigned)BENCH_MEAS * 1000u);
        if (strcmp(lines[c + 1u], want)) {
            printf("     line %u: %s     want %s", (unsigned)c + 1u, lines[c + 1u], want);
            ok = 0;
        }
    }
    ok &= nlines == BENCH_N + 2u && !strcmp(lines[BENCH_N + 1u], "bench done\r\n");
    ok &= says == BENCH_N + 1u && held_at_setup_ok && bench.mode == BENCH_OFF && bench.phase == BP_OFF;
    {                                                /* the end: as at power-on (host_init_live) */
        int32_t pa, pb;
        uint32_t ha = render_hash(SECS(0.2), &pa);
        host_init_live();
        ok &= ha == render_hash(SECS(0.2), &pb);
    }
    check("bench run: start line, every case in order (set up while the ISR holds), done, then the power-on state", ok);
}

static void test_calibrated_stress(void)
{
    static const uint8_t models[4] = {DM_M2_GSIN, DM_M2_ENS, DM_M2_GNOI, DM_M2_VOX};
    static const char *const names[4] = {"cal-gsin6-full", "cal-ens6-full", "cal-noise6-full", "cal-vox2-6-full"};
    static const uint8_t expected_template[71] = {
        127,127,127,127,127,127,127,127,
        0,127,127,0,127,64,64,0,
        0,127,0,0,0,32,64,64,
        64,127,127,16,127,63,0,127,
        127,127,127,127,64,100,0,64,0,14,
        0,17,27,0,127,127,0,96,
        0,18,27,0,127,127,0,96,
        0,26,27,0,127,127,0,96,
        0,127,127,127,0
    };
    unsigned ok = NTRK == 6 && BENCH_CAL == BENCH_MORE + 8u && BENCH_N == BENCH_CAL + 4u;
    for (unsigned c = 0; c < 4; ++c) {
        char name[32];
        host_init_live();bench_setup(BENCH_CAL + c);bench_name(BENCH_CAL + c, name);
        ok &= !strcmp(name, names[c]);
        ok &= song.g[G_BPM] == 120 && !song.playing && !transport_req && !song.g[G_CSRC];
        ok &= song.g[G_TAPE] == 127 && song.g[G_ADPC] == 127 && song.g[G_HISS] == 127 &&
              song.g[G_DRIVE] == 127 && song.g[G_RTYPE] == 2;
        for (unsigned pass = 0; pass < 2; ++pass) {
            unsigned active = 0;
            for (unsigned k = 0; k < NTRK; ++k) {
                track_t *t = &trk[k];uint8_t raw[71];
                mm_controls_get(t, raw);
                uint8_t expected[71];memcpy(expected, expected_template, sizeof expected);
                expected[42] = expected[50] = expected[58] = mm_control_track(k);
                ok &= !memcmp(raw, expected, sizeof raw);
                ok &= t->model == models[c] && t->p[P_MODEL] == models[c];
                active += !!t->v[0].active;
                ok &= t->gate_n == GATE_HELD && mmen_state[k].gate && t->p[P_FXG] == 1;
                ok &= !t->p[P_CHOR] && !t->p[P_REV] && !t->p[P_SLCR] && !t->p[P_MUTE] && t->p[P_OUT] == BUS_AB;
                for (unsigned i = 0; i < 8; ++i) ok &= raw[i] == 127;
                ok &= !raw[MMA_ATK] && !raw[MMA_REL] && raw[MMA_HOLD] == 127 && raw[MMA_DEC] == 127;
                ok &= raw[MMA_EQG] == 127 && raw[MMA_DIST] == 127 && raw[MMA_SRR] == 127 && raw[MMA_BITS] == 127;
                ok &= raw[MMA_FOLD] == 127 && raw[MMA_XOR] == 127 && raw[MMA_MODM] == 127 &&
                      raw[MMA_MODD] == 127 && raw[MMA_MODR] == 127 && !raw[MMA_MODT];
                ok &= raw[MMA_DSND] == 127 && raw[MMA_SPU] == 127 && raw[MMA_DFB] == 63 && raw[MMA_DTIM] == 16;
                for (unsigned l = 0; l < 3; ++l) {
                    unsigned q = MMA_LFO + 8 * l;
                    ok &= raw[q] == mm_control_track(k) &&
                          raw[q + 1] == 1 + (l == 0 ? MMA_BASE : l == 1 ? MMA_WDTH : MMA_EQG);
                    ok &= raw[q + 2] == 27 && !raw[q + 3] && raw[q + 4] == 127 && raw[q + 5] == 127 &&
                          !raw[q + 6] && raw[q + 7] == 96;
                }
            }
            ok &= active == 6; /* no hidden voice-cap reduction before or after warmup */
            if (!pass) render_mix(0, 0, SECS(0.5));
        }
        /* Reset reproducibility for the appended workloads, without rerunning
         * every historical case in the optional bounded stress-only invocation. */
        int32_t pa, pb;
        uint32_t a = render_hash(SECS(0.1), &pa);
        bench_reset();bench_setup(BENCH_CAL + c);render_mix(0, 0, SECS(0.5));
        uint32_t b = render_hash(SECS(0.1), &pb);
        ok &= a == b && pa > 0 && pa == pb;
    }
    check("bench: four appended calibrated full-chain cases keep all six voices and exact raw settings", ok);
}

int main(int argc, char **argv)
{
    uint32_t c, d, ok = 1;
    char a[32], b[32];
    if (argc > 1 && !strcmp(argv[1], "stress")) {
        test_calibrated_stress();
        return fails ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "measure")) {
        for (c = 0; c < BENCH_N; c++) {
            double best = 1e18, x;
            int k;
            for (k = 0; k < 3; k++)                  /* the lowest of 3 (scheduling noise) */
                if ((x = measure(c)) < best)
                    best = x;
            bench_name(c, a);
            printf("host %u %s %.1f\n", c, a, best);
        }
        return 0;
    }
    printf("     bench: %u cases\n", (unsigned)BENCH_N);
    for (c = 0; c < BENCH_N; c++) {
        bench_name(c, a);
        ok &= a[0] && strlen(a) < 24u && !strchr(a, ' ');
        for (d = 0; d < c; d++) {
            bench_name(d, b);
            ok &= strcmp(a, b) != 0;
        }
    }
    check("bench: every case has a unique one-word name", ok);
    {
        static const char *const WANT[8] = {"heavy+filter", "heavy+comp", "heavy-fx+comp", "heavy+sidechain",
                                            "heavy-fx+sidechain", "heavy+2acid", "mnm-kit", "mnm-kit+dual"};
        uint32_t w, okn = BENCH_CAL == BENCH_MORE + 8u && BENCH_N == BENCH_CAL + 4u;   /* (MNM-1: + its kit, without and with DUAL) */
        for (w = 0; w < 8u && okn; w++) {
            bench_name(BENCH_MORE + w, a);
            okn &= !strcmp(a, WANT[w]);
        }
        host_init_live();
        bench_setup(BENCH_MORE + 3u);                /* heavy+sidechain: SRC T1, DUCK on 2..8 */
        okn &= song.g[G_CSRC] == 1 && !trk[0].p[P_DUCK] && trk[5].p[P_DUCK] && trk[3].p[P_DIST];
        host_init_live();
        bench_setup(BENCH_MORE + 2u);                /* heavy-fx+comp: no DIST / SLICER / sends, no ducking */
        okn &= song.g[G_CSRC] == 1 && !trk[5].p[P_DUCK] && !trk[3].p[P_DIST] && !trk[3].p[P_REV];
        host_init_live();
        bench_setup(BENCH_MORE + 5u);                /* heavy+2acid: tracks 5 and 6 ACID, the rest the heavy kit */
        okn &= trk[4].p[P_MODEL] == DM_ACID && trk[5].p[P_MODEL] == DM_ACID && trk[3].p[P_MODEL] != DM_ACID &&
               trk[4].step[3].tie && !trk[4].step[3].on && trk[5].step[0].nt == -24 && trk[4].step[1].slide;
        host_init_live();
        bench_setup(BENCH_MORE);
        okn &= trk[5].p[P_FCUT] == 30 && trk[5].p[P_FRESO] == 60;
        check("bench: the filter, compressor and sidechain cases", okn);
    }
    test_calibrated_stress();
    ok = 1;
    for (c = 0; c < BENCH_N; c++) {                  /* the FM-1's reset = host_init_live (live defaults) */
        int32_t pa, pb;
        uint32_t ha, hb;
        host_init_live();
        bench_setup(c);
        ha = render_hash(SECS(1), &pa);
        host_init_live();                            /* a used machine: the heaviest case played a while */
        bench_setup(BENCH_PHYS);
        render_mix(0, 0, SECS(0.5));
        bench_reset();
        bench_setup(c);
        hb = render_hash(SECS(1), &pb);
        if (ha != hb) {
            bench_name(c, a);
            printf("     %s: after bench_reset %08x, after host_init %08x\n", a, hb, ha);
            ok = 0;
        }
        if (!(c == 0 ? pa == 0 : pa > 0)) {          /* idle is silent, every other case sounds */
            bench_name(c, a);
            printf("     %s: peak %d\n", a, (int)pa);
            ok = 0;
        }
    }
    check("bench: bench_reset + a case renders as host_init + it (every case); idle silent, the rest sound", ok);
    test_runner();
    printf(fails ? "bench_host: %d FAILED\n" : "bench_host: all passed\n", fails);
    return fails ? 1 : 0;
}
