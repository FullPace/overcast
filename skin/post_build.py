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

    def walk(node):
        nonlocal removed
        if isinstance(node, dict):
            comps = node.get("componentsData")
            if isinstance(comps, list):
                kinds = [c.get("componentData", {}).get("type") for c in comps]
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
    print("post_build: removed %d MPC name label(s)" % removed)


if __name__ == "__main__":
    main(sys.argv[1])
