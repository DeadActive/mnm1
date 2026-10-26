/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#include "../firmware/src/seq_options_ui.h"
static track_t track;
static pattern_options_t pattern;
static void fields(void)
{
 seq_options_default(&track.seq);
 for(unsigned id=0;id<SO_COUNT;id++)for(int v=SO_DESC[id].min;v<=SO_DESC[id].max;v++){
  int before[SO_COUNT];for(unsigned i=0;i<SO_COUNT;i++)before[i]=seqopt_get(&track,&pattern,i);
  seq_options_t saved=track.seq;pattern_options_t psaved=pattern;
  int changed=seqopt_set(&track,&pattern,id,v);
  if(id==SO_SWING&&v&&v<50){assert(!changed);assert(!memcmp(&saved,&track.seq,sizeof saved)&&!memcmp(&psaved,&pattern,sizeof pattern));continue;}
  assert(changed==(before[id]!=v));assert(seqopt_get(&track,&pattern,id)==v);assert(!seqopt_set(&track,&pattern,id,v));
  for(unsigned i=0;i<SO_COUNT;i++)if(i!=id&&!(id==SO_RSTEP&&(i==SO_RON||i==SO_ROFF))&&!(id==SO_SSTEP&&i==SO_SON))assert(before[i]==seqopt_get(&track,&pattern,i));
 }
 for(unsigned id=0;id<SO_COUNT;id++){
  seq_options_t saved=track.seq;pattern_options_t psaved=pattern;
  assert(!seqopt_set(&track,&pattern,id,SO_DESC[id].min-1));assert(!seqopt_set(&track,&pattern,id,SO_DESC[id].max+1));
  assert(!memcmp(&saved,&track.seq,sizeof saved)&&!memcmp(&psaved,&pattern,sizeof pattern));
 }
 assert(!seqopt_set(&track,&pattern,SO_COUNT,0));assert(seqopt_get(&track,&pattern,SO_COUNT)==0);
}
static void indexed(void)
{
 memset(&track.seq,0,sizeof track.seq);
 for(int n=1;n<=16;n++){seqopt_set(&track,&pattern,SO_RSTEP,n);seqopt_set(&track,&pattern,SO_ROFF,n-9);seqopt_set(&track,&pattern,SO_RON,n%2);}
 for(int n=1;n<=16;n++){seqopt_set(&track,&pattern,SO_RSTEP,n);assert(seqopt_get(&track,&pattern,SO_ROFF)==n-9);assert(seqopt_get(&track,&pattern,SO_RON)==n%2);}
 for(int n=1;n<=64;n++){seqopt_set(&track,&pattern,SO_SSTEP,n);seqopt_set(&track,&pattern,SO_SON,n%3==0);}
 for(int n=1;n<=64;n++){seqopt_set(&track,&pattern,SO_SSTEP,n);assert(seqopt_get(&track,&pattern,SO_SON)==(n%3==0));}
 seq_options_t saved=track.seq;seqopt_set(&track,&pattern,SO_RSTEP,1);seqopt_set(&track,&pattern,SO_SSTEP,1);assert(!memcmp(&saved,&track.seq,sizeof saved));
}
int main(void){fields();indexed();puts("sequence controls: all values/bounds, packed neighbors, indexed masks/offsets and UI-only selectors pass");}
