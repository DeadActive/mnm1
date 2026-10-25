/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (8 tracks, 128 places, the LFOs' copy, FDR8) */
/* MNM-1 0.5: PARAMETER LOCKS (Felucca's MOTION store and its base). A lock is a
 * knob's value on one step. A step with a trig or a lock first puts every knob the locks moved back to the kit's value
 * (the base), then sets its own locks: a locked value lasts until the next trig, an unlocked trig plays the kit's sound.
 * PARAMETER SLIDE (step_t.pslide): each knob locked on such a step glides to its next lock on the
 * track, there exactly at that step (unlocked trigs between don't stop it; a knob locked once doesn't slide). Locks are
 * set by a step held (ui_input.c: the LOCK mode of the 8-knob pages) or recorded live (below), 240 a project.
 * MOTION (upstream Felucca 1.0's motion.c, adapted): knob moves recorded per step and played with the pattern.
 * Recording (ui_input.c motion_capture): the selected track armed (song.rec) and playing, a turn of one of its sound
 * knobs (motion_param) stores the knob's value at the nearest step of that track (the one playing, or the next past
 * its half); the same knob and step again: replaced. Not recording, while motion plays: the turn is the new base.
 * Playback (seq.c seq_tick, motion_step): a track with PLAY on entering step s sets the knobs its events at s name
 * (clamped to the knob's range); a value holds until that knob's next event; step 0, STOP and PLAY off put the base
 * back (every knob as set, taken at PLAY). In a block an LFO on a knob holds its modulated copy in t->p and the
 * value it returns to in t->lsave (lfo.c): motion reads and writes the latter (motion_slot).
 * Store: 128 events shared by the 8 tracks (upstream: 64 for 4), saved with the project (FDR8). The main loop writes
 * with the audio IRQ off; the ISR reads. */
#define MOTION_MAX 240u                          /* (MNM-1: the locks of a project) */
typedef struct { uint8_t trk, step, param; int8_t value; } motion_event_t;   /* every recordable range: -64..127 */
typedef struct { uint16_t count; uint8_t on, rsv; motion_event_t ev[MOTION_MAX]; } motion_store_t;   /* on: PLAY bits */
typedef char motion_store_size[sizeof(motion_store_t) == 964u ? 1 : -1];
#define MO_SL 8u                                 /* knobs gliding at once on a track (PARAMETER SLIDE) */
typedef struct { uint8_t id, on; int16_t from, to; uint32_t len, at; } motion_slide_t;
#define MO_W ((P_COUNT + 31u) / 32u)            /* words of a track's moved-knob bits */
typedef char motion_ids_fit[P_COUNT <= 96 && MO_W == 3u ? 1 : -1];

static struct {
    motion_store_t s;                /* the events and PLAY bits (saved with the project) */
    int16_t base[NTRK][P_COUNT];     /* every knob as set, while motion plays */
    uint32_t act[NTRK][MO_W];        /* the knobs motion moved off the base (bit = id) */
    uint8_t base_ok, full;           /* tracks whose base is taken; a turn found no free place (the UI says so) */
    const motion_store_t *src;       /* a song's row: its slot's motion (song.c), 0 = mo.s */
    const uint8_t *model;            /* .. its slot's models: a track's model now another one skips its P_E events */
    motion_slide_t sl[NTRK][MO_SL];  /* the knobs gliding (PARAMETER SLIDE) */
} mo;

/* the knobs a lock takes: the sound (not the model, mute, choke, note, routing, timing); an LFO's eight (any
 * knob of the data pages) */
static int motion_param(uint32_t id)
{
    if ((id >= P_LFO1 && id < P_LFO1 + 2u * LF_N) || (id >= P_LFO3 && id <= P_LFO3_END))
        return 1;
    return (id >= P_E0 && id <= P_E7) || id == P_LEVEL || id == P_PAN || (id >= P_DIST && id <= P_REV) ||
           (id >= P_SLPAT && id <= P_SLDEPTH) || (id >= P_AATK && id <= P_PORT) || (id >= P_FWDTH && id <= P_NLEN) ||
           (id >= P_FLPQ && id <= P_FDEC) || (id >= P_FWOFS && id <= P_DWID);   /* AMP, FILTER, EFFECTS, LEN; not
                                                                                 * the ARP's */
}
static int motion_on(uint32_t k) { return (int)((mo.s.on >> k) & 1u); }
static uint32_t motion_count(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < mo.s.count; i++)
        n += mo.s.ev[i].trk == k;
    return n;
}
static int motion_moved(uint32_t k, uint32_t id) { return (int)((mo.act[k][id >> 5] >> (id & 31u)) & 1u); }
static void motion_mark(uint32_t k, uint32_t id) { mo.act[k][id >> 5] |= 1u << (id & 31u); }

/* knob id of t as it rests: an LFO on it this block keeps that in lsave (lfo.c), else t->p */
static int16_t *motion_slot(track_t *t, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < t->lnum; i++)
        if (t->lpid[i] == id)
            return &t->lsave[i];
    return &t->p[id];
}
static void motion_take_base(uint32_t k)
{
    uint32_t id;
    for (id = 0; id < P_COUNT; id++)
        mo.base[k][id] = *motion_slot(&trk[k], id);
    mo.act[k][0] = mo.act[k][1] = mo.act[k][2] = 0;
    mo.base_ok |= (uint8_t)(1u << k);
}
static int motion_sliding(uint32_t k, uint32_t id)   /* knob id of track k is gliding (PARAMETER SLIDE) */
{
    uint32_t i;
    for (i = 0; i < MO_SL; i++)
        if (mo.sl[k][i].on && mo.sl[k][i].id == id)
            return 1;
    return 0;
}
static void motion_restore_keep(uint32_t k, int keep)   /* the moved knobs back to the base (keep: but those gliding) */
{
    uint32_t id;
    if (!((mo.base_ok >> k) & 1u))
        return;
    for (id = 0; id < P_COUNT; id++)
        if (motion_moved(k, id) && !(keep && motion_sliding(k, id))) {
            *motion_slot(&trk[k], id) = mo.base[k][id];
            mo.act[k][id >> 5] &= ~(1u << (id & 31u));
        }
}
static void motion_restore(uint32_t k)            /* the moved knobs back to the base, the glides stopped */
{
    memset(mo.sl[k], 0, sizeof mo.sl[k]);
    motion_restore_keep(k, 0);
    mo.act[k][0] = mo.act[k][1] = mo.act[k][2] = 0;
}

/* ---- the ISR (seq.c) */
static void motion_begin(void)                    /* seq_start: the base of every track */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        motion_restore(k);                         /* (a restart while playing) */
        motion_take_base(k);
    }
}
static void motion_end(void)                      /* seq_stop: the patch back */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        motion_restore(k);
    mo.base_ok = 0;
}
/* the next lock of knob id on track k after step s (within LEN, round to s itself), its distance in steps (0: none) */
static uint32_t motion_next(const motion_store_t *m, uint32_t k, uint32_t s, uint32_t id, uint32_t len, int8_t *v)
{
    uint32_t i, best = 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->ev[i];
        if (e->trk == k && e->param == id && e->step < len) {
            uint32_t d = (e->step + len - s) % len;
            d = d ? d : len;
            if (!best || d < best) {
                best = d;
                *v = e->value;
            }
        }
    }
    return best == len ? 0u : best;                 /* (only s's own lock: no other place to glide to) */
}

/* t enters step s: a trig or a lock there puts the locked knobs back to the base (but those gliding), then its locks
 * are set; a SLIDE step's locks start gliding to their next */
static __attribute__((noinline)) void motion_step(track_t *t, uint32_t s)
{
    uint32_t k = (uint32_t)(t - trk), i, moved = 0, here = 0, len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
    const motion_store_t *m = mo.src ? mo.src : &mo.s;
    if (!((m->on >> k) & 1u))
        return;
    if (!((mo.base_ok >> k) & 1u))
        motion_take_base(k);
    for (i = 0; i < m->count && !here; i++)
        here = m->ev[i].trk == k && m->ev[i].step == s;
    if (!s || here || (s < NSTEP && t->step[s].on)) {
        moved = mo.act[k][0] | mo.act[k][1] | mo.act[k][2];
        if (!s)
            motion_restore(k);                     /* (the loop's start: every glide ends too) */
        else
            motion_restore_keep(k, 1);
    }
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->ev[i];
        if (e->trk == k && e->step == s &&
            !(mo.model && mo.model[k] != t->p[P_MODEL] && e->param >= P_E0 && e->param <= P_E7)) {
            const param_desc_t *d = track_desc(t, e->param);
            int16_t v = (int16_t)clamp(e->value, d->min, d->max);
            uint32_t j;
            *motion_slot(t, e->param) = v;
            motion_mark(k, e->param);
            moved = 1;
            for (j = 0; j < MO_SL; j++)            /* a lock of its own ends its glide */
                if (mo.sl[k][j].on && mo.sl[k][j].id == e->param)
                    mo.sl[k][j].on = 0;
            if (s < NSTEP && t->step[s].pslide) {   /* PARAMETER SLIDE: to the next lock of this knob */
                int8_t to;
                uint32_t dist = motion_next(m, k, s, e->param, len, &to);
                for (j = 0; j < MO_SL && mo.sl[k][j].on; j++)
                    ;
                if (dist && j < MO_SL) {
                    motion_slide_t *g = &mo.sl[k][j];
                    g->on = 1;
                    g->id = e->param;
                    g->from = v;
                    g->to = (int16_t)clamp(to, d->min, d->max);
                    g->len = dist * div_period((uint32_t)t->p[P_SDIV], 0);
                    g->at = 0;
                }
            }
        }
    }
    if (moved && t->lnum && song.lfo_in) {         /* an LFO holds a knob this block: its copy from the new value, */
        lfo_restore_track(t);                       /* so this step's hit reads it (as lfo_hit does) */
        lfo_track(t, 0);
    }
}

/* every block (seq.c events_block, before the steps): the glides n samples on */
static void motion_slide_block(uint32_t n)
{
    uint32_t k, j;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < MO_SL; j++) {
            motion_slide_t *g = &mo.sl[k][j];
            if (!g->on)
                continue;
            g->at = g->at + n < g->len ? g->at + n : g->len;
            {                                       /* (the fraction in 32 bits: the target links no 64-bit
                                                     * division, __divdi3 / __udivdi3) */
                uint32_t a = g->at, l = g->len ? g->len : 1u;
                int32_t fr;
                while (l >= 65536u) {
                    l >>= 1;
                    a >>= 1;
                }
                fr = (int32_t)((a << 16) / (l ? l : 1u));
                *motion_slot(&trk[k], g->id) = (int16_t)(g->from + (g->to - g->from) * fr / 65536);
            }
            if (g->at >= g->len)
                g->on = 0;
        }
}

/* ---- the IRQ off (callers) */
static void motion_add(uint32_t k, uint32_t s, uint32_t id, int32_t v)   /* replaces the same place; full: refused */
{
    uint32_t i;
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk == k && mo.s.ev[i].step == s && mo.s.ev[i].param == id)
            break;
    if (i == MOTION_MAX) {
        mo.full = 1;
        return;
    }
    if (i == mo.s.count && i >= MOTION_MAX) {
        mo.full = 1;
        return;
    }
    mo.s.ev[i].trk = (uint8_t)k;
    mo.s.ev[i].step = (uint8_t)s;
    mo.s.ev[i].param = (uint8_t)id;
    mo.s.ev[i].value = (int8_t)v;
    if (i == mo.s.count)
        mo.s.count++;
    mo.s.on |= (uint8_t)(1u << k);
}
static void motion_forget(void)                   /* new knobs (a project load, INIT ALL): nothing to put back */
{
    memset(mo.act, 0, sizeof mo.act);
    mo.base_ok = 0;
    mo.full = 0;
}
static void motion_rebase(uint32_t k)             /* track k's sound replaced (a model, INIT SND): the new base */
{
    uint32_t id;
    if (!((mo.base_ok >> k) & 1u))
        return;
    for (id = 0; id < P_COUNT; id++)               /* the knobs the model left alone: back to the patch first, or */
        if (motion_moved(k, id) && (id < P_E0 || id > P_E7))   /* their recorded value would become the patch */
            *motion_slot(&trk[k], id) = mo.base[k][id];
    motion_take_base(k);
}
static int motion_valid(const motion_store_t *m)  /* a saved store: places in range, recordable, no place twice */
{
    uint32_t i, j;
    if (m->count > MOTION_MAX)
        return 0;
    for (i = 0; i < m->count; i++) {
        const motion_event_t *e = &m->ev[i];
        if (e->trk >= NTRK || e->step >= NSTEP || !motion_param(e->param) || e->value < -64)
            return 0;
        for (j = 0; j < i; j++)
            if (m->ev[j].trk == e->trk && m->ev[j].step == e->step && m->ev[j].param == e->param)
                return 0;
    }
    return 1;
}

/* ---- the main loop (they take the IRQ guard) */
/* ui_input.c: knob id of t was just turned (t->p[id] is its new value) */
static void motion_capture(track_t *t, uint32_t id)
{
    uint32_t k = (uint32_t)(t - trk), s, len, cur;
    if (id >= P_COUNT || !motion_param(id))
        return;
    fm1_irq_off();
    if (song.playing && ((song.rec >> k) & 1u) && k == song.sel) {
        len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
        if (t->seq_pos >= 0x7FFFFFFFu) {
            s = 0;                                 /* PLAY: step 0 comes next */
        } else {
            s = t->seq_idx % len;
            cur = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt);
            if (t->seq_pos > cur / 2u)
                s = (s + 1u) % len;                /* past the half: the next step */
        }
        if ((mo.base_ok >> k) & 1u)
            motion_mark(k, id);                    /* a STOP before its step still puts the base back */
        motion_add(k, s, id, t->p[id]);
    } else if ((mo.base_ok >> k) & 1u) {
        mo.base[k][id] = t->p[id];                 /* a turn while motion plays: the new base */
    }
    fm1_irq_on();
}
static void motion_set_play(uint32_t k, int on)   /* the MOTION page's PLAY */
{
    fm1_irq_off();
    if (on) {
        mo.s.on |= (uint8_t)(1u << k);
    } else {
        mo.s.on &= (uint8_t)~(1u << k);
        motion_restore(k);
    }
    fm1_irq_on();
}
static void motion_clear(uint32_t k)              /* track k's events gone, its knobs back */
{
    uint32_t i, n = 0;
    fm1_irq_off();
    motion_restore(k);
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk != k)
            mo.s.ev[n++] = mo.s.ev[i];
    memset(&mo.s.ev[n], 0, (MOTION_MAX - n) * sizeof mo.s.ev[0]);
    mo.s.count = (uint16_t)n;
    mo.s.on &= (uint8_t)~(1u << k);
    mo.full = 0;
    fm1_irq_on();
}

/* ---- PARAMETER LOCKS (MNM-1: the 8-knob pages' LOCK mode, a step held; the IRQ off inside) */
/* the lock of knob id on step s of track k: its index in mo.s, -1 none */
static int lock_find(uint32_t k, uint32_t s, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk == k && mo.s.ev[i].step == s && mo.s.ev[i].param == id)
            return (int)i;
    return -1;
}
static uint32_t lock_count_step(uint32_t k, uint32_t s)   /* locks on step s of track k */
{
    uint32_t i, n = 0;
    for (i = 0; i < mo.s.count; i++)
        n += mo.s.ev[i].trk == k && mo.s.ev[i].step == s;
    return n;
}
/* knob id on step s of track k turned by steps: from its lock, else from the kit's value; the lock set (full: refused,
 * the UI says so) */
static void lock_turn(uint32_t k, uint32_t s, uint32_t id, int32_t steps)
{
    const param_desc_t *d = track_desc(&trk[k], id);
    int i = lock_find(k, s, id);
    int32_t v = i >= 0 ? mo.s.ev[i].value : ((mo.base_ok >> k) & 1u) ? mo.base[k][id] : *motion_slot(&trk[k], id);
    if (!motion_param(id) || s >= NSTEP)
        return;
    v = clamp(v + steps, d->min, d->max);
    fm1_irq_off();
    motion_add(k, s, id, v);
    fm1_irq_on();
}
static void lock_drop(uint32_t k, uint32_t s, uint32_t id)   /* the lock of knob id on step s gone (0xFF: all of s's) */
{
    uint32_t i, n = 0;
    fm1_irq_off();
    for (i = 0; i < mo.s.count; i++) {
        const motion_event_t *e = &mo.s.ev[i];
        if (!(e->trk == k && e->step == s && (id == 0xFFu || e->param == id)))
            mo.s.ev[n++] = *e;
    }
    memset(&mo.s.ev[n], 0, (MOTION_MAX - n) * sizeof mo.s.ev[0]);
    mo.s.count = (uint16_t)n;
    mo.full = 0;
    fm1_irq_on();
}
