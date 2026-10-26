/* SPDX-License-Identifier: GPL-3.0-only
 * Real-time track/pattern transpose (reference manual, TRANSPOSE pp.63–64).
 * Pattern data stays unchanged. SCALE and KEY belong to the track. */
#ifndef MNM_SEQ_TRANSPOSE_H
#define MNM_SEQ_TRANSPOSE_H
/* Keep scale quantization out of legacy DSP multiply scheduling windows. */
static __attribute__((noinline)) int seq_note(const track_t *t)
{
    unsigned scale=t->seq.scale_key>>4,key=(t->seq.scale_key&15u)%12u;
    int note=60+t->nt+t->seq.transpose;
    if(scale!=1)note+=song.seq.transpose; /* FIX ignores pattern transpose. */
    if(note<0)note=0;
    if(note>127)note=127;
    if(scale==2||scale==3){
        unsigned mask=scale==2?0xAB5u:0x5ADu; /* major / natural minor */
        /* Manual specifies scale membership, not rounding direction. Choose
         * the nearest legal MIDI note, preferring downward on equal distance. */
        for(int distance=0;distance<128;distance++){
            int below=note-distance,above=note+distance;
            if(below>=0&&(mask&(1u<<((below+12-(int)key)%12))))return below;
            if(above<=127&&(mask&(1u<<((above+12-(int)key)%12))))return above;
        }
    }
    return note;
}
#endif
