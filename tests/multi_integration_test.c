/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include <assert.h>
static unsigned failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__func__,__LINE__,#x);failures++;}}while(0)
static void fresh(unsigned mode,unsigned model)
{
 ui_host_init();transport_req=0;song.playing=0;song.solo=0;multi_stop();
 song.multi.mode=mode;song.multi.channel=12;song.multi.sustain=127;
 song.g[G_CSRC]=0;song.g[G_TAPE]=song.g[G_ADPC]=song.g[G_HISS]=song.g[G_DRIVE]=0;
 fm1_in.notes=0;kb_prev=0;
 for(unsigned k=0;k<NTRK;k++){
  drum_set_model(&trk[k],model);if(mmen_is_model(model))mmen_default(&trk[k]);
  trk[k].perf=(perf_options_t){0};seq_options_default(&trk[k].seq);
  trk[k].p[P_MUTE]=trk[k].p[P_CHOKE]=trk[k].p[P_ARPM]=0;
  trk[k].p[P_AATK]=0;trk[k].p[P_AHOLD]=trk[k].p[P_ADEC]=127;
  trk[k].p[P_CHOR]=trk[k].p[P_REV]=trk[k].p[P_DLY]=trk[k].p[P_SLCR]=0;
  memset(trk[k].step,0,sizeof trk[k].step);
 }
}
static int gate(unsigned k){return dv_gate_open(&trk[k],&trk[k].v[0]);}
static void midi(unsigned channel,unsigned note,unsigned velocity,int on)
{
 unsigned status=(on?0x90u:0x80u)|channel;
 midi_enqueue((status>>4)|(status<<8)|(note<<16)|(velocity<<24),0);midi_block();
}
static void all_split(void)
{
 fresh(MULTI_ALL,DM_M2_GSIN);trk[0].perf.route_flags=2;trk[1].perf.route_flags=3;trk[2].perf.route_flags=1;
 unsigned age=dvage;multi_note(60,113,1,0);CHECK(dvage==age+6);
 for(unsigned k=0;k<NTRK;k++)CHECK(gate(k)&&trk[k].v[0].vel==113&&trk[k].trig_nt==0);
 multi_note(67,87,1,0);multi_note(67,0,0,0);
 for(unsigned k=0;k<NTRK;k++)CHECK(gate(k)&&trk[k].trig_nt==0&&trk[k].v[0].vel==113);
 multi_note(60,0,0,0);for(unsigned k=0;k<NTRK;k++)CHECK(!gate(k));
 fresh(MULTI_ALL,DM_M2_GSIN);multi_note(60,100,1,0);drum_play(&trk[2],100,9);
 multi_note(60,0,0,0);CHECK(gate(2)&&trk[2].trig_nt==9);
 fresh(MULTI_SPLIT,DM_M2_GSIN);song.multi.split_key=60;song.multi.first_upper=3;
 multi_note(59,100,1,0);for(unsigned k=0;k<6;k++)CHECK(gate(k)==(k<3));
 multi_note(60,100,1,0);for(unsigned k=0;k<6;k++)CHECK(gate(k)&&trk[k].trig_nt==(k<3?-1:0));
 multi_note(59,0,0,0);for(unsigned k=0;k<6;k++)CHECK(gate(k)==(k>=3));
}
static void inputs(void)
{
 fresh(MULTI_ALL,DM_M2_GSIN);unsigned age=dvage;
 midi(11,60,100,1);CHECK(!multi.count&&dvage==age);
 midi(12,60,100,1);CHECK(multi.count==1&&dvage==age+6);
 song.seq_mode=3;song.octave=0;fm1_in.notes=1u<<7;render_mix(0,0,CTL);
 CHECK(multi.held[60]==3&&multi_panel_note[7]==61);
 midi(12,60,0,0);CHECK(multi.held[60]==2);for(unsigned k=0;k<6;k++)CHECK(gate(k));
 song.octave=2;fm1_in.notes=0;render_mix(0,0,CTL);
 CHECK(!multi.held[60]&&!multi.count&&!multi_panel_note[7]);for(unsigned k=0;k<6;k++)CHECK(!gate(k));
}
static void arp(void)
{
 fresh(MULTI_ALL,DM_M2_GSIN);
 for(unsigned k=0;k<6;k++)trk[k].seq.arp_mode_order=MMA_MODE_KEY|(MMA_PLAY_UP<<3);
 multi_note(60,100,1,0);multi_note(64,100,1,0);
 for(unsigned k=0;k<6;k++){
  CHECK(mmarp_nheld[k]==2&&trk[k].arp_on);
  trk[k].arp_pos=0;arp_tick(&trk[k],CTL);CHECK(mmen_state[k].note==60);
  trk[k].arp_pos=0;arp_tick(&trk[k],CTL);CHECK(mmen_state[k].note==64);
 }
 multi_note(64,0,0,0);for(unsigned k=0;k<6;k++)CHECK(mmarp_nheld[k]==1);
 multi_note(60,0,0,0);for(unsigned k=0;k<6;k++)CHECK(!mmarp_nheld[k]&&!gate(k));
}
static void sequence(void)
{
 fresh(MULTI_SEQSTART,DM_M2_GSIN);multi_note(60,100,1,0);CHECK(song.playing&&!song.multi_pitch16);
 trk[0].seq_cnt=7;multi_note(64,100,1,0);CHECK(trk[0].seq_cnt==0xFFFFFFFFu&&song.multi_pitch16==64);
 fresh(MULTI_SEQTRANSPOSE,DM_M2_GSIN);multi_note(60,100,1,0);trk[0].seq_cnt=7;
 multi_note(67,100,1,0);CHECK(trk[0].seq_cnt==7&&song.multi_pitch16==112);
 song.multi.timing=1;multi_tick(1,0);unsigned q=multi_quantum(&song.multi,beat_samples());
 multi_note(72,100,1,0);CHECK(multi.pending&&song.multi_pitch16==112);
 multi_tick(q-2,0);CHECK(multi.pending&&song.multi_pitch16==112);
 multi_tick(1,0);CHECK(!multi.pending&&song.multi_pitch16==192&&trk[0].seq_cnt==7);
 /* A released pending request cannot execute its pitch. */
 multi_tick(1,0);multi_note(76,100,1,0);multi_note(76,0,0,0);
 multi_tick(q-1,0);CHECK(song.multi_pitch16!=256);
 seq_stop();CHECK(!multi.count&&!multi.pending&&!multi.envactive&&!song.multi_pitch16);
 fresh(MULTI_SEQTRANSPOSE,DM_M2_GSIN);song.multi.timing=1;
 multi_note(60,100,1,0);multi_note(60,0,0,0);multi_tick(1,0);
 multi_note(72,100,1,0);CHECK(multi.pending);multi_note(72,0,0,0);
 CHECK(!multi.pending&&!multi.count);multi_tick(q,0);CHECK(!song.multi_pitch16);
}
static void pitch(unsigned model)
{
 fresh(MULTI_SEQTRANSPOSE,model);song.multi.portamento=80;multi_note(60,100,1,0);
 track_t *t=&trk[0];t->p[P_PORT]=0;t->p[P_E0]=mmen_is_model(model)?0:0;
 drum_play_one(t,100,0,0);unsigned age=t->v[0].age;
 multi_note(72,100,1,0);CHECK(!song.multi_pitch16&&multi.target16==192);
 for(unsigned i=0;i<100;i++)multi_tick(0,CTL);
 CHECK(song.multi_pitch16>0&&song.multi_pitch16<192);
 int shift=song.multi_pitch16;
 int32_t out[CTL]={0};track_render(t,out,CTL);CHECK(t->v[0].age==age);
 if(mmen_is_model(model))CHECK(mmen_state[0].pitch==60*64+shift*4&&t->multi_pitch_ref==shift);
 else CHECK(mm_pitch(t,&t->v[0],0)==60*16+shift);
 t->seq.scale_key=16;drum_play_one(t,100,0,0);
 song.multi_pitch16=123;memset(out,0,sizeof out);track_render(t,out,CTL);
 CHECK(seq_multi_pitch(t)==0&&t->multi_pitch_ref==0);
 if(mmen_is_model(model))CHECK(mmen_state[0].pitch==60*64);else CHECK(mm_pitch(t,&t->v[0],0)==60*16);
}
static uint64_t energy(unsigned mode)
{
 fresh(mode,DM_M2_GSIN);multi_note(60,100,1,0);
 multi.envactive=1;multi.envstage=MULTI_ENV_SUSTAIN;multi.env=0;song.multi.sustain=0;
 int32_t l[2048],r[2048];render_mix(l,r,2048);uint64_t sum=0;
 for(unsigned i=0;i<2048;i++)sum+=(uint32_t)(l[i]<0?-l[i]:l[i])+(uint32_t)(r[i]<0?-r[i]:r[i]);
 return sum;
}
static void envelope_render(void)
{
 fresh(MULTI_ALL,DM_M2_GSIN);song.multi.attack=100;song.multi.decay=40;
 song.multi.sustain=64;song.multi.release=40;multi_note(60,100,1,0);
 render_mix(0,0,CTL);CHECK(multi.env>0&&multi.env<MULTI_ENV_ONE&&multi.envstage==MULTI_ENV_ATTACK);
 multi.env=MULTI_ENV_ONE;multi.envstage=MULTI_ENV_DECAY;
 render_mix(0,0,CTL);CHECK(multi.env<MULTI_ENV_ONE&&multi.env>64u*MULTI_ENV_ONE/127u);
 multi.envstage=MULTI_ENV_SUSTAIN;render_mix(0,0,CTL);CHECK(multi.env==64u*MULTI_ENV_ONE/127u);
 unsigned held=multi.env;multi_note(60,0,0,0);render_mix(0,0,CTL);
 CHECK(multi.env<held&&multi.envstage==MULTI_ENV_RELEASE);
}
static void mode_change(void)
{
 fresh(MULTI_SEQTRANSPOSE,DM_M2_GSIN);multi_note(67,100,1,0);multi.pending=1;
 for(unsigned i=0;i<NPAGES;i++)if(PAGES[i].scope==SC_PERFOPT&&page_id(&PAGES[i],0)==PO_MMODE){ui.page=i;ui.home=0;break;}
 edit_param(0,-1);CHECK(!multi.count&&!multi.pending&&!song.multi_pitch16);
}
static void reset_regression(void)
{
 fresh(MULTI_ALL,DM_M2_GSIN);multi_note(60,100,1,0);multi.pending=1;
 song.multi_pitch16=99;expanded_reset_voices();
 CHECK(!multi.count&&!multi.pending&&!multi.envactive&&!song.multi_pitch16);
 for(unsigned k=0;k<NTRK;k++)CHECK(!multi_owner[k]);
}
static void shared_note_regression(void)
{
 for(unsigned order=0;order<2;order++) {
  fresh(MULTI_ALL,DM_M2_GSIN);trk[0].seq.arp_mode_order=MMA_MODE_KEY|(MMA_PLAY_UP<<3);
  multi_note(60,100,1,0);midi(0,60,100,1);render_mix(0,0,CTL);
  CHECK(mmarp_nheld[0]==1&&gate(0));
  if(order)midi(0,60,0,0);else multi_note(60,0,0,0);
  CHECK(mmarp_nheld[0]==1&&gate(0));
  if(order)multi_note(60,0,0,0);else midi(0,60,0,0);
  CHECK(!mmarp_nheld[0]&&!gate(0));
  fresh(MULTI_OFF,DM_M2_GSIN);trk[0].seq.arp_mode_order=MMA_MODE_KEY|(MMA_PLAY_UP<<3);
  song.sel=0;song.seq_mode=3;song.octave=0;
  fm1_in.notes=1u<<7;render_mix(0,0,CTL);midi(0,60,100,1);
  CHECK(mmarp_nheld[0]==1&&gate(0));
  if(order)midi(0,60,0,0);else {fm1_in.notes=0;render_mix(0,0,CTL);}
  CHECK(mmarp_nheld[0]==1&&gate(0));
  if(order){fm1_in.notes=0;render_mix(0,0,CTL);}else midi(0,60,0,0);
  CHECK(!mmarp_nheld[0]&&!gate(0));
 }
}
static void legacy_pitch_regression(void)
{
 for(unsigned glide=0;glide<2;glide++) {
  fresh(MULTI_SEQTRANSPOSE,DM_MMSAW);track_t*t=&trk[0];t->p[P_PORT]=127;t->p[P_E0]=0;
  drum_play_one(t,100,0,0);song.multi_pitch16=199;
  int before=mm_pitch(t,&t->v[0],0);CHECK(before==60*16+199);
  if(glide)mm_glide(t,&t->v[0]);else drum_play_one(t,100,0,0);
  CHECK(mm_pitch(t,&t->v[0],0)==before&&t->multi_pitch_ref==199);
 }
 fresh(MULTI_SEQTRANSPOSE,DM_M2_GSIN);track_t*t=&trk[0];drum_set_model(t,DM_FXRING);
 t->p[P_FXG]=1;t->p[P_PORT]=0;t->p[P_E0]=0;t->p[P_E0+3]=0;
 drum_play_one(t,100,0,0);song.multi_pitch16=7;
 for(unsigned i=0;i<3;i++) {
  uint32_t phase=t->v[0].ms.mm.ph[0];int32_t out[CTL]={0};
  fxring_render(t,&t->v[0],out,CTL);
  CHECK(t->v[0].ms.mm.ph[0]-phase==mm_inc(60*16+7)*CTL);
 }
 fresh(MULTI_SEQTRANSPOSE,DM_M2_GSIN);t=&trk[0];drum_set_model(t,DM_ACID);t->p[P_E0]=0;
 drum_play_one(t,100,0,0);unsigned age=t->v[0].age;
 for(unsigned i=0;i<3;i++) {
  song.multi_pitch16=(int16_t)(i?i==1?199:-7:0);
  uint32_t phase=t->v[0].ph[0];int32_t out[CTL]={0};acid_render(t,&t->v[0],out,CTL);
  CHECK(t->v[0].ph[0]-phase==acid_inc(60*16+song.multi_pitch16)*CTL);
  CHECK(t->v[0].age==age&&gate(0));
 }
}
int main(void)
{
 all_split();inputs();arp();sequence();pitch(DM_MMSAW);pitch(DM_M2_GSIN);
 reset_regression();shared_note_regression();legacy_pitch_regression();
 CHECK(energy(MULTI_ALL)==0);CHECK(energy(MULTI_SPLIT)>0);envelope_render();mode_change();
 if(failures){fprintf(stderr,"multi integration: %u failures\n",failures);return 1;}
 puts("multi_integration_test: passed");return 0;
}
