/* SPDX-License-Identifier: GPL-3.0-only
 * Sound data from MONO2K by Inigo Hernandez (GPL-3.0-only),
 * https://github.com/InigoHernandez/mono2k
 * Host-only exporter: link the source core so its factory migrations run.
 * Build with -I../mono2k/firmware/src -I../mono2k/firmware/gen
 * -I../mono2k/host and -DMONO2K_COMMIT='"<git revision>"'.
 * Run twice and compare stdout before replacing tests/mono2k_sounds.json.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m2k_core.c"
#include "m2k_bank_rev9.c"

#ifndef MONO2K_COMMIT
#error "Pass the source checkout's full git revision as MONO2K_COMMIT"
#endif
#ifdef NDEBUG
#error "The exporter requires its validation assertions"
#endif

static const char *const MACHINE_ENUM[M_COUNT] = {
    [M_GSIN] = "M_GSIN", [M_GPRC] = "M_GPRC", [M_SID] = "M_SID",
    [M_FMS] = "M_FMS", [M_FMD] = "M_FMD", [M_SAW] = "M_SAW",
    [M_ENS] = "M_ENS", [M_WAV] = "M_WAV", [M_VOX] = "M_VOX",
    [M_FMP] = "M_FMP", [M_PUL] = "M_PUL", [M_FXC] = "M_FXC",
    [M_FXR] = "M_FXR", [M_FXD] = "M_FXD", [M_GNOI] = "M_GNOI",
    [M_DKIK] = "M_DKIK", [M_DSNR] = "M_DSNR", [M_DHAT] = "M_DHAT",
    [M_DCYM] = "M_DCYM"
};

static const char *const PARAM_NAME[] = {
    "SYN_A", "SYN_B", "SYN_C", "SYN_D", "SYN_E", "SYN_F", "SYN_G", "SYN_H",
    "AMP_ATK", "AMP_HOLD", "AMP_DEC", "AMP_REL", "AMP_DIST", "AMP_VOL", "AMP_PAN", "AMP_PORT",
    "FILT_BASE", "FILT_WDTH", "FILT_HPQ", "FILT_LPQ", "FILT_ATK", "FILT_DEC", "FILT_BOFS", "FILT_WOFS",
    "FX_EQF", "FX_EQG", "FX_SRR", "FX_DTIM", "FX_DSND", "FX_DFB", "FX_DBAS", "FX_DWID",
    "DSTR_BITS", "DSTR_FOLD", "DSTR_XOR", "DSTR_SPU",
    "TRACK_PTCH", "TRACK_LEVL", "ARP_MODE", "ARP_SPEED", "ARP_RANGE", "TRACK_GATE",
    "LFO1_TRACK", "LFO1_DEST", "LFO1_WAVE", "LFO1_TRIG", "LFO1_MULT", "LFO1_SPEED", "LFO1_PHASE", "LFO1_DEPTH",
    "LFO2_TRACK", "LFO2_DEST", "LFO2_WAVE", "LFO2_TRIG", "LFO2_MULT", "LFO2_SPEED", "LFO2_PHASE", "LFO2_DEPTH",
    "LFO3_TRACK", "LFO3_DEST", "LFO3_WAVE", "LFO3_TRIG", "LFO3_MULT", "LFO3_SPEED", "LFO3_PHASE", "LFO3_DEPTH",
    "MOD_TYPE", "MOD_RATE", "MOD_DEPTH", "MOD_MIX", "WAVE_BANK"
};
_Static_assert(sizeof PARAM_NAME / sizeof *PARAM_NAME == P_COUNT, "source parameter layout changed");
_Static_assert(P_ATK == 8 && P_BASE == 16 && P_EQF == 24 && P_BITS == 32 && P_PTCH == 36 &&
               P_LFO == 42 && P_MODT == 66 && P_WBNK == 70, "source parameter offsets changed");
_Static_assert(M_COUNT == 19 && M2K_TRACKS == 6 && M2K_PATTERNS >= 16, "source machine or bank layout changed");

static void json_string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

static void check_defaults(void)
{
    m2k_pattern_t p;
    assert(strlen(MONO2K_COMMIT) == 40);
    assert(strspn(MONO2K_COMMIT, "0123456789abcdef") == 40);
    for (int m = 0; m < M_COUNT; m++) {
        assert(MACHINE_ENUM[m] && MI[m].name);
        kit_default(&p, 0, m);
        assert(p.machine[0] == m);
        assert(!memcmp(p.kit[0], MACH_DEF[m], 8));
        for (int k = 0; k < P_COUNT; k++) assert(p.kit[0][k] <= 127);
    }
    for (int k = 0; k < P_COUNT; k++)
        for (int j = 0; j < k; j++) assert(strcmp(PARAM_NAME[k], PARAM_NAME[j]));
}

static void export_bank(int historical)
{
    const int patterns = historical ? 16 : FACTORY_DEMOS;
    const int expected = historical ? 96 : 24;
    unsigned char seen[16][M2K_TRACKS] = {{0}};
    int count = 0;
    m2k_init(1);
    if (historical) {
        factory_rev9(); /* This function converts all historical sounds onto the current parameter laws. */
        for (int p = 0; p < patterns; p++)
            for (int t = 0; t < M2K_TRACKS; t++) sound_save(p * M2K_TRACKS + t, PAT(p), t, 0);
    } else {
        for (int i = expected; i < M2K_SOUNDS; i++) assert(!snd[i].name[0]);
    }
    printf("    {\n      \"bank\": \"%s\",\n", historical ? "historical_rev9" : "current");
    printf("      \"factory\": \"%s\",\n", historical ? "factory_rev9 (including migrations)" : "m2k_init / projects_factory");
    printf("      \"count\": %d,\n      \"sounds\": [\n", expected);
    for (int p = 0; p < patterns; p++) {
        const m2k_pattern_t *pattern = PAT(p);
        assert(pattern->name[0]);
        for (int t = 0; t < M2K_TRACKS; t++) {
            int slot = p * M2K_TRACKS + t;
            const m2k_sound_t *s = &snd[slot];
            assert(!seen[p][t]++);
            assert(s->name[0] && s->machine < M_COUNT);
            assert(s->machine == pattern->machine[t]);
            assert(!memcmp(s->kit, pattern->kit[t], P_COUNT));
            assert(snd_cat[slot] > CAT_ALL && snd_cat[slot] < CAT_COUNT);
            for (int k = 0; k < P_COUNT; k++) assert(s->kit[k] <= 127);
            printf("        {\n          \"source_id\": \"%s:A%02d:T%d\",\n",
                   historical ? "historical_rev9" : "current", p + 1, t + 1);
            printf("          \"slot\": %d, \"pattern\": %d, \"track\": %d,\n", slot + 1, p + 1, t + 1);
            printf("          \"pattern_name\": "); json_string(pattern->name);
            printf(", \"tempo_bpm\": %u,\n", 30u + pattern->glob[G_TMPO]);
            printf("          \"name\": "); json_string(s->name);
            printf(", \"category\": "); json_string(CAT_N[snd_cat[slot]]);
            printf(",\n          \"machine_id\": %u, \"machine_enum\": ", s->machine);
            json_string(MACHINE_ENUM[s->machine]);
            printf(", \"machine_name\": "); json_string(MI[s->machine].name);
            printf(",\n          \"synth_labels\": [");
            for (int k = 0; k < 8; k++) {
                if (k) printf(", ");
                json_string(MI[s->machine].syn[k].s4);
            }
            printf("],\n          \"params\": {\n");
            for (int k = 0; k < P_COUNT; k++) {
                printf("            "); json_string(PARAM_NAME[k]);
                printf(": %u%s\n", s->kit[k], k + 1 == P_COUNT ? "" : ",");
            }
            printf("          }\n        }%s\n", ++count == expected ? "" : ",");
        }
    }
    assert(count == expected);
    printf("      ]\n    }");
}

int main(void)
{
    check_defaults();
    printf("{\n  \"schema\": 1,\n  \"source\": \"MONO2K\",\n");
    printf("  \"author\": \"Inigo Hernandez\",\n  \"license\": \"GPL-3.0-only\",\n");
    printf("  \"repository\": \"https://github.com/InigoHernandez/mono2k\",\n");
    printf("  \"commit\": \"%s\",\n  \"parameter_count\": %d,\n", MONO2K_COMMIT, P_COUNT);
    printf("  \"encoding\": \"raw 0..127; bipolar parameters centred on 64\",\n  \"banks\": [\n");
    export_bank(0);
    printf(",\n");
    export_bank(1);
    printf("\n  ]\n}\n");
    return fflush(stdout) || ferror(stdout) ? 1 : 0;
}
