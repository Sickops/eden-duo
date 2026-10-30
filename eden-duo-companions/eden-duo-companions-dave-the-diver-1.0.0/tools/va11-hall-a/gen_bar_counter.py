"""The stand-by overlay's bar counter (dualscreen/bar_counter.png), full screen width: a lit front
edge, the top surface, and a plain front with a kick plate. Transparent above
it. Jill's shadow on the top is its own layer (dualscreen/bar_shadow.png, the same size): the neon
wall behind her throws it toward us, so it sits under her and widens toward the front edge; the
package draws it twice, one copy moved by each edge, so its sides stretch out on their own. The light on the rest of the top (dualscreen/bar_top_dim.png: the top surface and
its lit front edge, solid, for the package to fade in by va_neon) dips with the neon's flicker; the
shadow is drawn over it, so the dip never touches the shadow. Drawn on the bar window's 310x270 grid from y 216 down, cropped
and scaled x4 (nearest): it lands at (0, 864) on the 1240x1080 screen.

  python tools/va11-hall-a/gen_bar_counter.py
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DS = os.path.join(HERE, "..", "..", "packages", "VA11HallA", "dualscreen")
W, H, S = 310, 270, 4
# Jill's sprite at 4x spans x 476..1040 where she meets the counter: grid 119..260.
SHADOW_CX, SHADOW_HALF, SHADOW_SPREAD = 189.5, 70, 10
TOP_DIM = (14, 4, 18)      # what the counter's light fades toward when the neon dips


def hexc(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def counter(path, shadow_path, dim_path, edge, mid, top_lit, front, front_dark, trim):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = im.load()
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sp = sh.load()
    dim = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dp = dim.load()
    TOP, FRONT = 216, 232
    for y in range(TOP, FRONT):                 # the top surface, in perspective
        t = (y - TOP) / (FRONT - TOP)
        inset = round(8 - t * 8)
        for x in range(inset, W - inset):
            px[x, y] = edge + (255,)
            dp[x, y] = TOP_DIM + (255,)
            if abs(x - SHADOW_CX) < SHADOW_HALF + t * SHADOW_SPREAD:
                sp[x, y] = mid + (255,)
    for x in range(W):                          # the lit front edge of the top
        px[x, FRONT] = top_lit + (255,)
        px[x, FRONT + 1] = trim + (255,)
        dp[x, FRONT] = TOP_DIM + (255,)
    for y in range(FRONT + 2, H):               # the front
        for x in range(W):
            px[x, y] = front + (255,)
    for y in range(H - 6, H):                   # kick plate
        for x in range(W):
            px[x, y] = front_dark + (255,)
    box = im.getbbox()
    size = ((box[2] - box[0]) * S, (box[3] - box[1]) * S)
    im.crop(box).resize(size, Image.NEAREST).save(path)
    sh.crop(box).resize(size, Image.NEAREST).save(shadow_path, optimize=True)
    dim.crop(box).resize(size, Image.NEAREST).save(dim_path, optimize=True)


if __name__ == "__main__":
    counter(os.path.join(DS, "bar_counter.png"), os.path.join(DS, "bar_shadow.png"),
            os.path.join(DS, "bar_top_dim.png"),
            # the top (lit sides, Jill's shadow) from the artist's reference (new/table.png); the
            # lit front edge, the front, the kick plate and the trim are purples to go with it
            hexc("#3B2E56"), hexc("#2A0830"),
            hexc("#7A64A8"), hexc("#1E1530"), hexc("#140D22"), hexc("#2C2244"))
