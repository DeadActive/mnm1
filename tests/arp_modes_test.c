/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "../firmware/src/mm_arp_modes.h"
static void keys(void)
{
 uint8_t note[8]={0},n=0,down=0;
 assert(mma_keys_on(note,&n,&down,MMA_MODE_KEY,60)&MMA_KEYS_RESTART);
 mma_keys_on(note,&n,&down,MMA_MODE_KEY,67);mma_keys_on(note,&n,&down,MMA_MODE_KEY,64);
 assert(!mma_keys_on(note,&n,&down,MMA_MODE_KEY,67)&&n==3);
 mma_keys_off(note,&n,&down,MMA_MODE_KEY,67);assert(n==2&&down==3&&note[0]==60&&note[1]==64);
 mma_keys_off(note,&n,&down,MMA_MODE_KEY,60);mma_keys_off(note,&n,&down,MMA_MODE_KEY,64);assert(!n&&!down);
 mma_keys_on(note,&n,&down,MMA_MODE_SID,60);assert(!mma_keys_active(MMA_MODE_SID,n));
 mma_keys_on(note,&n,&down,MMA_MODE_SID,64);assert(mma_keys_active(MMA_MODE_SID,n));
 mma_keys_off(note,&n,&down,MMA_MODE_SID,60);assert(n==2&&down==2);
 mma_keys_on(note,&n,&down,MMA_MODE_SID,67);assert(n==2&&note[0]==64&&note[1]==67&&down==3);
 mma_keys_off(note,&n,&down,MMA_MODE_SID,64);mma_keys_off(note,&n,&down,MMA_MODE_SID,67);
 assert(n==2&&!down&&mma_keys_active(MMA_MODE_SID,n));
 assert(mma_keys_on(note,&n,&down,MMA_MODE_SID,72)&MMA_KEYS_RESTART);
 assert(n==1&&note[0]==72&&!mma_keys_active(MMA_MODE_SID,n));
 mma_keys_on(note,&n,&down,MMA_MODE_SID,76);mma_keys_stop(note,&n,&down);assert(!n&&!down);
 mma_keys_on(note,&n,&down,MMA_MODE_ADD,60);mma_keys_on(note,&n,&down,MMA_MODE_ADD,64);
 mma_keys_off(note,&n,&down,MMA_MODE_ADD,64);assert(n==2&&down==1);
 mma_keys_on(note,&n,&down,MMA_MODE_ADD,64);assert(n==2&&down==3);
 for(unsigned key=65;n<8;key++)mma_keys_on(note,&n,&down,MMA_MODE_ADD,key);
 assert(!mma_keys_on(note,&n,&down,MMA_MODE_ADD,100)&&n==8);
 assert(!mma_keys_off(note,&n,&down,MMA_MODE_ADD,100));
 for(unsigned i=1;i<8;i++)mma_keys_off(note,&n,&down,MMA_MODE_ADD,note[i]);
 assert(n==8&&down==1);mma_keys_off(note,&n,&down,MMA_MODE_ADD,60);assert(!n&&!down);
 assert(!mma_keys_on(note,&n,&down,MMA_MODE_OFF,60));assert(!mma_keys_on(note,&n,&down,MMA_MODE_KEY,128));
}
static void vector(unsigned order,unsigned range,unsigned jump,const int *want,unsigned len)
{
 const uint8_t note[8]={67,60,64};uint32_t index=0,rng=1;
 for(unsigned i=0;i<len;i++)assert(mma_arp_next(note,3,order,range,jump,&index,&rng)==want[i]);
}
int main(void)
{
 keys();const int t[]={67,60,64,79,72,76},u[]={60,64,67,72,76,79},d[]={79,76,72,67,64,60};
 const int c[]={60,64,67,64,60,64,67,64},j[]={60,64,79,72,76,67},j2[]={60,76,79,72,64,67};
 vector(MMA_PLAY_TRUE,2,0,t,6);vector(MMA_PLAY_UP,2,0,u,6);vector(MMA_PLAY_DOWN,2,0,d,6);
 vector(MMA_PLAY_CYCLE,1,0,c,8);vector(MMA_PLAY_UP,2,1,j,6);vector(MMA_PLAY_UP,2,127,j2,6);
 uint8_t one[8]={60};uint32_t idx=0,rng=0;
 for(unsigned i=0;i<100;i++)assert(mma_arp_next(one,1,MMA_PLAY_CYCLE,1,127,&idx,&rng)==60);
 uint8_t ns[8]={60,64,67};uint32_t a=123,b=123,ia=0,ib=0;unsigned seen=0;
 for(unsigned i=0;i<10000;i++){
  int x=mma_arp_next(ns,3,MMA_PLAY_RND,2,0,&ia,&a);assert(x==mma_arp_next(ns,3,MMA_PLAY_RND,2,0,&ib,&b));
  assert(x==60||x==64||x==67||x==72||x==76||x==79);seen|=1u<<(x-60);
 }
 assert(seen==((1u<<0)|(1u<<4)|(1u<<7)|(1u<<12)|(1u<<16)|(1u<<19)));
 puts("ARP modes: held/latch/add, capacity/order/cycle/range and explicit OJMP assumptions pass");
}
