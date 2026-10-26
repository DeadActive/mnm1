/* SPDX-License-Identifier: GPL-3.0-only
 * MONO2K reference by Inigo Hernandez, GPL-3.0-only.
 * Compile this file twice, with/without ARP_DONOR_REFERENCE, and compare stdout.
 * Donor build additionally needs -I../mono2k/firmware/src -I../mono2k/firmware/gen.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef ARP_DONOR_REFERENCE
#include "m2k_core.c"
#else
#define __attribute__(x)
#include "../firmware/src/core.h"
#undef __attribute__
#define MM_MODEL_FIRST 63
static unsigned event_count;static int event_note;static unsigned open_gate=1;
static int32_t clamp(int32_t x,int32_t lo,int32_t hi){return x<lo?lo:x>hi?hi:x;}
static int mmen_is_model(unsigned m){return m>=63&&m<82;}
static unsigned mmen_index(const track_t *t){for(unsigned i=0;i<NTRK;i++)if(t==&trk[i])return i;return NTRK;}
static void dummy_gate(track_t *t,dvoice_t *v,int o){(void)t;(void)v;(void)o;}
static const dmodel_t model={.gate=dummy_gate};
static const dmodel_t *trk_model(const track_t *t){(void)t;return &model;}
static int dv_gate_open(track_t *t,dvoice_t *v){(void)t;(void)v;return open_gate;}
static void drum_play(track_t *t,uint32_t vel,int32_t note){assert(vel==100);t->nt=note;event_note=note+60;event_count++;t->gate_n=999;}
static void drum_play_masked(track_t *t,uint32_t vel,int32_t note,unsigned flags){assert(!flags);drum_play(t,vel,note);}
static uint32_t beat_samples(void){return 2646000u/(unsigned)song.g[G_BPM];}
static uint32_t div_period(uint32_t div,uint32_t rem){(void)rem;return beat_samples()/(1u<<(div%4));}
#include "../firmware/src/arp.c"
#endif
static uint32_t hash=2166136261u;
static void add(unsigned x){hash=(hash^x)*16777619u;}
static void trace(unsigned mode,unsigned speed,unsigned range,unsigned count)
{
 static const uint8_t notes[8]={67,48,60,72,55,62,81,64};
 unsigned rawmode=(mode*128+5)/6,last=0;
#ifdef ARP_DONOR_REFERENCE
 m2k_init(1);sq.ktrk=0;sq.nheld=0;sq.playing=0;sq.arp_ctr=0;sq.arp_i=0;
 PAT(cur_pat)->glob[G_TMPO]=107;
 rng=0x2000;hold_bpm=137;sps_q8=169344000u/137;
 PAT(cur_pat)->kit[0][P_AMOD]=rawmode;PAT(cur_pat)->kit[0][P_ASPD]=speed;PAT(cur_pat)->kit[0][P_ARNG]=range;
 for(unsigned i=0;i<count;i++)live_on(notes[i],100);
 last=mon.trig[0];
#else
 memset(trk,0,sizeof trk);mmarp_reset();event_count=0;open_gate=1;
 song.g[G_BPM]=137;song.g[G_CLOCK]=0;
 track_t *t=&trk[0];t->model=MM_MODEL_FIRST;t->p[P_MODEL]=MM_MODEL_FIRST;t->p[P_ARPM]=rawmode;t->p[P_ARPS]=speed;t->p[P_ARPR]=range;t->rng=0x2000;t->gate_n=12345;
 for(unsigned i=0;i<count;i++)mmarp_held_on(t,notes[i]);
 assert(mmarp_period_q8(t)==((169344000u/137)*(4u<<(speed*4>>7))>>3));
#endif
 for(unsigned block=0;block<4096;block++) {
#ifdef ARP_DONOR_REFERENCE
  seq_block();unsigned now=mon.trig[0];int note=trk[0].note;
#else
  arp_tick(t,16);unsigned now=event_count;int note=event_note;assert(t->gate_n==12345);
#endif
  if(now!=last){add(block);add((unsigned)note);last=now;
#ifdef ARP_TRACE_DEBUG
   printf("%u %u %u %u %u %d\n",mode,speed,range,count,block,note);
#endif
  }
 }
}
#ifndef ARP_DONOR_REFERENCE
static void edges(void)
{
 track_t *t=&trk[0];memset(t,0,sizeof *t);mmarp_reset();t->model=MM_MODEL_FIRST;t->p[P_MODEL]=MM_MODEL_FIRST;
 for(int v=0;v<128;v++){t->p[P_ARPM]=v;assert(mmarp_mode(t)==(unsigned)v*6/128);}
 t->p[P_ARPM]=21;mmarp_held_on(t,60);assert(!t->arp_on);mmarp_held_off(t,60);
 t->p[P_ARPM]=22;mmarp_held_on(t,60);mmarp_held_on(t,60);assert(mmarp_nheld[0]==1);
 for(unsigned i=0;i<12;i++)mmarp_held_on(t,61+i);assert(mmarp_nheld[0]==8);
 for(unsigned i=0;i<12;i++)mmarp_held_off(t,60+i);assert(!mmarp_nheld[0]&&!t->arp_on);
 arp_note(t,0);assert(!t->arp_on); /* Sequencer notes do not start calibrated ARP. */
 mmarp_held_on(t,60);open_gate=0;unsigned before=event_count;arp_tick(t,32);assert(event_count==before&&!t->arp_on);open_gate=1;
 t->model=0;t->p[P_MODEL]=0;t->p[P_ARPM]=AM_UP;t->p[P_ARPC]=2;t->p[P_ARPR]=1;t->nt=0;
 arp_note(t,0);assert(t->arp_on&&t->arp_k==1);
 uint32_t seed=1;assert(arp_off(t->p,0,&seed)==0&&arp_off(t->p,1,&seed)==4&&arp_off(t->p,2,&seed)==7);
}
#endif
int main(void)
{
 static const unsigned edges8[]={0,31,32,63,64,95,96,127};
 for(unsigned mode=1;mode<6;mode++)for(unsigned s=0;s<8;s++)for(unsigned r=0;r<8;r++)for(unsigned n=1;n<=8;n+=7)trace(mode,edges8[s],edges8[r],n);
#ifndef ARP_DONOR_REFERENCE
 edges();
#endif
 printf("arp trace %08x\n",hash);return 0;
}
