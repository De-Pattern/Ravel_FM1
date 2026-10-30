/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the projects in the flash, compressed, as many as fit (PLAT_PROJECTS slots).
 *
 * The projects' area (0x97000 .. 0xCF000, 56 sectors of 4 KB, and three spare ones: 0xDB000 after the
 * sound banks, 0xDE000 and 0xDF000, unused) is a pool of sectors. A saved project
 * is a compressed stream (LZ, a 1 KB window: a project of 30 KB is about 1-2 KB, a busy one a few)
 * written across as many sectors as it needs, each with a 32-byte header naming the project, its
 * part, how many parts, and a sequence number. A save goes to free sectors and is only valid once
 * every part's header is written; the copy with the highest sequence number wins, so a save cut short
 * by a power loss leaves the last one in charge. Older versions' sectors are then free again.
 *
 * Projects saved before this (7 fixed slots of 32 KB at 0x97000 + p * 0x8000: a header "RVSL", the
 * data at 256) are read where they are, as projects 1-7. The first save compresses them into the pool,
 * one at a time, each freeing its 32 KB (the spare sectors make room for the first even when all seven
 * are full); a power loss in the middle leaves each where it was or where it went.
 *
 * Flash access is through st_read, st_erase and st_prog (ravel_fm1.c; the host test has its own).
 * Its state lives in the pool (PS_POOL). */
#include <stdint.h>

#ifndef PS_POOL
#define PS_POOL __attribute__((section(".pool")))
#endif
#define PS_BASE 0x00097000u
#define PS_SECTORS 59                      /* 56 in the area, then the three spare */
#define PS_SEC 4096u
#define PS_HDR 32u
#define PS_PAY (PS_SEC - PS_HDR)           /* 4064 bytes of stream a sector */
#define PS_MAGIC 0x32535652u               /* "RVS2" */
#define PS_OLD_MAGIC 0x4C535652u           /* "RVSL": the fixed slots */
#define PS_OLD_SLOTS 7
#define PS_OLD_SIZE 0x8000u
#define PS_MAXPARTS 9                      /* (a 30 KB project that will not compress at all: 8) */
#define PS_PROJ 32
#define PS_RAW_MAX 32768u
#define PS_WIN 1024u                       /* the LZ window: matches reach back at most this far */
#define PS_RING 2048u
#define PS_M (PS_RING - 1u)

typedef struct {
    uint32_t magic;
    uint8_t slot, part, nparts, comp;
    uint32_t seq, len, raw, crc, rcrc, hcrc;   /* len, crc: the stored stream's; raw, rcrc: the project's */
} ps_head;
typedef struct { uint32_t magic, len, crc, rsv; } ps_oldhead;
typedef char ps_head_fits[sizeof(ps_head) == PS_HDR ? 1 : -1];

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t ps_crc(uint32_t c, const uint8_t *b, uint32_t n)   /* zlib CRC-32, running (c: ~previous) */
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
static uint32_t ps_sec_addr(int k)
{
    static const uint32_t spare[3] = {0x000DB000u, 0x000DE000u, 0x000DF000u};
    return k < 56 ? PS_BASE + (uint32_t)k * PS_SEC : spare[(k - 56) % 3];
}

/* ------------------------------------------------------------------ the map --- */
typedef struct { uint8_t slot, part, nparts, ok; uint32_t seq, len, raw, crc, rcrc, comp; } ps_sinfo;
typedef struct { int8_t sec[PS_MAXPARTS]; uint8_t nparts, legacy, comp; uint32_t seq, len, raw, crc, rcrc; } ps_entry;
static ps_sinfo ps_si[PS_SECTORS] PS_POOL;
static ps_entry ps_e[PS_PROJ] PS_POOL;     /* each project's winning copy (nparts 0: empty) */
static uint8_t ps_used[PS_SECTORS] PS_POOL;
static uint32_t ps_seqmax;
static uint8_t ps_ready;
static uint8_t ps_cur_ok;                       /* the transfer's read cursor is good (a scan ends it) */

static void ps_scan(void)
{
    int k, p, q;
    ps_head h;
    ps_seqmax = 0;
    ps_cur_ok = 0;
    for (k = 0; k < PS_SECTORS; k++) {
        ps_sinfo *s = &ps_si[k];
        s->ok = 0;
        if (st_read(ps_sec_addr(k), &h, sizeof h))
            continue;
        if (h.magic != PS_MAGIC || ps_crc(0xFFFFFFFFu, (const uint8_t *)&h, sizeof h - 4u) != ~h.hcrc ||
            h.slot >= PS_PROJ || !h.nparts || h.nparts > PS_MAXPARTS || h.part >= h.nparts ||
            h.raw > PS_RAW_MAX || h.len > (uint32_t)h.nparts * PS_PAY || h.len + PS_PAY <= (uint32_t)h.nparts * PS_PAY)
            continue;
        s->ok = 1, s->slot = h.slot, s->part = h.part, s->nparts = h.nparts;
        s->seq = h.seq, s->len = h.len, s->raw = h.raw, s->crc = h.crc, s->rcrc = h.rcrc, s->comp = h.comp;
        if (h.seq > ps_seqmax)
            ps_seqmax = h.seq;
    }
    for (k = 0; k < PS_SECTORS; k++)
        ps_used[k] = 0;
    for (p = 0; p < PS_PROJ; p++) {               /* the highest sequence number whose parts are all there */
        ps_entry *e = &ps_e[p];
        uint32_t best = 0;
        int found = 0;
        e->nparts = 0;
        e->legacy = 0;
        for (k = 0; k < PS_SECTORS; k++) {
            const ps_sinfo *a = &ps_si[k];
            int8_t sec[PS_MAXPARTS];
            int have = 0;
            if (!a->ok || a->slot != p || a->part != 0 || (found && a->seq <= best))
                continue;
            for (q = 0; q < PS_MAXPARTS; q++)
                sec[q] = -1;
            for (q = 0; q < PS_SECTORS; q++) {
                const ps_sinfo *b = &ps_si[q];
                if (b->ok && b->slot == p && b->seq == a->seq && b->nparts == a->nparts && b->len == a->len &&
                    b->crc == a->crc && sec[b->part] < 0) {
                    sec[b->part] = (int8_t)q;
                    have++;
                }
            }
            if (have == a->nparts) {
                found = 1;
                best = a->seq;
                for (q = 0; q < PS_MAXPARTS; q++)
                    e->sec[q] = sec[q];
                e->nparts = a->nparts, e->comp = (uint8_t)a->comp, e->seq = a->seq;
                e->len = a->len, e->raw = a->raw, e->crc = a->crc, e->rcrc = a->rcrc;
            }
        }
        if (!found && p < PS_OLD_SLOTS) {         /* a project from the fixed slots, where it is */
            ps_oldhead o;
            int first = p * (int)(PS_OLD_SIZE / PS_SEC);
            if (!ps_si[first].ok && !st_read(ps_sec_addr(first), &o, sizeof o) && o.magic == PS_OLD_MAGIC &&
                o.len <= PS_OLD_SIZE - 256u) {
                int n = (int)((o.len + 256u + PS_SEC - 1u) / PS_SEC), clash = 0;
                for (q = 0; q < n; q++)
                    clash |= ps_si[first + q].ok;  /* (a sector of it reused: it is gone) */
                if (!clash) {
                    found = 1;
                    e->legacy = 1, e->comp = 0, e->nparts = (uint8_t)n, e->seq = 0;
                    e->len = e->raw = o.len, e->crc = e->rcrc = o.crc;
                    for (q = 0; q < PS_MAXPARTS; q++)
                        e->sec[q] = (int8_t)(q < n ? first + q : -1);
                }
            }
        }
        if (found)
            for (q = 0; q < e->nparts; q++)
                ps_used[(int)e->sec[q]] = 1;
    }
    ps_ready = 1;
}
static void ps_need(void)
{
    if (!ps_ready)
        ps_scan();
}
static int ps_free_count(void)
{
    int k, n = 0;
    ps_need();
    for (k = 0; k < PS_SECTORS; k++)
        n += !ps_used[k];
    return n;
}

/* --------------------------------------------------- reading a stored stream --- */
typedef struct {
    const ps_entry *e;
    uint32_t sp;                                   /* stream position */
    uint8_t buf[256];
    uint32_t bpos, bn;                             /* buf holds stream [bpos, bpos + bn) */
    int err;
} ps_rd;
static uint32_t ps_stream_addr(const ps_entry *e, uint32_t sp, uint32_t *room)
{
    if (e->legacy) {                               /* contiguous, after a 256-byte header */
        *room = e->len - sp;
        return ps_sec_addr(e->sec[0]) + 256u + sp;
    }
    *room = PS_PAY - sp % PS_PAY;
    return ps_sec_addr(e->sec[sp / PS_PAY]) + PS_HDR + sp % PS_PAY;
}
static int ps_rd_byte(ps_rd *r)
{
    if (r->sp >= r->e->len) {
        r->err = 1;
        return 0;
    }
    if (r->sp < r->bpos || r->sp >= r->bpos + r->bn) {
        uint32_t room, n = r->e->len - r->sp, a = ps_stream_addr(r->e, r->sp, &room);
        if (n > room)
            n = room;
        if (n > 256u)
            n = 256u;
        if (st_read(a, r->buf, n)) {
            r->err = 1;
            return 0;
        }
        r->bpos = r->sp, r->bn = n;
    }
    return r->buf[r->sp++ - r->bpos];
}

/* the decoder: produces the project a byte at a time, from the stream (or as it is, uncompressed) */
typedef struct {
    ps_rd r;
    uint8_t ring[PS_RING];
    uint32_t out, lit, mat, off;
    uint8_t phase, tok, slot;
    uint32_t seq;                                  /* (the copy it reads) */
    uint32_t scrc;                                 /* the stream's CRC, as it is read (running) */
} ps_dec;
static ps_dec ps_d PS_POOL;
static void ps_dec_start(ps_dec *d, int slot)
{
    d->r.e = &ps_e[slot];
    d->r.sp = 0, d->r.bpos = 0, d->r.bn = 0, d->r.err = 0;
    d->out = 0, d->lit = d->mat = d->off = 0, d->phase = 0;
    d->slot = (uint8_t)slot;
    d->seq = ps_e[slot].seq;
}
static int ps_dec_rd(ps_dec *d)
{
    return ps_rd_byte(&d->r);
}
static uint32_t ps_dec_ext(ps_dec *d, uint32_t n)
{
    int b;
    if (n != 15u)
        return n;
    do {
        b = ps_dec_rd(d);
        n += (uint32_t)b;
    } while (b == 255 && !d->r.err);
    return n;
}
/* the next n bytes of the project into dst (dst 0: skipped); 0, or -1 if the stream is wrong */
static int ps_dec_get(ps_dec *d, uint8_t *dst, uint32_t n)
{
    const ps_entry *e = d->r.e;
    while (n) {
        int b;
        if (d->out >= e->raw)
            return -1;
        if (!e->comp) {
            b = ps_dec_rd(d);
        } else {
            for (;;) {
                if (d->phase == 0) {               /* a token */
                    d->tok = (uint8_t)ps_dec_rd(d);
                    d->lit = ps_dec_ext(d, d->tok >> 4);
                    d->phase = d->lit ? 1 : 2;
                } else if (d->phase == 1) {        /* its literals */
                    if (d->lit)
                        break;
                    d->phase = 2;
                } else if (d->phase == 2) {        /* where the match reaches back to (0: none) */
                    if (d->out >= e->raw)
                        return -1;
                    d->off = (uint32_t)ps_dec_rd(d);
                    d->off |= (uint32_t)ps_dec_rd(d) << 8;
                    if (!d->off) {
                        d->phase = 0;
                        continue;
                    }
                    if (d->off > PS_WIN || d->off > d->out)
                        return -1;
                    d->mat = ps_dec_ext(d, d->tok & 15u) + 4u;
                    d->phase = 3;
                } else {                           /* the match */
                    if (d->mat)
                        break;
                    d->phase = 0;
                }
                if (d->r.err)
                    return -1;
            }
            if (d->phase == 1) {
                b = ps_dec_rd(d);
                d->lit--;
            } else {
                b = d->ring[(d->out - d->off) & PS_M];
                d->mat--;
            }
        }
        if (d->r.err)
            return -1;
        d->ring[d->out & PS_M] = (uint8_t)b;
        d->out++;
        if (dst)
            *dst++ = (uint8_t)b;
        n--;
    }
    return 0;
}

/* a copy as it reads back from the flash, decoded whole: is its project's CRC right? (0: yes) */
static int ps_verify(int slot)
{
    uint8_t b[256];
    uint32_t off, c = 0xFFFFFFFFu, raw = ps_e[slot].raw;
    ps_cur_ok = 0;
    ps_dec_start(&ps_d, slot);
    for (off = 0; off < raw; off += sizeof b) {
        uint32_t n = raw - off < sizeof b ? raw - off : (uint32_t)sizeof b;
        if (ps_dec_get(&ps_d, b, n))
            return -1;
        c = ps_crc(c, b, n);
    }
    return ~c == ps_e[slot].rcrc ? 0 : -1;
}

/* --------------------------------------------------------- writing a stream --- */
static struct {
    int8_t slot;
    uint8_t nparts;
    int8_t sec[PS_MAXPARTS];
    uint32_t pos, crc, seq;
    uint8_t page[256];
    uint32_t pn, ppos;                             /* page holds stream [ppos, ppos + pn) */
    int err;
} ps_w PS_POOL;
static int ps_alloc(void)
{
    int k, q;
    for (k = 0; k < PS_SECTORS; k++) {
        int mine = 0;
        if (ps_used[k])
            continue;
        for (q = 0; q < ps_w.nparts; q++)
            mine |= ps_w.sec[q] == k;
        if (!mine)
            return k;
    }
    return -1;
}
static void ps_w_flush(void)
{
    uint32_t done = 0;
    while (done < ps_w.pn && !ps_w.err) {
        uint32_t sp = ps_w.ppos + done, part = sp / PS_PAY, in = sp % PS_PAY, k = PS_PAY - in;
        if (k > ps_w.pn - done)
            k = ps_w.pn - done;
        if (part >= PS_MAXPARTS) {
            ps_w.err = -2;
            break;
        }
        if (part >= ps_w.nparts) {                 /* the next sector: a free one, erased */
            int s = ps_alloc();
            if (s < 0) {
                ps_w.err = -2;                     /* (full) */
                break;
            }
            ps_w.sec[ps_w.nparts++] = (int8_t)s;
            if (st_erase(ps_sec_addr(s))) {
                ps_w.err = -1;
                break;
            }
        }
        if (st_prog(ps_sec_addr(ps_w.sec[part]) + PS_HDR + in, ps_w.page + done, k))
            ps_w.err = -1;
        done += k;
    }
    ps_w.ppos += ps_w.pn;
    ps_w.pn = 0;
}
static void ps_w_byte(uint8_t b)
{
    ps_w.page[ps_w.pn++] = b;
    ps_w.crc = ps_crc(ps_w.crc, &b, 1);
    ps_w.pos++;
    if (ps_w.pn == sizeof ps_w.page)
        ps_w_flush();
}
static void ps_w_begin(int slot)
{
    ps_need();
    ps_w.slot = (int8_t)slot;
    ps_w.nparts = 0;
    ps_w.pos = 0, ps_w.crc = 0xFFFFFFFFu, ps_w.pn = 0, ps_w.ppos = 0, ps_w.err = 0;
    ps_w.seq = ps_seqmax + 1u;
}
/* every part's header, then the map again; the copy it replaces is let go (its first header erased,
 * so it can never come back) */
static int ps_w_commit(uint32_t raw, uint32_t rcrc)
{
    ps_head h;
    ps_entry old;
    int q, slot = ps_w.slot;
    ps_w_flush();
    if (ps_w.err)
        return ps_w.err;
    if (!ps_w.nparts)                              /* (an empty stream still takes a sector) */
        return -1;
    old = ps_e[slot];
    h.magic = PS_MAGIC;
    h.slot = (uint8_t)slot, h.nparts = ps_w.nparts, h.comp = 1;
    h.seq = ps_w.seq, h.len = ps_w.pos, h.raw = raw, h.crc = ~ps_w.crc, h.rcrc = rcrc;
    for (q = 0; q < ps_w.nparts; q++) {
        h.part = (uint8_t)q;
        h.hcrc = ~ps_crc(0xFFFFFFFFu, (const uint8_t *)&h, sizeof h - 4u);
        if (st_prog(ps_sec_addr(ps_w.sec[q]), &h, sizeof h))
            return -1;
    }
    ps_scan();
    if (ps_e[slot].nparts != ps_w.nparts || ps_e[slot].seq != ps_w.seq)
        return -1;                                 /* (it did not read back) */
    if (ps_verify(slot)) {                         /* the data did not read back: this copy goes, the old one stays */
        st_erase(ps_sec_addr(ps_w.sec[0]));
        ps_scan();
        return -1;
    }
    if (old.nparts) {
        int first = old.sec[0], used = 0;
        for (q = 0; q < ps_e[slot].nparts; q++)
            used |= ps_e[slot].sec[q] == first;
        if (!used)
            st_erase(ps_sec_addr(first));
        ps_scan();
    }
    return 0;
}

/* --------------------------------------------- the compressor (streaming LZ) --- */
/* Sequences as LZ4's block format has them: a token (literals << 4 | match length - 4), the literals,
 * then a 16-bit offset back and the match. An offset of 0 means literals only. The input is fed in
 * pieces of any size (a whole project from RAM, or a transfer's 256 bytes at a time). */
#define PS_LOOK 280u                               /* input kept ahead of the position being coded */
#define PS_MAXM 270u
static struct {
    uint8_t ring[PS_RING];
    uint32_t tab[1024];                            /* the last position + 1 each 4-byte hash was seen */
    uint32_t in, pos, anchor;
} ps_z PS_POOL;
static void ps_z_begin(void)
{
    uint32_t i;
    ps_z.in = ps_z.pos = ps_z.anchor = 0;
    for (i = 0; i < 1024u; i++)
        ps_z.tab[i] = 0;
}
static void ps_z_len(uint32_t n)
{
    while (n >= 255u) {
        ps_w_byte(255);
        n -= 255u;
    }
    ps_w_byte((uint8_t)n);
}
static void ps_z_emit(uint32_t off, uint32_t m, int last)   /* the literals [anchor, pos), then the match */
{
    uint32_t l = ps_z.pos - ps_z.anchor, i, ml = off ? m - 4u : 0u;
    ps_w_byte((uint8_t)((l >= 15u ? 15u : l) << 4 | (ml >= 15u ? 15u : ml)));
    if (l >= 15u)
        ps_z_len(l - 15u);
    for (i = 0; i < l; i++)
        ps_w_byte(ps_z.ring[(ps_z.anchor + i) & PS_M]);
    if (last)
        return;
    ps_w_byte((uint8_t)off);
    ps_w_byte((uint8_t)(off >> 8));
    if (off && ml >= 15u)
        ps_z_len(ml - 15u);
}
static uint32_t ps_z_hash(uint32_t p)
{
    uint32_t v = (uint32_t)ps_z.ring[p & PS_M] | (uint32_t)ps_z.ring[(p + 1u) & PS_M] << 8 |
                 (uint32_t)ps_z.ring[(p + 2u) & PS_M] << 16 | (uint32_t)ps_z.ring[(p + 3u) & PS_M] << 24;
    return (v * 2654435761u) >> 22;
}
static void ps_z_run(int final)
{
    while (final ? ps_z.pos < ps_z.in : ps_z.in - ps_z.pos > PS_LOOK) {
        uint32_t m = 0, c = 0;
        if (ps_z.in - ps_z.pos >= 4u) {
            uint32_t h = ps_z_hash(ps_z.pos), t = ps_z.tab[h];
            ps_z.tab[h] = ps_z.pos + 1u;
            if (t && ps_z.pos - (t - 1u) <= PS_WIN) {
                c = t - 1u;
                while (m < PS_MAXM && ps_z.pos + m < ps_z.in && ps_z.ring[(c + m) & PS_M] == ps_z.ring[(ps_z.pos + m) & PS_M])
                    m++;
            }
        }
        if (m >= 4u) {
            ps_z_emit(ps_z.pos - c, m, 0);
            ps_z.pos += m;
            ps_z.anchor = ps_z.pos;
        } else {
            ps_z.pos++;
            if (ps_z.pos - ps_z.anchor >= 512u) {   /* (literals only, before they leave the ring) */
                ps_z_emit(0, 0, 0);
                ps_z.anchor = ps_z.pos;
            }
        }
    }
    if (final && ps_z.anchor < ps_z.pos)
        ps_z_emit(0, 0, 1);
}
static void ps_z_feed(const uint8_t *p, uint32_t n)
{
    while (n--) {
        if (ps_z.in - ps_z.pos >= PS_RING - PS_WIN - 2u)
            ps_z_run(0);
        ps_z.ring[ps_z.in++ & PS_M] = *p++;
    }
}

/* a transfer from a computer in progress (ps_xbegin .. ps_xend): it has the writer; anything else that
 * writes ends it first (its next piece then fails), so the two never share the writer */
static struct { int8_t slot; uint32_t len, next, crc; } ps_x = {-1, 0, 0, 0};
static uint8_t ps_mig_fail[PS_OLD_SLOTS];          /* an old slot that did not fit: the free sectors then, + 1 */

/* a project still in the old fixed slots, compressed into the pool (its 32 KB then free); 1 if one was.
 * One that did not fit is tried again only once there is more room than there was. */
static int ps_migrate_one(int except)
{
    int p;
    for (p = 0; p < PS_OLD_SLOTS; p++) {
        ps_entry e;
        uint32_t off;
        uint8_t b[256];
        int bad = 0, room;
        ps_need();
        e = ps_e[p];
        if (p == except || !e.nparts || !e.legacy)
            continue;
        room = ps_free_count();
        if (ps_mig_fail[p] && room < ps_mig_fail[p])
            continue;
        ps_w_begin(p);
        ps_z_begin();
        for (off = 0; off < e.raw && !bad; off += sizeof b) {
            uint32_t n = e.raw - off < sizeof b ? e.raw - off : (uint32_t)sizeof b;
            bad = st_read(ps_sec_addr(e.sec[0]) + 256u + off, b, n) != 0;
            ps_z_feed(b, n);
        }
        if (!bad) {
            ps_z_run(1);
            if (!ps_w_commit(e.raw, e.rcrc)) {
                ps_mig_fail[p] = 0;
                return 1;
            }
        }
        ps_mig_fail[p] = (uint8_t)(room + 1);
        ps_scan();                                 /* (it did not fit: what it took is free again) */
    }
    return 0;
}
static void ps_migrate_all(int except)
{
    while (ps_migrate_one(except))
        ;
}

/* ----------------------------------------------------------------- the API --- */
/* (slot: the project, 0 .. PS_PROJ - 1) */
static int ps_load(int slot, void *dst, uint32_t max)
{
    const ps_entry *e;
    uint32_t n;
    ps_need();
    e = &ps_e[slot];
    if (!e->nparts)
        return -1;
    n = e->raw < max ? e->raw : max;
    ps_cur_ok = 0;                                 /* (a transfer's cursor starts again) */
    ps_dec_start(&ps_d, slot);
    if (ps_dec_get(&ps_d, (uint8_t *)dst, n))
        return -1;
    if (n == e->raw && ps_crc(0xFFFFFFFFu, (const uint8_t *)dst, n) != ~e->rcrc)
        return -1;
    return (int)e->raw;
}
static int ps_save(int slot, const void *src, uint32_t len)
{
    int rc;
    if (len > PS_RAW_MAX)
        return -1;
    ps_x.slot = -1;                                /* (a transfer left open ends here) */
    ps_migrate_all(slot);                          /* (the old fixed slots into the pool while there is room for it;
                                                    * nothing to do once they have been) */
    for (;;) {
        ps_w_begin(slot);
        ps_z_begin();
        ps_z_feed((const uint8_t *)src, len);
        ps_z_run(1);
        rc = ps_w_commit(len, ~ps_crc(0xFFFFFFFFu, (const uint8_t *)src, len));
        if (rc)
            ps_scan();                             /* (whatever it took is free again) */
        if (rc != -2 || !ps_migrate_one(slot))     /* full: an old fixed slot compressed, then again */
            return rc;
    }
}
/* a transfer: the pieces come in order, are compressed as they come, and the copy is made valid
 * at the end only if all of it came and its CRC is right */
static int ps_xbegin(int slot, uint32_t len)
{
    ps_x.slot = -1;
    if (len > PS_RAW_MAX)
        return -1;
    ps_migrate_all(slot);                          /* (all the room there can be, first) */
    ps_w_begin(slot);
    ps_z_begin();
    ps_x.slot = (int8_t)slot, ps_x.len = len, ps_x.next = 0, ps_x.crc = 0xFFFFFFFFu;
    return 0;
}
static int ps_xwrite(int slot, uint32_t off, const void *p, uint32_t n)
{
    if (slot != ps_x.slot || off != ps_x.next || off + n > ps_x.len)
        return -1;
    ps_z_feed((const uint8_t *)p, n);
    ps_x.crc = ps_crc(ps_x.crc, (const uint8_t *)p, n);
    ps_x.next += n;
    return ps_w.err ? -1 : 0;
}
static int ps_xend(int slot, uint32_t len, uint32_t crc)
{
    int rc;
    if (slot != ps_x.slot || len != ps_x.len || ps_x.next != len || ~ps_x.crc != crc) {
        ps_x.slot = -1;
        ps_scan();
        return -1;
    }
    ps_x.slot = -1;
    ps_z_run(1);
    rc = ps_w_commit(len, crc);
    if (rc)
        ps_scan();
    return rc;
}
/* a project cleared: every sector that names it erased, the first parts first (so a power loss in the
 * middle leaves no copy whole), or an old fixed slot's header; its room is free again. Only this
 * project's sectors are touched: each one's header names it. 0, -1 (empty, or the flash failed) */
static int ps_clear(int slot)
{
    int k, pass, rc = 0;
    ps_x.slot = -1;                                /* (a transfer left open ends here) */
    ps_need();
    if (!ps_e[slot].nparts)
        return -1;
    for (pass = 0; pass < 2; pass++)
        for (k = 0; k < PS_SECTORS; k++)
            if (ps_si[k].ok && ps_si[k].slot == slot && (ps_si[k].part == 0) == (pass == 0))
                rc |= st_erase(ps_sec_addr(k));
    if (ps_e[slot].legacy)                         /* (its sectors hold no pool header: the clash test in ps_scan) */
        rc |= st_erase(ps_sec_addr(ps_e[slot].sec[0]));
    ps_scan();
    return rc || ps_e[slot].nparts ? -1 : 0;
}
static int ps_info(int slot, uint32_t *len, uint32_t *crc)
{
    ps_need();
    if (!ps_e[slot].nparts)
        return -1;
    *len = ps_e[slot].raw;
    *crc = ps_e[slot].rcrc;
    return 0;
}
/* a transfer reading a project: in order, from a cursor kept between the pieces */
static int ps_read(int slot, uint32_t off, void *p, uint32_t n)
{
    ps_need();
    if (!ps_e[slot].nparts || off + n > ps_e[slot].raw)
        return -1;
    if (!ps_cur_ok || ps_d.slot != slot || off < ps_d.out || ps_d.seq != ps_e[slot].seq) {
        ps_dec_start(&ps_d, slot);
        ps_cur_ok = 1;
    }
    if ((off > ps_d.out && ps_dec_get(&ps_d, 0, off - ps_d.out)) || ps_dec_get(&ps_d, (uint8_t *)p, n)) {
        ps_cur_ok = 0;
        return -1;
    }
    return 0;
}
