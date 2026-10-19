/* Beads, fixed point. See bd.h for what this is and the number formats.
 *
 * Original code: Copyright 2017-2021 Emilie Gillet (MIT). Each section below
 * names the Beads file it follows; comments from the original are kept where
 * they explain behaviour. */
#include "bd.h"
#include "bd_tables.h"

/* No C library needed: the compiler built-ins lower to the firmware's own memset/memmove. */
#define memset __builtin_memset
#define memmove __builtin_memmove

/* ------------------------------------------------------------------------ */
/* Fixed-point helpers                                                       */
/* ------------------------------------------------------------------------ */

#define Q16 65536
#define Q15 32768
#define Q28 (1 << 28)
#define Q30 (1 << 30)
#define TQ ((int64_t)1 << 40)   /* trigger clock phase */

static inline int32_t mul16(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 16); }
static inline int32_t mul15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
static inline int32_t mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static inline int32_t mul28(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 28); }
static inline int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t iabs(int32_t a) { return a < 0 ? -a : a; }
static inline int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline int32_t clip16(int32_t x) { return x < -32768 ? -32768 : (x > 32767 ? 32767 : x); }

/* 2^(x / 65536) * 2^shift, rounded; x in Q16 octaves. */
static int32_t exp2_q(int32_t x_q16, int shift) {
  int32_t i = x_q16 >> 16;
  uint32_t f = (uint32_t)x_q16 & 0xffff;
  uint32_t a = bd_lut_exp2_q30[f >> 8], b = bd_lut_exp2_q30[(f >> 8) + 1];
  uint32_t m = a + (uint32_t)(((uint64_t)(b - a) * (f & 0xff)) >> 8);   /* Q30, 1..2 */
  int s = 30 - shift - i;
  if (s >= 32) return 0;
  if (s > 0) return (int32_t)((m + (1u << (s - 1))) >> s);
  if (s < -1) return 0x7fffffff;   /* out of range: saturate */
  return (int32_t)(m << -s);
}

/* stmlib::SemitonesToRatio, including its rounding: the fraction of a semitone
   is truncated to 1/256 before the table lookup. Returns the ratio * 2^shift. */
static inline int32_t semitones_to_ratio_q(int32_t semis_q16, int shift) {
  int32_t k = ((semis_q16 + (128 << 16)) >> 8) - (128 << 8);   /* 1/256 semitone steps */
  int32_t x = (int32_t)(((int64_t)k * 64 + (k >= 0 ? 1 : -1)) / 3);   /* octaves Q16: k / 3072 */
  return exp2_q(x, shift);
}
static inline int32_t semitones_to_ratio(int32_t semis_q16) { return semitones_to_ratio_q(semis_q16, 16); }

/* stmlib::SmoothStep, Q16 and Q15 */
static inline int32_t smoothstep16(int32_t t) { return mul16(mul16(t, t), 3 * Q16 - 2 * t); }
static inline int32_t smoothstep15(int32_t t) { return mul15(mul15(t, t), 3 * Q15 - 2 * t); }

/* stmlib::SoftClip on a Q15 sample */
#ifndef BD_READER_ONLY
static inline int32_t soft_clip(int32_t x) {
  int32_t ax = iabs(x), y;
  if (ax >= 3 * Q15) {
    y = Q15;
  } else {
    int32_t i = ax / 192, fr = ax - i * 192;
    int32_t a = bd_lut_softclip_q15[i], b = bd_lut_softclip_q15[i + 1];
    y = a + (b - a) * fr / 192;
  }
  return x < 0 ? -y : y;
}
#endif

/* Beads' mu-law encoder (mu_law.h), unchanged */
static inline uint8_t lin2mulaw(int16_t pcm_val) {
  int16_t mask, seg;
  uint8_t uval;
  pcm_val = (int16_t)(pcm_val >> 2);
  if (pcm_val < 0) { pcm_val = (int16_t)-pcm_val; mask = 0x7f; } else { mask = 0xff; }
  if (pcm_val > 8159) pcm_val = 8159;
  pcm_val = (int16_t)(pcm_val + (0x84 >> 2));
  if (pcm_val <= 0x3f) seg = 0;
  else if (pcm_val <= 0x7f) seg = 1;
  else if (pcm_val <= 0xff) seg = 2;
  else if (pcm_val <= 0x1ff) seg = 3;
  else if (pcm_val <= 0x3ff) seg = 4;
  else if (pcm_val <= 0x7ff) seg = 5;
  else if (pcm_val <= 0xfff) seg = 6;
  else if (pcm_val <= 0x1fff) seg = 7;
  else seg = 8;
  if (seg >= 8) return (uint8_t)(0x7f ^ mask);
  uval = (uint8_t)((seg << 4) | ((pcm_val >> (seg + 1)) & 0x0f));
  return (uint8_t)(uval ^ mask);
}

/* stmlib::Random: the same LCG, so the port draws the same numbers */
static inline uint32_t rng_word(bd_engine* e) { e->rng = e->rng * 1664525u + 1013904223u; return e->rng; }
static inline int32_t rng_q16(bd_engine* e) { return (int32_t)(rng_word(e) >> 16); }
static inline int32_t centered_uniform(bd_engine* e) { return 2 * rng_q16(e) - Q16; }
static inline int32_t pseudo_gaussian(bd_engine* e) {
  int32_t u = rng_q16(e);
  return mul16(u, mul16(u, 4 * u - 6 * Q16) + 4 * Q16) - Q16;
}

/* ------------------------------------------------------------------------ */
/* stmlib filters and small helpers                                          */
/* ------------------------------------------------------------------------ */

#ifndef BD_NO_DELAY
/* HysteresisFilter */
static inline int32_t hfilter(int32_t* state, int32_t value, int32_t threshold) {
  int32_t error = value - *state;
  if (error > threshold) *state = value - threshold;
  else if (error < -threshold) *state = value + threshold;
  return *state;
}
#endif

/* HysteresisQuantizer2 */
static void hq_init(bd_hquant* q, int num_steps, int32_t hysteresis, int symmetric) {
  q->num_steps = num_steps;
  q->hysteresis = hysteresis;
  q->scale = symmetric ? num_steps - 1 : num_steps;
  q->offset = symmetric ? 0 : -Q16 / 2;
  q->value = 0;
}

static int hq_process(bd_hquant* q, int32_t value) {
  int64_t v = (int64_t)value * q->scale + q->offset;
  int32_t sign = v > ((int64_t)q->value << 16) ? -1 : 1;
  int64_t x = v + sign * q->hysteresis + Q16 / 2;
  int32_t r = (int32_t)(x >= 0 ? (x >> 16) : -((-x) >> 16));   /* truncate toward zero, as a float cast */
  r = clampi(r, 0, q->num_steps - 1);
  q->value = r;
  return r;
}

/* stmlib::Svf with FREQUENCY_FAST tangent, Q28 coefficients, Q23 state */
static void svf_set(bd_svf* s, int32_t f_q16, int32_t resonance_q16) {
  /* g = f (pi + f^2 (a + b f^2)), a = 0.326 pi^3, b = 0.1823 pi^5 */
  const int64_t PI28 = 843314857;          /* pi in Q28 */
  const int64_t A28 = 2714281627LL;        /* 3.260e-01 * pi^3 in Q28 */
  const int64_t B28 = 14984224710LL;       /* 1.823e-01 * pi^5 in Q28 */
  int64_t f = ((int64_t)f_q16) << 12;      /* Q28 */
  int64_t f2 = (f * f) >> 28;
  int64_t g = (f * (PI28 + ((f2 * (A28 + ((B28 * f2) >> 28))) >> 28))) >> 28;
  int64_t r = ((int64_t)Q16 << 28) / resonance_q16;   /* 1 / resonance, Q28 */
  int64_t den = ((int64_t)1 << 28) + ((r * g) >> 28) + ((g * g) >> 28);
  s->g = (int32_t)g;
  s->r = (int32_t)r;
  s->h = (int32_t)(((int64_t)1 << 56) / den);
}

enum { SVF_LP, SVF_BP, SVF_HP };

static inline int32_t svf_process(bd_svf* s, int32_t in_q15, int mode) {
  int32_t in = in_q15 << 8;
  int32_t hp = mul28((int32_t)(in - (((int64_t)s->r * s->s1) >> 28) - (((int64_t)s->g * s->s1) >> 28) - s->s2), s->h);
  int32_t bp = mul28(s->g, hp) + s->s1;
  s->s1 = mul28(s->g, hp) + bp;
  int32_t lp = mul28(s->g, bp) + s->s2;
  s->s2 = mul28(s->g, bp) + lp;
  int32_t out = mode == SVF_LP ? lp : (mode == SVF_BP ? bp : hp);
  return out >> 8;
}

/* beads::Limiter (limiter.h) */
static void limiter_init(bd_limiter* l, int32_t decay_q30, int32_t max_peak_q16) {
  l->peak = l->peak_lp = 1 << 23;          /* 0.5 */
  l->decay = decay_q30;
  l->max_peak = max_peak_q16 << 8;
}

static inline void slope(int32_t* out, int32_t in, int32_t pos_q30, int32_t neg_q30) {
  int32_t error = in - *out;
  *out += mul30(error > 0 ? pos_q30 : neg_q30, error);
}

static inline void limiter_process(bd_limiter* l, bd_frame* f) {
  int32_t peak = imax(iabs(f->l), iabs(f->r));
  peak = imin(peak, 40 * Q15) << 9;        /* Q24 */
  slope(&l->peak, peak, Q30 / 10, l->decay);
  slope(&l->peak_lp, l->peak, Q30 / 2, l->decay);
  if (l->peak_lp > l->max_peak) {
    int32_t gain = (int32_t)(((int64_t)l->max_peak << 15) / l->peak_lp);   /* Q15 */
    f->l = mul15(f->l, gain);
    f->r = mul15(f->r, gain);
  }
}

/* ------------------------------------------------------------------------ */
/* Randomized parameters (randomized_parameter.h)                            */
/* ------------------------------------------------------------------------ */

static void walk_init(bd_walk* w, int32_t frequency_q32) {
  w->frequency = frequency_q32;
  w->phase = 0;
  w->from = 0;
  w->to = Q16;
}

static int32_t walk_value(bd_engine* e, bd_walk* w, int32_t amount, int step) {
  uint32_t f = (uint32_t)w->frequency * (uint32_t)step;
  f = amount <= 0 ? f / 4 : f * 4;
  uint32_t p = (uint32_t)w->phase + f;
  if (p < (uint32_t)w->phase) {   /* wrapped past 1.0 */
    w->from = w->to;
    w->to = centered_uniform(e);
  }
  w->phase = (int32_t)p;
  return w->from + mul16(w->to - w->from, smoothstep16((int32_t)(p >> 16)));
}

static inline int32_t knob_randomness(const bd_knob* k) {
  return k->has_cv ? imax(-k->amount, 0) : iabs(k->amount);
}

/* RandomizedParameter::value(modulation, step_size, range, clamp) */
static int32_t knob_value(bd_engine* e, const bd_knob* k, bd_walk* w, int step, int32_t range, int clamp) {
  int32_t result = k->base;
  if (!k->has_cv) {
    if (w) {
      result += mul16(mul16(k->amount, range), walk_value(e, w, k->amount, step));
    } else {
      int32_t blend = k->amount > 0 ? k->amount : imin(Q16, -3 * mul16(k->amount, rng_q16(e)));
      int32_t random_value = !clamp ? mul16(centered_uniform(e), range) : mul16(rng_q16(e), range) - result;
      result += mul16(random_value, blend);
    }
  } else {
    if (k->amount < 0) {
      int32_t extra = w ? walk_value(e, w, Q16, step) : pseudo_gaussian(e);
      result += mul16(mul16(imax(k->cv, 0), -k->amount), extra);
    } else {
      result += mul16(k->amount, k->cv);
    }
  }
  if (clamp) result = clampi(result, 0, Q16);
  return result;
}

static inline int32_t knob_simple(bd_engine* e, const bd_knob* k) { return knob_value(e, k, 0, 0, Q16, 1); }

/* pitch_value(): ratio Q16 */
static inline int32_t knob_pitch_ratio(bd_engine* e, const bd_knob* k) {
  return semitones_to_ratio(knob_value(e, k, 0, 0, 24 * Q16, 0));
}

/* ------------------------------------------------------------------------ */
/* The tape (delay_line.h)                                                   */
/* ------------------------------------------------------------------------ */

#define FREEZE_TAIL 256
#define INTERP_TAIL 4

static int32_t item_bytes_x2(int res, int mono) {   /* bytes per 2 samples */
  int bytes2 = res == BD_RES_16 ? 4 : (res == BD_RES_12 ? 3 : 2);
  return mono ? bytes2 : bytes2 * 2;
}

uint32_t bd_tape_bytes(bd_res res, int mono, int32_t samples) {
  int32_t n = samples + INTERP_TAIL;
  n += n & 1;
  uint32_t bytes = (uint32_t)(n / 2 * item_bytes_x2(res, mono)) + FREEZE_TAIL * sizeof(bd_frame);
  return (bytes + 7u) & ~7u;   /* bd_init keeps the buffer 8-byte aligned in size */
}

int32_t bd_tape_samples(bd_res res, int mono, uint32_t buffer_bytes) {
  if (buffer_bytes <= FREEZE_TAIL * sizeof(bd_frame)) return 0;
  uint32_t cap = buffer_bytes - FREEZE_TAIL * sizeof(bd_frame);
  int32_t n = (int32_t)(cap / (uint32_t)item_bytes_x2(res, mono)) * 2 - INTERP_TAIL;
  return n > 0 ? n : 0;
}

static inline void tape_store(bd_tape* t, int32_t i, bd_frame s) {
  uint8_t* p;
  if (t->mono) {
    int32_t v = clip16((s.l + s.r) >> 1);
    switch (t->res) {
      case BD_RES_16: ((int16_t*)t->items)[i] = (int16_t)v; break;
      case BD_RES_8: t->items[i] = lin2mulaw((int16_t)v); break;
      default: {
        uint32_t v12 = (uint32_t)(v >> 4) & 0xfff;
        p = t->items + (i >> 1) * 3;
        if (i & 1) { p[1] = (uint8_t)((p[1] & 0x0f) | (v12 << 4)); p[2] = (uint8_t)(v12 >> 4); }
        else { p[0] = (uint8_t)v12; p[1] = (uint8_t)((p[1] & 0xf0) | (v12 >> 8)); }
      }
    }
  } else {
    int32_t l = clip16(s.l), r = clip16(s.r);
    switch (t->res) {
      case BD_RES_16: ((int16_t*)t->items)[2 * i] = (int16_t)l; ((int16_t*)t->items)[2 * i + 1] = (int16_t)r; break;
      case BD_RES_8: t->items[2 * i] = lin2mulaw((int16_t)l); t->items[2 * i + 1] = lin2mulaw((int16_t)r); break;
      default: {
        uint32_t l12 = (uint32_t)(l >> 4) & 0xfff, r12 = (uint32_t)(r >> 4) & 0xfff;
        p = t->items + i * 3;
        p[0] = (uint8_t)l12; p[1] = (uint8_t)((l12 >> 8) | (r12 << 4)); p[2] = (uint8_t)(r12 >> 4);
      }
    }
  }
}

static inline int32_t sext12(uint32_t v) { return (int32_t)(v << 20) >> 20; }

static inline bd_frame tape_load(const bd_tape* t, int32_t i) {
  bd_frame f;
  const uint8_t* p;
  if (t->mono) {
    int32_t v;
    switch (t->res) {
      case BD_RES_16: v = ((const int16_t*)t->items)[i]; break;
      case BD_RES_8: v = bd_lut_ulaw_q15[t->items[i]]; break;
      default:
        p = t->items + (i >> 1) * 3;
        v = (i & 1) ? sext12(((uint32_t)p[1] >> 4) | ((uint32_t)p[2] << 4))
                    : sext12((uint32_t)p[0] | (((uint32_t)p[1] & 0x0f) << 8));
        v <<= 4;
    }
    f.l = f.r = v;
  } else {
    switch (t->res) {
      case BD_RES_16: f.l = ((const int16_t*)t->items)[2 * i]; f.r = ((const int16_t*)t->items)[2 * i + 1]; break;
      case BD_RES_8: f.l = bd_lut_ulaw_q15[t->items[2 * i]]; f.r = bd_lut_ulaw_q15[t->items[2 * i + 1]]; break;
      default:
        p = t->items + i * 3;
        f.l = sext12((uint32_t)p[0] | (((uint32_t)p[1] & 0x0f) << 8)) << 4;
        f.r = sext12(((uint32_t)p[1] >> 4) | ((uint32_t)p[2] << 4)) << 4;
    }
  }
  return f;
}

static void tape_reset(bd_tape* t) {
  t->write_ptr = 0;
  t->tail_write_ptr = 0;
  t->tail_xfade_duration = 0;
  t->frozen = 0;
  t->written_since_last_frozen = 0;
  t->duration = t->max_delay;
  if (!t->items || t->max_delay <= 0) return;   /* (port) an engine set up to share another's tape */
  bd_frame zero = { 0, 0 };
  for (int32_t i = 0; i < t->max_delay + INTERP_TAIL; ++i) tape_store(t, i, zero);
  memset(t->freeze_tail, 0, FREEZE_TAIL * sizeof(bd_frame));
}

static inline void tape_write_raw(bd_tape* t, bd_frame in) {
  tape_store(t, t->write_ptr, in);
  if (t->write_ptr < INTERP_TAIL) tape_store(t, t->write_ptr + t->max_delay, in);
  t->write_ptr = t->write_ptr == 0 ? t->max_delay - 1 : t->write_ptr - 1;
  if (++t->written_since_last_frozen >= t->max_delay) t->written_since_last_frozen = 0;
  if (++t->duration >= t->max_delay) t->duration = t->max_delay;
}

static inline void tape_write(bd_tape* t, bd_frame in, int frozen) {
  if (frozen) {
    if (!t->frozen) t->tail_write_ptr = 0;
    if (t->tail_write_ptr < FREEZE_TAIL) t->freeze_tail[t->tail_write_ptr++] = in;
    t->written_since_last_frozen = 0;
  } else {
    if (t->frozen && t->tail_write_ptr) t->tail_xfade_duration = t->tail_write_ptr;
    if (t->tail_write_ptr) {
      int32_t pos = t->tail_xfade_duration - t->tail_write_ptr;
      int32_t gain = smoothstep16((int32_t)(((int64_t)t->tail_write_ptr << 16) / t->tail_xfade_duration));
      in.l += mul16(t->freeze_tail[pos].l - in.l, gain);
      in.r += mul16(t->freeze_tail[pos].r - in.r, gain);
      --t->tail_write_ptr;
    }
    tape_write_raw(t, in);
  }
  t->frozen = (uint8_t)frozen;
}

/* Read<INTERPOLATION_HERMITE>(integral, fractional Q10). The tape format is
   chosen once per read, and the four neighbouring samples are fetched in one
   go, as Beads' LoadQuad does. */
static inline int32_t hermite(int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t frac) {
  int32_t c = (x2 - x0) >> 1;
  int32_t v = x1 - x2;
  int32_t w = c + v;
  int32_t a = w + v + ((x3 - x1) >> 1);
  int32_t b_neg = w + a;
  return (((((((a * frac) >> 10) - b_neg) * frac) >> 10) + c) * frac >> 10) + x1;
}

static inline bd_frame tape_read(const bd_tape* t, int32_t delay_integral, int32_t frac) {
  int32_t i = (t->write_ptr + delay_integral + t->max_delay - 1) % t->max_delay;
  if (i < 0) i += t->max_delay;
  bd_frame r;
  if (t->mono) {
    int32_t x0, x1, x2, x3;
    if (t->res == BD_RES_16) {
      const int16_t* p = (const int16_t*)t->items + i;
      x0 = p[0]; x1 = p[1]; x2 = p[2]; x3 = p[3];
    } else if (t->res == BD_RES_8) {
      const uint8_t* p = t->items + i;
      x0 = bd_lut_ulaw_q15[p[0]]; x1 = bd_lut_ulaw_q15[p[1]];
      x2 = bd_lut_ulaw_q15[p[2]]; x3 = bd_lut_ulaw_q15[p[3]];
    } else {
      x0 = tape_load(t, i).l; x1 = tape_load(t, i + 1).l;
      x2 = tape_load(t, i + 2).l; x3 = tape_load(t, i + 3).l;
    }
    r.l = r.r = hermite(x0, x1, x2, x3, frac);
  } else {
    bd_frame f0, f1, f2, f3;
    if (t->res == BD_RES_16) {
      const int16_t* p = (const int16_t*)t->items + 2 * i;
      f0.l = p[0]; f0.r = p[1]; f1.l = p[2]; f1.r = p[3];
      f2.l = p[4]; f2.r = p[5]; f3.l = p[6]; f3.r = p[7];
    } else {
      f0 = tape_load(t, i); f1 = tape_load(t, i + 1); f2 = tape_load(t, i + 2); f3 = tape_load(t, i + 3);
    }
    r.l = hermite(f0.l, f1.l, f2.l, f3.l, frac);
    r.r = hermite(f0.r, f1.r, f2.r, f3.r, frac);
  }
  return r;
}

static inline bd_frame tape_read_q10(const bd_tape* t, int32_t delay) {
  return tape_read(t, delay >> 10, delay & 1023);
}

/* ------------------------------------------------------------------------ */
/* Grains (grain_envelope.h, voice_stealing_envelope.h, delay_grain.h)       */
/* ------------------------------------------------------------------------ */

enum { ST_ATTACK, ST_SUSTAIN, ST_RELEASE, ST_DONE };

static void genv_wrap(bd_grain_env* g) {
  if (g->phase >= Q30) {
    int32_t old = g->inc[g->stage];
    int32_t left = g->phase - Q30;
    ++g->stage;
    g->phase = old ? (int32_t)((int64_t)left * g->inc[g->stage] / old) : 0;
  }
}

static void genv_start(bd_grain_env* g, int32_t duration_q8, int32_t shape, int32_t frac_q16,
                       int32_t min_attack_q8, int32_t min_release_q8) {
  g->stage = ST_ATTACK;
  g->phase = 0;
  int32_t sustain = imax(Q16 - 3 * shape, 0);
  int32_t s = (shape - 21845) * 3 / 2;
  if (s < 0) s = 0;
  else if (s < Q16 / 2) s = 4 * mul16(mul16(s, s), s);
  int32_t attack = s;
  int32_t release = mul16(Q16 - attack, Q16 - sustain);
  int32_t att_d = imax(mul16(duration_q8, attack), min_attack_q8 + 3 * mul16(min_attack_q8, sustain));
  int32_t sus_d = imax(mul16(duration_q8, sustain), 2 << 8);
  int32_t rel_d = imax(mul16(duration_q8, release), min_release_q8);
  g->inc[ST_ATTACK] = (int32_t)(((int64_t)1 << 38) / att_d);
  g->inc[ST_SUSTAIN] = (int32_t)(((int64_t)1 << 38) / sus_d);
  g->inc[ST_RELEASE] = (int32_t)(((int64_t)1 << 38) / rel_d);
  g->inc[ST_DONE] = 0;
  g->smoothness = attack < Q16 / 2 ? 2 * attack : 2 * Q16 - 2 * attack;
  g->phase += (int32_t)(((int64_t)g->inc[g->stage] * frac_q16) >> 16);
  genv_wrap(g);
}

static inline int32_t genv_render(bd_grain_env* g) {
  if (g->stage == ST_DONE) return 0;
  int32_t value;
  if (g->stage == ST_SUSTAIN) {
    value = Q15;
  } else {
    int attack = g->stage == ST_ATTACK;
    int32_t t = attack ? (g->phase >> 15) : Q15 - (g->phase >> 15);
    int32_t base = t;
    if (!attack) { int32_t t2 = mul15(t, t); base = mul15(t2, t2); }
    value = base + mul16(smoothstep15(t) - base, g->smoothness);
  }
  g->phase += g->inc[g->stage];
  genv_wrap(g);
  return value;
}

static inline int grain_done(const bd_grain* g) {
  return g->env.stage >= ST_DONE || g->steal.value <= 0;
}

static void grain_init(bd_grain* g) {
  g->initial_delay = g->delay = 0;
  g->speed = 1024;
  g->gain[0][0] = g->gain[0][1] = g->gain[1][0] = g->gain[1][1] = Q15 / 2;
  g->env.stage = ST_DONE;
  g->env.phase = 0;
  g->env.inc[0] = g->env.inc[1] = g->env.inc[2] = Q30 / 2;
  g->env.inc[3] = 0;
  g->env.smoothness = 0;
  g->steal.value = Q30;
  g->steal.decrement = 0;
  g->remaining = 0;
}

static void grain_fade_out(bd_grain* g, int32_t duration) {
  if (g->remaining > duration) {
    if (duration < 1) duration = 1;
    g->steal.decrement = Q30 / duration;
    g->remaining = (int32_t)(((int64_t)g->steal.value * duration) >> 30);
  }
}

static void grain_start(bd_engine* e, bd_grain* g, int32_t delay_q10, int32_t speed_q10, int32_t duration_q8,
                        int32_t shape, int32_t gain_q15, int32_t pan_q16, int32_t frac_q16) {
  const bd_tape* t = e->tp;
  int32_t cs = speed_q10 - (t->frozen ? 0 : 1024);
  int64_t delay = delay_q10 - (((int64_t)frac_q16 * cs) >> 16);
  if (cs != 0) {
    int64_t lim = ((int64_t)t->max_delay << 18) / iabs(cs);   /* Q8 samples */
    if (duration_q8 > lim) duration_q8 = (int32_t)lim;
  }
  if (cs > 0) {
    int64_t m = ((int64_t)duration_q8 * cs) >> 8;
    if (delay < m) delay = m;
  } else if (cs < 0) {
    int64_t m = ((int64_t)t->max_delay << 10) + (((int64_t)duration_q8 * cs) >> 8);
    if (delay > m) delay = m;
  }
  g->initial_delay = g->delay = (int32_t)delay;
  g->speed = speed_q10;

  /* Roughly equal power. Uses first order approximation of square root. */
  int32_t pan = pan_q16;
  if (!t->mono) {
    if (pan < Q16 / 2) {
      pan *= 2;
      g->gain[0][0] = gain_q15;
      g->gain[1][0] = 0;
      g->gain[1][1] = mul16(mul16(pan, 2 * Q16 - pan), gain_q15);
      g->gain[0][1] = mul16(mul16(Q16 - pan, Q16 + pan), gain_q15);
    } else {
      pan = (Q16 - pan) * 2;
      g->gain[1][1] = gain_q15;
      g->gain[0][1] = 0;
      g->gain[0][0] = mul16(mul16(pan, 2 * Q16 - pan), gain_q15);
      g->gain[1][0] = mul16(mul16(Q16 - pan, Q16 + pan), gain_q15);
    }
  } else {
    g->gain[1][0] = mul16(mul16(pan, 2 * Q16 - pan), gain_q15);
    g->gain[0][0] = mul16(mul16(Q16 - pan, Q16 + pan), gain_q15);
    g->gain[0][1] = g->gain[1][1] = 0;
  }
  int32_t min_attack_q8 = (int32_t)(((int64_t)e->sample_rate * 256) / 2000);   /* 0.5 ms */
  int32_t min_release_q8 = (int32_t)(((int64_t)e->sample_rate * 256) / 100);   /* 10 ms */
  genv_start(&g->env, duration_q8, shape, frac_q16, min_attack_q8, min_release_q8);
  g->remaining = duration_q8 >> 8;
  g->steal.value = Q30;
  g->steal.decrement = 0;
}

#define SAFETY_ZONE 256

/* Render<HEAD_CROSSING_POLICY_STOP, fade_out = true> */
static inline void grain_render(bd_grain* g, const bd_tape* t, bd_frame* out) {
  if (grain_done(g)) return;
  g->delay -= g->speed;
  if (!t->frozen) g->delay += 1024;
  const int32_t tail = t->max_delay << 10;
  if (g->delay < 0 || g->delay >= tail) {
    g->delay = 0;
    g->env.stage = ST_DONE;
  }
  int32_t d = iabs(g->delay >= tail / 2 ? g->delay - tail : g->delay);
  int32_t fade = d < (SAFETY_ZONE << 10) ? (d >> 3) : Q15;
  bd_frame s = tape_read_q10(t, g->delay);
  int32_t env = genv_render(&g->env);
  g->steal.value -= g->steal.decrement;
  int32_t steal = imax(g->steal.value, 0) >> 15;
  int32_t amp = mul15(mul15(smoothstep15(fade), env), steal);
  s.l = mul15(s.l, amp);
  s.r = mul15(s.r, amp);
  out->l += mul15(s.l, g->gain[0][0]) + mul15(s.r, g->gain[0][1]);
  out->r += mul15(s.l, g->gain[1][0]) + mul15(s.r, g->gain[1][1]);
  --g->remaining;
  if (grain_done(g)) g->remaining = 0;
}

/* ------------------------------------------------------------------------ */
/* Grain scheduling (granular_player.h, delay_granular_player.h)             */
/* ------------------------------------------------------------------------ */

static void player_reset(bd_player* p) {
  for (int i = 0; i < BD_NUM_GRAINS; ++i) {
    grain_init(&p->grain[i]);
    p->prio[i] = (uint8_t)i;
  }
}

static void player_init(bd_player* p, int32_t sr) {
  p->instant_xfade = sr / 200;
  p->preemptive_xfade = sr / 10;
  p->simple_threshold = sr / 40;
  p->grain_interval = 1;
  p->max_grain_duration = sr * 4;
  player_reset(p);
}

static int32_t grain_duration_q8(const bd_player* p, int32_t size) {
  const int32_t third = 21845;
  int reverse = size < third;
  int32_t clamped;
  if (reverse) {
    int32_t d3 = 3 * size;
    clamped = mul16(d3, 2 * Q16 - d3);
  } else {
    clamped = Q16 - 3 * (size - third) / 2;
  }
  int32_t shortness = imin(Q16, mul16(clamped, 67174));   /* 1.025 */
  int32_t ratio = semitones_to_ratio_q(-108 * shortness, 24);   /* Q24 */
  return (int32_t)(((int64_t)ratio * p->max_grain_duration) >> 16);
}

static void rotate_prio(bd_player* p) {
  uint8_t first = p->prio[0];
  memmove(&p->prio[0], &p->prio[1], BD_NUM_GRAINS - 1);
  p->prio[BD_NUM_GRAINS - 1] = first;
}

static void sort_grain_list(bd_player* p, int monophonic) {
  /* Whenever a new grain is started, we subject the first N grains to various
     crossfades, so they terminate quickly in case a new grain is started
     afterwards. */
  if (monophonic) {
    rotate_prio(p);
    grain_fade_out(&p->grain[p->prio[0]], imin(p->grain_interval, p->instant_xfade));
    for (int i = 1; i < BD_NUM_GRAINS - 1; ++i) grain_fade_out(&p->grain[p->prio[i]], p->instant_xfade);
  } else {
    int32_t fade_time[4];
    if (p->grain_interval < p->simple_threshold) {
      /* When the grains are arriving very fast, we simply assume that the
         first grain will be done or close to completion, and we steal it. */
      rotate_prio(p);
      fade_time[0] = p->grain_interval + 1;
      fade_time[1] = p->instant_xfade;
      fade_time[2] = p->instant_xfade;
      fade_time[3] = p->preemptive_xfade;
      if (p->grain_interval > p->instant_xfade) { int32_t x = fade_time[0]; fade_time[0] = fade_time[2]; fade_time[2] = x; }
    } else {
      /* Grains arriving slowly enough to re-sort the list: the 4 grains with
         the least time left come first, as candidates to steal. */
      for (int k = 0; k < 4; ++k) {
        int best = k;
        for (int i = k + 1; i < BD_NUM_GRAINS; ++i) {
          if (p->grain[p->prio[i]].remaining < p->grain[p->prio[best]].remaining) best = i;
        }
        uint8_t x = p->prio[k]; p->prio[k] = p->prio[best]; p->prio[best] = x;
      }
      fade_time[0] = p->instant_xfade;
      fade_time[1] = p->instant_xfade;
      fade_time[2] = p->grain_interval;
      fade_time[3] = p->preemptive_xfade;
      if (p->grain_interval > p->preemptive_xfade) { int32_t x = fade_time[2]; fade_time[2] = fade_time[3]; fade_time[3] = x; }
    }
    for (int i = 0; i < 4; ++i) grain_fade_out(&p->grain[p->prio[i]], fade_time[i]);
  }
  p->grain_interval = 0;
}

static void player_render(bd_engine* e, const bd_trig_out* trig, const bd_params* prm, bd_frame* out) {
  bd_player* p = &e->player;
  if (trig->trigger) {
    int32_t size = knob_simple(e, &prm->size);
    int reverse = size < 21845;

    /* amplitude and pan */
    int active = 0;
    for (int i = 0; i < BD_NUM_GRAINS; ++i) active += !grain_done(&p->grain[i]);
    int32_t n = active + 1;
    int32_t amplitude = bd_lut_inv_sqrt_q15[imin(n, 64)];
    int32_t spread = knob_randomness(&prm->time) + knob_randomness(&prm->size) +
                     knob_randomness(&prm->shape) + knob_randomness(&prm->pitch) + (Q16 - trig->regularity);
    spread = imin(spread, Q16);
    int32_t pan_spread = n * spread / 2;
    int32_t u = rng_q16(e), v = rng_q16(e);
    int32_t pan = Q16 / 2 + mul16(u - Q16 / 2, imin(mul16(pan_spread, v), Q16));

    const int32_t safety = SAFETY_ZONE + 1;
    int32_t max_delay = e->tp->duration - 2 * safety;
    /* argument order as GCC evaluates Beads' Start(...) call: right to left */
    int32_t shape = knob_simple(e, &prm->shape);
    int32_t duration = grain_duration_q8(p, size);
    int32_t ratio = knob_pitch_ratio(e, &prm->pitch);
    int32_t time = knob_simple(e, &prm->time);
    int32_t delay_q10 = (int32_t)(((int64_t)time * max_delay) >> 6) + (safety << 10);
    int32_t speed_q10 = (reverse ? -ratio : ratio) >> 6;
    grain_start(e, &p->grain[p->prio[0]], delay_q10, speed_q10, duration, shape, amplitude, pan, trig->fractional);
    sort_grain_list(p, prm->monophonic);
  }
  for (int i = 0; i < BD_NUM_GRAINS; ++i) grain_render(&p->grain[i], e->tp, out);
  ++p->grain_interval;
}

/* ------------------------------------------------------------------------ */
/* Grain triggers (trigger_generator.cc)                                     */
/* ------------------------------------------------------------------------ */

static void trig_init(bd_trigger* t, int32_t sr) {
  t->sample_rate = sr;
  t->max_pulse_frequency = (int64_t)(143830172671704LL / sr);   /* 130.81278 Hz, Q40 */
  t->divider_reset_interval = t->default_reset_interval = sr * 4;
  t->audio_rate_interval = sr / 48;
  t->audio_rate = 0;
  hq_init(&t->divider_quantizer, 8, 4915, 0);   /* 0.075 */
  t->accept_pulse = 1;
  t->pulse_count = 0;
  t->elapsed_since_last_pulse = sr / 2;
  t->phase = 0;
  t->target_frequency = t->frequency = TQ / t->elapsed_since_last_pulse;
  t->frequency_lp = Q16;
  t->division = 1;
  t->ratio = Q16;
  t->previous_flags = BD_GATE_LOW;
}

static void trig_pulse_train(bd_engine* e, const uint8_t* flags, int64_t frequency, int randomize, bd_trig_out* out, int n) {
  bd_trigger* t = &e->trig;
  if (!randomize) t->ratio = Q16;
  while (n--) {
    int64_t f = (frequency * t->ratio) >> 16;
    if (f < 1099511) f = 1099511;            /* 0.000001 */
    if (f > TQ / 4) f = TQ / 4;
    uint8_t fl = *flags++;
    int gate = fl & BD_GATE_HIGH;
    int trigger = fl & BD_GATE_RISING;
    if (gate) t->phase += f;
    int32_t frac = 0;
    int reset = 0;
    if (trigger) {
      t->phase = 0;
      reset = 1;
    } else if (t->phase >= TQ) {
      t->phase -= TQ;
      frac = (int32_t)((t->phase << 16) / f);
      reset = 1;
    }
    if (reset && randomize) {
      /* ratio = -1 / LogOneMinusX(0.1 + 0.9 u) */
      int32_t x = 6554 + mul16(58982, rng_q16(e));
      int32_t poly = mul16(x, Q16 + mul16(x, Q16 / 2 + mul16(x, 21843 + mul16(x, Q16 / 4))));
      t->ratio = (int32_t)(((int64_t)1 << 32) / poly);
    }
    out->fractional = frac;
    out->trigger = (uint8_t)(reset && gate);
    out->gate = (uint8_t)(t->phase < TQ / 2 && gate);
    out->regularity = randomize ? 0 : Q16;
    ++out;
  }
}

static const int kDivisionRatios[8] = { 1, 2, 3, 4, 6, 8, 12, 16 };

static void trig_divider(bd_engine* e, const uint8_t* flags, int32_t density, bd_trig_out* out, int n) {
  bd_trigger* t = &e->trig;
  int division = density < 31457 ? kDivisionRatios[hq_process(&t->divider_quantizer, mul16(density, 140902))] : 1;   /* 2.15 */
  int32_t probability_sqrt = density < 31457 ? 66191 : imax(mul16(density - 34079, 140902), 0);   /* 1.01 */
  int32_t probability = mul16(probability_sqrt, probability_sqrt);
  int32_t regularity = probability >= Q16 ? Q16 : 0;
  while (n--) {
    t->frequency += ((t->target_frequency - t->frequency) * t->frequency_lp) >> 16;
    t->phase += t->frequency;
    uint8_t fl = *flags++;
    int gate = fl & BD_GATE_HIGH;
    int trigger = fl & BD_GATE_RISING;
    if (trigger) {
      /* If we haven't seen a pulse for a long time (4s), resynchronize the
         pulse counter with the first pulse. */
      if (t->elapsed_since_last_pulse >= t->divider_reset_interval) {
        t->pulse_count = 0;
        t->phase = 0;
      }
      t->divider_reset_interval = imax(t->default_reset_interval, 4 * t->elapsed_since_last_pulse);
      /* If the pulses are coming fast (> 48 Hz), enable audio rate tracking. */
      int32_t threshold = t->audio_rate_interval + (t->audio_rate ? 8 : 0);
      t->audio_rate = (uint8_t)((t->elapsed_since_last_pulse < threshold) && (probability >= Q16));
      int32_t r = rng_q16(e);
      t->accept_pulse = (uint8_t)(r < probability && (t->pulse_count % division) == 0);
      if (t->accept_pulse) {
        int32_t period = t->elapsed_since_last_pulse;
        int64_t frequency = TQ / period;
        t->target_frequency = probability >= Q16 ? frequency / division : (frequency * probability) >> 16;
        if (t->audio_rate) {
          int no_glide = division != t->division;
          t->division = division;
          int64_t f2 = ((2 * frequency) >> 20) * (t->frequency >> 20);     /* 2 f freq, Q40 */
          int64_t up = t->frequency + ((t->frequency * 1311) >> 16) + f2;  /* 1.02 + 2f */
          int64_t down = t->frequency - ((t->frequency * 1311) >> 16) - f2;   /* 0.98 - 2f */
          no_glide |= t->target_frequency > up || t->target_frequency < down;
          /* Note: jitter is more audible on high frequencies, so the shorter
             the period, the lower the frequency of the smoothing filter. */
          t->frequency_lp = no_glide ? Q16 : (int32_t)(((int64_t)period << 16) / t->sample_rate);
        } else {
          t->phase = 0;
          t->frequency_lp = Q16;
        }
      } else if (probability < Q16) {
        t->target_frequency = (t->target_frequency * (Q16 - probability)) >> 16;
      }
      t->pulse_count = (t->pulse_count + 1) % 48;
      t->elapsed_since_last_pulse = 0;
    }
    ++t->elapsed_since_last_pulse;
    if (t->audio_rate) {
      int32_t frac = 0;
      int reset = t->phase >= TQ;
      if (reset) {
        t->phase -= TQ;
        frac = t->frequency ? (int32_t)((t->phase << 16) / t->frequency) : 0;
      }
      out->fractional = frac;
      out->trigger = (uint8_t)reset;
      out->gate = (uint8_t)(t->phase < TQ / 2);
    } else {
      if (t->phase >= TQ) t->phase = TQ;
      out->fractional = 0;
      out->trigger = (uint8_t)(t->accept_pulse && trigger);
      out->gate = (uint8_t)(t->accept_pulse && gate);
    }
    out->regularity = regularity;
    ++out;
  }
}

static void trig_render(bd_engine* e, bd_seed_state seed, const uint8_t* external, int32_t density,
                        int32_t linear_fm, int32_t exponential_fm, bd_trig_out* out, int n) {
  bd_trigger* t = &e->trig;
  if (external && seed == BD_SEED_LOCKED) {
    /* The CV only selects a divider: it swings the knob, end of the story. */
    density = clampi(density + linear_fm + exponential_fm, 0, Q16);
    trig_divider(e, external, density, out, n);
    return;
  }
  density = clampi(density + linear_fm, 0, Q16);
  int32_t fm = semitones_to_ratio(exponential_fm * 60);
  uint8_t merged[BD_MAX_BLOCK];
  for (int i = 0; i < n; ++i) {
    int gate = seed != BD_SEED_OFF;
    if (external) gate |= (*external++ & BD_GATE_HIGH);
    merged[i] = t->previous_flags = bd_gate_flags(t->previous_flags, gate);
  }
  if (density < 31457) {
    int64_t f = (((t->max_pulse_frequency * semitones_to_ratio_q(-192 * density, 30)) >> 30) * fm) >> 16;
    trig_pulse_train(e, merged, f, 0, out, n);
  } else if (density > 34079) {
    int64_t f = ((((t->max_pulse_frequency / 2) * semitones_to_ratio_q(-192 * (Q16 - density), 30)) >> 30) * fm) >> 16;
    trig_pulse_train(e, merged, f, 1, out, n);
  } else if (seed == BD_SEED_LOCKED) {
    for (int i = 0; i < n; ++i) {
      out[i].fractional = 0; out[i].trigger = 0; out[i].gate = 0; out[i].regularity = 0;
    }
  } else {
    /* Manual taps. */
    for (int i = 0; i < n; ++i) {
      t->phase += t->frequency;
      int tap = merged[i] & BD_GATE_RISING;
      if (tap) {
        t->phase = 0;
        t->frequency = TQ / t->elapsed_since_last_pulse;
        t->elapsed_since_last_pulse = 0;
      }
      ++t->elapsed_since_last_pulse;
      if (t->phase >= TQ) t->phase = TQ;
      out[i].fractional = 0;
      out[i].trigger = (uint8_t)(tap != 0);
      out[i].gate = (uint8_t)(merged[i] & BD_GATE_HIGH);
      out[i].regularity = Q16 - (density - 31457) * 25;
    }
  }
}

#ifndef BD_NO_DELAY
/* ------------------------------------------------------------------------ */
/* Delay mode: clock, play heads, controller (delay_clock.cc,                */
/* delay_controller.h/.cc)                                                   */
/* ------------------------------------------------------------------------ */

static void clock_init(bd_clock* c, int32_t sr) {
  c->average_interval = c->interval[0] = c->interval[1] = sr * 64;   /* 0.25 s, Q8 */
  c->max_interval = sr * 4;
  c->fade = Q30;
  c->min_fade_increment = Q30 / (sr / 4);
  c->max_fade_increment = Q30 / (sr / 50);
  c->fade_increment = c->max_fade_increment;
  /* The worst case scenario is a 1ms jitter on the clock signal */
  c->acceptable_jitter = (sr * 256) / 1000;
  c->jitter_sensitivity = Q16;
  c->schedule_change = 0;
  c->next_interval = 0;
  c->rotate = 0;
}

static void clock_process(bd_clock* c, int tick, int tap) {
  ++c->next_interval;
  c->fade += c->fade_increment;
  if (c->fade >= Q30 || c->fade < 0) c->fade = Q30;
  if (tick || tap) {
    if (c->next_interval < c->max_interval) {
      int32_t interval = c->next_interval << 8;
      int big_change = iabs(interval - c->interval[1]) > c->acceptable_jitter;
      int small_change = iabs(c->average_interval - c->interval[1]) > mul16(c->jitter_sensitivity, c->acceptable_jitter);
      if (big_change) {
        c->jitter_sensitivity = Q16;
        c->average_interval = interval;
      } else {
        c->average_interval += mul16(6554, interval - c->average_interval);
        c->jitter_sensitivity = imax(3277, mul16(c->jitter_sensitivity, 62259));
      }
      c->schedule_change = (uint8_t)(c->schedule_change || tap || big_change || small_change);
    }
    c->next_interval = 0;
  }
  int can_change = tap || (c->fade >= Q30);
  if (c->schedule_change && can_change) {
    ++c->rotate;
    c->interval[0] = c->interval[1];
    c->interval[1] = c->average_interval;
    c->fade = 0;
    c->fade_increment = clampi((int32_t)(((int64_t)2 << 38) / c->average_interval),
                               c->min_fade_increment, c->max_fade_increment);
    c->schedule_change = 0;
  }
}

static void clock_update(bd_clock* c, int32_t interval_q8, int32_t lp_q16) {
  c->interval[0] += mul16(lp_q16, interval_q8 - c->interval[0]);
  c->interval[1] = c->interval[0];
  c->fade = Q30;
  c->rotate = 0;
}

static inline int32_t clock_interval(const bd_clock* c, int i) { return c->interval[(i + c->rotate) % 2]; }
static inline int32_t clock_gain(const bd_clock* c, int i) {   /* Q16 */
  int j = (i + c->rotate) % 2;
  return j ? (c->fade >> 14) : Q16 - (c->fade >> 14);
}
static inline int clock_retrig(const bd_clock* c) { return c->fade == 0 && !c->next_interval; }
static inline int clock_new_index(const bd_clock* c) { return (c->rotate + 1) % 2; }

/* PlayHead::Triangle, Q16 */
static inline int32_t triangle(int32_t phase, int32_t pw) {
  if (pw <= 0) return Q16 - phase;
  if (pw >= Q16) return phase;
  return phase < pw ? (int32_t)(((int64_t)phase << 16) / pw)
                    : Q16 - (int32_t)(((int64_t)(phase - pw) << 16) / (Q16 - pw));
}

static const int32_t kPw[6] = { 0, 66, 655, 32768, 49152, 65470 };   /* 0, .001, .01, .5, .75, .999 */

static inline int32_t interpolate6(const int32_t* table, int32_t x_q16) {
  int32_t idx = x_q16 * 5;
  int32_t i = idx >> 16, f = idx & 0xffff;
  if (i >= 5) return table[5];
  return table[i] + mul16(table[i + 1] - table[i], f);
}

static void playhead_init(bd_playhead* h) {
  h->start = h->remaining = h->mid_point = 0;
  h->phase = h->phase_increment = 0;
  h->gain = 0;
}

static void playhead_start(bd_playhead* h, int32_t start, int32_t duration, int32_t gain) {
  if (duration < 1) duration = 1;
  h->start = start;
  h->remaining = duration;
  h->mid_point = duration >> 1;
  h->phase = 0;
  h->phase_increment = Q30 / duration;
  h->gain = gain;
}

static void playhead_render(bd_playhead* h, bd_tap_out* tap, int32_t shape, int slave) {
  if (h->remaining == 0) {
    tap->delay = 0;
    tap->gain = 0;
    return;
  }
  int32_t phase = h->phase >> 14;   /* Q16 */
  int32_t envelope;
  if (slave) {
    int32_t table[6] = { 0, 0, 0, triangle(phase, Q16 / 2), 0, 0 };
    envelope = interpolate6(table, shape);
  } else if (shape < 13107) {
    int32_t square = imin(100 * triangle(phase, Q16 / 2), Q16);
    int32_t tri = triangle(phase, kPw[1]);
    envelope = square + mul16(tri - square, shape * 5);
  } else {
    envelope = triangle(phase, interpolate6(kPw, shape));
  }
  tap->delay = (h->start + h->remaining) << 10;
  tap->gain = (int32_t)(((int64_t)h->gain * envelope) >> 17);   /* Q15 */
  --h->remaining;
  h->phase += h->phase_increment;
}

static int32_t tap_env_render(bd_delayctl* d, int i, int32_t shape) {
  int32_t phase = d->tap_env_phase[i] >> 14;
  int32_t envelope;
  if (shape < 13107) {
    int32_t tri = triangle(phase, kPw[1]);
    envelope = Q16 + mul16(tri - Q16, shape * 5);
  } else {
    envelope = triangle(phase, interpolate6(kPw, shape));
  }
  d->tap_env_phase[i] += d->tap_env_inc[i];
  if (d->tap_env_phase[i] >= Q30) d->tap_env_phase[i] -= Q30;
  return envelope;
}

static int32_t sine_render(bd_delayctl* d) {   /* SineOscillator::Render: 0..1 Q16 */
  uint32_t p = (uint32_t)d->tap_mod_phase + (uint32_t)d->tap_mod_frequency;
  d->tap_mod_phase = (int32_t)p;
  uint32_t i = p >> 22, f = (p >> 6) & 0xffff;
  int32_t a = bd_lut_sine_q15[i], b = bd_lut_sine_q15[i + 1];
  int32_t s = a + (int32_t)(((int64_t)(b - a) * f) >> 16);   /* Q15 */
  return Q16 / 2 + s;
}

#define K_MIN_DELAY (64 + 4)
#define NUM_DELAY_RATIOS 20

static const int32_t kRatio[NUM_DELAY_RATIOS + 2] = {
  /* Powers of 2 */
  4096, 4096, 8192, 8192, 16384, 16384, 32768, 32768, 65536, 65536,
  /* Dividers */
  65536, 65536, 32768, 21845, 16384, 10923, 8192, 6554, 5461, 4096, 4096, 4096,
};

static void delayctl_init(bd_delayctl* d, int32_t sr, int32_t max_delay) {
  memset(d, 0, sizeof(*d));
  d->max_delay = max_delay;
  clock_init(&d->clock, sr);
  for (int i = 0; i < BD_NUM_HEADS; ++i) {
    hq_init(&d->numerator_quantizer[i], 2, 3277, 0);
    playhead_init(&d->head[i]);
    playhead_init(&d->slave[i]);
  }
  walk_init(&d->position_mod, (int32_t)(429496730LL / sr));   /* 0.1 Hz, Q32 */
  walk_init(&d->shape_mod, (int32_t)(429496730LL / sr));
  d->tap_mod_phase = 0;
  d->tap_mod_frequency = (int32_t)(730144440LL / sr);         /* 0.17 Hz, Q32 */
  d->multiplier_lp = 0;
  d->shape = 0;
  d->linear_fm = 0;
  d->fm = Q16;
  d->density = 0;
  d->freeze = 0;
  d->seed_state = BD_SEED_OFF;
}

static void delayctl_render(bd_engine* e, bd_seed_state seed, const uint8_t* external, const uint8_t* freeze,
                            int32_t density, int32_t linear_fm, int32_t exponential_fm,
                            const bd_knob* position, const bd_knob* shape_knob, bd_taps* out, int n) {
  bd_delayctl* d = &e->delayctl;
  int synchronized = !(seed == BD_SEED_LOCKED && !external);
  if (synchronized) {
    linear_fm += exponential_fm;
    exponential_fm = 0;
  }
  /* Add more filtering to the CV. Because. */
  d->linear_fm += mul16(3277, linear_fm - d->linear_fm);
  density = clampi(density + d->linear_fm, 0, Q16);

  int32_t slice_modulation = mul16(65864, knob_value(e, position, &d->position_mod, n, Q16, 1));   /* 1.005 */
  int32_t shape_target = mul16(knob_value(e, shape_knob, &d->shape_mod, n, Q16, 1), 65208);       /* 0.995 */
  int32_t fm_target = semitones_to_ratio(-hfilter(&d->fm_filter, exponential_fm, 164) * 60);
  int32_t density_target = hfilter(&d->density_filter, density, 164);
  int32_t shape_inc = (shape_target - d->shape) / n;
  int32_t fm_inc = (fm_target - d->fm) / n;
  int32_t density_inc = (density_target - d->density) / n;
  int32_t shape_v = d->shape, fm_v = d->fm, density_v = d->density;

  int tap = seed == BD_SEED_ON && d->seed_state == BD_SEED_OFF;
  d->seed_state = (uint8_t)seed;

  for (int s = 0; s < n; ++s) {
    int32_t tap_ratio[2], tap_gain[2];
    density_v += density_inc;
    fm_v += fm_inc;
    int32_t dens = density_v, fm = fm_v;

    if (synchronized) {
      /* Record the interval between taps on the button or triggers on the
         SEED input. */
      clock_process(&d->clock, external && (*external & BD_GATE_RISING), tap);
      tap = 0;
      /* Use density to select a ratio, and FM to further multiply/divide it. */
      int32_t ratio = dens * NUM_DELAY_RATIOS;
      int ri = ratio >> 16;
      int32_t rf = (ratio & 0xffff) - Q16 / 2;
      rf = dens < Q16 / 2 ? rf * 4 : rf * 3 / 2;
      rf = clampi(rf + Q16 / 2, 0, Q16);
      if (ri > NUM_DELAY_RATIOS) ri = NUM_DELAY_RATIOS;
      int flip = ri & 1;
      tap_ratio[flip] = kRatio[ri];
      tap_ratio[1 - flip] = kRatio[ri + 1];
      tap_gain[flip] = Q16 - rf;
      tap_gain[1 - flip] = rf;
      tap_ratio[0] = mul16(tap_ratio[0], fm);
      tap_ratio[1] = mul16(tap_ratio[1], fm);
    } else {
      int32_t r24 = semitones_to_ratio_q(-216 * iabs(dens - Q16 / 2), 24);
      int64_t interval_q8 = (((int64_t)fm * d->max_delay) >> 8) * r24 >> 24;
      if (interval_q8 < (K_MIN_DELAY << 8)) interval_q8 = K_MIN_DELAY << 8;
      int32_t lp = clampi((int32_t)(((int64_t)10 << 24) / interval_q8), 328, 45875);   /* 0.005 .. 0.7 */
      clock_update(&d->clock, (int32_t)interval_q8, lp);
      tap_ratio[0] = Q16;
      tap_ratio[1] = 50463;   /* 0.77 */
      int32_t fade_to_odd = clampi((dens - Q16 / 2) * 25, 0, Q16);
      fade_to_odd = mul16(fade_to_odd, Q16 / 4 + mul16(49152, sine_render(d)));
      tap_gain[0] = Q16 - mul16(fade_to_odd, 13107);
      tap_gain[1] = mul16(fade_to_odd, 52429);
    }
    shape_v += shape_inc;
    int32_t shape = shape_v;

    if (freeze[s]) {
      /* Reset all play heads when freezing. */
      if (!d->freeze) {
        for (int i = 0; i < BD_NUM_HEADS; ++i) { playhead_init(&d->head[i]); playhead_init(&d->slave[i]); }
      }
      /* Reset and resync the play heads that will be faded in. */
      if (clock_retrig(&d->clock)) {
        int start = clock_new_index(&d->clock) * 2;
        for (int i = start; i < start + 2; ++i) { playhead_init(&d->head[i]); playhead_init(&d->slave[i]); }
      }
      int32_t slice_index_smooth = hfilter(&d->slice_index_filter, slice_modulation, 164);
      for (int i = 0; i < BD_NUM_HEADS; ++i) {
        bd_playhead* new_head = 0;
        if (d->head[i].remaining == 0) new_head = &d->head[i];
        else if (d->head[i].remaining == d->head[i].mid_point) new_head = &d->slave[i];
        int interval = i / 2, ratio = i % 2;
        if (new_head) {
          int32_t slice_duration = (int32_t)(((int64_t)clock_interval(&d->clock, interval) * tap_ratio[ratio]) >> 24);
          if (slice_duration < 1) slice_duration = 1;
          int num_slices = d->max_delay / slice_duration;
          if (num_slices < 1) num_slices = 1;
          if (d->numerator_quantizer[i].num_steps != num_slices) {
            hq_init(&d->numerator_quantizer[i], num_slices, 655 * num_slices, 0);   /* 0.05 / 5 * n */
          }
          int slice_index = hq_process(&d->numerator_quantizer[i], slice_index_smooth);
          playhead_start(new_head, slice_duration * slice_index + K_MIN_DELAY, slice_duration,
                         mul16(tap_gain[ratio], clock_gain(&d->clock, interval)));
        }
        playhead_render(&d->head[i], &out->tap[2 * i], shape, 0);
        playhead_render(&d->slave[i], &out->tap[2 * i + 1], shape, 1);
      }
      out->gate = (uint8_t)(d->head[clock_new_index(&d->clock) * 2].phase < Q30 / 2);
    } else {
      /* Reset all envelopes when unfreezing. */
      if (d->freeze) {
        for (int i = 0; i < BD_NUM_TAPS; ++i) { d->tap_env_phase[i] = 0; d->tap_env_inc[i] = 0; d->tap_env_delay[i] = 0; }
      }
      /* Reset and resync the envelopes that will be faded-in. */
      if (clock_retrig(&d->clock)) {
        int start = clock_new_index(&d->clock) * BD_NUM_HEADS;
        for (int i = start; i < start + BD_NUM_HEADS; ++i) { d->tap_env_phase[i] = 0; d->tap_env_inc[i] = 0; d->tap_env_delay[i] = 0; }
      }
      d->multiplier_lp += mul16(655, slice_modulation - d->multiplier_lp);
      int32_t multiplier = clampi(d->multiplier_lp * 15 + Q16, Q16, 16 * Q16);
      int mi = multiplier >> 16;
      int32_t mf = clampi(Q16 / 2 + 8 * ((multiplier & 0xffff) - Q16 / 2), 0, Q16);
      if (multiplier >= 16 * Q16) { mi = 16; mf = clampi(Q16 / 2 - 8 * (Q16 / 2), 0, Q16); }
      int32_t loudest = 0;
      int loudest_index = 0;
      for (int t = 0; t < BD_NUM_TAPS; ++t) {
        int interval = t / BD_NUM_HEADS;
        int ratio = (t / 2) % 2;
        int tp = t & 1;
        int i = (2 * ratio + ((mi & 1) ? tp : (1 - tp))) + interval * BD_NUM_HEADS;
        int64_t delay_q8 = (((int64_t)clock_interval(&d->clock, interval) * tap_ratio[ratio]) >> 16) * (mi + tp);
        if (delay_q8 < (K_MIN_DELAY << 8)) delay_q8 = K_MIN_DELAY << 8;
        if (delay_q8 > ((int64_t)d->max_delay << 8)) delay_q8 = (int64_t)d->max_delay << 8;
        if (delay_q8 != d->tap_env_delay[i]) {        /* (port) divide only when the delay moves */
          d->tap_env_delay[i] = (int32_t)delay_q8;
          d->tap_env_inc[i] = (int32_t)(((int64_t)1 << 38) / delay_q8);
        }
        out->tap[i].delay = (int32_t)(delay_q8 << 2);
        int32_t ratio_fade = tp ? mf : Q16 - mf;
        int32_t base_gain = mul16(mul16(tap_gain[ratio], ratio_fade), clock_gain(&d->clock, interval));
        out->tap[i].gain = (int32_t)(((int64_t)base_gain * tap_env_render(d, i, shape)) >> 17);
        if (base_gain > loudest) { loudest = base_gain; loudest_index = i; }
      }
      out->gate = (uint8_t)(d->tap_env_phase[loudest_index] < Q30 / 2);
    }
    d->freeze = freeze[s];
    if (external) ++external;
    ++out;
  }
  d->shape = shape_target;
  d->fm = fm_target;
  d->density = density_target;
}

/* ------------------------------------------------------------------------ */
/* Multitap delay (multitap_delay.h, tap.h)                                  */
/* ------------------------------------------------------------------------ */

static void multitap_render(bd_engine* e, const bd_taps* taps, int32_t speed, int32_t bandwidth, bd_frame* out) {
  const bd_tape* t = e->tp;
  /* Cross-fade between the original and pitch-shifted delays. */
  int32_t gain = clampi((iabs(speed - Q16) - 655) * 100, 0, Q16);
  int64_t ml = 0, mr = 0;
  const int32_t full = 1024 * 2048, half = full >> 1;
  int32_t phase_step = (speed - Q16) >> 6;   /* Q10 */
  for (int i = 0; i < BD_NUM_TAPS; ++i) {
    int32_t delay = taps->tap[i].delay;
    if (delay == 0) continue;
    int32_t tg = taps->tap[i].gain;
    /* pitch-shifted tap (two crossfaded heads) */
    int32_t* ph = &e->multitap.phase[i];
    *ph += phase_step;
    if (*ph >= full) *ph -= full; else if (*ph < 0) *ph += full;
    int32_t g1 = mul16(tg, gain);
    if (g1) {
      int32_t dly = imax(full + 1, delay + half);
      int32_t p16 = *ph >> 5;   /* Q16 */
      int32_t tri = p16 < Q16 / 2 ? 2 * p16 : 2 * Q16 - 2 * p16;
      bd_frame h1 = tape_read_q10(t, dly - *ph);
      bd_frame h2 = tape_read_q10(t, dly - ((*ph + half) % full));
      ml += (int64_t)(h2.l + mul16(h1.l - h2.l, tri)) * g1;
      mr += (int64_t)(h2.r + mul16(h1.r - h2.r, tri)) * g1;
    }
    /* no-latency tap */
    int32_t g2 = tg - g1;
    if (g2) {
      bd_frame h = tape_read_q10(t, delay);
      ml += (int64_t)h.l * g2;
      mr += (int64_t)h.r * g2;
    }
  }
  bd_frame mix = { (int32_t)(ml >> 15), (int32_t)(mr >> 15) };

  int32_t amount = iabs(2 * bandwidth - Q16);
  if (bandwidth != e->bandwidth_cache) {
    /* LP: -7 to -2 octaves, HP: -8 to -3 octaves */
    int32_t octave = bandwidth < Q16 / 2 ? -7 * Q16 + bandwidth * 10 : -8 * Q16 + (bandwidth - Q16 / 2) * 10;
    int32_t f = mul30(32113, semitones_to_ratio_q(12 * octave, 30));   /* 0.49 * 2^octave, Q16 */
    int32_t q = Q16 + mul16(16 * amount, f);
    svf_set(&e->bandwidth_filter[0], f, q);
    e->bandwidth_filter[1].g = e->bandwidth_filter[0].g;
    e->bandwidth_filter[1].r = e->bandwidth_filter[0].r;
    e->bandwidth_filter[1].h = e->bandwidth_filter[0].h;
    e->bandwidth_cache = bandwidth;
  }
  int32_t was = mul16(amount, 2 * Q16 - amount);
  int32_t wet_amount = mul16(was, 2 * Q16 - was);
  int mode = bandwidth < Q16 / 2 ? SVF_LP : SVF_HP;
  int32_t wl = svf_process(&e->bandwidth_filter[0], mix.l, mode);
  int32_t wr = svf_process(&e->bandwidth_filter[1], mix.r, mode);
  out->l += mix.l + mul16(wl - mix.l, wet_amount);
  out->r += mix.r + mul16(wr - mix.r, wet_amount);
}

#endif /* BD_NO_DELAY */
/* ------------------------------------------------------------------------ */
/* The processor (granular_processor.h, processor.cc)                        */
/* ------------------------------------------------------------------------ */

void bd_seed_rng(bd_engine* e, uint32_t seed) { e->rng = seed; }

void bd_configure(bd_engine* e, bd_res res, int mono) {
  bd_configure_length(e, res, mono, 0);
}

void bd_configure_length(bd_engine* e, bd_res res, int mono, int32_t samples) {
  e->res = (uint8_t)res;
  e->mono = (uint8_t)(mono != 0);
  bd_tape* t = &e->tape;
  e->tp = t;
  e->reader = 0;
  t->res = e->res;
  t->mono = e->mono;
  t->items = e->buffer;
  t->max_delay = bd_tape_samples(res, mono, e->buffer_bytes);
  if (samples > 0 && samples < t->max_delay) t->max_delay = samples & ~1;
  t->freeze_tail = (bd_frame*)(void*)(e->buffer + e->buffer_bytes - FREEZE_TAIL * sizeof(bd_frame));
  tape_reset(t);

  int32_t sr = e->sample_rate;
  player_init(&e->player, sr);
  trig_init(&e->trig, sr);
#ifndef BD_NO_DELAY
  delayctl_init(&e->delayctl, sr, t->max_delay);
#endif
  memset(&e->multitap, 0, sizeof(e->multitap));
  e->bandwidth_cache = -1;
  memset(e->bandwidth_filter, 0, sizeof(e->bandwidth_filter));

  /* DC blocker: high pass at 40 Hz, Q 0.5 */
  for (int i = 0; i < 2; ++i) {
    memset(&e->dc_blocker[i], 0, sizeof(bd_svf));
    svf_set(&e->dc_blocker[i], (int32_t)(((int64_t)40 << 16) / sr), Q16 / 2);
  }
  walk_init(&e->pitch_mod, (int32_t)(858993459LL / sr));   /* 0.2 Hz, Q32 */
  limiter_init(&e->input_limiter, Q30 / sr, 62259);        /* 0.95 */
  limiter_init(&e->output_limiter, Q30 / (sr * 6), 58982); /* 0.9 */
  e->feedback_ = e->speed_ = e->bandwidth_ = 0;
  e->previous_sample.l = e->previous_sample.r = 0;
  e->dry_ = e->wet_ = 0;
  e->transition = -(sr * 3) / 2;
}

void bd_init(bd_engine* e, uint8_t* buffer, uint32_t buffer_bytes, int32_t sample_rate) {
  memset(e, 0, sizeof(*e));
  e->buffer = buffer;
  e->buffer_bytes = buffer_bytes & ~7u;
  e->sample_rate = sample_rate;
  e->rng = 0x21;
  bd_configure(e, BD_RES_16, 1);
}

static const int32_t kLpCoefficient[3] = { 49152, 58982, 65470 };   /* 0.75, 0.9, 0.999 */
static const int32_t kSoftLimitGain[3] = { 114688, 85197, 0 };      /* 1.75, 1.3, 0 */

static void granular_process(bd_engine* e, const bd_trig_out* triggers, const bd_taps* taps,
                             const uint8_t* freeze, const bd_params* p, int32_t feedback,
                             const bd_frame* in, bd_frame* out, int n) {
  int32_t speed = semitones_to_ratio(knob_value(e, &p->pitch, &e->pitch_mod, n, 24 * Q16, 0));
  int32_t bandwidth = clampi(Q16 / 2 + p->size.amount / 2 + (p->size.has_cv ? p->size.cv : 0), 0, Q16);
  int32_t bw_inc = (bandwidth - e->bandwidth_) / n, bw = e->bandwidth_;
  int32_t fb_inc = (feedback - e->feedback_) / n, fbv = e->feedback_;
  int32_t sp_inc = (speed - e->speed_) / n, sp = e->speed_;

  const int32_t max_feedback_gain = taps ? 98304 : 196608;   /* 1.5 : 3 */
  const int32_t lp_coefficient = kLpCoefficient[e->res];
  const int32_t soft_limit_gain = kSoftLimitGain[e->res];
#ifndef BD_READER_ONLY
  const int32_t fb_gain = soft_limit_gain ? (int32_t)(((int64_t)max_feedback_gain << 16) / soft_limit_gain) : 0;
#endif

  for (int i = 0; i < n; ++i) {
    fbv += fb_inc;
    bw += bw_inc;
    sp += sp_inc;
#ifndef BD_READER_ONLY
    const int32_t fb = fbv;
    bd_frame input = in[i];
    bd_frame s = input;
    if (soft_limit_gain) {
      s.l += mul16(mul16(e->previous_sample.l, fb), fb_gain);
      s.r += mul16(mul16(e->previous_sample.r, fb), fb_gain);
    } else {
      int32_t g = mul16(mul16(fb, fb), max_feedback_gain);
      s.l += mul16(e->previous_sample.l, g);
      s.r += mul16(e->previous_sample.r, g);
    }
    limiter_process(&e->input_limiter, &s);
    if (soft_limit_gain) {
      s.l = soft_clip(mul16(s.l, soft_limit_gain));
      s.r = soft_clip(mul16(s.r, soft_limit_gain));
      s.l = input.l + mul16(s.l - input.l, fb);
      s.r = input.r + mul16(s.r - input.r, fb);
    }
#endif
    out[i].l = out[i].r = 0;
#ifndef BD_READER_ONLY
    if (!e->reader) tape_write(e->tp, s, freeze[i]);
#endif
    if (triggers) player_render(e, &triggers[i], p, &out[i]);
#ifndef BD_NO_DELAY
    if (taps) multitap_render(e, &taps[i], sp, bw, &out[i]);
#endif

    int32_t hl = svf_process(&e->dc_blocker[0], out[i].l, SVF_HP);
    int32_t hr = svf_process(&e->dc_blocker[1], out[i].r, SVF_HP);
    e->previous_sample.l += mul16(lp_coefficient, hl - e->previous_sample.l);
    e->previous_sample.r += mul16(lp_coefficient, hr - e->previous_sample.r);
  }
#ifdef BD_READER_ONLY
  (void)freeze; (void)in; (void)fbv; (void)max_feedback_gain; (void)soft_limit_gain;
#endif
#ifdef BD_NO_DELAY
  (void)bw; (void)sp;
#endif
  /* As in Beads, the speed and bandwidth ramps only move in delay mode. */
  if (taps) {
    e->bandwidth_ = bandwidth;
    e->speed_ = speed;
  }
  e->feedback_ = feedback;
}

void bd_process(bd_engine* e, const bd_params* p, bd_seed_state seed, const uint8_t* gate_flags,
                const uint8_t* freeze, const bd_frame* in, bd_frame* out, int n) {
  const int32_t sr = e->sample_rate;
  const int32_t max_transition = (sr * 3) / 2;   /* 1.5 s */
#ifdef BD_NO_DELAY
  /* a build without Beads' delay mode (BD_NO_DELAY: a small firmware) stays granular */
  const int delay_mode = 0;
#else
  const int delay_mode = e->transition > 0;
#endif
  const int32_t threshold = delay_mode ? 64225 : 64881;   /* 0.98 : 0.99 */
  e->transition += p->size.base > threshold ? n : -n;
  e->transition = clampi(e->transition, -max_transition, max_transition);

  int32_t expo_fm_amount = clampi(20 * (iabs(p->density - Q16 / 2) - 14746), 0, Q16);   /* 0.225 */
  int32_t lin_fm = mul16(Q16 - expo_fm_amount, p->density_cv);
  int32_t exp_fm = mul16(expo_fm_amount, p->density_cv);

  bd_trig_out triggers[BD_MAX_BLOCK];
#ifndef BD_NO_DELAY
  bd_taps taps[BD_MAX_BLOCK];
  if (delay_mode) {
    delayctl_render(e, seed, gate_flags, freeze, p->density, lin_fm, exp_fm, &p->time, &p->shape, taps, n);
    granular_process(e, 0, taps, freeze, p, p->feedback, in, out, n);
  } else
#endif
  {
    trig_render(e, seed, gate_flags, p->density, lin_fm, exp_fm, triggers, n);
    granular_process(e, triggers, 0, freeze, p, p->feedback, in, out, n);
  }

  /* Fade-out / fade-in the dry signal when switching between granular and
     delay mode. */
  int32_t fade_t = (int32_t)(((int64_t)iabs(e->transition) << 16) / ((sr * 3) / 4));
  int32_t wet_fade = smoothstep16(imin(Q16, fade_t));
  const int32_t dw = p->dry_wet;
  int32_t dry = mul16(mul16(Q16 - dw, Q16 + dw), 49152);                        /* 0.75 */
  int32_t wet = mul16(mul16(mul16(dw, 2 * Q16 - dw), 98304), wet_fade);         /* 1.5 */
  int32_t dry_inc = (dry - e->dry_) / n, wet_inc = (wet - e->wet_) / n;
  int32_t dv = e->dry_, wv = e->wet_;
  for (int i = 0; i < n; ++i) {
    bd_frame d = in[i];
#ifndef BD_READER_ONLY
    if (e->res == BD_RES_12) {
      d.l = (d.l / 8) * 8;
      d.r = (d.r / 8) * 8;
    } else if (e->res == BD_RES_8) {
      d.l = bd_lut_ulaw_q15[lin2mulaw((int16_t)clip16(d.l))];
      d.r = bd_lut_ulaw_q15[lin2mulaw((int16_t)clip16(d.r))];
    }
#endif
    dv += dry_inc;
    wv += wet_inc;
    out[i].l = mul16(dv, d.l) + mul16(wv, out[i].l);
    out[i].r = mul16(dv, d.r) + mul16(wv, out[i].r);
    limiter_process(&e->output_limiter, &out[i]);
  }
  e->dry_ = dry;
  e->wet_ = wet;
}

/* ------------------------------------------------------------------------ */
/* Display helpers (not in Beads)                                            */
/* ------------------------------------------------------------------------ */

int32_t bd_tape_peek(const bd_engine* e, int32_t delay) {
  const bd_tape* t = e->tp;
  if (t->max_delay <= 0) return 0;
  int32_t i = (t->write_ptr + 1 + delay) % t->max_delay;
  if (i < 0) i += t->max_delay;
  bd_frame f = tape_load(t, i);
  return (f.l + f.r) >> 1;
}

int bd_active_grains(const bd_engine* e, int32_t* delays, int32_t* levels, int max) {
  int n = 0;
  for (int i = 0; i < BD_NUM_GRAINS && n < max; ++i) {
    const bd_grain* g = &e->player.grain[i];
    if (grain_done(g)) continue;
    delays[n] = g->delay >> 10;
    levels[n] = imax(g->steal.value, 0) >> 15;
    ++n;
  }
  return n;
}

/* ------------------------------------------------------------------------ */
/* Shared tape (Ravel, not in Beads): one tape written by its owner, read by   */
/* any number of engines                                                     */
/* ------------------------------------------------------------------------ */

void bd_tape_setup(bd_tape* t, uint8_t* buffer, uint32_t buffer_bytes, bd_res res, int mono, int32_t samples) {
  buffer_bytes &= ~7u;
  t->res = (uint8_t)res;
  t->mono = (uint8_t)(mono != 0);
  t->items = buffer;
  t->max_delay = bd_tape_samples(res, mono, buffer_bytes);
  if (samples > 0 && samples < t->max_delay) t->max_delay = samples & ~1;
  t->freeze_tail = (bd_frame*)(void*)(buffer + buffer_bytes - FREEZE_TAIL * sizeof(bd_frame));
  tape_reset(t);
}

void bd_tape_clear(bd_tape* t) { tape_reset(t); }

void bd_tape_write(bd_tape* t, bd_frame in, int frozen) { tape_write(t, in, frozen); }

void bd_tape_put(bd_tape* t, int32_t delay, bd_frame s) {
  if (t->max_delay <= 0) return;
  int32_t i = (t->write_ptr + 1 + delay) % t->max_delay;
  if (i < 0) i += t->max_delay;
  tape_store(t, i, s);
  if (i < INTERP_TAIL) tape_store(t, i + t->max_delay, s);
}

bd_frame bd_tape_at(const bd_tape* t, int32_t delay_q10) {
  bd_frame z = { 0, 0 };
  if (t->max_delay <= 4) return z;
  int32_t lim = t->max_delay << 10;
  delay_q10 += 1024;   /* tape_read counts from the slot before the newest */
  delay_q10 %= lim;
  if (delay_q10 < 0) delay_q10 += lim;
  /* tape_read(d) reads the sample written d+1 writes ago; delay 0 = the newest */
  return tape_read(t, delay_q10 >> 10, delay_q10 & 1023);
}

void bd_share_tape(bd_engine* e, bd_tape* shared) {
  e->tp = shared;
  e->reader = 1;
  e->res = shared->res;
  e->mono = shared->mono;
  int32_t sr = e->sample_rate;
  player_init(&e->player, sr);
  trig_init(&e->trig, sr);
#ifndef BD_NO_DELAY
  delayctl_init(&e->delayctl, sr, shared->max_delay);
#endif
  memset(&e->multitap, 0, sizeof(e->multitap));
}
