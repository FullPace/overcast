# Overcast — Clouds (Parasites) for MPC

**Overcast** (by Padbangers) is Mutable Instruments Clouds with the Parasites firmware as a native MPC OS insert
effect: Granular, Stretch, Looping Delay, Spectral, Oliverb and Resonestor, in the look of the module's panel.

- **CLOUDS tab:** the mode selector at the top (the knob names follow the mode), the 3×3 knobs, In/Out Gain with an
  output limiter, Freeze, Reverse, Trigger, MIDI Pitch / Root Note and Quality.
- **MOD tab:** two envelopes (ADSR; triggered by MIDI notes, LFO 1, LFO 2 or the Trigger button), two LFOs (seven
  shapes, free or synced to the MPC tempo) and an 8-slot matrix: source → destination → amount (−100…+100 %).
  Destinations are the module's CV inputs: Position, Size, Pitch, Density, Texture, Blend, Spread, Feedback, Reverb,
  and Freeze / Trigger as gates (on above 50 %).
- **Q-Links** (4×4) on CLOUDS: Position Density Blend Trigger | Size Pitch Feedback MIDI Pitch | Texture Spread Reverb
  Root Note | Freeze Reverse Mode Quality. On MOD: Env 1 ADSR | Env 2 ADSR | LFO rates and shapes | Mod 1–4 amounts.
- **Play it melodically** like the module's V/OCT + TRIG: put Overcast on an audio track, make a MIDI track whose
  output is **Overcast 1** (one port per instance), turn MIDI Pitch on, Freeze on, Density at 12 o'clock, and play
  notes (Pad Perform in a scale mode, or a keyboard). Notes transpose relative to Root Note (C3).

After updating the plugin file, restart the MPC app: re-inserting the plugin doesn't always load the new build.
Build, deploy and design notes: [`CLAUDE.md`](CLAUDE.md).

## Credits and licenses

- Clouds DSP by Emilie Gillet, Parasites firmware by Matthias Puech — MIT, vendored in `third_party/parasites/`.
- Plugin framework: [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (submodule).
- Background (`skin/overcast_bg.jpg`) designed by the user; colours sampled from the Clouds panel artwork; knobs and
  tags drawn for this plugin.
- Not affiliated with Mutable Instruments or Akai Professional.
