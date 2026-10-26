/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 (Hortator fork): 2026 */
/* ARP (MNM-1's arpeggiator, per track: MODE SPEED RANGE CHORD on the ARP page). A note a gated model plays
 * (a step, a key, MIDI: drum_play) starts it: the note itself first, then every SPEED (a DIV at the tempo) the next
 * of the CHORD's notes over RANGE octaves, in MODE's order, each a new note (its AMP envelope from the start), for as
 * long as the note that started it is held (its gate open: the step's LEN, the key held). A slide or a legato key
 * moves the chord to the new note, the order goes on. MODE OFF, or a model without a gate (a drum): nothing. */
static const int8_t ARP_CHORD[12][5] = {   /* semitones over the note, -1 ends (params.c N_ARPC) */
    {0, 12, -1}, {0, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 11, -1}, {0, 3, 7, 10, -1},
    {0, 4, 7, 10, -1}, {0, 3, 6, -1}, {0, 4, 7, 14, -1}, {0, 3, 7, 14, -1}, {0, 5, 10, 15, -1},
};
enum { AM_OFF, AM_UP, AM_DOWN, AM_UPDN, AM_RND, AM_ORDR };
static uint8_t arp_self;                                 /* the ARP's own notes: drum_play starts no new run */

/* MONO2K live arpeggiator laws, GPL-3.0-only, Inigo Hernandez.
 * https://github.com/InigoHernandez/mono2k
 * Calibrated models use held notes, not the legacy fixed CHORD menu or steps.
 * arp_pos is the source countdown in Q8 samples for these models. */
#include "mm_arp_modes.h"
static uint8_t mmarp_held[NTRK][8],mmarp_nheld[NTRK];
static uint8_t mmarp_down[NTRK],mmarp_first[NTRK],mmarp_rhythm[NTRK];
static unsigned mmarp_manual_mode(const track_t *t){unsigned m=t->seq.arp_mode_order&7u;return m<=3?m:0;}
static int mmarp_active(const track_t *t){unsigned k=(unsigned)(t-trk);return k<NTRK&&mma_keys_active(mmarp_manual_mode(t),mmarp_nheld[k]);}
static int mmarp_keep_gate(const track_t *t){unsigned k=(unsigned)(t-trk);if(k>=NTRK)return 0;
 return (t->seq.arp_mode_order&7u)?(mmarp_down[k]||(mmarp_manual_mode(t)==MMA_MODE_SID&&mmarp_nheld[k]>1)):mmarp_nheld[k]!=0;}
static void mmarp_stop_manual(void){for(unsigned k=0;k<NTRK;k++)if(trk[k].seq.arp_mode_order&7u){mma_keys_stop(mmarp_held[k],&mmarp_nheld[k],&mmarp_down[k]);trk[k].arp_on=0;}}

static unsigned mmarp_mode(const track_t *t) { return (unsigned)clamp(t->p[P_ARPM],0,127)*6u>>7; }
static int mmarp_track(const track_t *t) { return mmen_is_model((unsigned)t->p[P_MODEL])||(t->seq.arp_mode_order&7u); }
static uint32_t mmarp_random(uint32_t *s) { if(!*s)*s=0x2000u;*s^=*s<<13;*s^=*s>>17;*s^=*s<<5;return *s; }
/* Ordered notes are repeated octave-major. ORDR preserves key insertion order;
 * all other modes sort the held keys first. Index remains within its cycle. */
static int32_t mmarp_next(const uint8_t *held,unsigned count,unsigned mode,unsigned range,
                         uint32_t *index,uint32_t *rng)
{
 uint8_t notes[8];if(!count)return 0;if(count>8)count=8;
 for(unsigned j=0;j<count;j++)notes[j]=held[j];
 if(mode!=AM_ORDR)for(unsigned j=1;j<count;j++)for(unsigned k=j;k>0&&notes[k-1]>notes[k];k--) {
  uint8_t v=notes[k];notes[k]=notes[k-1];notes[k-1]=v;
 }
 unsigned oct=1+((range>127?127:range)*4>>7),n=count*oct,i;
 switch(mode) {
 case AM_DOWN:i=*index%n;*index=(i+1)%n;i=n-1-i;break;
 case AM_UPDN:{unsigned period=n>1?2*n-2:1,q=*index%period;i=q<n?q:period-q;*index=(q+1)%period;break;}
 case AM_RND:i=mmarp_random(rng)%n;break;
 default:i=*index%n;*index=(i+1)%n;break;
 }
 return notes[i%count]+12*(int32_t)(i/count);
}
static uint32_t mmarp_period_q8(const track_t *t)
{
 if(t->seq.arp_mode_order&7u)return (beat_samples()*256u*(unsigned)(t->seq.arp_speed?t->seq.arp_speed:6))/24u;
 unsigned bpm=(unsigned)song.g[G_BPM];if(!bpm)bpm=1;
 uint32_t step=song.g[G_CLOCK]?beat_samples()*64u:169344000u/bpm;
 unsigned speed=(unsigned)clamp(t->p[P_ARPS],0,127)*4u>>7;
 return (step*(4u<<speed))>>3;
}
static void mmarp_held_on(track_t *t,unsigned note)
{
 unsigned k=mmen_index(t);if(k>=NTRK||!mmarp_track(t)||note>127)return;
 if(t->seq.arp_mode_order&7u){
  int was=mmarp_active(t);unsigned changed=mma_keys_on(mmarp_held[k],&mmarp_nheld[k],&mmarp_down[k],mmarp_manual_mode(t),note);
  if(changed&&(!was||(changed&MMA_KEYS_RESTART))){t->arp_k=t->arp_pos=0;mmarp_rhythm[k]=0;mmarp_first[k]=1;}
  t->arp_on=(uint8_t)mmarp_active(t);return;
 }
 for(unsigned i=0;i<mmarp_nheld[k];i++)if(mmarp_held[k][i]==note)return;
 if(mmarp_nheld[k]>=8)return;
 mmarp_held[k][mmarp_nheld[k]++]=(uint8_t)note;
 if(mmarp_nheld[k]==1){t->arp_k=0;t->arp_pos=0;}
 t->arp_nt=(int8_t)((int)note-60);t->arp_on=mmarp_mode(t)!=AM_OFF;
}
static void mmarp_held_off(track_t *t,unsigned note)
{
 unsigned k=mmen_index(t);if(k>=NTRK)return;
 if(t->seq.arp_mode_order&7u){mma_keys_off(mmarp_held[k],&mmarp_nheld[k],&mmarp_down[k],mmarp_manual_mode(t),note);t->arp_on=(uint8_t)mmarp_active(t);return;}
 unsigned j=0;for(unsigned i=0;i<mmarp_nheld[k];i++)if(mmarp_held[k][i]!=note)mmarp_held[k][j++]=mmarp_held[k][i];
 mmarp_nheld[k]=(uint8_t)j;if(!j)t->arp_on=0;
}
static void mmarp_reset(void) { memset(direct_midi_held,0,sizeof direct_midi_held);memset(multi_note_mask,0,sizeof multi_note_mask);memset(multi_panel_note,0,sizeof multi_panel_note);memset(mmarp_nheld,0,sizeof mmarp_nheld);memset(mmarp_down,0,sizeof mmarp_down);memset(mmarp_first,0,sizeof mmarp_first);memset(mmarp_rhythm,0,sizeof mmarp_rhythm); }
static void mmarp_manual_tick(track_t *t,uint32_t n)
{
 unsigned k=(unsigned)(t-trk);if(k>=NTRK||!mmarp_active(t)){t->arp_on=0;return;}t->arp_on=1;
 int32_t left=(int32_t)t->arp_pos-(int32_t)(n*256u);
 if(left>0){t->arp_pos=(uint32_t)left;return;}
 uint32_t period=mmarp_period_q8(t);left+=(int32_t)period;t->arp_pos=(uint32_t)(left>0?left:(int32_t)period);
 unsigned len=t->seq.arp_len?t->seq.arp_len:8,pos=mmarp_rhythm[k]%len;
 mmarp_rhythm[k]=(uint8_t)((pos+1)%len);
 int32_t note=mma_arp_next(mmarp_held[k],mmarp_nheld[k],t->seq.arp_mode_order>>3,
                           1+((t->seq.arp_flags>>3)&3),t->seq.arp_ojmp,&t->arp_k,&t->rng);
 if(!((t->seq.arp_mask[pos/8]>>(pos%8))&1u))return;
 unsigned flags=mmarp_first[k]?0:(t->seq.arp_flags&7u);mmarp_first[k]=0;
 uint32_t gate=t->gate_n;int8_t nt=t->nt;
 arp_self=1;drum_play_masked(t,t->rat_vel?t->rat_vel:100,clamp(note+t->seq.arp_offset[pos]-60,NT_MIN,NT_MAX),flags);arp_self=0;
 t->gate_n=gate;t->nt=nt;
}
static void mmarp_tick(track_t *t,uint32_t n)
{
 if(t->seq.arp_mode_order&7u){mmarp_manual_tick(t,n);return;}
 unsigned k=mmen_index(t),mode=mmarp_mode(t);
 if(k>=NTRK||!mode||!mmarp_nheld[k]){t->arp_on=0;return;}
 t->arp_on=1;
 /* Live parents own gate duration. Child notes never replace that countdown. */
 if(!dv_gate_open(t,&t->v[0])){t->arp_on=0;return;}
 int32_t left=(int32_t)t->arp_pos-(int32_t)(n*256u);
 if(left>0){t->arp_pos=(uint32_t)left;return;}
 left+=(int32_t)mmarp_period_q8(t);t->arp_pos=(uint32_t)left;
 int32_t note=mmarp_next(mmarp_held[k],mmarp_nheld[k],mode,(unsigned)clamp(t->p[P_ARPR],0,127),&t->arp_k,&t->rng);
 uint32_t gate=t->gate_n;int8_t nt=t->nt;
 arp_self=1;drum_play(t,t->rat_vel?t->rat_vel:100,clamp(note-60,NT_MIN,NT_MAX));arp_self=0;
 t->gate_n=gate;t->nt=nt;
}

static uint32_t arp_chord_n(const int16_t *p)
{
    const int8_t *c = ARP_CHORD[clamp(p[P_ARPC], 0, 11)];
    uint32_t n = 0;
    while (n < 5u && c[n] >= 0)
        n++;
    return n;
}

/* a note started by anything but the ARP: a run starts (or, gliding, the chord moves) */
static void arp_note(track_t *t, int glide)
{
    if (arp_self)
        return;
    if(t->seq.arp_mode_order&7u){t->arp_on=(uint8_t)mmarp_active(t);return;}
    if(mmarp_track(t)) {
        unsigned k=mmen_index(t);
        t->arp_on=k<NTRK&&mmarp_nheld[k]&&mmarp_mode(t)!=AM_OFF;
        return;
    }
    if (!t->p[P_ARPM] || !trk_model(t)->gate) {
        t->arp_on = 0;
        return;
    }
    t->arp_nt = t->nt;
    if (glide && t->arp_on)
        return;
    t->arp_on = 1;
    t->arp_k = 1;                                        /* the note itself was the first */
    t->arp_dir = 0;
    t->arp_pos = 0;
}

/* the offset of the run's k-th note on knobs p (k counts on for ever; the mode folds it); RND draws from *rng */
static int32_t arp_off(const int16_t *p, uint32_t k, uint32_t *rng)
{
    const int8_t *c = ARP_CHORD[clamp(p[P_ARPC], 0, 11)];
    uint32_t nc = arp_chord_n(p), r = (uint32_t)clamp(p[P_ARPR], 1, 4), n = nc * r, i;
    switch (clamp(p[P_ARPM], 0, AM_ORDR)) {
    case AM_DOWN:
        i = n - 1u - k % n;
        break;
    case AM_UPDN:
        i = n > 1u ? k % (2u * n - 2u) : 0u;
        i = i < n ? i : 2u * n - 2u - i;
        break;
    case AM_RND:
        *rng = *rng * 1664525u + 1013904223u;
        i = (*rng >> 16) % n;
        break;
    case AM_ORDR:                                        /* each chord note through the octaves, then the next */
        i = k % n;
        return c[i / r] + (int32_t)(i % r) * 12;
    default:
        i = k % n;
        break;
    }
    return c[i % nc] + (int32_t)(i / nc) * 12;
}
static int32_t arp_offset(track_t *t, uint32_t k) { return arp_off(t->p, k, &t->rng); }   /* the track's run */

/* every block: the run's notes whose time has come; it ends with the gate of the note that started it */
static void arp_tick(track_t *t, uint32_t n)
{
    uint32_t len;
    if(mmarp_track(t)){mmarp_tick(t,n);return;}
    if (!t->arp_on)
        return;
    if (!t->p[P_ARPM] || !dv_gate_open(t, &t->v[0])) {
        t->arp_on = 0;
        return;
    }
    len = div_period((uint32_t)t->p[P_ARPS], 0);
    t->arp_pos += n;
    if (t->arp_pos < len)
        return;
    t->arp_pos -= len;
    if (t->arp_pos >= len)
        t->arp_pos = 0;                                  /* (the tempo jumped: no burst of notes) */
    arp_self = 1;
    drum_play(t, t->rat_vel ? t->rat_vel : 100u, clamp(t->arp_nt + arp_offset(t, t->arp_k), NT_MIN, NT_MAX));
    arp_self = 0;
    t->nt = t->arp_nt;                                   /* the track's note stays the played one (KEYS, the roll) */
    t->arp_k++;
}
