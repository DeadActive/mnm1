/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (the slots read in place, a row as long as its longest track, LOOP, FDR9) */
/* SONG (upstream Felucca 1.0's song_chain.c, adapted): rows of {project slot, repeat} played in order with the sounds
 * loaded now. A row plays, per track, the slot's steps, its LEN DIV SWG SRC (CHAIN_TIMING) and its motion, read in
 * place from the slot: project.c chain_prepare (main loop, before PLAY) points chain.src at proj_slot[]. The row ends
 * when its longest track (chain.ref) has played its pattern `repeat` times; every track then starts the next row's
 * step 0 together, at that sample (chain.cut: seq_tick keeps the block's rest), on the grid a plain loop would be
 * on: each track's step remainder and swing pair phase from the song's time since PLAY (chain.b24, 1/24 beats:
 * every division's step is a whole number of them), so rows of any length, swing or division never drift. After the last row LOOP OFF stops,
 * LOOP ON plays row 1 again. STOP puts the timing and REC arming back; the current steps are never written. The
 * project's name lives here too (project.c, ui_name.c). The ISR runs the song; the main loop edits chain.cfg only
 * while it is off (chain_busy). */
#define CHAIN_ROWS 16u
#define NAME_LEN 12u
typedef struct { uint8_t slot, repeat; } chain_row_t;                 /* slot 0..7 (A..H), repeat 1..16 */
typedef struct { uint8_t count, loop, rsv[2]; chain_row_t row[CHAIN_ROWS]; } chain_config_v1_t;
typedef struct {
    int8_t transpose;
    uint8_t offset, length, mutes;
    uint16_t bpm;
    uint8_t loop_count, jump; /* jump: 0 next, 1..16 destination; count: extra jumps, 0 infinite */
} chain_row_options_t;
typedef struct {
    uint8_t count, loop, rsv[2]; /* rsv[0]: built-in demo sources */
    chain_row_t row[CHAIN_ROWS];
    chain_row_options_t options[CHAIN_ROWS];
} chain_config_t;
_Static_assert(sizeof(chain_config_v1_t)==36 && sizeof(chain_config_t)==164,"SONG wire layout");
typedef struct {                           /* a slot as its rows play it */
    const step_t *step[NTRK];
    const seq_options_t *seq;
    const pattern_options_t *options;
    const motion_store_t *m;               /* its motion (MOTION_NONE: none, or a broken one) */
    int16_t timing[NTRK][4];               /* CHAIN_TIMING, inside their ranges */
    uint8_t model[NTRK];                   /* its models: another model now = its P_E events skipped (motion.c) */
} chain_src_t;
static const uint8_t CHAIN_TIMING[4] = {P_SLEN, P_SDIV, P_SSWING, P_SRC};
static const motion_store_t MOTION_NONE;
static const char NAME_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+";
static struct {
    chain_config_t cfg;                    /* the song (the project's) */
    char name[NAME_LEN + 1u];              /* the project's name, "" = none */
    uint8_t from;                          /* the slot loaded or saved last + 1, 0 = none (its rename renames this) */
    chain_src_t src[8];                    /* the slots (chain_prepare) */
    seq_options_t keep_seq[NTRK];
    pattern_options_t keep_options;
    uint8_t keep_mutes, loop_left[CHAIN_ROWS];
    int16_t keep_bpm;
    int16_t keep[NTRK][4];                 /* the timing before the song (STOP puts it back) */
    volatile uint8_t armed, running;       /* PLAY on SONG: armed until seq_start takes it; running */
    uint8_t row, left, ref, slot;          /* the row playing, its repeats left (this one too), its longest track, slot */
    uint8_t rec, cut;                      /* REC before the song; a row changed this block */
    uint32_t swing_end_count;             /* last reference step whose nominal end counted a repeat */
    uint16_t b24;                          /* the song's time at the row playing: 1/24 beats since PLAY, mod 768 */
} chain;

static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS || c->loop > 1u || c->rsv[0]>1 || c->rsv[1])
        return 0;
    for (i = 0; i < c->count; i++) {
        const chain_row_options_t *o=&c->options[i];
        if (c->row[i].slot > (c->rsv[0]?1u:7u) || !c->row[i].repeat || c->row[i].repeat > 16u ||
            o->transpose < -48 || o->transpose > 48 || o->offset>=NSTEP || o->length>NSTEP ||
            o->mutes>=1u<<NTRK || (o->bpm && (o->bpm<40 || o->bpm>240)) ||
            o->loop_count>16 || o->jump>c->count)
            return 0;
    }
    return 1;
}
static int chain_busy(void) { return chain.running || chain.armed; }
static int name_char_ok(char c)
{
    uint32_t i;
    for (i = 0; NAME_SET[i]; i++)
        if (NAME_SET[i] == c)
            return 1;
    return 0;
}
static void name_set(char *d, const char *s)      /* d: NAME_LEN + 1 bytes, 0-padded */
{
    uint32_t i;
    for (i = 0; i <= NAME_LEN; i++)
        d[i] = 0;
    for (i = 0; i < NAME_LEN && s[i]; i++)
        d[i] = s[i];
}

/* ---- the ISR (seq.c) */
static const step_t *seq_steps(const track_t *t)   /* the steps a track plays: a song's row, else its own */
{
    return chain.running ? chain.src[chain.slot].step[t - trk] : t->step;
}
static uint32_t seq_source_length(const track_t *t)
{return (unsigned)clamp(chain.running?chain.src[chain.slot].timing[t-trk][0]:t->p[P_SLEN],1,NSTEP);}
static uint32_t seq_source_index(const track_t *t,uint32_t step)
{
    if(!chain.running)return step;
    unsigned len=seq_source_length(t);
    return (step+chain.cfg.options[chain.row].offset)%len;
}
static const step_t *seq_step_at(const track_t *t,uint32_t step)
{return &seq_steps(t)[seq_source_index(t,step)];}
#define B24_WRAP 768u                              /* a multiple of 24 and of every division's two steps in 1/24 beats */
static uint32_t step24(uint32_t div, uint32_t *den)   /* a step of DIV div in 1/24 beats (1/32: 3 .. 4BAR: 384) */
{
    return div_num_den(div, den) * 24u / *den;
}
static uint32_t chain_beats(const track_t *t, uint32_t *den)   /* t's pattern: num / den beats */
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    return len * div_num_den((uint32_t)t->p[P_SDIV], den);
}
static void chain_apply(uint32_t x)                /* row chain.row starts x samples into this block (PLAY: 0) */
{
    const chain_src_t *s;
    uint32_t k, j, best = 0, bn = 0, bd = 1;
    chain.slot = chain.cfg.row[chain.row].slot;
    chain.left = chain.cfg.row[chain.row].repeat;
    chain.swing_end_count=0xFFFFFFFFu;
    s = &chain.src[chain.slot];
    const chain_row_options_t *o=&chain.cfg.options[chain.row];
    if(o->bpm)song.g[G_BPM]=o->bpm;
    mo.src = s->m ? s->m : &MOTION_NONE;
    mo.model = s->model;
    song.seq=s->options?*s->options:(pattern_options_t){0};
    song.seq.transpose=(int8_t)(song.seq.transpose+o->transpose);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        uint32_t n, d;
        if(s->seq)t->seq=s->seq[k];else seq_options_default(&t->seq);
        motion_restore(k);                         /* the last row's motion off (motion.c) */
        for (j = 0; j < 4u; j++)
            t->p[CHAIN_TIMING[j]] = s->timing[k][j];
        if(o->length)t->p[P_SLEN]=o->length;
        t->p[P_MUTE]=(int16_t)(((chain.keep_mutes|o->mutes)>>k)&1u);
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFFu - x;              /* step 0, x samples into the block (seq_tick, chain.cut) */
        t->seq_cnt = 0xFFFFFFFFu;
        {                                          /* the grid since PLAY: its remainder, its swing pair phase */
            uint32_t den, s24 = step24((uint32_t)t->p[P_SDIV], &den);
            t->seq_par = (uint8_t)((chain.b24 / s24) & 1u);
            t->seq_rem = (uint8_t)((uint32_t)chain.b24 * beat_samples() % 24u * den / 24u);
        }
        t->rskip = 0;
        t->rat_n = 0;
        n = chain_beats(t, &d);
        if (n * bd > bn * d) {                     /* the longest; ties: the lowest track */
            best = k;
            bn = n;
            bd = d;
        }
    }
    chain.ref = (uint8_t)best;
}
static void chain_start(void)                      /* seq_start: an armed song from row 1 (a Start during one: again) */
{
    uint32_t k, j;
    if (!chain.armed && !chain.running)
        return;
    if (!chain.running) {
        chain.keep_options=song.seq;
        chain.keep_bpm=song.g[G_BPM];chain.keep_mutes=0;
        for(k=0;k<NTRK;k++)if(trk[k].p[P_MUTE])chain.keep_mutes|=(uint8_t)(1u<<k);
        for(k=0;k<NTRK;k++)chain.keep_seq[k]=trk[k].seq;
        for (k = 0; k < NTRK; k++)
            for (j = 0; j < 4u; j++)
                chain.keep[k][j] = trk[k].p[CHAIN_TIMING[j]];
        chain.rec = song.rec;
        song.rec = 0;
    }
    chain.armed = 0;
    chain.running = 1;
    chain.row = 0;
    chain.b24 = 0;
    song.g[G_BPM]=chain.keep_bpm;
    for(k=0;k<CHAIN_ROWS;k++)chain.loop_left[k]=chain.cfg.options[k].loop_count;
    chain_apply(0);
}
static void chain_stop(void)                       /* seq_stop: the timing and REC back */
{
    uint32_t k, j;
    chain.armed = 0;
    if (!chain.running)
        return;
    chain.running = 0;
    song.seq=chain.keep_options;
    song.g[G_BPM]=chain.keep_bpm;
    for(k=0;k<NTRK;k++)trk[k].p[P_MUTE]=(int16_t)((chain.keep_mutes>>k)&1u);
    for(k=0;k<NTRK;k++)trk[k].seq=chain.keep_seq[k];
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < 4u; j++)
            trk[k].p[CHAIN_TIMING[j]] = chain.keep[k][j];
    song.rec = chain.rec;
    mo.src = 0;
    mo.model = 0;
}
/* before the block's seq_ticks: the reference track's pattern ends in this block -> a repeat counted, or the next row
 * (x = the samples before that end) */
static void chain_tick(uint32_t n)
{
    track_t *t = &trk[chain.ref];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), cur;
    if (!chain.running || t->seq_pos >= 0x7FFFFFFFu || t->seq_idx + 1u != len)
        return;
    if(song.seq.swing) {
        if(chain.swing_end_count==t->seq_cnt)return;
        uint32_t period=div_period((uint32_t)t->p[P_SDIV],t->seq_rem);
        /* The row ends on the unswung grid, before a delayed next step zero.
         * Repeats leave the track clock alone so its full interval still plays. */
        cur=period-step_delay(t,period,t->seq_cnt);
    } else cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
    if (t->seq_pos + n < cur)
        return;
    if(song.seq.swing)chain.swing_end_count=t->seq_cnt;
    {
        uint32_t den;                              /* the song's time: + the reference's pattern */
        chain.b24 = (uint16_t)((chain.b24 + len * step24((uint32_t)t->p[P_SDIV], &den)) % B24_WRAP);
    }
    if (chain.left > 1u) {
        chain.left--;
        return;
    }
    const chain_row_options_t *o=&chain.cfg.options[chain.row];
    if(o->jump && (!o->loop_count || chain.loop_left[chain.row])) {
        unsigned dest=o->jump-1u;
        if(o->loop_count)chain.loop_left[chain.row]--;
        for(unsigned i=dest;i<chain.row;i++)chain.loop_left[i]=chain.cfg.options[i].loop_count;
        chain.row=(uint8_t)dest;
    } else if (chain.row + 1u < chain.cfg.count)
        chain.row++;
    else if (chain.cfg.loop) {
        chain.row = 0;
        for(unsigned i=0;i<CHAIN_ROWS;i++)chain.loop_left[i]=chain.cfg.options[i].loop_count;
    }
    else {
        seq_stop();
        return;
    }
    chain.cut = 1;
    chain_apply(cur - t->seq_pos);
    acid_gates_close();                            /* seq.c: every track restarts, no ACID gate held into the row */
}
