/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the loader's way back to stock (ldr_core.c built with LDR_TO_STOCK,
 * ota-stock.bin): the "device" is a unit with STOCK's flash head, Ravel installed and
 * Ravel's data in flash; the host serves STOCK's flash.bin with this loader.
 * The result must be what restoring STOCK with a rescue tool and putting the settings
 * areas back to factory state gives (MvaveFM1Unbricker's restore + repair-settings).
 *   ldr_stock_test RAVEL.fwsc STOCK.fwsc [OTHER_STOCK.fwsc] */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "ota-FM-1_900"
#define FELUCCA_OTA_DRYRUN 0
#define LDR_TO_STOCK 1

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, bad_range, record_cleared, f0_asked, cut_after = 0xFFFFFFFFu;

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../../firmware/fm1/src/ota.c"

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int in(uint32_t a, uint32_t lo, uint32_t hi) { return a >= lo && a < hi; }
static int ldr_erase(uint32_t off)
{
    if (off & 0xFFFu) bad_range++;
    if (!in(off, 0x4000u, 0x93000u) && !in(off, 0x94000u, 0xE9000u) && !in(off, 0xFC000u, 0xFF000u)) {
        printf("ERASE OUTSIDE THE WINDOWS %#x\n", off);
        bad_range++;
        return -1;
    }
    if (erases == cut_after) return -1;          /* "power loss": the session stops here */
    erases++;
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > 0x93000u || ((off & 0xFFu) + n) > 256u) bad_range++;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../../firmware/fm1/loader/ldr_core.c"

static uint8_t *load_logical(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    lg = malloc(n);
    for (i = 0; i < 20; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + 20 * 47, raw + 20 * 48, n - 20 * 48);
    *len = n - 20;
    free(raw);
    return lg;
}
static uint32_t flash_off(const uint8_t *lg)
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}
static int check(const char *what, int ok) { printf("%-60s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static uint8_t start[0x100000], want[0x100000];
static void fill(uint32_t lo, uint32_t hi, uint32_t seed)       /* data that isn't blank */
{
    uint32_t a;
    for (a = lo; a < hi; a++) start[a] = (uint8_t)((a * 2654435761u + seed) >> 13);
}
static void device(const uint8_t *ravel, uint32_t rfo, const uint8_t *stock, uint32_t sfo)
{
    memset(start, 0xFF, sizeof start);
    memcpy(start, stock + sfo, 0x4000);                  /* the unit's head: stock's */
    memcpy(start + 0x4000, ravel + rfo + 0x4000, 0x93000 - 0x4000);   /* Ravel installed */
    fill(0x93000, 0x94000, 1); memcpy(start + 0x93000, "\x55\xAA\xAA\x55", 4);  /* factory sector */
    fill(0x94000, 0x97000, 2);                           /* stock's own settings from before */
    fill(0x97000, 0xC0000, 3);                           /* Ravel's store */
    fill(0xE0000, 0xE4000, 4);                           /* the staged loader */
    fill(0xE5000, 0xE9000, 5);                           /* stock settings */
    fill(0xE9000, 0xEA000, 6);                           /* Bluetooth data */
    fill(0xEA000, 0xFC000, 7);                           /* factory voices */
    fill(0xFC000, 0xFD000, 8);                           /* Ravel's globals */
    fill(0xFF000, 0x100000, 9);                          /* key_mac */
    {
        uint8_t r[112] = {0};
        r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
        ota_wr16(r, ota_crc16(r + 2, 78, 0));
        memcpy(start + 0xE4F00, r, sizeof r);
    }
    memcpy(nor, start, sizeof nor);
    /* what a rescue tool + repair-settings leaves: stock's app area, the settings windows blank */
    memcpy(want, start, sizeof want);
    memcpy(want + 0x4000, stock + sfo + 0x4000, 0x93000 - 0x4000);
    memset(want + 0x94000, 0xFF, 0xE9000 - 0x94000);
    memset(want + 0xFC000, 0xFF, 0xFF000 - 0xFC000);
}

int main(int argc, char **argv)
{
    size_t rlen;
    uint8_t *ravel;
    uint32_t rfo, sfo, total_erases;
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_stock_test RAVEL.fwsc STOCK.fwsc [OTHER_STOCK.fwsc]\n"); return 2; }
    ravel = load_logical(argv[1], &rlen);
    logical = load_logical(argv[2], &logical_len);
    rfo = flash_off(ravel);
    sfo = flash_off(logical);

    device(ravel, rfo, logical, sfo);
    rc = ldr_session();
    total_erases = erases;
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes", rc == 0);
    bad += check("app area == stock's flash.bin", !memcmp(nor + 0x4000, logical + sfo + 0x4000, 0x93000 - 0x4000));
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, start, 0x4000));
    bad += check("factory sector 0x93000 untouched", !memcmp(nor + 0x93000, start + 0x93000, 0x1000));
    bad += check("stock settings 0x94000..0xE8FFF erased (Ravel's store too)", !memcmp(nor + 0x94000, want + 0x94000, 0xE9000 - 0x94000));
    bad += check("staged loader + record 0xE0000..0xE4FFF erased", nor[0xE0000] == 0xFF && nor[0xE4F06] == 0xFF);
    bad += check("Bluetooth data, factory voices untouched", !memcmp(nor + 0xE9000, start + 0xE9000, 0xFC000 - 0xE9000));
    bad += check("Ravel's globals 0xFC000..0xFEFFF erased", !memcmp(nor + 0xFC000, want + 0xFC000, 0x3000));
    bad += check("key_mac 0xFF000.. untouched", !memcmp(nor + 0xFF000, start + 0xFF000, 0x1000));
    bad += check("whole flash == rescue tool + repair-settings", !memcmp(nor, want, sizeof nor));
    bad += check("finish asked (0xF0000000), record cleared", f0_asked >= 1 && record_cleared);
    bad += check("no write outside the allowed windows", bad_range == 0);

    erases = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);

    /* power lost part way (in the app area, then in the settings), switched on again: resumes */
    {
        uint32_t cuts[2] = {total_erases / 4, total_erases * 3 / 4}, c;   /* app area, then settings */
        for (c = 0; c < 2; c++) {
            char what[80];
            device(ravel, rfo, logical, sfo);
            erases = 0; cut_after = cuts[c]; f0_asked = 0;
            rc = ldr_session();
            int stopped = rc != 0 && f0_asked == 0 && nor[0xE4F06] == 0x41 && nor[0xE0000] == start[0xE0000];
            cut_after = 0xFFFFFFFFu; erases = 0;
            rc = ldr_session();
            snprintf(what, sizeof what, "power lost after %u erases: record kept, resume completes", cuts[c]);
            bad += check(what, stopped && rc == 0 && !memcmp(nor, want, sizeof nor));
        }
    }

    /* a unit whose head is not this stock's: refused before anything is erased */
    {
        device(ravel, rfo, logical, sfo);
        nor[0x1234] ^= 0x01;
        erases = 0; f0_asked = 0;
        rc = ldr_session();
        bad += check("head from another version: refused (-13), nothing erased", rc == -13 && erases == 0 && f0_asked == 0);
    }
    if (argc > 3) {                                      /* e.g. V16 offered to a V15-head unit */
        size_t olen;
        uint8_t *other = load_logical(argv[3], &olen), *keep = logical;
        size_t keep_len = logical_len;
        device(ravel, rfo, logical, sfo);
        logical = other; logical_len = olen;
        erases = 0; f0_asked = 0;
        rc = ldr_session();
        bad += check("another stock version's package: refused, nothing erased", rc < 0 && erases == 0 && !memcmp(nor, start, sizeof nor));
        logical = keep; logical_len = keep_len;
    }
    /* a package for another chip key */
    {
        uint8_t save = logical[sfo + 0x4000 + 5];
        device(ravel, rfo, logical, sfo);
        logical[sfo + 0x4000 + 5] ^= 0x5A;
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[sfo + 0x4000 + 5] = save;
    }
    printf("%s\n", bad ? "STOCK LOADER TEST FAILED" : "stock loader test passed");
    return bad != 0;
}
