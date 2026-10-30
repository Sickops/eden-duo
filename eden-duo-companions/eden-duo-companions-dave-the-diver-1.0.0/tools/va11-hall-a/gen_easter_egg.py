"""The easter egg's art (dualscreen/egg_heart.png, egg_drawing.png): a small pixel heart for the
empty box under Age on the bar, in the game's drink pinks (#CF3C71, a #FF82CE highlight, a dark
outline), and the note's drawing: the artist's tools/va11-hall-a/art/lil_drawing.png, cropped to its lines and
fitted into DRAWING_BOX (smoothly: it is thin hand-drawn lines, which nearest-neighbour would break).
Without it, the heart, big, stands in.

  python tools/va11-hall-a/gen_easter_egg.py
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DS = os.path.join(HERE, "..", "..", "packages", "VA11HallA", "dualscreen")
HEART = [
    "..##...##..",
    ".####.####.",
    "###########",
    "###########",
    "###########",
    ".#########.",
    "..#######..",
    "...#####...",
    "....###....",
    ".....#.....",
]
FILL, LIGHT, LINE = (0xCF, 0x3C, 0x71), (0xFF, 0x82, 0xCE), (0x4A, 0x0B, 0x2A)
DRAWING = os.path.join(HERE, "art", "lil_drawing.png")
DRAWING_BOX = (600, 420)


def heart():
    w, h = len(HEART[0]) + 2, len(HEART) + 2
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    filled = {(x + 1, y + 1) for y, row in enumerate(HEART) for x, c in enumerate(row) if c == "#"}
    for x, y in filled:
        im.putpixel((x, y), FILL + (255,))
    for x, y in filled:  # the outline: every empty neighbour of the heart
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + dx, y + dy) not in filled:
                im.putpixel((x + dx, y + dy), LINE + (255,))
    for x, y in ((3, 3), (4, 3), (3, 4), (8, 3)):  # the shine on the left lobe, a dot on the right
        im.putpixel((x, y), LIGHT + (255,))
    return im


def main():
    h = heart()
    h.resize((h.width * 4, h.height * 4), Image.NEAREST).save(os.path.join(DS, "egg_heart.png"), optimize=True)
    if os.path.exists(DRAWING):
        d = Image.open(DRAWING).convert("RGBA")
        d = d.crop(d.getchannel("A").getbbox())
        f = min(DRAWING_BOX[0] / d.width, DRAWING_BOX[1] / d.height)
        d = d.resize((round(d.width * f), round(d.height * f)), Image.LANCZOS)
    else:
        d = h.resize((h.width * 20, h.height * 20), Image.NEAREST)
    d.save(os.path.join(DS, "egg_drawing.png"), optimize=True)
    print("egg_drawing.png", d.size)


if __name__ == "__main__":
    main()
