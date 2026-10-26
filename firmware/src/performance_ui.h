/* SPDX-License-Identifier: GPL-3.0-only
 * Virtual controls for the stored performance options. Include after core.h. */
#ifndef PERFORMANCE_UI_H
#define PERFORMANCE_UI_H
enum {
 PO_VDEST1,PO_VAMT1,PO_VDEST2,PO_VAMT2,PO_KDEST1,PO_KAMT1,PO_KDEST2,PO_KAMT2,
 PO_ROUTE,PO_HP,PO_LP,PO_MMODE,PO_MCHANNEL,PO_MKEY,PO_MTRACK,PO_MTIMING,
 PO_MATK,PO_MDEC,PO_MSUS,PO_MREL,PO_MPORT,PO_COUNT
};
static const char *const PO_MODES[]={"OFF","ALL","SPLIT","START","TRANS"};
static const char *const PO_TIMINGS[]={"OFF","1/16","2/16","4/16","8/16","16/16","32/16","64/16"};
static const param_desc_t PO_DESC[PO_COUNT]={
 [PO_VDEST1]=PD("DEST1",F_ASSIGN,0,P_COUNT,0),[PO_VAMT1]=PD("ADD1",F_INT,-64,63,0),
 [PO_VDEST2]=PD("DEST2",F_ASSIGN,0,P_COUNT,0),[PO_VAMT2]=PD("ADD2",F_INT,-64,63,0),
 [PO_KDEST1]=PD("DEST1",F_ASSIGN,0,P_COUNT,0),[PO_KAMT1]=PD("ADD1",F_INT,-64,63,0),
 [PO_KDEST2]=PD("DEST2",F_ASSIGN,0,P_COUNT,0),[PO_KAMT2]=PD("ADD2",F_INT,-64,63,0),
 [PO_ROUTE]=PD("TRIG",F_INT,0,6,0),[PO_HP]=PD("HP",F_ONOFF,0,1,1),[PO_LP]=PD("LP",F_ONOFF,0,1,1),
 [PO_MMODE]=PE("MODE",PO_MODES,0),[PO_MCHANNEL]=PD("CHAN",F_INT,1,16,7),
 [PO_MKEY]=PD("KEY",F_NOTE,0,127,60),[PO_MTRACK]=PD("TRACK",F_INT,1,6,4),
 [PO_MTIMING]=PE("TIMING",PO_TIMINGS,0),[PO_MATK]=PD("ATK",F_INT,0,127,0),
 [PO_MDEC]=PD("DEC",F_INT,0,127,0),[PO_MSUS]=PD("SUS",F_INT,0,127,127),
 [PO_MREL]=PD("REL",F_INT,0,127,0),[PO_MPORT]=PD("PORT",F_INT,0,127,0)
};
static int perfui_get(const track_t *t,const multi_options_t *m,unsigned id)
{
 if(id<=PO_KAMT2)return id&1u?t->perf.amount[id/2]:t->perf.dest[id/2];
 switch(id){
 case PO_ROUTE:return t->perf.route_flags&7;case PO_HP:return !(t->perf.route_flags&PERF_HP_OFF);
 case PO_LP:return !(t->perf.route_flags&PERF_LP_OFF);case PO_MMODE:return m->mode;
 case PO_MCHANNEL:return m->channel+1;case PO_MKEY:return m->split_key;case PO_MTRACK:return m->first_upper+1;
 case PO_MTIMING:return m->timing;case PO_MATK:return m->attack;case PO_MDEC:return m->decay;
 case PO_MSUS:return m->sustain;case PO_MREL:return m->release;case PO_MPORT:return m->portamento;
 default:return 0;
 }
}
static int perfui_set(track_t *t,multi_options_t *m,unsigned id,int value)
{
 if(id>=PO_COUNT||value<PO_DESC[id].min||value>PO_DESC[id].max||
    (id<=PO_KAMT2&&!(id&1u)&&!perf_destination_valid((unsigned)value))||perfui_get(t,m,id)==value)return 0;
 if(id<=PO_KAMT2){if(id&1u)t->perf.amount[id/2]=(int8_t)value;else t->perf.dest[id/2]=(uint8_t)value;return 1;}
 switch(id){
 case PO_ROUTE:t->perf.route_flags=(uint8_t)((t->perf.route_flags&~7u)|(unsigned)value);break;
 case PO_HP:case PO_LP:{unsigned bit=id==PO_HP?PERF_HP_OFF:PERF_LP_OFF;t->perf.route_flags=(uint8_t)((t->perf.route_flags&~bit)|(value?0:bit));break;}
 case PO_MMODE:m->mode=(uint8_t)value;break;case PO_MCHANNEL:m->channel=(uint8_t)(value-1);break;
 case PO_MKEY:m->split_key=(uint8_t)value;break;case PO_MTRACK:m->first_upper=(uint8_t)(value-1);break;
 case PO_MTIMING:m->timing=(uint8_t)value;break;case PO_MATK:m->attack=(uint8_t)value;break;
 case PO_MDEC:m->decay=(uint8_t)value;break;case PO_MSUS:m->sustain=(uint8_t)value;break;
 case PO_MREL:m->release=(uint8_t)value;break;case PO_MPORT:m->portamento=(uint8_t)value;break;
 default:return 0;
 }
 return 1;
}
#endif
