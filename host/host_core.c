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
int plat_midi_in(uint8_t msg[3])
{
    (void)msg;
    return 0;
}
void plat_midi_out(const uint8_t *msg, int n) { (void)msg, (void)n; }
int plat_audio_in(float *lr, int frames) { return host_audio_in(lr, frames); }

/* storage: slots in memory, kept across a simulated reboot */
static uint8_t store[PLAT_STORES][PLAT_SLOT_MAX];
static int32_t store_len[PLAT_STORES] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
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

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }

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
