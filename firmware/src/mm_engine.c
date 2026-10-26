/* SPDX-License-Identifier: GPL-3.0-only
 * MONO2K DSP by Inigo Hernandez, integration for the existing track callbacks.
 * https://github.com/InigoHernandez/mono2k
 * Persistent state belongs to a track, independently of voice allocation.
 */
#define MM2_MUL_OUT 1
#define MNM_NO_LIBC_HEADERS 1
#include "seq_transpose.h"
#include "mm_adapter.h"
#include "mm_controls.h"
#define MM_MODEL_FIRST 63u
static mma_track_t mmen_state[NTRK+1];
static uint8_t mmen_ready[NTRK+1];
static int32_t mmen_pitch[NTRK+1][(CTL+15)/16];
static int16_t mmen_scratch[2048];
/* PCM16 units, no further insert/AMP/VOL/pan processing needed by the mixer. */
static int32_t mmen_left[NTRK+1][CTL],mmen_right[NTRK+1][CTL];
static int32_t mmen_delay_send[NTRK+1][CTL],mmen_reverb_send[NTRK+1][CTL];
static int16_t mmen_pre[NTRK+1][CTL],mmen_post[NTRK+1][CTL];
static int32_t mmen_delta[(CTL+15)/16][NTRK][71];
/* Root routing fills this separately for each FX track; pre-VOL N-2 carrier. */
static int16_t mmen_carrier[CTL];
static unsigned mmen_accent;
static unsigned mmen_index(const track_t *t) {
 for(unsigned k=0;k<NTRK;k++)if(t==&trk[k])return k;
 return NTRK;
}
static int mmen_is_model(unsigned m) { return m>=MM_MODEL_FIRST && m<MM_MODEL_FIRST+MM2_COUNT; }
static void mmen_set_carrier(const int32_t *samples,unsigned n) {
 for(unsigned i=0;i<CTL;i++)mmen_carrier[i]=(int16_t)(samples&&i<n?mm2_sat16(samples[i]):0);
}
static const uint8_t mmen_synth_defaults[MM2_COUNT][8] = {
     {0, 0, 0, 0, 40, 0, 0, 64},
     {64, 0, 0, 40, 0, 40, 0, 0},
     {0, 0, 127, 0, 0, 0, 64, 64},
     {61, 64, 80, 32, 82, 64, 64, 64},
     {64, 64, 64, 0, 74, 80, 32, 64},
     {0, 0, 0, 0, 0, 0, 0, 64},
     {85, 100, 120, 0, 64, 0, 64, 64},
     {40, 64, 64, 0, 0, 70, 0, 0},
     {64, 64, 127, 0, 0, 64, 64, 64},
     {61, 64, 82, 64, 104, 80, 64, 64},
     {0, 0, 0, 0, 64, 0, 0, 64},
     {40, 50, 30, 64, 64, 64, 127, 100},
     {0, 0, 0, 127, 0, 0, 0, 64},
     {20, 60, 64, 127, 64, 64, 0, 100},
     {0, 0, 0, 0, 0, 0, 0, 64},
     {0, 44, 26, 21, 0, 64, 0, 16},
     {40, 81, 14, 127, 61, 64, 0, 6},
     {54, 60, 127, 127, 127, 0, 5, 0},
     {127, 64, 0, 64, 0, 110, 0, 107},
};
static void mmen_reset_all(void) {
 memset(mmen_pitch,0,sizeof mmen_pitch);
 memset(mmen_state,0,sizeof mmen_state);memset(mmen_ready,0,sizeof mmen_ready);
 memset(mmen_left,0,sizeof mmen_left);memset(mmen_right,0,sizeof mmen_right);
 memset(mmen_pre,0,sizeof mmen_pre);memset(mmen_post,0,sizeof mmen_post);
 memset(mmen_delay_send,0,sizeof mmen_delay_send);memset(mmen_reverb_send,0,sizeof mmen_reverb_send);
 memset(mmen_delta,0,sizeof mmen_delta);memset(mmen_carrier,0,sizeof mmen_carrier);
 mmen_accent=0;
}
static void mmen_default(track_t *t) {
 if(!mmen_is_model(t->model))return;
 unsigned m=t->model-MM_MODEL_FIRST,k=mmen_index(t);
 for(unsigned i=0;i<8;i++)t->p[P_E0+i]=mmen_synth_defaults[m][i];
 mm_controls_init(t,k<NTRK?k:0,m);
 mmen_ready[k]=0;
}
static mma_track_t *mmen_prepare(track_t *t) {
 unsigned k=mmen_index(t),m=t->model-MM_MODEL_FIRST;
 if(!mmen_is_model(t->model))return 0;
 if(!mmen_ready[k]||mmen_state[k].mach!=m) {
  int16_t *b=k<NTRK?fxb[k]:mmen_scratch;
  mma_init(&mmen_state[k],k,m,b,b+1024);mmen_ready[k]=1;
 }
 mmen_state[k].keytrack_off=(t->perf.route_flags>>3)&3u;
 mma_modulation(&mmen_state[k],k<NTRK?mmen_delta[(CTL+15)/16-1][k]:0);
 return &mmen_state[k];
}
static void mmen_multi_fraction(track_t *t,mma_track_t *s)
{
 int fraction=seq_multi_pitch(t)%16*4;
 int same=s->pitch==s->ptarget;s->ptarget+=fraction;if(same)s->pitch+=fraction;
}
static void mmen_trigger(track_t *t,dvoice_t *v) {
 mma_track_t *s=mmen_prepare(t);uint8_t raw[71];if(!s)return;
 t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);
 mm_controls_get(t,raw);mma_trigger(s,raw,(unsigned)seq_note(t),v->vel,0,-1);mmen_multi_fraction(t,s);
 v->active=1;v->t=0;
}
static void mmen_trigger_mask(track_t *t,dvoice_t *v,unsigned velocity,unsigned flags) {
 mma_track_t *s=mmen_prepare(t);uint8_t raw[71];if(!s)return;
 mm_controls_get(t,raw);
 int32_t pitch=s->pitch,target=s->ptarget;uint8_t note=s->note,slide=s->slide;
 if((flags&(ST_NOAMP|ST_NOPITCH))!=(ST_NOAMP|ST_NOPITCH))t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);
 mma_trigger_mask(s,raw,(unsigned)seq_note(t),velocity,0,-1,flags&(ST_NOAMP|ST_NOFLT));
 if((flags&(ST_NOAMP|ST_NOPITCH))!=(ST_NOAMP|ST_NOPITCH))mmen_multi_fraction(t,s);
 if((flags&(ST_NOAMP|ST_NOPITCH))==(ST_NOAMP|ST_NOPITCH)){
  s->pitch=pitch;s->ptarget=target;s->note=note;s->slide=slide;
 }
 if(!(flags&ST_NOAMP)){v->active=1;v->t=0;}
}
static void mmen_idle_filter(track_t *t,unsigned n) {
 unsigned k=mmen_index(t);if(!mmen_ready[k])return;
 mma_track_t *s=&mmen_state[k];if(s->sounding||s->astage||!s->fstage)return;
 uint8_t raw[71];mm_controls_get(t,raw);mma_parameters(s,raw);s->bpm=song.g[G_BPM]>0?(unsigned)song.g[G_BPM]:1;
 for(unsigned off=0;off<n;off+=16){
  mma_modulation(s,k<NTRK?mmen_delta[off/16][k]:0);mma_env_block(s);
 }
}
static void mmen_glide(track_t *t,dvoice_t *v) {
    t->trig_nt=t->nt;
 mma_track_t *s=mmen_prepare(t);uint8_t raw[71];if(!s)return;
 t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);
 mm_controls_get(t,raw);mma_trigger(s,raw,(unsigned)seq_note(t),v->vel,1,-1);mmen_multi_fraction(t,s);
 v->active=1;v->t=0;
}
static void mmen_gate(track_t *t,dvoice_t *v,int open) {
 mma_track_t *s=mmen_prepare(t);(void)v;if(!s)return;
 if(open==1){s->gate=1;s->let_go=0;}
 else mma_gate_end(s,open==0);
}
/* The live engine uses CTL=32. Short previews render whole source control blocks,
 * zero-pad FX input beyond n, and return only the requested samples. */
static void mmen_render(track_t *t,dvoice_t *v,int32_t *out,uint32_t n) {
 unsigned k=mmen_index(t);mma_track_t *s=mmen_prepare(t);uint8_t raw[71];
 if(n>CTL)n=CTL;
 for(unsigned i=0;i<CTL;i++) {
  mmen_left[k][i]=mmen_right[k][i]=mmen_delay_send[k][i]=mmen_reverb_send[k][i]=0;
  mmen_pre[k][i]=mmen_post[k][i]=0;
 }
 if(!s)return;
 int shift=seq_multi_pitch(t)-t->multi_pitch_ref;
 if(shift){s->pitch+=shift*4;s->ptarget+=shift*4;t->multi_pitch_ref=(int16_t)seq_multi_pitch(t);}
 mm_controls_get(t,raw);
 for(unsigned off=0;off<n;off+=16) {
  int16_t input[16],carrier[16];int32_t l[16],r[16],ds[16],rs[16];
  for(unsigned j=0;j<16;j++) {
   int is_fx=s->mach==MM2_FXC||s->mach==MM2_FXR||s->mach==MM2_FXD;
   input[j]=(int16_t)(is_fx&&off+j<n?mm2_sat16(fx_in_buf[off+j]):0);
   carrier[j]=is_fx&&off+j<n?mmen_carrier[off+j]:0;
  }
  int32_t prev=s->mach==MM2_SID&&k>0&&k<NTRK&&mmen_ready[k-1]?mmen_pitch[k-1][off/16]:0;
  mma_modulation(s,k<NTRK?mmen_delta[off/16][k]:0);
  mma_render(s,raw,(unsigned)song.g[G_BPM],mmen_accent,prev,input,carrier,l,r,ds,rs);
  mmen_pitch[k][off/16]=s->pitch;
  for(unsigned j=0;j<16&&off+j<n;j++) {
   unsigned i=off+j;int32_t mono=(l[j]+r[j])>>1;
   mmen_left[k][i]=l[j];mmen_right[k][i]=r[j];
   mmen_delay_send[k][i]=ds[j];mmen_reverb_send[k][i]=rs[j];
   mmen_pre[k][i]=s->before_volume[j];mmen_post[k][i]=s->after_volume[j];
   out[i]+=mono;v->last=mono;
  }
 }
 v->t+=n;t->mm_p16=s->pitch>>2;
 if(!s->sounding&&!s->astage)v->active=0;
}
#define DM_M2_GSIN_DEF {"G-SIN",1,0,{PD("FOLD", F_INT, 0, 127, 0), PD("SUB", F_INT, 0, 127, 0), PD("NOIS", F_INT, 0, 127, 0), PD("PDEP", F_INT, 0, 127, 0), PD("PDEC", F_INT, 0, 127, 40), PD("CLIK", F_INT, 0, 127, 0), PD("HARM", F_INT, 0, 127, 0), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,2,0}
#define DM_M2_GPRC_DEF {"GPRC",1,0,{PD("COLR", F_INT, 0, 127, 64), PD("S&H", F_INT, 0, 127, 0), PD("META", F_INT, 0, 127, 0), PD("MSPR", F_INT, 0, 127, 40), PD("PDEP", F_INT, 0, 127, 0), PD("PDEC", F_INT, 0, 127, 40), PD("GRIT", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,3,0}
#define DM_M2_SID_DEF {"SID2",1,0,{PD("PW", F_INT, 0, 127, 0), PD("PWAD", F_INT, 0, 127, 0), PD("PWRS", F_INT, 0, 127, 127), PD("WAVE", F_INT, 0, 127, 0), PD("MOD", F_INT, 0, 127, 0), PD("MSRC", F_INT, 0, 127, 0), PD("MFRQ", F_INT, 0, 127, 64), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,4,0}
#define DM_M2_FMS_DEF {"FMST",1,0,{PD("1FRQ", F_INT, 0, 127, 61), PD("1FIN", F_INT, 0, 127, 64), PD("1ENV", F_INT, 0, 127, 80), PD("1FB", F_INT, 0, 127, 32), PD("2FRQ", F_INT, 0, 127, 82), PD("2VOL", F_INT, 0, 127, 64), PD("TONE", F_INT, 0, 127, 64), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,5,0}
#define DM_M2_FMD_DEF {"FMDYN",1,0,{PD("1FRQ", F_INT, 0, 127, 64), PD("1FEN", F_INT, 0, 127, 64), PD("1VOL", F_INT, 0, 127, 64), PD("1VEN", F_INT, 0, 127, 0), PD("2FRQ", F_INT, 0, 127, 74), PD("2ENV", F_INT, 0, 127, 80), PD("2FB", F_INT, 0, 127, 32), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,6,0}
#define DM_M2_SAW_DEF {"SAW2",1,0,{PD("UNIL", F_INT, 0, 127, 0), PD("UNIW", F_INT, 0, 127, 0), PD("UNIX", F_INT, 0, 127, 0), PD("DRFT", F_INT, 0, 127, 0), PD("SUBX", F_INT, 0, 127, 0), PD("SUB1", F_INT, 0, 127, 0), PD("SUB2", F_INT, 0, 127, 0), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,7,0}
#define DM_M2_ENS_DEF {"ENS",1,0,{PD("PCH2", F_INT, 0, 127, 85), PD("PCH3", F_INT, 0, 127, 100), PD("PCH4", F_INT, 0, 127, 120), PD("WAVE", F_INT, 0, 127, 0), PD("PW", F_INT, 0, 127, 64), PD("CHRL", F_INT, 0, 127, 0), PD("CHRW", F_INT, 0, 127, 64), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,8,0}
#define DM_M2_WAV_DEF {"WAVE",1,0,{PD("WAVE", F_INT, 0, 127, 40), PD("SCAN", F_INT, 0, 127, 64), PD("STIM", F_INT, 0, 127, 64), PD("SYNC", F_INT, 0, 127, 0), PD("BITS", F_INT, 0, 127, 0), PD("DTUN", F_INT, 0, 127, 70), PD("MIX2", F_INT, 0, 127, 0), PD("STEP", F_INT, 0, 127, 0)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,9,0}
#define DM_M2_VOX_DEF {"VOX2",1,0,{PD("VOC1", F_INT, 0, 127, 64), PD("VOC2", F_INT, 0, 127, 64), PD("V-SW", F_INT, 0, 127, 127), PD("VOIC", F_INT, 0, 127, 0), PD("CONS", F_INT, 0, 127, 0), PD("CLEN", F_INT, 0, 127, 64), PD("CVOL", F_INT, 0, 127, 64), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,10,0}
#define DM_M2_FMP_DEF {"FMPAR",1,0,{PD("1FRQ", F_INT, 0, 127, 61), PD("1ENV", F_INT, 0, 127, 64), PD("2FRQ", F_INT, 0, 127, 82), PD("2ENV", F_INT, 0, 127, 64), PD("3FRQ", F_INT, 0, 127, 104), PD("3ENV", F_INT, 0, 127, 80), PD("TONE", F_INT, 0, 127, 64), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,11,0}
#define DM_M2_PUL_DEF {"PUL2",1,0,{PD("UNIL", F_INT, 0, 127, 0), PD("UNIW", F_INT, 0, 127, 0), PD("SUB1", F_INT, 0, 127, 0), PD("SUB2", F_INT, 0, 127, 0), PD("PW", F_INT, 0, 127, 64), PD("PWAD", F_INT, 0, 127, 0), PD("PWRS", F_INT, 0, 127, 0), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,12,0}
#define DM_M2_FXC_DEF {"CHO2",1,0,{PD("DEL", F_INT, 0, 127, 40), PD("DEP", F_INT, 0, 127, 50), PD("SPD", F_INT, 0, 127, 30), PD("MIX", F_INT, 0, 127, 64), PD("FB", F_INT, 0, 127, 64), PD("WID", F_INT, 0, 127, 64), PD("LP", F_INT, 0, 127, 127), PD("INP", F_INT, 0, 127, 100)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,13,1}
#define DM_M2_FXR_DEF {"RING2",1,0,{PD("WAVE", F_INT, 0, 127, 0), PD("EXT", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("MIX", F_INT, 0, 127, 127), PD("-", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("INP", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,14,1}
#define DM_M2_FXD_DEF {"DYN2",1,0,{PD("ATK", F_INT, 0, 127, 20), PD("REL", F_INT, 0, 127, 60), PD("THRS", F_INT, 0, 127, 64), PD("MIX", F_INT, 0, 127, 127), PD("RAT", F_INT, 0, 127, 64), PD("GAIN", F_INT, 0, 127, 64), PD("RMS", F_INT, 0, 127, 0), PD("INP", F_INT, 0, 127, 100)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,15,1}
#define DM_M2_GNOI_DEF {"NOISE",1,0,{PD("ST", F_INT, 0, 127, 0), PD("RED", F_INT, 0, 127, 0), PD("STON", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("-", F_INT, 0, 127, 0), PD("TUNE", F_INT, 0, 127, 64)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,16,0}
#define DM_M2_DKIK_DEF {"DKICK",1,0,{PD("WAVE", F_INT, 0, 127, 0), PD("PDEP", F_INT, 0, 127, 44), PD("PDEC", F_INT, 0, 127, 26), PD("PCRV", F_INT, 0, 127, 21), PD("FM", F_INT, 0, 127, 0), PD("FMFR", F_INT, 0, 127, 64), PD("SNAP", F_INT, 0, 127, 0), PD("CLIK", F_INT, 0, 127, 16)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,17,0}
#define DM_M2_DSNR_DEF {"DSNAR",1,0,{PD("TONE", F_INT, 0, 127, 40), PD("PDEP", F_INT, 0, 127, 81), PD("PDEC", F_INT, 0, 127, 14), PD("NOIS", F_INT, 0, 127, 127), PD("MIX", F_INT, 0, 127, 61), PD("NFLT", F_INT, 0, 127, 64), PD("CLAP", F_INT, 0, 127, 0), PD("CSPC", F_INT, 0, 127, 6)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,18,0}
#define DM_M2_DHAT_DEF {"DHAT",1,0,{PD("CARR", F_INT, 0, 127, 54), PD("M1FR", F_INT, 0, 127, 60), PD("M1", F_INT, 0, 127, 127), PD("M2FR", F_INT, 0, 127, 127), PD("M2", F_INT, 0, 127, 127), PD("WAVE", F_INT, 0, 127, 0), PD("CDEC", F_INT, 0, 127, 5), PD("OPEN", F_INT, 0, 127, 0)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,19,0}
#define DM_M2_DCYM_DEF {"DCYMB",1,0,{PD("CARR", F_INT, 0, 127, 127), PD("M1FR", F_INT, 0, 127, 64), PD("M1", F_INT, 0, 127, 0), PD("M2FR", F_INT, 0, 127, 64), PD("M2", F_INT, 0, 127, 0), PD("WAVE", F_INT, 0, 127, 110), PD("ATK", F_INT, 0, 127, 0), PD("DEC", F_INT, 0, 127, 107)},mmen_trigger,mmen_render,1,mmen_glide,mmen_gate,20,0}
