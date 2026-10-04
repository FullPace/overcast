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


SLIDER_TOP = 40    # the bar sits at y 40-60 of a horizontal slider's 100 px filmstrip frame
SLIDER_H = 44      # bar (20) + value (22) + 2


def compact_sliders(skins, tui, defs):
    """Horizontal sliders (the matrix amounts): the framework gives each a 130x116 frame around a 20 px bar, so the
    frames of rows 54 px apart overlap: the arrow keys then skip every other row, and the focus outline is huge.
    The frame shrinks to the bar and its value; the filmstrip sits 40 px higher, so the bar stays where it was.
    Focused, a teal copy of the filmstrip covers the black one: the bar itself shows the selection."""
    keys = set()
    for d in defs:
        comps = d["value"].get("componentsData", [])
        strips = [c for c in comps if c.get("componentData", {}).get("type") == "Knob"
                  and c["componentData"]["data"].get("filmStrip", "").startswith("sh_slider_h")]
        if not strips:
            continue
        keys.add(d["key"])
        strip = strips[0]
        x, _, w, h = strip["bounds"]["bounds"].split()
        strip["bounds"]["bounds"] = "%s %d %s %s" % (x, -SLIDER_TOP, w, h)
        width = 0
        for c in comps:
            cd = c["componentData"]
            if cd.get("type") == "Focus":
                width = int(c["bounds"]["bounds"].split()[2])
                c["bounds"]["bounds"] = "0 0 %d %d" % (width, SLIDER_H)
            elif cd.get("type") == "Label" and cd["data"].get("type") == "Value":
                c["bounds"]["bounds"] = "0 20 %s 22" % c["bounds"]["bounds"].split()[2]
        src = strip["componentData"]["data"]["filmStrip"]
        teal = src.replace(".png", "_focus.png")
        im = Image.open(os.path.join(skins, src)).convert("RGBA")
        px = im.load()
        for yy in range(im.size[1]):
            for xx in range(im.size[0]):
                r, g, b, a = px[xx, yy]
                if a and r < 110 and g < 110 and b < 110:      # the black bar; the light thumb stays
                    px[xx, yy] = (0, 151, 151, a)
        im.save(os.path.join(skins, teal))
        focus = copy.deepcopy(strip)
        focus["componentData"]["name"] = "Slider Focus"
        focus["componentData"]["data"]["filmStrip"] = teal
        focus["bounds"]["whenVisible"] = "WhenFocussed"
        comps.insert(comps.index(strip) + 1, focus)

    def place(node):
        if isinstance(node, dict):
            if node.get("componentData", {}).get("type") in keys:
                x, y, w, h = node["bounds"]["bounds"].split()
                node["bounds"]["bounds"] = "%s %d %s %d" % (x, int(y) + SLIDER_TOP, w, SLIDER_H)
            for v in node.values():
                place(v)
        elif isinstance(node, list):
            for v in node:
                place(v)
    place(tui)


SEG_MARGIN = 3


def frame_segment_groups(tui, defs):
    """Option bars and lists (enum_h / enum_v: mode, manual topics): only the first segment takes the focus, and it
    has no focus outline. Its frame grows to the whole group (plus a margin) with a teal outline while focused; the
    other segments lie on top of it, so their taps still reach them."""
    placed = {}

    def find(node):
        if isinstance(node, dict):
            t = str(node.get("componentData", {}).get("type", ""))
            if t.startswith("shSeg_"):
                placed[t] = node
            for v in node.values():
                find(v)
        elif isinstance(node, list):
            for v in node:
                find(v)
    find(tui["pageData"])
    by_key = {d["key"]: d for d in defs}
    for t, node in placed.items():
        if not t.endswith("_0"):
            continue
        group = [n for k, n in placed.items() if k.rsplit("_", 1)[0] == t.rsplit("_", 1)[0]]
        rects = [[int(v) for v in n["bounds"]["bounds"].split()] for n in group]
        x0 = min(r[0] for r in rects) - SEG_MARGIN
        y0 = min(r[1] for r in rects) - SEG_MARGIN
        x1 = max(r[0] + r[2] for r in rects) + SEG_MARGIN
        y1 = max(r[1] + r[3] for r in rects) + SEG_MARGIN
        ox, oy, _, _ = [int(v) for v in node["bounds"]["bounds"].split()]
        node["bounds"]["bounds"] = "%d %d %d %d" % (x0, y0, x1 - x0, y1 - y0)
        comps = by_key[t]["value"]["componentsData"]
        for c in comps:
            bx, by, bw, bh = [int(v) for v in c["bounds"]["bounds"].split()]
            c["bounds"]["bounds"] = "%d %d %d %d" % (bx + ox - x0, by + oy - y0, bw, bh)
        focus = copy.deepcopy(comps[0])
        focus["componentData"] = {"version": 1, "name": "Focus", "type": "Focus",
                                  "data": {"version": 1, "backgroundColour": "00000000", "outlineColour": "ff009797",
                                           "backgroundInset": 2.0, "outlineThickness": 2.0}}
        focus["bounds"].update({"whenVisible": "WhenFocussed", "acceptsHWFocus": "No",
                                "bounds": "0 0 %d %d" % (x1 - x0, y1 - y0)})
        comps.append(focus)
        # just before the group's other segments (not under the page background), so they stay on top for taps
        for parent in find_parents(tui["pageData"], node):
            first = min(k for k, v in enumerate(parent) if any(v is n for n in group))
            parent.remove(node)
            parent.insert(first, node)


def find_parents(root, child):
    out = []

    def walk(node):
        if isinstance(node, dict):
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            if any(v is child for v in node):
                out.append(node)
            for v in node:
                walk(v)
    walk(root)
    return out


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
    compact_sliders(skins, tui, defs)
    frame_segment_groups(tui, defs)

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

    def flake_outline(src):
        """The snowflake's shape grown by a few pixels, in teal: drawn under the flake, it shows as a teal rim."""
        name = src.replace(".png", "_focus.png")
        if not os.path.exists(os.path.join(skins, name)):
            from PIL import ImageFilter
            alpha = Image.open(os.path.join(skins, src)).convert("RGBA").split()[3]
            grown = alpha.filter(ImageFilter.MaxFilter(5))   # a 2 px rim, soft edges kept
            rim = Image.new("RGBA", alpha.size, (0, 151, 151, 0))
            rim.putalpha(grown)
            rim.save(os.path.join(skins, name))
        return name

    def flake_white(src):
        name = src.replace(".png", "_white.png")
        im = Image.open(os.path.join(skins, src)).convert("RGBA")
        white = Image.new("RGBA", im.size, (255, 255, 255, 0))
        white.putalpha(im.split()[3])
        white.save(os.path.join(skins, name))
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
                        is_knob = knob and strip.startswith(("sh_knob_", "sh_slider_h"))   # sliders: teal bar
                        images = json.dumps(comps)
                        if "_64x64" in images:          # the Freeze snowflake (w=64 in gen_layout): its colour is enough
                            is_knob = True
                        cd["data"].update({"backgroundColour": "00000000",
                                           "outlineColour": "00000000" if is_knob else "ff009797",
                                           "outlineThickness": 0.0 if is_knob else 2.0})
                button = [c for c in comps if c.get("componentData", {}).get("type") == "Button"]
                if button and "_64x64" in button[0]["componentData"]["data"].get("offImage", ""):
                    # the Freeze snowflake: focused, a teal outline in its own shape
                    outline = copy.deepcopy(button[0])
                    outline["componentData"] = {"version": 1, "name": "Focus Outline", "type": "Image",
                                                "data": {"version": 2, "imageType": "Regular", "colour": "0",
                                                         "image": flake_outline(button[0]["componentData"]["data"]["offImage"])}}
                    outline["bounds"]["whenVisible"] = "WhenFocussed"
                    outline["bounds"]["acceptsHWFocus"] = "No"
                    comps.insert(comps.index(button[0]), outline)   # under the flake: only the rim shows
                    # focused and off, the flake turns white (on, it stays teal): a second button on the same
                    # param, shown only while focused, over the grey one
                    white = copy.deepcopy(button[0])
                    white["componentData"]["name"] = "Button Focus"
                    white["componentData"]["data"]["offImage"] = flake_white(button[0]["componentData"]["data"]["offImage"])
                    white["bounds"]["whenVisible"] = "WhenFocussed"
                    comps.insert(comps.index(button[0]) + 1, white)
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
