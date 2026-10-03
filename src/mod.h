// Modulation for the Clouds plugin: 2 envelopes, 2 LFOs and an 8-slot matrix onto the module's CV inputs.
//
// Runs at control rate, once per 32-frame block (1378 Hz at 44.1 kHz). The matrix output is added to the knob
// values the way the module adds its CV inputs to the pots; Freeze and Trigger are gates (on above 0.5).
#pragma once

#include <stdint.h>

namespace mod {

const int kNumEnvs = 2;
const int kNumLfos = 2;
const int kNumSlots = 8;

enum Source { SRC_OFF, SRC_LFO1, SRC_LFO2, SRC_ENV1, SRC_ENV2, SRC_VELOCITY, SRC_LAST };
enum Dest {
  DST_OFF, DST_POSITION, DST_SIZE, DST_PITCH, DST_DENSITY, DST_TEXTURE, DST_BLEND, DST_SPREAD, DST_FEEDBACK,
  DST_REVERB, DST_FREEZE, DST_TRIGGER, DST_LAST
};
enum LfoShape { SHAPE_SINE, SHAPE_TRIANGLE, SHAPE_SAW_UP, SHAPE_SAW_DOWN, SHAPE_SQUARE, SHAPE_SAMPLE_HOLD,
                SHAPE_SMOOTH_RANDOM, SHAPE_LAST };
enum EnvTrigger { TRIG_MIDI, TRIG_LFO1, TRIG_LFO2, TRIG_BUTTON, TRIG_LAST };

struct EnvSettings { float attack, decay, sustain, release; int trigger; };   // knobs 0..1, trigger: EnvTrigger
struct LfoSettings { int shape; float rate; bool sync; int division; };      // rate 0..1, division: index

struct Settings {
  EnvSettings env[kNumEnvs];
  LfoSettings lfo[kNumLfos];
  int source[kNumSlots];
  int dest[kNumSlots];
  float amount[kNumSlots];   // -1..1
};

struct Inputs {
  bool midi_gate;        // a MIDI note is held
  bool midi_retrigger;   // a note-on arrived since the last block
  bool button_gate;      // the trigger button is held
  float velocity;        // last note-on velocity, 0..1
  bool playing;          // MPC transport
  double ppq;            // song position in quarter notes (-1: unknown)
  double bpm;
};

class Modulation {
 public:
  void Init(uint32_t seed);
  // One control-rate step of dt seconds; afterwards out(DST_x) is the summed modulation of that destination.
  void Process(const Settings& settings, const Inputs& inputs, float dt);
  float out(int dest) const { return out_[dest]; }

 private:
  float Lfo(int i, const LfoSettings& s, const Inputs& in, float dt);
  float Envelope(int i, const EnvSettings& s, bool gate, bool retrigger, float dt);
  float Random();

  float lfo_phase_[kNumLfos];
  float lfo_value_[kNumLfos];
  float lfo_held_[kNumLfos], lfo_from_[kNumLfos], lfo_to_[kNumLfos];
  enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
  Stage env_stage_[kNumEnvs];
  float env_value_[kNumEnvs];
  bool env_gate_[kNumEnvs];
  float out_[DST_LAST];
  uint32_t rng_;
};

}  // namespace mod
