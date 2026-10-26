/* SPDX-License-Identifier: GPL-3.0-only */
#include "drum_host.h"
#include <assert.h>
int main(void){
 drum_tracks_init();
 for(unsigned bpm=30;bpm<=300;bpm++){
  song.g[G_BPM]=bpm;
  for(unsigned id=10;id<=11;id++){
   unsigned den=id==10?3:6,rem=0,total=0;
   for(unsigned i=0;i<den*100;i++){
    total+=div_period(id,rem);rem=div_rem_next(id,rem);
   }
   assert(total==beat_samples()*100&&!rem);
   assert(div_samples(id)==beat_samples()/den);
   uint32_t d;assert(div_num_den(id,&d)==1&&d==den);
  }
 }
 assert(TP[P_SDIV].max==11&&!strcmp(N_DIV[10],"3/4X")&&!strcmp(N_DIV[11],"3/2X"));
 unsigned seen=0;for(unsigned i=0;i<12;i++){assert(DIV_ORDER[i]<12);seen|=1u<<DIV_ORDER[i];}assert(seen==4095);
 puts("fractional speeds: exact cumulative timing across 271 tempos; legacy IDs retained");
}
