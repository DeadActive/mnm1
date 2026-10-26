/* SPDX-License-Identifier: GPL-3.0-only
 * MONO2K reference by Inigo Hernandez, GPL-3.0-only.
 * cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
 * -Wno-unused-function -I../mono2k/firmware/src -I../mono2k/firmware/gen
 * tests/adapter_test.c -o /tmp/adapter_test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m2k_core.c"
#include "../firmware/src/mm_adapter.h"
static uint32_t test_rng=17;
static uint8_t random7(void){test_rng=test_rng*1664525u+1013904223u;return test_rng>>25;}
static unsigned long checks;
static void compare(int machine,int mode)
{
 const int ti=2;uint8_t p[71];int32_t deltas[71]={0};mma_track_t a;int16_t line[1024],modline[1024];
 m2k_init(1);memcpy(p,PAT(cur_pat)->kit[ti],71);memcpy(p,MACH_DEF[machine],8);
 if(mode)for(unsigned i=0;i<71;i++)p[i]=mode==1?0:mode==2?127:random7();
 /* Exercise all stages while preserving an audible envelope. */
 p[P_ATK]=mode>2?20:0;p[P_HOLD]=127;p[P_DEC]=127;p[P_REL]=20;
 if(mode==4){p[P_HOLD]=20;p[P_DEC]=40;}
 if(mode==5){p[P_HOLD]=0;p[P_DEC]=0;}
 if(mode==6){p[P_HOLD]=0;p[P_DEC]=127;}
 p[P_LEVL]=127;p[P_VOL]=100;p[P_DSND]=96;p[P_SPU]=64;
 if(!mode){p[P_EQG]=64;p[P_SRR]=p[P_BITS]=p[P_FOLD]=p[P_XOR]=p[P_MODM]=0;}
 if(mode>=3){p[P_MODT]=(mode%3)*43;p[P_MODM]=64;}
 memset(&trk[ti],0,sizeof trk[ti]);memset(ens_mem[ti],0,sizeof ens_mem[ti]);memset(mod_mem[ti],0,sizeof mod_mem[ti]);
 trk[ti].lfsr=0x1234567u+ti*7919u;trk[ti].lfsr2=0x9E3779B9u+ti*104729u;
 trk[ti].mach=machine;trk[ti].gate_left=-1;
 PAT(cur_pat)->machine[ti]=machine;PAT(cur_pat)->glob[G_ACNT]=63;
 memcpy(trk[ti].eff,p,71);hold_bpm=137;
 mma_init(&a,ti,machine,line,modline);
 if(mode>=3)for(int k=0;k<71;k++)deltas[k]=(int32_t)random7()*17-1024;
 memcpy(trk[ti].mod,deltas,sizeof deltas);mma_modulation(&a,deltas);
 voice_on(ti,60,120,0);mma_trigger(&a,p,60,120,0,-1);
 for(int b=0;b<1024;b++) {
  int32_t l[16]={0},r[16]={0},ds[16]={0},rs[16]={0},al[16],ar[16],ads[16],ars[16];
  int16_t input[16],carrier[16];
  if(b==160||b==192||b==224||b==320||b==352) {
   /* Disable/re-enable EQ and stereo after the mid coefficients changed.
    * Reusing the old gain/frequency after bypass must retain filter state. */
   p[P_EQG]=(b==160||b==320)?64:b==192?0:127;
   p[P_EQF]=21;deltas[P_EQG]=deltas[P_EQF]=0;
   p[P_BASE]=b==192?127:0;p[P_WDTH]=b==192?0:127;
   if(machine==M_ENS){p[5]=b==160?0:127;p[6]=127;deltas[5]=deltas[6]=0;}
   if(machine==M_GNOI){p[0]=b==160?0:127;p[2]=127;deltas[0]=deltas[2]=0;}
   memcpy(trk[ti].eff,p,71);memcpy(trk[ti].mod,deltas,sizeof deltas);
  }
  if(b==128&&mode>=3){for(int k=0;k<71;k++)deltas[k]=(int32_t)random7()*63-4096;memcpy(trk[ti].mod,deltas,sizeof deltas);}
  if(b==300){voice_off(ti);mma_gate_end(&a,1);}
  if(b==400){voice_on(ti,57,120,0);mma_trigger(&a,p,57,120,0,-1);}
  if(b==64){voice_on(ti,72,120,1);mma_trigger(&a,p,72,120,1,-1);}
  if(b==256){voice_on(ti,48,120,0);mma_trigger(&a,p,48,120,0,-1);}
  if(b==512){trk[ti].gate_left=1;a.gate_left=1;}
  if(b==768){voice_off(ti);mma_gate_end(&a,1);}
  for(int i=0;i<16;i++) {
   input[i]=sinp((uint32_t)(b*16+i)*24000000u)/2;carrier[i]=sinp((uint32_t)(b*16+i)*37000000u)/2;
   tout[ti-1][i]=tvol[ti-1][i]=input[i];tout[ti-2][i]=carrier[i];
   eside[ti][i]=bside[ti][i]=0;
  }
  trk[ti-1].pitch=43*64;
  track_block(ti,l,r,ds,rs);
  assert(mma_render(&a,p,137,63,43*64,input,carrier,al,ar,ads,ars));
  for(int i=0;i<16;i++) {
   if(l[i]!=al[i]||r[i]!=ar[i]||ds[i]!=ads[i]||rs[i]!=ars[i]){
    fprintf(stderr,"machine %d mode %d block %d sample %d: L %d/%d R %d/%d DS %d/%d RS %d/%d\n",machine,mode,b,i,l[i],al[i],r[i],ar[i],ds[i],ads[i],rs[i],ars[i]);assert(0);
   }
   assert(al[i]>=-32767&&al[i]<=32767&&ar[i]>=-32767&&ar[i]<=32767);
   assert(a.before_volume[i]==tout[ti][i]&&a.after_volume[i]==tvol[ti][i]);checks++;
  }
  assert(a.aenv==trk[ti].aenv&&a.fenv==trk[ti].fenv&&a.astage==trk[ti].astage&&a.pitch==trk[ti].pitch);
 }
}
int main(void)
{
 for(int m=0;m<19;m++)for(int mode=0;mode<9;mode++)compare(m,mode);
 mma_track_t a; uint8_t p[71]={0}; int32_t l[16],r[16],ds[16],rs[16];
 mma_init(&a,0,MM2_ENS,0,0);
 assert(!mma_render(&a,p,0,255,0,0,0,l,r,ds,rs));
 mma_init(&a,0,MM2_GSIN,0,0);p[MMA_MODM]=127;
 assert(!mma_render(&a,p,120,0,0,0,0,l,r,ds,rs));
 p[MMA_MODM]=0;assert(mma_render(&a,p,120,0,0,0,0,l,r,ds,rs));
 for(int i=0;i<16;i++)assert(!l[i]&&!r[i]&&!ds[i]&&!rs[i]);
 printf("adapter: %lu stereo+send+tap samples bit-identical; state %zu bytes; buffers4096 bytes\n",checks,sizeof(mma_track_t));return 0;
}
