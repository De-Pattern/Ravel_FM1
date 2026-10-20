/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the interface. Each button opens the section nearest its printed word; SELECT walks
 * the section's pages, ALGORITHM picks the track, PRESETS browses what the page holds (sounds,
 * patterns, scenes, slots, rows...), KNOB 1-4 set the four values in the knob row. Holding a
 * button gives its layer: SEQ makes the keys steps on any page (hold a step and turn a knob:
 * a lock), LFO puts the macros on the knobs, FX puts punch-in effects on the keys, ARP the tape,
 * GLO mutes and solos, SAVE stores scenes, SCL sets the root, REC catches and resamples.
 *
 * Every page: a top strip (track, sound, page), the knob row, a live picture, a footer that
 * says what the controls do right now. */
#include "rv.h"
#include "rv_gfx.c"

int rv_preset_count(void);
const char *rv_preset_name(int i);
int rv_preset_kind(int i);
void rv_preset_apply(int t, int i);
int rv_preset_step(int t, int cur, int dir);
void rv_scene_store(int s);
int rv_quantize_note(int n);
int rv_fold_key(int base, int k);
float rv_time_s(int v);
float rv_cut_hz(float v);

/* ---------------------------------------------------------- sections --- */
enum { SEC_HOME, SEC_VOICE, SEC_ARTIC, SEC_MOD, SEC_BUS, SEC_SCALE, SEC_TAPE, SEC_SEQ, SEC_SONG, SEC_SAVE, NSEC };
static const char *const sec_names[NSEC] = {"HOME", "VOICE", "ARTICULATION", "MOD", "BUSES", "SCALE", "TAPE", "SEQ", "SONG", "SAVE"};
static const uint8_t sec_button[NSEC] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SCL, B_ARP, B_SEQ, B_GLO, B_SAVE};

/* knob bindings: what a knob in the row stands for */
enum { K_NONE, K_TP, K_GP, K_STEP, K_ROUTE, K_MAT, K_MACRO, K_ROW, K_ARR, K_ARC, K_LIB, K_TLEVEL, K_SAVE, K_CZ };
/* K_CZ ids: line << 8 | envelope << 4 | field (the wave fields have no envelope) */
enum { CZF_WAVE1, CZF_WAVE2, CZF_KFA, CZF_KFW, CZF_RATE, CZF_LEVEL, CZF_SUS, CZF_END };
enum { SF_NOTE, SF_VEL, SF_LEN, SF_CHANCE, SF_MICRO, SF_RATCHET, SF_RCURVE, SF_COND, SF_ENV, SF_CUT, SF_TIE, SF_CORDA, SF_CORDB };
enum { RF_TYPE, RF_AMT, RF_ARG, RF_CHANCE };
enum { MF_SRC, MF_TRACK, MF_PARAM, MF_AMT };
enum { XF_DEST, XF_START, XF_END, XF_CURVE, XF_LO, XF_HI };
enum { AF_SCENE, AF_BARS, AF_ROWS, AF_MODE };
typedef struct { uint8_t kind; uint16_t id; } kb_t;

/* pictures */
enum { PIC_TRACKS, PIC_INFL, PIC_SOURCE, PIC_FILTER, PIC_SKIN, PIC_INSERT, PIC_MIX, PIC_ENV, PIC_LIB, PIC_MATRIX,
       PIC_LFO1, PIC_LFO2, PIC_DUST, PIC_MACRO, PIC_VOC, PIC_FB, PIC_PAIR, PIC_DELAY, PIC_SPACE, PIC_MASTER, PIC_SCALE,
       PIC_TAPE, PIC_STEPS, PIC_GRAPH, PIC_MOTION, PIC_MIXER, PIC_SCENES, PIC_ARR, PIC_ARCS, PIC_CLOCK, PIC_SAVE,
       PIC_CZWAVE, PIC_CZENV, PIC_BANDS };
/* when a page shows */
enum { V_ALL, V_SRC0, V_SRC1, V_SRC2, V_SRC3, V_SYNTH, V_STRIKE, V_TAPE, V_MLR, V_BEADS, V_SLICE, V_NOTTAPE,
       V_STEPSMODE, V_GRAPH, V_GRAPH2, V_CZL1, V_CZL2, V_NOTCZ };
/* what PRESETS does on the page */
enum { PR_NONE, PR_SOUND, PR_ROUTE, PR_MAT, PR_MACRO, PR_ROW, PR_PATTERN, PR_SCENE, PR_ARR, PR_ARC, PR_LIB, PR_SLOT, PR_STEPCUR,
       PR_CZSTEP };

typedef struct {
    uint8_t sec, vis, pic, pr;
    const char *title;
    kb_t k[4];
} page_t;

#define TP(p) {K_TP, p}
#define GP(p) {K_GP, p}
#define SF(p) {K_STEP, p}
#define RF(p) {K_ROUTE, p}
#define MF(p) {K_MAT, p}
#define XF(p) {K_MACRO, p}
#define NO {K_NONE, 0}
#define CZW(l, f) {K_CZ, (uint16_t)((l) << 8 | (f))}
#define CZE(l, e, f) {K_CZ, (uint16_t)((l) << 8 | (e) << 4 | (f))}

static const page_t pages[] = {
    {SEC_HOME, V_ALL, PIC_TRACKS, PR_SOUND, "TRACKS", {TP(TP_LEVEL), TP(TP_PAN), TP(TP_KIND), TP(TP_ENGINE)}},
    {SEC_HOME, V_ALL, PIC_INFL, PR_ROUTE, "INFLUENCE", {RF(RF_TYPE), RF(RF_AMT), RF(RF_ARG), RF(RF_CHANCE)}},

    {SEC_VOICE, V_ALL, PIC_SOURCE, PR_SOUND, "SOUND", {TP(TP_KIND), TP(TP_ENGINE), TP(TP_POLY), TP(TP_VELO)}},
    {SEC_VOICE, V_SRC0, PIC_SOURCE, PR_SOUND, "SOURCE 1", {TP(TP_S0), TP(TP_S1), TP(TP_S2), TP(TP_S3)}},
    {SEC_VOICE, V_SRC1, PIC_SOURCE, PR_SOUND, "SOURCE 2", {TP(TP_S4), TP(TP_S5), TP(TP_S6), TP(TP_S7)}},
    {SEC_VOICE, V_SRC2, PIC_SOURCE, PR_SOUND, "SOURCE 3", {TP(TP_S8), TP(TP_S9), TP(TP_S10), TP(TP_S11)}},
    {SEC_VOICE, V_SRC3, PIC_SOURCE, PR_SOUND, "SOURCE 4", {TP(TP_S12), TP(TP_S13), TP(TP_S14), TP(TP_S15)}},
    /* the CZ engine's lines: waves and key follow, then the three 8-step envelopes each */
    {SEC_VOICE, V_CZL1, PIC_CZWAVE, PR_SOUND, "LINE 1", {CZW(0, CZF_WAVE1), CZW(0, CZF_WAVE2), CZW(0, CZF_KFA), CZW(0, CZF_KFW)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCO 1", {CZE(0, CZE_DCO, CZF_RATE), CZE(0, CZE_DCO, CZF_LEVEL), CZE(0, CZE_DCO, CZF_SUS), CZE(0, CZE_DCO, CZF_END)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCW 1", {CZE(0, CZE_DCW, CZF_RATE), CZE(0, CZE_DCW, CZF_LEVEL), CZE(0, CZE_DCW, CZF_SUS), CZE(0, CZE_DCW, CZF_END)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCA 1", {CZE(0, CZE_DCA, CZF_RATE), CZE(0, CZE_DCA, CZF_LEVEL), CZE(0, CZE_DCA, CZF_SUS), CZE(0, CZE_DCA, CZF_END)}},
    {SEC_VOICE, V_CZL2, PIC_CZWAVE, PR_SOUND, "LINE 2", {CZW(1, CZF_WAVE1), CZW(1, CZF_WAVE2), CZW(1, CZF_KFA), CZW(1, CZF_KFW)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCO 2", {CZE(1, CZE_DCO, CZF_RATE), CZE(1, CZE_DCO, CZF_LEVEL), CZE(1, CZE_DCO, CZF_SUS), CZE(1, CZE_DCO, CZF_END)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCW 2", {CZE(1, CZE_DCW, CZF_RATE), CZE(1, CZE_DCW, CZF_LEVEL), CZE(1, CZE_DCW, CZF_SUS), CZE(1, CZE_DCW, CZF_END)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCA 2", {CZE(1, CZE_DCA, CZF_RATE), CZE(1, CZE_DCA, CZF_LEVEL), CZE(1, CZE_DCA, CZF_SUS), CZE(1, CZE_DCA, CZF_END)}},
    {SEC_VOICE, V_ALL, PIC_SOURCE, PR_SOUND, "PITCH", {TP(TP_PITCH), TP(TP_FINE), TP(TP_GLIDE), NO}},
    {SEC_VOICE, V_ALL, PIC_FILTER, PR_SOUND, "FILTER", {TP(TP_FTYPE), TP(TP_CUT), TP(TP_RES), TP(TP_KTRK)}},
    {SEC_VOICE, V_ALL, PIC_SKIN, PR_SOUND, "SKIN", {TP(TP_DRIVE), TP(TP_FOLD), TP(TP_BITS), TP(TP_DEREZ)}},
    {SEC_VOICE, V_ALL, PIC_SKIN, PR_SOUND, "SKIN 2", {TP(TP_MASK), TP(TP_VOC), NO, NO}},
    {SEC_VOICE, V_ALL, PIC_INSERT, PR_SOUND, "INSERT", {TP(TP_INS), TP(TP_INA), TP(TP_INB), TP(TP_INMIX)}},
    {SEC_VOICE, V_ALL, PIC_MIX, PR_SOUND, "MIX", {TP(TP_LEVEL), TP(TP_PAN), TP(TP_DLYS), TP(TP_REVS)}},
    {SEC_VOICE, V_ALL, PIC_MIX, PR_SOUND, "SENDS", {TP(TP_FBS), TP(TP_PAIRS), TP(TP_VOC), TP(TP_BANDS)}},

    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "ONSET", {TP(TP_ATK), TP(TP_ACURVE), TP(TP_OVER), TP(TP_GIVE)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "CONTINUANT", {TP(TP_DEC), TP(TP_SUS), TP(TP_LOOP), TP(TP_VELO)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "TERMINATION", {TP(TP_REL), TP(TP_TAIL), TP(TP_EFILT), TP(TP_GIVE)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "DESTINATIONS", {TP(TP_EDST1), TP(TP_EAMT1), TP(TP_EDST2), TP(TP_EAMT2)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "SHAPE", {TP(TP_ESHAPE), TP(TP_EMORPH), TP(TP_ETIME), TP(TP_EMODE)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "SHAPE 2", {TP(TP_EWARP), TP(TP_REL), TP(TP_GIVE), TP(TP_EFILT)}},
    {SEC_ARTIC, V_ALL, PIC_LIB, PR_LIB, "LIBRARY", {{K_LIB, 0}, {K_LIB, 1}, {K_LIB, 2}, NO}},

    {SEC_MOD, V_ALL, PIC_MATRIX, PR_MAT, "MATRIX", {MF(MF_SRC), MF(MF_TRACK), MF(MF_PARAM), MF(MF_AMT)}},
    {SEC_MOD, V_ALL, PIC_LFO1, PR_NONE, "LFO 1", {GP(GP_LFO1RATE), GP(GP_LFO1SHAPE), NO, NO}},
    {SEC_MOD, V_ALL, PIC_LFO2, PR_NONE, "LFO 2", {GP(GP_LFO2RATE), GP(GP_LFO2SHAPE), NO, NO}},
    {SEC_MOD, V_ALL, PIC_DUST, PR_NONE, "DUST", {GP(GP_DUSTRATE), GP(GP_DUSTDECAY), NO, NO}},
    {SEC_MOD, V_ALL, PIC_MACRO, PR_MACRO, "MACRO", {XF(XF_DEST), XF(XF_START), XF(XF_END), XF(XF_CURVE)}},
    {SEC_MOD, V_ALL, PIC_MACRO, PR_MACRO, "MACRO WINDOW", {XF(XF_LO), XF(XF_HI), {K_GP, GP_MACRO1}, {K_GP, GP_MACRO2}}},

    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "DELAY", {GP(GP_DLYTIME), GP(GP_DLYFB), GP(GP_DLYTONE), GP(GP_DLYLEVEL)}},
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "DELAY 2", {TP(TP_DLYS), GP(GP_DLYSPREAD), GP(GP_DLYWOB), GP(GP_DLYFREEZE)}},
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "GRAINS", {GP(GP_DLYMODE), GP(GP_GRDENS), GP(GP_GRSIZE), GP(GP_GRPITCH)}},
    {SEC_BUS, V_ALL, PIC_SPACE, PR_NONE, "SPACE", {GP(GP_REVSIZE), GP(GP_REVDAMP), GP(GP_REVLEVEL), TP(TP_REVS)}},
    {SEC_BUS, V_ALL, PIC_SPACE, PR_NONE, "SPACE 2", {GP(GP_REVMOD), GP(GP_REVDIFF), GP(GP_REVLOW), GP(GP_REVMODE)}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK", {GP(GP_FBTIME), GP(GP_FBREGEN), GP(GP_FBPROC), GP(GP_FBAMT)}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK 2", {GP(GP_FBTUNE), GP(GP_FBCUT), GP(GP_FBEDGE), GP(GP_FBRET)}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK 3", {GP(GP_FBLEVEL), TP(TP_FBS), NO, NO}},
    {SEC_BUS, V_ALL, PIC_PAIR, PR_NONE, "FILTER PAIR", {GP(GP_PAIRMODE), GP(GP_PAIRF1), GP(GP_PAIRF2), GP(GP_PAIRRES)}},
    {SEC_BUS, V_ALL, PIC_PAIR, PR_NONE, "PAIR 2", {GP(GP_PAIRT1), GP(GP_PAIRT2), GP(GP_PAIRLEVEL), TP(TP_PAIRS)}},
    {SEC_BUS, V_ALL, PIC_PAIR, PR_NONE, "PAIR LFOS", {GP(GP_PAIRLFO1), GP(GP_PAIRLFO2), GP(GP_PAIRDEPTH), NO}},
    {SEC_BUS, V_ALL, PIC_BANDS, PR_NONE, "BANDS", {TP(TP_BANDS), GP(GP_BANDLEVEL), GP(GP_BANDQ), GP(GP_BANDSHIFT)}},
    {SEC_BUS, V_ALL, PIC_BANDS, PR_NONE, "BAND SWEEP", {GP(GP_BANDSHAPE), GP(GP_BANDRATE), GP(GP_BANDDEPTH), GP(GP_BANDWIDTH)}},
    {SEC_BUS, V_ALL, PIC_BANDS, PR_NONE, "BANDS 1-4", {GP(GP_BAND1), GP(GP_BAND2), GP(GP_BAND3), GP(GP_BAND4)}},
    {SEC_BUS, V_ALL, PIC_BANDS, PR_NONE, "BANDS 5-8", {GP(GP_BAND5), GP(GP_BAND6), GP(GP_BAND7), GP(GP_BAND8)}},
    {SEC_BUS, V_ALL, PIC_VOC, PR_NONE, "VOCODER", {GP(GP_VOCBODY), GP(GP_VOCTILT), GP(GP_VOCLEVEL), TP(TP_VOC)}},
    {SEC_BUS, V_ALL, PIC_VOC, PR_NONE, "VOCODER 2", {GP(GP_VOCATK), GP(GP_VOCREL), GP(GP_VOCSHIFT), GP(GP_VOCQ)}},
    {SEC_BUS, V_ALL, PIC_MASTER, PR_NONE, "MASTER", {GP(GP_MDRIVE), GP(GP_CUTSIZE), GP(GP_CUTREP), GP(GP_CUTMIX)}},

    {SEC_SCALE, V_ALL, PIC_SCALE, PR_NONE, "SCALE", {GP(GP_ROOT), GP(GP_SCALE), GP(GP_TRANSPOSE), TP(TP_INTERVAL)}},

    {SEC_TAPE, V_ALL, PIC_TAPE, PR_ROW, "CAPTURE", {GP(GP_TAPEQUAL), GP(GP_TAPESRC), GP(GP_RESBARS), GP(GP_ERODE)}},
#ifdef RV_NO_AUDIO_IN
    {SEC_TAPE, V_ALL, PIC_TAPE, PR_ROW, "ROWS", {{K_ROW, 0}, {K_ROW, 1}, NO, NO}},   /* no audio input */
#else
    {SEC_TAPE, V_ALL, PIC_TAPE, PR_ROW, "ROWS", {{K_ROW, 0}, {K_ROW, 1}, GP(GP_INPUTLEVEL), GP(GP_TAPEMON)}},
#endif
    {SEC_TAPE, V_TAPE, PIC_TAPE, PR_SOUND, "PLAYBACK", {TP(TP_S0), TP(TP_S1), TP(TP_S2), TP(TP_S3)}},
    {SEC_TAPE, V_MLR, PIC_TAPE, PR_SOUND, "MLR", {TP(TP_S4), TP(TP_S5), TP(TP_PITCH), TP(TP_LEVEL)}},
    {SEC_TAPE, V_BEADS, PIC_TAPE, PR_SOUND, "BEADS", {TP(TP_S6), TP(TP_S7), TP(TP_S8), TP(TP_S9)}},
    {SEC_TAPE, V_BEADS, PIC_TAPE, PR_SOUND, "BEADS 2", {TP(TP_S10), TP(TP_S11), TP(TP_S12), TP(TP_S13)}},
    {SEC_TAPE, V_SLICE, PIC_TAPE, PR_SOUND, "SLICES", {TP(TP_S14), TP(TP_S15), TP(TP_PITCH), TP(TP_LEVEL)}},

    {SEC_SEQ, V_ALL, PIC_STEPS, PR_PATTERN, "STEPS", {SF(SF_NOTE), SF(SF_VEL), SF(SF_LEN), SF(SF_CHANCE)}},
    {SEC_SEQ, V_ALL, PIC_STEPS, PR_PATTERN, "STEP TIMING", {SF(SF_MICRO), SF(SF_RATCHET), SF(SF_RCURVE), SF(SF_COND)}},
    {SEC_SEQ, V_ALL, PIC_STEPS, PR_PATTERN, "STEP SHAPE", {SF(SF_ENV), SF(SF_CUT), SF(SF_TIE), NO}},
    {SEC_SEQ, V_ALL, PIC_STEPS, PR_PATTERN, "TRACK", {TP(TP_SEQMODE), TP(TP_LEN), TP(TP_RATE), TP(TP_CLKSRC)}},
    {SEC_SEQ, V_STEPSMODE, PIC_STEPS, PR_PATTERN, "DIRECTION", {TP(TP_DIR), NO, NO, NO}},
    {SEC_SEQ, V_GRAPH, PIC_GRAPH, PR_STEPCUR, "GRAPH", {SF(SF_CORDA), SF(SF_CORDB), TP(TP_BRANCH), TP(TP_H2ROLE)}},
    {SEC_SEQ, V_GRAPH, PIC_GRAPH, PR_STEPCUR, "HEAD 1", {TP(TP_H1TRANS), NO, NO, NO}},
    {SEC_SEQ, V_GRAPH, PIC_MOTION, PR_NONE, "HEAD 1 MOTION", {TP(TP_H1SHAPE), TP(TP_H1DEPTH), TP(TP_H1CYCLE), TP(TP_H1COMB)}},
    {SEC_SEQ, V_GRAPH2, PIC_GRAPH, PR_STEPCUR, "HEAD 2", {TP(TP_H2TARGET), TP(TP_H2RATE), TP(TP_H2START), TP(TP_H2TRANS)}},
    {SEC_SEQ, V_GRAPH2, PIC_MOTION, PR_NONE, "HEAD 2 MOTION", {TP(TP_H2SHAPE), TP(TP_H2DEPTH), TP(TP_H2CYCLE), TP(TP_H2COMB)}},

    {SEC_SONG, V_ALL, PIC_MIXER, PR_NONE, "MIXER 1-4", {{K_TLEVEL, 0}, {K_TLEVEL, 1}, {K_TLEVEL, 2}, {K_TLEVEL, 3}}},
    {SEC_SONG, V_ALL, PIC_MIXER, PR_NONE, "MIXER 5-8", {{K_TLEVEL, 4}, {K_TLEVEL, 5}, {K_TLEVEL, 6}, {K_TLEVEL, 7}}},
    {SEC_SONG, V_ALL, PIC_SCENES, PR_SCENE, "SCENES", {GP(GP_SONGMODE), GP(GP_TEMPO), NO, NO}},
    {SEC_SONG, V_ALL, PIC_ARR, PR_ARR, "ARRANGE", {{K_ARR, AF_SCENE}, {K_ARR, AF_BARS}, {K_ARR, AF_ROWS}, {K_ARR, AF_MODE}}},
    {SEC_SONG, V_ALL, PIC_ARCS, PR_ARC, "ARCS", {{K_ARC, 0}, NO, NO, NO}},
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK", {GP(GP_TEMPO), GP(GP_SWING), GP(GP_CLKMODE), GP(GP_CLKDEPTH)}},
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK B", {GP(GP_CLKSHAPE), GP(GP_CLKBRATE), GP(GP_CLKBSHAPE), GP(GP_CLKTHR)}},
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK FEEL", {GP(GP_CLKLOCK), GP(GP_QUANT), NO, NO}},

    {SEC_SAVE, V_ALL, PIC_SAVE, PR_SLOT, "PROJECT", {{K_SAVE, 0}, NO, NO, NO}},
};
#define NPAGES ((int)(sizeof(pages) / sizeof(pages[0])))

/* ------------------------------------------------------------- state --- */
static struct {
    uint8_t sec, page[NSEC], track, octave_ofs;
    int8_t octave;
    uint32_t prev_btn, prev_keys, btn_down_ms[NB];
    uint8_t layer_used[NB];
    uint8_t step_page;                       /* SEQ: which 16 steps the keys show */
    int16_t step_cur;                        /* the step the step pages edit */
    uint8_t cz_step;                         /* the step the CZ envelope pages edit, 0..7 */
    uint32_t step_key_held;                  /* step keys held (bit = white key index) */
    uint8_t step_turned;
    uint8_t key_note[NKEYS];
    uint8_t key_trk[NKEYS];                  /* the track a key's note went to, + 1 (0: the key plays no note) */
    uint8_t cur_route, cur_mat, cur_macro, cur_row, cur_arr, cur_arc, cur_scene, cur_slot, save_action;
    uint16_t cur_lib;
    int16_t preset_idx[RV_TRACKS];
    char msg[24];
    uint32_t msg_until;
    uint32_t turned_ms[4];
    uint8_t armed;
    uint8_t save_confirm;
    char slot_names[PLAT_SLOTS][13];
    uint8_t slot_used[PLAT_SLOTS];
    uint32_t mlr_held;                       /* white keys held on the tape page */
    uint32_t frame;
    uint8_t dust_hist[60];
    uint8_t dust_w;
} ui;

static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t black_keys[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

static int white_index(int k)
{
    int i;
    for (i = 0; i < 16; i++)
        if (white_keys[i] == k)
            return i;
    return -1;
}
static int black_index(int k)
{
    int i;
    for (i = 0; i < 11; i++)
        if (black_keys[i] == k)
            return i;
    return -1;
}

static void msg(const char *s)
{
    int i;
    for (i = 0; i < 23 && s[i]; i++)
        ui.msg[i] = s[i];
    ui.msg[i] = 0;
    ui.msg_until = plat_ms() + 1400;
}

static int sx_int(char *o, int v)
{
    char t[12];
    int n = 0, i = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    if (v < 0)
        o[i++] = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (n)
        o[i++] = t[--n];
    o[i] = 0;
    return i;
}
static int sx_cat(char *o, const char *s)
{
    int i = 0;
    while (o[i])
        i++;
    while (*s)
        o[i++] = *s++;
    o[i] = 0;
    return i;
}
static void sx_note(char *o, int n)
{
    static const char *nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char d[6];
    o[0] = 0;
    sx_cat(o, nm[((n % 12) + 12) % 12]);
    sx_int(d, n / 12 - 1);
    sx_cat(o, d);
}

/* --------------------------------------------------- page visibility --- */
static int src_page_visible(int t, int pg)
{
    int s;
    for (s = pg * 4; s < pg * 4 + 4; s++)
        if (rv_pinfo_track(t, TP_S0 + s)->name)
            return 1;
    return 0;
}

static int page_visible(int i)
{
    int t = ui.track, kind = rv.tp[t][TP_KIND], mode = rv.tp[t][TP_S0];
    switch (pages[i].vis) {
    case V_SRC0: return kind != KIND_TAPE && src_page_visible(t, 0);
    case V_SRC1: return kind != KIND_TAPE && src_page_visible(t, 1);
    case V_SRC2: return kind != KIND_TAPE && src_page_visible(t, 2);
    case V_SRC3: return kind != KIND_TAPE && src_page_visible(t, 3);
    case V_SYNTH: return kind == KIND_SYNTH;
    case V_STRIKE: return kind == KIND_STRIKE;
    case V_TAPE: return kind == KIND_TAPE;
    case V_MLR: return kind == KIND_TAPE && mode == TAPE_MLR;
    case V_BEADS: return kind == KIND_TAPE && mode == TAPE_BEADS;
    case V_SLICE: return kind == KIND_TAPE && mode == TAPE_SLICE;
    case V_NOTTAPE: return kind != KIND_TAPE;
    case V_STEPSMODE: return rv.tp[t][TP_SEQMODE] != SEQ_GRAPH && rv.tp[t][TP_DIR] != DIR_GRAPH;
    case V_GRAPH: return rv.tp[t][TP_SEQMODE] == SEQ_GRAPH || rv.tp[t][TP_DIR] == DIR_GRAPH;
    case V_GRAPH2: return (rv.tp[t][TP_SEQMODE] == SEQ_GRAPH || rv.tp[t][TP_DIR] == DIR_GRAPH) && rv.tp[t][TP_H2ROLE] != ROLE_OFF;
    case V_CZL1: return kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ && rv.tp[t][TP_S0] != 1;   /* not LINE = 2 */
    case V_CZL2: return kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ && (rv.tp[t][TP_S0] == 1 || rv.tp[t][TP_S0] == 3);
    case V_NOTCZ: return !(kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ);
    default: return 1;
    }
}

/* the visible pages of a section, in order */
static int sec_page_list(int sec, uint8_t *out)
{
    int i, n = 0;
    for (i = 0; i < NPAGES; i++)
        if (pages[i].sec == sec && page_visible(i))
            out[n++] = (uint8_t)i;
    return n;
}

static const page_t *cur_page(void)
{
    uint8_t list[16];
    int n = sec_page_list(ui.sec, list);
    if (!n)
        return &pages[0];
    if (ui.page[ui.sec] >= n)
        ui.page[ui.sec] = (uint8_t)(n - 1);
    return &pages[list[ui.page[ui.sec]]];
}

/* ------------------------------------------------------ the step edited --- */
static rv_step *step_ptr(int si)
{
    return &rv.pat[ui.track][rv.pat_cur[ui.track] % RV_PATTERNS].s[si & 63];
}

static int held_step(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (ui.step_key_held & (1u << i))
            return ui.step_page * 16 + i;
    return -1;
}

/* --------------------------------------------------- knob bindings --- */
static const char *const rcurve_names[] = {"EVEN", "FASTER", "SLOWER", "EVEN-F", "FAST-F", "SLOW-F"};
static const char *const cut_names[] = {"NONE", "LOOP", "REVERSE", "STUTTER"};
static const char *const save_actions[] = {"SAVE", "LOAD", "DEMO 1", "DEMO 2", "DEMO 3", "INIT"};
static const char *const curve_names[] = {"LINEAR", "EXP", "LOG", "S"};
static const char *const lib_names[] = {"ATTACK", "BODY", "DEATH"};

typedef struct { const char *name; int min, max; uint8_t fmt; const char *const *names; } kinfo_t;

static int macro_dest_count(void) { return RV_TRACKS * TP_N + GP_N; }

static kinfo_t kb_info(kb_t k)
{
    kinfo_t r = {0, 0, 0, PF_NUM, 0};
    const rv_pinfo *pi;
    switch (k.kind) {
    case K_TP:
        pi = rv_pinfo_track(ui.track, k.id);
        r.name = pi->name;
        if (ui.sec == SEC_BUS && (k.id == TP_DLYS || k.id == TP_REVS || k.id == TP_FBS || k.id == TP_PAIRS || k.id == TP_VOC || k.id == TP_BANDS)) {
            static char send[12];                    /* on a bus page a track's send reads as its own */
            send[0] = 'T';
            send[1] = (char)('1' + ui.track);
            send[2] = 0;
            sx_cat(send, " SEND");
            r.name = send;
        }
        r.min = pi->min;
        r.max = pi->max;
        r.fmt = pi->fmt;
        r.names = pi->names;
        if (k.id == TP_H2START)                      /* a step that exists */
            r.max = clampi(rv.tp[ui.track][TP_LEN], 1, RV_STEPS);
        if (k.id == TP_POLY && rv.tp[ui.track][TP_KIND] == KIND_STRIKE)
            r.max = 1;                               /* a strike track has two voices: MONO or 2 */
        break;
    case K_GP:
        pi = rv_pinfo_global(k.id);
        r.name = pi->name;
        r.min = pi->min;
        r.max = pi->max;
        r.fmt = pi->fmt;
        r.names = pi->names;
        break;
    case K_TLEVEL:
        r.name = "LEVEL";
        r.max = 127;
        break;
    case K_STEP: {
        static const char *const nm[] = {"NOTE", "VELO", "LENGTH", "CHANCE", "MICRO", "RATCHET", "R CURVE", "CONDITN", "ENV", "CUT", "TIE", "CORD A", "CORD B"};
        r.name = nm[k.id];
        switch (k.id) {
        case SF_NOTE: r.max = 127; r.fmt = PF_NOTE; break;
        case SF_VEL: r.min = 1; r.max = 127; break;
        case SF_LEN: r.min = 1; r.max = 127; break;
        case SF_CHANCE: r.max = 100; break;
        case SF_MICRO: r.min = -6; r.max = 12; r.fmt = PF_BI; break;
        case SF_RATCHET: r.min = 1; r.max = 8; break;
        case SF_RCURVE: r.max = 5; r.fmt = PF_ENUM; r.names = rcurve_names; break;
        case SF_COND: r.max = RV_NCOND - 1; r.fmt = PF_ENUM; r.names = rv_cond_names; break;
        case SF_ENV: r.max = 256; break;
        case SF_CUT: r.max = 3; r.fmt = PF_ENUM; r.names = cut_names; break;
        case SF_TIE: r.max = 1; r.fmt = PF_ONOFF; break;
        default: r.max = clampi(rv.tp[ui.track][TP_LEN], 1, RV_STEPS); break;   /* cords: steps 1..LENGTH */
        }
        break;
    }
    case K_ROUTE: {
        static const char *const nm[] = {"ROUTE", "AMOUNT", "TARGET", "CHANCE"};
        const rv_route *ro = &rv.infl[ui.track][ui.cur_route];
        r.name = nm[k.id];
        if (k.id == RF_TYPE) {
            r.max = RT_N - 1;
            r.fmt = PF_ENUM;
            r.names = rv_route_names;
        } else if (k.id == RF_AMT) {
            r.min = -64;
            r.max = 63;
            r.fmt = PF_BI;
        } else if (k.id == RF_ARG) {
            r.max = ro->type == RT_MOD ? TP_N - 1 : ro->type == RT_TAPE ? 16 : ro->type == RT_TRIG ? 16 : 0;
            r.name = ro->type == RT_MOD ? "PARAM" : ro->type == RT_TAPE ? "JUMP TO" : ro->type == RT_TRIG ? "DELAY" : "-";
        } else {
            r.max = 100;
            r.name = ro->type == RT_TRIG ? "CHANCE" : "-";
        }
        break;
    }
    case K_MAT: {
        static const char *const nm[] = {"SOURCE", "TRACK", "PARAM", "AMOUNT"};
        r.name = nm[k.id];
        if (k.id == MF_SRC) {
            r.max = MS_N - 1;
            r.fmt = PF_ENUM;
            r.names = rv_msrc_names;
        } else if (k.id == MF_TRACK) {
            r.max = RV_TRACKS;
        } else if (k.id == MF_PARAM) {
            r.max = 1000;
        } else {
            r.min = -64;
            r.max = 63;
            r.fmt = PF_BI;
        }
        break;
    }
    case K_MACRO: {
        static const char *const nm[] = {"DEST", "START", "END", "CURVE", "FROM %", "TO %"};
        r.name = nm[k.id];
        if (k.id == XF_DEST)
            r.max = macro_dest_count();
        else if (k.id == XF_START || k.id == XF_END) {
            r.min = -64;
            r.max = 63;
            r.fmt = PF_BI;
        } else if (k.id == XF_CURVE) {
            r.max = 3;
            r.fmt = PF_ENUM;
            r.names = curve_names;
        } else
            r.max = 100;
        break;
    }
    case K_ROW:
        r.name = k.id ? "END" : "START";
        r.min = k.id ? 1 : 0;
        r.max = k.id ? 128 : 127;
        break;
    case K_ARR: {
        static const char *const nm[] = {"SCENE", "BARS", "ROWS", "PLAYS"};
        r.name = nm[k.id];
        r.min = k.id == AF_MODE ? 0 : 1;
        r.max = k.id == AF_SCENE ? 16 : k.id == AF_BARS ? 64 : k.id == AF_ROWS ? RV_ARR : 1;
        if (k.id == AF_MODE) {
            r.fmt = PF_ENUM;
            r.names = rv_pinfo_global(GP_SONGMODE)->names;
        }
        break;
    }
    case K_ARC:
        r.name = "DENSITY";
        r.max = 100;
        break;
    case K_LIB:
        r.name = lib_names[k.id];
        r.max = 127;
        break;
    case K_SAVE:
        r.name = "ACTION";
        r.max = 5;
        r.fmt = PF_ENUM;
        r.names = save_actions;
        break;
    case K_CZ: {
        static const char *const fn[] = {"WAVE 1", "WAVE 2", "DCA KF", "DCW KF", "RATE", "LEVEL", "SUS", "END"};
        static const char *const w1[] = {"SAW", "SQUARE", "PULSE", "DBL SIN", "SAW PLS", "RESO 1", "RESO 2", "RESO 3"};
        static const char *const w2[] = {"OFF", "SAW", "SQUARE", "PULSE", "DBL SIN", "SAW PLS", "RESO 1", "RESO 2", "RESO 3"};
        static const char *const sus[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8"};
        int f = k.id & 15, l = (k.id >> 8) & 1;
        r.name = fn[f];
        switch (f) {
        case CZF_WAVE1: r.min = 1; r.max = 8; r.fmt = PF_ENUM; r.names = w1; break;
        case CZF_WAVE2:                              /* two resonant waves cannot be cascaded */
            r.max = rv.cz[ui.track].line[l].wave1 >= 6 ? 5 : 8;
            r.fmt = PF_ENUM;
            r.names = w2;
            break;
        case CZF_KFA: case CZF_KFW: r.max = 9; break;
        case CZF_RATE: case CZF_LEVEL: r.max = 99; break;
        case CZF_SUS: r.max = 8; r.fmt = PF_ENUM; r.names = sus; break;
        default: r.min = 1; r.max = 8; break;
        }
        break;
    }
    default:
        break;
    }
    return r;
}

/* macro destinations: one list over every track's modulatable parameters, then the globals */
static int macro_dst_to_index(uint16_t pid)
{
    if (pid >= RV_NPID)
        return 0;
    if (pid < RV_GPID)
        return 1 + (pid / RV_TP) * TP_N + pid % RV_TP;
    return 1 + RV_TRACKS * TP_N + (pid - RV_GPID);
}
static uint16_t macro_index_to_dst(int i)
{
    if (i <= 0)
        return 0xFFFF;
    i--;
    if (i < RV_TRACKS * TP_N)
        return (uint16_t)PID_T(i / TP_N, i % TP_N);
    return (uint16_t)PID_G(i - RV_TRACKS * TP_N);
}

static int kb_get(kb_t k)
{
    rv_step *s;
    switch (k.kind) {
    case K_TP: return rv.tp[ui.track][k.id];
    case K_GP: return rv.gp[k.id];
    case K_TLEVEL: return rv.tp[k.id][TP_LEVEL];
    case K_STEP:
        s = step_ptr(held_step() >= 0 ? held_step() : ui.step_cur);
        switch (k.id) {
        case SF_NOTE: return s->note;
        case SF_VEL: return s->vel;
        case SF_LEN: return s->len ? s->len : 1;
        case SF_CHANCE: return s->chance;
        case SF_MICRO: return s->micro;
        case SF_RATCHET: return (s->ratchet & 7) + 1;
        case SF_RCURVE: return ((s->ratchet >> 3) & 3) + ((s->ratchet & 32) ? 3 : 0);
        case SF_COND: return s->cond;
        case SF_ENV: return s->env;
        case SF_CUT: return s->cut;
        case SF_TIE: return (s->flags & ST_TIE) ? 1 : 0;
        case SF_CORDA: return s->to[0];
        default: return s->to[1];
        }
    case K_ROUTE: {
        const rv_route *r = &rv.infl[ui.track][ui.cur_route];
        return k.id == RF_TYPE ? r->type : k.id == RF_AMT ? r->amt : k.id == RF_ARG ? r->arg : r->chance;
    }
    case K_MAT: {
        const rv_mslot *m = &rv.mat[ui.cur_mat];
        if (k.id == MF_SRC)
            return m->src;
        if (k.id == MF_TRACK)
            return m->dst >= RV_GPID ? RV_TRACKS : m->dst / RV_TP;
        if (k.id == MF_PARAM)
            return m->dst >= RV_GPID ? m->dst - RV_GPID : m->dst % RV_TP;
        return m->amt;
    }
    case K_MACRO: {
        const rv_mdest *d = &rv.macro[ui.cur_macro / 8].d[ui.cur_macro % 8];
        switch (k.id) {
        case XF_DEST: return macro_dst_to_index(d->dst);
        case XF_START: return d->start;
        case XF_END: return d->end;
        case XF_CURVE: return d->curve;
        case XF_LO: return d->lo;
        default: return d->hi;
        }
    }
    case K_ROW: return rv.gp[GP_ROW1S + 2 * ui.cur_row + k.id];
    case K_ARR:
        if (k.id == AF_SCENE)
            return rv.arr[ui.cur_arr].scene + 1;
        if (k.id == AF_BARS)
            return rv.arr[ui.cur_arr].bars ? rv.arr[ui.cur_arr].bars : 1;
        if (k.id == AF_ROWS)
            return rv.arr_len ? rv.arr_len : 1;
        return rv.gp[GP_SONGMODE];
    case K_ARC: return rv.arcs[ui.track][ui.cur_arc & 15];
    case K_LIB: return rv.lib[ui.cur_lib & 255][k.id];
    case K_CZ: {
        const rv_czline *pl = &rv.cz[ui.track].line[(k.id >> 8) & 1];
        const rv_czenv *e = &pl->env[clampi((k.id >> 4) & 15, 0, 2)];
        switch (k.id & 15) {
        case CZF_WAVE1: return pl->wave1;
        case CZF_WAVE2: return pl->wave2;
        case CZF_KFA: return pl->kf_dca;
        case CZF_KFW: return pl->kf_dcw;
        case CZF_RATE: return e->rate[ui.cz_step & 7];
        case CZF_LEVEL: return e->level[ui.cz_step & 7];
        case CZF_SUS: return e->sus;
        default: return e->end;
        }
    }
    case K_SAVE: return ui.save_action;
    default: return 0;
    }
}

static void kb_set(kb_t k, int v)
{
    kinfo_t in = kb_info(k);
    rv_step *s;
    if (v < in.min)
        v = in.min;
    if (v > in.max)
        v = in.max;
    switch (k.kind) {
    case K_TP:
        if (k.id == TP_KIND && v != rv.tp[ui.track][TP_KIND]) {
            rv_track_defaults(ui.track, v, rv.tp[ui.track][TP_ENGINE]);
            ui.preset_idx[ui.track] = -1;
            return;
        }
        if (k.id == TP_ENGINE && v != rv.tp[ui.track][TP_ENGINE] && rv.tp[ui.track][TP_KIND] == KIND_SYNTH) {
            int p;
            rv.tp[ui.track][TP_ENGINE] = (int16_t)v;
            for (p = TP_S0; p <= TP_S15; p++) {
                const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
                rv.tp[ui.track][p] = pi->name ? pi->def : 0;
            }
            if (v == ENG_CZ)
                rv_cz_init(ui.track);
            if (v == ENG_SQ)                          /* the SQ comes with its own filter */
                rv.tp[ui.track][TP_FTYPE] = FT_SQ80;
            return;
        }
        rv_pid_set(PID_T(ui.track, k.id), v);
        rv_motion(PID_T(ui.track, k.id), v);
        break;
    case K_GP:
        rv_pid_set(PID_G(k.id), v);
        break;
    case K_TLEVEL:
        rv.tp[k.id][TP_LEVEL] = (int16_t)v;
        break;
    case K_STEP: {
        int si = held_step() >= 0 ? held_step() : ui.step_cur;
        s = step_ptr(si);
        switch (k.id) {
        case SF_NOTE: s->note = (uint8_t)v; break;
        case SF_VEL: s->vel = (uint8_t)v; break;
        case SF_LEN: s->len = (uint8_t)v; break;
        case SF_CHANCE: s->chance = (uint8_t)v; break;
        case SF_MICRO: s->micro = (int8_t)v; break;
        case SF_RATCHET: s->ratchet = (uint8_t)((s->ratchet & ~7) | (v - 1)); break;
        case SF_RCURVE: s->ratchet = (uint8_t)((s->ratchet & 7) | ((v % 3) << 3) | (v >= 3 ? 32 : 0)); break;
        case SF_COND: s->cond = (uint8_t)v; break;
        case SF_ENV: s->env = (uint8_t)(v > 255 ? 255 : v); break;
        case SF_CUT: s->cut = (uint8_t)v; break;
        case SF_TIE: s->flags = (uint8_t)(v ? s->flags | ST_TIE : s->flags & ~ST_TIE); break;
        case SF_CORDA: s->to[0] = (uint8_t)v; break;
        default: s->to[1] = (uint8_t)v; break;
        }
        if (k.id != SF_CORDA && k.id != SF_CORDB && !(s->flags & ST_ON) && held_step() >= 0)
            s->flags |= ST_ON;                  /* editing a held empty step brings it in */
        break;
    }
    case K_ROUTE: {
        rv_route *r = &rv.infl[ui.track][ui.cur_route];
        if (k.id == RF_TYPE) {
            r->type = (uint8_t)v;
            if (v == RT_MOD && !rv_is_moddest(r->arg))
                r->arg = TP_CUT;
            if (v == RT_TRIG && r->chance == 0)
                r->chance = 100;
            if (r->amt == 0)
                r->amt = 32;
        } else if (k.id == RF_AMT)
            r->amt = (int8_t)v;
        else if (k.id == RF_ARG) {
            if (r->type == RT_MOD)
                r->arg = (uint8_t)rv_moddest_next(r->arg, v > r->arg ? 1 : -1);
            else
                r->arg = (uint8_t)v;
        } else
            r->chance = (uint8_t)v;
        break;
    }
    case K_MAT: {
        rv_mslot *m = &rv.mat[ui.cur_mat];
        if (k.id == MF_SRC)
            m->src = (uint8_t)v;
        else if (k.id == MF_TRACK) {
            int p = m->dst >= RV_GPID ? TP_CUT : m->dst % RV_TP;
            m->dst = (uint16_t)(v >= RV_TRACKS ? PID_G(GP_FBREGEN) : PID_T(v, rv_is_moddest(p) ? p : TP_CUT));
        } else if (k.id == MF_PARAM) {
            if (m->dst >= RV_GPID) {
                int g = clampi(v, 0, GP_N - 1);
                m->dst = (uint16_t)PID_G(g);
            } else {
                int cur = m->dst % RV_TP;
                m->dst = (uint16_t)PID_T(m->dst / RV_TP, rv_moddest_next(cur, v > cur ? 1 : -1));
            }
        } else
            m->amt = (int8_t)v;
        break;
    }
    case K_MACRO: {
        rv_mdest *d = &rv.macro[ui.cur_macro / 8].d[ui.cur_macro % 8];
        switch (k.id) {
        case XF_DEST: {
            /* skip what modulation cannot move */
            int i = v, dir = v > macro_dst_to_index(d->dst) ? 1 : -1, guard = 2000;
            while (i > 0 && i <= macro_dest_count() && guard--) {
                uint16_t pid = macro_index_to_dst(i);
                if (pid >= RV_GPID ? rv_pinfo_pid(pid)->name != 0 : (rv_is_moddest(pid % RV_TP) && rv_pinfo_pid(pid)->name))
                    break;
                i += dir;
            }
            d->dst = macro_index_to_dst(clampi(i, 0, macro_dest_count()));
            break;
        }
        case XF_START: d->start = (int8_t)v; break;
        case XF_END: d->end = (int8_t)v; break;
        case XF_CURVE: d->curve = (uint8_t)v; break;
        case XF_LO: d->lo = (uint8_t)v; break;
        default: d->hi = (uint8_t)v; break;
        }
        break;
    }
    case K_ROW:
        rv.gp[GP_ROW1S + 2 * ui.cur_row + k.id] = (int16_t)v;
        break;
    case K_ARR:
        if (k.id == AF_SCENE)
            rv.arr[ui.cur_arr].scene = (uint8_t)(v - 1);
        else if (k.id == AF_BARS)
            rv.arr[ui.cur_arr].bars = (uint8_t)v;
        else if (k.id == AF_ROWS) {
            int i;
            for (i = rv.arr_len; i < v && i < RV_ARR; i++)
                if (!rv.arr[i].bars) {
                    rv.arr[i].bars = 4;
                    rv.arr[i].scene = (uint8_t)(i & 15);
                }
            rv.arr_len = (uint8_t)v;
        } else
            rv.gp[GP_SONGMODE] = (int16_t)v;
        break;
    case K_ARC:
        rv.arcs[ui.track][ui.cur_arc & 15] = (uint8_t)v;
        break;
    case K_LIB:
        rv.lib[ui.cur_lib & 255][k.id] = (uint8_t)v;
        break;
    case K_CZ: {
        rv_czline *pl = &rv.cz[ui.track].line[(k.id >> 8) & 1];
        rv_czenv *e = &pl->env[clampi((k.id >> 4) & 15, 0, 2)];
        switch (k.id & 15) {
        case CZF_WAVE1:
            pl->wave1 = (uint8_t)v;
            if (v >= 6 && pl->wave2 >= 6)
                pl->wave2 = 0;
            break;
        case CZF_WAVE2: pl->wave2 = (uint8_t)v; break;
        case CZF_KFA: pl->kf_dca = (uint8_t)v; break;
        case CZF_KFW: pl->kf_dcw = (uint8_t)v; break;
        case CZF_RATE: e->rate[ui.cz_step & 7] = (uint8_t)v; break;
        case CZF_LEVEL: e->level[ui.cz_step & 7] = (uint8_t)v; break;
        case CZF_SUS: e->sus = (uint8_t)(v > e->end ? e->end : v); break;   /* the sustain step is never after the end */
        default:
            e->end = (uint8_t)v;
            if (e->sus > e->end)
                e->sus = e->end;
            if (ui.cz_step >= v)
                ui.cz_step = (uint8_t)(v - 1);
            break;
        }
        break;
    }
    case K_SAVE:
        ui.save_action = (uint8_t)v;
        ui.save_confirm = 0;
        break;
    default:
        break;
    }
}

static void kb_format(kb_t k, int v, char *o)
{
    kinfo_t in = kb_info(k);
    o[0] = 0;
    if (!in.name) {
        return;
    }
    if (k.kind == K_TP && k.id == TP_INA && rv.tp[ui.track][TP_INS] == INS_COMB) {
        sx_note(o, 60 + v - 64);                    /* the comb's pitch: 64 = C4 */
        return;
    }
    if (k.kind == K_TP) {
        rv_param_format(PID_T(ui.track, k.id), v, o, 12);
        return;
    }
    if (k.kind == K_GP) {
        rv_param_format(PID_G(k.id), v, o, 12);
        return;
    }
    if (k.kind == K_STEP && k.id == SF_NOTE) {
        sx_note(o, v);
        return;
    }
    if (k.kind == K_STEP && k.id == SF_LEN) {
        int whole = v / 8, rem = v % 8;
        static const char *const fr[8] = {"", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8"};
        if (whole)
            sx_int(o, whole);
        if (rem) {
            if (whole)
                sx_cat(o, " ");
            sx_cat(o, fr[rem]);
        }
        return;
    }
    if (k.kind == K_STEP && k.id == SF_ENV) {
        if (!v)
            sx_cat(o, "TRACK");
        else {
            sx_cat(o, "#");
            sx_int(o + 1, v);
        }
        return;
    }
    if (k.kind == K_STEP && (k.id == SF_CORDA || k.id == SF_CORDB)) {
        if (!v || v > clampi(rv.tp[ui.track][TP_LEN], 1, RV_STEPS))   /* past LENGTH: not used */
            sx_cat(o, k.id == SF_CORDA ? "NEXT" : "NONE");
        else {
            sx_cat(o, "> ");
            sx_int(o + 2, v);
        }
        return;
    }
    if (k.kind == K_STEP && k.id == SF_CHANCE) {
        sx_int(o, v);
        sx_cat(o, "%");
        return;
    }
    if (k.kind == K_ROUTE && k.id == RF_ARG) {
        const rv_route *r = &rv.infl[ui.track][ui.cur_route];
        if (r->type == RT_MOD)
            rv_param_format(PID_T(ui.cur_route, TP_EDST1), r->arg, o, 12);
        else if (r->type == RT_TAPE) {
            if (v >= 16)
                sx_cat(o, "CATCH");
            else {
                sx_cat(o, "POS ");
                sx_int(o + 4, v + 1);
            }
        } else if (r->type == RT_TRIG) {
            if (!v)
                sx_cat(o, "NOW");
            else {
                sx_int(o, v);
                sx_cat(o, " STEP");
            }
        } else
            sx_cat(o, "-");
        return;
    }
    if (k.kind == K_ROUTE && k.id == RF_CHANCE) {
        if (rv.infl[ui.track][ui.cur_route].type != RT_TRIG)
            sx_cat(o, "-");
        else {
            sx_int(o, v);
            sx_cat(o, "%");
        }
        return;
    }
    if (k.kind == K_MAT && k.id == MF_TRACK) {
        if (v >= RV_TRACKS)
            sx_cat(o, "GLOBAL");
        else {
            sx_cat(o, "T");
            sx_int(o + 1, v + 1);
        }
        return;
    }
    if (k.kind == K_MAT && k.id == MF_PARAM) {
        const rv_mslot *m = &rv.mat[ui.cur_mat];
        char b[20];
        const char *lbl = rv_pid_label(m->dst, b, 20);
        int i = 0;
        while (*lbl && *lbl != ' ' && m->dst < RV_GPID)
            lbl++;
        if (*lbl == ' ')
            lbl++;
        for (i = 0; lbl[i] && i < 11; i++)
            o[i] = lbl[i];
        o[i] = 0;
        return;
    }
    if (k.kind == K_MACRO && k.id == XF_DEST) {
        const rv_mdest *d = &rv.macro[ui.cur_macro / 8].d[ui.cur_macro % 8];
        if (d->dst >= RV_NPID)
            sx_cat(o, "-");
        else {
            char b[20];
            int i;
            rv_pid_label(d->dst, b, 20);
            for (i = 0; b[i] && i < 11; i++)
                o[i] = b[i];
            o[i] = 0;
        }
        return;
    }
    if (in.fmt == PF_ENUM && in.names) {
        sx_cat(o, in.names[v - in.min]);
        return;
    }
    if (in.fmt == PF_ONOFF) {
        sx_cat(o, v ? "ON" : "OFF");
        return;
    }
    if (in.fmt == PF_BI && v > 0) {
        o[0] = '+';
        sx_int(o + 1, v);
        return;
    }
    sx_int(o, v);
}

/* is a lock in play for this knob on the playing step? (the value shows amber) */
static int kb_locked(kb_t k)
{
    int si = rv_live.step[ui.track][0], j;
    const rv_step *s;
    if (k.kind != K_TP || si < 0)
        return 0;
    s = &rv.pat[ui.track][rv.pat_cur[ui.track] % RV_PATTERNS].s[si];
    for (j = 0; j < 2; j++)
        if (s->lp[j] == k.id + 1)
            return 1;
    return 0;
}

/* write a lock on a step */
static void step_lock(int si, int p, int v)
{
    rv_step *s = step_ptr(si);
    const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
    int k, slot = -1;
    for (k = 0; k < 2; k++)
        if (s->lp[k] == p + 1)
            slot = k;
    if (slot < 0)
        slot = !s->lp[0] ? 0 : !s->lp[1] ? 1 : 1;
    s->lp[slot] = (uint8_t)(p + 1);
    s->lv[slot] = (uint8_t)clampi(v - pi->min, 0, 255);
    s->flags |= ST_ON;
}

static int step_lock_value(int si, int p, int *v)
{
    const rv_step *s = step_ptr(si);
    int k;
    for (k = 0; k < 2; k++)
        if (s->lp[k] == p + 1) {
            *v = rv_pinfo_track(ui.track, p)->min + s->lv[k];
            return 1;
        }
    return 0;
}

/* --------------------------------------------------------- input --- */
static int accel(int d, int i)
{
    static uint32_t last[NE];
    uint32_t now = plat_ms(), dt = now - last[i];
    int a = d < 0 ? -d : d;
    last[i] = now;
    if (a >= 3 || dt < 30)
        return d * 4;
    if (dt < 70)
        return d * 2;
    return d;
}

static void knob_turn(int i, int d)
{
    const page_t *pg = cur_page();
    kb_t k = pg->k[i];
    int held = held_step();
    uint32_t btn = plat_buttons();
    if (btn & (1u << B_LFO)) {                    /* LFO held: the macros */
        rv.gp[GP_MACRO1 + i] = (int16_t)clampi(rv.gp[GP_MACRO1 + i] + d, 0, 127);
        ui.layer_used[B_LFO] = 1;
        ui.turned_ms[i] = plat_ms();
        return;
    }
    if (btn & (1u << B_ENV) && held >= 0 && i == 0) {   /* ENV held with a step: its library shape */
        rv_step *s = step_ptr(held);
        s->env = (uint8_t)clampi(s->env + d, 0, 255);
        ui.layer_used[B_ENV] = 1;
        ui.step_turned = 1;
        return;
    }
    if (k.kind == K_NONE)
        return;
    if (held >= 0 && k.kind == K_TP && cur_page()->sec != SEC_SEQ) {
        /* a step held (SEQ held, or on the SEQ pages): turning a track knob writes a lock */
        int v;
        if (!step_lock_value(held, k.id, &v))
            v = rv.tp[ui.track][k.id];
        step_lock(held, k.id, clampi(v + d, kb_info(k).min, kb_info(k).max));
        ui.step_turned = 1;
        if (btn & (1u << B_SEQ))
            ui.layer_used[B_SEQ] = 1;
        ui.turned_ms[i] = plat_ms();
        return;
    }
    if (k.kind == K_STEP && held >= 0)
        ui.step_turned = 1;
    kb_set(k, kb_get(k) + d);
    ui.turned_ms[i] = plat_ms();
}

static void preset_turn(int d)
{
    const page_t *pg = cur_page();
    int t = ui.track;
    switch (pg->pr) {
    case PR_SOUND: {
        int cur = ui.preset_idx[t] >= 0 ? ui.preset_idx[t] : (d > 0 ? -1 : 0), i;
        int n = d > 0 ? d : -d;
        for (i = 0; i < n; i++)
            cur = rv_preset_step(t, cur, d > 0 ? 1 : -1);
        rv_preset_apply(t, cur);
        ui.preset_idx[t] = (int16_t)cur;
        msg(rv_preset_name(cur));
        break;
    }
    case PR_ROUTE: ui.cur_route = (uint8_t)clampi(ui.cur_route + d, 0, 7); break;
    case PR_MAT: ui.cur_mat = (uint8_t)clampi(ui.cur_mat + d, 0, RV_MATRIX - 1); break;
    case PR_MACRO: ui.cur_macro = (uint8_t)clampi(ui.cur_macro + d, 0, RV_MACROS * RV_MACRO_DESTS - 1); break;
    case PR_ROW: ui.cur_row = (uint8_t)clampi(ui.cur_row + d, 0, 3); break;
    case PR_PATTERN: {
        int p = clampi(rv.pat_cur[t] + d, 0, RV_PATTERNS - 1);
        rv_pattern_launch(t, p);
        if (!rv_live.playing)
            rv.pat_cur[t] = (uint8_t)p;
        msg(p == 0 ? "PATTERN A" : "PATTERN B");
        break;
    }
    case PR_SCENE: ui.cur_scene = (uint8_t)clampi(ui.cur_scene + d, 0, RV_SCENES - 1); break;
    case PR_ARR: ui.cur_arr = (uint8_t)clampi(ui.cur_arr + d, 0, (rv.arr_len ? rv.arr_len : 1) - 1); break;
    case PR_ARC: ui.cur_arc = (uint8_t)clampi(ui.cur_arc + d, 0, 15); break;
    case PR_LIB: ui.cur_lib = (uint16_t)clampi(ui.cur_lib + d, 0, RV_LIB - 1); break;
    case PR_SLOT: ui.cur_slot = (uint8_t)clampi(ui.cur_slot + d, 0, PLAT_SLOTS - 1); ui.save_confirm = 0; break;
    case PR_STEPCUR: ui.step_cur = (int16_t)clampi(ui.step_cur + d, 0, clampi(rv.tp[t][TP_LEN], 1, 64) - 1); break;
    case PR_CZSTEP: ui.cz_step = (uint8_t)clampi(ui.cz_step + d, 0, 7); break;
    default: break;
    }
}

static void slots_refresh(void)
{
    int i;
    for (i = 0; i < PLAT_SLOTS; i++)
        ui.slot_used[i] = (uint8_t)rv_slot_used(i, ui.slot_names[i]);
}

static void save_do(void)
{
    char b[24];
    b[0] = 0;
    switch (ui.save_action) {
    case 0:
        if (rv_save(ui.cur_slot) == 0) {
            sx_cat(b, "SAVED TO ");
            sx_int(b + 9, ui.cur_slot + 1);
            msg(b);
        } else
            msg("SAVE FAILED");
        break;
    case 1:
        rv_transport(0);
        if (rv_load(ui.cur_slot) == 0) {
            sx_cat(b, "LOADED ");
            sx_int(b + 7, ui.cur_slot + 1);
            msg(b);
        } else
            msg("SLOT EMPTY");
        break;
    case 2:
    case 3:
    case 4:
        rv_transport(0);
        rv_demo(ui.save_action == 4 ? 3 : ui.save_action - 2);
        msg(ui.save_action == 2 ? "DEMO: RAVEL" : ui.save_action == 3 ? "DEMO: DUST" : "DEMO: GRAPH");
        break;
    default:
        rv_transport(0);
        rv_demo(2);
        msg("NEW PROJECT");
        break;
    }
    slots_refresh();
    ui.save_confirm = 0;
}

static void section_open(int s)
{
    if (ui.sec == s) {                        /* tapping the section's button again: the next page */
        uint8_t list[16];
        int n = sec_page_list(s, list);
        ui.page[s] = (uint8_t)(n ? (ui.page[s] + 1) % n : 0);
    } else {
        ui.sec = (uint8_t)s;
    }
    if (s == SEC_SAVE)
        slots_refresh();
    if (s == SEC_TAPE && rv.tp[ui.track][TP_KIND] != KIND_TAPE) {   /* TAPE plays a tape track */
        int t;
        for (t = 0; t < RV_TRACKS; t++)
            if (rv.tp[t][TP_KIND] == KIND_TAPE) {
                char b[24] = "T";
                b[1] = (char)('1' + t);
                b[2] = 0;
                sx_cat(b, ": THE TAPE TRACK");
                ui.track = (uint8_t)t;
                msg(b);
                break;
            }
    }
}

static int tape_track(void)
{
    int t;
    if (rv.tp[ui.track][TP_KIND] == KIND_TAPE)
        return ui.track;
    for (t = 0; t < RV_TRACKS; t++)
        if (rv.tp[t][TP_KIND] == KIND_TAPE)
            return t;
    return -1;
}

/* a key went down (on) or up */
static void key_event(int k, int on)
{
    uint32_t btn = plat_buttons();
    int wi = white_index(k), bi = black_index(k), t = ui.track;
    int note = rv_fold_key(53 + ui.octave * 12, k);   /* chromatic, or folded into the scale */
    /* --- a key let go: whatever it started ends, whatever has changed since (the track, the page,
     * a layer held) --- */
    if (!on) {
        if (ui.key_trk[k]) {                    /* its note, on the track it went to */
            rv_live_note(ui.key_trk[k] - 1, ui.key_note[k], 0);
            ui.key_trk[k] = 0;
            return;
        }
        if (wi >= 0) {
            ui.mlr_held &= ~(1u << wi);
            if (ui.step_key_held & (1u << wi)) {   /* a step key: a tap toggles the step */
                ui.step_key_held &= ~(1u << wi);
                if (!ui.step_turned) {
                    rv_step *s = step_ptr(ui.step_page * 16 + wi);
                    s->flags ^= ST_ON;
                    if ((s->flags & ST_ON) && s->vel == 0)
                        s->vel = 100;
                }
                return;
            }
        }
    }
    /* --- layers --- */
    if (btn & (1u << B_FX)) {                 /* punch-in effects on the white keys */
        if (wi >= 0 && wi < 8)
            rv_punch(wi, on);
        ui.layer_used[B_FX] = 1;
        return;
    }
    if (btn & (1u << B_SCL)) {
        if (on) {
            rv.gp[GP_ROOT] = (int16_t)((53 + k) % 12);
            msg("ROOT SET");
        }
        ui.layer_used[B_SCL] = 1;
        return;
    }
    if (btn & (1u << B_GLO)) {
        if (on && wi >= 0) {
            if (wi < 8)
                rv.mute ^= (uint8_t)(1u << wi);
            else
                rv.solo ^= (uint8_t)(1u << (wi - 8));
        }
        if (on && bi == 0) {
            rv_tap_tempo();
            msg("TAP");
        }
        ui.layer_used[B_GLO] = 1;
        return;
    }
    if (btn & (1u << B_SAVE)) {
        if (on && wi >= 0) {
            char b[20] = "SCENE ";
            rv_scene_store(wi);
            sx_int(b + 6, wi + 1);
            sx_cat(b, " STORED");
            msg(b);
        }
        ui.layer_used[B_SAVE] = 1;
        return;
    }
    if (btn & (1u << B_REC)) {
        ui.layer_used[B_REC] = 1;
        return;
    }
    if ((btn & (1u << B_ARP)) && ui.sec != SEC_TAPE) {
        int tt = tape_track();
        ui.layer_used[B_ARP] = 1;
        if (tt >= 0 && on && wi >= 0)
            rv_tape_jump(tt, wi, -1);
        return;
    }
    /* --- steps: the SEQ section, or SEQ held on any page --- */
    if (ui.sec == SEC_SEQ || (btn & (1u << B_SEQ))) {
        if (btn & (1u << B_SEQ))
            ui.layer_used[B_SEQ] = 1;
        if (wi >= 0) {
            int si = ui.step_page * 16 + wi;
            if (on) {
                ui.step_key_held |= 1u << wi;
                ui.step_turned = 0;
                ui.step_cur = (int16_t)si;
                ui.btn_down_ms[0] = plat_ms();
            }
            return;                             /* (let go: above) */
        }
        if (bi >= 0 && on) {
            if (bi < RV_PATTERNS) {
                rv_pattern_launch(t, bi);
                if (!rv_live.playing)
                    rv.pat_cur[t] = (uint8_t)bi;
                msg(bi == 0 ? "PATTERN A" : "PATTERN B");
            } else if (bi == 10) {
                rv_pattern_clear(t);
                msg("PATTERN CLEARED");
            }
        }
        return;
    }
    /* --- the tape page: positions, rows and moves --- */
    if (ui.sec == SEC_TAPE && rv.tp[t][TP_KIND] == KIND_TAPE && rv.tp[t][TP_S0] == TAPE_MLR) {
        if (wi >= 0) {
            if (on) {
                int other = -1, i;
                for (i = 0; i < 16; i++)
                    if ((ui.mlr_held & (1u << i)) && i != wi)
                        other = i;
                ui.mlr_held |= 1u << wi;
                if (other >= 0)
                    rv_tape_jump(t, other < wi ? other : wi, other < wi ? wi : other);   /* two keys: loop the span */
                else
                    rv_tape_jump(t, wi, -1);
            }
            return;                             /* (let go: above) */
        }
        if (bi >= 0 && on) {
            if (bi < 4) {
                rv_tape_row(t, bi);
                msg(bi == 0 ? "ROW 1" : bi == 1 ? "ROW 2" : bi == 2 ? "ROW 3" : "ROW 4");
            } else if (bi < 8) {
                rv_tape_rate(t, bi - 4);
                msg(bi == 4 ? "REVERSE" : bi == 5 ? "HALF SPEED" : bi == 6 ? "DOUBLE SPEED" : "HOLD / RELEASE");
            } else if (bi == 8) {
                rv_catch();
                msg(rv_tape_frozen() ? "LISTENING" : "CAUGHT");
            } else if (bi == 9) {
                rv_resample();
                msg("RESAMPLE ON THE BAR");
            }
        }
        return;
    }
    if (ui.sec == SEC_TAPE && bi >= 8 && on) {
        if (bi == 8) {
            rv_catch();
            msg(rv_tape_frozen() ? "LISTENING" : "CAUGHT");
        } else if (bi == 9) {
            rv_resample();
            msg("RESAMPLE ON THE BAR");
        }
        return;
    }
    /* --- scenes on the SONG page --- */
    if (ui.sec == SEC_SONG && cur_page()->pic == PIC_SCENES) {
        if (on && wi >= 0) {
            char b[20] = "SCENE ";
            ui.cur_scene = (uint8_t)wi;
            rv_scene_launch(wi);
            sx_int(b + 6, wi + 1);
            msg(b);
        }
        if (on && bi >= 0 && bi < RV_PATTERNS)
            rv_pattern_launch(t, bi);
        return;
    }
    /* --- otherwise the keys play the track --- */
    if (on) {
        ui.key_note[k] = (uint8_t)clampi(note, 0, 127);
        ui.key_trk[k] = (uint8_t)(t + 1);
        if (ui.sec == SEC_ARTIC && cur_page()->pic == PIC_LIB)
            rv_note(t, ui.key_note[k], 100);   /* (auditions with the track's own articulation) */
        else
            rv_live_note(t, ui.key_note[k], 100);
    }
}

static void buttons(uint32_t now_btn)
{
    uint32_t down = now_btn & ~ui.prev_btn, up = ui.prev_btn & ~now_btn;
    uint32_t ms = plat_ms();
    int b, s;
    for (b = 0; b < NB; b++) {
        if (down & (1u << b)) {
            ui.btn_down_ms[b] = ms;
            ui.layer_used[b] = 0;
        }
    }
    /* REC held: OCT- catches, OCT+ resamples */
    if ((now_btn & (1u << B_REC)) && (down & (1u << B_OCTDN))) {
        rv_catch();
        msg(rv_tape_frozen() ? "LISTENING" : "CAUGHT");
        ui.layer_used[B_REC] = 1;
        down &= ~(1u << B_OCTDN);
    }
    if ((now_btn & (1u << B_REC)) && (down & (1u << B_OCTUP))) {
        rv_resample();
        msg("RESAMPLE ON THE BAR");
        ui.layer_used[B_REC] = 1;
        down &= ~(1u << B_OCTUP);
    }
    if (down & (1u << B_PLAY)) {
        rv_transport(!rv_live.playing);
    }
    if (down & (1u << B_OCTDN)) {
        if (ui.sec == SEC_SEQ || (now_btn & (1u << B_SEQ)))
            ui.step_page = (uint8_t)(ui.step_page ? ui.step_page - 1 : 0);
        else if (ui.octave > -3)
            ui.octave--;
    }
    if (down & (1u << B_OCTUP)) {
        if (ui.sec == SEC_SEQ || (now_btn & (1u << B_SEQ))) {
            int pages_n = (clampi(rv.tp[ui.track][TP_LEN], 1, 64) + 15) / 16;
            if (ui.step_page + 1 < pages_n)
                ui.step_page++;
        } else if (ui.octave < 3)
            ui.octave++;
    }
    /* section buttons act on release, when they were tapped and not used as a layer */
    for (s = 0; s < NSEC; s++) {
        b = sec_button[s];
        if ((up & (1u << b)) && !ui.layer_used[b] && ms - ui.btn_down_ms[b] < 600) {
            if (s == SEC_SAVE && ui.sec == SEC_SAVE) {
                if (ui.save_confirm)
                    save_do();
                else
                    ui.save_confirm = 1;
            } else {
                section_open(s);
            }
        }
    }
    if ((up & (1u << B_REC)) && !ui.layer_used[B_REC]) {
        ui.armed = !ui.armed;
        rv_record_arm(ui.track, ui.armed);
        msg(ui.armed ? "RECORDING ARMED" : "RECORDING OFF");
    }
    if ((up & (1u << B_FX)))
        for (s = 0; s < 8; s++)
            rv_punch(s, 0);
    ui.prev_btn = now_btn;
}

void ui_input_only(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), ch;
    int i, d;
    buttons(btn);
    ch = keys ^ ui.prev_keys;
    for (i = 0; i < NKEYS; i++)
        if (ch & (1u << i))
            key_event(i, (keys >> i) & 1);
    ui.prev_keys = keys;
    d = plat_enc(EN_SELECT);
    if (d) {
        uint8_t list[16];
        int n = sec_page_list(ui.sec, list);
        if (n)
            ui.page[ui.sec] = (uint8_t)clampi(ui.page[ui.sec] + d, 0, n - 1);
    }
    d = plat_enc(EN_ALGO);
    if (d) {
        int nt = clampi(ui.track + d, 0, RV_TRACKS - 1);
        if (nt != ui.track && ui.armed) {
            rv_record_arm(ui.track, 0);
            rv_record_arm(nt, 1);
        }
        ui.track = (uint8_t)nt;
    }
    d = plat_enc(EN_PRESET);
    if (d)
        preset_turn(d);
    for (i = 0; i < 4; i++) {
        d = plat_enc(EN_K1 + i);
        if (d)
            knob_turn(i, accel(d, EN_K1 + i));
    }
}

/* ------------------------------------------------------------ drawing --- */
#define PIC_Y 76
#define PIC_H 138
#define FOOT_Y 216

static uint16_t kind_col(int t)
{
    int k = rv.tp[t][TP_KIND];
    return k == KIND_SYNTH ? C_SYNTH : k == KIND_STRIKE ? C_STRIKE : C_TAPE;
}

static const char *sound_name(int t)
{
    int k = rv.tp[t][TP_KIND];
    if (k == KIND_SYNTH)
        return rv_engine_names[clampi(rv.tp[t][TP_ENGINE], 0, ENG_N - 1)];
    if (k == KIND_STRIKE) {
        const rv_pinfo *e = rv_pinfo_track(t, TP_S0);
        return e->names ? e->names[clampi(rv.tp[t][TP_S0], 0, e->max)] : "STRIKE";
    }
    {
        const rv_pinfo *m = rv_pinfo_track(t, TP_S0);
        return m->names ? m->names[clampi(rv.tp[t][TP_S0], 0, m->max)] : "TAPE";
    }
}

static void draw_top(void)
{
    char b[24];
    const page_t *pg = cur_page();
    int t = ui.track, x;
    g_fill(0, 0, 240, 22, C_PANEL);
    g_fill(3, 3, 24, 16, kind_col(t));
    b[0] = 'T';
    b[1] = (char)('1' + t);
    b[2] = 0;
    g_text_c(15, 3, &FONT_B, b, C_BG);
    if (ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0) {
        g_text(32, 3, &FONT_B, ui.msg, C_WHITE);
        return;
    }
    x = g_text(32, 5, &FONT_XS, rv_kind_names[clampi(rv.tp[t][TP_KIND], 0, 2)], kind_col(t));
    x = g_text(x + 4, 5, &FONT_XS, sound_name(t), C_MID);
    (void)x;
    g_text_r(236, 3, &FONT_B, pg->title, C_INK);
}

static void draw_knobs(void)
{
    const page_t *pg = cur_page();
    int i;
    uint32_t now = plat_ms();
    uint32_t btn = plat_buttons();
    int held = held_step();
    g_fill(0, 22, 240, 52, C_BG);
    for (i = 0; i < 4; i++) {
        int x = i * 60, cx = x + 30, v, frac_w;
        kb_t k = pg->k[i];
        kinfo_t in;
        char val[16];
        uint16_t vc = C_INK, lc = C_DIM;
        if (btn & (1u << B_LFO)) {            /* macros on the knobs */
            char nm[10] = "MACRO ";
            nm[6] = (char)('1' + i);
            nm[7] = 0;
            v = rv.gp[GP_MACRO1 + i];
            g_text_c(cx, 25, &FONT_T, nm, C_TEAL);
            sx_int(val, v);
            g_text_c(cx, 38, &FONT_B, val, C_WHITE);
            g_fill(x + 6, 62, 48, 3, C_LINE);
            g_fill(x + 6, 62, 48 * v / 127, 3, C_TEAL);
            continue;
        }
        in = kb_info(k);
        if (!in.name || k.kind == K_NONE) {
            continue;
        }
        if (k.kind == K_TP && !rv_tp_applies(ui.track, k.id)) {   /* does nothing as the track is set: greyed */
            v = kb_get(k);
            kb_format(k, v, val);
            g_text_c(cx, 25, &FONT_T, in.name, C_LINE);
            g_text_c(cx, 38, g_text_w(&FONT_B, val) > 56 ? &FONT_XS : &FONT_B, val, C_LINE);
            g_fill(x + 6, 62, 48, 3, C_LINE);
            if (i)
                g_vline(x, 26, 40, C_LINE);
            continue;
        }
        v = kb_get(k);
        if (held >= 0 && k.kind == K_TP) {
            int lv;
            if (step_lock_value(held, k.id, &lv)) {
                v = lv;
                vc = C_AMBER;
            } else {
                vc = C_DIM;
            }
            lc = C_AMBER;
        } else if (kb_locked(k)) {
            vc = C_AMBER;
        }
        if (now - ui.turned_ms[i] < 600)
            lc = C_TEAL;
        g_text_c(cx, 25, &FONT_T, in.name, lc);
        kb_format(k, v, val);
        g_text_c(cx, 38, g_text_w(&FONT_B, val) > 56 ? &FONT_XS : &FONT_B, val, vc);
        /* position bar: bipolar from the middle */
        g_fill(x + 6, 62, 48, 3, C_LINE);
        if (in.max > in.min) {
            if (in.min < 0 && in.max > 0) {
                int mid = x + 30, w = 24 * v / (v < 0 ? -in.min : in.max);
                if (w >= 0)
                    g_fill(mid, 62, w, 3, vc == C_DIM ? C_LINE : C_TEAL);
                else
                    g_fill(mid + w, 62, -w, 3, vc == C_DIM ? C_LINE : C_TEAL);
                g_vline(mid, 60, 7, C_MID);
            } else {
                frac_w = 48 * (v - in.min) / (in.max - in.min);
                g_fill(x + 6, 62, frac_w, 3, vc == C_AMBER ? C_AMBER : C_TEAL);
            }
        }
        if (i)
            g_vline(x, 26, 40, C_LINE);
    }
    g_hline(0, 73, 240, C_LINE);
}

static void draw_footer(void)
{
    uint8_t list[16];
    int n = sec_page_list(ui.sec, list), i, x;
    uint32_t btn = plat_buttons();
    const char *hint = "PRESETS: SOUNDS";
    const page_t *pg = cur_page();
    g_fill(0, FOOT_Y - 2, 240, 26, C_BG);
    g_hline(0, FOOT_Y - 2, 240, C_LINE);
    switch (pg->pr) {
    case PR_ROUTE: hint = "PRESETS: TARGET  ALGO: SOURCE"; break;
    case PR_MAT: hint = "PRESETS: SLOT"; break;
    case PR_MACRO: hint = "PRESETS: DEST  HOLD LFO: MACROS"; break;
    case PR_ROW: hint = "REC+OCT- CATCH  REC+OCT+ RESAMPLE"; break;
    case PR_PATTERN: hint = "KEYS: STEPS  OCT: PAGE"; break;
    case PR_SCENE: hint = "KEYS: LAUNCH  SAVE+KEY: STORE"; break;
    case PR_ARR: hint = "PRESETS: ROW"; break;
    case PR_ARC: hint = "PRESETS: BAR"; break;
    case PR_LIB: hint = "PRESETS: SHAPE"; break;
    case PR_SLOT: hint = ui.save_confirm ? "PRESS SAVE AGAIN TO CONFIRM" : "PRESETS: SLOT  SAVE: DO IT"; break;
    case PR_STEPCUR: hint = "PRESETS: STEP"; break;
    case PR_CZSTEP: hint = "PRESETS: STEP 1-8"; break;
    default:
        if (ui.sec == SEC_TAPE)
            hint = rv.tp[ui.track][TP_KIND] != KIND_TAPE ? "BLACK 9 CATCH  10 RESAMPLE"
                   : rv.tp[ui.track][TP_S0] == TAPE_MLR ? "TAP: JUMP  HOLD 2: LOOP  BLACK 1-4: ROW"
                   : "KEYS PLAY THE TAPE TRACK";
        else if (ui.sec == SEC_HOME && pg->pic == PIC_TRACKS)
            hint = "ALGO: TRACK  PRESETS: SOUND";
        else if (pg->pic == PIC_MOTION)
            hint = "SHAPE MOVES THE HEAD'S SPEED";
        else if (ui.sec == SEC_BUS)
            hint = "SEND: THIS TRACK INTO THE BUS";
        break;
    }
    if (btn & (1u << B_SEQ))
        hint = "STEP + KNOB = LOCK";
    else if (btn & (1u << B_LFO))
        hint = "KNOBS: MACRO 1-4";
    else if (btn & (1u << B_FX))
        hint = "LOOP REV STUT FRZ LP HP THRW FILL";
    else if (btn & (1u << B_GLO))
        hint = "1-8 MUTE  9-16 SOLO  BLK1 TAP";
    else if (btn & (1u << B_SAVE))
        hint = "WHITE KEY: STORE THE SCENE";
    else if (btn & (1u << B_REC))
        hint = "OCT- CATCH   OCT+ RESAMPLE";
    else if (btn & (1u << B_ARP))
        hint = "KEYS JUMP THE TAPE TRACK";
    else if (btn & (1u << B_SCL))
        hint = "KEY: ROOT";
    g_text(4, FOOT_Y + 5, &FONT_T, hint, C_DIM);
    x = 237 - n * 5;
    for (i = 0; i < n; i++)
        g_fill(x + i * 5, FOOT_Y + 9, 3, 3, i == ui.page[ui.sec] ? C_TEAL : C_LINE);
    /* transport */
    if (rv_live.playing) {
        g_fill(x - 14, FOOT_Y + 7, 3, 8, C_TEAL);
        g_fill(x - 10, FOOT_Y + 9, 3, 4, rv_live.beat == 0 ? C_WHITE : C_TEAL_D);
    }
    if (ui.armed)
        g_disc(x - 22, FOOT_Y + 11, 3, C_RED);
}

/* --- pictures --- */
static void pic_scope(int t, int y0, int h, uint16_t c)
{
    int i, prev = y0 + h / 2;
    float g = 1.0f / fm_maxf(0.05f, rv_live.meter[t]);
    if (g > 6.0f)
        g = 6.0f;
    for (i = 0; i < 64; i++) {
        float v = rv_live.scope[t][(rv_live.scope_w + i) & 63] * g;
        int y = y0 + h / 2 - (int)(clampf(v, -1.0f, 1.0f) * (float)(h / 2 - 2));
        int x = 8 + i * 224 / 63;
        if (i)
            g_line(8 + (i - 1) * 224 / 63, prev, x, y, c);
        prev = y;
    }
}

static void pic_tracks(void)
{
    int t;
    for (t = 0; t < RV_TRACKS; t++) {
        int x = t * 30, h, cx = x + 15;
        char b[4];
        uint16_t kc = kind_col(t);
        uint8_t muted = (rv.mute >> t) & 1u;
        if (t == ui.track)
            g_fill(x + 1, PIC_Y, 28, 124, C_PANEL);
        b[0] = (char)('1' + t);
        b[1] = 0;
        g_text_c(cx, PIC_Y + 2, &FONT_B, b, t == ui.track ? C_WHITE : kc);
        g_text_c(cx, PIC_Y + 20, &FONT_T, rv.tp[t][TP_KIND] == KIND_SYNTH ? "SYN" : rv.tp[t][TP_KIND] == KIND_STRIKE ? "STK" : "TAP", muted ? C_DIM : kc);
        /* level meter */
        g_fill(x + 9, PIC_Y + 36, 5, 60, C_LINE);
        h = (int)(clampf(fm_sqrtf(rv_live.meter[t]), 0.0f, 1.0f) * 60.0f);
        g_fill(x + 9, PIC_Y + 96 - h, 5, h, muted ? C_DIM : C_TEAL);
        /* articulation */
        g_fill(x + 16, PIC_Y + 36, 3, 60, C_LINE);
        h = (int)(clampf(rv_live.env[t], 0.0f, 1.0f) * 60.0f);
        g_fill(x + 16, PIC_Y + 96 - h, 3, h, C_AMBER);
        /* step */
        if (rv_live.step[t][0] >= 0) {
            char s[4];
            sx_int(s, rv_live.step[t][0] + 1);
            g_text_c(cx, PIC_Y + 100, &FONT_T, s, C_MID);
        }
        if (muted)
            g_text_c(cx, PIC_Y + 112, &FONT_T, "MUTE", C_RED);
        else if ((rv.solo >> t) & 1u)
            g_text_c(cx, PIC_Y + 112, &FONT_T, "SOLO", C_TRIG);
    }
    /* influence: a line under the columns from each source to each target */
    {
        int a, b2, n = 0;
        for (a = 0; a < RV_TRACKS; a++)
            for (b2 = 0; b2 < RV_TRACKS; b2++) {
                const rv_route *r = &rv.infl[a][b2];
                uint16_t c;
                int y;
                if (!r->type)
                    continue;
                c = r->type == RT_MOD ? C_TEAL : r->type == RT_TRIG ? C_TRIG : r->type == RT_EXCITE ? C_EXC : r->type == RT_TAPE ? C_TAPE : r->type == RT_CLOCK ? C_CLOCK : C_RING;
                if (rv_live.route_fire[a][b2] > 40)
                    c = C_WHITE;
                y = PIC_Y + 126 + (n % 3) * 4;
                g_line(a * 30 + 15, y, b2 * 30 + 15, y, c);
                g_disc(b2 * 30 + 15, y, 2, c);
                n++;
            }
    }
}

static uint16_t route_col(int type)
{
    switch (type) {
    case RT_MOD: return C_TEAL;
    case RT_TRIG: return C_TRIG;
    case RT_EXCITE: return C_EXC;
    case RT_TAPE: return C_TAPE;
    case RT_CLOCK: return C_CLOCK;
    case RT_RING: return C_RING;
    default: return C_LINE;
    }
}

static void pic_infl(void)
{
    int a, b, x0 = 30, y0 = PIC_Y + 12, cs = 13;
    char s[4];
    for (b = 0; b < RV_TRACKS; b++) {
        s[0] = (char)('1' + b);
        s[1] = 0;
        g_text_c(x0 + b * cs + 7, PIC_Y + 1, &FONT_T, s, b == ui.cur_route ? C_WHITE : C_DIM);
    }
    for (a = 0; a < RV_TRACKS; a++) {
        s[0] = 'T';
        s[1] = (char)('1' + a);
        s[2] = 0;
        g_text(6, y0 + a * cs + 1, &FONT_T, s, a == ui.track ? C_WHITE : C_DIM);
        for (b = 0; b < RV_TRACKS; b++) {
            const rv_route *r = &rv.infl[a][b];
            int x = x0 + b * cs, y = y0 + a * cs;
            uint16_t c = route_col(r->type);
            if (r->type) {
                float lv = 0.35f + 0.65f * (float)(r->amt < 0 ? -r->amt : r->amt) / 64.0f;
                if (r->type == RT_TRIG || r->type == RT_TAPE || r->type == RT_CLOCK)
                    lv = 1.0f;
                g_fill(x + 1, y + 1, cs - 2, cs - 2, g_mix(C_BG, c, lv));
                if (rv_live.route_fire[a][b] > 30)
                    g_frame(x, y, cs, cs, C_WHITE);
            } else {
                g_fill(x + 5, y + 5, 2, 2, C_LINE);
            }
            if (a == ui.track && b == ui.cur_route)
                g_frame(x - 1, y - 1, cs + 2, cs + 2, C_WHITE);
        }
    }
    /* legend */
    for (a = 1; a < RT_N; a++) {
        int y = PIC_Y + 10 + (a - 1) * 16;
        g_fill(150, y + 2, 8, 8, route_col(a));
        g_text(162, y, &FONT_T, rv_route_names[a], C_MID);
    }
    {
        static const char *const what[] = {"", "MOVES A KNOB OF", "PLAYS", "RINGS THE BODY OF", "JUMPS THE TAPE OF", "CLOCKS", "RING MODULATES"};
        const rv_route *r = &rv.infl[ui.track][ui.cur_route];
        char ln[48] = "";
        if (r->type) {
            char d[4] = "T";
            d[1] = (char)('1' + ui.track);
            d[2] = 0;
            sx_cat(ln, d);
            sx_cat(ln, " ");
            sx_cat(ln, what[r->type]);
            d[1] = (char)('1' + ui.cur_route);
            sx_cat(ln, " ");
            sx_cat(ln, d);
        } else {
            sx_cat(ln, "EMPTY CELL: TURN K1 TO MAKE A ROUTE");
        }
        g_text(6, PIC_Y + 124, &FONT_T, ln, C_MID);
    }
}

/* the CZ page's line: which line a page edits (from its title), and its two cycles at the DCW
 * envelope's highest level, drawn with the engine's own waveform function */
static int cz_page_line(void) { const char *tt = cur_page()->title; while (*tt && *tt != ' ') tt++; return tt[0] && tt[1] == '2'; }

static void pic_czwave(void)
{
    int t = ui.track, l = cz_page_line(), i, prev = 0, mx = 0, wa, wb, win;
    const rv_czline *pl = &rv.cz[t].line[l];
    char b[40];
    for (i = 0; i < clampi(pl->env[CZE_DCW].end, 1, 8); i++)
        if (pl->env[CZE_DCW].level[i] > mx)
            mx = pl->env[CZE_DCW].level[i];
    wa = clampi(pl->wave1, 1, 8);
    wb = clampi(pl->wave2, 0, 8);
    if (wa >= 6 && wb >= 6)
        wb = 0;
    win = wa >= 6 ? wa - 5 : wb >= 6 ? wb - 5 : 0;
    g_hline(10, PIC_Y + 60, 220, C_LINE);
    for (i = 0; i <= 220; i++) {
        float s = cz_line_sample(cz_chip_wave(wa), wb ? cz_chip_wave(wb) : cz_chip_wave(wa), win, (float)i * (4096.0f / 221.0f),
                                 (float)((127 * mx / 99) << 3));
        int y = PIC_Y + 104 - (int)(s * 88.0f);
        if (i)
            g_line(10 + i - 1, prev, 10 + i, y, C_SYNTH);
        prev = y;
    }
    b[0] = 0;
    sx_cat(b, "DCW AT ITS PEAK ");
    sx_int(b + 16, mx);
    g_text(8, PIC_Y + 6, &FONT_T, b, C_DIM);
    if (wb)
        g_text_r(232, PIC_Y + 6, &FONT_T, "TWO WAVES: AN OCTAVE DOWN", C_DIM);
}

/* an 8-step envelope: the levels, SUS and END, the step being edited and where the voice is */
static void pic_czenv(void)
{
    int t = ui.track, l = cz_page_line(), which, i, x0 = 14, w = 26, prevx = 14, prevy, live;
    const char *tt = cur_page()->title;
    const rv_czenv *e;
    char b[40];
    which = tt[2] == 'O' ? CZE_DCO : tt[2] == 'W' ? CZE_DCW : CZE_DCA;
    e = &rv.cz[t].line[l].env[which];
    live = rv_live.cz_step[t][l * 3 + which];
    prevy = PIC_Y + 104;
    g_hline(x0, PIC_Y + 104, 8 * w, C_LINE);
    for (i = 0; i < 8; i++) {
        int x = x0 + (i + 1) * w, y = PIC_Y + 104 - e->level[i] * 88 / 99, on = i < e->end;
        uint16_t c = !on ? C_LINE : i == ui.cz_step ? C_WHITE : C_SYNTH;
        g_line(prevx, prevy, x, y, c);
        g_disc(x, y, i == ui.cz_step ? 3 : 2, c);
        if (i == live)
            g_disc(x, PIC_Y + 112, 2, C_AMBER);
        b[0] = (char)('1' + i);
        b[1] = 0;
        g_text_c(x, PIC_Y + 116, &FONT_T, b, on ? C_DIM : C_LINE);
        if (e->sus == i + 1) {
            g_vline(x, PIC_Y + 14, 90, C_TEAL);
            if (e->end != i + 1)
                g_text_c(x, PIC_Y + 4, &FONT_T, "SUS", C_TEAL);
        }
        if (e->end == i + 1)
            g_text_c(x, PIC_Y + 4, &FONT_T, e->sus == i + 1 ? "SUS+END" : "END", C_AMBER);
        prevx = x;
        prevy = y;
    }
    b[0] = 0;
    sx_cat(b, "STEP ");
    sx_int(b + 5, ui.cz_step + 1);
    if (which == CZE_DCO) {                            /* what the DCO level means */
        int lv = e->level[ui.cz_step & 7], n;
        n = sx_cat(b, lv <= 63 ? "  +" : "  +");
        if (lv <= 63) {
            n = sx_int(b + n, lv * 625 / 100);
            sx_cat(b, " CENTS");
        } else {
            sx_int(b + n, (lv - 60) * 2);
            sx_cat(b, " SEMITONES");
        }
    }
    g_text(8, PIC_Y + 126, &FONT_T, b, C_DIM);
}

static void pic_source(void)
{
    int t = ui.track, kind = rv.tp[t][TP_KIND];
    char b[40];
    b[0] = 0;
    if (kind == KIND_STRIKE) {
        const rv_pinfo *e = rv_pinfo_track(t, TP_S0), *bo = rv_pinfo_track(t, TP_S1);
        g_frame(14, PIC_Y + 8, 90, 24, C_STRIKE);
        g_text_c(59, PIC_Y + 12, &FONT_B, e->names[clampi(rv.tp[t][TP_S0], 0, e->max)], C_INK);
        g_line(106, PIC_Y + 20, 132, PIC_Y + 20, C_MID);
        g_line(128, PIC_Y + 16, 132, PIC_Y + 20, C_MID);
        g_line(128, PIC_Y + 24, 132, PIC_Y + 20, C_MID);
        g_frame(136, PIC_Y + 8, 90, 24, C_STRIKE);
        g_text_c(181, PIC_Y + 12, &FONT_B, bo->names[clampi(rv.tp[t][TP_S1], 0, bo->max)], C_INK);
        pic_scope(t, PIC_Y + 40, 90, C_STRIKE);
        if (rv.tp[t][TP_S0] == EXC_INPUT && rv_live.exc_in[t] < 0.0005f) {   /* EXT with nothing coming in */
            g_text_c(120, PIC_Y + 106, &FONT_T, "EXT: NOTHING IS EXCITING THE BODY", C_AMBER);
            g_text_c(120, PIC_Y + 118, &FONT_T, "ADD AN EXCITE ROUTE ON HOME > INFLUENCE", C_DIM);
        }
    } else if (kind == KIND_TAPE) {
        g_text_c(120, PIC_Y + 10, &FONT_B, "A TAPE TRACK: SEE ARP / TAPE", C_TAPE);
        pic_scope(t, PIC_Y + 34, 96, C_TAPE);
    } else {
        static const char *const desc[ENG_N] = {"SAW TO PULSE, SUPERSAW, SUB, TZFM", "CASIO CZ: 2 LINES, 8-STEP DCO DCW DCA",
                                                "4 OPERATORS, 12 ALGORITHMS", "STRING, BOW, BREATH, MEMBRANE",
                                                "VOWELS: FILTER BANK OR FOF", "PULSAR EMITTERS, COUPLED",
                                                "SID WAVES, ARP OFFSETS, DECOMPOSE", "SQ-80: 3 OSCS, 8-BIT WAVES, CEM FILTER"};
        int e = clampi(rv.tp[t][TP_ENGINE], 0, ENG_N - 1);
        g_text(8, PIC_Y + 6, &FONT_B, rv_engine_names[e], C_SYNTH);
        g_text(8, PIC_Y + 30, &FONT_T, desc[e], C_MID);
        pic_scope(t, PIC_Y + 44, 90, C_TEAL);
    }
    (void)b;
}

/* the response of the track's filter at f (approximate, for the picture) */
static float filt_mag(int type, float f, float fc, float res)
{
    float w = f / fc, q = 0.5f + res * res * 18.0f, re = 1.0f - w * w, im = w / q, m2 = re * re + im * im, m;
    switch (type) {
    case FT_SH101:
    case FT_SQ80:
    case FT_LADDER: {
        float q2 = 0.6f + res * 4.0f, im2 = w / q2;
        m = 1.0f / fm_sqrtf(m2 > 1e-6f ? m2 : 1e-6f);
        m *= 1.0f / fm_sqrtf(re * re + im2 * im2 + 1e-6f);
        return m;
    }
    case FT_HP: return w * w / fm_sqrtf(m2 + 1e-6f);
    case FT_BP: return (w / q) / fm_sqrtf(m2 + 1e-6f) * (1.0f + res);
    default: return 1.0f / fm_sqrtf(m2 + 1e-6f);
    }
}

static void pic_filter(void)
{
    int t = ui.track, i, prev = 0, type = rv.tp[t][TP_FTYPE];
    float fc = rv_cut_hz(rv_param_eff(t, TP_CUT)), res = rv_param_eff(t, TP_RES) / 127.0f, fx;
    char b[20];
    for (i = 0; i < 4; i++) {
        int x = 8 + i * 56;
        g_vline(x, PIC_Y + 6, 112, C_LINE);
    }
    for (i = 0; i <= 224; i += 2) {
        float f = 20.0f * fm_exp2f((float)i / 224.0f * 10.0f);
        float m = type >= FT_ZVOWEL ? zp_mag(type - FT_ZVOWEL, rv_param_eff(t, TP_CUT) / 127.0f, res, f)
                                    : filt_mag(type == FT_LPG ? FT_LP : type, f, fc, res);
        float db = 20.0f * fm_log2f(m + 1e-6f) * 0.30103f;
        int y = PIC_Y + 50 - (int)(db * 2.2f);
        if (y < PIC_Y + 4)
            y = PIC_Y + 4;
        if (y > PIC_Y + 118)
            y = PIC_Y + 118;
        if (i)
            g_line(8 + i - 2, prev, 8 + i, y, C_TEAL);
        prev = y;
    }
    fx = (float)rv.tp[t][TP_CUT] / 127.0f * 224.0f + 8.0f;     /* the knob; the curve moves with modulation */
    g_vline((int)fx, PIC_Y + 4, 114, C_AMBER_D);
    rv_param_format(PID_T(t, TP_CUT), rv.tp[t][TP_CUT], b, 20);
    g_text((int)fx + 4 > 190 ? (int)fx - 40 : (int)fx + 4, PIC_Y + 6, &FONT_T, b, C_AMBER);
    g_text(8, PIC_Y + 122, &FONT_T, "100     1K       10K", C_DIM);
}

static void pic_skin(void)
{
    int t = ui.track, i, prev = 0;
    float drive = rv_param_eff(t, TP_DRIVE) / 127.0f, fl = rv_param_eff(t, TP_FOLD) / 127.0f;
    int bits = (int)rv_param_eff(t, TP_BITS);
    g_hline(60, PIC_Y + 64, 120, C_LINE);
    g_vline(120, PIC_Y + 4, 120, C_LINE);
    for (i = 0; i <= 120; i += 2) {
        float x = (float)(i - 60) / 60.0f * 1.2f, y = x;
        int py;
        if (drive > 0.01f)
            y = sat(y * (1.0f + drive * 9.0f)) / (1.0f + drive * 1.5f);
        if (fl > 0.01f)
            y = fold(y, fl);
        if (bits > 0) {
            float q = fm_exp2f(15.0f - (float)bits * (14.0f / 127.0f));
            y = (float)(int32_t)(y * q) / q;
        }
        py = PIC_Y + 64 - (int)(clampf(y, -1.0f, 1.0f) * 56.0f);
        if (i)
            g_line(60 + i - 2, prev, 60 + i, py, C_TEAL);
        prev = py;
    }
    g_text(6, PIC_Y + 6, &FONT_T, "IN > OUT", C_DIM);
    pic_scope(t, PIC_Y + 104, 30, C_TEAL_D);
}

static void pic_insert(void)
{
    int t = ui.track;
    static const char *const d[INS_N] = {"NO INSERT", "COMB: TUNE IS ITS NOTE", "FREQUENCY SHIFT, -200..+200 HZ",
                                         "CRUSH: RATE AND BITS", "SMEAR: FOUR ALLPASSES"};
    g_text(8, PIC_Y + 6, &FONT_B, d[clampi(rv.tp[t][TP_INS], 0, INS_N - 1)], C_INK);
    pic_scope(t, PIC_Y + 30, 100, C_TEAL);
}

static void pic_mix(void)
{
    int t = ui.track, x, h;
    float pan = rv_param_eff(t, TP_PAN) / 64.0f;
    g_hline(20, PIC_Y + 30, 200, C_LINE);
    x = 120 + (int)(pan * 100.0f);
    g_disc(x, PIC_Y + 30, 6, C_TEAL);
    g_text(20, PIC_Y + 40, &FONT_T, "L", C_DIM);
    g_text_r(220, PIC_Y + 40, &FONT_T, "R", C_DIM);
    h = (int)(clampf(fm_sqrtf(rv_live.meter[t]), 0.0f, 1.0f) * 70.0f);
    g_fill(110, PIC_Y + 60, 20, 70, C_LINE);
    g_fill(110, PIC_Y + 130 - h, 20, h, C_TEAL);
    {
        static const int sends[6] = {TP_FBS, TP_PAIRS, TP_BANDS, TP_DLYS, TP_REVS, TP_VOC};
        static const char *const sn[6] = {"FB", "PAIR", "BND", "DLY", "SPC", "VOC"};
        int i;
        for (i = 0; i < 6; i++) {
            int sx = i < 3 ? 14 + i * 30 : 148 + (i - 3) * 28, sh = rv.tp[t][sends[i]] * 50 / 127;
            g_fill(sx, PIC_Y + 80, 10, 50, C_LINE);
            g_fill(sx, PIC_Y + 130 - sh, 10, sh, C_AMBER);
            g_text(sx - 2, PIC_Y + 66, &FONT_T, sn[i], C_DIM);
        }
    }
}

/* the articulation: onset, continuant, termination, drawn from the track's knobs */
/* SHAPE: the morphed, warped shape across the screen, the hold point, and the playing voice */
static void pic_shape(int t)
{
    artic_cfg_t c;
    int i, prev = 0, top = PIC_Y + 14, base = PIC_Y + 112;
    float hold;
    char b[24];
    c.shape = (uint8_t)clampi(rv.tp[t][TP_ESHAPE], 1, SHP_N);
    c.morph = clampf(rv_param_eff(t, TP_EMORPH) / 127.0f, 0.0f, 1.0f);
    c.warp = clampf(rv_param_eff(t, TP_EWARP) / 64.0f, -1.0f, 1.0f);
    c.smode = (uint8_t)rv.tp[t][TP_EMODE];
    hold = shp_hold_u(&c);
    g_hline(8, base, 224, C_LINE);
    if (c.smode == SHM_GATED)
        g_vline(8 + (int)(hold * 224.0f), top - 6, base - top + 6, C_TEAL_D);
    for (i = 0; i <= 224; i++) {
        float y = shp_eval(c.shape, c.morph, c.warp, (float)i / 224.0f);
        int py = base - (int)(clampf(y, 0.0f, 1.0f) * (float)(base - top));
        if (i)
            g_line(8 + i - 1, prev, 8 + i, py, C_TEAL);
        prev = py;
    }
    rv_param_format(PID_T(t, TP_ETIME), rv.tp[t][TP_ETIME], b, 24);
    g_text(10, base + 4, &FONT_T, b, C_DIM);
    g_text_r(232, base + 4, &FONT_T, c.smode == SHM_GATED ? "HOLDS AT THE LINE" : c.smode == SHM_LOOP ? "LOOPS WHILE HELD" : c.smode == SHM_CYCLE ? "LOOPS, ENDS ITS CYCLE" : "PLAYS THROUGH", C_DIM);
    if (rv_live.stage[t] != AS_OFF && rv_live.env[t] > 0.001f) {
        float u = clampf(rv_live.stage_pos[t], 0.0f, 1.0f), lv = clampf(rv_live.env[t], 0.0f, 1.0f);
        g_disc(8 + (int)(u * 224.0f), base - (int)(lv * (float)(base - top)), 4, C_WHITE);
    }
}

static void pic_env(void)
{
    int t = ui.track, i, prev = 0, seg_x[4];
    float atk = rv_time_s((int)rv_param_eff(t, TP_ATK)), dec = rv_time_s((int)rv_param_eff(t, TP_DEC));
    float rel = rv_time_s((int)rv_param_eff(t, TP_REL)), sus = rv_param_eff(t, TP_SUS) / 127.0f;
    float over = rv_param_eff(t, TP_OVER) / 127.0f, curve = rv_param_eff(t, TP_ACURVE) / 64.0f, give = rv_param_eff(t, TP_GIVE) / 127.0f;
    float wa = 18.0f + fm_sqrtf(atk) * 40.0f, wd = 18.0f + fm_sqrtf(dec) * 40.0f, wr = 18.0f + fm_sqrtf(rel) * 40.0f, wh = 40.0f;
    float tot = wa + wd + wh + wr, sc = 224.0f / tot;
    const page_t *pg = cur_page();
    int focus = pg->title[0] == 'O' ? 0 : pg->title[0] == 'C' ? 1 : pg->title[0] == 'T' ? 2 : -1;
    if (rv.tp[t][TP_ESHAPE] > 0) {
        pic_shape(t);
        return;
    }
    int top = PIC_Y + 14, base = PIC_Y + 112;
    float peak = 1.0f + over * 0.6f;
    seg_x[0] = 8 + (int)(wa * sc);
    seg_x[1] = seg_x[0] + (int)(wd * sc);
    seg_x[2] = seg_x[1] + (int)(wh * sc);
    seg_x[3] = 232;
    if (focus >= 0)
        g_fill(focus == 0 ? 8 : focus == 1 ? seg_x[0] : seg_x[2], top - 8, focus == 0 ? seg_x[0] - 8 : focus == 1 ? seg_x[2] - seg_x[0] : 232 - seg_x[2], base - top + 10, C_PANEL);
    g_hline(8, base, 224, C_LINE);
    for (i = 8; i <= 232; i++) {
        float y, u;
        int py;
        if (i < seg_x[0]) {
            u = (float)(i - 8) / (float)(seg_x[0] - 8);
            y = (u + curve * u * (1.0f - u)) * peak;
            if (give > 0.01f && u > 0.8f)
                y += give * 0.12f * fm_sinf((u - 0.8f) * 30.0f);
        } else if (i < seg_x[1]) {
            float r;
            u = (float)(i - seg_x[0]) / (float)(seg_x[1] - seg_x[0]);
            r = 1.0f - u;
            y = peak + (sus - peak) * (1.0f - r * r * r);
            if (give > 0.01f)
                y += give * 0.15f * fm_expf(-u * 4.0f) * fm_sinf(u * 18.0f);
        } else if (i < seg_x[2]) {
            y = sus;
        } else {
            float r;
            u = (float)(i - seg_x[2]) / (float)(232 - seg_x[2]);
            r = 1.0f - u;
            y = sus * r * r * r;
        }
        py = base - (int)(clampf(y / 1.6f, 0.0f, 1.0f) * (float)(base - top));
        if (i > 8)
            g_line(i - 1, prev, i, py, i < seg_x[0] ? C_TEAL : i < seg_x[2] ? C_INK : C_AMBER);
        prev = py;
    }
    g_text(10, base + 4, &FONT_T, "ONSET", focus == 0 ? C_TEAL : C_DIM);
    g_text(seg_x[0] + 2, base + 4, &FONT_T, "CONTINUANT", focus == 1 ? C_INK : C_DIM);
    g_text_r(232, base + 4, &FONT_T, "TERMINATION", focus == 2 ? C_AMBER : C_DIM);
    /* the playing voice */
    {
        int st = rv_live.stage[t];
        float u = clampf(rv_live.stage_pos[t], 0.0f, 1.0f), lv = clampf(rv_live.env[t], 0.0f, 1.6f);
        int x = st == AS_ONSET ? 8 + (int)(u * (float)(seg_x[0] - 8)) : st == AS_CONT ? (u < 1.0f ? seg_x[0] + (int)(u * (float)(seg_x[1] - seg_x[0])) : (seg_x[1] + seg_x[2]) / 2) : st == AS_TERM ? seg_x[2] + (int)(u * (float)(232 - seg_x[2])) : -10;
        if (st != AS_OFF && lv > 0.001f)
            g_disc(x, base - (int)(lv / 1.6f * (float)(base - top)), 4, C_WHITE);
    }
    if (focus < 0) {
        char b[30] = "1: ";
        rv_param_format(PID_T(t, TP_EDST1), rv.tp[t][TP_EDST1], b + 3, 12);
        g_text(10, top - 10, &FONT_T, b, C_MID);
    }
}

static void pic_lib(void)
{
    int i, k;
    for (k = -3; k <= 3; k++) {
        int idx = (int)ui.cur_lib + k, x0 = 120 + k * 33 - 15, prev = 0;
        const uint8_t *l;
        char b[6];
        if (idx < 0 || idx >= RV_LIB)
            continue;
        l = rv.lib[idx];
        if (!k)
            g_fill(x0 - 2, PIC_Y + 30, 34, 70, C_PANEL);
        for (i = 0; i <= 30; i++) {
            float u = (float)i / 30.0f, a = l[0] / 127.0f, bd = l[1] / 127.0f, d = l[2] / 127.0f, y;
            float ea = 0.05f + a * 0.3f, eb = ea + 0.25f + bd * 0.2f, ed = eb + 0.1f + d * 0.3f;
            if (u < ea)
                y = u / ea;
            else if (u < eb)
                y = 1.0f - (1.0f - bd * bd) * (u - ea) / (eb - ea);
            else if (u < ed)
                y = bd * bd * (1.0f - (u - eb) / (ed - eb));
            else
                y = 0.0f;
            {
                int py = PIC_Y + 90 - (int)(y * 50.0f);
                if (i)
                    g_line(x0 + i - 1, prev, x0 + i, py, k ? C_DIM : C_TEAL);
                prev = py;
            }
        }
        sx_int(b, idx + 1);
        g_text_c(x0 + 15, PIC_Y + 104, &FONT_T, b, k ? C_DIM : C_WHITE);
    }
    g_text(8, PIC_Y + 4, &FONT_XS, "SHAPES A STEP CAN RECALL", C_MID);
    g_text(8, PIC_Y + 120, &FONT_T, "SEQ: STEP SHAPE > ENV", C_DIM);
}

static void pic_matrix(void)
{
    int i;
    for (i = 0; i < RV_MATRIX; i++) {
        const rv_mslot *m = &rv.mat[i];
        int y = PIC_Y + 2 + i * 16;
        char b[24], d[24];
        if (i == ui.cur_mat)
            g_fill(0, y - 1, 240, 15, C_PANEL);
        sx_int(b, i + 1);
        g_text(4, y, &FONT_T, b, C_DIM);
        g_text(16, y, &FONT_XS, rv_msrc_names[m->src < MS_N ? m->src : 0], m->src ? C_INK : C_DIM);
        if (m->src) {
            rv_pid_label(m->dst, d, 24);
            g_text(76, y, &FONT_XS, d, C_MID);
            b[0] = 0;
            if (m->amt > 0)
                sx_cat(b, "+");
            sx_int(b + (m->amt > 0), m->amt);
            g_text_r(200, y, &FONT_XS, b, C_INK);
            {
                float v = m->src == MS_KEY   /* the destination track's note: full bar at 5 octaves */
                    ? rv_live.key[m->dst < RV_GPID ? m->dst / RV_TP : 0] * (1.0f / 60.0f) * (float)m->amt / 63.0f
                    : rv_live.msrc[m->src] * (float)m->amt / 64.0f;
                int w = (int)(clampf(v, -1.0f, 1.0f) * 16.0f);
                g_hline(204, y + 7, 33, C_LINE);
                if (w > 0)
                    g_fill(220, y + 5, w, 5, C_TEAL);
                else
                    g_fill(220 + w, y + 5, -w, 5, C_TEAL);
            }
        }
    }
}

static void pic_lfo(int k)
{
    int i, prev = 0, shape = rv.gp[k ? GP_LFO2SHAPE : GP_LFO1SHAPE];
    float ph = lfo[k].ph;
    for (i = 0; i <= 224; i += 2) {
        float p = (float)i / 224.0f, v;
        switch (shape) {
        case 0: v = sin_lut(p); break;
        case 1: v = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p; break;
        case 2: v = 2.0f * p - 1.0f; break;
        case 3: v = 1.0f - 2.0f * p; break;
        case 4: v = p < 0.5f ? 1.0f : -1.0f; break;
        default: v = sin_lut(p * 3.0f) * 0.5f + sin_lut(p * 7.0f + 0.3f) * 0.3f; break;
        }
        {
            int y = PIC_Y + 60 - (int)(v * 50.0f);
            if (i)
                g_line(8 + i - 2, prev, 8 + i, y, C_TEAL_D);
            prev = y;
        }
    }
    g_vline(8 + (int)(ph * 224.0f), PIC_Y + 6, 108, C_TEAL);
    g_disc(8 + (int)(ph * 224.0f), PIC_Y + 60 - (int)((k ? rv_live.lfo2 : rv_live.lfo1) * 50.0f), 4, C_WHITE);
}

static void pic_dust(void)
{
    int i;
    ui.dust_hist[ui.dust_w] = (uint8_t)(clampf(rv_live.dust, 0.0f, 1.0f) * 100.0f);
    ui.dust_w = (uint8_t)((ui.dust_w + 1) % 60);
    for (i = 0; i < 60; i++) {
        int v = ui.dust_hist[(ui.dust_w + i) % 60];
        g_fill(8 + i * 4, PIC_Y + 116 - v, 3, v, i == 59 ? C_WHITE : C_TEAL);
    }
}

static void pic_macro(void)
{
    int m = ui.cur_macro / 8, d = ui.cur_macro % 8, i, prev = 0;
    const rv_mdest *md = &rv.macro[m].d[d];
    float v = rv.gp[GP_MACRO1 + m] / 127.0f;
    char b[24] = "MACRO ";
    b[6] = (char)('1' + m);
    b[7] = 0;
    sx_cat(b, "  DEST ");
    sx_int(b + 14, d + 1);
    g_text(8, PIC_Y + 2, &FONT_XS, b, C_TEAL);
    /* the window, then the curve */
    g_fill(20 + md->lo * 200 / 100, PIC_Y + 20, (md->hi - md->lo) * 200 / 100, 90, C_PANEL);
    g_hline(20, PIC_Y + 65, 200, C_LINE);
    for (i = 0; i <= 200; i += 2) {
        float u = (float)i / 200.0f, lo = md->lo * 0.01f, hi = md->hi * 0.01f, w, val;
        int y;
        if (hi <= lo + 0.001f)
            hi = lo + 0.001f;
        w = clampf((u - lo) / (hi - lo), 0.0f, 1.0f);
        w = macro_curve(md->curve, w);
        val = (float)md->start + ((float)md->end - (float)md->start) * w;
        y = PIC_Y + 65 - (int)(val / 64.0f * 44.0f);
        if (i)
            g_line(20 + i - 2, prev, 20 + i, y, md->dst < RV_NPID ? C_TEAL : C_DIM);
        prev = y;
    }
    g_vline(20 + (int)(v * 200.0f), PIC_Y + 18, 94, C_WHITE);
    /* the macro's other destinations */
    for (i = 0; i < RV_MACRO_DESTS; i++)
        g_fill(20 + i * 26, PIC_Y + 118, 22, 6, rv.macro[m].d[i].dst < RV_NPID ? (i == d ? C_TEAL : C_TEAL_D) : (i == d ? C_MID : C_LINE));
}

static void pic_voc(void)
{
    int b;
    static const char *const hz[8] = {"140", "220", "340", "520", "800", "1K2", "2K6", "6K"};
    for (b = 0; b < 8; b++) {
        int h = (int)(clampf(fm_sqrtf(rv_live.voc_band[b]) * 3.0f, 0.0f, 1.0f) * 100.0f);
        g_fill(14 + b * 28, PIC_Y + 10, 18, 100, C_LINE);
        g_fill(14 + b * 28, PIC_Y + 110 - h, 18, h, C_TEAL);
        g_text_c(23 + b * 28, PIC_Y + 114, &FONT_T, hz[b], C_DIM);
    }
}

static void pic_fb(void)
{
    static const char *const pn[6] = {"FILTER", "COMB", "SMEAR", "SHIFT", "FOLD", "CRUSH"};
    int e = (int)(clampf(rv_live.fb_env * 2.5f, 0.0f, 1.0f) * 100.0f);
    g_frame(20, PIC_Y + 20, 70, 30, C_MID);
    g_text_c(55, PIC_Y + 27, &FONT_XS, "DELAY", C_INK);
    g_frame(130, PIC_Y + 20, 90, 30, C_AMBER);
    g_text_c(175, PIC_Y + 27, &FONT_XS, pn[clampi(rv.gp[GP_FBPROC], 0, 5)], C_AMBER);
    g_line(90, PIC_Y + 35, 130, PIC_Y + 35, C_MID);
    g_line(175, PIC_Y + 50, 175, PIC_Y + 80, C_MID);
    g_line(175, PIC_Y + 80, 55, PIC_Y + 80, C_MID);
    g_line(55, PIC_Y + 80, 55, PIC_Y + 50, C_MID);
    g_text(70, PIC_Y + 84, &FONT_T, rv.gp[GP_FBEDGE] ? "EDGE GUARD ON" : "EDGE GUARD OFF", rv.gp[GP_FBEDGE] ? C_TEAL : C_RED);
    g_fill(20, PIC_Y + 104, 200, 6, C_LINE);
    g_fill(20, PIC_Y + 104, e * 2, 6, e > 85 ? C_RED : C_TEAL);
    g_text(20, PIC_Y + 114, &FONT_T, rv.gp[GP_FBRET] ? "RETURNS INTO A TRACK" : "RETURNS TO THE MASTER", C_DIM);
}

static void pic_pair(void)
{
    int k, i, prev = 0;
    for (k = 0; k < 2; k++) {
        float fc = rv_cut_hz((float)rv.gp[k ? GP_PAIRF2 : GP_PAIRF1]), res = rv.gp[GP_PAIRRES] / 127.0f;
        for (i = 0; i <= 224; i += 2) {
            float f = 20.0f * fm_exp2f((float)i / 224.0f * 10.0f), m = filt_mag(FT_BP, f, fc, res);
            int y = PIC_Y + 100 - (int)(clampf(m, 0.0f, 3.0f) * 30.0f);
            if (i)
                g_line(8 + i - 2, prev, 8 + i, y, k ? C_AMBER : C_TEAL);
            prev = y;
        }
    }
}

/* BANDS: eight bars, each band's level (outline) and its gain now, sweep included (filled) */
static void pic_bands(void)
{
    static const char *const hz[8] = {"150", "240", "380", "600", "950", "1.5K", "2.4K", "3.8K"};
    int k;
    for (k = 0; k < 8; k++) {
        int x = 12 + k * 28, full = rv.gp[GP_BAND1 + k] * 96 / 127, now = (int)(clampf(rv_live.band_g[k], 0.0f, 1.0f) * 96.0f);
        g_fill(x, PIC_Y + 104 - 96, 20, 96, C_LINE);
        g_frame(x, PIC_Y + 104 - full, 20, full > 0 ? full : 1, C_TEAL_D);
        g_fill(x + 3, PIC_Y + 104 - now, 14, now, C_TEAL);
        g_text_c(x + 10, PIC_Y + 110, &FONT_XS, hz[k], C_DIM);
    }
}

static void pic_delay(void)
{
    int i;
    float fb = rv.gp[GP_DLYFB] / 127.0f, a = 1.0f;
    for (i = 0; i < 8; i++) {
        int h = (int)(a * 90.0f);
        g_fill(20 + i * 26, PIC_Y + 110 - h, 8, h, i & 1 ? C_AMBER : C_TEAL);
        a *= fb;
    }
    g_text(20, PIC_Y + 116, &FONT_T, "L      R      L      R", C_DIM);
}

static void pic_space(void)
{
    int i;
    float sz = rv.gp[GP_REVSIZE] / 127.0f, d = rv.gp[GP_REVDAMP] / 127.0f;
    for (i = 0; i < 56; i++) {
        float a = fm_expf(-(float)i / (4.0f + sz * 40.0f)) * (0.6f + 0.4f * sin_lut((float)i * 0.37f));
        int h = (int)(a * 100.0f);
        g_fill(8 + i * 4, PIC_Y + 110 - h, 3, h, g_mix(C_TEAL, C_TEAL_D, d * (float)i / 56.0f));
    }
}

static void pic_master(void)
{
    int hl = (int)(clampf(fm_sqrtf(rv_live.master_l), 0.0f, 1.0f) * 110.0f), hr = (int)(clampf(fm_sqrtf(rv_live.master_r), 0.0f, 1.0f) * 110.0f);
    g_fill(90, PIC_Y + 10, 24, 110, C_LINE);
    g_fill(126, PIC_Y + 10, 24, 110, C_LINE);
    g_fill(90, PIC_Y + 120 - hl, 24, hl, hl > 100 ? C_RED : C_TEAL);
    g_fill(126, PIC_Y + 120 - hr, 24, hr, hr > 100 ? C_RED : C_TEAL);
    g_text_c(102, PIC_Y + 124, &FONT_T, "L", C_DIM);
    g_text_c(138, PIC_Y + 124, &FONT_T, "R", C_DIM);
}

static void pic_scale(void)
{
    static const uint16_t masks[12] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x295, 0x4A9, 0x555, 0x18D, 0x463};
    static const char *const nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int root = rv.gp[GP_ROOT], sc = clampi(rv.gp[GP_SCALE], 0, 11), i, ivl = rv.tp[ui.track][TP_INTERVAL];
    for (i = 0; i < 12; i++) {
        int rel = ((i - root) % 12 + 12) % 12, in = (masks[sc] >> rel) & 1, x = 6 + i * 19;   /* C in bit 0 */
        int pass = ivl <= 1 || rel % ivl == 0;
        g_fill(x, PIC_Y + 20, 16, 60, in ? (i == root ? C_TEAL : C_PANEL) : C_BG);
        g_frame(x, PIC_Y + 20, 16, 60, C_LINE);
        g_text_c(x + 8, PIC_Y + 64, &FONT_T, nm[i], in ? C_INK : C_DIM);
        if (ivl > 1)
            g_fill(x + 4, PIC_Y + 86, 8, 8, pass ? C_AMBER : C_LINE);
    }
    g_text(8, PIC_Y + 104, &FONT_T, ivl > 1 ? "AMBER: THE NOTES THIS TRACK'S INTERVAL PLAYS" : "SCL + KEY SETS THE ROOT", C_DIM);
    if (sc)
        g_text(8, PIC_Y + 116, &FONT_T, "THE KEYS PLAY THE SCALE, FOLDED", C_TEAL);
}

static void pic_tape(void)
{
    static uint8_t pk[224];
    int t = ui.track, istape = rv.tp[t][TP_KIND] == KIND_TAPE, row = istape ? rv.tp[t][TP_S1] : ui.cur_row, i, frozen = rv_tape_frozen();
    float pos[32], lvl[32];
    int n;
    char b[40];
    /* the whole tape, with the rows as spans */
    rv_tape_overview(pk, 112, -1);
    for (i = 0; i < 112; i++) {
        int h = pk[i] * 10 / 255;
        g_fill(8 + i * 2, PIC_Y + 12 - h / 2 - 1, 2, h + 1, frozen ? C_AMBER_D : C_LINE);
    }
    for (i = 0; i < 4; i++) {
        int s = rv.gp[GP_ROW1S + 2 * i] * 224 / 128, e = rv.gp[GP_ROW1E + 2 * i] * 224 / 128;
        g_hline(8 + s, PIC_Y + 20 + i * 2, e - s, i == row ? C_TEAL : C_LINE);
    }
    /* the row, large */
    rv_tape_overview(pk, 224, row);
    for (i = 0; i < 224; i++) {
        int h = pk[i] * 70 / 255;
        g_vline(8 + i, PIC_Y + 66 - h / 2, h + 1, frozen ? C_AMBER : C_AMBER_D);
    }
    for (i = 0; i <= 16; i++)
        g_vline(8 + i * 14, PIC_Y + 104, i % 4 ? 3 : 6, C_DIM);
    if (istape) {
        n = rv_tape_heads(t, pos, 4);
        for (i = 0; i < n; i++)
            g_vline(8 + (int)(clampf(pos[i], 0.0f, 1.0f) * 223.0f), PIC_Y + 28, 76, i ? C_TEAL_D : C_TEAL);
        n = rv_tape_grains(t, pos, lvl, 16);
        for (i = 0; i < n; i++)
            g_disc(8 + (int)(clampf(pos[i], 0.0f, 1.0f) * 223.0f), PIC_Y + 96 - (int)(lvl[i] * 60.0f), 2, C_TEAL);
        if (rv.tp[t][TP_S0] == TAPE_SLICE) {
            n = rv_tape_slices(t, pos, 32);
            for (i = 0; i < n; i++)
                g_vline(8 + (int)(pos[i] * 223.0f), PIC_Y + 30, 6, C_WHITE);
        }
    }
    b[0] = 0;
    sx_cat(b, frozen ? "CAUGHT  " : "LISTENING  ");
    {
        int ds = (int)(rv_tape_seconds() * 10.0f + 0.5f);
        sx_int(b + (frozen ? 8 : 11), ds / 10);
        sx_cat(b, ".");
        {
            char d[4];
            sx_int(d, ds % 10);
            sx_cat(b, d);
        }
        sx_cat(b, "S");
    }
    g_text(8, PIC_Y + 112, &FONT_XS, b, frozen ? C_AMBER : C_MID);
    {
        char r[8] = "ROW ";
        r[4] = (char)('1' + row);
        r[5] = 0;
        g_text_r(232, PIC_Y + 112, &FONT_XS, r, C_TEAL);
    }
    {
        int loud = 0;
        for (i = 0; i < 224; i++)
            if (pk[i] > 8)
                loud++;
        if (loud < 6) {                              /* nothing on it yet: say how to fill it */
            g_fill(16, PIC_Y + 40, 208, 40, C_BG);
            g_frame(16, PIC_Y + 40, 208, 40, C_LINE);
            g_text_c(120, PIC_Y + 46, &FONT_XS, "THE TAPE IS SILENT", C_INK);
            g_text_c(120, PIC_Y + 62, &FONT_T, "PLAY, THEN HOLD REC AND PRESS OCT+", C_MID);
        }
    }
    if (!istape)
        g_text(8, PIC_Y + 126, &FONT_T, "PICK T7 OR T8 (ALGO) TO PLAY THE TAPE", C_DIM);
    else if (rv.tp[t][TP_S0] == TAPE_MLR)
        g_text(8, PIC_Y + 126, &FONT_T, "16 WHITE KEYS = 16 SPOTS IN THE ROW", C_DIM);
}

static void pic_steps(void)
{
    int t = ui.track, len = clampi(rv.tp[t][TP_LEN], 1, 64), i, play = rv_live.step[t][0], play2 = rv_live.step[t][1];
    const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    char b[8];
    /* the 64-step overview */
    for (i = 0; i < 64; i++) {
        int x = 8 + (i % 16) * 14 + (i / 16) * 0, y = PIC_Y + 2 + (i / 16) * 6;
        uint16_t c = i >= len ? C_BG : (p->s[i].flags & ST_ON) ? C_TEAL_D : C_LINE;
        if (i / 16 == ui.step_page && i < len)
            c = (p->s[i].flags & ST_ON) ? C_TEAL : C_DIM;
        if (i == play)
            c = C_WHITE;
        g_fill(x, y, 12, 4, c);
    }
    /* the 16 keys' steps, large */
    for (i = 0; i < 16; i++) {
        int si = ui.step_page * 16 + i, x = 8 + (i % 8) * 28, y = PIC_Y + 30 + (i / 8) * 46;
        const rv_step *s = &p->s[si];
        uint16_t bg = si >= len ? C_BG : (s->flags & ST_ON) ? C_PANEL : C_BG;
        g_fill(x, y, 26, 42, bg);
        if (si >= len)
            continue;
        g_frame(x, y, 26, 42, si == play || si == play2 ? C_WHITE : si == ui.step_cur ? C_TEAL : C_LINE);
        if (s->flags & ST_ON) {
            int vh = s->vel * 20 / 127;
            if (s->flags & ST_JUMP) {
                sx_cat(b, "");
                b[0] = 'J';
                sx_int(b + 1, s->note + 1);
            } else {
                sx_note(b, s->note);
            }
            g_text_c(x + 13, y + 2, &FONT_T, b, C_INK);
            g_fill(x + 3, y + 38 - vh, 4, vh, C_TEAL);
            if (s->lp[0] || s->lp[1])
                g_fill(x + 20, y + 16, 3, 3, C_AMBER);
            if (s->ratchet & 7) {
                int r;
                for (r = 0; r <= (s->ratchet & 7); r++)
                    g_vline(x + 10 + r * 2, y + 30, 6, C_INK);
            }
            if (s->micro)
                g_hline(x + 13 + s->micro, y + 40, 3, C_AMBER);
            if (s->chance < 100)
                g_text(x + 9, y + 18, &FONT_T, "%", C_MID);
            if (s->env)
                g_fill(x + 20, y + 22, 3, 3, C_TEAL);
            if (s->cut)
                g_fill(x + 20, y + 28, 3, 3, C_RED);
        }
        sx_int(b, si + 1);
        if (!(s->flags & ST_ON))
            g_text_c(x + 13, y + 14, &FONT_T, b, C_DIM);
    }
    {
        char pb[24] = "PATTERN ";
        pb[8] = (char)('A' + rv.pat_cur[t] % RV_PATTERNS);
        pb[9] = 0;
        sx_cat(pb, "   PAGE ");
        sx_int(pb + 17, ui.step_page + 1);
        g_text(8, PIC_Y + 124, &FONT_T, pb, C_MID);
    }
}

static void pic_graph(void)
{
    int t = ui.track, len = clampi(rv.tp[t][TP_LEN], 1, 64), i;
    const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    int cx = 120, cy = PIC_Y + 66, r = 56;
    int xs[64], ys[64];
    for (i = 0; i < len; i++) {
        float a = (float)i / (float)len;
        xs[i] = cx + (int)(sin_lut(a) * (float)r);
        ys[i] = cy - (int)(sin_lut(a + 0.25f) * (float)r);
    }
    for (i = 0; i < len; i++) {
        const rv_step *s = &p->s[i];
        int own = s->to[0] && s->to[0] <= len, a = own ? s->to[0] - 1 : (i + 1) % len;   /* as rv_seq.c */
        g_line(xs[i], ys[i], xs[a], ys[a], own ? C_TEAL : C_LINE);
        if (s->to[1] && s->to[1] - 1 < len)
            g_line(xs[i], ys[i], xs[s->to[1] - 1], ys[s->to[1] - 1], C_AMBER);
    }
    for (i = 0; i < len; i++) {
        const rv_step *s = &p->s[i];
        uint16_t c = (s->flags & ST_ON) ? C_INK : C_DIM;
        if (i == rv_live.step[t][0])
            c = C_WHITE;
        g_disc(xs[i], ys[i], i == ui.step_cur ? 4 : 2, i == ui.step_cur ? C_TEAL : c);
        if (i == rv_live.step[t][1])
            g_disc(xs[i], ys[i], 3, C_AMBER);
    }
    if (rv.tp[t][TP_SEQMODE] != SEQ_GRAPH && rv.tp[t][TP_DIR] != DIR_GRAPH)
        g_text_c(120, PIC_Y + 124, &FONT_T, "SET MODE TO GRAPH ON THE TRACK PAGE", C_DIM);
    else {
        static const char *const rn[] = {"OFF", "NOTES", "RHYTHM", "MELODY"};
        int r2 = clampi(rv.tp[t][TP_H2ROLE], 0, 3), tg = rv.tp[t][TP_H2TARGET];
        int r1 = r2 == ROLE_PITCH ? ROLE_GATE : r2 == ROLE_GATE ? ROLE_PITCH : ROLE_NOTES;   /* as rv_seq.c */
        char b[32] = "H1 ";
        sx_cat(b, rn[r1]);
        g_text(6, PIC_Y + 6, &FONT_T, b, C_WHITE);
        if (r2) {
            char c[32] = "H2 ";
            sx_cat(c, rn[r2]);
            if (tg > 0) {
                int n = sx_cat(c, " T");
                sx_int(c + n, tg);
            }
            g_text_r(234, PIC_Y + 6, &FONT_T, c, C_AMBER);
        }
    }
}

/* a playhead's function generator: its shape over a cycle, where the head is, and its rate now */
static float fg_curve(int shape, float u)
{
    switch (shape) {
    case FG_RISE: return u;
    case FG_FALL: return 1.0f - u;
    case FG_ARCH: return sin_lut(u * 0.5f);
    case FG_DECAY: return fm_expf(-8.0f * u);
    case FG_SWELL: return 0.5f - 0.5f * sin_lut(u + 0.25f);
    default: return 0.0f;
    }
}

static void pic_motion(void)
{
    int t = ui.track, h = cur_page()->k[0].id == TP_H2SHAPE ? 1 : 0, i, prev = 0;
    int shape = clampi(rv.tp[t][h ? TP_H2SHAPE : TP_H1SHAPE], 0, FG_N - 1);
    float base = rv.tp[t][h ? TP_H2RATE : TP_RATE], pos = rv_live.fg_pos[t][h];
    uint16_t col = h ? C_AMBER : C_TEAL;
    int x0 = 12, x1 = 228, yb = PIC_Y + 104, ht = 80;
    char b[40];
    (void)base;
    g_hline(x0, yb, x1 - x0, C_LINE);
    if (shape == FG_OFF) {
        g_text_c(120, PIC_Y + 40, &FONT_XS, "NO MOTION: THE HEAD KEEPS ITS RATE", C_DIM);
        g_text_c(120, PIC_Y + 58, &FONT_T, "SHAPE MOVES ITS RATE AGAINST THE CLOCK", C_DIM);
        return;
    }
    if (shape >= FG_LFO1) {                          /* a modulator: its level now, as a bar */
        int w = (int)(rv_live.fg[t][h] * (float)(x1 - x0));
        g_fill(x0, yb - 30, w, 24, col);
        g_frame(x0, yb - 30, x1 - x0, 24, C_LINE);
    } else {
        for (i = 0; i <= x1 - x0; i += 2) {
            int y = yb - (int)(fg_curve(shape, (float)i / (float)(x1 - x0)) * (float)ht);
            if (i)
                g_line(x0 + i - 2, prev, x0 + i, y, col);
            prev = y;
        }
        i = (int)(pos * (float)(x1 - x0));
        g_vline(x0 + i, yb - ht, ht, C_DIM);
        g_disc(x0 + i, yb - (int)(fg_curve(shape, pos) * (float)ht), 4, C_WHITE);
    }
    /* the rate now, as a multiple of the clock */
    {
        int c100 = (int)(rv_live.fg_rate[t][h] * 100.0f + 0.5f), n = sx_int(b, c100 / 100);
        b[n++] = '.';
        b[n++] = (char)('0' + (c100 / 10) % 10);
        b[n++] = (char)('0' + c100 % 10);
        b[n] = 0;
    }
    sx_cat(b, " STEPS PER PULSE");
    g_text_c(120, PIC_Y + 112, &FONT_T, b, C_MID);
}

static void pic_mixer(void)
{
    int t;
    for (t = 0; t < RV_TRACKS; t++) {
        int x = 8 + t * 29, lv = rv.tp[t][TP_LEVEL] * 100 / 127, h = (int)(clampf(fm_sqrtf(rv_live.meter[t]), 0.0f, 1.0f) * 100.0f);
        char b[4];
        uint8_t muted = (rv.mute >> t) & 1u;
        g_fill(x, PIC_Y + 6, 8, 100, C_LINE);
        g_fill(x, PIC_Y + 106 - h, 8, h, muted ? C_DIM : C_TEAL);
        g_fill(x + 11, PIC_Y + 106 - lv, 12, 3, kind_col(t));
        b[0] = (char)('1' + t);
        b[1] = 0;
        g_text_c(x + 12, PIC_Y + 110, &FONT_XS, b, muted ? C_RED : t == ui.track ? C_WHITE : C_MID);
    }
}

static void pic_scenes(void)
{
    int s;
    for (s = 0; s < RV_SCENES; s++) {
        int x = 10 + (s % 8) * 28, y = PIC_Y + 8 + (s / 8) * 40;
        const rv_scene *sc = &rv.scene[s];
        char b[4];
        uint16_t c = sc->used ? C_PANEL : C_BG;
        g_fill(x, y, 24, 34, c);
        g_frame(x, y, 24, 34, s == rv.scene_cur ? C_TEAL : s == ui.cur_scene ? C_MID : C_LINE);
        sx_int(b, s + 1);
        g_text_c(x + 12, y + 10, &FONT_XS, b, sc->used ? C_INK : C_DIM);
        if (sc->resample)
            g_fill(x + 9, y + 26, 6, 3, C_AMBER);
    }
    g_text(10, PIC_Y + 92, &FONT_T, "WHITE KEYS LAUNCH ON THE BAR", C_DIM);
    g_text(10, PIC_Y + 104, &FONT_T, "HOLD SAVE + WHITE KEY TO STORE", C_DIM);
    g_text(10, PIC_Y + 116, &FONT_T, "BLACK 1-2: THIS TRACK'S PATTERN A, B", C_DIM);
}

static void pic_arr(void)
{
    int i, x = 8, tot = 0;
    for (i = 0; i < rv.arr_len; i++)
        tot += rv.arr[i].bars ? rv.arr[i].bars : 1;
    if (!tot)
        tot = 1;
    for (i = 0; i < rv.arr_len && i < RV_ARR; i++) {
        int w = (rv.arr[i].bars ? rv.arr[i].bars : 1) * 224 / tot;
        char b[4];
        g_fill(x, PIC_Y + 30, w - 1 > 0 ? w - 1 : 1, 40, i == ui.cur_arr ? C_TEAL_D : C_PANEL);
        sx_int(b, rv.arr[i].scene + 1);
        if (w > 10)
            g_text_c(x + w / 2, PIC_Y + 42, &FONT_XS, b, C_INK);
        x += w;
    }
    g_text(8, PIC_Y + 80, &FONT_T, rv.gp[GP_SONGMODE] ? "PLAYS THE ARRANGEMENT (SONG)" : "PLAYS PATTERNS: SET PLAYS TO SONG", C_MID);
    g_text(8, PIC_Y + 92, &FONT_T, "NUMBERS ARE SCENES", C_DIM);
}

static void pic_arcs(void)
{
    int t, i;
    for (t = 0; t < RV_TRACKS; t++) {
        int prev = 0;
        for (i = 0; i < 16; i++) {
            int y = PIC_Y + 110 - rv.arcs[t][i] * 100 / 100, x = 12 + i * 14;
            if (i)
                g_line(x - 14, prev, x, y, t == ui.track ? C_TEAL : C_LINE);
            prev = y;
        }
    }
    g_vline(12 + ui.cur_arc * 14, PIC_Y + 6, 108, C_WHITE);
    {
        int b = rv_live.bar & 15;
        g_fill(10 + b * 14, PIC_Y + 116, 5, 5, C_AMBER);
    }
}

static void pic_clock(void)
{
    int i, prev = 0;
    char b[12];
    sx_int(b, rv.gp[GP_TEMPO]);
    g_text(8, PIC_Y + 6, &FONT_B, b, C_INK);
    g_text(8 + g_text_w(&FONT_B, b) + 4, PIC_Y + 10, &FONT_T, "BPM", C_DIM);
    for (i = 0; i <= 224; i += 2) {
        float p = (float)i / 224.0f * 2.0f;
        float w = clock_wave(p, rv.gp[GP_CLKSHAPE] / 127.0f);
        int y = PIC_Y + 70 - (int)(w * 26.0f);
        if (i)
            g_line(8 + i - 2, prev, 8 + i, y, C_TEAL);
        prev = y;
    }
    for (i = 0; i <= 224; i += 2) {
        float p = (float)i / 224.0f;
        float w = clock_wave(p, rv.gp[GP_CLKBSHAPE] / 127.0f);
        int y = PIC_Y + 112 - (int)(w * 12.0f);
        if (i)
            g_line(8 + i - 2, prev, 8 + i, y, C_AMBER_D);
        prev = y;
    }
    g_fill(200, PIC_Y + 8, 30, 30, g_mix(C_BG, C_TEAL, rv_live.clock_env));
}

static void pic_save(void)
{
    int i;
    for (i = 0; i < PLAT_SLOTS; i++) {
        int y = PIC_Y + 2 + i * 15;
        char b[20];
        if (i == ui.cur_slot)
            g_fill(0, y - 1, 240, 14, ui.save_confirm ? C_AMBER_D : C_PANEL);
        sx_int(b, i + 1);
        g_text(8, y, &FONT_XS, b, C_DIM);
        g_text(26, y, &FONT_XS, ui.slot_used[i] ? ui.slot_names[i] : "EMPTY", ui.slot_used[i] ? C_INK : C_DIM);
    }
    {
        char b[24] = "PLAYING: ";
        sx_cat(b, rv.name);
        g_text(130, PIC_Y + 2, &FONT_T, b, C_MID);
    }
}

static void draw_picture(void)
{
    g_fill(0, PIC_Y - 2, 240, PIC_H + 2, C_BG);
    switch (cur_page()->pic) {
    case PIC_TRACKS: pic_tracks(); break;
    case PIC_INFL: pic_infl(); break;
    case PIC_SOURCE: pic_source(); break;
    case PIC_FILTER: pic_filter(); break;
    case PIC_SKIN: pic_skin(); break;
    case PIC_INSERT: pic_insert(); break;
    case PIC_MIX: pic_mix(); break;
    case PIC_ENV: pic_env(); break;
    case PIC_LIB: pic_lib(); break;
    case PIC_MATRIX: pic_matrix(); break;
    case PIC_LFO1: pic_lfo(0); break;
    case PIC_LFO2: pic_lfo(1); break;
    case PIC_DUST: pic_dust(); break;
    case PIC_MACRO: pic_macro(); break;
    case PIC_VOC: pic_voc(); break;
    case PIC_FB: pic_fb(); break;
    case PIC_PAIR: pic_pair(); break;
    case PIC_DELAY: pic_delay(); break;
    case PIC_SPACE: pic_space(); break;
    case PIC_MASTER: pic_master(); break;
    case PIC_BANDS: pic_bands(); break;
    case PIC_SCALE: pic_scale(); break;
    case PIC_TAPE: pic_tape(); break;
    case PIC_STEPS: pic_steps(); break;
    case PIC_GRAPH: pic_graph(); break;
    case PIC_MOTION: pic_motion(); break;
    case PIC_MIXER: pic_mixer(); break;
    case PIC_SCENES: pic_scenes(); break;
    case PIC_ARR: pic_arr(); break;
    case PIC_ARCS: pic_arcs(); break;
    case PIC_CLOCK: pic_clock(); break;
    case PIC_SAVE: pic_save(); break;
    case PIC_CZWAVE: pic_czwave(); break;
    case PIC_CZENV: pic_czenv(); break;
    default: break;
    }
}

static void leds(void)
{
    uint32_t lb = 0, lk = 0;
    int i, t = ui.track;
    lb |= 1u << sec_button[ui.sec];
    if (rv_live.playing)
        lb |= 1u << B_PLAY;
    if (ui.armed)
        lb |= 1u << B_REC;
    if (ui.sec == SEC_SEQ || (plat_buttons() & (1u << B_SEQ))) {
        const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
        for (i = 0; i < 16; i++) {
            int si = ui.step_page * 16 + i;
            if ((p->s[si].flags & ST_ON) ^ (si == rv_live.step[t][0]))
                lk |= 1u << white_keys[i];
        }
        lk |= 1u << black_keys[rv.pat_cur[t] % RV_PATTERNS];
    } else if (ui.sec == SEC_TAPE && rv.tp[t][TP_KIND] == KIND_TAPE) {
        float pos[2];
        if (rv_tape_heads(t, pos, 1) > 0)
            lk |= 1u << white_keys[clampi((int)(pos[0] * 16.0f), 0, 15)];
        lk |= 1u << black_keys[clampi(rv.tp[t][TP_S1], 0, 3)];
        if (rv_tape_frozen())
            lk |= 1u << black_keys[8];
    } else {
        lk = plat_keys();
    }
    plat_leds(lb, lk);
}

void ui_frame(void)
{
    int b, i, j;
    ui_input_only();
    ui.frame++;
    for (i = 0; i < RV_TRACKS; i++)
        for (j = 0; j < RV_TRACKS; j++)
            if (rv_live.route_fire[i][j] > 0)
                rv_live.route_fire[i][j] = (uint8_t)(rv_live.route_fire[i][j] > 24 ? rv_live.route_fire[i][j] - 24 : 0);
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        draw_top();
        draw_knobs();
        draw_picture();
        draw_footer();
        g_send();
    }
    leds();
}

void ui_init(void)
{
    int t;
    uint8_t *p = (uint8_t *)&ui;
    unsigned i;
    for (i = 0; i < sizeof(ui); i++)
        p[i] = 0;
    for (t = 0; t < RV_TRACKS; t++)
        ui.preset_idx[t] = -1;
    ui.sec = SEC_HOME;
    ui.prev_btn = plat_buttons();
    ui.prev_keys = plat_keys();
}

/* a whole-screen notice (the device's start, update, crash and safe-mode screens): a title and up
 * to three lines, on black, or on red for a crash */
void ui_notice(const char *title, const char *l1, const char *l2, const char *l3, int red)
{
    int b;
    uint16_t bg = red ? RGB(150, 20, 24) : RGB(0, 0, 0);
    for (b = 0; b < PLAT_H; b += GB_H) {
        g_band(b);
        g_fill(0, b, PLAT_W, GB_H, bg);
        if (title)
            g_text_c(PLAT_W / 2, 70, &FONT_B, title, C_WHITE);
        if (l1)
            g_text_c(PLAT_W / 2, 110, &FONT_XS, l1, red ? C_WHITE : C_INK);
        if (l2)
            g_text_c(PLAT_W / 2, 132, &FONT_XS, l2, red ? C_WHITE : C_INK);
        if (l3)
            g_text_c(PLAT_W / 2, 160, &FONT_XS, l3, red ? C_WHITE : C_RED);
        g_send();
    }
}

/* for the emulator page: where the interface is */
int ui_section(void) { return ui.sec; }
int ui_track(void) { return ui.track; }
const char *ui_page_title(void) { return cur_page()->title; }
void ui_goto(int sec, int page)
{
    if (sec >= 0 && sec < NSEC) {
        ui.sec = (uint8_t)sec;
        if (page >= 0)
            ui.page[sec] = (uint8_t)page;
        if (sec == SEC_SAVE)
            slots_refresh();
    }
}
void ui_set_track(int t) { ui.track = (uint8_t)clampi(t, 0, RV_TRACKS - 1); }
int ui_octave(void) { return ui.octave; }
int ui_armed(void) { return ui.armed; }
