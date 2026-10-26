/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* H1 (spec Hortator spec 2026-10-05-device-readiness-design.md §3): the drum firmware's boot path,
 * main.c's order, with our real code, against hostile flash and .noinit contents, built with ASan + UBSan.
 * H5 in the boot: the same cases with SEQ held. A sanitizer report aborts the run (nonzero exit). */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#include "hortator_old_proj.h"                 /* MNM-1: Hortator's records, written to flash (never read) */

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static void rfill(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = (uint8_t)(rnd() >> 24);
}

/* a sample slot k with a valid header and random (or plausible) zones */
static void slot_header(uint32_t k, int plausible)
{
    smp_user_hdr_t h;
    uint32_t i;
    rfill(&h, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)(1u + rnd() % 16u);
    h.data_len = plausible ? 0x8000u : rnd();
    for (i = 0; plausible && i < h.nz; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = 0;
        z->n = 1000u + rnd() % 30000u;
        z->idx = 0;
        z->rate = 1u << 16;
        z->ls = 0;
        z->le = z->n - 1u;
        z->lo = 0;
        z->hi = 127;
    }
    memcpy(hflash + 0xA0000u + k * 0x14000u, &h, sizeof h);
}

enum { F_ERASED, F_ZERO, F_RANDOM, F_HEADERS, F_FELUCCA, F_KINDS };
static const char *const KIND[F_KINDS] = {"erased", "zeros", "random", "valid headers, garbage fields", "felucca records"};

static void flash_image(int kind, uint32_t seed)
{
    rs = seed * 2654435761u + 7u;
    memset(hflash, kind == F_ZERO ? 0x00 : 0xFF, sizeof hflash);
    if (kind == F_RANDOM)
        rfill(hflash + 0x93000u, sizeof hflash - 0x93000u);   /* the data area (the app area is ours) */
    if (kind == F_HEADERS || kind == F_FELUCCA) {
        persist_t p;
        project_t pr;
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            slot_header(k, kind == F_FELUCCA);
        rfill(&p, sizeof p);
        p.magic = PERSIST_MAGIC;
        if (kind == F_FELUCCA)
            p.panel = PANEL_DEFAULT;
        else
            p.panel.magic = PANEL_MAGIC;              /* a valid panel magic over random ids */
        st_save(OBJ_SETTINGS, &p, sizeof p);
        for (k = 0; k < 4; k++) {
            if (kind == F_HEADERS && k == 1u) {      /* an M1 project ("FDR1"): valid sum over random fields */
                project_v1_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V1;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            if (kind == F_HEADERS && k == 2u) {      /* an M3 project ("FDR3") */
                project_v3_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V3;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            if (kind == F_HEADERS && k == 3u) {      /* an M2 project ("FDR2"): valid sum over random fields */
                project_v2_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V2;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            rfill(&pr, sizeof pr);
            pr.magic = kind == F_FELUCCA ? 0x334E5546u : PROJ_MAGIC;   /* Felucca's "FUN3", or ours */
            pr.size = sizeof pr;
            pr.sum = proj_sum(&pr);                   /* ours: valid sum over random tracks */
            st_save(OBJ_PROJECT0 + k, &pr, sizeof pr);
        }
    }
}

static int32_t L[CTL], R[CTL];

/* main.c's order (fm1_main): persist_boot, settings_init, (LCD), input / ADC init, panel_init,
 * felucca_init = drum_boot_init, audio, USB, IRQs; then the main loop (UI + the update service) */
static int boot(int kind, uint32_t seed, int seq)
{
    uint32_t f, i, bounded = 1, peak = 0;
    flash_image(kind, seed);
    rfill(&settings, sizeof settings);               /* .noinit RAM after the stock firmware: anything */
    rfill(proj_slot, sizeof proj_slot);
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    if (kind == F_FELUCCA && !usr_nz[0])               /* the glue reads the image: valid slots are found */
        return 0;
    settings_init();
    panel_init();
    fm1_in.buttons = seq ? 1u << panel.btn[B_SEQ] : 0u;
    drum_boot_init();
    fm1_in.buttons = 0;
    for (f = 0; f < 2u * FS; f += CTL) {             /* 2 s of the audio ISR */
        render_mix(L, R, CTL);
        for (i = 0; i < CTL; i++) {
            bounded &= L[i] <= 32767 && L[i] >= -32768 && R[i] <= 32767 && R[i] >= -32768;
            peak |= (uint32_t)abs(L[i]) | (uint32_t)abs(R[i]);
        }
    }
    for (i = 0; i < 30; i++)                         /* the main loop's first UI frames */
        ui_frame();
    for (i = 0; !seq && i < 4; i++) {                /* the first hands-on minutes: load each slot, play */
        project_load(i);
        transport_req = 1;
        render_mix(0, 0, CTL * 64);
        ui_frame();
    }
    return bounded && settings.zoom <= 1u && settings.mutebar <= 1u && (seq ? safe_start && peak == 0 : !safe_start);
}

/* MUTE NEXT BAR (TRACKS) survives a power cycle beside ZOOM, in Felucca's settings format (zoom's bit 1) */
static int mutebar_persists(void)
{
    int ok = 1;
    uint32_t z, m;
    for (z = 0; z < 2u; z++)
        for (m = 0; m < 2u; m++) {
            memset(hflash, 0xFF, sizeof hflash);
            host_init();
            persist_boot();                          /* finds the flash */
            settings.magic = SETTINGS_MAGIC;
            settings.palette = THEME_TAG | 1u;
            settings.lowcut = 0;
            settings.zoom = z;
            settings.mutebar = m;
            settings.accel = (z ^ m) & 1u;
            settings_save();
            rfill(&settings, sizeof settings);       /* power off: .noinit is anything */
            persist_boot();
            settings_init();
            ok &= settings.zoom == z && settings.mutebar == m && settings.accel == ((z ^ m) & 1u);
        }
    return ok;
}

/* USB LEVEL (MENU): FIXED survives a power cycle in bit 3 of the stored zoom word; MASTER when it is clear (a 0.7.0
 * save); the other settings read back unchanged */
static int usb_level_persists(void)
{
    int ok = 1;
    uint32_t u;
    for (u = 0; u < 2u; u++) {
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 2u;
        settings.lowcut = 2;
        settings.zoom = 1;
        settings.mutebar = 1;
        settings.accel = 0;
        settings.usbfix = u;
        settings.perfpage = u;
        settings_save();
        rfill(&settings, sizeof settings);           /* power off: .noinit is anything */
        persist_boot();
        settings_init();
        ok &= settings.usbfix == u && settings.perfpage == u && fx_usb_fixed == u && settings.palette == (THEME_TAG | 2u) && settings.lowcut == 2 &&
              settings.zoom == 1 && settings.mutebar == 1 && settings.accel == 0;
    }
    return ok;
}

/* a settings record longer than ours (another firmware's, same marker): the defaults, not a half-read record */
static int settings_oversize_refused(void)
{
    struct {
        persist_t p;
        uint8_t more[16];
    } r;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();                                  /* finds the flash */
    memset(&r, 0, sizeof r);
    r.p.magic = PERSIST_MAGIC;
    r.p.palette = 2;                                 /* an old id (CYAN), a record that is rejected */
    r.p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &r, sizeof r);
    memset(&settings, 0, sizeof settings);           /* power off */
    persist_boot();
    settings_init();
    return settings.palette == (THEME_TAG | 0u);   /* the default: MD RED */
}

/* spec 2026-10-10 §8: an older record (no Bluetooth bits) loads exactly as before: SPEAKER EQ BASS+ stays, no device,
 * MODE AUTO */
static int settings_old_record(void)
{
    persist_t p;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();                                  /* finds the flash */
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = 2;
    p.lowcut = 2;                                    /* BASS+ */
    p.zoom = 1u | 1u << 1 | 1u << 3 | 1u << 4;       /* ZOOM, NEXT BAR, USB FIXED, PERFORM PAGE (bits 0..4) */
    p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &p, sizeof p);
    rfill(&settings, sizeof settings);               /* power off: .noinit is anything */
    rfill(&btset, sizeof btset);                     /* (the load replaces whatever was there) */
    rfill(&lastp, sizeof lastp);
    persist_boot();
    settings_init();
    return lastp.set == 0u && settings.lowcut == 2u && fx_lowcut == 2u && settings.zoom == 1u && settings.mutebar == 1u && settings.accel == 1u &&
           settings.usbfix == 1u && settings.perfpage == 1u && settings.palette == (THEME_TAG | 2u) && btset.btmode == 0u &&
           btset.bt_type == 0u && btset.bt_lo == 0u && btset.bt_hi == 0u;
}

/* the remembered device (address, type) and MODE survive a save and a load in zoom's and lowcut's spare bits; lowcut
 * and the zoom flags keep their values */
static int settings_bt_round_trip(void)
{
    uint32_t m, ok = 1;
    for (m = 0; m < 3u; m++) {
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 3u;
        settings.lowcut = m;                         /* FLAT LOWCUT BASS+ */
        settings.zoom = 1;
        settings.mutebar = 0;
        settings.accel = 1;
        settings.usbfix = 1;
        settings.perfpage = 0;
        btset.btmode = m;                            /* AUTO SMC MIDI */
        btset.bt_type = m & 1u;
        btset.bt_lo = 0xA1B2C3u + m;                 /* address bytes 0..2 */
        btset.bt_hi = 0xFFEEDDu - m;                 /* .. 3..5 */
        settings_save();
        st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= p.zoom >> 8 == 0xA1B2C3u + m && p.lowcut >> 8 == 0xFFEEDDu - m && (p.lowcut & 0xFFu) == m &&
              ((p.zoom >> 5) & 3u) == m && ((p.zoom >> 7) & 1u) == (m & 1u) && (p.zoom & 0x1Fu) == (1u | 1u << 3);
        rfill(&settings, sizeof settings);           /* power off */
        memset(&btset, 0, sizeof btset);             /* (.bss) */
        persist_boot();
        settings_init();
        ok &= btset.btmode == m && btset.bt_type == (m & 1u) && btset.bt_lo == 0xA1B2C3u + m &&
              btset.bt_hi == 0xFFEEDDu - m && settings.lowcut == m && fx_lowcut == m && settings.zoom == 1u &&
              settings.mutebar == 0u && settings.accel == 1u && settings.usbfix == 1u && settings.perfpage == 0u &&
              settings.palette == (THEME_TAG | 3u);
    }
    return (int)ok;
}

/* an update keeps .noinit where it was: `settings` stays Felucca's 32 B (the RAM project slots after it are found
 * again); the Bluetooth fields are outside it, from the flash record only: none -> no device, MODE AUTO, the .noinit
 * settings kept */
static int settings_bt_after_update(void)
{
    memset(hflash, 0xFF, sizeof hflash);             /* no settings record */
    host_init();
    settings.magic = SETTINGS_MAGIC;
    settings.palette = THEME_TAG | 2u;
    settings.lowcut = 1;
    settings.zoom = 1;
    settings.mutebar = 1;
    settings.accel = 1;
    settings.usbfix = 0;
    settings.perfpage = 0;
    rfill(&btset, sizeof btset);                     /* (anything before: a record-less boot clears it) */
    persist_boot();
    settings_init();
    return sizeof settings == 32u && btset.btmode == 0u && btset.bt_type == 0u && btset.bt_lo == 0u &&
           btset.bt_hi == 0u && settings.palette == (THEME_TAG | 2u) && settings.lowcut == 1u && settings.zoom == 1u &&
           settings.mutebar == 1u;
}

/* a record whose MODE bits read 3 (no such MODE): AUTO; the address kept */
static int settings_bt_mode_range(void)
{
    persist_t p;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = 2;
    p.zoom = 3u << 5 | 1u << 7 | 0x123456u << 8;
    p.lowcut = 0x654321u << 8;
    p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &p, sizeof p);
    persist_boot();
    settings_init();
    return btset.btmode == 0u && btset.bt_type == 1u && btset.bt_lo == 0x123456u && btset.bt_hi == 0x654321u &&
           settings.lowcut == 0u;
}

/* spec 2026-10-11-fixes §9: METRO in lowcut's bits 5..6 survives a power cycle beside SPEAKER EQ (bits 0..1) and the
 * device's address bytes 3..5 (bits 8..31); a record without them (0.16.0's) and no record at all load OFF */
static int settings_metro_round_trip(void)
{
    uint32_t m, ok = 1;
    for (m = 0; m < 4u; m++) {
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 3u;
        settings.lowcut = m % 3u;
        settings.zoom = 1;
        btset.btmode = 1;
        btset.bt_lo = 0x112233u;
        btset.bt_hi = 0xFFEEDDu;
        fx_metro = (uint8_t)m;
        settings_save();
        st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= ((p.lowcut >> 5) & 3u) == m && (p.lowcut & 3u) == m % 3u && p.lowcut >> 8 == 0xFFEEDDu &&
              !(p.lowcut & 0x9Cu);
        rfill(&settings, sizeof settings);           /* power off */
        memset(&btset, 0, sizeof btset);             /* (.bss) */
        fx_metro = 0;
        persist_boot();
        settings_init();
        ok &= fx_metro == m && settings.lowcut == m % 3u && fx_lowcut == m % 3u && btset.bt_hi == 0xFFEEDDu &&
              btset.btmode == 1u && settings.palette == (THEME_TAG | 3u) && settings.zoom == 1u;
    }
    {                                                /* 0.16.0's record: BASS+, no METRO bits */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        memset(&p, 0, sizeof p);
        p.magic = PERSIST_MAGIC;
        p.palette = 2;
        p.lowcut = 2u | 0x654321u << 8;
        p.panel = PANEL_DEFAULT;
        st_save(OBJ_SETTINGS, &p, sizeof p);
        fx_metro = 3;
        persist_boot();
        settings_init();
        ok &= fx_metro == 0u && settings.lowcut == 2u && btset.bt_hi == 0x654321u;
    }
    memset(hflash, 0xFF, sizeof hflash);             /* no record (an update from the stock firmware) */
    host_init();
    fx_metro = 2;
    persist_boot();
    settings_init();
    ok &= fx_metro == 0u;
    return (int)ok;
}

/* spec 2026-10-10 §2 amendment 2026-10-12: AUTOSTART (BLE build) in the palette word's bit 8, round-tripped by both
 * builds as the device is; the palette itself in bits 0..7 (kept, not reset by settings_init); an older record and
 * none load OFF */
static int settings_autostart_round_trip(void)
{
    uint32_t a, ok = 1;
    for (a = 0; a < 2u; a++) {
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 3u;
        settings.lowcut = 2;
        settings.zoom = 1;
        btset.btmode = 2;
        btset.bt_lo = 0x123456u;
        btset.bt_hi = 0xABCDEFu;
        btset.bt_auto = a;
        backlight = 1;
        settings_save();
        st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= p.palette == (THEME_TAG | 3u | a << 8 | 1u << 9) &&   /* (bit 9: BACKLIGHT's level DIM LO, item 9) */
              (p.lowcut & 0xFFu) == (2u | 1u << 7) && p.lowcut >> 8 == 0xABCDEFu && p.zoom >> 8 == 0x123456u &&
              ((p.zoom >> 5) & 3u) == 2u;
        rfill(&settings, sizeof settings);           /* power off */
        memset(&btset, 0, sizeof btset);             /* (.bss) */
        btset.bt_auto = !a;
        backlight = 0;
        persist_boot();
        settings_init();
        ok &= btset.bt_auto == a && settings.palette == (THEME_TAG | 3u) && settings.lowcut == 2u && settings.zoom == 1u &&
              btset.btmode == 2u && btset.bt_lo == 0x123456u && btset.bt_hi == 0xABCDEFu && backlight == 1u;
    }
    {                                                /* an older record: palette 2, no bit 8 */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        memset(&p, 0, sizeof p);
        p.magic = PERSIST_MAGIC;
        p.palette = 2;
        p.panel = PANEL_DEFAULT;
        st_save(OBJ_SETTINGS, &p, sizeof p);
        btset.bt_auto = 1;
        persist_boot();
        settings_init();
        ok &= btset.bt_auto == 0u && settings.palette == (THEME_TAG | 2u);
    }
    {                                                /* spec 2026-10-19-md-ui §2: an old palette id (MONO) with AUTOSTART */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        memset(&p, 0, sizeof p);
        p.magic = PERSIST_MAGIC;
        p.palette = 4u | 1u << 8;
        p.panel = PANEL_DEFAULT;
        st_save(OBJ_SETTINGS, &p, sizeof p);
        persist_boot();
        settings_init();
        ok &= btset.bt_auto == 1u && settings.palette == (THEME_TAG | 7u) && C_BG == THEMES[7].bg;   /* RED ON BLACK
                                                                                                * (MNM-1: index 7) */
    }
    memset(hflash, 0xFF, sizeof hflash);             /* no record */
    host_init();
    btset.bt_auto = 1;
    persist_boot();
    settings_init();
    ok &= btset.bt_auto == 0u;
    return (int)ok;
}

/* spec 2026-10-11-fixes §12: BACKLIGHT in lowcut's bit 7 survives a power cycle beside SPEAKER EQ, the last project,
 * METRO and the device's address bytes 3..5; a record without it (0.16.0's, this branch's before it) and no record at
 * all load OFF */
static int settings_backlight_round_trip(void)
{
    uint32_t b, ok = 1;
    for (b = 0; b < 2u; b++) {
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.lowcut = 2;
        fx_metro = 3;
        lastp.set = 1;
        lastp.slot = 2;
        btset.bt_hi = 0xABCDEFu;
        backlight = b;
        settings_save();
        st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= ((p.lowcut >> 7) & 1u) == b && (p.lowcut & 0x7Fu) == (2u | 1u << 2 | 2u << 3 | 3u << 5) &&
              p.lowcut >> 8 == 0xABCDEFu;
        rfill(&settings, sizeof settings);           /* power off */
        memset(&btset, 0, sizeof btset);             /* (.bss) */
        backlight = !b;
        fx_metro = 0;
        persist_boot();
        settings_init();
        ok &= backlight == b && settings.lowcut == 2u && fx_metro == 3u && lastp.set == 1u && lastp.slot == 2u &&
              btset.bt_hi == 0xABCDEFu;
    }
    {                                                /* an older record: BASS+, METRO MID, no bit 7 */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        memset(&p, 0, sizeof p);
        p.magic = PERSIST_MAGIC;
        p.lowcut = 2u | 2u << 5 | 0x654321u << 8;
        p.panel = PANEL_DEFAULT;
        st_save(OBJ_SETTINGS, &p, sizeof p);
        backlight = 1;
        persist_boot();
        settings_init();
        ok &= backlight == 0u && settings.lowcut == 2u && fx_metro == 2u;
    }
    memset(hflash, 0xFF, sizeof hflash);             /* no record (an update from the stock firmware) */
    host_init();
    backlight = 1;
    persist_boot();
    settings_init();
    ok &= backlight == 0u;
    for (b = 0; b < BL_COUNT; b++) {                 /* fixes 2026-10-21 item 9: every level survives, in the palette
                                                      * word's bits 9..11, lowcut's bit 7 "not OFF" for an older one */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = THEME_TAG | 2u;
        btset.bt_auto = 1;
        backlight = b;
        settings_save();
        st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= ((p.lowcut >> 7) & 1u) == (b != 0u) && ((p.palette >> 9) & 7u) == b && (p.palette & 0x1FFu) ==
              ((THEME_TAG | 2u) & 0xFFu) + 0x100u;
        backlight = 0;
        btset.bt_auto = 0;
        persist_boot();
        settings_init();
        ok &= backlight == b && btset.bt_auto == 1u && theme_index(settings.palette) == 2u;
    }
    {                                                /* an older Hortator's record: ON (bit 7), no level: DIM LO */
        persist_t p;
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        memset(&p, 0, sizeof p);
        p.magic = PERSIST_MAGIC;
        p.lowcut = 1u << 7;
        p.panel = PANEL_DEFAULT;
        st_save(OBJ_SETTINGS, &p, sizeof p);
        persist_boot();
        settings_init();
        ok &= backlight == BL_DIM_LO;
    }
    return (int)ok;
}

/* spec 2026-10-10 §4: a project saved with CLK BLE (3) loads as CLK INT in a build whose CLK ends at TRS (2), not as
 * TRS; a value inside the range is kept */
static int clock_beyond_range_is_int(void)
{
    project_t *p = &proj_slot[0];
    uint32_t ok;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    project_save(0);
    p->g[G_CLOCK] = 3;
    p->sum = proj_sum(p);
    song.g[G_CLOCK] = 1;
    project_load(0);
    ok = GP[G_CLOCK].max == 2 && song.g[G_CLOCK] == 0;
    p->g[G_CLOCK] = 2;
    p->sum = proj_sum(p);
    project_load(0);
    ok &= song.g[G_CLOCK] == 2;
    p->g[G_CLOCK] = -1;
    p->sum = proj_sum(p);
    project_load(0);
    return (int)(ok & (song.g[G_CLOCK] == 0));
}

/* FDR7 keeps TYPE through a save and a load */
static int fdr7_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    song.g[G_RTYPE] = 1;
    project_save(2);
    song.g[G_RTYPE] = 0;
    memset(proj_slot, 0, sizeof proj_slot);
    project_load(2);
    return song.g[G_RTYPE] == 1;
}
/* FDR8: the motion goes through a save and a load (flash) */
static int fdr8_motion_round_trip(void)
{
    motion_store_t want;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    motion_add(0, 4, P_E1, 100);
    motion_add(3, 63, P_PAN, -64);                   /* (MNM-1: tracks 1 .. 6) */
    motion_add(5, 0, P_LFO2 + LF_DPTH, 127);
    mo.s.on = 0x29u;
    want = mo.s;
    project_save(2);
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    project_load(2);
    return !memcmp(&mo.s, &want, sizeof want);
}
/* a broken motion block: the project loads, the motion dropped */
static int fdr8_bad_motion_dropped(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[3].step[5].on = 1;
    motion_add(0, 4, P_E1, 100);
    project_save(0);
    p->motion.ev[0].trk = 9;                         /* out of range */
    p->sum = proj_sum(p);
    memset(&mo, 0, sizeof mo);
    trk[3].step[5].on = 0;
    project_load(0);
    return trk[3].step[5].on && mo.s.count == 0u;
}
/* Review Focus 3: a load while motion plays: the stop after it puts nothing of the old project back */
static int fdr8_load_while_playing(void)
{
    uint32_t b;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[0].p[P_E1] = 11;
    project_save(1);                                 /* the project: E1 11 */
    trk[0].p[P_E1] = 64;
    motion_add(0, 0, P_E1, 100);
    transport_req = 1;
    for (b = 0; b < 8u; b++)
        render_mix(L, R, CTL);                       /* motion plays: E1 100, the base 64 */
    project_load(1);
    for (b = 0; b < 8u; b++)
        render_mix(L, R, CTL);                       /* the stop the load asked for */
    return !song.playing && trk[0].p[P_E1] == 11;
}
/* the song and the name go through a save and a load (flash; FDRA) */
static int fdr9_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    chain.cfg.count = 2;
    chain.cfg.loop = 1;
    chain.cfg.row[0] = (chain_row_t){1, 4};
    chain.cfg.row[1] = (chain_row_t){3, 16};
    name_set(chain.name, "BREAK 2");
    project_save(2);
    memset(&chain, 0, sizeof chain);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(2);
    return chain.cfg.count == 2u && chain.cfg.loop == 1u && chain.cfg.row[0].slot == 1u &&
           chain.cfg.row[0].repeat == 4u && chain.cfg.row[1].slot == 3u && chain.cfg.row[1].repeat == 16u &&
           str_eq(chain.name, "BREAK 2") && chain.from == 3u && sizeof(project_t) <= 3840u;   /* (a sector) */
}
/* FDRA: the five filter knobs through a save and a load (flash) */
static int fdra_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[5].p[P_FLPQ] = 77;                           /* (MNM-1: LPQ, WOFS) */
    trk[5].p[P_FWOFS] = -21;
    trk[5].p[P_FCUT] = 33;
    trk[5].p[P_FRESO] = 101;
    trk[5].p[P_FENV] = -17;
    trk[5].p[P_FDEC] = 90;
    project_save(3);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(3);
    return trk[5].p[P_FLPQ] == 77 && trk[5].p[P_FWOFS] == -21 && trk[5].p[P_FCUT] == 33 && trk[5].p[P_FRESO] == 101 &&
           trk[5].p[P_FENV] == -17 && trk[5].p[P_FDEC] == 90;
}
/* Review Focus 4: out-of-range filter values in a stored project: clamped on load */
static int fdra_clamps(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    project_save(0);
    p->t[4].p[P_FLPQ] = 300;
    p->t[4].p[P_FCUT] = 300;
    p->t[4].p[P_FENV] = -200;
    p->sum = proj_sum(p);
    project_load(0);
    return trk[4].p[P_FLPQ] == 127 && trk[4].p[P_FCUT] == 127 && trk[4].p[P_FENV] == -64;
}
/* FDRB (ACID): the step's bytes: byte 0 = on | acc << 1 | slide << 2 | tie << 3 | rat << 4, byte 1 cond, byte 2 nt */
static int fdrb_step_layout(void)
{
    step_t s;
    const uint8_t *b = (const uint8_t *)&s;
    memset(&s, 0, sizeof s);
    s.on = 1;
    s.tie = 1;
    s.rat = 2;
    s.cond = 5;
    s.nt = -3;
    return sizeof(step_t) == 4u && b[0] == (1u | 8u | 0x20u) && b[1] == 5u && (int8_t)b[2] == -3 && b[3] == 0u &&
           sizeof(project_vb_t) == 3672u;
}
/* FDRB: SLIDE, TIE and the note through a save and a load (flash) */
static int fdrb_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[2].step[4].on = 1;
    trk[2].step[4].slide = 1;
    trk[2].step[4].nt = -12;
    trk[2].step[5].tie = 1;
    project_save(0);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(0);
    return trk[2].step[4].on && trk[2].step[4].slide && trk[2].step[4].nt == -12 && trk[2].step[5].tie &&
           !trk[2].step[5].on;
}
/* Review Focus 5: a stored step's reserved bits / byte and an out-of-range note: cleared and clamped on load */
static int fdrb_clamps(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    project_save(0);
    ((uint8_t *)&p->t[4].step[2])[0] = 0xFFu;        /* every flag, the reserved bits too */
    ((uint8_t *)&p->t[4].step[2])[3] = 0x5Au;
    p->t[4].step[3].nt = 100;                        /* past NT_MAX (C4 + 67 = G9) */
    p->t[4].step[5].nt = -100;
    p->sum = proj_sum(p);
    project_load(0);
    return trk[4].step[2].on && trk[4].step[2].acc && trk[4].step[2].slide && trk[4].step[2].tie &&
           trk[4].step[2].rat == 3u && ((const uint8_t *)&trk[4].step[2])[0] == 0x7Fu &&
           ((const uint8_t *)&trk[4].step[2])[3] == (0x5Au&ST_MASK) && trk[4].step[3].nt == NT_MAX && trk[4].step[5].nt == NT_MIN;
}
/* a broken song or name: the project loads, they are dropped */
static int fdr9_bad_song_name_dropped(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[3].step[5].on = 1;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    name_set(chain.name, "OK");
    project_save(0);
    p->song.row[0].repeat = 0;                       /* invalid */
    p->name[0] = 'a';                                /* outside the NAME set */
    p->sum = proj_sum(p);
    memset(&chain, 0, sizeof chain);
    trk[3].step[5].on = 0;
    project_load(0);
    return trk[3].step[5].on && chain.cfg.count == 0u && !chain.name[0];
}
/* chain_prepare: rows need used slots; the slots' garbage (cond, rat, timing, motion) is made safe */
static int song_garbage_slot(void)
{
    project_t *p = &proj_slot[1];
    uint32_t b, rc_empty, rc;
    memset(hflash, 0xFF, sizeof hflash);
    memset(proj_slot, 0, sizeof proj_slot);          /* (no slot left in RAM by the tests before) */
    host_init();
    persist_boot();
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){1, 1};
    rc_empty = chain_prepare();
    project_save(1);
    memset(p->t[0].step, 0xFF, sizeof p->t[0].step);  /* cond 255, rat 255, on 255 */
    p->t[0].p[P_SLEN] = 999;
    p->t[0].p[P_SDIV] = -7;
    p->motion.count = 200;                           /* broken */
    p->sum = proj_sum(p);
    rc = chain_prepare();
    for (b = 0; b < 400u; b++)
        render_mix(L, R, CTL);
    transport_req = 2;
    render_mix(L, R, CTL);
    return rc_empty == 4u && rc == 0u && chain.src[1].timing[0][0] == NSTEP && chain.src[1].timing[0][1] == 0 &&
           chain.src[1].m == &MOTION_NONE && !song.playing;
}
/* LOAD while a song plays: refused */
static int song_blocks_load(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[0].p[P_E1] = 11;
    project_save(0);
    trk[0].p[P_E1] = 64;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    chain_prepare();
    render_mix(L, R, CTL);
    project_load(0);
    transport_req = 2;
    render_mix(L, R, CTL);
    return trk[0].p[P_E1] == 64 && str_eq(ui.msg, "STOP TO LOAD");
}
/* a rename writes only the name (flash), and renames the current project when it is that slot */
static int rename_slot(void)
{
    char b[NAME_LEN + 1u];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[1].step[2].on = 1;
    name_set(chain.name, "A NAME");
    project_save(3);
    project_rename(3, "INTRO");
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    return project_name(3, b) && str_eq(b, "INTRO") && proj_slot[3].t[1].step[2].on && str_eq(chain.name, "INTRO") &&
           !project_name(2, b) && !b[0];
}
/* ---- FDRC (spec 2026-10-11 §10): PRIO per track; the older formats load with PRIO HIGH */

static void all_prio(int16_t p)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_PRIO] = p;
}
static int all_prio_is(int16_t p)
{
    uint32_t k, ok = 1;
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_PRIO] == p;
    return (int)ok;
}

/* PRIO through a save and a load (flash; FDRC's, now FDRD's) */
static int fdrc_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[1].p[P_PRIO] = 1;
    trk[5].p[P_PRIO] = 1;
    project_save(0);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(0);
    return trk[1].p[P_PRIO] == 1 && trk[5].p[P_PRIO] == 1 && trk[0].p[P_PRIO] == 0 && sizeof(project_t) <= 3840u &&
           proj_slot[0].magic == PROJ_MAGIC;       /* "MNM3" */
}

/* one pattern played 2 s from a loaded slot: a hash of the mix */
static uint32_t play_hash(uint32_t slot)
{
    uint32_t b, i, h = 2166136261u;
    project_load(slot);
    transport_req = 1;
    for (b = 0; b < 2u * FS / CTL; b++) {
        render_mix(L, R, CTL);
        for (i = 0; i < CTL; i++)
            h = (h ^ (uint32_t)L[i] ^ ((uint32_t)R[i] << 7)) * 16777619u;
    }
    transport_req = 2;
    render_mix(L, R, CTL);
    return h;
}
static void pattern_kit(void)                        /* the power-on kit, a groove of on / accent steps */
{
    uint32_t s;
    host_init();
    memset(&mo, 0, sizeof mo);
    memset(&chain, 0, sizeof chain);
    for (s = 0; s < 16u; s++) {
        trk[0].step[s].on = s % 4u == 0u;
        trk[1].step[s].on = s % 8u == 4u;
        trk[3].step[s].on = s % 2u == 0u;
        trk[3].step[s].acc = s % 4u == 2u;
        trk[5].step[s].on = s == 14u;
    }
}


/* MNM2 round trip: LFO 3's eight knobs, the EFFECTS' (EQ, the delay) */
static int fdre_slots_round_trip(void)
{
    static const int16_t V[8] = {LP_PTCH, 6, LT_HALF, LW_IEXP, 5, 99, 17, 101};
    uint32_t i, ok = 1;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    for (i = 0; i < 8u; i++)
        trk[5].p[P_LFO3 + i] = V[i];
    trk[5].p[P_EQF] = 11;
    trk[5].p[P_EQG] = 120;
    trk[5].p[P_DTIM] = 96;
    trk[5].p[P_DLY] = -50;
    trk[5].p[P_DFB] = 70;
    trk[5].p[P_DBAS] = 12;
    trk[5].p[P_DWID] = 33;
    project_save(3);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(3);
    for (i = 0; i < 8u; i++)
        ok &= trk[5].p[P_LFO3 + i] == V[i];
    return ok && trk[5].p[P_EQF] == 11 && trk[5].p[P_EQG] == 120 && trk[5].p[P_DTIM] == 96 && trk[5].p[P_DLY] == -50 &&
           trk[5].p[P_DFB] == 70 && trk[5].p[P_DBAS] == 12 && trk[5].p[P_DWID] == 33 &&
           sizeof(project_t) <= ST_PAYLOAD_MAX && proj_slot[3].magic == PROJ_MAGIC;   /* "MNM3" */
}

/* ---- the last project at power-on (spec 2026-10-11 §6, §9: lowcut's bits 2..4) */

static uint32_t host_jedec_keep;
/* main.c's boot after a power cycle: .noinit anything, .bss zero (song, the tracks, ui), the flash kept; SEQ held:
 * safe start. Returns the IRQ enables drum_boot_init did (main.c turns them on after audio_init: none) */
static uint32_t power_on(int seq)
{
    uint32_t irq;
    rfill(&settings, sizeof settings);
    rfill(proj_slot, sizeof proj_slot);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&ui, 0, sizeof ui);
    memset(&mo, 0, sizeof mo);
    memset(&chain, 0, sizeof chain);
    rfill(&lastp, sizeof lastp);                     /* (persist_boot sets it: from the flash record or 0) */
    transport_req = 0;
    persist_boot();
    settings_init();
    panel_init();
    fm1_in.buttons = seq ? 1u << panel.btn[B_SEQ] : 0u;
    irq = host_irq_on;
    drum_boot_init();
    fm1_in.buttons = 0;
    return host_irq_on - irq;
}
static uint32_t lc_bits(void)                         /* the stored record's lowcut bits 0..7 */
{
    persist_t p;
    memset(&p, 0, sizeof p);
    st_load(OBJ_SETTINGS, &p, sizeof p);
    return p.lowcut & 0xFFu;
}
static int power_on_kit(void)                        /* the tracks and globals as drum_tracks_init sets them */
{
    static track_t want[NTRK];
    static int16_t g[G_COUNT];
    uint32_t k, ok = 1;
    track_t keep[NTRK];
    int16_t gk[G_COUNT];
    memcpy(keep, trk, sizeof trk);
    memcpy(gk, song.g, sizeof gk);
    drum_tracks_init();
    memcpy(want, trk, sizeof trk);
    memcpy(g, song.g, sizeof g);
    memcpy(trk, keep, sizeof trk);
    memcpy(song.g, gk, sizeof gk);
    for (k = 0; k < NTRK; k++)
        ok &= !memcmp(trk[k].p, want[k].p, sizeof want[k].p) && !memcmp(trk[k].step, want[k].step, sizeof want[k].step);
    return (int)ok && !memcmp(song.g, g, sizeof g);
}
static void fresh_flash(void)                        /* a device with nothing saved: SPEAKER EQ BASS+ */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    settings.magic = 0;
    settings_init();
    settings.lowcut = 2;
    fx_lowcut = 2;
}

/* a flash SAVE remembers its slot (settings saved); the next power-on loads it, stopped, LOADED, without enabling
 * the IRQs; SPEAKER EQ keeps its value beside the bits */
static int last_project_loads(void)
{
    uint32_t ok, irq;
    fresh_flash();
    trk[3].p[P_E1] = 11;
    trk[3].step[6].on = 1;
    trk[2].p[P_PRIO] = 1;
    song.g[G_BPM] = 97;
    name_set(chain.name, "LAST ONE");
    project_save(1);
    project_save(3);                                 /* the last one wins */
    ok = str_eq(ui.msg, "SAVED") && lastp.set == 1u && lastp.slot == 3u && lc_bits() == (2u | 1u << 2 | 3u << 3);
    irq = power_on(0);
    ok &= trk[3].p[P_E1] == 11 && trk[3].step[6].on && trk[2].p[P_PRIO] == 1 && song.g[G_BPM] == 97 &&
          str_eq(chain.name, "LAST ONE") && chain.from == 4u && song.g[G_SLOT] == 4 && !song.playing &&
          str_eq(ui.msg, "LOADED") && !ui.confirm && irq == 0u && settings.lowcut == 2u && fx_lowcut == 2u &&
          lastp.set == 1u && lastp.slot == 3u;
    transport_req = 1;                               /* it plays after the boot */
    render_mix(0, 0, CTL * 64);
    return (int)ok && song.playing;
}

/* the remembered slot empty: confirm 5 over the power-on kit; NO keeps the slot (asked again at the next power-on),
 * YES forgets it (saved: the next power-on is the power-on kit, no question) */
static int last_project_empty(void)
{
    uint32_t ok, i, lit = 0;
    fresh_flash();
    project_save(1);                                 /* remembered: B */
    memset(hflash + 0x97000u + 1u * 2u * 4096u, 0xFF, 2u * 4096u);   /* slot B's flash erased */
    ok = power_on(0) == 0u && ui.confirm == 5u && ui.confirm_trk == 1u && power_on_kit() && !song.playing &&
         !str_eq(ui.msg, "EMPTY SLOT") && lastp.set == 1u;
    ui_frame();
    for (i = 70u * 240u; i < 150u * 240u; i++)
        lit += fb[i] != 0;
    ok &= lit > 300u;
    shot("build/ui_shots/fixes/load_failed.ppm");
    press(B_OCTDN);                                  /* NO */
    ui_frame();
    release_all();
    ok &= !ui.confirm && lastp.set == 1u && lastp.slot == 1u && lc_bits() == (2u | 1u << 2 | 1u << 3) && power_on_kit();
    ok &= power_on(0) == 0u && ui.confirm == 5u && ui.confirm_trk == 1u;   /* asked again */
    for (i = 0; i < 5u; i++)                         /* nothing else reacts meanwhile */
        ui_frame();
    ok &= ui.confirm == 5u;
    press(B_OCTUP);                                  /* YES */
    ui_frame();
    release_all();
    ok &= !ui.confirm && lastp.set == 0u && lc_bits() == 2u && power_on_kit() && settings.lowcut == 2u;
    ok &= power_on(0) == 0u && !ui.confirm && power_on_kit() && !str_eq(ui.msg, "LOADED") && settings.lowcut == 2u;
    return (int)ok;
}

/* the remembered slot unreadable (a record whose sum fails): the same question */
static int last_project_corrupt(void)
{
    project_t c;
    fresh_flash();
    trk[4].p[P_E1] = 5;
    project_save(2);
    c = proj_slot[2];
    c.sum ^= 0x10u;
    st_save(OBJ_PROJECT0 + 2u, &c, sizeof c);
    return power_on(0) == 0u && ui.confirm == 5u && ui.confirm_trk == 2u && power_on_kit();
}

/* SEQ held at power-on (safe start): no load, no question; the slot stays remembered */
static int last_project_safe_start(void)
{
    fresh_flash();
    trk[3].p[P_E1] = 11;
    project_save(0);
    power_on(1);
    return safe_start && !ui.confirm && power_on_kit() && !str_eq(ui.msg, "LOADED") && lastp.set == 1u &&
           lastp.slot == 0u;
}

/* an older settings record (no last-project bits): the power-on kit as before, SPEAKER EQ kept; a project saved
 * in RAM only (no flash) remembers nothing */
static int last_project_old_record_no_flash(void)
{
    persist_t p;
    uint32_t ok;
    fresh_flash();
    trk[3].p[P_E1] = 11;
    project_save(0);                                 /* a slot, but the record does not name it */
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = 2;
    p.lowcut = 1;                                    /* LOWCUT */
    p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &p, sizeof p);
    ok = power_on(0) == 0u && !ui.confirm && power_on_kit() && settings.lowcut == 1u && lastp.set == 0u;
    host_jedec = 0;                                  /* no flash */
    fresh_flash();
    trk[3].p[P_E1] = 11;
    project_save(0);
    ok &= !flash_ok && str_eq(ui.msg, "SAVED (RAM)") && lastp.set == 0u;
    ok &= power_on(0) == 0u && !ui.confirm && !flash_ok && lastp.set == 0u;
    host_jedec = host_jedec_keep;
    return (int)ok;
}

/* lowcut's bits: SPEAKER EQ (0..2) and every slot round trip, the Bluetooth bytes (8..31) kept */
static int last_project_bits(void)
{
    uint32_t m, k, ok = 1;
    for (m = 0; m < 3u; m++)
        for (k = 0; k < 4u; k++) {
            persist_t p;
            fresh_flash();
            settings.lowcut = m;
            btset.bt_hi = 0x123456u;
            lastp.set = 1;
            lastp.slot = (uint8_t)k;
            settings_save();
            st_load(OBJ_SETTINGS, &p, sizeof p);
            ok &= p.lowcut == (m | 1u << 2 | k << 3 | 0x123456u << 8);
            rfill(&settings, sizeof settings);
            memset(&lastp, 0, sizeof lastp);
            persist_boot();
            settings_init();
            ok &= settings.lowcut == m && fx_lowcut == m && lastp.set == 1u && lastp.slot == k && btset.bt_hi == 0x123456u;
        }
    return (int)ok;
}
/* MEMB at every knob extreme (TUNE, DECAY, TONE, HEAD, POS, BEND, STICK) and both velocities: bounded, in memory */
static int memb_extremes(void)
{
    static const int16_t LO[8] = {-24, 0, 0, 0, 0, 0, 0, 0}, HI[8] = {24, 127, 127, 127, 127, 127, 127, 0};
    uint32_t c, k, i;
    int ok = 1;
    for (c = 0; c < 256u; c++) {
        host_init();
        drum_set_model(&trk[0], DM_MEMB);
        for (k = 0; k < 7u; k++)
            trk[0].p[P_E0 + k] = ((c >> k) & 1u) ? HI[k] : LO[k];
        drum_hit(&trk[0], (c & 128u) ? 127u : 96u);
        for (i = 0; i < 40u; i++) {
            render_mix(L, R, CTL);
            ok &= L[0] <= 32767 && L[0] >= -32768;
        }
    }
    return ok;
}
int main(void)
{
    char what[96];
    host_jedec_keep = host_jedec;
    int k, s, seq;







    check("project: the reverb TYPE survives a save and a load", fdr7_round_trip());
    check("project: FDR8 keeps the motion through a save and a load", fdr8_motion_round_trip());

    check("project: a broken motion block is dropped, the project loads", fdr8_bad_motion_dropped());
    check("project: a load while motion plays: the old base is not put back", fdr8_load_while_playing());
    check("project: FDRA keeps the song and the name through a save and a load", fdr9_round_trip());

    check("project: FDRA keeps the five filter knobs through a save and a load", fdra_round_trip());
    check("project: out-of-range filter values are clamped on load", fdra_clamps());
    check("project: FDRB's step layout (flags, cond, note offset) in 4 bytes", fdrb_step_layout());


    check("project: FDRB keeps SLIDE, TIE and the note through a save and a load", fdrb_round_trip());
    check("project: a stored step's reserved bits are cleared, its note clamped", fdrb_clamps());

    check("project: a broken song or name is dropped, the project loads", fdr9_bad_song_name_dropped());
    check("song: an empty slot refused; a slot's garbage made safe (ASan)", song_garbage_slot());
    check("song: LOAD while a song plays: STOP TO LOAD", song_blocks_load());
    check("project: a rename writes the name only, the current project's too", rename_slot());
    check("MEMB at every extreme: bounded (ASan)", memb_extremes());

    check("settings: MUTE NEXT BAR, ZOOM and KNOB ACCEL survive a power cycle (Felucca's settings format)", mutebar_persists());
    check("settings: USB LEVEL FIXED survives a power cycle; a save without it reads MASTER", usb_level_persists());
    check("settings: a record longer than ours loads the defaults (not truncated)", settings_oversize_refused());
    check("settings: an older record (no Bluetooth bits) loads as before (SPEAKER EQ BASS+ stays, no device)",
          settings_old_record());
    check("settings: the remembered device and MODE survive a power cycle in the spare bits; SPEAKER EQ keeps its value",
          settings_bt_round_trip());
    check("settings: a record's MODE bits beyond MIDI load AUTO", settings_bt_mode_range());
    check("settings: .noinit's settings stay 32 B (an update finds the RAM projects); no record: no device",
          settings_bt_after_update());
    check("settings: METRO survives a power cycle in lowcut's bits 5..6 (SPEAKER EQ, the device kept); 0.16.0's record "
          "and none load OFF", settings_metro_round_trip());
    check("settings: BACKLIGHT survives a power cycle in lowcut's bit 7 (SPEAKER EQ, the last project, METRO, the "
          "device kept); an older record and none load OFF; every level (item 9) kept, an older ON is DIM LO",
          settings_backlight_round_trip());
    check("settings: AUTOSTART survives a power cycle in the palette word's bit 8 (the palette, SPEAKER EQ, BACKLIGHT, "
          "the device kept); an older record and none load OFF", settings_autostart_round_trip());
    check("project: CLK beyond this build's range (BLE) loads as INT, not TRS", clock_beyond_range_is_int());

    check("project: PRIO kept through a save and a load (FDRE)", fdrc_round_trip());



    check("project: LFO 3 and the EFFECTS kept through a save and a load (MNM2, one flash sector)", fdre_slots_round_trip());


    check("last project: a flash SAVE remembers its slot; power-on loads it (stopped, LOADED, no IRQ enable)",
          last_project_loads());
    check("last project: an empty slot asks START NEW PROJECT? over the power-on kit; NO asks again, YES forgets",
          last_project_empty());
    check("last project: an unreadable slot asks the same", last_project_corrupt());
    check("last project: safe start loads nothing and asks nothing", last_project_safe_start());
    check("last project: an older record boots the power-on kit; a RAM-only save remembers nothing",
          last_project_old_record_no_flash());
    check("last project: lowcut's bits keep SPEAKER EQ, the slot and the Bluetooth bytes", last_project_bits());
    for (k = 0; k < F_KINDS; k++)
        for (s = 0; s < (k == F_RANDOM || k == F_HEADERS ? 8 : 1); s++)
            for (seq = 0; seq < 2; seq++) {
                snprintf(what, sizeof what, "boot: %s #%d (%s)", KIND[k], s, seq ? "safe" : "normal");
                check(what, boot(k, (uint32_t)s + 1u, seq));
            }
    printf(fails ? "boot_test: %d FAILED\n" : "boot_test: all passed\n", fails);
    return fails ? 1 : 0;
}
