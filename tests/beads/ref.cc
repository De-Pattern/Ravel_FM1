// Float reference: renders every scenario through the original Mutable
// Instruments Beads DSP code (MIT), unchanged, so the fixed-point port can be
// compared against it.
//
// Mirrors beads::Processor::Process minus the reverb and the wow and flutter,
// which the firmware does not use, and runs the granular processor at the
// scenario's sample rate with a 192 KB tape, as the firmware does.
//
// Build: see tests/beads/Makefile (needs EURORACK=<path to pichenettes/eurorack
// with stmlib checked out inside it>).

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "beads/dsp/granular_processor.h"
#include "beads/dsp/trigger_generator.h"
#include "beads/dsp/delay_controller.h"
#include "beads/dsp/limiter.h"
#include "beads/dsp/modulated_parameter.h"
#include "beads/dsp/mu_law.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "stmlib/utils/random.h"

extern "C" {
#include "scenarios.h"
}

using namespace beads;
using namespace stmlib;

static const size_t kTapeBytes = 192 * 1024;
static const size_t kBlock = 32;

// One bank layout as Beads expects (3 banks of 256 KB); the tape only uses
// the first 192 KB of the first bank.
static uint8_t bank[3][262144];

template<Resolution res, int max_delay>
struct Engine {
  GranularProcessor<res, max_delay, 1> gp;
  TriggerGenerator trig;
  DelayController delay;
  Limiter out_limiter;
  float dry_, wet_, transition;
  float sr;

  void Init(float sample_rate, bool mono) {
    sr = sample_rate;
    uint8_t* b[3] = { bank[0], bank[1], bank[2] };
    gp.Init(sample_rate, b);
    gp.Reset();
    trig.Init(sample_rate);
    delay.Init(sample_rate, gp.max_delay_times_ratio() * (mono ? 2 : 1));
    out_limiter.Init(1.0f / (sample_rate * 6.0f), 0.9f);
    dry_ = wet_ = 0.0f;
    transition = -1.5f;
  }

  void Process(SeedButtonState seed, const GateFlags* gates, const bool* freeze,
               const ModulatedParameter& density, const Parameters& p,
               float feedback, float dry_wet, bool mono,
               const FloatFrame* in, FloatFrame* out, size_t size) {
    const bool delay_mode = transition > 0.0f;
    const float threshold = delay_mode ? 0.98f : 0.99f;
    const float direction = p.size.base > threshold ? 1.0f : -1.0f;
    transition += direction * float(size) / sr;
    CONSTRAIN(transition, -1.5f, 1.5f);
    float expo_fm_amount = 20.0f * (fabsf(density.base - 0.5f) - 0.225f);
    CONSTRAIN(expo_fm_amount, 0.0f, 1.0f);

    TriggerGenerator::Output triggers[kBlock];
    DelayController::Output taps[kBlock];
    if (delay_mode) {
      delay.Render(seed, gates, freeze, density.base,
                   (1.0f - expo_fm_amount) * density.cv, expo_fm_amount * density.cv,
                   p.time, p.shape, taps, size);
      gp.Process(NULL, taps, freeze, p, feedback, mono, in, out, size);
    } else {
      trig.Render(seed, gates, density.base,
                  (1.0f - expo_fm_amount) * density.cv, expo_fm_amount * density.cv,
                  triggers, size);
      gp.Process(triggers, NULL, freeze, p, feedback, mono, in, out, size);
    }
    const float wet_fade = SmoothStep(std::min(1.0f, fabsf(transition) / 0.75f));
    const float dry = (1.0f - dry_wet) * (1.0f + dry_wet) * 0.75f;
    const float wet = dry_wet * (2.0f - dry_wet) * 1.5f * wet_fade;
    ParameterInterpolator dm(&dry_, dry, size), wm(&wet_, wet, size);
    for (size_t i = 0; i < size; ++i) {
      FloatFrame d = in[i];
      if (res == RESOLUTION_12_BIT) {
        d.l = static_cast<int32_t>(in[i].l * 4096.0f) / 4096.0f;
        d.r = static_cast<int32_t>(in[i].r * 4096.0f) / 4096.0f;
      } else if (res == RESOLUTION_8_BIT) {
        d.l = MuLaw2Lin(Lin2MuLaw(Clip16(static_cast<int32_t>(in[i].l * 32768.0f))));
        d.r = MuLaw2Lin(Lin2MuLaw(Clip16(static_cast<int32_t>(in[i].r * 32768.0f))));
      }
      out[i] = dm.Next() * d + wm.Next() * out[i];
    }
    out_limiter.Process(out, size);
  }
};

// Controls rounded to Q16 as the port receives them, so both see the same values.
static float q(float x) { return float(int32_t(x * 65536.0f + (x >= 0 ? 0.5f : -0.5f))) / 65536.0f; }

template<Resolution res, int max_delay>
static void Run(const Scenario& s, FILE* fo) {
  static Engine<res, max_delay> e;
  e.Init(s.sr, s.mono);
  Random::Seed(0x21);
  InputGen g; input_init(&g);
  long total = long(s.seconds * s.sr);
  GateFlags prev = GATE_FLAG_LOW;
  for (long n0 = 0; n0 < total; n0 += kBlock) {
    FloatFrame in[kBlock], out[kBlock];
    GateFlags gates[kBlock];
    bool freeze[kBlock];
    for (size_t i = 0; i < kBlock; ++i) {
      long n = n0 + long(i);
      input_next(&g, n, s.sr, &in[i].l, &in[i].r);
      bool gate = s.seed_mode == 2 ? seed_gate(&s, n) : false;
      prev = ExtractGateFlags(prev, gate);
      gates[i] = prev;
      freeze[i] = frozen_at(&s, n);
    }
    Parameters p;
    p.time.base = q(s.time); p.time.attenuverter = q(s.time_amt); p.time.has_cv = false; p.time.cv = 0;
    p.size.base = q(s.size); p.size.attenuverter = q(s.size_amt); p.size.has_cv = false; p.size.cv = 0;
    p.shape.base = q(s.shape); p.shape.attenuverter = q(s.shape_amt); p.shape.has_cv = false; p.shape.cv = 0;
    p.pitch.base = q(s.pitch); p.pitch.attenuverter = q(s.pitch_amt); p.pitch.has_cv = false; p.pitch.cv = 0;
    p.monophonic_mode = s.monophonic;
    ModulatedParameter d; d.base = q(s.density); d.cv = 0.0f;
    SeedButtonState seed = s.seed_mode == 1 ? SEED_BUTTON_STATE_ON : SEED_BUTTON_STATE_OFF;
    e.Process(seed, s.seed_mode == 2 ? gates : NULL, freeze, d, p,
              q(s.feedback), q(s.dry_wet), s.mono, in, out, kBlock);
    fwrite(out, sizeof(FloatFrame), kBlock, fo);
  }
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "out";
  for (int i = 0; i < NUM_SCENARIOS; ++i) {
    const Scenario& s = kScenarios[i];
    std::string path = std::string(dir) + "/" + s.name + "_ref.f32";
    FILE* fo = fopen(path.c_str(), "wb");
    if (!fo) { perror(path.c_str()); return 1; }
    memset(bank, 0, sizeof(bank));
    switch (s.bits) {
      case 8:  Run<RESOLUTION_8_BIT,  int(kTapeBytes * 8 / 16)>(s, fo); break;
      case 12: Run<RESOLUTION_12_BIT, int(kTapeBytes * 8 / 24)>(s, fo); break;
      default: Run<RESOLUTION_16_BIT, int(kTapeBytes * 8 / 32)>(s, fo); break;
    }
    fclose(fo);
    printf("ref %-14s done\n", s.name);
  }
  return 0;
}
