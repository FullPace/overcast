#!/usr/bin/env python3
"""Writes ../layout.conf: two MPC tabs in the look of the Clouds front panel.

CLOUDS: the mode selector at the top, a 3x3 knob grid, a gain column, the right column (Freeze, Reverse,
Trigger, Quality) and MIDI. Every name is baked per mode (when=mode:<m>), so MPC swaps them with the mode;
skin/post_build.py removes MPC's own name labels so nothing shows twice.
MOD: two envelopes and two LFOs on the left, the 8-slot modulation matrix on the right.

    python3 skin/gen_layout.py
"""
import json
import os

MODES = ["GRANULAR", "STRETCH", "LOOPING DELAY", "SPECTRAL", "OLIVERB", "RESONESTOR"]

# Names per mode (Granular, Stretch, Looping Delay, Spectral, Oliverb, Resonestor), from the Clouds and Parasites
# manuals. Same table as kNames in src/engine.cc, which feeds MPC's Q-Link display.
NAMES = {
    "position": ["POSITION", "SCRUB", "DELAY", "BUFFER", "PREDELAY", "BURST"],
    "size":     ["SIZE", "WINDOW", "LOOP SIZE", "WARP", "ROOM SIZE", "CHORD"],
    "texture":  ["TEXTURE", "FILTER", "FILTER", "QUANTIZE", "DAMPING", "DAMPING"],
    "density":  ["DENSITY", "DIFFUSION", "DIFFUSION", "REFRESH", "DECAY", "DECAY"],
    "pitch":    ["PITCH", "PITCH", "PITCH", "PITCH", "SHIMMER", "PITCH"],
    "spread":   ["SPREAD", "SPREAD", "SPREAD", "SPREAD", "DIFFUSION", "VOICES L/R"],
    "dry_wet":  ["BLEND", "BLEND", "BLEND", "BLEND", "DRY/WET", "DISTORTION"],
    "feedback": ["FEEDBACK", "FEEDBACK", "FEEDBACK", "FEEDBACK", "MOD SPEED", "HARMONICS"],
    "reverb":   ["REVERB", "REVERB", "REVERB", "REVERB", "MOD AMOUNT", "SCATTER"],
    "freeze":   ["FREEZE", "FREEZE", "FREEZE", "FREEZE", "INFINITE", "VOICE LOCK"],
    "trigger":  ["TRIGGER", "LOOP SYNC", "TAP", "GLITCH", "CLOCK", "STRIKE"],
}

# As on the module: POSITION / DENSITY / IN GAIN raspberry, SIZE / TEXTURE teal; white knobs get plain dark names.
COLOUR = {"position": "raspberry", "density": "raspberry", "size": "teal", "texture": "teal", "in_gain": "raspberry"}

GRID = [["position", "size", "texture"],
        ["density", "pitch", "spread"],
        ["dry_wet", "feedback", "reverb"]]
# The background (skin/overcast_bg.jpg, the user's design) has a title band at the top and a border band at the
# bottom; controls stay in between: layout y 196..662 (image y 110..576).
BACKGROUND = "art file=skin/overcast_bg.jpg"
# One clean grid: five evenly spaced columns x three rows, every knob the same size.
GRID_X = [128, 384, 640, 896, 1152]
COLS = GRID_X[:3]
ROWS = [292, 422, 552]
R = 38
GAIN_X = GRID_X[3]    # 4th column, as on the Q-Links: In Gain, Out Gain, Freeze
RIGHT = GRID_X[4]     # the 4th Q-Link row's other controls: Quality, MIDI Pitch, Reverse
TAG_W, TAG_H = 132, 26
TOGGLE_H = 26
NAME_GAP = 14         # knob edge (r) to the centre of its name
MOD_KNOB_R = 24       # all knobs on the MODULATION tab (envelopes and LFO rates)
SECTION_X, SECTION_W, SECTION_H = 55, 90, 34   # ENV / LFO tags: taller, for bigger lettering

# Q-Links: listed column by column, top to bottom (shadow_skin maps the list onto MPC's 4x4 grid that way).
# Rows as the user asked: Position Size Texture In Gain / Density Pitch Spread Out Gain /
# Blend Feedback Reverb Freeze / Mode Quality MIDI Pitch Reverse.
QLINKS_MAIN = ["position", "density", "dry_wet", "mode",
               "size", "pitch", "feedback", "quality",
               "texture", "spread", "reverb", "midi_pitch",
               "in_gain", "out_gain", "freeze", "reverse"]
QLINKS_MOD = ["env1_attack", "env1_decay", "env1_sustain", "env1_release",
              "env2_attack", "env2_decay", "env2_sustain", "env2_release",
              "lfo1_rate", "lfo1_shape", "lfo2_rate", "lfo2_shape",
              "mod1_amt", "mod2_amt", "mod3_amt", "mod4_amt"]

THEME = [
    "art_css=skin/clouds.css",
    # ink = popup list entries (and MPC's own name labels, removed by post_build.py); ink_dim = the value text
    "theme_bg=e6e6e3", "theme_panel=e6e6e3", "theme_line=b4b5b4", "theme_ink=1a1919", "theme_ink_dim=4a4b4a",
    "theme_ink_faint=b4b5b4", "theme_accent=c83d58", "theme_accent_hi=c83d58", "theme_knob_face=e9e9e6",
    "theme_knob_ring=d7d8d6", "theme_knob_dot=1a1919", "theme_lcd=ffffff", "theme_seg_active=009797",
    "theme_seg_inactive=f4f4f2", "theme_seg_active_tx=ffffff", "theme_box=ffffff", "theme_btn_bg=c83d58",
    "theme_btn_text=ffffff", "theme_btn_text_plain=ffffff",
]


# MANUAL tab: a topic list on the left, the topic's text on the right (when=manual_page:<i>, like the mode names).
MANUAL = [
    ("BASICS", [
        "Overcast records the track's audio into a buffer and plays it back as clouds of grains, stretched,",
        "looped, frozen as a spectrum, as a reverb or through resonators: Mutable Instruments Clouds with the",
        "Parasites firmware. Pick the mode at the top of the OVERCAST tab; the knob names follow the mode.",
        "",
        "FREEZE (snowflake): stops recording. The cloud keeps playing what is in the buffer; with BLEND below",
        "100 % you still hear the dry input next to it.",
        "QUALITY: lower quality = longer buffer, grittier sound. 16-bit stereo about 0.75 s, 16-bit mono 1.35 s,",
        "8-bit stereo 3 s, 8-bit mono 5.4 s. Changing quality or mode clears the buffer.",
        "REVERSE: grains (Granular) and the delay (Looping Delay) play backwards.",
        "IN GAIN / OUT GAIN: level into the module and after it; the output has a limiter.",
        "Q-Links follow the tab on screen: four rows of four, as on the OVERCAST tab.",
    ]),
    ("MIDI", [
        "MPC OS sends no MIDI to insert effects, so every Overcast opens its own MIDI input:",
        "Overcast 1, Overcast 2, ... (the number counts the instances in the order they were inserted).",
        "",
        "To play it: make a MIDI track, set its MIDI output to Overcast 1 (any channel) and record notes.",
        "Every note is a trigger, like the module's TRIG input: a grain in Granular, a strike in Resonestor,",
        "a tap in Looping Delay. Envelopes with their trigger set to MIDI follow the notes too.",
        "",
        "MIDI PITCH on: notes also transpose, relative to C3, like the module's V/OCT input.",
        "MIDI PITCH off: notes only trigger, and the PITCH knob alone sets the pitch.",
        "",
        "Classic setting: Granular, FREEZE on, DENSITY at 12 o'clock: every note plays a grain at its pitch.",
    ]),
    ("MODULATION", [
        "Two envelopes, two LFOs and an 8-slot matrix, like patch cables into the module's CV inputs.",
        "",
        "ENV 1 / ENV 2: attack, decay, sustain, release. TRIGGER: MIDI notes, or LFO 1 / LFO 2 (each cycle).",
        "LFO 1 / LFO 2: seven shapes. SYNC off: the knob sets the rate in Hz. SYNC on: it locks to the MPC",
        "tempo and picks a note value from 4 bars to 1/32.",
        "",
        "MATRIX: source -> destination -> amount (-100 to +100 %). Sources: LFOs, envelopes, note velocity.",
        "Several slots on one destination add up. FREEZE and TRIGGER are gates: on above 50 %.",
        "An LFO square wave on TRIGGER clocks grains, or strikes the Resonestor, in time with the MPC.",
        "",
        "Q-Links on this tab: ENV 1, ENV 2, the LFO rates and shapes, the amounts of slots 1 to 4.",
    ]),
    ("GRANULAR", [
        "The classic Clouds sound: short grains taken from the buffer, layered into a cloud.",
        "",
        "POSITION: where in the buffer the grains start (right = further back in time).",
        "SIZE: grain length, from tiny clicks to long smears.",
        "PITCH: transposition of the grains, in semitones.",
        "DENSITY: 12 o'clock = no grains. Counter-clockwise: grains at a steady rate,",
        "clockwise: at random times; further out = more grains.",
        "TEXTURE: grain envelope from hard square to soft bell; past 3 o'clock it adds diffusion.",
        "BLEND, SPREAD, FEEDBACK, REVERB: dry/wet mix, stereo spread, feedback into the buffer, reverb.",
        "TRIGGER / MIDI note: plays one grain. FREEZE holds the buffer.",
    ]),
    ("STRETCH", [
        "Time stretching and pitch shifting (WSOLA): the buffer plays smoothly, without the grain texture.",
        "",
        "SCRUB: the playback position in the buffer; move it to scrub through the recording.",
        "WINDOW: the size of the pieces that are spliced together (small = buzzy, large = smooth).",
        "PITCH: transposition, independent of speed.",
        "DIFFUSION: smears the sound into a wash.",
        "FILTER: low-pass counter-clockwise, high-pass clockwise.",
        "LOOP SYNC (trigger / MIDI note): restarts playback in sync.",
        "With FREEZE on, SCRUB moves through the frozen audio: a slow-motion player.",
    ]),
    ("LOOPING DELAY", [
        "A delay with a pitch shifter; with FREEZE on it becomes a looper.",
        "",
        "DELAY: the delay time.",
        "LOOP SIZE: the pitch shifter's window, and the loop length while frozen.",
        "PITCH: transposes the echoes (near 0 the pitch shifter is bypassed).",
        "DIFFUSION: smears the echoes.",
        "FILTER: low-pass counter-clockwise, high-pass clockwise.",
        "FEEDBACK: more repeats.",
        "TAP (trigger / MIDI notes): taps the delay time, so echoes follow your notes' rhythm.",
        "REVERSE plays the delay backwards.",
    ]),
    ("SPECTRAL", [
        "A phase vocoder: the sound is turned into its spectrum and resynthesised.",
        "",
        "BUFFER: which part of the buffer is analysed.",
        "WARP: shifts and bends the spectrum (formant-like changes); 12 o'clock = unchanged.",
        "PITCH: transposition.",
        "REFRESH: how often the spectrum is updated; low = slow, held chords; far out adds phase randomness.",
        "QUANTIZE: reduces the spectrum to fewer, louder partials: from smooth to robotic.",
        "GLITCH (trigger / MIDI note): a spectral glitch.",
        "FREEZE holds the current spectrum: an endless drone.",
    ]),
    ("OLIVERB", [
        "Parasites' big modulated reverb, from Clouds' reverb.",
        "",
        "PREDELAY: time before the reverb starts (synced to taps on CLOCK).",
        "ROOM SIZE: the size of the space.",
        "DAMPING: low-pass counter-clockwise, high-pass clockwise.",
        "DECAY: reverb time.",
        "SHIMMER: pitches the reverb tail up or down (e.g. +12 for the classic shimmer).",
        "DIFFUSION: how dense the reflections are. DRY/WET: the mix.",
        "MOD SPEED / MOD AMOUNT: modulation of the reverb's delay lines (chorus, wobble).",
        "INFINITE (freeze): the tail never decays. CLOCK (trigger / MIDI notes): tap tempo for PREDELAY.",
    ]),
    ("RESONESTOR", [
        "Parasites' resonator: two voices of tuned combs, excited by bursts or by the input.",
        "Play it with MIDI notes (or an LFO / envelope on TRIGGER); each strike switches voice.",
        "",
        "BURST: the excitation, from a short damped click to a longer noisy burst.",
        "CHORD: the interval/chord the combs are tuned to.",
        "PITCH: the root pitch (MIDI PITCH on: follows the notes).",
        "DECAY: how long the voices ring. DAMPING: dark to bright and narrow.",
        "VOICES L/R: the two voices apart in stereo / separated. DISTORTION: drive.",
        "HARMONICS: harmonic or inharmonic tuning. SCATTER: spreads the comb tunings.",
        "VOICE LOCK (freeze): keeps playing the current voice. STRIKE: the trigger.",
    ]),
]


def tag(cx, cy, colour, w=TAG_W, h=TAG_H):
    """Pill images keep their aspect ratio on screen, so other sizes need their own image (pill_<colour>_section)."""
    return "art file=skin/pill_%s.png x=%d y=%d w=%d h=%d" % (colour, cx - w // 2, cy - h // 2, w, h)


def text(cx, cy, label, colour, size=1.5, when=None, weight=700, align=None):
    line = 'text cx=%d cy=%d label="%s" size=%s weight=%d color=%s' % (cx, cy, label, size, weight, colour)
    line += " align=%s" % align if align else ""
    if when is None:
        return line
    return line + (" when=mode:%d" % when if isinstance(when, int) else " when=" + when)


def name(key, cx, cy, colour, label=None):
    """A control's name: white on a coloured tag, or plain dark lettering. Per mode when it changes with it."""
    out = [tag(cx, cy, colour)] if colour else []
    ink = "ffffff" if colour else "1a1919"
    names = NAMES.get(key) or [label] * len(MODES)
    if len(set(names)) == 1:
        return out + [text(cx, cy, names[0], ink)]
    return out + [text(cx, cy, n, ink, when=m) for m, n in enumerate(names)]


def knob(key, cx, cy, r, label=None, colour="auto"):
    colour = COLOUR.get(key) if colour == "auto" else colour
    img = colour if colour in ("raspberry", "teal") else "white"
    return (name(key, cx, cy + r + NAME_GAP, colour, label) +
            ['knob cx=%d cy=%d r=%d label="" key=%s img=skin/knob_%s.png' % (cx, cy, r, key, img)])


def toggle(key, cx, cy, colour="grey", label=None):
    return name(key, cx, cy + TOGGLE_H // 2 + 14, colour, label) + ['toggle cx=%d cy=%d label="" key=%s' % (cx, cy, key)]


def main_page():
    L = ["[tab OVERCAST]", BACKGROUND,
         'enum_h cx=640 cy=216 label="" key=mode sw=204 options="%s"' % ",".join(MODES)]
    for r, row in enumerate(GRID):
        for c, key in enumerate(row):
            L += knob(key, COLS[c], ROWS[r], R)
    L += knob("in_gain", GAIN_X, ROWS[0], R, "IN GAIN")
    L += knob("out_gain", GAIN_X, ROWS[1], R, "OUT GAIN")
    # Freeze: a big snowflake (grey off, teal on) instead of a toggle, its name level with the knob names
    L.append('toggle cx=%d cy=%d label="" key=freeze img=skin/freeze_off.png img_on=skin/freeze_on.png w=64 h=64'
             % (GAIN_X, ROWS[2]))
    L.append(text(GAIN_X, ROWS[2] + R + NAME_GAP, "FREEZE", "1a1919"))
    L += [text(RIGHT, ROWS[0] - 30, "QUALITY", "1a1919", size=1.2),
          'popup cx=%d cy=%d w=190 h=40 label="" key=quality' % (RIGHT, ROWS[0])]
    L += toggle("midi_pitch", RIGHT, ROWS[1] - 10, colour=None, label="MIDI PITCH")
    L += toggle("reverse", RIGHT, ROWS[2] - 10, colour=None, label="REVERSE")
    L.append('qlinks "OVERCAST" = ' + ",".join(QLINKS_MAIN))
    return L


def mod_page():
    L = ["[tab MODULATION]", BACKGROUND]
    # left: two envelopes and two LFOs, one row each, the section name in front of the row
    rows = [238, 359]       # env 1's trigger list level with the matrix's first row; 121 apart, so the two rows'
                            # 116 px knob frames don't touch (touching frames confused MPC's arrow-key navigation)
    lfo_rows = [506, 611]   # 50 px lower than before: the LFO rows lost their headings
    ex = [170, 295, 420, 545]
    for e in (1, 2):
        cy = rows[e - 1]
        L += [tag(SECTION_X, cy, "raspberry_section", SECTION_W, SECTION_H), text(SECTION_X, cy, "ENV %d" % e, "ffffff", size=2)]
        for i, (p, lab) in enumerate((("attack", "ATTACK"), ("decay", "DECAY"), ("sustain", "SUSTAIN"),
                                      ("release", "RELEASE"))):
            L += knob("env%d_%s" % (e, p), ex[i], cy, MOD_KNOB_R, lab, colour=None)
        L += [text(690, cy - 30, "TRIGGER", "1a1919", size=1.2),
              'popup cx=690 cy=%d w=124 h=36 label="" key=env%d_trig' % (cy, e)]
    for l in (1, 2):
        # one compact row: shape list, sync toggle with plain text, rate knob whose value shows Hz or, synced, the
        # note value (the engine maps the knob onto 4 bars .. 1/32); no headings, no separate sync-rate list
        cy = lfo_rows[l - 1]
        L += [tag(SECTION_X, cy, "teal_section", SECTION_W, SECTION_H), text(SECTION_X, cy, "LFO %d" % l, "ffffff", size=2)]
        L.append('popup cx=205 cy=%d w=150 h=36 label="" key=lfo%d_shape' % (cy, l))
        L += toggle("lfo%d_sync" % l, 345, cy - 8, colour=None, label="SYNC")
        L.append('knob cx=465 cy=%d r=%d label="" key=lfo%d_rate img=skin/knob_white.png' % (cy - 10, MOD_KNOB_R, l))
    # right: the matrix
    x0 = 770
    L += [text(x0 + 100, 208, "SOURCE", "636463", size=1.2), text(x0 + 260, 208, "DESTINATION", "636463", size=1.2),
          text(x0 + 420, 208, "AMOUNT", "636463", size=1.2)]
    for k in range(1, 9):
        cy = 238 + (k - 1) * 54
        L += [text(x0 + 8, cy, str(k), "1a1919", size=1.4),
              'popup cx=%d cy=%d w=140 h=36 label="" key=mod%d_src' % (x0 + 100, cy, k),
              # 150 wide and 10 px left: its two-column list opens under it, clear of the screen edge
              'popup cx=%d cy=%d w=150 h=36 label="" key=mod%d_dst' % (x0 + 260, cy, k),
              # the slider bar (y 40-60 of its 100 px frame) starts level with the destination box's top edge
              'slider_h cx=%d cy=%d w=100 h=20 label="" key=mod%d_amt' % (x0 + 420, cy - 8, k)]
    L.append('qlinks "MODULATION" = ' + ",".join(QLINKS_MOD))
    return L


def manual_page():
    L = ["[tab MANUAL]", BACKGROUND,
         'enum_v cx=150 cy=430 label="" key=manual_page sw=220 options="%s"' % ",".join(t for t, _ in MANUAL)]
    for i, (title, lines) in enumerate(MANUAL):
        when = "manual_page:%d" % i
        L.append(text(320, 214, title, "c83d58", size=1.8, when=when, align="left"))
        for j, line in enumerate(lines):
            if line:
                L.append(text(320, 254 + j * 35, line.replace('"', "'"), "1a1919", size=1.45, when=when, weight=600,
                              align="left"))
    L.append('qlinks "MANUAL" = manual_page')
    return L


def qlink_bounds():
    """Per tab, the screen area of each Q-Link column (MPC X 4x4 grid; the MPC highlights it while a Q-Link turns).
    Skin coordinates are layout y - 86. Written to skin/qlink_bounds.json for post_build.py."""
    def rect(x0, y0, x1, y1):
        return "%d %d %d %d" % (x0, y0 - 86, x1 - x0, y1 - y0)
    top, bottom = ROWS[0] - 50, ROWS[2] + R + 60
    main = [rect(c - 70, top, c + 70, bottom) for c in COLS] + [rect(GAIN_X - 70, top, GAIN_X + 70, bottom)]
    mod = [rect(110, 208, 610, 326), rect(110, 327, 610, 445), rect(10, 466, 530, 660), rect(760, 220, 1260, 420)]
    manual = [rect(30, 196, 270, 664)] + [rect(0, 196, 1, 197)] * 3
    return [main, mod, manual]


def main():
    L = ["# Clouds (Parasites) skin. Generated by skin/gen_layout.py -- edit that, not this file."] + THEME + [""]
    L += main_page() + [""] + mod_page() + [""] + manual_page()
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "layout.conf")
    open(path, "w").write("\n".join(L) + "\n")
    print("wrote", os.path.normpath(path))
    side = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qlink_bounds.json")
    json.dump(qlink_bounds(), open(side, "w"), indent=1)


if __name__ == "__main__":
    main()
