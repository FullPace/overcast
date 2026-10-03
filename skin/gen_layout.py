#!/usr/bin/env python3
"""Writes ../layout.conf: two MPC tabs in the look of the Clouds front panel.

CLOUDS: the mode selector at the top, a 3x3 knob grid, a gain column, the right column (Freeze, Reverse,
Trigger, Quality) and MIDI. Every name is baked per mode (when=mode:<m>), so MPC swaps them with the mode;
skin/post_build.py removes MPC's own name labels so nothing shows twice.
MOD: two envelopes and two LFOs on the left, the 8-slot modulation matrix on the right.

    python3 skin/gen_layout.py
"""
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
COLS = [120, 330, 540]
ROWS = [292, 422, 552]
R = 38
GAIN_X = 720          # 4th column, as on the Q-Links: In Gain, Out Gain, Freeze
RIGHT = 950           # the 4th Q-Link row's other controls: Quality, MIDI Pitch, Reverse
TAG_W, TAG_H = 132, 26
TOGGLE_H = 26
NAME_GAP = 14         # knob edge (r) to the centre of its name

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
    # ink = MPC's own name labels (removed by post_build.py); ink_dim = the value text under the controls
    "theme_bg=e6e6e3", "theme_panel=e6e6e3", "theme_line=b4b5b4", "theme_ink=ffffff", "theme_ink_dim=4a4b4a",
    "theme_ink_faint=b4b5b4", "theme_accent=c83d58", "theme_accent_hi=c83d58", "theme_knob_face=e9e9e6",
    "theme_knob_ring=d7d8d6", "theme_knob_dot=1a1919", "theme_lcd=ffffff", "theme_seg_active=009797",
    "theme_seg_inactive=f4f4f2", "theme_seg_active_tx=ffffff", "theme_box=ffffff", "theme_btn_bg=c83d58",
    "theme_btn_text=ffffff", "theme_btn_text_plain=ffffff",
]


def tag(cx, cy, colour, w=TAG_W):
    return "art file=skin/pill_%s.png x=%d y=%d w=%d h=%d" % (colour, cx - w // 2, cy - TAG_H // 2, w, TAG_H)


def text(cx, cy, label, colour, size=1.5, when=None):
    line = 'text cx=%d cy=%d label="%s" size=%s weight=700 color=%s' % (cx, cy, label, size, colour)
    return line + (" when=mode:%d" % when if when is not None else "")


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
    L += knob("in_gain", GAIN_X, ROWS[0], 30, "IN GAIN")
    L += knob("out_gain", GAIN_X, ROWS[1], 30, "OUT GAIN")
    L += toggle("freeze", GAIN_X, ROWS[2] - 10, "raspberry")
    L += [tag(RIGHT, ROWS[0] - 34, "grey"), text(RIGHT, ROWS[0] - 34, "QUALITY", "ffffff"),
          'enum_v cx=%d cy=%d label="" key=quality options="16-BIT STEREO,16-BIT MONO,8-BIT STEREO,8-BIT MONO"'
          % (RIGHT, ROWS[0] + 56)]
    L += toggle("midi_pitch", RIGHT, ROWS[1] + 50, label="MIDI PITCH")
    L += toggle("reverse", RIGHT, ROWS[2] - 10, label="REVERSE")
    L.append('qlinks "OVERCAST" = ' + ",".join(QLINKS_MAIN))
    return L


def mod_page():
    L = ["[tab MODULATION]", BACKGROUND]
    # left: two envelopes and two LFOs, one row each, the section name in front of the row
    rows = [226, 341, 456, 571]
    ex = [170, 295, 420, 545]
    for e in (1, 2):
        cy = rows[e - 1]
        L += [tag(55, cy, "raspberry", 90), text(55, cy, "ENV %d" % e, "ffffff")]
        for i, (p, lab) in enumerate((("attack", "ATTACK"), ("decay", "DECAY"), ("sustain", "SUSTAIN"),
                                      ("release", "RELEASE"))):
            L += knob("env%d_%s" % (e, p), ex[i], cy, 24, lab, colour=None)
        L += [text(690, cy - 30, "TRIGGER", "1a1919", size=1.2),
              'popup cx=690 cy=%d w=124 h=36 label="" key=env%d_trig' % (cy, e)]
    for l in (1, 2):
        cy = rows[1 + l]
        L += [tag(55, cy, "teal", 90), text(55, cy, "LFO %d" % l, "ffffff")]
        L += [text(205, cy - 30, "SHAPE", "1a1919", size=1.2),
              'popup cx=205 cy=%d w=150 h=36 label="" key=lfo%d_shape' % (cy, l)]
        L += knob("lfo%d_rate" % l, 360, cy, 24, "RATE", colour=None)
        L += toggle("lfo%d_sync" % l, 480, cy - 8, label="SYNC")
        L += [text(620, cy - 30, "SYNC RATE", "1a1919", size=1.2),
              'popup cx=620 cy=%d w=124 h=36 label="" key=lfo%d_div' % (cy, l)]
    # right: the matrix
    x0 = 770
    L += [text(x0 + 100, 208, "SOURCE", "636463", size=1.2), text(x0 + 270, 208, "DESTINATION", "636463", size=1.2),
          text(x0 + 420, 208, "AMOUNT", "636463", size=1.2)]
    for k in range(1, 9):
        cy = 238 + (k - 1) * 54
        L += [text(x0 + 8, cy, str(k), "1a1919", size=1.4),
              'popup cx=%d cy=%d w=140 h=36 label="" key=mod%d_src' % (x0 + 100, cy, k),
              'popup cx=%d cy=%d w=170 h=36 label="" key=mod%d_dst' % (x0 + 270, cy, k),
              'slider_h cx=%d cy=%d w=100 h=20 label="" key=mod%d_amt' % (x0 + 420, cy - 14, k)]
    L.append('qlinks "MODULATION" = ' + ",".join(QLINKS_MOD))
    return L


def main():
    L = ["# Clouds (Parasites) skin. Generated by skin/gen_layout.py -- edit that, not this file."] + THEME + [""]
    L += main_page() + [""] + mod_page()
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "layout.conf")
    open(path, "w").write("\n".join(L) + "\n")
    print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
