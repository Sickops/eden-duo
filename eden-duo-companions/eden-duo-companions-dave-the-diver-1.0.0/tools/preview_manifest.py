#!/usr/bin/env python3
"""Renders one page of a companion manifest to a PNG, as a layout preview.

The real art is decoded from the player's own game files by the native module, so a preview
cannot show it: a picture is drawn as a tinted placeholder carrying its key. Rects, pills,
labels, values and the repeat grid are the runtime's own geometry, which is what a layout
review is about.

  python3 tools/preview_manifest.py packages/MarioKart8Deluxe/dualscreen/manifest.json \
      race_map_light out.png [--format short|long] [--scale 0.6]
"""
import argparse
import json
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

# A rough stand-in for the game's fonts: cap height 5 * text_scale px, close to the runtime's
# measurement of turbo_MARIOFont, drawn bold so labels stay readable in the preview.
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\arialbd.ttf",
    r"C:\Windows\Fonts\segoeuib.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]


def load_font(px):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return ImageFont.truetype(path, max(6, int(px)))
    return ImageFont.load_default()


def color(value, default=(0, 0, 0, 0)):
    """'#AARRGGBB' (the manifest's form) -> (r, g, b, a)."""
    if value is None:
        return default
    if isinstance(value, int):
        a = (value >> 24) & 0xFF
        return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, a)
    if not isinstance(value, str) or not value.startswith("#"):
        return default
    h = value[1:]
    if len(h) == 6:
        h = "FF" + h
    a, r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4, 6))
    return (r, g, b, a)


def align_value(widget):
    value = widget.get("align", 0)
    if isinstance(value, str):
        return {"left": 0, "center": 1, "right": 2}.get(value, 0)
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def substitute(text, index):
    if not isinstance(text, str):
        return text
    return text.replace("{i}", str(index))


# Published values a widget position may read. A preview cannot sample the game, so the few
# binds a layout's geometry uses are given a plausible value: a card sits on its own rank, the
# local player's card sits on its rank, map markers spread over the picture.
CONSTS = {}


def bind_value(bind, index, count):
    if not bind:
        return 0.0
    if bind in CONSTS:
        return float(CONSTS[bind])
    if bind.endswith(".row_y"):
        return float(index + 1)
    if bind.endswith(".row_dx"):
        return 0.0
    if bind in ("me.rank", "mc.row_y"):
        return 3.0
    if bind.endswith((".mapf_x", ".map_x")):
        return 0.25 + 0.5 * ((index * 5) % 12) / 11.0
    if bind.endswith((".mapf_y", ".map_y")):
        return 0.2 + 0.6 * ((index * 7) % 12) / 11.0
    return 0.0


def expand(page, fmt, mode):
    """The page's widgets with repeats expanded and the other row format / mode dropped."""
    out = []
    for widget in page["widgets"]:
        anim = widget.get("anim") or {}
        group = anim.get("group", "")
        if fmt == "short" and group in ("fmt_long", "c_long"):
            continue
        if fmt == "long" and group in ("fmt_short", "c_short"):
            continue
        need = widget.get("need_bind", "")
        if fmt == "short" and need == "fl.l1":
            continue
        if fmt == "long" and need == "fl.l0":
            continue
        repeat = int(widget.get("repeat", 0) or 0)
        if repeat <= 0:
            out.append(dict(widget, _i=None))
            continue
        cols = int(widget.get("repeat_cols", 0) or 0)
        dx = int(widget.get("repeat_dx", 0) or 0)
        dy = int(widget.get("repeat_dy", 0) or 0)
        row_dy = int(widget.get("repeat_row_dy", 0) or 0) or dy
        for i in range(repeat):
            w = {}
            for k, v in widget.items():
                if isinstance(v, str):
                    w[k] = substitute(v, i)
                elif isinstance(v, list):
                    w[k] = [substitute(x, i) if isinstance(x, str) else x for x in v]
                elif isinstance(v, dict):
                    w[k] = {kk: substitute(vv, i) if isinstance(vv, str) else vv
                            for kk, vv in v.items()}
                else:
                    w[k] = v
            x, y, iw, ih = w["rect"]
            if cols > 0:
                x += (i % cols) * dx
                y += (i // cols) * row_dy
            else:
                x += i * dx
                y += i * dy
            w["rect"] = [x, y, iw, ih]
            w["_i"] = i
            out.append(w)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest")
    ap.add_argument("page")
    ap.add_argument("out")
    ap.add_argument("--format", default="short", choices=("short", "long"))
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--dark-bg", default=None,
                    help="page background when the manifest's colour is missing")
    args = ap.parse_args()

    with open(args.manifest, encoding="utf-8") as f:
        manifest = json.load(f)
    CONSTS.clear()
    for d in manifest.get("derived", []):
        if not d.get("terms") and "add" in d and "cmp" not in d and "select" not in d:
            CONSTS[d["name"]] = d["add"]
    page = next((p for p in manifest["pages"] if p["id"] == args.page), None)
    if page is None:
        sys.exit("no page '{}'".format(args.page))
    w = int(page.get("canvas_w") or manifest.get("canvas_w") or 1240)
    h = int(page.get("canvas_h") or manifest.get("canvas_h") or 1080)
    bg = color(manifest.get("background"), (20, 22, 26, 255))
    image = Image.new("RGBA", (w, h), bg)
    draw = ImageDraw.Draw(image, "RGBA")
    widgets = expand(page, args.format, None)

    def text_at(x, y, text, scale, fill, anchor="la"):
        font = load_font(5 * scale * 1.42)
        draw.text((x, y), text, font=font, fill=fill, anchor=anchor)

    for widget in widgets:
        kind = widget.get("type")
        rect = [int(round(v)) for v in widget["rect"]]
        if len(rect) != 4:
            continue
        x, y, iw, ih = rect
        # x_bind / y_bind move ANY widget, not just rects: a rank card is a stack of images and
        # labels placed on its live row.
        if widget.get("x_bind"):
            x += int(round(bind_value(widget["x_bind"], widget.get("_i") or 0, 12) *
                           widget.get("x_scale", 1)))
        if widget.get("y_bind"):
            y += int(round(bind_value(widget["y_bind"], widget.get("_i") or 0, 12) *
                           widget.get("y_scale", 1)))
        if iw <= 0 or ih <= 0:
            if kind in ("label", "value"):
                iw, ih = 1, 1
            else:
                continue
        box = [x, y, x + iw, y + ih]
        if kind == "rect":
            fill = color(widget.get("bg"), (0, 0, 0, 0))
            if fill[3]:
                draw.rectangle(box, fill=fill)
            edge = color(widget.get("color"), (0, 0, 0, 0))
            if edge[3]:
                width = max(1, int(widget.get("frame", 2)))
                draw.rectangle(box, outline=edge, width=width)
        elif kind == "button":
            fill = color(widget.get("bg"), (255, 255, 255, 40))
            edge = color(widget.get("color"), (0, 0, 0, 0))
            if edge[3] == 0:
                edge = fill
            width = max(1, int(widget.get("border", 3)))
            if widget.get("pill"):
                draw.rounded_rectangle(box, radius=min(iw, ih) // 2, fill=fill, outline=edge,
                                       width=width)
            else:
                draw.rectangle(box, fill=fill, outline=edge, width=width)
            text = widget.get("text") or ""
            if text:
                scale = int(widget.get("text_scale", 3))
                inset = int(widget.get("text_inset", 12))
                if align_value(widget) == 1:
                    text_at((x + x + iw) // 2, y + ih // 2, text, scale,
                            color(widget.get("color"), (255, 255, 255, 255)), "mm")
                else:
                    text_at(x + inset, y + ih // 2, text, scale,
                            color(widget.get("color"), (255, 255, 255, 255)), "lm")
        elif kind in ("label", "value"):
            scale = int(widget.get("text_scale", 5))
            fill = color(widget.get("color"), (255, 255, 255, 255))
            align = align_value(widget)
            text = widget.get("text")
            if text is None and widget.get("bind_text"):
                text = "<" + widget["bind_text"] + ">"
            if text is None and widget.get("bind"):
                text = str(int(bind_value(widget["bind"], widget.get("_i") or 0, 12))
                           if not widget["bind"].endswith(("_x", "_y")) else "")
            if not text:
                continue
            if align == 1:
                text_at(x, y, text, scale, fill, "ma")
            elif align == 2:
                text_at(x, y, text, scale, fill, "ra")
            else:
                text_at(x, y, text, scale, fill, "la")
        elif kind in ("image", "map"):
            src = widget.get("src") or widget.get("src_bind") or widget.get("bind") or ""
            tint = color(widget.get("color"), (255, 255, 255, 255))
            # A placeholder, not the art: dark enough to be seen on a white plate, tinted by the
            # widget's own colour so a map stays blue and a highlight stays light.
            base = (tint[0] * 45 // 100, tint[1] * 45 // 100, tint[2] * 45 // 100, 85)
            draw.rectangle(box, fill=base, outline=(255, 255, 255, 110), width=1)
            name = os.path.basename(str(src).replace("^", "#").split("#")[-1]) or "image"
            if iw >= 90 and ih >= 60:
                text_at(x + 4, y + ih - 14, name[:28], 2, (255, 255, 255, 190), "la")

    if args.scale != 1.0:
        image = image.resize((max(1, int(w * args.scale)), max(1, int(h * args.scale))),
                             Image.LANCZOS)
    image.convert("RGBA").save(args.out)
    print("{} {}x{} -> {}".format(args.page, w, h, args.out))


if __name__ == "__main__":
    main()
