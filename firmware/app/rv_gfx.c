/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: drawing. The screen is drawn in bands of GB_H rows: the UI paints the whole frame
 * once per band, every primitive clipped to it, and each band is sent to the panel as soon as
 * it is done. No full-screen buffer: 19 KB instead of 113 KB.
 * Fonts: Barlow Semi Condensed, 2-bit anti-aliased (tools/gen_rv_font.py). */
#include "rv.h"

typedef struct {
    uint8_t h, first, last;
    const uint8_t *adv;
    const int8_t *bx;          /* where the bitmap starts, from the pen */
    const uint8_t *bw;         /* bitmap width; rows of (bw + 3) / 4 bytes, 2 bits a pixel */
    const uint16_t *off;
    const uint8_t *data;
} rv_font_t;
#include "rv_font.h"

#define GB_H 16
static uint16_t gb[PLAT_W * GB_H];
static int g_y0, g_y1;

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
/* Silt's colours: a dark green-black ground, light ink, teal for live movement, amber for memory and the tape */
#define C_BG RGB(14, 18, 16)
#define C_PANEL RGB(28, 35, 31)
#define C_LINE RGB(48, 58, 52)
#define C_DIM RGB(86, 100, 91)
#define C_MID RGB(140, 153, 143)
#define C_INK RGB(212, 220, 213)
#define C_WHITE RGB(244, 247, 244)
#define C_AMBER RGB(214, 154, 88)
#define C_AMBER_D RGB(110, 80, 48)
#define C_TEAL RGB(98, 184, 203)
#define C_TEAL_D RGB(42, 84, 94)
#define C_RED RGB(222, 92, 80)
#define C_TRIG RGB(236, 206, 108)
#define C_EXC RGB(228, 112, 74)
#define C_CLOCK RGB(160, 132, 222)
#define C_RING RGB(222, 122, 182)
#define C_SYNTH RGB(112, 192, 206)
#define C_STRIKE RGB(226, 134, 94)
#define C_TAPE RGB(214, 154, 88)

static void g_band(int y0)
{
    g_y0 = y0;
    g_y1 = y0 + GB_H;
}

static void g_send(void) { plat_blit(0, g_y0, PLAT_W, GB_H, gb); }

static inline void g_pset(int x, int y, uint16_t c)
{
    if ((unsigned)x < PLAT_W && y >= g_y0 && y < g_y1)
        gb[(y - g_y0) * PLAT_W + x] = c;
}

static void g_fill(int x, int y, int w, int h, uint16_t c)
{
    int x1 = x + w, y1 = y + h, i, j;
    if (x < 0)
        x = 0;
    if (x1 > PLAT_W)
        x1 = PLAT_W;
    if (y < g_y0)
        y = g_y0;
    if (y1 > g_y1)
        y1 = g_y1;
    for (j = y; j < y1; j++) {
        uint16_t *p = gb + (j - g_y0) * PLAT_W;
        for (i = x; i < x1; i++)
            p[i] = c;
    }
}

static void g_hline(int x, int y, int w, uint16_t c) { g_fill(x, y, w, 1, c); }
static void g_vline(int x, int y, int h, uint16_t c) { g_fill(x, y, 1, h, c); }

static void g_frame(int x, int y, int w, int h, uint16_t c)
{
    g_hline(x, y, w, c);
    g_hline(x, y + h - 1, w, c);
    g_vline(x, y, h, c);
    g_vline(x + w - 1, y, h, c);
}

static void g_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, guard = 1000;
    if ((y0 < g_y0 && y1 < g_y0) || (y0 >= g_y1 && y1 >= g_y1))
        return;
    while (guard--) {
        int e2 = 2 * err;
        g_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void g_disc(int cx, int cy, int r, uint16_t c)
{
    int y, x;
    if (cy + r < g_y0 || cy - r >= g_y1)
        return;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r)
                g_pset(cx + x, cy + y, c);
}

static inline uint16_t g_blend(uint16_t bg, uint16_t fg, uint32_t a)   /* a 0..15 */
{
    uint32_t r = ((fg >> 11) * a + (bg >> 11) * (15u - a)) / 15u;
    uint32_t g = (((fg >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u;
    uint32_t b = ((fg & 31u) * a + (bg & 31u) * (15u - a)) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t g_glyph(const rv_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && (f->last < 'a' || !f->adv[ch - f->first]))
        ch -= 32u;
    if (ch < f->first || ch > f->last || f->adv[ch - f->first] == 0)
        ch = ch == ' ' ? ' ' : '-';
    if (ch < f->first || ch > f->last)
        ch = f->first;
    return ch - f->first;
}

static const uint8_t alpha2[4] = {0, 5, 10, 15};

static int g_text(int x, int y, const rv_font_t *f, const char *s, uint16_t c)
{
    if (y + f->h < g_y0 || y >= g_y1) {
        for (; *s; s++)
            x += f->adv[g_glyph(f, (uint8_t)*s)];
        return x;
    }
    for (; *s; s++) {
        uint32_t gi = g_glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 3u) / 4u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h && w; gy++) {
            int py = y + (int)gy;
            uint16_t *row;
            if (py < g_y0 || py >= g_y1)
                continue;
            row = gb + (py - g_y0) * PLAT_W;
            for (gx = 0; gx < w; gx++) {
                uint32_t a = (gd[gy * bpr + gx / 4u] >> (6u - 2u * (gx & 3u))) & 3u;
                int px = x + f->bx[gi] + (int)gx;
                if (!a || (unsigned)px >= PLAT_W)
                    continue;
                row[px] = a == 3u ? c : g_blend(row[px], c, alpha2[a]);
            }
        }
        x += f->adv[gi];
    }
    return x;
}

static int g_text_w(const rv_font_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++)
        w += f->adv[g_glyph(f, (uint8_t)*s)];
    return w;
}

static void g_text_c(int cx, int y, const rv_font_t *f, const char *s, uint16_t c) { g_text(cx - g_text_w(f, s) / 2, y, f, s, c); }
static void g_text_r(int rx, int y, const rv_font_t *f, const char *s, uint16_t c) { g_text(rx - g_text_w(f, s), y, f, s, c); }

static uint16_t g_mix(uint16_t a, uint16_t b, float t)
{
    int k = (int)(t * 15.0f + 0.5f);
    if (k < 0)
        k = 0;
    if (k > 15)
        k = 15;
    return g_blend(a, b, (uint32_t)k);
}
