/* SPDX-License-Identifier: GPL-3.0-only
 * Built-in two-phrase demo. Immutable sources never occupy user-library slots.
 * Include after init_all; chain_prepare dispatches rsv[0]==1 here. */
#ifndef MNM_DEMO_SONG_H
#define MNM_DEMO_SONG_H
static const uint8_t DEMO_MODELS[NTRK]={DM_K808,DM_S808,DM_HATC,DM_MMSAW,DM_MMPLS,DM_FXPHS};
#define DEMO_HIT {.on=1}
#define DEMO_ACC {.on=1,.acc=1}
#define DEMO_NOTE(n) {.on=1,.nt=(n)}
static const step_t DEMO_STEPS[2][NTRK][NSTEP]={
 {
  {[0]=DEMO_ACC,[4]=DEMO_HIT,[8]=DEMO_HIT,[12]=DEMO_HIT},
  {[4]=DEMO_ACC,[12]=DEMO_ACC},
  {[2]=DEMO_HIT,[6]=DEMO_HIT,[10]=DEMO_HIT,[14]=DEMO_HIT,[15]=DEMO_HIT},
  {[0]=DEMO_NOTE(-27),[3]=DEMO_NOTE(-27),[6]=DEMO_NOTE(-20),[8]=DEMO_NOTE(-24),[11]=DEMO_NOTE(-20),[14]=DEMO_NOTE(-29)},
  {[0]=DEMO_NOTE(-3),[3]=DEMO_NOTE(4),[6]=DEMO_NOTE(7),[8]=DEMO_NOTE(0),[11]=DEMO_NOTE(4),[14]=DEMO_NOTE(9)},
  {0}
 },{
  {[0]=DEMO_ACC,[6]=DEMO_HIT,[8]=DEMO_HIT,[14]=DEMO_ACC},
  {[4]=DEMO_ACC,[12]=DEMO_ACC,[15]={.on=1,.cond=10}},
  {[0]=DEMO_HIT,[2]=DEMO_HIT,[4]=DEMO_HIT,[6]=DEMO_HIT,[8]=DEMO_HIT,[10]=DEMO_HIT,[12]=DEMO_HIT,[14]=DEMO_HIT},
  {[0]=DEMO_NOTE(-32),[3]=DEMO_NOTE(-25),[6]=DEMO_NOTE(-20),[8]=DEMO_NOTE(-29),[11]=DEMO_NOTE(-22),[14]=DEMO_NOTE(-27)},
  {[0]=DEMO_NOTE(4),[2]=DEMO_NOTE(0),[5]=DEMO_NOTE(-4),[8]=DEMO_NOTE(2),[10]=DEMO_NOTE(7),[14]=DEMO_NOTE(-3)},
  {0}
 }
};
#undef DEMO_NOTE
#undef DEMO_ACC
#undef DEMO_HIT
static const motion_store_t DEMO_MOTION[2]={
 {.count=10,.on=(1u<<3)|(1u<<4)|(1u<<5),.ev={
  {3,0,P_FWDTH,28},{3,4,P_FWDTH,42},{3,8,P_FWDTH,34},{3,12,P_FWDTH,52},
  {4,0,P_E1,44},{4,4,P_E1,76},{4,8,P_E1,32},{4,12,P_E1,90},
  {5,0,P_E0,50},{5,8,P_E0,84}}},
 {.count=10,.on=(1u<<3)|(1u<<4)|(1u<<5),.ev={
  {3,0,P_FWDTH,38},{3,4,P_FWDTH,56},{3,8,P_FWDTH,30},{3,12,P_FWDTH,46},
  {4,0,P_E1,82},{4,4,P_E1,48},{4,8,P_E1,96},{4,12,P_E1,36},
  {5,0,P_E0,92},{5,8,P_E0,60}}}
};
#define DEMO_SEQ(scale) {.swing={170,170,170,170,170,170,170,170},.arp_mask={255,255},.arp_len=8,.arp_speed=6,.scale_key=(scale)}
static const seq_options_t DEMO_SEQ[NTRK]={DEMO_SEQ(16),DEMO_SEQ(16),DEMO_SEQ(16),DEMO_SEQ(0),DEMO_SEQ(0),DEMO_SEQ(0)};
#undef DEMO_SEQ
static const pattern_options_t DEMO_OPTIONS={0};
static uint32_t demo_song_prepare(void)
{
    if(transport_busy()||chain_busy())return 2;
    if(chain.cfg.rsv[0]!=1||!chain.cfg.count||!chain_valid(&chain.cfg))return 1;
    for(unsigned row=0;row<chain.cfg.count;row++)if(chain.cfg.row[row].slot>=2)return 3+chain.cfg.row[row].slot;
    for(unsigned phrase=0;phrase<2;phrase++){
        chain_src_t *d=&chain.src[phrase];d->m=&DEMO_MOTION[phrase];d->seq=DEMO_SEQ;d->options=&DEMO_OPTIONS;
        for(unsigned k=0;k<NTRK;k++){
            d->step[k]=DEMO_STEPS[phrase][k];d->model[k]=DEMO_MODELS[k];
            d->timing[k][0]=16;d->timing[k][1]=2;d->timing[k][2]=0;d->timing[k][3]=0;
        }
    }
    fm1_irq_off();chain.armed=1;fm1_irq_on();transport_req=1;return 0;
}
static void demo_song_load(void)
{
    if(transport_busy()||chain_busy()){ui_message("STOP FIRST");return;}
    init_all();fm1_irq_off();
    song.g[G_BPM]=112;song.g[G_CSRC]=0;
    const uint8_t level[NTRK]={103,86,55,86,90,90};
    for(unsigned k=0;k<NTRK;k++){
        track_t *t=&trk[k];drum_set_model(t,DEMO_MODELS[k]);
        t->seq=DEMO_SEQ[k];t->p[P_LEVEL]=level[k];t->p[P_PAN]=0;
        t->p[P_MUTE]=t->p[P_CHOKE]=t->p[P_ARPM]=t->p[P_DIST]=t->p[P_SLCR]=0;
        t->p[P_CHOR]=t->p[P_REV]=t->p[P_DLY]=0;t->p[P_EQG]=64;
        t->p[P_OUT]=k==4?BUS_OFF:BUS_AB;t->p[P_IN]=IN_NEIB;t->p[P_FXG]=0;
        t->p[P_SLEN]=16;t->p[P_SDIV]=2;t->p[P_SSWING]=0;t->p[P_SRC]=0;
        t->p[P_FCUT]=0;t->p[P_FWDTH]=127;t->p[P_FRESO]=t->p[P_FLPQ]=t->p[P_FENV]=t->p[P_FWOFS]=0;
        for(unsigned l=0;l<3;l++)t->p[lfo_base(l)+LF_DPTH]=0;
        memcpy(t->step,DEMO_STEPS[0][k],sizeof t->step);
        t->pre=PRE_NONE;t->pre_ed=0;
    }
    track_t *bass=&trk[3],*melody=&trk[4],*phaser=&trk[5];
    bass->p[P_E0]=0;bass->p[P_E1]=5;bass->p[P_E2]=1;bass->p[P_E3]=30;bass->p[P_E4]=0;
    bass->p[P_AATK]=0;bass->p[P_AHOLD]=127;bass->p[P_ADEC]=127;bass->p[P_AREL]=20;
    bass->p[P_FWDTH]=28;bass->p[P_FLPQ]=25;bass->p[P_NLEN]=80;
    melody->p[P_E0]=0;melody->p[P_E1]=44;melody->p[P_E2]=14;melody->p[P_E3]=25;
    melody->p[P_AATK]=0;melody->p[P_AHOLD]=0;melody->p[P_ADEC]=48;melody->p[P_AREL]=24;
    melody->p[P_FWDTH]=66;melody->p[P_NLEN]=90;
    phaser->p[P_E0]=50;phaser->p[P_E1]=70;phaser->p[P_E2]=25;phaser->p[P_E3]=65;phaser->p[P_E4]=18;
    phaser->p[P_AATK]=0;phaser->p[P_AHOLD]=127;phaser->p[P_ADEC]=127;phaser->p[P_AREL]=20;
    mo.s=DEMO_MOTION[0];motion_forget();
    chain.cfg.count=8;chain.cfg.loop=1;chain.cfg.rsv[0]=1;
    for(unsigned row=0;row<8;row++){
        chain.cfg.row[row]=(chain_row_t){(uint8_t)((row/2)&1u),2};
        chain.cfg.options[row].length=16;
    }
    /* Intro/outro space, a transposed middle and an offset variation.
     * Drum tracks use FIX, so melodic transposition leaves their tuning alone. */
    chain.cfg.options[0].mutes=(1u<<4)|(1u<<5);
    chain.cfg.options[3].transpose=5;
    chain.cfg.options[4].offset=4;
    chain.cfg.options[5].transpose=-5;
    chain.cfg.options[6].mutes=1u<<2;
    chain.cfg.options[7].mutes=1u<<4;
    name_set(chain.name,"AFTER HOURS");chain.from=0;song.sel=0;
    fm1_irq_on();ui.bank=0;ui.song_row=0;ui.force=1;ui_message("DEMO LOADED");
}
#endif
