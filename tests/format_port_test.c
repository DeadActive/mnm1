/* SPDX-License-Identifier: GPL-3.0-only
 * Format source: MONO2K by Inigo Hernandez (GPL-3.0-only).
 * cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
 * -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined
 * -Ibuild/gen -Ifirmware/src tests/format_port_test.c -lm -o /tmp/format_port_test
 */
#include <assert.h>
#include "drum_host.h"
static void expect(unsigned m,unsigned id,int raw,const char *want,const char *unit)
{
 char v[16];const char *u;
 trk[0].p[P_MODEL]=MM_MODEL_FIRST+m;
 const param_desc_t *d=track_desc(&trk[0],MM_CONTROL_ID[id]);
 param_format(d,raw-mm_control_bias(id),v,&u);
 if(strcmp(v,want)||strcmp(u,unit)){fprintf(stderr,"m%u p%u raw%d: '%s' '%s' expected '%s' '%s'\n",m,id,raw,v,u,want,unit);assert(0);}
}
int main(void)
{
 host_init();song.sel=0;song.g[G_BPM]=120;
 expect(MM2_GSIN,7,64,"0.000","cent");expect(MM2_GSIN,7,65,"3.125","cent");
 expect(MM2_GSIN,7,63,"-3.13","cent");expect(MM2_GSIN,7,0,"-200","cent");expect(MM2_GSIN,7,127,"196.9","cent");
 expect(MM2_ENS,0,62,"OFF","");expect(MM2_FMS,0,0,"1/64","ratio");expect(MM2_FMS,0,127,"4","ratio");
 expect(MM2_FMD,0,64,"1.0","ratio");expect(MM2_FMD,0,127,"2.0","ratio");expect(MM2_FMD,4,127,"4.0","ratio");
 expect(MM2_SID,3,127,"NOIS","");expect(MM2_SID,4,127,"R+S","");expect(MM2_SID,5,127,"PREV","");
 expect(MM2_SID,6,0,"C3","");expect(MM2_SID,6,32,"C4","");
 expect(MM2_VOX,4,64,"--","n/a");expect(MM2_VOX,2,127,"--","n/a");
 expect(MM2_GSIN,9,127,"GATE","");expect(MM2_GSIN,9,32,"500","ms");
 expect(MM2_GSIN,10,127,"INF","");expect(MM2_GSIN,21,127,"INF","");
 expect(MM2_GSIN,14,0,"L64","");expect(MM2_GSIN,14,64,"C","");expect(MM2_GSIN,14,127,"R63","");
 expect(MM2_GSIN,16,16,"0.0","oct");expect(MM2_GSIN,32,127,"3","bit");
 expect(MM2_GSIN,38,21,"OFF","");expect(MM2_GSIN,38,22,"UP","");
 expect(MM2_GSIN,39,0,"1/32","note");expect(MM2_GSIN,39,127,"1/4","note");
 expect(MM2_GSIN,40,127,"4","oct");expect(MM2_GSIN,42,127,"T6","");
 expect(MM2_GSIN,44,127,"RND","");expect(MM2_GSIN,45,127,"HALF","");
 expect(MM2_GSIN,46,127,"X128","");expect(MM2_GSIN,48,64,"180.0","deg");expect(MM2_GSIN,49,64,"0","");
 expect(MM2_GSIN,43,0,"OFF","");expect(MM2_GSIN,43,127,"--","");
 trk[0].p[P_L1TRK]=mm_control_track(1);trk[1].p[P_MODEL]=MM_MODEL_FIRST+MM2_FMS;
 expect(MM2_GSIN,43,1,"1FRQ","");
 for(unsigned m=0;m<19;m++) {
  trk[0].p[P_MODEL]=MM_MODEL_FIRST+m;
  for(unsigned id=0;id<71;id++)for(int raw=0;raw<128;raw++) {
   char b[8];memset(b,0x5a,sizeof b);const char *u;
   int stored=raw-mm_control_bias(id);
   param_format(track_desc(&trk[0],MM_CONTROL_ID[id]),stored,b+1,&u);
   if(b[0]!=0x5a||b[7]!=0x5a||strlen(b+1)>5||strlen(u)>5){fprintf(stderr,"width m%u id%u raw%d '%s' '%s'\n",m,id,raw,b+1,u);assert(0);}
   assert(stored==raw-mm_control_bias(id));
  }
 }
 /* Shared raw descriptors do not capture legacy parameter descriptors. */
 char v[16];const char *u;param_format(&TP[P_AHOLD],127,v,&u);assert(!strcmp(v,"GATE"));
 param_format(&DMODELS[DM_MMSAW].edit[0],7,v,&u);assert(!strcmp(v,"+7"));
 puts("format: 172672 calibrated values bounded, semantic boundaries and legacy dispatch pass");return 0;
}
