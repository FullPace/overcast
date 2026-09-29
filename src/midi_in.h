// A MIDI input port of our own, because MPC OS doesn't send MIDI to insert effects.
//
// Each instance opens an ALSA sequencer client ("Clouds 1", "Clouds 2", ...) with one writable port.
// MPC detects it without a restart and offers it as a MIDI output, so a MIDI track can play the
// effect. libasound is loaded at runtime (it is already in the MPC process), so the build needs no
// ALSA headers; where it's missing (the Mac test build) the port simply isn't there.
#pragma once

#include <stdint.h>

namespace midi_in {

struct Port;

Port* Open();                  // NULL if ALSA isn't available
void Close(Port* port);

// Drains pending events without blocking and passes note on/off as raw 3-byte MIDI messages.
// Call from the audio thread once per block.
void Poll(Port* port, void (*handler)(void* ctx, const uint8_t* msg, int len), void* ctx);

}  // namespace midi_in
