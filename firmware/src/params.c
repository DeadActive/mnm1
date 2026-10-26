/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Parameter descriptions and value formatting. P_E0..P_E7 are described by the track's model. */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR", "3/4X", "3/2X"};   /* ids fixed: slow rates appended */
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS",   /* G_CLOCK: = the input ring's source (1 USB, 2 TRS, */
#if FELUCCA_BLE
                                      "BLE",                   /* 3 BLE: bt_midi.c) */
#endif
};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_SLOT[] = {"-", "A", "B", "C", "D", "E", "F", "G", "H"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_RTYPE[] = {"ROOM", "SPRNG", "STEREO"};              /* G_RTYPE: the reverb bus's model (fx.c); SPRING in 5 */
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
    [P_MVOL] = PD("VOL", F_INT, 0, 127, 64),
    [P_BITS] = PD("BITS", F_INT, 0, 127, 0),
    [P_FOLD] = PD("FOLD", F_INT, 0, 127, 0),
    [P_XOR] = PD("XOR", F_INT, 0, 127, 0),
    [P_SPU] = PD("SPU", F_INT, 0, 127, 0),
    [P_MODT] = PD("TYPE", F_INT, 0, 127, 0),
    [P_MODR] = PD("RATE", F_INT, 0, 127, 40),
    [P_MODD] = PD("DEPTH", F_INT, 0, 127, 64),
    [P_MODM] = PD("MIX", F_INT, 0, 127, 0),
    [P_WBNK] = PD("BANK", F_INT, 0, 127, 0),
    [P_L1TRK] = PD("TRK", F_INT, 0, NTRK, 0),
    [P_L2TRK] = PD("TRK", F_INT, 0, NTRK, 0),
    [P_L3TRK] = PD("TRK", F_INT, 0, NTRK, 0),
    [P_L1PHS] = PD("PHASE", F_INT, 0, 127, 0),
    [P_L2PHS] = PD("PHASE", F_INT, 0, 127, 0),
    [P_L3PHS] = PD("PHASE", F_INT, 0, 127, 0),
    [P_PTCH] = PD("PTCH", F_INT, 0, 127, 64),
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
    [G_SLOT] = {"SLOT", F_ENUM, 1, 8, 1, N_SLOT, 0},
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
    [G_TAPE] = PD("TAPE", F_INT, 0, 127, 0),
    [G_ADPC] = PD("CRUNCH", F_INT, 0, 127, 0),
    [G_HISS] = PD("HISS", F_INT, 0, 127, 0),
    [G_DRIVE] = PD("DRIVE", F_INT, 0, 127, 0),
};

/* after G_COUNT: the TOOLS actions (G_CLRALL, G_INITALL) and GLOBAL 3/3's MUTE and METRO (G_MUTEBAR, G_METRO: the
 * device settings settings.mutebar, fx_metro); their knob values are song.act, not song.g / projects */
static const char *const N_MUTEBAR[] = {"NOW", "BAR"};
static const char *const N_METRO[] = {"OFF", "LOW", "MID", "HIGH"};
static const param_desc_t GP_ACT[G_ACT_END-G_COUNT] = {PE("CLR*", N_GO, 0), PE("INIT*", N_GO, 0), PE("MUTE", N_MUTEBAR, 0),
    PE("METRO", N_METRO, 0), {"KIT",F_ENUM,1,8,1,N_SLOT,0},PE("NAME",N_DASH,0),PE("LOAD",N_GO,0),PE("SAVE",N_GO,0)};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].edit[id - P_E0];
    if ((uint32_t)t->p[P_MODEL] >= MM_MODEL_FIRST && (uint32_t)t->p[P_MODEL] < MM_MODEL_FIRST + MM2_COUNT) {
        const param_desc_t *d = mm_control_desc(id);
        if (d) return d;
    }
    return &TP[id];
}

/* A family change must not interpret source raw LFO/ARP values as legacy enums. */
static void mmen_legacy_defaults(track_t *t)
{
    uint32_t id;
    for (id = P_E7 + 1; id < P_COUNT; id++)
        if (mm_control_desc(id) && id != P_LEVEL && id != P_PAN)
            t->p[id] = TP[id].def;
}

/* (upstream 1.0.2, #48) the note divisions in the order of their length, longest first (triplets between their
 * neighbours), on the knobs and the gauges; the stored values (N_DIV ids: projects) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[12] = {9, 8, 7, 6, 0, 1, 4, 10, 2, 5, 11, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
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

/* Value laws from MONO2K by Inigo Hernandez (GPL-3.0-only).
 * https://github.com/InigoHernandez/mono2k
 * Raw storage and knob ranges stay unchanged. Formatting owns no UI layout.
 * Five visible characters plus terminator; units stay separately displayed. */
enum { MMF_ADEC, MMF_AHLD, MMF_AMOD, MMF_AREL, MMF_ARNG, MMF_ASPD, MMF_ATIM, MMF_BI, MMF_BITS, MMF_CONS, MMF_DBHZ, MMF_DCNT, MMF_DECT, MMF_DEST, MMF_DMUL, MMF_DNHZ, MMF_DOPN, MMF_DRA, MMF_DRD, MMF_DTIM, MMF_DWV4, MMF_DWV5, MMF_EQHZ, MMF_FATK, MMF_FDEC, MMF_FOCT, MMF_FR24, MMF_FR64, MMF_FRSQ, MMF_FWID, MMF_GATE, MMF_LTRG, MMF_LWAV, MMF_MFRQ, MMF_MHZ, MMF_MODT, MMF_MSRC, MMF_MULT, MMF_NUM, MMF_ONOFF, MMF_PAN, MMF_PCH, MMF_SEMI, MMF_SIDM, MMF_SIDW, MMF_SRR, MMF_TIME, MMF_TRK, MMF_TUN2, MMF_VOF1, MMF_VOF2, MMF_WAVE, MMF_WBNK };
static const uint8_t mmf_synth[19][8]={
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_TIME,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_BI,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_TIME,MMF_NUM,MMF_NUM},
{MMF_NUM,MMF_NUM,MMF_ONOFF,MMF_SIDW,MMF_SIDM,MMF_MSRC,MMF_MFRQ,MMF_TUN2},
{MMF_FR24,MMF_BI,MMF_BI,MMF_NUM,MMF_FR24,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_FR64,MMF_BI,MMF_NUM,MMF_BI,MMF_FRSQ,MMF_BI,MMF_NUM,MMF_TUN2},
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_PCH,MMF_PCH,MMF_PCH,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_WAVE,MMF_BI,MMF_DECT,MMF_NUM,MMF_NUM,MMF_BI,MMF_NUM,MMF_NUM},
{MMF_VOF1,MMF_VOF2,MMF_ONOFF,MMF_NUM,MMF_CONS,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_FR24,MMF_BI,MMF_FR24,MMF_BI,MMF_FR24,MMF_BI,MMF_NUM,MMF_TUN2},
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_ONOFF,MMF_TUN2},
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_BI,MMF_NUM,MMF_NUM,MMF_NUM},
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM},
{MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM},
{MMF_NUM,MMF_NUM,MMF_ONOFF,MMF_NUM,MMF_NUM,MMF_NUM,MMF_NUM,MMF_TUN2},
{MMF_DWV4,MMF_DMUL,MMF_DRA,MMF_NUM,MMF_NUM,MMF_SEMI,MMF_NUM,MMF_NUM},
{MMF_DWV4,MMF_DMUL,MMF_DRA,MMF_DNHZ,MMF_NUM,MMF_BI,MMF_DCNT,MMF_DRA},
{MMF_SEMI,MMF_SEMI,MMF_NUM,MMF_SEMI,MMF_NUM,MMF_DWV5,MMF_DRD,MMF_DOPN},
{MMF_SEMI,MMF_SEMI,MMF_NUM,MMF_SEMI,MMF_NUM,MMF_DWV5,MMF_DRA,MMF_DRD}};
static const uint8_t mmf_common[63]={MMF_ATIM,MMF_AHLD,MMF_ADEC,MMF_AREL,MMF_BI,MMF_NUM,MMF_PAN,MMF_NUM,MMF_FOCT,MMF_FWID,MMF_NUM,MMF_NUM,MMF_FATK,MMF_FDEC,MMF_BI,MMF_BI,MMF_EQHZ,MMF_BI,MMF_SRR,MMF_DTIM,MMF_BI,MMF_NUM,MMF_DBHZ,MMF_NUM,MMF_BITS,MMF_NUM,MMF_NUM,MMF_NUM,MMF_SEMI,MMF_NUM,MMF_AMOD,MMF_ASPD,MMF_ARNG,MMF_GATE,MMF_TRK,MMF_DEST,MMF_LWAV,MMF_LTRG,MMF_MULT,MMF_NUM,MMF_NUM,MMF_BI,MMF_TRK,MMF_DEST,MMF_LWAV,MMF_LTRG,MMF_MULT,MMF_NUM,MMF_NUM,MMF_BI,MMF_TRK,MMF_DEST,MMF_LWAV,MMF_LTRG,MMF_MULT,MMF_NUM,MMF_NUM,MMF_BI,MMF_MODT,MMF_MHZ,MMF_NUM,MMF_NUM,MMF_WBNK};
static const uint32_t mmf_env_ms10[128] = {
    3, 11, 11, 12, 13, 14, 15, 16,
    17, 19, 20, 21, 23, 25, 26, 28,
    30, 32, 35, 37, 40, 43, 46, 49,
    53, 57, 61, 65, 70, 75, 80, 86,
    92, 98, 106, 113, 121, 130, 139, 149,
    160, 171, 184, 197, 211, 226, 243, 260,
    279, 299, 320, 343, 368, 394, 422, 453,
    485, 520, 557, 597, 640, 686, 735, 788,
    844, 905, 970, 1040, 1114, 1194, 1280, 1372,
    1470, 1576, 1689, 1810, 1940, 2079, 2229, 2389,
    2560, 2744, 2941, 3152, 3378, 3620, 3880, 4159,
    4457, 4777, 5120, 5487, 5881, 6303, 6756, 7241,
    7760, 8317, 8914, 9554, 10240, 10975, 11763, 12607,
    13512, 14482, 15521, 16635, 17829, 19109, 20480, 21950,
    23525, 25214, 27024, 28963, 31042, 33270, 35658, 38217,
    40960, 43900, 47051, 50428, 54047, 57926, 62084, 66540,
};
static const uint32_t mmf_aatk_ms10[128] = {
    9, 10, 11, 12, 13, 14, 15, 16,
    17, 19, 20, 22, 24, 26, 28, 31,
    33, 36, 39, 42, 46, 50, 54, 59,
    64, 69, 75, 81, 88, 96, 104, 113,
    122, 133, 144, 156, 169, 184, 199, 216,
    234, 254, 276, 299, 324, 352, 382, 414,
    449, 487, 529, 574, 622, 675, 732, 794,
    862, 935, 1014, 1100, 1193, 1295, 1404, 1523,
    1653, 1793, 1945, 2110, 2289, 2483, 2693, 2922,
    3170, 3438, 3730, 4046, 4389, 4762, 5165, 5604,
    6079, 6594, 7153, 7760, 8418, 9132, 9907, 10747,
    11658, 12647, 13719, 14883, 16145, 17514, 18999, 20610,
    22358, 24254, 26311, 28543, 30963, 33589, 36437, 39528,
    42880, 46516, 50461, 54740, 59382, 64418, 69881, 75807,
    82236, 89210, 96776, 104983, 113886, 123544, 134021, 145387,
    157716, 171091, 185601, 201340, 218415, 236938, 257031, 278829,
};
static const uint32_t mmf_adec_ms10[128] = {
    249, 266, 284, 304, 324, 347, 371, 396,
    423, 452, 483, 517, 552, 590, 631, 674,
    720, 770, 823, 879, 940, 1004, 1073, 1147,
    1226, 1310, 1400, 1496, 1599, 1709, 1826, 1952,
    2086, 2229, 2382, 2546, 2721, 2908, 3108, 3322,
    3550, 3794, 4054, 4333, 4631, 4949, 5289, 5653,
    6041, 6456, 6900, 7374, 7881, 8422, 9001, 9619,
    10281, 10987, 11742, 12549, 13411, 14333, 15318, 16370,
    17495, 18698, 19982, 21356, 22823, 24391, 26068, 27859,
    29773, 31819, 34006, 36343, 38840, 41509, 44362, 47410,
    50668, 54150, 57871, 61848, 66098, 70640, 75494, 80682,
    86226, 92151, 98484, 105251, 112484, 120214, 128475, 137303,
    146738, 156822, 167599, 179116, 191424, 204579, 218637, 233661,
    249718, 266878, 285218, 304817, 325764, 348150, 372074, 397642,
    424967, 454170, 485380, 518734, 554381, 592477, 633191, 676703,
    723205, 772902, 826014, 882777, 943439, 1008271, 1077557, 1151605,
};
static const uint32_t mmf_fatk_ms10[128] = {
    10, 11, 11, 12, 12, 13, 14, 15,
    16, 16, 17, 18, 19, 21, 22, 23,
    24, 26, 27, 29, 30, 32, 34, 36,
    38, 40, 42, 45, 47, 50, 53, 56,
    59, 75, 96, 123, 157, 168, 180, 192,
    206, 218, 231, 245, 260, 283, 308, 335,
    365, 390, 417, 446, 477, 515, 556, 600,
    647, 700, 757, 818, 885, 957, 1035, 1119,
    1210, 1344, 1492, 1657, 1841, 2044, 2270, 2521,
    2800, 3053, 3330, 3631, 3960, 4318, 4709, 5135,
    5600, 6093, 6630, 7213, 7849, 8540, 9292, 10110,
    11000, 11982, 13051, 14216, 15485, 16868, 18373, 20014,
    21800, 23704, 25775, 28026, 30474, 33136, 36031, 39178,
    42600, 46325, 50375, 54780, 59570, 64779, 70443, 76602,
    83300, 90508, 98340, 106849, 116095, 126140, 137055, 148914,
    161800, 175866, 191154, 207772, 225834, 245467, 266806, 290000,
};
static const uint32_t mmf_fdec_ms10[128] = {
    240, 254, 268, 284, 300, 317, 336, 355,
    375, 397, 420, 444, 470, 497, 525, 555,
    587, 621, 657, 695, 735, 777, 822, 869,
    919, 972, 1028, 1087, 1150, 1216, 1286, 1360,
    1439, 1521, 1609, 1702, 1800, 1903, 2013, 2129,
    2251, 2381, 2518, 2663, 2817, 2979, 3150, 3332,
    3524, 3726, 3941, 4168, 4408, 4662, 4930, 5214,
    5514, 5832, 6168, 6523, 6898, 7296, 7716, 8160,
    8630, 9127, 9652, 10208, 10796, 11418, 12075, 12771,
    13506, 14284, 15106, 15976, 16896, 17869, 18898, 19986,
    21137, 22354, 23641, 25002, 26442, 27965, 29575, 31278,
    33079, 34984, 36998, 39129, 41382, 43765, 46285, 48950,
    51769, 54750, 57902, 61237, 64763, 68492, 72436, 76607,
    81018, 85683, 90617, 95835, 101354, 107190, 113362, 119890,
    126793, 134095, 141816, 149982, 158619, 167752, 177412, 187628,
    198432, 209858, 221942, 234722, 248238, 262532, 277650, 293637,
};
static const uint32_t mmf_drm_ra_ms10[128] = {
    15, 19, 30, 42, 54, 66, 79, 91,
    104, 117, 131, 144, 158, 172, 186, 201,
    215, 230, 246, 261, 277, 293, 310, 327,
    344, 361, 379, 397, 416, 435, 454, 474,
    494, 514, 535, 557, 579, 601, 624, 647,
    671, 696, 721, 746, 773, 800, 827, 856,
    885, 914, 945, 976, 1008, 1041, 1075, 1110,
    1146, 1183, 1221, 1260, 1300, 1342, 1385, 1429,
    1474, 1521, 1570, 1620, 1672, 1725, 1781, 1838,
    1898, 1959, 2023, 2090, 2159, 2231, 2306, 2384,
    2465, 2550, 2639, 2731, 2828, 2930, 3036, 3148,
    3265, 3389, 3520, 3657, 3803, 3957, 4120, 4294,
    4479, 4676, 4887, 5113, 5355, 5617, 5899, 6204,
    6537, 6899, 7296, 7733, 8215, 8751, 9351, 10025,
    10789, 11662, 12670, 13846, 15235, 16902, 18940, 21487,
    24761, 29128, 35241, 44410, 59692, 90257, 181950, 181950,
};
static const uint32_t mmf_drm_rd_ms10[128] = {
    15, 122, 239, 358, 479, 602, 727, 853,
    982, 1114, 1247, 1383, 1521, 1661, 1804, 1950,
    2098, 2249, 2403, 2559, 2719, 2881, 3046, 3215,
    3387, 3562, 3741, 3924, 4110, 4300, 4493, 4691,
    4893, 5100, 5310, 5526, 5746, 5971, 6200, 6436,
    6676, 6923, 7175, 7433, 7697, 7967, 8245, 8529,
    8821, 9120, 9426, 9741, 10064, 10396, 10737, 11088,
    11448, 11819, 12200, 12593, 12997, 13414, 13843, 14286,
    14743, 15214, 15701, 16205, 16725, 17263, 17821, 18398,
    18996, 19616, 20260, 20928, 21623, 22345, 23097, 23880,
    24697, 25549, 26439, 27369, 28343, 29363, 30433, 31556,
    32737, 33980, 35290, 36673, 38135, 39683, 41325, 43070,
    44927, 46907, 49025, 51294, 53730, 56354, 59189, 62259,
    65596, 69237, 73224, 77610, 82458, 87844, 93865, 100637,
    108313, 117085, 127207, 139016, 152971, 169718, 190187, 215772,
    248668, 292529, 353935, 446043, 599556, 906583, 1827664, 1827664,
};
static const uint8_t mmf_ens_pidx[128] = {
    0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8,
    8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15, 16, 16, 16,
    17, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 25,
    25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 28, 28, 28, 28, 29, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 32,
};
static const char *const mmf_ens_pname[33] = {"-12", "-11", "-10", "-9", "-8", "-3:2", "-7", "-6", "-5", "-4:3", "-4", "-5:4", "-6:5", "-3", "-2", "-1", "OFF", "+1", "+2", "+3", "+6:5", "+5:4", "+4", "+4:3", "+5", "+6", "+7", "+3:2", "+8", "+9", "+10", "+11", "+12"};
static const char *const mmf_fmr24_name[24] = {"1/64", "1/32", "1/16", "3/32", "1/8", "5/32", "3/16", "1/4", "5/16", "3/8", "7/16", "1/2", "5/8", "3/4", "7/8", "1", "1.25", "1.5", "1.75", "2", "2.5", "3", "3.5", "4"};
static const uint16_t mmf_mod_hz10[128] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4,
    4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8,
    9, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13, 13,
    14, 14, 15, 15, 16, 16, 17, 18, 18, 19, 20, 20,
    21, 22, 23, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 34, 35, 36, 38, 39, 40, 42, 43, 45, 47, 48,
    50, 52, 54, 56, 58, 60, 62, 65, 67, 70, 72, 75,
    78, 80, 83, 86, 90, 93, 96, 100,
};
static const uint16_t mmf_dbhz[128] = {
    11, 11, 12, 13, 13, 14, 15, 16, 17, 18, 19, 20,
    22, 23, 25, 26, 28, 29, 31, 33, 35, 37, 40, 42,
    45, 48, 51, 54, 57, 61, 64, 68, 73, 77, 82, 87,
    92, 98, 104, 111, 118, 125, 133, 141, 150, 159, 169, 179,
    191, 202, 215, 228, 243, 258, 274, 291, 309, 328, 348, 370,
    393, 417, 443, 471, 500, 531, 564, 599, 636, 676, 718, 762,
    810, 860, 914, 970, 1031, 1095, 1163, 1235, 1312, 1393, 1480, 1572,
    1669, 1773, 1883, 2000, 2124, 2256, 2396, 2545, 2703, 2871, 3050, 3239,
    3440, 3654, 3881, 4122, 4378, 4651, 4939, 5246, 5572, 5918, 6286, 6677,
    7091, 7532, 8000, 8497, 9025, 9586, 10181, 10814, 11485, 12199, 12957, 13762,
    14617, 15525, 16490, 17514, 18602, 19758, 20985, 22289,
};
static const uint16_t mmf_vo_f1hz[128] = {
    199, 202, 204, 207, 210, 212, 215, 217, 220, 222, 225, 227, 230, 231, 233, 234,
    236, 237, 239, 240, 242, 245, 248, 252, 255, 257, 260, 262, 265, 267, 269, 272,
    274, 276, 279, 281, 284, 286, 288, 290, 292, 293, 295, 296, 298, 301, 304, 307,
    310, 312, 314, 317, 319, 322, 324, 327, 330, 336, 342, 349, 355, 358, 361, 365,
    368, 376, 384, 392, 400, 404, 407, 411, 415, 418, 420, 423, 426, 431, 436, 442,
    447, 452, 457, 463, 468, 472, 475, 479, 483, 488, 494, 499, 505, 511, 518, 524,
    531, 534, 537, 540, 543, 547, 551, 556, 560, 563, 566, 570, 573, 578, 583, 589,
    594, 601, 608, 615, 622, 629, 636, 644, 651, 655, 659, 664, 668, 679, 690, 702,
};
static const uint16_t mmf_vo_f2hz[128] = {
    457, 461, 464, 468, 472, 477, 482, 488, 493, 473, 453, 434, 416, 416, 416, 416,
    416, 416, 416, 417, 417, 417, 417, 418, 418, 417, 417, 416, 416, 486, 568, 664,
    776, 782, 788, 795, 801, 812, 824, 835, 847, 856, 865, 875, 884, 901, 919, 937,
    955, 965, 976, 986, 997, 1009, 1021, 1033, 1045, 1065, 1085, 1105, 1126, 1135, 1145, 1154,
    1164, 1179, 1195, 1210, 1226, 1243, 1261, 1279, 1297, 1318, 1338, 1360, 1381, 1394, 1408, 1421,
    1435, 1457, 1478, 1501, 1523, 1543, 1563, 1584, 1605, 1626, 1647, 1669, 1691, 1713, 1734, 1757,
    1779, 1799, 1819, 1839, 1859, 1890, 1921, 1953, 1985, 2013, 2042, 2071, 2100, 2123, 2147, 2171,
    2195, 2221, 2247, 2273, 2300, 2344, 2389, 2434, 2481, 2519, 2557, 2596, 2635, 2675, 2716, 2757,
};
static const char *const mmf_wave_name[32] = {"SINE", "SAW", "SQR", "TRI", "ORGN", "BELL", "GLAS", "PLST", "LTEX", "CHRM", "HOLW", "NASL", "VOXA", "VOXO", "VOXI", "BUZZ", "PULS", "FORM", "STRG", "MTL1", "MTL2", "ODD8", "EVEN", "RAMP", "STEP", "BITS", "8BIT", "SID1", "KNIF", "SPIK", "CRSH", "NOIZ"};
static void mmf_fix(char *out,int32_t value,int decimals) {
 char b[16];fmt_fix(b,value,decimals);
 if(b[0]=='.'){out[0]='0';str_cpy(out+1,b,15);}
 else if(b[0]=='-'&&b[1]=='.'){out[0]='-';out[1]='0';str_cpy(out+2,b+1,14);}
 else str_cpy(out,b,16);
}
static void mmf_hz(char *out,const char **unit,unsigned hz) {
 if(hz>=1000){mmf_fix(out,(int32_t)(hz/100),1);*unit="kHz";}
 else {fmt_int(out,(int32_t)hz);*unit="Hz";}
}
static void mmf_time(char *out,const char **unit,unsigned ms10) {
 if(ms10<100){mmf_fix(out,(int32_t)ms10,1);*unit="ms";}
 else if(ms10<10000){fmt_int(out,(int32_t)(ms10/10));*unit="ms";}
 else if(ms10<1000000){mmf_fix(out,(int32_t)(ms10/1000),1);*unit="s";}
 else {fmt_int(out,(int32_t)(ms10/10000));*unit="s";}
}
static void mmf_ratio(char *out,int r,int sq) {
 static const uint16_t fraction[]={64,128,256,384,512,576,640,768,1024,1280,1536,1792,2048,2304,2560,3072,3584};
 static const char *const name[]={"1/64","1/32","1/16","3/32","1/8","9/64","5/32","3/16","1/4","5/16","3/8","7/16","1/2","9/16","5/8","3/4","7/8"};
 for(unsigned i=0;i<17;i++)if(r==fraction[i]&&(sq?r<=1024||r==2304:r!=576)){str_cpy(out,name[i],6);return;}
 if(!r){str_cpy(out,"0.0",6);return;}
 if(r<4096){int m=r*1000/4096;out[0]='.';out[1]='0'+m/100;out[2]='0'+m/10%10;out[3]='0'+m%10;out[4]=0;return;}
 int c=r*100/4096;mmf_fix(out,c%100?c:c/10,c%100?2:1);
}
/* Mill cents retain the 3.125-cent law; round only to fit the visible width. */
static void mmf_tune(char *out,int v) {
 int mil=(v-64)*3125,dec=3,div=1;char tmp[12];
 for(;;){int q=(mil+(mil<0?-div/2:div/2))/div;mmf_fix(tmp,q,dec);
  if(str_len(tmp)<=5||!dec){str_cpy(out,tmp,6);return;}div*=10;dec--;}
}
static int mmf_format(const param_desc_t *d,int value,char *out,const char **unit) {
 int id=-1,m=-1,kind=MMF_NUM,v;unsigned j,k;
 for(j=0;j<19&&m<0;j++)for(k=0;k<8;k++)if(d==&DMODELS[MM_MODEL_FIRST+j].edit[k]){m=(int)j;id=(int)k;break;}
 if(m>=0)kind=mmf_synth[m][id];
 else for(j=8;j<71;j++)if(d==&MM_CONTROL_DESC[j-8]){id=(int)j;kind=mmf_common[j-8];break;}
 if(id<0)return 0;
 v=clamp(value+(id>=8?mm_control_bias((unsigned)id):0),0,127);*unit="";
 if(m==MM2_VOX&&(id==2||id==4||id==5||id==6)){str_cpy(out,"--",6);*unit="n/a";return 1;}
 if(d->label&&d->label[0]=='-'){str_cpy(out,"--",6);return 1;}
 static const char *const sidw[]={"TRI","SAW","PULS","MIX","NOIS"},*const sidm[]={"OFF","RING","SYNC","R+S"};
 static const char *const drumw[]={"SIN","TRI","SAW","SQR","NOIS"},*const lw[]={"TRI","SIN","SQR","SAW","EXP","RMP","RND"};
 static const char *const lt[]={"FREE","TRIG","HOLD","ONE","HALF"},*const am[]={"OFF","UP","DOWN","U/D","RND","ORDR"};
 static const char *const speed[]={"1/32","1/16","1/8","1/4"},*const mod[]={"CHOR","FLNG","PHAS"};
 unsigned bpm=(unsigned)song.g[G_BPM];if(!bpm)bpm=1;
 switch(kind) {
 case MMF_BI:fmt_int(out,v-64);break;
 case MMF_TUN2:mmf_tune(out,v);*unit="cent";break;
 case MMF_PCH:str_cpy(out,mmf_ens_pname[mmf_ens_pidx[v]],6);break;
 case MMF_FR24:str_cpy(out,mmf_fmr24_name[mm2_fr24(v)],6);*unit="ratio";break;
 case MMF_FR64:mmf_ratio(out,v==127?8192:v*64,0);*unit="ratio";break;
 case MMF_FRSQ:mmf_ratio(out,v==127?16384:v*v,1);*unit="ratio";break;
 case MMF_SIDW:str_cpy(out,sidw[v*5>>7],6);break;
 case MMF_SIDM:str_cpy(out,sidm[v*4>>7],6);break;
 case MMF_ONOFF:str_cpy(out,v>=64?"ON":"OFF",6);break;
 case MMF_MSRC:str_cpy(out,v>=64?"PREV":"MFRQ",6);break;
 case MMF_MFRQ:{int note=48+(v*3+4)/8;str_cpy(out,N_NOTE[note%12],6);fmt_int(out+str_len(out),note/12-1);break;}
 case MMF_SEMI:fmt_int(out,v-64);*unit="semi";break;
 case MMF_TIME:case MMF_DECT:case MMF_ATIM:case MMF_AHLD:case MMF_ADEC:case MMF_AREL:case MMF_FATK:case MMF_FDEC:
  if(v==127&&(kind==MMF_DECT||kind==MMF_ADEC||kind==MMF_FDEC)){str_cpy(out,"INF",6);break;}
  if(v==127&&kind==MMF_AHLD){str_cpy(out,"GATE",6);break;}
  mmf_time(out,unit,kind==MMF_ATIM?mmf_aatk_ms10[v]:kind==MMF_AHLD?(unsigned)v*18750u/bpm:
   kind==MMF_ADEC||kind==MMF_AREL?mmf_adec_ms10[v]:kind==MMF_FATK?mmf_fatk_ms10[v]:kind==MMF_FDEC?mmf_fdec_ms10[v]:mmf_env_ms10[v]);break;
 case MMF_DRA:case MMF_DRD:case MMF_DOPN:
  if(kind==MMF_DOPN&&!v){str_cpy(out,"OFF",6);break;}
  if(kind!=MMF_DRA&&v==127){str_cpy(out,"INF",6);break;}
  mmf_time(out,unit,kind==MMF_DRA?mmf_drm_ra_ms10[v]:mmf_drm_rd_ms10[v]);break;
 case MMF_DWV4:case MMF_DWV5:str_cpy(out,drumw[v*(kind==MMF_DWV4?4:5)>>7],6);break;
 case MMF_DMUL:mmf_fix(out,(256+mm2_t_drm_pamt[v])*10/256,1);*unit="pitch";break;
 case MMF_DNHZ:mmf_hz(out,unit,22050*(v|1)/127);break;
 case MMF_DCNT:if(!(v*16>>7))str_cpy(out,"OFF",6);else{fmt_int(out,v*16>>7);*unit="hits";}break;
 case MMF_VOF1:fmt_int(out,mmf_vo_f1hz[v]);*unit="Hz F1";break;
 case MMF_VOF2:fmt_int(out,mmf_vo_f2hz[v]);*unit="Hz F2";break;
 case MMF_CONS:str_cpy(out,"--",6);*unit="n/a";break;
 case MMF_WAVE:if(TSEL->p[P_WBNK]>=64){out[0]='O';out[1]='S';out[2]='0'+(v/2+1)/10;out[3]='0'+(v/2+1)%10;out[4]=0;}else str_cpy(out,mmf_wave_name[v/4],6);break;
 case MMF_WBNK:str_cpy(out,v>=64?"M64":"M2K",6);break;
 case MMF_FOCT:mmf_fix(out,(v-16)*10/8,1);*unit="oct";break;
 case MMF_FWID:mmf_fix(out,v*10/8,1);*unit="oct";break;
 case MMF_EQHZ:mmf_hz(out,unit,mmc_eqhz[v]);break;
 case MMF_DBHZ:mmf_hz(out,unit,mmf_dbhz[v]);break;
 case MMF_SRR:if(!v)str_cpy(out,"OFF",6);else mmf_hz(out,unit,mmc_srrhz[v]);break;
 case MMF_PAN:if(v==64)str_cpy(out,"C",6);else{out[0]=v<64?'L':'R';fmt_int(out+1,v<64?64-v:v-64);}break;
 case MMF_DTIM:if(!v)str_cpy(out,"COMB",6);else{int den=256,num=v;while(!(num&1)&&den>1){num>>=1;den>>=1;}
  fmt_int(out,num);if(den>=128)*unit=den==256?"/256":"/128";else{unsigned n=str_len(out);out[n]='/';fmt_int(out+n+1,den);*unit="note";}}break;
 case MMF_BITS:fmt_int(out,16-(v*14>>7));*unit="bit";break;
 case MMF_TRK:out[0]='T';fmt_int(out+1,(v*6>>7)+1);break;
 case MMF_DEST:
  if(!v||v>71)str_cpy(out,v?"--":"OFF",6);
  else{unsigned src=(unsigned)(id-42)/8,track=(unsigned)clamp(TSEL->p[MM_CONTROL_ID[42+src*8]],0,127)*6>>7;
   str_cpy(out,track_desc(&trk[track],MM_CONTROL_ID[v-1])->label,6);}break;
 case MMF_LWAV:str_cpy(out,lw[v*7>>7],6);break;
 case MMF_LTRG:str_cpy(out,lt[v*5>>7],6);break;
 case MMF_MULT:out[0]='X';fmt_int(out+1,1<<(v*8>>7));break;
 case MMF_AMOD:str_cpy(out,am[v*6>>7],6);break;
 case MMF_ASPD:str_cpy(out,speed[v*4>>7],6);*unit="note";break;
 case MMF_ARNG:fmt_int(out,1+(v*4>>7));*unit="oct";break;
 case MMF_GATE:mmf_fix(out,v*10/16,1);*unit="steps";break;
 case MMF_MODT:str_cpy(out,mod[v*3>>7],6);break;
 case MMF_MHZ:mmf_fix(out,mmf_mod_hz10[v],1);*unit="Hz";break;
 default:
  if(id>=42&&id<66&&(id-42)%8==6){mmf_fix(out,v*3600/128,1);*unit="deg";}
  else fmt_int(out,v);
  break;
 }
 return 1;
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    if (mmf_format(d,v,val,unit)) return;
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
    case F_ASSIGN:
        str_cpy(val,v>0&&v<=P_COUNT?track_desc(TSEL,(unsigned)v-1)->label:"---",6);
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
