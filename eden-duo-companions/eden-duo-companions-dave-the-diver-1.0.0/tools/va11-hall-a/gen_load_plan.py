#!/usr/bin/env python3
"""Generate VAllhalla's guest load plan for VA-11 Hall-A (base game v0, main build 32D80C9A...).

Lets the module make the game play its own sounds (no audio ships with the package): the ice in
the shaker while the player shakes it by hand, and a click when an error window is closed.

It patches two `bl`s in GameMaker's bytecode interpreter: right after the VM calls a built-in
function (main+0x2690C and main+0x26C0C, `blr x8`, two variants of that call) it calls a tiny
helper (main+0x17160, no arguments). Both `bl`s are pointed at a stub in the zero padding after .text; the stub serves at most one request from the
mailbox, then continues to the original callee. It runs many times per frame, on the game's own
thread, between two built-in calls.

  mailbox +0x00 u32 seq        (module) bumped after the request below is written
          +0x04 u32 taken      (stub)   seq the stub has started
          +0x08 u32 heartbeat  (stub)   +1 every pass
          +0x0C u32 done       (stub)   seq whose result is stored
          +0x10 u64 fn         (module) guest address to call, 0 = cancelled
          +0x18 u64 x0..x4     (module) arguments
          +0x40 u64 result     (stub)   x0 after the call
          +0x80 ...            (module) scratch (RValue arguments and result)

The functions it is asked to call (found 15 Oct 2026 by disassembly):
  main+0x1D9520  audio_play_sound(sound, priority, loop)   registered at main+0x1DBF98
  main+0x1D9DD0  audio_sound_gain(sound, gain, time_ms)    registered at main+0x1DC270
both GameMaker built-ins: (RValue& result, CInstance* self, CInstance* other, int argc, RValue* args).

usage: tools/va11-hall-a/gen_load_plan.py --verify <exefs main NSO>   (checks layout, hook and cave bytes)
"""
import argparse
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PKG = ROOT / "packages/VA11HallA/dualscreen"
TITLE = "010086500D3C8000"
BUILD = "32D80C9AFC27E2EFEBAA048B1E6A120C" + "0" * 32
SEGMENTS = [(0x0, 0x2C2460), (0x2C3000, 0x141EDC), (0x405000, 0x6EF1E0)]
HOOKS, CALLEE = (0x26938, 0x26C20), 0x17160  # 0x26C20 alone never ran (heartbeat 0)
CAVE = 0x2C2470  # .text ends at 0x2C2460; .rodata starts at 0x2C3000 (zero padding, executable)

STUB_ASM = """
    adrp x9, 0
    add  x9, x9, #0
    ldr  w10, [x9, #8]
    add  w10, w10, #1
    str  w10, [x9, #8]
    ldr  w10, [x9]
    ldr  w11, [x9, #4]
    cmp  w10, w11
    b.eq tail
    dmb  ishld
    stp  x29, x30, [sp, #-0x20]!
    mov  x29, sp
    stp  x19, x20, [sp, #0x10]
    mov  x19, x9
    mov  w20, w10
    str  w20, [x19, #4]
    ldr  x16, [x19, #0x10]
    cbz  x16, skip
    ldp  x0, x1, [x19, #0x18]
    ldp  x2, x3, [x19, #0x28]
    ldr  x4, [x19, #0x38]
    blr  x16
    str  x0, [x19, #0x40]
skip:
    dmb  ish
    str  w20, [x19, #0xc]
    ldp  x19, x20, [sp, #0x10]
    ldp  x29, x30, [sp], #0x20
tail:
    nop
"""


def check(cond, msg):
    if not cond:
        raise SystemExit(f"verify failed: {msg}")


def branch(op, source, target):
    delta = target - source
    check(delta % 4 == 0 and -(1 << 27) <= delta < (1 << 27), f"branch {source:#x} -> {target:#x} out of range")
    return op | ((delta // 4) & 0x3FFFFFF)


def stub_bytes():
    from keystone import Ks, KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN
    code, _ = Ks(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN).asm(STUB_ASM, CAVE)
    w = list(struct.unpack(f"<{len(code) // 4}I", bytes(code)))
    w[0], w[1] = 0x90000009, 0x91000129  # adrp/add x9, mailbox: the loader fills in the address
    w[-1] = branch(0x14000000, CAVE + 4 * (len(w) - 1), CALLEE)  # tail: b callee
    return struct.pack(f"<{len(w)}I", *w)


def make_plan(bss):
    payload = stub_bytes()
    check(CAVE + len(payload) <= SEGMENTS[1][0], "stub overruns the padding before .rodata")
    return {
        "format": 1, "title_id": TITLE, "build_id": BUILD, "module": "main",
        "layout": {"segments": [{"location": loc, "size": size} for loc, size in SEGMENTS], "bss_size": bss},
        "mailbox_size": 0x1000,
        "patches": [{"offset": h, "expected": struct.pack("<I", branch(0x94000000, h, CALLEE)).hex(),
                     "replacement": struct.pack("<I", branch(0x94000000, h, CAVE)).hex()} for h in HOOKS],
        "payloads": [{"offset": CAVE, "expected": bytes(len(payload)).hex(), "bytes": payload.hex(),
                      "relocations": [
                          {"offset": 0, "kind": "aarch64_adrp", "target": "mailbox", "addend": 0},
                          {"offset": 4, "kind": "aarch64_add_lo12", "target": "mailbox", "addend": 0}]}]}


def read_nso(path):
    import lz4.block
    d = pathlib.Path(path).read_bytes()
    check(d[:4] == b"NSO0", "not an NSO")
    flags = struct.unpack_from("<I", d, 0xC)[0]
    segs = [struct.unpack_from("<III", d, 0x10 + i * 0x10) for i in range(3)]
    csz = struct.unpack_from("<III", d, 0x60)
    check(d[0x40:0x60].hex().upper() == BUILD, "build id")
    check([(s[1], s[2]) for s in segs] == SEGMENTS, "segments")
    text = d[segs[0][0]:segs[0][0] + csz[0]]
    if flags & 1:
        text = lz4.block.decompress(text, uncompressed_size=segs[0][2])
    text += bytes(SEGMENTS[1][0] - len(text))  # page padding up to .rodata
    return text, struct.unpack_from("<I", d, 0x3C)[0]


def main():
    ap = argparse.ArgumentParser(description="Write VAllhalla's guest load plan, checked against main.")
    ap.add_argument("--verify", metavar="NSO", required=True, help="the game's exefs main (base v0)")
    args = ap.parse_args()
    text, bss = read_nso(args.verify)
    plan = make_plan(bss)
    for w in plan["patches"] + plan["payloads"]:
        exp = bytes.fromhex(w["expected"])
        check(text[w["offset"]:w["offset"] + len(exp)] == exp, f"bytes at {w['offset']:#x}")
    out = PKG / "guest" / BUILD / "load-plan.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan, indent=1) + "\n")
    print("verified against", args.verify, "- wrote", out.relative_to(ROOT), f"({len(stub_bytes())} byte stub)")


if __name__ == "__main__":
    main()
