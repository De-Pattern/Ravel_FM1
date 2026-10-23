/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's platform: everything the instrument needs from the machine it runs on.
 *
 * The FM-1 implements it bare metal on Felucca's open hardware layer (firmware/fm1/src/plat_fm1.c); the desktop
 * simulator and the browser emulator implement it on plain memory (host/host_core.c). */
#pragma once
#include <stdint.h>

/* buttons by printed label (firmware/fm1/src/panel.c maps them to the key matrix) */
enum { B_OCTDN, B_OCTUP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, NB };
/* encoders */
enum { EN_SELECT, EN_ALGO, EN_K1, EN_K2, EN_K3, EN_K4, EN_PRESET, NE };
#define NKEYS 27                        /* F3 .. G5 */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);            /* held: bit B_* */
uint32_t plat_keys(void);               /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);             /* detents since the last call, + = clockwise */
uint32_t plat_master(void);             /* MASTER knob, 0..4096 */
void plat_leds(uint32_t buttons, uint32_t keys);

/* the screen: a band of native-endian RGB565 pixels */
#define PLAT_W 240
#define PLAT_H 240
void plat_blit(int x, int y, int w, int h, const uint16_t *px);

/* storage: numbered slots of up to PLAT_SLOT_MAX bytes */
#define PLAT_SLOT_MAX (64u * 1024u)
#define PLAT_SLOTS 7                    /* the projects (0..6; 7 was the eighth, now the sound banks' room) */
#define PLAT_BANK_OLD 8                 /* the first sound bank's old place (read once, to move it) */
#define PLAT_BANK0 9                    /* the sound banks A..D */
#define PLAT_BANKS 4
#define PLAT_STORES (PLAT_BANK0 + PLAT_BANKS)
int plat_load(int slot, void *dst, uint32_t max);          /* copies up to max; returns the stored length, < 0 = empty */
int plat_save(int slot, const void *src, uint32_t len);    /* 0 = saved */
/* a slot written in pieces (a transfer from a computer): begin empties it, writes land at off (256-byte
 * pieces), end checks the CRC-32 (zlib's) of all len bytes and only then makes it valid; 0 = ok */
int plat_slot_begin(int slot, uint32_t len);
int plat_slot_write(int slot, uint32_t off, const void *p, uint32_t n);
int plat_slot_end(int slot, uint32_t len, uint32_t crc);
int plat_slot_info(int slot, uint32_t *len, uint32_t *crc);   /* 0 = used */
int plat_slot_read(int slot, uint32_t off, void *p, uint32_t n);
void plat_sysex_out(const uint8_t *p, uint32_t n);         /* one whole F0 .. F7 message to the computer */

/* audio coming in (a file in the emulator; none on the FM-1, whose USB sends Ravel's output): fills up to
 * frames stereo frames, interleaved, -1..1; returns how many (0 = nothing playing in) */
int plat_audio_in(float *lr, int frames);

/* MIDI: complete short messages */
int plat_midi_in(uint8_t msg[3]);       /* 1 = got one */
void plat_midi_out(const uint8_t *msg, int n);

uint32_t plat_cpu_pct(void);            /* audio render load, % */
uint32_t plat_cycles(void);             /* a free-running count of the processor's cycles (0: none to read) */
uint32_t plat_cycles_hz(void);          /* how many of them a second */
uint32_t plat_xruns(void);              /* audio blocks that were not ready in time, since boot */
