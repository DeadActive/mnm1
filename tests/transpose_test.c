/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_host.h"
#include "../firmware/src/seq_transpose.h"
#include <assert.h>
static int expected(int note,int track,int pattern,unsigned scale,unsigned key)
{
    static const unsigned intervals[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
    int target=note+track+(scale==1?0:pattern);
    if(target<0)target=0;if(target>127)target=127;
    if(scale<2)return target;
    int best=-1,distance=1000;
    /* Independent exhaustive MIDI search, ascending order resolves ties down. */
    for(int candidate=0;candidate<128;candidate++){
        unsigned relative=(unsigned)(candidate+12-(int)key)%12;
        int allowed=0;for(unsigned j=0;j<7;j++)allowed|=relative==intervals[scale-2][j];
        int delta=candidate-target;if(delta<0)delta=-delta;
        if(allowed&&delta<distance){best=candidate;distance=delta;}
    }
    return best;
}
static void test_domain(void)
{
    ui_host_init();track_t *t=&trk[0];
    const int transpose[]={-48,-1,0,1,48};
    for(unsigned scale=0;scale<4;scale++)for(unsigned key=0;key<12;key++)
    for(int note=0;note<128;note++)for(unsigned a=0;a<5;a++)for(unsigned b=0;b<5;b++){
        t->nt=(int8_t)(note-60);t->seq.transpose=(int8_t)transpose[a];
        t->seq.scale_key=(uint8_t)(scale*16+key);song.seq.transpose=(int8_t)transpose[b];
        /* These pattern fields are stored, but do not define a second quantizer. */
        song.seq.scale=3;song.seq.key=11;
        track_t before=*t;song_t original=song;
        int got=seq_note(t);
        assert(got>=0&&got<=127&&got==expected(note,transpose[a],transpose[b],scale,key));
        assert(!memcmp(t,&before,sizeof before)&&!memcmp(&song,&original,sizeof original));
    }
    seq_options_default(&t->seq);song.seq=(pattern_options_t){0};
    for(int note=0;note<128;note++){t->nt=(int8_t)(note-60);assert(seq_note(t)==note);}
    t->seq.scale_key=2<<4;t->nt=1;assert(seq_note(t)==60); /* C# ties C/D: down. */
    t->seq.scale_key=3<<4;t->nt=4;assert(seq_note(t)==63); /* E ties Eb/F: down. */
}
static void test_trigger(void)
{
    ui_host_init();track_t *t=&trk[0];drum_set_model(t,DM_M2_GSIN);mmen_default(t);
    t->seq.transpose=5;song.seq.transpose=7;
    drum_play(t,96,0);assert(mmen_state[0].note==72&&t->nt==0);
    assert(mm_note16(t)==72*16);
    t->seq.scale_key=1<<4;drum_play(t,96,0);assert(mmen_state[0].note==65);
    t->seq.scale_key=2<<4;t->seq.transpose=0;song.seq.transpose=1;
    drum_play(t,96,0);assert(mmen_state[0].note==60);
}
int main(void)
{
    test_domain();test_trigger();puts("transpose_test: all passed");return 0;
}
