/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: drawing. The screen is drawn in bands of GB_H rows: the UI paints the whole frame
 * once per band, every primitive clipped to it, and each band is sent to the panel as soon as
 * it is done. No full-screen buffer: 19 KB instead of 113 KB.
 * Fonts: Space Grotesk, 2-bit anti-aliased (tools/gen_rv_font.py). Lines and discs are anti-aliased
 * (each pixel covered by how much of the stroke falls on it), so the drawing reads as drawn. */
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
/* a deep black ground, quiet greys, light ink; teal for live movement, amber for memory and the tape */
#define C_BG RGB(6, 7, 8)
#define C_PANEL RGB(20, 22, 25)
#define C_LINE RGB(30, 33, 37)
#define C_DIM RGB(78, 82, 86)
#define C_MID RGB(141, 144, 147)
#define C_INK RGB(236, 236, 234)
#define C_WHITE RGB(255, 255, 255)
/* the four knobs' colours: the part of a drawing a knob moves is drawn in its colour */
#define C_K1 RGB(91, 143, 214)
#define C_K2 RGB(93, 190, 142)
#define C_K3 RGB(230, 230, 226)
#define C_K4 RGB(158, 136, 206)            /* a pale, deep purple */
/* the perc tracks keep the earlier greens (their lanes and machines are drawn as they were) */
#define C_O_PANEL RGB(28, 35, 31)
#define C_O_LINE RGB(48, 58, 52)
#define C_O_DIM RGB(86, 100, 91)
#define C_O_MID RGB(140, 153, 143)
/* the track kinds' badges: the colours they always had, deeper */
#define C_SYNTH_D RGB(44, 104, 115)
#define C_STRIKE_D RGB(134, 73, 46)
#define C_TAPE_D RGB(126, 98, 48)
#define C_PERC_D RGB(120, 37, 50)
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
#define C_PERC RGB(196, 62, 76)            /* a deep red, apart from the tape's amber and the strike's orange */

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

static void g_seg(float x0, float y0, float x1, float y1, float w, uint16_t c);
static void g_line_px(int x0, int y0, int x1, int y1, uint16_t c);
/* lines are drawn smooth: a stroke 1.3 pixels wide (straight ones stay one pixel, crisp) */
static void g_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x0 == x1 || y0 == y1) {
        g_line_px(x0, y0, x1, y1, c);
        return;
    }
    if ((y0 < g_y0 - 2 && y1 < g_y0 - 2) || (y0 > g_y1 + 1 && y1 > g_y1 + 1))
        return;
    g_seg((float)x0 + 0.5f, (float)y0 + 0.5f, (float)x1 + 0.5f, (float)y1 + 0.5f, 1.3f, c);
}
static void g_line_px(int x0, int y0, int x1, int y1, uint16_t c)
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

static void g_discf(float cx, float cy, float r, uint16_t c);
static void g_disc(int cx, int cy, int r, uint16_t c)
{
    if (cy + r < g_y0 || cy - r >= g_y1)
        return;
    g_discf((float)cx + 0.5f, (float)cy + 0.5f, (float)r + 0.3f, c);
}

static inline uint16_t g_blend(uint16_t bg, uint16_t fg, uint32_t a)   /* a 0..15 */
{
    uint32_t r = ((fg >> 11) * a + (bg >> 11) * (15u - a)) / 15u;
    uint32_t g = (((fg >> 5) & 63u) * a + ((bg >> 5) & 63u) * (15u - a)) / 15u;
    uint32_t b = ((fg & 31u) * a + (bg & 31u) * (15u - a)) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* a pixel covered a (0..1) by colour c */
static inline void g_cover(int x, int y, uint16_t c, float a)
{
    uint16_t *p;
    if ((unsigned)x >= PLAT_W || y < g_y0 || y >= g_y1 || a <= 0.03f)
        return;
    p = &gb[(y - g_y0) * PLAT_W + x];
    *p = a >= 0.97f ? c : g_blend(*p, c, (uint32_t)(a * 15.0f + 0.5f));
}

/* a stroke from (x0, y0) to (x1, y1), w pixels wide, round-ended, anti-aliased */
static void g_seg(float x0, float y0, float x1, float y1, float w, uint16_t c)
{
    float r = w * 0.5f, dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy, ext = r + 1.0f;
    int xa = (int)(fm_minf(x0, x1) - ext), xb = (int)(fm_maxf(x0, x1) + ext) + 1;
    int ya = (int)(fm_minf(y0, y1) - ext), yb = (int)(fm_maxf(y0, y1) + ext) + 1, x, y;
    if (ya < g_y0)
        ya = g_y0;
    if (yb > g_y1)
        yb = g_y1;
    if (xa < 0)
        xa = 0;
    if (xb > PLAT_W)
        xb = PLAT_W;
    for (y = ya; y < yb; y++)
        for (x = xa; x < xb; x++) {
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0, k = l2 > 1e-6f ? (px * dx + py * dy) / l2 : 0.0f, ex, ey, d2;
            k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
            ex = px - k * dx;
            ey = py - k * dy;
            d2 = ex * ex + ey * ey;
            if (d2 < ext * ext) {
                float a = r + 0.5f - fm_sqrtf(d2);
                if (a > 0.0f) {
                    uint16_t *p = &gb[(y - g_y0) * PLAT_W + x];
                    if (a >= 1.0f)
                        *p = c;
                    else
                        *p = g_blend(*p, c, (uint32_t)(a * 15.0f + 0.5f));
                }
            }
        }
}

/* a filled disc and a ring, anti-aliased (centre and radius in pixels, as floats) */
static void g_discf(float cx, float cy, float r, uint16_t c)
{
    int x, y;
    for (y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
        for (x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            g_cover(x, y, c, r + 0.5f - fm_sqrtf(dx * dx + dy * dy));
        }
}
static void g_ring(float cx, float cy, float r, float w, uint16_t c)
{
    int x, y;
    for (y = (int)(cy - r - w); y <= (int)(cy + r + w); y++)
        for (x = (int)(cx - r - w); x <= (int)(cx + r + w); x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy) - r;
            g_cover(x, y, c, w * 0.5f + 0.5f - (d < 0.0f ? -d : d));
        }
}

/* a rounded box: filled, or its outline one pixel wide (r: the corners' radius) */
static void g_rbox(int x, int y, int w, int h, int r, uint16_t c, int filled)
{
    int j;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (y + h <= g_y0 || y >= g_y1)
        return;
    if (filled) {
        g_fill(x, y + r, w, h - 2 * r, c);
        g_fill(x + r, y, w - 2 * r, r, c);
        g_fill(x + r, y + h - r, w - 2 * r, r, c);
        for (j = 0; j < 4; j++) {                     /* the corners: quarter discs */
            float cx = (float)(x + ((j & 1) ? w - r : r)), cy = (float)(y + ((j & 2) ? h - r : r));
            int xx, yy, x0 = (j & 1) ? x + w - r : x, y0 = (j & 2) ? y + h - r : y;
            for (yy = y0; yy < y0 + r; yy++)
                for (xx = x0; xx < x0 + r; xx++) {
                    float dx = (float)xx + 0.5f - cx, dy = (float)yy + 0.5f - cy;
                    g_cover(xx, yy, c, (float)r + 0.5f - fm_sqrtf(dx * dx + dy * dy));
                }
        }
    } else {
        g_fill(x + r, y, w - 2 * r, 1, c);
        g_fill(x + r, y + h - 1, w - 2 * r, 1, c);
        g_fill(x, y + r, 1, h - 2 * r, c);
        g_fill(x + w - 1, y + r, 1, h - 2 * r, c);
        for (j = 0; j < 4; j++) {
            float cx = (float)(x + ((j & 1) ? w - r : r)), cy = (float)(y + ((j & 2) ? h - r : r));
            int xx, yy, x0 = (j & 1) ? x + w - r : x, y0 = (j & 2) ? y + h - r : y;
            for (yy = y0; yy < y0 + r; yy++)
                for (xx = x0; xx < x0 + r; xx++) {
                    float dx = (float)xx + 0.5f - cx, dy = (float)yy + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy) - ((float)r - 0.5f);
                    g_cover(xx, yy, c, 1.0f - (d < 0.0f ? -d : d));
                }
        }
    }
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
