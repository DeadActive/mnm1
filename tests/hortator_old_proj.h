/* SPDX-License-Identifier: GPL-3.0-only
 * MNM-1 tests: Hortator's project records (FDR1..FDRD), as its project.c laid them out. MNM-1 reads none of them
 * (project.c "MNM1"); the boot tests still write them to flash: a device updated from Hortator must boot. */
#define PROJ_MAGIC_VE 0x45524446u              /* "FDRE": Hortator's last */
#define PROJ_MAGIC_VD 0x44524446u              /* "FDRD": each track's preset and edited mark; converted on load */
#define PROJ_MAGIC_VC 0x43524446u              /* "FDRC": PRIO per track, no presets; converted on load */
#define PROJ_MAGIC_VB 0x42524446u              /* "FDRB": the step's note, SLIDE and TIE (ACID); converted on load */
#define PROJ_MAGIC_VA 0x41524446u              /* "FDRA": the FILTER, the old steps; converted on load (same size) */
#define PROJ_MAGIC_V9 0x39524446u              /* "FDR9": the song and the name, converted on load */
#define PROJ_MAGIC_V8 0x38524446u              /* "FDR8": the motion, converted on load */
#define PROJ_MAGIC_V7 0x37524446u              /* "FDR7": the reverb TYPE (sound pack), converted on load */
#define PROJ_MAGIC_V6 0x36524446u              /* "FDR6": GHOST projects, converted on load */
#define PROJ_MAGIC_V5 0x35524446u              /* "FDR5": RESON projects, converted on load */
#define PROJ_MAGIC_V4 0x34524446u              /* "FDR4": LFO projects, converted on load */
#define PROJ_MAGIC_V3 0x33524446u              /* "FDR3": M3 projects, converted on load */
#define PROJ_MAGIC_V2 0x32524446u              /* "FDR2": M2 projects, converted on load */
#define PROJ_MAGIC_V1 0x31524446u              /* "FDR1": M1 projects, converted on load */
/* the motion store as Hortator saved it (128 events; MNM-1 0.5's holds 240) */
typedef struct { uint8_t count, on, rsv[2]; motion_event_t ev[128]; } motion_store_v_t;
typedef struct { uint8_t on, acc, cond, rat; } step_v9_t;   /* a step as FDR2..FDRA stored it */
/* FDRA: everything, the steps as FDR2..FDR9 (same size as FDRB: told by the marker) */
typedef struct {
    int16_t p[P_PRIO];
    step_v9_t step[NSTEP];
} proj_trk_va_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_va_t t[8];
    motion_store_v_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_va_t;
typedef char project_va_size[sizeof(project_va_t) == 3672u ? 1 : -1];
/* FDRB (0.16.0): everything but PRIO, 3672 B (FDRA's size: told by the marker) */
typedef struct {
    int16_t p[P_PRIO];
    step_t step[NSTEP];
} proj_trk_vb_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_vb_t t[8];
    motion_store_v_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_vb_t;
typedef char project_vb_size[sizeof(project_vb_t) == 3672u ? 1 : -1];
/* a track as FDRC and FDRD stored it: the parameters up to PRIO (no DEST slots 3 / 4, no slot sources) */
typedef struct {
    int16_t p[P_PRIO + 1];
    step_t step[NSTEP];
} proj_trk_vd_t;
/* FDRC (0.17.0 .. 0.20.0): everything but the presets, 3688 B; FDRD's fields up to the name at the same offsets */
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_vd_t t[8];
    motion_store_v_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_vc_t;
/* FDRD (0.21.0 .. 0.22.0): everything but the DEST slots' new parameters, 3712 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_vd_t t[8];
    motion_store_v_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint16_t pre[8];
    uint8_t pre_ed[8];
    uint32_t sum;
} project_vd_t;
typedef char project_vc_size[sizeof(project_vc_t) == 3688u && sizeof(project_vd_t) == 3712u &&
                             __builtin_offsetof(project_vc_t, name) == __builtin_offsetof(project_vd_t, name) ? 1 : -1];

typedef struct { uint8_t on, acc; } step_v1_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_GMODE];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_SRC]; step_v1_t step[NSTEP]; } t[8];
    uint32_t sum;
} project_v1_t;
/* M2's format: the globals before G_CSRC, the parameters before P_DUCK */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CSRC];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_DUCK]; step_v9_t step[NSTEP]; } t[8];
    uint32_t sum;
} project_v2_t;
/* M3's format: the globals before G_CGHOST, the parameters before P_LFO1 */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_LFO1]; step_v9_t step[NSTEP]; } t[8];
    uint32_t sum;
} project_v3_t;
/* the LFO format: the globals before G_CGHOST, the parameters before P_RMODEL */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[48];   /* (Hortator's P_RMODEL) */ step_v9_t step[NSTEP]; } t[8];
    uint32_t sum;
} project_v4_t;
/* a track as FDR5..FDR9 stored it: the parameters before P_FTYPE (no FILTER) */
typedef struct {
    int16_t p[P_FLPQ];          /* (Hortator's P_FTYPE: the same id) */
    step_v9_t step[NSTEP];
} proj_trk_v9_t;
/* RESON's format: the globals before G_CGHOST (the parameters before P_FTYPE) */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[8];
    uint32_t sum;
} project_v5_t;
/* GHOST's format: the globals before G_RTYPE (the parameters as now). 3024 B, as an FDR5 record: the version is
 * told by the marker, not the size */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_RTYPE];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[8];
    uint32_t sum;
} project_v6_t;
/* the sound pack's format: everything but the motion. 3028 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[8];
    uint32_t sum;
} project_v7_t;
/* motion's format: everything but the song and the name. 3544 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[8];
    motion_store_v_t motion;
    uint32_t sum;
} project_v8_t;
/* FDR9: everything but the FILTER. 3592 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[41];   /* frozen original global count */
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[8];
    motion_store_v_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_v9_t;
static union {
    project_v1_t v1; project_v2_t v2; project_v3_t v3; project_v4_t v4; project_v5_t v5; project_v6_t v6; project_v7_t v7;
    project_v8_t v8; project_v9_t v9; project_va_t va; project_vb_t vb; project_vc_t vc; project_vd_t vd;
} proj_old;

