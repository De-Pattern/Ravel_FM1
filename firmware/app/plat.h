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
int plat_battery(void);                 /* the battery: 0..3 bars, 4 on USB power, -1 none here */
void plat_leds(uint32_t buttons, uint32_t keys);

/* the screen: a band of native-endian RGB565 pixels */
#define PLAT_W 240
#define PLAT_H 240
void plat_blit(int x, int y, int w, int h, const uint16_t *px);

/* storage: numbered slots of up to PLAT_SLOT_MAX bytes */
#define PLAT_SLOT_MAX (64u * 1024u)
/* projects 1-7 are slots 0-6 (as they always were; 7 was the eighth, now the sound banks' room), the
 * sound banks A-D 9-12, and projects 8-32 slots 13-37 (so a slot keeps its number in files and transfers) */
#define PLAT_SLOTS 7                    /* the first projects' slots, 0..6 */
#define PLAT_BANK_OLD 8                 /* the first sound bank's old place (read once, to move it) */
#define PLAT_BANK0 9                    /* the sound banks A..D */
#define PLAT_BANKS 4
#define PLAT_PROJECTS 32                /* projects in all */
#define PLAT_PROJ_MORE 13               /* the slot of project 8 */
#define PLAT_PROJ(p) ((p) < PLAT_SLOTS ? (p) : PLAT_PROJ_MORE + (p) - PLAT_SLOTS)   /* project p (0..31): its slot */
#define PLAT_STORES (PLAT_PROJ_MORE + PLAT_PROJECTS - PLAT_SLOTS)
static inline int plat_proj_of(int slot)   /* a slot's project (0..31), or -1 */
{
    return slot >= 0 && slot < PLAT_SLOTS ? slot : slot >= PLAT_PROJ_MORE && slot < PLAT_STORES ? slot - PLAT_PROJ_MORE + PLAT_SLOTS : -1;
}
static inline int plat_is_bank(int slot) { return slot >= PLAT_BANK0 && slot < PLAT_BANK0 + PLAT_BANKS; }
/* the device's own settings (MIDI IN), kept across power-off apart from any project: a length, -1 none */
int plat_prefs_load(void *dst, int max);
int plat_prefs_save(const void *src, int len);
void plat_backlight(int level);          /* the unlit buttons and keys glow dimly: 0 off, 1 low, 2 mid, 3 high */
int plat_proj_clear(int slot);          /* a project slot emptied, its room free again: 0, or -1 (empty, or not a project slot) */
int plat_proj_room(int *free_pct);      /* how much room the projects have left, % (-1: not counted here) */
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
int plat_midi_in(uint8_t msg[3]);       /* 1 = got one: a channel message, or a real-time byte (F8-FF) in msg[0] */
int plat_midi_age(void);                /* how long ago (samples) the real-time byte plat_midi_in just gave came in; -1: not known */
void plat_midi_out(const uint8_t *msg, int n);

uint32_t plat_cpu_pct(void);            /* audio render load, % */
int plat_flash_info(uint32_t v[4]);           /* the flash as the boot set it up: JEDEC id, status 1|2<<8, controller word, its clock divider (-1: no flash) */
uint32_t plat_cpu_peak(void);           /* the busiest audio block lately, % of its time (0: not measured) */
uint32_t plat_cycles(void);             /* a free-running count of the processor's cycles (0: none to read) */
uint32_t plat_cycles_hz(void);          /* how many of them a second */
uint32_t plat_xruns(void);
/* a second core (the FM-1's AC79 has one): on asks it to start (0 = running; < 0 = none here, or it
 * would not start, or it failed before and stays off until power-off). run hands it fn(arg) while
 * this core goes on (1 = it took it); wait waits for it (0 = done; < 0 = it failed and is off now:
 * the caller does that work itself). */
int plat_par_on(int on);
int plat_par_run(void (*fn)(uint32_t), uint32_t arg);
int plat_par_wait(void);
void plat_par_sync(void);               /* both cores' writes seen before what follows */
uint32_t plat_par_cycles(void);         /* a cycle count either core can read (as plat_cycles) */
/* the core's instruction-fetch waits (the FM-1 runs its code from the flash, through a cache): on
 * starts counting them (0 = counting; < 0 = not here), wait is the count so far, in cycles */
int plat_fetch_on(int on);
uint32_t plat_fetch_wait(void);              /* audio blocks that were not ready in time, since boot */
