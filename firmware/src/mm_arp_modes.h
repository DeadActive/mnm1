/* SPDX-License-Identifier: GPL-3.0-only
 * Held-note ARP helpers. Manual-derived behavior, without hardware validation.
 * Caller owns note[8], count, and down bits indexed by insertion order. */
#ifndef MM_ARP_MODES_H
#define MM_ARP_MODES_H
#include <stdint.h>
enum { MMA_MODE_OFF, MMA_MODE_KEY, MMA_MODE_SID, MMA_MODE_ADD };
enum { MMA_PLAY_TRUE, MMA_PLAY_UP, MMA_PLAY_DOWN, MMA_PLAY_CYCLE, MMA_PLAY_RND };
enum { MMA_KEYS_CHANGED=1u, MMA_KEYS_RESTART=2u };
static int mma_keys_active(unsigned mode,unsigned count)
{ return mode==MMA_MODE_SID?count>1:((mode==MMA_MODE_KEY||mode==MMA_MODE_ADD)&&count>0); }
static void mma_keys_stop(uint8_t note[8],uint8_t *count,uint8_t *down)
{ for(unsigned i=0;i<8;i++)note[i]=0;*count=0;*down=0; }
/* Stable compaction keeps TRUE order and remaps the corresponding down bits. */
static void mma_keys_prune(uint8_t note[8],uint8_t *count,uint8_t *down)
{
 unsigned n=0;uint8_t bits=0;
 for(unsigned i=0;i<*count&&i<8;i++)if((*down>>i)&1u){note[n]=note[i];bits|=(uint8_t)(1u<<n);n++;}
 *count=(uint8_t)n;*down=bits;
}
static unsigned mma_keys_on(uint8_t note[8],uint8_t *count,uint8_t *down,unsigned mode,unsigned key)
{
 if(mode<MMA_MODE_KEY||mode>MMA_MODE_ADD||key>127||*count>8)return 0;
 for(unsigned i=0;i<*count;i++)if(note[i]==key&&((*down>>i)&1u))return 0;
 unsigned restart=!*down;
 if(mode==MMA_MODE_SID)mma_keys_prune(note,count,down);
 for(unsigned i=0;i<*count;i++)if(note[i]==key){*down|=(uint8_t)(1u<<i);return MMA_KEYS_CHANGED;}
 /* The ninth distinct key is rejected, including its eventual key-off.
  * Physical keys beyond this fixed capacity cannot be represented. */
 if(*count==8)return 0;
 note[*count]=(uint8_t)key;*down|=(uint8_t)(1u<<*count);(*count)++;
 return MMA_KEYS_CHANGED|(restart?MMA_KEYS_RESTART:0);
}
static unsigned mma_keys_off(uint8_t note[8],uint8_t *count,uint8_t *down,unsigned mode,unsigned key)
{
 if(*count>8)return 0;
 for(unsigned i=0;i<*count;i++)if(note[i]==key){
  if(!((*down>>i)&1u))return 0;
  *down&=(uint8_t)~(1u<<i);
  if(mode==MMA_MODE_KEY)mma_keys_prune(note,count,down);
  else if(mode==MMA_MODE_ADD&&!*down)mma_keys_stop(note,count,down);
  return MMA_KEYS_CHANGED;
 }
 return 0;
}
static uint32_t mma_arp_random(uint32_t *state)
{if(!*state)*state=0x2000u;*state^=*state<<13;*state^=*state>>17;*state^=*state<<5;return *state;}
/* Range is a literal octave count, clamped to the existing 1..4 range.
 * OJMP assumption: advance the octave boundary within the full chord rather
 * than discard notes. Wrap after count*range positions; oversize OJMP clamps
 * to count-1. The manual does not specify wrap or CYCLE/RND interaction;
 * this deterministic rule needs hardware verification before a fidelity claim.
 * Return an unclamped MIDI note; caller applies transpose/scale/range policy. */
static int32_t mma_arp_next(const uint8_t note[8],unsigned count,unsigned order,unsigned range,
                          unsigned ojmp,uint32_t *index,uint32_t *random)
{
 if(!count)return 0;if(count>8)count=8;if(range<1)range=1;if(range>4)range=4;
 uint8_t sorted[8];for(unsigned i=0;i<count;i++)sorted[i]=note[i];
 if(order!=MMA_PLAY_TRUE)for(unsigned i=1;i<count;i++)for(unsigned j=i;j&&sorted[j-1]>sorted[j];j--){uint8_t v=sorted[j];sorted[j]=sorted[j-1];sorted[j-1]=v;}
 unsigned total=count*range,p;
 if(order==MMA_PLAY_RND)p=mma_arp_random(random)%total;
 else if(order==MMA_PLAY_CYCLE){unsigned period=total>1?2*total-2:1,q=*index%period;p=q<total?q:period-q;*index=(q+1)%period;}
 else {p=*index%total;*index=(p+1)%total;if(order==MMA_PLAY_DOWN)p=total-1-p;}
 if(ojmp>=count)ojmp=count-1;
 return sorted[p%count]+12*(int32_t)(((p+ojmp)/count)%range);
}
#endif
