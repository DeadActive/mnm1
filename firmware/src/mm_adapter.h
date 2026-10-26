/* SPDX-License-Identifier: GPL-3.0-only
 * Ported from MONO2K by Inigo Hernandez (GPL-3.0-only).
 * https://github.com/InigoHernandez/mono2k
 * Source revision 78d7cdfd676dd8a88a06e6b794fbcfb5fa2b1537.
 * Pure 16-sample track adapter at 44100 Hz. Parameters use source indexes.
 * Host supplies effective raw71 parameters, note events, tempo and FX routing.
 * Arpeggiator, three LFOs, locks and delay/bus processors remain host-owned.
 * Delay and SPU outputs are sends, not processing. No allocation or engine state.
 * Buffers: separate 1024 int16 cells for machine and MOD, zeroed on init.
 */
#ifndef MNM_ADAPTER_H
#define MNM_ADAPTER_H
#include "mm_machines.h"
#include "mm_calibrated.h"
#define MMA_HOLD_K (44100u * 60u * 8u / 16u)
#define MMA_MOD_SIDE_HP 378129
enum {
    MMA_SYN = 0,
    MMA_ATK = 8, MMA_HOLD, MMA_DEC, MMA_REL, MMA_DIST, MMA_VOL, MMA_PAN, MMA_PORT,
    MMA_BASE = 16, MMA_WDTH, MMA_HPQ, MMA_LPQ, MMA_FATK, MMA_FDEC, MMA_BOF, MMA_WOF,
    MMA_EQF = 24, MMA_EQG, MMA_SRR, MMA_DTIM, MMA_DSND, MMA_DFB, MMA_DBAS, MMA_DWID,
    MMA_BITS = 32, MMA_FOLD, MMA_XOR, MMA_SPU,
    MMA_PTCH = 36, MMA_LEVL, MMA_AMOD, MMA_ASPD, MMA_ARNG, MMA_GATE,
    MMA_LFO = 42,
    MMA_MODT = 66, MMA_MODR, MMA_MODD, MMA_MODM,
    MMA_WBNK = 70,
    MMA_COUNT = 71
};
typedef struct {
 mm2_machine_t machine;
 uint8_t eff[MMA_COUNT];
 const int32_t *mod_q8; /* caller-owned 71 deltas, valid during render/trigger */
 uint8_t note,gate,vel,sounding,slide,let_go,astage,fstage,mach;
 uint32_t aenv,fenv,bpm;
 int32_t pitch,ptarget,hold,amp0,amp1,gate_left,peak;
 mmc_svf_t eq,lp1,hp2,eqs,lp1s,hp2s;
 uint32_t cached_lp_g,cached_hp_g,cached_eq_g;
 uint8_t cached_eq_gain;
 mmc_op_t hp1,lp2a,lp2b,hp1s,lp2as,lp2bs,mhp1,mhp2;
 int32_t eqc,srr_hold,srr_prev,srr_holds,srr_prevs,msrr_hold,msrr_prev;
 uint32_t srr_acc,mph; uint16_t mw;
 int32_t map[4],mfb; int16_t *mod_buffer;
 int16_t before_volume[16],after_volume[16];
} mma_track_t;
static inline int32_t mma_p8(const mma_track_t *t,int id) {
 int32_t delta=t->mod_q8?t->mod_q8[id]:0;
 return MM2_CLAMP(t->eff[id]*256+MM2_CLAMP(delta,-32512,32512),0,32512);
}
static inline int32_t mma_p7(const mma_track_t *t,int id) { return mma_p8(t,id)>>8; }
static inline int32_t mma_pb8(const mma_track_t *t,int id) { return mma_p8(t,id)-16384; }
static inline int32_t mma_pn(const mma_track_t *t,int id,int n) { return (mma_p7(t,id)*n)>>7; }
/* Bind current or preceding-block modulation before render or note trigger.
 * NULL means no modulation. The caller retains the 71-value array. */
static void mma_modulation(mma_track_t *t,const int32_t *delta_q8) {
 t->mod_q8=delta_q8;t->machine.mod_q8=delta_q8;
 t->machine.wavebank_mod_q8=delta_q8?delta_q8[MMA_WBNK]:0;
}
/* Coefficient copies never touch audio state or rounding remainders. Side
 * filters may have slept while the mid changed; copy when they run again. */
static inline void mma_coefficients_copy(mmc_svf_t *dst,const mmc_svf_t *src) {
 dst->a1=src->a1;dst->a2=src->a2;dst->a3=src->a3;dst->k=src->k;
}
/* All table g values are positive, so zero after init is an invalid cache. */
static inline void mma_coefficients_set(mmc_svf_t *f,uint32_t *cached_g,uint32_t g,uint32_t k) {
 if(*cached_g!=g||(uint32_t)f->k!=k){mmc_svf_set(f,g,k);*cached_g=g;}
}
static void mma_parameters(mma_track_t *t,const uint8_t raw[71]) {
 for(unsigned i=0;i<71;i++)t->eff[i]=raw[i]>127?127:raw[i];
}
static const uint16_t mma_glide[128] = {
    65535, 22903, 21940, 21008, 20105, 19233, 18390, 17577, 16793, 16038, 15311, 14612,
    13941, 13296, 12678, 12084, 11516, 10971, 10449, 9950, 9473, 9017, 8581, 8164,
    7766, 7387, 7025, 6679, 6350, 6036, 5737, 5451, 5180, 4921, 4675, 4441,
    4218, 4006, 3804, 3612, 3429, 3256, 3091, 2934, 2785, 2643, 2509, 2381,
    2259, 2144, 2034, 1930, 1832, 1738, 1649, 1564, 1484, 1408, 1335, 1267,
    1201, 1140, 1081, 1025, 972, 922, 875, 830, 787, 746, 708, 671,
    636, 603, 572, 543, 515, 488, 463, 439, 416, 395, 374, 355,
    336, 319, 302, 287, 272, 258, 244, 232, 220, 208, 198, 187,
    178, 168, 160, 151, 144, 136, 129, 122, 116, 110, 104, 99,
    94, 89, 84, 80, 76, 72, 68, 65, 61, 58, 55, 52,
    49, 47, 44, 42, 40, 38, 36, 34,
};
static const uint16_t mma_dist_m[19] = {
    4096, 4096, 4096, 3486, 4096, 1223, 4981, 1830, 3486, 4240, 2984, 4096, 4096, 4096,
    4096, 4096, 4096, 4096, 4096,
};
static const uint32_t mma_aatk_inc[128] = {
    431413283, 397687501, 366598236, 337939378, 311520929, 287167746, 264718375, 244023986,
    224947384, 207362097, 191151541, 176208248, 162433149, 149734919, 138029375, 127238913,
    117291997, 108122682, 99670179, 91878452, 84695843, 78074736, 71971235, 66344876,
    61158357, 56377296, 51969995, 47907234, 44162081, 40709705, 37527220, 34593525,
    31889172, 29396233, 27098179, 24979776, 23026979, 21226842, 19567431, 18037745,
    16627643, 15327775, 14129524, 13024947, 12006720, 11068094, 10202844, 9405236,
    8669980, 7992204, 7367412, 6791464, 6260541, 5771122, 5319964, 4904075,
    4520699, 4167292, 3841514, 3541203, 3264369, 3009177, 2773934, 2557081,
    2357181, 2172908, 2003041, 1846453, 1702106, 1569044, 1446384, 1333313,
    1229081, 1132997, 1044425, 962777, 887512, 818130, 754173, 695215,
    640867, 590767, 544584, 502011, 462766, 426589, 393241, 362499,
    334161, 308038, 283957, 261758, 241295, 222432, 205043, 189014,
    174238, 160617, 148061, 136486, 125816, 115980, 106914, 98556,
    90851, 83749, 77202, 71166, 65603, 60475, 55747, 51389,
    47372, 43668, 40255, 37108, 34207, 31533, 29068, 26795,
    24700, 22769, 20989, 19349, 17836, 16442, 15156, 13972,
};
static const uint32_t mma_adec_coef[128] = {
    1941621575, 1954243524, 1966128183, 1977314108, 1987838409, 1997736725, 2007043213, 2015790556,
    2024009970, 2031731228, 2038982684, 2045791311, 2052182734, 2058181273, 2063809985, 2069090713,
    2074044128, 2078689779, 2083046140, 2087130655, 2090959790, 2094549076, 2097913150, 2101065809,
    2104020044, 2106788084, 2109381437, 2111810930, 2114086742, 2116218440, 2118215018, 2120084922,
    2121836086, 2123475962, 2125011542, 2126449390, 2127795666, 2129056149, 2130236259, 2131341081,
    2132375383, 2133343633, 2134250024, 2135098483, 2135892691, 2136636100, 2137331942, 2137983247,
    2138592854, 2139163420, 2139697436, 2140197237, 2140665006, 2141102790, 2141512506, 2141895948,
    2142254798, 2142590627, 2142904911, 2143199029, 2143474271, 2143731847, 2143972889, 2144198457,
    2144409542, 2144607074, 2144791920, 2144964896, 2145126762, 2145278231, 2145419970, 2145552604,
    2145676717, 2145792857, 2145901534, 2146003228, 2146098388, 2146187433, 2146270755, 2146348723,
    2146421680, 2146489949, 2146553829, 2146613604, 2146669537, 2146721875, 2146770848, 2146816674,
    2146859554, 2146899677, 2146937222, 2146972352, 2147005225, 2147035984, 2147064766, 2147091697,
    2147116897, 2147140477, 2147162541, 2147183187, 2147202505, 2147220581, 2147237495, 2147253322,
    2147268131, 2147281988, 2147294954, 2147307086, 2147318439, 2147329061, 2147339001, 2147348301,
    2147357004, 2147365146, 2147372766, 2147379895, 2147386566, 2147392808, 2147398649, 2147404114,
    2147409228, 2147414013, 2147418491, 2147422680, 2147426600, 2147430268, 2147433700, 2147436912,
};
static const uint32_t mma_fatk_inc[128] = {
    389566195, 368546370, 348660712, 329848024, 312050413, 295213107, 279284293, 264214949,
    249958702, 236471679, 223712376, 211641526, 200221983, 189418605, 179198144, 169529149,
    160381864, 151728139, 143541343, 135796283, 128469122, 121537313, 114979524, 108775573,
    102906369, 97353850, 92100927, 87131437, 82430086, 77982405, 73774709, 69794046,
    66028169, 51697238, 40476731, 31691553, 24813133, 23184072, 21661964, 20239787,
    18910980, 17841753, 16832979, 15881241, 14983315, 13765058, 12645854, 11617650,
    10673046, 9982332, 9336318, 8732111, 8167006, 7567744, 7012453, 6497906,
    6021116, 5567905, 5148808, 4761256, 4402876, 4071471, 3765010, 3481617,
    3219555, 2899007, 2610374, 2350478, 2116458, 1905737, 1715996, 1545147,
    1391308, 1275835, 1169946, 1072845, 983803, 902152, 827277, 758616,
    695654, 639356, 587614, 540059, 496353, 456184, 419266, 385336,
    354151, 325129, 298485, 274025, 251569, 230953, 212027, 194651,
    178700, 164345, 151142, 139001, 127835, 117565, 108121, 99435,
    91447, 84094, 77333, 71115, 65396, 60138, 55303, 50856,
    46767, 43042, 39614, 36460, 33556, 30884, 28424, 26160,
    24077, 22151, 20380, 18750, 17250, 15870, 14601, 13433,
};
static const uint32_t mma_fdec_dec[128] = {
    16243709, 15359282, 14523009, 13732270, 12984584, 12277607, 11609124, 10977038,
    10379367, 9814238, 9279878, 8774613, 8296859, 7845116, 7417971, 7014082,
    6632183, 6271078, 5929635, 5606782, 5301507, 5012854, 4739917, 4481841,
    4237817, 4007079, 3788904, 3582608, 3387544, 3203102, 3028701, 2863796,
    2707870, 2560434, 2421025, 2289206, 2164565, 2046710, 1935272, 1829902,
    1730268, 1636060, 1546981, 1462752, 1383109, 1307802, 1236596, 1169266,
    1105603, 1045406, 988486, 934666, 883776, 835656, 790157, 747135,
    706455, 667991, 631621, 597230, 564713, 533966, 504893, 477403,
    451409, 426831, 403591, 381617, 360839, 341192, 322615, 305050,
    288440, 272736, 257886, 243845, 230568, 218014, 206144, 194920,
    184307, 174272, 164783, 155811, 147328, 139306, 131721, 124549,
    117768, 111356, 105293, 99560, 94139, 89014, 84167, 79584,
    75251, 71154, 67280, 63617, 60153, 56878, 53781, 50853,
    48084, 45466, 42990, 40650, 38436, 36344, 34365, 32494,
    30724, 29052, 27470, 25974, 24560, 23223, 21958, 20763,
    19632, 18563, 17553, 16597, 15693, 14839, 14031, 0,
};
static const uint32_t mma_mod_inc[128] = {
    9739, 10099, 10472, 10858, 11259, 11675, 12106, 12553,
    13017, 13498, 13996, 14513, 15049, 15604, 16181, 16778,
    17398, 18040, 18706, 19397, 20113, 20856, 21626, 22425,
    23253, 24111, 25002, 25925, 26882, 27875, 28904, 29972,
    31078, 32226, 33416, 34650, 35929, 37256, 38632, 40058,
    41538, 43072, 44662, 46311, 48021, 49795, 51633, 53540,
    55517, 57567, 59693, 61897, 64183, 66553, 69010, 71559,
    74201, 76941, 79782, 82728, 85783, 88951, 92236, 95642,
    99173, 102836, 106633, 110571, 114654, 118887, 123277, 127830,
    132550, 137445, 142520, 147783, 153240, 158898, 164766, 170850,
    177159, 183701, 190485, 197519, 204812, 212375, 220218, 228349,
    236782, 245525, 254592, 263993, 273741, 283850, 294331, 305200,
    316470, 328156, 340274, 352839, 365868, 379378, 393387, 407914,
    422977, 438596, 454792, 471586, 489000, 507057, 525781, 545196,
    565328, 586204, 607851, 630296, 653571, 677705, 702731, 728680,
    755588, 783489, 812421, 842421, 873529, 905785, 939233, 973915,
};
static void mma_env_block(mma_track_t *t)
{
    switch (t->astage) {
    case 1:
        t->aenv += mma_aatk_inc[t->eff[MMA_ATK]];
        if (t->aenv >= (1u << 30)) {
            t->aenv = 1u << 30;
            t->astage = 2;
            int h = t->eff[MMA_HOLD];
            t->hold = h == 127 ? -1 : (int32_t)((((uint32_t)h * MMA_HOLD_K) / t->bpm + 128) >> 8);
        }
        break;
    case 2:
        if (t->hold < 0) { if (!t->gate) t->astage = 4; }
        else if (t->let_go) t->astage = 4;
        else if (t->hold-- <= 0) t->astage = 3;
        break;
    case 3: {
        if (t->let_go) { t->astage = 4; break; }
        int d = mma_p7(t, MMA_DEC);
        if (d < 127) t->aenv = mm2_mulu_hi(t->aenv, mma_adec_coef[d], 31);
        else if (!t->gate) t->astage = 4;
        if (t->aenv < (1u << 15)) { t->aenv = 0; t->astage = 0; }
        break;
    }
    case 4:
        t->aenv = mm2_mulu_hi(t->aenv, mma_adec_coef[mma_p7(t, MMA_REL)], 31);
        if (t->aenv < (1u << 15)) { t->aenv = 0; t->astage = 0; }
        break;
    default:
        t->aenv = 0;
        break;
    }
    if (t->astage == 0) t->sounding = 0;
    if (t->fstage == 1) {
        t->fenv += mma_fatk_inc[t->eff[MMA_FATK]];
        if (t->fenv >= (1u << 30)) { t->fenv = 1u << 30; t->fstage = 2; }
    } else if (t->fstage == 2) {
        uint32_t d = mma_fdec_dec[mma_p7(t, MMA_FDEC)];
        t->fenv = t->fenv > d ? t->fenv - d : 0;
    }
}
static inline int32_t mma_mod_tick(mma_track_t *t, int16_t *buf, int32_t x, int type, uint32_t inc, int32_t dep, int32_t mix,
                               int32_t *side)
{
    t->mph += inc;
    int32_t tri = (int32_t)(t->mph >> 16);
    tri = tri < 32768 ? tri : 65535 - tri;
    int32_t y;
    if (type == 2) {
        int32_t a = 3277 + ((tri * (dep * 206)) >> 15);
        int32_t v = x + (t->mfb >> 1);
        for (int k = 0; k < 4; k++) {
            int32_t u = v - ((a * t->map[k]) >> 15);
            v = (int32_t)mm2_mul_sh(a, u, 15) + t->map[k];
            t->map[k] = MM2_CLAMP(u, -65536, 65536);
        }
        y = v;
        t->mfb = MM2_CLAMP(y, -65536, 65536);
    } else {
        int32_t ys[2];
        for (int c = 0; c < 2; c++) {
            int32_t tr = c ? 32767 - tri : tri;
            int32_t d8 = type == 0 ? (309 << 8) + ((tr * (dep * 441)) >> 14)
                                   : (20 << 8) + ((tr * (dep * 200)) >> 14);
            int32_t pos = ((int32_t)t->mw << 8) - d8;
            uint32_t j = (uint32_t)(pos >> 8);
            int32_t f = pos & 255;
            ys[c] = (buf[j & (MM2_DELAY_CELLS - 1)] * (256 - f) + buf[(j + 1) & (MM2_DELAY_CELLS - 1)] * f) >> 8;
        }
        int32_t w = type == 1 ? x + ((ys[0] * 18000) >> 15) : x;
        buf[t->mw & (MM2_DELAY_CELLS - 1)] = (int16_t)mm2_sat16(w);
        t->mw++;
        y = (ys[0] + ys[1]) >> 1;
        *side = (((ys[0] - ys[1]) >> 1) * mix) >> 8;
        return x + (((y - x) * mix) >> 8);
    }
    *side = 0;
    return x + (((y - x) * mix) >> 8);
}
static inline int32_t mma_dist_in(int32_t b, int32_t g, int sh)
{
    return (b * (g >> 16) + ((b * (g & 0xFFFF)) >> 16)) >> (sh - 16);
}

static void mma_chain_filter(int32_t *x, int eq_on, int32_t eqc, int32_t ceil, mmc_svf_t *eq, mmc_svf_t *lp1, mmc_svf_t *hp2,
                         mmc_op_t *hp1, mmc_op_t *lp2a, mmc_op_t *lp2b)
{
    if (eq_on) {
        mmc_svf_t f = *eq;
        for (int i = 0; i < MM2_BLOCK; i++) {
            int32_t b;
            mmc_svf_tick(&f, x[i], 0, &b, 0);
            int32_t y = x[i] + (int32_t)mm2_mul_sh(b, eqc, 24);
            x[i] = MM2_CLAMP(y, -(1 << 26), 1 << 26);
        }
        *eq = f;
    }
    {
        mmc_svf_t f = *lp1;
        for (int i = 0; i < MM2_BLOCK; i++) { int32_t v; mmc_svf_tick(&f, x[i], &v, 0, 0); x[i] = MM2_CLAMP(v, -ceil, ceil); }
        *lp1 = f;
    }
    {
        mmc_svf_t f = *hp2;
        for (int i = 0; i < MM2_BLOCK; i++) {
            int32_t u;
            mmc_svf_tick(&f, x[i], 0, 0, &u);
            f.s1 = MM2_CLAMP(f.s1, -ceil, ceil);
            f.s2 = MM2_CLAMP(f.s2, -ceil, ceil);
            x[i] = u;
        }
        *hp2 = f;
    }
    mmc_op_t h = *hp1, la = *lp2a, lb = *lp2b;
    for (int i = 0; i < MM2_BLOCK; i++) { int32_t u = x[i]; u -= mmc_op_lp(&h, u); x[i] = mmc_op_lp(&lb, mmc_op_lp(&la, u)); }
    *hp1 = h; *lp2a = la; *lp2b = lb;
}
static void mma_init(mma_track_t *t,unsigned track,unsigned machine,int16_t *machine_buffer,int16_t *mod_buffer) {
 *t=(mma_track_t){0};t->bpm=120;t->gate_left=-1;t->mach=(uint8_t)machine;
 t->mod_buffer=mod_buffer;
 mm2_machine_init(&t->machine,track,machine_buffer);
 for(unsigned i=0;mod_buffer&&i<1024;i++)mod_buffer[i]=0;
}
/* Timed gates count control blocks; -1 means explicitly held. */
static void mma_gate_end(mma_track_t *t,int explicit_off) {
 t->gate=0;t->gate_left=-1;if(explicit_off)t->let_go=1;
}
enum { MMA_NOAMP=1u, MMA_NOFLT=2u };
/* Independent trigger lanes. Note/velocity always update as in MONO2K.
 * Unlike its slide shortcut, NOAMP never reopens the gate: a filter-only
 * sliding event may restart FLT while preserving the running AMP lifetime. */
static void mma_trigger_mask(mma_track_t *t,const uint8_t raw[71],unsigned note,unsigned velocity,
                             int slide,int32_t gate_blocks,unsigned skip) {
 mma_parameters(t,raw);t->note=note>127?127:note;t->ptarget=t->note*64;
 t->vel=velocity>127?127:velocity;
 if(!(skip&MMA_NOAMP))t->gate_left=gate_blocks;
 if(slide&&t->sounding){
  t->slide=1;
  if(!(skip&MMA_NOAMP)){t->gate=1;return;}
 }else{
  t->slide=0;if(!t->sounding||!t->eff[MMA_PORT])t->pitch=t->ptarget;
 }
 if(!(skip&MMA_NOAMP)){
  t->gate=1;t->let_go=0;t->sounding=1;t->astage=1;
  if(!t->eff[MMA_ATK])t->aenv=0;
  mm2_machine_trigger(&t->machine,t->mach,t->eff);
 }
 if(!(skip&MMA_NOFLT)){t->fstage=1;t->fenv=0;}
}
static void mma_trigger(mma_track_t *t,const uint8_t raw[71],unsigned note,unsigned velocity,int slide,int32_t gate_blocks) {
 mma_trigger_mask(t,raw,note,velocity,slide,gate_blocks,0);
}
/* FX input is pre-VOL for chorus/dynamics and post-VOL/clip/LEVEL for ring.
 * previous_pitch_q6 is preceding track's pitch (FM external frequency).
 * external_carrier is ring's separate two-tracks-back pre-volume source.
 * L/R are PCM16-scale int32, overwritten. Sends and taps also overwritten.
 * Returns 0 for missing buffers/invalid machine; returns 1 for valid silence.
 * Reinitialize explicitly when changing machine; do not reinit each trigger. */
static int mma_render(mma_track_t *t,const uint8_t raw[71],unsigned bpm,unsigned accent,
 int32_t previous_pitch_q6,const int16_t *input,const int16_t *external_carrier,
 int32_t L[16],int32_t R[16],int32_t dsend[16],int32_t rsend[16]) {
 mma_parameters(t,raw);mma_modulation(t,t->mod_q8);t->bpm=bpm?bpm:1;accent=accent>127?127:accent;
 int m=t->mach;int16_t machine_out[16],machine_side[16];
 for(unsigned i=0;i<16;i++)L[i]=R[i]=dsend[i]=rsend[i]=t->before_volume[i]=t->after_volume[i]=0;
 if((unsigned)m>=MM2_COUNT || ((m==MM2_ENS||m==MM2_FXC)&&!t->machine.delay) || (mma_p7(t,MMA_MODM)&&!t->mod_buffer))return 0;
    if (t->gate_left > 0 && --t->gate_left == 0) { t->gate = 0; t->gate_left = -1;  }
    mma_env_block(t);

    if (!t->sounding && t->astage == 0) {
        t->amp0 = t->amp1 = 0;
        t->peak = 0;
        return 1;
    }

    int g = t->slide && t->eff[MMA_PORT] == 0 ? 40 : mma_p7(t, MMA_PORT);
    int32_t gd = t->ptarget - t->pitch;
    int32_t gs = gd * mma_glide[g];
    t->pitch += gd > 0 ? (gs + 65535) >> 16 : gs >> 16;
    int32_t pitch = t->pitch + (mma_pb8(t, MMA_PTCH) >> 2);
    mm2_machine_render(&t->machine,m,t->eff,pitch,previous_pitch_q6,t->eff[MMA_WBNK],machine_out,machine_side,input,0,external_carrier);

    int32_t fe = (int32_t)(t->fenv >> 15);
    int32_t db = mma_pb8(t, MMA_BOF), dw = mma_pb8(t, MMA_WOF);
    int32_t ob = (((7 * db * (db < 0 ? -db : db)) >> 16) * fe) >> 15;
    int32_t ow = (((7 * dw * (dw < 0 ? -dw : dw)) >> 16) * fe) >> 15;
    int32_t sb = mma_p8(t, MMA_BASE) + ob, sl = sb + mma_p8(t, MMA_WDTH) + ow;
    int32_t key = t->note * 8 * 256;
    if (m == MM2_GSIN || m == MM2_GNOI) key += (mma_pb8(t, MMA_SYN + 7) >> 7) * 32;
    int32_t ph = key + 12 * (sb - 16 * 256), pl = key + 12 * (sl - 16 * 256);
    int lq = mma_p7(t, MMA_LPQ), hq = mma_p7(t, MMA_HPQ);
    mma_coefficients_set(&t->lp1, &t->cached_lp_g, mmc_fg_at(mmc_fg, pl + mmc_lpc[lq]), mmc_lpk[lq]);
    mma_coefficients_set(&t->hp2, &t->cached_hp_g, mmc_fg_at(mmc_fg, ph + mmc_hpc[hq]), mmc_hpk[hq]);
    t->lp2a.g = t->lp2b.g = (int32_t)mmc_fg_at(mmc_fg1, pl + MMC_LP2_OFFSET * 256);
    t->hp1.g = (int32_t)mmc_fg_at(mmc_fg1, ph + MMC_HP1_OFFSET * 256);
    int side = (m == MM2_ENS && mma_p7(t, MMA_SYN + 5) && mma_p7(t, MMA_SYN + 6))
               || (m == MM2_GNOI && mma_p7(t, MMA_SYN + 2) >= 64 && mma_p7(t, MMA_SYN + 0));
    if (side) {
        mma_coefficients_copy(&t->lp1s, &t->lp1);
        mma_coefficients_copy(&t->hp2s, &t->hp2);
        t->lp2as.g = t->lp2bs.g = t->lp2a.g;
        t->hp1s.g = t->hp1.g;
    }
    int32_t d8 = mma_p8(t, MMA_DIST), di = d8 >> 8, df = d8 & 255, dn = di < 127 ? di + 1 : 127;
    int32_t dh = (int32_t)(mmc_dist_h[di] + mm2_mulu_hi(mmc_dist_h[dn] - mmc_dist_h[di], (uint32_t)df, 8));
    int32_t ceil = ((mmc_dist_c[di] + (((mmc_dist_c[dn] - mmc_dist_c[di]) * df) >> 8)) * mma_dist_m[m]) >> 12;
    int eqv = mma_p7(t, MMA_EQG), eq_on = eqv != 64;
    if (eq_on) {
        int gain_changed = !t->cached_eq_g || t->cached_eq_gain != eqv;
        uint32_t k = (uint32_t)t->eq.k;
        if (gain_changed) {
            uint32_t gb = mmc_eqgain[eqv];
            k = eqv > 64 ? MMC_EQ_K : mm2_mulu_hi(MMC_EQ_K, gb, 16);
            int32_t c = (int32_t)mm2_mul_sh((int32_t)(gb - 65536), MMC_EQ_K, 16);
            t->eqc = eqv > 64 ? c : -c;
            t->cached_eq_gain = (uint8_t)eqv;
        }
        mma_coefficients_set(&t->eq, &t->cached_eq_g, mmc_eqg[mma_p7(t, MMA_EQF)], k);
        if (side) mma_coefficients_copy(&t->eqs, &t->eq);
    }
    t->amp0 = t->amp1;
    t->amp1 = (int32_t)(t->aenv >> 15);
    int32_t vol = mmc_volk[mma_p7(t, MMA_VOL)];
    int32_t acc = 256 + (t->vel > 100 ? (accent * 2) : 0);
    vol = (vol * acc) >> 8;
    int32_t lev = mmc_vol[mma_p7(t, MMA_LEVL)];
    int srr = mma_p7(t, MMA_SRR);
    uint32_t sinc = mmc_srr_inc[srr];
    int bits = 16 - (mma_p7(t, MMA_BITS) * 14 >> 7);
    int32_t fold = mma_p7(t, MMA_FOLD), xr = mma_p7(t, MMA_XOR), xm = (xr * xr * 2) & 0x7FFF;
    int pan = mma_p7(t, MMA_PAN);
    int32_t gl = mmc_pan[pan], gr = mmc_pan[127 - pan];
    int32_t sd = mma_p7(t, MMA_DSND) - 64, ds = sd * sd * 8;
    int32_t rs = mmc_vol[mma_p7(t, MMA_SPU)];
    int mtype = mma_pn(t, MMA_MODT, 3), mmix = mma_p7(t, MMA_MODM) * 2, mdep = mma_p7(t, MMA_MODD);
    uint32_t minc = mma_mod_inc[mma_p7(t, MMA_MODR)];
    int32_t pk = 0;
    int ish = m == MM2_GNOI ? 24 - 2 : m == MM2_FXR ? 24 - 1 : 24;

    int32_t x[MM2_BLOCK], xs[MM2_BLOCK], msd[MM2_BLOCK];
    for (int i = 0; i < MM2_BLOCK; i++) x[i] = MM2_CLAMP(mma_dist_in(machine_out[i], dh, ish), -(1 << 24), 1 << 24);
    mma_chain_filter(x, eq_on, t->eqc, ceil, &t->eq, &t->lp1, &t->hp2, &t->hp1, &t->lp2a, &t->lp2b);
    if (side) {
        for (int i = 0; i < MM2_BLOCK; i++) xs[i] = MM2_CLAMP(mma_dist_in(machine_side[i], dh, ish), -(1 << 24), 1 << 24);
        mma_chain_filter(xs, eq_on, t->eqc, ceil, &t->eqs, &t->lp1s, &t->hp2s, &t->hp1s, &t->lp2as, &t->lp2bs);
    }
    int32_t a0 = t->amp0, da = t->amp1 - t->amp0;
    for (int i = 0; i < MM2_BLOCK; i++) {
        int32_t s = x[i];
        if (fold) s = mm2_fold16((mm2_sat16(s) * (256 + fold * 6)) >> 8);
        if (xr) s = (int32_t)(int16_t)((uint16_t)mm2_sat16(s) ^ (uint16_t)xm);
        x[i] = (int32_t)mm2_mul_sh(s, a0 + ((da * i) >> 4), 15);
    }
    if (mmix) {
        int16_t *mb = t->mod_buffer;
        for (int i = 0; i < MM2_BLOCK; i++) x[i] = mma_mod_tick(t, mb, x[i], mtype, minc, mdep, mmix, &msd[i]);
        t->mhp1.g = t->mhp2.g = MMA_MOD_SIDE_HP;
    }
    int16_t *to = t->before_volume, *tv = t->after_volume;
    for (int i = 0; i < MM2_BLOCK; i++) {
        int32_t s = x[i];
        to[i] = (int16_t)mm2_sat16(s);
        s = (int32_t)mm2_mul_sh(s, vol, 15);
        if (srr) {
            t->srr_acc += sinc;
            if (t->srr_acc >= 65536u) { t->srr_acc -= 65536u; t->srr_prev = t->srr_hold; t->srr_hold = s; }
            s = t->srr_prev + (int32_t)mm2_mul_sh(t->srr_hold - t->srr_prev, (int32_t)t->srr_acc, 16);
        }
        if (bits < 16) s = (mm2_sat16(s) >> (16 - bits)) * (1 << (16 - bits));
        int32_t sd = 0;
        if (m == MM2_FXC) sd = (int32_t)mm2_mul_sh((machine_side[i] * (a0 + ((da * i) >> 4))) >> 15, vol, 15);
        if (side) {
            int32_t ss = (int32_t)mm2_mul_sh(xs[i], a0 + ((da * i) >> 4), 15);
            ss = (int32_t)mm2_mul_sh(ss, vol, 15);
            if (srr) {
                if (t->srr_acc < sinc) { t->srr_prevs = t->srr_holds; t->srr_holds = ss; }
                ss = t->srr_prevs + (int32_t)mm2_mul_sh(t->srr_holds - t->srr_prevs, (int32_t)t->srr_acc, 16);
            }
            if (bits < 16) ss = (mm2_sat16(ss) >> (16 - bits)) * (1 << (16 - bits));
            sd = (int32_t)mm2_mul_sh(ss, mmc_pan[64], 15);
        }
        if (mmix && mtype != 2) {
            int32_t ms = (int32_t)mm2_mul_sh(msd[i], vol, 15);
            ms -= mmc_op_lp(&t->mhp1, ms);
            ms -= mmc_op_lp(&t->mhp2, ms);
            if (srr) {
                if (t->srr_acc < sinc) { t->msrr_prev = t->msrr_hold; t->msrr_hold = ms; }
                ms = t->msrr_prev + (int32_t)mm2_mul_sh(t->msrr_hold - t->msrr_prev, (int32_t)t->srr_acc, 16);
            }
            if (bits < 16) ms = (mm2_sat16(ms) >> (16 - bits)) * (1 << (16 - bits));
            sd += (int32_t)mm2_mul_sh(ms, mmc_pan[64], 15);
        }
        uint64_t lr = mm2_mul2_q15(s, gl, gr);
        int32_t l = (int32_t)MM2_CLAMP((int64_t)(int32_t)lr + sd, -MMC_TRACK_CLIP, MMC_TRACK_CLIP);
        int32_t r = (int32_t)MM2_CLAMP((int64_t)(int32_t)(lr >> 32) - sd, -MMC_TRACK_CLIP, MMC_TRACK_CLIP);
        int32_t as = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        if (ar > as) as = ar;
        if (as > pk) pk = as;
        L[i] += (l * lev) >> 15;
        R[i] += (r * lev) >> 15;
        int32_t mm = MM2_CLAMP(s, -MMC_TRACK_CLIP, MMC_TRACK_CLIP);
        tv[i] = (int16_t)((mm * lev) >> 15);
        dsend[i] = (mm * ds) >> 15;
        rsend[i] += (((mm * rs) >> 15) * lev) >> 15;
    }
    t->peak = pk;
return 1;
}
#endif
