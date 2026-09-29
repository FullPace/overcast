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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>

#include "clouds/dsp/granular_processor.h"
#include "midi_in.h"

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
  P_PAGE_0, P_PAGE_1, P_PAGE_2, P_PAGE_3, P_PAGE_4, P_PAGE_5,
  P_IN_GAIN, P_OUT_GAIN,
  P_LAST
};

enum Kind { CONTINUOUS, OPTION, MOMENTARY, PAGE };

struct ParamInfo {
  const char* key;
  float def;
  Kind kind;
};

// Same list as params.json (names, ranges, labels), in VST order.
const ParamInfo kParams[P_LAST] = {
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
  // Page markers: each MPC tab of the skin is one mode and shows its marker (a title readout) first. MPC asks for
  // a page's params when it shows the page (verified with a probe build), so a marker being read means its tab
  // just came up: the mode follows the tab.
  { "page_0", 0, PAGE }, { "page_1", 0, PAGE }, { "page_2", 0, PAGE },
  { "page_3", 0, PAGE }, { "page_4", 0, PAGE }, { "page_5", 0, PAGE },
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

const double kPageSettle = 0.2;   // seconds

double NowSeconds() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
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
  midi_in::Port* midi;   // our own MIDI input ("Clouds N"): MPC sends no MIDI to insert effects

  // Page markers read (see kParams): a tab coming up reads only its own marker, but MPC also reads every param
  // in one go (e.g. when the plugin is inserted), all markers included. So a marker only switches the mode once
  // no other marker has been read for kPageSettle: a burst of different markers is ignored.
  volatile int page_pending;       // -1: none
  volatile bool page_burst;
  volatile double page_time;
};

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
  s->page_pending = -1;
  return s;
}

void Destroy(void* inst) {
  Instance* s = static_cast<Instance*>(inst);
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
    fprintf(stderr, "Clouds: MIDI received (%02x %02x %02x)\n", msg[0], msg[1], msg[2]);
  }
  uint8_t status = msg[0] & 0xf0;
  if (status == 0x90 && msg[2] > 0) {
    s->trigger_pending = true;
    ++s->notes_held;
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
    if (kParams[p].kind == MOMENTARY || kParams[p].kind == PAGE) continue;
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
  for (int p = 0; p < P_LAST; ++p) {
    if (strcmp(key, kParams[p].key)) continue;
    if (kParams[p].kind == PAGE) return;   // read-only
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

int GetParam(void* inst, const char* key, char* buf, int buf_len) {
  const Instance* s = static_cast<const Instance*>(inst);
  ProbeCount(key);
  if (!strcmp(key, "state")) return SaveState(s, buf, buf_len);
  size_t key_len = strlen(key);
  if (key_len > 8 && !strncmp(key, "page_", 5) && !strcmp(key + key_len - 8, "_display")) {
    // A page marker's display text (its tab title): shown, so its tab is up.
    char base[16];
    snprintf(base, sizeof base, "%.*s", static_cast<int>(key_len - 8), key);
    return GetParam(inst, base, buf, buf_len);
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
    if (kParams[p].kind == PAGE) {
      static const char* const kTitles[6] = { "Granular", "Stretch", "Looping Delay", "Spectral", "Oliverb",
                                              "Resonestor" };
      int mode = p - P_PAGE_0;
      Instance* w = const_cast<Instance*>(s);
      double now = NowSeconds();
      if (w->page_pending >= 0 && w->page_pending != mode && now - w->page_time < kPageSettle) w->page_burst = true;
      w->page_pending = mode;
      w->page_time = now;
      return snprintf(buf, buf_len, "%s", kTitles[mode]);
    }
    if (kParams[p].kind != CONTINUOUS) return snprintf(buf, buf_len, "%d", Option(s, static_cast<Param>(p)));
    return snprintf(buf, buf_len, "%g", s->param[p]);
  }
  return 0;
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
  PlaybackMode mode = PlaybackMode(Clamp(Option(s, P_MODE), 0, int(PLAYBACK_MODE_LAST) - 1));
  if (mode != g.playback_mode()) fprintf(stderr, "Clouds: mode %d -> %d\n", int(g.playback_mode()), int(mode));
  g.set_playback_mode(mode);
  g.set_quality(Clamp(Option(s, P_QUALITY), 0, 3));

  Parameters* p = g.mutable_parameters();
  p->position = Clamp(s->smoothed[P_POSITION], 0.0f, kMaxKnob);
  p->size = Clamp(s->smoothed[P_SIZE], 0.0f, kMaxKnob);
  float note = Option(s, P_MIDI_PITCH) ? s->note_offset : 0.0f;
  p->pitch = Clamp(s->smoothed[P_PITCH] + note, -48.0f, 48.0f);
  p->density = Clamp(s->smoothed[P_DENSITY], 0.0f, kMaxKnob);
  p->texture = Clamp(s->smoothed[P_TEXTURE], 0.0f, kMaxKnob);
  p->dry_wet = Clamp(s->smoothed[P_DRY_WET], 0.0f, kMaxKnob);
  p->stereo_spread = Clamp(s->smoothed[P_SPREAD], 0.0f, kMaxKnob);
  p->feedback = Clamp(s->smoothed[P_FEEDBACK], 0.0f, kMaxKnob);
  p->reverb = Clamp(s->smoothed[P_REVERB], 0.0f, kMaxKnob);
  p->freeze = Option(s, P_FREEZE) != 0;
  p->granular.reverse = Option(s, P_REVERSE) != 0;
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
  if (s->page_pending >= 0 && NowSeconds() - s->page_time > kPageSettle) {
    if (!s->page_burst) s->param[P_MODE] = static_cast<float>(s->page_pending);   // this mode's tab came up
    fprintf(stderr, "Clouds: page %d %s\n", s->page_pending, s->page_burst ? "ignored (burst)" : "shown");
    s->page_pending = -1;
    s->page_burst = false;
  }
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
    s->processor.Prepare();
    OutputStage(s, out + offset, n);
  }
}

const mpc_engine_t kEngine = { Create, Destroy, Midi, SetParam, GetParam, Render, Process };

}  // namespace

extern "C" const mpc_engine_t* mpc_engine(void) { return &kEngine; }
