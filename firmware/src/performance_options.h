/* SPDX-License-Identifier: GPL-3.0-only
 * Byte-only performance options. Include after the core parameter IDs.
 * These helpers define storage and routing, not controller response laws. */
#ifndef PERFORMANCE_OPTIONS_H
#define PERFORMANCE_OPTIONS_H
#include <stdint.h>
typedef struct {
 uint8_t dest[4];
 int8_t amount[4];
 uint8_t route_flags;
} perf_options_t;
typedef struct {
 uint8_t mode,split_key,first_upper,timing;
 uint8_t attack,decay,sustain,release,portamento,channel;
} multi_options_t;
_Static_assert(sizeof(perf_options_t)==9,"performance wire size");
_Static_assert(sizeof(multi_options_t)==10,"multi wire size");
enum { PERF_HP_OFF=8u, PERF_LP_OFF=16u };
enum { MULTI_OFF,MULTI_ALL,MULTI_SPLIT,MULTI_SEQSTART,MULTI_SEQTRANSPOSE };
static void perf_options_default(perf_options_t *p){*p=(perf_options_t){0};}
static void multi_options_default(multi_options_t *p)
{*p=(multi_options_t){.split_key=60,.first_upper=3,.sustain=127,.channel=6};}
/* Matches musical LFO destinations: SYN/AMP/FILTER/FX and LFO shape/rate/depth.
 * PAGE/DEST/TRIG and cross-track targets are excluded, as are model, timing,
 * routing and performance switches. Callers additionally reject unavailable
 * model-specific SYN knobs using their descriptors before applying a delta. */
static int perf_param_allowed(unsigned id)
{
 if(id>=P_E0&&id<=P_E7)return 1;
 const unsigned base[3]={P_LFO1,P_LFO2,P_LFO3};
 for(unsigned i=0;i<3;i++)if(id>=base[i]+LF_WAVE&&id<=base[i]+LF_DPTH)return 1;
 switch(id){
 case P_LEVEL:case P_PAN:case P_DIST:case P_CHOR:case P_DLY:case P_REV:
 case P_AATK:case P_AHOLD:case P_ADEC:case P_AREL:case P_PORT:
 case P_FCUT:case P_FWDTH:case P_FRESO:case P_FLPQ:case P_FATK:case P_FDEC:case P_FENV:case P_FWOFS:
 case P_EQF:case P_EQG:case P_SRR:case P_DTIM:case P_DFB:case P_DBAS:case P_DWID:
 case P_MVOL:case P_BITS:case P_FOLD:case P_XOR:case P_SPU:
 case P_MODT:case P_MODR:case P_MODD:case P_MODM:case P_WBNK:
 case P_L1PHS:case P_L2PHS:case P_L3PHS:case P_PTCH:return 1;
 default:return 0;
 }
}
static int perf_destination_valid(unsigned dest)
{return !dest||(dest<=P_COUNT&&perf_param_allowed(dest-1));}
static int perf_options_valid(const perf_options_t *p)
{
 if(!p||(p->route_flags&0xE0u)||(p->route_flags&7u)>6)return 0;
 for(unsigned i=0;i<4;i++)if(!perf_destination_valid(p->dest[i])||p->amount[i]<-64||p->amount[i]>63)return 0;
 return 1;
}
static int multi_options_valid(const multi_options_t *p)
{
 return p&&p->mode<=MULTI_SEQTRANSPOSE&&p->split_key<=127&&p->first_upper<6&&p->timing<8&&
 p->attack<=127&&p->decay<=127&&p->sustain<=127&&p->release<=127&&p->portamento<=127&&p->channel<16;
}
/* Includes source exactly once. A malformed edge ends traversal at its source;
 * invalid initial source/null options produce no destinations. No triggers run. */
static uint8_t perf_route_mask(const perf_options_t options[6],unsigned source)
{
 if(!options||source>=6)return 0;
 unsigned mask=0;
 while(!(mask&(1u<<source))){
  mask|=1u<<source;unsigned flags=options[source].route_flags,next=flags&7u;
  if(!next||next>6||(flags&0xE0u))break;
  source=next-1;
 }
 return (uint8_t)mask;
}
#endif
