/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: single-precision maths without libm.
 *
 * The FM-1's FPU is float only and the firmware links no maths library, so these are all
 * the transcendental functions the DSP uses. Plain textbook methods: split a float into
 * exponent and mantissa, a short polynomial on a reduced range, and the exponent put back.
 * The host and browser builds use the same code, so they compute the same samples.
 *
 * Accuracy against libm: exp2 1e-7 relative, log2 6e-7 absolute, sqrt 1e-7 relative,
 * sin 4e-7 and cos 4e-6 absolute on [-100, 100], sin of turns 2e-7.
 * NaN in, anything out; denormals are not produced for in-range arguments. */
#pragma once
#include <stdint.h>

#define FM_PI 3.14159265358979f
#define FM_TWO_PI 6.28318530717959f
#define FM_LN2 0.693147180559945f
#define FM_LOG2E 1.44269504088896f

/* RV_HOT: render code the FM-1 copies into the chip's own RAM at start (app.ld .fast_text), so it runs
 * without waiting on the flash. That RAM is more than a direct call's reach (4 MB) from the flash both
 * ways, so: the flash calls it through a pointer the compiler cannot see through (RV_FAR), and it calls
 * nothing in the flash, its helpers forced inline (RV_INL). RAVEL_FAST=0 builds it all in the flash */
#if defined(RV_FM1) && (!defined(RAVEL_FAST) || RAVEL_FAST)
#define RV_HOT __attribute__((section(".fast_text"), noinline))
#define RV_INL static inline __attribute__((always_inline))
/* (read through a volatile pointer: the compiler cannot turn it back into a direct call) */
#define RV_FAR(fn) ({ static __typeof__(&fn) const volatile rv_far_ = fn; rv_far_; })
#elif defined(RV_PROF_HOT)                   /* (the simulator's profile: RV_HOT kept apart, so it can be counted) */
#define RV_HOT __attribute__((noinline))
#define RV_INL static inline __attribute__((always_inline))
#define RV_FAR(fn) fn
#else
#define RV_HOT
#define RV_INL static inline
#define RV_FAR(fn) fn
#endif

typedef union { float f; uint32_t u; int32_t i; } fm_bits_t;

RV_INL float fm_fabsf(float x) { fm_bits_t b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
static inline float fm_minf(float a, float b) { return a < b ? a : b; }
RV_INL float fm_maxf(float a, float b) { return a > b ? a : b; }
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
RV_INL float fm_flush(float x) { return fm_fabsf(x) < 1e-20f ? 0.0f : x; }
/* n floats of decaying state: anything under about 1e-15 to 0, before it sinks into the denormals
 * (numbers so small a floating-point unit may take many times as long on them; a ringing filter
 * left alone gets there in a second or two). Once a block is enough */
static inline void fm_flush_n(float *p, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        fm_bits_t b;
        b.f = p[i];
        if ((b.u & 0x7F800000u) < 0x26800000u)       /* exponent under 2^-50 */
            p[i] = 0.0f;
    }
}

/* floor, for |x| < 2^31 */
RV_INL float fm_floorf(float x)
{
    float t = (float)(int32_t)x;
    return t > x ? t - 1.0f : t;
}

/* 2^x: 2^n from the exponent bits, 2^f (f in [0, 1)) from a degree-6 fit */
RV_INL float fm_exp2f(float x)
{
    fm_bits_t s;
    float n, f, p;
    if (x < -126.0f)
        return 0.0f;
    if (x > 127.0f)
        x = 127.0f;
    n = fm_floorf(x);
    f = x - n;
    p = 0.000218775048f;
    p = p * f + 0.00123878215f;
    p = p * f + 0.00968458045f;
    p = p * f + 0.0554804263f;
    p = p * f + 0.240230502f;
    p = p * f + 0.693146929f;
    p = p * f + 1.0f;
    s.u = (uint32_t)((int32_t)n + 127) << 23;
    return p * s.f;
}
/* a cutoff knob (0..127) in Hz: 20 Hz to 20 kHz (rv_params.c's rv_cut_hz; this one inlines into the RAM code) */
RV_INL float rv_cut_hz_i(float v) { return 20.0f * fm_exp2f(v * (10.0f / 127.0f)); }

static inline float fm_expf(float x) { return fm_exp2f(x * FM_LOG2E); }

/* log2 for x > 0: the exponent, plus log2 of the mantissa m in [0.707, 1.414) through
 * atanh: log2 m = 2/ln2 (t + t^3/3 + t^5/5 + t^7/7 + t^9/9), t = (m - 1) / (m + 1) */
RV_INL float fm_log2f(float x)
{
    fm_bits_t b;
    int32_t e;
    float m, t, t2, s;
    if (x <= 0.0f)
        return -126.0f;
    b.f = x;
    e = (int32_t)((b.u >> 23) & 0xFFu) - 127;
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u;
    m = b.f;
    if (m > 1.41421356f) {
        m *= 0.5f;
        e++;
    }
    t = (m - 1.0f) / (m + 1.0f);
    t2 = t * t;
    s = 1.0f / 9.0f;
    s = s * t2 + 1.0f / 7.0f;
    s = s * t2 + 1.0f / 5.0f;
    s = s * t2 + 1.0f / 3.0f;
    s = s * t2 + 1.0f;
    return (float)e + s * t * 2.88539008f;   /* 2 / ln 2 */
}

static inline float fm_powf(float x, float y) { return x > 0.0f ? fm_exp2f(y * fm_log2f(x)) : 0.0f; }

/* sin on [-pi/2, pi/2]: odd Taylor series to x^11 */
static float fm_sin_half(float x)
{
    float x2 = x * x, p = -2.50521084e-8f;
    p = p * x2 + 2.75573192e-6f;
    p = p * x2 - 1.98412698e-4f;
    p = p * x2 + 8.33333333e-3f;
    p = p * x2 - 1.66666667e-1f;
    return x + x * x2 * p;
}

/* sin of 2 pi r for r in [-0.5, 0.5]: fold to [-1/4, 1/4] turn */
static float fm_sin_r(float r)
{
    if (r > 0.25f)
        r = 0.5f - r;
    else if (r < -0.25f)
        r = -0.5f - r;
    return fm_sin_half(r * FM_TWO_PI);
}

/* sin of a phase in turns (1 = a full cycle), any range */
static inline float fm_sin_turns(float ph) { return fm_sin_r(ph - fm_floorf(ph + 0.5f)); }

/* sin(x): x in turns, the whole turns taken out in two parts so large x keep their precision */
RV_INL float fm_sinf(float x)
{
    float k = fm_floorf(x * 0.159154943f + 0.5f);
    float r = (x - k * 6.28125f) - k * 1.93530718e-3f;   /* 2 pi = 6.28125 + 0.00193530718 */
    return fm_sin_r(r * 0.159154943f);
}

static inline float fm_cosf(float x) { return fm_sinf(x + 1.57079633f); }

RV_INL float fm_tanf(float x)
{
    float c = fm_cosf(x);
    if (fm_fabsf(c) < 1e-7f)
        c = c < 0.0f ? -1e-7f : 1e-7f;
    return fm_sinf(x) / c;
}

/* sqrt for x >= 0: a guess from the exponent bits, then three Newton steps */
RV_INL float fm_sqrtf(float x)
{
    fm_bits_t b;
    float y;
    if (x <= 0.0f)
        return 0.0f;
    b.f = x;
    b.u = 0x1FBD1DF5u + (b.u >> 1);
    y = b.f;
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    y = 0.5f * (y + x / y);
    return y;
}
