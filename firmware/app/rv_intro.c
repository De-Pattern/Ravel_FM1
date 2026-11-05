/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the start screen, as a worn VHS tape plays it. Snow fills the dark; then the snow comes loose from
 * the picture: its brightest specks stay, drift apart in the rain, and gather into the RAVEL logo while the
 * sky flickers white. The logo holds, trembling as a tape does, and starts to run in drips; then the tape
 * fails (the picture wobbles, smears down and bleeds), the player crashes (the picture warps in waves, tears
 * sideways in bands and breaks into sliding blocks, every frame), and the tube collapses to a line. Greys, a little teal, a touch of deep red, and the colour fringes of tape. About
 * six seconds; any key, button or knob skips it. Everything is worked out from the time each frame (no
 * state but the start), so a slow frame only skips ahead.
 * Part of the engine unit (ravel_app.c), after rv_ui.c (its drawing and its input). */

#define IN_NP 200                        /* the specks that leave the snow */
#define IN_NRAIN 46
#define IN_NDROP 12
#define IN_FAIL 4300u                    /* ms: the tape starts to fail */
#define IN_CRASH 5000u                   /* the player crashes into blocks */
#define IN_OFF 5550u                     /* the tube collapses */
#define IN_END 5900u
#define IN_LY 102                        /* the logo's letters: top */
#define IN_LH 16                         /* and height */

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
static const uint8_t in_lx[5] = {68, 99, 130, 161, 193};   /* the letters' left edges */
#define IN_MX 18                         /* the mark: two twisted ribbons, half in halftone */
#define IN_MW 40
#define IN_MY 98
#define IN_MH 24

static struct {
    uint8_t on;
    uint32_t t0;
    int8_t ox, oy;                       /* the camera's shake this frame */
    uint16_t pr[PLAT_W];                 /* the last row sent (the smear drags it down) */
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

static uint16_t in_grey(float v)
{
    int g = (int)(in_clamp(v) * 255.0f);
    return RGB(g, g, g);
}
#define IN_TEAL RGB(40, 186, 172)
#define IN_RED RGB(138, 14, 26)

/* the sky: a white flash that flickers (lightning out of sight), how bright at t (0..1) */
static float in_flash(uint32_t t)
{
    static const uint16_t at[5] = {1150, 1730, 2650, 3480, 4380};
    float f = 0.0f;
    int i;
    for (i = 0; i < 5; i++)
        if (t >= at[i] && t < at[i] + 320u) {
            uint32_t d = t - at[i];
            float a = 1.0f - (float)d / 320.0f;
            a *= d < 50u ? 1.0f : d < 90u ? 0.15f : d < 150u ? 0.85f : d < 175u ? 0.1f : 0.6f;   /* (on, off, on, dying) */
            if (a > f)
                f = a;
        }
    return f;
}

static void intro_start(void)
{
    in.t0 = plat_ms();
    in.on = 1;
}

/* how much snow covers the picture: all of it at first, a little while the logo is there, all again as the tape fails */
static float in_snow(uint32_t t)
{
    return t < IN_FAIL ? 1.0f - in_smooth(((float)t - 650.0f) / 800.0f) * 0.94f : 0.06f + in_smooth(((float)t - (float)IN_FAIL) / 900.0f) * 0.9f;
}

/* the mark's two ribbons: their middle at x (0 .. IN_MW) */
static float in_rib(int k, float x)
{
    float s = in_smooth(x / (float)IN_MW);
    return (float)IN_MY + 4.0f + (k ? 1.0f - s : s) * (float)(IN_MH - 8);
}

/* speck i's place in the logo */
static void in_target(int i, float *x, float *y)
{
    if (in_rnd((uint32_t)i, 30) < 0.28f) {             /* on the mark */
        float u = in_rnd((uint32_t)i, 31) * (float)IN_MW;
        *x = (float)IN_MX + u;
        *y = in_rib(i & 1, u) + (in_rnd((uint32_t)i, 32) - 0.5f) * 5.0f;
    } else {                                           /* on a stroke of a letter */
        int s = (int)(in_hash((uint32_t)i * 31u + 5u) % (uint32_t)IN_NSEG);
        float u = in_rnd((uint32_t)i, 33), lx = (float)in_lx[in_seg_l[s]];
        *x = lx + (float)in_seg[s][0] + (float)(in_seg[s][2] - in_seg[s][0]) * u;
        *y = (float)IN_LY + (float)in_seg[s][1] + (float)(in_seg[s][3] - in_seg[s][1]) * u;
    }
}

/* speck i at t: where, and how bright (0: unseen). It sits in the snow, comes loose, drifts apart on the wind,
 * then flies to its place in the logo, swirling in */
static float in_speck(int i, float t, float *x, float *y)
{
    float x0 = in_rnd((uint32_t)i, 1) * (float)PLAT_W, y0 = in_rnd((uint32_t)i, 2) * (float)PLAT_H;
    float a = in_rnd((uint32_t)i, 3), u = in_clamp((t - 900.0f) / 1500.0f), r = u * u * (20.0f + 50.0f * in_rnd((uint32_t)i, 4));
    float gust = fm_sin_turns(t * 0.00021f) * 14.0f;
    float px = x0 + fm_sin_turns(a + 0.25f) * r + (gust + fm_sin_turns(t * 0.0004f + a * 9.0f) * 9.0f) * u;
    float py = y0 + fm_sin_turns(a) * r + fm_sin_turns(t * 0.0003f + a * 5.0f) * 7.0f * u + u * 10.0f;
    float g = in_smooth((t - 2150.0f - in_rnd((uint32_t)i, 5) * 450.0f) / 750.0f), tx, ty, sw;
    in_target(i, &tx, &ty);
    sw = (1.0f - g) * (1.0f - g) * 26.0f;
    *x = px + (tx - px) * g + fm_sin_turns(a * 3.0f + t * 0.0011f + 0.25f) * sw;
    *y = py + (ty - py) * g + fm_sin_turns(a * 3.0f + t * 0.0011f) * sw * 0.5f;
    if (t < 600.0f)
        return 0.0f;
    return (0.35f + 0.6f * in_rnd((uint32_t)i, 6)) * in_smooth((t - 600.0f) / 500.0f) * (1.0f - in_smooth((t - 3050.0f) / 350.0f));
}

/* the background of rows b .. b+GB_H: the dark sky (lit from above when it flashes) and the snow over it, as a
 * worn tape's static is: dense, in short streaks across, a speck of colour in many (white, violet, blue, a little
 * green), rows a shade brighter or darker in bands that drift, a brighter band rolling down, now and then a
 * row of tall ticks; new every frame */
static __attribute__((noinline)) void in_back(uint32_t t, int b, float fl, float sn, float fade)
{
    int j, x;
    uint32_t fr = t / 33u;
    float tf = (float)t, roll = in_mod(tf * 0.11f, 320.0f) - 40.0f, op = 1.0f - in_smooth((tf - 600.0f) / 700.0f);   /* op: the opening static */
    for (j = 0; j < GB_H; j++) {
        int y = b + j;
        float up = 1.0f - (float)y / (float)PLAT_H, sky = 0.012f + fl * (0.06f + 0.42f * up * up);
        uint16_t *row = gb + j * PLAT_W;
        uint32_t rh = in_hash((uint32_t)y * 7919u + fr * 104729u);
        float band = 0.78f + 0.14f * fm_sin_turns((float)y * 0.017f + tf * 0.0007f) + 0.08f * fm_sin_turns((float)y * 0.061f - tf * 0.0013f), dr = (float)y - roll, gain;
        int ticks = (rh & 63u) == 0u, sx = (int)((rh >> 20) & 3u);   /* (a row of ticks; the row's own jitter) */
        if ((rh & 7u) == 0u)
            band *= 0.45f;                                 /* (a dropped-out row) */
        band = 1.0f + (band - 1.0f) * op;                 /* (the bands: the opening static's) */
        if (dr > 0.0f && dr < 14.0f)
            band += 0.45f * (1.0f - fm_fabsf(dr - 7.0f) / 7.0f);   /* (the rolling band) */
        gain = sn * band * fade;
        for (x = -sx; x < PLAT_W;) {
            uint32_t h = in_hash((uint32_t)(x + 8) * 2654435761u ^ rh);
            int run = 1 + (int)(h & 7u), k, g, r, gg, bb;   /* a streak: 1 to 8 pixels */
            float n = (float)((h >> 3) & 255u) * (1.0f / 255.0f), v;
            uint16_t c;
            n = op * n * n * (1.6f - 0.6f * n) + (1.0f - op) * n * n * n;   /* (dense at the start, sparse after, as it was) */
            if (ticks && ((h >> 11) & 3u) == 0u)
                n = 1.0f, run = 1;
            v = sky * fade + gain * n * 0.95f;
            g = (int)(in_clamp(v) * 255.0f);
            switch ((float)((h >> 16) & 255u) < op * 255.0f ? (h >> 12) & 7u : 0u) {   /* (the speck's colour: the opening static's) */
            case 4: case 5: r = g * 7 / 10; gg = g * 1 / 2; bb = g; break;        /* violet */
            case 6: r = g * 2 / 5; gg = g * 1 / 2; bb = g; break;                 /* blue */
            case 7: r = g * 1 / 2; gg = g; bb = g * 4 / 5; break;                 /* green */
            default: r = g; gg = g; bb = g + (g >> 3); break;                     /* white */
            }
            c = RGB(r, gg, bb > 255 ? 255 : bb);
            for (k = 0; k < run; k++, x++)
                if (x >= 0 && x < PLAT_W)
                    row[x] = k == run - 1 && run > 2 ? RGB(r * 3 / 5, gg * 3 / 5, bb * 3 / 5 > 255 ? 255 : bb * 3 / 5) : c;
        }
    }
}

/* the logo: the mark's ribbons (halftone on the left, solid on the right), the letters as smooth strokes,
 * the dashes around them; dv: its brightness; mt: how far it has run down (0..1) */
static __attribute__((noinline)) void in_logo(int b, float dv, float mt, int dy)
{
    int x, s, ox = in.ox, oy = in.oy + dy;
    static const uint8_t bay[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    if (b + GB_H < IN_MY - 6 + oy || b > IN_LY + IN_LH + 70 + oy)
        return;
    for (x = 0; x < IN_MW; x++) {                     /* the mark */
        float lv;
        if (IN_MX + x + ox < g_cx0 || IN_MX + x + ox >= g_cx1)
            continue;                                 /* (outside the strip being drawn) */
        {
        float sag = mt * mt * 30.0f * in_rnd((uint32_t)x / 3u, 81);
        int k, yy;
        lv = 0.3f + 0.7f * in_smooth((float)x / (float)(IN_MW * 0.6f));
        for (k = 0; k < 2; k++) {
            float c = in_rib(k, (float)x);
            for (yy = (int)(c - 3.0f); yy <= (int)(c + 3.0f + sag); yy++) {
                int sx = IN_MX + x + ox, sy = yy + oy;
                if (sy < b || sy >= b + GB_H)
                    continue;
                if (lv >= 0.98f || (float)bay[(sy & 3) * 4 + (sx & 3)] < lv * 16.0f)
                    g_pset(sx, sy, in_grey(dv * (0.55f + 0.45f * lv) * (yy > (int)(c + 3.0f) ? 0.8f : 1.0f)));
            }
        }
        }
    }
    for (s = 0; s < IN_NSEG; s++) {                   /* the letters, sagging as they melt (more at the bottom) */
        float lx = (float)(in_lx[in_seg_l[s]] + ox), x0 = lx + in_seg[s][0], x1 = lx + in_seg[s][2];
        float s0 = mt * mt * 22.0f * in_rnd((uint32_t)(x0 / 4.0f), 82), s1 = mt * mt * 22.0f * in_rnd((uint32_t)(x1 / 4.0f), 82);
        float y0 = (float)(IN_LY + oy + in_seg[s][1]), y1 = (float)(IN_LY + oy + in_seg[s][3]);
        if (fm_maxf(x0, x1) + 3.0f < (float)g_cx0 || fm_minf(x0, x1) - 3.0f > (float)g_cx1)
            continue;
        y0 += s0 * (float)in_seg[s][1] / (float)IN_LH;
        y1 += s1 * (float)in_seg[s][3] / (float)IN_LH;
        g_seg(x0 + 0.5f, y0 + 0.5f, x1 + 0.5f, y1 + 0.5f, 2.1f, in_grey(dv));
    }
    if (dv > 0.3f) {                                  /* the dashes and squares of the logo */
        uint16_t c = in_grey(dv * 0.8f);
        g_fill(4 + ox, 111 + oy, 12, 1, c);
        g_fill(36 + ox, 121 + oy, 88, 1, in_grey(dv * 0.55f));
        g_fill(100 + ox, 120 + oy, 6, 3, c);
        g_fill(150 + ox, 121 + oy, 40, 1, in_grey(dv * 0.45f));
        g_fill(219 + ox, 111 + oy, 10, 1, c);
        g_fill(200 + ox, 125 + oy, 3, 3, c);
        g_fill(208 + ox, 125 + oy, 3, 3, c);
        g_fill(14 + ox, 95 + oy, 3, 3, c);
        g_fill(52 + ox, 92 + oy, 16, 1, c);
        g_fill(42 + ox, 128 + oy, 3, 3, c);
    }
}

/* a drip at t: where it leaves the logo, how far its end has run, its bulb's size; 0 if not yet */
static int in_drip(int k, uint32_t t, float *x, float *top, float *end, float *r)
{
    float ts = 3550.0f + in_rnd((uint32_t)k, 21) * 1100.0f, dt = ((float)t - ts) * 0.001f;
    int s = (int)(in_hash((uint32_t)k * 13u + 3u) % (uint32_t)IN_NSEG);
    float u = in_rnd((uint32_t)k, 22);
    if (dt < 0.0f)
        return 0;
    if (in_seg[s][1] != 16 && in_seg[s][3] != 16)
        s = IN_NSEG - 1;                              /* (from a stroke that reaches the bottom: L's foot) */
    *x = (float)in_lx[in_seg_l[s]] + (float)in_seg[s][0] + (float)(in_seg[s][2] - in_seg[s][0]) * (in_seg[s][1] == in_seg[s][3] ? u : 1.0f) + 0.5f;
    if (in_seg[s][1] != in_seg[s][3] && in_seg[s][1] == 16)
        *x = (float)in_lx[in_seg_l[s]] + (float)in_seg[s][0] + 0.5f;
    *top = (float)(IN_LY + IN_LH) + 1.0f;
    *r = 1.2f + in_rnd((uint32_t)k, 24) * 1.3f + fm_minf(dt, 0.5f) * 1.2f;
    *end = *top + (dt < 0.5f ? dt * 6.0f : 3.0f + 18.0f * (dt - 0.5f) + 120.0f * (dt - 0.5f) * (dt - 0.5f) * (0.6f + in_rnd((uint32_t)k, 23)));
    return 1;
}

/* everything over the background at t: rain, the specks, the logo, its drips, the maker's name; dy: how far this
 * strip has fallen in the melt */
static __attribute__((noinline)) void in_scene(uint32_t t, int b, int dy, float fl, float fade)
{
    int i, ox = in.ox, oy = in.oy + dy;
    float tf = (float)t, lv = in_smooth((tf - 2950.0f) / 450.0f), melt = in_smooth((tf - 3700.0f) / 1800.0f);
    /* rain: thin slanting streaks, leaning with the wind */
    if (t > 450) {
        float lean = 0.18f + fm_sin_turns(tf * 0.00021f) * 0.12f, rv = in_smooth((tf - 450.0f) / 600.0f) * (1.0f - in_smooth((tf - 3300.0f) / 1200.0f) * 0.6f);
        for (i = 0; i < IN_NRAIN; i++) {
            float sp = 0.32f + in_rnd(i, 11) * 0.22f, len = 5.0f + in_rnd(i, 12) * 9.0f;
            int y = (int)in_mod(in_rnd(i, 13) * 280.0f + tf * sp, 280.0f) - 20 + oy;
            int x = (int)in_mod(in_rnd(i, 14) * 300.0f + (float)y * lean + tf * 0.02f, 300.0f) - 30 + ox;
            int ddx = (int)(len * lean), ddy = (int)len;
            if (y < b - 20 || y - ddy > b + GB_H || x < g_cx0 - 20 || x - ddx > g_cx1 + 4)
                continue;
            g_line_px(x, y, x - ddx, y - ddy, in_grey((0.16f + in_rnd(i, 15) * 0.18f + fl * 0.3f) * rv * fade));
        }
    }
    /* the specks: loose from the snow, drifting, gathering (a short streak behind each as it moves) */
    if (t > 600 && t < 3450)
        for (i = 0; i < IN_NP; i++) {
            float x, y, x0, y0, v = in_speck(i, tf, &x, &y);
            int ix, iy;
            if (v <= 0.0f)
                continue;
            ix = (int)x + ox;
            iy = (int)y + oy;
            if (iy < b - 4 || iy > b + GB_H + 3)
                continue;
            in_speck(i, tf - 40.0f, &x0, &y0);
            if (fm_fabsf(x0 - x) + fm_fabsf(y0 - y) > 1.5f)
                g_line_px((int)x0 + ox, (int)y0 + oy, ix, iy, in_grey(v * 0.35f * fade));
            g_fill(ix, iy, 1 + (i % 5 == 0), 1 + (i % 7 == 0), in_grey((v + fl * 0.2f) * fade));
            if (i % 41 == 0)
                g_pset(ix + 1, iy, IN_TEAL);
        }
    /* the logo, a teal and red ghost on either side as it lands */
    if (lv > 0.0f) {
        if (t < 3600 || (in_hash(t / 60u) & 7u) == 0u) {
            float sp = t < 3600 ? (1.0f - lv) * 4.0f + 1.0f : 2.0f;
            in.ox = (int8_t)(ox - (int)sp);
            in_logo(b, lv * 0.32f, melt, dy);
            in.ox = (int8_t)(ox + (int)sp);
            in_logo(b, lv * 0.22f, melt, dy);
            in.ox = (int8_t)ox;
        }
        in_logo(b, (0.25f + 0.7f * lv) * (1.0f - melt * 0.25f) * fade, melt, dy);
    }
    /* the drips: a thread from the bottom of a letter, thicker where it leaves, swelling into a drop that hangs,
     * lets go and runs; shaded, lit from the upper left */
    for (i = 0; i < IN_NDROP; i++) {
        float x, top, end, r, yy;
        if (!in_drip(i, t, &x, &top, &end, &r) || end + r + (float)oy < (float)b - 2.0f || top + (float)oy > (float)(b + GB_H + 2))
            continue;
        x += (float)ox;
        if (x + r + 3.0f < (float)g_cx0 || x - r - 3.0f > (float)g_cx1)
            continue;
        for (yy = top; yy < end; yy += 1.0f) {        /* the thread: tapering, from the letter's width down to a hair */
            float f = (yy - top) / (end - top + 1.0f), w = 1.0f + (1.0f - f) * (1.0f - f) * 1.6f;
            g_seg(x, yy + (float)oy, x, yy + 1.0f + (float)oy, w, in_grey((0.75f - f * 0.15f) * fade));
        }
        g_discf(x, end + (float)oy, r * 0.8f, in_grey(0.5f * fade));                                     /* (its shadow side) */
        g_discf(x - r * 0.15f, end - r * 0.25f + (float)oy, r * 0.75f, in_grey(0.8f * fade));          /* (the body) */
        g_discf(x - r * 0.35f, end - r * 0.45f + (float)oy, r * 0.3f, i == 4 ? IN_RED : in_grey(fade));  /* (the light on it) */
    }
    /* the maker's name, small, once the logo is there: it flickers like a failing tube, now and then jumping a
     * pixel with a teal or red ghost */
    if (t > 3300 && b + GB_H > 210 + oy && b < 226 + oy && g_cx1 > PLAT_W / 2 - 36 + ox && g_cx0 < PLAT_W / 2 + 36 + ox) {
        uint32_t q = in_hash(t / 45u), q2 = in_hash(t / 45u + 991u);
        int dark = (q & 7u) == 0u || ((q >> 3) & 15u) == 0u, jx = (q2 & 15u) == 0u ? ((q2 >> 4) & 1u ? 1 : -1) : 0;
        float v = (0.30f + (float)((q >> 8) & 255u) * (0.32f / 255.0f)) * fade;
        if (jx)
            g_text_c(PLAT_W / 2 - jx * 2 + ox, 214 + oy, &FONT_T, "de-pattern", (q2 >> 5) & 1u ? IN_TEAL : IN_RED);
        if (!dark)
            g_text_c(PLAT_W / 2 + jx + ox, 214 + oy, &FONT_T, "de-pattern", in_grey(v));
    }
}

/* the tape, over the finished band: rows pushed sideways (the camera's shake, a tracking band rolling down,
 * the wobble of a failing tape, the head switching at the bottom), the colour bleeding off to the sides, each
 * row smeared down into the next as the tape melts, and in the crash, blocks */
static __attribute__((noinline)) void in_tape(uint32_t t, int b)
{
    float tf = (float)t, fail = in_smooth((tf - (float)IN_FAIL) / 700.0f), smear = in_smooth((tf - (float)IN_FAIL - 200.0f) / 900.0f) * 0.85f;
    float crash = t >= IN_CRASH ? in_smooth((tf - (float)IN_CRASH) / 260.0f) : 0.0f;   /* (the crash: 0 .. 1) */
    int trk = (int)in_mod(tf * 0.06f, 300.0f) - 30, cr = 1 + (int)(fail * 3.0f + crash * 2.0f), j, x;
    uint32_t tq = t / 28u;                            /* (the tears change every 28 ms: jittery, never held) */
    uint16_t tmp[PLAT_W];
    for (j = 0; j < GB_H; j++) {
        int y = b + j, sh = 0, d = y - trk;
        uint16_t *row = gb + j * PLAT_W;
        uint32_t h = in_hash((uint32_t)y * 2654435761u + (t / 40u) * 40503u);
        if (d >= 0 && d < 7)                          /* the tracking band */
            sh += (int)((h >> 3) % 7u) - 3;
        if (y >= PLAT_H - 7)                          /* the head switching */
            sh += (int)((h >> 6) % 9u) + 2;
        sh += (int)(fail * (fm_sin_turns(tf * 0.0017f + (float)y * 0.013f) * 9.0f + ((h & 7u) == 0u ? (float)((h >> 9) % 31u) - 15.0f : 0.0f)));
        if (crash > 0.0f) {                           /* the crash: the picture warps in waves, bands of rows tear sideways */
            uint32_t bh = in_hash((uint32_t)(y / (6 + (int)(in_hash(tq * 31u + 7u) & 15u))) * 977u + tq * 40503u);
            float w = fm_sin_turns(tf * 0.0035f + (float)y * 0.008f) * 12.0f + fm_sin_turns(tf * 0.0071f - (float)y * 0.021f) * 4.0f;
            sh += (int)(crash * w);
            if ((bh & 7u) == 0u)
                sh += (int)(crash * (float)((int)((bh >> 4) % 49u) - 24));
        }
        for (x = 0; x < PLAT_W; x++) {                /* sideways, and the colours apart: red one way, blue the other */
            int xs = x - sh, xr = xs - cr, xb = xs + cr;
            uint16_t c = row[xs < 0 ? 0 : xs >= PLAT_W ? PLAT_W - 1 : xs];
            uint16_t cR = row[xr < 0 ? 0 : xr >= PLAT_W ? PLAT_W - 1 : xr], cB = row[xb < 0 ? 0 : xb >= PLAT_W ? PLAT_W - 1 : xb];
            tmp[x] = (uint16_t)((cR & 0xF800u) | (c & 0x07E0u) | (cB & 0x001Fu));
        }
        for (x = 0; x < PLAT_W; x++) {
            uint16_t c = tmp[x];
            if (smear > 0.0f && y > 0) {              /* the melt: the row above runs down into this one */
                uint16_t p = in.pr[x];
                uint32_t a = (uint32_t)(smear * 15.0f * (0.6f + 0.4f * (float)((in_hash((uint32_t)x / 4u * 97u + (uint32_t)y / 2u) >> 4) & 1u)));
                uint32_t lp = ((p >> 11) + ((p >> 5) & 63u) / 2u + (p & 31u)), lc = ((c >> 11) + ((c >> 5) & 63u) / 2u + (c & 31u));
                if (lp > lc)                          /* (only light runs down over dark) */
                    c = g_blend(c, p, a);
            }
            if (j & 1)                                /* (scan lines) */
                c = (uint16_t)(c - ((c >> 3) & 0x18E3u));
            row[x] = c;
            in.pr[x] = c;
        }
    }
    if (t >= IN_CRASH && t < IN_OFF) {                /* the crash: blocks of 8 frozen to a colour, moved, or garish */
        int bx, by;
        uint32_t q = (t - IN_CRASH) / 30u;           /* (new blocks every 30 ms) */
        for (by = b & ~7; by < b + GB_H; by += 8)
            for (bx = 0; bx < PLAT_W; bx += 8) {
                uint32_t h = in_hash((uint32_t)bx * 7919u + (uint32_t)by * 104729u + q * 2246822519u);
                int yy, n = (int)(h & 15u), lim = 2 + (int)((t - IN_CRASH) / 90u);
                if (lim > 7)
                    lim = 7;
                if (n >= lim)
                    continue;
                for (yy = by; yy < by + 8; yy++) {
                    uint16_t *row = gb + (yy - b) * PLAT_W, c0;
                    if (yy < b || yy >= b + GB_H)
                        continue;
                    c0 = row[bx + (int)((h >> 14) & 7u)];
                    for (x = bx; x < bx + 8; x++)
                        row[x] = n < 1 && ((h >> 7) & 1u) ? ((h >> 8) & 3u) == 0u ? IN_TEAL : ((h >> 8) & 3u) == 1u ? IN_RED : (yy & 1) ? C_WHITE : RGB(0, 40, 0)
                               : n < 5 ? g_blend(row[x], c0, 10u) : row[(x + 8 * (int)((h >> 10) % 5u) + (yy - by)) % PLAT_W];
                }
            }
    }
}

/* one frame; 0 once it is over */
static int intro_frame(void)
{
    uint32_t t = plat_ms() - in.t0, lk = 0, ts;
    int b, k;
    float fl, fade, sn;
    if (!in.on)
        return 0;
    if (t >= IN_END) {
        in.on = 0;
        return 0;
    }
    /* (in the crash the picture's own time slows to a crawl, so the melted logo stays in it as before, while the
     * tape warps and tears it every frame: nothing holds still) */
    ts = t >= IN_CRASH && t < IN_OFF ? IN_CRASH + (t - IN_CRASH) / 5u : t;
    fl = in_flash(ts);
    fade = 1.0f - in_smooth(((float)t - (float)IN_END + 250.0f) / 250.0f);
    sn = in_snow(ts);
    {                                                  /* the camera's shake: a pixel now and then, more as the tape fails */
        uint32_t h = in_hash(t / 70u);
        int big = (h & 15u) == 0u || t > IN_FAIL;
        in.ox = (int8_t)(((h >> 4) % 3u) - 1 + (big ? (int)((h >> 8) % 5u) - 2 : 0));
        in.oy = (int8_t)((h & 0x300u) == 0u ? ((h >> 12) & 1u ? 1 : -1) : 0);
        if (t > 650 && t < 900)
            in.oy = (int8_t)(in.oy + (int)(fm_sin_turns((float)(t - 650u) / 250.0f * 0.5f) * 30.0f));   /* (a roll as the picture locks) */
    }
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        if (t >= IN_OFF) {                            /* the tube collapses: to a line, to a dot, gone */
            float u = ((float)t - (float)IN_OFF) / (float)(IN_END - IN_OFF), hh = 40.0f * (1.0f - u) * (1.0f - u) * (1.0f - u) + 1.0f;
            int w = (int)(PLAT_W * (u < 0.6f ? 1.0f : (1.0f - u) * 2.5f));
            g_fill(0, b, PLAT_W, GB_H, RGB(0, 0, 0));
            g_fill(PLAT_W / 2 - w / 2, PLAT_H / 2 - (int)hh, w, 2 * (int)hh, in_grey((0.55f + u * 0.45f) * fade));
            g_fill(PLAT_W / 2 - w / 2, PLAT_H / 2 - 1, w, 2, in_grey(fade));
        } else {
            in_back(ts, b, fl, sn, fade);
            if (t < IN_FAIL)
                in_scene(ts, b, 0, fl, fade);
            else {                                    /* the melt: 4-pixel strips, each falling at its own pace */
                for (k = 0; k < PLAT_W / 4; k++) {
                    float d0 = (float)IN_FAIL + 250.0f + in_rnd((uint32_t)k / 3u, 71) * 400.0f + fm_sin_turns((float)k * 0.03f) * 150.0f, dt = (float)ts - d0;
                    int dy = dt > 0.0f ? (int)(dt * dt * (0.00004f + in_rnd((uint32_t)k / 2u, 72) * 0.00006f)) : 0;
                    g_cx0 = k * 4;
                    g_cx1 = g_cx0 + 4;
                    in_scene(ts, b, dy, fl, fade);
                }
                g_cx0 = 0;
                g_cx1 = PLAT_W;
            }
            in_tape(t, b);
        }
        g_send();
    }
    /* the keys' lights follow the picture: all at a flash; the snow's sparkle, thinning as the specks come loose;
     * a sweep as they gather into the logo; the tracking band rolling past while it holds; flicker as the tape
     * fails; blocks of three in the crash; then in from both ends as the tube collapses, to the middle key, out */
    if (fl > 0.45f && t < IN_FAIL)
        lk = 0x07FFFFFFu;
    else if (t < 900)
        lk = in_hash(t / 50u) & in_hash(t / 50u + 99u);
    else if (t < 2150)
        lk = in_hash(t / 90u) & in_hash(t / 90u + 99u) & in_hash(t / 90u + 7u);
    else if (t < 3200)
        lk = 7u << (((t - 2150u) / 40u) % 25u);
    else if (t < IN_FAIL)
        lk = 1u << ((t / 150u) % 27u);
    else if (t < IN_CRASH)
        lk = in_hash(t / 60u) & in_hash(t / 60u + 5u);
    else if (t < IN_OFF) {
        uint32_t h = in_hash(t / 45u * 7919u), g;
        for (g = 0; g < 9; g++)
            if ((h >> (g * 3)) & 1u)
                lk |= 7u << (g * 3);
    } else {
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
