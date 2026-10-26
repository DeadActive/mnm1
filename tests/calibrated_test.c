/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 calibrated scalar DSP checks; no platform or engine dependencies.
 * cc -O2 -Wall -Wextra -fsanitize=undefined tests/calibrated_test.c -lm -o /tmp/calibrated_test */
#include "../firmware/src/mm_calibrated.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
#define CHECK(label, test) do { if (!(test)) { fprintf(stderr, "FAIL %s (line %d)\n", label, __LINE__); failures++; } } while (0)

static void test_filter(void)
{
    unsigned q, i;
    double worst = 0;
    for (q = 0; q < 128; q++) {
        for (i = 0; i <= MMC_FILTER_TOP; i += 7) {
            mmc_svf_t f = {0};
            double g = (double)mmc_fg[i] / (1u << 24);
            double k = (double)mmc_hpk[q] / (1u << 24);
            double a1 = 1 / (1 + g * (g + k));
            double err;
            mmc_svf_set(&f, mmc_fg[i], mmc_hpk[q]);
            err = fabs((double)f.a1 / (1u << 30) - a1);
            if (err > worst) worst = err;
            CHECK("coefficient a1 matches floating point", err < 1.3e-7);
            CHECK("coefficient a2 matches floating point", fabs((double)f.a2 / (1u << 30) - g * a1) < 1.3e-7);
            CHECK("coefficient a3 matches floating point", fabs((double)f.a3 / (1u << 30) - g * g * a1) < 1.3e-7);
        }
    }
    {
        mmc_svf_t f = {0};
        double g = tan(3.14159265358979323846 * 131.0 / MMC_RATE);
        double damped, undamped;
        int32_t y;
        mmc_svf_set(&f, (uint32_t)llround(g * (1u << 24)), mmc_hpk[127]);
        damped = (double)f.a1 / (1u << 30);
        undamped = 1 / (1 + g * g);
        CHECK("131 Hz high-Q damping survives coefficient quantization", undamped - damped > 5e-6);
        for (i = 0; i < MMC_RATE * 4u; i++) {
            mmc_svf_tick(&f, i ? 0 : 32767, &y, NULL, NULL);
            CHECK("high-Q impulse state bounded", abs(f.s1) <= MMC_STATE_MAX && abs(f.s2) <= MMC_STATE_MAX);
        }
    }
    CHECK("coefficient lookup clamps low", mmc_fg_at(mmc_fg, -1) == mmc_fg[0]);
    CHECK("coefficient lookup clamps high", mmc_fg_at(mmc_fg, INT32_MAX) == mmc_fg[MMC_FILTER_TOP]);
    CHECK("half-position interpolation", mmc_fg_at(mmc_fg, 500 * 256 + 128) == mmc_fg[500] + (mmc_fg[501] - mmc_fg[500]) / 2);
    printf("filter coefficient worst absolute error %.3g\n", worst);
}

static void test_envelopes(void)
{
    static const unsigned values[] = {0, 16, 32, 64, 96, 120, 127};
    unsigned i, bpm;
    for (i = 0; i < sizeof values / sizeof *values; i++) {
        unsigned v = values[i], n = 0;
        double expected = .000903 * exp(.0814 * v) * MMC_RATE;
        mmc_amp_t a = {0};
        mmc_amp_trigger(&a, 0);
        while (a.stage == MMC_ENV_ATTACK && n < MMC_RATE * 40u) {
            mmc_amp_tick(&a, (int32_t)v, 0, 127, 64, 120);
            n++;
        }
        CHECK("AMP attack matches measured law", fabs(n - expected) <= 1.1 + expected * .001);
        a.stage = MMC_ENV_RELEASE;
        a.level = MMC_ENV_ONE;
        n = 0;
        expected = .00829 * exp(.06646 * v) * MMC_RATE;
        while (a.level > MMC_ENV_ONE / 10u && n < MMC_RATE * 50u) {
            mmc_amp_tick(&a, 0, 0, 0, (int32_t)v, 120);
            n++;
        }
        CHECK("AMP release -20 dB matches measured law", fabs(n - expected) <= 2 + expected * .003);
    }
    for (bpm = 60; bpm <= 240; bpm *= 2) {
        mmc_amp_t a = {0};
        unsigned n = 0;
        mmc_amp_trigger(&a, 0);
        while (a.stage == MMC_ENV_ATTACK) mmc_amp_tick(&a, 0, 32, 64, 64, bpm);
        while (a.stage == MMC_ENV_HOLD) { mmc_amp_tick(&a, 0, 32, 64, 64, bpm); n++; }
        CHECK("HOLD 32 is one beat", n == MMC_RATE * 60u / bpm);
        CHECK("HOLD 8 is a sixteenth", abs((int)mmc_hold_samples(8, bpm) - (int)(MMC_RATE * 60u / (bpm * 4u))) <= 1);
    }
    {
        mmc_amp_t a = {MMC_ENV_ONE, UINT32_MAX, MMC_ENV_HOLD, 1, 0};
        mmc_filter_env_t f = {0};
        unsigned n = 0;
        for (i = 0; i < 1024; i++) mmc_amp_tick(&a, 0, 127, 127, 0, 120);
        CHECK("gated HOLD sustains", a.level == MMC_ENV_ONE && a.stage == MMC_ENV_HOLD);
        mmc_amp_release(&a);
        for (i = 0; i < MMC_RATE; i++) mmc_amp_tick(&a, 0, 127, 127, 0, 120);
        CHECK("explicit note-off releases gated HOLD", a.stage == MMC_ENV_IDLE && !a.level);
        mmc_filter_trigger(&f);
        while (f.attack) { mmc_filter_tick(&f, 64, 64); n++; }
        CHECK("filter attack 64 is 121 ms", abs((int)n - (int)(.121 * MMC_RATE)) <= 2);
        n = 0;
        while (f.level) { mmc_filter_tick(&f, 64, 64); n++; }
        CHECK("filter decay 64 is 863 ms", abs((int)n - (int)(.863 * MMC_RATE)) <= 2);
        f.level = MMC_ENV_ONE;
        for (i = 0; i < 100; i++) mmc_filter_tick(&f, 0, 127);
        CHECK("filter decay 127 holds", f.level == MMC_ENV_ONE);
        CHECK("quadratic filter offsets", mmc_filter_offset(32, MMC_ENV_ONE) == 7 * 32 * 32 &&
              mmc_filter_offset(-32, MMC_ENV_ONE) == -7 * 32 * 32);
    }
}

static void test_gate_semantics(void)
{
    mmc_amp_t a = {0};
    unsigned i;
    a.level = MMC_ENV_ONE / 2;
    mmc_amp_trigger(&a, 64);
    CHECK("nonzero attack retriggers from current amplitude", a.level == MMC_ENV_ONE / 2);
    mmc_amp_release(&a);
    CHECK("note-off during attack does not truncate attack", a.stage == MMC_ENV_ATTACK);
    for (i = 0; a.stage == MMC_ENV_ATTACK && i < MMC_RATE * 2u; i++)
        mmc_amp_tick(&a, 64, 127, 127, 64, 120);
    mmc_amp_tick(&a, 64, 127, 127, 64, 120);
    CHECK("held note-off enters release after attack", a.stage == MMC_ENV_RELEASE);
    mmc_amp_trigger(&a, 0);
    CHECK("zero attack retriggers from zero", a.level == 0 && a.gate && !a.let_go);
    while (a.stage == MMC_ENV_ATTACK) mmc_amp_tick(&a, 0, 32, 64, 64, 120);
    mmc_amp_gate_end(&a);
    for (i = 0; i < 100; i++) mmc_amp_tick(&a, 0, 32, 64, 64, 120);
    CHECK("timed gate leaves finite hold running", a.stage == MMC_ENV_HOLD && a.level == MMC_ENV_ONE);
    a.hold = 1;
    mmc_amp_tick(&a, 0, 32, 64, 64, 120);
    mmc_amp_tick(&a, 0, 32, 64, 64, 120);
    CHECK("timed gate leaves finite decay running", a.stage == MMC_ENV_DECAY && a.level < MMC_ENV_ONE);
    mmc_amp_tick(&a, 0, 32, 127, 64, 120);
    CHECK("timed gate releases infinite decay", a.stage == MMC_ENV_RELEASE);
    a.stage = MMC_ENV_HOLD;
    a.hold = UINT32_MAX;
    mmc_amp_tick(&a, 0, 127, 64, 64, 120);
    CHECK("timed gate releases infinite hold", a.stage == MMC_ENV_RELEASE);
}

static void test_laws_and_chain(void)
{
    unsigned i;
    mmc_srr_t s = {0};
    CHECK("DIST 64 and 72 unity", mmc_dist_h[64] == 1u << 24 && mmc_dist_h[72] == 1u << 24);
    CHECK("DIST low setting provides headroom", mmc_dist_h[0] < 1000u);
    CHECK("DIST 80 is 4.58 dB gain", fabs(20 * log10((double)mmc_dist_h[80] / (1u << 24)) - 4.58) < .001);
    CHECK("machine ceiling scales independently of input", mmc_dist_ceiling(100, 2048) == mmc_dist_c[100] / 2);
    CHECK("volume squared", abs((int)mmc_vol[64] - (int)llround(32767 * pow(64.0 / 127, 2))) == 0);
    CHECK("pan endpoints", mmc_pan[0] == 32767 && mmc_pan[127] == 0);
    CHECK("pan constant power", fabs(hypot(mmc_pan[32], mmc_pan[95]) - 32767) < 1.1);
    CHECK("SRR endpoint 2.8 kHz", fabs((double)mmc_srr_inc[127] * MMC_RATE / 65536 - 2813) < 3);
    CHECK("SRR bypass exact", mmc_srr_tick(&s, -12345, 0) == -12345);
    memset(&s, 0, sizeof s);
    for (i = 0; i < 10000; i++) {
        int32_t y = mmc_srr_tick(&s, (int32_t)i, 64);
        CHECK("SRR interpolation bounded on ramp", y >= 0 && y <= (int32_t)i);
    }
    for (i = 0; i < 16; i++) {
        mmc_chain_t c = {0};
        unsigned n;
        int32_t h = i & 1 ? MMC_FILTER_TOP * 256 : 120 * 256;
        int32_t l = i & 2 ? MMC_FILTER_TOP * 256 : 120 * 256;
        mmc_chain_set(&c, h, l, 127, 127, i & 4 ? 127 : 0, i & 8 ? 127 : 0);
        for (n = 0; n < MMC_RATE; n++) {
            int32_t y = mmc_chain_tick(&c, (n & 16) ? 32767 : -32768,
                                       mmc_dist_h[127], 22, mmc_dist_ceiling(127, 5000));
            CHECK("extreme cutoff/Q/EQ chain remains bounded", y >= -MMC_STATE_MAX && y <= MMC_STATE_MAX);
        }
    }
    for (i = 0; i < 128; i += 7) {
        mmc_chain_t c = {0};
        unsigned n;
        mmc_chain_set(&c, 32 * 8 * 256, 60 * 8 * 256, (int)i, 127 - (int)i, (int)i, (int)i);
        for (n = 0; n < 4096; n++) {
            int32_t y = mmc_chain_tick(&c, (n & 16) ? 32767 : -32768, mmc_dist_h[127], 24,
                                       mmc_dist_ceiling(127, 4096));
            CHECK("chain bounded under max drive and resonance", y >= -MMC_STATE_MAX && y <= MMC_STATE_MAX);
        }
    }
}
int main(void)
{
    test_filter();
    test_envelopes();
    test_gate_semantics();
    test_laws_and_chain();
    if (failures) { fprintf(stderr, "%u failures\n", failures); return 1; }
    puts("calibrated_test: all passed");
    return 0;
}
