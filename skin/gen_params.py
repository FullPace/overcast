#!/usr/bin/env python3
"""Rewrites the modulation part of ../params.json (2 envelopes, 2 LFOs, 8 matrix slots) after the base params.

The keys and order must match InitModParams() in src/engine.cc; the option lists match the enums in src/mod.h.

    python3 skin/gen_params.py
"""
import json
import os
import re

ENV_TRIGGERS = ["MIDI", "LFO 1", "LFO 2"]   # (mod.h also has TRIG_BUTTON; the button is gone from the skin)
LFO_SHAPES = ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Smooth Rnd"]
DIVISIONS = ["4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/32"]
SOURCES = ["Off", "LFO 1", "LFO 2", "Env 1", "Env 2", "Velocity"]
DESTS = ["Off", "Position", "Size", "Pitch", "Density", "Texture", "Blend", "Spread", "Feedback", "Reverb",
         "Freeze", "Trigger"]
MOD_KEY = re.compile(r"^(env\d|lfo\d|mod\d|page_)")   # mod\d, not "mode"


def mod_params():
    out = []
    for e in (1, 2):
        out += [
            {"key": "env%d_attack" % e, "name": "Env %d Attack" % e, "min": 0.0, "max": 1.0, "default": 0.1,
             "dynamic_display": True},
            {"key": "env%d_decay" % e, "name": "Env %d Decay" % e, "min": 0.0, "max": 1.0, "default": 0.5,
             "dynamic_display": True},
            {"key": "env%d_sustain" % e, "name": "Env %d Sustain" % e, "min": 0.0, "max": 1.0, "default": 0.7},
            {"key": "env%d_release" % e, "name": "Env %d Release" % e, "min": 0.0, "max": 1.0, "default": 0.5,
             "dynamic_display": True},
            {"key": "env%d_trig" % e, "name": "Env %d Trigger" % e, "options": ENV_TRIGGERS, "default": 0},
        ]
    for l in (1, 2):
        out += [
            {"key": "lfo%d_shape" % l, "name": "LFO %d Shape" % l, "options": LFO_SHAPES, "default": 0},
            {"key": "lfo%d_rate" % l, "name": "LFO %d Rate" % l, "min": 0.0, "max": 1.0, "default": 0.5,
             "dynamic_display": True},
            {"key": "lfo%d_sync" % l, "name": "LFO %d Sync" % l, "options": ["Off", "On"], "default": 0},
            {"key": "lfo%d_div" % l, "name": "LFO %d Division" % l, "options": DIVISIONS, "default": 4},
        ]
    for m in range(1, 9):
        out += [
            {"key": "mod%d_src" % m, "name": "Mod %d Source" % m, "options": SOURCES, "default": 0},
            {"key": "mod%d_dst" % m, "name": "Mod %d Dest" % m, "options": DESTS, "default": 0},
            {"key": "mod%d_amt" % m, "name": "Mod %d Amount" % m, "min": -100.0, "max": 100.0, "unit": "%",
             "display": "int", "default": 0.0},
        ]
    return out


def main():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "params.json")
    d = json.load(open(path))
    base = [p for p in d["params"] if not MOD_KEY.match(p["key"])]
    if not any(p["key"] == "mode" for p in base):   # restore it if an earlier run dropped it
        base.insert(0, {"key": "mode", "name": "Mode", "options": ["Granular", "Stretch", "Looping Delay", "Spectral",
                                                                   "Oliverb", "Resonestor"], "default": 0})
    d["params"] = base + mod_params()
    keys = {p["key"] for p in d["params"]}
    d["sections"] = [s for s in d.get("sections", []) if s["label"] not in ("Envelopes", "LFOs", "Matrix")]
    for s in d["sections"]:
        s["keys"] = [k for k in s["keys"] if k in keys]
    d["sections"] += [
        {"label": "Envelopes", "keys": [p["key"] for p in mod_params() if p["key"].startswith("env")]},
        {"label": "LFOs", "keys": [p["key"] for p in mod_params() if p["key"].startswith("lfo")]},
        {"label": "Matrix", "keys": [p["key"] for p in mod_params() if p["key"].startswith("mod")]},
    ]
    json.dump(d, open(path, "w"), indent=2)
    open(path, "a").write("\n")
    print("params.json: %d params" % len(d["params"]))


if __name__ == "__main__":
    main()
