/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel on a simulated FM-1: the whole instrument (screen, controls, engine) against a platform
 * made of plain memory. No C library is used here, so the same file builds the desktop
 * simulator (host/ravel_host.c: scripts, PNG and WAV) and the browser emulator
 * (web/emu/ravel_web.c, WebAssembly).
 *
 * Timing: audio in blocks of 128 frames at 44,100 Hz,
 * the interface polled every millisecond and drawn every 16 ms. */
#include <stdint.h>
#include "../firmware/app/plat.h"

/* the front end receives the audio and supplies the input */
static void host_audio(const int32_t *blk, uint32_t n);
static int host_audio_in(float *lr, int frames);

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[PLAT_W * PLAT_H];                /* native RGB565 */
void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < PLAT_W && y + j < PLAT_H)
                fb[(y + j) * PLAT_W + x + i] = px[j * w + i];
}

/* ---------------------------------------------------------- platform --- */
static uint32_t now_ms, held_btn, held_keys, master = 3000;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v;
    if (role < 0 || role >= NE)
        return 0;
    v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k)
{
    lit_btn = b;
    lit_keys = k;
}
/* MIDI in: what the simulator's scripts (midi a b c) and the emulator's page (web_midi) send */
static uint8_t hm_q[256][3];
static uint32_t hm_w, hm_r;
void host_midi_push(int a, int b, int c)
{
    if (hm_w - hm_r < 256u) {
        hm_q[hm_w & 255u][0] = (uint8_t)a, hm_q[hm_w & 255u][1] = (uint8_t)b, hm_q[hm_w & 255u][2] = (uint8_t)c;
        hm_w++;
    }
}
int plat_midi_in(uint8_t msg[3])
{
    if (hm_r == hm_w)
        return 0;
    msg[0] = hm_q[hm_r & 255u][0], msg[1] = hm_q[hm_r & 255u][1], msg[2] = hm_q[hm_r & 255u][2];
    hm_r++;
    return 1;
}
int plat_midi_age(void) { return -1; }
void plat_midi_out(const uint8_t *msg, int n) { (void)msg, (void)n; }
int plat_audio_in(float *lr, int frames) { return host_audio_in(lr, frames); }

/* storage: slots in memory, kept across a simulated reboot */
static uint8_t store[PLAT_STORES][PLAT_SLOT_MAX];
static int32_t store_len[PLAT_STORES] = {[0 ... PLAT_STORES - 1] = -1};
static uint32_t store_writes;
int plat_load(int slot, void *dst, uint32_t max)
{
    int32_t n, i;
    if (slot < 0 || slot >= PLAT_STORES || (n = store_len[slot]) < 0)
        return -1;
    for (i = 0; i < n && (uint32_t)i < max; i++)
        ((uint8_t *)dst)[i] = store[slot][i];
    return n;
}
int plat_save(int slot, const void *src, uint32_t len)
{
    uint32_t i;
    if (slot < 0 || slot >= PLAT_STORES || len > PLAT_SLOT_MAX)
        return -1;
    for (i = 0; i < len; i++)
        store[slot][i] = ((const uint8_t *)src)[i];
    store_len[slot] = (int32_t)len;
    store_writes++;
    return 0;
}

/* a slot written in pieces, as from a computer (the FM-1's room: 32 KB a project, 12 KB a bank, less
 * the header) */
static uint32_t host_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static uint32_t host_room(int slot) { return plat_is_bank(slot) ? 0x3000u - 256u : 0x8000u - 256u; }
int plat_proj_room(int *free_pct) { *free_pct = 100; return -1; }   /* (no flash to fill here) */
int plat_proj_clear(int slot)
{
    if (plat_proj_of(slot) < 0 || store_len[slot] < 0)
        return -1;
    store_len[slot] = -1;
    store_writes++;
    return 0;
}
int plat_slot_begin(int slot, uint32_t len)
{
    if (slot < 0 || slot >= PLAT_STORES || len > host_room(slot))
        return -1;
    store_len[slot] = -1;
    return 0;
}
int plat_slot_write(int slot, uint32_t off, const void *p, uint32_t n)
{
    uint32_t i;
    if (slot < 0 || slot >= PLAT_STORES || off + n > host_room(slot) || store_len[slot] >= 0)
        return -1;
    for (i = 0; i < n; i++)
        store[slot][off + i] = ((const uint8_t *)p)[i];
    return 0;
}
int plat_slot_end(int slot, uint32_t len, uint32_t crc)
{
    if (slot < 0 || slot >= PLAT_STORES || len > host_room(slot) || store_len[slot] >= 0 || host_crc32(store[slot], len) != crc)
        return -1;
    store_len[slot] = (int32_t)len;
    store_writes++;
    return 0;
}
int plat_slot_info(int slot, uint32_t *len, uint32_t *crc)
{
    if (slot < 0 || slot >= PLAT_STORES || store_len[slot] < 0)
        return -1;
    *len = (uint32_t)store_len[slot];
    *crc = host_crc32(store[slot], *len);
    return 0;
}
int plat_slot_read(int slot, uint32_t off, void *p, uint32_t n)
{
    uint32_t i;
    if (slot < 0 || slot >= PLAT_STORES || store_len[slot] < 0 || off + n > (uint32_t)store_len[slot])
        return -1;
    for (i = 0; i < n; i++)
        ((uint8_t *)p)[i] = store[slot][off + i];
    return 0;
}
/* SysEx to the computer: kept for whoever drives the simulator (the emulator page, the tests) */
static uint8_t sx_out[8192];
static uint32_t sx_out_n;
void plat_sysex_out(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && sx_out_n < sizeof sx_out; i++)
        sx_out[sx_out_n++] = p[i];
}

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_cpu_peak(void) { return 0; }
int plat_battery(void) { return -1; }   /* (no battery here) */
void plat_backlight(int level) { (void)level; }   /* (no LEDs here) */
static uint8_t prefs_mem[256];                   /* (kept across a simulated reboot, as the slots are) */
static int prefs_len = -1;
int plat_prefs_load(void *dst, int max)
{
    int i, n = prefs_len < max ? prefs_len : max;
    for (i = 0; i < n; i++)
        ((uint8_t *)dst)[i] = prefs_mem[i];
    return prefs_len < 0 ? -1 : n;
}
int plat_prefs_save(const void *src, int len)
{
    int i;
    if (len > (int)sizeof prefs_mem)
        return -1;
    for (i = 0; i < len; i++)
        prefs_mem[i] = ((const uint8_t *)src)[i];
    prefs_len = len;
    return 0;
}
int plat_flash_info(uint32_t v[4]) { v[0] = v[1] = v[2] = v[3] = 0; return -1; }
#ifdef __wasm__
uint32_t plat_cycles(void) { return 0; }        /* (the browser gives no clock to read here) */
uint32_t plat_cycles_hz(void) { return 1000000000u; }
#else
#include <time.h>
uint32_t plat_cycles(void)                       /* this machine: nanoseconds stand in for cycles */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec);
}
/* RAVEL_CPU_SCALE=k: run as if this machine were k times slower (the CPU page, the overload guard
 * and the stress test then read about as they would on the FM-1, once k is measured against it) */
#include <stdlib.h>
uint32_t plat_cycles_hz(void)
{
    static uint32_t hz;
    if (!hz) {
        const char *e = getenv("RAVEL_CPU_SCALE");
        double k = e ? atof(e) : 1.0;
        hz = (uint32_t)(1e9 / (k >= 1.0 ? k : 1.0));
    }
    return hz;
}
#endif
uint32_t plat_xruns(void) { return 0; }

/* no second core here. RAVEL_FAKE_CORE1=1 stands one in that does its work at once (the split's
 * bookkeeping tested); =fail one that takes the work and never does it (the fallback tested) */
static int par_fake, par_fail;
int plat_fetch_on(int on) { return on ? -1 : 0; }   /* (no flash to wait on here) */
uint32_t plat_fetch_wait(void) { return 0; }
int plat_par_on(int on)
{
#ifndef __wasm__
    const char *e = getenv("RAVEL_FAKE_CORE1");
    par_fake = on && e && *e;
    par_fail = par_fake && e[0] == 'f';
#else
    (void)on;
#endif
    return on && !par_fake ? -1 : 0;
}
int plat_par_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!par_fake)
        return 0;
    if (!par_fail)
        fn(arg);
    return 1;
}
void plat_par_sync(void) {}
uint32_t plat_par_cycles(void) { return plat_cycles(); }
int plat_par_wait(void)
{
    if (par_fail) {
        par_fake = par_fail = 0;                  /* off from now on, as the FM-1 does */
        return -1;
    }
    return 0;
}

/* ----------------------------------------------------------- the app --- */
void ravel_start(void);
void rv_render(int32_t *out, int frames);
void ui_frame(void);
void ui_input_only(void);

#define BLOCK 128u
static int32_t abuf[BLOCK * 2u];
static uint32_t frames_due_x1000, last_frame;

static void boot(void)
{
    now_ms = 0;
    last_frame = 0;
    frames_due_x1000 = 0;
    ravel_start();
}

/* the master knob, as the FM-1's audio interrupt applies it */
static void master_gain(int32_t *b, uint32_t n)
{
    uint32_t i;
    int32_t g = (int32_t)(master > 4096u ? 4096u : master);
    for (i = 0; i < 2u * n; i++)
        b[i] = (int32_t)(((int64_t)b[i] * g) >> 12);
}

/* the device for ms milliseconds: audio as it falls due, the interface every 16 ms */
static void run_ms(uint32_t ms)
{
    while (ms--) {
        now_ms++;
        frames_due_x1000 += 44100u;
        while (frames_due_x1000 >= BLOCK * 1000u) {
            frames_due_x1000 -= BLOCK * 1000u;
            rv_render(abuf, (int)BLOCK);
            master_gain(abuf, BLOCK);
            host_audio(abuf, BLOCK);
        }
        if (now_ms - last_frame >= 16u) {
            last_frame = now_ms;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}
