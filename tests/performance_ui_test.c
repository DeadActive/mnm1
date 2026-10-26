/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#include "../firmware/src/performance_ui.h"
static track_t track;
static multi_options_t multi;
int main(void)
{
 perf_options_default(&track.perf);multi_options_default(&multi);
 for(unsigned id=0;id<PO_COUNT;id++)assert(perfui_get(&track,&multi,id)==PO_DESC[id].def);
 for(unsigned id=0;id<PO_COUNT;id++)for(int v=PO_DESC[id].min;v<=PO_DESC[id].max;v++){
  perf_options_t p=track.perf;multi_options_t m=multi;int before[PO_COUNT];
  for(unsigned i=0;i<PO_COUNT;i++)before[i]=perfui_get(&track,&multi,i);
  int changed=perfui_set(&track,&multi,id,v);
  if(id<=PO_KAMT2&&!(id&1u)&&!perf_destination_valid((unsigned)v)){
   assert(!changed&&!memcmp(&p,&track.perf,sizeof p)&&!memcmp(&m,&multi,sizeof m));continue;
  }
  assert(changed==(before[id]!=v));assert(perfui_get(&track,&multi,id)==v);assert(!perfui_set(&track,&multi,id,v));
  for(unsigned i=0;i<PO_COUNT;i++)if(i!=id)assert(perfui_get(&track,&multi,i)==before[i]);
  assert(perf_options_valid(&track.perf)&&multi_options_valid(&multi));
 }
 for(unsigned id=0;id<PO_COUNT;id++){
  perf_options_t p=track.perf;multi_options_t m=multi;
  assert(!perfui_set(&track,&multi,id,PO_DESC[id].min-1));assert(!perfui_set(&track,&multi,id,PO_DESC[id].max+1));
  assert(!memcmp(&p,&track.perf,sizeof p)&&!memcmp(&m,&multi,sizeof m));
 }
 assert(!perfui_set(&track,&multi,PO_COUNT,0));assert(!perfui_get(&track,&multi,PO_COUNT));
 /* A setter owns its own bits even if reserved bits are present in memory. */
 track.perf.route_flags=0xF8;assert(perfui_set(&track,&multi,PO_ROUTE,6));assert(track.perf.route_flags==0xFE);
 assert(perfui_set(&track,&multi,PO_HP,1));assert(track.perf.route_flags==0xF6);
 assert(perfui_set(&track,&multi,PO_LP,1));assert(track.perf.route_flags==0xE6);
 perfui_set(&track,&multi,PO_MCHANNEL,1);assert(multi.channel==0);perfui_set(&track,&multi,PO_MCHANNEL,16);assert(multi.channel==15);
 perfui_set(&track,&multi,PO_MTRACK,1);assert(multi.first_upper==0);perfui_set(&track,&multi,PO_MTRACK,6);assert(multi.first_upper==5);
 puts("performance UI: defaults, values/bounds, whitelist, packed bits and channel/track offsets pass");
}
