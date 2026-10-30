/* SPDX-License-Identifier: GPL-3.0-only */
/* The project store (firmware/fm1/src/projstore.c) on a simulated NOR flash: projects from the old
 * fixed slots are read where they are; saves round-trip; the store fills and says so without losing
 * anything; transfers in pieces round-trip and read back in order; and a power loss at any flash
 * operation of a save leaves either the old copy or the new one, never neither, and no other project
 * touched.   cc -O2 -o projstore_test tests/fm1/projstore_test.c && ./projstore_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PS_POOL
static uint8_t flash[0x100000];
static long budget = -1;
static int bad_prog;                               /* a program that silently drops a bit (in a stream byte) */
static int in_pool(uint32_t off, uint32_t n) { return (off >= 0x97000u && off + n <= 0xCF000u) || off >> 12 == 0xDBu || off >> 12 == 0xDEu || off >> 12 == 0xDFu; }                           /* flash operations left before the "power loss" (-1: none) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, flash + off, n); return 0; }
static int st_erase(uint32_t off)
{
    if (budget == 0) return -1;
    if (budget > 0) budget--;
    if (!in_pool(off, 4096u) || (off & 4095u)) { printf("FAIL erase outside the pool %x\n", off); exit(1); }
    memset(flash + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (budget == 0) return -1;
    if (budget > 0) budget--;
    if (!in_pool(off, n) || (off >> 12) != ((off + n - 1u) >> 12)) { printf("FAIL program outside the pool %x\n", off); exit(1); }
    for (i = 0; i < n; i++)
        flash[off + i] &= ((const uint8_t *)src)[i];   /* NOR: bits only clear */
    if (bad_prog && n > 64 && (off & 4095u) >= 32u)
        flash[off + 10] &= 0xFE, bad_prog = 0;
    return 0;
}
#include "../../firmware/fm1/src/projstore.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define RAW 30240u
static uint8_t proj[PS_PROJ][RAW], tmp[RAW + 64];
static uint32_t rng = 1;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void make(uint8_t *p, int kind)             /* project-like (a 12-byte step repeated, some edits), or noise */
{
    uint32_t i;
    for (i = 0; i < RAW; i++)
        p[i] = kind ? (uint8_t)rnd() : (uint8_t)((i % 12u) == 1 ? 60 : (i % 12u) == 2 ? 100 : 0);
    if (!kind)
        for (i = 0; i < 200; i++)
            p[rnd() % RAW] = (uint8_t)rnd();
}
static int same(int slot, const uint8_t *want)
{
    ps_ready = 0;                                  /* (as after a restart: the map from the flash) */
    return ps_load(slot, tmp, sizeof tmp) == (int)RAW && !memcmp(tmp, want, RAW);
}

int main(void)
{
    int p, i, k;
    uint8_t old[RAW];
    memset(flash, 0xFF, sizeof flash);
    /* the old fixed slots: 7 projects */
    for (p = 0; p < 7; p++) {
        ps_oldhead h = {PS_OLD_MAGIC, RAW, 0, 0xFFFFFFFFu};
        make(proj[p], 0);
        h.crc = ~ps_crc(0xFFFFFFFFu, proj[p], RAW);
        memcpy(flash + PS_BASE + p * 0x8000u, &h, sizeof h);
        memcpy(flash + PS_BASE + p * 0x8000u + 256u, proj[p], RAW);
    }
    for (p = 0; p < 7; p++)
        CHECK(same(p, proj[p]), "old project %d read where it is", p + 1);
    CHECK(ps_free_count() == 3, "the old slots fill the area; the 3 spare sectors are free (%d)", ps_free_count());
    /* saving project 2 again: compressed into the spare room, its old 32 KB let go */
    proj[1][5000] ^= 0x55;
    CHECK(ps_save(1, proj[1], RAW) == 0, "save project 2");
    CHECK(same(1, proj[1]), "project 2 reads back");
    ps_ready = 0;
    printf("project 2 took %d sector(s), %u bytes; %d free now\n", ps_e[1].nparts, ps_e[1].len, ps_free_count());
    CHECK(ps_free_count() >= 9, "its old 8 sectors came back (%d free)", ps_free_count());
    for (p = 0; p < 7; p++)
        CHECK(same(p, proj[p]), "project %d still there", p + 1);
    /* fill: project-like ones into slots 7.. until full */
    for (p = 7; p < PS_PROJ; p++) {
        make(proj[p], 0);
        k = ps_save(p, proj[p], RAW);
        if (k) { CHECK(k == -2, "a full store says so (%d)", k); break; }
    }
    ps_ready = 0;
    printf("project-like saves: %d projects stored, %d free; old fixed slots left: %d\n", p, ps_free_count(),
           ps_e[0].legacy + ps_e[2].legacy + ps_e[3].legacy + ps_e[4].legacy + ps_e[5].legacy + ps_e[6].legacy);
    for (i = 0; i < p; i++)
        CHECK(same(i, proj[i]), "project %d intact after filling", i + 1);
    /* noise (30 KB that will not compress) where there is no room: refused, nothing lost */
    /* noise (30 KB that will not compress: 8 sectors) over projects until it no longer fits: each
     * save either lands whole or is refused, and nothing else changes */
    for (k = 0, i = 0; i < 8; i++) {
        make(old, 1);
        k = ps_save(10 + i, old, RAW);
        if (k == 0)
            memcpy(proj[10 + i], old, RAW);
        else
            break;
    }
    ps_ready = 0;
    printf("noise projects stored: %d, then %d (%d free)\n", i, k, ps_free_count());
    CHECK(k == -2, "the store fills and says so");
    for (i = 0; i < p; i++)
        CHECK(same(i, proj[i]), "project %d intact after a refused save", i + 1);
    /* a transfer in 256-byte pieces, then read back in pieces, in order and out of order */
    proj[4][123] ^= 1;
    CHECK(ps_xbegin(4, RAW) == 0, "transfer begins");
    for (i = 0; i < (int)RAW; i += 256)
        CHECK(ps_xwrite(4, (uint32_t)i, proj[4] + i, RAW - (uint32_t)i < 256u ? RAW - (uint32_t)i : 256u) == 0, "piece %d", i);
    CHECK(ps_xend(4, RAW, ~ps_crc(0xFFFFFFFFu, proj[4], RAW)) == 0, "transfer ends");
    CHECK(same(4, proj[4]), "transferred project reads back");
    {
        uint32_t len, crc;
        CHECK(ps_info(4, &len, &crc) == 0 && len == RAW && crc == ~ps_crc(0xFFFFFFFFu, proj[4], RAW), "info: length and CRC of the project");
        for (i = 0; i < (int)RAW; i += 256) {
            uint32_t n = RAW - (uint32_t)i < 256u ? RAW - (uint32_t)i : 256u;
            CHECK(ps_read(4, (uint32_t)i, tmp, n) == 0 && !memcmp(tmp, proj[4] + i, n), "read piece %d", i);
        }
        CHECK(ps_read(4, 1000, tmp, 100) == 0 && !memcmp(tmp, proj[4] + 1000, 100), "read again from earlier");
        CHECK(ps_read(0, 0, tmp, 64) == 0 && !memcmp(tmp, proj[0], 64), "read another project");
        CHECK(ps_xbegin(4, RAW) == 0 && ps_xwrite(4, 0, proj[4], 256) == 0 && ps_xend(4, RAW, 0) != 0 && same(4, proj[4]),
              "a transfer cut short leaves the old copy");
    }
    /* a save on the FM-1 while a transfer is open ends the transfer: the saved project is safe */
    {
        uint8_t mine[RAW];
        memcpy(mine, proj[8], RAW);
        mine[77] ^= 9;
        CHECK(ps_xbegin(5, RAW) == 0, "transfer begins");
        for (i = 0; i < 15 * 1024; i += 256)
            ps_xwrite(5, (uint32_t)i, proj[5] + i, 256);
        CHECK(ps_save(8, mine, RAW) == 0, "a save in the middle of it");
        memcpy(proj[8], mine, RAW);
        CHECK(ps_xwrite(5, 15 * 1024, proj[5] + 15 * 1024, 256) != 0, "the transfer's next piece is refused");
        CHECK(ps_xend(5, RAW, ~ps_crc(0xFFFFFFFFu, proj[5], RAW)) != 0, "and it does not end");
        CHECK(same(8, proj[8]) && same(5, proj[5]), "both projects as they should be");
    }
    /* a copy whose data did not program right is caught before the old one is let go */
    {
        uint8_t next[RAW];
        static uint8_t save_flash[sizeof flash];
        memcpy(next, proj[6], RAW);
        next[100] ^= 1;
        memcpy(save_flash, flash, sizeof flash);
        bad_prog = 1;
        k = ps_save(6, next, RAW);
        bad_prog = 0;
        CHECK(k == -1 && same(6, proj[6]), "a bad program: refused (%d), the old copy kept", k);
    }
    /* clearing a project: gone (after a restart too), its room back, the others untouched; then saved again */
    {
        int before = ps_free_count(), used = ps_e[9].nparts;
        CHECK(used > 0, "project 10 is there to clear");
        CHECK(ps_clear(9) == 0, "clear project 10");
        ps_ready = 0;
        CHECK(ps_load(9, tmp, sizeof tmp) < 0, "project 10 is gone after a restart");
        CHECK(ps_free_count() == before + used, "its %d sectors are free again (%d, was %d)", used, ps_free_count(), before);
        for (i = 0; i < p; i++)
            if (i != 9)
                CHECK(same(i, proj[i]), "project %d untouched by the clear", i + 1);
        CHECK(ps_clear(9) == -1, "clearing an empty project says so");
        CHECK(ps_save(9, proj[9], RAW) == 0 && same(9, proj[9]), "project 10 saved again");
    }
    /* power loss at every flash operation of a clear: the project whole or gone, nothing else touched */
    {
        static uint8_t save_flash[sizeof flash];
        int ops, cut;
        memcpy(save_flash, flash, sizeof flash);
        ps_ready = 0;
        budget = 1000000;
        ps_clear(9);
        ops = (int)(1000000 - budget);
        budget = -1;
        memcpy(flash, save_flash, sizeof flash);
        for (cut = 0; cut <= ops; cut++) {
            ps_ready = 0;
            budget = cut;
            ps_clear(9);
            budget = -1;
            ps_ready = 0;
            k = ps_load(9, tmp, sizeof tmp);
            CHECK(k < 0 || same(9, proj[9]), "cut after %d of a clear: project 10 half there", cut);
            for (i = 0; i < p; i++)
                if (i != 9)
                    CHECK(same(i, proj[i]), "cut after %d of a clear: project %d touched", cut, i + 1);
            memcpy(flash, save_flash, sizeof flash);
        }
        ps_ready = 0;
    }
    /* power loss at every flash operation of a save: the old copy or the new, nothing else touched */
    {
        uint8_t next[RAW];
        int ops, cut;
        memcpy(next, proj[2], RAW);
        for (i = 0; i < 300; i++) next[rnd() % RAW] = (uint8_t)rnd();
        budget = -1;
        {   /* count the operations a save takes */
            static uint8_t save_flash[sizeof flash];
            memcpy(save_flash, flash, sizeof flash);
            budget = 1000000;
            ps_save(2, next, RAW);
            ops = (int)(1000000 - budget);
            memcpy(flash, save_flash, sizeof flash);
            budget = -1;
        }
        printf("a save takes %d flash operations\n", ops);
        for (cut = 0; cut <= ops; cut++) {
            static uint8_t save_flash[sizeof flash];
            int ok_old, ok_new;
            memcpy(save_flash, flash, sizeof flash);
            ps_ready = 0;
            budget = cut;
            ps_save(2, next, RAW);
            budget = -1;
            ok_old = same(2, proj[2]);
            ok_new = same(2, next);
            CHECK(ok_old || ok_new, "cut after %d operations: project 3 is neither old nor new", cut);
            for (i = 0; i < p; i++)
                if (i != 2)
                    CHECK(same(i, proj[i]), "cut after %d: project %d touched", cut, i + 1);
            memcpy(flash, save_flash, sizeof flash);
        }
    }
    /* power loss at every flash operation of the first save after an update (it moves the seven old
     * slots into the pool first): every project is still readable, as it was */
    {
        static uint8_t base[sizeof flash], next[RAW];
        int ops, cut;
        memset(flash, 0xFF, sizeof flash);
        for (p = 0; p < 7; p++) {
            ps_oldhead h = {PS_OLD_MAGIC, RAW, 0, 0xFFFFFFFFu};
            make(proj[p], p == 3);                 /* (one of them noise: it needs 8 sectors in the pool too) */
            h.crc = ~ps_crc(0xFFFFFFFFu, proj[p], RAW);
            memcpy(flash + PS_BASE + p * 0x8000u, &h, sizeof h);
            memcpy(flash + PS_BASE + p * 0x8000u + 256u, proj[p], RAW);
        }
        memcpy(base, flash, sizeof flash);
        make(next, 0);
        ps_ready = 0;
        budget = 1000000;
        CHECK(ps_save(20, next, RAW) == 0, "the first save after an update");
        ops = (int)(1000000 - budget);
        budget = -1;
        ps_ready = 0;
        printf("the first save (moving 7 old slots) takes %d flash operations; %d free after\n", ops, ps_free_count());
        for (p = 0; p < 7; p++)
            CHECK(same(p, proj[p]) && !ps_e[p].legacy, "old project %d moved", p + 1);
        for (cut = 0; cut < ops; cut += 3) {
            memcpy(flash, base, sizeof flash);
            ps_ready = 0;
            budget = cut;
            ps_save(20, next, RAW);
            budget = -1;
            for (p = 0; p < 7; p++)
                CHECK(same(p, proj[p]), "cut after %d: old project %d lost", cut, p + 1);
        }
        memcpy(flash, base, sizeof flash);
    }
    /* clearing a project still in an old fixed slot: gone, the other old ones still read where they are */
    {
        ps_ready = 0;
        CHECK(ps_e[2].legacy || (ps_need(), ps_e[2].legacy), "project 3 is in its old slot");
        CHECK(ps_clear(2) == 0, "clear old project 3");
        ps_ready = 0;
        CHECK(ps_load(2, tmp, sizeof tmp) < 0, "old project 3 is gone after a restart");
        for (p = 0; p < 7; p++)
            if (p != 2)
                CHECK(same(p, proj[p]) && ps_e[p].legacy, "old project %d still where it was", p + 1);
        make(proj[2], 0);
        CHECK(ps_save(2, proj[2], RAW) == 0 && same(2, proj[2]), "project 3 saved again (the old ones moving in)");
        for (p = 0; p < 7; p++)
            CHECK(same(p, proj[p]), "project %d after that save", p + 1);
    }
    printf(fails ? "%d FAILED\n" : "projstore: all ok\n", fails);
    return fails != 0;
}
