/* Renders every scenario through the fixed-point port, with the same input,
 * gates and freeze as the float reference (ref.cc), and writes float32
 * stereo files next to the reference's for comparison. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/beads/bd.h"
#include "scenarios.h"

#define BLOCK 32
static bd_engine engine;

static int32_t q16(float x) { return (int32_t)(x * 65536.0f + (x >= 0 ? 0.5f : -0.5f)); }

static int32_t to_q15(float x) {
  float v = x * 32768.0f;
  return (int32_t)(v >= 0 ? v + 0.5f : v - 0.5f);
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "out";
  double total_audio = 0, total_cpu = 0;
  for (int k = 0; k < NUM_SCENARIOS; ++k) {
    const Scenario* s = &kScenarios[k];
    if (getenv("ONLY") && strcmp(getenv("ONLY"), s->name)) continue;
    bd_res res = s->bits == 8 ? BD_RES_8 : (s->bits == 12 ? BD_RES_12 : BD_RES_16);
    int32_t frames = (int32_t)(192 * 1024 * 8 / (s->bits * 2));
    int32_t samples = s->mono ? frames * 2 : frames;
    uint32_t bytes = bd_tape_bytes(res, s->mono, samples);
    uint8_t* buffer = (uint8_t*)calloc(1, bytes);
    bd_init(&engine, buffer, bytes, (int32_t)s->sr);
    bd_configure_length(&engine, res, s->mono, samples);
    bd_seed_rng(&engine, 0x21);
    if (engine.tape.max_delay != samples) {
      fprintf(stderr, "%s: tape length %d, expected %d\n", s->name, engine.tape.max_delay, samples);
      return 1;
    }

    bd_params p;
    memset(&p, 0, sizeof(p));
    p.time.base = q16(s->time); p.time.amount = q16(s->time_amt);
    p.size.base = q16(s->size); p.size.amount = q16(s->size_amt);
    p.shape.base = q16(s->shape); p.shape.amount = q16(s->shape_amt);
    p.pitch.base = q16(s->pitch); p.pitch.amount = q16(s->pitch_amt);
    p.density = q16(s->density);
    p.feedback = q16(s->feedback);
    p.dry_wet = q16(s->dry_wet);
    p.monophonic = (uint8_t)s->monophonic;
    bd_seed_state seed = s->seed_mode == 1 ? BD_SEED_ON : BD_SEED_OFF;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s_port.f32", dir, s->name);
    FILE* fo = fopen(path, "wb");
    if (!fo) { perror(path); return 1; }
    InputGen g;
    input_init(&g);
    long total = (long)(s->seconds * s->sr);
    uint8_t prev = BD_GATE_LOW;
    clock_t cpu = 0;
    for (long n0 = 0; n0 < total; n0 += BLOCK) {
      bd_frame in[BLOCK], out[BLOCK];
      uint8_t gates[BLOCK], freeze[BLOCK];
      for (int i = 0; i < BLOCK; ++i) {
        long n = n0 + i;
        float l, r;
        input_next(&g, n, s->sr, &l, &r);
        in[i].l = to_q15(l);
        in[i].r = to_q15(r);
        prev = bd_gate_flags(prev, s->seed_mode == 2 ? seed_gate(s, n) : 0);
        gates[i] = prev;
        freeze[i] = (uint8_t)frozen_at(s, n);
      }
      clock_t c0 = clock();
      bd_process(&engine, &p, seed, s->seed_mode == 2 ? gates : NULL, freeze, in, out, BLOCK);
      cpu += clock() - c0;
      float f[BLOCK * 2];
      for (int i = 0; i < BLOCK; ++i) {
        f[2 * i] = out[i].l / 32768.0f;
        f[2 * i + 1] = out[i].r / 32768.0f;
      }
      fwrite(f, sizeof(float), BLOCK * 2, fo);
    }
    fclose(fo);
    free(buffer);
    double secs = (double)cpu / CLOCKS_PER_SEC;
    total_audio += s->seconds;
    total_cpu += secs;
    printf("port %-14s done  %.1f x realtime on this machine\n", s->name, s->seconds / (secs > 0 ? secs : 1e-9));
  }
  printf("all: %.0f x realtime\n", total_audio / total_cpu);
  return 0;
}
