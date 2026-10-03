#!/usr/bin/env python3
"""Post-build touch-up of the generated skin (run by build.sh on build/skin).

The layout bakes each control's name (per mode on the CLOUDS tab, on a coloured tag or as plain dark lettering, as on
the module). MPC would draw the parameter name on top of it, so knob, toggle and slider components lose their
"Name" label here.

    post_build.py "<skin dir>/Plugin Skins"
"""
import json
import os
import sys

def main(skins):
    path = os.path.join(skins, "TUI.json")
    tui = json.load(open(path))
    removed = 0
    moved = 0

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
    json.dump(tui, open(path, "w"))
    print("post_build: removed %d MPC name label(s), moved %d slider value(s)" % (removed, moved))


if __name__ == "__main__":
    main(sys.argv[1])
