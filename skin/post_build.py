#!/usr/bin/env python3
"""Post-build touch-up of the generated skin (run by build.sh on build/skin).

The layout bakes each control's name (per mode on the CLOUDS tab, on a coloured tag or as plain dark lettering, as on
the module). MPC would draw the parameter name on top of it, so knob, toggle and slider components lose their
"Name" label here.

    post_build.py "<skin dir>/Plugin Skins"
"""
import copy
import json
import os
import sys

from PIL import Image, ImageDraw


def main(skins):
    path = os.path.join(skins, "TUI.json")
    tui = json.load(open(path))
    removed = 0
    moved = 0

    # The LFO rate knobs share radius and look with the envelope knobs, so find their own definitions through the
    # params they are bound to ("Parameter <index>" in params.json order). They get a copy of the shared definition.
    keys = [p["key"] for p in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                                            "params.json")))["params"]]
    lfo_params = {"Parameter %d" % keys.index(k) for k in ("lfo1_rate", "lfo2_rate")}
    defs = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
    lfo_defs = set()

    def split_lfo_defs(node):
        if isinstance(node, dict):
            remap = node.get("handle remapping", {}).get("map", [])
            ctype = node.get("componentData", {}).get("type", "")
            if any(m.get("value") in lfo_params for m in remap) and ctype.startswith("shKnob"):
                new_key = ctype + "_lfo"
                if new_key not in lfo_defs:
                    src = [d for d in defs if d["key"] == ctype][0]
                    defs.append({"key": new_key, "value": copy.deepcopy(src["value"])})
                    lfo_defs.add(new_key)
                node["componentData"]["type"] = new_key
            for v in node.values():
                split_lfo_defs(v)
        elif isinstance(node, list):
            for v in node:
                split_lfo_defs(v)

    split_lfo_defs(list(defs))   # the placed controls live in the page definitions
    for d in defs:
        d["value"]["_def_key"] = d["key"]

    def walk(node):
        nonlocal removed, moved
        if isinstance(node, dict):
            comps = node.get("componentsData")
            if isinstance(comps, list):
                kinds = [c.get("componentData", {}).get("type") for c in comps]
                strips = [c["componentData"]["data"].get("filmStrip", "") for c in comps
                          if c.get("componentData", {}).get("type") == "Knob"]
                if strips and strips[0].startswith("sh_slider_h"):
                    # Horizontal sliders (matrix amounts): the bar sits at y 40-60 of the 100 px frame, but the
                    # framework puts the value at y 84, inside the next matrix row. Put it right under the bar.
                    for c in comps:
                        cd = c.get("componentData", {})
                        if cd.get("type") == "Label" and cd.get("data", {}).get("type") == "Value":
                            c["bounds"]["bounds"] = "0 60 130 22"
                            moved += 1
                if node.get("_def_key") in lfo_defs:
                    # LFO rate knobs carry no name: their value moves up right under the knob
                    knob = [c for c in comps if c.get("componentData", {}).get("type") == "Knob"][0]
                    size = int(knob["bounds"]["bounds"].split()[3])           # s = 2r + 10
                    name_y = size // 2 + (size - 10) // 2 + 2
                    for c in comps:
                        cd = c.get("componentData", {})
                        if cd.get("type") == "Label" and cd.get("data", {}).get("type") == "Value":
                            x, y, w, h = c["bounds"]["bounds"].split()
                            c["bounds"]["bounds"] = "%s %d %s %s" % (x, name_y, w, h)
                            moved += 1
                if "Knob" in kinds or "Button" in kinds or "Slider" in kinds:
                    keep = [c for c in comps if not (c.get("componentData", {}).get("type") == "Label"
                                                     and c["componentData"]["data"].get("type") == "Name")]
                    removed += len(comps) - len(keep)
                    comps[:] = keep
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)

    walk(tui)
    for d in defs:
        d["value"].pop("_def_key", None)

    # Q-Link indicators: the MPC highlights the screen area of the Q-Link column being turned (stock skins do this;
    # the framework switches it off). Rectangles per tab come from gen_layout.py.
    bounds = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "qlink_bounds.json")))
    for tab, rects in zip(tui["pageData"]["tabs"], bounds):
        tab["qlinkBoundsData"] = rects

    def ring_image(size):
        """A teal ring around the knob (drawn knobs fill 0.37 of their frame), for the focused knob."""
        name = "focus_ring_%d.png" % size
        n = size * 4
        im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        r_out, width = n * 0.47, n * 0.045
        d.ellipse((n / 2 - r_out, n / 2 - r_out, n / 2 + r_out, n / 2 + r_out), outline=(0, 151, 151, 255),
                  width=int(width))
        im.resize((size, size), Image.LANCZOS).save(os.path.join(skins, name))
        return name

    def show_qlinks_and_focus(node):
        if isinstance(node, dict):
            if "hideQLinkBounds" in node:
                node["hideQLinkBounds"] = False
            comps = node.get("componentsData")
            if isinstance(comps, list):
                knob = [c for c in comps if c.get("componentData", {}).get("type") == "Knob"]
                strip = knob[0]["componentData"]["data"].get("filmStrip", "") if knob else ""
                for c in comps:
                    cd = c.get("componentData", {})
                    if cd.get("type") == "Focus":
                        # the active control: the framework makes the focus outline transparent; non-knob controls
                        # get a teal outline, knobs a ring image instead (an outline around the knob gets clipped)
                        is_knob = knob and strip.startswith("sh_knob_")
                        images = json.dumps(comps)
                        if "_64x64" in images:          # the Freeze snowflake (w=64 in gen_layout): its colour is enough
                            is_knob = True
                        cd["data"].update({"backgroundColour": "00000000",
                                           "outlineColour": "00000000" if is_knob else "ff009797",
                                           "outlineThickness": 0.0 if is_knob else 2.0})
                if knob and strip.startswith("sh_knob_"):
                    size = int(knob[0]["bounds"]["bounds"].split()[3])
                    ring = copy.deepcopy(knob[0])
                    ring["componentData"] = {"version": 1, "name": "Focus Ring", "type": "Image",
                                             "data": {"version": 2, "imageType": "Regular", "colour": "0",
                                                      "image": ring_image(size)}}
                    ring["bounds"]["whenVisible"] = "WhenFocussed"
                    ring["bounds"]["acceptsHWFocus"] = "No"
                    comps.append(ring)
            for v in node.values():
                show_qlinks_and_focus(v)
        elif isinstance(node, list):
            for v in node:
                show_qlinks_and_focus(v)

    show_qlinks_and_focus(tui)
    json.dump(tui, open(path, "w"))
    print("post_build: removed %d MPC name label(s), moved %d value label(s)" % (removed, moved))


if __name__ == "__main__":
    main(sys.argv[1])
