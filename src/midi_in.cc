#include "midi_in.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

namespace midi_in {

namespace {

// The parts of the ALSA sequencer ABI we use (alsa/seq_event.h, alsa/seq.h).
typedef struct _snd_seq snd_seq_t;
struct SeqEvent {
  unsigned char type, flags, tag, queue;
  unsigned int time[2];
  unsigned char source_client, source_port, dest_client, dest_port;
  union {
    struct { unsigned char channel, note, velocity, off_velocity; unsigned int duration; } note;
    unsigned char raw[12];
  } data;
};
const int kSeqOpenInput = 2;
const int kSeqNonblock = 1;
const unsigned kCapWrite = 1 << 1, kCapSubsWrite = 1 << 6;   // SND_SEQ_PORT_CAP_WRITE, _SUBS_WRITE
const unsigned kTypeMidiGeneric = 1 << 1, kTypeApplication = 1 << 20;
const int kEventNoteOn = 6, kEventNoteOff = 7;

struct Api {
  bool loaded;
  int (*open)(snd_seq_t**, const char*, int, int);
  int (*close)(snd_seq_t*);
  int (*set_client_name)(snd_seq_t*, const char*);
  int (*create_simple_port)(snd_seq_t*, const char*, unsigned, unsigned);
  int (*event_input)(snd_seq_t*, SeqEvent**);
};

Api api;
int next_number = 1;

bool LoadApi() {
  if (api.loaded) return true;
  void* lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return false;
  api.open = reinterpret_cast<int (*)(snd_seq_t**, const char*, int, int)>(dlsym(lib, "snd_seq_open"));
  api.close = reinterpret_cast<int (*)(snd_seq_t*)>(dlsym(lib, "snd_seq_close"));
  api.set_client_name = reinterpret_cast<int (*)(snd_seq_t*, const char*)>(dlsym(lib, "snd_seq_set_client_name"));
  api.create_simple_port =
      reinterpret_cast<int (*)(snd_seq_t*, const char*, unsigned, unsigned)>(dlsym(lib, "snd_seq_create_simple_port"));
  api.event_input = reinterpret_cast<int (*)(snd_seq_t*, SeqEvent**)>(dlsym(lib, "snd_seq_event_input"));
  api.loaded = api.open && api.close && api.set_client_name && api.create_simple_port && api.event_input;
  return api.loaded;
}

}  // namespace

struct Port {
  snd_seq_t* seq;
};

Port* Open() {
  if (!LoadApi()) return NULL;
  snd_seq_t* seq = NULL;
  if (api.open(&seq, "default", kSeqOpenInput, kSeqNonblock) < 0) return NULL;
  char name[32];
  snprintf(name, sizeof name, "Clouds %d", next_number++);
  api.set_client_name(seq, name);
  if (api.create_simple_port(seq, "MIDI In", kCapWrite | kCapSubsWrite, kTypeMidiGeneric | kTypeApplication) < 0) {
    api.close(seq);
    return NULL;
  }
  Port* port = static_cast<Port*>(calloc(1, sizeof(Port)));
  if (!port) {
    api.close(seq);
    return NULL;
  }
  port->seq = seq;
  return port;
}

void Close(Port* port) {
  if (!port) return;
  api.close(port->seq);
  free(port);
}

void Poll(Port* port, void (*handler)(void* ctx, const uint8_t* msg, int len), void* ctx) {
  if (!port) return;
  SeqEvent* ev = NULL;
  for (int i = 0; i < 64 && api.event_input(port->seq, &ev) >= 0 && ev; ++i) {
    if (ev->type != kEventNoteOn && ev->type != kEventNoteOff) continue;
    uint8_t msg[3] = {
      static_cast<uint8_t>((ev->type == kEventNoteOn ? 0x90 : 0x80) | (ev->data.note.channel & 0x0f)),
      static_cast<uint8_t>(ev->data.note.note & 0x7f),
      static_cast<uint8_t>(ev->type == kEventNoteOn ? ev->data.note.velocity & 0x7f : 0),
    };
    handler(ctx, msg, 3);
  }
}

}  // namespace midi_in
