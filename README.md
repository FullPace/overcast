# Clouds (Parasites) for MPC

Mutable Instruments Clouds with the Parasites firmware as a native MPC OS insert effect: Granular, Stretch,
Looping Delay, Spectral, Oliverb and Resonestor, one MPC tab each, in the look of the module's panel.

- **Tabs = modes.** Tapping a tab at the bottom switches the mode; the knob names follow it.
- **Q-Links** (4×4): Position Density Blend Trigger | Size Pitch Feedback Mode | Texture Spread Reverb Root Note |
  Freeze Reverse Quality Out Gain.
- **In Gain / Out Gain** with an output limiter.
- **Play it melodically** like the module's V/OCT + TRIG: put Clouds on an audio track, make a MIDI track whose
  output is **Clouds 1** (one port per Clouds instance), turn MIDI Pitch on, Freeze on, Density at 12 o'clock,
  and play notes (Pad Perform in a scale mode, or a keyboard). Notes transpose relative to Root Note (C3).

After updating the plugin file, restart the MPC app: re-inserting the plugin doesn't always load the new build.
Build, deploy and design notes: [`CLAUDE.md`](CLAUDE.md).

## Credits and licenses

- Clouds DSP by Emilie Gillet, Parasites firmware by Matthias Puech — MIT, vendored in `third_party/parasites/`.
- Plugin framework: [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (submodule).
- Colours sampled from the Clouds panel artwork; knobs and tags drawn for this plugin.
- Not affiliated with Mutable Instruments or Akai Professional.
