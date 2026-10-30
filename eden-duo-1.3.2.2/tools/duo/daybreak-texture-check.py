"""Check a 1920x1080 Thor capture of the prologue café's first Van/Elaine dialogue.

Pause at Van's "Huh... It ain't every day..." line; keep the default camera.
Run: python tools/duo/daybreak-texture-check.py screenshot.png
Requires Pillow. Checks actual window materials, not emulator source or mock output.

Temporal check: record 15+ seconds at Elaine's "You're a master of your craft,
Victor." line, 1920x1080, without advancing dialogue or moving the camera.
Run: python tools/duo/daybreak-texture-check.py --popping recording.mp4
Requires ffmpeg on PATH; inspects every frame for rear-wall disappearance.

Gameplay check: stand inside Montmart Bistro facing the double entrance doors;
keep the front wall centered, with no camera movement, and record 20 seconds.
Run: python tools/duo/daybreak-texture-check.py --gameplay-popping recording.mp4
"""

import subprocess
import sys
from PIL import Image


def check(path):
    with Image.open(path) as image:
        assert image.size == (1920, 1080), "Use an unscaled Thor main-screen capture"
        image = image.convert("RGB")
        # ponytail: fixed prologue camera; use scene goldens if more cameras need coverage.
        for box in ((0, 330, 110, 650), (1690, 480, 1900, 630)):
            pixels = list(image.crop(box).get_flattened_data())
            corrupt = sum((g > r + 22 and g > b + 22) or
                          (r > g + 22 and b > g + 22) for r, g, b in pixels)
            wood = sum(r >= g + 4 and g >= b + 4 for r, g, b in pixels)
            assert corrupt / len(pixels) < 0.01, f"Rainbow window texture in {box}"
            assert wood / len(pixels) > 0.25, f"Expected café window material in {box}"
    print("PASS: café window materials have no rainbow corruption")


def check_popping(path, gameplay=False):
    # ponytail: fixed café cameras; motion-aligned comparisons if cameras move.
    crop, size, minimum_luma = (("1000:190:320:80", "250:48", 60) if gameplay else
                               ("680:200:20:60", "170:50", 50))
    decoded = subprocess.run(
        ["ffmpeg", "-nostdin", "-v", "error", "-threads", "1", "-filter_threads", "1",
         "-i", path, "-vf", f"crop={crop},scale={size}", "-fps_mode", "passthrough",
         "-f", "rawvideo", "-pix_fmt", "gray", "pipe:1"],
        capture_output=True, check=True).stdout
    frame_size = 250 * 48 if gameplay else 170 * 50
    assert len(decoded) >= 300 * frame_size, "Record at least 15 seconds of the paused scene"
    assert len(decoded) % frame_size == 0, "Incomplete decoded frame"
    worst = 0.0
    previous = decoded[:frame_size]
    for offset in range(0, len(decoded), frame_size):
        frame = decoded[offset:offset + frame_size]
        worst = max(worst, sum(abs(a - b) for a, b in zip(previous, frame)) / frame_size)
        previous = frame
        assert sum(frame) / frame_size > minimum_luma, "Rear wall is missing or dark in a captured frame"
    assert worst < 10, f"Rear wall pops between consecutive frames (mean luma change {worst:.1f})"
    print(f"PASS: rear wall remains present across {len(decoded) // frame_size} frames")


if __name__ == "__main__":
    if sys.argv[1] in ("--popping", "--gameplay-popping"):
        check_popping(sys.argv[2], sys.argv[1] == "--gameplay-popping")
    else:
        check(sys.argv[1])
