# Parasites Clouds (vendored)

Unmodified subset of Matthias Puech's *Parasites* alternative firmware for Mutable Instruments
Clouds, itself based on Emilie Gillet's Clouds code. MIT license (see `stmlib/LICENSE` and the
notice in each file's header).

- `clouds/` — from https://github.com/mqtthiqs/parasites @ 32fa66f (`clouds/dsp/`, `resources.*`, `drivers/debug_pin.h` for its TEST stubs)
- `stmlib/` — Parasites' stmlib fork https://github.com/mqtthiqs/stmlib @ 8ab2aae (only the
  headers/sources the Clouds DSP uses)

The hardware parts (drivers, UI, CV scaler, settings storage) are not included; `src/engine.cc`
replaces the firmware main loop.
