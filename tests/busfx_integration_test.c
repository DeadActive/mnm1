/* SPDX-License-Identifier: GPL-3.0-only
 * Full CTL-block mixer coverage for the optional stereo bus effects. */
#include "drum_host.h"
#include <assert.h>
#define FRAMES 8192u
static int32_t dry_l[FRAMES], dry_r[FRAMES], wet_l[FRAMES], wet_r[FRAMES];

static void setup(void)
{
    uint32_t k;
    host_init_live();
    for (k = 0; k < NTRK; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_MUTE] = k != 0;
        trk[k].p[P_CHOR] = trk[k].p[P_DLY] = trk[k].p[P_REV] = 0;
    }
    drum_set_model(&trk[0], DM_MMSAW);
    trk[0].p[P_AHOLD] = trk[0].p[P_ADEC] = 127;
    trk[0].p[P_DIST] = 0;
    song.g[G_TAPE] = song.g[G_ADPC] = song.g[G_HISS] = song.g[G_DRIVE] = 0;
    song.g[G_RTYPE] = 0;
    drum_play(&trk[0], 110, 0);
    trk[0].gate_n = GATE_HELD;
    render_mix(0, 0, CTL);                       /* initialize the bus state */
}
static uint64_t energy(const int32_t *x, uint32_t n)
{
    uint64_t e = 0;
    while (n--) { int64_t a = *x++; assert(a >= -32768 && a <= 32767); e += a * a; }
    return e;
}
static void test_bypass_and_amounts(void)
{
    const uint32_t ids[] = {G_TAPE, G_ADPC, G_HISS, G_DRIVE};
    uint32_t k;
    setup(); render_mix(dry_l, dry_r, FRAMES);
    assert(energy(dry_l, FRAMES) > 100000u);
    setup();
    bfx.tape.wow = 0x123456u;
    bfx.tape.tlp_l = 12345;
    bfx.crunch.ad_pred[0] = -12345;
    bfx.hiss.hs = 17;
    memset(tape_buf, 0x5A, sizeof tape_buf);
    render_mix(wet_l, wet_r, FRAMES);
    assert(!memcmp(dry_l, wet_l, sizeof dry_l) && !memcmp(dry_r, wet_r, sizeof dry_r));
    for (k = 0; k < sizeof ids / sizeof ids[0]; k++) {
        setup(); song.g[ids[k]] = 100;
        if (ids[k] == G_HISS) song.playing = 1;  /* hiss follows rolling transport */
        render_mix(wet_l, wet_r, FRAMES);
        assert(energy(wet_l, FRAMES) > 100000u);
        assert(memcmp(dry_l, wet_l, sizeof dry_l));
        assert(memcmp(dry_l + FRAMES - CTL, wet_l + FRAMES - CTL, CTL * sizeof *wet_l));
    }
}
static void test_stereo_tail(void)
{
    uint32_t i;
    uint64_t diff = 0, tail;
    setup();
    song.g[G_RTYPE] = 2; song.g[G_RSIZE] = 110; song.g[G_RDAMP] = 50;
    trk[0].p[P_REV] = 110;
    render_mix(wet_l, wet_r, FRAMES);
    for (i = 0; i < FRAMES; i++) diff += (uint64_t)abs(wet_l[i] - wet_r[i]);
    assert(diff > 10000u);
    drum_cut(&trk[0]);
    render_mix(wet_l, wet_r, FRAMES);
    tail = energy(wet_l + FRAMES / 2, FRAMES / 2);
    assert(tail > 10000u);                       /* reverb survives the source note */
    assert(bfx.reverb.buffer == rev_comb);
    printf("stereo difference %llu, tail energy %llu\n", (unsigned long long)diff, (unsigned long long)tail);
}
static void test_reverb_switches(void)
{
    uint32_t from, to, i;
    for (from = 0; from < 3; from++) for (to = 0; to < 3; to++) if (from != to) {
        setup(); song.g[G_RTYPE] = from; trk[0].p[P_REV] = 127;
        render_mix(wet_l, wet_r, FRAMES);
        drum_cut(&trk[0]);
        trk[0].p[P_REV] = 0;                  /* exclude the source filter release */
        song.g[G_RTYPE] = to;
        render_mix(wet_l, wet_r, CTL);
        assert(fx.rtype == to);
        for (i = 0; i < sizeof rev_comb / sizeof *rev_comb; i++) assert(rev_comb[i] == 0);
        for (i = 0; i < sizeof rev_ap / sizeof *rev_ap; i++) assert(rev_ap[i] == 0);
        render_mix(wet_l, wet_r, FRAMES);
        energy(wet_l, FRAMES); energy(wet_r, FRAMES);
        for (i = 0; i < CTL; i++) assert(wet[i] == 0 && stereo_wet[i] == 0);
    }
}
int main(void)
{
    assert(CTL == 32);
    test_bypass_and_amounts(); test_stereo_tail(); test_reverb_switches();
    puts("busfx_integration_test: all passed");
    return 0;
}
