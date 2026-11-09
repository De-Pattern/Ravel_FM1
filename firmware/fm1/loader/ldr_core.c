/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader, the portable part (step 2 of the M-UPGRADE update
 * protocol).
 *
 * The host serves the package ("logical image") with cmd 0x30 reads (ota.c).
 * The loader:
 *   1. reads the UFW header and finds flash.bin;
 *   2. checks that the package's app area decrypts with THIS chip's key
 *      (taken from isd_config.ini in the device's own flash head), so a
 *      package for another key is refused before anything is erased;
 *   3. writes ONLY the app area [0x4000, 0x93000), sector by sector, skipping
 *      sectors that are already equal, and verifies each one; the flash head
 *      (SPL, isd_config) is never written;
 *   4. asks 0xF0000000 ("success"), then invalidates the update record (RAM
 *      and flash) so the SPL boots the new app, and resets.
 * Power loss during 3: the record is still there, the SPL runs the loader
 * again on the next power-on and the host can resume.
 *
 * Hooks from the platform (loader.c, tests/ldr_test.c):
 *   ldr_fread(off, p, n)  ldr_erase(off)  ldr_prog(off, p, n)   flash, 0 = ok
 *   ldr_record_clear()    forget the RAM update record
 *   ldr_progress(done, total)
 * plus the ota.c hooks (frames, time, idle).
 *
 * LDR_TO_STOCK (ota-stock.bin, used only by the install page's "Back to the stock
 * firmware"): the package is M-VAVE's own flash.bin with this loader in place of
 * M-VAVE's. If the unit's flash head (boot area, 16 KB) isn't the package's (Ravel
 * installed from stock brings its own), it is written too, as M-VAVE's loader does
 * on every update, but only if the package's head carries this chip's key and the
 * flash has no write protection set, and last: after the app area and the settings,
 * so a power loss before it still resumes from the record (the old head boots this
 * loader again). After the app area,
 * the settings areas stock keeps its VM in are put back to how a factory unit has
 * them: [0x94000, 0xE9000) and Felucca's globals [0xFC000, 0xFF000) erased, the
 * factory sector 0x93000, the Bluetooth data 0xE9000 and the factory voices
 * 0xEA000..0xFBFFF never touched. The staged loader [0xE0000, 0xE5000) goes last,
 * after the record, so a power loss before that still resumes. */
#define LDR_APP_LO 0x4000u
#define LDR_APP_HI 0x93000u
#define LDR_REC_LO 0x93000u                     /* update records live above the app ... */
#define LDR_REC_HI 0xFC000u                     /* ... and below Felucca's globals */
#ifdef LDR_TO_STOCK
#define LDR_STAGE_LO 0xE0000u                   /* the staged loader and its record */
#define LDR_STAGE_HI 0xE5000u
static int ldr_wipe(uint32_t lo, uint32_t hi);
static void ldr_records_erase(void);
static uint32_t ldr_flash_sr(void);             /* SR1 | SR2 << 8 (loader.c) */
#define LDR_HEAD 0x4000u
#define LDR_SR_PROTECT 0x407Cu                  /* BP0-BP4 (SR1 bits 2-6), CMP (SR2 bit 6) */
static uint8_t ldr_head[LDR_HEAD];              /* the package's head, when it is to be written */
static int ldr_head_rd(uint32_t off, void *p, uint32_t n)
{
    if (off > LDR_HEAD || n > LDR_HEAD - off)
        return -1;
    {
        uint8_t *d = p;
        uint32_t i;
        for (i = 0; i < n; i++)
            d[i] = ldr_head[off + i];
    }
    return 0;
}
#define LDR_REFUSED 1                           /* ldr_session: refused before anything was written */
#endif

static int ldr_fread(uint32_t off, void *p, uint32_t n);
static int ldr_erase(uint32_t off);
static int ldr_prog(uint32_t off, const void *p, uint32_t n);
static void ldr_record_clear(void);
static void ldr_progress(uint32_t done, uint32_t total);

/* SFC cipher: per 32-byte block, key ^ (block offset >> 2) */
static void ldr_sfc(uint8_t *p, uint32_t n, uint32_t base_off, uint32_t key)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 32u) {
        uint32_t k = key ^ ((base_off + i) >> 2);
        for (j = 0; j < 32u && i + j < n; j++) {
            p[i + j] ^= (uint8_t)k;
            k = ((k << 1) ^ (k & 0x8000u ? 0x1021u : 0u)) & 0xFFFFu;
        }
    }
}

/* chip key from the device's flash head: JLFS top entries (ENC 0xFFFF) -> isd_config.ini blob */
#ifndef LDR_TO_STOCK
static int ldr_chip_key(uint32_t *key)
{
#else
#define ldr_chip_key(key) ldr_key_of(ldr_fread, key)
static int ldr_key_of(int (*ldr_fread)(uint32_t, void *, uint32_t), uint32_t *key)   /* (a head's reader) */
{
#endif
    uint8_t e[32], blob[34];
    uint32_t off, i, sum, k;
    for (off = 32; off < 0x400u; off += 32u) {
        if (ldr_fread(off, e, 32))
            return -1;
        ota_jl_enc(e, 32);
        if (ota_crc16(e + 2, 30, 0) != ota_rd16(e))
            return -2;
        if (!ota_memeq(e + 16, (const uint8_t *)"isd_config.ini", 15)) {
            if (ota_rd16(e + 14))
                return -3;                               /* last entry, not found */
            continue;
        }
        if (ldr_fread(ota_rd32(e + 4), blob, 34) || ota_crc16(blob, 32, 0) != ota_rd16(blob + 32))
            return -4;
        for (i = 0, sum = 0; i < 16u; i++)
            sum += blob[i];
        sum &= 0xFFu;
        sum = sum >= 0xE0u ? 0xAAu : sum <= 0x10u ? 0x55u : sum;
        for (i = 0, k = 0; i < 16u; i++)
            if ((uint32_t)(blob[16 + i] ^ blob[15 - i]) < sum)
                k |= 1u << i;
        *key = k;
        return 0;
    }
    return -3;
}

static int ldr_session(void)
{
    static uint8_t hdr[0x400], sec[0x1000], cur[0x1000];
    uint32_t i, k, fl_off, fl_size, ota_off, ota_len, key, s, done = 0;
    int rc;
    /* 1. UFW header + entry list (ota.c) */
    if ((rc = ota_ufw(hdr, &fl_off, &fl_size, &ota_off, &ota_len)) != 0)
        return rc;
    if (!fl_off || fl_size < LDR_APP_HI)
        return -3;
#ifdef LDR_TO_STOCK
    if (fl_size != LDR_APP_HI) {                         /* V15's flash.bin is exactly the app area's end */
        ldr_records_erase();                             /* (V16's is longer: not something to cut short) */
        return LDR_REFUSED;
    }
#endif
    /* 2. the package's app area must decrypt with this chip's key */
#ifndef LDR_TO_STOCK
    if ((rc = ldr_chip_key(&key)) != 0)
        return -40 + rc;
#else
    for (k = 0; k < LDR_HEAD; k += 512u)                 /* the package's head (2b, 3c) */
        if (ota_read(fl_off + k, ldr_head + k, 512))
            return -12;
    if ((rc = ldr_chip_key(&key)) != 0) {
        /* a head this loader was writing when the power went (3c): part the package's, so the key is
         * the package head's (checked against the app area below, as always) */
        uint32_t part = 0;
        for (s = 0; s < LDR_HEAD; s += 0x1000u)
            if (!ldr_fread(s, cur, 0x1000u) && ota_memeq(ldr_head + s, cur, 0x1000u))
                part = 1;
        if (!part || ldr_key_of(ldr_head_rd, &key))
            return -40 + rc;
    }
#endif
    if (ota_read(fl_off + LDR_APP_LO, sec, 32))
        return -5;
    ldr_sfc(sec, 32, 0, key);
    if (ota_crc16(sec + 2, 30, 0) != ota_rd16(sec)) {
#ifdef LDR_TO_STOCK
        ldr_records_erase();                             /* (refused: the next power-on runs the app, not this) */
        return LDR_REFUSED;
#endif
        return -6;                                       /* package for another chip key */
    }
#ifdef LDR_TO_STOCK
    /* 2b. the package's head: if the unit's differs, it is written last (3c), and only if it carries
     *     this chip's key and nothing protects the flash; if not, nothing is written and the record
     *     goes, so the unit doesn't come back into this loader: loader.c restarts it into what it had */
    {
        int head_new = 0;
        uint32_t hkey;
        for (k = 0; k < LDR_HEAD; k += 512u) {
            if (ldr_fread(k, cur, 512))
                return -12;
            if (!ota_memeq(ldr_head + k, cur, 512))
                head_new = 1;
        }
        if (head_new && (ldr_key_of(ldr_head_rd, &hkey) || hkey != key || (ldr_flash_sr() & LDR_SR_PROTECT))) {
            ldr_records_erase();
            return LDR_REFUSED;                          /* a head for another chip, or protected flash */
        }
    }
#endif
    /* 3. app area, sector by sector */
    for (s = LDR_APP_LO; s < LDR_APP_HI; s += 0x1000u) {
        for (k = 0; k < 0x1000u; k += 512u)
            if (ota_read(fl_off + s + k, sec + k, 512))
                return -7;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (!ota_memeq(sec, cur, 0x1000u)) {
            uint32_t tries;
            for (tries = 0; tries < 2u; tries++) {
                if (ldr_erase(s))
                    return -9;
                for (k = 0; k < 0x1000u; k += 256u)
                    if (ldr_prog(s + k, sec + k, 256))
                        return -10;
                if (ldr_fread(s, cur, 0x1000u))
                    return -8;
                if (ota_memeq(sec, cur, 0x1000u))
                    break;
            }
            if (tries == 2u)
                return -11;                              /* verify failed twice */
        }
        done += 0x1000u;
        ldr_progress(done, LDR_APP_HI - LDR_APP_LO);
    }
#ifdef LDR_TO_STOCK
    /* 3b. stock's settings areas as on a factory unit (the record's sector stays for now) */
    if (ldr_wipe(LDR_APP_HI + 0x1000u, LDR_STAGE_LO) || ldr_wipe(LDR_STAGE_HI, 0xE9000u) ||
        ldr_wipe(0xFC000u, 0xFF000u))
        return -14;
    /* 3c. the head, last (see 2b), sector by sector, verified; a sector that already matches is left */
    for (s = 0; s < LDR_HEAD; s += 0x1000u) {
        uint32_t tries;
        if (ldr_fread(s, cur, 0x1000u))
            return -8;
        if (ota_memeq(ldr_head + s, cur, 0x1000u))
            continue;
        for (tries = 0; tries < 3u; tries++) {
            if (ldr_erase(s))
                continue;
            for (k = 0; k < 0x1000u; k += 256u)
                if (ldr_prog(s + k, ldr_head + s + k, 256))
                    break;
            if (!ldr_fread(s, cur, 0x1000u) && ota_memeq(ldr_head + s, cur, 0x1000u))
                break;
        }
        if (tries == 3u)
            return -15;                                  /* (the record stays: power on resumes this) */
        ldr_progress(0, 0);
    }
#endif
    /* 4. finish: the host confirms, the record goes, the SPL boots the app */
    for (i = 0; i < 4u; i++)
        if (!ota_read(0xF0000000u, sec, 8))
            break;
    ldr_record_clear();
    for (s = LDR_REC_HI; s > LDR_REC_LO; s -= 0x1000u) {   /* flash records: 4K boundary - 256 */
        uint8_t r[80];
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0))
            ldr_erase(s - 0x1000u);
    }
#ifdef LDR_TO_STOCK
    ldr_wipe(LDR_STAGE_LO, LDR_STAGE_HI);                /* the staged loader: nothing to resume now */
#endif
    return 0;
}

#ifdef LDR_TO_STOCK
static void ldr_records_erase(void)                      /* the update record, RAM and flash */
{
    uint32_t s;
    ldr_record_clear();
    for (s = LDR_REC_HI; s > LDR_REC_LO; s -= 0x1000u) {
        uint8_t r[80];
        if (ldr_fread(s - 0x100u, r, 80))
            break;
        if (ota_rd16(r + 6) == 0x5441u && ota_rd16(r) && ota_rd16(r) == ota_crc16(r + 2, 78, 0))
            ldr_erase(s - 0x1000u);
    }
}

static int ldr_wipe(uint32_t lo, uint32_t hi)            /* erase the sectors in [lo, hi) not yet blank */
{
    static uint8_t b[0x1000];
    uint32_t s, i, used;
    for (s = lo; s < hi; s += 0x1000u) {
        if (ldr_fread(s, b, 0x1000u))
            return -1;
        for (i = 0, used = 0; i < 0x1000u && !used; i++)
            used = b[i] != 0xFFu;
        if (used && ldr_erase(s))
            return -1;
        ldr_progress(0, 0);
    }
    return 0;
}
#endif
