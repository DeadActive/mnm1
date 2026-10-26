/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#include "../firmware/src/performance_options.h"
static void options(void)
{
 perf_options_t p;multi_options_t m;perf_options_default(&p);multi_options_default(&m);
 assert(sizeof p==9&&sizeof m==10&&perf_options_valid(&p)&&multi_options_valid(&m));
 assert(!m.mode&&m.split_key==60&&m.first_upper==3&&!m.timing&&!m.attack&&!m.decay&&m.sustain==127&&!m.release&&!m.portamento&&m.channel==6);
 const unsigned forbidden[]={P_MODEL,P_MUTE,P_CHOKE,P_NOTE,P_SLEN,P_SDIV,P_SSWING,P_SRC,P_PRIO,P_OUT,P_IN,P_FXG,P_ARPM,P_ARPS,P_ARPR,P_ARPC,P_NLEN,P_LFO1+LF_PAGE,P_LFO2+LF_DEST,P_LFO3+LF_TRIG,P_L1TRK,P_L2TRK,P_L3TRK};
 for(unsigned i=0;i<sizeof forbidden/sizeof *forbidden;i++){p.dest[0]=(uint8_t)(forbidden[i]+1);assert(!perf_options_valid(&p));}
 const unsigned allowed[]={P_E0,P_E7,P_LEVEL,P_PTCH,P_FCUT,P_EQG,P_MODM,P_LFO1+LF_WAVE,P_LFO2+LF_SPD,P_LFO3+LF_DPTH,P_L3PHS};
 for(unsigned i=0;i<sizeof allowed/sizeof *allowed;i++){p.dest[0]=(uint8_t)(allowed[i]+1);assert(perf_options_valid(&p));}
 for(unsigned slot=0;slot<4;slot++){
  perf_options_default(&p);p.dest[slot]=255;assert(!perf_options_valid(&p));p.dest[slot]=(uint8_t)(P_COUNT+1);assert(!perf_options_valid(&p));p.dest[slot]=0;
  for(int amount=-128;amount<=127;amount++){p.amount[slot]=(int8_t)amount;assert(perf_options_valid(&p)==(amount>=-64&&amount<=63));}
 }
 perf_options_default(&p);for(unsigned flags=0;flags<256;flags++){p.route_flags=(uint8_t)flags;assert(perf_options_valid(&p)==(!(flags&0xE0u)&&(flags&7u)<=6));}
 const uint8_t maximum[10]={4,127,5,7,127,127,127,127,127,15};
 for(unsigned i=0;i<10;i++)for(unsigned value=0;value<256;value++){multi_options_default(&m);((uint8_t*)&m)[i]=(uint8_t)value;assert(multi_options_valid(&m)==(value<=maximum[i]));}
 assert(!perf_options_valid(0)&&!multi_options_valid(0));
}
static void routes(void)
{
 perf_options_t p[6]={{0}};
 assert(!perf_route_mask(0,0)&&!perf_route_mask(p,6)&&!perf_route_mask(p,255));
 /* Exhaust every graph with an OFF or one of six destination edges. The
  * oracle grows a reachable set for six rounds, independent of path order. */
 for(unsigned graph=0;graph<117649;graph++){
  unsigned digits=graph;
  for(unsigned k=0;k<6;k++){p[k].route_flags=(uint8_t)((digits%7)|PERF_HP_OFF|PERF_LP_OFF);digits/=7;}
  for(unsigned start=0;start<6;start++){
   unsigned expected=1u<<start;
   for(unsigned round=0;round<6;round++)for(unsigned k=0;k<6;k++)if(expected&(1u<<k)){
    unsigned target=p[k].route_flags&7u;if(target)expected|=1u<<(target-1);
   }
   assert(perf_route_mask(p,start)==expected);
  }
 }
 for(unsigned k=0;k<6;k++)p[k].route_flags=(uint8_t)(k+1);
 for(unsigned k=0;k<6;k++)assert(perf_route_mask(p,k)==(1u<<k));
 p[0].route_flags=2;p[1].route_flags=7;assert(perf_route_mask(p,0)==3);
 p[1].route_flags=0x23;assert(perf_route_mask(p,0)==3);
}
int main(void){options();routes();puts("performance options: byte bounds, whitelist, defaults and all 117649 route graphs pass");}
