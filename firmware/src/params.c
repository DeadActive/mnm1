/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Parameter descriptions and value formatting. P_E0..P_E7 are described by the track's model. */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};   /* ids fixed: slow rates appended */
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS",   /* G_CLOCK: = the input ring's source (1 USB, 2 TRS, */
#if FELUCCA_BLE
                                      "BLE",                   /* 3 BLE: bt_midi.c) */
#endif
};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SLOT[] = {"-", "A", "B", "C", "D"};   /* G_SLOT 1..4: the project slots A..D */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_RTYPE[] = {"ROOM", "SPRNG"};              /* G_RTYPE: the reverb bus's model (fx.c); SPRING in 5 */
static const char *const N_CHOKE[] = {"OFF", "1", "2", "3", "4"};
static const char *const N_SRC[] = {"STEP", "G-KCK", "G-SNR", "G-HAT"};   /* P_SRC: its steps or a Grids channel */
static const char *const N_GMODE[] = {"MAP", "EUCL"};
static const char *const N_GHOST[] = {"MUTE", "KEEP", "HIDE"};
static const char *const N_CSRC[] = {"OFF", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const N_KNEE[] = {"HARD", "SOFT"};
static const char *const N_LWAVE[] = {"TRI", "SINE", "SQR", "SAW", "EXP", "RND", "ITRI", "ISIN", "ISQR", "RAMP",
                                      "IEXP"};   /* LFO WAVE (LW_) */
static const char *const N_LTRIG[] = {"FREE", "TRIG", "HOLD", "ONE", "HALF"};   /* LFO TRIG (LT_) */
static const char *const N_LPAGE[] = {"SYN", "AMP", "FLT", "EFX", "LF1", "LF2", "LF3", "PTCH"};   /* LFO PAGE (LP_) */
static const char *const N_LPTCH[] = {"VIB", "1/2", "SEMI", "2ST", "5TH", "OCT", "4OCT", "16OC"};   /* PTCH's DESTs */
static const char *const N_BUS[] = {"AB", "CD", "EF", "OFF"};   /* P_OUT: the mix buses (fx.c) */
static const char *const N_FXIN[] = {"NEIB", "AB", "CD", "EF"};  /* P_IN: an FX machine's input */
static const char *const N_FXG[] = {"ON", "TRIG"};               /* P_FXG */
static const char *const N_ARPM[] = {"OFF", "UP", "DOWN", "UPDN", "RND", "ORDR"};   /* ARP MODE (arp.c) */
static const char *const N_ARPC[] = {"OCT", "5TH", "MAJ", "MIN", "SUS4", "MAJ7", "MIN7", "DOM7", "DIM", "ADD9",
                                     "MIN9", "QUART"};   /* ARP CHORD (arp.c ARP_CHORD) */
static const char *const N_PRIO[] = {"HIGH", "LOW"};                 /* P_PRIO */
#define LFO_TP(b, df)                                                                                     \
    [b + LF_PAGE] = PE("PAGE", N_LPAGE, 0), [b + LF_DEST] = PD("DEST", df, 0, 7, 0),                          \
    [b + LF_TRIG] = PE("TRIG", N_LTRIG, 0), [b + LF_WAVE] = PE("WAVE", N_LWAVE, 0),                          \
    [b + LF_MULT] = PD("MULT", F_LMULT, 0, 6, 1), [b + LF_SPD] = PD("SPD", F_INT, 0, 127, 64),              \
    [b + LF_INTL] = PD("INTL", F_INT, 0, 127, 0), [b + LF_DPTH] = PD("DPTH", F_INT, 0, 127, 0)
static const param_desc_t TP[P_COUNT] = {
    [P_MODEL] = PE("MODEL", N_MODEL, 0),
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_CHOKE] = PE("CHOKE", N_CHOKE, 0),
    [P_NOTE] = PD("NOTE", F_INT, 0, 127, 36),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DSND", F_INT, -64, 63, 0),      /* the track's delay's send (+: ping-pong) */
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_AATK] = PD("ATK", F_TIME, 0, 127, 0),       /* AMP (mm_synth.c mm_out) */
    [P_AHOLD] = PD("HOLD", F_AHOLD, 0, 127, 127),
    [P_ADEC] = PD("DEC", F_ADEC, 0, 127, 127),
    [P_AREL] = PD("REL", F_ADEC, 0, 126, 40),
    [P_PORT] = PD("PORT", F_INT, 0, 127, 0),
    [P_SRC] = PE("SRC", N_SRC, 0),
    [P_DUCK] = PE("DUCK", N_ONOFF, 0),
    LFO_TP(P_LFO1, F_LDEST1),
    LFO_TP(P_LFO2, F_LDEST2),
    LFO_TP(P_LFO3, F_LDEST3),
    [P_FWDTH] = PD("WDTH", F_INT, 0, 127, 127),   /* FILTER: the low-pass this far over BASE (filter.c) */
    [P_FATK] = PD("ATK", F_TIME, 0, 127, 0),
    [P_SRR] = PD("SRR", F_INT, 0, 127, 0),
    [P_NLEN] = PD("LEN", F_INT, 1, 127, 96),       /* a step's note: LEN / 128 of it */
    [P_ARPM] = PE("MODE", N_ARPM, 0),
    [P_ARPS] = PE("SPEED", N_DIV, 2),
    [P_ARPR] = {"RANGE", F_INT, 1, 4, 1, 0, "OCT"},
    [P_FLPQ] = PD("LPQ", F_INT, 0, 127, 0),
    [P_FCUT] = PD("BASE", F_INT, 0, 127, 0),
    [P_FRESO] = PD("HPQ", F_INT, 0, 127, 0),
    [P_FENV] = PD("BOFS", F_INT, -64, 63, 0),
    [P_FDEC] = PD("DEC", F_FDEC, 0, 127, 40),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ARPC] = PE("CHORD", N_ARPC, 0),
    [P_OUT] = PE("OUT", N_BUS, BUS_AB),
    [P_IN] = PE("IN", N_FXIN, IN_NEIB),
    [P_FXG] = PE("GATE", N_FXG, 0),
    [P_FWOFS] = PD("WOFS", F_INT, -64, 63, 0),
    [P_EQF] = PD("EQF", F_CUTOFF, 0, 127, 64),     /* EFFECTS (fx.c track_eq, track_delay) */
    [P_EQG] = PD("EQG", F_EQG, 0, 127, 64),
    [P_DTIM] = PD("DTIM", F_INT, 1, 127, 48),
    [P_DFB] = PD("DFB", F_INT, 0, 127, 32),
    [P_DBAS] = PD("DBAS", F_INT, 0, 127, 0),
    [P_DWID] = PD("DWID", F_INT, 0, 127, 96),
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_DASH, 0),
    [G_SYNC] = PE("SYNC", N_DASH, 0),
    [G_ROUTE] = PE("ROUT", N_DASH, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = {"SLOT", F_ENUM, 1, 4, 1, N_SLOT, 0},
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 1, 16, 10),
    [G_GMODE] = PE("MODE", N_GMODE, 0),
    [G_GX] = PD("X", F_INT, 0, 127, 64),
    [G_GY] = PD("Y", F_INT, 0, 127, 64),
    [G_GCHAOS] = PD("CHAOS", F_PCT, 0, 127, 0),
    [G_GFILL1] = PD("FIL K", F_PCT, 0, 127, 64),
    [G_GFILL2] = PD("FIL S", F_PCT, 0, 127, 64),
    [G_GFILL3] = PD("FIL H", F_PCT, 0, 127, 64),
    [G_GLEN1] = PD("LEN K", F_STEPS, 1, 32, 16),
    [G_GLEN2] = PD("LEN S", F_STEPS, 1, 32, 12),
    [G_GLEN3] = PD("LEN H", F_STEPS, 1, 32, 8),
    [G_CSRC] = PE("SRC", N_CSRC, 0),
    [G_CTHR] = PD("THRSH", F_CTHR, 0, 127, 26),
    [G_CRAT] = PD("RATIO", F_CRAT, 0, 127, 45),
    [G_CREL] = PD("REL", F_CREL, 0, 127, 26),
    [G_CATK] = PD("ATK", F_CATK, 0, 127, 2),
    [G_CKNEE] = PE("KNEE", N_KNEE, 1),
    [G_CMKUP] = PD("MKUP", F_CMKUP, 0, 127, 0),
    [G_CGHOST] = PE("GHOST", N_GHOST, CG_KEEP),
    [G_RTYPE] = PE("TYPE", N_RTYPE, 0),
};

/* after G_COUNT: the TOOLS actions (G_CLRALL, G_INITALL) and GLOBAL 3/3's MUTE and METRO (G_MUTEBAR, G_METRO: the
 * device settings settings.mutebar, fx_metro); their knob values are song.act, not song.g / projects */
static const char *const N_MUTEBAR[] = {"NOW", "BAR"};
static const char *const N_METRO[] = {"OFF", "LOW", "MID", "HIGH"};
static const param_desc_t GP_ACT[4] = {PE("CLR*", N_GO, 0), PE("INIT*", N_GO, 0), PE("MUTE", N_MUTEBAR, 0),
                                       PE("METRO", N_METRO, 0)};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].edit[id - P_E0];
    return &TP[id];
}

/* (upstream 1.0.2, #48) the note divisions in the order of their length, longest first (triplets between their
 * neighbours), on the knobs and the gauges; the stored values (N_DIV ids: projects) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t *enum_order(const param_desc_t *d) { return d->names == N_DIV ? DIV_ORDER : 0; }
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    return clamp(v + steps, d->min, d->max);
}

static void fmt_ms10(char *val, const char **unit, uint32_t ms10)   /* a time in 0.1 ms: "4.5ms", "120ms", "1.20s" */
{
    if (ms10 < 100u) {
        fmt_fix(val, (int32_t)ms10, 1);
        *unit = "ms";
    } else if (ms10 < 10000u) {
        fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
        *unit = "ms";
    } else {
        fmt_fix(val, (int32_t)(ms10 / 100u), 2);
        if (ms10 >= 100000u)
            fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
        *unit = "s";
    }
}

static uint32_t flt_dec_idx(int32_t v);           /* filter.c */
static const uint8_t LFO_PAGE_ID[LP_PTCH][8];      /* lfo.c */
/* LFO l's DEST dest by name: the knob of its PAGE by its label ("--" where the engine has none), PTCH's range */
static const char *lfo_dest_label(const track_t *t, uint32_t l, int32_t dest)
{
    uint32_t pg = (uint32_t)clamp(t->p[(l == 0u ? P_LFO1 : l == 1u ? P_LFO2 : P_LFO3) + LF_PAGE], 0, LP_N - 1);
    const param_desc_t *d;
    dest = clamp(dest, 0, 7);
    if (pg == LP_PTCH)
        return N_LPTCH[dest];
    d = track_desc(t, LFO_PAGE_ID[pg][dest]);
    return d->label && d->label[0] != '-' ? d->label : "--";
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT: {                                     /* the share of the knob's own range (0..100 swing, 0..127) */
        int32_t r = d->max - d->min > 0 ? d->max - d->min : 1;
        fmt_int(val, ((v - d->min) * 100 + r / 2) / r);
        *unit = "%";
        break;
    }
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME:
        fmt_ms10(val, unit, TIME_MS_X10[v & 127]);
        break;
    case F_LDEST1:                                    /* the knob on the LFO's PAGE, by its label */
    case F_LDEST2:
    case F_LDEST3:
        str_cpy(val, lfo_dest_label(TSEL, (uint32_t)(d->fmt - F_LDEST1), v), 6);
        break;
    case F_LMULT:                                     /* 1X .. 64X */
        fmt_int(val, 1 << clamp(v, 0, 6));
        str_cpy(val + str_len(val), "X", 2);
        break;
    case F_EQG:                                       /* the EQ's gain, +-36 dB (64: 0) */
        fmt_int(val, (clamp(v, 0, 127) - 64) * 36 / 64);
        *unit = "dB";
        break;
    case F_NOTEO:                                     /* a MIDI note as name + octave (60 = C4): "C3", "F#5" */
        str_cpy(val, N_NOTE[(uint32_t)clamp(v, 0, 127) % 12u], 6);
        fmt_int(val + str_len(val), clamp(v, 0, 127) / 12 - 1);
        break;
    case F_AHOLD:                                     /* AMP HOLD: 1 ms .. 8 s; 127 while the gate is open */
        if (v >= 127)
            str_cpy(val, "GATE", 6);
        else
            fmt_ms10(val, unit, TIME_MS_X10[clamp(v, 0, 126)]);
        break;
    case F_ADEC:                                      /* AMP DEC / REL, to -60 dB: DECAY_K's 5 ms .. 4 s; DEC 127 stays */
        if (v >= 127)
            str_cpy(val, "INF", 6);
        else
            fmt_ms10(val, unit, TIME_MS_X10[22 + clamp(v, 0, 126) * 726 / 1000]);
        break;
    case F_FDEC:                                      /* FILTER DECAY: 5 ms .. 2 s (filter.c flt_dec_idx) */
        fmt_ms10(val, unit, TIME_MS_X10[flt_dec_idx(v)]);
        break;
    case F_CTHR: {                                    /* whole dB from -10 down (the column fits "-24 dB") */
        int32_t d = comp_thr_dbx10(v);
        if (d <= -100)
            fmt_int(val, (d - 5) / 10);
        else
            comp_fmt_db10(val, d);
        *unit = "dB";
        break;
    }
    case F_CRAT:
        comp_ratio_text(v, val, unit);
        break;
    case F_CMKUP:
        comp_makeup_text(v, song.g[G_CTHR], val, unit);
        break;
    case F_CATK:
        fmt_ms10(val, unit, COMP_ATK_MS_X10[clamp(v, 0, 127)]);
        break;
    case F_CREL:
        fmt_ms10(val, unit, COMP_REL_MS_X10[clamp(v, 0, 127)]);
        break;
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* PROB of a step (step_t.cond). The knob runs 0 %, 5 % .. 100 % (positions 0..20), 1-SHOT (21), then A/B
 * (22..56: 1/2, 2/2, 1/3 .. 8/8, "plays on loop A of every B"). Stored so that 0 is 100 % (a zeroed step is
 * plain): 0 = 100 %, 1..20 = 0 .. 95 %, 21.. as the knob. */
#define COND_POS_100 20u
#define COND_1SHOT 21u
#define COND_MAX 56u
static uint32_t cond_pos(uint32_t c) { return !c ? COND_POS_100 : c <= COND_POS_100 ? c - 1u : c < COND_MAX ? c : COND_MAX; }
static uint32_t cond_store(uint32_t pos)
{
    return pos == COND_POS_100 ? 0u : pos < COND_POS_100 ? pos + 1u : pos < COND_MAX ? pos : COND_MAX;
}

static void cond_ab(uint32_t c, uint32_t *a, uint32_t *b)   /* an A/B code (22..56): A of every B */
{
    uint32_t i = c > 22u ? (c < COND_MAX ? c : COND_MAX) - 22u : 0u, n = 2u;
    while (i >= n) {
        i -= n;
        n++;
    }
    *a = i + 1u;
    *b = n;
}

static void cond_format(uint32_t c, char *s)      /* "100%", "35%", "1-SHOT", "3/5"; s holds 8 bytes */
{
    uint32_t a, b;
    if (c < COND_1SHOT) {
        fmt_int(s, !c ? 100 : (int32_t)(c - 1u) * 5);
        str_cpy(s + str_len(s), "%", 2);
    } else if (c == COND_1SHOT) {
        str_cpy(s, "1-SHOT", 8);
    } else {
        cond_ab(c, &a, &b);
        s[0] = (char)('0' + a);
        s[1] = '/';
        s[2] = (char)('0' + b);
        s[3] = 0;
    }
}
