#!/usr/bin/env python3
"""Runtime 24: make Breath of the Wild's Map page follow the companion pane's shape.

The page is drawn for 1240x1080. On a foldable's split pane the strip is ~21:9 or wider, and a
page fitted by height leaves a third of the width black. "canvas_follows_panel" widens the page's
canvas to the pane's own aspect (the height, and so every text size, stays), and this script binds
the pieces that have to follow that width:

  ui.dx    = max(0, round(1080 * @panel.w / @panel.h) - 1240)   the extra width
  ui.half  = ui.dx / 2                                          a page-centred element's shift
  ui.wide  = 1 when the pane is wider than the page              (gates the 1240-wide frame)

  * the map, the dims and the full-bleed state backgrounds widen  (w_bind = ui.dx)
  * the right-hand column (popups, tool grids, layers list, the Pins / Layers bar, the inspect
    panel, the travel / cancel buttons) keeps its place at the pane's right edge (x_bind = ui.dx)
  * the page-centred things (the warp-confirm modal, the state notices, the Hyrule title) stay
    centred (x_bind = ui.half)
  * the top / bottom frames are the 1240-wide window's chrome: hidden when the pane is wide
  * the two top / bottom glows are rotated bands: they widen (h_bind, pre-rotation) and recentre
    (x_bind = y_bind = ui.half with y_scale -1, the rect's centre is what the rotation uses)

Everything else (the Back / Close button at the left edge, the map's own overlays) is untouched.

  python3 tools/botw/adapt_map_wide.py packages/BreathOfTheWild/dualscreen/manifest.json [--dry-run]
"""
import json
import sys

# Full-bleed backgrounds: they widen with the page (rect x 0, the page's whole width).
WIDEN = {
    "hyrule_map",
    "pp_map_modal_block",
    "wt_map_confirm_dim",
    "ov_event_dim",
    "ov_menu_dim",
    "ov_menu_plate",
    "ov_menu_plate_edge",
    "ov_menu_plate_edge_bottom",
    "rl_state_wrongpatch_bg",
}
# The game's 1240-wide top / bottom frame: hidden on a wide pane.
HIDE_WIDE = {"pg_map_frame_top", "pg_map_frame_bottom"}
# Rotated gradient bands: widen along their pre-rotation height and recentre.
GLOWS = {"pg_map_glow_top", "pg_map_glow_bottom"}
# Page-centred: the warp-confirm modal, the state notices, the Hyrule title, the edit hints.
CENTRED = {
    "wt_map_confirm_panel",
    "wt_map_confirm_edge",
    "wt_map_confirm_name",
    "wt_map_confirm_status",
    "wt_map_confirm_cancel_fill",
    "wt_map_confirm_cancel_fill_i13_pressed",
    "wt_map_confirm_cancel_tap",
    "wt_map_confirm_cancel_label",
    "wt_map_confirm_go_fill",
    "wt_map_confirm_go_fill_i13_pressed",
    "wt_map_confirm_go_tap",
    "wt_map_confirm_go_label",
    "state_title_5",
    "state_title_6",
    "state_unknown_5",
    "rl_state_wrongpatch_logo",
    "rl_state_wrongpatch_title",
    "rl_state_wrongpatch_reason",
    "rl_state_wrongpatch_fix",
    "rl_state_wrongpatch_note",
    "ov_menu_caption",
    "full_map_1",
    "full_map_3",
    "full_map_22",
    "rl_map_hint_sel",
}
# The right-hand column starts here (the popups' left edge); anything at or right of it is
# anchored to the pane's right edge.
RIGHT_EDGE_X = 500
# Never moved: the left command bar (Back / Close) and the probe.
KEEP = {"fx_probe"}


def main():
    path = sys.argv[1]
    dry = "--dry-run" in sys.argv
    with open(path, encoding="utf-8") as f:
        manifest = json.load(f)
    page = next(p for p in manifest["pages"] if p["id"] == "map")

    page["canvas_w"] = 1240
    page["canvas_h"] = 1080
    page["canvas_follows_panel"] = True

    # Derived: the extra width, its half, and the wide gate. Written as expressions over
    # "@panel.w" / "@panel.h" so ui.dx is exactly the canvas the runtime computes.
    width = "round(1080 * @panel.w / @panel.h)"
    derived = [d for d in manifest["derived"] if d.get("name") not in ("ui.dx", "ui.half", "ui.wide")]
    derived += [
        {"name": "ui.dx", "expr": f"max(0, {width} - 1240)"},
        {"name": "ui.half", "expr": f"max(0, ({width} - 1240) / 2)"},
        {"name": "ui.wide", "expr": f"{width} > 1240 ? 1 : 0"},
    ]
    manifest["derived"] = derived

    changed = {"widen": [], "right": [], "centre": [], "hide": [], "glow": [], "skipped": []}
    for w in page["widgets"]:
        wid = w.get("id", "")
        r = w.get("rect") or [0, 0, 0, 0]
        x, y, rw, rh = r
        if wid in KEEP:
            continue
        has_pos = bool(w.get("x_bind")) or bool(w.get("y_bind"))
        has_size = bool(w.get("w_bind")) or bool(w.get("h_bind"))
        if wid in HIDE_WIDE:
            if not has_pos:
                w["hide_bind"] = "ui.wide"
                changed["hide"].append(wid)
            continue
        if wid in GLOWS:
            if not has_pos:
                w["x_bind"] = "ui.half"
                w["y_bind"] = "ui.half"
                w["y_scale"] = -1
            if not has_size:
                w["h_bind"] = "ui.dx"
            changed["glow"].append(wid)
            continue
        if wid in WIDEN or (x == 0 and rw >= 1200):
            if not has_size:
                w["w_bind"] = "ui.dx"
                changed["widen"].append(wid)
            continue
        if wid in CENTRED:
            if not has_pos:
                w["x_bind"] = "ui.half"
                changed["centre"].append(wid)
            else:
                changed["skipped"].append((wid, "already positioned"))
            continue
        if x >= RIGHT_EDGE_X:
            if not has_pos:
                w["x_bind"] = "ui.dx"
                changed["right"].append(wid or f"#{page['widgets'].index(w)}")
            else:
                changed["skipped"].append((wid, "already positioned"))

    for kind in ("widen", "right", "centre", "hide", "glow"):
        print(f"{kind:<7} {len(changed[kind]):>3}")
    if changed["skipped"]:
        print("skipped (already bound):", changed["skipped"])

    if dry:
        return
    text = json.dumps(manifest, ensure_ascii=False, separators=(",", ":"))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {path} ({len(text.encode('utf-8'))} bytes)")


main()
