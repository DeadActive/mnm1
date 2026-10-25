/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 (Hortator fork): 2026 */
/* ARP (MNM-1's arpeggiator, per track: MODE SPEED RANGE CHORD on the ARP page). A note a gated model plays
 * (a step, a key, MIDI: drum_play) starts it: the note itself first, then every SPEED (a DIV at the tempo) the next
 * of the CHORD's notes over RANGE octaves, in MODE's order, each a new note (its AMP envelope from the start), for as
 * long as the note that started it is held (its gate open: the step's LEN, the key held). A slide or a legato key
 * moves the chord to the new note, the order goes on. MODE OFF, or a model without a gate (a drum): nothing. */
static const int8_t ARP_CHORD[12][5] = {   /* semitones over the note, -1 ends (params.c N_ARPC) */
    {0, 12, -1}, {0, 7, -1}, {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 5, 7, -1}, {0, 4, 7, 11, -1}, {0, 3, 7, 10, -1},
    {0, 4, 7, 10, -1}, {0, 3, 6, -1}, {0, 4, 7, 14, -1}, {0, 3, 7, 14, -1}, {0, 5, 10, 15, -1},
};
enum { AM_OFF, AM_UP, AM_DOWN, AM_UPDN, AM_RND, AM_ORDR };
static uint8_t arp_self;                                 /* the ARP's own notes: drum_play starts no new run */

static uint32_t arp_chord_n(const int16_t *p)
{
    const int8_t *c = ARP_CHORD[clamp(p[P_ARPC], 0, 11)];
    uint32_t n = 0;
    while (n < 5u && c[n] >= 0)
        n++;
    return n;
}

/* a note started by anything but the ARP: a run starts (or, gliding, the chord moves) */
static void arp_note(track_t *t, int glide)
{
    if (arp_self)
        return;
    if (!t->p[P_ARPM] || !trk_model(t)->gate) {
        t->arp_on = 0;
        return;
    }
    t->arp_nt = t->nt;
    if (glide && t->arp_on)
        return;
    t->arp_on = 1;
    t->arp_k = 1;                                        /* the note itself was the first */
    t->arp_dir = 0;
    t->arp_pos = 0;
}

/* the offset of the run's k-th note on knobs p (k counts on for ever; the mode folds it); RND draws from *rng */
static int32_t arp_off(const int16_t *p, uint32_t k, uint32_t *rng)
{
    const int8_t *c = ARP_CHORD[clamp(p[P_ARPC], 0, 11)];
    uint32_t nc = arp_chord_n(p), r = (uint32_t)clamp(p[P_ARPR], 1, 4), n = nc * r, i;
    switch (clamp(p[P_ARPM], 0, AM_ORDR)) {
    case AM_DOWN:
        i = n - 1u - k % n;
        break;
    case AM_UPDN:
        i = n > 1u ? k % (2u * n - 2u) : 0u;
        i = i < n ? i : 2u * n - 2u - i;
        break;
    case AM_RND:
        *rng = *rng * 1664525u + 1013904223u;
        i = (*rng >> 16) % n;
        break;
    case AM_ORDR:                                        /* each chord note through the octaves, then the next */
        i = k % n;
        return c[i / r] + (int32_t)(i % r) * 12;
    default:
        i = k % n;
        break;
    }
    return c[i % nc] + (int32_t)(i / nc) * 12;
}
static int32_t arp_offset(track_t *t, uint32_t k) { return arp_off(t->p, k, &t->rng); }   /* the track's run */

/* every block: the run's notes whose time has come; it ends with the gate of the note that started it */
static void arp_tick(track_t *t, uint32_t n)
{
    uint32_t len;
    if (!t->arp_on)
        return;
    if (!t->p[P_ARPM] || !dv_gate_open(t, &t->v[0])) {
        t->arp_on = 0;
        return;
    }
    len = div_period((uint32_t)t->p[P_ARPS], 0);
    t->arp_pos += n;
    if (t->arp_pos < len)
        return;
    t->arp_pos -= len;
    if (t->arp_pos >= len)
        t->arp_pos = 0;                                  /* (the tempo jumped: no burst of notes) */
    arp_self = 1;
    drum_play(t, t->rat_vel ? t->rat_vel : 100u, clamp(t->arp_nt + arp_offset(t, t->arp_k), NT_MIN, NT_MAX));
    arp_self = 0;
    t->nt = t->arp_nt;                                   /* the track's note stays the played one (KEYS, the roll) */
    t->arp_k++;
}
