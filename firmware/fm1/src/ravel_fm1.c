/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel on the M-VAVE FM-1, bare metal: one compilation unit for the platform (Felucca's layout, by
 * way of X0X: the HAL is header-only, so everything that touches it is here). The instrument itself
 * (firmware/app/ravel_app.c) and the Beads port (core/beads) are separate units at -O2, built by
 * tools/build_fm1.py. No vendor SDK: the hardware layer, USB, storage and the updater are
 * Felucca's and X0X's (GPL-3.0). Order matters. */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "fm1_cpu1.h"
#include "fm1_flash.h"
#include "fm1_xip.h"
#include "fm1_perf.h"

#include "libc.c"
#include "lcd.c"

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

#include "plat.h"                 /* Ravel's platform interface (firmware/app/plat.h) */

/* the instrument (ravel_app.c) */
void ravel_start(void);
void rv_render(int32_t *out, int frames);
void ui_frame(void);
void ui_input_only(void);
void ui_notice(const char *title, const char *l1, const char *l2, const char *l3, int red);
void rv_transport(int run);
void rv_xfer_msg(const uint8_t *m, uint32_t d);   /* projects and sounds over SysEx (app/rv_xfer.c) */

#define FELUCCA_OTA 1             /* M-UPGRADE update entry: keeps the web installer working */
#ifndef RAVEL_CDC
#define RAVEL_CDC 0               /* USB serial console off: one plain MIDI interface */
#endif
#define FELUCCA_CDC RAVEL_CDC
#ifndef RAVEL_UAC
#define RAVEL_UAC 1               /* USB audio: the master output to a computer, recordable over the cable */
#endif
#define FELUCCA_UAC RAVEL_UAC
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_82"     /* package identity (tools/build_fm1.py sets the release's) */
#endif
#include "usb.c"
#ifndef RAVEL_TRS
#define RAVEL_TRS 1               /* MIDI IN on the TRS jack */
#endif
#if RAVEL_TRS
#include "midi_uart.c"
#endif

/* storage objects (storage.c): only the panel calibration is one; projects are slots (plat_fm1.c) */
enum { OBJ_SET, OBJ_SOUND, OBJ_PAT0, OBJ_PAT1, OBJ_PANEL, OBJ_SONG, OBJ_MOTION, OBJ_NOBJ };

static uint8_t flash_ok;          /* JEDEC id matched at boot */
static void audio_silence(void);
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#include "storage.c"
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

struct bootguard_s { uint32_t magic, failed, pending; };
extern struct bootguard_s bootguard;
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECKING", "LOADER", "CONFIRM", "RESTARTING"};
    if (step < 9u) {
        ui_notice("UPDATE", STEP[step < 6u ? step : 0], "KEEP THE FM-1 CONNECTED", 0, 0);
        return;
    }
    ui_notice("UPDATE", code == 1 ? "DRY RUN OK" : "FAILED", 0, 0, code != 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard.pending = 0;                              /* intentional reset */
    bootguard.failed = 0;                               /* a new install starts with a clean slate */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

#include "panel.c"
#include "plat_fm1.c"
#include "main_fm1.c"
