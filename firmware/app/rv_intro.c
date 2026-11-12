/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the start screen, as a gritty take on a lo-fi 90s music-TV cartoon, after the key art
 * (docs/brand/ravel-key-art.png). The tube warms up (a dot, a line, opening); out of the dark and the film grain
 * the three cloaked figures boil in before the cross and the red circle; RAVEL slams in letter by letter (a
 * cartoon smear, then settled, worn, a deep red shadow behind it), its outline boiling; the stripes slide in, the
 * maker's name types itself, and smoke rises off the letters and pours off the L, thickening until it takes the
 * picture; then the tube collapses to a line. Everything moves on twelve drawings a second, as cheap animation
 * did, with the TV's scan lines kept faint. About six seconds; any key, button or knob skips
 * it. Worked out from the time each frame (no state but the start), so a slow frame only skips ahead.
 * Part of the engine unit (ravel_app.c), after rv_ui.c (its drawing and its input). */

#define IN_SMOKE 4700u                   /* ms: the smoke starts taking the picture */
#define IN_OFF 5500u                     /* the tube collapses */
#define IN_END 5900u
#define IN_ON 420u                       /* the tube warming up, a line opening into the picture */
#define IN_FPS 83u                       /* a drawing every 83 ms: twelve a second */
#define IN_LY 152                        /* the logo's letters: top */
#define IN_LH 16                         /* and height */
#define IN_ART_X (PLAT_W / 2 - CLOAK_W / 2)
#define IN_ART_Y 14

/* the logo's letters as strokes, in pixels from each letter's top left (22 x 16): R A V E L, wide and thin,
 * the A without its bar */
static const int8_t in_seg[][4] = {
    {0, 0, 0, 16}, {0, 0, 19, 0}, {19, 0, 21, 2}, {21, 2, 21, 6}, {21, 6, 19, 8}, {0, 8, 19, 8}, {12, 8, 21, 16},   /* R */
    {0, 16, 11, 0}, {11, 0, 22, 16},                                                                                 /* A */
    {0, 0, 11, 16}, {11, 16, 22, 0},                                                                                 /* V */
    {0, 0, 0, 16}, {0, 0, 21, 0}, {0, 8, 17, 8}, {0, 16, 21, 16},                                                     /* E */
    {0, 0, 0, 16}, {0, 16, 20, 16},                                                                                  /* L */
};
static const uint8_t in_seg_l[] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 3, 4, 4};   /* each stroke's letter */
#define IN_NSEG ((int)(sizeof in_seg / sizeof in_seg[0]))
static const uint8_t in_lx[5] = {46, 77, 108, 139, 171};   /* the letters' left edges */

static struct {
    uint8_t on;
    uint32_t t0;
    int8_t ox, oy;                       /* the film's weave this drawing */
} in;

static uint32_t in_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static float in_rnd(uint32_t i, uint32_t k) { return (float)(in_hash(i * 2654435761u + k * 40503u + 17u) >> 8) * (1.0f / 16777216.0f); }
static float in_clamp(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float in_smooth(float x) { x = in_clamp(x); return x * x * (3.0f - 2.0f * x); }
static float in_mod(float x, float m) { return x - m * fm_floorf(x / m); }

static uint16_t in_grey(float v)          /* (neutral: green, with its sixth bit, at twice red and blue) */
{
    uint32_t k = (uint32_t)(in_clamp(v) * 31.0f + 0.5f);
    return (uint16_t)(k << 11 | (k * 2u) << 5 | k);
}
#define IN_RED RGB(150, 16, 30)
#define IN_BONE RGB(222, 214, 206)

static void intro_start(void)
{
    in.t0 = plat_ms();
    in.on = 1;
}

/* the background of rows b .. b+GB_H: near black, film grain (new each drawing), now and then a faint
 * scratched dash across, as the key art has */
static __attribute__((noinline)) RV_SMALL void in_back(uint32_t fr, int b, float fade)
{
    int j, x;
    for (j = 0; j < GB_H; j++) {
        int y = b + j;
        uint16_t *row = gb + j * PLAT_W;
        uint32_t rh = in_hash((uint32_t)y * 7919u + fr * 104729u);
        int dash = (rh & 31u) == 0u, dx = (int)((rh >> 5) % 200u), dw = 8 + (int)((rh >> 13) & 63u);
        for (x = 0; x < PLAT_W; x++) {
            uint32_t h = in_hash((uint32_t)x * 2654435761u ^ rh);
            float v = 0.025f + (float)(h & 15u) * 0.0035f;
            if (dash && x >= dx && x < dx + dw && (h & 48u))
                v += 0.07f;
            row[x] = in_grey(v * fade);
        }
    }
}

/* the picture: the figures before the cross and the circle (rv_cloaks.h), a touch see-through; a: how much */
#include "rv_cloaks.h"
static __attribute__((noinline)) RV_SMALL void in_art(int b, float a, int ox, int oy)
{
    int x0 = IN_ART_X + ox, y0 = IN_ART_Y + oy, y, x;
    a *= 0.88f;
    for (y = b > y0 ? b : y0; y < b + GB_H && y < y0 + CLOAK_H; y++) {
        const uint8_t *row = cloak_px + (uint32_t)(y - y0) * CLOAK_W / 2u;
        for (x = 0; x < CLOAK_W; x++) {
            int i = (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
            if (i)
                g_cover(x0 + x, y, cloak_pal[i], a);
        }
    }
}

/* the logo at drawing fr: each letter slams in at its time (two drawings stretched tall, a cartoon smear), its
 * strokes boiling a little as hand-drawn lines do, a deep red shadow behind; then worn: specks of it rubbed off */
static __attribute__((noinline)) RV_SMALL void in_logo(uint32_t t, uint32_t fr, int b, float fade, int ox, int oy)
{
    int s, pass, j, x;
    if (b + GB_H < IN_LY - 12 + oy || b > IN_LY + IN_LH + 12 + oy)
        return;
    for (pass = 0; pass < 2; pass++)
        for (s = 0; s < IN_NSEG; s++) {
            int l = in_seg_l[s];
            uint32_t at = 1750u + (uint32_t)l * 110u;
            float lx = (float)(in_lx[l] + ox), st = 1.0f, y0, y1;
            if (t < at)
                continue;
            if (t < at + 2u * IN_FPS)                  /* (the smear: tall, then taller's half) */
                st = t < at + IN_FPS ? 1.8f : 1.35f;
            y0 = (float)IN_LY + 8.0f + ((float)in_seg[s][1] - 8.0f) * st + (float)oy;
            y1 = (float)IN_LY + 8.0f + ((float)in_seg[s][3] - 8.0f) * st + (float)oy;
            g_seg(lx + in_seg[s][0] + (in_rnd(fr / 2u, (uint32_t)s * 4u) - 0.5f) * 1.2f + (float)pass * -2.0f + 2.0f,
                  y0 + (in_rnd(fr / 2u, (uint32_t)s * 4u + 1u) - 0.5f) * 1.2f + (float)(1 - pass),
                  lx + in_seg[s][2] + (in_rnd(fr / 2u, (uint32_t)s * 4u + 2u) - 0.5f) * 1.2f + (float)pass * -2.0f + 2.0f,
                  y1 + (in_rnd(fr / 2u, (uint32_t)s * 4u + 3u) - 0.5f) * 1.2f + (float)(1 - pass),
                  pass ? 2.2f : 2.6f, pass ? g_mix(RGB(0, 0, 0), IN_BONE, fade) : g_mix(RGB(0, 0, 0), IN_RED, fade * 0.8f));
        }
    for (j = 0; j < GB_H; j++) {                      /* worn: specks rubbed off the letters (the same ones every drawing) */
        int y = b + j;
        uint16_t *row = gb + j * PLAT_W;
        if (y < IN_LY - 2 + oy || y > IN_LY + IN_LH + 2 + oy)
            continue;
        for (x = 40; x < 200; x++)
            if ((row[x] >> 11) > 20u && (row[x] & 31u) > 18u && (in_hash((uint32_t)(x - ox) * 977u + (uint32_t)(y - oy) * 7919u) & 7u) < 2u)
                row[x] = g_blend(row[x], RGB(40, 34, 34), 10u);
    }
}

/* a pixel covered a (0..1) by c, rounded by a dither (smoke is many thin layers: rounded the same way each time,
 * a thin layer moves 5-bit red and blue less than half a step and 6-bit green a whole one, and grey goes green) */
static void in_cov(int x, int y, uint16_t c, float a)
{
    uint16_t *p;
    uint32_t k, q, d;
    if (x < g_cx0 || x >= g_cx1 || y < g_y0 || y >= g_y1 || a <= 0.004f)
        return;
    p = &gb[(y - g_y0) * PLAT_W + x];
    k = (uint32_t)(a * 255.0f), q = 255u - k, d = ((uint32_t)x * 37u + (uint32_t)y * 91u + k * 13u) & 255u;
    *p = (uint16_t)((((c >> 11) * k + (*p >> 11) * q + d) / 255u) << 11 | ((((c >> 5) & 63u) * k + ((*p >> 5) & 63u) * q + d) / 255u) << 5 |
                    (((c & 31u) * k + (*p & 31u) * q + d) / 255u));
}

/* smoke: puffs rising off the letters' tops and pouring off the L's foot, drifting right and up as they grow
 * and thin; k: how thick (0..1, then more as it takes the picture) */
static __attribute__((noinline)) RV_SMALL void in_smoke(uint32_t t, int b, float k, int ox, int oy)
{
    int i, y, x;
    float big = k > 1.0f ? 1.0f + (k - 1.0f) * 0.6f : 1.0f;   /* (thicker, it billows bigger) */
    for (i = 0; i < 46; i++) {
        float life = 1900.0f + 900.0f * in_rnd((uint32_t)i, 3), u = in_mod((float)t + (float)i * 97.0f, life) / life;
        int lf = i % 3 != 0;                          /* (two in three off the L, the rest off the tops) */
        float sx = lf ? 189.0f + 6.0f * in_rnd((uint32_t)i, 4) : 46.0f + 147.0f * in_rnd((uint32_t)i, 5);
        float sy = lf ? (float)(IN_LY + IN_LH - 3) : (float)IN_LY + 2.0f;
        float cx = sx + u * (lf ? 22.0f + 34.0f * in_rnd((uint32_t)i, 6) : 10.0f) + fm_sin_turns(u * 1.1f + in_rnd((uint32_t)i, 7)) * 8.0f * u + (float)ox;
        float cy = sy - u * (36.0f + 60.0f * in_rnd((uint32_t)i, 8)) * big + (float)oy, r = (3.0f + u * (lf ? 19.0f : 12.0f)) * big;
        float a = fm_minf(1.0f, k) * (0.13f + 0.08f * (k > 1.0f ? k - 1.0f : 0.0f)) * (1.0f - u) * in_smooth(u / 0.1f);
        int y0 = (int)(cy - r), y1 = (int)(cy + r);
        uint16_t c = g_mix(RGB(150, 144, 140), RGB(70, 60, 62), u);
        if (a < 0.02f || y1 < b || y0 >= b + GB_H)
            continue;
        for (y = y0 < b ? b : y0; y <= y1 && y < b + GB_H; y++)
            for (x = (int)(cx - r); x <= (int)(cx + r); x++) {
                float dx = (float)x - cx, dy = (float)y - cy, d = (dx * dx + dy * dy) / (r * r);
                if (d < 1.0f)
                    in_cov(x, y, c, a * (1.0f - d));
            }
    }
}

/* everything over the background at t (on its drawing fr) */
static __attribute__((noinline)) RV_SMALL void in_scene(uint32_t t, uint32_t fr, int b, float fade)
{
    int ox = in.ox, oy = in.oy;
    float art = in_smooth(((float)t - 600.0f) / 900.0f), sm = in_smooth(((float)t - 2100.0f) / 700.0f) + in_smooth(((float)t - (float)IN_SMOKE) / 700.0f) * 2.2f;
    if (art > 0.0f && !(t < 1500 && (in_hash(fr) & 3u) == 0u))   /* (boiling in: a drawing missing now and then) */
        in_art(b, art * fade * (1.0f - 0.85f * in_smooth(((float)t - (float)IN_SMOKE - 300.0f) / 500.0f)), ox, oy);
    in_logo(t, fr, b, fade * (1.0f - 0.7f * in_smooth(((float)t - (float)IN_SMOKE - 400.0f) / 400.0f)), ox, oy);
    if (t > 2400 && b + GB_H > 182 + oy && b < 190 + oy) {   /* the stripes, sliding in from the left, and a line on */
        int k, n = (int)((t - 2400u) / IN_FPS);
        for (k = 0; k < 7 && k < n; k++) {
            int x = 46 + k * 6 + ox, yy;
            for (yy = 0; yy < 4; yy++)
                g_fill(x + 3 - yy, 183 + yy + oy, 3, 1, g_mix(RGB(0, 0, 0), k < 4 ? RGB(150, 146, 144) : IN_RED, fade));
        }
        if (n > 7)
            g_fill(92 + ox, 186 + oy, (n - 7) * 14 < 101 ? (n - 7) * 14 : 101, 1, in_grey(0.32f * fade));
    }
    if (t > 2900 && b + GB_H > 218 && b < 230) {     /* the maker's name, typed, and its red square */
        static const char nm[] = "DE-PATTERN";
        char sh[11];
        int k, n = (int)((t - 2900u) / IN_FPS);
        for (k = 0; k < 10; k++)
            sh[k] = k < n ? nm[k] : 0;
        sh[10] = 0;
        g_text(138 + ox, 220 + oy, &FONT_XS, sh, in_grey(0.78f * fade));
        if (n > 10)
            g_fill(214 + ox, 222 + oy, 4, 4, g_mix(RGB(0, 0, 0), IN_RED, fade));
    }
    if (sm > 0.0f)
        in_smoke(t, b, sm * fade, ox, oy);
}

/* the TV, faint: every other row a shade darker; on some drawings a scratch down the film */
static __attribute__((noinline)) RV_SMALL void in_tv(uint32_t fr, int b)
{
    int j, x, sc = (in_hash(fr * 31u) & 3u) == 0u ? (int)(in_hash(fr * 37u) % (uint32_t)PLAT_W) : -1;
    for (j = 0; j < GB_H; j++) {
        uint16_t *row = gb + j * PLAT_W;
        if ((b + j) & 1)
            for (x = 0; x < PLAT_W; x++)
                row[x] = (uint16_t)(row[x] - ((row[x] >> 3) & 0x18E3u));
        if (sc >= 0)
            row[sc] = g_blend(row[sc], RGB(120, 116, 110), 5u);
    }
}

/* one frame; 0 once it is over */
static int intro_frame(void)
{
    uint32_t t = plat_ms() - in.t0, lk = 0, fr, ts;
    int b;
    float fade;
    if (!in.on)
        return 0;
    if (t >= IN_END) {
        in.on = 0;
        return 0;
    }
    fr = t / IN_FPS;
    ts = fr * IN_FPS;                                 /* (the picture moves on twelve drawings a second) */
    fade = 1.0f - in_smooth(((float)t - (float)IN_END + 250.0f) / 250.0f);
    {                                                  /* the film's weave: a pixel now and then */
        uint32_t h = in_hash(fr * 7u + 3u);
        in.ox = (int8_t)((h & 7u) == 0u ? ((h >> 3) & 1u ? 1 : -1) : 0);
        in.oy = (int8_t)((h & 0x70u) == 0u ? ((h >> 7) & 1u ? 1 : -1) : 0);
    }
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        if (t >= IN_OFF) {                            /* the tube collapses: to a line, to a dot, gone */
            float u = ((float)t - (float)IN_OFF) / (float)(IN_END - IN_OFF), hh = 40.0f * (1.0f - u) * (1.0f - u) * (1.0f - u) + 1.0f;
            int w = (int)(PLAT_W * (u < 0.6f ? 1.0f : (1.0f - u) * 2.5f));
            g_fill(0, b, PLAT_W, GB_H, RGB(0, 0, 0));
            g_fill(PLAT_W / 2 - w / 2, PLAT_H / 2 - (int)hh, w, 2 * (int)hh, in_grey((0.4f + u * 0.4f) * fade));
            g_fill(PLAT_W / 2 - w / 2, PLAT_H / 2 - 1, w, 2, in_grey(0.8f * fade));
        } else {
            in_back(fr, b, fade);
            in_scene(ts, fr, b, fade);
            in_tv(fr, b);
            if (t < IN_ON) {                          /* the tube warming up: a dot, a line across, opening into the picture */
                float u = (float)t / (float)IN_ON, w = in_smooth(u / 0.3f), hh = 1.0f + in_smooth((u - 0.25f) / 0.75f) * 125.0f;
                int x0 = PLAT_W / 2 - (int)(w * PLAT_W * 0.5f), yy;
                for (yy = b; yy < b + GB_H; yy++) {
                    float d = fm_fabsf((float)yy - (float)(PLAT_H / 2));
                    if (d > hh)
                        g_fill(0, yy, PLAT_W, 1, RGB(0, 0, 0));
                    else if (d < 2.0f + (1.0f - u) * 3.0f) {   /* (the bright line, fading as it opens) */
                        g_fill(0, yy, PLAT_W, 1, RGB(0, 0, 0));
                        g_fill(x0, yy, PLAT_W - 2 * x0, 1, in_grey(0.9f - u * 0.5f));
                    }
                }
            }
        }
        g_send();
    }
    /* the keys' lights: out from the middle as the tube warms; one by one as the letters land; a slow drift like
     * the smoke; in from both ends as the tube collapses */
    if (t < IN_ON) {
        int w = (int)(13.0f * (float)t / (float)IN_ON), k2;
        for (k2 = 13 - w; k2 <= 13 + w; k2++)
            lk |= 1u << k2;
    } else if (t >= 1750 && t < 2400) {
        uint32_t n = (t - 1750u) / 110u + 1u;
        lk = (1u << (n * 5u > 27u ? 27u : n * 5u)) - 1u;
    } else if (t >= 2400 && t < IN_OFF)
        lk = in_hash(fr / 2u) & in_hash(fr / 2u + 9u) & in_hash(fr / 2u + 17u);
    else if (t >= IN_OFF) {
        int w = (int)(13.0f * (1.0f - ((float)t - (float)IN_OFF) / (float)(IN_END - IN_OFF - 100))), k2;
        for (k2 = 13 - w; k2 <= 13 + w; k2++)
            if (k2 >= 0 && k2 < 27)
                lk |= 1u << k2;
    }
    plat_leds(0, fade > 0.05f ? lk & 0x07FFFFFFu : 0);
    return 1;
}

/* any key, button or knob ends it (and does nothing else) */
static int intro_skip(void)
{
    uint32_t k = plat_keys(), bt = plat_buttons();
    int i, turned = 0;
    for (i = 0; i < NE; i++)
        turned |= plat_enc(i) != 0;
    if ((k & ~ui.prev_keys) || (bt & ~ui.prev_btn) || turned) {
        for (i = 0; i < NB; i++)
            if ((bt >> i) & 1u)
                ui.layer_used[i] = 1;                 /* (its let-go switches no page) */
        in.on = 0;
    }
    ui.prev_keys = k;
    ui.prev_btn = bt;
    return in.on;
}

/* the platform starts it (the FM-1 at power-on, the emulator page as it opens) */
void ui_intro(void) { intro_start(); }
int ui_intro_on(void) { return in.on; }
