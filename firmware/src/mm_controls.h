/* SPDX-License-Identifier: GPL-3.0-only
 * Raw sound mapping/defaults from MONO2K by Inigo Hernandez,
 * https://github.com/InigoHernandez/mono2k
 * Revision 78d7cdfd676dd8a88a06e6b794fbcfb5fa2b1537.
 * Include after core.h types, before params.c. No model-table dependency.
 * SYN descriptors remain the selected machine's responsibility. Calibrated
 * common controls use raw 0..127; the four existing signed controls use -64..63.
 */
#ifndef MNM_CONTROLS_H
#define MNM_CONTROLS_H
#include <stdint.h>
#define MM_CONTROL_COUNT 71u
static const uint8_t MM_CONTROL_ID[MM_CONTROL_COUNT] = {
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_AATK, P_AHOLD, P_ADEC, P_AREL, P_DIST, P_MVOL, P_PAN, P_PORT,
    P_FCUT, P_FWDTH, P_FRESO, P_FLPQ, P_FATK, P_FDEC, P_FENV, P_FWOFS,
    P_EQF, P_EQG, P_SRR, P_DTIM, P_DLY, P_DFB, P_DBAS, P_DWID,
    P_BITS, P_FOLD, P_XOR, P_SPU, P_PTCH, P_LEVEL, P_ARPM, P_ARPS,
    P_ARPR, P_NLEN, P_L1TRK, P_LFO1+LF_DEST, P_LFO1+LF_WAVE, P_LFO1+LF_TRIG, P_LFO1+LF_MULT, P_LFO1+LF_SPD,
    P_L1PHS, P_LFO1+LF_DPTH, P_L2TRK, P_LFO2+LF_DEST, P_LFO2+LF_WAVE, P_LFO2+LF_TRIG, P_LFO2+LF_MULT, P_LFO2+LF_SPD,
    P_L2PHS, P_LFO2+LF_DPTH, P_L3TRK, P_LFO3+LF_DEST, P_LFO3+LF_WAVE, P_LFO3+LF_TRIG, P_LFO3+LF_MULT, P_LFO3+LF_SPD,
    P_L3PHS, P_LFO3+LF_DPTH, P_MODT, P_MODR, P_MODD, P_MODM, P_WBNK,
};
static const uint8_t MM_CONTROL_DEFAULT[MM_CONTROL_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 64, 64, 64, 64, 64, 0,
    0, 127, 0, 0, 0, 32, 64, 64,
    64, 64, 0, 64, 64, 32, 0, 127,
    0, 0, 0, 0, 64, 100, 0, 64,
    0, 14, 10, 0, 0, 0, 0, 32,
    0, 64, 10, 0, 0, 0, 0, 32,
    0, 64, 10, 0, 0, 0, 0, 32,
    0, 64, 0, 40, 64, 0, 0,
};
static const param_desc_t MM_CONTROL_DESC[MM_CONTROL_COUNT-8] = {
    PD("ATK", F_INT, 0, 127, 0), /* P_AATK */
    PD("HOLD", F_INT, 0, 127, 0), /* P_AHOLD */
    PD("DEC", F_INT, 0, 127, 64), /* P_ADEC */
    PD("REL", F_INT, 0, 127, 64), /* P_AREL */
    PD("DST", F_INT, 0, 127, 64), /* P_DIST */
    PD("VOL", F_INT, 0, 127, 64), /* P_MVOL */
    PD("PAN", F_INT, -64, 63, 0), /* P_PAN */
    PD("PORT", F_INT, 0, 127, 0), /* P_PORT */
    PD("BASE", F_INT, 0, 127, 0), /* P_FCUT */
    PD("WDTH", F_INT, 0, 127, 127), /* P_FWDTH */
    PD("HPQ", F_INT, 0, 127, 0), /* P_FRESO */
    PD("LPQ", F_INT, 0, 127, 0), /* P_FLPQ */
    PD("ATK", F_INT, 0, 127, 0), /* P_FATK */
    PD("DEC", F_INT, 0, 127, 32), /* P_FDEC */
    PD("BOFS", F_INT, -64, 63, 0), /* P_FENV */
    PD("WOFS", F_INT, -64, 63, 0), /* P_FWOFS */
    PD("EQF", F_INT, 0, 127, 64), /* P_EQF */
    PD("EQG", F_INT, 0, 127, 64), /* P_EQG */
    PD("SRR", F_INT, 0, 127, 0), /* P_SRR */
    PD("DTIM", F_INT, 0, 127, 64), /* P_DTIM */
    PD("DSND", F_INT, -64, 63, 0), /* P_DLY */
    PD("DFB", F_INT, 0, 127, 32), /* P_DFB */
    PD("DBAS", F_INT, 0, 127, 0), /* P_DBAS */
    PD("DWID", F_INT, 0, 127, 127), /* P_DWID */
    PD("BITS", F_INT, 0, 127, 0), /* P_BITS */
    PD("FOLD", F_INT, 0, 127, 0), /* P_FOLD */
    PD("XOR", F_INT, 0, 127, 0), /* P_XOR */
    PD("SPU", F_INT, 0, 127, 0), /* P_SPU */
    PD("PTCH", F_INT, 0, 127, 64), /* P_PTCH */
    PD("LVL", F_INT, 0, 127, 100), /* P_LEVEL */
    PD("MODE", F_INT, 0, 127, 0), /* P_ARPM */
    PD("SPEED", F_INT, 0, 127, 64), /* P_ARPS */
    PD("RANGE", F_INT, 0, 127, 0), /* P_ARPR */
    PD("LEN", F_INT, 0, 127, 14), /* P_NLEN */
    PD("TRK", F_INT, 0, 127, 10), /* P_L1TRK */
    PD("DEST", F_INT, 0, 127, 0), /* P_LFO1+LF_DEST */
    PD("WAVE", F_INT, 0, 127, 0), /* P_LFO1+LF_WAVE */
    PD("TRIG", F_INT, 0, 127, 0), /* P_LFO1+LF_TRIG */
    PD("MULT", F_INT, 0, 127, 0), /* P_LFO1+LF_MULT */
    PD("SPD", F_INT, 0, 127, 32), /* P_LFO1+LF_SPD */
    PD("PHASE", F_INT, 0, 127, 0), /* P_L1PHS */
    PD("DPTH", F_INT, 0, 127, 64), /* P_LFO1+LF_DPTH */
    PD("TRK", F_INT, 0, 127, 10), /* P_L2TRK */
    PD("DEST", F_INT, 0, 127, 0), /* P_LFO2+LF_DEST */
    PD("WAVE", F_INT, 0, 127, 0), /* P_LFO2+LF_WAVE */
    PD("TRIG", F_INT, 0, 127, 0), /* P_LFO2+LF_TRIG */
    PD("MULT", F_INT, 0, 127, 0), /* P_LFO2+LF_MULT */
    PD("SPD", F_INT, 0, 127, 32), /* P_LFO2+LF_SPD */
    PD("PHASE", F_INT, 0, 127, 0), /* P_L2PHS */
    PD("DPTH", F_INT, 0, 127, 64), /* P_LFO2+LF_DPTH */
    PD("TRK", F_INT, 0, 127, 10), /* P_L3TRK */
    PD("DEST", F_INT, 0, 127, 0), /* P_LFO3+LF_DEST */
    PD("WAVE", F_INT, 0, 127, 0), /* P_LFO3+LF_WAVE */
    PD("TRIG", F_INT, 0, 127, 0), /* P_LFO3+LF_TRIG */
    PD("MULT", F_INT, 0, 127, 0), /* P_LFO3+LF_MULT */
    PD("SPD", F_INT, 0, 127, 32), /* P_LFO3+LF_SPD */
    PD("PHASE", F_INT, 0, 127, 0), /* P_L3PHS */
    PD("DPTH", F_INT, 0, 127, 64), /* P_LFO3+LF_DPTH */
    PD("TYPE", F_INT, 0, 127, 0), /* P_MODT */
    PD("RATE", F_INT, 0, 127, 40), /* P_MODR */
    PD("DEPTH", F_INT, 0, 127, 64), /* P_MODD */
    PD("MIX", F_INT, 0, 127, 0), /* P_MODM */
    PD("BANK", F_INT, 0, 127, 0), /* P_WBNK */
};
static inline int mm_control_bias(unsigned source)
{
    return source==14 || source==22 || source==23 || source==28 ? 64 : 0;
}
static inline uint8_t mm_control_raw(int value)
{
    return (uint8_t)(value<0 ? 0 : value>127 ? 127 : value);
}
static inline void mm_controls_get(const track_t *t, uint8_t raw[MM_CONTROL_COUNT])
{
    for(unsigned i=0;i<MM_CONTROL_COUNT;++i)
        raw[i]=mm_control_raw((int)t->p[MM_CONTROL_ID[i]]+mm_control_bias(i));
}
static inline void mm_controls_set(track_t *t, const uint8_t raw[MM_CONTROL_COUNT])
{
    for(unsigned i=0;i<MM_CONTROL_COUNT;++i)
        t->p[MM_CONTROL_ID[i]]=(int16_t)((int)mm_control_raw(raw[i])-mm_control_bias(i));
}
/* Only calibrated models should consult these overrides; legacy descriptors stay
 * untouched. NULL means unrelated parameter or machine-owned SYN descriptor. */
static inline const param_desc_t *mm_control_desc(unsigned id)
{
    for(unsigned i=8;i<MM_CONTROL_COUNT;++i)
        if(MM_CONTROL_ID[i]==id) return &MM_CONTROL_DESC[i-8];
    return 0;
}
/* Source enc(track,6), the centre of each quantization interval. There is no ALL
 * target in this format: (raw*6)>>7 always means absolute track 0..5. */
static inline uint8_t mm_control_track(unsigned track)
{
    return (uint8_t)(((track<6 ? track : 5)*128u+64u)/6u);
}
/* Remap a full preset's self targets only. Other targets and within-bin raw
 * values stay byte-identical; loading onto the original track changes nothing. */
static inline void mm_controls_remap(uint8_t raw[MM_CONTROL_COUNT],
                                     unsigned source_track, unsigned selected_track)
{
    if(source_track>=6 || selected_track>=6 || source_track==selected_track) return;
    for(unsigned l=0;l<3;++l) {
        unsigned p=42+l*8;
        if((((unsigned)raw[p]*6u)>>7) == source_track) raw[p]=mm_control_track(selected_track);
    }
}
/* Initialise common sound controls after selecting machine SYN defaults. Source
 * FX machines 11..13 hold open until gate end; all others use HOLD zero.
 * Routing, mute, note, sequencer, model and legacy LFO PAGE/INTL are untouched. */
static inline void mm_controls_init(track_t *t, unsigned track_index, unsigned source_machine)
{
    for(unsigned i=8;i<MM_CONTROL_COUNT;++i)
        t->p[MM_CONTROL_ID[i]]=(int16_t)((int)MM_CONTROL_DEFAULT[i]-mm_control_bias(i));
    if(source_machine>=11 && source_machine<=13) t->p[P_AHOLD]=127;
    t->p[P_L1TRK]=t->p[P_L2TRK]=t->p[P_L3TRK]=mm_control_track(track_index);
}
#endif
