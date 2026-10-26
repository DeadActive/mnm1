/* SPDX-License-Identifier: GPL-3.0-only
 * Velocity/keytrack response inferred from the manual, not hardware measured:
 * signed depth spans the destination range at source127 (+63 / -64).
 * Source LFO Q8 deltas remain in their adapter; this layer changes raw bases only. */
#ifndef MNM_CONTROLLER_MOD_H
#define MNM_CONTROLLER_MOD_H
static struct {
    int16_t base[4];
    uint8_t id[4],count,velocity,key,applied;
} ctlmod_state[NTRK];
static uint8_t ctlmod_active;
static unsigned ctlmod_index(const track_t *t)
{
    for(unsigned k=0;k<NTRK;k++)if(t==&trk[k])return k;
    return NTRK;
}
static void ctlmod_restore(unsigned k)
{
    if(!ctlmod_state[k].applied)return;
    for(unsigned j=0;j<ctlmod_state[k].count;j++)
        trk[k].p[ctlmod_state[k].id[j]]=ctlmod_state[k].base[j];
    ctlmod_state[k].count=0;ctlmod_state[k].applied=0;
}
static void ctlmod_apply(unsigned k)
{
    track_t *t=&trk[k];int32_t delta[4]={0};
    ctlmod_state[k].count=0;ctlmod_state[k].applied=0;
    if(!(t->perf.dest[0]|t->perf.dest[1]|t->perf.dest[2]|t->perf.dest[3]))return;
    ctlmod_state[k].applied=1;
    for(unsigned route=0;route<4;route++){
        unsigned dest=t->perf.dest[route];int amount=t->perf.amount[route];
        if(!dest||!amount||!perf_destination_valid(dest)||amount< -64||amount>63)continue;
        unsigned id=dest-1;const param_desc_t *d=track_desc(t,id);
        if(!d||d->max<=d->min)continue;
        unsigned j=0;while(j<ctlmod_state[k].count&&ctlmod_state[k].id[j]!=id)j++;
        if(j==ctlmod_state[k].count){
            ctlmod_state[k].id[j]=(uint8_t)id;ctlmod_state[k].base[j]=t->p[id];ctlmod_state[k].count++;
        }
        unsigned source=route<2?ctlmod_state[k].velocity:ctlmod_state[k].key;
        delta[j]+=amount*(int)source*(d->max-d->min)/(127*(amount<0?64:63));
    }
    for(unsigned j=0;j<ctlmod_state[k].count;j++){
        unsigned id=ctlmod_state[k].id[j];const param_desc_t *d=track_desc(t,id);
        t->p[id]=(int16_t)clamp(ctlmod_state[k].base[j]+delta[j],d->min,d->max);
    }
}
static int16_t *ctlmod_base_slot(track_t *t,unsigned id)
{
    unsigned k=ctlmod_index(t);if(k==NTRK||!ctlmod_state[k].applied)return 0;
    for(unsigned j=0;j<ctlmod_state[k].count;j++)
        if(ctlmod_state[k].id[j]==id)return &ctlmod_state[k].base[j];
    return 0;
}
static void ctlmod_refresh(track_t *t)
{
    unsigned k=ctlmod_index(t);if(k==NTRK)return;
    int own=ctlmod_state[k].applied;
    int lfo=song.lfo_in&&(t->lnum||own);
    if(lfo)lfo_restore_track(t);
    if(own){ctlmod_restore(k);ctlmod_apply(k);}
    if(lfo)lfo_track(t,0);
}
static void ctlmod_begin(void)
{
    for(unsigned k=0;k<NTRK;k++){ctlmod_restore(k);ctlmod_apply(k);}
    ctlmod_active=1;
}
/* Caller removes the legacy LFO layer first. */
static void ctlmod_end(void)
{
    for(unsigned k=0;k<NTRK;k++)ctlmod_restore(k);
    ctlmod_active=0;
}
/* Explicit input also covers manual ARP modes that do not trigger a voice.
 * Generated ARP notes keep the last pressed input's controller sources. */
static void ctlmod_input(track_t *t,unsigned velocity,int nt)
{
    unsigned k=ctlmod_index(t);if(k==NTRK)return;
    uint8_t v=(uint8_t)(velocity<1?1:velocity>127?127:velocity);
    uint8_t key=(uint8_t)clamp(60+nt,0,127);
    if(v==ctlmod_state[k].velocity&&key==ctlmod_state[k].key)return;
    ctlmod_state[k].velocity=v;ctlmod_state[k].key=key;
    if((ctlmod_active||ctlmod_state[k].applied)&&ctlmod_state[k].count)ctlmod_refresh(t);
}
static int ctlmod_note_begin(track_t *t,unsigned velocity,int nt,unsigned flags)
{
    unsigned k=ctlmod_index(t);if(k==NTRK||flags&ST_NOTEOFF)return 0;
    if(!arp_self)
        ctlmod_input(t,velocity,flags&ST_NOPITCH?(int)ctlmod_state[k].key-60:nt);
    if(ctlmod_active||ctlmod_state[k].applied)return 0;
    ctlmod_apply(k);return 1;
}
static void ctlmod_note_end(track_t *t,int temporary)
{
    unsigned k=ctlmod_index(t);if(temporary&&k<NTRK)ctlmod_restore(k);
}
static void ctlmod_reset(void)
{
    ctlmod_end();memset(ctlmod_state,0,sizeof ctlmod_state);
    /* Until the first note, use full velocity and middle C. */
    for(unsigned k=0;k<NTRK;k++){ctlmod_state[k].velocity=127;ctlmod_state[k].key=60;}
}
_Static_assert(sizeof ctlmod_state+sizeof ctlmod_active<256,"controller modulation state budget");
#endif
