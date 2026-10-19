/* Test scenarios shared by the float reference (original Beads code) and the
 * fixed-point port. Plain C, used from C and C++.
 *
 * Each scenario is a fixed set of control values held for the whole render,
 * plus an optional freeze window and an optional seed/clock gate pattern. */
#ifndef FM1_BEADS_SCENARIOS_H
#define FM1_BEADS_SCENARIOS_H

#include <stdint.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
  const char* name;
  int bits;          /* 8, 12 or 16 */
  int mono;          /* 1 = mono tape, 0 = stereo */
  float sr;          /* engine sample rate */
  float seconds;
  /* grain parameters, as Beads' knobs (0..1) and attenuverters (-1..1) */
  float time, time_amt;
  float size, size_amt;
  float shape, shape_amt;
  float pitch, pitch_amt;      /* pitch in semitones */
  float density;               /* 0..1; < 0.48 regular, > 0.52 random */
  float feedback, dry_wet;
  float freeze_from, freeze_to; /* seconds; freeze_to <= freeze_from = never */
  int seed_mode;     /* 0 = none, 1 = seed held (free running), 2 = clock pulses */
  float clock_hz;    /* for seed_mode 2 */
  int monophonic;
} Scenario;

static const Scenario kScenarios[] = {
  /* name            bits mono sr      sec  time  ta    size  sa    shape sha   pitch pa    dens  fb    dw    frz0 frz1 seed clk  mono */
  { "regular",        8,  1, 22050.f, 6.f, 0.30f, 0.0f, 0.55f, 0.0f, 0.50f, 0.0f,  0.0f, 0.0f, 0.30f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 0 },
  { "regular16",     16,  1, 44100.f, 4.f, 0.30f, 0.0f, 0.55f, 0.0f, 0.50f, 0.0f,  0.0f, 0.0f, 0.30f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 0 },
  { "dense_short",   12,  1, 44100.f, 4.f, 0.10f, 0.0f, 0.75f, 0.0f, 0.20f, 0.0f,  7.0f, 0.0f, 0.15f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 0 },
  { "reverse_up",    16,  0, 44100.f, 4.f, 0.40f, 0.0f, 0.20f, 0.0f, 0.60f, 0.0f, 12.0f, 0.0f, 0.36f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 0 },
  { "random_cloud",   8,  1, 22050.f, 6.f, 0.50f, 0.6f, 0.50f, 0.4f, 0.50f, 0.3f,  0.0f, 0.5f, 0.75f, 0.2f, 0.8f, 0.f, 0.f, 1, 0.f, 0 },
  { "frozen",        12,  1, 44100.f, 6.f, 0.60f, 0.2f, 0.45f, 0.0f, 0.70f, 0.0f, -5.0f, 0.0f, 0.70f, 0.0f, 1.0f, 2.f, 5.f, 1, 0.f, 0 },
  { "feedback",       8,  1, 22050.f, 6.f, 0.20f, 0.0f, 0.60f, 0.0f, 0.50f, 0.0f,  0.0f, 0.0f, 0.25f, 0.8f, 0.7f, 0.f, 0.f, 1, 0.f, 0 },
  { "clocked",       16,  1, 44100.f, 4.f, 0.25f, 0.0f, 0.62f, 0.0f, 0.40f, 0.0f,  0.0f, 0.0f, 0.50f, 0.0f, 1.0f, 0.f, 0.f, 2, 8.f, 0 },
  { "delay",         16,  1, 44100.f, 6.f, 0.30f, 0.0f, 1.00f, 0.0f, 0.10f, 0.0f,  0.0f, 0.0f, 0.55f, 0.5f, 0.6f, 0.f, 0.f, 2, 4.f, 0 },
  { "delay_frozen",  12,  1, 44100.f, 6.f, 0.40f, 0.0f, 1.00f, 0.0f, 0.60f, 0.0f,  0.0f, 0.0f, 0.50f, 0.0f, 1.0f, 3.f, 6.f, 2, 4.f, 0 },
  { "swarm",         16,  1, 44100.f, 4.f, 0.35f, 0.3f, 0.80f, 0.0f, 0.45f, 0.0f,  0.0f, 0.2f, 0.05f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 0 },
  { "stereo8",        8,  0, 22050.f, 4.f, 0.30f, 0.0f, 0.55f, 0.0f, 0.50f, 0.0f,  5.0f, 0.0f, 0.30f, 0.3f, 0.8f, 0.f, 0.f, 1, 0.f, 0 },
  { "mono_mode",     16,  1, 44100.f, 4.f, 0.30f, 0.0f, 0.60f, 0.0f, 0.50f, 0.0f,  0.0f, 0.0f, 0.20f, 0.0f, 1.0f, 0.f, 0.f, 1, 0.f, 1 },
};
#define NUM_SCENARIOS ((int)(sizeof(kScenarios) / sizeof(kScenarios[0])))

/* Deterministic test input: plucks every 250 ms at changing pitches, a quiet
 * saw sweep under them, and short noise bursts. Its own random generator, so
 * it never disturbs the engine's. */
typedef struct { uint32_t rng; double saw_phase; double pluck_phase; double pluck_env; double pluck_freq; } InputGen;

static inline void input_init(InputGen* g) {
  g->rng = 12345u; g->saw_phase = 0.0; g->pluck_phase = 0.0; g->pluck_env = 0.0; g->pluck_freq = 220.0;
}

static inline void input_next(InputGen* g, long n, float sr, float* l, float* r) {
  static const double kPitches[8] = { 220.0, 330.0, 277.18, 440.0, 196.0, 587.33, 293.66, 880.0 };
  long period = (long)(0.25f * sr);
  if (n % period == 0) {
    g->pluck_env = 1.0;
    g->pluck_freq = kPitches[(n / period) % 8];
  }
  double t = (double)n / sr;
  g->pluck_phase += g->pluck_freq / sr;
  if (g->pluck_phase >= 1.0) g->pluck_phase -= 1.0;
  double pluck = sin(2.0 * M_PI * g->pluck_phase) * g->pluck_env;
  g->pluck_env *= exp(-1.0 / (0.08 * sr));
  double sweep_f = 80.0 + 40.0 * sin(2.0 * M_PI * 0.1 * t);
  g->saw_phase += sweep_f / sr;
  if (g->saw_phase >= 1.0) g->saw_phase -= 1.0;
  double saw = (2.0 * g->saw_phase - 1.0) * 0.12;
  g->rng = g->rng * 1664525u + 1013904223u;
  double noise = ((double)(g->rng >> 8) / 8388608.0 - 1.0);
  double burst = (fmod(t, 1.5) < 0.04) ? noise * 0.3 : 0.0;
  double mono = 0.5 * pluck + saw + burst;
  *l = (float)mono;
  *r = (float)(0.8 * mono + 0.2 * saw);
}

/* Seed gate for sample n: returns 1 when the gate is high. */
static inline int seed_gate(const Scenario* s, long n) {
  if (s->seed_mode == 1) return 1;
  if (s->seed_mode == 2) {
    long period = (long)(s->sr / s->clock_hz);
    return (n % period) < (period / 4);
  }
  return 0;
}

static inline int frozen_at(const Scenario* s, long n) {
  float t = (float)n / s->sr;
  return s->freeze_to > s->freeze_from && t >= s->freeze_from && t < s->freeze_to;
}

#endif
