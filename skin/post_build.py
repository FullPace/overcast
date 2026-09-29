#!/usr/bin/env python3
"""Post-build touch-up of the generated skin (run by build.sh on build/skin).

Each MPC tab of this skin is one mode, so the layout bakes each control's name for that mode (on a coloured tag, or
plain dark lettering under the white knobs, as on the module). MPC would draw the parameter name on top of it, one
name for all tabs, so the knob and toggle components lose their "Name" label here.

    post_build.py "<skin dir>/Plugin Skins"
"""
import json
import os
import sys

MARKER_R = 7   # gen_layout.py's page marker knob radius

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
                strip = [c["componentData"]["data"].get("filmStrip", "") for c in comps
                         if c.get("componentData", {}).get("type") == "Knob"]
                if strip and strip[0].startswith("sh_knob_r%d_" % MARKER_R):
                    # the page marker: keep only the (invisible) knob, no value or name text
                    removed += len(comps) - 1
                    comps[:] = [c for c in comps if c.get("componentData", {}).get("type") == "Knob"]
                elif "Knob" in kinds or "Button" in kinds:
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
