/* Beads, fixed point: Mutable Instruments Beads' grain and delay engine
 * rewritten in integer C for Ravel, a firmware for the M-VAVE FM-1.
 *
 * Original: Copyright 2017-2021 Emilie Gillet, MIT licence (see LICENSE-beads).
 * Port: same structure and behaviour as beads/dsp (granular_processor.h,
 * delay_granular_player.h, delay_grain.h, trigger_generator.cc,
 * delay_controller.cc, multitap_delay.h and the processor's mix stage),
 * with every float replaced by fixed point. Not included: the reverb, wow and
 * flutter, and the wavetable synth mode.
 *
 * Number formats used throughout:
 *   samples   Q15 in int32_t (1.0 = 32768), with headroom
 *   controls  Q16 (1.0 = 65536); pitch in semitones Q16
 *   delays    Q10 samples (as Beads' own integer delays)
 */
#ifndef BD_H
#define BD_H

#include <stdint.h>

/* Build options for a small firmware (Ravel's FM-1 build and its emulator):
 *   BD_NO_DELAY      no delay mode: the engine stays granular at any SIZE
 *   BD_READER_ONLY   every engine reads a shared tape (bd_share_tape) and never writes its own:
 *                    no input stage (feedback, limiter, soft clip), and the input is silent */
#ifndef BD_NUM_GRAINS
#define BD_NUM_GRAINS 32        /* Beads uses 32; the firmware may use fewer */
#endif
#define BD_MAX_BLOCK 64          /* largest block bd_process accepts */

#define BD_ONE 65536

typedef struct { int32_t l, r; } bd_frame;

typedef enum { BD_RES_8 = 0, BD_RES_12 = 1, BD_RES_16 = 2 } bd_res;

/* Gate flags, as stmlib's */
enum { BD_GATE_LOW = 0, BD_GATE_HIGH = 1, BD_GATE_RISING = 2, BD_GATE_FALLING = 4 };

/* The seed button */
typedef enum { BD_SEED_OFF = 0, BD_SEED_ON = 1, BD_SEED_LOCKED = 2 } bd_seed_state;

/* One knob with its attenuverter: Beads' RandomizedParameter. With no
 * modulation routed (has_cv = 0), the amount sets random variation: positive
 * = smooth random, negative = jumpy random. With modulation routed, a
 * positive amount scales it and a negative amount adds random to it. */
typedef struct {
  int32_t base;     /* Q16 0..1; pitch: semitones Q16 */
  int32_t amount;   /* Q16 -1..1 */
  int32_t cv;       /* Q16 modulation */
  uint8_t has_cv;
} bd_knob;

typedef struct {
  bd_knob time, size, shape, pitch;
  int32_t density;      /* Q16 0..1: < 0.48 regular, 0.48..0.52 taps, > 0.52 random */
  int32_t density_cv;   /* Q16 */
  int32_t feedback;     /* Q16 0..1 */
  int32_t dry_wet;      /* Q16 0..1 */
  uint8_t monophonic;
} bd_params;

/* ---- internal state (exposed so the engine can live in a static) ---- */

typedef struct {
  uint8_t* items;
  bd_frame* freeze_tail;     /* 256 frames, Q15 */
  int32_t max_delay;         /* samples (frames when stereo) */
  int32_t write_ptr, tail_write_ptr, tail_xfade_duration;
  int32_t written_since_last_frozen, duration;
  uint8_t frozen, res, mono;
} bd_tape;

typedef struct {
  int32_t stage;
  int32_t phase;             /* Q30 */
  int32_t inc[4];            /* Q30 per sample */
  int32_t smoothness;        /* Q16 */
} bd_grain_env;

typedef struct {
  int32_t value;             /* Q30 */
  int32_t decrement;         /* Q30 */
} bd_steal_env;

typedef struct {
  int32_t remaining;
  bd_grain_env env;
  bd_steal_env steal;
  int32_t gain[2][2];        /* Q15 */
  int32_t delay, speed, initial_delay;   /* Q10 */
} bd_grain;

typedef struct {
  bd_grain grain[BD_NUM_GRAINS];
  uint8_t prio[BD_NUM_GRAINS];
  int32_t preemptive_xfade, instant_xfade, simple_threshold;
  int32_t max_grain_duration;   /* samples */
  int32_t grain_interval;
} bd_player;

typedef struct {
  uint8_t trigger, gate;
  int32_t fractional;        /* Q16 */
  int32_t regularity;        /* Q16 */
} bd_trig_out;

typedef struct {
  int32_t num_steps;
  int32_t hysteresis;        /* Q16 */
  int32_t scale;             /* steps */
  int32_t offset;            /* Q16 */
  int32_t value;
} bd_hquant;

typedef struct {
  int64_t phase;             /* Q40 */
  int64_t frequency;         /* Q40 cycles per sample */
  int64_t target_frequency;  /* Q40 */
  int32_t frequency_lp;      /* Q16 */
  int32_t ratio;             /* Q16 */
  int32_t division;
  uint8_t audio_rate, accept_pulse;
  int64_t max_pulse_frequency;   /* Q40 */
  int32_t audio_rate_interval;
  int32_t pulse_count, elapsed_since_last_pulse;
  int32_t divider_reset_interval, default_reset_interval;
  int32_t sample_rate;
  uint8_t previous_flags;
  bd_hquant divider_quantizer;
} bd_trigger;

typedef struct {
  int32_t phase, frequency;  /* Q16 */
  int32_t from, to;          /* Q16, -1..1 */
} bd_walk;

typedef struct { int32_t delay; int32_t gain; } bd_tap_out;   /* Q10, Q15 */

#define BD_NUM_TAPS 8
#define BD_NUM_HEADS 4

typedef struct {
  bd_tap_out tap[BD_NUM_TAPS];
  uint8_t gate;
} bd_taps;

typedef struct {
  int32_t start, remaining, mid_point;
  int32_t phase, phase_increment;   /* Q30 */
  int32_t gain;                     /* Q16 */
} bd_playhead;

typedef struct {
  int32_t interval[2];        /* samples Q8 */
  int32_t average_interval;   /* Q8 */
  int32_t fade, fade_increment, max_fade_increment, min_fade_increment;   /* Q30 */
  int32_t acceptable_jitter;  /* Q8 */
  int32_t jitter_sensitivity; /* Q16 */
  uint8_t schedule_change;
  int32_t next_interval, max_interval, rotate;
} bd_clock;

typedef struct {
  int32_t max_delay;
  bd_clock clock;
  bd_playhead head[BD_NUM_HEADS], slave[BD_NUM_HEADS];
  bd_hquant numerator_quantizer[BD_NUM_HEADS];
  int32_t tap_env_phase[BD_NUM_TAPS], tap_env_inc[BD_NUM_TAPS];   /* Q30 */
  int32_t tap_env_delay[BD_NUM_TAPS];                              /* Q8, the delay tap_env_inc is for */
  bd_walk position_mod, shape_mod;
  int32_t tap_mod_phase, tap_mod_frequency;   /* Q32 unsigned in int32 */
  int32_t density_filter, fm_filter, slice_index_filter;   /* Q16 hysteresis values */
  int32_t multiplier_lp;      /* Q16 */
  int32_t shape, linear_fm, fm, density;    /* Q16 (fm: ratio Q16) */
  uint8_t freeze;
  uint8_t seed_state;
} bd_delayctl;

typedef struct {
  int32_t g, r, h;           /* Q28 */
  int32_t s1, s2;            /* Q23 */
} bd_svf;

typedef struct {
  int32_t peak, peak_lp;     /* Q24 */
  int32_t decay;             /* Q30 */
  int32_t max_peak;          /* Q24 */
} bd_limiter;

typedef struct {
  int32_t phase[BD_NUM_TAPS];
} bd_multitap;

typedef struct {
  /* configuration */
  int32_t sample_rate;
  uint8_t res, mono;
  uint8_t* buffer;
  uint32_t buffer_bytes;

  bd_tape tape;
  bd_tape* tp;                            /* the tape in use: &tape, or a shared one */
  uint8_t reader;                         /* reads a shared tape and never writes it */
  bd_player player;
  bd_trigger trig;
  bd_delayctl delayctl;
  bd_multitap multitap;
  bd_svf bandwidth_filter[2];
  int32_t bandwidth_cache;
  bd_svf dc_blocker[2];
  bd_limiter input_limiter, output_limiter;
  bd_walk pitch_mod;

  int32_t feedback_, speed_, bandwidth_;   /* interpolator states, Q16 */
  bd_frame previous_sample;               /* Q15 */
  int32_t dry_, wet_;                     /* Q16 */
  int32_t transition;                     /* delay-mode transition, samples (+-1.5 s) */
  uint32_t rng;
} bd_engine;

/* Bytes the tape needs for a given length: use to size the buffer. */
uint32_t bd_tape_bytes(bd_res res, int mono, int32_t samples);

/* Length the tape holds, in samples, for a buffer size. */
int32_t bd_tape_samples(bd_res res, int mono, uint32_t buffer_bytes);

/* Set up the engine on a buffer. The buffer holds the tape plus a 2 KB freeze
 * tail. Resets the tape. */
void bd_init(bd_engine* e, uint8_t* buffer, uint32_t buffer_bytes, int32_t sample_rate);

/* Change the tape format (clears it). The tape uses the whole buffer. */
void bd_configure(bd_engine* e, bd_res res, int mono);

/* Same, with the tape limited to a length in samples (0 = the whole buffer). */
void bd_configure_length(bd_engine* e, bd_res res, int mono, int32_t samples);

/* Seed the engine's random generator (Beads' LCG). */
void bd_seed_rng(bd_engine* e, uint32_t seed);

/* Process n frames (n <= BD_MAX_BLOCK). gate_flags may be NULL (no clock
 * input); freeze is one flag per frame. in and out may not alias. */
void bd_process(bd_engine* e, const bd_params* p, bd_seed_state seed,
                const uint8_t* gate_flags, const uint8_t* freeze,
                const bd_frame* in, bd_frame* out, int n);

/* For a display, read from another context than bd_process (a stale value only
 * draws late): the tape's sample at a delay (0 = newest), Q15 mono, and the grains
 * playing now, as delays in samples and levels in Q15. Returns the number of grains. */
int32_t bd_tape_peek(const bd_engine* e, int32_t delay);
int bd_active_grains(const bd_engine* e, int32_t* delays, int32_t* levels, int max);

/* Shared tape (Ravel): a tape owned outside any engine. The owner writes it
 * (bd_tape_write, or bd_tape_put to overwrite at a delay); engines attached with
 * bd_share_tape read it and never write it (their feedback is the owner's job).
 * Delays count back from the newest sample: 0 = just written. */
void bd_tape_setup(bd_tape* t, uint8_t* buffer, uint32_t buffer_bytes, bd_res res, int mono, int32_t samples);
void bd_tape_clear(bd_tape* t);
void bd_tape_write(bd_tape* t, bd_frame in, int frozen);
void bd_tape_put(bd_tape* t, int32_t delay, bd_frame s);
bd_frame bd_tape_at(const bd_tape* t, int32_t delay_q10);
void bd_share_tape(bd_engine* e, bd_tape* shared);

/* Gate flags from a gate level, as stmlib's ExtractGateFlags. */
static inline uint8_t bd_gate_flags(uint8_t previous, int high) {
  previous &= BD_GATE_HIGH;
  if (high) return previous ? BD_GATE_HIGH : (BD_GATE_RISING | BD_GATE_HIGH);
  return previous ? BD_GATE_FALLING : BD_GATE_LOW;
}

#endif
