/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the screen in words, for the FM-1 Screen Reader (github.com/charlesvestal/fm1-reader): a page on the
 * computer that speaks what Ravel's screen shows, or hands it to the computer's own screen reader. announce.c
 * (fm1-base's, MPL-2.0) keeps the protocol; this says what changed. It is part of rv_ui.c (its state and its
 * drawing's own text), run once a frame on the interface's side, never in the audio.
 *
 * Nothing runs while no reader listens: speak() returns at once. When one does: a new page says its name, the
 * track and its sound, the knobs and what PRESETS and the keys do; a knob turned says its name and value once it
 * settles; a list moved says the item; messages, play and stop, record, and steps switched on or off are said
 * as they happen. The words file (firmware/words.tsv, published on the site) says the screen's short words in
 * full. */

static struct {
    uint8_t started, playing, armed;
    int16_t held;
    uint32_t ctx, sel;
    uint64_t steps;
    char val[4][28];
} sp RV_RAMBSS;
static char sp_buf[ANN_MAX + 1] RV_RAMBSS;
static uint32_t sp_n;

static void sp_cat(const char *s)
{
    while (s && *s && sp_n < ANN_MAX)
        sp_buf[sp_n++] = *s++;
    sp_buf[sp_n] = 0;
}
static void sp_int(int v)
{
    char b[12];
    sx_int(b, v);
    sp_cat(b);
}
static void sp_sep(const char *s)                    /* ". " or ", " between parts (none at the start) */
{
    if (sp_n)
        sp_cat(s);
}
static uint32_t sp_hash(uint32_t h, const char *s)
{
    while (s && *s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* a value as said: "-" is NONE, a lone ">" (a cord's step) TO */
static const char *sp_value(const char *v, char *o)
{
    int n = 0;
    if (v[0] == '-' && !v[1])
        return "NONE";
    for (; *v && n < 13; v++)
        if (*v == '>' && (v[1] == ' ' || (v[1] >= '0' && v[1] <= '9')) && (n == 0 || o[n - 1] == ' ')) {
            o[n++] = 'T', o[n++] = 'O';
            if (v[1] != ' ')
                o[n++] = ' ';
        } else
            o[n++] = *v;
    o[n] = 0;
    return o;
}

/* knob i as the strip shows it: its name and value ("" for an empty cell) */
static void sp_knob(int i, char *o)
{
    const page_t *pg = cur_page();
    kb_t k = page_knob(pg, i);
    kinfo_t in;
    char val[16], said[16];
    int v, n = 0, held = held_step();
    const char *a, *b;
    o[0] = 0;
    if (plat_buttons() & (1u << B_LFO)) {             /* macros on the knobs */
        val[0] = 0;
        sx_int(val + sx_cat(val, "MACRO "), i + 1);
        sx_cat(val, " ");
        sx_int(val + sx_cat(val, ""), rv.gp[GP_MACRO1 + i]);
        sx_cat(o, val);
        return;
    }
    in = kb_info(k);
    if (!in.name || k.kind == K_NONE || (in.name[0] == '-' && !in.name[1]))
        return;                                       /* (an empty cell, or a setting no block has) */
    v = kb_get(k);
    if (held >= 0 && k.kind == K_TP)
        step_lock_value(held, k.id, &v);              /* (a step held: its lock, if it has one) */
    kb_format(k, v, val);
    for (a = in.name; *a && n < 26; a++)
        o[n++] = *a;
    o[n++] = ' ';
    if (k.kind == K_TP && !rv_tp_applies(ui.track, k.id))
        b = "NOT IN USE";
    else
        b = sp_value(val, said);
    for (; *b && n < 27; b++)
        o[n++] = *b;
    o[n] = 0;
}

/* the list item PRESETS has chosen on this page, with its place ("" if the page shows none of its own) */
static void sp_item(void)
{
    const page_t *pg = cur_page();
    switch (pg->pr) {
    case PR_SLOT:
        sp_cat("SLOT ");
        sp_int(ui.cur_slot + 1);
        sp_cat(ui.slot_used[ui.cur_slot] ? " " : " EMPTY");
        if (ui.slot_used[ui.cur_slot])
            sp_cat(ui.slot_names[ui.cur_slot]);
        sp_cat(", ");
        sp_int(ui.cur_slot + 1);
        sp_cat(" OF ");
        sp_int(PLAT_PROJECTS);
        break;
    case PR_BANK:
        sp_cat("SOUND ");
        sp_cat(bank_letters[rv_bank_current()]);
        sp_int(ui.cur_sound + 1);
        sp_cat(" ");
        sp_cat(rv_bank_used(ui.cur_sound) ? rv_bank_name(ui.cur_sound) : "EMPTY");
        break;
    case PR_WLIB: {
        int e = wl_entry();
        if (e == WL_NEW)
            sp_cat("NEW: SAVE HERE");
        else if (e >= 100)
            sp_cat(rv_bank_used(e - 100) ? rv_bank_name(e - 100) : "EMPTY");
        else
            sp_cat(rv_weft_preset_name(e));
        break;
    }
    case PR_SCENE: sp_cat("SCENE "); sp_int(ui.cur_scene + 1); break;
    case PR_ROUTE: sp_cat("TARGET T"); sp_int(ui.cur_route + 1); break;
    case PR_MAT: sp_cat("SLOT "); sp_int(ui.cur_mat + 1); sp_cat(" OF "); sp_int(RV_MATRIX); break;
    case PR_MACRO: sp_cat("MACRO "); sp_int(ui.cur_macro / RV_MACRO_DESTS + 1); sp_cat(" DEST "); sp_int(ui.cur_macro % RV_MACRO_DESTS + 1); break;
    case PR_ARR: sp_cat("ROW "); sp_int(ui.cur_arr + 1); break;
    case PR_ARC: sp_cat("BAR "); sp_int(ui.cur_arc + 1); break;
    case PR_LIB: sp_cat("ENVELOPE "); sp_int(ui.cur_lib + 1); sp_cat(" OF "); sp_int(RV_SENV); break;
    case PR_PATTERN: sp_cat("PATTERN "); sp_int(rv.pat_cur[ui.track] + 1); break;
    default: break;
    }
}

/* the context: the page, the track and its sound, what the knobs are */
static void sp_screen(int knobs, int hint)
{
    const page_t *pg = cur_page();
    int t = ui.track, i;
    sp_cat(pg->title);
    sp_cat(". T");
    sp_int(t + 1);
    sp_cat(" ");
    sp_cat(rv_kind_names[clampi(rv.tp[t][TP_KIND], 0, KIND_N - 1)]);
    sp_cat(" ");
    sp_cat(sound_name(t));
    i = (int)sp_n;
    sp_cat(". ");
    {
        uint32_t m = sp_n;
        sp_item();
        if (sp_n == m)
            sp_n = (uint32_t)i, sp_buf[sp_n] = 0;
    }
    if (knobs)
        for (i = 0; i < 4; i++)
            if (sp.val[i][0]) {
                sp_sep(i ? ", " : ". ");
                sp_cat(sp.val[i]);
            }
    if (hint) {                                       /* (the footer's parts, two spaces apart: sentences) */
        const char *h = foot_hint();
        sp_sep(". ");
        for (; *h && sp_n < ANN_MAX; h++)
            if (h[0] == ' ' && h[1] == ' ') {
                sp_cat(". ");
                while (h[1] == ' ')
                    h++;
            } else
                sp_buf[sp_n++] = *h, sp_buf[sp_n] = 0;
    }
}

/* once a frame: what changed on the screen, said */
static void speak(void)
{
    const page_t *pg;
    uint32_t now = plat_ms(), ctx, sel, btn;
    uint64_t steps = 0;
    int i, all, turned = -1, t = ui.track;
    if (!ann_on()) {
        sp.started = 0;
        ann_tick(now);
        return;
    }
    all = ann_where();                                /* (asked for, or a new reader) */
    all |= !sp.started;
    pg = cur_page();
    btn = plat_buttons();
    ctx = (uint32_t)(pg - pages) | (uint32_t)t << 8 | (uint32_t)rv.tp[t][TP_KIND] << 11 | (uint32_t)rv.tp[t][TP_ENGINE] << 15 |
          (btn & (1u << B_LFO) ? 1u << 20 : 0u) | (uint32_t)ui.lane_row << 22 |
          (uint32_t)ui.lfo_row << 24 | (uint32_t)ui.cz_row << 25 | (uint32_t)ui.lib_row << 28;
    sel = sp_hash(2166136261u, sound_name(t));
    sel = (sel ^ ui.cur_slot ^ (uint32_t)ui.cur_sound << 6 ^ (uint32_t)ui.wl_sel << 12 ^ (uint32_t)ui.cur_scene << 18 ^
           (uint32_t)ui.cur_route << 23 ^ (uint32_t)ui.cur_mat << 26) * 16777619u;
    sel = (sel ^ ui.cur_macro ^ (uint32_t)ui.cur_arr << 8 ^ (uint32_t)ui.cur_arc << 16 ^ (uint32_t)ui.cur_lib << 20 ^
           (uint32_t)rv.pat_cur[t] << 28) * 16777619u;
    for (i = 0; i < 4; i++) {
        char v[28];
        sp_knob(i, v);
        if (sx_cmp(v, sp.val[i]) && now - ui.turned_ms[i] < 600u)
            turned = i;                               /* (a knob turned: its value changed) */
        sx_cat((sp.val[i][0] = 0, sp.val[i]), v);
    }
    sp_n = 0;
    sp_buf[0] = 0;
    if (all) {                                        /* everything: the reader came, or asked */
        sp_screen(1, 1);
        ann_say(sp_buf, 0, ANN_NOW);
    } else if (ctx != sp.ctx) {                       /* another page, track, sound kind or layer: settle, then say */
        sp_screen(1, 0);
        ann_later(sp_buf, ANN_NOW, 200, now);
    } else if (sel != sp.sel) {                       /* PRESETS moved: the sound or the item, and the knobs it brings */
        if (pg->pr == PR_SOUND || pg->pr == PR_WEFT || pg->pr == PR_CZSTEP || pg->pr == PR_LANE)
            sp_cat(sound_name(t));
        else
            sp_item();
        ann_later(sp_buf, ANN_NOW, 150, now);
    } else if (held_step() != sp.held && held_step() >= 0) {   /* a step held: its number, and its knobs (its locks) */
        sp_cat("STEP ");
        sp_int(held_step() + 1);
        for (i = 0; i < 4; i++)
            if (sp.val[i][0]) {
                sp_cat(i ? ", " : ". ");
                sp_cat(sp.val[i]);
            }
        ann_later(sp_buf, ANN_NOW, 150, now);
    } else if (turned >= 0) {                         /* a knob: its name and value, once it settles */
        sp_cat(sp.val[turned]);
        ann_later(sp_buf, ANN_NOW, 120, now);
    }
    if (sp.started && rv_live.playing != sp.playing)
        ann_say(rv_live.playing ? "PLAYING" : "STOPPED", 0, ANN_NOW);
    if (sp.started && ui.armed != sp.armed)
        ann_say(ui.armed ? "RECORD ARMED" : "RECORD OFF", 0, ANN_NOW);
    if (ui.sec == SEC_SEQ && !is_kit_ui(t)) {         /* a step switched on or off by hand */
        const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
        uint64_t d;
        for (i = 0; i < RV_STEPS; i++)
            if (p->s[i].flags & ST_ON)
                steps |= 1ull << i;
        d = steps ^ sp.steps;
        if (sp.started && (ctx & 0xFFFFu) == (sp.ctx & 0xFFFFu) && d && !(d & (d - 1)) && !(rv_live.playing && ui.armed)) {
            for (i = 0; !(d >> i & 1u); i++)
                ;
            sp_n = 0;
            sp_cat("STEP ");
            sp_int(i + 1);
            sp_cat(steps >> i & 1u ? " ON" : " OFF");
            ann_say(sp_buf, 0, ANN_NOW);
        }
    }
    sp.steps = steps;
    sp.held = (int16_t)held_step();
    sp.started = 1;
    sp.ctx = ctx;
    sp.sel = sel;
    sp.playing = (uint8_t)rv_live.playing;
    sp.armed = ui.armed;
    ann_tick(now);
}

#ifdef RV_SPEAKDUMP
/* the simulator's speakdump: every page of every kind of track as a reader hears it on arriving, and every value
 * of every list knob (the audit checks these words; fm1-reader tools/audit.mjs) */
static void sp_dump_sec(int sec, void (*out)(const char *kind, const char *text));
void rv_speak_dump(void (*out)(const char *kind, const char *text))
{
    int m;
    ui.track = 0;                                     /* every drum machine's own knobs, and every WEFT block's */
    for (m = 0; m < PM_N; m++) {
        rv_track_defaults(0, KIND_PERC, 0);
        rv.tp[0][TP_S0] = (int16_t)m;
        rv_track_src_defaults(0);
        sp_dump_sec(SEC_VOICE, out);
    }
    for (m = 1; m < WO_N; m++) {
        rv_track_defaults(0, KIND_WEFT, 0);
        weft_preset_apply(0, 0);
        rv.weft[0].type[0] = (uint8_t)m;
        ui.wf_blk = 0;
        sp_dump_sec(SEC_VOICE, out);
    }
    {
    static const uint8_t cfg[][2] = {{KIND_STRIKE, 0}, {KIND_PERC, 0}, {KIND_PERC, 1}, {KIND_WEFT, 0}, {KIND_LOOP, 0}, {KIND_COMP, 0}};
    int c, sec;
    for (c = 0; c < ENG_N + (int)(sizeof cfg / sizeof cfg[0]); c++) {
        int kind = c < ENG_N ? KIND_SYNTH : cfg[c - ENG_N][0], eng = c < ENG_N ? c : cfg[c - ENG_N][1];
        ui.track = kind == KIND_COMP ? 7 : 0;
        rv_track_defaults(ui.track, kind, eng);
        if (kind == KIND_WEFT)
            weft_preset_apply(ui.track, 0);
        for (sec = 0; sec < NSEC; sec++)
            sp_dump_sec(sec, out);
    }
    }
}

static void sp_dump_sec(int sec, void (*out)(const char *kind, const char *text))
{
    uint8_t list[SEC_PAGES_MAX];
    int n = sec_page_list(sec, list), j, i;
    ui.sec = (uint8_t)sec;
    for (j = 0; j < n; j++) {
        ui.page[sec] = (uint8_t)j;
        for (i = 0; i < 4; i++)
            sp_knob(i, sp.val[i]);
        sp_n = 0;
        sp_screen(1, 1);
        out("SCREEN", sp_buf);
        for (i = 0; i < 4; i++) {
            kb_t k = page_knob(cur_page(), i);
            kinfo_t in = kb_info(k);
            int v, step;
            if (!in.name || k.kind == K_NONE || k.kind == K_LRN)
                continue;
            step = in.fmt == PF_ENUM || in.max - in.min <= 40 ? 1 : (in.max - in.min) / 4;
            for (v = in.min; v <= in.max; v += step) {
                char val[16], said[16];
                if (in.name[0] == '-' && !in.name[1])
                    break;
                kb_format(k, v, val);
                sp_n = 0;
                sp_cat(in.name);
                sp_cat(" ");
                sp_cat(sp_value(val, said));
                out("KNOB", sp_buf);
            }
        }
    }
}
#endif
