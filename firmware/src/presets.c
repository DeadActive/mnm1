/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Legacy presets contain MODEL and eight SOUND knobs. SOURCE/ARCHIVE factory
 * sounds contain all 71 mapped controls; pattern, mute, note and routing stay put.
 * Stable factory UIDs and user-slot references survive bank additions in projects.
 * UPR1 keeps 128 eight-knob user slots; full sounds require a separate store.
 * Included after dmodels.c and before params.c/drum_core.c. */
typedef struct { uint8_t model; int8_t k[8]; char name[9]; } preset_t;
#define PRE_NONE 0xFFFFu                                /* no preset (track_t.pre) */
#define PRE_NUSER 128u                                  /* user slots */
enum { PB_MNM, PB_MD, PB_MODERN, PB_CLASSIC, PB_SOURCE, PB_ARCHIVE, PB_USER, PB_N };   /* (the order shown; MODERN before CLASSIC since fix 8: a
                                                          * project's reference is a uid, never an id or a bank) */
static const char *const N_PBANK[PB_N] = {"MNM", "MD", "MODERN", "CLASSIC", "SOURCE", "ARCHIVE", "USER"};
#include "mm_controls.h"
#include "mm_sound_bank.h"
#include "preset_bank.h"
#define PRE_FULL_FIRST 210u
#define PRE_NID (PRE_NFACT + PRE_NUSER)                 /* ids 0 .. PRE_NID - 1 */
#define ACID_MAXTRK 2u                                  /* ACID is the model of 2 tracks at most (CPU), by user decision */

/* the user bank: the flash record's payload as it is (project.c), 1284 B. A slot: used 1, else empty */
typedef struct { uint8_t used, model; int8_t k[8]; } upr_slot_t;
typedef struct { uint32_t magic; upr_slot_t s[PRE_NUSER]; } upr_rec_t;
typedef char upr_rec_size[sizeof(upr_rec_t) == 4u + 10u * PRE_NUSER ? 1 : -1];
typedef char pre_id_fits[PRE_NID < PRE_NONE ? 1 : -1];
#define UPR_MAGIC 0x31525055u                           /* "UPR1" */
static upr_rec_t upr;
static uint8_t upr_pending;                             /* changed while playing: written at the next STOP */
/* Late-bound by project storage; DSP-only hosts retain the legacy user bank. */
static struct {
    int (*get)(uint32_t, preset_t *);
    int (*prepare)(uint32_t);
    void (*apply)(track_t *);
} full_user;
static int preset_prepare(uint32_t id)
{
    return id>=PRE_NFACT && full_user.prepare ? full_user.prepare(id-PRE_NFACT) : 0;
}

static void mmen_legacy_defaults(track_t *t);            /* params.c */
static int preset_calibrated(uint32_t model)
{
    return model >= MM_MODEL_FIRST && model < MM_MODEL_FIRST + MM2_COUNT;
}

static void drum_cut(track_t *t);                       /* drum_core.c */
static uint32_t acid_tracks(const track_t *except);
static void motion_rebase(uint32_t k);                  /* motion.c */

/* a user slot's name: "<MODEL> NN", NN = slot + 1 (two digits, three from 100: then without the space when the
 * model's name has 5 characters: "SDTRX128") */
static void upr_name(char *d, uint32_t slot, uint32_t model)
{
    const char *m = N_MODEL[model % NMODELS];
    uint32_t n = 0, nn = slot + 1u;
    while (*m && n < 5u)
        d[n++] = *m++;
    if (nn < 100u || n < 5u)
        d[n++] = ' ';
    if (nn >= 100u)
        d[n++] = (char)('0' + nn / 100u);
    d[n++] = (char)('0' + nn / 10u % 10u);
    d[n++] = (char)('0' + nn % 10u);
    d[n] = 0;
}

/* preset id -> *out; 1, or 0 for an empty user slot or a bad id */
static int preset_get(uint32_t id, preset_t *out)
{
    const upr_slot_t *s;
    if (id < PRE_NFACT) {
        *out = PRE_FACTORY[id];
        return 1;
    }
    if (id >= PRE_NID)
        return 0;
    if (full_user.get) return full_user.get(id-PRE_NFACT,out);
    s = &upr.s[id - PRE_NFACT];
    if (s->used != 1u || s->model >= NMODELS)
        return 0;
    out->model = s->model;
    memcpy(out->k, s->k, sizeof out->k);
    upr_name(out->name, id - PRE_NFACT, s->model);
    return 1;
}

/* the name shown for id (9 bytes): the preset's, "-" for none or an empty user slot */
static void preset_name_of(uint32_t id, char *out)
{
    preset_t p;
    uint32_t i;
    if (!preset_get(id, &p)) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    for (i = 0; i < 8u && p.name[i]; i++)
        out[i] = p.name[i];
    out[i] = 0;
}

/* t's preset as shown (the name on the SOUND pages and TRACKS, the list's mark and cursor): PRE_NONE for none, an
 * emptied user slot, or one now holding another voice than t's (never another voice's name on the track) */
static uint32_t preset_of(const track_t *t)
{
    preset_t p;
    if (!preset_get(t->pre, &p) || p.model != (uint32_t)t->p[P_MODEL])
        return PRE_NONE;
    return t->pre;
}

/* a project's preset reference (project.c, FDRD): stable across firmwares whose banks grow or are reordered, unlike the
 * ids (positions). A factory preset: its uid (PRE_UID, append-only: tools/gen_presets.py), 1..0x7FFF; a user slot:
 * PRE_REC_USER | slot; none: PRE_NONE. The _in forms take the bank's uids and size (tests: another firmware's) */
#define PRE_REC_USER 0x8000u
static uint32_t preset_rec_enc_in(uint32_t id, const uint16_t *uid, uint32_t nfact)
{
    if (id < nfact)
        return uid[id];
    if (id - nfact < PRE_NUSER)
        return PRE_REC_USER | (id - nfact);
    return PRE_NONE;
}
static uint32_t preset_rec_dec_in(uint32_t rec, const uint16_t *uid, uint32_t nfact)   /* PRE_NONE: unknown */
{
    uint32_t i;
    if (rec & PRE_REC_USER)
        return (rec & 0x7FFFu) < PRE_NUSER ? nfact + (rec & 0x7FFFu) : PRE_NONE;
    for (i = 0; rec && i < nfact; i++)
        if (uid[i] == rec)
            return i;
    return PRE_NONE;
}
static uint32_t preset_rec_enc(uint32_t id) { return preset_rec_enc_in(id, PRE_UID, PRE_NFACT); }
static uint32_t preset_rec_dec(uint32_t rec) { return preset_rec_dec_in(rec, PRE_UID, PRE_NFACT); }

static uint32_t preset_bank_of(uint32_t id)            /* PB_*, PB_N for a bad id */
{
    uint32_t b;
    for (b = 0; b < PB_N; b++)
        if (id >= PRE_BANK_START[b] && id < PRE_BANK_START[b + 1u])
            return b;
    return PB_N;
}

static uint32_t preset_first_of(uint32_t model)        /* the voice's first factory preset, PRE_NONE if none */
{
    uint32_t id;
    for (id = 0; id < PRE_NFACT; id++)
        if (PRE_FACTORY[id].model == model)
            return id;
    return PRE_NONE;
}

/* id is a preset t may load: not empty, not a third ACID track */
static const char *model_refused(const track_t *t, uint32_t model)
{
    if (model >= NMODELS)
        return "INVALID MODEL";
    if (model == DM_ACID && acid_tracks(t) >= ACID_MAXTRK)
        return "ACID: 2 TRACKS MAX";
    return fxres_refused(t, DMODELS[model].render);
}
static int preset_ok(const track_t *t, uint32_t id, preset_t *p)
{
    return preset_get(id, p) && !model_refused(t, p->model);
}

/* Apply the voice and, for SOURCE/ARCHIVE, its complete mapped sound. Another
 * machine cuts the old voice; same-machine loads preserve the running voice. */
static void preset_apply(track_t *t, const preset_t *p, uint32_t id)
{
    if (id>=PRE_NFACT && full_user.apply) {
        full_user.apply(t);
        t->pre=(uint16_t)id;t->pre_ed=0;
        return;
    }
    perf_options_default(&t->perf);
    const dmodel_t *m = &DMODELS[p->model % NMODELS];
    uint32_t i;
    if ((uint32_t)t->p[P_MODEL] != p->model || t->model != p->model) {
        drum_cut(t);
        mmen_ready[mmen_index(t)] = 0;
        if (mmen_index(t) < NTRK) fxs[mmen_index(t)].model = 255;
    }
    if (preset_calibrated((uint32_t)t->p[P_MODEL]) && !preset_calibrated(p->model))
        mmen_legacy_defaults(t);
    t->p[P_MODEL] = (int16_t)(p->model % NMODELS);
    t->model = (uint8_t)t->p[P_MODEL];
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)clamp(p->k[i], m->edit[i].min, m->edit[i].max);
    if (id >= PRE_FULL_FIRST && id - PRE_FULL_FIRST < MM_SOUND_COUNT) {
        const mm_sound_t *sound = &MM_SOUNDS[id - PRE_FULL_FIRST];
        uint8_t raw[MM_CONTROL_COUNT];
        memcpy(raw, sound->raw, sizeof raw);
        mm_controls_remap(raw, sound->source_track, (uint32_t)(t - trk));
        mm_controls_set(t, raw);
    }
    t->pre = (uint16_t)id;
    t->pre_ed = 0;
}

/* Load with the audio IRQ off and update motion's base. Return -1 for an empty
 * slot, invalid ID or machine-cap refusal; pattern and routing stay untouched. */
static int preset_load(track_t *t, uint32_t id)
{
    preset_t p;
    if (!preset_ok(t, id, &p))
        return -1;
    if (preset_prepare(id)) return -1;
    fm1_irq_off();
    preset_apply(t, &p, id);
    motion_rebase((uint32_t)(t - trk));
    fm1_irq_on();
    return 0;
}

/* t's voice put back as it was (the PRESETS list's audition ended, ui_preset.c): MODEL, the 8 SOUND knobs k, the
 * reference pre and its edited mark ed, as they were (no clamp); the audio IRQ off, another voice cut first, motion's
 * base follows, as preset_load */
static void preset_put(track_t *t, uint32_t model, const int16_t *k, uint32_t pre, uint32_t ed)
{
    uint32_t i;
    fm1_irq_off();
    if ((uint32_t)t->p[P_MODEL] != model || t->model != model) {
        drum_cut(t);
        mmen_ready[mmen_index(t)] = 0;
        if (mmen_index(t) < NTRK) fxs[mmen_index(t)].model = 255;
    }
    t->p[P_MODEL] = (int16_t)(model % NMODELS);
    t->model = (uint8_t)t->p[P_MODEL];
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = k[i];
    t->pre = (uint16_t)pre;
    t->pre_ed = (uint8_t)ed;
    motion_rebase((uint32_t)(t - trk));
    fm1_irq_on();
}

/* Full sound auditions also change AMP/FILTER/LFO/effect controls. Keep the old
 * eight-knob restore API for its callers; the list restores its complete snapshot. */
static void preset_put_full(track_t *t, const int16_t *p, uint32_t pre, uint32_t ed, const perf_options_t *perf)
{
    fm1_irq_off();
    if (t->p[P_MODEL] != p[P_MODEL] || t->model != (uint32_t)p[P_MODEL])
        drum_cut(t);
    memcpy(t->p, p, sizeof t->p);
    t->perf=*perf;
    t->model = (uint8_t)t->p[P_MODEL];
    t->pre = (uint16_t)pre;
    t->pre_ed = (uint8_t)ed;
    motion_rebase((uint32_t)(t - trk));
    fm1_irq_on();
}

/* the next (dir > 0) / previous loadable id from `from`, through every bank in order, wrapping (empty user slots and a
 * capped ACID skipped). No current preset (from out of range, PRE_NONE): the first preset of the track's voice */
static uint32_t preset_step(const track_t *t, uint32_t from, int dir)
{
    preset_t p;
    uint32_t id, n;
    if (from >= PRE_NID) {
        id = preset_first_of((uint32_t)t->p[P_MODEL]);
        if (id != PRE_NONE && preset_ok(t, id, &p))
            return id;
        from = dir > 0 ? PRE_NID - 1u : 0u;
    }
    if (!dir)
        return from;
    for (n = 0, id = from; n < PRE_NID; n++) {
        id = dir > 0 ? (id + 1u < PRE_NID ? id + 1u : 0u) : (id ? id - 1u : PRE_NID - 1u);
        if (preset_ok(t, id, &p))
            return id;
    }
    return from;
}

/* a user edit of t's voice (a SOUND knob: the panel, MIDI LEARN, the SMC knobs; not LFOs or MOTION playback) */
static void preset_touch(track_t *t) { t->pre_ed = 1; }
#define PRE_SOUND(id) ((uint32_t)(id) - P_E0 < 8u)      /* a parameter id is one of the 8 SOUND knobs */

static int preset_user_free(void)                      /* the first free user slot, -1 none */
{
    uint32_t i;
    for (i = 0; i < PRE_NUSER; i++)
        if (full_user.get ? !full_user.get(i,&(preset_t){0}) : upr.s[i].used != 1u)
            return (int)i;
    return -1;
}

/* the user bank in RAM: slot <- a voice, model and the 8 SOUND knobs from kv[P_E0..P_E7] (a track's p[], or motion's
 * base while it plays: project.c preset_user_save; preset_user_delete empties one; both write the flash) */
static void upr_set(uint32_t slot, uint32_t model, const int16_t *kv)
{
    upr_slot_t *s = &upr.s[slot];
    uint32_t i;
    s->used = 1;
    s->model = (uint8_t)(model % NMODELS);
    for (i = 0; i < 8u; i++)
        s->k[i] = (int8_t)clamp(kv[P_E0 + i], -128, 127);
}
