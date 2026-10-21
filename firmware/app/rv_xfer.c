/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: projects and sounds to and from a computer, over USB-MIDI SysEx.
 *
 * Messages are framed as the updater's are (firmware/fm1/src/ota.c): F0, then 8-to-7 packed
 * (00 59 cmd len24 body ~sum(body)), then F7. The computer asks; Ravel answers each message with
 * the same command:
 *   0x60 INFO                         -> "RVX1", stores, then for each store: length (or FFFFFFFF), CRC-32
 *   0x61 READ  slot off32 n16         -> slot off32 status data[n]        (n <= 256)
 *   0x62 BEGIN slot len32             -> slot status                      (the slot is erased: empty until END)
 *   0x63 WRITE slot off32 data        -> slot off32 status                (256 bytes at 256-byte offsets)
 *   0x64 END   slot len32 crc32       -> slot status                      (checked; then the slot is valid)
 * Stores are the platform's slots: projects 0-6, sound banks 9-12 (plat.h). Everything is
 * little-endian; status 0 is ok. The data is what plat_save stores, so a project or a bank moves
 * between the emulator and an FM-1 unchanged, and is checked again when it is loaded. */
#include "rv.h"

#define XF_MAX 256u
static uint8_t xf_msg[8 + 8 + XF_MAX] RV_RAMBSS, xf_wire[2 + ((8 + 8 + XF_MAX) * 8 + 6) / 7] RV_RAMBSS;   /* (in RAM, outside the pool) */

static int xf_slot_ok(int s) { return (s >= 0 && s < PLAT_SLOTS) || (s >= PLAT_BANK0 && s < PLAT_STORES); }
static uint32_t xf_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void xf_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

static void xf_send(uint32_t cmd, uint32_t n)       /* the body is already in xf_msg + 6 */
{
    uint32_t i, s = 0, acc = 0, nb = 0, o = 1;
    xf_msg[0] = 0, xf_msg[1] = 0x59, xf_msg[2] = (uint8_t)cmd;
    xf_msg[3] = (uint8_t)n, xf_msg[4] = (uint8_t)(n >> 8), xf_msg[5] = (uint8_t)(n >> 16);
    for (i = 0; i < n; i++)
        s += xf_msg[6 + i];
    xf_msg[6 + n] = (uint8_t)~s;
    xf_wire[0] = 0xF0;
    for (i = 0; i < 7u + n; i++) {                  /* 8-to-7, LSB first */
        acc |= (uint32_t)xf_msg[i] << nb;
        nb += 8u;
        while (nb >= 7u) {
            xf_wire[o++] = (uint8_t)(acc & 0x7Fu);
            acc >>= 7;
            nb -= 7u;
        }
    }
    if (nb)
        xf_wire[o++] = (uint8_t)(acc & 0x7Fu);
    xf_wire[o++] = 0xF7;
    plat_sysex_out(xf_wire, o);
}

/* one decoded message (00 59 cmd len24 body chk), already checked */
void rv_xfer_msg(const uint8_t *m, uint32_t d)
{
    const uint8_t *b = m + 6;
    uint32_t cmd = m[2], n = d - 7u, i;
    uint8_t *r = xf_msg + 6;
    int slot = n ? b[0] : -1, st;
    switch (cmd) {
    case 0x60:
        rv_bank_settle();                               /* (a name being edited is kept first) */
        r[0] = 'R', r[1] = 'V', r[2] = 'X', r[3] = '1', r[4] = PLAT_STORES;
        for (i = 0; i < PLAT_STORES; i++) {
            uint32_t len = 0xFFFFFFFFu, crc = 0;
            if (xf_slot_ok((int)i) && plat_slot_info((int)i, &len, &crc))
                len = 0xFFFFFFFFu, crc = 0;
            xf_wr32(r + 5 + i * 8, len);
            xf_wr32(r + 9 + i * 8, crc);
        }
        xf_send(cmd, 5 + PLAT_STORES * 8);
        break;
    case 0x61: {
        uint32_t off = n >= 7 ? xf_rd32(b + 1) : 0, k = n >= 7 ? (uint32_t)b[5] | (uint32_t)b[6] << 8 : 0;
        st = n < 7 || !xf_slot_ok(slot) || k > XF_MAX || plat_slot_read(slot, off, r + 6, k) ? 1 : 0;
        r[0] = (uint8_t)slot;
        xf_wr32(r + 1, off);
        r[5] = (uint8_t)st;
        xf_send(cmd, 6 + (st ? 0 : k));
        break;
    }
    case 0x62:
        st = n < 5 || !xf_slot_ok(slot) ? 1 : plat_slot_begin(slot, xf_rd32(b + 1)) ? 2 : 0;
        if (!st)
            rv_transport(0);                            /* (writing the flash pauses the sound anyway) */
        r[0] = (uint8_t)slot, r[1] = (uint8_t)st;
        xf_send(cmd, 2);
        break;
    case 0x63: {
        uint32_t off = n >= 5 ? xf_rd32(b + 1) : 0;
        st = n < 5 || !xf_slot_ok(slot) || n - 5 > XF_MAX || (off & (XF_MAX - 1u)) ? 1 : plat_slot_write(slot, off, b + 5, n - 5) ? 2 : 0;
        r[0] = (uint8_t)slot;
        xf_wr32(r + 1, off);
        r[5] = (uint8_t)st;
        xf_send(cmd, 6);
        break;
    }
    case 0x64:
        st = n < 9 || !xf_slot_ok(slot) ? 1 : plat_slot_end(slot, xf_rd32(b + 1), xf_rd32(b + 5)) ? 3 : 0;
        r[0] = (uint8_t)slot, r[1] = (uint8_t)st;
        xf_send(cmd, 2);
        if (!st)
            ui_storage_changed(slot);                   /* the bank in RAM, the slot list, a message */
        break;
    default:
        break;
    }
}

/* a whole SysEx message as received (the 7-bit bytes between F0 and F7): checked, then handled.
 * (The FM-1 decodes with the updater's code and calls rv_xfer_msg itself.) */
#define xf_dec xf_wire                              /* (decoded in the reply's wire buffer: each command reads its input before it answers) */
int rv_xfer_wire(const uint8_t *p, uint32_t n)
{
    uint32_t acc = 0, nb = 0, d = 0, s = 0, i;
    while (n--) {
        acc |= (uint32_t)(*p++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) {
            if (d < sizeof xf_dec)
                xf_dec[d] = (uint8_t)acc;
            d++;
            acc >>= 8;
            nb -= 8u;
        }
    }
    if (d < 7u || d > sizeof xf_dec || xf_dec[0] != 0 || xf_dec[1] != 0x59 ||
        ((uint32_t)xf_dec[3] | (uint32_t)xf_dec[4] << 8 | (uint32_t)xf_dec[5] << 16) + 7u != d)
        return 0;
    for (i = 6; i + 1u < d; i++)
        s += xf_dec[i];
    if ((uint8_t)~s != xf_dec[d - 1u] || xf_dec[2] < 0x60 || xf_dec[2] > 0x64)
        return 0;
    rv_xfer_msg(xf_dec, d);
    return 1;
}
