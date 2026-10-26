/* SPDX-License-Identifier: GPL-3.0-only
 * Reference: MONO2K by Inigo Hernandez, GPL-3.0-only.
 * https://github.com/InigoHernandez/mono2k
 * cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
 * -Wno-unused-function -I../mono2k/firmware/src -I../mono2k/firmware/gen
 * tests/busfx_port_test.c -o /tmp/busfx_port_test
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m2k_core.c"
#include "../firmware/src/mm_busfx.h"
_Static_assert(MMB_TAPE_CELLS*sizeof(int16_t)<=8192,"tape buffer budget");
_Static_assert(MMB_REVERB_CELLS*sizeof(int16_t)<=8192,"reverb buffer budget");
static uint32_t test_rng=19;
static int32_t next_sample(void) {test_rng=test_rng*1664525u+1013904223u;return (int32_t)(test_rng>>16)-32768;}
static unsigned long checks;
static void compare(unsigned mode,unsigned amount)
{
 mmb_tape_t tape; mmb_reverb_t verb; mmb_crunch_t crunch; mmb_hiss_t hiss;
 int16_t tape_memory[MMB_TAPE_CELLS],verb_memory[MMB_REVERB_CELLS];
 m2k_init(1);
 memset(&bus,0,sizeof bus);bus.hs=0xACE1u;
 memset(tp_buf,0,sizeof tp_buf);memset(cb_buf,0,sizeof cb_buf);memset(ap_buf,0,sizeof ap_buf);
 memset(&perf,0,sizeof perf);perf.kill=32767;
 memset(PAT(cur_pat)->glob,0,sizeof PAT(cur_pat)->glob);
 mmb_tape_init(&tape,tape_memory);mmb_reverb_init(&verb,verb_memory);
 mmb_crunch_init(&crunch);mmb_hiss_init(&hiss);
 uint8_t *g=PAT(cur_pat)->glob;
 g[G_TAPE]=(mode==0||mode==4)?amount:0;
 g[G_VERB]=(mode==1||mode==4)?amount:0;
 g[G_ADPC]=(mode==2||mode==4)?amount:0;
 g[G_HISS]=(mode==3||mode==4)?amount:0;
 g[G_DAMP]=amount;g[G_DRIV]=amount;master_vol=127;
 for(unsigned block=0;block<8192;block++) {
  int32_t l[16],r[16],pl[16],pr[16],send[16];int16_t actual[32],expected[32];
  /* Impulses, full-scale noise, silence and changing live controls. */
  for(unsigned i=0;i<16;i++) {
   l[i]=block<1024?next_sample():block==1024&&i==0?32767:0;
   r[i]=block<1024?next_sample():block==1024&&i==0?-32767:0;
   send[i]=(l[i]+r[i])/2;pl[i]=l[i];pr[i]=r[i];
  }
  if(block==512)g[G_DAMP]=127-amount;
  if(block==2048)g[G_DAMP]=127;
  tape_rolling=block<2048;
  bus_block(l,r,send,expected);
  mmb_reverb_block(&verb,g[G_VERB],g[G_DAMP],send,pl,pr);
  mmb_tape_block(&tape,g[G_TAPE],pl,pr);
  mmb_crunch_block(&crunch,g[G_ADPC],pl,pr);
  mmb_hiss_block(&hiss,g[G_HISS],tape_rolling,pl,pr);
  mmb_drive_block(g[G_DRIV],master_vol,pl,pr,actual);
  assert(memcmp(actual,expected,sizeof actual)==0);checks+=32;
  assert(tape.tw==bus.tw&&tape.wow==bus.wow&&tape.flut==bus.flut);
  assert(tape.tlp_l==bus.tlp_l&&tape.tlp_r==bus.tlp_r);
  assert(memcmp(tape_memory,tp_buf,sizeof tp_buf)==0);
  assert(memcmp(verb_memory,cb_buf,sizeof cb_buf)==0);
  assert(memcmp(verb_memory+2468,ap_buf,sizeof ap_buf)==0);
  assert(memcmp(verb.cb_lp,bus.cb_lp,sizeof bus.cb_lp)==0);
  assert(memcmp(verb.cb_r,bus.cb_r,sizeof bus.cb_r)==0);
  assert(memcmp(verb.cb_pos,bus.cb_pos,sizeof bus.cb_pos)==0);
  assert(memcmp(verb.ap_pos,bus.ap_pos,sizeof bus.ap_pos)==0);
  assert(memcmp(crunch.ad_pred,bus.ad_pred,sizeof bus.ad_pred)==0);
  assert(memcmp(crunch.ad_idx,bus.ad_idx,sizeof bus.ad_idx)==0);
  assert(memcmp(crunch.ad_hold,bus.ad_hold,sizeof bus.ad_hold)==0);
  assert(crunch.ad_acc==bus.ad_acc);
  assert(hiss.hs==bus.hs&&hiss.hlp==bus.hlp&&hiss.crackle==bus.crackle&&hiss.hgain==bus.hgain);
 }
 assert(hiss.hgain==0);
}
static void invariants(void)
{
 mmb_tape_t t; mmb_reverb_t v; mmb_crunch_t c; mmb_hiss_t h;
 int32_t l[16]={0},r[16]={0},send[16]={0};
 int16_t tb[MMB_TAPE_CELLS],vb[MMB_REVERB_CELLS],out[32];
 mmb_tape_init(&t,tb);mmb_reverb_init(&v,vb);mmb_crunch_init(&c);mmb_hiss_init(&h);
 for(unsigned b=0;b<1024;b++) {
  mmb_tape_block(&t,127,l,r);mmb_reverb_block(&v,127,127,send,l,r);
  mmb_hiss_block(&h,127,0,l,r);mmb_drive_block(127,127,l,r,out);
  for(unsigned i=0;i<32;i++) assert(out[i]==0);
 }
 /* Adaptive quantization can retain tiny source-matched silence residuals. */
 mmb_crunch_block(&c,127,l,r);
 for(unsigned i=0;i<16;i++)assert(l[i]>=-8&&l[i]<=8&&r[i]>=-8&&r[i]<=8);
 mmb_tape_init(&t,NULL);mmb_reverb_init(&v,NULL);
 mmb_tape_block(&t,255,l,r);mmb_reverb_block(&v,255,255,send,l,r);
 mmb_hiss_block(&h,255,1,l,r);mmb_crunch_block(&c,255,l,r);mmb_drive_block(255,255,l,r,out);
}
int main(void)
{
 unsigned amounts[]={0,1,16,63,64,126,127};
 for(unsigned mode=0;mode<5;mode++)for(unsigned i=0;i<sizeof amounts/sizeof amounts[0];i++)compare(mode,amounts[i]);
 invariants();
 printf("bus FX: %lu samples bit-identical; state tape=%zu reverb=%zu crunch=%zu hiss=%zu; buffers=8192/7298 bytes\n",checks,sizeof(mmb_tape_t),sizeof(mmb_reverb_t),sizeof(mmb_crunch_t),sizeof(mmb_hiss_t));
 return 0;
}
