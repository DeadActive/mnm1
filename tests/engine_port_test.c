/* SPDX-License-Identifier: GPL-3.0-only
 * Callback integration: production types against the independently driven adapter.
 * cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
 * -Wno-unused-function tests/engine_port_test.c -o /tmp/engine_port_test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#define CTL 32u
static int32_t clamp(int32_t x,int32_t lo,int32_t hi){return x<lo?lo:x>hi?hi:x;}
static int16_t fxb[NTRK][4096];
static int32_t fx_in_buf[CTL];
#include "../firmware/src/mm_engine.c"
static const dmodel_t models[]={DM_M2_GSIN_DEF,DM_M2_GPRC_DEF,DM_M2_SID_DEF,DM_M2_FMS_DEF,DM_M2_FMD_DEF,DM_M2_SAW_DEF,DM_M2_ENS_DEF,DM_M2_WAV_DEF,DM_M2_VOX_DEF,DM_M2_FMP_DEF,DM_M2_PUL_DEF,DM_M2_FXC_DEF,DM_M2_FXR_DEF,DM_M2_FXD_DEF,DM_M2_GNOI_DEF,DM_M2_DKIK_DEF,DM_M2_DSNR_DEF,DM_M2_DHAT_DEF,DM_M2_DCYM_DEF};
static unsigned long checks;
static void run(unsigned machine)
{
 const unsigned k=2;track_t *t=&trk[k];dvoice_t *v=&t->v[0];uint8_t raw[71];
 mma_track_t ref;int16_t mb[1024],md[1024];
 memset(trk,0,sizeof trk);mmen_reset_all();song.g[G_BPM]=137;
 t->model=MM_MODEL_FIRST+machine;mmen_default(t);mm_controls_get(t,raw);
 assert(models[machine].mm==machine+2);
 for(unsigned i=0;i<8;i++){assert(raw[i]==mmen_synth_defaults[machine][i]);assert(raw[i]==models[machine].edit[i].def);}
 raw[MMA_HOLD]=127;raw[MMA_DEC]=127;raw[MMA_MODM]=63;raw[MMA_PORT]=60;
 mm_controls_set(t,raw);mma_init(&ref,k,machine,mb,md);
 v->vel=120;models[machine].trigger(t,v);mma_trigger(&ref,raw,60,120,0,-1);
 for(unsigned block=0;block<256;block++) {
  int32_t out[32]={0},rl[32],rr[32];int16_t input[32],carrier[32];
  if(block==16){t->nt=12;models[machine].glide(t,v);mma_trigger(&ref,raw,72,120,1,-1);}
  if(block==32){/* dv_init-like clearing must preserve track phase and glide. */
   memset(v,0,sizeof *v);v->vel=96;t->nt=-12;models[machine].trigger(t,v);mma_trigger(&ref,raw,48,96,0,-1);
  }
  if(block==64){models[machine].gate(t,v,2);mma_gate_end(&ref,0);assert(!ref.let_go&&!mmen_state[k].let_go);}
  if(block==128){models[machine].gate(t,v,0);mma_gate_end(&ref,1);}
  for(unsigned i=0;i<32;i++) {
   fx_in_buf[i]=input[i]=mm2_sinp((block*32+i)*26000000u)/2;
   mmen_carrier[i]=carrier[i]=mm2_sinp((block*32+i)*47000000u)/3;
  }
  for(unsigned half=0;half<2;half++)for(unsigned p=0;p<71;p++)
   mmen_delta[half][k][p]=(int32_t)((block*7+half*157+p*5)%513)-256;
  models[machine].render(t,v,out,32);
  for(unsigned off=0;off<32;off+=16) {
   int32_t ds[16],rs[16];mma_modulation(&ref,mmen_delta[off/16][k]);mma_render(&ref,raw,137,0,0,input+off,carrier+off,rl+off,rr+off,ds,rs);
   for(unsigned j=0;j<16;j++) {
    unsigned i=off+j;assert(rl[i]==mmen_left[k][i]&&rr[i]==mmen_right[k][i]);
    assert(out[i]==((rl[i]+rr[i])>>1));
    assert(mmen_pre[k][i]==ref.before_volume[j]&&mmen_post[k][i]==ref.after_volume[j]);
    assert(mmen_delay_send[k][i]==ds[j]&&mmen_reverb_send[k][i]==rs[j]);checks++;
   }
  }
  assert(mmen_state[k].pitch==ref.pitch&&mmen_state[k].aenv==ref.aenv);
  assert(v->last==out[31]);
 }
}
static void bounds(void)
{
 track_t scratch={0};dvoice_t v={0};int32_t out[34];
 scratch.model=MM_MODEL_FIRST+MM2_ENS;mmen_default(&scratch);v.vel=100;
 mmen_trigger(&scratch,&v);assert(mmen_index(&scratch)==NTRK);
 assert(mmen_state[NTRK].machine.delay==mmen_scratch);
 for(unsigned n=0;n<=32;n++) {
  for(unsigned i=0;i<34;i++)out[i]=1234567;
  mmen_render(&scratch,&v,out+1,n);
  assert(out[0]==1234567);for(unsigned i=n+1;i<34;i++)assert(out[i]==1234567);
 }
 scratch.model=MM_MODEL_FIRST+MM2_FXC;mmen_trigger(&scratch,&v);
 assert(mmen_state[NTRK].mach==MM2_FXC);
 mmen_set_carrier(NULL,0);for(unsigned i=0;i<32;i++)assert(!mmen_carrier[i]);
 mmen_reset_all();for(unsigned i=0;i<=NTRK;i++)assert(!mmen_ready[i]);
}
int main(void)
{
 for(unsigned m=0;m<19;m++)run(m);bounds();
 printf("engine: %lu stereo callback samples match adapter; persistence, gates, scratch and short lengths pass\n",checks);return 0;
}
