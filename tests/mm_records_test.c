/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include "../firmware/src/mm_records.h"
#include <assert.h>
static mmr_sound_t sound,again;
static mmr_kit_t kit;
static mmr_pattern_t pattern;
static mmr_sounds_t sounds;
static mmr_kits_t kits;
static track_t before[NTRK];
static uint32_t seed=7;
static uint32_t random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static void defaults(void)
{
    host_init_live();
    for(unsigned k=0;k<NTRK;k++){
        drum_set_model(&trk[k],DM_MMGND);
        for(unsigned id=0;id<P_COUNT;id++)trk[k].p[id]=TP[id].def;
        trk[k].p[P_MODEL]=DM_MMGND;trk[k].model=DM_MMGND;
    }
}
static void assert_sound_equal(const track_t *a,const track_t *b)
{
    for(unsigned id=0;id<P_COUNT;id++)if(mmr_sound_param(id))assert(a->p[id]==b->p[id]);
    assert(!memcmp(&a->perf,&b->perf,sizeof a->perf));
}
static void roundtrip(void)
{
    before[0]=trk[0];
    assert(mmr_sound_capture(&sound,&trk[0],"SOUND"));
    assert(mmr_sound_valid(&sound));
    assert(mmr_sound_capture(&again,&trk[0],"SOUND"));assert(!memcmp(&sound,&again,sizeof sound));
    for(unsigned id=0;id<P_COUNT;id++)if(!mmr_sound_param(id))assert(!sound.p[id]);
    before[1]=trk[1];
    assert(mmr_sound_apply(&trk[1],&sound));
    assert_sound_equal(&trk[0],&trk[1]);
    for(unsigned id=0;id<P_COUNT;id++)if(!mmr_sound_param(id))assert(trk[1].p[id]==before[1].p[id]);
    assert(!memcmp(trk[1].step,before[1].step,sizeof trk[1].step));
}
static void test_sounds(void)
{
    for(unsigned id=0;id<PRE_NFACT;id++){
        defaults();assert(!preset_load(&trk[0],id));roundtrip();
        assert(mmr_preset_ref(&sound)==PRE_UID[id]);
    }
    for(unsigned n=0;n<128;n++){
        defaults();unsigned model=random_word()%NMODELS;
        trk[0].p[P_MODEL]=(int16_t)model;
        for(unsigned id=0;id<P_COUNT;id++)if(mmr_sound_param(id)&&id!=P_MODEL){
            const param_desc_t *d=mmr_desc(model,id);
            trk[0].p[id]=(int16_t)(d->min+(int)(random_word()%(unsigned)(d->max-d->min+1)));
        }
        roundtrip();
    }
    defaults();assert(!preset_load(&trk[0],preset_first_of(DM_CPEFM)));
    trk[0].p[P_E0]=DMODELS[DM_CPEFM].edit[0].min;
    trk[0].p[P_PAN]=-64;trk[0].p[P_FENV]=-64;trk[0].p[P_DLY]=-64;
    roundtrip();assert(trk[1].p[P_PAN]==-64&&trk[1].p[P_DLY]==-64);
    before[1]=trk[1];again=sound;sound.p[P_MODEL]=255;
    assert(!mmr_sound_apply(&trk[1],&sound));assert(!memcmp(&before[1],&trk[1],sizeof trk[1]));
    sound=again;sound.p[P_MUTE]=1;assert(!mmr_sound_valid(&sound));
    sound=again;sound.p[P_E0]=127;assert(!mmr_sound_valid(&sound));
    sound=again;sound.used=2;assert(!mmr_sound_valid(&sound));
    sound=again;sound.name[8]='X';assert(!mmr_sound_valid(&sound));
    sound=again;
}
static void test_split(void)
{
    defaults();
    for(unsigned k=0;k<NTRK;k++){
        assert(!preset_load(&trk[k],PRE_FULL_FIRST+k));
        trk[k].p[P_NOTE]=(int16_t)(40+k);trk[k].p[P_OUT]=k%3;
        trk[k].p[P_SLEN]=(int16_t)(16+k);trk[k].p[P_MUTE]=k&1;
        trk[k].step[k].on=1;trk[k].step[k].nt=(int8_t)k;
    }
    memset(&mo.s,0,sizeof mo.s);mo.s.count=1;mo.s.ev[0]=(motion_event_t){0,1,P_E0,12};
    memset(&chain.cfg,0,sizeof chain.cfg);chain.cfg.count=1;chain.cfg.row[0]=(chain_row_t){1,2};
    assert(mmr_pattern_capture(&pattern,trk,song.g,&mo.s,&chain.cfg,4,7,"TWELVECHARS!"));
    assert(mmr_kit_capture(&kit,trk,"KIT"));
    memcpy(before,trk,sizeof before);
    for(unsigned k=0;k<NTRK;k++){
        trk[k].p[P_SLEN]=2;trk[k].p[P_NOTE]=90;trk[k].p[P_E0]=0;
        memset(trk[k].step,0,sizeof trk[k].step);
    }
    assert(mmr_kit_apply(trk,&kit));
    for(unsigned k=0;k<NTRK;k++){
        assert_sound_equal(&trk[k],&before[k]);assert(trk[k].p[P_NOTE]==40+(int)k);
        assert(trk[k].p[P_SLEN]==2&&!trk[k].step[k].on);
        trk[k].p[P_NOTE]=99;trk[k].p[P_E0]=3;
    }
    assert(mmr_pattern_apply(trk,song.g,&mo.s,&chain.cfg,&pattern));
    for(unsigned k=0;k<NTRK;k++){
        assert(trk[k].p[P_NOTE]==99&&trk[k].p[P_E0]==3);
        assert(trk[k].p[P_SLEN]==16+(int)k&&trk[k].step[k].on);
    }
    assert(pattern.selected==4&&pattern.kit==7&&mo.s.count==1&&chain.cfg.row[0].repeat==2);
    memcpy(before,trk,sizeof before);pattern.kit=8;
    assert(!mmr_pattern_apply(trk,song.g,&mo.s,&chain.cfg,&pattern));assert(!memcmp(before,trk,sizeof before));
    pattern.kit=7;pattern.timing[0][1]=0;assert(!mmr_pattern_valid(&pattern));pattern.timing[0][1]=16;
    pattern.motion.count=MOTION_MAX+1;assert(!mmr_pattern_valid(&pattern));pattern.motion.count=1;
    pattern.g[G_BPM]=0;assert(!mmr_pattern_valid(&pattern));pattern.g[G_BPM]=120;
    pattern.version=MMR_PATTERN_VERSION+1;assert(!mmr_pattern_valid(&pattern));pattern.version=MMR_PATTERN_VERSION;
    kit.config[5][4]=999;assert(!mmr_kit_apply(trk,&kit));assert(!memcmp(before,trk,sizeof before));kit.config[5][4]=2;
}
static void test_blocks(void)
{
    memset(&sounds,0,sizeof sounds);sounds.version=MMR_VERSION;
    for(unsigned i=0;i<MMR_SOUNDS_PER_OBJECT;i++)sounds.sound[i]=sound;
    assert(mmr_sounds_valid(&sounds));sounds.sound[31].used=2;assert(!mmr_sounds_valid(&sounds));
    memset(&kits,0,sizeof kits);kits.version=MMR_VERSION;
    kits.kit[0]=kits.kit[1]=kit;assert(mmr_kits_valid(&kits));
    kits.kit[1].sound[5].p[P_MODEL]=255;assert(!mmr_kits_valid(&kits));
    printf("record bytes: sound %zu, pattern %zu, two kits %zu, 32 sounds %zu\n",sizeof sound,sizeof pattern,sizeof kits,sizeof sounds);
}
static void test_upgrade_and_performance(void)
{
    static struct {uint16_t version,reserved;mmr_sound_v1_t sound[32];} old_sounds;
    static struct {uint16_t version,reserved;mmr_kit_v1_t kit[2];} old_kits;
    static union {mmr_sounds_t sounds;mmr_kits_t kits;} work;
    defaults();assert(mmr_sound_capture(&sound,&trk[0],"BASE"));
    old_sounds.version=1;
    for(unsigned i=0;i<32;i++){
        memcpy(&old_sounds.sound[i],&sound,110);
        snprintf(old_sounds.sound[i].name,9,"S%02u",i);
        old_sounds.sound[i].preset_ref=(uint16_t)(0x8000u+i);
        old_sounds.sound[i].edited=i&1;old_sounds.sound[i].p[P_LEVEL]=(uint8_t)(64+i);
    }
    memcpy(&work,&old_sounds,sizeof old_sounds);assert(mmr_group_upgrade(&work,sizeof old_sounds,0));assert(mmr_sounds_valid(&work.sounds));
    const perf_options_t disabled={0};
    for(unsigned i=0;i<32;i++){
        assert(!memcmp(&work.sounds.sound[i],&old_sounds.sound[i],110));
        assert(mmr_preset_ref(&work.sounds.sound[i])==0x8000u+i);
        assert(!memcmp(&work.sounds.sound[i].perf,&disabled,sizeof disabled));
    }
    assert(mmr_kit_capture(&kit,trk,"KIT"));old_kits.version=1;
    for(unsigned i=0;i<2;i++){
        memcpy(&old_kits.kit[i],&kit,98);snprintf(old_kits.kit[i].name,13,"KIT%u",i);
        for(unsigned k=0;k<6;k++)old_kits.kit[i].sound[k]=old_sounds.sound[i*6+k];
    }
    memcpy(&work,&old_kits,sizeof old_kits);assert(mmr_group_upgrade(&work,sizeof old_kits,1));assert(mmr_kits_valid(&work.kits));
    multi_options_t dm;multi_options_default(&dm);
    for(unsigned i=0;i<2;i++){
        assert(!memcmp(&work.kits.kit[i],&old_kits.kit[i],98));assert(!memcmp(&work.kits.kit[i].multi,&dm,sizeof dm));
        for(unsigned k=0;k<6;k++){
            assert(!memcmp(&work.kits.kit[i].sound[k],&old_kits.kit[i].sound[k],110));
            assert(!memcmp(&work.kits.kit[i].sound[k].perf,&disabled,sizeof disabled));
        }
    }
    for(unsigned kind=0;kind<2;kind++){
        unsigned len=kind?sizeof old_kits:sizeof old_sounds;
        const void *old=kind?(const void*)&old_kits:(const void*)&old_sounds;
        const unsigned badlen[]={0,1,2,len-1,len+1};
        for(unsigned i=0;i<5;i++){memcpy(&work,old,len);assert(!mmr_group_upgrade(&work,badlen[i],kind));}
        memcpy(&work,old,len);((uint16_t*)&work)[0]=99;assert(!mmr_group_upgrade(&work,len,kind));
        memcpy(&work,old,len);((uint16_t*)&work)[1]=1;
        assert(!(mmr_group_upgrade(&work,len,kind)&&(kind?mmr_kits_valid(&work.kits):mmr_sounds_valid(&work.sounds))));
    }
    defaults();trk[0].perf=(perf_options_t){.dest={P_E0+1,P_FCUT+1,P_PTCH+1,P_EQG+1},.amount={-64,63,-17,31},.route_flags=6|PERF_HP_OFF};
    roundtrip();assert(!memcmp(&sound.perf,&trk[0].perf,sizeof sound.perf));
    song.multi=(multi_options_t){4,72,2,7,19,31,87,127,42};
    assert(mmr_kit_capture(&kit,trk,"PERFORM"));
    multi_options_default(&song.multi);memset(&trk[0].perf,0,sizeof trk[0].perf);
    assert(mmr_kit_apply(trk,&kit));assert(!memcmp(&song.multi,&kit.multi,sizeof song.multi));assert(!memcmp(&trk[0].perf,&kit.sound[0].perf,sizeof trk[0].perf));
    kit.multi.mode=5;assert(!mmr_kit_valid(&kit));kit.multi.mode=4;
    sound.perf.route_flags=7;assert(!mmr_sound_valid(&sound));
}
int main(void){test_sounds();test_split();test_blocks();test_upgrade_and_performance();puts("mm_records_test: all passed");return 0;}
