/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's platform (firmware/app/plat.h) on the FM-1: Felucca's HAL and panel map, X0X's LED
 * picture, the LCD, flash slots for projects, and USB / TRS MIDI.
 * Part of the unity build (ravel_fm1.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }
/* the battery, from the divider on ADC ch3 (smoothed in the main loop): Melodee's thresholds, 531 / 561 /
 * 591, as 0-3 bars; on a USB host's power the reading says little, so a bolt instead (4) */
static volatile int32_t batt_raw;               /* 0: not read yet */
int plat_battery(void)
{
    int32_t b = batt_raw;
    if (usb.config && !usb.suspended)
        return 4;
    if (!b)
        return -1;
    return b >= 591 ? 3 : b >= 561 ? 2 : b >= 531 ? 1 : 0;
}

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing and
 * relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY)                         /* PLAY's green LED (keyless position col 8, row PA5, X0X) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF || !on)
            continue;
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = 0;
    }
    fm1_led_dim_div = 0u;
}

/* the screen: Ravel draws bands of native RGB565; the panel wants them byte-swapped, from RAM, and
 * the transfer runs on while the next band is drawn. Two swap buffers take turns (lcd_window waits
 * for the transfer before starting the next, so the one being filled is never being sent).
 * Bands that did not change are not sent: the SPI link is the slow part of a frame. */
#define BLIT_MAX (PLAT_W * 16)
static uint16_t blit_buf[2][BLIT_MAX] __attribute__((aligned(4)));
static uint8_t blit_k;
static uint32_t band_hash[PLAT_H / 8];
void plat_blit(int x, int y, int w, int h, const uint16_t *px)
{
    uint32_t hsh = 2166136261u, n, i;
    uint16_t *b;
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > PLAT_W || y + h > PLAT_H)
        return;
    n = (uint32_t)(w * h);
    if (n > BLIT_MAX)
        return;
    for (i = 0; i < n; i++)
        hsh = (hsh ^ px[i]) * 16777619u;
    if (x == 0 && w == PLAT_W && (y & 7) == 0) {
        if (band_hash[y / 8] == hsh)
            return;
        band_hash[y / 8] = hsh;
    }
    b = blit_buf[blit_k];
    blit_k ^= 1u;
    for (i = 0; i < n; i++)
        b[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
    lcd_blit((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, b);
}

/* projects: compressed, in a pool of sectors (projstore.c: Felucca's data area 0x97000 .. 0xCF000 and
 * three spare sectors), as many as fit, 32 at most; then the four sound banks of 12 KB (0xCF000 ..
 * 0xDB000, where the eighth fixed project slot was), the panel calibration at 0xDC000 and the
 * updater's staging at 0xE0000. (The first sound bank once sat at 0xD7000: kept readable as
 * PLAT_BANK_OLD so it can be moved.) A bank: a header, then the data; the header is written last, so
 * a slot cut short by a power loss reads as empty. Writing pauses the sound (the code runs from the
 * same flash). */
#include "projstore.c"
#define SLOT_BASE 0x00097000u
#define SLOT_SIZE 0x8000u
#define SLOT_MAGIC 0x4C535652u                  /* "RVSL" */
typedef struct { uint32_t magic, len, crc, rsv; } slot_head;
#define BANK_BASE 0x000CF000u
#define BANK_SIZE 0x3000u
#define BANK_OLD 0x000D7000u
static uint32_t slot_base(int slot)
{
    return slot >= PLAT_BANK0 ? BANK_BASE + (uint32_t)(slot - PLAT_BANK0) * BANK_SIZE
           : slot == PLAT_BANK_OLD ? BANK_OLD : SLOT_BASE + (uint32_t)slot * SLOT_SIZE;
}
static uint32_t slot_room(int slot) { return slot >= PLAT_BANK0 ? BANK_SIZE : slot == PLAT_BANK_OLD ? 0x5000u : SLOT_SIZE; }
static int slot_ok(int slot) { return plat_is_bank(slot) || slot == PLAT_BANK_OLD; }   /* (the banks: projects go to projstore.c) */
int plat_proj_room(int *free_pct)
{
    *free_pct = flash_ok ? ps_free_count() * 100 / PS_SECTORS : 0;
    return 0;
}

int plat_load(int slot, void *dst, uint32_t max)
{
    slot_head h;
    uint32_t base = slot_base(slot), n;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_load(plat_proj_of(slot), dst, max);
    if (!flash_ok || !slot_ok(slot))
        return -1;
    if (st_read(base, &h, sizeof h) || h.magic != SLOT_MAGIC || h.len > slot_room(slot) - 256u)
        return -1;
    n = h.len < max ? h.len : max;
    if (st_read(base + 256u, dst, n))
        return -1;
    if (n == h.len && st_crc32(dst, n) != h.crc)
        return -1;
    return (int)h.len;
}

int plat_save(int slot, const void *src, uint32_t len)
{
    slot_head h;
    uint32_t base = slot_base(slot), off;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_save(plat_proj_of(slot), src, len);   /* (-2: no room left) */
    if (!flash_ok || !slot_ok(slot) || slot == PLAT_BANK_OLD || len > slot_room(slot) - 256u)   /* (a bank stays in its 12 KB) */
        return -1;
    for (off = 0; off < len + 256u; off += 0x1000u)
        if (st_erase(base + off))
            return -1;
    for (off = 0; off < len; off += 256u) {    /* (the project lives in RAM, as the driver wants) */
        uint32_t k = len - off > 256u ? 256u : len - off;
        if (st_prog(base + 256u + off, (const uint8_t *)src + off, k))
            return -1;
    }
    h.magic = SLOT_MAGIC;
    h.len = len;
    h.crc = st_crc32(src, len);
    h.rsv = 0xFFFFFFFFu;
    if (st_prog(base, &h, sizeof h))
        return -1;
    {   /* read back: a failing part must not report SAVED */
        slot_head c;
        if (st_read(base, &c, sizeof c) || c.magic != SLOT_MAGIC || c.len != len || c.crc != h.crc)
            return -1;
    }
    return 0;
}

/* a slot written in pieces, from a computer (rv_xfer.c): BEGIN erases it (header too, so it reads as empty
 * until END), each piece is programmed in place, END reads it all back for the CRC and only then writes the
 * header. Nothing is held in RAM. */
static uint32_t xf_crc_upd(uint32_t c, const uint8_t *b, uint32_t n)   /* zlib CRC-32, running (c: ~previous) */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static int8_t xf_open = -1;                     /* the slot being written, or -1 */
int plat_slot_begin(int slot, uint32_t len)
{
    uint32_t base = slot_base(slot), off;
    xf_open = -1;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_xbegin(plat_proj_of(slot), len);
    if (!flash_ok || !slot_ok(slot) || slot == PLAT_BANK_OLD || len > slot_room(slot) - 256u)
        return -1;
    for (off = 0; off < len + 256u; off += 0x1000u)
        if (st_erase(base + off))
            return -1;
    xf_open = (int8_t)slot;
    return 0;
}
int plat_slot_write(int slot, uint32_t off, const void *p, uint32_t n)
{
    if (flash_ok && plat_proj_of(slot) >= 0)
        return (off & 255u) || n > 256u ? -1 : ps_xwrite(plat_proj_of(slot), off, p, n);
    if (slot != xf_open || (off & 255u) || n > 256u || off + n > slot_room(slot) - 256u)
        return -1;
    return st_prog(slot_base(slot) + 256u + off, p, n) ? -1 : 0;
}
int plat_slot_end(int slot, uint32_t len, uint32_t crc)
{
    uint8_t buf[256];
    uint32_t base = slot_base(slot), off, c = 0xFFFFFFFFu;
    slot_head h, back;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_xend(plat_proj_of(slot), len, crc);
    if (slot != xf_open || len > slot_room(slot) - 256u)
        return -1;
    xf_open = -1;
    for (off = 0; off < len; off += 256u) {
        uint32_t k = len - off > 256u ? 256u : len - off;
        if (st_read(base + 256u + off, buf, k))
            return -1;
        c = xf_crc_upd(c, buf, k);
    }
    if (~c != crc)
        return -1;
    h.magic = SLOT_MAGIC;
    h.len = len;
    h.crc = crc;
    h.rsv = 0xFFFFFFFFu;
    if (st_prog(base, &h, sizeof h) || st_read(base, &back, sizeof back) || back.magic != SLOT_MAGIC || back.len != len || back.crc != crc)
        return -1;
    return 0;
}
int plat_slot_info(int slot, uint32_t *len, uint32_t *crc)
{
    slot_head h;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_info(plat_proj_of(slot), len, crc);
    if (!flash_ok || !slot_ok(slot) || st_read(slot_base(slot), &h, sizeof h) || h.magic != SLOT_MAGIC || h.len > slot_room(slot) - 256u)
        return -1;
    *len = h.len;
    *crc = h.crc;
    return 0;
}
int plat_slot_read(int slot, uint32_t off, void *p, uint32_t n)
{
    uint32_t len, crc;
    if (flash_ok && plat_proj_of(slot) >= 0)
        return ps_read(plat_proj_of(slot), off, p, n);
    if (plat_slot_info(slot, &len, &crc) || off + n > len)
        return -1;
    return st_read(slot_base(slot) + 256u + off, p, n) ? -1 : 0;
}
void plat_sysex_out(const uint8_t *p, uint32_t n)
{
#if FELUCCA_OTA
    ota_wire_send(p, n);
#else
    (void)p, (void)n;
#endif
}

/* no audio input on the bare-metal FM-1 (USB carries Ravel's output to the computer) */
int plat_audio_in(float *lr, int frames)
{
    (void)lr;
    (void)frames;
    return 0;
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets (byte 0 = cable / CIN) */
static int midi_age = -1;
int plat_midi_age(void) { return midi_age; }
int plat_midi_in(uint8_t msg[3])
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], cin = pkt & 15u;
        mi_r++;
        if (cin >= 0x8u && cin <= 0xEu) {      /* channel messages */
            msg[0] = (uint8_t)(pkt >> 8);
            msg[1] = (uint8_t)(pkt >> 16);
            msg[2] = (uint8_t)(pkt >> 24);
            return 1;
        }
        if (cin == 0xFu && (uint8_t)(pkt >> 8) >= 0xF8u) {   /* real time: clock, start, continue, stop */
            uint32_t age = (MIDI_STAMP() - (pkt >> 16)) & 0xFFFFu;   /* 10.67 us units */
            midi_age = age > 4096u ? 2048 : (int)((age * 482u) >> 10);   /* in samples (x 0.4704) */
            msg[0] = (uint8_t)(pkt >> 8);
            msg[1] = msg[2] = 0;
            return 1;
        }
    }
    return 0;
}
void plat_midi_out(const uint8_t *msg, int n)
{
    uint32_t cin;
    if (n < 1 || n > 3 || !(msg[0] & 0x80u))
        return;
    cin = (uint32_t)(msg[0] >> 4);
    midi_out_event(cin | ((uint32_t)msg[0] << 8) | ((uint32_t)(n > 1 ? msg[1] : 0) << 16) |
                   ((uint32_t)(n > 2 ? msg[2] : 0) << 24));
}

static volatile uint32_t audio_cpu_pct, audio_cpu_peak, audio_xruns;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_cpu_peak(void) { return audio_cpu_peak; }
int plat_flash_info(uint32_t v[4]) { v[0] = flash_jedec; v[1] = flash_sr; v[2] = flash_con; v[3] = flash_baud; return 0; }
uint32_t plat_xruns(void) { return audio_xruns; }
/* core 0's cycle counter (fm1_perf.h), if it moves; else TIMER4's 24 MHz ticks scaled to the 360 MHz clock */
static int8_t perf_ok = -1;
uint32_t plat_cycles(void)
{
    if (perf_ok < 0)
        perf_ok = (int8_t)fm1_perf_cycles_run();
    return perf_ok ? fm1_perf_cycles() : fm1_ticks() * (360u / 24u);
}
uint32_t plat_cycles_hz(void) { return 360000000u; }

/* the second core (fm1_cpu1.h), started when GLO > CPU asks for it. It starts in the chip's ROM
 * with its start word in the top of RAM, so both guards are opened for that and closed again. The
 * first time it fails (a wrong job read, or a job not finished) it is held until power-off and the
 * render does that work itself (fm1_cpu1_wait). Turned off between renders: idle then */
/* Ravel starts it at power-on. While it runs, a mark in the memory that survives a reset (not a
 * power-off) says so: after any reset with it running (a crash, a hang the watchdog caught, an update)
 * it stays off until the FM-1 is switched off and on, so a unit it does not suit never loops */
int plat_par_on(int on)
{
    static uint8_t started;
    int r;
    if (!on) {
        fm1_cpu1_hold();
        if (fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.barred == 2u)
            fm1_c1_bar.barred = 0;                  /* held on purpose: not running any more */
        return 0;
    }
    if (fm1_c1_on)
        return 0;
    if (!started && fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.barred == 2u)
        fm1_c1_bar.barred = 1;                      /* it was running when the FM-1 reset */
    if (fm1_c1_gave_up || (fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.barred == 1u))
        return -1;
    fm1_guard_unlock_top();
    fm1_guard_pc_open();
    r = fm1_cpu1_start();
    fm1_guard_enable(FM1_GUARD_PC);
    fm1_guard_lock_top();
    if (r == 0) {
        started = 1;
        fm1_c1_bar.magic = FM1_C1_BAR_MAGIC;
        fm1_c1_bar.barred = 2;                      /* running (see above) */
    }
    return r;
}
int plat_par_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
/* the instruction-fetch stall count (fm1_perf.h): enabled only when GLO > CPU asks (unverified on
 * hardware; the SDK's bits may also arm a fault) */
int plat_fetch_on(int on)
{
    fm1_perf_stalls_enable(on);
    return 0;
}
uint32_t plat_fetch_wait(void)
{
    uint32_t s[3];
    fm1_perf_stalls(s);
    return s[0];
}
int plat_par_wait(void) { return fm1_cpu1_wait(); }
void plat_par_sync(void) { fm1_cpu1_sync(); }
uint32_t plat_par_cycles(void) { return fm1_ticks() * (360u / 24u); }   /* (TIMER4: either core reads the same) */

/* the interrupts' stack (fm1_guard.h): filled with a mark before any interrupt runs (X0X) */
#define IRQ_STACK_MARK 0x49525153u                  /* "IRQS" */
static void irq_stack_mark(void)
{
    uint32_t *p = (uint32_t *)(void *)_sstack_lo, *top = (uint32_t *)(void *)_sstack_top - 16;
    uint32_t here = (uint32_t)(uintptr_t)&p;
    if (here > (uint32_t)(uintptr_t)p && here < (uint32_t)(uintptr_t)top)
        top = (uint32_t *)(uintptr_t)((here - 256u) & ~3u);
    for (; p < top; p++)
        *p = IRQ_STACK_MARK;
}
