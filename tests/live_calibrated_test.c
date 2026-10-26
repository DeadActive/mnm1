/* SPDX-License-Identifier: GPL-3.0-only
 * Physical/MIDI note ownership, full project persistence and calibrated pages. */
#include "ui_host.h"
#include <assert.h>
static void fresh(void)
{
    ui_host_init();
    for(unsigned k=0;k<NTRK;k++) {
        drum_set_model(&trk[k],DM_MMGND);
        memset(trk[k].step,0,sizeof trk[k].step);
    }
    drum_set_model(&trk[0],DM_M2_GSIN);mmen_default(&trk[0]);
    trk[0].p[P_AHOLD]=127;trk[0].p[P_AREL]=0;
    song.sel=0;song.seq_mode=3;song.octave=0;
}
static void press_keys(uint32_t mask)
{
    fm1_in.notes=mask;render_mix(0,0,CTL);
}
static void midi(unsigned status,unsigned note,unsigned velocity)
{
    midi_enqueue((status>>4)|(status<<8)|(note<<16)|(velocity<<24),0);
    render_mix(0,0,CTL);
}
static void test_keys(void)
{
    fresh();
    press_keys(1u<<7);press_keys((1u<<7)|(1u<<11));press_keys((1u<<7)|(1u<<11)|(1u<<14));
    assert(mmarp_nheld[0]==3);
    assert(mmarp_held[0][0]==60&&mmarp_held[0][1]==64&&mmarp_held[0][2]==67);
    press_keys((1u<<7)|(1u<<14));
    assert(mmarp_nheld[0]==2&&mmarp_held[0][0]==60&&mmarp_held[0][1]==67);
    assert(mmen_state[0].gate);
    press_keys(1u<<7);assert(mmarp_nheld[0]==1&&mmen_state[0].gate);
    press_keys(0);assert(!mmarp_nheld[0]&&!mmen_state[0].gate);
    render_mix(0,0,44100);assert(!trk[0].v[0].active);
    fresh();press_keys(1u<<7);
    song.octave=2;press_keys(0);
    assert(!mmarp_nheld[0]&&!calibrated_key_owner[7]);
    fresh();press_keys(1u<<7);
    drum_set_model(&trk[1],DM_M2_GSIN);mmen_default(&trk[1]);song.sel=1;
    press_keys((1u<<7)|(1u<<9));
    assert(mmarp_nheld[0]==1&&mmarp_nheld[1]==1);
    assert(calibrated_key_owner[7]==1&&calibrated_key_owner[9]==2);
    press_keys(1u<<9);assert(!mmarp_nheld[0]&&mmarp_nheld[1]==1&&!mmen_state[0].gate);
    press_keys(0);assert(!mmarp_nheld[1]&&!mmen_state[1].gate);
}
static void test_midi(void)
{
    fresh();
    midi(0x90,72,100);
    assert(trk[0].v[0].active&&mmarp_nheld[0]==1&&mmarp_held[0][0]==72);
    assert(mmen_state[0].note==72&&mmen_state[0].gate);
    midi(0x90,76,100);assert(mmarp_nheld[0]==2);
    midi(0x80,72,0);assert(mmarp_nheld[0]==1&&mmen_state[0].gate);
    midi(0x90,76,0);assert(!mmarp_nheld[0]&&!mmen_state[0].gate);
    render_mix(0,0,44100);assert(!trk[0].v[0].active);
    fresh();drum_set_model(&trk[0],DM_K808);
    song.g[G_DRCH]=10;
    uint32_t note=trk[0].p[P_NOTE],age=hit_age(&trk[0]);
    midi(0x90,note,100);assert(hit_age(&trk[0])==age);
    midi(0x99,note,100);assert(hit_age(&trk[0])!=age&&trk[0].v[0].active);
    age=hit_age(&trk[0]);midi(0x89,note,0);
    assert(hit_age(&trk[0])==age&&trk[0].v[0].active);
}
static void test_project(void)
{
    uint8_t raw[71],loaded[71];
    fresh();
    for(unsigned id=0;id<MM_SOUND_COUNT;id++) {
        assert(!preset_load(&trk[0],PRE_FULL_FIRST+id));
        mm_controls_get(&trk[0],raw);
        trk[0].step[17].on=1;trk[0].step[17].nt=7;
        project_save(0);
        drum_set_model(&trk[0],DM_K808);
        assert(proj_load(0,0));
        mm_controls_get(&trk[0],loaded);
        assert(!memcmp(raw,loaded,sizeof raw));
        assert(trk[0].pre==PRE_FULL_FIRST+id&&!trk[0].pre_ed);
        assert(trk[0].step[17].on&&trk[0].step[17].nt==7);
    }
}
static void test_pages(void)
{
    unsigned found=0;
    fresh();
    for(unsigned i=0;i<NPAGES;i++) {
        const page_t *p=&PAGES[i];
        if(p->id[0]==P_BITS||p->id[0]==P_MODT) {
            assert(!page_hidden(i));
            drum_set_model(TSEL,DM_MMSAW);assert(page_hidden(i));
            drum_set_model(TSEL,DM_M2_GSIN);found++;
        }
        if(!strcmp(p->title,"LOFI")){assert(p->fam==FAM_FX&&!page_hidden(i));found++;}
        for(unsigned j=0;j<4;j++) {
            if(p->tab==TAB_AMP&&p->id[j]==P_LEVEL)assert(page_id(p,j)==P_MVOL);
            if(p->tab>=TAB_LF1&&p->tab<=TAB_LF3) {
                unsigned l=p->tab-TAB_LF1,b=lfo_base(l);
                if(p->id[j]==b+LF_PAGE)assert(page_id(p,j)==P_L1TRK+l);
                if(p->id[j]==b+LF_INTL)assert(page_id(p,j)==P_L1PHS+l);
            }
        }
    }
    assert(found==3);
}
int main(void)
{
    test_keys();test_midi();test_project();test_pages();
    puts("live_calibrated_test: all passed");return 0;
}
