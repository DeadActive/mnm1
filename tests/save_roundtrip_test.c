/* SPDX-License-Identifier: GPL-3.0-only
 * Real project SAVE against flash, including byte records and interrupted IO. */
#define FELUCCA_FLASH 1
#define FLASH_HOST_FAULTS 1
#include <stdint.h>
static uint8_t hflash[0x100000],saved_flash[sizeof hflash];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash+0xA0000u+(k)*0x14000u)
#include "ui_host.h"
#include <assert.h>
static project_t expected[4],readback;
static void reboot_host(void)
{
 host_flash_fault_reset();ui_host_init();memset(proj_slot,0,sizeof proj_slot);persist_boot();assert(flash_ok);project_boot();
}
static void fixture(unsigned value)
{
 song.g[G_BPM]=(int16_t)(100+value);song.sel=value%NTRK;
 for(unsigned k=0;k<NTRK;k++){
  drum_set_model(&trk[k],DM_M2_GSIN);mmen_default(&trk[k]);
  trk[k].p[P_PAN]=(int16_t)((int)value-32);trk[k].p[P_E0]=(int16_t)(value+k);
  seq_options_default(&trk[k].seq);trk[k].seq.arp_offset[15]=(int8_t)(value-40);
  trk[k].perf=(perf_options_t){.dest={P_E0+1,P_FCUT+1,P_PTCH+1,P_EQG+1},.amount={-64,63,-17,31},.route_flags=(uint8_t)(k+1|PERF_HP_OFF)};
  for(unsigned s=0;s<NSTEP;s++)trk[k].step[s]=(step_t){.on=1,.acc=s&1,.nt=(int8_t)(s-32)};
 }
 song.multi=(multi_options_t){4,72,2,7,19,31,87,127,42,6};
 memset(&chain.cfg,0,sizeof chain.cfg);chain.cfg.count=16;
 for(unsigned r=0;r<16;r++){
  chain.cfg.row[r]=(chain_row_t){r%4,1+r%16};
  chain.cfg.options[r]=(chain_row_options_t){.transpose=(int8_t)(r-8),.offset=r,.length=64-r,.mutes=r,.bpm=(uint16_t)(100+r),.loop_count=r,.jump=(uint8_t)((r+1)%16)};
 }
 mo.s.count=MOTION_MAX;mo.s.on=63;
 for(unsigned i=0;i<MOTION_MAX;i++)mo.s.ev[i]=(motion_event_t){i%NTRK,i/NTRK,P_PAN,(int8_t)((int)(i%128)-64)};
 snprintf(chain.name,sizeof chain.name,"SAVE%u",value);
}
static void assert_disk(unsigned slot,const project_t *wanted)
{
 assert(st_load(OBJ_PROJECT0+slot,&readback,sizeof readback)==sizeof readback);
 assert(!memcmp(&readback,wanted,sizeof readback));
}
static unsigned save_boundaries(unsigned slot)
{
 fixture(20+slot);host_flash_fault_reset();project_save(slot);assert(!strcmp(ui.msg,"SAVED"));
 return host_flash_fault.attempts;
}
static void successful_saves(void)
{
 memset(hflash,255,sizeof hflash);reboot_host();
 for(unsigned slot=0;slot<4;slot++){
  fixture(slot+1);host_flash_fault_reset();project_save(slot);assert(!strcmp(ui.msg,"SAVED"));assert(lastp.set&&lastp.slot==slot);
  expected[slot]=proj_slot[slot];assert_disk(slot,&expected[slot]);
  reboot_host();assert(lastp.set&&lastp.slot==slot&&song.g[G_SLOT]==slot+1);
  assert(song.g[G_BPM]==101+(int)slot&&!memcmp(&chain.cfg,&expected[slot].song,sizeof chain.cfg));
  assert(!memcmp(&song.multi,&expected[slot].multi,sizeof song.multi));
  for(unsigned k=0;k<NTRK;k++)assert(!memcmp(&trk[k].perf,&expected[slot].perf[k],sizeof trk[k].perf));
  assert_disk(slot,&expected[slot]);
 }
 printf("SAVE: four slots reboot/load full %zu-byte records and last-project marker\n",sizeof(project_t));
}
static void interrupted_saves(void)
{
 for(unsigned slot=0;slot<4;slot++){
  /* Select this bank first so settings are unchanged during payload faults. */
  assert(proj_load(slot,0));lastp.set=1;lastp.slot=(uint8_t)slot;settings_save();
  memcpy(saved_flash,hflash,sizeof hflash);unsigned count=save_boundaries(slot);
  for(unsigned boundary=1;boundary<=count;boundary++){
   memcpy(hflash,saved_flash,sizeof hflash);reboot_host();fixture(20+slot);
   host_flash_fault_reset();host_flash_fault.fail_at=boundary;project_save(slot);
   assert(host_flash_fault.attempts==boundary);assert(!strcmp(ui.msg,"SAVE ERROR"));assert_disk(slot,&expected[slot]);
   reboot_host();assert(lastp.slot==slot&&song.g[G_BPM]==101+(int)slot);assert_disk(slot,&expected[slot]);
  }
  memcpy(hflash,saved_flash,sizeof hflash);reboot_host();fixture(20+slot);
  host_flash_fault_reset();host_flash_fault.no_op=1;project_save(slot);
  assert(!strcmp(ui.msg,"SAVE ERROR"));assert(host_flash_fault.attempts==count);
  assert(!memcmp(hflash,saved_flash,sizeof hflash));assert_disk(slot,&expected[slot]);
  reboot_host();assert_disk(slot,&expected[slot]);
  printf("SAVE slot %u: all %u erase/program failures + no-op write protection preserve old copy\n",slot,count);
 }
}
static void last_marker_failure(void)
{
 assert(proj_load(0,0));lastp.set=1;lastp.slot=0;settings_save();memcpy(saved_flash,hflash,sizeof hflash);
 unsigned project_writes=2+(sizeof(project_t)+255)/256;
 /* The new project can commit while the separate settings object fails. */
 for(unsigned extra=1;extra<=3;extra++){
  memcpy(hflash,saved_flash,sizeof hflash);reboot_host();fixture(33);
  host_flash_fault_reset();host_flash_fault.fail_at=project_writes+extra;project_save(1);
  if(strcmp(ui.msg,"SAVED; BOOT ERROR"))fprintf(stderr,"SAVE marker failure boundary %u reported '%s'\n",extra,ui.msg);
  assert(!strcmp(ui.msg,"SAVED; BOOT ERROR"));assert_disk(1,&proj_slot[1]);
  reboot_host();assert(lastp.set&&lastp.slot==0);assert_disk(0,&expected[0]);
  fixture(33);project_save(1);assert(!strcmp(ui.msg,"SAVED"));
  reboot_host();assert(lastp.set&&lastp.slot==1&&song.g[G_BPM]==133);

 }
}
int main(void){successful_saves();interrupted_saves();last_marker_failure();puts("save roundtrip: all passed");}
