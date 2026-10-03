# Overcast (Clouds / Parasites) — runbook

Everything needed to build, deploy and continue the Clouds insert effect lives in this folder. Read this first;
`README.md` says what the plugin is and how users install it. This repo was split out of the developer's
`mpc-x-hacks` repo (history kept); the CV-jack plugin Marbles CV and the CV protocol notes stay there.

## What this is

**2026-10-03 redesign:** the mode is chosen in the plugin again (selector on the CLOUDS tab), and a second tab MOD
holds two envelopes, two LFOs and an 8-slot matrix onto the module's CV inputs (`src/mod.{h,cc}`, params from
`skin/gen_params.py`, LFO sync via `HAS_HOST_TRANSPORT`). The tab-as-mode mechanism described below was removed;
its findings (Q-Link re-read on tab switch) stay valid. The background is the user's `skin/overcast_bg.jpg`
(controls live in image y 110..576, between its bands).

The plugin is called **Overcast** (renamed from "Clouds" on 2026-10-03: MPC folder `Padbangers - VST - Overcast`,
`overcast.so`, MIDI port "Overcast N", same uid `PbCl`). The repo folder is still `clouds/`.
MPC rewrites `MPC.settings` with multi-line `<PLUGIN>` elements: remove an entry by its whole element, not by
line (removing the old Clouds entry by line left a file-less element with the same uid behind).


Mutable Instruments Clouds with Matthias Puech's Parasites firmware (MIT, vendored in `third_party/parasites/`) as a
native MPC OS insert effect, built with the sd88me/mpc-vst-plugins framework (submodule). Six modes: Granular,
Stretch, Looping Delay, Spectral, Oliverb, Resonestor — one MPC tab each.

Status (2026-09-30), confirmed by the user on the device: sound in all modes, the tabs switch the mode, per-mode
names, Q-Links with option zones, In/Out Gain + limiter, MIDI in via the "Overcast N" port (connected, notes arriving
not yet confirmed by ear). Not done: CPU bench, per-tab independent settings (discussed, not decided).

## Folder layout

| Path | What |
|---|---|
| `src/engine.cc` | Replaces the firmware main loop: params → GranularProcessor, page markers, gain, limiter |
| `src/midi_in.{h,cc}` | Our own ALSA seq input port per instance (MPC sends no MIDI to insert effects) |
| `params.json` | Parameter list = VST order. Nothing released yet; once shared, only append |
| `skin/gen_layout.py` | Writes `layout.conf` (don't edit that by hand): tabs, knobs, name tags, Q-Links |
| `skin/post_build.py` | Run by `build.sh` on the built skin: strips MPC's own name labels (see "Skin") |
| `skin/*.png`, `skin/clouds.css` | Knob images, name tags, blank marker image, colours |
| `skin/background_template.png` | 1280×628 render of the current page, for the user designing a background |
| `patches/` | Framework wrapper patches: host transport (unused here) and option Q-Link zones |
| `test/fx_test.cc` | Native effect test. Its last run found an out-of-bounds read (fixed by kMaxKnob); not re-run since |

## Mac prerequisites (the developer's machine, set up 2026-09-29)

Homebrew: `docker colima docker-buildx bash`. Docker runs in Colima, not Docker Desktop.

- Start: `colima start` — **outside Claude's Bash sandbox**, otherwise the VM's network helper can't connect and
  every pull times out.
- The user's **VPN blocks the VM's internet**. Pulling images needs the VPN disconnected; building with images
  already present works with it on.
- `~/.colima/default/colima.yaml` mounts `/Users/cypher`, `/Volumes/Daten/Development`, `/private/tmp/claude-501`
  (write `/Users/cypher`, not `~`).
- ARM32 emulation is lost on every VM restart; `build.sh` re-registers it (`tonistiigi/binfmt --install arm`).
- macOS `/bin/bash` 3.2 breaks the framework scripts; `build.sh` uses `/opt/homebrew/bin/bash`.
- The repo lives on an exFAT volume where Docker's file sharing fails, so `build.sh` mirrors it to
  `~/.cache/overcast-build` and copies the results back to `build/`.

## Device facts (the developer's MPC X, `ssh mpcx`)

- A button-remap shim loads via `/etc/ld.so.preload`; check it is still loaded after restarts:
  `grep -c shim_remap6 /proc/$(pidof MPC)/maps` (≈7). Never put it into an `LD_PRELOAD` as well.
- App log: `journalctl -u acvs` (the engine logs `Overcast: mode a -> b`). Crashes show as
  `code=dumped, status=11/SEGV`.
- BusyBox userland, no python, no curl. Settings: `/media/az01-internal/Settings/MPC/MPC.settings`.
- 44.1 kHz, 128-frame blocks. Screenshots: `ssh mpcx "/data/hacks/drmshot /tmp/shot.png 270"`.

## Build and deploy

```sh
colima start                  # outside the Bash sandbox; the user's VPN must be off for image pulls
./build.sh                    # -> build/overcast.so + skin (runs skin/post_build.py)
./deploy.sh                   # copy to the MPC; --yes also registers it (restarts the app)
python3 skin/gen_layout.py    # after changing the layout generator, then build again
```

Skin preview (Pillow isn't installed on the Mac, so in the renderer's image):

```sh
S=~/.cache/overcast-build/port
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$S":/w -w /w mpc-vst-html-art python3 \
  third_party/mpc-vst-plugins/tools/studio.py preview "build/skin/Padbangers - VST - Overcast/Plugin Skins" -o /w/build/preview_%d.png
```

**Always check that MPC runs the new build.** MPC keeps a plugin's `.so` loaded while any instance exists (undo
history included), so "remove and insert again" often keeps the old code. `deploy.sh` compares the inode MPC has
mapped with the new file and says when the app must be restarted (ask the user to save first). Several rounds of
"fixes" went nowhere on 2026-09-30 because the device still ran an old build.

## How it works (what was learned on the device)

- **Effects get no MIDI.** MPC OS sends no MIDI to insert effects (verified: no event reached `Midi()`). Each
  instance opens an ALSA sequencer client "Overcast N" with a writable port "MIDI In"; a MIDI track picks it as its
  output. The caps must be `WRITE | SUBS_WRITE` = bits 1 and **6** — with bit 5 (SUBS_READ) MPC listed it only as
  an input. libasound is `dlopen`ed (already in the MPC process), so the build needs no ALSA headers.
- **Modes as MPC tabs.** A tab switch changes no parameter; MPC only reads the new page's **Q-Link parameters**
  (Q-Links follow the page). Readouts and ordinary controls are not re-read on a switch. So each tab carries a
  marker param `page_<m>` in its Q-Link set (in MIDI Pitch's slot, shown as "Mode: <name>") plus an invisible knob
  for it. Reading a marker switches the mode — unless several different markers are read within 0.2 s: MPC reads
  every param when the plugin is inserted, and that burst is ignored. Consequence: after inserting/loading, the
  saved mode stays active while MPC shows the first tab. The engine logs `Clouds: page N shown` / `mode a -> b`
  to the MPC journal.
- **Zeroed memory.** Clouds' objects are firmware globals; `GranularProcessor::Init()` never sets `silence_`, so an
  instance in uninitialised memory can stay silent forever. Instances are `calloc`ed.
- **Knob range.** The module's pots never reach 1.0 (65535/65536); the dry/wet crossfade reads one entry past its
  table at exactly 1.0. Continuous params are clamped to `kMaxKnob`.
- **Sample rate.** Runs at the MPC's 44.1 kHz instead of 32 kHz: pitch is right, buffers and time constants shrink
  by 32/44.1.
- **Blocks.** Each 128-frame MPC block runs as four 32-frame blocks, each followed by `Prepare()` (the module's
  main-loop work: FFT frames in Spectral, WSOLA correlation).
- **Gain.** In Gain (dB, soft knee before the processor), Out Gain (default +6 dB), then a stereo-linked peak
  limiter at −1 dBFS (~150 ms release) and the soft knee.
- **MIDI mapping** (as the module's TRIG and V/OCT): note-on = trigger, held notes = gate, note − Root Note added
  to Pitch in semitones and held after note-off (like a CV). Melodic playing: Freeze on, Density at 12 o'clock.

## Skin

- One tab per mode, generated by `skin/gen_layout.py`: 3×3 knobs (Position Size Texture / Density Pitch Spread /
  Blend Feedback Reverb), a gain column (In/Out Gain), right column Freeze, Reverse, Trigger, Quality; row 4 MIDI
  Pitch and Root Note. Names per mode come from the Clouds "Secrets" page and the Parasites manual (table in
  `gen_layout.py` and `kNames` in `engine.cc`, the latter for MPC's Q-Link display via `dynamic_name`).
- The look follows the module (colours sampled from the panel artwork in the Parasites repo,
  `clouds/hardware_design/panel/clouds.ai`, which is a PDF): raspberry `#c83d58`, teal `#009797`, ink `#1a1919`;
  knobs drawn in `skin/` as black ribbed bodies with coloured caps. POSITION/DENSITY/IN GAIN raspberry,
  SIZE/TEXTURE teal (white text on tags); white knobs get plain dark names, no tag. The user asked for no
  ornaments, only the panel colour.
- MPC draws each control's name itself, in one global colour, and would show it on top of the baked per-tab names.
  `post_build.py` removes those "Name" labels from knob/toggle components and strips the marker knob down to its
  (blank) image.
- Q-Links (MPC X 4×4, listed column by column): Position Density Blend Trigger | Size Pitch Feedback Mode-marker |
  Texture Spread Reverb Root | Freeze Reverse Quality OutGain. Option params use Q-Link zones (patch 0002).
- A user-designed background: 1280×628 px PNG/JPG as `skin/background.png`, to be added as the first `art` line of
  each tab (`art file=skin/background.png fit=cover`). Not added yet.

## Next steps / open

- Per-tab independent settings (each mode remembering its own knobs) — the user asked why Granular settings
  seemed to affect other modes; that turned out to be the stale build. Shared knobs are how the module works.
- CPU bench (`third_party/mpc-vst-plugins/tools/bench.sh`), especially Spectral.
- Confirm by ear that MIDI notes transpose/trigger.
- 32 kHz resampling if the shorter buffer matters.
