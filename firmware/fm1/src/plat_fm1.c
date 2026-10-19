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

/* projects: eight slots of 32 KB in Felucca's data area (0x97000 .. 0xD7000; the panel calibration
 * is at 0xDC000, the updater's staging at 0xE0000). A header, then the data; the header is written
 * last, so a slot cut short by a power loss reads as empty. Writing pauses the sound (the code runs
 * from the same flash). */
#define SLOT_BASE 0x00097000u
#define SLOT_SIZE 0x8000u
#define SLOT_MAGIC 0x4C535652u                  /* "RVSL" */
typedef struct { uint32_t magic, len, crc, rsv; } slot_head;

int plat_load(int slot, void *dst, uint32_t max)
{
    slot_head h;
    uint32_t base = SLOT_BASE + (uint32_t)slot * SLOT_SIZE, n;
    if (!flash_ok || slot < 0 || slot >= PLAT_SLOTS)
        return -1;
    if (st_read(base, &h, sizeof h) || h.magic != SLOT_MAGIC || h.len > SLOT_SIZE - 256u)
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
    uint32_t base = SLOT_BASE + (uint32_t)slot * SLOT_SIZE, off;
    if (!flash_ok || slot < 0 || slot >= PLAT_SLOTS || len > SLOT_SIZE - 256u)
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

/* no audio input on the bare-metal FM-1 (USB carries Ravel's output to the computer) */
int plat_audio_in(float *lr, int frames)
{
    (void)lr;
    (void)frames;
    return 0;
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets (byte 0 = cable / CIN) */
int plat_midi_in(uint8_t msg[3])
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], cin = pkt & 15u;
        mi_r++;
        if (cin >= 0x8u && cin <= 0xEu) {      /* channel messages only */
            msg[0] = (uint8_t)(pkt >> 8);
            msg[1] = (uint8_t)(pkt >> 16);
            msg[2] = (uint8_t)(pkt >> 24);
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

static volatile uint32_t audio_cpu_pct, audio_xruns;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }

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
