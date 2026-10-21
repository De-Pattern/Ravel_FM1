/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: sound presets (PRESETS on the voice pages) and the demo projects. */
#include "rv.h"

typedef struct { const char *name; uint8_t kind, engine; const int8_t *v; const rv_cz *cz; } rv_preset;   /* v: (parameter, value) pairs; cz: a CZ tone */
#define END -1

/* ---------------------------------------------------------------- synth --- */
static const int8_t p_glass[] = {TP_S0, 0, TP_S2, 5, TP_S3, 44, TP_S9, 20, TP_CUT, 72, TP_RES, 18, TP_EFILT, 14, TP_ATK, 62, TP_DEC, 84,
                                  TP_SUS, 100, TP_REL, 92, TP_GIVE, 30, TP_REVS, 70, TP_DLYS, 30, TP_POLY, 3, TP_LEVEL, 84, END};
static const int8_t p_sub[] = {TP_S0, 90, TP_S4, 90, TP_CUT, 52, TP_RES, 34, TP_EFILT, 34, TP_ATK, 2, TP_DEC, 52, TP_SUS, 70, TP_REL, 34,
                                TP_POLY, 0, TP_GLIDE, 24, TP_PITCH, -12, TP_DRIVE, 30, TP_LEVEL, 96, END};
static const int8_t p_pluck[] = {TP_S0, 20, TP_S9, 6, TP_CUT, 46, TP_RES, 44, TP_EFILT, 46, TP_ATK, 0, TP_DEC, 84, TP_SUS, 0, TP_REL, 44,
                                  TP_REVS, 34, TP_DLYS, 40, TP_LEVEL, 127, END};
/* CZ tones (original patches, in the CZ's own units). General: S0 LINE (0 1, 1 2, 2 1+1', 3 1+2'),
 * S1 OCTAVE (1 = 0), S2 MOD, S3 CHORUS, S4-S7 detune sign, octave, note, fine, S8-S11 vibrato wave,
 * delay, rate, depth, S12 DCW +-, S13 FOAM, S14 VEL DCW. A line: wave 1, wave 2, DCA and DCW key
 * follow, then DCO, DCW, DCA as {rates}, {levels}, sustain step, end step. */
#define CZ_FLAT {{99, 99, 99, 99, 99, 99, 99, 99}, {0}, 0, 1}
#define CZ_OPEN_FILTER TP_FTYPE, FT_LP, TP_CUT, 127, TP_RES, 0, TP_EFILT, 0, TP_KTRK, 0
static const rv_cz cz_brass = {{
    {1, 0, 2, 3, {{{99, 62}, {6, 0}, 0, 2},                      /* a breath of pitch on the attack */
                  {{58, 38, 50}, {86, 60, 0}, 2, 3},
                  {{72, 50, 56}, {99, 90, 0}, 2, 3}}},
    {1, 0, 2, 3, {CZ_FLAT, {{58, 38, 50}, {86, 60, 0}, 2, 3}, {{72, 50, 56}, {99, 90, 0}, 2, 3}}}}};
static const int8_t p_czbrass[] = {TP_S0, 2, TP_S7, 6, TP_S8, 0, TP_S9, 30, TP_S10, 74, TP_S11, 9, TP_S3, 1, CZ_OPEN_FILTER,
                                    TP_LEVEL, 74, END};
static const rv_cz cz_bell = {{
    {4, 0, 3, 2, {CZ_FLAT, {{99, 46, 60}, {62, 12, 0}, 0, 3}, {{99, 38, 99}, {99, 0, 0}, 0, 2}}},
    {3, 0, 3, 2, {CZ_FLAT, {{99, 40}, {40, 0}, 0, 2}, {{99, 34}, {94, 0}, 0, 2}}}}};
static const int8_t p_czbell[] = {TP_S0, 3, TP_S2, 1, TP_S5, 1, TP_S6, 7, TP_S7, 3, TP_S3, 2, CZ_OPEN_FILTER, TP_REVS, 50,
                                   TP_LEVEL, 88, END};
static const rv_cz cz_foam = {{
    {2, 4, 1, 4, {CZ_FLAT, {{40, 30, 44}, {90, 50, 0}, 2, 3}, {{60, 99, 58}, {99, 99, 0}, 2, 3}}},
    {2, 4, 1, 4, {CZ_FLAT, {{40, 30, 44}, {90, 50, 0}, 2, 3}, {{60, 99, 58}, {99, 99, 0}, 2, 3}}}}};
static const int8_t p_foam[] = {TP_S0, 0, TP_S13, 96, TP_S8, 0, TP_S10, 60, TP_S11, 6, TP_S3, 3, CZ_OPEN_FILTER, TP_DLYS, 40,
                                 TP_LEVEL, 80, END};
static const rv_cz cz_strings = {{
    {1, 0, 1, 2, {CZ_FLAT, {{44, 40, 45}, {58, 46, 0}, 2, 3}, {{52, 99, 50}, {99, 99, 0}, 2, 3}}},
    {1, 0, 1, 2, {CZ_FLAT, {{44, 40, 45}, {58, 46, 0}, 2, 3}, {{52, 99, 50}, {99, 99, 0}, 2, 3}}}}};
static const int8_t p_czstrings[] = {TP_S0, 2, TP_S7, 12, TP_S8, 0, TP_S9, 40, TP_S10, 70, TP_S11, 10, TP_S3, 2, CZ_OPEN_FILTER,
                                      TP_LEVEL, 68, END};
static const rv_cz cz_reso = {{
    {6, 0, 0, 2, {CZ_FLAT, {{48, 42, 60, 50}, {99, 30, 60, 0}, 3, 4}, {{99, 99, 55}, {99, 92, 0}, 2, 3}}},
    {6, 0, 0, 2, {CZ_FLAT, {{48, 42, 60, 50}, {99, 30, 60, 0}, 3, 4}, {{99, 99, 55}, {99, 92, 0}, 2, 3}}}}};
static const int8_t p_czreso[] = {TP_S0, 0, TP_S3, 1, CZ_OPEN_FILTER, TP_LEVEL, 105, END};
static const rv_cz cz_bass = {{
    {1, 0, 0, 2, {CZ_FLAT, {{99, 62, 60}, {88, 22, 0}, 2, 3}, {{99, 58, 72}, {99, 78, 0}, 2, 3}}},
    {2, 0, 0, 2, {CZ_FLAT, {{99, 55, 60}, {60, 10, 0}, 2, 3}, {{99, 60, 72}, {90, 70, 0}, 2, 3}}}}};
static const int8_t p_czbass[] = {TP_S0, 3, TP_S1, 0, TP_S4, 1, TP_S5, 1, TP_S7, 2, CZ_OPEN_FILTER, TP_POLY, 0, TP_LEVEL, 88, END};
static const rv_cz cz_noise = {{
    {5, 0, 0, 0, {{{99, 50}, {40, 0}, 0, 2}, {{99, 52}, {70, 0}, 0, 2}, {{99, 48}, {99, 0}, 0, 2}}},
    {7, 1, 0, 0, {CZ_FLAT, {{99, 46}, {99, 0}, 0, 2}, {{99, 44}, {92, 0}, 0, 2}}}}};
static const int8_t p_cznoise[] = {TP_S0, 3, TP_S2, 2, TP_S5, 1, CZ_OPEN_FILTER, TP_LEVEL, 74, END};
static const int8_t p_fmep[] = {TP_S0, 3, TP_S2, 3, TP_S3, 3, TP_S4, 3, TP_S5, 23, TP_S6, 74, TP_S7, 92, TP_S8, 40, TP_S13, 62,
                                 TP_FTYPE, FT_LP, TP_CUT, 120, TP_EFILT, 0, TP_ATK, 0, TP_DEC, 92, TP_SUS, 50, TP_REL, 70, TP_REVS, 40, TP_LEVEL, 110, END};
static const int8_t p_fmbell[] = {TP_S0, 0, TP_S3, 27, TP_S4, 28, TP_S5, 19, TP_S6, 64, TP_S7, 40, TP_S8, 24, TP_S13, 84, TP_FTYPE, FT_LP,
                                   TP_CUT, 124, TP_ATK, 0, TP_DEC, 100, TP_SUS, 10, TP_REL, 96, TP_REVS, 64, TP_DLYS, 40, TP_LEVEL, 90, END};
static const int8_t p_fmgrowl[] = {TP_S0, 1, TP_S1, 96, TP_S3, 1, TP_S4, 5, TP_S6, 80, TP_S7, 50, TP_S8, 60, TP_S13, 50, TP_S14, 40,
                                    TP_CUT, 70, TP_RES, 40, TP_EFILT, 30, TP_POLY, 0, TP_PITCH, -12, TP_DRIVE, 40, TP_LEVEL, 108, END};
static const int8_t p_string[] = {TP_S0, 0, TP_S1, 92, TP_S2, 20, TP_S3, 22, TP_S7, 100, TP_FTYPE, FT_LP, TP_CUT, 120, TP_EFILT, 0,
                                   TP_ATK, 0, TP_DEC, 110, TP_SUS, 127, TP_REL, 96, TP_REVS, 40, TP_LEVEL, 83, END};
/* RESIN: the bowed string and the reed, pushed toward breaking down */
#define RS_OPEN TP_FTYPE, FT_LP, TP_CUT, 122, TP_RES, 10, TP_EFILT, 0
static const int8_t p_wraith[] = {TP_S0, 0, TP_PITCH, -12, TP_S1, 95, TP_S2, 38, TP_S3, 30, TP_S4, 70, TP_S6, 108, TP_S7, 110, TP_S8, 66,
                                  TP_S9, 77, TP_S10, 70, TP_S11, 61, TP_S12, 30, TP_S13, 54, TP_S14, 60, TP_S15, 25, RS_OPEN,
                                  TP_ATK, 83, TP_SUS, 127, TP_REL, 107, TP_REVS, 50, TP_LEVEL, 100, END};
static const int8_t p_upstairs[] = {TP_S0, 0, TP_S1, 91, TP_S2, 48, TP_S3, 20, TP_S4, 55, TP_S5, 74, TP_S6, 80, TP_S7, 15, TP_S8, 57,
                                    TP_S9, 60, TP_S10, 20, TP_S11, 36, TP_S12, 55, TP_S13, 30, TP_S14, 70, TP_S15, 30, RS_OPEN,
                                    TP_POLY, 0, TP_GLIDE, 34, TP_ATK, 60, TP_SUS, 127, TP_REL, 80, TP_REVS, 30, TP_LEVEL, 100, END};
static const int8_t p_violin[] = {TP_S0, 0, TP_S1, 70, TP_S2, 50, TP_S3, 22, TP_S4, 15, TP_S6, 80, TP_S7, 10, TP_S8, 55, TP_S9, 35,
                                  TP_S10, 60, TP_S11, 20, RS_OPEN, TP_ATK, 55, TP_SUS, 127, TP_REL, 70, TP_REVS, 40, TP_LEVEL, 100, END};
static const int8_t p_foghorn[] = {TP_S0, 1, TP_PITCH, -12, TP_S1, 104, TP_S2, 86, TP_S3, 40, TP_S4, 44, TP_S5, 57, TP_S6, 40, TP_S7, 100,
                                   TP_S8, 40, TP_S9, 10, TP_S10, 90, TP_S11, 30, TP_S12, 25, TP_S13, 30, TP_S14, 60, TP_S15, 23, RS_OPEN,
                                   TP_ATK, 40, TP_SUS, 127, TP_REL, 70, TP_REVS, 40, TP_LEVEL, 78, END};
static const int8_t p_throatsax[] = {TP_S0, 1, TP_PITCH, -12, TP_S1, 90, TP_S2, 70, TP_S3, 30, TP_S4, 90, TP_S5, 30, TP_S6, 90, TP_S7, 70,
                                     TP_S8, 60, TP_S9, 25, TP_S10, 40, TP_S11, 25, TP_S12, 45, TP_S13, 20, TP_S14, 50, TP_S15, 40, RS_OPEN,
                                     TP_ATK, 30, TP_SUS, 127, TP_REL, 60, TP_REVS, 30, TP_LEVEL, 100, END};
static const int8_t p_bowed[] = {TP_S0, 1, TP_S1, 70, TP_S4, 110, TP_S7, 110, TP_S6, 60, TP_FTYPE, FT_LP, TP_CUT, 110, TP_EFILT, 0,
                                  TP_ATK, 56, TP_SUS, 116, TP_REL, 76, TP_GIVE, 30, TP_REVS, 60, TP_LEVEL, 66, END};
static const int8_t p_breath[] = {TP_S0, 2, TP_S1, 60, TP_S4, 96, TP_S7, 100, TP_FTYPE, FT_LP, TP_CUT, 100, TP_EFILT, 10, TP_ATK, 40,
                                   TP_SUS, 110, TP_REL, 60, TP_REVS, 60, TP_LEVEL, 59, END};
static const int8_t p_membrane[] = {TP_S0, 3, TP_S1, 80, TP_S2, 30, TP_S5, 30, TP_S7, 70, TP_FTYPE, FT_LP, TP_CUT, 127, TP_EFILT, 0,
                                     TP_ATK, 0, TP_SUS, 127, TP_REL, 100, TP_LEVEL, 75, END};
static const int8_t p_vowel[] = {TP_S0, 30, TP_S2, 44, TP_S5, 72, TP_S7, 34, TP_FTYPE, FT_LP, TP_CUT, 116, TP_EFILT, 0, TP_ATK, 24,
                                  TP_SUS, 110, TP_REL, 66, TP_GIVE, 24, TP_REVS, 50, TP_LEVEL, 62, TP_EDST1, TP_S0, TP_EAMT1, 20, END};
static const int8_t p_fof[] = {TP_S0, 70, TP_S2, 30, TP_S3, 1, TP_S6, 40, TP_S7, 20, TP_FTYPE, FT_LP, TP_CUT, 124, TP_EFILT, 0,
                                TP_ATK, 40, TP_SUS, 110, TP_REL, 80, TP_REVS, 70, TP_POLY, 3, TP_LEVEL, 106, END};
static const int8_t p_pulsar[] = {TP_S0, 1, TP_S1, 30, TP_S2, 2, TP_S3, 40, TP_S4, 50, TP_S6, 84, TP_FTYPE, FT_LP, TP_CUT, 110,
                                   TP_EFILT, 10, TP_ATK, 10, TP_DEC, 80, TP_SUS, 80, TP_REL, 70, TP_DLYS, 50, TP_LEVEL, 108, END};
static const int8_t p_swarm[] = {TP_S0, 3, TP_S1, 50, TP_S2, 3, TP_S3, 70, TP_S4, 96, TP_S5, 40, TP_S6, 60, TP_FTYPE, FT_LP, TP_CUT, 100,
                                  TP_ATK, 50, TP_SUS, 110, TP_REL, 90, TP_GIVE, 40, TP_REVS, 70, TP_LEVEL, 96, END};
static const int8_t p_dustvoice[] = {TP_S0, 7, TP_S1, 20, TP_S2, 2, TP_S5, 96, TP_S6, 96, TP_FTYPE, FT_BP, TP_CUT, 100, TP_RES, 50,
                                      TP_EFILT, 0, TP_ATK, 0, TP_SUS, 110, TP_REL, 60, TP_DLYS, 60, TP_LEVEL, 127, END};
/* CHIP: S0 WAVE, S1 PW, S2 RATE, S3 MODE, S4-S6 OF2-OF4, S7 STEPS, S8 SUB, S9 SUB OCT, S10 SYNC,
 * S11 OVER, S12 SRR, S13 CRUSH, S14 DECOMP, S15 FRAG */
static const int8_t p_chiparp[] = {TP_S0, CW_PULSE, TP_S1, 40, TP_S2, 92, TP_S3, 0, TP_S4, 3, TP_S5, 7, TP_S6, 12, TP_S7, 4,
                                   TP_CUT, 127, TP_ATK, 0, TP_DEC, 60, TP_SUS, 90, TP_REL, 20, TP_LEVEL, 108, END};
static const int8_t p_sidbass[] = {TP_S0, CW_SAWPUL, TP_S1, 70, TP_S7, 1, TP_S8, 70, TP_S9, 2, TP_S11, 30, TP_FTYPE, FT_MS20,
                                   TP_CUT, 74, TP_RES, 70, TP_EFILT, 30, TP_ATK, 0, TP_DEC, 50, TP_SUS, 70, TP_REL, 15,
                                   TP_LEVEL, 127, END};
static const int8_t p_chipkick[] = {TP_S0, CW_KICK, TP_S7, 1, TP_S11, 30, TP_CUT, 127, TP_ATK, 0, TP_DEC, 40, TP_SUS, 0,
                                    TP_REL, 10, TP_LEVEL, 127, END};
static const int8_t p_chipsnare[] = {TP_S0, CW_SNARE, TP_S7, 1, TP_S13, 8, TP_CUT, 127, TP_ATK, 0, TP_DEC, 74, TP_SUS, 0,
                                     TP_REL, 10, TP_LEVEL, 127, END};
static const int8_t p_decomposed[] = {TP_S0, CW_PWM, TP_S1, 90, TP_S2, 70, TP_S3, 3, TP_S4, 7, TP_S5, 12, TP_S7, 3, TP_S12, 40,
                                      TP_S13, 6, TP_S14, 70, TP_S15, 40, TP_CUT, 120, TP_ATK, 0, TP_DEC, 70, TP_SUS, 80,
                                      TP_REL, 30, TP_LEVEL, 110, TP_REVS, 40, END};
static const int8_t p_syncarp[] = {TP_S0, CW_SAW, TP_S2, 86, TP_S3, 2, TP_S4, 7, TP_S5, 12, TP_S6, 19, TP_S7, 4, TP_S8, 40,
                                   TP_S9, 2, TP_S10, 1, TP_FTYPE, FT_SH101, TP_CUT, 96, TP_RES, 40, TP_ATK, 0, TP_DEC, 70,
                                   TP_SUS, 80, TP_REL, 25, TP_LEVEL, 127, END};
/* the new filters, on VA */
static const int8_t p_101bass[] = {TP_FTYPE, FT_SH101, TP_CUT, 58, TP_RES, 64, TP_EFILT, 40, TP_ATK, 0, TP_DEC, 45,
                                   TP_SUS, 40, TP_REL, 20, TP_POLY, 0, TP_LEVEL, 127, END};
static const int8_t p_sembrass[] = {TP_FTYPE, FT_SEM, TP_FMODE, 0, TP_CUT, 52, TP_RES, 46, TP_EFILT, 44, TP_KTRK, 40, TP_ATK, 20,
                                    TP_DEC, 62, TP_SUS, 80, TP_REL, 40, TP_LEVEL, 92, END};
static const int8_t p_semnotch[] = {TP_FTYPE, FT_SEM, TP_FMODE, 64, TP_CUT, 66, TP_RES, 70, TP_EFILT, 20, TP_ATK, 70,
                                    TP_SUS, 110, TP_REL, 80, TP_REVS, 50, TP_LEVEL, 119, END};
static const int8_t p_msscream[] = {TP_FTYPE, FT_MS20, TP_CUT, 70, TP_RES, 112, TP_EFILT, 36, TP_ATK, 0, TP_DEC, 60,
                                    TP_SUS, 60, TP_REL, 30, TP_LEVEL, 115, END};
static const int8_t p_lpg[] = {TP_S0, 40, TP_S1, 30, TP_S9, 8, TP_FTYPE, FT_LPG, TP_CUT, 110, TP_RES, 10, TP_ATK, 0, TP_DEC, 84, TP_SUS, 0,
                                TP_REL, 40, TP_REVS, 40, TP_LEVEL, 127, END};

/* SQ (after the SQ-80): S0-S2 WAVE 1-3 (0 SAW, 1 SQUARE, 2 PULSE, 3 SINE, 4 TRI, 5 BELL, 6 BASS,
 * 7 PIANO, 8 EPIANO, 9 VOICE1, 10 VOICE2, 11 ORGAN, 12 REED, 13 SYNTH1, 14 SYNTH2, 15-17 FORMT1-3,
 * 18 OCTAVE, 19 DIGIT, 20 GLINT, 21 SPIKE), S3-S4 SEMI 2-3, S5 DETUNE, S6-S7 LEVEL 2-3, S8 MOD,
 * S9 E2 DEC, S10 E2 SUS, S11 P ENV, S12 TRANS, S13 VIBRATO, S14 VIB RATE, S15 GRIT */
#define SQF TP_FTYPE, FT_SQ80
static const int8_t q_piano[] = {TP_S0, 7, TP_S1, 8, TP_S2, 21, TP_S3, 0, TP_S4, 24, TP_S5, 10, TP_S6, 60, TP_S7, 0, TP_S9, 52,
                                 TP_S10, 10, TP_S12, 90, TP_S15, 70, SQF, TP_CUT, 62, TP_RES, 10, TP_EFILT, 40, TP_KTRK, 60,
                                 TP_ATK, 0, TP_DEC, 82, TP_SUS, 0, TP_REL, 50, TP_POLY, 3, TP_LEVEL, 106, END};
static const int8_t q_vox[] = {TP_S0, 9, TP_S1, 10, TP_S2, 15, TP_S3, 0, TP_S5, 22, TP_S6, 100, TP_S7, 40, TP_S9, 80, TP_S10, 90,
                               TP_S13, 40, TP_S14, 60, TP_S15, 60, SQF, TP_CUT, 80, TP_RES, 20, TP_EFILT, 10, TP_ATK, 60,
                               TP_SUS, 110, TP_REL, 80, TP_POLY, 3, TP_REVS, 60, TP_LEVEL, 124, END};
static const int8_t q_bells[] = {TP_S0, 5, TP_S1, 20, TP_S2, 3, TP_S3, 19, TP_S4, 24, TP_S5, 12, TP_S6, 50, TP_S7, 30, TP_S9, 70,
                                 TP_S10, 0, TP_S12, 60, TP_S15, 80, SQF, TP_CUT, 96, TP_RES, 16, TP_EFILT, 30, TP_ATK, 0,
                                 TP_DEC, 96, TP_SUS, 0, TP_REL, 90, TP_POLY, 3, TP_REVS, 60, TP_DLYS, 30, TP_LEVEL, 111, END};
static const int8_t q_bass[] = {TP_S0, 6, TP_S1, 0, TP_S3, -12, TP_S5, 6, TP_S6, 70, TP_S9, 44, TP_S10, 20, TP_S11, 4,
                                TP_S15, 96, SQF, TP_CUT, 44, TP_RES, 50, TP_EFILT, 52, TP_ATK, 0, TP_DEC, 60, TP_SUS, 80,
                                TP_REL, 20, TP_POLY, 0, TP_PITCH, -12, TP_LEVEL, 127, END};
static const int8_t q_sync[] = {TP_S0, 0, TP_S1, 0, TP_S3, 19, TP_S6, 110, TP_S8, SQM_SYNC, TP_S9, 64, TP_S10, 30, TP_S11, 30,
                                TP_S15, 76, SQF, TP_CUT, 74, TP_RES, 30, TP_EFILT, 36, TP_ATK, 0, TP_DEC, 70, TP_SUS, 90,
                                TP_REL, 30, TP_LEVEL, 122, END};
static const int8_t q_pad[] = {TP_S0, 13, TP_S1, 14, TP_S2, 17, TP_S3, 0, TP_S4, 12, TP_S5, 34, TP_S6, 100, TP_S7, 50, TP_S9, 110,
                               TP_S10, 60, TP_S13, 20, TP_S14, 50, TP_S15, 56, SQF, TP_CUT, 60, TP_RES, 36, TP_EFILT, 30,
                               TP_ATK, 90, TP_SUS, 110, TP_REL, 100, TP_POLY, 3, TP_REVS, 80, TP_LEVEL, 127, END};
static const int8_t q_organ[] = {TP_S0, 11, TP_S1, 18, TP_S2, 19, TP_S3, 12, TP_S4, 19, TP_S5, 4, TP_S6, 60, TP_S7, 0, TP_S9, 20,
                                 TP_S10, 127, TP_S12, 40, TP_S13, 24, TP_S14, 80, TP_S15, 100, SQF, TP_CUT, 110, TP_RES, 0,
                                 TP_EFILT, 0, TP_ATK, 2, TP_SUS, 127, TP_REL, 16, TP_POLY, 3, TP_LEVEL, 89, END};

/* WAVE: S0 TABLE 1 (0 PPG SWEEP, 1 RESONANT, 2 PULSE, 3 SYNC, 4 VOICES, 5 ORGAN, 6 BELLS, 7 FORMANT,
 * 8 DIGITAL, 9 SQR TO SAW, 10 PARTIALS, 11 OCTAVES, 12 FM, 13 BRASS, 14 STRINGS, 15 PPG HARD,
 * 16 RANDOM, 17 CHOIR, 18 E-PIANO, 19 WAVEFOLD), S1 POS 1, S2 TABLE 2, S3 POS 2, S4 SEMI 2, S5 DETUNE,
 * S6 MIX, S7 SAT, S8 XMOD, S9 XM AMT, S10 WARP, S11 WARP AMT, S12 WAVE ENV (a SHAPE), S13 WE AMT,
 * S14 WE TIME, S15 ASIC */
static const int8_t w_ppgpad[] = {TP_S0, 1, TP_S1, 4, TP_S2, 7, TP_S3, 10, TP_S5, 30, TP_S6, 60, TP_S7, 30, TP_S12, 12, TP_S13, 40,
                                  TP_S14, 115, TP_S15, 20, TP_FTYPE, FT_XLP24, TP_CUT, 80, TP_RES, 40, TP_EFILT, 10, TP_ATK, 80,
                                  TP_SUS, 110, TP_REL, 95, TP_POLY, 3, TP_REVS, 70, TP_LEVEL, 77, END};
static const int8_t w_bells[] = {TP_S0, 6, TP_S1, 20, TP_S2, 6, TP_S3, 50, TP_S4, 19, TP_S6, 40, TP_S8, WX_FM, TP_S9, 50, TP_S15, 40,
                                 TP_FTYPE, FT_XLP24, TP_CUT, 110, TP_RES, 10, TP_ATK, 0, TP_DEC, 96, TP_SUS, 0, TP_REL, 90,
                                 TP_POLY, 3, TP_REVS, 60, TP_DLYS, 30, TP_LEVEL, 82, END};
static const int8_t w_mwsync[] = {TP_S0, 9, TP_S1, 30, TP_S2, 0, TP_S3, 63, TP_S4, 12, TP_S6, 90, TP_S8, WX_SYNC, TP_S12, 8, TP_S13, -50,
                                  TP_S14, 92, TP_S15, 30, TP_FTYPE, FT_XLP24, TP_CUT, 90, TP_RES, 30, TP_EFILT, 20, TP_ATK, 0,
                                  TP_DEC, 70, TP_SUS, 90, TP_REL, 30, TP_LEVEL, 85, END};
static const int8_t w_digibass[] = {TP_S0, 8, TP_S1, 18, TP_S10, WW_QUANT, TP_S11, 50, TP_S15, 110, TP_FTYPE, FT_XLP24, TP_CUT, 56,
                                    TP_RES, 60, TP_EFILT, 44, TP_ATK, 0, TP_DEC, 55, TP_SUS, 60, TP_REL, 20, TP_POLY, 0,
                                    TP_PITCH, -12, TP_LEVEL, 97, END};
static const int8_t w_voxsweep[] = {TP_S0, 4, TP_S1, 0, TP_S7, 50, TP_S10, WW_MIRROR, TP_S11, 40, TP_S12, 24, TP_S13, 63, TP_S14, 105,
                                    TP_S15, 64, TP_FTYPE, FT_XNOTCH, TP_CUT, 70, TP_RES, 30, TP_ATK, 40, TP_SUS, 110, TP_REL, 70,
                                    TP_POLY, 3, TP_REVS, 50, TP_LEVEL, 106, END};
static const int8_t w_remap[] = {TP_S0, 12, TP_S1, 30, TP_S10, WW_REMAP4, TP_S11, 0, TP_S12, 6, TP_S13, 30, TP_S14, 70,
                                 TP_FTYPE, FT_XSH, TP_CUT, 100, TP_RES, 20, TP_ATK, 0, TP_DEC, 60, TP_SUS, 80, TP_REL, 30,
                                 TP_LEVEL, 86, END};

/* LOGIC: S0 A WAVE, S1 B RATIO (0 1/4, 1 1/3, 2 1/2, 3 2/3, 4 3/4, 5 1, 6 5/4, 7 4/3, 8 3/2, 9 5/3, 10 2,
 * 11 5/2, 12 3, 13 4, 14 5, 15 6, 16 7, 17 8, 18 11, 19 16), S2 B WAVE, S3 C RATIO, S4 C WAVE, S5 OP 1,
 * S6 OP 2, S7 OP 2 AMT, S8 BITS, S9 PW, S10 B DRIFT, S11 DRY A, S12 SMOOTH, S13 GRAIN, S14 G SPRAY,
 * S15 G PITCH */
static const int8_t l_xor[] = {TP_S0, LW_SAW, TP_S1, 8, TP_S2, LW_TRI, TP_S5, LO_XOR, TP_S8, 6, TP_S12, 40, TP_FTYPE, FT_XLP24,
                               TP_CUT, 96, TP_RES, 30, TP_EFILT, 20, TP_ATK, 0, TP_DEC, 70, TP_SUS, 80, TP_REL, 30, TP_LEVEL, 92, END};
static const int8_t l_comp[] = {TP_S0, LW_SINE, TP_S1, 5, TP_S2, LW_TRI, TP_S5, LO_COMP, TP_S10, 40, TP_S12, 50, TP_FTYPE, FT_LP,
                                TP_CUT, 90, TP_RES, 20, TP_ATK, 30, TP_SUS, 110, TP_REL, 70, TP_POLY, 3, TP_REVS, 40, TP_LEVEL, 69, END};
static const int8_t l_grainvox[] = {TP_S0, LW_SAW, TP_S1, 10, TP_S2, LW_SINE, TP_S5, LO_MULT, TP_S13, 60, TP_S14, 30, TP_S15, 12,
                                    TP_FTYPE, FT_ZVOWEL, TP_CUT, 30, TP_RES, 50, TP_EFILT, 40, TP_KTRK, 0, TP_ATK, 40, TP_SUS, 110,
                                    TP_REL, 70, TP_POLY, 3, TP_REVS, 60, TP_LEVEL, 79, END};
static const int8_t l_crunch[] = {TP_S0, LW_SAW, TP_S1, 16, TP_S2, LW_SAW, TP_S3, 2, TP_S4, LW_TRI, TP_S5, LO_NAND, TP_S6, LO_MOD,
                                  TP_S7, 60, TP_S8, 3, TP_S12, 20, TP_FTYPE, FT_MS20, TP_CUT, 80, TP_RES, 50, TP_EFILT, 30,
                                  TP_ATK, 0, TP_DEC, 50, TP_SUS, 50, TP_REL, 20, TP_POLY, 0, TP_LEVEL, 127, END};

/* ORGAN: S0 STOP (0 FLUTE, 1 NACHTHORN, 2 PRINCIPAL, 3 GAMBA, 4 TRUMPET), S1-S7 16' 8' 4' 2 2/3' 2' 1 3/5' 1',
 * S8 CHIFF, S9 SPEECH, S10 SPREAD, S11 AIR, S12 WIND, S13 TREMLNT, S14 TREM RT, S15 CELESTE */
static const int8_t o_infinite[] = {TP_S0, 1, TP_S1, 0, TP_S2, 110, TP_S3, 80, TP_S4, 50, TP_S5, 60, TP_S8, 55, TP_S9, 62, TP_S10, 70,
                                    TP_S11, 40, TP_S12, 40, TP_REVS, 110, TP_LEVEL, 96, END};
static const int8_t o_plenum[] = {TP_S0, 2, TP_S1, 70, TP_S2, 110, TP_S3, 90, TP_S4, 60, TP_S5, 80, TP_S6, 40, TP_S7, 60, TP_S8, 30,
                                  TP_REVS, 100, TP_LEVEL, 104, END};
static const int8_t o_celeste[] = {TP_S0, 3, TP_S2, 110, TP_S3, 30, TP_S13, 30, TP_S14, 60, TP_S15, 70, TP_S8, 15, TP_REVS, 100, TP_LEVEL, 111, END};
static const int8_t o_flute[] = {TP_S0, 0, TP_S2, 50, TP_S3, 110, TP_S8, 85, TP_S9, 40, TP_S11, 30, TP_REVS, 90, TP_LEVEL, 100, END};
static const int8_t o_trompette[] = {TP_S0, 4, TP_S2, 110, TP_S3, 60, TP_S8, 20, TP_S9, 30, TP_REVS, 80, TP_LEVEL, 116, END};
/* MONO: S0 SAW, S1 PULSE, S2 SUB, S3 SUB OCT, S4 NOISE, S5 PW, S6 PWM, S7 SCOOP, S8 SCOOP T, S9 VIBRATO,
 * S10 VIB RATE, S11 VIB DLY, S12 VEL>CUT, S13 DRIFT, S14 RETRIG, S15 DRIVE (the engine sets the SH-101
 * filter, key tracking and the envelope; these change what differs) */
static const int8_t m_organic[] = {TP_S0, 127, TP_S2, 8, TP_S9, 4, TP_S10, 89, TP_S11, 40, TP_LEVEL, 100, END};
static const int8_t m_woodwind[] = {TP_S0, 30, TP_S1, 100, TP_S5, 30, TP_S7, 40, TP_S9, 15, TP_S11, 70, TP_ATK, 60, TP_CUT, 65, TP_RES, 30,
                                    TP_LEVEL, 100, END};
static const int8_t m_cello[] = {TP_S0, 127, TP_S9, 18, TP_S10, 80, TP_S11, 60, TP_S15, 50, TP_ATK, 62, TP_CUT, 68, TP_RES, 20, TP_KTRK, 110,
                                 TP_LEVEL, 100, END};
static const int8_t m_rubber[] = {TP_S0, 90, TP_S2, 70, TP_S7, 0, TP_S9, 0, TP_S14, 1, TP_CUT, 50, TP_RES, 40, TP_EFILT, 50, TP_ATK, 0,
                                  TP_DEC, 60, TP_SUS, 30, TP_PITCH, -12, TP_LEVEL, 127, END};
static const int8_t m_scoop[] = {TP_S0, 40, TP_S1, 80, TP_S5, 40, TP_S6, 40, TP_S7, 70, TP_S8, 60, TP_S9, 20, TP_LEVEL, 107, TP_DLYS, 30, END};

/* the Z filters: CUTOFF is MORPH, RESO is XFORM */
static const int8_t p_talkbox[] = {TP_S0, 30, TP_S2, 3, TP_S3, 30, TP_FTYPE, FT_ZTALK, TP_CUT, 10, TP_RES, 70, TP_EFILT, 60,
                                   TP_KTRK, 0, TP_ATK, 30, TP_DEC, 70, TP_SUS, 70, TP_REL, 40, TP_POLY, 0, TP_GLIDE, 20,
                                   TP_LEVEL, 112, END};
static const int8_t p_phasepad[] = {TP_S0, 0, TP_S2, 5, TP_S3, 40, TP_FTYPE, FT_ZPHASER, TP_CUT, 20, TP_RES, 110, TP_EFILT, 50,
                                    TP_KTRK, 0, TP_ATK, 90, TP_DEC, 110, TP_SUS, 90, TP_REL, 90, TP_POLY, 3, TP_REVS, 60,
                                    TP_LEVEL, 73, END};
static const int8_t p_zvox[] = {TP_S0, 0, TP_FTYPE, FT_ZVOWEL, TP_CUT, 0, TP_RES, 60, TP_EFILT, 64, TP_KTRK, 0, TP_ATK, 50,
                                TP_DEC, 90, TP_SUS, 20, TP_REL, 60, TP_POLY, 3, TP_REVS, 50, TP_LEVEL, 80, END};
static const int8_t p_zsweep[] = {TP_S0, 64, TP_FTYPE, FT_ZSWEEP, TP_CUT, 30, TP_RES, 100, TP_EFILT, 50, TP_KTRK, 40, TP_ATK, 0,
                                  TP_DEC, 55, TP_SUS, 40, TP_REL, 25, TP_POLY, 0, TP_LEVEL, 127, END};

/* --------------------------------------------------------------- strike --- */
static const int8_t s_wood[] = {TP_S0, EXC_CLICK, TP_S1, BODY_MODAL, TP_S2, 52, TP_S3, 60, TP_S5, 12, TP_S10, 40, TP_S11, 10, TP_LEVEL, 127, END};
static const int8_t s_metal[] = {TP_S0, EXC_METAL, TP_S1, BODY_DISC, TP_S2, 84, TP_S3, 96, TP_S5, 40, TP_S13, 20, TP_S12, 80, TP_LEVEL, 56, TP_REVS, 30, END};
static const int8_t s_kick[] = {TP_S0, EXC_CLICK, TP_S1, BODY_PING, TP_S2, 60, TP_S14, 96, TP_S10, 70, TP_S11, 14, TP_PITCH, -24, TP_DRIVE, 30, TP_LEVEL, 127, END};
static const int8_t s_snare[] = {TP_S0, EXC_NOISE, TP_S1, BODY_MODAL, TP_S2, 40, TP_S3, 90, TP_S5, 60, TP_S10, 90, TP_S11, 40, TP_LEVEL, 73, END};
static const int8_t s_hat[] = {TP_S0, EXC_METAL, TP_S1, BODY_PING, TP_S2, 24, TP_S3, 120, TP_S10, 60, TP_S11, 20, TP_PITCH, 24, TP_FTYPE, FT_HP, TP_CUT, 96, TP_LEVEL, 65, END};
static const int8_t s_dustbell[] = {TP_S0, EXC_DUST, TP_S1, BODY_MODAL, TP_S2, 96, TP_S3, 80, TP_S5, 70, TP_S8, 30, TP_S9, 30, TP_S10, 0, TP_LEVEL, 104, TP_REVS, 60, END};
static const int8_t s_rain[] = {TP_S0, EXC_RAIN, TP_S1, BODY_DISC, TP_S2, 70, TP_S3, 100, TP_S8, 10, TP_S9, 60, TP_S10, 0, TP_LEVEL, 63, TP_DLYS, 40, END};
static const int8_t s_bowbar[] = {TP_S0, EXC_BOW, TP_S1, BODY_MODAL, TP_S2, 110, TP_S3, 70, TP_S5, 30, TP_S10, 0, TP_ATK, 40, TP_LEVEL, 70, TP_REVS, 60, END};
static const int8_t s_strings[] = {TP_S0, EXC_CLICK, TP_S1, BODY_STRINGS, TP_S2, 100, TP_S3, 90, TP_S6, 50, TP_S5, 30, TP_S10, 20, TP_LEVEL, 127, END};
static const int8_t s_clockwork[] = {TP_S0, EXC_DUST, TP_S1, BODY_PING, TP_S2, 70, TP_S8, 127, TP_S9, 70, TP_S10, 0, TP_PITCH, 12, TP_LEVEL, 127, END};
static const int8_t s_fmtom[] = {TP_S0, EXC_FM, TP_S1, BODY_MODAL, TP_S2, 60, TP_S3, 40, TP_S14, 60, TP_S10, 40, TP_PITCH, -12, TP_LEVEL, 63, END};
static const int8_t s_pipe[] = {TP_S0, EXC_BLOW, TP_S1, BODY_PING, TP_S2, 110, TP_S3, 60, TP_S10, 0, TP_ATK, 50, TP_LEVEL, 89, TP_REVS, 50, END};
static const int8_t s_listen[] = {TP_S0, EXC_INPUT, TP_S1, BODY_MODAL, TP_S2, 100, TP_S3, 80, TP_S10, 0, TP_LEVEL, 92, END};

/* ----------------------------------------------------------------- perc --- */
/* S0 MACHINE, S1-S8 its parameters (see rv_params.c perc_mdef), S9 AMD, S10 AMF, S11 EQF, S12 EQG,
 * S13 FLTF, S14 FLTW, S15 FLTQ */
static const int8_t d_trxkick[] = {TP_S0, PM_TRX_BD, TP_S1, 36, TP_S2, 70, TP_S3, 60, TP_S4, 36, TP_S5, 60, TP_S8, 30, TP_LEVEL, 90, END};
static const int8_t d_trxsnare[] = {TP_S0, PM_TRX_SD, TP_S1, 56, TP_S2, 46, TP_S5, 90, TP_S6, 80, TP_LEVEL, 100, END};
static const int8_t d_trxclap[] = {TP_S0, PM_TRX_CP, TP_S1, 70, TP_S6, 50, TP_S7, 50, TP_LEVEL, 110, TP_REVS, 30, END};
static const int8_t d_trxhat[] = {TP_S0, PM_TRX_CH, TP_S2, 24, TP_S5, 80, TP_LEVEL, 127, TP_PAN, 10, END};
static const int8_t d_trxopen[] = {TP_S0, PM_TRX_OH, TP_S2, 70, TP_S5, 80, TP_LEVEL, 117, TP_PAN, 10, END};
static const int8_t d_trxcow[] = {TP_S0, PM_TRX_CB, TP_LEVEL, 98, TP_DLYS, 30, END};
static const int8_t d_efmkick[] = {TP_S0, PM_EFM_BD, TP_S5, 50, TP_S6, 20, TP_S7, 30, TP_S8, 30, TP_LEVEL, 98, END};
static const int8_t d_efmsnare[] = {TP_S0, PM_EFM_SD, TP_S3, 90, TP_S5, 70, TP_S6, 75, TP_LEVEL, 107, END};
static const int8_t d_efmhat[] = {TP_S0, PM_EFM_HH, TP_S2, 20, TP_LEVEL, 110, TP_PAN, -12, END};
static const int8_t d_efmbell[] = {TP_S0, PM_EFM_CB, TP_S1, 90, TP_S2, 90, TP_S6, 85, TP_LEVEL, 98, TP_REVS, 40, END};
static const int8_t d_pikick[] = {TP_S0, PM_PI_BD, TP_S1, 30, TP_S4, 30, TP_S5, 50, TP_LEVEL, 96, END};
static const int8_t d_pisnare[] = {TP_S0, PM_PI_SD, TP_S6, 100, TP_LEVEL, 100, TP_REVS, 25, END};
static const int8_t d_pitom[] = {TP_S0, PM_PI_MT, TP_S1, 40, TP_S8, 60, TP_LEVEL, 100, TP_REVS, 25, END};
static const int8_t d_piride[] = {TP_S0, PM_PI_RC, TP_LEVEL, 97, TP_PAN, 16, END};
static const int8_t d_pishaker[] = {TP_S0, PM_PI_MA, TP_S2, 90, TP_LEVEL, 103, TP_PAN, -16, END};
static const int8_t d_metal[] = {TP_S0, PM_PI_ML, TP_S1, 80, TP_S12, 30, TP_S15, 60, TP_S13, 40, TP_S14, 60, TP_LEVEL, 84, TP_REVS, 50, END};
static const int8_t d_gndsub[] = {TP_S0, PM_GND_SN, TP_S1, 20, TP_S2, 80, TP_S3, 40, TP_S4, 30, TP_LEVEL, 110, END};
static const int8_t d_blip[] = {TP_S0, PM_GND_IM, TP_S1, 30, TP_S3, 60, TP_S4, 90, TP_LEVEL, 127, TP_DLYS, 40, END};

/* PERC KITs: the six lanes' machines (each at its defaults); the track is a KIT (engine 1) */
static const int8_t k_kit[] = {TP_LEVEL, 92, END};
static const int8_t k_kitrev[] = {TP_LEVEL, 92, TP_REVS, 30, END};
typedef struct { const char *name; uint8_t m[RV_LANES]; } kit_preset_t;
static const kit_preset_t kit_presets[] = {
    {"TRX KIT", {PM_TRX_BD, PM_TRX_SD, PM_TRX_CH, PM_TRX_OH, PM_TRX_CP, PM_TRX_RS}},
    {"EFM KIT", {PM_EFM_BD, PM_EFM_SD, PM_EFM_HH, PM_EFM_CY, PM_EFM_CP, PM_EFM_RS}},
    {"PI KIT", {PM_PI_BD, PM_PI_SD, PM_PI_HH, PM_PI_RC, PM_PI_MT, PM_PI_MA}},
    {"MD MIX KIT", {PM_TRX_BD, PM_PI_SD, PM_EFM_HH, PM_TRX_OH, PM_TRX_CP, PM_PI_ML}},
};

/* ----------------------------------------------------------------- tape --- */
static const int8_t t_mlr[] = {TP_S0, TAPE_MLR, TP_S1, 0, TP_S2, 6, TP_S4, 1, TP_LEVEL, 100, END};
static const int8_t t_cloud[] = {TP_S0, TAPE_BEADS, TP_S1, 0, TP_S6, 60, TP_S7, 84, TP_S8, 80, TP_S10, 96, TP_S12, 127, TP_LEVEL, 96, TP_REVS, 50, END};
static const int8_t t_keys[] = {TP_S0, TAPE_BEADS, TP_S1, 0, TP_S6, 40, TP_S7, 60, TP_S8, 50, TP_S10, 64, TP_S12, 127, TP_LEVEL, 100, END};
static const int8_t t_slices[] = {TP_S0, TAPE_SLICE, TP_S1, 0, TP_S14, 16, TP_S15, 70, TP_ATK, 2, TP_DEC, 50, TP_SUS, 90, TP_REL, 30, TP_LEVEL, 100, END};
static const int8_t t_half[] = {TP_S0, TAPE_MLR, TP_S1, 1, TP_S2, 5, TP_S3, 4, TP_S4, 2, TP_LEVEL, 90, TP_DLYS, 40, END};

static const rv_preset presets[] = {
    {"GLASS PAD", KIND_SYNTH, ENG_VA, p_glass, 0}, {"SUB BASS", KIND_SYNTH, ENG_VA, p_sub, 0}, {"PLUCK", KIND_SYNTH, ENG_VA, p_pluck, 0},
    {"LOW GATE", KIND_SYNTH, ENG_VA, p_lpg, 0}, {"CZ BELL", KIND_SYNTH, ENG_CZ, p_czbell, &cz_bell}, {"CZ BRASS", KIND_SYNTH, ENG_CZ, p_czbrass, &cz_brass},
    {"FOAM", KIND_SYNTH, ENG_CZ, p_foam, &cz_foam}, {"CZ STRINGS", KIND_SYNTH, ENG_CZ, p_czstrings, &cz_strings},
    {"RESO SWEEP", KIND_SYNTH, ENG_CZ, p_czreso, &cz_reso}, {"CZ BASS", KIND_SYNTH, ENG_CZ, p_czbass, &cz_bass},
    {"NOISE HIT", KIND_SYNTH, ENG_CZ, p_cznoise, &cz_noise}, {"FM PIANO", KIND_SYNTH, ENG_FM, p_fmep, 0}, {"FM BELLS", KIND_SYNTH, ENG_FM, p_fmbell, 0},
    {"FM GROWL", KIND_SYNTH, ENG_FM, p_fmgrowl, 0}, {"STRING", KIND_SYNTH, ENG_PHYS, p_string, 0}, {"BOWED", KIND_SYNTH, ENG_PHYS, p_bowed, 0},
    {"WRAITH", KIND_SYNTH, ENG_RESIN, p_wraith, 0}, {"UPSTAIRS", KIND_SYNTH, ENG_RESIN, p_upstairs, 0},
    {"RESIN VIOLIN", KIND_SYNTH, ENG_RESIN, p_violin, 0}, {"FOGHORN", KIND_SYNTH, ENG_RESIN, p_foghorn, 0},
    {"THROAT SAX", KIND_SYNTH, ENG_RESIN, p_throatsax, 0},
    {"BREATH", KIND_SYNTH, ENG_PHYS, p_breath, 0}, {"MEMBRANE", KIND_SYNTH, ENG_PHYS, p_membrane, 0}, {"VOWEL", KIND_SYNTH, ENG_FORMANT, p_vowel, 0},
    {"CHOIR FOF", KIND_SYNTH, ENG_FORMANT, p_fof, 0}, {"PULSAR", KIND_SYNTH, ENG_PARTICLE, p_pulsar, 0}, {"SWARM", KIND_SYNTH, ENG_PARTICLE, p_swarm, 0},
    {"DUST VOICE", KIND_SYNTH, ENG_PARTICLE, p_dustvoice, 0},
    {"CHIP ARP", KIND_SYNTH, ENG_CHIP, p_chiparp, 0}, {"SID BASS", KIND_SYNTH, ENG_CHIP, p_sidbass, 0}, {"CHIP KICK", KIND_SYNTH, ENG_CHIP, p_chipkick, 0},
    {"CHIP SNARE", KIND_SYNTH, ENG_CHIP, p_chipsnare, 0}, {"DECOMPOSED", KIND_SYNTH, ENG_CHIP, p_decomposed, 0},
    {"SYNC ARP", KIND_SYNTH, ENG_CHIP, p_syncarp, 0}, {"101 BASS", KIND_SYNTH, ENG_VA, p_101bass, 0}, {"MS SCREAM", KIND_SYNTH, ENG_VA, p_msscream, 0}, {"SEM BRASS", KIND_SYNTH, ENG_VA, p_sembrass, 0}, {"SEM NOTCH", KIND_SYNTH, ENG_VA, p_semnotch, 0},
    {"SQ PIANO", KIND_SYNTH, ENG_SQ, q_piano, 0}, {"SQ VOX", KIND_SYNTH, ENG_SQ, q_vox, 0}, {"SQ BELLS", KIND_SYNTH, ENG_SQ, q_bells, 0},
    {"SQ BASS", KIND_SYNTH, ENG_SQ, q_bass, 0}, {"SQ SYNC", KIND_SYNTH, ENG_SQ, q_sync, 0}, {"SQ PAD", KIND_SYNTH, ENG_SQ, q_pad, 0},
    {"SQ ORGAN", KIND_SYNTH, ENG_SQ, q_organ, 0},
    {"TALK BOX", KIND_SYNTH, ENG_VA, p_talkbox, 0}, {"PHASE PAD", KIND_SYNTH, ENG_VA, p_phasepad, 0},
    {"Z VOX", KIND_SYNTH, ENG_VA, p_zvox, 0}, {"Z SWEEP", KIND_SYNTH, ENG_VA, p_zsweep, 0},
    {"PPG PAD", KIND_SYNTH, ENG_WAVE, w_ppgpad, 0}, {"WAVE BELLS", KIND_SYNTH, ENG_WAVE, w_bells, 0},
    {"MW SYNC", KIND_SYNTH, ENG_WAVE, w_mwsync, 0}, {"DIGI BASS", KIND_SYNTH, ENG_WAVE, w_digibass, 0},
    {"VOX SWEEP", KIND_SYNTH, ENG_WAVE, w_voxsweep, 0}, {"REMAP STEP", KIND_SYNTH, ENG_WAVE, w_remap, 0},
    {"LOGIC XOR", KIND_SYNTH, ENG_LOGIC, l_xor, 0}, {"COMPARATOR", KIND_SYNTH, ENG_LOGIC, l_comp, 0},
    {"GRAIN VOX", KIND_SYNTH, ENG_LOGIC, l_grainvox, 0}, {"BIT CRUNCH", KIND_SYNTH, ENG_LOGIC, l_crunch, 0},
    {"INFINITE", KIND_SYNTH, ENG_ORGAN, o_infinite, 0}, {"PLENUM", KIND_SYNTH, ENG_ORGAN, o_plenum, 0},
    {"VOX CELESTE", KIND_SYNTH, ENG_ORGAN, o_celeste, 0}, {"FLUTE 4'", KIND_SYNTH, ENG_ORGAN, o_flute, 0},
    {"TROMPETTE", KIND_SYNTH, ENG_ORGAN, o_trompette, 0},
    {"ORGANIC SAW", KIND_SYNTH, ENG_MONO, m_organic, 0}, {"WOODWIND", KIND_SYNTH, ENG_MONO, m_woodwind, 0},
    {"CELLO LINE", KIND_SYNTH, ENG_MONO, m_cello, 0}, {"RUBBER BASS", KIND_SYNTH, ENG_MONO, m_rubber, 0},
    {"SCOOP LEAD", KIND_SYNTH, ENG_MONO, m_scoop, 0},
    {"WOOD", KIND_STRIKE, 0, s_wood, 0}, {"METAL", KIND_STRIKE, 0, s_metal, 0}, {"KICK BODY", KIND_STRIKE, 0, s_kick, 0},
    {"SNARE AIR", KIND_STRIKE, 0, s_snare, 0}, {"HAT", KIND_STRIKE, 0, s_hat, 0}, {"DUST BELL", KIND_STRIKE, 0, s_dustbell, 0},
    {"RAIN GLASS", KIND_STRIKE, 0, s_rain, 0}, {"BOWED BAR", KIND_STRIKE, 0, s_bowbar, 0}, {"STRINGS", KIND_STRIKE, 0, s_strings, 0},
    {"CLOCKWORK", KIND_STRIKE, 0, s_clockwork, 0}, {"FM TOM", KIND_STRIKE, 0, s_fmtom, 0}, {"PIPE", KIND_STRIKE, 0, s_pipe, 0},
    {"LISTENER", KIND_STRIKE, 0, s_listen, 0},
    {"TRX KIT", KIND_PERC, 1, k_kit, 0}, {"EFM KIT", KIND_PERC, 1, k_kit, 0}, {"PI KIT", KIND_PERC, 1, k_kitrev, 0},
    {"MD MIX KIT", KIND_PERC, 1, k_kit, 0},
    {"TRX KICK", KIND_PERC, 0, d_trxkick, 0}, {"TRX SNARE", KIND_PERC, 0, d_trxsnare, 0}, {"TRX CLAP", KIND_PERC, 0, d_trxclap, 0},
    {"TRX HAT", KIND_PERC, 0, d_trxhat, 0}, {"TRX OPEN HAT", KIND_PERC, 0, d_trxopen, 0}, {"TRX COWBELL", KIND_PERC, 0, d_trxcow, 0},
    {"EFM KICK", KIND_PERC, 0, d_efmkick, 0}, {"EFM SNARE", KIND_PERC, 0, d_efmsnare, 0}, {"EFM HAT", KIND_PERC, 0, d_efmhat, 0},
    {"EFM BELL", KIND_PERC, 0, d_efmbell, 0}, {"PI KICK", KIND_PERC, 0, d_pikick, 0}, {"PI SNARE", KIND_PERC, 0, d_pisnare, 0},
    {"PI TOM", KIND_PERC, 0, d_pitom, 0}, {"PI RIDE", KIND_PERC, 0, d_piride, 0}, {"PI SHAKER", KIND_PERC, 0, d_pishaker, 0},
    {"METALLICA", KIND_PERC, 0, d_metal, 0}, {"GND SUB", KIND_PERC, 0, d_gndsub, 0}, {"IMPULSE", KIND_PERC, 0, d_blip, 0},
    {"MLR LOOP", KIND_TAPE, 0, t_mlr, 0}, {"CLOUD", KIND_TAPE, 0, t_cloud, 0}, {"GRAIN KEYS", KIND_TAPE, 0, t_keys, 0},
    {"SLICES", KIND_TAPE, 0, t_slices, 0}, {"HALF SPEED", KIND_TAPE, 0, t_half, 0},
};
#define NPRESETS ((int)(sizeof(presets) / sizeof(presets[0])))

int rv_preset_count(void) { return NPRESETS; }
const char *rv_preset_name(int i) { return i >= 0 && i < NPRESETS ? presets[i].name : ""; }
int rv_preset_is_kit(int i) { return i >= 0 && i < NPRESETS && presets[i].kind == KIND_PERC && presets[i].engine == 1; }
int rv_preset_kind(int i) { return i >= 0 && i < NPRESETS ? presets[i].kind : -1; }

/* KIND changed on the track: its defaults (a PERC track starts as a KIT; a synth keeps its engine),
 * and its steps cleared when it changes between lanes and notes, which can't read each other's */
void rv_track_set_kind(int t, int kind)
{
    int was_kit = rv.tp[t & 7][TP_KIND] == KIND_PERC && rv.tp[t & 7][TP_ENGINE] == 1;
    int eng = kind == KIND_PERC ? 1 : kind == KIND_SYNTH && rv.tp[t & 7][TP_KIND] == KIND_SYNTH ? rv.tp[t & 7][TP_ENGINE] : 0;
    rv_track_defaults(t, kind, eng);
    if (was_kit != (kind == KIND_PERC)) {
        int pt = rv.pat_cur[t & 7];
        for (rv.pat_cur[t & 7] = 0; rv.pat_cur[t & 7] < RV_PATTERNS; rv.pat_cur[t & 7]++)
            rv_pattern_clear(t);
        rv.pat_cur[t & 7] = (uint8_t)pt;
    }
}

void rv_preset_apply(int t, int i)
{
    const int8_t *v;
    if (i < 0 || i >= NPRESETS)
        return;
    {
        int was_kit = rv.tp[t & 7][TP_KIND] == KIND_PERC && rv.tp[t & 7][TP_ENGINE] == 1;
        int is_kit = presets[i].kind == KIND_PERC && presets[i].engine == 1;
        rv_track_defaults(t, presets[i].kind, presets[i].engine);
        if (was_kit != is_kit) {                      /* lanes and notes don't read each other's steps */
            int pt = rv.pat_cur[t & 7];
            for (rv.pat_cur[t & 7] = 0; rv.pat_cur[t & 7] < RV_PATTERNS; rv.pat_cur[t & 7]++)
                rv_pattern_clear(t);
            rv.pat_cur[t & 7] = (uint8_t)pt;
        }
    }
    if (presets[i].kind == KIND_PERC && presets[i].engine == 1) {   /* a kit: its lanes' machines */
        unsigned k;
        int l, j;
        for (k = 0; k < sizeof(kit_presets) / sizeof(kit_presets[0]); k++) {
            const char *a = kit_presets[k].name, *b = presets[i].name;
            while (*a && *a == *b)
                a++, b++;
            if (*a == 0 && *b == 0)
                for (l = 0; l < RV_LANES; l++) {
                    int m = kit_presets[k].m[l];
                    rv.kit[t & 7].mach[l] = (uint8_t)m;
                    for (j = 0; j < 8; j++)
                        rv.kit[t & 7].p[l][j] = (uint8_t)(perc_pi[m][j].name ? perc_pi[m][j].def : 0);
                }
        }
    }
    if (presets[i].kind == KIND_PERC) {               /* the machine first, so its own defaults load */
        for (v = presets[i].v; *v >= 0; v += 2)
            if (v[0] == TP_S0)
                rv.tp[t][TP_S0] = v[1];
        rv_track_src_defaults(t);
    }
    for (v = presets[i].v; *v >= 0; v += 2)
        rv.tp[t][v[0]] = v[1];
    rv_fenv_from_env(rv.tp[t & 7]);                   /* the presets were made with one envelope for both */
    if (presets[i].cz)
        rv.cz[t & 7] = *presets[i].cz;
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
