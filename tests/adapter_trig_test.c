/* SPDX-License-Identifier: GPL-3.0-only
 * Independent trigger-lane tests against the MONO2K reference. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m2k_core.c"
#include "../firmware/src/mm_adapter.h"
static unsigned samples;
static void differential(unsigned machine,unsigned skip,int active,int slide)
{
 const int k=2;uint8_t p[71];mma_track_t a;int16_t line[1024],modline[1024],zero[16]={0};
 m2k_init(1);memcpy(p,PAT(cur_pat)->kit[k],71);memcpy(p,MACH_DEF[machine],8);
 p[P_ATK]=19;p[P_HOLD]=127;p[P_DEC]=127;p[P_REL]=40;p[P_FATK]=27;p[P_FDEC]=70;
 memset(&trk[k],0,sizeof trk[k]);memset(ens_mem[k],0,sizeof ens_mem[k]);memset(mod_mem[k],0,sizeof mod_mem[k]);
 trk[k].lfsr=0x1234567u+k*7919u;trk[k].lfsr2=0x9E3779B9u+k*104729u;
 trk[k].mach=machine;trk[k].gate_left=-1;PAT(cur_pat)->machine[k]=machine;PAT(cur_pat)->glob[G_ACNT]=63;
 memcpy(trk[k].eff,p,71);hold_bpm=137;mma_init(&a,k,machine,line,modline);
 if(active){voice_on(k,60,120,0);mma_trigger(&a,p,60,120,0,-1);}
 for(int b=0;b<48;b++) {
  if(b==8){
   unsigned source_skip=(skip&MMA_NOAMP?ST_NOAMP:0)|(skip&MMA_NOFLT?ST_NOFLT:0)|ST_NOLFO;
   if(!(skip&MMA_NOAMP))trk[k].gate_left=12;
   voice_trig(k,67,99,slide,source_skip);mma_trigger_mask(&a,p,67,99,slide,12,skip);
  }
  int32_t l[16]={0},r[16]={0},ds[16]={0},rs[16]={0},al[16],ar[16],ads[16],ars[16];
  memset(tout[k-1],0,sizeof tout[k-1]);memset(tvol[k-1],0,sizeof tvol[k-1]);memset(tout[k-2],0,sizeof tout[k-2]);
  memset(eside[k],0,sizeof eside[k]);memset(bside[k],0,sizeof bside[k]);
  track_block(k,l,r,ds,rs);assert(mma_render(&a,p,137,63,0,zero,zero,al,ar,ads,ars));
  assert(!memcmp(l,al,sizeof l)&&!memcmp(r,ar,sizeof r)&&!memcmp(ds,ads,sizeof ds)&&!memcmp(rs,ars,sizeof rs));
  assert(a.aenv==trk[k].aenv&&a.astage==trk[k].astage&&a.fenv==trk[k].fenv&&a.fstage==trk[k].fstage);
  assert(a.gate==trk[k].gate&&a.gate_left==trk[k].gate_left&&a.sounding==trk[k].sounding);samples+=16;
 }
 if(!active&&(skip&MMA_NOAMP)){assert(!a.sounding&&!a.astage);if(!(skip&MMA_NOFLT))assert(a.fenv);}
}
static void lane_invariants(void)
{
 mma_track_t a;uint8_t raw[71]={0};int16_t line[1024],modline[1024];
 mma_init(&a,0,MM2_GSIN,line,modline);raw[MMA_HOLD]=raw[MMA_DEC]=127;
 for(unsigned skip=0;skip<4;skip++)for(int slide=0;slide<2;slide++){
  mma_trigger(&a,raw,60,110,0,23);a.aenv=12345;a.fenv=67890;a.fstage=2;
  a.gate=0;a.let_go=1;a.gate_left=7;
  mma_track_t before=a;
  mma_trigger_mask(&a,raw,64,88,slide,99,skip);
  assert(a.note==64&&a.vel==88&&a.ptarget==64*64);
  if(skip&MMA_NOAMP){
   assert(a.gate==before.gate&&a.gate_left==before.gate_left&&a.let_go==before.let_go);
   assert(a.aenv==before.aenv&&a.astage==before.astage&&a.sounding==before.sounding);
   assert(!memcmp(&a.machine,&before.machine,sizeof a.machine));
  }
  if((skip&MMA_NOFLT)||(slide&&!(skip&MMA_NOAMP)))assert(a.fenv==before.fenv&&a.fstage==before.fstage);
  else assert(a.fenv==0&&a.fstage==1);
 }
}
int main(void)
{
 for(unsigned m=0;m<19;m++)for(unsigned skip=0;skip<4;skip++)for(int active=0;active<2;active++) {
  differential(m,skip,active,0);
  /* NOAMP+active slide intentionally fixes the reference's regate shortcut;
   * its independent lane invariants are checked separately below. */
  if(!(skip&MMA_NOAMP)||!active)differential(m,skip,active,1);
 }
 lane_invariants();printf("trigger lanes: %u donor-identical samples; NOAMP slide/gate invariants pass\n",samples);
 return 0;
}
