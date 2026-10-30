"""The stand-by overlay's pixel dissolve (dualscreen/dither_1..16.png): 32x32 tiles of a 4x4 Bayer
dither in 8 px blocks (2 pixels of the bar window's 4 px grid), tile k covering k/16 of the
screen in the overlay's dark purple. The package tiles one over the whole screen (va_dither) while
the overlay swaps Jill's bar and the doodles.

  python tools/va11-hall-a/gen_transitions.py
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DS = os.path.join(HERE, "..", "..", "packages", "VA11HallA", "dualscreen")
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
COLOR = (0x12, 0x0E, 0x1C)
BLOCK = 8


def main():
    for k in range(1, 17):
        tile = Image.new("RGBA", (4, 4), (0, 0, 0, 0))
        for y in range(4):
            for x in range(4):
                if BAYER[y][x] < k:
                    tile.putpixel((x, y), COLOR + (255,))
        tile.resize((4 * BLOCK, 4 * BLOCK), Image.NEAREST).save(os.path.join(DS, "dither_%d.png" % k), optimize=True)


if __name__ == "__main__":
    main()
