#include "mod.h"

#include <math.h>
#include <string.h>

namespace mod {

namespace {

const float kTwoPi = 6.28318530718f;
// LFO sync divisions in quarter notes per cycle: 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32.
const double kDivisionBeats[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
const int kNumDivisions = sizeof(kDivisionBeats) / sizeof(kDivisionBeats[0]);

// Knob 0..1 -> seconds, 1 ms .. 10 s (exponential, like an envelope pot).
float KnobToSeconds(float k) { return 0.001f * powf(10000.0f, k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k)); }
// Knob 0..1 -> Hz, 0.01 .. 30 Hz.
float KnobToHz(float k) { return 0.01f * powf(3000.0f, k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k)); }

}  // namespace

void Modulation::Init(uint32_t seed) {
  memset(this, 0, sizeof(*this));
  rng_ = seed ? seed : 1;
  for (int i = 0; i < kNumLfos; ++i) lfo_to_[i] = lfo_held_[i] = Random();
}

float Modulation::Random() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;   // -1..1
}

float Modulation::Lfo(int i, const LfoSettings& s, const Inputs& in, float dt) {
  float previous = lfo_phase_[i];
  if (s.sync && in.playing && in.ppq >= 0.0) {
    // Locked to the song position, so the LFO lines up with the bar.
    int d = s.division < 0 ? 0 : (s.division >= kNumDivisions ? kNumDivisions - 1 : s.division);
    double cycles = in.ppq / kDivisionBeats[d];
    lfo_phase_[i] = static_cast<float>(cycles - floor(cycles));
  } else {
    float hz;
    if (s.sync) {
      int d = s.division < 0 ? 0 : (s.division >= kNumDivisions ? kNumDivisions - 1 : s.division);
      hz = static_cast<float>(in.bpm / 60.0 / kDivisionBeats[d]);   // tempo, transport stopped
    } else {
      hz = KnobToHz(s.rate);
    }
    lfo_phase_[i] += hz * dt;
    lfo_phase_[i] -= floorf(lfo_phase_[i]);
  }
  float p = lfo_phase_[i];
  bool wrapped = p < previous;
  if (wrapped) {   // a new cycle: new random values
    lfo_held_[i] = Random();
    lfo_from_[i] = lfo_to_[i];
    lfo_to_[i] = Random();
  }
  switch (s.shape) {
    case SHAPE_SINE: return sinf(kTwoPi * p);
    case SHAPE_TRIANGLE: return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    case SHAPE_SAW_UP: return 2.0f * p - 1.0f;
    case SHAPE_SAW_DOWN: return 1.0f - 2.0f * p;
    case SHAPE_SQUARE: return p < 0.5f ? 1.0f : -1.0f;
    case SHAPE_SAMPLE_HOLD: return lfo_held_[i];
    case SHAPE_SMOOTH_RANDOM: {
      float t = 0.5f - 0.5f * cosf(3.14159265f * p);   // cosine interpolation between random points
      return lfo_from_[i] + (lfo_to_[i] - lfo_from_[i]) * t;
    }
    default: return 0.0f;
  }
}

float Modulation::Envelope(int i, const EnvSettings& s, bool gate, bool retrigger, float dt) {
  if ((gate && !env_gate_[i]) || (gate && retrigger)) env_stage_[i] = ATTACK;
  if (!gate && env_gate_[i] && env_stage_[i] != IDLE) env_stage_[i] = RELEASE;
  env_gate_[i] = gate;
  float& v = env_value_[i];
  switch (env_stage_[i]) {
    case ATTACK:
      v += dt / KnobToSeconds(s.attack);
      if (v >= 1.0f) { v = 1.0f; env_stage_[i] = DECAY; }
      break;
    case DECAY:
      v -= dt / KnobToSeconds(s.decay) * (1.0f - s.sustain);
      if (v <= s.sustain) { v = s.sustain; env_stage_[i] = SUSTAIN; }
      break;
    case SUSTAIN:
      v = s.sustain;
      break;
    case RELEASE:
      v -= dt / KnobToSeconds(s.release);
      if (v <= 0.0f) { v = 0.0f; env_stage_[i] = IDLE; }
      break;
    case IDLE:
      v = 0.0f;
      break;
  }
  return v;
}

void Modulation::Process(const Settings& s, const Inputs& in, float dt) {
  float src[SRC_LAST];
  src[SRC_OFF] = 0.0f;
  for (int i = 0; i < kNumLfos; ++i) lfo_value_[i] = Lfo(i, s.lfo[i], in, dt);
  src[SRC_LFO1] = lfo_value_[0];
  src[SRC_LFO2] = lfo_value_[1];
  for (int i = 0; i < kNumEnvs; ++i) {
    const EnvSettings& e = s.env[i];
    bool gate = false, retrigger = false;
    switch (e.trigger) {
      case TRIG_MIDI: gate = in.midi_gate; retrigger = in.midi_retrigger; break;
      case TRIG_LFO1: gate = lfo_value_[0] > 0.0f; break;
      case TRIG_LFO2: gate = lfo_value_[1] > 0.0f; break;
      case TRIG_BUTTON: gate = in.button_gate; break;
    }
    src[SRC_ENV1 + i] = Envelope(i, e, gate, retrigger, dt);
  }
  src[SRC_VELOCITY] = in.velocity;

  for (int d = 0; d < DST_LAST; ++d) out_[d] = 0.0f;
  for (int k = 0; k < kNumSlots; ++k) {
    int a = s.source[k], d = s.dest[k];
    if (a <= SRC_OFF || a >= SRC_LAST || d <= DST_OFF || d >= DST_LAST) continue;
    out_[d] += src[a] * s.amount[k];
  }
}

}  // namespace mod
