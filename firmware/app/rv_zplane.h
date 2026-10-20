/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel's Z filters: morphing filters in the manner of E-mu's Z-plane filters. Each type is a
 * cascade of six two-pole, two-zero sections (twelve poles) defined at the four corners of a
 * square: MORPH runs one way (the CUTOFF knob, with its envelope, key tracking and modulation),
 * XFORM the other (the RESO knob). Between corners every section's frequency glides on a log
 * scale and its Q and gain glide straight, so the whole response moves as one shape. The frames
 * are Ravel's own designs (vowels, talking, phaser notches, resonant peaks, a sweep, metal).
 * A peak section is a pole pair over a zero pair at the same frequency, so it is flat away from
 * its peak (a negative gain makes it a dip); LP and HP sections are plain two-pole filters. The
 * cascade's loudest point is held to +12 dB, checked at the sections' own frequencies. */
#pragma once

enum { ZK_PEAK, ZK_LP, ZK_HP };
typedef struct { uint8_t kind; float f, q, db; } zp_sec_t;
#define ZP_SECS 6
#define ZP_TYPES (FT_N - FT_ZVOWEL)

static const zp_sec_t zp_frames[ZP_TYPES][4][ZP_SECS] = {   /* corners: morph 0, morph 1 (XFORM 0); morph 0, morph 1 (XFORM 1) */
    { /* Z VOWEL */
        {{ZK_PEAK, 730.0f, 9.00f, 15.0f}, {ZK_PEAK, 1090.0f, 9.00f, 12.0f}, {ZK_PEAK, 2440.0f, 9.00f, 9.0f}, {ZK_PEAK, 3400.0f, 9.00f, 6.0f}, {ZK_PEAK, 4500.0f, 9.00f, 3.0f}, {ZK_LP, 5200.0f, 0.70f, 0.0f}},
        {{ZK_PEAK, 270.0f, 9.00f, 15.0f}, {ZK_PEAK, 2290.0f, 9.00f, 12.0f}, {ZK_PEAK, 3010.0f, 9.00f, 9.0f}, {ZK_PEAK, 3600.0f, 9.00f, 6.0f}, {ZK_PEAK, 4800.0f, 9.00f, 3.0f}, {ZK_LP, 5200.0f, 0.70f, 0.0f}},
        {{ZK_PEAK, 850.0f, 14.00f, 18.0f}, {ZK_PEAK, 1220.0f, 14.00f, 15.0f}, {ZK_PEAK, 2810.0f, 14.00f, 12.0f}, {ZK_PEAK, 3900.0f, 14.00f, 8.0f}, {ZK_PEAK, 4950.0f, 14.00f, 4.0f}, {ZK_LP, 6500.0f, 0.90f, 0.0f}},
        {{ZK_PEAK, 310.0f, 14.00f, 18.0f}, {ZK_PEAK, 2790.0f, 14.00f, 15.0f}, {ZK_PEAK, 3310.0f, 14.00f, 12.0f}, {ZK_PEAK, 4300.0f, 14.00f, 8.0f}, {ZK_PEAK, 5200.0f, 14.00f, 4.0f}, {ZK_LP, 6500.0f, 0.90f, 0.0f}},
    },
    { /* Z TALK */
        {{ZK_PEAK, 300.0f, 7.00f, 15.0f}, {ZK_PEAK, 870.0f, 7.00f, 12.0f}, {ZK_PEAK, 2240.0f, 7.00f, 8.0f}, {ZK_PEAK, 3300.0f, 7.00f, 5.0f}, {ZK_HP, 90.0f, 0.70f, 0.0f}, {ZK_LP, 4500.0f, 0.70f, 0.0f}},
        {{ZK_PEAK, 660.0f, 9.00f, 15.0f}, {ZK_PEAK, 1700.0f, 9.00f, 12.0f}, {ZK_PEAK, 2400.0f, 9.00f, 8.0f}, {ZK_PEAK, 3300.0f, 9.00f, 5.0f}, {ZK_HP, 90.0f, 0.70f, 0.0f}, {ZK_LP, 4500.0f, 0.70f, 0.0f}},
        {{ZK_PEAK, 450.0f, 15.00f, 18.0f}, {ZK_PEAK, 800.0f, 15.00f, 15.0f}, {ZK_PEAK, 2830.0f, 15.00f, 10.0f}, {ZK_PEAK, 3500.0f, 15.00f, 6.0f}, {ZK_HP, 90.0f, 0.70f, 0.0f}, {ZK_LP, 4500.0f, 1.20f, 0.0f}},
        {{ZK_PEAK, 400.0f, 15.00f, 18.0f}, {ZK_PEAK, 2000.0f, 15.00f, 15.0f}, {ZK_PEAK, 2550.0f, 15.00f, 10.0f}, {ZK_PEAK, 3600.0f, 15.00f, 6.0f}, {ZK_HP, 90.0f, 0.70f, 0.0f}, {ZK_LP, 4500.0f, 1.20f, 0.0f}},
    },
    { /* Z PHASER */
        {{ZK_PEAK, 150.0f, 2.00f, -30.0f}, {ZK_PEAK, 300.0f, 2.00f, -30.0f}, {ZK_PEAK, 600.0f, 2.00f, -30.0f}, {ZK_PEAK, 1200.0f, 2.00f, -30.0f}, {ZK_PEAK, 2400.0f, 2.00f, -30.0f}, {ZK_PEAK, 4800.0f, 2.00f, -30.0f}},
        {{ZK_PEAK, 900.0f, 2.00f, -30.0f}, {ZK_PEAK, 1800.0f, 2.00f, -30.0f}, {ZK_PEAK, 3600.0f, 2.00f, -30.0f}, {ZK_PEAK, 7200.0f, 2.00f, -30.0f}, {ZK_PEAK, 12000.0f, 2.00f, -30.0f}, {ZK_PEAK, 16000.0f, 2.00f, -30.0f}},
        {{ZK_PEAK, 150.0f, 4.00f, -30.0f}, {ZK_PEAK, 210.0f, 6.00f, 12.0f}, {ZK_PEAK, 600.0f, 4.00f, -30.0f}, {ZK_PEAK, 840.0f, 6.00f, 12.0f}, {ZK_PEAK, 2400.0f, 4.00f, -30.0f}, {ZK_PEAK, 3360.0f, 6.00f, 12.0f}},
        {{ZK_PEAK, 900.0f, 4.00f, -30.0f}, {ZK_PEAK, 1260.0f, 6.00f, 12.0f}, {ZK_PEAK, 3600.0f, 4.00f, -30.0f}, {ZK_PEAK, 5040.0f, 6.00f, 12.0f}, {ZK_PEAK, 12000.0f, 4.00f, -30.0f}, {ZK_PEAK, 11200.0f, 6.00f, 12.0f}},
    },
    { /* Z PEAKS */
        {{ZK_PEAK, 300.0f, 6.00f, 9.0f}, {ZK_PEAK, 520.0f, 6.00f, 9.0f}, {ZK_PEAK, 830.0f, 6.00f, 9.0f}, {ZK_PEAK, 1250.0f, 6.00f, 9.0f}, {ZK_PEAK, 1900.0f, 6.00f, 9.0f}, {ZK_PEAK, 2800.0f, 6.00f, 9.0f}},
        {{ZK_PEAK, 1200.0f, 6.00f, 9.0f}, {ZK_PEAK, 2080.0f, 6.00f, 9.0f}, {ZK_PEAK, 3320.0f, 6.00f, 9.0f}, {ZK_PEAK, 5000.0f, 6.00f, 9.0f}, {ZK_PEAK, 7600.0f, 6.00f, 9.0f}, {ZK_PEAK, 11200.0f, 6.00f, 9.0f}},
        {{ZK_PEAK, 300.0f, 30.00f, 16.0f}, {ZK_PEAK, 520.0f, 30.00f, 16.0f}, {ZK_PEAK, 830.0f, 30.00f, 16.0f}, {ZK_PEAK, 1250.0f, 30.00f, 16.0f}, {ZK_PEAK, 1900.0f, 30.00f, 16.0f}, {ZK_PEAK, 2800.0f, 30.00f, 16.0f}},
        {{ZK_PEAK, 1200.0f, 30.00f, 16.0f}, {ZK_PEAK, 2080.0f, 30.00f, 16.0f}, {ZK_PEAK, 3320.0f, 30.00f, 16.0f}, {ZK_PEAK, 5000.0f, 30.00f, 16.0f}, {ZK_PEAK, 7600.0f, 30.00f, 16.0f}, {ZK_PEAK, 11200.0f, 30.00f, 16.0f}},
    },
    { /* Z SWEEP */
        {{ZK_LP, 120.0f, 0.60f, 0.0f}, {ZK_LP, 160.0f, 0.60f, 0.0f}, {ZK_LP, 220.0f, 0.60f, 0.0f}, {ZK_PEAK, 140.0f, 1.50f, 3.0f}, {ZK_HP, 30.0f, 0.70f, 0.0f}, {ZK_PEAK, 1000.0f, 1.00f, 0.0f}},
        {{ZK_LP, 15000.0f, 0.60f, 0.0f}, {ZK_LP, 17000.0f, 0.60f, 0.0f}, {ZK_LP, 19000.0f, 0.60f, 0.0f}, {ZK_PEAK, 12000.0f, 1.50f, 3.0f}, {ZK_HP, 30.0f, 0.70f, 0.0f}, {ZK_PEAK, 1000.0f, 1.00f, 0.0f}},
        {{ZK_LP, 120.0f, 4.00f, 0.0f}, {ZK_LP, 160.0f, 0.60f, 0.0f}, {ZK_LP, 220.0f, 0.60f, 0.0f}, {ZK_PEAK, 240.0f, 10.00f, 14.0f}, {ZK_HP, 30.0f, 0.70f, 0.0f}, {ZK_PEAK, 1000.0f, 1.00f, 0.0f}},
        {{ZK_LP, 6000.0f, 4.00f, 0.0f}, {ZK_LP, 17000.0f, 0.60f, 0.0f}, {ZK_LP, 19000.0f, 0.60f, 0.0f}, {ZK_PEAK, 9000.0f, 10.00f, 14.0f}, {ZK_HP, 30.0f, 0.70f, 0.0f}, {ZK_PEAK, 1000.0f, 1.00f, 0.0f}},
    },
    { /* Z METAL */
        {{ZK_PEAK, 400.0f, 40.00f, 20.0f}, {ZK_PEAK, 1090.0f, 40.00f, 20.0f}, {ZK_PEAK, 1980.0f, 40.00f, 20.0f}, {ZK_PEAK, 2950.0f, 40.00f, 20.0f}, {ZK_PEAK, 4230.0f, 40.00f, 20.0f}, {ZK_PEAK, 5600.0f, 40.00f, 20.0f}},
        {{ZK_PEAK, 840.0f, 40.00f, 20.0f}, {ZK_PEAK, 1740.0f, 40.00f, 20.0f}, {ZK_PEAK, 3760.0f, 40.00f, 20.0f}, {ZK_PEAK, 4600.0f, 40.00f, 20.0f}, {ZK_PEAK, 7190.0f, 40.00f, 20.0f}, {ZK_PEAK, 8400.0f, 40.00f, 20.0f}},
        {{ZK_PEAK, 400.0f, 120.00f, 26.0f}, {ZK_PEAK, 1090.0f, 120.00f, 26.0f}, {ZK_PEAK, 1980.0f, 120.00f, 26.0f}, {ZK_PEAK, 2950.0f, 120.00f, 26.0f}, {ZK_PEAK, 4230.0f, 120.00f, 26.0f}, {ZK_PEAK, 5600.0f, 120.00f, 26.0f}},
        {{ZK_PEAK, 840.0f, 120.00f, 26.0f}, {ZK_PEAK, 1740.0f, 120.00f, 26.0f}, {ZK_PEAK, 3760.0f, 120.00f, 26.0f}, {ZK_PEAK, 4600.0f, 120.00f, 26.0f}, {ZK_PEAK, 7190.0f, 120.00f, 26.0f}, {ZK_PEAK, 8400.0f, 120.00f, 26.0f}},
    },};

/* the twelve-pole response at (morph, xform), as coefficients for the cascade */
static void zp_design(int type, float m, float x, float c[ZP_SECS][5], float *gain)
{
    const zp_sec_t (*fr)[ZP_SECS] = zp_frames[clampi(type, 0, ZP_TYPES - 1)];
    float w1[ZP_SECS], peak = 0.0f;
    int s, j;
    m = clampf(m, 0.0f, 1.0f);
    x = clampf(x, 0.0f, 1.0f);
    for (s = 0; s < ZP_SECS; s++) {
        const zp_sec_t *a = &fr[0][s], *b = &fr[1][s], *a2 = &fr[2][s], *b2 = &fr[3][s];
        float lf0 = fm_log2f(a->f) + (fm_log2f(b->f) - fm_log2f(a->f)) * m;
        float lf1 = fm_log2f(a2->f) + (fm_log2f(b2->f) - fm_log2f(a2->f)) * m;
        float f = fm_exp2f(lf0 + (lf1 - lf0) * x);
        float q0 = a->q + (b->q - a->q) * m, q1 = a2->q + (b2->q - a2->q) * m, q = q0 + (q1 - q0) * x;
        float d0 = a->db + (b->db - a->db) * m, d1 = a2->db + (b2->db - a2->db) * m, db = d0 + (d1 - d0) * x;
        float w, cw, r, a1, a2c, b0, b1, b2c, k;
        f = clampf(f, 20.0f, RV_NYQ * 0.95f);
        w = 6.2831853f * f * RV_DT;
        cw = fm_cosf(w);
        r = fm_expf(-3.14159265f * (f / q) * RV_DT);
        if (r > 0.99985f)
            r = 0.99985f;
        a1 = -2.0f * r * cw;
        a2c = r * r;
        if (a->kind == ZK_PEAK) {
            float qz = q / fm_exp2f(db * 0.16609640f), rz = fm_expf(-3.14159265f * (f / qz) * RV_DT);   /* 10^(db/20) */
            if (rz > 0.99985f)
                rz = 0.99985f;
            b0 = 1.0f;
            b1 = -2.0f * rz * cw;
            b2c = rz * rz;
            k = (1.0f + a1 + a2c) / (b0 + b1 + b2c);  /* flat at DC */
        } else if (a->kind == ZK_LP) {
            b0 = 1.0f, b1 = 2.0f, b2c = 1.0f;
            k = (1.0f + a1 + a2c) / 4.0f;
        } else {
            b0 = 1.0f, b1 = -2.0f, b2c = 1.0f;
            k = (1.0f - a1 + a2c) / 4.0f;             /* flat at Nyquist */
        }
        c[s][0] = b0 * k;
        c[s][1] = b1 * k;
        c[s][2] = b2c * k;
        c[s][3] = a1;
        c[s][4] = a2c;
        w1[s] = w;
    }
    /* the cascade's level at each section's frequency: the loudest is held to +12 dB */
    for (j = 0; j < ZP_SECS; j++) {
        float c1 = fm_cosf(w1[j]), s1 = fm_sinf(w1[j]), c2 = c1 * c1 - s1 * s1, s2 = 2.0f * s1 * c1, mag2 = 1.0f;
        for (s = 0; s < ZP_SECS; s++) {
            float nr = c[s][0] + c[s][1] * c1 + c[s][2] * c2, ni = -(c[s][1] * s1 + c[s][2] * s2);
            float dr = 1.0f + c[s][3] * c1 + c[s][4] * c2, di = -(c[s][3] * s1 + c[s][4] * s2);
            mag2 *= (nr * nr + ni * ni) / (dr * dr + di * di + 1e-20f);
        }
        if (mag2 > peak)
            peak = mag2;
    }
    *gain = peak > 15.85f ? fm_sqrtf(15.85f / peak) : 1.0f;   /* 15.85 = (+12 dB)^2 */
}

/* its magnitude at f Hz (for the FILTER picture) */
static float zp_mag(int type, float m, float x, float f)
{
    float c[ZP_SECS][5], g, w = 6.2831853f * f * RV_DT, c1 = fm_cosf(w), s1 = fm_sinf(w), c2 = c1 * c1 - s1 * s1, s2 = 2.0f * s1 * c1, mag2 = 1.0f;
    int s;
    zp_design(type, m, x, c, &g);
    for (s = 0; s < ZP_SECS; s++) {
        float nr = c[s][0] + c[s][1] * c1 + c[s][2] * c2, ni = -(c[s][1] * s1 + c[s][2] * s2);
        float dr = 1.0f + c[s][3] * c1 + c[s][4] * c2, di = -(c[s][3] * s1 + c[s][4] * s2);
        mag2 *= (nr * nr + ni * ni) / (dr * dr + di * di + 1e-20f);
    }
    return fm_sqrtf(mag2) * g;
}

static inline float zp_tick(zp_t *z, float x)
{
    int s;
    x *= z->g;
    for (s = 0; s < ZP_SECS; s++) {                   /* transposed direct form II */
        const float *c = z->c[s];
        float y = c[0] * x + z->s[s][0];
        z->s[s][0] = c[1] * x - c[3] * y + z->s[s][1];
        z->s[s][1] = c[2] * x - c[4] * y;
        x = y;
    }
    return x;
}
