/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel in a browser: the same instrument and simulated FM-1 as the desktop simulator
 * (host/host_core.c), built freestanding for WebAssembly (web/emu/build.sh) and driven by
 * the page (index.html). */
#include <stdint.h>

#define EXPORT(name) __attribute__((export_name(#name)))

/* the C library the instrument needs: the compiler's built-ins call these */
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *p = d;
    while (n--)
        *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--)
        *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *p = d;
    const uint8_t *q = s;
    if (p < q) {
        while (n--)
            *p++ = *q++;
    } else {
        p += n;
        q += n;
        while (n--)
            *--p = *--q;
    }
    return d;
}
int memcmp(const void *a, const void *b, unsigned long n)
{
    const uint8_t *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

#include "../../host/host_core.c"
#include "../../firmware/app/rv.h"

int ui_section(void);
int ui_track(void);
const char *ui_page_title(void);
void ui_goto(int sec, int page);
void ui_set_track(int t);
int ui_octave(void);
int ui_armed(void);
int rv_preset_count(void);
const char *rv_preset_name(int i);
int rv_preset_kind(int i);
void rv_preset_apply(int t, int i);

/* ------------------------------------------------------------- audio --- */
#define AUDIO_MAX 16384u
static float audio[AUDIO_MAX * 2u];
static uint32_t audio_n;

static void host_audio(const int32_t *blk, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && audio_n < AUDIO_MAX; i++, audio_n++) {
        audio[2u * audio_n] = (float)blk[2u * i] * (1.0f / 8388608.0f);
        audio[2u * audio_n + 1u] = (float)blk[2u * i + 1u] * (1.0f / 8388608.0f);
    }
}

/* audio coming in: the page writes a file's samples into this ring */
#define IN_RING 32768u
static float in_ring[IN_RING * 2u];
static uint32_t in_w, in_r;
static int host_audio_in(float *lr, int frames)
{
    int i;
    for (i = 0; i < frames && in_r != in_w; i++, in_r++) {
        lr[2 * i] = in_ring[2u * (in_r % IN_RING)];
        lr[2 * i + 1] = in_ring[2u * (in_r % IN_RING) + 1u];
    }
    return i;
}
EXPORT(web_in_ptr) float *web_in_ptr(void) { return in_ring; }
EXPORT(web_in_write_pos) uint32_t web_in_write_pos(void) { return in_w % IN_RING; }
EXPORT(web_in_space) uint32_t web_in_space(void) { return IN_RING - 1u - (in_w - in_r); }
EXPORT(web_in_commit) void web_in_commit(uint32_t n) { in_w += n; }
EXPORT(web_in_ring) uint32_t web_in_ring(void) { return IN_RING; }
EXPORT(web_in_clear) void web_in_clear(void) { in_r = in_w; }

/* ---------------------------------------------------------- running --- */
EXPORT(rv_boot) void rv_boot(void)
{
    held_btn = held_keys = 0;
    boot();
}
EXPORT(rv_run) uint32_t rv_run(uint32_t ms)
{
    audio_n = 0;
    if (ms > 250u)
        ms = 250u;
    run_ms(ms);
    return audio_n;
}
EXPORT(rv_audio) float *rv_audio(void) { return audio; }
EXPORT(rv_fb) uint16_t *rv_fb(void) { return fb; }
EXPORT(rv_set_buttons) void rv_set_buttons(uint32_t m) { held_btn = m; }
EXPORT(rv_set_keys) void rv_set_keys(uint32_t m) { held_keys = m; }
EXPORT(rv_enc) void rv_enc(int role, int32_t d)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += d;
}
EXPORT(rv_set_master) void rv_set_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
EXPORT(rv_leds_buttons) uint32_t rv_leds_buttons(void) { return lit_btn; }
EXPORT(rv_leds_keys) uint32_t rv_leds_keys(void) { return lit_keys; }
EXPORT(rv_sample_rate) uint32_t rv_sample_rate(void) { return 44100u; }

/* flash slots, so the page can keep them between visits */
EXPORT(rv_store) uint8_t *rv_store(uint32_t s) { return s < PLAT_STORES ? store[s] : 0; }
EXPORT(rv_store_len) int rv_store_len(uint32_t s) { return s < PLAT_STORES ? store_len[s] : -1; }
EXPORT(rv_store_set_len) void rv_store_set_len(uint32_t s, int n)
{
    if (s < PLAT_STORES && n <= (int)PLAT_SLOT_MAX)
        store_len[s] = n;
}
EXPORT(rv_store_writes) uint32_t rv_store_writes(void) { return store_writes; }
EXPORT(rv_store_slots) uint32_t rv_store_slots(void) { return PLAT_STORES; }   /* the projects and the sound banks */
EXPORT(rv_store_max) uint32_t rv_store_max(void) { return PLAT_SLOT_MAX; }

/* ------------------------------------------------------ the instrument --- */
EXPORT(web_track) int web_track(void) { return ui_track(); }
EXPORT(web_set_track) void web_set_track(int t) { ui_set_track(t); }
EXPORT(web_section) int web_section(void) { return ui_section(); }
EXPORT(web_goto) void web_goto(int sec, int page) { ui_goto(sec, page); }
EXPORT(web_moom) void web_moom(void) { if (moom_active()) moom_exit(); else moom_start(); }   /* the hidden game (on the FM-1: HOME + SAVE at power-on) */
EXPORT(web_moom_on) int web_moom_on(void) { return moom_active(); }
EXPORT(web_moom_level) void web_moom_level(int lv) { moom_warp(lv); }   /* tests: straight to a level */
EXPORT(web_moom_pose) void web_moom_pose(float x, float y, float dir) { moom_pose(x, y, dir); }
EXPORT(web_moom_exit) void web_moom_exit(void) { moom_exit(); }
EXPORT(web_page_title) const char *web_page_title(void) { return ui_page_title(); }
EXPORT(web_kind) int web_kind(int t) { return rv.tp[t & 7][TP_KIND]; }
EXPORT(web_engine) int web_engine(int t) { return rv.tp[t & 7][TP_ENGINE]; }
EXPORT(web_src0) int web_src0(int t) { return rv.tp[t & 7][TP_S0]; }
EXPORT(web_set_kind) void web_set_kind(int t, int kind, int engine) { if (kind == KIND_PERC) rv_track_set_kind(t & 7, kind); else rv_track_defaults(t & 7, kind, engine); }
EXPORT(web_meter) float web_meter(int t) { return rv_live.meter[t & 7]; }
EXPORT(web_fenv) float web_fenv(int t) { return rv_live.fenv[t & 7]; }   /* the FILTER ENV of the newest voice */
float rv_senv_level(int t, int *env);
void rv_senv_start(int t, int e);
EXPORT(web_senv) float web_senv(int t) { return rv_senv_level(t & 7, 0); }   /* STEP ENV: its level now */
EXPORT(web_senv_env) int web_senv_env(int t) { int e = -1; rv_senv_level(t & 7, &e); return e; }   /* the envelope playing (0-31), or -1 */
EXPORT(web_senv_start) void web_senv_start(int t, int e) { rv_senv_start(t & 7, e); }   /* (tests) */
EXPORT(web_step_set) void web_step_set(int t, int s, int on, int env) { rv_step *p = &rv.pat[t & 7][rv.pat_cur[t & 7] % RV_PATTERNS].s[s & 63]; p->flags = on ? (uint8_t)(p->flags | ST_ON) : (uint8_t)(p->flags & ~ST_ON); p->env = (uint8_t)env; if (on && !p->vel) { p->vel = 100; p->len = 4; p->note = 60; p->chance = 100; } }
EXPORT(web_master_l) float web_master_l(void) { return rv_live.master_l; }
EXPORT(web_master_r) float web_master_r(void) { return rv_live.master_r; }
EXPORT(web_playing) int web_playing(void) { return rv_live.playing; }
EXPORT(web_armed) int web_armed(void) { return ui_armed(); }
EXPORT(web_frozen) int web_frozen(void) { return rv_tape_frozen(); }
EXPORT(web_transport) void web_transport(int run) { rv_transport(run); }
EXPORT(web_catch) void web_catch(void) { rv_catch(); }
EXPORT(web_resample) void web_resample(void) { rv_resample(); }
EXPORT(web_all_off) void web_all_off(void) { rv_all_off(); }
int rv_save(int slot);
int rv_load(int slot);
EXPORT(web_save) int web_save(int slot) { return rv_save(slot); }   /* (tests) */
EXPORT(web_load) int web_load(int slot) { return rv_load(slot); }
EXPORT(web_demo) void web_demo(int which)
{
    rv_transport(0);
    rv_demo(which);
}
EXPORT(web_project_name) const char *web_project_name(void) { return rv.name; }
EXPORT(web_tempo) int web_tempo(void) { return rv.gp[GP_TEMPO]; }
EXPORT(web_bar) int web_bar(void) { return rv_live.bar; }
EXPORT(web_beat) int web_beat(void) { return rv_live.beat; }
EXPORT(web_step) int web_step(int t) { return rv_live.step[t & 7][0]; }
EXPORT(web_mute) int web_mute(void) { return rv.mute; }
EXPORT(web_set_mute) void web_set_mute(int m) { rv.mute = (uint8_t)m; }
EXPORT(web_tape_seconds) float web_tape_seconds(void) { return rv_tape_seconds(); }
EXPORT(web_kind_name) const char *web_kind_name(int k) { return rv_kind_names[k % KIND_N]; }
EXPORT(web_engine_name) const char *web_engine_name(int e) { return rv_engine_names[e % ENG_N]; }
EXPORT(web_sound_name) const char *web_sound_name(int t)
{
    const rv_pinfo *pi = rv_pinfo_track(t & 7, TP_S0);
    int k = rv.tp[t & 7][TP_KIND];
    if (k == KIND_SYNTH)
        return rv_engine_names[rv.tp[t & 7][TP_ENGINE] % ENG_N];
    return pi->names ? pi->names[rv.tp[t & 7][TP_S0] - pi->min] : "";
}
EXPORT(web_preset_count) int web_preset_count(void) { return rv_preset_count(); }
EXPORT(web_preset_name) const char *web_preset_name(int i) { return rv_preset_name(i); }
EXPORT(web_preset_kind) int web_preset_kind(int i) { return rv_preset_kind(i); }
EXPORT(web_preset_apply) void web_preset_apply(int t, int i) { rv_preset_apply(t & 7, i); }

/* the sound bank (SAVE > SOUNDS): list, load into a track, keep a track's sound under a name */
int rv_bank_used(int i);
const char *rv_bank_name(int i);
const char *rv_bank_kind_label(int i);
int rv_bank_store(int t, int i, const char *name);
int rv_bank_apply(int t, int i);
int rv_bank_clear(int i);
static char bank_name_buf[16];
void rv_bank_select(int b);
int rv_bank_current(void);
EXPORT(web_bank_n) int web_bank_n(void) { return 32; }
EXPORT(web_bank_banks) int web_bank_banks(void) { return PLAT_BANKS; }
EXPORT(web_bank_select) void web_bank_select(int b) { rv_bank_select(b); }   /* banks A..D: one in RAM at a time */
EXPORT(web_bank_current) int web_bank_current(void) { return rv_bank_current(); }
EXPORT(web_bank_used) int web_bank_used(int i) { return rv_bank_used(i); }
EXPORT(web_bank_name) const char *web_bank_name(int i) { return rv_bank_name(i); }
EXPORT(web_bank_kind_label) const char *web_bank_kind_label(int i) { return rv_bank_kind_label(i); }
EXPORT(web_bank_load) int web_bank_load(int t, int i) { return rv_bank_apply(t & 7, i); }
EXPORT(web_bank_name_buf) char *web_bank_name_buf(void) { return bank_name_buf; }   /* the page writes a name here, */
EXPORT(web_bank_store) int web_bank_store(int t, int i) { bank_name_buf[11] = 0; return rv_bank_store(t & 7, i, bank_name_buf); }   /* then stores */
EXPORT(web_bank_clear) int web_bank_clear(int i) { return rv_bank_clear(i); }
extern int rv_fault_count, rv_fault_where, rv_fault_track;
EXPORT(web_fault_track) int web_fault_track(void) { return rv_fault_track; }
EXPORT(web_fault_count) int web_fault_count(void) { return rv_fault_count; }
EXPORT(web_fault_where) int web_fault_where(void) { return rv_fault_where; }
EXPORT(web_cpu) uint32_t web_cpu(void) { return rv_live.voices_synth + rv_live.voices_strike; }
EXPORT(web_tp) int web_tp(int t, int p) { return p >= 0 && p < TP_N ? rv.tp[t & 7][p] : 0; }
EXPORT(web_tp_set) void web_tp_set(int t, int p, int v) { if (p >= 0 && p < TP_N) rv_pid_set(PID_T(t & 7, p), v); }
EXPORT(web_head_step) int web_head_step(int t, int h) { return rv_live.step[t & 7][h & 1]; }
EXPORT(web_fg_rate) float web_fg_rate(int t, int h) { return rv_live.fg_rate[t & 7][h & 1]; }
EXPORT(web_gp) int web_gp(int g) { return g >= 0 && g < GP_N ? rv.gp[g] : 0; }
EXPORT(web_gp_set) void web_gp_set(int g, int v) { rv_pid_set(PID_G(g), v); }
EXPORT(web_gp_tapesrc) int web_gp_tapesrc(void) { return GP_TAPESRC; }
EXPORT(web_gp_tapemon) int web_gp_tapemon(void) { return GP_TAPEMON; }
EXPORT(web_gp_tempo) int web_gp_tempo(void) { return GP_TEMPO; }

EXPORT(web_mat_set) void web_mat_set(int i, int src, int dst, int amt) { rv_mslot *m = &rv.mat[i & 7]; m->src = (uint8_t)src; m->dst = (uint16_t)dst; m->amt = (int8_t)amt; }   /* (tests) */
EXPORT(web_mat_amt) int web_mat_amt(int i) { return rv_live.mat_amt[i & 7]; }   /* a row's AMOUNT as other rows move it */
EXPORT(web_pid_mat) int web_pid_mat(int k) { return PID_MAT(k); }
EXPORT(web_pid_t) int web_pid_t(int t, int p) { return PID_T(t, p); }
EXPORT(web_pid_g) int web_pid_g(int g) { return PID_G(g); }

/* projects and sounds to and from a computer: SysEx in (whole messages, F0..F7), replies out */
static uint8_t sx_in[1024];
EXPORT(web_sysex_in_buf) uint8_t *web_sysex_in_buf(void) { return sx_in; }
EXPORT(web_sysex_in) int web_sysex_in(uint32_t n)       /* sx_in holds F0 .. F7 */
{
    if (n < 2 || n > sizeof sx_in || sx_in[0] != 0xF0 || sx_in[n - 1] != 0xF7)
        return 0;
    return rv_xfer_wire(sx_in + 1, n - 2);
}
EXPORT(web_sysex_out) uint8_t *web_sysex_out(void) { return sx_out; }
EXPORT(web_sysex_out_len) uint32_t web_sysex_out_len(void) { return sx_out_n; }
EXPORT(web_sysex_out_clear) void web_sysex_out_clear(void) { sx_out_n = 0; }
EXPORT(web_storage_changed) void web_storage_changed(int slot) { ui_storage_changed(slot); }   /* (an imported file, written straight in) */
EXPORT(web_mat_dst) int web_mat_dst(int i) { return rv.mat[i & 7].dst; }   /* (tests) */
EXPORT(web_eff) float web_eff(int t, int p) { return rv_param_eff(t & 7, p); }   /* a track parameter as modulation has it now (tests) */
EXPORT(web_kit_rate) void web_kit_rate(int t, int l, int v) { rv.kit_rate[t & 7][l % RV_LANES] = (uint8_t)RV_LRATE_STORE(v); }   /* (tests) v: 0..8, 4 = the track's */
EXPORT(web_kit_hit) void web_kit_hit(int t, int s, int l) { rv_step *p = &rv.pat[t & 7][rv.pat_cur[t & 7] % RV_PATTERNS].s[s & 63]; p->flags |= ST_ON; p->note |= (uint8_t)(1u << l); p->vel = 100; p->chance = 100; }
EXPORT(web_step_byte) void web_step_byte(int t, int s, int field, int v)   /* (tests) 0: ratchet, 1: cut, 2: note, 3: vel, 4: len (a kit: which lanes roll) */
{
    rv_step *p = &rv.pat[t & 7][rv.pat_cur[t & 7] % RV_PATTERNS].s[s & 63];
    if (field == 0) p->ratchet = (uint8_t)v;
    else if (field == 1) p->cut = (uint8_t)v;
    else if (field == 2) p->note = (uint8_t)v;
    else if (field == 4) p->len = (uint8_t)v;
    else p->vel = (uint8_t)v;
}
EXPORT(web_punch) void web_punch(int fx, int on) { rv_punch(fx, on); }
EXPORT(web_route_set) void web_route_set(int a, int b, int type, int amt) { rv.infl[a & 7][b & 7].type = (uint8_t)type; rv.infl[a & 7][b & 7].amt = (int8_t)amt; }
EXPORT(web_scene_store) void web_scene_store(int s) { rv_scene_store(s); }
EXPORT(web_scene_launch) void web_scene_launch(int s) { rv_scene_launch(s); }
EXPORT(web_live_glide) float web_live_glide(void) { return rv_live.glide; }
EXPORT(web_live_bed) float web_live_bed(void) { return rv_live.bed_env; }
EXPORT(web_lane_step) int web_lane_step(int t, int l) { return rv_live.lane_step[t & 7][l % RV_LANES]; }
EXPORT(web_step_info) int web_step_info(int t, int st, int what)   /* (tests) 0: on, 1: note, 2: notes in its chord (1-4) */
{
    const rv_step *p = &rv.pat[t & 7][rv.pat_cur[t & 7] % RV_PATTERNS].s[st & 63];
    return what == 0 ? (p->flags & ST_ON) != 0 : what == 1 ? p->note : 1 + (p->chord[0] != 0) + (p->chord[1] != 0) + (p->chord[2] != 0);
}
