/* SPDX-License-Identifier: GPL-3.0-only */
/* MOOM: a hidden first-person game. A major incident manager in business casual walks the bank's
 * infrastructure (server racks, log walls, dashboards) with a mouse-shaped blaster and resolves
 * bugs, failed changes and 500 errors before the uptime runs out. Level 2 goes inside the bridge call
 * itself: a desktop in 3D with a browser open, where incident graphs and lines of code from bad
 * changes attack, and ghouls crawl out of CHANGE REQUEST portals until each portal is rejected. A raycaster (as Wolfenstein 3D
 * drew it) on the 240 x 240 screen, drawn band by band like the rest of the UI; every wall,
 * sprite and sound is made in code. Included by rv_ui.c (it uses the drawing primitives).
 *
 * In: hold HOME and SAVE while powering on (the emulator: its MOOM button). Out: HOME. The synth
 * stops while it runs and starts again afterwards (if it was playing). Its state lives outside the
 * RAM pool (RV_RAMBSS).
 *
 * Keys, like arrow keys: G4 turn left, G#4 forward, A4 back, B4 turn right (C#4 forward too; the left
 * hand: F#3 forward, G3 back, F3 / A3 sideways, G#3 / A#3 turn). Fire: C4, D4, E4, F4 or any key from
 * C5 up. PLAY starts from the title, then pauses and resumes. SELECT or any knob turns too. HOME
 * leaves. The emulator: W A S D move and strafe, arrows move and turn, Space fires, Enter is PLAY, Esc
 * leaves. */

#define MM_W 16
#define MM_VY0 16                      /* the view: rows 16..199 */
#define MM_VY1 200
#define MM_HZ ((MM_VY0 + MM_VY1) / 2)
#define MM_NENT 40
enum { ME_NONE, ME_BUG, ME_CHG, ME_ERR, ME_COFFEE, ME_TINY,   /* ME_TINY: little bugs, in swarms of three */
       ME_GRAPH, ME_CODE, ME_GHOUL, ME_PORTAL };               /* level 2: incident graphs, bad code, ghouls and their portals */
#define MM_PORTAL_SPAWNS 4                                     /* ghouls a portal lets out, at most */
enum { SFX_CLICK = 1, SFX_HIT, SFX_KILL, SFX_HURT, SFX_PICK, SFX_WIN, SFX_DIE, SFX_TITLE };

typedef struct { float x, y, hp, flash, dying, cool; uint8_t kind, alive, n; } mm_ent;   /* n: a portal's ghouls so far */
typedef struct {
    uint8_t on, was_playing, level, state;     /* state 0 play, 1 cleared, 2 down, 3 the title, 4 joining the bridge */
    uint8_t paused;                            /* PLAY during play */
    float px, py, dir, uptime, fire_cool, recoil, hurt, t, mttr, state_t, spin, held;   /* spin: turning speed; held: how long a turn key has been down */
    uint32_t last_ms, prev_btn, prev_keys;
    int open, total;
    char map[MM_W * MM_W];
    mm_ent e[MM_NENT];
    int16_t top[PLAT_W], bot[PLAT_W], htop[PLAT_W];   /* per column: the wall's drawn span and its true top */
    uint8_t wtype[PLAT_W], tex[PLAT_W], shade[PLAT_W];
    float zbuf[PLAT_W];
    char log[3][40];
    uint16_t log_col[3];
} moom_t;
static moom_t mm RV_RAMBSS;
static int moom_on(void) { return mm.on; }

/* '#' rack, 'L' log wall, 'D' dashboard, 'X' the exit (opens when all is resolved);
 * 'P' start, 'b' bug, 't' a swarm of three little bugs, 'c' failed change, 'e' 500 error, 'k' coffee.
 * Level 2, inside the call: 'W' the desktop, 'B' a browser window, 'V' the call's video tiles;
 * 'g' an incident graph, 'd' lines of code from a bad change, 'r' a CHANGE REQUEST portal */
static const char *const mm_levels[2][MM_W] = {
    {"################",
     "#P..#....L.....#",
     "#...#..b.L..c..#",
     "#...D....t....e#",
     "#.....##LL..#..#",
     "#..b..#.....#..#",
     "##.####..k..D..#",
     "#..t...#....#.t#",
     "#.c....#.b..##.#",
     "#..LL.....t....#",
     "#..L.....##.e..#",
     "#..L..k..##....#",
     "#.....b....t...#",
     "####D....LL..c.#",
     "#e.......L....XX",
     "################"},
    {"WWWWWWWWWWWWWWWW",
     "WP...B....V....W",
     "W....B..g.V..d.W",
     "W.d......r.....W",
     "WVVV..WWW...BB.W",
     "W.g.......W..d.W",
     "W....k....W.g..W",
     "W...BBB...W....W",
     "W.d.......V..r.W",
     "W...r.W.......dW",
     "WW..g.W...WWW..W",
     "W.....B..d.Wk..W",
     "W.d...B....W...W",
     "W.....B..g.V...W",
     "W...........XX.W",
     "WWWWWWWWWWWWWWWW"}};
static const char *const mm_names[2] = {"DATA CENTER A", "INSIDE THE BRIDGE CALL"};

/* each kind: how fast it comes, how much uptime it takes a second up close, its hit points, how
 * wide a target it is, how big it's drawn */
static const struct { float speed, dps, hp, hitw, size; } mm_kind[10] = {
    {0, 0, 0, 0, 0},
    {1.1f, 7.0f, 1.0f, 0.32f, 0.8f},      /* bug */
    {0.7f, 10.0f, 2.0f, 0.32f, 0.8f},     /* failed change */
    {1.5f, 14.0f, 3.0f, 0.32f, 0.8f},     /* 500 error */
    {0, 0, 0, 0, 0.8f},                   /* coffee */
    {2.1f, 3.0f, 1.0f, 0.2f, 0.34f},      /* little bug */
    {0.6f, 8.0f, 3.0f, 0.36f, 0.75f},     /* incident graph (and it pages you from afar) */
    {2.4f, 5.0f, 1.0f, 0.3f, 0.62f},      /* bad code */
    {1.6f, 16.0f, 3.0f, 0.34f, 1.0f},     /* ghoul */
    {0, 0, 6.0f, 0.42f, 1.15f},           /* CHANGE REQUEST portal */
};

static uint32_t mm_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    return x ^ (x >> 16);
}
static uint16_t mm_dim(uint16_t c, int s)      /* s: 0..255 brightness */
{
    int r = (c >> 11) * s >> 8, g = ((c >> 5) & 63) * s >> 8, b = (c & 31) * s >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static void mm_log(const char *s, uint16_t c)
{
    int i;
    for (i = 0; i < 2; i++) {
        __builtin_memcpy(mm.log[i], mm.log[i + 1], sizeof mm.log[i]);
        mm.log_col[i] = mm.log_col[i + 1];
    }
    for (i = 0; i < 39 && s[i]; i++)
        mm.log[2][i] = s[i];
    mm.log[2][i] = 0;
    mm.log_col[2] = c;
}
static void mm_logn(const char *a, int n, const char *b, uint16_t c)
{
    char s[40] = {0};
    int k = sx_cat(s, a);
    k = sx_int(s + k, n);
    sx_cat(s, b);
    mm_log(s, c);
}

static void mm_sfx(int k) { moom_sfx_req = (uint8_t)k; }

static void mm_load(int lv)
{
    int x, y, n = 0;
    mm.level = (uint8_t)(lv & 1);
    __builtin_memset(mm.e, 0, sizeof mm.e);
    mm.open = 0;
    for (y = 0; y < MM_W; y++)
        for (x = 0; x < MM_W; x++) {
            char c = mm_levels[mm.level][y][x];
            mm.map[y * MM_W + x] = (c == '#' || c == 'L' || c == 'D' || c == 'X' || c == 'W' || c == 'B' || c == 'V') ? c : '.';
            if (c == 'P') {
                mm.px = x + 0.5f;
                mm.py = y + 0.5f;
                mm.dir = 0.0f;
            } else if (c == 't') {                       /* a swarm: three little bugs */
                int k;
                for (k = 0; k < 3 && n < MM_NENT; k++) {
                    mm_ent *e = &mm.e[n++];
                    e->x = x + 0.3f + 0.2f * k;
                    e->y = y + 0.35f + 0.15f * (k & 1);
                    e->alive = 1;
                    e->kind = ME_TINY;
                    e->hp = 1.0f;
                    mm.open++;
                }
            } else if ((c == 'b' || c == 'c' || c == 'e' || c == 'k' || c == 'g' || c == 'd' || c == 'r') && n < MM_NENT) {
                mm_ent *e = &mm.e[n++];
                e->x = x + 0.5f;
                e->y = y + 0.5f;
                e->alive = 1;
                e->kind = c == 'b' ? ME_BUG : c == 'c' ? ME_CHG : c == 'e' ? ME_ERR : c == 'g' ? ME_GRAPH
                        : c == 'd' ? ME_CODE : c == 'r' ? ME_PORTAL : ME_COFFEE;
                e->hp = mm_kind[e->kind].hp;
                e->cool = e->kind == ME_PORTAL ? 2.5f : e->kind == ME_GRAPH ? 1.5f + 0.4f * (float)(x & 3) : 0.0f;
                if (e->kind != ME_COFFEE)
                    mm.open++;
            }
        }
    mm.total = mm.open;
    mm.uptime = 100.0f;
    mm.state = 0;
    mm.mttr = 0.0f;
    if (mm.level) {
        mm_log("JOINED THE BRIDGE. CAMERA ON.", RGB(150, 140, 240));
        mm_logn("SCOPE: ", mm.open, " OPEN + CHANGE REQUESTS", C_AMBER);
    } else {
        mm_log("SEV1 DECLARED  BRIDGE OPEN", C_RED);
        mm_logn("SCOPE: ", mm.open, " OPEN ISSUES", C_AMBER);
    }
    mm_log(mm_names[mm.level], C_TEAL);
}

static int mm_wall(float x, float y)
{
    int ix = (int)x, iy = (int)y;
    char c;
    if (ix < 0 || iy < 0 || ix >= MM_W || iy >= MM_W)
        return 1;
    c = mm.map[iy * MM_W + ix];
    return c != '.' && !(c == 'X' && mm.open == 0);
}

void moom_start(void)
{
    __builtin_memset(&mm, 0, sizeof mm);
    mm.on = 1;
    mm.was_playing = rv_live.playing;
    if (rv_live.playing)
        rv_transport(0);
    rv_all_off();
    mm.last_ms = plat_ms();
    mm.prev_btn = plat_buttons();
    mm.prev_keys = plat_keys();
    mm_load(0);
    mm.state = 3;                                     /* the title card and its riff first */
    mm.state_t = 4.5f;
    mm_sfx(SFX_TITLE);
}

/* straight to a level (the emulator's tests and screenshots); level 2 starts with joining the call */
void moom_warp(int lv)
{
    if (!mm.on)
        moom_start();
    mm_load(lv);
    mm.state = lv ? 4 : 0;
    mm.state_t = 3.2f;
}

void moom_pose(float x, float y, float dir) { mm.px = x, mm.py = y, mm.dir = dir; }   /* (the screenshots) */

static void moom_stop(void);
int moom_active(void) { return mm.on; }
void moom_exit(void)
{
    if (mm.on)
        moom_stop();
}

static void moom_stop(void)
{
    mm.on = 0;
    moom_music = 0;
    ui.prev_btn = plat_buttons();          /* no button or key edge leaks into the synth */
    ui.prev_keys = plat_keys();
    if (mm.was_playing)
        rv_transport(1);
}

/* ------------------------------------------------------------ the world --- */
static void mm_move(float dx, float dy)
{
    const float r = 0.22f;
    if (!mm_wall(mm.px + dx + (dx > 0 ? r : -r), mm.py))
        mm.px += dx;
    if (!mm_wall(mm.px, mm.py + dy + (dy > 0 ? r : -r)))
        mm.py += dy;
}

static int mm_sees(float x0, float y0, float x1, float y1)   /* a clear line of sight */
{
    float dx = x1 - x0, dy = y1 - y0, d = fm_sqrtf(dx * dx + dy * dy);
    int i, n = (int)(d * 4.0f) + 1;
    for (i = 1; i < n; i++)
        if (mm_wall(x0 + dx * i / n, y0 + dy * i / n))
            return 0;
    return 1;
}

static void mm_fire(void)
{
    float cx = fm_cosf(mm.dir), cy = fm_sinf(mm.dir), best = 1e9f;
    int i, hit = -1;
    for (i = 0; i < MM_NENT; i++) {
        mm_ent *e = &mm.e[i];
        float dx, dy, along, side;
        if (!e->alive || e->dying > 0.0f || e->kind == ME_COFFEE)
            continue;
        dx = e->x - mm.px;
        dy = e->y - mm.py;
        along = dx * cx + dy * cy;
        side = -dx * cy + dy * cx;
        if (along > 0.1f && fm_fabsf(side) < mm_kind[e->kind].hitw && along < best && along < mm.zbuf[PLAT_W / 2] + 0.3f) {
            best = along;
            hit = i;
        }
    }
    mm.fire_cool = 0.28f;
    mm.recoil = 1.0f;
    if (hit < 0) {
        mm_sfx(SFX_CLICK);
        return;
    }
    {
        mm_ent *e = &mm.e[hit];
        e->hp -= 1.0f;
        e->flash = 0.12f;
        if (e->hp <= 0.0f) {
            static const char *const done[10] = {"", "INFO BUG FIXED, TICKET CLOSED", "INFO CHANGE ROLLED BACK",
                                                 "INFO 500S CLEARED, SVC HEALTHY", "", "INFO MINOR BUG SQUASHED",
                                                 "INFO ALERT ACKED, GRAPH GREEN", "INFO BAD COMMIT REVERTED",
                                                 "INFO REGRESSION EXORCISED", "INFO CHANGE REQUEST REJECTED"};
            e->dying = 0.6f;
            mm.open--;
            mm_sfx(SFX_KILL);
            mm_log(done[e->kind], C_TEAL);
            if (mm.open == 0) {
                mm_log("ALL CLEAR: EXIT ACCESS GRANTED", RGB(110, 210, 120));
                mm_sfx(SFX_WIN);
            }
        } else
            mm_sfx(SFX_HIT);
    }
}

static void mm_update(float dt, uint32_t keys, uint32_t down_keys, uint32_t btn, uint32_t down_btn, int turn)
{
    float cx, cy, fwd = 0.0f, side = 0.0f, sp = 2.6f * dt;
    int i;
    if (!mm.paused)
        mm.t += dt;
    if (mm.state == 3) {                               /* the title: on its own, or PLAY / a key skips it */
        mm.state_t -= dt;
        if (mm.state_t <= 0.0f || (mm.state_t < 4.0f && ((down_btn & (1u << B_PLAY)) || down_keys)))
            mm.state = 0;
        return;
    }
    if (mm.state == 4 && mm.state_t < 2.6f && ((down_btn & (1u << B_PLAY)) || down_keys))
        mm.state_t = 0.0f;                             /* joining: PLAY or a key skips the zoom */
    if (mm.state != 0) {                               /* a message, then on */
        mm.state_t -= dt;
        if (mm.state_t <= 0.0f) {
            if (mm.state == 1 && mm.level == 0) {      /* level 1 done: into the bridge call */
                mm_load(1);
                mm.state = 4;
                mm.state_t = 3.2f;
            } else if (mm.state == 1)
                mm_load(mm.level + 1);
            else if (mm.state == 4)
                mm.state = 0;
            else
                mm_load(mm.level);
        }
        return;
    }
    if (down_btn & (1u << B_PLAY)) {                   /* PLAY: pause, and again to go on */
        mm.paused ^= 1;
        mm.held = mm.spin = 0.0f;
    }
    if (mm.paused)
        return;
    mm.mttr += dt;
    mm.dir += (float)turn * 0.085f;
    if (keys & ((1u << 15) | (1u << 8) | (1u << 1)))   /* G#4 (up), C#4, F#3: forward */
        fwd += 1.0f;
    if (keys & ((1u << 16) | (1u << 2)))               /* A4 (down), G3: back */
        fwd -= 1.0f;
    if (keys & (1u << 0))                              /* F3 / A3: sideways */
        side -= 1.0f;
    if (keys & (1u << 4))
        side += 1.0f;
    {   /* turning like an analog stick: it eases in, builds while held, and glides to a stop */
        int want = ((keys & ((1u << 18) | (1u << 5))) ? 1 : 0) - ((keys & ((1u << 14) | (1u << 3))) ? 1 : 0);   /* B4 / A#3 right, G4 / G#3 left */
        float target;
        mm.held = want ? mm.held + dt : 0.0f;
        target = (float)want * (0.9f + 1.3f * fm_minf(mm.held / 0.4f, 1.0f));   /* 0.9 rad/s at first, 2.2 rad/s after 0.4 s */
        mm.spin += (target - mm.spin) * fm_minf(dt * (want ? 9.0f : 10.0f), 1.0f);
        mm.dir += mm.spin * dt;
    }
    cx = fm_cosf(mm.dir);
    cy = fm_sinf(mm.dir);
    if (fwd != 0.0f || side != 0.0f)
        mm_move((cx * fwd - cy * side) * sp, (cy * fwd + cx * side) * sp);
    mm.fire_cool -= dt;
    mm.recoil *= fm_expf(-dt * 9.0f);
    mm.hurt -= dt;
    if (mm.fire_cool <= 0.0f && (down_keys & (0xFFF80000u | (1u << 7) | (1u << 9) | (1u << 11) | (1u << 12))))   /* C4 D4 E4 F4, C5 up */
        mm_fire();
    (void)btn;
    for (i = 0; i < MM_NENT; i++) {
        mm_ent *e = &mm.e[i];
        float dx, dy, d;
        if (!e->alive)
            continue;
        e->flash -= dt;
        if (e->dying > 0.0f) {
            e->dying -= dt;
            if (e->dying <= 0.0f)
                e->alive = 0;
            continue;
        }
        dx = mm.px - e->x;
        dy = mm.py - e->y;
        d = fm_sqrtf(dx * dx + dy * dy);
        if (e->kind == ME_COFFEE) {
            if (d < 0.5f && mm.uptime < 100.0f) {
                e->alive = 0;
                mm.uptime = fm_minf(100.0f, mm.uptime + 30.0f);
                mm_sfx(SFX_PICK);
                mm_log("COFFEE: UPTIME +30%", C_AMBER);
            }
            continue;
        }
        if (e->kind == ME_PORTAL) {                    /* a CHANGE REQUEST: lets a ghoul out now and then, while you're near */
            e->cool -= dt;
            if (e->cool <= 0.0f && e->n < MM_PORTAL_SPAWNS && d < 8.0f && mm_sees(e->x, e->y, mm.px, mm.py)) {
                int k;
                for (k = 0; k < MM_NENT && mm.e[k].alive; k++)
                    ;
                if (k < MM_NENT) {
                    mm_ent *g = &mm.e[k];
                    __builtin_memset(g, 0, sizeof *g);
                    g->x = e->x + dx / d * 0.35f;
                    g->y = e->y + dy / d * 0.35f;
                    g->alive = 1;
                    g->kind = ME_GHOUL;
                    g->hp = mm_kind[ME_GHOUL].hp;
                    e->n++;
                    mm.open++;
                    mm_sfx(SFX_HURT);
                    mm_log("WARN CR APPROVED: SOMETHING CAME THRU", RGB(190, 90, 220));
                }
                e->cool = 4.5f;
            }
            continue;
        }
        if (e->kind == ME_GRAPH && d > 0.6f && d < 7.0f && mm_sees(e->x, e->y, mm.px, mm.py)) {   /* it pages you */
            e->cool -= dt;
            if (e->cool <= 0.0f) {
                static const char *const pg[3] = {"CRIT P1 ALERT: LATENCY P99 SPIKE", "CRIT P1 ALERT: ERROR RATE 40%", "CRIT P1 ALERT: SATURATION 100%"};
                e->cool = 2.4f;
                mm.uptime -= 5.0f;
                mm.hurt = 0.25f;
                mm_sfx(SFX_HURT);
                mm_log(pg[mm_hash((uint32_t)i + (uint32_t)(mm.t * 10.0f)) % 3], C_RED);
            }
        }
        if (d > 0.55f && d < 9.0f && mm_sees(e->x, e->y, mm.px, mm.py)) {
            float v = mm_kind[e->kind].speed * dt / d;
            float w = e->kind == ME_TINY ? fm_sinf(mm.t * 9.0f + (float)i * 2.1f) * 0.6f   /* little bugs zig-zag, */
                    : e->kind == ME_CODE ? fm_sinf(mm.t * 13.0f + (float)i) * 0.5f       /* code glitches sideways, */
                    : e->kind == ME_GHOUL ? fm_sinf(mm.t * 2.3f + (float)i * 1.7f) * 0.35f : 0.0f;   /* ghouls stalk */
            float nx = e->x + (dx - dy * w) * v, ny = e->y + (dy + dx * w) * v;
            if (!mm_wall(nx, e->y))
                e->x = nx;
            if (!mm_wall(e->x, ny))
                e->y = ny;
        } else if (d <= 0.6f) {                        /* close enough to hurt */
            mm.uptime -= mm_kind[e->kind].dps * dt;
            e->cool -= dt;
            if (e->cool <= 0.0f) {
                e->cool = 0.6f;
                mm.hurt = 0.25f;
                mm_sfx(SFX_HURT);
            }
        }
    }
    if (mm.uptime <= 0.0f) {
        mm.uptime = 0.0f;
        mm.state = 2;
        mm.state_t = 3.0f;
        mm_sfx(SFX_DIE);
        mm_log("SLA BREACHED: ESCALATED TO EXECS", C_RED);
    }
    if (mm.open == 0 && (int)mm.px >= MM_W - 3 && (int)mm.py >= MM_W - 3) {   /* through the exit */
        mm.state = 1;
        mm.state_t = 3.5f;
        mm_sfx(SFX_WIN);
        mm_log(mm.level ? "CHANGE ROLLED BACK. BRIDGE CLOSED." : "RESOLVED. RCA DUE FRIDAY", RGB(110, 210, 120));
    }
}

/* --------------------------------------------------------- the picture --- */
static void mm_cast(void)
{
    float cx = fm_cosf(mm.dir), cy = fm_sinf(mm.dir), plx = -cy * 0.66f, ply = cx * 0.66f;
    int x;
    for (x = 0; x < PLAT_W; x++) {
        float cam = 2.0f * x / PLAT_W - 1.0f, rx = cx + plx * cam, ry = cy + ply * cam;
        int mx = (int)mm.px, my = (int)mm.py, sx, sy, side = 0, i, h;
        float ddx = rx == 0.0f ? 1e30f : fm_fabsf(1.0f / rx), ddy = ry == 0.0f ? 1e30f : fm_fabsf(1.0f / ry), sdx, sdy, d, wx;
        char c = '#';
        sx = rx < 0 ? -1 : 1;
        sy = ry < 0 ? -1 : 1;
        sdx = (rx < 0 ? mm.px - mx : mx + 1.0f - mm.px) * ddx;
        sdy = (ry < 0 ? mm.py - my : my + 1.0f - mm.py) * ddy;
        for (i = 0; i < 64; i++) {
            if (sdx < sdy) {
                sdx += ddx;
                mx += sx;
                side = 0;
            } else {
                sdy += ddy;
                my += sy;
                side = 1;
            }
            if (mx < 0 || my < 0 || mx >= MM_W || my >= MM_W)
                break;
            c = mm.map[my * MM_W + mx];
            if (c != '.' && !(c == 'X' && mm.open == 0))
                break;
        }
        d = side == 0 ? sdx - ddx : sdy - ddy;
        if (d < 0.05f)
            d = 0.05f;
        wx = side == 0 ? mm.py + d * ry : mm.px + d * rx;
        wx -= fm_floorf(wx);
        h = (int)((MM_VY1 - MM_VY0) / d);
        mm.zbuf[x] = d;
        mm.htop[x] = (int16_t)(MM_HZ - h / 2);
        mm.top[x] = (int16_t)(MM_HZ - h / 2 < MM_VY0 ? MM_VY0 : MM_HZ - h / 2);
        mm.bot[x] = (int16_t)(MM_HZ + h / 2 > MM_VY1 ? MM_VY1 : MM_HZ + h / 2);
        mm.wtype[x] = (uint8_t)c;
        mm.tex[x] = (uint8_t)(wx * 255.0f);
        mm.shade[x] = (uint8_t)clampi((int)(255.0f / (1.0f + d * 0.22f)) - side * 40, 40, 255);
    }
}

static float mm_seg(float u, float v, float x0, float y0, float x1, float y1);

/* the walls, made in code: u, v in 0..255 */
static uint16_t mm_wall_px(int type, int u, int v, int x)
{
    uint32_t t = (uint32_t)(mm.t * 8.0f);
    switch (type) {
    case 'L': {                                        /* a log viewer: lines of text scrolling up, coloured by level */
        int row = (v + (int)(mm.t * 40.0f)) / 14, in = (v + (int)(mm.t * 40.0f)) % 14;
        uint32_t h = mm_hash((uint32_t)row * 977u + (uint32_t)x / 8u * 0u + 31u);
        int len = 90 + (int)(h % 150), lvl = (int)(h >> 8) % 10;
        uint16_t tc = lvl == 0 ? C_RED : lvl < 3 ? C_AMBER : lvl < 6 ? C_TEAL : RGB(120, 190, 130);
        if (in < 3 || in > 10 || u < 14 || u > len)
            return RGB(10, 16, 13);
        if (u < 34)
            return C_DIM;                              /* the timestamp */
        return (mm_hash((uint32_t)row * 131u + (uint32_t)u / 6u) & 3) ? tc : RGB(10, 16, 13);   /* words */
    }
    case 'D': {                                        /* a dashboard: a grid and a live series with a spike */
        int f, g = (u % 32 == 0) || (v % 32 == 0);
        float ph = (float)u * 0.05f + mm.t * 1.7f;
        f = 140 + (int)(fm_sinf(ph) * 30.0f + fm_sinf(ph * 2.3f) * 12.0f);
        if (((u + (int)(mm.t * 60.0f)) & 255) > 200)
            f -= 70;                                   /* an incident in the graph */
        if (v < 18)
            return v < 16 ? RGB(28, 38, 44) : C_LINE;  /* the panel's title bar */
        if (v > f - 3 && v < f + 3)
            return ((u + (int)(mm.t * 60.0f)) & 255) > 200 ? C_RED : C_TEAL;
        if (v > f)
            return RGB(18, 40, 46);                    /* the area under the line */
        return g ? RGB(30, 40, 36) : RGB(14, 22, 20);
    }
    case 'W': {                                        /* the desktop: wallpaper, icons down the left, the taskbar */
        int wave = 120 + (int)(fm_sinf((float)u * 0.025f + mm.t * 0.35f) * 34.0f), k = (v - 12) / 46, iv = (v - 12) % 46;
        if (v > 230) {                                 /* the taskbar */
            if (v > 234 && v < 252) {
                if (u > 8 && u < 26)
                    return (u - 17) * (u - 17) + (v - 243) * (v - 243) < 60 ? RGB(90, 170, 230) : RGB(26, 28, 34);   /* the start button */
                if (u > 34 && u < 112 && (u - 34) % 26 < 18)
                    return (mm_hash((uint32_t)(u - 34) / 26u * 7u) & 1) ? RGB(220, 170, 70) : RGB(110, 120, 220);   /* pinned apps */
                if (u > 196 && u < 246 && v > 237 && v < 249 && ((u / 5) & 1))
                    return RGB(200, 205, 215);         /* the clock */
            }
            return RGB(26, 28, 34);
        }
        if (u > 10 && u < 40 && k < 4 && v >= 12) {    /* desktop icons and their labels */
            if (iv < 26 && u > 14 && u < 36)
                return k == 0 ? RGB(240, 200, 80) : k == 1 ? RGB(80, 160, 240) : k == 2 ? RGB(230, 230, 236) : RGB(120, 200, 120);
            if (iv > 29 && iv < 33)
                return RGB(220, 226, 236);
        }
        if (v > wave - 14 && v < wave + 14)            /* a soft band across the wallpaper */
            return RGB(40 + (wave + 14 - v) * 2, 70 + (wave + 14 - v) * 3, 150 + (wave + 14 - v) * 2);
        return RGB(14 + v / 12, 28 + v / 10, 70 + v / 5);
    }
    case 'B': {                                        /* a browser window: tabs, the address bar, a status page in trouble */
        if (u < 4 || u > 251)
            return RGB(48, 50, 58);
        if (v < 16)                                    /* the tab strip */
            return u > 8 && u < 112 && v > 3 ? (u > 14 && u < 22 && v > 7 && v < 13 ? C_RED : RGB(60, 62, 72)) : RGB(34, 36, 42);
        if (v < 34) {                                  /* the toolbar and its address field */
            if (u > 30 && u < 232 && v > 19 && v < 31) {
                if (u > 34 && u < 40 && v > 22 && v < 28)
                    return RGB(90, 200, 110);          /* the padlock */
                return u > 46 && u < 170 && v > 23 && v < 27 && ((u / 6) % 5) ? RGB(150, 154, 166) : RGB(30, 32, 38);
            }
            return (u > 8 && u < 24 && v > 21 && v < 29) ? RGB(120, 124, 134) : RGB(60, 62, 72);
        }
        if (v > 40 && v < 64 && u > 12 && u < 244) {   /* the banner: MAJOR INCIDENT, blinking */
            int on = ((int)(mm.t * 2.0f) & 1) == 0;
            if (v > 48 && v < 56 && u > 22 && u < 150 && ((u / 7) % 4))
                return C_WHITE;
            return on ? RGB(210, 40, 50) : RGB(150, 26, 36);
        }
        if (v > 72 && v < 220) {                       /* the components: a dot, a name, a state */
            int row = (v - 72) / 22, iv = (v - 72) % 22;
            uint32_t h = mm_hash((uint32_t)row * 59u + 3u);
            if (iv > 6 && iv < 15) {
                if (u > 14 && u < 24)
                    return (h & 3) == 0 ? RGB(90, 200, 110) : (h & 3) == 1 ? C_AMBER : C_RED;
                if (u > 32 && u < 32 + 40 + (int)(h % 70) && ((u / 5) % 6))
                    return RGB(90, 94, 104);
                if (u > 190 && u < 236)
                    return (h & 3) == 0 ? RGB(200, 236, 206) : RGB(250, 214, 210);
            }
            if (iv == 21)
                return RGB(220, 222, 228);
        }
        return RGB(244, 245, 248);
    }
    case 'V': {                                        /* the call: four video tiles, someone always talking, the toolbar */
        int col = u < 128, row = v < 112, tx = col ? u - 8 : u - 132, ty = row ? v - 8 : v - 116, tile = (row ? 0 : 2) + (col ? 0 : 1);
        int talk = (int)(mm.t * 0.7f) & 3;
        if (v > 226) {
            if (v > 232 && v < 248 && u > 104 && u < 152)
                return RGB(200, 40, 50);               /* leave */
            if (v > 234 && v < 246 && ((u > 64 && u < 80) || (u > 84 && u < 100) || (u > 156 && u < 172) || (u > 176 && u < 192)))
                return RGB(70, 72, 84);
            return RGB(28, 28, 36);
        }
        if (tx < 0 || ty < 0 || tx > 116 || ty > 100)
            return RGB(18, 18, 24);
        if (tile == talk && (tx < 3 || ty < 3 || tx > 113 || ty > 97))
            return RGB(130, 120, 240);                 /* the speaker's ring */
        {
            int ax = tx - 58, ay = ty - 46, r2 = ax * ax + ay * ay;
            uint32_t h = mm_hash((uint32_t)tile * 31u + 7u);
            if (r2 < 24 * 24)
                return r2 > 22 * 22 ? RGB(230, 230, 236) : RGB(60 + (int)(h & 127), 50 + (int)((h >> 8) & 127), 90 + (int)((h >> 16) & 127));
            if (ty > 84 && ty < 92 && tx > 6 && tx < 50)
                return ((tx / 5) & 1) ? RGB(200, 200, 210) : RGB(44, 44, 56);   /* a name */
            if (ty > 82 && ty < 94 && tx > 98 && tx < 110 && tile != talk)
                return C_RED;                          /* muted */
        }
        return RGB(44, 44, 56);
    }
    case 'X':                                          /* the exit: a red door, green once all is clear */
        if (u < 16 || u > 240 || v < 10)
            return RGB(60, 64, 62);
        if (mm.level && mm.open)                        /* in the call: LEAVE, red, a handset on it */
            return mm_seg((float)u, (float)v, 90.0f, 128.0f, 166.0f, 128.0f) < 10.0f || (fm_fabsf((float)u - 128.0f) < 50.0f && v > 120 && v < 146 && (u < 96 || u > 160)) ? C_WHITE : RGB(170, 34, 44);
        return mm.open ? ((v / 20 + u / 40) & 1 ? RGB(150, 40, 40) : RGB(120, 30, 30)) : RGB(60, 170, 90);
    default: {                                         /* a server rack: units, vents, blinking lights */
        int unit = v / 21, iv = v % 21;
        if (u < 18 || u > 237)
            return RGB(40, 44, 46);                    /* the rails */
        if (iv == 0)
            return RGB(18, 20, 22);
        if (u > 30 && u < 40 && iv > 7 && iv < 13) {
            uint32_t h = mm_hash((uint32_t)unit * 13u + (uint32_t)x / 64u + (t >> (unit & 3)));
            return (h & 3) == 0 ? C_AMBER : (h & 7) == 1 ? C_RED : RGB(80, 220, 120);
        }
        if (u > 60 && u < 220 && iv > 5 && iv < 16 && (u & 3) == 0)
            return RGB(22, 26, 28);                    /* vents */
        return RGB(48, 54, 58);
    }
    }
}

/* the distance from (u, v) to the segment (x0, y0)-(x1, y1) */
static float mm_seg(float u, float v, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, k = ((u - x0) * dx + (v - y0) * dy) / (dx * dx + dy * dy), px, py;
    k = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
    px = x0 + k * dx - u;
    py = y0 + k * dy - v;
    return fm_sqrtf(px * px + py * py);
}

/* the sprites: u, v in 0..1; returns 0 for see-through */
static uint16_t mm_sprite_px(const mm_ent *e, float u, float v)
{
    float t = mm.t, dx, dy;
    if (e->dying > 0.0f) {                             /* resolved: a green check that fades */
        float a = e->dying / 0.6f, d1, d2;
        d1 = mm_seg(u, v, 0.24f, 0.52f, 0.42f, 0.70f);   /* the short stroke, then the long one */
        d2 = mm_seg(u, v, 0.42f, 0.70f, 0.80f, 0.26f);
        if (d1 < 0.055f || d2 < 0.055f)
            return mm_dim(RGB(90, 230, 120), (int)(255 * a));
        return 0;
    }
    if (e->flash > 0.0f) {
        uint16_t c = mm_sprite_px(&(mm_ent){e->x, e->y, e->hp, 0.0f, 0.0f, 0.0f, e->kind, 1, 0}, u, v);
        return c ? C_WHITE : 0;
    }
    switch (e->kind) {
    case ME_TINY:                                      /* a little bug: the same beetle, smaller, yellow-green, quicker legs */
    case ME_BUG: {                                     /* a beetle: body, head, six legs that walk, antennae */
        int tiny = e->kind == ME_TINY;
        float leg = fm_sinf(t * (tiny ? 34.0f : 18.0f)) * 0.03f;
        dx = (u - 0.5f) / 0.26f;
        dy = (v - 0.62f) / 0.22f;
        if (dx * dx + dy * dy < 1.0f)
            return fm_fabsf(u - 0.5f) < 0.012f ? RGB(60, 0, 20) : tiny ? RGB(190, 200, 40) : RGB(200, 30, 70);
        dx = (u - 0.5f) / 0.12f;
        dy = (v - 0.36f) / 0.1f;
        if (dx * dx + dy * dy < 1.0f)
            return (fm_fabsf(u - 0.45f) < 0.03f || fm_fabsf(u - 0.55f) < 0.03f) && fm_fabsf(v - 0.34f) < 0.03f ? C_WHITE : RGB(110, 10, 40);
        {
            int k;
            for (k = 0; k < 3; k++) {
                float ly = 0.52f + k * 0.1f + ((k & 1) ? leg : -leg);
                if (fm_fabsf(v - ly - (u - 0.5f) * 0.25f) < 0.018f && fm_fabsf(u - 0.5f) > 0.2f && fm_fabsf(u - 0.5f) < 0.42f)
                    return RGB(70, 10, 30);
            }
        }
        if (v < 0.28f && v > 0.12f && (fm_fabsf(u - 0.42f + (0.28f - v) * 0.6f) < 0.015f || fm_fabsf(u - 0.58f - (0.28f - v) * 0.6f) < 0.015f))
            return RGB(70, 10, 30);
        return 0;
    }
    case ME_CHG: {                                     /* a change ticket: paper, a red header, CHG, a big red X */
        float bob = fm_sinf(t * 3.0f) * 0.02f;
        v -= bob;
        if (u < 0.22f || u > 0.78f || v < 0.12f || v > 0.9f)
            return 0;
        if (fm_fabsf((u - 0.22f) - (v - 0.2f) * 0.8f) < 0.05f || fm_fabsf((0.78f - u) - (v - 0.2f) * 0.8f) < 0.05f)
            return v > 0.22f ? C_RED : RGB(160, 20, 20);
        if (v < 0.24f)
            return RGB(160, 20, 20);
        if (v > 0.3f && ((int)(v * 40.0f) & 1) == 0 && u > 0.28f && u < 0.72f)
            return RGB(150, 150, 150);
        return RGB(232, 228, 214);
    }
    case ME_ERR: {                                     /* a 500 error box that glitches */
        float g = ((mm_hash((uint32_t)(v * 30.0f) + (uint32_t)(t * 12.0f)) & 15) == 0) ? 0.06f : 0.0f;
        static const uint16_t dig[3] = {0x79CF, 0x7B6F, 0x7B6F};   /* 5, 0, 0 as 3 x 5 bitmaps */
        u -= g;
        if (u < 0.1f || u > 0.9f || v < 0.22f || v > 0.82f)
            return 0;
        if (v < 0.32f)
            return u > 0.8f && u < 0.86f ? C_WHITE : RGB(170, 20, 30);
        if (v > 0.42f && v < 0.72f && u > 0.2f && u < 0.8f) {
            int d = (int)((u - 0.2f) / 0.2f), cx = (int)(((u - 0.2f) - d * 0.2f) / 0.2f * 4.0f), cy = (int)((v - 0.42f) / 0.3f * 5.0f);
            if (d < 3 && cx < 3 && ((dig[d] >> (14 - (cy * 3 + cx))) & 1))
                return C_WHITE;
        }
        return RGB(220, 40, 50);
    }
    case ME_GRAPH: {                                   /* an incident graph: a monitor panel, its red line spiking at you */
        float f, ph = u * 9.0f + t * 2.2f;
        if (u < 0.1f || u > 0.9f || v < 0.16f || v > 0.8f)
            return 0;
        if (u < 0.13f || u > 0.87f || v > 0.77f)
            return RGB(60, 64, 74);                    /* the bezel */
        if (v < 0.27f)                                 /* its title bar, an alert light that pulses */
            return (fm_fabsf(u - 0.8f) < 0.035f && fm_fabsf(v - 0.215f) < 0.035f) ? mm_dim(C_RED, 120 + (int)(fm_sinf(t * 10.0f) * 120.0f + 15.0f))
                   : (u > 0.18f && u < 0.6f && fm_fabsf(v - 0.215f) < 0.015f && ((int)(u * 60.0f) % 4)) ? RGB(220, 220, 230) : RGB(40, 44, 54);
        f = 0.6f - 0.06f * fm_sinf(ph) - 0.03f * fm_sinf(ph * 2.7f);
        if (fm_fabsf(fm_sinf(u * 3.0f + t * 0.9f)) > 0.82f)
            f -= 0.25f * (fm_fabsf(fm_sinf(u * 3.0f + t * 0.9f)) - 0.82f) / 0.18f;   /* the spikes */
        if (fm_fabsf(v - f) < 0.022f)
            return f < 0.48f ? RGB(255, 60, 60) : RGB(255, 170, 60);
        if (v > f)
            return f < 0.48f ? RGB(80, 14, 20) : RGB(60, 30, 16);
        if (fm_fabsf(v - 0.42f) < 0.006f)
            return RGB(150, 30, 30);                   /* the threshold */
        return ((int)(u * 20.0f) % 4 == 0 || (int)(v * 20.0f) % 4 == 0) ? RGB(26, 30, 36) : RGB(14, 16, 22);
    }
    case ME_CODE: {                                    /* lines of code from a bad change: a diff, red and green, glitching */
        int row = (int)((v - 0.24f) / 0.065f);
        float g = (mm_hash((uint32_t)row * 17u + (uint32_t)(t * 14.0f)) & 7) == 0 ? 0.07f : 0.0f;
        uint32_t h = mm_hash((uint32_t)row * 977u + 5u);
        int minus = (h & 3) != 0, word;
        u -= g;
        if (u < 0.06f || u > 0.94f || v < 0.24f || v > 0.24f + 0.065f * 8)
            return 0;
        if (fm_fabsf(v - (0.24f + row * 0.065f + 0.0325f)) > 0.024f)
            return minus ? RGB(60, 10, 16) : RGB(10, 44, 20);
        if (u < 0.14f)
            return RGB(110, 110, 120);                 /* line numbers */
        if (u < 0.2f)                                  /* - or + */
            return (fm_fabsf(u - 0.17f) < 0.02f || (!minus && fm_fabsf(v - (0.24f + row * 0.065f + 0.0325f)) < 0.006f && fm_fabsf(u - 0.17f) < 0.02f) ||
                    fm_fabsf(v - (0.24f + row * 0.065f + 0.0325f)) < 0.007f) ? (minus ? RGB(255, 90, 90) : RGB(120, 240, 130)) : (minus ? RGB(60, 10, 16) : RGB(10, 44, 20));
        word = (int)((u - 0.2f) / 0.05f);
        if (u > 0.2f + 0.05f * (float)(4 + (h >> 8) % 12) || (mm_hash(h + (uint32_t)word) % 5) == 0)
            return minus ? RGB(60, 10, 16) : RGB(10, 44, 20);
        return minus ? ((mm_hash(h + (uint32_t)word) & 1) ? RGB(255, 140, 140) : RGB(240, 200, 120)) : RGB(150, 240, 160);
    }
    case ME_GHOUL: {                                   /* a ghoul from a change request: spined, many-eyed, dripping */
        float pulse = 0.5f + 0.5f * fm_sinf(t * 7.0f + e->x * 3.0f), tw = fm_sinf(t * 23.0f + e->y) * 0.012f, by, ex, k;
        int i;
        static const float eye[4][3] = {{0.44f, 0.40f, 0.032f}, {0.56f, 0.40f, 0.032f}, {0.40f, 0.455f, 0.018f}, {0.60f, 0.455f, 0.018f}};
        for (i = 0; i < 4; i++) {                      /* four eyes that burn */
            float a = u - eye[i][0], b = v - eye[i][1], r2 = a * a + b * b, rr = eye[i][2] * eye[i][2];
            if (r2 < rr)
                return r2 < rr * 0.25f ? RGB(255, 236, 160) : RGB(255, 40 + (int)(pulse * 80.0f), 20);
            if (r2 < rr * 2.6f)
                return mm_dim(RGB(255, 30, 20), 70 + (int)(pulse * 90.0f));   /* their glow */
        }
        if (v > 0.47f && v < 0.6f) {                   /* fangs */
            float q = (v - 0.47f) / 0.13f;
            if (fm_fabsf(u - 0.45f - q * 0.02f) < 0.025f * (1.0f - q) || fm_fabsf(u - 0.55f + q * 0.02f) < 0.025f * (1.0f - q) ||
                fm_fabsf(u - 0.5f) < 0.012f * (1.0f - q))
                return RGB(232, 226, 200);
        }
        if ((fm_fabsf(u - 0.465f) < 0.006f || fm_fabsf(u - 0.535f) < 0.006f) && v > 0.55f && v < 0.62f + 0.06f * fm_fabsf(fm_sinf(t * 1.3f + u * 9.0f)))
            return RGB(130, 230, 70);                  /* it drips */
        ex = (u - 0.5f) / 0.16f;
        k = (v - 0.43f) / 0.11f;
        if (ex * ex + k * k < 1.0f)
            return RGB(30, 8, 34);                     /* the head */
        ex = (u - 0.5f) / 0.33f;
        by = 0.63f - 0.22f * fm_sqrtf(fm_maxf(0.0f, 1.0f - ex * ex));   /* the top of its back */
        for (i = 0; i < 6; i++) {                      /* bone spines along it */
            float su = 0.24f + 0.104f * (float)i, w = 0.035f - fm_fabsf(u - su);
            if (w > 0.0f && v < by && v > by - 0.15f * (w / 0.035f) - (i == 2 || i == 3 ? 0.04f : 0.0f))
                return RGB(205, 196, 170);
        }
        k = (v - 0.63f) / 0.2f;
        if (ex * ex + k * k < 1.0f && v > by - 0.005f)   /* the carapace: ribbed, with red veins */
            return fm_fabsf(fm_sinf(u * 46.0f + v * 6.0f)) < 0.18f ? RGB(80, 20, 62)
                   : fm_fabsf(fm_sinf(u * 13.0f - v * 31.0f + 1.0f)) < 0.05f ? RGB(150, 20, 30) : RGB(38, 12, 44);
        for (i = 0; i < 8; i++) {                      /* long legs, jointed, twitching */
            float sd = i < 4 ? -1.0f : 1.0f, j = (float)(i & 3);
            float x0 = 0.5f + sd * 0.2f, y0 = 0.62f + j * 0.04f, x1 = 0.5f + sd * (0.4f + j * 0.025f), y1 = 0.42f + j * 0.06f + ((i & 1) ? tw : -tw);
            float x2 = 0.5f + sd * (0.44f + j * 0.015f), y2 = 0.98f;
            if (mm_seg(u, v, x0, y0, x1, y1) < 0.013f || mm_seg(u, v, x1, y1, x2, y2) < 0.01f)
                return RGB(56, 18, 56);
        }
        {   /* a ghastly haze around it that comes and goes */
            float a = (u - 0.5f) / 0.48f, b = (v - 0.6f) / 0.42f;
            if (a * a + b * b < 1.0f && (mm_hash((uint32_t)(u * 40.0f) * 73u + (uint32_t)(v * 40.0f) * 151u + (uint32_t)(t * 6.0f)) & 15) == 0)
                return RGB(70, 30, 90);
        }
        return 0;
    }
    case ME_PORTAL: {                                  /* a CHANGE REQUEST portal: a red vortex in a frame, turning */
        float a = (u - 0.5f) / 0.38f, b = (v - 0.56f) / 0.42f, r = fm_sqrtf(a * a + b * b), ang, ca, sa, rx;
        if (r > 1.08f)
            return 0;
        if (r > 0.94f)                                 /* the rim: it crackles */
            return (mm_hash((uint32_t)(u * 50.0f) + (uint32_t)(v * 50.0f) * 61u + (uint32_t)(t * 20.0f)) & 3) ? RGB(255, 90, 50) : RGB(255, 220, 140);
        ang = r * 7.0f - t * 3.5f;
        ca = fm_cosf(ang);
        sa = fm_sinf(ang);
        rx = a * ca - b * sa;
        if (r < 0.16f)
            return RGB(255, 240, 200);                 /* its eye */
        if (fm_fabsf(rx) < 0.12f + r * 0.05f)
            return mm_dim(RGB(255, 110, 40), 255 - (int)(r * 120.0f));   /* the arms */
        return (a * sa + b * ca) > 0.0f ? RGB(120, 16, 48) : RGB(40, 6, 30);
    }
    default: {                                         /* coffee: a mug and steam */
        float s = fm_sinf(v * 20.0f + t * 4.0f) * 0.03f;
        if (v > 0.55f && v < 0.9f && u > 0.32f && u < 0.62f)
            return v < 0.6f ? RGB(80, 50, 30) : RGB(236, 232, 224);
        if (v > 0.62f && v < 0.82f && u > 0.62f && u < 0.72f && !(u > 0.64f && u < 0.69f && v > 0.66f && v < 0.78f))
            return RGB(236, 232, 224);
        if (v > 0.28f && v < 0.5f && (fm_fabsf(u - 0.42f - s) < 0.015f || fm_fabsf(u - 0.52f + s) < 0.015f))
            return RGB(160, 170, 165);
        return 0;
    }
    }
}

/* the view in this band: ceiling, walls, floor, then the sprites far to near */
static void mm_draw_view(void)
{
    int x, y, y0 = g_y0 > MM_VY0 ? g_y0 : MM_VY0, y1 = g_y1 < MM_VY1 ? g_y1 : MM_VY1, i, j;
    float cx = fm_cosf(mm.dir), cy = fm_sinf(mm.dir);
    int order[MM_NENT], n = 0;
    float dist[MM_NENT];
    if (y0 >= y1)
        return;
    for (x = 0; x < PLAT_W; x++) {
        int top = mm.top[x], bot = mm.bot[x], h = MM_HZ - mm.htop[x];
        for (y = y0; y < y1; y++) {
            uint16_t c;
            if (y < top)                                 /* the ceiling: cable trays; in the call, its dark top bar */
                c = mm.level ? (((y - MM_VY0) % 30 == 0) ? RGB(46, 40, 70) : RGB(24, 22, 36))
                             : ((y - MM_VY0) % 22 == 0 || (x + y) % 40 == 0) ? RGB(26, 30, 34) : RGB(16, 18, 22);
            else if (y >= bot) {                       /* the raised floor: tiles that close in; in the call, a glassy blue */
                int dy = y - MM_HZ, row = 2000 / (dy > 0 ? dy : 1);
                c = mm.level ? ((row % 6 == 0) ? RGB(40, 52, 110) : RGB(16, 22, 52)) : (row % 8 == 0) ? RGB(40, 46, 44) : RGB(24, 28, 27);
            } else
                c = mm_dim(mm_wall_px(mm.wtype[x], mm.tex[x], (y - mm.htop[x]) * 128 / (h > 0 ? h : 1), x), mm.shade[x]);
            gb[(y - g_y0) * PLAT_W + x] = c;
        }
    }
    for (i = 0; i < MM_NENT; i++)
        if (mm.e[i].alive) {
            float dx = mm.e[i].x - mm.px, dy = mm.e[i].y - mm.py;
            dist[n] = dx * dx + dy * dy;
            order[n++] = i;
        }
    for (i = 1; i < n; i++)                            /* far to near */
        for (j = i; j > 0 && dist[j] > dist[j - 1]; j--) {
            float tf = dist[j];
            int ti = order[j];
            dist[j] = dist[j - 1];
            order[j] = order[j - 1];
            dist[j - 1] = tf;
            order[j - 1] = ti;
        }
    for (i = 0; i < n; i++) {
        const mm_ent *e = &mm.e[order[i]];
        float dx = e->x - mm.px, dy = e->y - mm.py, along = dx * cx + dy * cy, side = -dx * cy + dy * cx;
        int sxc, size, sx0, sy0, shade;
        if (along < 0.15f)
            continue;
        sxc = (int)(PLAT_W / 2 * (1.0f + side / (along * 0.66f)));
        size = (int)((MM_VY1 - MM_VY0) / along * mm_kind[e->kind].size);
        sx0 = sxc - size / 2;
        sy0 = MM_HZ - size / 2 + (int)((MM_VY1 - MM_VY0) / along * 0.1f);
        if (e->kind == ME_TINY || e->kind == ME_GHOUL || e->kind == ME_PORTAL)   /* these stand on the floor */
            sy0 = MM_HZ + (int)((MM_VY1 - MM_VY0) / along * 0.5f) - size;
        else if (e->kind == ME_GRAPH || e->kind == ME_CODE)   /* these hover */
            sy0 -= (int)((MM_VY1 - MM_VY0) / along * (0.12f + 0.04f * fm_sinf(mm.t * 2.0f + e->x)));
        shade = clampi((int)(255.0f / (1.0f + along * 0.18f)), 60, 255);
        for (x = sx0 < 0 ? 0 : sx0; x < sx0 + size && x < PLAT_W; x++) {
            if (mm.zbuf[x] < along)
                continue;
            for (y = sy0 < y0 ? y0 : sy0; y < sy0 + size && y < y1; y++) {
                uint16_t c = mm_sprite_px(e, (float)(x - sx0) / size, (float)(y - sy0) / size);
                if (c)
                    gb[(y - g_y0) * PLAT_W + x] = mm_dim(c, shade);
            }
        }
        if (e->kind == ME_PORTAL && !e->dying && size > 44 && sxc > 0 && sxc < PLAT_W && mm.zbuf[sxc] >= along) {   /* its sign */
            const char *lb = size > 64 ? "CHANGE REQUEST" : "CR";
            int w = g_text_w(&FONT_T, lb), ty = sy0 - 12;
            if (ty >= MM_VY0 && ty + 11 < MM_VY1) {
                g_fill(sxc - w / 2 - 3, ty - 1, w + 6, 12, RGB(110, 14, 20));
                g_text_c(sxc, ty, &FONT_T, lb, RGB(255, 220, 200));
            }
        }
    }
}

/* the incident manager's hand: a business-casual cuff, a hand, a mouse-shaped blaster */
static void mm_draw_hand(void)
{
    int r = (int)(mm.recoil * 10.0f), bx = 150 + r / 2, by = 150 + r;
    g_fill(bx + 36, by + 26, 54, 30, RGB(150, 186, 222));            /* the shirt sleeve */
    g_fill(bx + 32, by + 22, 10, 30, RGB(236, 240, 244));             /* the cuff */
    g_disc(bx + 26, by + 30, 14, RGB(222, 176, 140));                 /* the hand */
    g_fill(bx + 14, by + 30, 26, 14, RGB(222, 176, 140));
    g_disc(bx + 12, by + 8, 16, RGB(226, 228, 230));                  /* the mouse */
    g_fill(bx - 4, by + 8, 32, 24, RGB(226, 228, 230));
    g_vline(bx + 12, by - 6, 16, RGB(150, 154, 158));                 /* its two buttons */
    g_fill(bx + 10, by - 2, 5, 8, RGB(90, 94, 98));                   /* the wheel */
    g_line(bx + 12, by - 8, bx + 4, by - 26, RGB(70, 74, 78));        /* the cable, the barrel */
    if (mm.recoil > 0.55f) {                                          /* a click: rings and a cursor flash */
        g_frame(bx - 2, by - 40, 20, 20, C_TEAL);
        g_frame(bx - 8, by - 46, 32, 32, C_TEAL_D);
    }
    {   /* the crosshair: a mouse pointer */
        int x0 = PLAT_W / 2, y0 = MM_HZ - 6, k;
        for (k = 0; k < 10; k++)
            g_hline(x0, y0 + k, k < 7 ? k / 2 + 1 : 2, C_WHITE);
        g_line(x0, y0, x0, y0 + 9, RGB(20, 20, 20));
    }
}

static void mm_draw_hud(void)
{
    char b[24];
    int up = (int)(mm.uptime + 0.5f), w = up * 70 / 100, m = (int)mm.mttr;
    uint16_t uc = up > 60 ? RGB(90, 210, 120) : up > 30 ? C_AMBER : C_RED;
    int i;
    /* the log: the newest line at the top, in the colour of its level */
    g_fill(0, 0, PLAT_W, MM_VY0, RGB(8, 12, 10));
    g_text(4, 2, &FONT_T, mm.log[2], mm.log_col[2] ? mm.log_col[2] : C_DIM);
    /* the console under the view */
    g_fill(0, MM_VY1, PLAT_W, PLAT_H - MM_VY1, C_PANEL);
    g_hline(0, MM_VY1, PLAT_W, C_LINE);
    g_text(6, MM_VY1 + 3, &FONT_T, "UPTIME", C_DIM);
    g_fill(6, MM_VY1 + 17, 70, 6, C_LINE);
    g_fill(6, MM_VY1 + 17, w, 6, uc);
    b[0] = 0;
    sx_int(b, up);
    sx_cat(b, "%");
    g_text(80, MM_VY1 + 13, &FONT_T, b, uc);
    b[0] = 0;
    sx_cat(b, "OPEN ");
    sx_int(b + 5, mm.open);
    g_text(118, MM_VY1 + 3, &FONT_T, b, mm.open ? C_AMBER : RGB(90, 210, 120));
    b[0] = 0;
    sx_cat(b, "MTTR ");
    i = sx_int(b + 5, m / 60) + 5;
    b[i++] = ':';
    b[i++] = (char)('0' + (m % 60) / 10);
    b[i++] = (char)('0' + m % 10);
    b[i] = 0;
    g_text(118, MM_VY1 + 15, &FONT_T, b, C_MID);
    g_text_r(234, MM_VY1 + 3, &FONT_T, "MOOM", C_TEAL);
    g_text_r(234, MM_VY1 + 15, &FONT_T, mm.level ? "LVL 2" : "LVL 1", C_DIM);
    g_text_r(234, MM_VY1 + 28, &FONT_T, "PLAY: PAUSE", C_DIM);
    g_text(4, MM_VY1 + 28, &FONT_T, mm.log[1], mm.log_col[1] ? mm.log_col[1] : C_DIM);
    if (mm.hurt > 0.0f) {                                             /* hit: the view's edge goes red */
        g_fill(0, MM_VY0, PLAT_W, 4, C_RED);
        g_fill(0, MM_VY1 - 4, PLAT_W, 4, C_RED);
        g_fill(0, MM_VY0, 4, MM_VY1 - MM_VY0, C_RED);
        g_fill(PLAT_W - 4, MM_VY0, 4, MM_VY1 - MM_VY0, C_RED);
    }
    if (mm.paused && mm.state == 0) {
        g_fill(30, 80, 180, 64, RGB(8, 12, 10));
        g_frame(30, 80, 180, 64, C_AMBER);
        g_text_c(120, 88, &FONT_B, "BRIDGE ON HOLD", C_AMBER);
        g_text_c(120, 110, &FONT_T, "PLAY: BACK TO THE CALL", C_MID);
        g_text_c(120, 124, &FONT_T, "HOME: LEAVE MOOM", C_DIM);
    }
    if (mm.state == 1 || mm.state == 2) {
        g_fill(20, 86, 200, 52, RGB(8, 12, 10));
        g_frame(20, 86, 200, 52, mm.state == 1 ? RGB(90, 210, 120) : C_RED);
        g_text_c(120, 94, &FONT_B, mm.state == 2 ? "SLA BREACHED" : mm.level ? "CHANGE ROLLED BACK" : "INCIDENT RESOLVED", mm.state == 1 ? RGB(90, 210, 120) : C_RED);
        g_text_c(120, 116, &FONT_T, mm.state == 2 ? "BRIDGE REOPENS..." : mm.level ? "BRIDGE CLOSED. RCA DUE FRIDAY" : "NEW BRIDGE CALL INCOMING...", C_MID);
    }
}

/* the title card: MOOM in chunky letters, fire-coloured, outlined, with a drop shadow, over a log
 * that scrolls (Ravel's own lettering) */
static void mm_draw_title(void)
{
    static const uint8_t glyph[2][9] = {{0xC3, 0xE7, 0xFF, 0xDB, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3},   /* M, 8 x 9 */
                                        {0x3C, 0x7E, 0xE7, 0xC3, 0xC3, 0xC3, 0xE7, 0x7E, 0x3C}};  /* O */
    static const uint8_t word[4] = {0, 1, 1, 0};
    const int S = 6, X0 = 15, Y0 = 46, GAP = 6, W = 8 * S + GAP;
    int L, r, c, y, row, pass;
    g_fill(0, 0, PLAT_W, PLAT_H, RGB(6, 8, 7));
    for (row = 0; row < 21; row++) {                    /* the log behind, scrolling up */
        int yy = row * 12 - ((int)(mm.t * 30.0f) % 12);
        uint32_t h = mm_hash((uint32_t)(row + (int)(mm.t * 2.5f)) * 2654435761u);
        uint16_t col = (h & 7) == 0 ? RGB(70, 22, 20) : (h & 3) == 1 ? RGB(60, 48, 24) : RGB(20, 44, 40);
        int x = 6, k;
        for (k = 0; k < 6; k++) {
            int w = 10 + (int)((h >> (k * 4)) & 15) * 3;
            g_fill(x, yy + 3, w, 5, col);
            x += w + 5;
        }
    }
    for (pass = 0; pass < 2; pass++)                    /* the shadow, then a dark outline */
        for (L = 0; L < 4; L++)
            for (r = 0; r < 9; r++)
                for (c = 0; c < 8; c++)
                    if ((glyph[word[L]][r] >> (7 - c)) & 1) {
                        int bx = X0 + L * W + c * S, by = Y0 + r * S;
                        if (pass == 0)
                            g_fill(bx + 5, by + 6, S, S, RGB(34, 4, 4));
                        else
                            g_fill(bx - 2, by - 2, S + 4, S + 4, RGB(60, 10, 6));
                    }
    for (L = 0; L < 4; L++)
        for (r = 0; r < 9; r++)
            for (c = 0; c < 8; c++) {
                const uint8_t *g = glyph[word[L]];
                int bx = X0 + L * W + c * S, by = Y0 + r * S, up = r > 0 && ((g[r - 1] >> (7 - c)) & 1);
                if (!((g[r] >> (7 - c)) & 1))
                    continue;
                for (y = 0; y < S; y++) {               /* fire, top to bottom: pale yellow, orange, deep red */
                    float f = (float)(r * S + y) / (9.0f * S);
                    int R = 255 - (int)(f * 80.0f), G = f < 0.5f ? 230 - (int)(f * 2.0f * 120.0f) : 110 - (int)((f - 0.5f) * 2.0f * 85.0f), B = 80 - (int)(f * 70.0f);
                    g_hline(bx, by + y, S, RGB(R, G, B));
                }
                if (!up)
                    g_hline(bx, by, S, RGB(255, 246, 200));   /* a lit top edge */
            }
    {   /* what MOOM stands for, in the largest type that fits */
        static const char *const tag = "MAJOR OUTAGE & OFFENSE MITIGATOR";
        const rv_font_t *f = g_text_w(&FONT_B, tag) <= 234 ? &FONT_B : g_text_w(&FONT_XS, tag) <= 234 ? &FONT_XS : &FONT_T;
        g_text_c(120, 116, f, tag, C_TEAL);
    }
    g_text_c(120, 140, &FONT_T, "THE BRIDGE IS OPEN. THE BUGS ARE IN.", C_MID);
    if (((int)(mm.t * 2.0f) & 1) == 0)
        g_text_c(120, 196, &FONT_T, "PRESS PLAY TO JOIN THE CALL", C_AMBER);
    g_text_c(120, 222, &FONT_T, "HOME (ESC): BACK TO RAVEL", C_DIM);
}

/* joining the bridge: the call's window, its tiles, and yours growing until you are inside it */
static void mm_draw_join(void)
{
    float f = 1.0f - mm.state_t / 3.2f, z = f < 0.35f ? 0.0f : (f - 0.35f) / 0.65f;
    int i, x0, y0, x1, y1;
    z = z * z;
    g_fill(0, 0, PLAT_W, PLAT_H, RGB(18, 18, 24));
    g_fill(0, 0, PLAT_W, 18, RGB(36, 34, 52));
    g_text(6, 3, &FONT_T, "SEV1 BRIDGE  00:42:17", RGB(200, 196, 230));
    g_fill(206, 4, 28, 10, RGB(200, 40, 50));
    for (i = 0; i < 6; i++) {                          /* the others on the call */
        int tx = 8 + (i % 3) * 77, ty = 26 + (i / 3) * 80;
        uint32_t h = mm_hash((uint32_t)i * 31u + 7u);
        if (i == 4)
            continue;
        g_fill(tx, ty, 71, 72, RGB(44, 44, 56));
        g_disc(tx + 35, ty + 32, 16, RGB(60 + (int)(h & 127), 50 + (int)((h >> 8) & 127), 90 + (int)((h >> 16) & 127)));
        g_fill(tx + 4, ty + 60, 30, 6, RGB(150, 150, 166));
        if (i == ((int)(mm.t * 1.5f) % 6) && i != 4)
            g_frame(tx, ty, 71, 72, RGB(130, 120, 240));
    }
    g_fill(0, 194, PLAT_W, 46, RGB(28, 28, 36));
    g_fill(100, 204, 40, 16, RGB(200, 40, 50));
    x0 = (int)(85.0f * (1.0f - z));                    /* your tile, zooming to fill the view */
    y0 = (int)(106.0f + (16.0f - 106.0f) * z);
    x1 = (int)(156.0f + (240.0f - 156.0f) * z);
    y1 = (int)(178.0f + (200.0f - 178.0f) * z);
    g_fill(x0, y0, x1 - x0, y1 - y0, RGB(14, 28, 70));
    g_frame(x0, y0, x1 - x0, y1 - y0, RGB(130, 120, 240));
    g_text_c((x0 + x1) / 2, (y0 + y1) / 2 - 6, z > 0.4f ? &FONT_B : &FONT_T, "YOU", C_WHITE);
    g_text_c(120, 224, &FONT_T, z > 0.05f ? "GOING INSIDE THE CALL..." : "JOINING THE BRIDGE...", C_AMBER);
}

/* one frame: input, the world, then the picture band by band */
void moom_frame(void)
{
    uint32_t now = plat_ms(), btn = plat_buttons(), keys = plat_keys();
    uint32_t down_btn = btn & ~mm.prev_btn, down_keys = keys & ~mm.prev_keys;
    float dt = (float)(now - mm.last_ms) * 0.001f;
    int turn = 0, i, b;
    mm.last_ms = now;
    if (dt > 0.1f)
        dt = 0.1f;
    if (down_btn & (1u << B_HOME)) {
        moom_stop();
        return;
    }
    turn += plat_enc(EN_SELECT);
    for (i = 0; i < 4; i++)
        turn += plat_enc(EN_K1 + i);
    (void)plat_enc(EN_ALGO);
    (void)plat_enc(EN_PRESET);
    mm.prev_btn = btn;
    mm.prev_keys = keys;
    mm_update(dt, keys, down_keys, btn, down_btn, turn);
    moom_music = mm.state == 0 && !mm.paused ? (uint8_t)(mm.level + 1) : 0;   /* the soundtrack under play only */
    if (mm.state == 3 || mm.state == 4) {
        for (b = 0; b < PLAT_H; b += GB_H) {
            g_band(b);
            if (mm.state == 3)
                mm_draw_title();
            else
                mm_draw_join();
            g_send();
        }
        plat_leds(1u << B_PLAY, 0);
        return;
    }
    mm_cast();
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        mm_draw_view();
        mm_draw_hand();
        mm_draw_hud();
        g_send();
    }
    plat_leds(1u << B_HOME, 0);
}
