/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the start screen. A storm in a few bits: a cloud of dust turning round in the dark, wind
 * and rain through it, lightning; the dust gathers into RAVEL, the word holds a moment, then runs
 * down the screen in drips. Greys, a little teal, a touch of
 * deep red. About five seconds; any key, button or knob skips it. Everything is worked out from the
 * time each frame (no state but the start), so a slow frame only skips ahead.
 * Part of the engine unit (ravel_app.c), after rv_ui.c (its drawing and its input). */

#define IN_NP 210                        /* dust */
#define IN_NRAIN 46
#define IN_NDROP 16
#define IN_END 5600u                     /* ms */
#define IN_CELL 3                        /* the word's pixels, on screen */
#define IN_X0 26
#define IN_Y0 92
#define IN_CX 120
#define IN_CY 108

/* the word, 5 letters of 11 x 11 (wide, flat, the A without its bar, as in the logo) */
static const char *const in_glyph[5][11] = {
    {"##########.", "###########", "##.......##", "##.......##", "###########", "##########.",
     "##....###..", "##.....###.", "##......###", "##.......##", "##.......##"},
    {"....###....", "....###....", "...##.##...", "...##.##...", "..##...##..", "..##...##..",
     ".##.....##.", ".##.....##.", "##.......##", "##.......##", "##.......##"},
    {"##.......##", "##.......##", "##.......##", ".##.....##.", ".##.....##.", "..##...##..",
     "..##...##..", "...##.##...", "...##.##...", "....###....", "....###...."},
    {"###########", "###########", "##.........", "##.........", "#########..", "#########..",
     "##.........", "##.........", "##.........", "###########", "###########"},
    {"##.........", "##.........", "##.........", "##.........", "##.........", "##.........",
     "##.........", "##.........", "##.........", "###########", "###########"},
};

static struct {
    uint8_t on, nbot;
    uint16_t n;
    uint32_t t0, last;
    uint8_t cx[320], cy[320];            /* the word's cells (63 x 11 grid) */
    uint8_t bot[64];                     /* cells with nothing under them (where drips start) */
    int16_t px[IN_NP], py[IN_NP], tx[IN_NP], ty[IN_NP];   /* (t: where it was a moment ago, for its streak) */
    uint8_t pk[IN_NP];                   /* each grain this frame: 0 unseen, 1 far .. 3 near, 4 landed */
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
static float in_cos(float turns) { return fm_sin_turns(turns + 0.25f); }

static uint16_t in_grey(float v)
{
    int g = (int)(in_clamp(v) * 255.0f);
    return RGB(g, g, g);
}
#define IN_TEAL RGB(40, 186, 172)
#define IN_TEAL_D RGB(16, 74, 70)
#define IN_RED RGB(138, 14, 26)

/* lightning: how bright the sky is at t (0..1), and whether a bolt shows */
static float in_flash(uint32_t t, int *bolt)
{
    static const uint16_t at[6] = {380, 470, 1180, 1720, 3060, 3160};
    static const uint8_t bolt_of[6] = {0, 0, 1, 0, 1, 0};
    float f = 0.0f;
    int i;
    *bolt = -1;
    for (i = 0; i < 6; i++)
        if (t >= at[i] && t < at[i] + 260u) {
            float a = 1.0f - (float)(t - at[i]) / 260.0f;
            a = a * a * (i == 1 || i == 5 ? 0.55f : 1.0f);
            if (a > f)
                f = a;
            if (bolt_of[i] && t < at[i] + 140u)
                *bolt = i;
        }
    return f;
}

static void intro_start(void)
{
    int l, r, c, i;
    in.n = 0;
    for (l = 0; l < 5; l++)
        for (r = 0; r < 11; r++)
            for (c = 0; c < 11; c++)
                if (in_glyph[l][r][c] == '#' && in.n < sizeof in.cx) {
                    in.cx[in.n] = (uint8_t)(l * 13 + c);
                    in.cy[in.n] = (uint8_t)r;
                    in.n++;
                }
    in.nbot = 0;
    for (i = 0; i < in.n && in.nbot < sizeof in.bot; i++) {
        int x = in.cx[i] % 13, lt = in.cx[i] / 13, y = in.cy[i];
        if (y == 10 || in_glyph[lt][y + 1][x] != '#')
            in.bot[in.nbot++] = (uint8_t)i;
    }
    in.t0 = plat_ms();
    in.last = 0;
    in.on = 1;
}

/* a grain at time tf: a cloud turning round the middle (near grains big and bright, far ones small
 * and dim), pushed by the wind; from 1.9 s each flies to its cell of the word */
static int in_grain(int i, float tf, float *ox, float *oy, float *og)
{
    float spin = tf * 0.00023f, gust = fm_sin_turns(tf * 0.00021f) * 16.0f + fm_sin_turns(tf * 0.00067f) * 6.0f;
    float a0 = in_rnd(i, 1), rad = 18.0f + in_rnd(i, 2) * in_rnd(i, 8) * 110.0f, h = (in_rnd(i, 3) - 0.5f) * (150.0f - rad * 0.6f);
    float sp = 0.6f + in_rnd(i, 4) * 0.9f, ph = a0 + spin * sp * (1.2f - rad / 150.0f);
    float x = in_cos(ph) * rad, z = fm_sin_turns(ph) * rad, k = 1.0f / (1.0f - z / 220.0f);
    float sx = (float)IN_CX + x * k + gust * (0.5f + 0.5f * (h + 75.0f) / 150.0f);
    float sy = (float)IN_CY + h * k * 0.85f + fm_sin_turns(tf * 0.0004f + a0 * 3.0f) * 5.0f + gust * 0.15f * in_cos(ph);
    int depth = z > 25.0f ? 3 : z > -25.0f ? 2 : 1;
    int cell = (int)((uint32_t)i * in.n / IN_NP);
    float d0 = 1900.0f + in_rnd(i, 6) * 520.0f, g = in_smooth((tf - d0) / 760.0f);
    if (g > 0.0f) {                                   /* gathering: to its cell of the word, swirling in */
        float txp = (float)(IN_X0 + in.cx[cell] * IN_CELL + 1), typ = (float)(IN_Y0 + in.cy[cell] * IN_CELL + 1);
        float sw = (1.0f - g) * (1.0f - g) * 34.0f;
        sx = sx + (txp - sx) * g + in_cos(ph * 3.0f) * sw;
        sy = sy + (typ - sy) * g + fm_sin_turns(ph * 3.0f) * sw * 0.5f;
        if (g > 0.6f)
            depth = 3;
    }
    *ox = sx;
    *oy = sy;
    *og = g;
    return depth;
}

static void in_dust(uint32_t t)
{
    float tf = (float)t;
    int i;
    for (i = 0; i < IN_NP; i++) {
        float x, y, g, x0, y0, g0;
        int depth = in_grain(i, tf, &x, &y, &g);
        float appear = in_smooth((tf - in_rnd(i, 5) * 700.0f) / 400.0f);   /* (the cloud thickens out of nothing) */
        in_grain(i, tf - 45.0f, &x0, &y0, &g0);
        in.px[i] = (int16_t)x;
        in.py[i] = (int16_t)y;
        {                                             /* (a streak of a few pixels at most) */
            float dx = x0 - x, dy = y0 - y, m = fm_fabsf(dx) > fm_fabsf(dy) ? fm_fabsf(dx) : fm_fabsf(dy);
            if (m > 5.0f) {
                dx *= 5.0f / m;
                dy *= 5.0f / m;
            }
            in.tx[i] = (int16_t)(x + dx);
            in.ty[i] = (int16_t)(y + dy);
        }
        in.pk[i] = (uint8_t)(g >= 1.0f ? 4 : appear < in_rnd(i, 7) ? 0 : depth);
    }
}

/* how much of cell c of the word shows at t (0..1) */
static float in_cell(int c, uint32_t t)
{
    float d0 = 2350.0f + in_rnd((uint32_t)c, 9) * 700.0f + (float)(in.cx[c]) * 6.0f;
    return in_smooth(((float)t - d0) / 260.0f);
}

/* a drip: when it lets go, how far it has fallen, and its head's size */
static int in_drip(int k, uint32_t t, int *x, int *top, int *head, int *r)
{
    float ts = 3650.0f + in_rnd((uint32_t)k, 21) * 1300.0f, dt = ((float)t - ts) * 0.001f;
    int c = in.bot[(uint32_t)(in_rnd((uint32_t)k, 22) * (float)in.nbot) % in.nbot];
    float fall;
    if (dt < 0.0f)
        return 0;
    *x = IN_X0 + in.cx[c] * IN_CELL + 1;
    *top = IN_Y0 + in.cy[c] * IN_CELL + IN_CELL;
    if (dt < 0.35f) {                                  /* swelling under the letter */
        *head = *top + (int)(dt * 10.0f);
        *r = 1 + (int)(dt * 6.0f);
        return 1;
    }
    dt -= 0.35f;
    fall = 40.0f * dt + 260.0f * dt * dt * (0.7f + in_rnd((uint32_t)k, 23) * 0.6f);
    *head = *top + 3 + (int)fall;
    *r = 2 + (int)(in_rnd((uint32_t)k, 24) * 2.0f);
    return *head - 20 < PLAT_H;
}

/* the maker's name in a lower-case pixel hand of its own (4 x 8, a bit a pixel, high bit left) */
static const char in_lc_ch[] = "de-patrn";
static const uint8_t in_lc[8][8] = {
    {0x01, 0x01, 0x07, 0x09, 0x09, 0x09, 0x07, 0x00},   /* d */
    {0x00, 0x00, 0x06, 0x09, 0x0f, 0x08, 0x07, 0x00},   /* e */
    {0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00},   /* - */
    {0x00, 0x00, 0x0e, 0x09, 0x09, 0x0e, 0x08, 0x08},   /* p */
    {0x00, 0x00, 0x06, 0x01, 0x07, 0x09, 0x07, 0x00},   /* a */
    {0x04, 0x04, 0x0e, 0x04, 0x04, 0x04, 0x03, 0x00},   /* t */
    {0x00, 0x00, 0x0b, 0x0c, 0x08, 0x08, 0x08, 0x00},   /* r */
    {0x00, 0x00, 0x0e, 0x09, 0x09, 0x09, 0x09, 0x00},   /* n */
};

static void in_lower(int x, int y, const char *str, uint16_t c)
{
    for (; *str; str++, x += 5) {
        int g = 0, r, i;
        while (in_lc_ch[g] && in_lc_ch[g] != *str)
            g++;
        if (!in_lc_ch[g])
            continue;
        for (r = 0; r < 8; r++)
            for (i = 0; i < 4; i++)
                if ((in_lc[g][r] >> (3 - i)) & 1u)
                    g_pset(x + i, y + r, c);
    }
}

/* one frame; 0 once it is over */
static int intro_frame(void)
{
    uint32_t t = plat_ms() - in.t0, lk = 0;
    int b, i, bolt;
    float fl, fade, rainv;
    if (!in.on)
        return 0;
    if (t >= IN_END) {
        in.on = 0;
        return 0;
    }
    fl = in_flash(t, &bolt);
    fade = 1.0f - in_smooth(((float)t - 5150.0f) / 450.0f);
    rainv = (1.0f - in_smooth(((float)t - 3200.0f) / 1200.0f) * 0.7f) * fade;
    in_dust(t);
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        g_fill(0, b, PLAT_W, GB_H, in_grey((0.012f + fl * 0.12f) * fade));
        /* rain: thin slanting streaks, leaning with the wind */
        {
            float lean = 0.18f + fm_sin_turns((float)t * 0.00021f) * 0.12f;
            for (i = 0; i < IN_NRAIN; i++) {
                float sp = 0.32f + in_rnd(i, 11) * 0.22f, len = 5.0f + in_rnd(i, 12) * 9.0f;
                int y = (int)in_mod(in_rnd(i, 13) * 280.0f + (float)t * sp, 280.0f) - 20;
                int x = (int)in_mod(in_rnd(i, 14) * 300.0f + (float)y * lean + (float)t * 0.02f, 300.0f) - 30;
                int dx = (int)(len * lean), dy = (int)len;
                if (y < b - 20 || y - dy > b + GB_H)
                    continue;
                g_line_px(x, y, x - dx, y - dy, in_grey((0.16f + in_rnd(i, 15) * 0.18f + fl * 0.3f) * rainv));
            }
        }
        /* the bolt: a jagged line down from the top, teal round its white core */
        if (bolt >= 0) {
            int x = 40 + (int)(in_rnd((uint32_t)bolt, 41) * 160.0f), y = -2, s;
            for (s = 0; s < 11; s++) {
                int nx = x + (int)((in_rnd((uint32_t)bolt * 16u + (uint32_t)s, 42) - 0.5f) * 30.0f), ny = y + 10 + (int)(in_rnd((uint32_t)bolt * 16u + (uint32_t)s, 43) * 12.0f);
                g_line_px(x - 1, y, nx - 1, ny, IN_TEAL_D);
                g_line_px(x + 1, y, nx + 1, ny, IN_TEAL);
                g_line_px(x, y, nx, ny, C_WHITE);
                if (s == 5)
                    g_line_px(nx, ny, nx + 18, ny + 24, in_grey(0.5f));   /* (a branch) */
                x = nx;
                y = ny;
            }
        }
        /* the word: its cells as they fill, white, with a teal and a red ghost while the light flashes */
        if (b + GB_H > IN_Y0 - 30 && b < IN_Y0 + 11 * IN_CELL + 30) {
            float split = (t > 2900 && t < 3400) ? fl * 3.0f + (t < 3100 ? 1.0f : 0.0f) : 0.0f;
            float melt = in_smooth(((float)t - 4200.0f) / 1100.0f);
            for (i = 0; i < in.n; i++) {
                float v = in_cell(i, t);
                int x, y, sag;
                if (v <= 0.0f)
                    continue;
                x = IN_X0 + in.cx[i] * IN_CELL;
                {                              /* (it runs: each column slides and stretches at its own pace) */
                    float k = melt * in_rnd(in.cx[i], 51) * in_rnd(in.cx[i], 52) * 2.2f;
                    sag = (int)(k * (float)in.cy[i]);
                    y = IN_Y0 + in.cy[i] * IN_CELL + sag;
                    sag = (int)(k * (float)(in.cy[i] + 1)) - sag;   /* (now: how much taller the cell grows) */
                }
                if ((int)split > 0) {
                    g_fill(x - (int)split - 1, y, IN_CELL, IN_CELL + sag, IN_TEAL);
                    g_fill(x + (int)split + 1, y, IN_CELL, IN_CELL + sag, IN_RED);
                }
                if (v < 1.0f && ((in_hash((uint32_t)i + t / 40u) & 3u) == 0u))
                    continue;                          /* (still settling: it flickers) */
                g_fill(x, y, IN_CELL, IN_CELL + sag, in_grey((0.25f + 0.7f * v) * (1.0f - melt * 0.35f) * fade));
            }
            /* glitch: dashes beside the word, as on the logo, flickering in */
            if (t > 2700) {
                for (i = 0; i < 9; i++) {
                    uint32_t ep = t / 90u;
                    int y = IN_Y0 - 14 + (int)(in_rnd((uint32_t)i, 61 + ep % 5u) * 62.0f);
                    int x = (int)(in_rnd((uint32_t)i, 62 + ep % 3u) * 220.0f), w = 6 + (int)(in_rnd((uint32_t)i, 63) * 34.0f);
                    if ((in_hash((uint32_t)i * 7u + ep) & 3u) != 0u)
                        continue;
                    g_fill(x, y, w, 2, i == 3 ? IN_TEAL : in_grey(0.85f * fade));
                    if (i == 6)
                        g_fill(x + w + 4, y, 3, 2, IN_RED);
                }
            }
        }
        /* the dust */
        for (i = 0; i < IN_NP; i++) {
            int y = in.py[i], k = in.pk[i];
            if (!k || k == 4 || (y < b - 3 && in.ty[i] < b - 3) || (y >= b + GB_H && in.ty[i] >= b + GB_H))
                continue;
            if (k >= 2 && (in.tx[i] != in.px[i] || in.ty[i] != y))   /* (its streak, in the wind) */
                g_line_px(in.tx[i], in.ty[i], in.px[i], y, in_grey((k == 3 ? 0.3f : 0.16f) * fade));
            if (k == 3)
                g_fill(in.px[i], y, 2 + (i % 11 == 0), 2 + (i % 11 == 0), in_grey((0.82f + fl * 0.2f) * fade));
            else if (k == 2)
                g_fill(in.px[i], y, 2, 1, in_grey((0.46f + fl * 0.3f) * fade));
            else
                g_pset(in.px[i], y, in_grey((0.24f + fl * 0.3f) * fade));
            if (i % 37 == 0 && k == 3)
                g_pset(in.px[i] + 2, y, IN_TEAL);      /* (a few catch the light) */
        }
        /* the drips: a thinning trail down from the letter, a round head at its end */
        for (i = 0; i < IN_NDROP; i++) {
            int x, top, head, r;
            if (!in_drip(i, t, &x, &top, &head, &r) || head + r < b || top > b + GB_H)
                continue;
            g_fill(x, top, 2, (head - top) / 3 + 1, in_grey(0.7f * fade));   /* (thick where it leaves the letter) */
            g_fill(x, top, 1, head - top, in_grey(0.58f * fade));
            g_disc(x, head, r, in_grey(0.9f * fade));
            if (i == 5)
                g_pset(x, head - r, IN_RED);           /* (one runs red) */
            else
                g_pset(x - 1, head - 1, C_WHITE);
        }
        /* the maker's name, small, once the word is there: it flickers like a failing tube, now and then
         * jumping a pixel with a teal or red ghost */
        if (t > 3200 && b + GB_H > 212 && b < 228) {
            uint32_t q = in_hash(t / 45u), q2 = in_hash(t / 45u + 991u);
            int dark = (q & 7u) == 0u || ((q >> 3) & 15u) == 0u, jx = (q2 & 15u) == 0u ? ((q2 >> 4) & 1u ? 1 : -1) : 0;
            float v = (0.30f + (float)((q >> 8) & 255u) * (0.32f / 255.0f)) * fade;
            if (jx)
                in_lower(PLAT_W / 2 - 25 - jx * 2, 214, "de-pattern", (q2 >> 5) & 1u ? IN_TEAL : IN_RED);
            if (!dark)
                in_lower(PLAT_W / 2 - 25 + jx, 214, "de-pattern", in_grey(v));
        }
        g_send();
    }
    /* the keys' lights: all at a flash, a few with the rain, along the word as it forms */
    if (fl > 0.45f)
        lk = 0x07FFFFFFu;
    else if (t < 2300)
        lk = (in_hash(t / 70u) & in_hash(t / 70u + 99u) & in_hash(t / 70u + 7u)) & 0x07FFFFFFu;
    else if (t < 3400)
        lk = (1u << (((t - 2300u) / 40u) % 27u));
    plat_leds(0, fade > 0.1f ? lk : 0);
    in.last = t;
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
