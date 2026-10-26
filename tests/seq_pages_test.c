/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_DRAW 1
#include "ui_host.h"
#include <assert.h>
static void page(const char *title)
{
 for(unsigned i=0;i<NPAGES;i++)if(!strcmp(PAGES[i].title,title)){
  ui.page=(uint8_t)i;ui.home=0;return;
 }
 assert(!"missing sequencer page");
}
static void knob(unsigned slot,int amount)
{
 host_ticks+=1000000u*FM1_TICKS_PER_US;edit_param(slot,amount);
}
int main(void)
{
 ui_host_init();page("ARP MODE");knob(0,1);
 assert((TSEL->seq.arp_mode_order&7)==MMA_MODE_KEY);
 mmarp_held_on(TSEL,60);assert(mmarp_nheld[0]);knob(0,1);
 assert((TSEL->seq.arp_mode_order&7)==MMA_MODE_SID&&!mmarp_nheld[0]&&!TSEL->arp_on);
 int16_t *a,*b;page_desc(cur_page(),0,&a);page_desc(cur_page(),1,&b);
 assert(a!=b&&*a==MMA_MODE_SID&&*b==0);
 page("ARP TRIG");knob(1,-1);knob(2,-1);knob(3,-1);assert((TSEL->seq.arp_flags&7)==7);
 page("ARP RHYTHM");knob(1,1);assert(seqopt_rhythm_step==1);knob(2,-1);knob(3,-1);
 assert(!(TSEL->seq.arp_mask[0]&2)&&TSEL->seq.arp_offset[1]==-1);
 page("SWING MASK");knob(0,1);assert(song.seq.swing==50);knob(0,-1);assert(!song.seq.swing);
 knob(0,1);knob(0,1);assert(song.seq.swing==51);knob(2,1);assert(TSEL->seq.swing[0]&1);
 chain.running=1;knob(0,1);assert(song.seq.swing==51);chain.running=0;
 page("TRANSPOSE");knob(0,1);knob(3,-1);assert(TSEL->seq.transpose==1&&song.seq.transpose==-1);
 track_clear(TSEL);for(unsigned i=0;i<8;i++)assert(TSEL->seq.swing[i]==0xAA);
 ui.msg_t=0;ui_draw();shot("/tmp/mnm-seq-options.ppm");
 puts("sequencer pages: packed controls, mode reset, swing gap and song edit lock pass");
}
