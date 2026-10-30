"""The stand-by overlay's neon layers. The dimmer (dualscreen/bg_neon_dim.png): a muted violet layer over the
bar window's lit middle that takes the edge off its cyan next to Jill's skin. Its alpha follows
how bright the window is under it (the shelves and navy bottles stay as they are) and fades out
toward the sides. The package tints it by va_neon (steady / dip / deep dip) to flicker the neon.
The tube (dualscreen/bg_neon_tube.png): a vertical neon down the middle of the light, soft and wide
as if behind the frosted glass (a pale cyan centre blurring out into the cyan), only on the lit glass (the shelves and bottles stay in front of it); the
package dims it with the same va_neon so the tube is what flickers.
The bloom (dualscreen/bg_neon_bloom.png): the lit window (with the dimmer and the tube) blurred,
so its light bleeds over the shelves' and bottles' edges like a haze; dimmed by va_neon too.
The mote (dualscreen/neon_mote.png): one speck of dust in the light, an 8x8 dot in a faint ring, that the package repeats and the module moves (va_mote<i>_x/_y/_a).
Drawn on the window's 310x270 grid and scaled x4 (nearest), like the window itself (the bloom is
blurred at full size).

  python tools/va11-hall-a/gen_neon_dim.py
"""
import math
import os
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
DS = os.path.join(HERE, "..", "..", "packages", "VA11HallA", "dualscreen")
S = 4
COLOR = (40, 24, 60)       # muted violet: dims the cyan and pulls it toward Jill's warm tones
MAX_ALPHA = 0.5            # at full tint (deep dip); the steady glow uses half of it
TUBE_HOT = (196, 250, 255)  # the tube's centre, through the glass
TUBE_GLOW_C = (110, 236, 255)
TUBE_BODY, TUBE_HALO = 7, 22    # blur widths in grid px (gaussian sigma-ish)
TUBE_PEAK = 0.62                # the centre's alpha: soft, not a hot line
BLOOM_RADIUS = 36               # px at full size
BLOOM_ALPHA = 0.75              # the haze over a dark spot right next to the light
MOTE = (205, 250, 255)


def main():
    bg = Image.open(os.path.join(DS, "bg_bar_window.png")).convert("RGB")
    W, H = bg.width // S, bg.height // S
    src = bg.resize((W, H), Image.NEAREST).load()
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = out.load()
    x0, x1 = 10, 300                           # the window (as make_bg drew it)
    cx, half = (x0 + x1) / 2, (x1 - x0) / 2
    for y in range(H):
        for x in range(W):
            d = abs(x - cx) / half             # 0 centre .. 1 edge
            fall = 1.0 if d < 0.30 else max(0.0, 1 - (d - 0.30) / 0.40)
            lit = max(src[x, y]) / 255         # the window's light; black / navy stay near 0
            a = round(255 * MAX_ALPHA * fall * lit * lit)
            if a:
                px[x, y] = COLOR + (a,)
    out.resize((W * S, H * S), Image.NEAREST).save(os.path.join(DS, "bg_neon_dim.png"), optimize=True)

    tube = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    tp = tube.load()
    for y in range(H):
        for x in range(W):
            if max(src[x, y]) < 200:           # not the lit glass: a shelf, a bottle, the frame
                continue
            d = abs(x + 0.5 - cx)              # grid px from the tube's centre line
            body = math.exp(-(d / TUBE_BODY) ** 2)
            a = TUBE_PEAK * body + 0.22 * math.exp(-(d / TUBE_HALO) ** 2)
            if a > 0.02:
                c = tuple(round(TUBE_GLOW_C[i] + (TUBE_HOT[i] - TUBE_GLOW_C[i]) * body) for i in range(3))
                tp[x, y] = c + (round(255 * min(1.0, a)),)
    tube = tube.resize((W * S, H * S), Image.NEAREST)
    tube.save(os.path.join(DS, "bg_neon_tube.png"), optimize=True)

    lit = Image.open(os.path.join(DS, "bg_bar_window.png")).convert("RGBA")
    dim = Image.open(os.path.join(DS, "bg_neon_dim.png"))
    dim.putalpha(dim.getchannel("A").point(lambda v: v * 0x80 // 255))   # the steady glow
    lit.alpha_composite(dim)
    lit.alpha_composite(tube)
    glow = lit.convert("RGB").filter(ImageFilter.GaussianBlur(BLOOM_RADIUS))
    # Alpha: how much brighter the blurred light is than the pixel under it, so it lands on the
    # dark edges (shelves, bottles, the frame by the glass) and leaves the glass itself alone.
    base = lit.convert("L").load()
    gl = glow.convert("L").load()
    bloom = glow.convert("RGBA")
    bp = bloom.load()
    for y in range(bloom.height):
        for x in range(bloom.width):
            a = max(0, gl[x, y] - base[x, y]) / 255
            r, g, b, _ = bp[x, y]
            bp[x, y] = (r, g, b, round(255 * min(1.0, a * BLOOM_ALPHA * 2.2)))
    bloom.save(os.path.join(DS, "bg_neon_bloom.png"), optimize=True)

    mote = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
    for y in range(4):
        for x in range(4):
            core = x in (1, 2) and y in (1, 2)
            corner = x in (0, 3) and y in (0, 3)
            if not corner:
                mote.putpixel((x, y), MOTE + (255 if core else 70,))
    mote.resize((4 * S, 4 * S), Image.NEAREST).save(os.path.join(DS, "neon_mote.png"), optimize=True)


if __name__ == "__main__":
    main()
