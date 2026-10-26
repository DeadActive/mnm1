/* SPDX-License-Identifier: GPL-3.0-only
 * Serial/concurrent calibrated rendering through the actual mixer. */
#include <pthread.h>
#include <assert.h>
#include "drum_host.h"
#define BLOCKS 160
static pthread_t worker;
static void (*worker_fn)(void *);
static void *worker_context;
static unsigned submitted, joined, refused;
static void *work(void *unused) { (void)unused; worker_fn(worker_context); return 0; }
static int begin(void (*fn)(void *), void *context)
{
    worker_fn=fn;worker_context=context;
    assert(pthread_create(&worker,0,work,0)==0);submitted++;return 1;
}
static int decline(void (*fn)(void *), void *context)
{ (void)fn;(void)context;refused++;return 0; }
static void end(void) { assert(pthread_join(worker,0)==0);joined++; }
static uint64_t hash(const void *data,size_t n,uint64_t h)
{const uint8_t*p=data;while(n--)h=(h^*p++)*1099511628211ULL;return h;}
static uint64_t state(void)
{
 uint64_t h=1469598103934665603ULL;
 h=hash(trk,sizeof trk,h);h=hash(mmen_state,sizeof mmen_state,h);
 h=hash(mmen_pitch,sizeof mmen_pitch,h);h=hash(mmen_pre,sizeof mmen_pre,h);h=hash(mmen_post,sizeof mmen_post,h);
 h=hash(fxb,sizeof fxb,h);h=hash(td_buf,sizeof td_buf,h);h=hash(mmtd,sizeof mmtd,h);
 return h;
}
static void setup(unsigned scenario)
{
 host_init_live();
 /* Reset tap scratch as well: serial FX-source-first may intentionally read
  * the preceding block before that input track is rendered this block. */
 memset(trk_post,0,sizeof trk_post);memset(fx_in_buf,0,sizeof fx_in_buf);
 song.g[G_BPM]=137;song.g[G_CSRC]=scenario>=19?(scenario%6)+1:0;
 song.g[G_CTHR]=90;song.g[G_CRAT]=100;
 song.g[G_TAPE]=71;song.g[G_ADPC]=63;song.g[G_HISS]=43;song.g[G_DRIVE]=61;song.g[G_RTYPE]=2;
 const unsigned models[]={MM2_ENS,MM2_GNOI,MM2_GSIN,MM2_SAW,MM2_VOX,MM2_GPRC};
 for(unsigned k=0;k<NTRK;k++) {
  unsigned m=MM_MODEL_FIRST+models[k];
  if(k==2)m=MM_MODEL_FIRST+(scenario%19);
  if(scenario==20&&k==0)m=DM_MMGND;
  if(scenario==21&&k==3)m=MM_MODEL_FIRST+MM2_SID;
  if(scenario==22&&k==2)m=DM_M2_FXR;
  if(scenario==23&&k==2)m=DM_M2_FXC;
  if(scenario==24&&k==2)m=DM_M2_FXD;
  drum_set_model(&trk[k],m);track_t*t=&trk[k];
  if(mmen_is_model(m)) {
   mmen_default(t);uint8_t raw[71];mm_controls_get(t,raw);
   raw[MMA_ATK]=0;raw[MMA_HOLD]=127;raw[MMA_DEC]=127;raw[MMA_REL]=29;
   raw[MMA_EQG]=100;raw[MMA_DIST]=90;raw[MMA_MODM]=75;raw[MMA_MODT]=scenario%2?127:0;
   raw[MMA_DSND]=108;raw[MMA_DFB]=60;raw[MMA_DTIM]=12;raw[MMA_SPU]=81;
   for(unsigned l=0;l<3;l++){unsigned p=MMA_LFO+l*8;raw[p]=mm_control_track((k+l)%NTRK);raw[p+1]=(uint8_t)(1+(l==0?MMA_BASE:l==1?MMA_EQG:5));raw[p+2]=27;raw[p+4]=109;raw[p+5]=127;raw[p+7]=91;}
   if(m==MM_MODEL_FIRST+MM2_SID)raw[5]=127;
   mm_controls_set(t,raw);
  }
  memset(t->step,0,sizeof t->step);t->p[P_FXG]=1;t->p[P_IN]=scenario==23?IN_AB:IN_NEIB;
  t->p[P_OUT]=k%3;t->p[P_DUCK]=80;t->p[P_CHOR]=35;t->p[P_REV]=50;
  drum_play(t,100,0);t->gate_n=GATE_HELD;
 }
 if(scenario>=22)song.g[G_CSRC]=3; /* FX before its preceding input tracks */
 if(scenario==21)song.g[G_CSRC]=4; /* SID before preceding pitch producer */
}
static int32_t reference[BLOCKS][CTL*2];
static uint64_t reference_state[BLOCKS];
static void run(unsigned scenario,unsigned mode)
{
 cal_parallel_begin=0;cal_parallel_end=0;setup(scenario);
 if(mode){cal_parallel_begin=mode==1?begin:decline;cal_parallel_end=end;}
 for(unsigned b=0;b<BLOCKS;b++) {
  if(b==40){drum_cut(&trk[0]);drum_cut(&trk[4]);}
  if(b==72){drum_play(&trk[0],120,0);trk[0].gate_n=GATE_HELD;}
  if(b==96){drum_set_model(&trk[5],DM_M2_SAW);mmen_default(&trk[5]);drum_play(&trk[5],100,0);trk[5].gate_n=GATE_HELD;}
  if(b==120){for(unsigned k=0;k<NTRK;k++)drum_cut(&trk[k]);}
  int32_t output[CTL*2];mix_block(output,CTL);uint64_t h=state();
  if(!mode){memcpy(reference[b],output,sizeof output);reference_state[b]=h;}
  else if(memcmp(reference[b],output,sizeof output)||reference_state[b]!=h){
   fprintf(stderr,"parallel mismatch scenario=%u mode=%u block=%u audio=%d state=%d\n",scenario,mode,b,memcmp(reference[b],output,sizeof output)!=0,reference_state[b]!=h);exit(1);
  }
 }
}
int main(void)
{
 for(unsigned s=0;s<25;s++){run(s,0);run(s,2);run(s,1);}
 assert(submitted&&submitted==joined&&refused);
 printf("parallel: 25 scenarios, %u concurrent jobs; serial audio/state exact; %u refusals\n",submitted,refused);
 return 0;
}
