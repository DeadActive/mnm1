/* SPDX-License-Identifier: GPL-3.0-only
 * Delay port from MONO2K by Inigo Hernandez, https://github.com/InigoHernandez/mono2k
 * Stereo adapter: two fixed 8192-byte mu-law lines, 44.1 kHz, 16-sample calls.
 * Normal echoes preserve sides; positive send crosses sides on every echo.
 * Fixed stereo layout preserves tails across send changes. Relative to the donor's
 * mono normal mode, long-delay clock reduction starts at half the delay time.
 * Filter tables: round(32767*(1-exp(-2*pi*f/22050))), f=500 (HP) or
 * 900 (LP) * 2**((value-64)/11.5), HP capped at 10kHz, LP unity above 10kHz.
 */
#ifndef MNM_DELAY_H
#define MNM_DELAY_H
#include <stdint.h>
#ifndef MNM_NO_LIBC_HEADERS
#include <string.h>
#endif
#define MMD_BYTES 16384u
#define MMD_LINE 8192u
#define MMD_BLOCK 16u
/* Wide-product helpers MUST remain out of line on pi32v2. Scan final linked
 * disassembly with tools/check_mul64.py; host tests cannot prove scheduling. */
#if defined(__GNUC__) || defined(__clang__)
#define MMD_NOINLINE __attribute__((noinline))
#else
#error "Define a compiler-specific noinline attribute for the delay multiply"
#endif
static MMD_NOINLINE int32_t mmd_mac15(int32_t a, int32_t b, int32_t r)
{
    return (int32_t)(((int64_t)a * b + r) >> 15);
}
static const uint16_t mmd_dly_hp[128] = {
    98, 105, 111, 118, 125, 133, 141, 150, 159, 169, 180, 191,
    203, 215, 228, 243, 258, 274, 290, 308, 328, 348, 369, 392,
    416, 442, 469, 498, 529, 561, 596, 633, 671, 713, 757, 803,
    852, 904, 960, 1018, 1081, 1147, 1216, 1291, 1369, 1452, 1540, 1634,
    1732, 1837, 1947, 2065, 2188, 2319, 2458, 2605, 2759, 2923, 3096, 3278,
    3470, 3674, 3888, 4113, 4351, 4602, 4865, 5142, 5434, 5740, 6061, 6399,
    6752, 7122, 7510, 7915, 8338, 8780, 9240, 9720, 10218, 10736, 11272, 11828,
    12403, 12996, 13606, 14235, 14879, 15540, 16214, 16902, 17601, 18309, 19026, 19748,
    20474, 21200, 21925, 22645, 23357, 24059, 24747, 25419, 26070, 26699, 27303, 27878,
    28423, 28936, 29414, 29857, 30263, 30633, 30871, 30871, 30871, 30871, 30871, 30871,
    30871, 30871, 30871, 30871, 30871, 30871, 30871, 30871,
};
static const uint16_t mmd_dly_lp[128] = {
    177, 188, 200, 212, 225, 239, 254, 270, 286, 304, 323, 343,
    364, 386, 410, 435, 462, 491, 521, 553, 587, 623, 662, 702,
    745, 791, 840, 891, 946, 1004, 1065, 1130, 1199, 1272, 1349, 1431,
    1518, 1610, 1707, 1810, 1919, 2035, 2157, 2286, 2423, 2567, 2720, 2881,
    3052, 3232, 3422, 3622, 3833, 4056, 4291, 4538, 4799, 5072, 5360, 5663,
    5980, 6314, 6663, 7029, 7412, 7813, 8232, 8669, 9125, 9599, 10093, 10606,
    11137, 11688, 12258, 12847, 13453, 14077, 14718, 15374, 16045, 16730, 17426, 18133,
    18848, 19569, 20294, 21020, 21745, 22467, 23181, 23886, 24578, 25254, 25911, 26546,
    27156, 27738, 28291, 28812, 29299, 29750, 30166, 30545, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
};
typedef struct {
    uint32_t write, phase, quiet;
    int32_t time_q8, acc[2], count, previous[2], hp[2], lp[2];
    uint32_t hr[2], lr[2];
    uint8_t pingpong, sleeping;
} mmd_state;
static inline int mmd_p7(int x) { return x < 0 ? 0 : x > 127 ? 127 : x; }
static inline int32_t mmd_decode(uint8_t code)
{
    unsigned u = (uint8_t)~code;
    int32_t x = (int32_t)(((u & 15u) * 8u + 132u) << ((u >> 4) & 7u)) - 132;
    return u & 128u ? -x : x;
}
static inline uint8_t mmd_encode(int32_t x, int32_t gate)
{
    int sign = x < 0 ? 128 : 0;
    /* All callers are bounded to the line/input sum, far from INT32_MIN. */
    int32_t a = x < 0 ? -x : x;
    x = a > 32635 ? 32635 : a;
    x += 132;
    int e = x >= 0x800 ? 4 : 0;
    if (x >= (0x200 << e)) e += 2;
    if (x >= (0x100 << e)) e++;
    uint8_t q = (uint8_t)~(sign | (e << 4) | ((x >> (e + 3)) & 15));
    int32_t d = mmd_decode(q);
    if (a < gate && q != 255 && q != 127 && (d < 0 ? -d : d) >= a) q++;
    return q;
}
static inline int32_t mmd_read(const uint8_t *b, uint32_t pos)
{
    uint32_t j = pos >> 8, f = pos & 255;
    return (mmd_decode(b[j & (MMD_LINE-1)]) * (int32_t)(256-f) +
            mmd_decode(b[(j+1) & (MMD_LINE-1)]) * (int32_t)f) >> 8;
}
static inline int32_t mmd_target(int dtim, uint32_t bpm)
{
    if (bpm < 1) bpm = 1;
    if (bpm > 1000) bpm = 1000;
    /* Half-rate samples Q8: DTIM/256 of a whole note plus 334 full samples. */
    return (int32_t)((mmd_p7(dtim) * ((169344000u / bpm) >> 5) + (167u << 8) + 128u) & ~255u);
}
static inline void mmd_init(mmd_state *s, uint8_t *buffer)
{
    memset(s, 0, sizeof(*s));
    memset(buffer, 127, MMD_BYTES);
    s->quiet = MMD_LINE;
    s->sleeping = 1;
}
/* Inputs are signed-16-bit sample units in int32_t, post-pan/post-level.
 * Outputs are wet only, overwritten (also while asleep). Input/output arrays may
 * alias by side. Buffer must be caller-owned MMD_BYTES, state separately owned.
 * DSND 64 mutes new input and retains mode; raw parameters are clamped to 0..127.
 * Call every 16 samples, including silence, so feedback tails and timing advance. */
static inline void mmd_process(mmd_state *s, uint8_t *buffer,
    const int32_t *in_l, const int32_t *in_r, int32_t *out_l, int32_t *out_r,
    uint32_t bpm, int dtim, int dsnd, int dfb, int dbas, int dwid)
{
    dsnd = mmd_p7(dsnd); dfb = mmd_p7(dfb); dbas = mmd_p7(dbas);
    if (dsnd != 64) s->pingpong = dsnd > 64;
    int sd = dsnd - 64, send = sd * sd * 8;
    int32_t target = mmd_target(dtim, bpm), delta = target - s->time_q8;
    s->time_q8 = delta > -64 && delta < 64 ? target : s->time_q8 + (delta >> 5);
    int quiet = s->quiet >= MMD_LINE;
    for (unsigned i=0; quiet && i<MMD_BLOCK; ++i)
        quiet = !send || (!in_l[i] && !in_r[i]);
    for (unsigned c=0; quiet && c<2; ++c)
        quiet = s->hp[c]>=-3 && s->hp[c]<=3 && s->lp[c]>=-3 && s->lp[c]<=3 &&
                s->previous[c]>=-3 && s->previous[c]<=3 && s->acc[c]>=-3 && s->acc[c]<=3;
    if (quiet) {
        for (unsigned c=0;c<2;++c) s->hp[c]=s->lp[c]=s->previous[c]=s->acc[c]=s->hr[c]=s->lr[c]=0;
        s->count=0; s->sleeping=1;
        for (unsigned i=0;i<MMD_BLOCK;++i) out_l[i]=out_r[i]=0;
        return;
    }
    s->sleeping=0;
    uint32_t th=(uint32_t)s->time_q8>>8, room=MMD_LINE-4;
    uint32_t rate=th<=room ? 65536u : (room<<16)/th;
    uint32_t back=th<=room ? (uint32_t)s->time_q8 : room<<8;
    int32_t fb=dfb*512, gate=dfb<64 ? 1024/(64-dfb) : 0;
    int32_t ahp=mmd_dly_hp[dbas], alp=mmd_dly_lp[mmd_p7(dbas+mmd_p7(dwid))];
    for (unsigned i=0;i<MMD_BLOCK;i+=2) {
        int32_t input[2]={(in_l[i]+in_l[i+1])>>1,(in_r[i]+in_r[i+1])>>1}, e[2];
        uint32_t pos=(s->write<<8)+(s->phase>>8)-back;
        for (unsigned c=0;c<2;++c) {
            int32_t y=mmd_read(buffer+c*MMD_LINE,pos), d=y-s->hp[c];
            s->hp[c]+=mmd_mac15(d,ahp,(int32_t)s->hr[c]);
            s->hr[c]=((uint32_t)d*(uint32_t)ahp+s->hr[c])&32767u;
            d=y-s->hp[c]-s->lp[c];
            s->lp[c]+=mmd_mac15(d,alp,(int32_t)s->lr[c]);
            s->lr[c]=((uint32_t)d*(uint32_t)alp+s->lr[c])&32767u;
            e[c]=s->lp[c];
        }
        for (unsigned c=0;c<2;++c) {
            unsigned source=c ^ s->pingpong;
            s->acc[c]+=mmd_mac15(input[source],send,0)+mmd_mac15(e[source],fb,0);
        }
        ++s->count; s->phase+=rate;
        if (s->phase>=65536u) {
            s->phase-=65536u;
            int loud=0;
            for (unsigned c=0;c<2;++c) {
                uint8_t q=mmd_encode(s->acc[c]/s->count,gate);
                buffer[c*MMD_LINE+(s->write&(MMD_LINE-1))]=q;
                loud |= q!=255 && q!=127;
                s->acc[c]=0;
            }
            s->quiet=loud ? 0 : s->quiet<0x40000000u ? s->quiet+1 : s->quiet;
            ++s->write; s->count=0;
        }
        out_l[i]=(s->previous[0]+e[0])>>1; out_l[i+1]=e[0];
        out_r[i]=(s->previous[1]+e[1])>>1; out_r[i+1]=e[1];
        s->previous[0]=e[0]; s->previous[1]=e[1];
    }
}
#endif
