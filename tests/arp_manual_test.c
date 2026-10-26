/* SPDX-License-Identifier: GPL-3.0-only
 * Manual ARP through production MIDI, trigger lanes and mixer. */
#include "ui_host.h"
#include <assert.h>
static track_t *fresh(unsigned mode)
{
 ui_host_init();for(unsigned k=0;k<NTRK;k++){drum_set_model(&trk[k],DM_MMGND);memset(trk[k].step,0,sizeof trk[k].step);}
 track_t*t=&trk[0];drum_set_model(t,DM_M2_GSIN);mmen_default(t);
 t->p[P_AATK]=0;t->p[P_AHOLD]=t->p[P_ADEC]=127;t->p[P_AREL]=0;t->p[P_ARPM]=0;
 t->seq.arp_mode_order=(uint8_t)(mode|(MMA_PLAY_UP<<3));t->seq.arp_len=4;t->seq.arp_mask[0]=15;t->seq.arp_mask[1]=0;
 t->seq.arp_flags=0;t->seq.arp_speed=6;
 song.sel=0;song.seq_mode=3;song.octave=0;return t;
}
static void midi(unsigned on,unsigned note)
{
 unsigned status=on?0x90:0x80;
 midi_enqueue((status>>4)|(status<<8)|(note<<16)|((on?100u:0u)<<24),0);render_mix(0,0,CTL);
}
static void next(track_t*t){t->arp_pos=0;arp_tick(t,CTL);}
static void held_modes(void)
{
 track_t*t=fresh(MMA_MODE_KEY);midi(1,60);assert(t->arp_on&&mmen_state[0].note==60);
 midi(1,64);next(t);assert(mmen_state[0].note==60);next(t);assert(mmen_state[0].note==64);midi(0,60);assert(mmarp_nheld[0]==1&&mmen_state[0].gate);
 midi(0,64);assert(!t->arp_on&&!mmarp_nheld[0]&&!mmen_state[0].gate);
 t=fresh(MMA_MODE_SID);midi(1,60);assert(!t->arp_on&&mmen_state[0].gate);
 midi(1,64);assert(t->arp_on);midi(0,60);midi(0,64);assert(t->arp_on&&mmarp_nheld[0]==2&&mmen_state[0].gate);
 next(t);assert(mmen_state[0].note==64);midi(1,72);assert(!t->arp_on&&mmarp_nheld[0]==1&&mmen_state[0].note==72);
 midi(1,76);midi(0,72);midi(0,76);assert(t->arp_on);seq_stop();assert(!t->arp_on&&!mmarp_nheld[0]&&!mmen_state[0].gate);
 t=fresh(MMA_MODE_ADD);midi(1,60);midi(1,64);midi(0,64);assert(t->arp_on&&mmarp_nheld[0]==2);
 midi(1,67);midi(0,67);assert(mmarp_nheld[0]==3);midi(0,60);assert(!t->arp_on&&!mmarp_nheld[0]&&!mmen_state[0].gate);
 t=fresh(MMA_MODE_OFF);midi(1,60);assert(!t->arp_on&&mmen_state[0].gate);midi(0,60);assert(!mmen_state[0].gate);
}
static void physical_keys(void)
{
 for(unsigned mode=MMA_MODE_KEY;mode<=MMA_MODE_ADD;mode++){
  track_t*t=fresh(mode);
  fm1_in.notes=1u<<7;render_mix(0,0,CTL);
  fm1_in.notes=(1u<<7)|(1u<<11);render_mix(0,0,CTL);
  assert(t->arp_on&&mmarp_nheld[0]==2);
  fm1_in.notes=1u<<7;render_mix(0,0,CTL);
  assert(mmarp_nheld[0]==(mode==MMA_MODE_KEY?1:2));
  fm1_in.notes=0;render_mix(0,0,CTL);
  assert(!!t->arp_on==(mode==MMA_MODE_SID));
  assert(!!mmen_state[0].gate==(mode==MMA_MODE_SID));
  seq_stop();assert(!t->arp_on&&!mmen_state[0].gate);
 }
}
static void rhythm_and_lanes(void)
{
 for(unsigned flags=0;flags<8;flags++){
  track_t*t=fresh(MMA_MODE_KEY);t->seq.arp_flags=(uint8_t)flags;t->seq.arp_mask[0]=6;
  t->seq.arp_offset[1]=7;t->seq.arp_offset[2]=-12;t->p[P_LFO1+LF_TRIG]=32;t->p[P_L1PHS]=0;
  mmarp_held_on(t,60);next(t);assert(!t->v[0].active&&mmarp_first[0]); /* muted first position */
  next(t);assert(t->v[0].active&&mmen_state[0].note==67&&!mmarp_first[0]);
  assert(mmen_state[0].astage==1&&mmen_state[0].fstage==1); /* first audible hit always all lanes */
  mmen_state[0].aenv=12345;mmen_state[0].fenv=67890;mmen_state[0].fstage=2;
  mmen_lfo_state.voice[0][0].phase=1234567;t->v[0].t=777;t->gate_n=1234;
  next(t);assert(mmen_state[0].note==48&&t->gate_n==1234);
  if(flags&ST_NOAMP){assert(mmen_state[0].aenv==12345&&t->v[0].t==777);}else assert(mmen_state[0].aenv==0&&t->v[0].t==0);
  if(flags&ST_NOFLT)assert(mmen_state[0].fenv==67890&&mmen_state[0].fstage==2);else assert(mmen_state[0].fenv==0&&mmen_state[0].fstage==1);
  assert(mmen_lfo_state.voice[0][0].phase==((flags&ST_NOLFO)?1234567u:0u));
  uint32_t age=t->v[0].age;next(t);assert(t->v[0].age==age); /* position3 muted */
 }
}
int main(void){held_modes();physical_keys();rhythm_and_lanes();puts("manual ARP: MIDI KEY/SID/ADD/OFF, release/STOP, rhythm/offset and first/subsequent trigger lanes pass");}
