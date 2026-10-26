/* SPDX-License-Identifier: GPL-3.0-only
 * Virtual performance pages through production parameter lookup and knobs. */
#define UI_DRAW 1
#include "ui_host.h"
#include <assert.h>
static void page(const char *title)
{
 for(unsigned i=0;i<NPAGES;i++)if(!strcmp(PAGES[i].title,title)){ui.page=(uint8_t)i;ui.home=0;assert(cur_page()->scope==SC_PERFOPT);return;}
 assert(!"missing performance page");
}
static void knob(unsigned slot,int amount)
{host_ticks+=1000000u*FM1_TICKS_PER_US;edit_param(slot,amount);}
static void destinations(void)
{
 page("ASSIGN VEL");TSEL->pre_ed=0;
 knob(0,1);assert(TSEL->perf.dest[0]==P_E0+1&&TSEL->pre_ed);
 int16_t *value;const param_desc_t*d=page_desc(cur_page(),0,&value);
 assert(d==&PO_DESC[PO_VDEST1]&&d->fmt==F_ASSIGN&&*value==P_E0+1);
 char label[6],expected[6];const char *unit;
 param_format(d,*value,label,&unit);str_cpy(expected,track_desc(TSEL,P_E0)->label,6);assert(!strcmp(label,expected));
 perfui_set(TSEL,&song.multi,PO_VDEST1,P_PAN+1);knob(0,1);
 assert(TSEL->perf.dest[0]==P_DIST+1); /* skip MUTE, CHOKE and NOTE */
 knob(0,-1);assert(TSEL->perf.dest[0]==P_PAN+1);
 perfui_set(TSEL,&song.multi,PO_VDEST1,0);TSEL->pre_ed=0;knob(0,-1);assert(!TSEL->pre_ed&&!TSEL->perf.dest[0]);
 param_format(d,0,label,&unit);assert(!strcmp(label,"---"));
}
static void all_pages(void)
{
 const char *names[]={"ASSIGN VEL","ASSIGN KEY","TRIG TRACK","MULTI MODE","MULTI ENV","MULTI TIME"};
 for(unsigned p=0;p<6;p++){
  page(names[p]);
  int16_t *pointers[4]={0};
  for(unsigned slot=0;slot<4;slot++){
   unsigned id=page_id(cur_page(),slot);int16_t *value;const param_desc_t*d=page_desc(cur_page(),slot,&value);
   if(id==0xFF){assert(!d&&!value);continue;}
   assert(d==&PO_DESC[id]&&value&&*value==perfui_get(TSEL,&song.multi,id));pointers[slot]=value;
   for(unsigned prev=0;prev<slot;prev++)if(pointers[prev])assert(value!=pointers[prev]);
   int before=*value,delta=before==d->max?-1:1;
   TSEL->pre_ed=0;knob(slot,delta);
   int after=perfui_get(TSEL,&song.multi,id);assert(after!=before);
   assert(!!TSEL->pre_ed==(id<PO_MMODE));
   d=page_desc(cur_page(),slot,&value);assert(d==&PO_DESC[id]&&*value==after);
   char text[6];const char *unit;param_format(d,*value,text,&unit);assert(memchr(text,0,sizeof text));
  }
  ui.msg_t=0;ui.force=1;ui_draw();
 }
}
static void selected_track(void)
{
 song.sel=0;page("ASSIGN KEY");perfui_set(TSEL,&song.multi,PO_KAMT1,10);
 trk[0].pre_ed=0;trk[1].pre_ed=0;multi_options_t globals=song.multi;
 song.sel=1;perfui_set(TSEL,&song.multi,PO_KAMT1,-10);int16_t*value;
 page_desc(cur_page(),1,&value);assert(*value==-10);knob(1,1);
 assert(trk[1].perf.amount[2]==-9&&trk[0].perf.amount[2]==10&&trk[1].pre_ed&&!trk[0].pre_ed);
 assert(!memcmp(&song.multi,&globals,sizeof globals));
 page("MULTI MODE");page_desc(cur_page(),1,&value);assert(*value==globals.channel+1);
 song.sel=0;page_desc(cur_page(),1,&value);assert(*value==globals.channel+1);
 trk[0].pre_ed=trk[1].pre_ed=0;knob(1,1);assert(song.multi.channel==globals.channel+1&&!trk[0].pre_ed&&!trk[1].pre_ed);
 globals=song.multi;chain.running=1;knob(1,1);chain.running=0;assert(!memcmp(&song.multi,&globals,sizeof globals));
 page("TRIG TRACK");perf_options_t saved=TSEL->perf;chain.running=1;knob(0,1);chain.running=0;assert(!memcmp(&TSEL->perf,&saved,sizeof saved));
}
int main(void)
{
 ui_host_init();destinations();all_pages();selected_track();
 puts("performance pages: descriptors, knob whitelist/labels, preset edits, selected-track/global ownership and rendering pass");
}
