/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * MNM-1: 2026 Piotr Szwach */
/* The web simulator's first-visit demos, saved to project slots 1-4 with the firmware's own project_save; slot 1 is
 * then loaded (and remembered as the last project, so a returning visit opens it), so PLAY grooves at once. Each demo
 * starts from MNM-1's power-on kit (drum_core.c: GND kick, FM+ snare through the FLANGER on T3, SWSAW bass, SWPLS arp
 * on bus CD into the REVERB on T6) and swaps machines on some tracks:
 *   1 the power-on kit as it is;
 *   2 SID lead (through the flanger), DPRO pad, an LFO opening the bass's filter;
 *   3 VO-6 singing over the kit, an LFO on its vowel;
 *   4 GND hats and an FM+ bass.
 * pat: 'x' = a note, 'X' = an accent; nt[]: each step's note from C4. FX tracks (T3, T6) are triggered on step 1, so
 * they pass audio (an FX machine needs a trig). */
typedef struct {
    uint32_t model[NTRK];                                /* NMODELS: the power-on kit's machine */
    const char *pat[NTRK];
    int8_t nt[NTRK][16];
} sim_kit_t;

#define KIT NMODELS
#define FXG "x..............."                           /* an FX track's gate: one trig a bar */

static const sim_kit_t SIM_KITS[4] = {
    {{KIT, KIT, KIT, KIT, KIT, KIT},
     {"X...x...x...x...", "....X.......X..x", FXG, "x..x..x...x.x.x.", "x.......x.......", FXG},
     {{0}, {0}, {0}, {0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 3, 0, 5, 0, 7, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 5}, {0}}},
    {{KIT, DM_MMSID, KIT, KIT, DM_MMDP, KIT},
     {"X...x...x...x...", "x..x..x.x.x..x..", FXG, "x.xx..x.x.xx..x.", "x...............", FXG},
     {{0}, {12, 0, 0, 15, 0, 0, 19, 0, 17, 0, 15, 0, 0, 12, 0, 0}, {0},
      {0, 0, 0, 0, 0, 0, 12, 0, -2, 0, -2, 10, 0, 0, -2, 0}, {0}, {0}}},
    {{KIT, KIT, KIT, KIT, DM_MMVO, KIT},
     {"X..x..x.X..x..x.", "....X.......X...", FXG, "x...x...x...x...", "x.......x...x...", FXG},
     {{0}, {0}, {0}, {0, 0, 0, 0, 5, 0, 0, 0, 3, 0, 0, 0, -2, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 7}, {0}}},
    {{KIT, DM_MMGND, KIT, DM_MMFM, KIT, KIT},
     {"X.....x...x.....", "x.x.x.x.x.x.x.xX", FXG, "x..x...x.x..x...", "x.......x.......", FXG},
     {{0}, {0}, {0}, {-12, 0, 0, -12, 0, 0, 0, 0, -5, 0, -12, 0, 2, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, -2}, {0}}},
};

static void sim_lfo(track_t *t, uint32_t page, uint32_t dest, uint32_t wave, uint32_t spd, uint32_t dpth)
{
    int16_t *q = &t->p[P_LFO1];
    q[LF_PAGE] = (int16_t)page;
    q[LF_DEST] = (int16_t)dest;
    q[LF_TRIG] = LT_FREE;
    q[LF_WAVE] = (int16_t)wave;
    q[LF_SPD] = (int16_t)spd;
    q[LF_DPTH] = (int16_t)dpth;
}

static void sim_demo_install(void)
{
    uint32_t s, i, k;
    for (s = 0; s < 4u; s++) {
        const sim_kit_t *kit = &SIM_KITS[s];
        drum_tracks_init();
        for (i = 0; i < NTRK; i++) {
            track_t *t = &trk[i];
            if (kit->model[i] != KIT)
                drum_set_model(t, kit->model[i]);
            for (k = 0; k < 16u; k++) {
                t->step[k].on = kit->pat[i][k] != '.';
                t->step[k].acc = kit->pat[i][k] == 'X';
                t->step[k].nt = kit->nt[i][k];
            }
        }
        if (s == 1u) {
            trk[1].p[P_E0] = 0;                           /* SID: a lead in the fourth octave */
            trk[4].p[P_ARPM] = 0;                         /* DPRO: a held pad, the arp off */
            trk[4].p[P_AHOLD] = 127;
            trk[4].p[P_NLEN] = 127;
            sim_lfo(&trk[3], LP_FLT, 1, LW_TRI, 16, 90);  /* the bass's WDTH breathes over two bars */
        } else if (s == 2u) {
            trk[4].p[P_ARPM] = 0;
            trk[4].p[P_NLEN] = 120;
            sim_lfo(&trk[4], LP_SYN, 1, LW_SINE, 32, 110);   /* VO-6's VOWL */
        }
        song.sel = 0;
        project_save(s);
    }
    lastp.set = 1;                                        /* the demos as one install: slot 1 is the one remembered
                                                           * (a returning visit opens it, as before), not slot 4 */
    lastp.slot = 0;
    settings_save();
    drum_tracks_init();
    project_load(0);
}
