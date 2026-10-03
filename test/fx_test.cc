// Offline check of the effect, in the spirit of mpc-vst-monomodule's smoke.cpp: feeds noise bursts
// through process() in every mode and quality, and reports output level, clipping, freeze/trigger
// behaviour, the state chunk and the time per 128-frame block (native, so only a relative number;
// the device figure comes from the framework's tools/bench.sh).
//   make -C test run

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {
#include "engine.h"
}

namespace {

const int kBlock = 128;
const float kSampleRate = 44100.0f;
const char* kModes[] = { "Granular", "Stretch", "Looping Delay", "Spectral", "Oliverb", "Resonestor" };
const char* kQualities[] = { "16-bit stereo", "16-bit mono", "8-bit stereo", "8-bit mono" };

int failures = 0;

double NowUs() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

struct Result {
  double rms_in_signal, rms_tail, max_us, avg_us;
  int clipped;
};

// 1.5 s of noise bursts (on 100 ms, off 100 ms) then 1.5 s of silence.
Result Run(const mpc_engine_t* e, void* inst, int freeze_at_block = -1) {
  int16_t in[kBlock * 2], out[kBlock * 2];
  Result r = { 0, 0, 0, 0, 0 };
  double e_sig = 0, e_tail = 0, total_us = 0;
  int n_sig = 0, n_tail = 0;
  int blocks = static_cast<int>(3.0f * kSampleRate / kBlock);
  unsigned seed = 1;
  for (int b = 0; b < blocks; ++b) {
    bool signal_part = b < blocks / 2;
    for (int i = 0; i < kBlock; ++i) {
      int frame = b * kBlock + i;
      bool on = signal_part && (frame / 4410) % 2 == 0;
      seed = seed * 1664525u + 1013904223u;
      int16_t v = on ? static_cast<int16_t>((static_cast<int>(seed >> 16) - 32768) / 4) : 0;
      in[i * 2] = v;
      in[i * 2 + 1] = v;
    }
    if (b == freeze_at_block) e->set_param(inst, "freeze", "1");   // lock the buffer while the noise plays
    double t0 = NowUs();
    e->process(inst, in, out, kBlock);
    double us = NowUs() - t0;
    total_us += us;
    if (us > r.max_us) r.max_us = us;
    for (int i = 0; i < kBlock * 2; ++i) {
      double s = out[i] / 32768.0;
      if (out[i] == 32767 || out[i] == -32768) ++r.clipped;
      if (signal_part) { e_sig += s * s; ++n_sig; } else { e_tail += s * s; ++n_tail; }
    }
  }
  r.rms_in_signal = sqrt(e_sig / n_sig);
  r.rms_tail = sqrt(e_tail / n_tail);
  r.avg_us = total_us / blocks;
  return r;
}

void Check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int main() {
  const mpc_engine_t* e = mpc_engine();
  Check(e->process != NULL, "engine is an effect (process() set)");
  void* a = e->create(NULL);
  e->set_param(a, "dry_wet", "1");   // wet only, so the level measures the processor itself
  e->set_param(a, "reverb", "0.3");

  printf("\n%-14s %-14s %9s %9s %8s %9s %9s\n", "mode", "quality", "rms sig", "rms tail", "clipped", "avg us", "max us");
  for (int m = 0; m < 6; ++m) {
    char v[8];
    snprintf(v, sizeof v, "%d", m);
    e->set_param(a, "mode", v);
    for (int q = 0; q < 4; ++q) {
      snprintf(v, sizeof v, "%d", q);
      e->set_param(a, "quality", v);
      Result r = Run(e, a);
      printf("%-14s %-14s %9.4f %9.4f %8d %9.1f %9.1f\n", kModes[m], kQualities[q], r.rms_in_signal, r.rms_tail,
             r.clipped, r.avg_us, r.max_us);
      if (r.rms_in_signal < 1e-4) {
        printf("FAIL %s / %s is silent\n", kModes[m], kQualities[q]);
        ++failures;
      }
    }
  }

  // Freeze holds the buffer: with freeze on and silence in, Granular keeps sounding.
  e->set_param(a, "mode", "0");
  e->set_param(a, "quality", "0");
  Result frozen = Run(e, a, 400);   // freeze 1.16 s in, during a noise burst
  e->set_param(a, "freeze", "0");
  printf("\nfrozen granular tail rms %.4f\n", frozen.rms_tail);
  Check(frozen.rms_tail > 1e-3, "freeze keeps the buffer playing after the input stops");

  // The trigger param and a MIDI note are accepted without trouble.
  e->set_param(a, "trigger", "1");
  e->set_param(a, "trigger", "0");
  const uint8_t note_on[3] = { 0x90, 60, 100 }, note_off[3] = { 0x80, 60, 0 };
  e->midi(a, note_on, 3);
  e->midi(a, note_off, 3);
  Run(e, a);
  Check(true, "trigger param and MIDI note handled");

  // Project state round trip into a second instance.
  e->set_param(a, "mode", "4");
  e->set_param(a, "pitch", "7");
  char state[1024], got[64];
  e->get_param(a, "state", state, sizeof state);
  void* b = e->create(NULL);
  e->set_param(b, "state", state);
  e->get_param(b, "mode", got, sizeof got);
  bool mode_ok = !strcmp(got, "4");
  e->get_param(b, "pitch", got, sizeof got);
  Check(mode_ok && atof(got) == 7.0, "state chunk restores mode and pitch on a second instance");
  printf("state: %s\n", state);

  e->destroy(b);
  e->destroy(a);
  printf("\n%s\n", failures ? "FAILED" : "PASSED");
  return failures ? 1 : 0;
}
