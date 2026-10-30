#!/usr/bin/env python3
"""gen_load_plan.py [--verify <main.img>] - Diablo III: Eternal Collection guest load plan
(2.7.7.92380 only; 2.7.6 has no plan and is refused by the runtime's own build-id match).

Gear Equip and Unequip call the game's own client item-placement entry on the game thread, with
no native menu and no input: a `bl` inside the game thread's own top-level loop (runs every loop
iteration regardless of menu state or combat - confirmed live with a breakpoint during plain idle
standing) is patched to a stub in the zero padding after .text. Once per iteration the stub serves
at most one request from the mailbox, then continues to the original callee with every
argument/result register restored:

  mailbox +0x00 u32 seq        (module) bumped after the request below is written
          +0x04 u32 taken      (stub)   seq the stub has started (a request never runs twice)
          +0x08 u32 heartbeat  (stub)   +1 every frame
          +0x0C u32 done       (stub)   seq whose result is stored
          +0x10 u64 item_acd   (module) ACD* of the item to place; 0 = cancelled
          +0x18 u32 dest_container (module) destination container's ACD id (the hero's own, for
                                    both an equip slot and the backpack)
          +0x1C u32 dest_slot  (module) 0 = backpack, 1..13 = equip slot
          +0x20 u32 dest_x     (module) backpack cell x (0 for an equip slot)
          +0x24 u32 dest_y     (module) backpack cell y (0 for an equip slot)
          +0x28 u64 result     (stub)   the call's x0 return value
          +0x80 ...0xF0        (stub)   scratch: the 0x70-byte D3MoveRequest the stub builds fresh
                                every call ({u32 type=1} at +0, destination fields at +0x60..0x6c,
                                everything else zeroed) before calling the game's own function

The stub calls main+0x308FD0 (D3_PlaceItem: `u64 (ItemACD* item, D3MoveRequest* req, u32 w2=0,
u32 w3=0, u32 w4=1)`), the exact function the native console inventory ring/list calls on A
(equip) and on the implicit "move the displaced item to a free backpack cell" step of a swap
(reverse-engineered live: a write watchpoint on an item's own ACD+0xA8/0xAC, backtraced through
the x29/x30 frame chain to this function; its own call site and argument registers were then
confirmed with a software breakpoint at its entry across two independent native actions - an
equip-with-eviction and the native "quick re-equip" (StickL) - both decoding to the same
{type:1, dest_container, dest_slot, dest_x, dest_y} shape, dest_container always the hero's own
ACD id). This keeps every game-side validation (legal slot, backpack space, compatibility) exactly
as the native menu does (verified with breakpoints on the native equip and swap paths).

Drop is not implemented this way: the native X-drop in the list reaches a different, heavier
function (building a much larger request from a template via a separate "release" helper) that
was not fully isolated in the time available; Drop stays on the native-menu drive
(d3_gear_drive.{h,cpp}) to avoid any risk of item loss from an incompletely understood call.

usage: gen_load_plan.py [--verify main.img]   (checks layout, the hook and the cave bytes, plus
the D3_PlaceItem/LocalPlayerGet prologues, against the 2.7.7 image)
"""
import argparse
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PKG = ROOT / "packages/DiabloIII/dualscreen"
TITLE = "01001B300B9BE000"
BUILD = "2607A74F5DF7754CC0357B5DF7E496931355D8CA" + "0" * 24
SEGMENTS = [(0x0, 0xC72B90), (0xC73000, 0x4249C4), (0x1098000, 0xC4A14)]
BSS = 0x9495EC
# The game thread's own top-level loop (`while (*flag != 2) { bl 0x667ab0; bl 0x4C84F0; }`, found
# at the bottom of the equip/unequip call stack's live backtrace, main+0x968 pinned by a software
# breakpoint there firing during plain idle standing with no menu and no combat; main+0x4C8908, the
# first hook site tried - a `bl LocalPlayerGet` inside a per-player update - looked equally
# promising from the same backtrace but a breakpoint on its cave stub never fired during idle: it is
# gated behind a per-actor event flag, not unconditional, and was dropped before shipping). Hooking
# `bl main+0x4C84F0` here fires every loop iteration unconditionally, confirmed live at a roughly
# steady ~38 ms cadence with the gdb stub attached (interactive, so slower than unattended 60 Hz;
# still far under any input-to-result latency that would read as non-instant).
HOOK, CALLEE = 0x964, 0x4C84F0
PLACE_ITEM = 0x308FD0  # D3_PlaceItem(ItemACD* item, D3MoveRequest* req, u32 w2, u32 w3, u32 w4)
CAVE = 0xC72B90  # .text ends here; .rodata starts at 0xC73000 (0x470 B of zero padding, executable)
MAILBOX_SIZE = 0x1000

# llvm-mc -triple=aarch64 + ld.lld -Ttext=0xC72B90 of the stub (tools/d3/d3_load_plan_stub.s);
# words 0/1 (adrp/add x9, mailbox) are loader relocations, everything else is already resolved
# module-relative (bl/b are PC-relative, so a uniform load_base shift never changes the encoding).
STUB = [
    0xD0009C69,  # 000 adrp x9, mailbox                 (loader relocation)
    0x91000129,  # 004 add  x9, x9, :lo12:mailbox       (loader relocation)
    0xB940092A,  # 008 ldr  w10, [x9, #8]
    0x1100054A,  # 00c add  w10, w10, #1
    0xB900092A,  # 010 str  w10, [x9, #8]               heartbeat
    0xB940012A,  # 014 ldr  w10, [x9]                   seq
    0xB940052B,  # 018 ldr  w11, [x9, #4]                taken
    0x6B0B015F,  # 01c cmp  w10, w11
    0x54000660,  # 020 b.eq tail
    0xD50339BF,  # 024 dmb  ishld                       read the request after seq
    0xA9B47BFD,  # 028 stp  x29, x30, [sp, #-0xc0]!
    0x910003FD,  # 02c mov  x29, sp
    0xA90107E0,  # 030 stp  x0, x1, [sp, #0x10]          the callee's arguments
    0xA9020FE2,  # 034 stp  x2, x3, [sp, #0x20]
    0xA90317E4,  # 038 stp  x4, x5, [sp, #0x30]
    0xA9041FE6,  # 03c stp  x6, x7, [sp, #0x40]
    0xA9054FE8,  # 040 stp  x8, x19, [sp, #0x50]
    0xF90033F4,  # 044 str  x20, [sp, #0x60]
    0xAD0387E0,  # 048 stp  q0, q1, [sp, #0x70]
    0xAD048FE2,  # 04c stp  q2, q3, [sp, #0x90]
    0xAA0903F3,  # 050 mov  x19, x9
    0x2A0A03F4,  # 054 mov  w20, w10
    0xB9000674,  # 058 str  w20, [x19, #4]               taken = seq
    0xF9400A60,  # 05c ldr  x0, [x19, #0x10]             item_acd
    0xB4000300,  # 060 cbz  x0, 1f                       0 = no request
    0x91020261,  # 064 add  x1, x19, #0x80               scratch D3MoveRequest
    0xA9007C3F,  # 068 stp  xzr, xzr, [x1, #0x00]        zero the 0x70-byte request
    0xA9017C3F,  # 06c stp  xzr, xzr, [x1, #0x10]
    0xA9027C3F,  # 070 stp  xzr, xzr, [x1, #0x20]
    0xA9037C3F,  # 074 stp  xzr, xzr, [x1, #0x30]
    0xA9047C3F,  # 078 stp  xzr, xzr, [x1, #0x40]
    0xA9057C3F,  # 07c stp  xzr, xzr, [x1, #0x50]
    0xA9067C3F,  # 080 stp  xzr, xzr, [x1, #0x60]
    0x52800028,  # 084 mov  w8, #1
    0xB9000028,  # 088 str  w8, [x1]                     type = 1 (container/slot move)
    0xB9401A68,  # 08c ldr  w8, [x19, #0x18]              dest_container
    0xB9006028,  # 090 str  w8, [x1, #0x60]
    0xB9401E68,  # 094 ldr  w8, [x19, #0x1c]              dest_slot
    0xB9006428,  # 098 str  w8, [x1, #0x64]
    0xB9402268,  # 09c ldr  w8, [x19, #0x20]              dest_x
    0xB9006828,  # 0a0 str  w8, [x1, #0x68]
    0xB9402668,  # 0a4 ldr  w8, [x19, #0x24]              dest_y
    0xB9006C28,  # 0a8 str  w8, [x1, #0x6c]
    0x52800002,  # 0ac mov  w2, #0
    0x52800003,  # 0b0 mov  w3, #0
    0x52800024,  # 0b4 mov  w4, #1
    0,           # 0b8 bl   D3_PlaceItem
    0xF9001660,  # 0bc str  x0, [x19, #0x28]              result
    0xD5033BBF,  # 0c0 1: dmb ish
    0xB9000E74,  # 0c4 str  w20, [x19, #0xc]              done = seq
    0xAD448FE2,  # 0c8 ldp  q2, q3, [sp, #0x90]
    0xAD4387E0,  # 0cc ldp  q0, q1, [sp, #0x70]
    0xF94033F4,  # 0d0 ldr  x20, [sp, #0x60]
    0xA9454FE8,  # 0d4 ldp  x8, x19, [sp, #0x50]
    0xA9441FE6,  # 0d8 ldp  x6, x7, [sp, #0x40]
    0xA94317E4,  # 0dc ldp  x4, x5, [sp, #0x30]
    0xA9420FE2,  # 0e0 ldp  x2, x3, [sp, #0x20]
    0xA94107E0,  # 0e4 ldp  x0, x1, [sp, #0x10]
    0xA8CC7BFD,  # 0e8 ldp  x29, x30, [sp], #0xc0
    0,           # 0ec tail: b CALLEE
]


def check(cond, msg):
    """A failed check stops the script (not an assert, so python -O still checks)."""
    if not cond:
        raise SystemExit(f"verify failed: {msg}")


def branch(op, source, target):
    delta = target - source
    check(delta % 4 == 0 and -(1 << 27) <= delta < (1 << 27), f"branch {source:#x} -> {target:#x} out of range")
    return op | ((delta // 4) & 0x3FFFFFF)


def fixups():
    w = list(STUB)
    at = lambda off: CAVE + off
    w[0xB8 // 4] = branch(0x94000000, at(0xB8), PLACE_ITEM)
    w[0xEC // 4] = branch(0x14000000, at(0xEC), CALLEE)
    check(all(w), "an unfilled stub word")
    return struct.pack(f"<{len(w)}I", *w)


def make_plan():
    payload = fixups()
    check(CAVE + len(payload) <= SEGMENTS[1][0], "stub overruns the padding before .rodata")
    return {
        "format": 1, "title_id": TITLE, "build_id": BUILD, "module": "main",
        "layout": {"segments": [{"location": loc, "size": size} for loc, size in SEGMENTS], "bss_size": BSS},
        "mailbox_size": MAILBOX_SIZE,
        "patches": [{"offset": HOOK, "expected": struct.pack("<I", branch(0x94000000, HOOK, CALLEE)).hex(),
                     "replacement": struct.pack("<I", branch(0x94000000, HOOK, CAVE)).hex()}],
        "payloads": [{"offset": CAVE, "expected": bytes(len(payload)).hex(), "bytes": payload.hex(),
                      "relocations": [
                          {"offset": 0, "kind": "aarch64_adrp", "target": "mailbox", "addend": 0},
                          {"offset": 4, "kind": "aarch64_add_lo12", "target": "mailbox", "addend": 0}]}]}


# D3_PlaceItem's prologue (sub sp,sp,#0x70; str d8,[sp,#0x20]; str x25,[sp,#0x28]; stp x24,x23,
# [sp,#0x30]) and LocalPlayerGet's first word, checked by --verify so a future build drift fails
# loudly instead of silently mis-hooking.
PLACE_ITEM_PROLOGUE = bytes.fromhex("ffc301d1e81300fdf91700f9f85f03a9")


def verify(path):
    image = pathlib.Path(path).read_bytes()
    check(len(image) >= SEGMENTS[2][0] + SEGMENTS[2][1], "image too short for the declared layout")
    plan = make_plan()
    for w in plan["patches"] + plan["payloads"]:
        exp = bytes.fromhex(w["expected"])
        check(image[w["offset"]:w["offset"] + len(exp)] == exp, f"expected bytes at {w['offset']:#x}")
    # The patches check above already pins CALLEE (it is baked into the hook's "expected" bytes as
    # the branch target); this additionally pins D3_PLACE_ITEM, which no other check touches.
    check(image[PLACE_ITEM:PLACE_ITEM + 16] == PLACE_ITEM_PROLOGUE, "D3_PlaceItem prologue drifted")


def main():
    ap = argparse.ArgumentParser(description="Write the D3 guest load plan (and optionally check it).")
    ap.add_argument("--verify", metavar="IMG", help="main.img (flat, relocated) to check the plan against")
    args = ap.parse_args()
    plan = make_plan()
    if args.verify:
        verify(args.verify)
        print("verified against", args.verify)
    out = PKG / "guest" / BUILD / "load-plan.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan, indent=1) + "\n")
    print("wrote", out.relative_to(ROOT))


if __name__ == "__main__":
    main()
