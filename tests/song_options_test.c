/* SPDX-License-Identifier: GPL-3.0-only */
#define UI_DRAW 1
#include "ui_host.h"
#include <assert.h>
static step_t steps[2][NTRK][NSTEP];
static motion_store_t locks[2];
static pattern_options_t source_options;
static void fresh(unsigned rows)
{
 ui_host_init();init_all();transport_req=0;
 memset(steps,0,sizeof steps);memset(locks,0,sizeof locks);source_options=(pattern_options_t){0};
 chain.cfg.count=(uint8_t)rows;
 for(unsigned r=0;r<rows;r++)chain.cfg.row[r]=(chain_row_t){(uint8_t)(r&1),1};
 for(unsigned p=0;p<2;p++){
  chain.src[p].m=&locks[p];chain.src[p].options=&source_options;
  for(unsigned k=0;k<NTRK;k++){
   chain.src[p].step[k]=steps[p][k];chain.src[p].model[k]=(uint8_t)trk[k].model;
   chain.src[p].timing[k][0]=8;chain.src[p].timing[k][1]=2;
   for(unsigned s=0;s<NSTEP;s++)steps[p][k][s].nt=(int8_t)s;
  }
 }
}
static void start(void){chain.armed=1;seq_start();assert(chain.running);}
static void finish_row(void)
{
 track_t *t=&trk[chain.ref];t->seq_idx=(uint16_t)(t->p[P_SLEN]-1);t->seq_cnt=t->seq_idx;
 t->seq_rem=0;t->seq_pos=step_samples(t,div_period(t->p[P_SDIV],0),t->seq_cnt)-1;
 chain_tick(1);
}
static void range_and_restore(void)
{
 fresh(3);source_options.transpose=5;song.g[G_BPM]=120;trk[1].p[P_MUTE]=1;
 chain.cfg.options[0]=(chain_row_options_t){12,6,4,4,100,0,0};
 chain.cfg.options[2].bpm=140;
 seq_options_t original=trk[0].seq;start();
 assert(song.g[G_BPM]==100&&song.seq.transpose==17&&trk[0].p[P_SLEN]==4);
 assert(trk[1].p[P_MUTE]&&trk[2].p[P_MUTE]);
 for(unsigned s=0;s<4;s++){
  assert(seq_step_at(&trk[0],s)->nt==(int)((s+6)%8));
  assert(view_step(&trk[0],s).nt==seq_step_at(&trk[0],s)->nt);
 }
 trk[0].nt=0;assert(seq_note(&trk[0])==77);
 trk[0].seq.scale_key=16;assert(seq_note(&trk[0])==60);
 trk[0].seq=original;
 finish_row();assert(chain.row==1&&song.g[G_BPM]==100&&!trk[2].p[P_MUTE]);
 finish_row();assert(chain.row==2&&song.g[G_BPM]==140);
 finish_row();assert(!chain.running&&!song.playing&&song.g[G_BPM]==120&&song.seq.transpose==0);
 assert(trk[1].p[P_MUTE]&&!trk[2].p[P_MUTE]&&trk[0].p[P_SLEN]==16);
 assert(!memcmp(&original,&trk[0].seq,sizeof original));
}
static void mapped_locks(void)
{
 fresh(1);track_t*t=&trk[0];chain.cfg.options[0].offset=6;chain.cfg.options[0].length=4;
 locks[0].on=1;locks[0].count=2;
 locks[0].ev[0]=(motion_event_t){0,7,P_LEVEL,30};locks[0].ev[1]=(motion_event_t){0,1,P_LEVEL,90};
 steps[0][0][7].on=1;steps[0][0][7].pslide=1;
 start();assert(!view_lock_count(0,0)&&view_lock_count(0,1)==1&&view_lock_count(0,3)==1);
 motion_step(t,0);motion_step(t,1);assert(t->p[P_LEVEL]==30);
 assert(mo.sl[0][0].on&&mo.sl[0][0].to==90&&mo.sl[0][0].len==2*div_period(2,0));
 motion_step(t,3);assert(t->p[P_LEVEL]==90&&!mo.sl[0][0].on);
 seq_stop();assert(t->p[P_LEVEL]!=90);
 fresh(1);chain.src[0].timing[0][0]=4;chain.cfg.options[0].offset=3;chain.cfg.options[0].length=10;
 locks[0].on=1;locks[0].count=1;locks[0].ev[0]=(motion_event_t){0,0,P_LEVEL,77};
 start();int8_t v=0;assert(motion_next(&locks[0],0,1,P_LEVEL,10,&v)==4&&v==77);seq_stop();
}
static void source_wrap_and_swing(void)
{
 fresh(1);source_options.swing=75;chain.cfg.options[0].offset=1;
 start();trk[0].seq.swing[0]=2;
 assert(step_delay(&trk[0],1000,0)==500&&step_delay(&trk[0],1000,1)==0);seq_stop();
 fresh(1);chain.src[0].timing[0][0]=4;chain.cfg.options[0].offset=2;chain.cfg.options[0].length=8;
 locks[0].on=1;locks[0].count=1;locks[0].ev[0]=(motion_event_t){0,3,P_PAN,31};
 start();motion_step(&trk[0],1);assert(trk[0].p[P_PAN]==31);
 motion_step(&trk[0],2);assert(trk[0].p[P_PAN]==0);seq_stop();
}
static void nested_flow(void)
{
 fresh(5);chain.cfg.options[1].jump=1;chain.cfg.options[1].loop_count=1;
 chain.cfg.options[3].jump=1;chain.cfg.options[3].loop_count=2;
 start();const unsigned expected[]={0,1,0,1,2,3,0,1,0,1,2,3,0,1,0,1,2,3,4};
 for(unsigned i=0;i<sizeof expected/sizeof*expected;i++){assert(chain.running&&chain.row==expected[i]);finish_row();}
 assert(!chain.running);
 fresh(2);chain.cfg.options[0].jump=1;chain.cfg.options[0].loop_count=0;start();
 for(unsigned i=0;i<20;i++){finish_row();assert(chain.running&&chain.row==0);}seq_stop();
 fresh(2);chain.cfg.options[0].jump=2;start();finish_row();assert(chain.row==1);finish_row();assert(!chain.running);
}
static void page(const char*name)
{
 for(unsigned i=0;i<NPAGES;i++)if(!strcmp(PAGES[i].title,name)){ui.page=(uint8_t)i;ui.home=0;return;}
 assert(!"missing SONG page");
}
static void tap(unsigned key)
{press(key);ui_frame();release_all();ui_frame();}
static void navigation(void)
{
 fresh(0);page("STEP");
 unsigned expected[NPAGES],count=0;
 for(unsigned i=0;i<NPAGES;i++)if(PAGES[i].fam==FAM_SEQ&&!page_hidden(i))expected[count++]=i;
 ui.page=(uint8_t)expected[0];
 for(unsigned i=1;i<count;i++){page_turn(1);assert(ui.page==expected[i]);}
 page_turn(1);assert(ui.page==expected[count-1]);
 for(unsigned i=count-1;i>0;i--){page_turn(-1);assert(ui.page==expected[i-1]);}
 page_turn(-1);assert(ui.page==expected[0]);
}
static void controls(void)
{
 fresh(2);page("SONG RANGE");edit_param(1,-999);edit_param(2,999);edit_param(3,999);
 assert(chain.cfg.options[0].transpose==-48&&chain.cfg.options[0].offset==63&&chain.cfg.options[0].length==64);
 page("SONG MIX");edit_param(1,99);edit_param(2,1);edit_param(3,1);
 assert(chain.cfg.options[0].mutes==32&&chain.cfg.options[0].bpm==40);
 edit_param(3,-1);assert(!chain.cfg.options[0].bpm);
 page("SONG FLOW");edit_param(1,99);edit_param(2,99);assert(chain.cfg.options[0].jump==2&&chain.cfg.options[0].loop_count==16);
 chain.armed=1;chain_config_t before=chain.cfg;edit_param(1,-1);assert(!memcmp(&before,&chain.cfg,sizeof before));chain.armed=0;
 chain.cfg.options[1].transpose=11;chain.cfg.options[1].jump=1;ui.confirm=3;ui.confirm_trk=0;tap(B_OCTUP);
 assert(chain.cfg.count==1&&chain.cfg.options[0].transpose==11&&!chain.cfg.options[0].jump);
 page("SONG FILE");edit_param(0,1);assert(ui.confirm==7);tap(B_OCTDN);assert(chain.cfg.count==1);
 edit_param(0,1);transport_req=1;tap(B_OCTUP);assert(chain.cfg.count==1);seq_stop();transport_req=0;
 edit_param(1,1);transport_req=1;tap(B_OCTUP);assert(!chain.cfg.rsv[0]);seq_stop();transport_req=0;
 edit_param(1,1);assert(ui.confirm==8);tap(B_OCTUP);assert(chain.cfg.rsv[0]&&chain.cfg.count==8);
 page("SONG FILE");edit_param(0,1);tap(B_OCTUP);assert(!chain.cfg.rsv[0]&&!chain.cfg.count);
 const char*names[]={"SONG","SONG RANGE","SONG MIX","SONG FLOW","SONG FILE"};
 for(unsigned i=0;i<5;i++){page(names[i]);ui.force=1;ui_draw();}
}
int main(void)
{
 range_and_restore();mapped_locks();source_wrap_and_swing();nested_flow();navigation();controls();
 puts("song_options_test: range/locks/slides, tempo/mutes restore, nested flow and UI passed");return 0;
}
