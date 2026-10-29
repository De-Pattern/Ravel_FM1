/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the interface. Each button opens the section nearest its printed word; SELECT walks
 * the section's pages, ALGORITHM picks the track, PRESETS browses what the page holds (sounds,
 * patterns, scenes, slots, rows...), KNOB 1-4 set the four values in the knob row. Holding a
 * button gives its layer: SEQ makes the keys steps on any page (hold a step and turn a knob:
 * a lock), LFO puts the macros on the knobs, FX puts punch-in effects on the keys,
 * GLO mutes and solos, SAVE stores scenes, SCL sets the root, REC arms recording.
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
enum { SEC_HOME, SEC_VOICE, SEC_ARTIC, SEC_MOD, SEC_BUS, SEC_SCALE, SEC_ARP, SEC_SEQ, SEC_SONG, SEC_SAVE, NSEC };
static const char *const sec_names[NSEC] = {"HOME", "VOICE", "ARTICULATION", "MOD", "BUSES", "SCALE", "ARP", "SEQ", "SONG", "SAVE"};
static const uint8_t sec_button[NSEC] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SCL, B_ARP, B_SEQ, B_GLO, B_SAVE};

/* knob bindings: what a knob in the row stands for */
enum { K_NONE, K_TP, K_GP, K_STEP, K_ROUTE, K_MAT, K_MACRO, K_ARR, K_ARC, K_LIB, K_TLEVEL, K_SAVE, K_CZ, K_LANE, K_LFOR, K_OSC, K_BANK, K_EUC, K_STRESS };   /* K_STRESS: the stress test (STEP, ENGINE, AUTO) */   /* K_EUC: EUCLID, HITS and ROTATE */   /* K_LFOR, K_OSC: resolved by page_knob() */
/* K_LANE: a KIT lane's settings (the lane chosen on the top-row buttons or black keys 3-8) */
enum { LF_SEL, LF_MACH, LF_P0, LF_LEVEL = LF_P0 + 8, LF_LEN, LF_CHANCE, LF_RATE, LF_NONE, LF_SLOT0 };   /* LF_SLOT0-3: a LANE page's knobs, by row */
/* K_CZ ids: line << 8 | envelope << 4 | field (the wave fields have no envelope) */
enum { CZF_WAVE1, CZF_WAVE2, CZF_KFA, CZF_KFW, CZF_RATE, CZF_LEVEL, CZF_SUS, CZF_END, CZF_SLOT0 };   /* CZF_SLOT0-3: an envelope page's knobs, by OCT row; RATE/LEVEL carry their stage in bits 9-11 */
enum { SF_NOTE, SF_VEL, SF_LEN, SF_CHANCE, SF_MICRO, SF_RATCHET, SF_RCURVE, SF_COND, SF_ENV, SF_CUT, SF_TIE, SF_CORDA, SF_CORDB, SF_ACC, SF_RMOVE, SF_CSHAPE, SF_CSIZE };
enum { RF_TYPE, RF_AMT, RF_ARG, RF_CHANCE };
enum { MF_SRC, MF_TRACK, MF_PARAM, MF_AMT };
enum { XF_DEST, XF_START, XF_END, XF_CURVE, XF_LO, XF_HI };
enum { AF_SCENE, AF_BARS, AF_ROWS, AF_MODE, AF_STEMPO };   /* AF_STEMPO: the current scene's own tempo */
typedef struct { uint8_t kind; uint16_t id; } kb_t;

/* pictures */
enum { PIC_TRACKS, PIC_INFL, PIC_SOURCE, PIC_FILTER, PIC_SKIN, PIC_INSERT, PIC_MIX, PIC_ENV, PIC_LIB, PIC_MATRIX,
       PIC_LFO1, PIC_LFO2, PIC_DUST, PIC_MACRO, PIC_FB, PIC_DELAY, PIC_SPACE, PIC_BED, PIC_EUCLID, PIC_CPU, PIC_STRESS, PIC_MASTER, PIC_SCALE,
       PIC_STEPS, PIC_GRAPH, PIC_MOTION, PIC_MIXER, PIC_SCENES, PIC_ARR, PIC_ARCS, PIC_CLOCK, PIC_SAVE,
       PIC_CZWAVE, PIC_CZENV, PIC_DRUM, PIC_LANE, PIC_BANK, PIC_FENV, PIC_SENV, PIC_ARP, PIC_COMP, PIC_FLASH, PIC_MIDI };
/* when a page shows */
enum { V_ALL, V_SRC0, V_SRC1, V_SRC2, V_SRC3, V_SYNTH, V_STRIKE, V_NOTTAPE, V_PKIT,
       V_STEPSMODE, V_GRAPH, V_GRAPH2, V_CZL1, V_CZL2, V_NOTCZ, V_KIT, V_NOTKIT, V_SEM, V_OSCMIX, V_COMP };
/* what PRESETS does on the page */
enum { PR_NONE, PR_SOUND, PR_ROUTE, PR_MAT, PR_MACRO, PR_PATTERN, PR_SCENE, PR_ARR, PR_ARC, PR_LIB, PR_SLOT, PR_STEPCUR, PR_LANE,
       PR_CZSTEP, PR_LFOROW, PR_BANK };

#define USND 1000                       /* ui.preset_idx at or above: your own sound USND + slot (the current bank) */
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
#define LF(f) {K_LANE, (uint16_t)(f)}
#define CZW(l, f) {K_CZ, (uint16_t)((l) << 8 | (f))}
#define CZE(l, e, f) {K_CZ, (uint16_t)((l) << 8 | (e) << 4 | (f))}

static const page_t pages[] = {
    {SEC_HOME, V_ALL, PIC_TRACKS, PR_SOUND, "TRACKS", {TP(TP_LEVEL), TP(TP_PAN), TP(TP_KIND), TP(TP_ENGINE)}},
    {SEC_HOME, V_ALL, PIC_INFL, PR_ROUTE, "INFLUENCE", {RF(RF_TYPE), RF(RF_AMT), RF(RF_ARG), RF(RF_CHANCE)}},

    {SEC_VOICE, V_ALL, PIC_SOURCE, PR_SOUND, "SOUND", {TP(TP_KIND), TP(TP_ENGINE), TP(TP_POLY), TP(TP_VELO)}},
    /* COMP (T8): the bus compressor on the main mix; a COMP track shows these and nothing else of a voice */
    {SEC_VOICE, V_COMP, PIC_COMP, PR_NONE, "COMP", {TP(TP_S0), TP(TP_S1), TP(TP_S2), TP(TP_S3)}},
    {SEC_VOICE, V_COMP, PIC_COMP, PR_NONE, "COMP 2", {TP(TP_S4), TP(TP_S5), TP(TP_KIND), NO}},
    {SEC_VOICE, V_SRC0, PIC_SOURCE, PR_SOUND, "SOURCE 1", {TP(TP_S0), TP(TP_S1), TP(TP_S2), TP(TP_S3)}},
    {SEC_VOICE, V_SRC1, PIC_SOURCE, PR_SOUND, "SOURCE 2", {TP(TP_S4), TP(TP_S5), TP(TP_S6), TP(TP_S7)}},
    {SEC_VOICE, V_SRC2, PIC_SOURCE, PR_SOUND, "SOURCE 3", {TP(TP_S8), TP(TP_S9), TP(TP_S10), TP(TP_S11)}},
    {SEC_VOICE, V_SRC3, PIC_SOURCE, PR_SOUND, "SOURCE 4", {TP(TP_S12), TP(TP_S13), TP(TP_S14), TP(TP_S15)}},
    {SEC_VOICE, V_OSCMIX, PIC_SOURCE, PR_SOUND, "OSC MIX", {{K_OSC, 0}, {K_OSC, 1}, {K_OSC, 2}, {K_OSC, 3}}},   /* each oscillator's level */
    /* a KIT: a page per lane, its whole sound on screen; PRESETS picks the machine, OCT the knobs' row */
#define LANEPG(n) {SEC_VOICE, V_KIT, PIC_LANE, PR_LANE, "LANE " #n, {LF(LF_SLOT0), LF(LF_SLOT0 + 1), LF(LF_SLOT0 + 2), LF(LF_SLOT0 + 3)}}
    LANEPG(1), LANEPG(2), LANEPG(3), LANEPG(4), LANEPG(5), LANEPG(6),
    {SEC_VOICE, V_PKIT, PIC_SOURCE, PR_SOUND, "KIT FX", {TP(TP_S9), TP(TP_S10), TP(TP_S11), TP(TP_S12)}},
    {SEC_VOICE, V_PKIT, PIC_SOURCE, PR_SOUND, "KIT FILTER", {TP(TP_S13), TP(TP_S14), TP(TP_S15), NO}},
    /* the CZ engine's lines: waves and key follow, then the three 8-step envelopes each */
    {SEC_VOICE, V_CZL1, PIC_CZWAVE, PR_SOUND, "LINE 1", {CZW(0, CZF_WAVE1), CZW(0, CZF_WAVE2), CZW(0, CZF_KFA), CZW(0, CZF_KFW)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCO 1", {CZE(0, CZE_DCO, CZF_SLOT0), CZE(0, CZE_DCO, CZF_SLOT0 + 1), CZE(0, CZE_DCO, CZF_SLOT0 + 2), CZE(0, CZE_DCO, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCW 1", {CZE(0, CZE_DCW, CZF_SLOT0), CZE(0, CZE_DCW, CZF_SLOT0 + 1), CZE(0, CZE_DCW, CZF_SLOT0 + 2), CZE(0, CZE_DCW, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_CZL1, PIC_CZENV, PR_CZSTEP, "DCA 1", {CZE(0, CZE_DCA, CZF_SLOT0), CZE(0, CZE_DCA, CZF_SLOT0 + 1), CZE(0, CZE_DCA, CZF_SLOT0 + 2), CZE(0, CZE_DCA, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_CZL2, PIC_CZWAVE, PR_SOUND, "LINE 2", {CZW(1, CZF_WAVE1), CZW(1, CZF_WAVE2), CZW(1, CZF_KFA), CZW(1, CZF_KFW)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCO 2", {CZE(1, CZE_DCO, CZF_SLOT0), CZE(1, CZE_DCO, CZF_SLOT0 + 1), CZE(1, CZE_DCO, CZF_SLOT0 + 2), CZE(1, CZE_DCO, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCW 2", {CZE(1, CZE_DCW, CZF_SLOT0), CZE(1, CZE_DCW, CZF_SLOT0 + 1), CZE(1, CZE_DCW, CZF_SLOT0 + 2), CZE(1, CZE_DCW, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_CZL2, PIC_CZENV, PR_CZSTEP, "DCA 2", {CZE(1, CZE_DCA, CZF_SLOT0), CZE(1, CZE_DCA, CZF_SLOT0 + 1), CZE(1, CZE_DCA, CZF_SLOT0 + 2), CZE(1, CZE_DCA, CZF_SLOT0 + 3)}},
    {SEC_VOICE, V_ALL, PIC_SOURCE, PR_SOUND, "PITCH", {TP(TP_PITCH), TP(TP_FINE), TP(TP_GLIDE), TP(TP_KTRK)}},   /* VCF KEY: the filter follows the note */
    {SEC_VOICE, V_ALL, PIC_FILTER, PR_SOUND, "FILTER", {TP(TP_FTYPE), TP(TP_CUT), TP(TP_RES), TP(TP_EFILT)}},   /* ENV>CUT: also on TERMINATION, the same setting */
    {SEC_VOICE, V_SEM, PIC_FILTER, PR_SOUND, "SEM", {TP(TP_FMODE), TP(TP_FBP), TP(TP_CUT), TP(TP_RES)}},
    {SEC_VOICE, V_ALL, PIC_SKIN, PR_SOUND, "SKIN", {TP(TP_DRIVE), TP(TP_FOLD), TP(TP_BITS), NO}},
    {SEC_VOICE, V_ALL, PIC_SKIN, PR_SOUND, "SKIN 2", {TP(TP_MASK), TP(TP_DEREZ), TP(TP_GRAIN), NO}},
    {SEC_VOICE, V_ALL, PIC_INSERT, PR_SOUND, "INSERT", {TP(TP_INS), TP(TP_INA), TP(TP_INB), TP(TP_INMIX)}},
    {SEC_VOICE, V_ALL, PIC_MIX, PR_SOUND, "MIX", {TP(TP_LEVEL), TP(TP_PAN), TP(TP_DLYS), TP(TP_REVS)}},
    {SEC_VOICE, V_ALL, PIC_MIX, PR_SOUND, "SENDS", {TP(TP_FBS), TP(TP_DLYS), TP(TP_REVS), NO}},

    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "ENVELOPE", {TP(TP_ATK), TP(TP_DEC), TP(TP_SUS), TP(TP_REL)}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "ONSET", {TP(TP_ACURVE), TP(TP_OVER), TP(TP_GIVE), NO}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "LOOP / TAIL", {TP(TP_LOOP), TP(TP_TAIL), NO, NO}},
    {SEC_ARTIC, V_ALL, PIC_ENV, PR_SOUND, "DESTINATIONS", {TP(TP_EDST1), TP(TP_EAMT1), TP(TP_EDST2), TP(TP_EAMT2)}},
    /* the FILTER ENV: its own delay, attack, hold, decay, sustain, release, bends, loop and rate */
    {SEC_ARTIC, V_ALL, PIC_FENV, PR_SOUND, "FILTER ENV", {TP(TP_FATK), TP(TP_FDEC), TP(TP_FSUS), TP(TP_FREL)}},
    {SEC_ARTIC, V_ALL, PIC_FENV, PR_SOUND, "FENV SHAPE", {TP(TP_FDLY), TP(TP_FHOLD), TP(TP_FACRV), TP(TP_FDCRV)}},
    {SEC_ARTIC, V_ALL, PIC_FENV, PR_SOUND, "FENV LOOP", {TP(TP_FLOOP), TP(TP_FRATE), NO, NO}},
    {SEC_ARTIC, V_ALL, PIC_FENV, PR_SOUND, "FENV ROUTE", {TP(TP_EFILT), TP(TP_FDST), TP(TP_FAMT), TP(TP_KTRK)}},   /* ENV>CUT (also on FILTER); a second destination of its own */
    {SEC_ARTIC, V_NOTKIT, PIC_SENV, PR_SOUND, "STEP ENV", {TP(TP_SEDST), TP(TP_SEAMT), TP(TP_SEDEF), NO}},   /* where the LIBRARY goes */
    {SEC_ARTIC, V_ALL, PIC_LIB, PR_LIB, "LIBRARY", {{K_LIB, 0}, {K_LIB, 1}, {K_LIB, 2}, {K_LIB, 3}}},   /* OCT: segment 1-8, then LOOP and NEXT */

    {SEC_MOD, V_ALL, PIC_MATRIX, PR_MAT, "MATRIX", {MF(MF_SRC), MF(MF_TRACK), MF(MF_PARAM), MF(MF_AMT)}},
    {SEC_MOD, V_ALL, PIC_LFO1, PR_NONE, "LFOS", {GP(GP_LFO1RATE), GP(GP_LFO1SHAPE), GP(GP_LFO2RATE), GP(GP_LFO2SHAPE)}},
    {SEC_MOD, V_ALL, PIC_LFO2, PR_LFOROW, "LFO SHAPE", {{K_LFOR, 0}, {K_LFOR, 1}, {K_LFOR, 2}, NO}},   /* OCT: LFO 1 or LFO 2 */
    {SEC_MOD, V_ALL, PIC_DUST, PR_NONE, "DUST", {GP(GP_DUSTRATE), GP(GP_DUSTDECAY), NO, NO}},
    {SEC_MOD, V_ALL, PIC_MACRO, PR_MACRO, "MACRO", {XF(XF_DEST), XF(XF_START), XF(XF_END), XF(XF_CURVE)}},
    {SEC_MOD, V_ALL, PIC_MACRO, PR_MACRO, "MACRO WINDOW", {XF(XF_LO), XF(XF_HI), {K_GP, GP_MACRO1}, {K_GP, GP_MACRO2}}},

    /* each bus: this track's send first */
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "DELAY", {TP(TP_DLYS), GP(GP_DLYTIME), GP(GP_DLYFB), GP(GP_DLYLEVEL)}},
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "DELAY 2", {GP(GP_DLYTONE), GP(GP_DLYSPREAD), GP(GP_DLYDBL), GP(GP_DLYFREEZE)}},
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "TAPE WEAR", {GP(GP_DLYWOB), GP(GP_DLYFLUT), GP(GP_DLYSAT), GP(GP_DLYAGE)}},
    {SEC_BUS, V_ALL, PIC_DELAY, PR_NONE, "GRAINS", {GP(GP_DLYMODE), GP(GP_GRDENS), GP(GP_GRSIZE), GP(GP_GRPITCH)}},
    {SEC_BUS, V_ALL, PIC_SPACE, PR_NONE, "SPACE", {TP(TP_REVS), GP(GP_REVSIZE), GP(GP_REVDAMP), GP(GP_REVLEVEL)}},
    {SEC_BUS, V_ALL, PIC_SPACE, PR_NONE, "SPACE 2", {GP(GP_REVMOD), GP(GP_REVDIFF), GP(GP_REVLOW), NO}},   /* (12-BIT: DECAY, CRUNCH) */
    {SEC_BUS, V_ALL, PIC_BED, PR_NONE, "BED", {GP(GP_BEDLEVEL), GP(GP_BEDDENS), GP(GP_BEDTONE), GP(GP_BEDSHIM)}},   /* dust on a tuned body, under everything */
    {SEC_BUS, V_ALL, PIC_BED, PR_NONE, "BED 2", {GP(GP_BEDDUCK), GP(GP_BEDDECAY), GP(GP_BEDSPACE), NO}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK", {TP(TP_FBS), GP(GP_FBTIME), GP(GP_FBREGEN), GP(GP_FBLEVEL)}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK 2", {GP(GP_FBPROC), GP(GP_FBAMT), GP(GP_FBTUNE), GP(GP_FBCUT)}},
    {SEC_BUS, V_ALL, PIC_FB, PR_NONE, "FEEDBACK 3", {GP(GP_FBEDGE), GP(GP_FBRET), GP(GP_PAIRF1), GP(GP_PAIRF2)}},   /* BASE, WIDTH: the loop's band */
    {SEC_BUS, V_ALL, PIC_MASTER, PR_NONE, "MASTER", {GP(GP_MDRIVE), GP(GP_CUTSIZE), GP(GP_CUTREP), GP(GP_CUTMIX)}},
    {SEC_BUS, V_ALL, PIC_MASTER, PR_NONE, "MASTER 2", {GP(GP_CUTSHAPE), NO, NO, NO}},

    {SEC_SCALE, V_ALL, PIC_SCALE, PR_NONE, "SCALE", {GP(GP_ROOT), GP(GP_SCALE), GP(GP_TRANSPOSE), TP(TP_INTERVAL)}},

    /* ARP: the held notes, one at a time on the clock */
    {SEC_ARP, V_ALL, PIC_ARP, PR_NONE, "ARP", {TP(TP_ARPMODE), TP(TP_ARPRATE), TP(TP_ARPOCT), TP(TP_ARPLATCH)}},   /* HOLD: works with the arp off too */
    {SEC_ARP, V_ALL, PIC_ARP, PR_NONE, "ARP 2", {TP(TP_ARPGATE), TP(TP_ARPREP), TP(TP_ARPCHNC), NO}},

    {SEC_SEQ, V_COMP, PIC_CLOCK, PR_NONE, "TEMPO", {GP(GP_TEMPO), GP(GP_SWING), NO, NO}},   /* (a COMP track has no steps) */
    {SEC_SEQ, V_KIT, PIC_DRUM, PR_PATTERN, "DRUM", {LF(LF_SEL), LF(LF_LEN), LF(LF_RATE), LF(LF_CHANCE)}},   /* (SWING: GLO > CLOCK) */
    {SEC_SEQ, V_KIT, PIC_DRUM, PR_PATTERN, "DRUM STEP", {SF(SF_ACC), SF(SF_VEL), SF(SF_CHANCE), SF(SF_RATCHET)}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "STEPS", {SF(SF_NOTE), SF(SF_VEL), SF(SF_LEN), SF(SF_CHANCE)}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "TEMPO", {GP(GP_TEMPO), GP(GP_SWING), TP(TP_LEN), TP(TP_RATE)}},   /* (the same TEMPO and SWING as GLO > CLOCK) */
    {SEC_SEQ, V_KIT, PIC_DRUM, PR_PATTERN, "TEMPO", {GP(GP_TEMPO), GP(GP_SWING), TP(TP_LEN), TP(TP_RATE)}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "STEP TIMING", {SF(SF_MICRO), SF(SF_RATCHET), SF(SF_RCURVE), SF(SF_COND)}},
    {SEC_SEQ, V_KIT, PIC_DRUM, PR_PATTERN, "STEP TIMING", {SF(SF_MICRO), SF(SF_RCURVE), SF(SF_COND), SF(SF_RMOVE)}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "STEP SHAPE", {SF(SF_ENV), SF(SF_CUT), SF(SF_TIE), SF(SF_RMOVE)}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "STEP CUT", {SF(SF_CUT), SF(SF_CSHAPE), SF(SF_CSIZE), NO}},
    {SEC_SEQ, V_NOTKIT, PIC_STEPS, PR_PATTERN, "TRACK", {TP(TP_SEQMODE), TP(TP_LEN), TP(TP_RATE), TP(TP_CLKSRC)}},
    {SEC_SEQ, V_KIT, PIC_DRUM, PR_PATTERN, "TRACK", {TP(TP_LEN), TP(TP_RATE), TP(TP_CLKSRC), NO}},
    {SEC_SEQ, V_STEPSMODE, PIC_STEPS, PR_PATTERN, "DIRECTION", {TP(TP_DIR), NO, NO, NO}},
    {SEC_SEQ, V_GRAPH, PIC_GRAPH, PR_STEPCUR, "GRAPH", {SF(SF_CORDA), SF(SF_CORDB), TP(TP_BRANCH), TP(TP_H2ROLE)}},
    {SEC_SEQ, V_GRAPH, PIC_GRAPH, PR_STEPCUR, "HEAD 1", {TP(TP_H1TRANS), NO, NO, NO}},
    {SEC_SEQ, V_GRAPH, PIC_MOTION, PR_NONE, "HEAD 1 MOTION", {TP(TP_H1SHAPE), TP(TP_H1DEPTH), TP(TP_H1CYCLE), TP(TP_H1COMB)}},
    {SEC_SEQ, V_GRAPH2, PIC_GRAPH, PR_STEPCUR, "HEAD 2", {TP(TP_H2TARGET), TP(TP_H2RATE), TP(TP_H2START), TP(TP_H2TRANS)}},
    {SEC_SEQ, V_GRAPH2, PIC_MOTION, PR_NONE, "HEAD 2 MOTION", {TP(TP_H2SHAPE), TP(TP_H2DEPTH), TP(TP_H2CYCLE), TP(TP_H2COMB)}},
    /* EUCLID: HITS spread as evenly as they go over the LENGTH, turned by ROTATE, written into the steps
     * as you turn (a kit: into the chosen lane), so they are ordinary steps from then on */
    {SEC_SEQ, V_NOTKIT, PIC_EUCLID, PR_PATTERN, "EUCLID", {{K_EUC, 0}, {K_EUC, 1}, TP(TP_LEN), NO}},
    {SEC_SEQ, V_KIT, PIC_EUCLID, PR_PATTERN, "EUCLID", {LF(LF_SEL), {K_EUC, 0}, {K_EUC, 1}, LF(LF_LEN)}},

    {SEC_SONG, V_ALL, PIC_MIXER, PR_NONE, "MIXER 1-4", {{K_TLEVEL, 0}, {K_TLEVEL, 1}, {K_TLEVEL, 2}, {K_TLEVEL, 3}}},
    {SEC_SONG, V_ALL, PIC_MIXER, PR_NONE, "MIXER 5-8", {{K_TLEVEL, 4}, {K_TLEVEL, 5}, {K_TLEVEL, 6}, {K_TLEVEL, 7}}},
    /* the clock right after the mixers: the tempo two pages in */
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK", {GP(GP_TEMPO), GP(GP_SWING), GP(GP_CLKMODE), GP(GP_CLKDEPTH)}},
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK B", {GP(GP_CLKSHAPE), GP(GP_CLKBRATE), GP(GP_CLKBSHAPE), GP(GP_CLKTHR)}},
    {SEC_SONG, V_ALL, PIC_CLOCK, PR_NONE, "CLOCK FEEL", {GP(GP_CLKLOCK), GP(GP_QUANT), GP(GP_METRO), GP(GP_COUNTIN)}},
    {SEC_SONG, V_ALL, PIC_MIDI, PR_NONE, "MIDI IN", {GP(GP_MIDICH), GP(GP_MIDICLK), GP(GP_MIDITRN), NO}},   /* USB and the TRS jack */
    {SEC_SONG, V_ALL, PIC_SCENES, PR_SCENE, "SCENES", {GP(GP_SONGMODE), {K_ARR, AF_STEMPO}, GP(GP_TEMPO), GP(GP_SCNGLIDE)}},
    {SEC_SONG, V_ALL, PIC_ARR, PR_ARR, "ARRANGE", {{K_ARR, AF_SCENE}, {K_ARR, AF_BARS}, {K_ARR, AF_ROWS}, {K_ARR, AF_MODE}}},
    {SEC_SONG, V_ALL, PIC_ARCS, PR_ARC, "ARCS", {{K_ARC, 0}, GP(GP_ARCSPAN), NO, NO}},
    {SEC_SONG, V_ALL, PIC_CPU, PR_NONE, "CPU", {{K_STRESS, 3}, {K_STRESS, 4}, NO, NO}},   /* where the audio's time goes; CORES */
    {SEC_SONG, V_ALL, PIC_STRESS, PR_NONE, "STRESS", {{K_STRESS, 0}, {K_STRESS, 1}, {K_STRESS, 2}, NO}},   /* how much this unit takes */
    {SEC_SONG, V_ALL, PIC_FLASH, PR_NONE, "FLASH", {NO, NO, NO, NO}},   /* the flash as the boot set it up (read only) */

    {SEC_SAVE, V_ALL, PIC_SAVE, PR_SLOT, "PROJECT", {{K_SAVE, 0}, {K_SAVE, 1}, {K_SAVE, 2}, NO}},   /* the action; the project's name, as SOUNDS names a sound */
    {SEC_SAVE, V_ALL, PIC_BANK, PR_BANK, "SOUNDS", {{K_BANK, 0}, {K_BANK, 1}, {K_BANK, 2}, {K_BANK, 3}}},   /* your own sounds, any kind, in banks A-D */
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
    uint8_t cz_row;                          /* the CZ envelope pages: stages 1-2, 3-4, 5-6, 7-8, or SUS and END (OCT) */
    uint8_t lane[RV_TRACKS];                 /* each KIT track's chosen lane */
    uint8_t lane_row;                        /* the LANE pages: which row of the lane's settings the knobs hold */
    uint8_t lfo_row;                         /* LFO SHAPE: LFO 1 or LFO 2 on the knobs */
    uint8_t lib_row;                         /* LIBRARY: segment 1-8 on the knobs, or (8) start, time, loop and next */
    uint32_t step_key_held;                  /* step keys held (bit = white key index) */
    uint8_t step_turned;
    uint8_t key_note[NKEYS];
    uint8_t key_trk[NKEYS];                  /* the track a key's note went to, + 1 (0: the key plays no note) */
    uint8_t cur_route, cur_mat, cur_macro, cur_arr, cur_arc, cur_scene, cur_slot, save_action, proj_cursor;
    uint16_t cur_lib;
    int16_t preset_idx[RV_TRACKS];
    char msg[24];
    uint32_t msg_until;
    uint32_t turned_ms[4];
    uint8_t armed;
    int8_t srec_t;                  /* step recording on this track (-1: off) */
    int8_t srec_seen;               /* the cursor last shown (the keys' page follows it) */
    uint8_t save_confirm;
    uint8_t cur_sound, bank_action, bank_cursor, bank_dirty;   /* SOUNDS: the slot, LOAD/SAVE/CLEAR, the name's cursor */
    uint32_t bank_edit_ms;                   /* a name edited: kept in flash once the naming pauses */
    char slot_names[PLAT_PROJECTS][13];
    uint8_t slot_used[PLAT_PROJECTS];
    int8_t room;                           /* the projects' room left, % (-1: not counted here) */
    int8_t euc_rot[RV_TRACKS][RV_LANES];     /* EUCLID: the last ROTATE turned to (a track's in [0], a kit's per lane) */
    uint32_t frame;
    uint8_t dust_hist[60];
    uint8_t dust_w;
    uint8_t conf_what;                       /* a destructive action waiting for its second try */
    /* the stress test: the step asked for and the one built, its engine, AUTO, whether its project is
     * the one in memory; what each step measured (% of the time, 255: not yet) and where XRUNS came */
    uint8_t st_step, st_built, st_eng, st_auto, st_on;
    uint8_t c2_tried;                         /* the second core was asked for at start */
    uint16_t st_res[RV_STRESS_N];               /* (0xFFFF: not measured yet) */
    uint8_t st_res2[RV_STRESS_N];               /* the second core's, % (0: not working) */
    uint32_t st_xr, st_t0, st_turn, st_x0;
    float st_acc, st_acc2;
    uint16_t st_nacc;
    uint32_t conf_at;
} ui;

static void bank_keep(void);

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

int rv_preset_is_kit(int i);
static int track_has_steps(int t)
{
    int p, i;
    for (p = 0; p < RV_PATTERNS; p++)
        for (i = 0; i < RV_STEPS; i++)
            if (rv.pat[t][p].s[i].flags & ST_ON)
                return 1;
    return 0;
}
static int track_has_pattern_steps(int t)          /* the current pattern only */
{
    int i;
    for (i = 0; i < RV_STEPS; i++)
        if (rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[i].flags & ST_ON)
            return 1;
    return 0;
}
/* an action that destroys steps: the first try only warns; the same action again, after a pause
 * (so one quick spin of a knob can't do it) and within 2.5 s, does it */
enum { CONF_KIND = 1, CONF_MODE, CONF_CLEAR, CONF_PRESET, CONF_PASTE, CONF_STRESS, CONF_SCENE };   /* (CONF_SCENE + the scene) */
static int confirmed(int what, const char *warn)
{
    uint32_t now = plat_ms(), dt = now - ui.conf_at;
    if (ui.conf_what == what && dt < 2500) {
        if (dt < 300)
            return 0;
        ui.conf_what = 0;
        return 1;
    }
    ui.conf_what = (uint8_t)what;
    ui.conf_at = now;
    msg(warn);
    return 0;
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
/* a LANE page's knob: the field it holds in the current row (A-D, E-H, then level, chance, length) */
static int lane_field(int id)
{
    static const uint8_t rows[3][4] = {{LF_P0, LF_P0 + 1, LF_P0 + 2, LF_P0 + 3}, {LF_P0 + 4, LF_P0 + 5, LF_P0 + 6, LF_P0 + 7},
                                       {LF_LEVEL, LF_CHANCE, LF_LEN, LF_RATE}};
    return id >= LF_SLOT0 ? rows[ui.lane_row % 3][(id - LF_SLOT0) & 3] : id;
}

static void lane_msg(int t);
static void lane_open(int t, int l);
static int is_kit_ui(int t) { return rv.tp[t][TP_KIND] == KIND_PERC && rv.tp[t][TP_ENGINE] == 1; }
/* a kit lane's machine */
static const char *lane_mname(int t, int l) { return perc_mnames[rv.kit[t].mach[l] % PM_N]; }
static const rv_pinfo *lane_pinfo(int t, int l, int f) { return &perc_pi[rv.kit[t].mach[l] % PM_N][f & 7]; }
static uint16_t lane_col(int t) { (void)t; return C_PERC; }

static int src_page_visible(int t, int pg)
{
    int s;
    for (s = pg * 4; s < pg * 4 + 4; s++)
        if (rv_pinfo_track(t, TP_S0 + s)->name)
            return 1;
    return 0;
}

/* a page every knob of which does nothing as the track is set (ENV on a drum machine, FILTER on strike) */
static int page_all_dead(int i, int t)
{
    int k, any = 0;
    for (k = 0; k < 4; k++) {
        kb_t b = pages[i].k[k];
        if (b.kind == K_NONE)
            continue;
        if (b.kind != K_TP || rv_tp_applies(t, b.id))
            return 0;
        any = 1;
    }
    return any;
}

/* OSC MIX: the engine's oscillator levels side by side (0: an engine with one source) */
static const uint8_t *osc_mix_map(int eng)
{
    static const uint8_t va[4] = {TP_OSC1, TP_S4, TP_S5, 0}, fm[4] = {TP_OSC1, TP_S6, TP_S7, TP_S8};
    static const uint8_t wv[4] = {TP_OSC1, TP_S15, 0, 0}, ch[4] = {TP_OSC1, TP_S8, 0, 0}, mo[4] = {TP_S0, TP_S1, TP_S2, TP_S4};
    switch (eng) {
    case ENG_VA: return va;
    case ENG_FM: return fm;
    case ENG_WAVE: return wv;
    case ENG_CHIP: return ch;
    case ENG_MONO: return mo;
    default: return 0;
    }
}

/* a page's knob, with the indirect ones resolved: LFO SHAPE's row, OSC MIX's engine */
static kb_t page_knob(const page_t *pg, int i)
{
    kb_t k = pg->k[i];
    if (k.kind == K_LFOR) {
        static const uint8_t g[2][3] = {{GP_LFO1SYM, GP_LFO1PHASE, GP_LFO1MODE}, {GP_LFO2SYM, GP_LFO2PHASE, GP_LFO2MODE}};
        k.kind = K_GP;
        k.id = g[ui.lfo_row & 1][k.id % 3];
    } else if (k.kind == K_CZ && (k.id & 15) >= CZF_SLOT0) {   /* a CZ envelope: two stages a row, then SUS and END */
        int slot = (k.id & 15) - CZF_SLOT0, row = ui.cz_row % 5, base = k.id & ~15;
        if (row < 4)
            k.id = (uint16_t)(base | ((2 * row + (slot >> 1)) << 9) | ((slot & 1) ? CZF_LEVEL : CZF_RATE));
        else if (slot < 2)
            k.id = (uint16_t)(base | (slot ? CZF_END : CZF_SUS));
        else
            k.kind = K_NONE;
    } else if (k.kind == K_OSC) {
        const uint8_t *m = osc_mix_map(rv.tp[ui.track][TP_ENGINE]);
        int p = m ? m[k.id & 3] : 0;
        k.kind = p ? K_TP : K_NONE;
        k.id = (uint16_t)p;
    } else if (k.kind == K_TP && pg->pic == PIC_SOURCE && rv.tp[ui.track][TP_KIND] == KIND_SYNTH && rv.tp[ui.track][TP_ENGINE] == ENG_WAVE) {
        if (k.id == TP_VELO)                          /* WAVE: MIX (table 2 in) where VELO sits on SOUND */
            k.id = TP_S6;
        else if (k.id == TP_S15 && pg->vis == V_SRC3)   /* VELO on SOURCE 4, ASIC on OSC MIX */
            k.id = TP_VELO;
    }
    return k;
}

static int page_visible(int i)
{
    int t = ui.track, kind = rv.tp[t][TP_KIND];
    if ((pages[i].sec == SEC_ARTIC || pages[i].pic == PIC_FILTER) && page_all_dead(i, t))
        return 0;
    if (is_kit_ui(t) && pages[i].sec == SEC_VOICE && pages[i].title[0] == 'P' && pages[i].title[1] == 'I')   /* PITCH: a lane's own */
        return 0;
    if (kind == KIND_COMP)                            /* COMP: its own pages where a voice's would be */
        return (pages[i].sec == SEC_VOICE || pages[i].sec == SEC_ARTIC || pages[i].sec == SEC_SEQ) ? pages[i].vis == V_COMP : pages[i].vis != V_COMP;
    if (pages[i].vis == V_COMP)
        return 0;
    switch (pages[i].vis) {
    case V_SRC0: return src_page_visible(t, 0);
    case V_SRC1: return src_page_visible(t, 1);
    case V_SRC2: return !is_kit_ui(t) && src_page_visible(t, 2);
    case V_SRC3: return !is_kit_ui(t) && src_page_visible(t, 3);
    case V_SYNTH: return kind == KIND_SYNTH;
    case V_STRIKE: return kind == KIND_STRIKE;
    case V_PKIT: return kind == KIND_PERC && is_kit_ui(t);
    case V_NOTTAPE: return 1;
    case V_STEPSMODE: return !is_kit_ui(t) && rv.tp[t][TP_SEQMODE] != SEQ_GRAPH && rv.tp[t][TP_DIR] != DIR_GRAPH;
    case V_GRAPH: return !is_kit_ui(t) && (rv.tp[t][TP_SEQMODE] == SEQ_GRAPH || rv.tp[t][TP_DIR] == DIR_GRAPH);
    case V_GRAPH2: return !is_kit_ui(t) && (rv.tp[t][TP_SEQMODE] == SEQ_GRAPH || rv.tp[t][TP_DIR] == DIR_GRAPH) && rv.tp[t][TP_H2ROLE] != ROLE_OFF;
    case V_CZL1: return kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ && rv.tp[t][TP_S0] != 1;   /* not LINE = 2 */
    case V_CZL2: return kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ && (rv.tp[t][TP_S0] == 1 || rv.tp[t][TP_S0] == 3);
    case V_NOTCZ: return !(kind == KIND_SYNTH && rv.tp[t][TP_ENGINE] == ENG_CZ);
    case V_KIT: return is_kit_ui(t);
    case V_NOTKIT: return !is_kit_ui(t);
    case V_SEM: return kind == KIND_SYNTH && rv.tp[t][TP_FTYPE] == FT_SEM;
    case V_OSCMIX: return kind == KIND_SYNTH && osc_mix_map(rv.tp[t][TP_ENGINE]) != 0;
    default: return 1;
    }
}

/* the visible pages of a section, in order */
#define SEC_PAGES_MAX 32                 /* the most pages one section can show (FX has 18, a two-line CZ's EDIT 20) */
static int sec_page_list(int sec, uint8_t *out)
{
    int i, n = 0;
    for (i = 0; i < NPAGES; i++)
        if (pages[i].sec == sec && page_visible(i) && n < SEC_PAGES_MAX)
            out[n++] = (uint8_t)i;
    return n;
}

static const page_t *cur_page(void)
{
    uint8_t list[SEC_PAGES_MAX];
    int n = sec_page_list(ui.sec, list);
    if (!n)
        return &pages[0];
    if (ui.page[ui.sec] >= n)
        ui.page[ui.sec] = (uint8_t)(n - 1);
    if (pages[list[ui.page[ui.sec]]].pic == PIC_LANE)   /* a LANE page is its lane */
        ui.lane[ui.track] = (uint8_t)((pages[list[ui.page[ui.sec]]].title[5] - '1') % RV_LANES);
    return &pages[list[ui.page[ui.sec]]];
}

/* a change that shows or hides pages (SHAPE on hides ONSET...) keeps you on the page you were on */
static void page_follow(int gp)
{
    uint8_t list[SEC_PAGES_MAX];
    int n = sec_page_list(ui.sec, list), i;
    for (i = 0; i < n; i++)
        if (list[i] == gp)
            ui.page[ui.sec] = (uint8_t)i;
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
static const char *const rcurve_names[] = {"EVEN", "FASTER", "SLOWER", "BOUNCE", "EVEN-F", "FAST-F", "SLOW-F"};
static const char *const rmove_names[] = {"NONE", "UP", "DOWN", "DARK"};
static const char *const cshape_names[] = {"EVEN", "BOUNCE", "DRAG"};
static const char *const csize_names[] = {"MASTER", "1/32", "1/16", "1/8", "1/6", "1/4", "1/3", "1/2"};
static const char *const cut_names[] = {"NONE", "LOOP", "REVERSE", "STUTTER"};
static const char *const save_actions[] = {"SAVE", "LOAD", "DEMO 1", "DEMO 2", "DEMO 3", "INIT"};
static const char *const bank_actions[] = {"LOAD", "SAVE", "CLEAR"};
static const char *const bank_letters[] = {"A", "B", "C", "D"};
static const char *const curve_names[] = {"LINEAR", "EXP", "LOG", "S"};
static const char *const lib_names[2][4] = {{"LEVEL", "TIME", "BEND", "SEGS"}, {"START", "TIME", "LOOP", "NEXT"}};
static const char *const lib_time[] = {"STEPS", "FREE"};
static const char *const lib_loop[] = {"OFF", "TO 1", "TO 2", "TO 3", "TO 4", "TO 5", "TO 6", "TO 7", "TO 8"};
static const char *const lib_next[RV_SENV + 1] = {"NONE", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
                                                  "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32"};

typedef struct { const char *name; int min, max; uint8_t fmt; const char *const *names; } kinfo_t;

static int macro_dest_count(void) { return RV_TRACKS * TP_N + GP_N; }

/* ------------------------------------------------------------ EUCLID --- */
static int euc_lane(int t) { return is_kit_ui(t) ? ui.lane[t] % RV_LANES : 0; }
static int euc_len(int t)                    /* the steps it spreads over: the track's LENGTH, or a kit lane's */
{
    int n = clampi(rv.tp[t][TP_LEN], 1, RV_STEPS);
    if (is_kit_ui(t) && rv.kit[t].len[euc_lane(t)])
        n = clampi(rv.kit[t].len[euc_lane(t)], 1, RV_STEPS);
    return n;
}
static int euc_on(int t, int i)
{
    const rv_step *s = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS].s[i];
    return is_kit_ui(t) ? (s->flags & ST_ON) && ((s->note >> euc_lane(t)) & 1) : (s->flags & ST_ON) != 0;
}
static int euc_hits(int t)                   /* how many play now (so HITS starts from the pattern as it is) */
{
    int i, n = 0, len = euc_len(t);
    for (i = 0; i < len; i++)
        n += euc_on(t, i);
    return n;
}
/* k hits over n steps, the first on step 1 (Bjorklund's pattern: step i plays when i*k mod n < k), turned */
static void euc_write(int t, int hits, int rot)
{
    int i, len = euc_len(t), l = euc_lane(t), pat = rv.pat_cur[t] % RV_PATTERNS;
    uint8_t bit = (uint8_t)(1u << l);
    hits = clampi(hits, 0, len);
    rot = ((rot % len) + len) % len;
    for (i = 0; i < len; i++) {
        rv_step *s = &rv.pat[t][pat].s[i];
        int j = (i - rot + len) % len, on = hits > 0 && (j * hits) % len < hits;
        if (is_kit_ui(t)) {
            if (on && !(s->note & bit)) {
                if (!(s->flags & ST_ON)) {           /* an empty step: start it clean */
                    s->note = s->env = s->len = 0;
                    s->vel = 100;
                    s->chance = 100;
                    s->ratchet = 0;
                    s->micro = 0;
                    s->cond = 0;
                }
                s->note |= bit;
            } else if (!on && (s->note & bit)) {
                s->note &= (uint8_t)~bit;
                s->env &= (uint8_t)~bit;
                s->len &= (uint8_t)~bit;
                rv_kitlock_clear(t, pat, i, l);
            }
            s->flags = (uint8_t)(s->note & 63 ? s->flags | ST_ON : s->flags & ~ST_ON);
        } else if (on) {
            s->flags |= ST_ON;
            if (s->vel == 0)
                s->vel = 100;
        } else {
            s->flags &= (uint8_t)~ST_ON;
        }
    }
}

static int stress_loaded(void);
static kinfo_t kb_info(kb_t k)
{
    kinfo_t r = {0, 0, 0, PF_NUM, 0};
    const rv_pinfo *pi;
    switch (k.kind) {
    case K_TP:
        pi = rv_pinfo_track(ui.track, k.id);
        r.name = pi->name;
        if (ui.sec == SEC_BUS && (k.id == TP_DLYS || k.id == TP_REVS || k.id == TP_FBS)) {
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
        if (k.id == TP_POLY && (rv.tp[ui.track][TP_KIND] == KIND_STRIKE || rv.tp[ui.track][TP_KIND] == KIND_PERC))
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
        static const char *const nm[] = {"NOTE", "VELO", "LENGTH", "CHANCE", "MICRO", "RATCHET", "R CURVE", "CONDITN", "ENV", "CUT", "TIE", "CORD A", "CORD B", "ACCENT", "R MOVE", "C SHAPE", "C SIZE"};
        r.name = nm[k.id];
        switch (k.id) {
        case SF_NOTE: r.max = 127; r.fmt = PF_NOTE; break;
        case SF_VEL: r.min = 1; r.max = 127; break;
        case SF_LEN: r.min = 1; r.max = RV_LEN_MAX; break;   /* to 64 steps */
        case SF_CHANCE: r.max = 100; break;
        case SF_MICRO: r.min = -6; r.max = 12; r.fmt = PF_BI; break;
        case SF_RATCHET: r.min = 1; r.max = 8; break;
        case SF_ACC: { static const char *const oo[] = {"OFF", "ON"}; r.max = 1; r.fmt = PF_ENUM; r.names = oo; break; }
        case SF_RCURVE: r.max = 6; r.fmt = PF_ENUM; r.names = rcurve_names; break;
        case SF_RMOVE: r.max = 3; r.fmt = PF_ENUM; r.names = rmove_names; break;
        case SF_CSHAPE: r.max = 2; r.fmt = PF_ENUM; r.names = cshape_names; break;
        case SF_CSIZE: r.max = 7; r.fmt = PF_ENUM; r.names = csize_names; break;
        case SF_COND: r.max = RV_NCOND - 1; r.fmt = PF_ENUM; r.names = rv_cond_names; break;
        case SF_ENV: r.max = RV_SENV; break;
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
            r.max = ro->type == RT_MOD ? TP_N - 1 : ro->type == RT_TRIG ? 16 : 0;
            r.name = ro->type == RT_MOD ? "PARAM" : ro->type == RT_TRIG ? "DELAY" : "-";
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
            r.max = RV_TRACKS + 1;                      /* T1-T8, GLOBAL, MATRIX (another row's AMOUNT) */
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
    case K_ARR: {
        static const char *const nm[] = {"SCENE", "BARS", "ROWS", "PLAYS", "S TEMPO"};
        static char sn[10] = "S1 TEMPO";
        r.name = nm[k.id];
        r.min = k.id == AF_MODE ? 0 : 1;
        r.max = k.id == AF_SCENE ? RV_ARR_END + 1 : k.id == AF_BARS ? 64 : k.id == AF_ROWS ? RV_ARR : 1;
        if (k.id == AF_STEMPO) {                     /* 29 shows KEEP: the scene leaves the tempo alone */
            int n = (rv.scene_cur & 15) + 1;
            sn[0] = 'S';
            sx_int(sn + 1, n);
            sx_cat(sn, n < 10 ? " TEMPO" : " TEMP");   /* "S3 TEMPO", "S12 TEMP" */
            r.name = sn;
            r.min = 29;
            r.max = 240;
        }
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
    case K_STRESS: {
        static const char *const autos[2] = {"OFF", "RUN"};
        static const char *const nums[RV_STRESS_N] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
        static const char *const cores[2] = {"1", "2"}, *const onoff[2] = {"OFF", "ON"};
        r.name = k.id == 0 ? "STEP" : k.id == 1 ? "ENGINE" : k.id == 2 ? "AUTO" : k.id == 3 ? "CORES" : "FLASH WT";
        r.max = k.id == 0 ? RV_STRESS_N - 1 : k.id == 1 ? ENG_N - 1 : 1;
        r.fmt = PF_ENUM;
        r.names = k.id == 0 ? nums : k.id == 1 ? rv_engine_names : k.id == 2 ? autos : k.id == 3 ? cores : onoff;
        break;
    }
    case K_EUC:
        r.name = k.id ? "ROTATE" : "HITS";
        r.max = euc_len(ui.track) - (k.id ? 1 : 0);
        break;
    case K_LIB: {                              /* the LIBRARY: a segment's LEVEL, TIME, BEND (and SEGS), or the envelope's own */
        int set = ui.lib_row >= RV_SENV_SEG;
        r.name = lib_names[set][k.id & 3];
        switch ((set << 2) | (k.id & 3)) {
        case 0: case 1: case 4: r.max = 127; break;
        case 2: r.min = -7; r.max = 7; r.fmt = PF_BI; break;
        case 3: r.max = RV_SENV_SEG; break;
        case 5: r.max = 1; r.fmt = PF_ENUM; r.names = lib_time; break;
        case 6: r.max = RV_SENV_SEG; r.fmt = PF_ENUM; r.names = lib_loop; break;
        default: r.max = RV_SENV; r.fmt = PF_ENUM; r.names = lib_next; break;
        }
        break;
    }
    case K_SAVE:
        if (k.id == 0) {
            r.name = "ACTION";
            r.max = 5;
            r.fmt = PF_ENUM;
            r.names = save_actions;
        } else {                                      /* the project's name: where, then which letter */
            r.name = k.id == 1 ? "CURSOR" : "LETTER";
            r.min = k.id == 1 ? 1 : 0;
            r.max = k.id == 1 ? (int)sizeof rv.name - 1 : rv_bank_nchars() - 1;
        }
        break;
    case K_BANK:                              /* SOUNDS: the action, then the name (a used slot's) */
        if (k.id == 0) {
            r.name = "ACTION";
            r.max = 2;
            r.fmt = PF_ENUM;
            r.names = bank_actions;
        } else if (k.id == 3) {
            r.name = "BANK";
            r.max = RV_BANKS - 1;
            r.fmt = PF_ENUM;
            r.names = bank_letters;
        } else if (rv_bank_used(ui.cur_sound)) {
            r.name = k.id == 1 ? "CURSOR" : "LETTER";
            r.min = k.id == 1 ? 1 : 0;
            r.max = k.id == 1 ? RV_BANK_NAME - 1 : rv_bank_nchars() - 1;
        }
        break;
    case K_LANE: {
        static const char *const lanes[RV_LANES] = {"1", "2", "3", "4", "5", "6"};
        int l = ui.lane[ui.track] % RV_LANES;
        switch (lane_field(k.id)) {
        case LF_NONE: break;
        case LF_SEL: r.name = "LANE"; r.max = RV_LANES - 1; r.fmt = PF_ENUM; r.names = lanes; break;
        case LF_MACH:
            r.name = "MACHINE"; r.max = PM_N - 1; r.fmt = PF_ENUM; r.names = perc_mnames;
            break;
        case LF_LEVEL: r.name = "LEVEL"; r.max = 127; break;
        case LF_LEN: {                       /* 0: the track's */
            static const char *const ln[RV_STEPS + 1] = {"TRK", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63", "64"};
            r.name = "LENGTH"; r.max = RV_STEPS; r.fmt = PF_ENUM; r.names = ln; break;
        }
        case LF_CHANCE: r.name = "CHANCE"; r.max = 100; break;
        case LF_RATE: r.name = "RATE"; r.max = 8; r.fmt = PF_ENUM; r.names = rv_lrate_names; break;
        default: {
            const rv_pinfo *pi = lane_pinfo(ui.track, l, lane_field(k.id) - LF_P0);
            r.name = pi->name;
            r.min = 0;
            r.max = 127;
            r.fmt = PF_NUM;
            r.names = 0;
            break;
        }
        }
        break;
    }
    case K_CZ: {
        static const char *const fn[] = {"WAVE 1", "WAVE 2", "DCA KF", "DCW KF", "RATE", "LEVEL", "SUS", "END"};
        static const char *const w1[] = {"SAW", "SQUARE", "PULSE", "DBL SIN", "SAW PLS", "RESO 1", "RESO 2", "RESO 3"};
        static const char *const w2[] = {"OFF", "SAW", "SQUARE", "PULSE", "DBL SIN", "SAW PLS", "RESO 1", "RESO 2", "RESO 3"};
        static const char *const sus[] = {"OFF", "1", "2", "3", "4", "5", "6", "7", "8"};
        static const char *const rn[8] = {"RATE 1", "RATE 2", "RATE 3", "RATE 4", "RATE 5", "RATE 6", "RATE 7", "RATE 8"};
        static const char *const ln[8] = {"LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4", "LEVEL 5", "LEVEL 6", "LEVEL 7", "LEVEL 8"};
        int f = k.id & 15, l = (k.id >> 8) & 1;
        r.name = f == CZF_RATE ? rn[(k.id >> 9) & 7] : f == CZF_LEVEL ? ln[(k.id >> 9) & 7] : fn[f & 7];
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
        case SF_RATCHET:                              /* a kit: the chosen lane's roll */
            if (is_kit_ui(ui.track) && !((s->len >> (ui.lane[ui.track] % RV_LANES)) & 1))
                return 1;
            return (s->ratchet & 7) + 1;
        case SF_ACC: return (s->env >> (ui.lane[ui.track] % RV_LANES)) & 1;
        case SF_RCURVE: return ((s->ratchet >> 3) & 3) == 3 ? 3 : ((s->ratchet >> 3) & 3) + ((s->ratchet & 32) ? 4 : 0);
        case SF_RMOVE: return (s->ratchet >> 6) & 3;
        case SF_COND: return s->cond;
        case SF_ENV: return s->env;
        case SF_CUT: return s->cut & 3;
        case SF_CSHAPE: return clampi((s->cut >> 2) & 3, 0, 2);
        case SF_CSIZE: return (s->cut >> 4) & 7;
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
            return m->dst >= RV_NPID ? RV_TRACKS + 1 : m->dst >= RV_GPID ? RV_TRACKS : m->dst / RV_TP;
        if (k.id == MF_PARAM)
            return m->dst >= RV_NPID ? m->dst - RV_NPID : m->dst >= RV_GPID ? m->dst - RV_GPID : m->dst % RV_TP;
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
    case K_ARR:
        if (k.id == AF_SCENE)
            return rv.arr[ui.cur_arr].scene + 1;
        if (k.id == AF_STEMPO)
            return rv.scene_tempo[rv.scene_cur & 15] ? rv.scene_tempo[rv.scene_cur & 15] : 29;
        if (k.id == AF_BARS)
            return rv.arr[ui.cur_arr].bars ? rv.arr[ui.cur_arr].bars : 1;
        if (k.id == AF_ROWS)
            return rv.arr_len ? rv.arr_len : 1;
        return rv.gp[GP_SONGMODE];
    case K_ARC: return rv.arcs[ui.track][ui.cur_arc & 15];
    case K_STRESS: return k.id == 0 ? ui.st_step : k.id == 1 ? ui.st_eng : k.id == 2 ? ui.st_auto : k.id == 3 ? rv_live.cores2 : rv_live.fetch_on;
    case K_EUC: return k.id ? clampi(ui.euc_rot[ui.track][euc_lane(ui.track)], 0, euc_len(ui.track) - 1) : euc_hits(ui.track);
    case K_LIB: {
        const rv_senv *e = &rv.senv[ui.cur_lib % RV_SENV];
        int sg = ui.lib_row % RV_SENV_SEG;
        if (ui.lib_row >= RV_SENV_SEG)
            return k.id == 0 ? e->start : k.id == 1 ? SENV_FREE(e) : k.id == 2 ? e->loop : e->next;
        return k.id == 0 ? e->lvl[sg] : k.id == 1 ? e->tim[sg] : k.id == 2 ? senv_crv(e, sg) : SENV_NSEG(e);
    }
    case K_CZ: {
        const rv_czline *pl = &rv.cz[ui.track].line[(k.id >> 8) & 1];
        const rv_czenv *e = &pl->env[clampi((k.id >> 4) & 15, 0, 2)];
        switch (k.id & 15) {
        case CZF_WAVE1: return pl->wave1;
        case CZF_WAVE2: return pl->wave2;
        case CZF_KFA: return pl->kf_dca;
        case CZF_KFW: return pl->kf_dcw;
        case CZF_RATE: return e->rate[(k.id >> 9) & 7];
        case CZF_LEVEL: return e->level[(k.id >> 9) & 7];
        case CZF_SUS: return e->sus;
        default: return e->end;
        }
    }
    case K_LANE: {
        const rv_kit *kt = &rv.kit[ui.track];
        int l = ui.lane[ui.track] % RV_LANES, held = held_step(), f = lane_field(k.id);
        if (held >= 0 && ((f >= LF_P0 && f < LF_P0 + 8) || f == LF_LEVEL)) {   /* a held step shows its lock */
            int i = rv_kitlock_find(ui.track, rv.pat_cur[ui.track] % RV_PATTERNS, held, l, f == LF_LEVEL ? KL_LEVEL : f - LF_P0);
            if (i >= 0)
                return rv.kitlk[ui.track][i].val;
        }
        switch (f) {
        case LF_NONE: return 0;
        case LF_SEL: return l;
        case LF_MACH: return kt->mach[l];
        case LF_LEVEL: return kt->level[l];
        case LF_LEN: return kt->len[l];
        case LF_CHANCE: return kt->chance[l];
        case LF_RATE: return RV_LRATE_KNOB(rv.kit_rate[ui.track][l] % 9);
        default: return kt->p[l][(f - LF_P0) & 7];
        }
    }
    case K_SAVE:
        return k.id == 0 ? ui.save_action : k.id == 1 ? ui.proj_cursor + 1
             : rv_bank_char_index(rv.name[ui.proj_cursor % (sizeof rv.name - 1)]);
    case K_BANK:
        return k.id == 0 ? ui.bank_action : k.id == 3 ? rv_bank_current() : k.id == 1 ? ui.bank_cursor + 1
             : rv_bank_char_index(rv_bank_name(ui.cur_sound)[ui.bank_cursor % (RV_BANK_NAME - 1)]);
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
            if (is_kit_ui(ui.track) != (v == KIND_PERC) && track_has_steps(ui.track) && !confirmed(CONF_KIND, "TURN AGAIN: CLEARS STEPS"))
                return;
            rv_track_set_kind(ui.track, v);
            ui.preset_idx[ui.track] = -1;
            return;
        }
        if (k.id == TP_S0 && v != rv.tp[ui.track][TP_S0] && rv.tp[ui.track][TP_KIND] == KIND_SYNTH && (rv.tp[ui.track][TP_ENGINE] == ENG_RESIN || rv.tp[ui.track][TP_ENGINE] == ENG_TINE)) {
            int p;                                   /* RESIN, TINE: another model starts from its own settings */
            rv.tp[ui.track][TP_S0] = (int16_t)v;
            for (p = TP_S1; p <= TP_S15; p++) {
                const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
                rv.tp[ui.track][p] = pi->name ? pi->def : 0;
            }
            return;
        }
        if (k.id == TP_S0 && v != rv.tp[ui.track][TP_S0] && rv.tp[ui.track][TP_KIND] == KIND_PERC) {
            int p;                                   /* a new machine starts from its own defaults */
            rv.tp[ui.track][TP_S0] = (int16_t)v;
            for (p = TP_S1; p <= TP_S8; p++) {
                const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
                rv.tp[ui.track][p] = pi->name ? pi->def : 0;
            }
            return;
        }
        if (k.id == TP_ENGINE && v != rv.tp[ui.track][TP_ENGINE] && rv.tp[ui.track][TP_KIND] == KIND_SYNTH) {
            int p;
            rv.tp[ui.track][TP_ENGINE] = (int16_t)v;
            for (p = TP_S0; p <= TP_S15; p++)            /* (first cleared: a knob's table may follow the old engine's MODEL) */
                rv.tp[ui.track][p] = 0;
            for (p = TP_S0; p <= TP_S15; p++) {
                const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
                rv.tp[ui.track][p] = pi->name ? pi->def : 0;
            }
            if (v == ENG_CZ)
                rv_cz_init(ui.track);
            if (v == ENG_ORGAN || v == ENG_MONO || v == ENG_TINE)
                rv_engine_track_defaults(ui.track, v);
            return;
        }
        if (k.id == TP_ENGINE && rv.tp[ui.track][TP_KIND] == KIND_PERC && v != rv.tp[ui.track][TP_ENGINE]) {
            int pt = rv.pat_cur[ui.track], p;            /* notes mean nothing to lanes, or lanes to notes: start clean */
            if (track_has_steps(ui.track) && !confirmed(CONF_MODE, "TURN AGAIN: CLEARS STEPS"))
                return;
            rv.tp[ui.track][TP_ENGINE] = (int16_t)v;
            if (!v)                                       /* SINGLE: the machine starts from its own defaults */
                for (p = TP_S1; p <= TP_S8; p++) {
                    const rv_pinfo *pi = rv_pinfo_track(ui.track, p);
                    rv.tp[ui.track][p] = pi->name ? pi->def : 0;
                }
            for (rv.pat_cur[ui.track] = 0; rv.pat_cur[ui.track] < RV_PATTERNS; rv.pat_cur[ui.track]++)
                rv_pattern_clear(ui.track);
            rv.pat_cur[ui.track] = (uint8_t)pt;
            msg(v ? "KIT: STEPS CLEARED" : "SINGLE: STEPS CLEARED");
            return;
        }
        if (rv_pinfo_track(ui.track, k.id)->fmt == PF_DEST && v != rv.tp[ui.track][k.id]) {   /* a destination: only what can be moved */
            int cur = rv.tp[ui.track][k.id];
            v = rv_moddest_next_t(ui.track, cur, v > cur ? 1 : -1);
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
        case SF_ACC:                                 /* a kit: the chosen lane's hit, full velocity */
            if (v)
                s->env |= (uint8_t)(1u << (ui.lane[ui.track] % RV_LANES));
            else
                s->env &= (uint8_t)~(1u << (ui.lane[ui.track] % RV_LANES));
            break;
        case SF_RATCHET:
            s->ratchet = (uint8_t)((s->ratchet & ~7) | (v - 1));
            if (is_kit_ui(ui.track)) {                /* a kit: the chosen lane ratchets on this step */
                if (v > 1)
                    s->len |= (uint8_t)(1u << (ui.lane[ui.track] % RV_LANES));
                else
                    s->len &= (uint8_t)~(1u << (ui.lane[ui.track] % RV_LANES));
            }
            break;
        case SF_RCURVE:                               /* EVEN FASTER SLOWER BOUNCE, then the first three fading */
            s->ratchet = (uint8_t)((s->ratchet & (7 | 192)) | ((v == 3 ? 3 : v % 4) << 3) | (v >= 4 ? 32 : 0));
            break;
        case SF_RMOVE: s->ratchet = (uint8_t)((s->ratchet & 63) | ((v & 3) << 6)); break;
        case SF_COND: s->cond = (uint8_t)v; break;
        case SF_ENV: s->env = (uint8_t)(v > 255 ? 255 : v); break;
        case SF_CUT: s->cut = (uint8_t)((s->cut & ~3) | (v & 3)); break;
        case SF_CSHAPE: s->cut = (uint8_t)((s->cut & ~12) | ((v & 3) << 2)); break;
        case SF_CSIZE: s->cut = (uint8_t)((s->cut & ~112) | ((v & 7) << 4)); break;
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
                r->arg = (uint8_t)rv_moddest_next_t(ui.cur_route & 7, r->arg, v > r->arg ? 1 : -1);
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
            if (v > RV_TRACKS)                          /* MATRIX: the next row's AMOUNT */
                m->dst = (uint16_t)PID_MAT((ui.cur_mat + 1) % RV_MATRIX);
            else
                m->dst = (uint16_t)(v == RV_TRACKS ? PID_G(GP_FBREGEN) : PID_T(v, rv_is_moddest(p) ? p : TP_CUT));
        } else if (k.id == MF_PARAM) {
            if (m->dst >= RV_NPID) {                    /* which row: any but this one */
                int cur = m->dst - RV_NPID, nk = clampi(v, 0, RV_MATRIX - 1);
                if (nk == ui.cur_mat)
                    nk = clampi(nk + (v > cur ? 1 : -1), 0, RV_MATRIX - 1);
                if (nk == ui.cur_mat)
                    nk = cur;
                m->dst = (uint16_t)PID_MAT(nk);
            } else if (m->dst >= RV_GPID) {             /* GLOBAL: the next that can be moved */
                int cur = m->dst - RV_GPID, g = clampi(v, 0, GP_N - 1), dir = v > cur ? 1 : -1;
                while (g >= 0 && g < GP_N && !rv_is_moddest_g(g))
                    g += dir;
                if (g >= 0 && g < GP_N)
                    m->dst = (uint16_t)PID_G(g);
            } else {
                int cur = m->dst % RV_TP;
                m->dst = (uint16_t)PID_T(m->dst / RV_TP, rv_moddest_next_t(m->dst / RV_TP, cur, v > cur ? 1 : -1));
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
                if (pid >= RV_GPID ? rv_pinfo_pid(pid)->name != 0 && rv_is_moddest_g(pid - RV_GPID) : (rv_is_moddest(pid % RV_TP) && rv_pinfo_pid(pid)->name))
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
    case K_ARR:
        if (k.id == AF_SCENE)
            rv.arr[ui.cur_arr].scene = (uint8_t)(v - 1);
        else if (k.id == AF_STEMPO) {
            rv.scene_tempo[rv.scene_cur & 15] = (uint8_t)(v < 30 ? 0 : v);
            if (v >= 30)
                rv.gp[GP_TEMPO] = (int16_t)v;        /* hear it now */
        }
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
    case K_STRESS:
        if (k.id == 4) {                               /* FLASH WAIT: count the code fetch's waits */
            if (plat_fetch_on(v) < 0) {
                msg("NOT MEASURED HERE: THE FM-1 COUNTS IT");
                break;
            }
            rv_live.fetch_on = (uint8_t)v;
            rv_live.fetch_pct = 0.0f;
            break;
        }
        if (k.id == 3) {                               /* CORES: the second core on or off (until power-off) */
            if (v && !rv_live.cores2) {
                if (plat_par_on(1) < 0) {
                    msg(rv_live.c2_fail ? "CORE 2 FAILED: OFF TILL POWER-OFF" : "CORE 2 CAN'T START NOW");
                    break;
                }
                rv_live.c2_fail = 0;
                rv_live.cores2 = 1;
                msg("TWO CORES: HALF THE SYNTH VOICES ON CORE 2");
            } else if (!v && rv_live.cores2) {
                rv_live.cores2 = 0;
                plat_par_on(0);
            }
            break;
        }
        if ((!ui.st_on || (ui.st_built == 0 && !stress_loaded())) && v > 0 && k.id != 1) {   /* starting: it replaces the project */
            if (!confirmed(CONF_STRESS, "TURN AGAIN: STRESS REPLACES THE PROJECT"))
                break;
            ui.st_on = 1;
            __builtin_memset(ui.st_res, 0xFF, sizeof ui.st_res);
            __builtin_memset(ui.st_res2, 0, sizeof ui.st_res2);
            ui.st_xr = 0;
        }
        if (k.id == 0)
            ui.st_step = (uint8_t)v;
        else if (k.id == 1)
            ui.st_eng = (uint8_t)v;
        else {
            ui.st_auto = (uint8_t)v;
            if (v) {                                   /* from the first step, the results anew */
                ui.st_step = 1;
                __builtin_memset(ui.st_res, 0xFF, sizeof ui.st_res);
                __builtin_memset(ui.st_res2, 0, sizeof ui.st_res2);
                ui.st_xr = 0;
            } else
                ui.st_step = 0;
        }
        if (k.id == 1 && ui.st_step && stress_loaded())
            ui.st_built = 255;                         /* the same step again, with this engine */
        ui.st_turn = plat_ms();
        break;
    case K_EUC:
        if (k.id)
            ui.euc_rot[ui.track][euc_lane(ui.track)] = (int8_t)v;
        euc_write(ui.track, k.id ? euc_hits(ui.track) : v, ui.euc_rot[ui.track][euc_lane(ui.track)]);
        break;
    case K_LIB: {
        rv_senv *e = &rv.senv[ui.cur_lib % RV_SENV];
        int sg = ui.lib_row % RV_SENV_SEG;
        if (ui.lib_row >= RV_SENV_SEG) {
            if (k.id == 0) e->start = (uint8_t)v;
            else if (k.id == 1) e->flags = (uint8_t)((e->flags & 15) | (v ? 16 : 0));
            else if (k.id == 2) e->loop = (uint8_t)v;
            else e->next = (uint8_t)v;
            break;
        }
        if (k.id == 3) {
            e->flags = (uint8_t)((e->flags & 16) | v);
            break;
        }
        if (k.id == 0) e->lvl[sg] = (uint8_t)v;
        else if (k.id == 1) e->tim[sg] = (uint8_t)v;
        else senv_set_crv(e, sg, v);
        if (SENV_NSEG(e) < sg + 1)                    /* editing a segment past the end brings it in */
            e->flags = (uint8_t)((e->flags & 16) | (sg + 1));
        break;
    }
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
        case CZF_RATE: e->rate[(k.id >> 9) & 7] = (uint8_t)v; break;
        case CZF_LEVEL: e->level[(k.id >> 9) & 7] = (uint8_t)v; break;
        case CZF_SUS: e->sus = (uint8_t)(v > e->end ? e->end : v); break;   /* the sustain step is never after the end */
        default:
            e->end = (uint8_t)v;
            if (e->sus > e->end)
                e->sus = e->end;
            break;
        }
        break;
    }
    case K_LANE: {
        rv_kit *kt = &rv.kit[ui.track];
        int l = ui.lane[ui.track] % RV_LANES, i, f = lane_field(k.id);
        switch (f) {
        case LF_NONE: break;
        case LF_SEL: ui.lane[ui.track] = (uint8_t)v; break;
        case LF_MACH:                                /* a new machine starts from its own defaults */
            if (v != kt->mach[l]) {
                kt->mach[l] = (uint8_t)v;
                for (i = 0; i < 8; i++)
                    kt->p[l][i] = (uint8_t)(perc_pi[v][i].name ? perc_pi[v][i].def : 0);
            }
            break;
        case LF_LEVEL: kt->level[l] = (uint8_t)v; break;
        case LF_LEN: kt->len[l] = (uint8_t)v; break;
        case LF_CHANCE: kt->chance[l] = (uint8_t)v; break;
        case LF_RATE: rv.kit_rate[ui.track][l] = (uint8_t)RV_LRATE_STORE(v); break;
        default: kt->p[l][(f - LF_P0) & 7] = (uint8_t)v; break;
        }
        break;
    }
    case K_SAVE:
        if (k.id == 0)
            ui.save_action = (uint8_t)v;
        else if (k.id == 1)
            ui.proj_cursor = (uint8_t)(v - 1);
        else {                                        /* a letter at the cursor (no holes before it; trailing spaces go) */
            static const char bc[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+#&'.";
            int pos = ui.proj_cursor % (int)(sizeof rv.name - 1), j;
            rv.name[sizeof rv.name - 1] = 0;
            for (j = 0; j < pos; j++)
                if (!rv.name[j])
                    rv.name[j] = ' ';
            rv.name[pos] = bc[clampi(v, 0, (int)sizeof bc - 2)];
            for (j = (int)sizeof rv.name - 2; j >= 0 && (rv.name[j] == ' ' || !rv.name[j]); j--)
                rv.name[j] = 0;
        }
        ui.save_confirm = 0;
        break;
    case K_BANK:
        if (k.id == 0)
            ui.bank_action = (uint8_t)v;
        else if (k.id == 3) {
            bank_keep();
            rv_bank_select(v);
            ui.bank_cursor = 0;
        } else if (k.id == 1)
            ui.bank_cursor = (uint8_t)(v - 1);
        else {
            rv_bank_set_char(ui.cur_sound, ui.bank_cursor, v);
            ui.bank_dirty = 1;
            ui.bank_edit_ms = plat_ms();
        }
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
    if (k.kind == K_ARR && ((k.id == AF_SCENE && v > RV_ARR_END) || (k.id == AF_STEMPO && v < 30))) {
        sx_cat(o, k.id == AF_SCENE ? "END" : "KEEP");
        return;
    }
    if (k.kind == K_TP && k.id == TP_INA && rv.tp[ui.track][TP_INS] == INS_COMB) {
        sx_note(o, 60 + v - 64);                    /* the comb's pitch: 64 = C4 */
        return;
    }
    if ((k.kind == K_BANK || k.kind == K_SAVE) && k.id == 2) {      /* the letter at the cursor */
        static const char bc[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+#&'.";
        if (v <= 0)
            sx_cat(o, "SPACE");
        else
            o[0] = bc[v % (int)(sizeof bc - 1)], o[1] = 0;
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
        if (v > 128) {                               /* past 16 steps: half steps */
            whole = 16 + (v - 128) / 2;
            rem = (v - 128) & 1 ? 4 : 0;
        }
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
    if (k.kind == K_STEP && k.id == SF_ENV) {      /* the LIBRARY envelope the step starts (STEP ENV) */
        if (!v)
            sx_cat(o, "-");
        else
            sx_int(o, v);
        return;
    }
    if (k.kind == K_LIB && k.id == 1 && ui.lib_row < RV_SENV_SEG) {   /* a segment's time: steps (in eighths), or FREE */
        if (SENV_FREE(&rv.senv[ui.cur_lib % RV_SENV]))
            rv_param_format(PID_T(ui.track, TP_ATK), v, o, 12);
        else {
            static const char *const fr[8] = {"", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8"};
            if (v / 8 || !(v % 8))
                sx_int(o, v / 8);
            if (v % 8) {
                if (v / 8)
                    sx_cat(o, " ");
                sx_cat(o, fr[v % 8]);
            }
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
        else if (r->type == RT_TRIG) {
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
        if (v > RV_TRACKS)
            sx_cat(o, "MATRIX");
        else if (v == RV_TRACKS)
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
    kb_t k = page_knob(pg, i);
    int held = held_step();
    uint32_t btn = plat_buttons();
    if (btn & (1u << B_LFO)) {                    /* LFO held: the macros */
        rv.gp[GP_MACRO1 + i] = (int16_t)clampi(rv.gp[GP_MACRO1 + i] + d, 0, 127);
        ui.layer_used[B_LFO] = 1;
        ui.turned_ms[i] = plat_ms();
        return;
    }
    if (btn & (1u << B_ENV) && held >= 0 && i == 0 && !is_kit_ui(ui.track)) {   /* ENV held with a step: its library shape */
        rv_step *s = step_ptr(held);
        s->env = (uint8_t)clampi(s->env + d, 0, RV_SENV);
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
    if (held >= 0 && k.kind == K_LANE && ((lane_field(k.id) >= LF_P0 && lane_field(k.id) < LF_P0 + 8) || lane_field(k.id) == LF_LEVEL)) {
        /* a kit step held (SEQ held on a LANE page): turning the lane's sound locks it for that lane on that step */
        int l = ui.lane[ui.track] % RV_LANES, pat = rv.pat_cur[ui.track] % RV_PATTERNS;
        int f = lane_field(k.id) == LF_LEVEL ? KL_LEVEL : lane_field(k.id) - LF_P0;
        if (!rv_kitlock_set(ui.track, pat, held, l, f, clampi(kb_get(k) + d, 0, 127)))
            msg("LOCKS FULL (48 A KIT)");
        else {
            rv_step *s = step_ptr(held);               /* a lock on an empty lane-step puts the hit there */
            if (!(s->flags & ST_ON)) {
                s->note = s->env = s->len = 0;
                s->vel = 100;
                s->chance = 100;
                s->ratchet = 0;
                s->micro = 0;
                s->cond = 0;
            }
            s->note |= (uint8_t)(1u << l);
            s->flags |= ST_ON;
        }
        ui.step_turned = 1;
        if (btn & (1u << B_SEQ))
            ui.layer_used[B_SEQ] = 1;
        ui.turned_ms[i] = plat_ms();
        return;
    }
    if (k.kind == K_STEP && held >= 0)
        ui.step_turned = 1;
    {
        int gp = (int)(cur_page() - pages), nv = kb_get(k) + d;
        /* (the tape is gone: KIND and a route's TYPE step over its old values, kept for old projects;
         * COMP is T8's alone) */
        if (((k.kind == K_TP && k.id == TP_KIND && nv == KIND_TAPE) || (k.kind == K_ROUTE && k.id == RF_TYPE && nv == RT_TAPE)))
            nv += d > 0 ? 1 : -1;
        if (k.kind == K_TP && k.id == TP_KIND && nv >= KIND_COMP && ui.track != 7)
            nv = KIND_PERC;
        kb_set(k, nv);
        page_follow(gp);
    }
    ui.turned_ms[i] = plat_ms();
}

/* SOUNDS: a name being edited goes to flash once the naming pauses (or the slot changes) */
static void bank_keep(void)
{
    if (ui.bank_dirty) {
        ui.bank_dirty = 0;
        rv_bank_flush();
    }
}

/* SOUNDS: SAVE does the action. LOAD and SAVE into an empty slot at once; overwriting, clearing, or
 * a load that would clear the track's steps (between a kit and notes) want a second press */
static void bank_do(void)
{
    int t = ui.track, i = ui.cur_sound, used = rv_bank_used(i);
    char b[24];
    bank_keep();
    switch (ui.bank_action) {
    case 0:
        if (!used) {
            msg("SLOT EMPTY");
            break;
        }
        if (rv_bank_is_kit(i) != is_kit_ui(t) && track_has_steps(t) && !ui.save_confirm) {
            ui.save_confirm = 1;
            msg("AGAIN: CLEARS STEPS");
            return;
        }
        rv_bank_apply(t, i);
        ui.preset_idx[t] = -1;
        msg(rv_bank_name(i)[0] ? rv_bank_name(i) : "LOADED");
        break;
    case 1:
        if (used && !ui.save_confirm) {
            ui.save_confirm = 1;
            msg("AGAIN: REPLACE IT");
            return;
        }
        if (used)
            rv_bank_store(t, i, 0);               /* (an overwritten slot keeps its name) */
        else {
            int kind = rv.tp[t][TP_KIND], e = rv.tp[t][TP_ENGINE];
            b[0] = 0;
            if (ui.preset_idx[t] >= 0 && ui.preset_idx[t] < USND)
                sx_cat(b, rv_preset_name(ui.preset_idx[t]));
            else {
                sx_cat(b, kind == KIND_SYNTH ? rv_engine_names[e % 12] : kind == KIND_STRIKE ? "STRIKE"
                          : e == 1 ? "KIT" : "PERC");
                sx_cat(b, " ");
                sx_int(b + sx_cat(b, bank_letters[rv_bank_current()]), i + 1);
            }
            b[RV_BANK_NAME - 1] = 0;
            rv_bank_store(t, i, b);
            ui.bank_cursor = 0;
        }
        msg("SOUND SAVED");
        break;
    default:
        if (!used) {
            msg("SLOT EMPTY");
            break;
        }
        if (!ui.save_confirm) {
            ui.save_confirm = 1;
            msg("AGAIN: CLEAR IT");
            return;
        }
        rv_bank_clear(i);
        msg("SOUND CLEARED");
        break;
    }
    ui.save_confirm = 0;
}

/* PRESETS on the voice pages: the factory sounds of the track's kind, then your own of that kind in the
 * bank chosen on SAVE > SOUNDS (ui.preset_idx at or above USND), round and round */
static int usnd_ok(int t, int b) { return rv_bank_used(b) && rv_bank_kind(b) == rv.tp[t][TP_KIND]; }
static int fact_next(int t, int f, int dir)
{
    for (f += dir; f >= 0 && f < rv_preset_count(); f += dir)
        if (rv_preset_kind(f) == rv.tp[t][TP_KIND])
            return f;
    return -1;
}
static int usnd_next(int t, int b, int dir)
{
    for (b += dir; b >= 0 && b < RV_BANK_N; b += dir)
        if (usnd_ok(t, b))
            return b;
    return -1;
}
static int sound_step(int t, int cur, int dir)
{
    int r;
    if (dir > 0) {
        if (cur < USND) {
            if ((r = fact_next(t, cur, 1)) >= 0)
                return r;
            if ((r = usnd_next(t, -1, 1)) >= 0)
                return USND + r;
            return fact_next(t, -1, 1);
        }
        if ((r = usnd_next(t, cur - USND, 1)) >= 0)
            return USND + r;
        return (r = fact_next(t, -1, 1)) >= 0 ? r : cur;
    }
    if (cur >= USND) {
        if ((r = usnd_next(t, cur - USND, -1)) >= 0)
            return USND + r;
        return (r = fact_next(t, rv_preset_count(), -1)) >= 0 ? r : cur;
    }
    if (cur >= 0 && (r = fact_next(t, cur, -1)) >= 0)
        return r;
    if ((r = usnd_next(t, RV_BANK_N, -1)) >= 0)
        return USND + r;
    return fact_next(t, rv_preset_count(), -1);
}

static void preset_turn(int d)
{
    const page_t *pg = cur_page();
    int t = ui.track;
    switch (pg->pr) {
    case PR_SOUND:
    case PR_CZSTEP: {                                 /* (the CZ envelope pages: OCT walks the stages, PRESETS the sounds) */
        int cur = ui.preset_idx[t], i, kit;
        int n = d > 0 ? d : -d;
        for (i = 0; i < n; i++)
            cur = sound_step(t, cur, d > 0 ? 1 : -1);
        if (cur < 0)
            break;
        kit = cur >= USND ? rv_bank_is_kit(cur - USND) : rv_preset_is_kit(cur);
        if (kit != is_kit_ui(t) && track_has_steps(t) && !confirmed(CONF_PRESET, "TURN AGAIN: CLEARS STEPS"))
            break;
        if (cur >= USND) {                            /* one of your own: the bank chosen on SAVE > SOUNDS */
            char b[24];
            if (rv_bank_apply(t, cur - USND))
                break;
            b[0] = 0;
            sx_int(b + sx_cat(b, bank_letters[rv_bank_current()]), cur - USND + 1);
            sx_cat(b, " ");
            sx_cat(b, rv_bank_name(cur - USND));
            msg(b);
        } else {
            rv_preset_apply(t, cur);
            msg(rv_preset_name(cur));
        }
        ui.preset_idx[t] = (int16_t)cur;
        break;
    }
    case PR_LANE: {                                   /* a LANE page: PRESETS walks the machines */
        rv_kit *kt = &rv.kit[t];
        int l = ui.lane[t] % RV_LANES, m = clampi(kt->mach[l] + d, 0, PM_N - 1), j;
        if (m != kt->mach[l]) {
            kt->mach[l] = (uint8_t)m;
            for (j = 0; j < 8; j++)
                kt->p[l][j] = (uint8_t)(perc_pi[m][j].name ? perc_pi[m][j].def : 0);
        }
        lane_msg(t);
        break;
    }
    case PR_ROUTE: ui.cur_route = (uint8_t)clampi(ui.cur_route + d, 0, 7); break;
    case PR_MAT: ui.cur_mat = (uint8_t)clampi(ui.cur_mat + d, 0, RV_MATRIX - 1); break;
    case PR_MACRO: ui.cur_macro = (uint8_t)clampi(ui.cur_macro + d, 0, RV_MACROS * RV_MACRO_DESTS - 1); break;
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
    case PR_LIB: ui.cur_lib = (uint16_t)clampi(ui.cur_lib + d, 0, RV_SENV - 1); break;
    case PR_SLOT: ui.cur_slot = (uint8_t)clampi(ui.cur_slot + d, 0, PLAT_PROJECTS - 1); ui.save_confirm = 0; break;
    case PR_BANK: {                                   /* the slots, on through the banks: A32 then B1 */
        int at = clampi(rv_bank_current() * RV_BANK_N + ui.cur_sound + d, 0, RV_BANKS * RV_BANK_N - 1);
        bank_keep();
        rv_bank_select(at / RV_BANK_N);
        ui.cur_sound = (uint8_t)(at % RV_BANK_N);
        ui.save_confirm = 0;
        ui.bank_cursor = 0;
        break;
    }
    case PR_STEPCUR: ui.step_cur = (int16_t)clampi(ui.step_cur + d, 0, clampi(rv.tp[t][TP_LEN], 1, 64) - 1); break;
    default: break;
    }
}

static void slots_refresh(void)
{
    int i, pct;
    for (i = 0; i < PLAT_PROJECTS; i++)
        ui.slot_used[i] = (uint8_t)rv_slot_used(PLAT_PROJ(i), ui.slot_names[i]);
    ui.room = (int8_t)(plat_proj_room(&pct) < 0 ? -1 : pct);
}

static void save_do(void)
{
    char b[24];
    b[0] = 0;
    switch (ui.save_action) {
    case 0: {
        int rc = rv_save(PLAT_PROJ(ui.cur_slot));
        if (rc == 0) {
            sx_cat(b, "SAVED TO ");
            sx_int(b + 9, ui.cur_slot + 1);
            msg(b);
        } else
            msg(rc == -2 ? "NO ROOM: SAVE OVER ANOTHER" : "SAVE FAILED");
        break;
    }
    case 1:
        rv_transport(0);
        if (rv_load(PLAT_PROJ(ui.cur_slot)) == 0) {
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
        uint8_t list[SEC_PAGES_MAX];
        int n = sec_page_list(s, list), p = n ? (ui.page[s] + 1) % n : 0;
        if (s == SEC_BUS && n)                       /* FX: on to the next bus, not just the next page */
            while (p && pages[list[p]].pic == pages[list[(p + n - 1) % n]].pic)
                p = (p + 1) % n;
        ui.page[s] = (uint8_t)p;
    } else {
        ui.sec = (uint8_t)s;
    }
    if (s == SEC_SAVE)
        slots_refresh();
}

/* open a kit lane's EDIT page */
static void lane_open(int t, int l)
{
    uint8_t list[SEC_PAGES_MAX];
    int n = sec_page_list(SEC_VOICE, list), i;
    ui.lane[t] = (uint8_t)l;
    for (i = 0; i < n; i++)
        if (pages[list[i]].pic == PIC_LANE && pages[list[i]].title[5] - '1' == l) {
            ui.sec = SEC_VOICE;
            ui.page[SEC_VOICE] = (uint8_t)i;
            break;
        }
    lane_msg(t);
}

static void lane_msg(int t)
{
    static char b[24];
    int l = ui.lane[t] % RV_LANES;
    b[0] = 'L';
    b[1] = 'A';
    b[2] = 'N';
    b[3] = 'E';
    b[4] = ' ';
    b[5] = (char)('1' + l);
    b[6] = ':';
    b[7] = ' ';
    b[8] = 0;
    sx_cat(b, lane_mname(t, l));
    msg(b);
}


/* a key went down (on) or up */
/* on a SEQ page the keys are the steps, except while recording on a synth or strike track:
 * then they play (and record) notes, and holding SEQ makes them steps again */
static int seq_keys_play(int t)
{
    int kind = rv.tp[t][TP_KIND];
    return ui.sec == SEC_SEQ && ui.armed && !(plat_buttons() & (1u << B_SEQ)) && (kind == KIND_SYNTH || kind == KIND_STRIKE);
}

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
            if (ui.step_key_held & (1u << wi)) {   /* a step key: a tap toggles the step */
                ui.step_key_held &= ~(1u << wi);
                if (!ui.step_turned && ui.srec_t == t) {   /* step recording: SEQ and a step put the cursor there */
                    rv_step_rec(SREC_TO, t, ui.step_page * 16 + wi);
                    return;
                }
                if (!ui.step_turned) {
                    rv_step *s = step_ptr(ui.step_page * 16 + wi);
                    if (is_kit_ui(t)) {
                        uint8_t bit = (uint8_t)(1u << (ui.lane[t] % RV_LANES));
                        if (!(s->flags & ST_ON)) {     /* an empty step: start it clean */
                            s->note = s->env = s->len = 0;
                            s->vel = 100;
                            s->chance = 100;
                            s->ratchet = 0;
                            s->micro = 0;
                            s->cond = 0;
                        }
                        if (!(s->note & bit))          /* a kit lane: a tap on, a tap off */
                            s->note |= bit;
                        else {
                            s->note &= (uint8_t)~bit;
                            s->env &= (uint8_t)~bit;
                            s->len &= (uint8_t)~bit;
                            rv_kitlock_clear(t, rv.pat_cur[t] % RV_PATTERNS, ui.step_page * 16 + wi, ui.lane[t] % RV_LANES);
                        }
                        s->flags = (uint8_t)(s->note & 63 ? s->flags | ST_ON : s->flags & ~ST_ON);
                    } else {
                        s->flags ^= ST_ON;
                        if ((s->flags & ST_ON) && s->vel == 0)
                            s->vel = 100;
                    }
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
            char b[24] = "SCENE ";
            if (rv.scene[wi & 15].used) {            /* a stored scene: the first press asks */
                char w[24] = "AGAIN: OVERWRITE S";
                sx_int(w + 18, wi + 1);
                if (!confirmed(CONF_SCENE + wi, w)) {
                    ui.layer_used[B_SAVE] = 1;
                    return;
                }
            }
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
    /* --- steps: the SEQ section, or SEQ held on any page --- */
    if ((ui.sec == SEC_SEQ || (btn & (1u << B_SEQ))) && !seq_keys_play(t)) {
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
        if (bi >= 2 && bi < 2 + RV_LANES && on && is_kit_ui(t)) {   /* black keys 3-8: the kit's lanes */
            ui.lane[t] = (uint8_t)(bi - 2);
            lane_msg(t);
            return;
        }
        if (bi >= 0 && on) {
            if (bi < RV_PATTERNS) {
                rv_pattern_launch(t, bi);
                if (!rv_live.playing)
                    rv.pat_cur[t] = (uint8_t)bi;
                msg(bi == 0 ? "PATTERN A" : "PATTERN B");
            } else if (bi == 8) {                     /* black key 9: COPY */
                char b[24] = "COPIED T1 A";
                rv_pattern_copy(t);
                b[8] = (char)('1' + t);
                b[10] = (char)('A' + rv.pat_cur[t] % RV_PATTERNS);
                msg(b);
            } else if (bi == 9) {                     /* black key 10: PASTE (asks first over steps) */
                int r;
                if (track_has_pattern_steps(t) && !confirmed(CONF_PASTE, "PRESS AGAIN TO PASTE"))
                    return;
                r = rv_pattern_paste(t);
                msg(r == 0 ? "PASTED" : r == -1 ? "COPY A PATTERN FIRST" : "KIT AND NOTES DON'T MIX");
            } else if (bi == 10) {
                if (track_has_pattern_steps(t) && !confirmed(CONF_CLEAR, "PRESS AGAIN TO CLEAR"))
                    return;
                rv_pattern_clear(t);
                msg("PATTERN CLEARED");
            }
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
        if (is_kit_ui(t)) {                           /* a kit: the white keys play the lanes, 1-6 and again */
            if (bi >= 2 && bi < 2 + RV_LANES) {       /* black keys 3-8 on EDIT: that lane's page */
                if (ui.sec == SEC_VOICE)
                    lane_open(t, bi - 2);
                return;
            }
            if (wi < 0)
                return;
            note = 60 + wi % RV_LANES;
        }
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
    if (down & (1u << B_PLAY)) {
        rv_transport(!rv_live.playing);
    }
    if ((down & ((1u << B_OCTDN) | (1u << B_OCTUP))) && cur_page()->pr == PR_CZSTEP) {
        ui.cz_row = (uint8_t)((ui.cz_row + ((down & (1u << B_OCTUP)) ? 1 : 4)) % 5);   /* the next two stages */
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if ((down & ((1u << B_OCTDN) | (1u << B_OCTUP))) && cur_page()->pr == PR_LIB) {
        ui.lib_row = (uint8_t)((ui.lib_row + ((down & (1u << B_OCTUP)) ? 1 : RV_SENV_SEG)) % (RV_SENV_SEG + 1));   /* segment 1-8, then the envelope's own */
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if ((down & ((1u << B_OCTDN) | (1u << B_OCTUP))) && cur_page()->pr == PR_LFOROW) {
        ui.lfo_row ^= 1;                              /* LFO SHAPE: the other LFO on the knobs */
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if ((down & ((1u << B_OCTDN) | (1u << B_OCTUP))) && cur_page()->pic == PIC_LANE && !(now_btn & (1u << B_SEQ))) {
        ui.lane_row = (uint8_t)((ui.lane_row + ((down & (1u << B_OCTUP)) ? 1 : 2)) % 3);   /* the knobs' row */
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if ((down & ((1u << B_OCTDN) | (1u << B_OCTUP))) && ui.srec_t >= 0 && !(now_btn & (1u << B_SEQ))) {
        int up1 = (down & (1u << B_OCTUP)) != 0;
        if (now_btn & (1u << B_REC)) {                /* step recording: REC and OCT, the keys' octave */
            ui.octave = (int8_t)clampi(ui.octave + (up1 ? 1 : -1), -3, 3);
            ui.layer_used[B_REC] = 1;
        } else                                        /* OCT+ a rest, OCT- back a step */
            rv_step_rec(up1 ? SREC_REST : SREC_BACK, ui.srec_t, 0);
        down &= ~((1u << B_OCTDN) | (1u << B_OCTUP));
    }
    if (down & (1u << B_OCTDN)) {
        if ((ui.sec == SEC_SEQ || (now_btn & (1u << B_SEQ))) && !seq_keys_play(ui.track)) {
            ui.step_page = (uint8_t)(ui.step_page ? ui.step_page - 1 : 0);
            if (cur_page()->pr == PR_STEPCUR)         /* the graph: the knobs go to the page's first step */
                ui.step_cur = (int16_t)(ui.step_page * 16);
        }
        else if (ui.octave > -3)
            ui.octave--;
    }
    if (down & (1u << B_OCTUP)) {
        if ((ui.sec == SEC_SEQ || (now_btn & (1u << B_SEQ))) && !seq_keys_play(ui.track)) {
            int pages_n = (clampi(rv.tp[ui.track][TP_LEN], 1, 64) + 15) / 16;
            if (ui.step_page + 1 < pages_n) {
                ui.step_page++;
                if (cur_page()->pr == PR_STEPCUR)
                    ui.step_cur = (int16_t)(ui.step_page * 16);
            }
        } else if (ui.octave < 3)
            ui.octave++;
    }
    /* section buttons act on release, when they were tapped and not used as a layer */
    for (s = 0; s < NSEC; s++) {
        b = sec_button[s];
        if ((up & (1u << b)) && !ui.layer_used[b] && ms - ui.btn_down_ms[b] < 600) {
            if (s == SEC_SAVE && ui.sec == SEC_SAVE && cur_page()->pr == PR_BANK) {
                bank_do();
            } else if (s == SEC_SAVE && ui.sec == SEC_SAVE) {
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

static int moom_on(void);
void ui_input_only(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), ch;
    int i, d;
    rv_live.ui_track = ui.track;                      /* (MIDI in plays this track: OMNI, or one channel) */
    if (moom_on())                                    /* (the game reads its own input, once a frame) */
        return;
    {                                                 /* step recording: stopped and armed on a SEQ page */
        int t = ui.track, kind = rv.tp[t][TP_KIND];
        int want = ui.sec == SEC_SEQ && ui.armed && !rv_live.playing && (kind == KIND_SYNTH || kind == KIND_STRIKE) ? t : -1;
        if (want != ui.srec_t) {
            if (want >= 0)
                rv_step_rec(SREC_ON, want, 0);
            else
                rv_step_rec(SREC_OFF, 0, 0);
            ui.srec_t = (int8_t)want;
            ui.srec_seen = -1;
        }
        if (want >= 0 && rv_live.srec_step >= 0 && rv_live.srec_step != ui.srec_seen) {
            ui.srec_seen = rv_live.srec_step;         /* the keys' page follows the cursor */
            ui.step_page = (uint8_t)(rv_live.srec_step / 16);
        }
    }
    buttons(btn);
    ch = keys ^ ui.prev_keys;
    for (i = 0; i < NKEYS; i++)
        if (ch & (1u << i))
            key_event(i, (keys >> i) & 1);
    ui.prev_keys = keys;
    d = plat_enc(EN_SELECT);
    if (d) {
        uint8_t list[SEC_PAGES_MAX];
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
    if (d) {
        int gp = (int)(cur_page() - pages);
        preset_turn(d);
        page_follow(gp);                              /* (a sound of another kind may hide or show pages) */
    }
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
    return k == KIND_SYNTH ? C_SYNTH : k == KIND_STRIKE ? C_STRIKE : k == KIND_PERC ? C_PERC : C_TAPE;
}

/* the badge's colour: the kind's own, deeper */
static uint16_t kind_dcol(int t)
{
    int k = rv.tp[t][TP_KIND];
    return k == KIND_SYNTH ? C_SYNTH_D : k == KIND_STRIKE ? C_STRIKE_D : k == KIND_PERC ? C_PERC_D : C_TAPE_D;
}
static const uint16_t knob_col[4] = {C_K1, C_K2, C_K3, C_K4};

static const char *sound_name(int t)
{
    int k = rv.tp[t][TP_KIND];
    if (k == KIND_SYNTH)
        return rv_engine_names[clampi(rv.tp[t][TP_ENGINE], 0, ENG_N - 1)];
    if (k == KIND_PERC)
        return is_kit_ui(t) ? "KIT" : perc_mnames[clampi(rv.tp[t][TP_S0], 0, PM_N - 1)];
    if (k == KIND_STRIKE) {
        const rv_pinfo *e = rv_pinfo_track(t, TP_S0);
        return e->names ? e->names[clampi(rv.tp[t][TP_S0], 0, e->max)] : "STRIKE";
    }
    {
        const rv_pinfo *m = rv_pinfo_track(t, TP_S0);
        return k == KIND_COMP ? "GLUE" : m->names ? m->names[clampi(rv.tp[t][TP_S0], 0, m->max)] : "";
    }
}

static void draw_top(void)
{
    char b[24];
    const page_t *pg = cur_page();
    int t = ui.track, x;
    g_fill(0, 0, 240, 22, C_BG);
    g_rbox(4, 3, 21, 16, 4, kind_dcol(t), 1);         /* the track's badge, in its kind's deep colour */
    b[0] = 'T';
    b[1] = (char)('1' + t);
    b[2] = 0;
    g_text_c(14, 4, &FONT_B, b, g_mix(kind_dcol(t), C_INK, 0.6f));
    if (ui.msg_until && (int32_t)(ui.msg_until - plat_ms()) > 0) {
        g_text(31, 4, &FONT_B, ui.msg, C_WHITE);
        return;
    }
    x = g_text(31, 7, &FONT_T, rv_kind_names[clampi(rv.tp[t][TP_KIND], 0, KIND_N - 1)], C_MID);
    x = g_text(x + 4, 7, &FONT_T, sound_name(t), C_MID);
    {
        /* the audio load, where the platform measures it (the FM-1): a thin bar and the percent,
         * held for a moment at its peak so a spike can be read */
        static uint32_t hold, hold_ms;
        uint32_t cpu = plat_cpu_pct(), tw = (uint32_t)g_text_w(&FONT_B, pg->title);
        if (cpu > 0) {
            int cx = 236 - (int)tw - 48;
            uint16_t cc = cpu >= 90 ? C_RED : cpu >= 75 ? C_AMBER : C_MID;
            if (cpu >= hold || plat_ms() - hold_ms > 1500u) {
                hold = cpu;
                hold_ms = plat_ms();
            }
            if (cx > x + 6) {
                int n = 4;
                b[0] = 'C', b[1] = 'P', b[2] = 'U', b[3] = ' ';
                n += sx_int(b + 4, (int)(hold > 999 ? 999 : hold));
                b[n] = '%';
                b[n + 1] = 0;
                g_text(cx, 7, &FONT_T, b, cc);
                g_rbox(cx, 18, 38, 2, 1, C_LINE, 1);
                g_rbox(cx, 18, (int)(38u * (cpu > 100 ? 100 : cpu) / 100u) > 1 ? (int)(38u * (cpu > 100 ? 100 : cpu) / 100u) : 1, 2, 1, cc, 1);
            }
        }
    }
    g_text_r(236, 4, &FONT_B, pg->title, C_INK);
}

/* one knob: a small dial in its colour, its name, its value, and a bar in its colour (bipolar ones
 * from the middle); greyed when it does nothing as the track is set */
static void knob_draw(int i, const char *name, const char *val, int v, int mn, int mx, uint16_t lc, uint16_t vc, int grey)
{
    int x = i * 60;
    uint16_t kc = grey ? C_KOFF : knob_col[i], ring = grey ? C_KDIV : g_mix(C_KPANEL, kc, 0.45f);
    float frac = mx > mn ? (float)(v - mn) / (float)(mx - mn) : 0.0f, an = (135.0f + 270.0f * clampf(frac, 0.0f, 1.0f)) * (3.14159265f / 180.0f);
    const rv_font_t *fv = &FONT_B;
    g_ring(x + 10.0f, 29.5f, 4.6f, 1.2f, ring);
    g_seg(x + 10.0f + fm_cosf(an) * 1.6f, 29.5f + fm_sinf(an) * 1.6f, x + 10.0f + fm_cosf(an) * 4.8f, 29.5f + fm_sinf(an) * 4.8f, 1.5f, kc);
    g_text(x + 18, 25, &FONT_T, name, lc);
    if (g_text_w(fv, val) > 54)
        fv = &FONT_XS;
    if (g_text_w(fv, val) > 56)
        fv = &FONT_T;
    g_text_c(x + 30, fv == &FONT_B ? 38 : fv == &FONT_XS ? 40 : 42, fv, val, vc);
    g_rbox(x + 6, 60, 48, 2, 1, C_KTRACK, 1);
    if (grey || mx <= mn)
        return;
    if (mn < 0 && mx > 0) {
        int mid = x + 30, w = 24 * v / (v < 0 ? -mn : mx);
        if (w >= 0)
            g_fill(mid, 60, w, 2, kc);
        else
            g_fill(mid + w, 60, -w, 2, kc);
        g_vline(mid, 58, 6, C_MID);
    } else if (frac > 0.0f)
        g_rbox(x + 6, 60, (int)(48.0f * frac) > 2 ? (int)(48.0f * frac) : 2, 2, 1, kc, 1);
}

static void draw_knobs(void)
{
    const page_t *pg = cur_page();
    int i;
    uint32_t now = plat_ms();
    uint32_t btn = plat_buttons();
    int held = held_step();
    g_fill(0, 22, 240, 52, C_BG);
    g_grain(0, 22, 240, 47, C_KPANEL);                /* the knob strip, one cell per physical knob */
    g_hline(0, 22, 240, C_KDIV);
    g_hline(0, 68, 240, C_KDIV);
    for (i = 1; i < 4; i++)
        g_vline(i * 60, 25, 41, C_KDIV);              /* the dividers stay where a cell is empty */
    for (i = 0; i < 4; i++) {
        int v;
        kb_t k = page_knob(pg, i);
        kinfo_t in;
        char val[16];
        uint16_t vc = C_INK, lc = C_MID;
        if (btn & (1u << B_LFO)) {            /* macros on the knobs */
            char nm[10] = "MACRO ";
            nm[6] = (char)('1' + i);
            nm[7] = 0;
            v = rv.gp[GP_MACRO1 + i];
            sx_int(val, v);
            knob_draw(i, nm, val, v, 0, 127, knob_col[i], C_WHITE, 0);
            continue;
        }
        in = kb_info(k);
        if (!in.name || k.kind == K_NONE) {
            continue;
        }
        if (k.kind == K_TP && !rv_tp_applies(ui.track, k.id)) {   /* does nothing as the track is set: greyed */
            v = kb_get(k);
            kb_format(k, v, val);
            if (k.id == TP_ENGINE) {                 /* a strike or tape track has no engine to name */
                val[0] = '-';
                val[1] = 0;
            }
            knob_draw(i, in.name, val, v, in.min, in.max, C_KOFF, C_KOFF, 1);
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
            lc = knob_col[i];                         /* just turned: its name lights in its colour */
        kb_format(k, v, val);
        knob_draw(i, in.name, val, v, in.min, in.max, lc, vc, 0);
    }
}

static void draw_footer(void)
{
    uint8_t list[SEC_PAGES_MAX];
    int n = sec_page_list(ui.sec, list), i, x;
    uint32_t btn = plat_buttons();
    const char *hint = "PRESETS: SOUNDS";
    const page_t *pg = cur_page();
    g_fill(0, FOOT_Y - 2, 240, 26, C_BG);
    g_hline(6, FOOT_Y - 2, 228, C_LINE);
    switch (pg->pr) {
    case PR_ROUTE: hint = "PRESETS: TARGET  ALGO: SOURCE"; break;
    case PR_MAT: hint = "PRESETS: SLOT"; break;
    case PR_MACRO: hint = "PRESETS: DEST  HOLD LFO: MACROS"; break;
    case PR_PATTERN:
        hint = ui.srec_t >= 0 ? "OCT+ REST  OCT- BACK"
               : seq_keys_play(ui.track) ? "REC: KEYS PLAY  HOLD SEQ: STEPS"
               : !is_kit_ui(ui.track) ? "KEYS: STEPS  OCT: PAGE"
               : held_step() >= 0 ? "HOLD THE STEP: ITS KNOBS ON DRUM STEP"
               : "BLACK 3-8: LANE  KEYS: STEPS ON/OFF";
        break;
    case PR_SCENE: hint = "KEYS: LAUNCH  SAVE+KEY: STORE"; break;
    case PR_ARR: hint = "PRESETS: ROW"; break;
    case PR_ARC: hint = "PRESETS: BAR"; break;
    case PR_LIB: {
        static char lh[34];
        lh[0] = 0;
        if (ui.lib_row >= RV_SENV_SEG)
            sx_cat(lh, "PRESETS: ENVELOPE  OCT: SEGMENTS");
        else {
            sx_int(lh + sx_cat(lh, "PRESETS: ENVELOPE  OCT: SEG "), ui.lib_row + 1);
        }
        hint = lh;
        break;
    }
    case PR_SLOT: hint = ui.save_confirm ? "PRESS SAVE AGAIN TO CONFIRM" : "PRESETS: SLOT  SAVE: DO IT"; break;
    case PR_STEPCUR: {                                /* the graph: which steps the keys hold, and OCT for more */
        static char gh[40];
        int len = clampi(rv.tp[ui.track][TP_LEN], 1, RV_STEPS), a = ui.step_page * 16 + 1, z = a + 15 > len ? len : a + 15;
        gh[0] = 0;
        if (len <= 16)
            sx_cat(gh, "PRESETS, KEYS: STEP");
        else {
            sx_cat(gh, "KEYS: ");
            sx_int(gh + sx_cat(gh, ""), a);
            sx_cat(gh, "-");
            sx_int(gh + sx_cat(gh, ""), z);
            sx_cat(gh, "  OCT: MORE");
        }
        hint = gh;
        break;
    }
    case PR_BANK:
        hint = ui.save_confirm ? "PRESS SAVE AGAIN TO CONFIRM"
               : rv_bank_used(ui.cur_sound) ? "PRESETS: SLOT  2,3: NAME  4: BANK"
               : "PRESETS: SLOT  4: BANK  SAVE: DO IT";
        break;
    case PR_CZSTEP: {
        static const char *const rh[5] = {"OCT+: STAGES 3-4", "OCT+: STAGES 5-6", "OCT+: STAGES 7-8", "OCT+: SUS, END", "OCT+: STAGES 1-2"};
        hint = rh[ui.cz_row % 5];
        break;
    }
    case PR_LFOROW: hint = ui.lfo_row ? "LFO 2 ON THE KNOBS  OCT: LFO 1" : "LFO 1 ON THE KNOBS  OCT: LFO 2"; break;
    case PR_LANE: hint = held_step() >= 0 ? "TURN A KNOB: LOCK IT FOR THIS STEP" : "PRESETS: MACHINE  OCT: KNOB ROW"; break;
    default:
        if (ui.sec == SEC_HOME && pg->pic == PIC_TRACKS)
            hint = "ALGO: TRACK  PRESETS: SOUND";
        else if (pg->pic == PIC_MOTION)
            hint = "SHAPE MOVES THE HEAD'S SPEED";
        else if (ui.sec == SEC_BUS)
            hint = "SEND: THIS TRACK INTO IT";
        else if (pg->pic == PIC_ARP)
            hint = "KEYS: HOLD NOTES TO ARPEGGIATE";
        else if (pg->pic == PIC_COMP)
            hint = "T8 SQUEEZES THE MAIN MIX";
        break;
    }
    if (rv_live.countin) {
        static char ci[16] = "COUNT-IN ";
        ci[9] = (char)('0' + rv_live.countin % 10);
        ci[10] = 0;
        hint = ci;
    } else if (btn & (1u << B_SEQ))
        hint = "STEP + KNOB = LOCK";
    else if (btn & (1u << B_LFO))
        hint = "KNOBS: MACRO 1-4";
    else if (btn & (1u << B_FX))
        hint = "LOOP REV STUT FRZ LP HP THRW FILL";
    else if (btn & (1u << B_GLO))
        hint = "1-8 MUTE  9-16 SOLO  BLK1 TAP";
    else if (btn & (1u << B_SAVE))
        hint = "WHITE KEY: STORE THE SCENE";
    else if (btn & (1u << B_SCL))
        hint = "KEY: ROOT";
    {   /* the battery, at the very right (on the FM-1): bars, or a bolt on USB power */
        int bt = plat_battery();
        x = 237 - n * 5;
        if (bt >= 0) {
            uint16_t c = bt == 0 ? C_RED : C_MID;
            int k;
            x -= 16;
            g_frame(224, FOOT_Y + 7, 11, 7, c);
            g_fill(235, FOOT_Y + 9, 1, 3, c);
            if (bt == 4) {                            /* a bolt */
                g_fill(230, FOOT_Y + 8, 2, 1, C_INK);
                g_fill(229, FOOT_Y + 9, 2, 1, C_INK);
                g_fill(228, FOOT_Y + 10, 4, 1, C_INK);
                g_fill(229, FOOT_Y + 11, 2, 1, C_INK);
                g_fill(228, FOOT_Y + 12, 2, 1, C_INK);
            } else
                for (k = 0; k < bt; k++)
                    g_fill(226 + k * 3, FOOT_Y + 9, 2, 3, c);
        }
    }
    {   /* the hint, cut short if it would run into the dots and the transport */
        char hb[48];
        int k = 0, room = x - (rv_live.playing ? 16 : 0) - (ui.armed ? 10 : 0) - 10;
        while (hint[k] && k < 47)
            hb[k] = hint[k], k++;
        hb[k] = 0;
        while (k > 0 && g_text_w(&FONT_T, hb) > room) {   /* whole words go */
            hb[--k] = 0;
            while (k > 0 && hb[k - 1] != ' ')
                hb[--k] = 0;
            while (k > 0 && hb[k - 1] == ' ')
                hb[--k] = 0;
        }
        g_text(6, FOOT_Y + 6, &FONT_T, hb, g_mix(C_DIM, C_MID, 0.35f));
    }
    for (i = 0; i < n; i++)                           /* the section's pages: dots, this one lit */
        g_discf((float)(x + i * 5) + 1.5f, (float)FOOT_Y + 10.5f, 1.25f, i == ui.page[ui.sec] ? C_INK : C_DIM);
    /* transport */
    if (rv_live.playing) {
        g_fill(x - 14, FOOT_Y + 7, 3, 8, C_TEAL);
        g_fill(x - 10, FOOT_Y + 9, 3, 4, rv_live.beat == 0 ? C_WHITE : C_TEAL_D);
    }
    if (ui.armed)
        g_disc(x - 22, FOOT_Y + 11, 3, C_RED);
}

/* --- pictures --- */
/* a meter: a rounded track of height th whose bottom is at y + th, filled from the bottom to
 * frac in colour c; the fill keeps its rounded cap, shrinking to a dot near zero */
static void meter_bar(int x, int y, int w, int th, float frac, uint16_t track, uint16_t c)
{
    int h = (int)(clampf(frac, 0.0f, 1.0f) * (float)th + 0.5f);
    g_rbox(x, y, w, th, w / 2, track, 1);
    if (h <= 0)
        return;
    if (h < w)                                        /* too short for both caps: a dot that grows */
        g_rbox(x + (w - h) / 2, y + th - h, h, h, h / 2, c, 1);
    else
        g_rbox(x, y + th - h, w, h, w / 2, c, 1);
}

static void pic_scope(int t, int y0, int h, uint16_t c)
{
    int i, prev = y0 + h / 2;
    float g = 1.0f / fm_maxf(0.05f, rv_live.meter[t]);
    if (g > 6.0f)
        g = 6.0f;
    for (i = 0; i < 64; i++) {
        float v = (float)rv_live.scope[t][(rv_live.scope_w + i) & 63] * (1.0f / 8192.0f) * g;
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
            g_rbox(x + 2, PIC_Y, 26, 122, 6, C_PANEL, 1);
        b[0] = (char)('1' + t);
        b[1] = 0;
        g_text_c(cx, PIC_Y + 2, &FONT_B, b, t == ui.track ? C_WHITE : kc);
        g_text_c(cx, PIC_Y + 20, &FONT_T, rv.tp[t][TP_KIND] == KIND_SYNTH ? "SYN" : rv.tp[t][TP_KIND] == KIND_STRIKE ? "STK" : rv.tp[t][TP_KIND] == KIND_PERC ? "PRC" : "CMP", muted ? C_DIM : kc);
        /* the level, in the track's colour, and beside it the articulation (its envelope) */
        meter_bar(x + 6, PIC_Y + 32, 11, 66, fm_sqrtf(rv_live.meter[t]), C_LINE, muted ? C_DIM : kc);
        meter_bar(x + 19, PIC_Y + 32, 6, 66, rv_live.env[t], C_LINE, muted ? C_DIM : g_mix(kc, C_INK, 0.55f));
        (void)h;
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
                c = r->type == RT_MOD ? C_TEAL : r->type == RT_TRIG ? C_TRIG : r->type == RT_EXCITE ? C_EXC : r->type == RT_TAPE ? C_TAPE : r->type == RT_CLOCK ? C_CLOCK : r->type == RT_SPARK ? C_SPARK : C_RING;
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
    case RT_SPARK: return C_SPARK;
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
        if (a == RT_TAPE) continue;                         /* the tape is gone; its number is kept */
        int y = PIC_Y + 8 + (a - 1 - (a > RT_TAPE)) * 15;
        g_fill(150, y + 2, 8, 8, route_col(a));
        g_text(162, y, &FONT_T, rv_route_names[a], C_MID);
    }
    {
        static const char *const what[] = {"", "MOVES A KNOB OF", "PLAYS", "RINGS THE BODY OF", "", "CLOCKS", "RING MODULATES", "TAPS THE BODY OF"};
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
        int lit = ui.cz_row % 5 < 4 && i / 2 == ui.cz_row % 5;   /* the two stages on the knobs */
        uint16_t c = !on ? (lit ? C_DIM : C_LINE) : lit ? C_WHITE : C_SYNTH;
        g_line(prevx, prevy, x, y, c);
        g_disc(x, y, lit ? 3 : 2, c);
        if (lit)                                       /* which knobs hold this stage: RATE and LEVEL */
            g_text_c(x, y < PIC_Y + 30 ? y + 6 : y - 16, &FONT_T, (i & 1) ? "3 4" : "1 2", C_TEAL);
        if (i == live)
            g_disc(x, PIC_Y + 112, 2, C_AMBER);
        b[0] = (char)('1' + i);
        b[1] = 0;
        g_text_c(x, PIC_Y + 116, &FONT_T, b, on ? C_DIM : C_LINE);
        if (e->sus == i + 1) {
            g_vline(x, PIC_Y + 14, 90, C_TEAL);
            if (e->end != i + 1)
                g_text_c(x, PIC_Y + 4, &FONT_T, ui.cz_row % 5 == 4 ? "1 SUS" : "SUS", C_TEAL);   /* (on its row: knob 1) */
        }
        if (e->end == i + 1)
            g_text_c(x, PIC_Y + 4, &FONT_T, e->sus == i + 1 ? (ui.cz_row % 5 == 4 ? "1 SUS 2 END" : "SUS+END") : ui.cz_row % 5 == 4 ? "2 END" : "END", C_AMBER);
        prevx = x;
        prevy = y;
    }
    b[0] = 0;
    if (ui.cz_row % 5 == 4)
        sx_cat(b, e->end < 8 ? "SUS HOLDS, END: THE LAST STAGE" : "SUS HOLDS WHILE THE KEY IS DOWN");
    else if (which == CZE_DCO) {                       /* what the DCO levels mean: the pitch of the two stages */
        int k;
        for (k = 0; k < 2; k++) {
            int st = (ui.cz_row % 5) * 2 + k, lv = e->level[st], n;
            char nb[3] = {(char)('1' + st), ':', 0};
            sx_cat(b, k ? "  " : "");
            sx_cat(b, nb);
            n = sx_cat(b, " +");
            sx_int(b + n, lv <= 63 ? lv * 625 / 100 : (lv - 60) * 2);
            sx_cat(b, lv <= 63 ? "C" : " ST");
        }
    } else {
        sx_cat(b, "KNOBS 1 2: STAGE  ");
        b[17] = (char)('1' + (ui.cz_row % 5) * 2);
        sx_cat(b, "   3 4: STAGE  ");
        b[sx_cat(b, "") - 1] = (char)('2' + (ui.cz_row % 5) * 2);
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
    } else if (kind == KIND_PERC) {
        static const char *const fam[4] = {"TRX: ANALOG-STYLE DRUMS", "EFM: TWO-OPERATOR FM DRUMS", "P-I: PHYSICAL MODELS", "GND: SINE, NOISE, IMPULSE"};
        int m = clampi(rv.tp[t][TP_S0], 0, PM_N - 1);
        if (is_kit_ui(t)) {                           /* a kit: its six lanes */
            int l;
            g_text(8, PIC_Y + 6, &FONT_B, "KIT", C_PERC);
            g_text(8, PIC_Y + 30, &FONT_T, "SIX LANES: EDIT > LANE, SEQ > DRUM", C_O_MID);
            for (l = 0; l < RV_LANES; l++) {
                char nb[4] = {(char)('1' + l), 0, 0, 0};
                int x = 8 + (l % 3) * 78, y = PIC_Y + 50 + (l / 3) * 30;
                g_text(x, y, &FONT_T, nb, C_O_DIM);
                g_text(x + 12, y, &FONT_B, perc_mnames[rv.kit[t].mach[l] % PM_N], l == ui.lane[t] % RV_LANES ? C_PERC : C_INK);
            }
            pic_scope(t, PIC_Y + 112, 14, C_PERC);
            return;
        }
        g_text(8, PIC_Y + 6, &FONT_B, perc_mnames[m], C_PERC);
        g_text(8, PIC_Y + 30, &FONT_T, fam[m <= PM_TRX_XC ? 0 : m <= PM_EFM_CY ? 1 : m <= PM_PI_CC ? 2 : 3], C_O_MID);
        pic_scope(t, PIC_Y + 44, 90, C_PERC);
    } else {
        static const char *const desc[ENG_N] = {"SAW TO PULSE, SUPERSAW, SUB, TZFM", "CASIO CZ: 2 LINES, 8-STEP DCO DCW DCA",
                                                "4 OPERATORS, 12 ALGORITHMS", "STRING, BOW, BREATH, MEMBRANE",
                                                "VOWELS: FILTER BANK OR FOF", "PULSAR EMITTERS, COUPLED",
                                                "SID WAVES, ARP OFFSETS, DECOMPOSE", "WAVETABLES: 2 OSCS, WARPS, WAVE ENV", "LOGIC: 3 SOURCES, 2 FUNCTIONS, GRAINS",
                                                "PIPE ORGAN: RANKS, SPEECH, AIR, WIND", "SH-101 STYLE, PLAYED LIKE A HORN",
                                                "BOWED STRING OR REED, AND QUAKE", "KALIMBA, MARIMBA, ELECTRIC PIANO"};
        int e = clampi(rv.tp[t][TP_ENGINE], 0, ENG_N - 1);
        if (e == ENG_WAVE) {                          /* the wavetable: its waves stacked back to front, the one playing lit */
            const rv_pinfo *tn = rv_pinfo_track(t, TP_S0);
            int two = rv_param_eff(t, TP_S6) > 0.5f, w, f, j;
            char pb[24];
            for (w = 0; w < 1 + two; w++) {           /* with MIX up, table 2 below table 1, each half as deep */
                int tb = clampi(rv.tp[t][w ? TP_S2 : TP_S0], 0, WT_TABLES - 1), pos = clampi((int)rv_param_eff(t, w ? TP_S3 : TP_S1), 0, 63), cur = pos * 15 / 63;
                float base = two ? (float)PIC_Y + (w ? 118.0f : 58.0f) : (float)PIC_Y + 104.0f, dy = two ? 2.1f : 4.6f, amp = two ? 9.0f : 18.0f;
                uint16_t lit = w ? C_AMBER : C_TEAL;
                for (f = 15; f >= 0; f--) {
                    float kp = (float)f * (7.0f / 15.0f), ox = 14.0f + (float)f * 6.2f, oy = base - (float)f * dy, px = 0.0f, py = 0.0f;
                    uint16_t c = f == cur ? lit : g_mix(C_BG, C_MID, 0.75f - 0.03f * (float)f);
                    if (oy - amp - 6.0f > (float)g_y1 || oy + amp + 6.0f < (float)g_y0)
                        continue;                     /* (not in this band) */
                    for (j = 0; j <= 48; j++) {
                        float u = (float)j / 48.0f, x = ox + u * 118.0f, y = oy - wtab_read(tb, kp, u * 0.999f, 0.0f, 0) * amp;
                        if (j)
                            g_seg(px, py, x, y, f == cur ? 1.8f : 0.8f, c);
                        px = x;
                        py = y;
                    }
                }
                pb[0] = 0;
                sx_cat(pb, w ? "2 " : two ? "1 " : "");
                sx_int(pb + sx_cat(pb, "POS "), pos);
                if (!two)
                    sx_cat(pb, " / 63");
                g_text(8, two ? (w ? PIC_Y + 66 : PIC_Y + 2) : PIC_Y + 2, &FONT_T, pb, w ? g_mix(C_BG, C_AMBER, 0.8f) : C_DIM);
                g_text_r(232, two ? (w ? PIC_Y + 66 : PIC_Y + 2) : PIC_Y + 2, &FONT_T, tn->names ? tn->names[tb] : "", C_DIM);
            }
            if (!two)
                g_seg(14.0f, (float)PIC_Y + 126.0f, 14.0f + 15.0f * 6.2f, (float)PIC_Y + 126.0f - 15.0f * 4.6f, 0.8f, C_LINE);
            return;
        }
        g_text(8, PIC_Y + 6, &FONT_B, rv_engine_names[e], C_SYNTH);
        g_text(8, PIC_Y + 30, &FONT_T, desc[e], C_MID);
        pic_scope(t, PIC_Y + 44, 90, C_TEAL);
    }
    (void)b;
}

static float pic_sem_m;
static int pic_sem_bp;
/* the response of the track's filter at f (approximate, for the picture) */
static float filt_mag(int type, float f, float fc, float res)
{
    float w = f / fc, q = 0.5f + res * res * 18.0f, re = 1.0f - w * w, im = w / q, m2 = re * re + im * im, m;
    switch (type) {
    case FT_SH101:
    case FT_LADDER: {
        float q2 = 0.6f + res * 4.0f, im2 = w / q2;
        m = 1.0f / fm_sqrtf(m2 > 1e-6f ? m2 : 1e-6f);
        m *= 1.0f / fm_sqrtf(re * re + im2 * im2 + 1e-6f);
        return m;
    }
    case FT_HP: return w * w / fm_sqrtf(m2 + 1e-6f);
    case FT_XNOTCH: return fm_fabsf(re) / fm_sqrtf(m2 + 1e-6f);
    case FT_XBP24: { float b = (w / q) / fm_sqrtf(m2 + 1e-6f); return b * b * (1.0f + res * 2.0f); }
    case FT_XLP24: { float im2 = w / 0.55f; return 1.0f / fm_sqrtf(m2 + 1e-6f) / fm_sqrtf(re * re + im2 * im2 + 1e-6f); }
    case FT_XSH: case FT_XSHAPE: return 1.0f;
    case FT_SEM: {
        float q3 = 0.55f + res * res * 11.0f, im3 = w / q3, d = fm_sqrtf(re * re + im3 * im3 + 1e-6f);
        return pic_sem_bp ? im3 / d * fm_sqrtf(q3) : fm_fabsf((1.0f - pic_sem_m) - pic_sem_m * w * w) / d;
    }
    case FT_BP: return (w / q) / fm_sqrtf(m2 + 1e-6f) * (1.0f + res);
    default: return 1.0f / fm_sqrtf(m2 + 1e-6f);
    }
}

static void pic_filter(void)
{
    int t = ui.track, i, prev = 0, type = rv.tp[t][TP_FTYPE];
    float fc = rv_cut_hz(rv_param_eff(t, TP_CUT)), res = rv_param_eff(t, TP_RES) / 127.0f, fx;
    char b[20];
    pic_sem_m = clampf(rv_param_eff(t, TP_FMODE) / 127.0f, 0.0f, 1.0f);
    pic_sem_bp = rv.tp[t][TP_FBP] != 0;
    for (i = 0; i < 3; i++)                                   /* 100 Hz, 1 kHz, 10 kHz: 20 Hz to 20 kHz over 224 px */
        g_vline(8 + (int)(224.0f * (2.3219f + 3.3219f * (float)i) / 10.0f), PIC_Y + 6, 112, C_LINE);
    for (i = 0; i <= 224; i += 2) {
        float f = 20.0f * fm_exp2f((float)i / 224.0f * 10.0f);
        float m = FT_IS_Z(type) ? zp_mag(type - FT_ZVOWEL, rv_param_eff(t, TP_CUT) / 127.0f, res, f)
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
    g_text(52, PIC_Y + 122, &FONT_T, "100", C_DIM);
    g_text(130, PIC_Y + 122, &FONT_T, "1K", C_DIM);
    g_text(201, PIC_Y + 122, &FONT_T, "10K", C_DIM);
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
    h = 0;
    (void)h;
    meter_bar(108, PIC_Y + 58, 24, 72, fm_sqrtf(rv_live.meter[t]), C_PANEL, C_TEAL);
    {
        static const int sends[3] = {TP_FBS, TP_DLYS, TP_REVS};
        static const char *const sn[3] = {"FB", "DLY", "SPC"};
        int i;
        for (i = 0; i < 3; i++) {
            int sx = i < 1 ? 42 : 146 + (i - 1) * 40;
            meter_bar(sx, PIC_Y + 80, 12, 50, rv.tp[t][sends[i]] / 127.0f, C_PANEL, C_AMBER);
            g_text_c(sx + 6, PIC_Y + 66, &FONT_T, sn[i], C_DIM);
        }
    }
}

/* the articulation: onset, continuant, termination, drawn from the track's knobs */
/* the FILTER ENV: delay, attack, hold, decay, sustain, release as drawn with their bends, the
 * loop when it is on, the playing voice, and how deep it goes into the cutoff */
static void pic_fenv(void)
{
    int t = ui.track, i, prev = 0, x[6], top = PIC_Y + 18, base = PIC_Y + 108;
    float sc = fm_exp2f(-rv_param_eff(t, TP_FRATE) / 16.0f), sus = rv_param_eff(t, TP_FSUS) / 127.0f;
    float ka = rv_param_eff(t, TP_FACRV) * (6.0f / 64.0f), kd = -rv_param_eff(t, TP_FDCRV) * (6.0f / 64.0f);
    static const uint8_t tp_of[5] = {TP_FDLY, TP_FATK, TP_FHOLD, TP_FDEC, TP_FREL};
    float w[6], tot = 0.0f;
    int loop = rv.tp[t][TP_FLOOP] != 0, amt = rv.tp[t][TP_EFILT];
    char b[28];
    if (rv.tp[t][TP_KIND] != KIND_SYNTH || (rv.tp[t][TP_FTYPE] == FT_LPG && !rv.tp[t][TP_FAMT])) {
        g_text_c(120, PIC_Y + 40, &FONT_B, rv.tp[t][TP_KIND] != KIND_SYNTH ? "NO FILTER HERE" : "THE LPG FOLLOWS", C_DIM);
        g_text_c(120, PIC_Y + 62, &FONT_T, rv.tp[t][TP_KIND] != KIND_SYNTH ? "THE FILTER ENV IS FOR SYNTH TRACKS" : "THE VOLUME ENVELOPE", C_DIM);
        return;
    }
    for (i = 0; i < 5; i++) {
        float s = rv_time_s((int)rv_param_eff(t, tp_of[i])) * sc;
        w[i < 4 ? i : 5] = (i == 0 || i == 2) && s < 0.001f ? 0.0f : 10.0f + fm_sqrtf(s) * 36.0f;
    }
    w[4] = 30.0f;                                      /* the sustain */
    for (i = 0; i < 6; i++)
        tot += w[i];
    x[0] = 8 + (int)(w[0] * 224.0f / tot);
    for (i = 1; i < 6; i++)
        x[i] = x[i - 1] + (int)(w[i] * 224.0f / tot);
    x[5] = 232;
    g_hline(8, base, 224, C_LINE);
    for (i = 8; i <= 232; i++) {
        float y, u;
        int py;
        if (i < x[0])
            y = 0.0f;
        else if (i < x[1])
            u = (float)(i - x[0]) / (float)(x[1] - x[0] > 0 ? x[1] - x[0] : 1), y = fe_bend(u, ka);
        else if (i < x[2])
            y = 1.0f;
        else if (i < x[3])
            u = (float)(i - x[2]) / (float)(x[3] - x[2] > 0 ? x[3] - x[2] : 1), y = 1.0f + (sus - 1.0f) * fe_bend(u, kd);
        else if (i < x[4])
            y = sus;
        else
            u = (float)(i - x[4]) / (float)(x[5] - x[4] > 0 ? x[5] - x[4] : 1), y = sus * (1.0f - fe_bend(u, kd));
        py = base - (int)(clampf(y, 0.0f, 1.0f) * (float)(base - top));
        if (i > 8)
            g_line(i - 1, prev, i, py, i < x[0] ? C_DIM : i < x[1] ? C_K1 : i < x[2] ? C_DIM : i < x[3] ? C_K2 : i < x[4] ? C_K3 : C_K4);   /* each stage in its knob's colour */
        prev = py;
    }
    if (loop) {                                        /* the loop: from the end of the decay back to the delay */
        g_hline(8, top - 10, x[3] - 8, C_AMBER_D);
        g_vline(8, top - 10, 6, C_AMBER_D);
        g_vline(x[3] - 1, top - 10, 6, C_AMBER_D);
        g_text_c((8 + x[3]) / 2, top - 15, &FONT_T, "LOOPS WHILE HELD", C_AMBER);
    }
    {   /* the playing voice */
        int st = rv_live.fenv_stage[t];
        float u = clampf(rv_live.fenv_pos[t], 0.0f, 1.0f), lv = clampf(rv_live.fenv[t], 0.0f, 1.0f);
        int px = st == FE_DLY ? 8 + (int)(u * (float)(x[0] - 8)) : st == FE_ATK ? x[0] + (int)(u * (float)(x[1] - x[0]))
               : st == FE_HOLD ? x[1] + (int)(u * (float)(x[2] - x[1])) : st == FE_DEC ? x[2] + (int)(u * (float)(x[3] - x[2]))
               : st == FE_SUS ? (x[3] + x[4]) / 2 : st == FE_REL ? x[4] + (int)(u * (float)(x[5] - x[4])) : -10;
        if (st != FE_OFF && rv_live.stage[t] != AS_OFF && rv_live.env[t] > 0.001f)
            g_disc(px, base - (int)(lv * (float)(base - top)), 4, C_WHITE);
    }
    b[0] = 0;
    sx_int(b + sx_cat(b, amt > 0 ? "ENV>CUT +" : "ENV>CUT "), amt);
    g_text(10, base + 4, &FONT_T, b, amt ? C_MID : C_DIM);
    if (rv.tp[t][TP_FAMT]) {                          /* and its own destination */
        int fa = rv.tp[t][TP_FAMT], k = 0;
        b[0] = 0;
        k = sx_cat(b, "INTO ");
        rv_param_format(PID_T(t, TP_FDST), rv.tp[t][TP_FDST], b + k, 12);
        k = sx_cat(b, fa > 0 ? " +" : " ");
        sx_int(b + k, fa);
        g_text_r(232, base + 4, &FONT_T, b, C_TEAL);
    } else
        g_text_r(232, base + 4, &FONT_T, "D A H D S R", C_DIM);
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
    int focus = pg->title[0] == 'O' ? 0 : pg->title[0] == 'L' ? 4 : pg->title[0] == 'E' ? 3 : -1;   /* 3: all of it; 4: the loop and the tail */
    if (rv.tp[t][TP_KIND] == KIND_PERC) {
        g_text_c(120, PIC_Y + 40, &FONT_B, "THE MACHINE MAKES", C_PERC);
        g_text_c(120, PIC_Y + 62, &FONT_B, "ITS OWN ENVELOPE", C_PERC);
        g_text_c(120, PIC_Y + 92, &FONT_T, rv.tp[t][TP_ENGINE] == 1 ? "SET DECAYS ON EDIT > LANE 1-6" : "SET ITS DECAY ON THE SOURCE PAGES", C_DIM);
        return;
    }
    int top = PIC_Y + 14, base = PIC_Y + 112;
    float peak = 1.0f + over * 0.6f;
    seg_x[0] = 8 + (int)(wa * sc);
    seg_x[1] = seg_x[0] + (int)(wd * sc);
    seg_x[2] = seg_x[1] + (int)(wh * sc);
    seg_x[3] = 232;
    if (focus == 0 || focus == 4)                     /* the stage the page is about, behind the curve */
        g_fill(focus == 0 ? 8 : seg_x[0], top - 8, focus == 0 ? seg_x[0] - 8 : 232 - seg_x[0], base - top + 10, C_PANEL);
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
    g_text(10, base + 4, &FONT_T, "ONSET", focus == 0 || focus == 3 ? C_TEAL : C_DIM);
    g_text(seg_x[0] + 2, base + 4, &FONT_T, "CONTINUANT", focus >= 3 ? C_INK : C_DIM);
    g_text_r(232, base + 4, &FONT_T, "TERMINATION", focus >= 3 ? C_AMBER : C_DIM);
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

/* a LIBRARY envelope across the screen: its segments with their bends, the step grid (synced), the
 * loop, where it chains to, the segment on the knobs (sel, or -1) and the playing position of track t */
static void senv_draw(int idx, int sel, int t)
{
    const rv_senv *e = &rv.senv[idx % RV_SENV];
    int n = SENV_NSEG(e), i, x[RV_SENV_SEG + 1], top = PIC_Y + 22, base = PIC_Y + 100, tot8 = 0;
    float w[RV_SENV_SEG], tw = 0.0f, lv0 = (float)e->start / 127.0f;
    char b[32];
    if (!n) {
        g_text_c(120, PIC_Y + 46, &FONT_B, "EMPTY", C_DIM);
        g_text_c(120, PIC_Y + 70, &FONT_T, "EDIT A SEGMENT, OR TURN SEGS", C_DIM);
        return;
    }
    for (i = 0; i < n; i++) {
        float sec = rv_time_s(e->tim[i]);
        w[i] = SENV_FREE(e) ? (e->tim[i] ? 6.0f + fm_sqrtf(sec) * 40.0f : 2.0f) : (e->tim[i] ? (float)e->tim[i] : 0.6f);
        tw += w[i];
        tot8 += e->tim[i];
    }
    x[0] = 8;
    for (i = 0; i < n; i++)
        x[i + 1] = x[i] + (int)(w[i] * 224.0f / tw + 0.5f);
    x[n] = 232;
    if (sel >= 0 && sel < n)
        g_fill(x[sel], top - 6, x[sel + 1] - x[sel] > 2 ? x[sel + 1] - x[sel] : 2, base - top + 8, C_PANEL);
    g_hline(8, base, 224, C_LINE);
    if (!SENV_FREE(e) && tot8 > 0)                     /* the steps under it */
        for (i = 0; i <= tot8; i += 8) {
            int gx = 8 + (int)((float)i * 224.0f / tw);
            g_vline(gx, base + 1, (i / 8) % 4 ? 3 : 6, C_DIM);
        }
    {
        float from = lv0;
        int prev = base - (int)(from * (float)(base - top));
        for (i = 0; i < n; i++) {
            float to = (float)e->lvl[i] / 127.0f, k = (float)senv_crv(e, i) * 0.9f;
            int xx, py;
            for (xx = x[i]; xx <= x[i + 1]; xx++) {
                float u = x[i + 1] > x[i] ? (float)(xx - x[i]) / (float)(x[i + 1] - x[i]) : 1.0f;
                py = base - (int)((from + (to - from) * fe_bend(u, k)) * (float)(base - top));
                g_line(xx > 8 ? xx - 1 : xx, prev, xx, py, i == sel ? C_WHITE : C_TEAL);
                prev = py;
            }
            from = to;
        }
    }
    if (e->loop && e->loop <= n) {                     /* the loop: back from the end to its segment */
        int lx = x[e->loop - 1];
        g_hline(lx, top - 12, 232 - lx, C_AMBER_D);
        g_vline(lx, top - 12, 5, C_AMBER_D);
        g_vline(231, top - 12, 5, C_AMBER_D);
        g_text_c((lx + 232) / 2, top - 18, &FONT_T, "LOOP", C_AMBER);
    } else if (e->next) {
        b[0] = 0;
        sx_int(b + sx_cat(b, "THEN "), e->next);
        g_text_r(232, top - 18, &FONT_T, b, C_AMBER);
    }
    if (senv[t & 7].on && senv[t & 7].env == idx % RV_SENV && senv[t & 7].seg < n) {   /* playing */
        int sg = senv[t & 7].seg, px = x[sg] + (int)(clampf(senv[t & 7].u, 0.0f, 1.0f) * (float)(x[sg + 1] - x[sg]));
        g_disc(px, base - (int)(clampf(senv[t & 7].lvl, 0.0f, 1.0f) * (float)(base - top)), 4, C_WHITE);
    }
    b[0] = 0;
    sx_int(b + sx_cat(b, "ENV "), idx % RV_SENV + 1);
    g_text(8, PIC_Y + 2, &FONT_XS, b, C_INK);
    b[0] = 0;
    if (SENV_FREE(e))
        sx_cat(b, "FREE TIME");
    else {
        int st = tot8 / 8;
        sx_int(b, st);
        sx_cat(b, tot8 % 8 ? "+ STEPS" : st == 1 ? " STEP" : " STEPS");
    }
    g_text(62, PIC_Y + 4, &FONT_T, b, C_DIM);
}

static void pic_lib(void)
{
    int t = ui.track;
    senv_draw(ui.cur_lib, ui.lib_row < RV_SENV_SEG ? ui.lib_row : -1, t);
    if (ui.lib_row >= RV_SENV_SEG)
        g_text(8, PIC_Y + 112, &FONT_T, "START, TIME BASE, LOOP, NEXT", C_AMBER);
    else {
        char b[24];
        b[0] = 0;
        sx_int(b + sx_cat(b, "SEGMENT "), ui.lib_row + 1);
        g_text(8, PIC_Y + 112, &FONT_T, b, C_MID);
    }
    g_text_r(232, PIC_Y + 112, &FONT_T, "SEQ: STEP SHAPE > ENV", C_DIM);
}

/* STEP ENV: the envelope this track is playing (or the one its notes start), and where it goes */
static void pic_senv(void)
{
    int t = ui.track, e = senv[t].on ? senv[t].env : rv.tp[t][TP_SEDEF] - 1;
    char b[30];
    if (e < 0) {
        g_text_c(120, PIC_Y + 36, &FONT_B, "NO ENVELOPE YET", C_DIM);
        g_text_c(120, PIC_Y + 60, &FONT_T, "STEPS PICK ONE ON SEQ > STEP SHAPE > ENV", C_DIM);
        g_text_c(120, PIC_Y + 74, &FONT_T, "OR SE NOTE STARTS ONE ON EVERY NOTE", C_DIM);
    } else
        senv_draw(e, -1, t);
    b[0] = 0;
    sx_cat(b, "INTO ");
    rv_param_format(PID_T(t, TP_SEDST), rv.tp[t][TP_SEDST], b + 5, 20);
    g_text(8, PIC_Y + 112, &FONT_T, b, rv.tp[t][TP_SEAMT] ? C_MID : C_DIM);
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
            g_text_r(200, y, &FONT_XS, b, rv_live.mat_amt[i] != m->amt ? C_TEAL : C_INK);   /* bone: another row is moving it */
            {
                float a = (float)rv_live.mat_amt[i];
                float v = m->src == MS_KEY   /* the destination track's note: full bar at 5 octaves */
                    ? rv_live.key[m->dst < RV_GPID ? m->dst / RV_TP : 0] * (1.0f / 60.0f) * a / 63.0f
                    : rv_live.msrc[m->src] * a / 64.0f;
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
    int i, prev = 0, shape = rv.gp[k ? GP_LFO2SHAPE : GP_LFO1SHAPE], yc = PIC_Y + 30 + k * 60;   /* LFO 1 above, LFO 2 below */
    int edit = cur_page()->pr == PR_LFOROW, on = edit && (ui.lfo_row & 1) == k;
    float ph = lfo[k].ph, sym = clampf(geff[k ? GP_LFO2SYM : GP_LFO1SYM] / 64.0f, -1.0f, 1.0f);
    float phase = clampf(geff[k ? GP_LFO2PHASE : GP_LFO1PHASE] / 360.0f, 0.0f, 0.9999f);
    static const char *const md[3] = {"", " RETRIG", " 1 SHOT"};
    char lb[16] = "LFO 1";
    lb[4] = (char)('1' + k);
    sx_cat(lb, md[clampi(rv.gp[k ? GP_LFO2MODE : GP_LFO1MODE], 0, 2)]);
    g_text(232 - g_text_w(&FONT_XS, lb), yc - 28, &FONT_XS, lb, on ? C_TEAL : C_DIM);
    if (ph > 1.0f)
        ph = 1.0f;
    for (i = 0; i <= 224; i += 2) {
        float p = (float)i / 224.0f, q = p + phase, v;
        if (q >= 1.0f)
            q -= 1.0f;
        q = lfo_warp(q, sym);                         /* the wave as SYM and PHASE make it */
        v = shape <= 4 ? lfo_wave(shape, q) : sin_lut(q * 3.0f) * 0.5f + sin_lut(q * 7.0f + 0.3f) * 0.3f;
        {
            int y = yc - (int)(v * 22.0f);
            if (i)
                g_line(8 + i - 2, prev, 8 + i, y, on ? C_TEAL : C_TEAL_D);
            prev = y;
        }
    }
    g_vline(8 + (int)(ph * 224.0f), yc - 26, 52, C_TEAL);
    g_disc(8 + (int)(ph * 224.0f), yc - (int)((k ? rv_live.lfo2 : rv_live.lfo1) * 22.0f), 3, C_WHITE);
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



static void pic_delay(void)
{
    int i;
    float fb = rv.gp[GP_DLYFB] / 127.0f, a = 1.0f;
    g_hline(8, PIC_Y + 107, 224, C_LINE);
    for (i = 0; i < 8; i++) {                         /* the repeats, each fainter, left and right in turn */
        int x = 10 + i * 28;
        uint16_t c = i & 1 ? C_AMBER : C_TEAL;
        meter_bar(x + 5, PIC_Y + 10, 10, 96, a, C_LINE, c);
        g_text_c(x + 10, PIC_Y + 112, &FONT_T, i & 1 ? "R" : "L", i & 1 ? C_AMBER_D : C_TEAL_D);
        a *= fb;
    }
}

static void pic_space(void)
{
    int i;
    float sz = rv.gp[GP_REVSIZE] / 127.0f, d = rv.gp[GP_REVDAMP] / 127.0f;
    for (i = 0; i < 28; i++) {                        /* the reflections thinning into the tail */
        float a = fm_expf(-(float)i / (2.0f + sz * 20.0f)) * (0.6f + 0.4f * sin_lut((float)i * 0.74f));
        int h = (int)(a * 100.0f), x = 8 + i * 8;
        uint16_t c = g_mix(C_TEAL, C_TEAL_D, d * (float)i / 28.0f);
        if (h >= 6)
            g_rbox(x, PIC_Y + 112 - h, 6, h, 3, c, 1);
        else if (h > 1)
            g_disc(x + 3, PIC_Y + 112 - h / 2, h / 2 + 1, c);
    }
}

/* the BED: its body's modes as lines (wood: spread and short; metal: clustered and long), the
 * shimmer above, the dust as it falls, and how far the tracks are holding it down */
static void pic_bed(void)
{
    static const float wood[6] = {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f};
    static const float metal[6] = {1.0f, 1.483f, 1.801f, 2.546f, 2.630f, 3.897f};
    static uint8_t last_hit, dust_x[12], dust_a[12];
    float tone = rv.gp[GP_BEDTONE] / 127.0f, shim = rv.gp[GP_BEDSHIM] / 127.0f, lvl = rv.gp[GP_BEDLEVEL] / 127.0f;
    float env = clampf(fm_sqrtf(rv_live.bed_env) * 2.5f, 0.0f, 1.0f);
    int k, base = PIC_Y + 100;
    uint16_t on = lvl > 0.0f ? C_TEAL : C_DIM;
    g_hline(8, base, 224, C_LINE);
    for (k = 0; k < 6; k++) {                         /* the modes: where they sit (log), how long they ring */
        float rr = wood[k] + (metal[k] - wood[k]) * tone, ring = (0.3f + tone * 0.7f) / (1.0f + (float)k * (1.2f - tone * 0.9f));
        int x = 16 + (int)(fm_log2f(rr) / 4.3f * 200.0f), h = (int)(20.0f + 60.0f * ring * (0.4f + 0.6f * env));
        g_rbox(x - 2, base - h, 5, h, 2, g_mix(C_TEAL_D, on, fm_powf(0.75f, (float)k)), 1);
    }
    if (shim > 0.0f)                                  /* the shimmer: an octave and two up */
        for (k = 0; k < 2; k++) {
            int x = 16 + (int)((1.0f + (float)k) / 4.3f * 200.0f), h = (int)(10.0f + 40.0f * shim);
            g_rbox(x - 1, base - h - 6, 3, h, 1, C_AMBER, 1);
        }
    if (rv_live.bed_hit != last_hit) {                /* the dust falling */
        last_hit = rv_live.bed_hit;
        for (k = 11; k > 0; k--) {
            dust_x[k] = dust_x[k - 1];
            dust_a[k] = dust_a[k - 1];
        }
        dust_x[0] = (uint8_t)(16 + (last_hit * 73) % 200);
        dust_a[0] = 255;
    }
    for (k = 0; k < 12; k++) {
        if (dust_a[k] > 20)
            g_disc(dust_x[k], PIC_Y + 12 + k * 4, 1, g_mix(C_BG, on, dust_a[k] / 255.0f));
        dust_a[k] = (uint8_t)(dust_a[k] * 7 / 8);
    }
    g_text(8, PIC_Y + 106, &FONT_T, tone < 0.35f ? "WOOD" : tone > 0.65f ? "METAL" : "WOOD / METAL", C_DIM);
    if (rv.gp[GP_BEDDUCK] > 0) {                      /* the duck: full is free */
        int w = (int)(60.0f * clampf(rv_live.bed_duck, 0.0f, 1.0f));
        g_text_r(160, PIC_Y + 106, &FONT_T, "DUCK", C_DIM);
        g_fill(166, PIC_Y + 108, 60, 4, C_LINE);
        g_fill(166, PIC_Y + 108, w, 4, C_AMBER);
    }
    if (lvl <= 0.0f)
        g_text_c(120, PIC_Y + 40, &FONT_T, "LEVEL UP TO HEAR IT", C_DIM);
}

/* EUCLID: the steps in a ring, the playing ones lit, the step on now marked */
static void pic_euclid(void)
{
    int t = ui.track, len = euc_len(t), i, hits = euc_hits(t), cx = 120, cy = PIC_Y + 58, now;
    float rad = 46.0f;
    char b[24] = "E(";
    now = is_kit_ui(t) ? rv_live.lane_step[t][euc_lane(t)] : rv_live.step[t][0];
    for (i = 0; i < len; i++) {
        float a = (float)i / (float)len;
        float x = (float)cx + rad * sin_lut(a), y = (float)cy - rad * sin_lut(a + 0.25f);
        int on = euc_on(t, i);
        if (on)
            g_discf(x, y, len > 32 ? 3.0f : 4.5f, i == now ? C_WHITE : kind_dcol(t));
        else
            g_discf(x, y, 1.5f, i == now ? C_WHITE : C_LINE);
    }
    {
        char nb[8];
        sx_int(nb, hits);
        sx_cat(b, nb);
        sx_cat(b, ",");
        sx_int(nb, len);
        sx_cat(b, nb);
        sx_cat(b, ")");
    }
    g_text_c(cx, cy - 6, &FONT_B, b, C_INK);
    g_text(8, PIC_Y + 116, &FONT_T, is_kit_ui(t) ? "WRITES THE CHOSEN LANE AS YOU TURN" : "WRITES THE STEPS AS YOU TURN", C_DIM);
}

/* the stress test: builds the asked-for step once the knob rests, measures each step from its first
 * second to its third (the average of the reading), and AUTO walks the steps, three seconds each */
static int stress_loaded(void)
{
    return rv.name[0] == 'S' && rv.name[1] == 'T' && rv.name[2] == 'R' && rv.name[3] == 'E' && rv.name[4] == 'S' && rv.name[5] == 'S' && !rv.name[6];
}
static void stress_tick(void)
{
    uint32_t now = plat_ms();
    if (!ui.st_on)
        return;
    if (ui.st_built && ui.st_built != 255 && !stress_loaded()) {   /* another project came in: the test is over */
        ui.st_on = ui.st_step = ui.st_built = ui.st_auto = 0;
        rv_live.guard_off = 0;
        return;
    }
    if (ui.st_step != ui.st_built && now - ui.st_turn > 200u) {
        rv_stress(ui.st_step, ui.st_eng);
        ui.st_built = ui.st_step;
        ui.st_t0 = now;
        ui.st_x0 = plat_xruns();
        ui.st_acc = ui.st_acc2 = 0.0f;
        ui.st_nacc = 0;
        if (!ui.st_step)
            msg("STRESS STOPPED");
        return;
    }
    if (!ui.st_built || ui.st_step != ui.st_built)
        return;
    if (now - ui.st_t0 >= 1000u && now - ui.st_t0 < 3000u) {
        ui.st_acc += rv_live.load;
        ui.st_acc2 += rv_live.c2_used ? rv_live.c2_load : 0.0f;
        ui.st_nacc++;
    }
    if (plat_xruns() != ui.st_x0)
        ui.st_xr |= 1u << ui.st_built;
    if (now - ui.st_t0 >= 3000u && ui.st_nacc) {
        ui.st_res[ui.st_built] = (uint16_t)clampi((int)(ui.st_acc / (float)ui.st_nacc + 0.5f), 0, 9999);
        ui.st_res2[ui.st_built] = (uint8_t)clampi((int)(ui.st_acc2 / (float)ui.st_nacc + 0.5f), 0, 255);
        ui.st_nacc = 0;
        if (ui.st_auto) {                              /* the next step, or done */
            ui.st_step = (uint8_t)(ui.st_built + 1 < RV_STRESS_N ? ui.st_built + 1 : 0);
            if (!ui.st_step)
                ui.st_auto = 0;
            ui.st_turn = now - 1000u;
        }
    }
}

/* GLO > STRESS: the step playing, its reading now, and the table of what each step measured */
static void pic_stress(void)
{
    char b[32];
    int k;
    b[0] = 0;
    if (!ui.st_on) {
        g_text_c(120, PIC_Y + 30, &FONT_B, "STRESS TEST", C_INK);
        g_text_c(120, PIC_Y + 54, &FONT_T, "TURN STEP OR SET AUTO TO RUN", C_DIM);
        g_text_c(120, PIC_Y + 66, &FONT_T, "IT REPLACES THE PROJECT: SAVE FIRST", C_RED);
        g_text_c(120, PIC_Y + 78, &FONT_T, "EACH STEP ADDS LOAD; XRUNS MARK THE EDGE", C_DIM);
        return;
    }
    sx_cat(b, rv_stress_name(ui.st_step));
    if (ui.st_step) {
        sx_cat(b, "  ");
        int l = 0;
        while (b[l])
            l++;
        sx_int(b + l, (int)(rv_live.load + 0.5f));
        sx_cat(b, "%");
    }
    g_text(6, PIC_Y + 2, &FONT_B, b, ui.st_auto ? C_TEAL : C_INK);
    if (plat_cycles() == 0) {
        g_text(6, PIC_Y + 20, &FONT_T, "NO CLOCK HERE: THE FM-1 MEASURES IT", C_DIM);
        return;
    }
    for (k = 1; k < RV_STRESS_N; k++) {
        int x = k <= 8 ? 6 : 122, y = PIC_Y + 20 + ((k - 1) % 8) * 12;
        uint16_t c = (ui.st_xr >> k) & 1u ? C_RED : k == ui.st_step ? C_INK : C_DIM;
        g_text(x, y, &FONT_T, rv_stress_name(k), c);
        b[0] = 0;
        if (ui.st_res[k] == 0xFFFFu)
            sx_cat(b, "-");
        else {
            sx_int(b, ui.st_res[k]);
            if (ui.st_res2[k]) {                      /* and the second core's */
                int l = 0;
                sx_cat(b, "/");
                while (b[l])
                    l++;
                sx_int(b + l, ui.st_res2[k]);
            }
            sx_cat(b, (ui.st_xr >> k) & 1u ? "%!" : "%");
        }
        g_text_r(x + 110, y, &FONT_T, b, c);
    }
}

/* GLO > CPU: each part of the audio render as a share of the time it has, the whole, the
 * platform's own reading and the blocks that came too late */
static void pic_cpu(void)
{
    static const char *const nm[10] = {"CONTROL", "SYNTH", "STRIKE", "PERC", "TRACK FX", "BED", "FEEDBACK",
                                       "DELAY", "SPACE", "MASTER"};
    int k;
    float tot = 0.0f;
    char b[40];
    for (k = 0; k < 10; k++)
        tot += rv_live.prof[k];
    if (tot <= 0.0f) {
        g_text_c(120, PIC_Y + 40, &FONT_B, "NO CLOCK HERE", C_DIM);
        g_text_c(120, PIC_Y + 64, &FONT_T, "THE FM-1 AND THE SIMULATOR MEASURE IT", C_DIM);
        return;
    }
    for (k = 0; k < 10; k++) {
        int x = k < 6 ? 6 : 122, y = PIC_Y + 2 + (k % 6) * 15, w = (int)(clampf(rv_live.prof[k], 0.0f, 50.0f) * 1.0f);
        g_text(x, y, &FONT_T, nm[k], rv_live.prof[k] > 5.0f ? C_INK : C_DIM);
        g_fill(x + 60, y + 3, 50, 3, C_LINE);
        g_fill(x + 60, y + 3, w > 0 ? w : (rv_live.prof[k] > 0.05f ? 1 : 0), 3, rv_live.prof[k] > 20.0f ? C_RED : C_TEAL);
    }
    b[0] = 0;
    sx_cat(b, "RENDER ");                           /* the first core's (the guard's reading): the one that must keep up */
    if (rv_live.load > 0.0f)
        tot = rv_live.load;
    sx_int(b + 7, (int)(tot + 0.5f));
    sx_cat(b, "%");
    g_text(6, PIC_Y + 104, &FONT_B, b, tot > 90.0f ? C_RED : C_INK);
    if (plat_cpu_peak()) {                          /* the busiest audio block lately, the whole interrupt */
        b[0] = 0;
        sx_cat(b, "PEAK ");
        sx_int(b + 5, (int)plat_cpu_peak());
        sx_cat(b, "%");
        g_text_r(234, PIC_Y + 86, &FONT_T, b, plat_cpu_peak() > 95u ? C_RED : C_DIM);
    }
    b[0] = 0;
    sx_cat(b, "XRUNS ");
    sx_int(b + 6, (int)plat_xruns());
    g_text_r(234, PIC_Y + 95, &FONT_T, b, plat_xruns() ? C_RED : C_DIM);
    if (rv_live.fetch_on) {                         /* the share of the render spent waiting for its code */
        b[0] = 0;
        sx_cat(b, "FLASH WAIT ");
        sx_int(b + 11, (int)(rv_live.fetch_pct + 0.5f));
        sx_cat(b, "%");
        g_text_r(234, PIC_Y + 113, &FONT_T, b, rv_live.fetch_pct > 25.0f ? C_RED : C_DIM);
    }
    if (rv_live.cores2 || rv_live.c2_fail) {        /* the second core: its load (the buses, its synth voices) */
        if (rv_live.c2_fail || !rv_live.c2_used)
            g_text_r(234, PIC_Y + 104, &FONT_T, rv_live.c2_fail ? "CORE 2 FAILED: ONE CORE" : "CORE 2 IDLE", rv_live.c2_fail ? C_RED : C_DIM);
        else {
            b[0] = 0;
            sx_cat(b, "CORE 2 ");
            sx_int(b + 7, (int)(rv_live.c2_load + 0.5f));
            sx_cat(b, "%, WAIT ");                  /* and this core waiting for it */
            {
                int l = 0;
                while (b[l])
                    l++;
                sx_int(b + l, (int)(rv_live.c2_wait + 0.5f));
            }
            sx_cat(b, "%");
            g_text_r(234, PIC_Y + 104, &FONT_T, b, rv_live.c2_load > 90.0f ? C_RED : C_TEAL);
        }
    }
    b[0] = 0;                                       /* the overload guard */
    if (rv_live.guard_off)
        sx_cat(b, "GUARD OFF (STRESS)");
    else if (rv_live.cap == 255)
        sx_cat(b, "GUARD: ALL VOICES");
    else {
        sx_cat(b, "GUARD: ");
        sx_int(b + 7, rv_live.cap);
        sx_cat(b, " VOICES");
    }
    if (rv_live.shed) {
        int l = 0;
        sx_cat(b, ", ");
        while (b[l])
            l++;
        sx_int(b + l, rv_live.shed);
        sx_cat(b, " FADED");
    }
    g_text(6, PIC_Y + 125, &FONT_T, b, rv_live.cap == 255 && !rv_live.guard_off ? C_DIM : C_RED);
}

/* ARP: the held notes as they came, then one round of the pattern they make: a column a step, as
 * high as its note, the step just played lit */
static void pic_arp(void)
{
    int t = ui.track, n = rv_live.arp_n[t], mode = rv.tp[t][TP_ARPMODE], oct = clampi(rv.tp[t][TP_ARPOCT], 1, 4), i;
    char b[24];
    uint8_t held[RV_ARP_HELD];
    if (is_kit_ui(t)) {
        g_text_c(120, PIC_Y + 46, &FONT_B, "NOT ON A KIT", C_DIM);
        g_text_c(120, PIC_Y + 68, &FONT_T, "THE ARP PLAYS NOTES; A KIT'S KEYS ARE LANES", C_DIM);
        return;
    }
    if (n > RV_ARP_HELD)
        n = RV_ARP_HELD;
    for (i = 0; i < n; i++)
        held[i] = rv_live.arp_note[t][i];
    if (mode == ARP_OFF) {
        if (rv.tp[t][TP_ARPLATCH]) {                /* HOLD alone: the keys' notes sound on */
            for (i = 0; i < n; i++) {
                sx_note(b, held[i]);
                g_text_c(15 + i * 30, PIC_Y + 2, &FONT_T, b, C_INK);
            }
            g_text_c(120, PIC_Y + 46, &FONT_B, n ? "HOLDING" : "HOLD ON", C_AMBER);
            g_text_c(120, PIC_Y + 68, &FONT_T, n ? "THE NEXT CHORD REPLACES THESE" : "THE NOTES YOU PLAY SOUND ON", C_DIM);
            g_text_c(120, PIC_Y + 82, &FONT_T, "HOLD OFF LETS THEM GO", C_DIM);
            return;
        }
        g_text_c(120, PIC_Y + 46, &FONT_B, "ARP OFF", C_DIM);
        g_text_c(120, PIC_Y + 68, &FONT_T, "KNOB 1: UP, DOWN, UP-DN, DN-UP, PLAYED, RANDOM", C_DIM);
        g_text_c(120, PIC_Y + 82, &FONT_T, "KNOB 4, HOLD: THE KEYS' NOTES SOUND ON", C_DIM);
        return;
    }
    for (i = 0; i < n; i++) {                       /* the notes it holds */
        sx_note(b, held[i]);
        g_text_c(15 + i * 30, PIC_Y + 2, &FONT_T, b, C_INK);
    }
    if (!n) {
        g_text_c(120, PIC_Y + 46, &FONT_B, "HOLD SOME KEYS", C_DIM);
        g_text_c(120, PIC_Y + 68, &FONT_T, "OR PLAY STEPS: THEIR NOTES ARPEGGIATE", C_DIM);
    } else {
        int len = mode == ARP_RANDOM ? n * oct : rv_arp_len(n, mode, oct), lo = 127, hi = 0, show = len > 32 ? 32 : len;
        int pos = rv_live.arp_pos[t], first = len > 32 ? (pos / 32) * 32 : 0, w = 224 / (show > 0 ? show : 1), x0;
        int nt[32];
        for (i = 0; i < show; i++) {
            int k = first + i;
            if (mode == ARP_RANDOM) {               /* (no pattern: the notes it picks from, low to high) */
                uint8_t up[RV_ARP_HELD];
                int j, q;
                for (j = 0; j < n; j++)
                    up[j] = held[j];
                q = rv_arp_note(up, n, ARP_UP, oct, k);
                nt[i] = q;
            } else {
                nt[i] = rv_arp_note(held, n, mode, oct, k % len);
            }
            lo = nt[i] < lo ? nt[i] : lo;
            hi = nt[i] > hi ? nt[i] : hi;
        }
        if (w > 28)
            w = 28;
        x0 = 120 - show * w / 2;
        for (i = 0; i < show; i++) {
            int h = 10 + (hi > lo ? (nt[i] - lo) * 70 / (hi - lo) : 35), x = x0 + i * w;
            int lit = mode != ARP_RANDOM && (first + i) == pos % len;
            g_fill(x, PIC_Y + 104 - h, w > 3 ? w - 2 : 1, h, lit ? C_TEAL : C_AMBER_D);
        }
        b[0] = 0;
        sx_cat(b, mode == ARP_RANDOM ? "RANDOM FROM " : "");
        sx_int(b + (mode == ARP_RANDOM ? 12 : 0), mode == ARP_RANDOM ? n * oct : len);
        sx_cat(b, mode == ARP_RANDOM ? " NOTES" : " STEPS");
        g_text(8, PIC_Y + 110, &FONT_T, b, C_DIM);
    }
    if (rv.tp[t][TP_ARPLATCH])
        g_text_r(232, PIC_Y + 110, &FONT_T, "HOLD", C_AMBER);
    g_text(8, PIC_Y + 124, &FONT_T, rv.tp[t][TP_KIND] == KIND_COMP ? "" : "ON THE MASTER CLOCK, PLAYING OR NOT", C_DIM);
}

/* COMP: the gain reduction as a meter falling from the top (0 to 20 dB), and the curve it squeezes
 * the level by, with where the mix sits on it now */
static void pic_comp(void)
{
    float gr = rv_live.comp_gr, in = rv_live.comp_in, thr = clampf((float)rv.tp[7][TP_S0], -30.0f, 0.0f);
    static const float slope[3] = {0.5f, 0.75f, 0.9f};
    float sl = slope[clampi(rv.tp[7][TP_S1], 0, 2)];
    int i, h = (int)(clampf(gr / 20.0f, 0.0f, 1.0f) * 104.0f + 0.5f), x0 = 104, y0 = PIC_Y + 6, sz = 104;
    char b[16];
    g_rbox(20, PIC_Y + 6, 24, 104, 6, C_PANEL, 1);
    if (h > 0)
        g_fill(22, PIC_Y + 6, 20, h, gr > 10.0f ? C_RED : C_TEAL);
    for (i = 0; i <= 20; i += 4) {
        int y = PIC_Y + 6 + i * 104 / 20;
        g_fill(46, y, 4, 1, C_DIM);
        sx_int(b, i);
        g_text(52, y - 4, &FONT_T, b, C_DIM);
    }
    b[0] = '-';
    i = sx_int(b + 1, (int)(gr * 10.0f + 0.5f) / 10);
    b[1 + i] = '.';
    sx_int(b + 2 + i, (int)(gr * 10.0f + 0.5f) % 10);
    sx_cat(b, "DB");
    g_text_c(32, PIC_Y + 114, &FONT_T, gr > 0.05f ? b : "0DB", C_INK);
    /* the curve: in, -40..0 dB across; out down */
    g_frame(x0, y0, sz, sz, C_LINE);
    {
        int px = x0 + (int)((thr + 40.0f) * (float)sz / 40.0f);
        g_line(x0, y0 + sz, px, y0 + sz - (px - x0), C_DIM);
        g_line(px, y0 + sz - (px - x0), x0 + sz, y0 + sz - (px - x0) - (int)((float)(x0 + sz - px) * (1.0f - sl)), C_TEAL);
        if (in > -40.0f && rv.tp[7][TP_KIND] == KIND_COMP) {
            float o = in > thr ? thr + (in - thr) * (1.0f - sl) : in;
            int dx = x0 + (int)((fm_minf(in, 0.0f) + 40.0f) * (float)sz / 40.0f), dy = y0 + sz - (int)((o + 40.0f) * (float)sz / 40.0f);
            g_fill(dx - 2, dy - 2, 5, 5, C_AMBER);
        }
    }
    g_text(x0, y0 + sz + 4, &FONT_T, "IN", C_DIM);
    g_text_r(x0 + sz, y0 + sz + 4, &FONT_T, "THE MAIN MIX", C_DIM);
    g_text(216, y0 + 2, &FONT_T, "OUT", C_DIM);
}

/* FLASH: how the boot set the flash up, read once at start and never written: the chip, its status
 * (QE: its four-wire mode allowed), the controller's read mode and clock divider. A photo of it
 * says whether a faster read is possible on this unit */
static void sx_hex(char *o, uint32_t v, int digits)
{
    int i;
    for (i = 0; i < digits; i++)
        o[i] = "0123456789ABCDEF"[(v >> (4 * (digits - 1 - i))) & 15u];
    o[digits] = 0;
}
static void pic_flash(void)
{
    uint32_t v[4];
    char b[40];
    int y = PIC_Y + 4;
    g_text_r(234, PIC_Y + 124, &FONT_T, "RAVEL " RAVEL_BUILD, C_MID);   /* the version running */
    if (plat_flash_info(v) < 0) {
        g_text_c(120, PIC_Y + 46, &FONT_B, "NO FLASH HERE", C_DIM);
        g_text_c(120, PIC_Y + 68, &FONT_T, "THE FM-1 SHOWS ITS FLASH'S SETUP ON THIS PAGE", C_DIM);
        return;
    }
    b[0] = 0;
    sx_cat(b, "CHIP ");
    sx_hex(b + 5, v[0], 6);
    sx_cat(b, v[0] == 0x856014u ? "  PUYA 1 MB" : "  (NOT THE USUAL)");
    g_text(8, y, &FONT_T, b, C_INK);
    b[0] = 0;
    sx_cat(b, "STATUS ");
    sx_hex(b + 7, v[1] & 0xFFu, 2);
    sx_cat(b, " ");
    sx_hex(b + 10, (v[1] >> 8) & 0xFFu, 2);
    sx_cat(b, (v[1] >> 9) & 1u ? "   QE ON" : "   QE OFF");
    g_text(8, y + 16, &FONT_T, b, C_INK);
    b[0] = 0;
    sx_cat(b, "READ MODE ");
    sx_int(b + 10, (int)((v[2] >> 8) & 15u));
    sx_cat(b, "   CON ");
    sx_hex(b + sx_cat(b, ""), v[2], 8);
    g_text(8, y + 32, &FONT_T, b, C_INK);
    b[0] = 0;
    sx_cat(b, "CLOCK DIVIDER ");
    sx_int(b + 14, (int)(v[3] & 0xFFFFu));
    sx_cat(b, "   WORD ");
    sx_hex(b + sx_cat(b, ""), v[3], 8);
    g_text(8, y + 48, &FONT_T, b, C_INK);
    g_text(8, y + 74, &FONT_T, "READ AT START. NOTHING HERE", C_DIM);
    g_text(8, y + 88, &FONT_T, "WRITES THE FLASH OR CHANGES IT.", C_DIM);
    g_text(8, y + 108, &FONT_T, "A PHOTO OF THIS PAGE, PLEASE", C_AMBER);
}

/* MIDI IN: where notes go, the clock (its tempo, or that none comes), the last note */
static void pic_midi(void)
{
    char b[40];
    int sel = rv.gp[GP_MIDICH], y = PIC_Y + 4;
    static uint8_t seen;
    static uint32_t flash_ms;
    b[0] = 0;
    if (sel == 17)
        sx_cat(b, "NOTES: CHANNELS 1-8 PLAY T1-T8");
    else {
        sx_cat(b, sel ? "NOTES: CHANNEL " : "NOTES: ANY CHANNEL");
        if (sel)
            sx_int(b + sx_cat(b, ""), sel);
        sx_cat(b, " > THIS TRACK");
    }
    g_text(8, y, &FONT_T, b, C_INK);
    if (rv_live.midi_hit != seen) {
        seen = rv_live.midi_hit;
        flash_ms = plat_ms();
    }
    if (rv_live.midi_ch) {
        static const char *nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int n = rv_live.midi_note;
        b[0] = 0;
        sx_cat(b, "LAST: CH ");
        sx_int(b + sx_cat(b, ""), rv_live.midi_ch);
        sx_cat(b, "  ");
        sx_cat(b, nm[n % 12]);
        sx_int(b + sx_cat(b, ""), n / 12 - 1);
        g_text(8, y + 16, &FONT_T, b, plat_ms() - flash_ms < 150u ? C_WHITE : C_MID);
    } else
        g_text(8, y + 16, &FONT_T, "NO NOTES YET", C_DIM);
    b[0] = 0;
    if (!rv.gp[GP_MIDICLK])
        sx_cat(b, "CLOCK: RAVEL'S OWN (GLO > CLOCK)");
    else if (rv_live.midi_clk) {
        int bpm10 = (int)(rv_live.midi_bpm * 10.0f + 0.5f);
        sx_cat(b, "CLOCK: MIDI ");
        sx_int(b + sx_cat(b, ""), bpm10 / 10);
        sx_cat(b, ".");
        sx_int(b + sx_cat(b, ""), bpm10 % 10);
        sx_cat(b, " BPM");
    } else
        sx_cat(b, "CLOCK: MIDI, NONE COMING");
    g_text(8, y + 40, &FONT_T, b, rv.gp[GP_MIDICLK] && !rv_live.midi_clk ? C_RED : C_INK);
    g_fill(8, y + 56, 30, 30, g_mix(C_BG, C_TEAL, rv_live.clock_env));
    g_text(8, y + 96, &FONT_T, rv.gp[GP_MIDITRN] ? "START AND STOP: IGNORED" : "START AND STOP: FOLLOWED", C_DIM);
    g_text(8, y + 110, &FONT_T, "USB, TRS. CC1: WHEEL  PRESSURE: TOUCH", C_DIM);
}

static void pic_master(void)
{
    float l = fm_sqrtf(rv_live.master_l), r = fm_sqrtf(rv_live.master_r);
    meter_bar(84, PIC_Y + 6, 28, 112, l, C_PANEL, l > 0.91f ? C_RED : C_TEAL);
    meter_bar(128, PIC_Y + 6, 28, 112, r, C_PANEL, r > 0.91f ? C_RED : C_TEAL);
    g_text_c(98, PIC_Y + 122, &FONT_T, "L", C_DIM);
    g_text_c(142, PIC_Y + 122, &FONT_T, "R", C_DIM);
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

/* a KIT: six lanes of the current sixteen steps; accents bright, the chosen lane lit, each lane's
 * playhead where that lane is (lanes with their own length run apart) */
static void pic_drum(void)
{
    int t = ui.track, l, i, sel = ui.lane[t] % RV_LANES;
    const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    const rv_kit *kt = &rv.kit[t];
    int tlen = clampi(rv.tp[t][TP_LEN], 1, RV_STEPS);
    for (l = 0; l < RV_LANES; l++) {
        int y = PIC_Y + 6 + l * 20, len = kt->len[l] ? kt->len[l] : tlen;
        uint16_t lc = l == sel ? lane_col(t) : C_O_DIM;
        char nb[8];
        const char *mn = lane_mname(t, l);
        int j = 0;
        while (mn[j] && mn[j] != '-')
            j++;
        nb[0] = (char)('1' + l);
        nb[1] = ' ';
        nb[2] = mn[j] ? mn[j + 1] : mn[0];
        nb[3] = mn[j] && mn[j + 1] ? mn[j + 2] : 0;
        nb[4] = 0;
        if (l == sel)
            g_fill(0, y - 2, 240, 19, C_O_PANEL);
        if (rv.kit_rate[t][l] % 9) {                  /* a lane at its own rate: its name and the rate, stacked */
            g_text(4, y - 1, &FONT_T, nb, lc);
            g_text(4, y + 8, &FONT_T, rv_lrate_names[RV_LRATE_KNOB(rv.kit_rate[t][l] % 9)], l == sel ? C_INK : C_O_MID);
        } else
            g_text(4, y + 2, &FONT_XS, nb, lc);
        for (i = 0; i < 16; i++) {
            int si = ui.step_page * 16 + i, x = 40 + i * 12;
            const rv_step *s = &p->s[si];
            int on = (s->flags & ST_ON) && ((s->note >> l) & 1), acc = on && ((s->env >> l) & 1);
            uint16_t c = si >= len ? C_BG : on ? (acc ? C_WHITE : lc == C_O_DIM ? C_O_MID : lane_col(t)) : ((i & 3) ? C_O_LINE : C_O_DIM);
            if (si >= len)
                g_frame(x, y, 10, 14, C_O_LINE);
            else
                g_fill(x, y + (on ? 0 : 5), 10, on ? 14 : 4, c);
            if (on && ((s->len >> l) & 1))
                g_fill(x + 3, y - 2, 4, 2, C_TEAL);  /* ratchet */
            if (on) {                                /* a lock on this lane-step: a dot */
                int f, lk = 0;
                for (f = 0; f <= KL_LEVEL && !lk; f++)
                    lk = rv_kitlock_find(t, rv.pat_cur[t] % RV_PATTERNS, si, l, f) >= 0;
                if (lk)
                    g_fill(x + 3, y + 5, 4, 4, C_TEAL);
            }
            if (rv_live.playing && si == rv_live.lane_step[t][l])
                g_frame(x - 1, y - 1, 12, 16, C_TEAL);
        }
    }
}

/* a KIT lane's page: the six lanes as tabs, the machine, and every setting of the lane as a list,
 * the row the knobs hold lit and numbered */
static void pic_lane(void)
{
    static const char *const fam[4] = {"TRX: ANALOG-STYLE", "EFM: FM", "P-I: PHYSICAL MODEL", "GND"};
    int t = ui.track, l = ui.lane[t] % RV_LANES, m = rv.kit[t].mach[l] % PM_N, i;
    const rv_kit *kt = &rv.kit[t];
    uint16_t lc = lane_col(t);
    for (i = 0; i < RV_LANES; i++) {                  /* the tabs */
        char nb[2] = {(char)('1' + i), 0};
        int x = 4 + i * 39;
        g_fill(x, PIC_Y + 2, 36, 14, i == l ? lc : C_O_PANEL);
        g_text_c(x + 18, PIC_Y + 5, &FONT_XS, nb, i == l ? C_WHITE : C_O_DIM);
    }
    g_text(8, PIC_Y + 22, &FONT_B, lane_mname(t, l), lc);
    g_text_r(234, PIC_Y + 26, &FONT_T, fam[m <= PM_TRX_XC ? 0 : m <= PM_EFM_CY ? 1 : m <= PM_PI_CC ? 2 : 3], C_O_DIM);
    for (i = 0; i < 12; i++) {                        /* A-H, then LEVEL, CHANCE, LENGTH, RATE */
        int row = i < 4 ? 0 : i < 8 ? 1 : 2, col = i % 2, x = 6 + col * 118, y = PIC_Y + 46 + (i / 2) * 13;
        int lit = row == ui.lane_row % 3, v, mx = 127;
        const char *nm;
        char vb[8];
        if (i < 8) {
            nm = lane_pinfo(t, l, i)->name;
            v = kt->p[l][i];
            mx = 127;
        } else {
            static const char *const extra[4] = {"LEVEL", "CHANCE", "LENGTH", "RATE"};
            nm = extra[i - 8];
            v = i == 8 ? kt->level[l] : i == 9 ? kt->chance[l] : i == 10 ? kt->len[l] : RV_LRATE_KNOB(rv.kit_rate[t][l] % 9);
            mx = i == 9 ? 100 : i == 10 ? RV_STEPS : i == 11 ? 8 : 127;
        }
        if (!nm)
            continue;
        if (lit) {
            char kn[2] = {(char)('1' + (i < 8 ? i % 4 : i - 8)), 0};
            g_text(x, y, &FONT_XS, kn, C_TEAL);
        }
        g_text(x + 9, y, &FONT_XS, nm, lit ? C_INK : C_O_DIM);
        g_fill(x + 52, y + 3, 40, 3, C_O_LINE);
        g_fill(x + 52, y + 3, v * 40 / (mx ? mx : 1), 3, lit ? lc : C_O_MID);
        vb[0] = 0;
        if (i == 10 && v == 0)
            sx_cat(vb, "TRK");
        else if (i == 11)
            sx_cat(vb, rv_lrate_names[v % 9]);
        else
            sx_int(vb, v);
        g_text_r(x + 114, y, &FONT_XS, vb, lit ? C_INK : C_O_DIM);
    }
}

static void pic_steps(void)
{
    int t = ui.track, len = clampi(rv.tp[t][TP_LEN], 1, 64), i, play = rv_live.step[t][0], play2 = rv_live.step[t][1];
    const rv_pattern *p = &rv.pat[t][rv.pat_cur[t] % RV_PATTERNS];
    char b[8];
    /* the 64-step overview */
    for (i = 0; i < 64; i++) {
        int x = 8 + (i % 16) * 14 + (i / 16) * 0, y = PIC_Y + 2 + (i / 16) * 6;
        uint16_t c = i >= len ? C_BG : (p->s[i].flags & ST_ON) ? kind_dcol(t) : C_LINE;
        if (i / 16 == ui.step_page && i < len)
            c = (p->s[i].flags & ST_ON) ? g_mix(kind_dcol(t), C_INK, 0.5f) : C_DIM;
        if (i == play)
            c = C_WHITE;
        if (i == rv_live.srec_step && ui.srec_t == t)
            c = C_RED;
        g_rbox(x, y, 12, 3, 1, c, 1);
    }
    /* the 16 keys' steps, large */
    for (i = 0; i < 16; i++) {
        int si = ui.step_page * 16 + i, x = 8 + (i % 8) * 28, y = PIC_Y + 30 + (i / 8) * 46;
        const rv_step *s = &p->s[si];
        uint16_t lt = g_mix(kind_dcol(t), C_INK, 0.55f);
        if (si >= len)
            continue;
        if (s->flags & ST_ON)                         /* a step that plays: filled in the track's colour */
            g_rbox(x, y, 25, 42, 4, kind_dcol(t), 1);
        else
            g_rbox(x, y, 25, 42, 4, C_LINE, 0);
        if (si == play || si == play2)
            g_rbox(x, y, 25, 42, 4, C_WHITE, 0);
        if (ui.srec_t == t && si == rv_live.srec_step) {   /* step recording: the next note goes here */
            g_rbox(x - 2, y - 2, 29, 46, 5, C_RED, 0);
            g_rbox(x - 1, y - 1, 27, 44, 5, C_RED, 0);
        } else if (si == ui.step_cur)                 /* the step the knobs edit: ringed in KNOB 1's colour */
            g_rbox(x - 2, y - 2, 29, 46, 5, C_K1, 0);
        if (s->flags & ST_ON) {
            int vh = s->vel * 20 / 127;
            if (s->flags & ST_JUMP) {
                sx_cat(b, "");
                b[0] = 'J';
                sx_int(b + 1, s->note + 1);
            } else {
                sx_note(b, s->note);
            }
            g_text_c(x + 13, y + 3, &FONT_T, b, C_INK);
            if (!(s->flags & ST_JUMP) && (s->chord[0] || s->chord[1] || s->chord[2])) {   /* a chord: how many notes */
                int nn = 1 + (s->chord[0] != 0) + (s->chord[1] != 0) + (s->chord[2] != 0);
                char cb[4] = {'X', (char)('0' + nn), 0, 0};
                g_text_c(x + 13, y + 12, &FONT_T, cb, C_TEAL);
            }
            g_rbox(x + 4, y + 38 - vh, 2, vh > 2 ? vh : 2, 1, lt, 1);   /* its velocity */
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
            if (s->cut & 3)
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
        if (ui.srec_t == t && rv_live.srec_step >= 0) {
            char sb[16] = "STEP REC ";
            sx_int(sb + 9, rv_live.srec_step + 1);
            g_text_r(232, PIC_Y + 124, &FONT_T, sb, C_RED);
        }
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
        if (len > 16 && i / 16 == ui.step_page)      /* the steps on the keys now: slate */
            c = (s->flags & ST_ON) ? C_STRIKE : C_STRIKE_D;
        if (i == rv_live.step[t][0])
            c = C_WHITE;
        if (i == ui.step_cur)                         /* the step on the knobs: its colour, a slate edge right against it */
            g_ring((float)xs[i] + 0.5f, (float)ys[i] + 0.5f, 4.9f, 1.2f, C_STRIKE);
        g_disc(xs[i], ys[i], i == ui.step_cur ? 4 : 2, i == ui.step_cur ? C_TEAL : c);
        if (i == rv_live.step[t][1])
            g_disc(xs[i], ys[i], 3, C_AMBER);
    }
    if (len > 16 || ui.step_cur > 0) {                /* the step on the knobs */
        char sb[12] = "STEP ";
        sx_int(sb + 5, ui.step_cur + 1);
        g_text(6, PIC_Y + 112, &FONT_T, sb, C_TEAL);
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
        if (t == ui.track)
            g_rbox(x - 4, PIC_Y + 2, 28, 122, 6, C_PANEL, 1);
        meter_bar(x, PIC_Y + 6, 10, 100, (float)h / 100.0f, C_LINE, muted ? C_DIM : kind_col(t));
        g_rbox(x + 15, PIC_Y + 6, 2, 100, 1, C_LINE, 1);   /* the fader: a rail and a handle at its level */
        g_rbox(x + 11, PIC_Y + 103 - lv, 10, 6, 3, t == ui.track ? C_WHITE : g_mix(kind_col(t), C_INK, 0.4f), 1);
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
        g_rbox(x, y, 24, 34, 5, c, 1);
        g_rbox(x, y, 24, 34, 5, s == rv.scene_cur ? C_TEAL : s == ui.cur_scene ? C_MID : C_LINE, 0);
        sx_int(b, s + 1);
        g_text_c(x + 12, y + 10, &FONT_XS, b, sc->used ? C_INK : C_DIM);
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
        if (rv.arr[i].scene >= RV_ARR_END) {
            b[0] = 'E';
            b[1] = 'N';
            b[2] = 'D';
            b[3] = 0;
        } else
            sx_int(b, rv.arr[i].scene + 1);
        if (w > 10)
            g_text_c(x + w / 2, PIC_Y + 42, &FONT_XS, b, C_INK);
        x += w;
    }
    g_text(8, PIC_Y + 80, &FONT_T, rv.gp[GP_SONGMODE] ? "PLAYS THE ARRANGEMENT (SONG)" : "PLAYS PATTERNS: SET PLAYS TO SONG", C_MID);
    g_text(8, PIC_Y + 92, &FONT_T, "NUMBERS ARE SCENES. END STOPS THE SONG", C_DIM);
    g_text(8, PIC_Y + 104, &FONT_T, "NO END: THE SONG LOOPS", C_DIM);
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
        int b = (rv_live.bar >> clampi(rv.gp[GP_ARCSPAN], 0, 2)) & 15;   /* (a point lasts SPAN bars) */
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
    int r, top = clampi(ui.cur_slot - 4, 0, PLAT_PROJECTS - 8);   /* eight of the 32, around the chosen one */
    for (r = 0; r < 8; r++) {
        int i = top + r, y = PIC_Y + 2 + r * 15;
        char b[20];
        if (i == ui.cur_slot)
            g_fill(0, y - 1, 240, 14, ui.save_confirm ? C_AMBER_D : C_PANEL);
        sx_int(b, i + 1);
        g_text(8, y, &FONT_XS, b, C_DIM);
        g_text(26, y, &FONT_XS, ui.slot_used[i] ? ui.slot_names[i] : "EMPTY", ui.slot_used[i] ? C_INK : C_DIM);
    }
    {   /* the project's name (CURSOR and LETTER edit it; SAVE keeps it with the slot) */
        char b[16];
        int k, x;
        g_text(132, PIC_Y + 2, &FONT_T, "NAME", C_DIM);
        g_text(132, PIC_Y + 14, &FONT_XS, rv.name[0] ? rv.name : "-", C_INK);
        for (k = 0; k < ui.proj_cursor && k < (int)sizeof rv.name - 1; k++)
            b[k] = rv.name[k] ? rv.name[k] : ' ';
        b[k] = 0;
        x = 132 + g_text_w(&FONT_XS, b);
        g_fill(x, PIC_Y + 25, 7, 2, C_AMBER);
        g_text(132, PIC_Y + 34, &FONT_T, "2: CURSOR  3: LETTER", C_DIM);
        if (ui.room >= 0) {                           /* the flash: how much room the projects have left */
            b[0] = 0;
            sx_int(b + sx_cat(b, "ROOM "), ui.room);
            sx_cat(b, "%");
            g_text(132, PIC_Y + 104, &FONT_T, b, ui.room < 10 ? C_RED : C_DIM);
            g_fill(132, PIC_Y + 116, 96, 4, C_LINE);
            g_fill(132, PIC_Y + 116, 96 * ui.room / 100, 4, ui.room < 10 ? C_RED : C_AMBER);
        }
    }
}

/* SOUNDS: nine slots of the bank around the chosen one, each with its kind on the right */
static void pic_bank(void)
{
    int r, top = clampi(ui.cur_sound - 4, 0, RV_BANK_N - 9);
    for (r = 0; r < 9; r++) {
        int i = top + r, y = PIC_Y + 2 + r * 15, used = rv_bank_used(i);
        char b[20];
        if (i == ui.cur_sound)
            g_fill(0, y - 1, 240, 14, ui.save_confirm ? C_AMBER_D : C_PANEL);
        b[0] = 0;
        sx_int(b + sx_cat(b, bank_letters[rv_bank_current()]), i + 1);
        g_text(4, y, &FONT_XS, b, C_DIM);
        if (!used) {
            g_text(32, y, &FONT_XS, "EMPTY", C_LINE);
            continue;
        }
        {
            const char *nm = rv_bank_name(i);
            int kind = rv_bank_kind(i);
            uint16_t kc = rv_bank_is_kit(i) || kind == KIND_PERC ? C_PERC : kind == KIND_STRIKE ? C_STRIKE
                          : C_SYNTH;
            g_text(32, y, &FONT_XS, nm[0] ? nm : "-", C_INK);
            if (i == ui.cur_sound && ui.sec == SEC_SAVE) {   /* the name's cursor */
                int k, x;
                for (k = 0; k < ui.bank_cursor && k < RV_BANK_NAME - 1; k++)
                    b[k] = nm[k] ? nm[k] : ' ';
                b[k] = 0;
                x = 32 + g_text_w(&FONT_XS, b);
                g_fill(x, y + 11, 7, 2, C_AMBER);
            }
            if (kind == KIND_SYNTH)
                g_text(138, y + 2, &FONT_T, rv_engine_names[rv_bank_engine(i) % 12], C_DIM);
            g_text_r(234, y, &FONT_XS, rv_bank_kind_label(i), kc);
        }
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
    case PIC_LFO1:
    case PIC_LFO2: pic_lfo(0); pic_lfo(1); break;
    case PIC_DUST: pic_dust(); break;
    case PIC_MACRO: pic_macro(); break;
    case PIC_FB: pic_fb(); break;
    case PIC_DELAY: pic_delay(); break;
    case PIC_SPACE: pic_space(); break;
    case PIC_MASTER: pic_master(); break;
    case PIC_BED: pic_bed(); break;
    case PIC_CPU: pic_cpu(); break;
    case PIC_STRESS: pic_stress(); break;
    case PIC_EUCLID: pic_euclid(); break;
    case PIC_SCALE: pic_scale(); break;
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
    case PIC_DRUM: pic_drum(); break;
    case PIC_LANE: pic_lane(); break;
    case PIC_BANK: pic_bank(); break;
    case PIC_FENV: pic_fenv(); break;
    case PIC_SENV: pic_senv(); break;
    case PIC_ARP: pic_arp(); break;
    case PIC_COMP: pic_comp(); break;
    case PIC_FLASH: pic_flash(); break;
    case PIC_MIDI: pic_midi(); break;
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
        int kit = is_kit_ui(t), l = ui.lane[t] % RV_LANES;
        for (i = 0; i < 16; i++) {
            int si = ui.step_page * 16 + i, on = kit ? (p->s[si].flags & ST_ON) && ((p->s[si].note >> l) & 1) : (p->s[si].flags & ST_ON) != 0;
            int here = kit ? rv_live.playing && si == rv_live.lane_step[t][l] : si == rv_live.step[t][0];
            if (ui.srec_t == t && si == rv_live.srec_step)
                on = (plat_ms() / 200u) & 1u, here = 0;   /* step recording: the cursor's key blinks */
            if (on ^ here)
                lk |= 1u << white_keys[i];
        }
        lk |= 1u << black_keys[rv.pat_cur[t] % RV_PATTERNS];
        if (kit) {
            lk |= 1u << black_keys[2 + l];
        }
    } else {
        lk = plat_keys();
    }
    plat_leds(lb, lk);
}

#include "rv_moom.c"

void ui_frame(void)
{
    int b, i, j;
    if (mm.on) {                                      /* the hidden game has the screen and the panel */
        moom_frame();
        return;
    }
    ui_input_only();
    if (!ui.c2_tried) {                               /* the second core, on from the start (GLO > CPU, CORES) */
        ui.c2_tried = 1;
        if (plat_par_on(1) == 0)
            rv_live.cores2 = 1;
    }
    stress_tick();
    if (ui.bank_dirty && (plat_ms() - ui.bank_edit_ms > 1500u || ui.sec != SEC_SAVE))
        bank_keep();                                  /* a renamed sound, kept once the naming pauses */
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
    ui.srec_t = ui.srec_seen = -1;
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

/* a slot written from a computer: the lists follow, and the screen says what came */
void ui_storage_changed(int slot)
{
    char b[24] = "RECEIVED ";
    rv_bank_reload(slot);
    slots_refresh();
    if (plat_is_bank(slot)) {
        sx_cat(b, "BANK ");
        sx_cat(b, bank_letters[(slot - PLAT_BANK0) & 3]);
    } else {
        sx_cat(b, "PROJECT ");
        sx_int(b + 17, plat_proj_of(slot) + 1);
    }
    msg(b);
}
