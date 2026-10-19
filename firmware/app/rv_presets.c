/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: sound presets (PRESETS on the voice pages) and the demo projects. */
#include "rv.h"

typedef struct { const char *name; uint8_t kind, engine; const int8_t *v; } rv_preset;   /* v: (parameter, value) pairs */
#define END -1

/* ---------------------------------------------------------------- synth --- */
static const int8_t p_glass[] = {TP_S0, 0, TP_S2, 5, TP_S3, 44, TP_S9, 20, TP_CUT, 72, TP_RES, 18, TP_EFILT, 14, TP_ATK, 62, TP_DEC, 84,
                                  TP_SUS, 100, TP_REL, 92, TP_GIVE, 30, TP_REVS, 70, TP_DLYS, 30, TP_POLY, 3, TP_LEVEL, 84, END};
static const int8_t p_sub[] = {TP_S0, 90, TP_S4, 90, TP_CUT, 52, TP_RES, 34, TP_EFILT, 34, TP_ATK, 2, TP_DEC, 52, TP_SUS, 70, TP_REL, 34,
                                TP_POLY, 0, TP_GLIDE, 24, TP_PITCH, -12, TP_DRIVE, 30, TP_LEVEL, 96, END};
static const int8_t p_pluck[] = {TP_S0, 20, TP_S9, 6, TP_CUT, 46, TP_RES, 44, TP_EFILT, 46, TP_ATK, 0, TP_DEC, 48, TP_SUS, 0, TP_REL, 44,
                                  TP_REVS, 34, TP_DLYS, 40, TP_LEVEL, 90, END};
static const int8_t p_czbell[] = {TP_S0, 5, TP_S3, 62, TP_S4, 40, TP_FTYPE, FT_LP, TP_CUT, 112, TP_RES, 10, TP_EFILT, 0, TP_ATK, 0,
                                   TP_DEC, 84, TP_SUS, 24, TP_REL, 84, TP_REVS, 50, TP_LEVEL, 84, END};
static const int8_t p_czbrass[] = {TP_S0, 0, TP_S3, 28, TP_S4, 52, TP_S1, 1, TP_S2, 40, TP_S5, 30, TP_CUT, 100, TP_ATK, 34, TP_DEC, 72,
                                    TP_SUS, 92, TP_REL, 60, TP_GIVE, 44, TP_LEVEL, 84, END};
static const int8_t p_foam[] = {TP_S0, 1, TP_S3, 84, TP_S6, 96, TP_S7, 30, TP_S4, -20, TP_CUT, 118, TP_ATK, 20, TP_SUS, 100, TP_REL, 70,
                                 TP_BITS, 30, TP_LEVEL, 76, TP_DLYS, 40, END};
static const int8_t p_fmep[] = {TP_S0, 3, TP_S2, 3, TP_S3, 3, TP_S4, 3, TP_S5, 23, TP_S6, 74, TP_S7, 92, TP_S8, 40, TP_S13, 62,
                                 TP_FTYPE, FT_LP, TP_CUT, 120, TP_EFILT, 0, TP_ATK, 0, TP_DEC, 92, TP_SUS, 50, TP_REL, 70, TP_REVS, 40, TP_LEVEL, 88, END};
static const int8_t p_fmbell[] = {TP_S0, 0, TP_S3, 27, TP_S4, 28, TP_S5, 19, TP_S6, 64, TP_S7, 40, TP_S8, 24, TP_S13, 84, TP_FTYPE, FT_LP,
                                   TP_CUT, 124, TP_ATK, 0, TP_DEC, 100, TP_SUS, 10, TP_REL, 96, TP_REVS, 64, TP_DLYS, 40, TP_LEVEL, 82, END};
static const int8_t p_fmgrowl[] = {TP_S0, 1, TP_S1, 96, TP_S3, 1, TP_S4, 5, TP_S6, 80, TP_S7, 50, TP_S8, 60, TP_S13, 50, TP_S14, 40,
                                    TP_CUT, 70, TP_RES, 40, TP_EFILT, 30, TP_POLY, 0, TP_PITCH, -12, TP_DRIVE, 40, TP_LEVEL, 82, END};
static const int8_t p_string[] = {TP_S0, 0, TP_S1, 92, TP_S2, 20, TP_S3, 22, TP_S7, 100, TP_FTYPE, FT_LP, TP_CUT, 120, TP_EFILT, 0,
                                   TP_ATK, 0, TP_DEC, 110, TP_SUS, 127, TP_REL, 96, TP_REVS, 40, TP_LEVEL, 100, END};
static const int8_t p_bowed[] = {TP_S0, 1, TP_S1, 70, TP_S4, 110, TP_S7, 110, TP_S6, 60, TP_FTYPE, FT_LP, TP_CUT, 110, TP_EFILT, 0,
                                  TP_ATK, 56, TP_SUS, 116, TP_REL, 76, TP_GIVE, 30, TP_REVS, 60, TP_LEVEL, 104, END};
static const int8_t p_breath[] = {TP_S0, 2, TP_S1, 60, TP_S4, 96, TP_S7, 100, TP_FTYPE, FT_LP, TP_CUT, 100, TP_EFILT, 10, TP_ATK, 40,
                                   TP_SUS, 110, TP_REL, 60, TP_REVS, 60, TP_LEVEL, 104, END};
static const int8_t p_membrane[] = {TP_S0, 3, TP_S1, 80, TP_S2, 30, TP_S5, 30, TP_S7, 70, TP_FTYPE, FT_LP, TP_CUT, 127, TP_EFILT, 0,
                                     TP_ATK, 0, TP_SUS, 127, TP_REL, 100, TP_LEVEL, 100, END};
static const int8_t p_vowel[] = {TP_S0, 30, TP_S2, 44, TP_S5, 72, TP_S7, 34, TP_FTYPE, FT_LP, TP_CUT, 116, TP_EFILT, 0, TP_ATK, 24,
                                  TP_SUS, 110, TP_REL, 66, TP_GIVE, 24, TP_REVS, 50, TP_LEVEL, 86, TP_EDST1, TP_S0, TP_EAMT1, 20, END};
static const int8_t p_fof[] = {TP_S0, 70, TP_S2, 30, TP_S3, 1, TP_S6, 40, TP_S7, 20, TP_FTYPE, FT_LP, TP_CUT, 124, TP_EFILT, 0,
                                TP_ATK, 40, TP_SUS, 110, TP_REL, 80, TP_REVS, 70, TP_POLY, 3, TP_LEVEL, 90, END};
static const int8_t p_pulsar[] = {TP_S0, 1, TP_S1, 30, TP_S2, 2, TP_S3, 40, TP_S4, 50, TP_S6, 84, TP_FTYPE, FT_LP, TP_CUT, 110,
                                   TP_EFILT, 10, TP_ATK, 10, TP_DEC, 80, TP_SUS, 80, TP_REL, 70, TP_DLYS, 50, TP_LEVEL, 96, END};
static const int8_t p_swarm[] = {TP_S0, 3, TP_S1, 50, TP_S2, 3, TP_S3, 70, TP_S4, 96, TP_S5, 40, TP_S6, 60, TP_FTYPE, FT_LP, TP_CUT, 100,
                                  TP_ATK, 50, TP_SUS, 110, TP_REL, 90, TP_GIVE, 40, TP_REVS, 70, TP_LEVEL, 96, END};
static const int8_t p_dustvoice[] = {TP_S0, 7, TP_S1, 20, TP_S2, 2, TP_S5, 96, TP_S6, 96, TP_FTYPE, FT_BP, TP_CUT, 100, TP_RES, 50,
                                      TP_EFILT, 0, TP_ATK, 0, TP_SUS, 110, TP_REL, 60, TP_DLYS, 60, TP_LEVEL, 110, END};
static const int8_t p_lpg[] = {TP_S0, 40, TP_S1, 30, TP_S9, 8, TP_FTYPE, FT_LPG, TP_CUT, 110, TP_RES, 10, TP_ATK, 0, TP_DEC, 40, TP_SUS, 0,
                                TP_REL, 40, TP_REVS, 40, TP_LEVEL, 104, END};

/* --------------------------------------------------------------- strike --- */
static const int8_t s_wood[] = {TP_S0, EXC_CLICK, TP_S1, BODY_MODAL, TP_S2, 52, TP_S3, 60, TP_S5, 12, TP_S10, 40, TP_S11, 10, TP_LEVEL, 100, END};
static const int8_t s_metal[] = {TP_S0, EXC_METAL, TP_S1, BODY_DISC, TP_S2, 84, TP_S3, 96, TP_S5, 40, TP_S13, 20, TP_S12, 80, TP_LEVEL, 76, TP_REVS, 30, END};
static const int8_t s_kick[] = {TP_S0, EXC_CLICK, TP_S1, BODY_PING, TP_S2, 60, TP_S14, 96, TP_S10, 70, TP_S11, 14, TP_PITCH, -24, TP_DRIVE, 30, TP_LEVEL, 110, END};
static const int8_t s_snare[] = {TP_S0, EXC_NOISE, TP_S1, BODY_MODAL, TP_S2, 40, TP_S3, 90, TP_S5, 60, TP_S10, 90, TP_S11, 40, TP_LEVEL, 92, END};
static const int8_t s_hat[] = {TP_S0, EXC_METAL, TP_S1, BODY_PING, TP_S2, 24, TP_S3, 120, TP_S10, 60, TP_S11, 20, TP_PITCH, 24, TP_FTYPE, FT_HP, TP_CUT, 96, TP_LEVEL, 72, END};
static const int8_t s_dustbell[] = {TP_S0, EXC_DUST, TP_S1, BODY_MODAL, TP_S2, 96, TP_S3, 80, TP_S5, 70, TP_S8, 30, TP_S9, 30, TP_S10, 0, TP_LEVEL, 70, TP_REVS, 60, END};
static const int8_t s_rain[] = {TP_S0, EXC_RAIN, TP_S1, BODY_DISC, TP_S2, 70, TP_S3, 100, TP_S8, 10, TP_S9, 60, TP_S10, 0, TP_LEVEL, 72, TP_DLYS, 40, END};
static const int8_t s_bowbar[] = {TP_S0, EXC_BOW, TP_S1, BODY_MODAL, TP_S2, 110, TP_S3, 70, TP_S5, 30, TP_S10, 0, TP_ATK, 40, TP_LEVEL, 92, TP_REVS, 60, END};
static const int8_t s_strings[] = {TP_S0, EXC_CLICK, TP_S1, BODY_STRINGS, TP_S2, 100, TP_S3, 90, TP_S6, 50, TP_S5, 30, TP_S10, 20, TP_LEVEL, 96, END};
static const int8_t s_clockwork[] = {TP_S0, EXC_DUST, TP_S1, BODY_PING, TP_S2, 70, TP_S8, 127, TP_S9, 70, TP_S10, 0, TP_PITCH, 12, TP_LEVEL, 80, END};
static const int8_t s_fmtom[] = {TP_S0, EXC_FM, TP_S1, BODY_MODAL, TP_S2, 60, TP_S3, 40, TP_S14, 60, TP_S10, 40, TP_PITCH, -12, TP_LEVEL, 92, END};
static const int8_t s_pipe[] = {TP_S0, EXC_BLOW, TP_S1, BODY_PING, TP_S2, 110, TP_S3, 60, TP_S10, 0, TP_ATK, 50, TP_LEVEL, 100, TP_REVS, 50, END};
static const int8_t s_listen[] = {TP_S0, EXC_INPUT, TP_S1, BODY_MODAL, TP_S2, 100, TP_S3, 80, TP_S10, 0, TP_LEVEL, 92, END};

/* ----------------------------------------------------------------- tape --- */
static const int8_t t_mlr[] = {TP_S0, TAPE_MLR, TP_S1, 0, TP_S2, 6, TP_S4, 1, TP_LEVEL, 100, END};
static const int8_t t_cloud[] = {TP_S0, TAPE_BEADS, TP_S1, 0, TP_S6, 60, TP_S7, 84, TP_S8, 80, TP_S10, 96, TP_S12, 127, TP_LEVEL, 96, TP_REVS, 50, END};
static const int8_t t_keys[] = {TP_S0, TAPE_BEADS, TP_S1, 0, TP_S6, 40, TP_S7, 60, TP_S8, 50, TP_S10, 64, TP_S12, 127, TP_LEVEL, 100, END};
static const int8_t t_slices[] = {TP_S0, TAPE_SLICE, TP_S1, 0, TP_S14, 16, TP_S15, 70, TP_ATK, 2, TP_DEC, 50, TP_SUS, 90, TP_REL, 30, TP_LEVEL, 100, END};
static const int8_t t_half[] = {TP_S0, TAPE_MLR, TP_S1, 1, TP_S2, 5, TP_S3, 4, TP_S4, 2, TP_LEVEL, 90, TP_DLYS, 40, END};

static const rv_preset presets[] = {
    {"GLASS PAD", KIND_SYNTH, ENG_VA, p_glass}, {"SUB BASS", KIND_SYNTH, ENG_VA, p_sub}, {"PLUCK", KIND_SYNTH, ENG_VA, p_pluck},
    {"LOW GATE", KIND_SYNTH, ENG_VA, p_lpg}, {"CZ BELL", KIND_SYNTH, ENG_PD, p_czbell}, {"CZ BRASS", KIND_SYNTH, ENG_PD, p_czbrass},
    {"FOAM", KIND_SYNTH, ENG_PD, p_foam}, {"FM PIANO", KIND_SYNTH, ENG_FM, p_fmep}, {"FM BELLS", KIND_SYNTH, ENG_FM, p_fmbell},
    {"FM GROWL", KIND_SYNTH, ENG_FM, p_fmgrowl}, {"STRING", KIND_SYNTH, ENG_PHYS, p_string}, {"BOWED", KIND_SYNTH, ENG_PHYS, p_bowed},
    {"BREATH", KIND_SYNTH, ENG_PHYS, p_breath}, {"MEMBRANE", KIND_SYNTH, ENG_PHYS, p_membrane}, {"VOWEL", KIND_SYNTH, ENG_FORMANT, p_vowel},
    {"CHOIR FOF", KIND_SYNTH, ENG_FORMANT, p_fof}, {"PULSAR", KIND_SYNTH, ENG_PARTICLE, p_pulsar}, {"SWARM", KIND_SYNTH, ENG_PARTICLE, p_swarm},
    {"DUST VOICE", KIND_SYNTH, ENG_PARTICLE, p_dustvoice},
    {"WOOD", KIND_STRIKE, 0, s_wood}, {"METAL", KIND_STRIKE, 0, s_metal}, {"KICK BODY", KIND_STRIKE, 0, s_kick},
    {"SNARE AIR", KIND_STRIKE, 0, s_snare}, {"HAT", KIND_STRIKE, 0, s_hat}, {"DUST BELL", KIND_STRIKE, 0, s_dustbell},
    {"RAIN GLASS", KIND_STRIKE, 0, s_rain}, {"BOWED BAR", KIND_STRIKE, 0, s_bowbar}, {"STRINGS", KIND_STRIKE, 0, s_strings},
    {"CLOCKWORK", KIND_STRIKE, 0, s_clockwork}, {"FM TOM", KIND_STRIKE, 0, s_fmtom}, {"PIPE", KIND_STRIKE, 0, s_pipe},
    {"LISTENER", KIND_STRIKE, 0, s_listen},
    {"MLR LOOP", KIND_TAPE, 0, t_mlr}, {"CLOUD", KIND_TAPE, 0, t_cloud}, {"GRAIN KEYS", KIND_TAPE, 0, t_keys},
    {"SLICES", KIND_TAPE, 0, t_slices}, {"HALF SPEED", KIND_TAPE, 0, t_half},
};
#define NPRESETS ((int)(sizeof(presets) / sizeof(presets[0])))

int rv_preset_count(void) { return NPRESETS; }
const char *rv_preset_name(int i) { return i >= 0 && i < NPRESETS ? presets[i].name : ""; }
int rv_preset_kind(int i) { return i >= 0 && i < NPRESETS ? presets[i].kind : -1; }

void rv_preset_apply(int t, int i)
{
    const int8_t *v;
    if (i < 0 || i >= NPRESETS)
        return;
    rv_track_defaults(t, presets[i].kind, presets[i].engine);
    for (v = presets[i].v; *v >= 0; v += 2)
        rv.tp[t][v[0]] = v[1];
}

/* the preset whose kind and engine match a track best, from i in direction dir */
int rv_preset_step(int t, int cur, int dir)
{
    int k = rv.tp[t][TP_KIND], n;
    for (n = 0; n < NPRESETS; n++) {
        cur += dir;
        if (cur < 0)
            cur = NPRESETS - 1;
        if (cur >= NPRESETS)
            cur = 0;
        if (presets[cur].kind == k)
            return cur;
    }
    return cur;
}

/* ------------------------------------------------------------------ demos --- */
static rv_step *st(int t, int s) { return &rv.pat[t][0].s[s]; }

static void on(int t, int s, int note, int vel, int len)
{
    rv_step *p = st(t, s);
    p->flags = ST_ON;
    p->note = (uint8_t)note;
    p->vel = (uint8_t)vel;
    p->len = (uint8_t)len;
}

static void route(int a, int b, int type, int amt, int arg)
{
    rv.infl[a][b].type = (uint8_t)type;
    rv.infl[a][b].amt = (int8_t)amt;
    rv.infl[a][b].arg = (uint8_t)arg;
    rv.infl[a][b].chance = 100;
}

static int find_preset(const char *name)
{
    int i, k;
    for (i = 0; i < NPRESETS; i++) {
        for (k = 0; name[k] && presets[i].name[k] == name[k]; k++)
            ;
        if (!name[k] && !presets[i].name[k])
            return i;
    }
    return 0;
}

static void name_set(const char *s)
{
    int i;
    for (i = 0; i < 11 && s[i]; i++)
        rv.name[i] = s[i];
    rv.name[i] = 0;
}

void rv_demo(int which)
{
    int s;
    rv_project_init();
    if (which == 0) {
        /* RAVEL: synthesize, catch, rebuild. Scene 1 resamples its first bar into row 1, and the
         * tape tracks play it back: T7 jumps around it (the wood hits move its playhead), T8 grains it. */
        name_set("RAVEL");
        rv.gp[GP_TEMPO] = 104;
        rv_preset_apply(0, find_preset("GLASS PAD"));
        rv_preset_apply(1, find_preset("SUB BASS"));
        rv_preset_apply(2, find_preset("PULSAR"));
        rv_preset_apply(3, find_preset("STRING"));
        rv_preset_apply(4, find_preset("WOOD"));
        rv_preset_apply(5, find_preset("METAL"));
        rv_preset_apply(6, find_preset("MLR LOOP"));
        rv_preset_apply(7, find_preset("CLOUD"));
        rv.tp[0][TP_LEN] = 32;
        on(0, 0, 57, 80, 60);
        on(0, 16, 53, 80, 60);
        on(0, 24, 55, 70, 30);
        rv.tp[1][TP_LEN] = 16;
        on(1, 0, 45, 110, 6);
        on(1, 3, 45, 80, 3);
        on(1, 6, 57, 90, 2);
        on(1, 8, 43, 110, 6);
        on(1, 11, 43, 80, 3);
        on(1, 14, 55, 90, 2);
        rv.tp[2][TP_LEN] = 12;                     /* polymeter against 16 */
        on(2, 0, 76, 90, 3);
        on(2, 3, 79, 70, 3);
        on(2, 5, 81, 80, 6);
        on(2, 9, 74, 70, 3);
        rv.tp[3][TP_LEN] = 16;
        on(3, 2, 69, 100, 4);
        on(3, 7, 72, 90, 4);
        on(3, 13, 67, 100, 4);
        st(3, 13)->ratchet = 2 | (1 << 3);
        rv.tp[4][TP_LEN] = 16;
        for (s = 0; s < 16; s += 4)
            on(4, s, 50, 120, 2);
        on(4, 10, 62, 80, 2);
        on(4, 15, 57, 70, 2);
        st(4, 15)->micro = 6;
        rv.tp[5][TP_LEN] = 16;
        on(5, 6, 74, 70, 2);
        on(5, 14, 79, 60, 2);
        rv.tp[6][TP_LEN] = 16;
        rv.tp[7][TP_LEN] = 16;
        on(7, 0, 60, 100, 32);
        on(7, 8, 67, 90, 32);
        /* influence: wood excites the metal, moves the tape's playhead; the bass opens the pad */
        route(4, 5, RT_EXCITE, 40, 0);
        route(4, 6, RT_TAPE, 0, 4);
        route(1, 0, RT_MOD, 30, TP_CUT);
        route(3, 7, RT_TRIG, 0, 0);
        rv.gp[GP_DLYTIME] = 92;
        rv.gp[GP_DLYFB] = 60;
        rv.gp[GP_REVSIZE] = 90;
        rv.gp[GP_TAPESRC] = 0;
        rv.gp[GP_RESBARS] = 1;
        rv.gp[GP_ROW2S] = 64;
        rv.gp[GP_ROW2E] = 128;
        rv.scene[0].used = 1;
        rv.scene[0].resample = 1;
        for (s = 0; s < RV_TRACKS; s++) {
            int b;
            rv.scene[0].level[s] = (uint8_t)rv.tp[s][TP_LEVEL];
            for (b = 0; b < RV_TRACKS; b++)
                rv.scene[0].infl[s][b] = rv.infl[s][b];
        }
        rv.arr_len = 1;
        rv.arr[0].scene = 0;
        rv.arr[0].bars = 8;
        rv.gp[GP_SONGMODE] = 1;                    /* SONG: the scene re-catches the tape every 8 bars */
    } else if (which == 1) {
        /* DUST: lowercase. Clicks, droplets through the vocoder, a struck disc rung by dust,
         * a membrane and the feedback bus returning into the strings. */
        name_set("DUST");
        rv.gp[GP_TEMPO] = 92;
        rv_preset_apply(0, find_preset("CHOIR FOF"));
        rv_preset_apply(1, find_preset("DUST VOICE"));
        rv_preset_apply(2, find_preset("MEMBRANE"));
        rv_preset_apply(3, find_preset("LOW GATE"));
        rv_preset_apply(4, find_preset("CLOCKWORK"));
        rv_preset_apply(5, find_preset("STRINGS"));
        rv_preset_apply(6, find_preset("SLICES"));
        rv_preset_apply(7, find_preset("GRAIN KEYS"));
        rv.tp[0][TP_VOC] = 90;
        rv.tp[0][TP_LEVEL] = 40;
        rv.gp[GP_VOCBODY] = 1;
        rv.tp[0][TP_LEN] = 32;
        on(0, 0, 60, 70, 120);
        on(0, 16, 63, 70, 120);
        rv.tp[1][TP_LEN] = 16;
        rv.tp[1][TP_CLKSRC] = 3;                   /* dust clocks it */
        on(1, 0, 72, 90, 2);
        on(1, 5, 79, 80, 2);
        on(1, 9, 84, 70, 2);
        rv.tp[2][TP_LEN] = 16;
        on(2, 0, 41, 110, 4);
        on(2, 11, 41, 80, 4);
        rv.tp[3][TP_LEN] = 16;
        rv.tp[3][TP_DIR] = DIR_PING;
        on(3, 2, 67, 100, 2);
        on(3, 6, 70, 90, 2);
        on(3, 9, 74, 90, 2);
        on(3, 13, 77, 100, 2);
        rv.tp[4][TP_LEN] = 16;
        on(4, 4, 60, 100, 16);
        on(4, 12, 60, 100, 16);
        rv.tp[5][TP_LEN] = 16;
        on(5, 8, 55, 100, 4);
        rv.tp[5][TP_FBS] = 50;
        rv.gp[GP_FBRET] = 6;                       /* the feedback bus rings T6's strings */
        rv.gp[GP_FBPROC] = 2;
        rv.gp[GP_FBREGEN] = 90;
        rv.tp[6][TP_LEN] = 16;
        for (s = 0; s < 16; s += 2)
            on(6, s, 53 + (s * 5) % 16, 100, 4);
        route(2, 6, RT_CLOCK, 0, 0);
        route(4, 5, RT_EXCITE, 30, 0);
        rv.gp[GP_REVSIZE] = 110;
        rv.gp[GP_REVLEVEL] = 100;
        rv.scene[0].used = 1;
        rv.scene[0].resample = 1;
        for (s = 0; s < RV_TRACKS; s++) {
            int b;
            rv.scene[0].level[s] = (uint8_t)rv.tp[s][TP_LEVEL];
            for (b = 0; b < RV_TRACKS; b++)
                rv.scene[0].infl[s][b] = rv.infl[s][b];
        }
        rv.arr_len = 1;
        rv.arr[0].scene = 0;
        rv.arr[0].bars = 16;
        rv.gp[GP_SONGMODE] = 1;
    } else if (which == 3) {
        /* GRAPH: the graph sequencer. T1's two heads split rhythm from melody: head 1 walks a
         * short looping path and plays (GATE), head 2 wanders the long way round at a third of
         * the speed, swelling faster and slower, and sets the pitch (PITCH). T2's head 1 rolls
         * into its steps (a DECAY motion), and its head 2 plays T5's wood on its own path. */
        static const uint8_t mel[16] = {60, 63, 67, 70, 72, 70, 67, 65, 63, 58, 60, 55, 67, 74, 72, 62};
        name_set("GRAPH");
        rv.gp[GP_TEMPO] = 96;
        rv_preset_apply(0, find_preset("CZ BELL"));
        rv_preset_apply(1, find_preset("SUB BASS"));
        rv_preset_apply(4, find_preset("WOOD"));
        rv_preset_apply(5, find_preset("METAL"));
        for (s = 2; s < RV_TRACKS; s++)
            if (s != 4 && s != 5)
                rv.mute |= (uint8_t)(1u << s);
        rv.tp[0][TP_LEN] = 16;
        for (s = 0; s < 16; s++)
            on(0, s, mel[s], 70 + (s * 37) % 50, 3);
        st(0, 3)->to[0] = 1;                          /* cords (step + 1): steps 0-3 loop back to 0, */
        st(0, 3)->to[1] = 8;                          /* or, every other time, out to 7 */
        st(0, 11)->to[0] = 1;                         /* and 11 returns to 0 */
        rv.tp[0][TP_SEQMODE] = SEQ_GRAPH;
        rv.tp[0][TP_BRANCH] = 1;                      /* TURNS */
        rv.tp[0][TP_H1ROLE] = 1;                      /* GATE */
        rv.tp[0][TP_H2ROLE] = ROLE_PITCH;
        rv.tp[0][TP_H2RATE] = 1;                      /* 1/3 */
        rv.tp[0][TP_H2START] = 9;
        rv.tp[0][TP_H2SHAPE] = FG_SWELL;
        rv.tp[0][TP_H2DEPTH] = 48;
        rv.tp[0][TP_H2CYCLE] = 12;
        rv.tp[0][TP_REVS] = 70;
        rv.tp[0][TP_DLYS] = 50;
        rv.tp[1][TP_LEN] = 8;
        on(1, 0, 36, 110, 4);
        on(1, 3, 36, 80, 2);
        on(1, 5, 43, 90, 2);
        on(1, 6, 34, 100, 4);
        st(1, 6)->to[1] = 1;
        rv.tp[1][TP_SEQMODE] = SEQ_GRAPH;
        rv.tp[1][TP_BRANCH] = 0;                      /* CHANCE */
        rv.tp[1][TP_H1SHAPE] = FG_DECAY;
        rv.tp[1][TP_H1DEPTH] = 63;
        rv.tp[1][TP_H1CYCLE] = 8;
        rv.tp[1][TP_H2ROLE] = ROLE_NOTES;
        rv.tp[1][TP_H2TARGET] = 5;                    /* head 2 plays T5 */
        rv.tp[1][TP_H2RATE] = 6;                      /* x2 */
        rv.tp[1][TP_H2START] = 4;
        rv.tp[1][TP_H2SHAPE] = FG_LFO1;
        rv.tp[1][TP_H2DEPTH] = 40;
        rv.tp[1][TP_H2COMB] = COMB_ADD;
        rv.gp[GP_LFO1RATE] = 30;
        rv.tp[5][TP_LEN] = 16;
        on(5, 4, 74, 60, 2);
        on(5, 12, 79, 50, 2);
        rv.tp[5][TP_REVS] = 60;
        rv.gp[GP_REVSIZE] = 100;
        rv.gp[GP_DLYTIME] = 96;
    } else {
        name_set("BLANK");
    }
}
