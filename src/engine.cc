// Clouds (Parasites firmware) for MPC OS: Mutable Instruments Clouds' granular processor, with the
// Parasites modes Oliverb and Resonestor, as an MPC insert effect.
//
// Replaces the firmware main loop (clouds/clouds.cc): parameters come from plugin params instead of
// pots/CV, the blend knob's four functions are separate params, and each 128-frame MPC block is run
// as four 32-frame blocks (the module's block size), each followed by Prepare() — the work the
// module does in its main loop (FFT frames in Spectral, WSOLA correlation in Stretch/Oliverb).
//
// The processor runs at the MPC's 44.1 kHz instead of the module's 32 kHz: pitch is unaffected,
// buffer length and time constants shrink by 32/44.1.

#include <math.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include "clouds/dsp/granular_processor.h"
#include "midi_in.h"
#include "mod.h"

extern "C" {
#include "engine.h"
}

using namespace clouds;

namespace {

const size_t kBlock = 32;                    // clouds::kMaxBlockSize
const size_t kLargeBufferSize = 118784;      // the module's block_mem
const size_t kSmallBufferSize = 65536 - 128; // the module's block_ccm
const float kSmoothing = 0.1f;               // per 32-frame block, ~7 ms, like the module's CV filters
// The module's pots read at most 65535/65536, never 1.0. The DSP relies on that: e.g. the dry/wet
// crossfade looks up table[16 * x + 1] in a 17-entry table, one past the end at exactly 1.0.
const float kMaxKnob = 65535.0f / 65536.0f;

enum Param {
  P_MODE, P_QUALITY,
  P_POSITION, P_SIZE, P_PITCH, P_DENSITY, P_TEXTURE,
  P_DRY_WET, P_SPREAD, P_FEEDBACK, P_REVERB,
  P_FREEZE, P_REVERSE, P_TRIGGER,
  P_MIDI_PITCH, P_MIDI_ROOT,
  P_IN_GAIN, P_OUT_GAIN,
  // modulation page: per envelope attack, decay, sustain, release, trigger; per LFO shape, rate, sync,
  // division; per matrix slot source, destination, amount
  P_MOD_FIRST,
  P_ENV_FIRST = P_MOD_FIRST,
  P_LFO_FIRST = P_ENV_FIRST + mod::kNumEnvs * 5,
  P_SLOT_FIRST = P_LFO_FIRST + mod::kNumLfos * 4,
  P_MANUAL = P_SLOT_FIRST + mod::kNumSlots * 3,   // the MANUAL tab's topic; skin only, the engine ignores it
  P_LAST
};

enum Kind { CONTINUOUS, OPTION, MOMENTARY };

struct ParamInfo {
  const char* key;
  float def;
  Kind kind;
};

// Same list as params.json (names, ranges, labels), in VST order. The modulation entries are filled in by
// InitModParams(); params.json gets them from skin/gen_params.py.
ParamInfo kParams[P_LAST] = {
  { "mode", 0, OPTION },          // Granular
  { "quality", 0, OPTION },       // 16-bit stereo
  { "position", 0.5f, CONTINUOUS },
  { "size", 0.5f, CONTINUOUS },
  { "pitch", 0.0f, CONTINUOUS },  // semitones
  { "density", 0.7f, CONTINUOUS },  // 12 o'clock is silence in Granular; start with regular grains
  { "texture", 0.5f, CONTINUOUS },
  { "dry_wet", 0.5f, CONTINUOUS },
  { "spread", 0.5f, CONTINUOUS },
  { "feedback", 0.0f, CONTINUOUS },
  { "reverb", 0.0f, CONTINUOUS },
  { "freeze", 0, OPTION },
  { "reverse", 0, OPTION },
  { "trigger", 0, MOMENTARY },
  { "midi_pitch", 1, OPTION },      // notes transpose, like the module's V/Oct input
  { "midi_root", 60, OPTION },      // the note that plays at the Pitch knob's setting (integer)
  { "in_gain", 0.0f, CONTINUOUS },    // dB before the processor, like the module's IN GAIN (-18..+6 there)
  { "out_gain", 6.0f, CONTINUOUS },   // dB after it, into the limiter
};

// Control names per mode (Granular, Stretch, Looping Delay, Spectral, Oliverb, Resonestor), from the Clouds and
// Parasites manuals. MPC shows them on the Q-Links through the wrapper's dynamic_name (get_param("<key>_name"));
// skin/gen_layout.py has the same table for the on-screen labels.
struct ModeNames {
  const char* key;
  const char* name[6];
};
const ModeNames kNames[] = {
  { "position", { "Position", "Scrub", "Delay", "Buffer", "Predelay", "Burst" } },
  { "size", { "Size", "Window", "Loop Size", "Warp", "Room Size", "Chord" } },
  { "texture", { "Texture", "Filter", "Filter", "Quantize", "Damping", "Damping" } },
  { "density", { "Density", "Diffusion", "Diffusion", "Refresh", "Decay", "Decay" } },
  { "pitch", { "Pitch", "Pitch", "Pitch", "Pitch", "Shimmer", "Pitch" } },
  { "spread", { "Spread", "Spread", "Spread", "Spread", "Diffusion", "Voices L/R" } },
  { "dry_wet", { "Blend", "Blend", "Blend", "Blend", "Dry/Wet", "Distortion" } },
  { "feedback", { "Feedback", "Feedback", "Feedback", "Feedback", "Mod Speed", "Harmonics" } },
  { "reverb", { "Reverb", "Reverb", "Reverb", "Reverb", "Mod Amount", "Scatter" } },
  { "freeze", { "Freeze", "Freeze", "Freeze", "Freeze", "Infinite", "Voice Lock" } },
  { "trigger", { "Trigger", "Loop Sync", "Tap", "Glitch", "Clock", "Strike" } },
};

char mod_keys[P_MANUAL - P_MOD_FIRST][16];

void InitModParams() {
  static bool done = false;
  if (done) return;
  done = true;
  int k = 0;
  const char* env_keys[5] = { "attack", "decay", "sustain", "release", "trig" };
  const float env_defaults[5] = { 0.1f, 0.5f, 0.7f, 0.5f, mod::TRIG_MIDI };
  for (int e = 0; e < mod::kNumEnvs; ++e) {
    for (int j = 0; j < 5; ++j, ++k) {
      snprintf(mod_keys[k], sizeof mod_keys[k], "env%d_%s", e + 1, env_keys[j]);
      kParams[P_MOD_FIRST + k] = { mod_keys[k], env_defaults[j], j == 4 ? OPTION : CONTINUOUS };
    }
  }
  const char* lfo_keys[4] = { "shape", "rate", "sync", "div" };
  const float lfo_defaults[4] = { mod::SHAPE_SINE, 0.5f, 0, 4 };   // 4 = 1/4 note
  for (int l = 0; l < mod::kNumLfos; ++l) {
    for (int j = 0; j < 4; ++j, ++k) {
      snprintf(mod_keys[k], sizeof mod_keys[k], "lfo%d_%s", l + 1, lfo_keys[j]);
      kParams[P_MOD_FIRST + k] = { mod_keys[k], lfo_defaults[j], j == 1 ? CONTINUOUS : OPTION };
    }
  }
  const char* slot_keys[3] = { "src", "dst", "amt" };
  for (int m = 0; m < mod::kNumSlots; ++m) {
    for (int j = 0; j < 3; ++j, ++k) {
      snprintf(mod_keys[k], sizeof mod_keys[k], "mod%d_%s", m + 1, slot_keys[j]);
      kParams[P_MOD_FIRST + k] = { mod_keys[k], 0.0f, j == 2 ? CONTINUOUS : OPTION };   // amount in %
    }
  }
  kParams[P_MANUAL] = { "manual_page", 0, OPTION };
}

template<typename T> T Clamp(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

struct Instance {
  GranularProcessor processor;
  uint8_t* large_buffer;
  uint8_t* small_buffer;

  float param[P_LAST];
  float smoothed[P_LAST];
  bool smoothing_started;

  bool trigger_pending;  // a trigger param press or MIDI note-on, consumed by the next block
  int notes_held;        // MIDI notes down: the gate input (Resonestor, looping delay)
  float note_offset;     // semitones from the last note-on, held after note-off (like a V/Oct CV)
  float limiter_peak;    // output limiter's envelope (linear, both channels)
  ShortFrame gained_in[kBlock];
  bool midi_seen;        // logged the first MIDI event
  midi_in::Port* midi;   // our own MIDI input ("Overcast N"): MPC sends no MIDI to insert effects

  mod::Modulation modulation;
  float velocity;        // last note-on velocity, 0..1 (a modulation source)
  bool midi_retrigger;   // a note-on since the last control step (restarts MIDI-triggered envelopes)
  bool mod_trigger_high; // the matrix's Trigger destination was above 0.5 last step
  bool playing;          // MPC transport (HAS_HOST_TRANSPORT), for synced LFOs
  double ppq, bpm;

  // Mode and quality changes re-initialise the processor's buffers (a CPU spike); a turning Q-Link would do that
  // every block. A change applies at once, further ones at most every kReconfigureBlocks.
  int applied_mode, applied_quality;
  int blocks_since_reconfigure;

  // The module runs GranularProcessor::Prepare() in its main loop, concurrently with the audio interrupt: FFT
  // frames in Spectral, WSOLA correlation, buffer set-up on a mode change. The DSP is written for that (the STFT
  // hands frames over through counters), so here it runs in its own thread too, on another core, and the audio
  // thread only processes samples. Spectral's whole-frame FFTs no longer land in single audio blocks.
  pthread_t prepare_thread;
  bool prepare_running;
  volatile bool prepare_quit;
  pthread_mutex_t prepare_mutex;
  pthread_cond_t prepare_wake;
};

const int kReconfigureBlocks = 689;   // 0.5 s of 32-frame blocks

int Option(const Instance* s, Param p) { return static_cast<int>(s->param[p] + 0.5f); }

// The firmware's objects are globals, zeroed before Init(); some members are never set by Init()
// (e.g. clouds' GranularProcessor::silence_), so instances live in zeroed memory too.
Instance* NewInstance() {
  void* mem = calloc(1, sizeof(Instance));
  return mem ? new (mem) Instance : NULL;
}

void DeleteInstance(Instance* s) {
  s->~Instance();
  free(s);
}

void* PrepareLoop(void* arg) {
  Instance* s = static_cast<Instance*>(arg);
  while (!s->prepare_quit) {
    pthread_mutex_lock(&s->prepare_mutex);
    timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 3000000;   // at least every 3 ms, as a fallback to the per-block wake-up
    if (until.tv_nsec >= 1000000000) { until.tv_sec += 1; until.tv_nsec -= 1000000000; }
    pthread_cond_timedwait(&s->prepare_wake, &s->prepare_mutex, &until);
    pthread_mutex_unlock(&s->prepare_mutex);
    // Like the module's main loop: call it repeatedly; it returns at once when there is nothing to do.
    for (int i = 0; i < 4 && !s->prepare_quit; ++i) s->processor.Prepare();
  }
  return NULL;
}

void* Create(const char* data_dir) {
  (void)data_dir;
  Instance* s = NewInstance();
  if (!s) return NULL;
  s->large_buffer = new (std::nothrow) uint8_t[kLargeBufferSize];
  s->small_buffer = new (std::nothrow) uint8_t[kSmallBufferSize];
  if (!s->large_buffer || !s->small_buffer) {
    delete[] s->large_buffer;
    delete[] s->small_buffer;
    DeleteInstance(s);
    return NULL;
  }
  memset(s->large_buffer, 0, kLargeBufferSize);
  memset(s->small_buffer, 0, kSmallBufferSize);
  s->processor.Init(s->large_buffer, kLargeBufferSize, s->small_buffer, kSmallBufferSize);
  memset(s->processor.mutable_parameters(), 0, sizeof(Parameters));
  for (int p = 0; p < P_LAST; ++p) s->param[p] = s->smoothed[p] = kParams[p].def;
  s->smoothing_started = false;
  s->trigger_pending = false;
  s->notes_held = 0;
  s->processor.set_playback_mode(PLAYBACK_MODE_GRANULAR);
  s->processor.set_quality(0);
  s->processor.Prepare();
  s->midi = midi_in::Open();
  s->modulation.Init(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s)));
  s->ppq = -1.0;
  s->bpm = 120.0;
  s->blocks_since_reconfigure = kReconfigureBlocks;   // the first change applies at once
  pthread_mutex_init(&s->prepare_mutex, NULL);
  pthread_cond_init(&s->prepare_wake, NULL);
  s->prepare_running = pthread_create(&s->prepare_thread, NULL, PrepareLoop, s) == 0;
  return s;
}

void Destroy(void* inst) {
  Instance* s = static_cast<Instance*>(inst);
  if (s->prepare_running) {
    s->prepare_quit = true;
    pthread_cond_signal(&s->prepare_wake);
    pthread_join(s->prepare_thread, NULL);
  }
  pthread_cond_destroy(&s->prepare_wake);
  pthread_mutex_destroy(&s->prepare_mutex);
  midi_in::Close(s->midi);
  delete[] s->large_buffer;
  delete[] s->small_buffer;
  DeleteInstance(s);
}

void Midi(void* inst, const uint8_t* msg, int len) {
  Instance* s = static_cast<Instance*>(inst);
  if (len < 3) return;
  if (!s->midi_seen) {
    s->midi_seen = true;
    fprintf(stderr, "Overcast: MIDI received (%02x %02x %02x)\n", msg[0], msg[1], msg[2]);
  }
  uint8_t status = msg[0] & 0xf0;
  if (status == 0x90 && msg[2] > 0) {
    s->trigger_pending = true;
    ++s->notes_held;
    s->velocity = msg[2] / 127.0f;
    s->midi_retrigger = true;
    if (Option(s, P_MIDI_PITCH)) s->note_offset = static_cast<float>(msg[1] - Option(s, P_MIDI_ROOT));
  } else if (status == 0x80 || (status == 0x90 && msg[2] == 0)) {
    if (s->notes_held > 0) --s->notes_held;
  }
}

void SetParam(void* inst, const char* key, const char* val);

// Project save/load: the wrapper stores get_param("state") as the plugin chunk ("key=value;...").
int SaveState(const Instance* s, char* buf, int buf_len) {
  int len = 0;
  for (int p = 0; p < P_LAST && len < buf_len; ++p) {
    if (kParams[p].kind == MOMENTARY) continue;
    len += snprintf(buf + len, buf_len - len, "%s=%g;", kParams[p].key, s->param[p]);
  }
  return len < buf_len ? len : buf_len - 1;
}

void LoadState(Instance* s, const char* state) {
  char item[64];
  while (*state) {
    size_t n = strcspn(state, ";");
    if (n < sizeof item) {
      memcpy(item, state, n);
      item[n] = 0;
      char* eq = strchr(item, '=');
      if (eq) {
        *eq = 0;
        if (strcmp(item, "state")) SetParam(s, item, eq + 1);
      }
    }
    state += n;
    if (*state == ';') ++state;
  }
}

void SetParam(void* inst, const char* key, const char* val) {
  Instance* s = static_cast<Instance*>(inst);
  if (!strcmp(key, "state")) {
    LoadState(s, val);
    return;
  }
  if (!strcmp(key, "host_transport")) {
    int playing = 0;
    double ppq = -1.0, bpm = 0.0;
    if (sscanf(val, "%d %lf %lf", &playing, &ppq, &bpm) >= 2) {
      s->playing = playing != 0;
      s->ppq = ppq;
      if (bpm > 0.0) s->bpm = bpm;
    }
    return;
  }
  for (int p = 0; p < P_LAST; ++p) {
    if (strcmp(key, kParams[p].key)) continue;
    float v = static_cast<float>(atof(val));
    if (kParams[p].kind == MOMENTARY) {
      if (v > 0.5f && s->param[p] <= 0.5f) s->trigger_pending = true;
    }
    s->param[p] = v;
    return;
  }
}

// PAGE_PROBE build: which params does MPC poll while a given skin tab is showing? Counts get_param calls per
// key and prints them every ~2 s to stderr (the MPC journal).
#ifdef PAGE_PROBE
int probe_count[P_LAST + 1];
int probe_frames;
void ProbeCount(const char* key) {
  for (int p = 0; p < P_LAST; ++p) {
    if (!strncmp(key, kParams[p].key, strlen(kParams[p].key))) { ++probe_count[p]; return; }
  }
  ++probe_count[P_LAST];
}
void ProbeReport(int frames) {
  probe_frames += frames;
  if (probe_frames < 88200) return;
  probe_frames = 0;
  char line[512];
  int len = snprintf(line, sizeof line, "Clouds probe:");
  for (int p = 0; p < P_LAST; ++p) {
    if (probe_count[p]) len += snprintf(line + len, sizeof line - len, " %s=%d", kParams[p].key, probe_count[p]);
    probe_count[p] = 0;
  }
  snprintf(line + len, sizeof line - len, " other=%d", probe_count[P_LAST]);
  probe_count[P_LAST] = 0;
  fprintf(stderr, "%s\n", line);
}
#else
void ProbeCount(const char*) { }
void ProbeReport(int) { }
#endif

const char* const kDivisionNames[] = { "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32" };

int LfoDivision(float rate_knob) { return Clamp(static_cast<int>(rate_knob * 7.0f + 0.5f), 0, 7); }

int GetParam(void* inst, const char* key, char* buf, int buf_len) {
  const Instance* s = static_cast<const Instance*>(inst);
  ProbeCount(key);
  if (!strcmp(key, "state")) return SaveState(s, buf, buf_len);
  size_t key_len = strlen(key);
  if (key_len > 8 && !strcmp(key + key_len - 8, "_display")) {
    // Readable values for the envelope times and LFO rates (dynamic_display in params.json).
    for (int p = P_MOD_FIRST; p < P_SLOT_FIRST; ++p) {
      if (strlen(kParams[p].key) != key_len - 8 || strncmp(key, kParams[p].key, key_len - 8)) continue;
      float v = Clamp(s->param[p], 0.0f, 1.0f);
      if (strstr(key, "_rate")) {
        int l = (p - P_LFO_FIRST) / 4;
        if (s->param[P_LFO_FIRST + l * 4 + 2] > 0.5f) return snprintf(buf, buf_len, "%s", kDivisionNames[LfoDivision(v)]);
        return snprintf(buf, buf_len, "%.2f Hz", 0.01f * powf(3000.0f, v));
      }
      float sec = 0.001f * powf(10000.0f, v);
      return sec < 1.0f ? snprintf(buf, buf_len, "%d ms", static_cast<int>(sec * 1000.0f + 0.5f))
                        : snprintf(buf, buf_len, "%.1f s", sec);
    }
    return 0;
  }
  if (key_len > 5 && !strcmp(key + key_len - 5, "_name")) {
    int mode = Clamp(Option(s, P_MODE), 0, 5);
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
      if (strlen(kNames[i].key) == key_len - 5 && !strncmp(key, kNames[i].key, key_len - 5)) {
        return snprintf(buf, buf_len, "%s", kNames[i].name[mode]);
      }
    }
    return 0;
  }
  for (int p = 0; p < P_LAST; ++p) {
    if (strcmp(key, kParams[p].key)) continue;
    if (kParams[p].kind != CONTINUOUS) return snprintf(buf, buf_len, "%d", Option(s, static_cast<Param>(p)));
    return snprintf(buf, buf_len, "%g", s->param[p]);
  }
  return 0;
}

// One control-rate step of the envelopes, LFOs and matrix.
const mod::Modulation& UpdateModulation(Instance* s) {
  mod::Settings ms;
  for (int e = 0; e < mod::kNumEnvs; ++e) {
    const float* v = &s->param[P_ENV_FIRST + e * 5];
    ms.env[e] = { v[0], v[1], v[2], v[3], static_cast<int>(v[4] + 0.5f) };
  }
  for (int l = 0; l < mod::kNumLfos; ++l) {
    const float* v = &s->param[P_LFO_FIRST + l * 4];
    // Synced, the rate knob picks the note value (0 = 4 bars .. 1 = 1/32); lfo<n>_div is no longer on the skin.
    ms.lfo[l] = { static_cast<int>(v[0] + 0.5f), v[1], v[2] > 0.5f, LfoDivision(v[1]) };
  }
  for (int k = 0; k < mod::kNumSlots; ++k) {
    const float* v = &s->param[P_SLOT_FIRST + k * 3];
    ms.source[k] = static_cast<int>(v[0] + 0.5f);
    ms.dest[k] = static_cast<int>(v[1] + 0.5f);
    ms.amount[k] = v[2] / 100.0f;   // -100..+100 %
  }
  mod::Inputs in = { s->notes_held > 0, s->midi_retrigger, s->param[P_TRIGGER] > 0.5f, s->velocity,
                     s->playing, s->ppq, s->bpm };
  s->midi_retrigger = false;
  s->modulation.Process(ms, in, static_cast<float>(kBlock) / 44100.0f);
  return s->modulation;
}

// Once per 32-frame block, as the module's CV scaler does before each Process().
void UpdateParameters(Instance* s) {
  for (int p = 0; p < P_LAST; ++p) {
    if (kParams[p].kind != CONTINUOUS) continue;
    if (!s->smoothing_started) s->smoothed[p] = s->param[p];
    s->smoothed[p] += kSmoothing * (s->param[p] - s->smoothed[p]);
  }
  s->smoothing_started = true;

  GranularProcessor& g = s->processor;
  int mode = Clamp(Option(s, P_MODE), 0, int(PLAYBACK_MODE_LAST) - 1);
  int quality = Clamp(Option(s, P_QUALITY), 0, 3);
  if (s->blocks_since_reconfigure < kReconfigureBlocks) ++s->blocks_since_reconfigure;
  if ((mode != s->applied_mode || quality != s->applied_quality) && s->blocks_since_reconfigure >= kReconfigureBlocks) {
    if (mode != s->applied_mode) fprintf(stderr, "Overcast: mode %d -> %d\n", s->applied_mode, mode);
    s->applied_mode = mode;
    s->applied_quality = quality;
    s->blocks_since_reconfigure = 0;
  }
  g.set_playback_mode(PlaybackMode(s->applied_mode));
  g.set_quality(s->applied_quality);

  const mod::Modulation& m = UpdateModulation(s);
  Parameters* p = g.mutable_parameters();
  p->position = Clamp(s->smoothed[P_POSITION] + m.out(mod::DST_POSITION), 0.0f, kMaxKnob);
  p->size = Clamp(s->smoothed[P_SIZE] + m.out(mod::DST_SIZE), 0.0f, kMaxKnob);
  float note = Option(s, P_MIDI_PITCH) ? s->note_offset : 0.0f;
  p->pitch = Clamp(s->smoothed[P_PITCH] + note + 24.0f * m.out(mod::DST_PITCH), -48.0f, 48.0f);
  p->density = Clamp(s->smoothed[P_DENSITY] + m.out(mod::DST_DENSITY), 0.0f, kMaxKnob);
  p->texture = Clamp(s->smoothed[P_TEXTURE] + m.out(mod::DST_TEXTURE), 0.0f, kMaxKnob);
  p->dry_wet = Clamp(s->smoothed[P_DRY_WET] + m.out(mod::DST_BLEND), 0.0f, kMaxKnob);
  p->stereo_spread = Clamp(s->smoothed[P_SPREAD] + m.out(mod::DST_SPREAD), 0.0f, kMaxKnob);
  p->feedback = Clamp(s->smoothed[P_FEEDBACK] + m.out(mod::DST_FEEDBACK), 0.0f, kMaxKnob);
  p->reverb = Clamp(s->smoothed[P_REVERB] + m.out(mod::DST_REVERB), 0.0f, kMaxKnob);
  // gates, as the module's FREEZE and TRIG inputs: on above 0.5
  p->freeze = Option(s, P_FREEZE) != 0 || m.out(mod::DST_FREEZE) > 0.5f;
  p->granular.reverse = Option(s, P_REVERSE) != 0;
  bool mod_trigger = m.out(mod::DST_TRIGGER) > 0.5f;
  if (mod_trigger && !s->mod_trigger_high) s->trigger_pending = true;
  s->mod_trigger_high = mod_trigger;
  p->trigger = s->trigger_pending;
  p->gate = s->notes_held > 0 || s->param[P_TRIGGER] > 0.5f;
  s->trigger_pending = false;
}

void Render(void* inst, int16_t* out_lr, int frames) {
  (void)inst;
  memset(out_lr, 0, sizeof(int16_t) * 2 * frames);  // an effect: the wrapper calls process()
}

inline int16_t ToShort(float x) {
  x *= 32768.0f;
  return static_cast<int16_t>(x > 32767.0f ? 32767.0f : (x < -32768.0f ? -32768.0f : x));
}

// A soft knee above 0.9 so the input gain can't hard-clip into the processor.
inline float SoftSaturate(float x) {
  const float t = 0.9f;
  float a = fabsf(x);
  if (a <= t) return x;
  float over = (a - t) / (1.0f - t);
  float y = t + (1.0f - t) * over / (1.0f + over);   // approaches 1.0
  return x < 0.0f ? -y : y;
}

// Output gain, then a stereo-linked peak limiter at -1 dBFS (instant attack via the envelope's fast rise,
// ~150 ms release), then the soft knee as a safety net.
void OutputStage(Instance* s, ShortFrame* out, size_t n) {
  const float threshold = 0.891f;          // -1 dBFS
  const float release = 1.0f - 1.0f / (0.15f * 44100.0f);
  float gain = powf(10.0f, s->smoothed[P_OUT_GAIN] / 20.0f);
  for (size_t i = 0; i < n; ++i) {
    float l = out[i].l / 32768.0f * gain;
    float r = out[i].r / 32768.0f * gain;
    float peak = fmaxf(fabsf(l), fabsf(r));
    s->limiter_peak = peak > s->limiter_peak ? peak : s->limiter_peak * release;
    float g = s->limiter_peak > threshold ? threshold / s->limiter_peak : 1.0f;
    out[i].l = ToShort(SoftSaturate(l * g));
    out[i].r = ToShort(SoftSaturate(r * g));
  }
}

void Process(void* inst, const int16_t* in_lr, int16_t* out_lr, int frames) {
  Instance* s = static_cast<Instance*>(inst);
  midi_in::Poll(s->midi, Midi, s);
  ProbeReport(frames);
  if (s->prepare_running) pthread_cond_signal(&s->prepare_wake);
  // Process() takes a non-const input; it only reads it.
  ShortFrame* in = reinterpret_cast<ShortFrame*>(const_cast<int16_t*>(in_lr));
  ShortFrame* out = reinterpret_cast<ShortFrame*>(out_lr);
  for (int offset = 0; offset < frames; offset += kBlock) {
    size_t n = frames - offset < static_cast<int>(kBlock) ? frames - offset : kBlock;
    UpdateParameters(s);
    float in_gain = powf(10.0f, s->smoothed[P_IN_GAIN] / 20.0f);
    for (size_t i = 0; i < n; ++i) {
      s->gained_in[i].l = ToShort(SoftSaturate(in[offset + i].l / 32768.0f * in_gain));
      s->gained_in[i].r = ToShort(SoftSaturate(in[offset + i].r / 32768.0f * in_gain));
    }
    s->processor.Process(s->gained_in, out + offset, n);
    if (!s->prepare_running) s->processor.Prepare();
    OutputStage(s, out + offset, n);
  }
}

const mpc_engine_t kEngine = { Create, Destroy, Midi, SetParam, GetParam, Render, Process };

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) {
  InitModParams();
  return &kEngine;
}
