/* SPDX-License-Identifier: GPL-3.0-only
 * MULTI performance state only; caller owns routing and engine events.
 * Envelope/portamento laws reuse existing firmware curves. Their match to
 * physical hardware has not been measured. No inactive-envelope auto bypass. */
#ifndef MNM_MULTI_RUNTIME_H
#define MNM_MULTI_RUNTIME_H
#define MULTI_ENV_ONE (1u<<24)
enum {MULTI_ENV_ATTACK,MULTI_ENV_DECAY,MULTI_ENV_SUSTAIN,MULTI_ENV_RELEASE,MULTI_ENV_DONE};
typedef struct {
    uint8_t held[128],velocity[128],order[128];
    unsigned count;
    uint8_t pending,note,pending_velocity;
    uint32_t phase,env;
    uint8_t envstage,envactive,gate;
    int32_t pitch16,target16,pitch_fraction;
} multi_runtime_t;
static void multi_hold(multi_runtime_t *s,unsigned note,unsigned velocity,int on,unsigned source)
{
    if(note>127||source>1)return;
    unsigned bit=1u<<source,i=0;
    while(i<s->count&&s->order[i]!=note)i++;
    if(on){
        s->held[note]|=(uint8_t)bit;s->velocity[note]=(uint8_t)(velocity<1?1:velocity>127?127:velocity);
        /* Retrigger moves the existing note to newest, never duplicates it. */
        if(i<s->count){for(unsigned j=i;j+1<s->count;j++)s->order[j]=s->order[j+1];s->count--;}
        s->order[s->count++]=(uint8_t)note;
    }else{
        s->held[note]&=(uint8_t)~bit;
        if(!s->held[note]&&i<s->count){
            for(unsigned j=i;j+1<s->count;j++)s->order[j]=s->order[j+1];
            s->count--;s->velocity[note]=0;
        }
    }
}
static int multi_last(const multi_runtime_t *s,unsigned low,unsigned high)
{
    for(unsigned i=s->count;i>0;i--){unsigned note=s->order[i-1];if(note>=low&&note<=high)return (int)note;}
    return -1;
}
static unsigned multi_tracks(const multi_options_t *c,unsigned note)
{
    if(note>127||c->mode==MULTI_OFF)return 0;
    if(c->mode!=MULTI_SPLIT)return (1u<<NTRK)-1;
    unsigned first=c->first_upper<NTRK?c->first_upper:NTRK;
    unsigned lower=(1u<<first)-1,all=(1u<<NTRK)-1;
    return note<c->split_key?lower:all&~lower;
}
static void multi_env_on(multi_runtime_t *s)
{s->envactive=1;s->gate=1;s->env=0;s->envstage=MULTI_ENV_ATTACK;}
static void multi_env_off(multi_runtime_t *s)
{s->gate=0;if(s->envactive)s->envstage=MULTI_ENV_RELEASE;}
static int32_t multi_env_sample(multi_runtime_t *s,const multi_options_t *c)
{
    if(!s->envactive)return 32767;
    unsigned sustain=(unsigned)c->sustain*MULTI_ENV_ONE/127u;
    switch(s->envstage){
    case MULTI_ENV_ATTACK:{
        unsigned inc=ENV_LIN[c->attack&127u]>>5;if(!inc)inc=1;
        s->env+=inc;if(s->env>=MULTI_ENV_ONE){s->env=MULTI_ENV_ONE;s->envstage=MULTI_ENV_DECAY;}break;
    }
    case MULTI_ENV_DECAY:
        if(s->env>sustain)s->env=(uint32_t)mulq16((int32_t)s->env,DECAY_K[c->decay&127u]);
        if(s->env<=sustain){s->env=sustain;s->envstage=MULTI_ENV_SUSTAIN;}break;
    case MULTI_ENV_SUSTAIN:s->env=sustain;break;
    case MULTI_ENV_RELEASE:
        if(c->release<127){
            s->env=(uint32_t)mulq16((int32_t)s->env,DECAY_K[c->release]);
            if(s->env<1024){s->env=0;s->envstage=MULTI_ENV_DONE;}
        }break;
    default:s->env=0;break;
    }
    unsigned gain=s->env>>9;return (int32_t)(gain>32767?32767:gain);
}
static void multi_pitch_tick(multi_runtime_t *s,const multi_options_t *c,unsigned n)
{
    if(!n)return;
    if(!c->portamento){s->pitch16=s->target16;s->pitch_fraction=0;return;}
    unsigned k=ENV_EXP[20+(unsigned)(c->portamento&127u)*90/127];
    /* Q16 residual retains sub-1/16-semitone motion for long portamento.
     * CTL=32 uses the existing block factor; shorter pieces scale it linearly. */
    while(n){
        unsigned chunk=n>32?32:n,factor=k*chunk/32;
        int32_t distance=(s->target16-s->pitch16)*65536-s->pitch_fraction;
        if(!distance)break;
        if(distance>-256&&distance<256){s->pitch16=s->target16;s->pitch_fraction=0;break;}
        int32_t delta=mulq16(distance,factor);
        if(!delta)delta=distance>0?1:-1;
        int32_t fraction=s->pitch_fraction+delta;
        s->pitch16+=fraction>>16;
        s->pitch_fraction=(int32_t)((uint32_t)fraction&65535u);
        n-=chunk;
    }
}
static uint32_t multi_quantum(const multi_options_t *c,uint32_t beat)
{
    if(!c->timing)return 0;
    unsigned timing=c->timing>7?7:c->timing;
    return beat*(1u<<(timing-1))/4u;
}
static int multi_clock_tick(multi_runtime_t *s,uint32_t n,uint32_t quantum)
{
    if(!quantum){s->phase=0;return 0;}
    uint32_t phase=s->phase%quantum,part=n%quantum;
    int crossed=n>=quantum||part>=quantum-phase;
    s->phase=part>=quantum-phase?part-(quantum-phase):phase+part;
    return crossed;
}
#endif
