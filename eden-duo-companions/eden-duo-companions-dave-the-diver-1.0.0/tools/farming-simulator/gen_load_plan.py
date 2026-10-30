#!/usr/bin/env python3
"""gen_load_plan.py [--verify <main NSO | decompressed image>] - Farming Simulator Nintendo Switch Edition
guest load plan (0100D04004176000, main build 7D0651174A65DD84; the v0 and v65536 mains are identical).

Instant game calls (Garage Enter / Reset, Map Visit / Tag place) run the game's own Lua on the game's
main thread. The plan (format 1, no extra code pages) patches the main loop's `bl beginAsyncGC` at
main+0x8440 to a stub in the zero padding after .text (main+0x3577B8 .. 0x358000). At that point
x0 = ScriptSystem* and the main thread owns the idle Lua state: no Lua frame is active and the Giants
"Lua GC Thread" is parked until beginAsyncGC releases it (lua_symbols.py proves every address used).

Once per frame the stub bumps a heartbeat and serves at most one request, then continues to
beginAsyncGC with x0 restored:

  mailbox +0x00 u32 seq        (module) bumped after the request below is written
          +0x04 u32 taken      (stub)   seq the stub has started (a request never runs twice)
          +0x08 u32 heartbeat  (stub)   +1 every frame
          +0x0C u32 done       (stub)   seq whose outcome is stored
          +0x10 u32 length     (module) Lua chunk bytes, 1..0xF00 (0 = cancelled)
          +0x14 u32 status     (stub)   0 ran, 1 state unavailable (no L / async GC owns it),
                                        2 load error, 3 runtime error, 4 bad length
          +0x18 i64 result     (stub)   lua_tointeger of the chunk's first result
          +0x20 char[0xE0]     (stub)   lua_tolstring of the second result / the error message
          +0x100 char[0xF00]   (module) the chunk (Lua source)

The helper: if ScriptSystem+0x218 (yielded) is clear and L = *ScriptSystem is set:
top = lua_gettop(L); luaL_loadbuffer(L, chunk, length, NULL) and lua_pcall(L, 0, 2, 0) (both protected,
so a Lua error can never unwind through the stub); copy the results; lua_settop(L, top).

usage: gen_load_plan.py [--verify NSO]   (checks layout, the hook, the cave and every callee against the
NSO or the decompressed image)
"""
import argparse
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
PKG = ROOT / "packages/FarmingSimulatorNintendoSwitchEdition/dualscreen"
TITLE = "0100D04004176000"
BUILD = "7D0651174A65DD848CB60BE3304FD629" + "0" * 32
SEGMENTS = [(0x0, 0x3577B8), (0x358000, 0xF40B4), (0x44D000, 0x1ACB8)]
BSS = 0x8B0348
HOOK, BEGIN_ASYNC_GC = 0x8440, 0x3A82C   # main loop: bl get_holder; bl deref; bl beginAsyncGC
CAVE = 0x3577B8                          # .text ends here; .rodata starts at 0x358000
MAILBOX_SIZE = 0x1000

LUA_GETTOP = 0x1CECC0
LUA_SETTOP = 0x1CECD0
LUA_TOINTEGER = 0x1D028C
LUA_TOLSTRING = 0x1D0768
LUA_PCALL = 0x1D3FA4
LUAL_LOADBUFFER = 0x1D5264

# llvm-mc -triple=aarch64 of the stub; zero words are filled by fixups().
STUB = [
    0x90000009,  # 000 adrp x9, mailbox                 (loader relocation)
    0x91000129,  # 004 add  x9, x9, :lo12:mailbox       (loader relocation)
    0xB940092A,  # 008 ldr  w10, [x9, #8]
    0x1100054A,  # 00c add  w10, w10, #1
    0xB900092A,  # 010 str  w10, [x9, #8]               heartbeat
    0xB940012A,  # 014 ldr  w10, [x9]                   seq
    0xB940052B,  # 018 ldr  w11, [x9, #4]               taken
    0x6B0B015F,  # 01c cmp  w10, w11
    0x540008E0,  # 020 b.eq tail
    0xD50339BF,  # 024 dmb  ishld                       read the request after seq
    0xA9BB7BFD,  # 028 stp  x29, x30, [sp, #-0x50]!
    0x910003FD,  # 02c mov  x29, sp
    0xA90153F3,  # 030 stp  x19, x20, [sp, #0x10]
    0xA9025BF5,  # 034 stp  x21, x22, [sp, #0x20]
    0xA90363F7,  # 038 stp  x23, x24, [sp, #0x30]
    0xF90023E0,  # 03c str  x0, [sp, #0x40]             ScriptSystem*, beginAsyncGC's argument
    0xAA0903F3,  # 040 mov  x19, x9
    0xAA0003F4,  # 044 mov  x20, x0
    0x2A0A03F5,  # 048 mov  w21, w10
    0xB9000675,  # 04c str  w21, [x19, #4]              taken = seq
    0xF9000E7F,  # 050 str  xzr, [x19, #0x18]           result = 0
    0x3900827F,  # 054 strb wzr, [x19, #0x20]           text = ""
    0x52800038,  # 058 mov  w24, #1                     status: state unavailable
    0xB4000614,  # 05c cbz  x20, finish
    0x39486288,  # 060 ldrb w8, [x20, #0x218]           yielded: the async GC thread owns L
    0x350005C8,  # 064 cbnz w8, finish
    0xF9400296,  # 068 ldr  x22, [x20]                  L
    0xB4000596,  # 06c cbz  x22, finish
    0x52800098,  # 070 mov  w24, #4                     status: bad length
    0xB9401268,  # 074 ldr  w8, [x19, #0x10]
    0x51000509,  # 078 sub  w9, w8, #1
    0x713BFD3F,  # 07c cmp  w9, #0xeff                  1..0xF00
    0x540004E2,  # 080 b.hs finish
    0xAA1603E0,  # 084 mov  x0, x22
    0,           # 088 bl   lua_gettop
    0x2A0003F7,  # 08c mov  w23, w0
    0xAA1603E0,  # 090 mov  x0, x22
    0x91040261,  # 094 add  x1, x19, #0x100             chunk
    0xB9401262,  # 098 ldr  w2, [x19, #0x10]            length
    0xAA1F03E3,  # 09c mov  x3, xzr                     chunkname "?"
    0,           # 0a0 bl   luaL_loadbuffer
    0x52800058,  # 0a4 mov  w24, #2                     status: load error
    0x350001A0,  # 0a8 cbnz w0, text
    0xAA1603E0,  # 0ac mov  x0, x22
    0x2A1F03E1,  # 0b0 mov  w1, wzr                     nargs
    0x52800042,  # 0b4 mov  w2, #2                      nresults
    0x2A1F03E3,  # 0b8 mov  w3, wzr                     no errfunc
    0,           # 0bc bl   lua_pcall
    0x52800078,  # 0c0 mov  w24, #3                     status: runtime error
    0x350000C0,  # 0c4 cbnz w0, text
    0xAA1603E0,  # 0c8 mov  x0, x22
    0x12800021,  # 0cc mov  w1, #-2
    0,           # 0d0 bl   lua_tointeger
    0xF9000E60,  # 0d4 str  x0, [x19, #0x18]            result
    0x2A1F03F8,  # 0d8 mov  w24, wzr                    status: ran
    0xAA1603E0,  # 0dc text: mov x0, x22
    0x12800001,  # 0e0 mov  w1, #-1
    0xAA1F03E2,  # 0e4 mov  x2, xzr
    0,           # 0e8 bl   lua_tolstring               NULL unless a string or number
    0xB4000120,  # 0ec cbz  x0, restore
    0x91008269,  # 0f0 add  x9, x19, #0x20
    0x52801BEA,  # 0f4 mov  w10, #0xdf
    0x3840140B,  # 0f8 copy: ldrb w11, [x0], #1
    0x3400008B,  # 0fc cbz  w11, terminate
    0x3800152B,  # 100 strb w11, [x9], #1
    0x7100054A,  # 104 subs w10, w10, #1
    0x54FFFF81,  # 108 b.ne copy
    0x3900013F,  # 10c terminate: strb wzr, [x9]
    0xAA1603E0,  # 110 restore: mov x0, x22
    0x2A1703E1,  # 114 mov  w1, w23
    0,           # 118 bl   lua_settop                  the Lua stack exactly as found
    0xB9001678,  # 11c finish: str w24, [x19, #0x14]    status
    0xD5033BBF,  # 120 dmb  ish
    0xB9000E75,  # 124 str  w21, [x19, #0xc]            done = seq
    0xF94023E0,  # 128 ldr  x0, [sp, #0x40]
    0xA94363F7,  # 12c ldp  x23, x24, [sp, #0x30]
    0xA9425BF5,  # 130 ldp  x21, x22, [sp, #0x20]
    0xA94153F3,  # 134 ldp  x19, x20, [sp, #0x10]
    0xA8C57BFD,  # 138 ldp  x29, x30, [sp], #0x50
    0,           # 13c tail: b beginAsyncGC
]
CALLS = [(0x088, LUA_GETTOP), (0x0A0, LUAL_LOADBUFFER), (0x0BC, LUA_PCALL), (0x0D0, LUA_TOINTEGER),
         (0x0E8, LUA_TOLSTRING), (0x118, LUA_SETTOP)]
TAIL = 0x13C


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
    for off, fn in CALLS:
        w[off // 4] = branch(0x94000000, CAVE + off, fn)
    w[TAIL // 4] = branch(0x14000000, CAVE + TAIL, BEGIN_ASYNC_GC)
    check(all(w), "an unfilled stub word")
    return struct.pack(f"<{len(w)}I", *w)


def make_plan():
    payload = fixups()
    check(CAVE + len(payload) <= SEGMENTS[1][0], "stub overruns the padding before .rodata")
    return {
        "format": 1, "title_id": TITLE, "build_id": BUILD, "module": "main",
        "layout": {"segments": [{"location": loc, "size": size} for loc, size in SEGMENTS], "bss_size": BSS},
        "mailbox_size": MAILBOX_SIZE,
        "patches": [{"offset": HOOK, "expected": struct.pack("<I", branch(0x94000000, HOOK, BEGIN_ASYNC_GC)).hex(),
                     "replacement": struct.pack("<I", branch(0x94000000, HOOK, CAVE)).hex()}],
        "payloads": [{"offset": CAVE, "expected": bytes(len(payload)).hex(), "bytes": payload.hex(),
                      "relocations": [
                          {"offset": 0, "kind": "aarch64_adrp", "target": "mailbox", "addend": 0},
                          {"offset": 4, "kind": "aarch64_add_lo12", "target": "mailbox", "addend": 0}]}]}


def image_of(path):
    d = pathlib.Path(path).read_bytes()
    if d[:4] != b"NSO0":
        return d  # the decompressed image (research static-main/main.reloc.bin)
    import lz4.block
    flags = struct.unpack_from("<I", d, 0xC)[0]
    segs = [struct.unpack_from("<III", d, 0x10 + i * 0x10) for i in range(3)]
    csz = struct.unpack_from("<III", d, 0x60)
    check(d[0x40:0x60].hex().upper() == BUILD, "build id")
    check([(s[1], s[2]) for s in segs] == SEGMENTS, "segments")
    check(struct.unpack_from("<I", d, 0x3C)[0] == BSS, "bss")
    out = bytearray(SEGMENTS[2][0] + SEGMENTS[2][1])
    for i, (foff, moff, size) in enumerate(segs):
        blob = d[foff:foff + csz[i]]
        if flags & (1 << i):
            blob = lz4.block.decompress(blob, uncompressed_size=size)
        out[moff:moff + size] = blob
    return bytes(out)


def verify(path, plan):
    image = image_of(path)
    u32 = lambda off: struct.unpack_from("<I", image, off)[0]
    for w in plan["patches"] + plan["payloads"]:
        exp = bytes.fromhex(w["expected"])
        check(image[w["offset"]:w["offset"] + len(exp)] == exp, f"expected bytes at {w['offset']:#x}")
    # The hook: x0 = *get_script_holder() right before it (lua_symbols.py proves the holder chain).
    check(u32(HOOK - 8) == branch(0x94000000, HOOK - 8, 0x3C760) and
          u32(HOOK - 4) == branch(0x94000000, HOOK - 4, 0x3C834), "hook context (get holder, deref)")
    # beginAsyncGC: strb #1 -> +0x218, then the +0x219 check.
    check(u32(BEGIN_ASYNC_GC + 4) == 0x39086008 and u32(BEGIN_ASYNC_GC + 8) == 0x39486408, "beginAsyncGC body")
    # Callees: the exact first words lua_symbols.py's checks pin.
    check(image[LUA_GETTOP:LUA_GETTOP + 16] == bytes.fromhex("082442a9280108cb00fd43d3c0035fd6"), "lua_gettop")
    check(u32(LUA_SETTOP) == 0xF81D0FF5 and u32(LUA_SETTOP + 0x14) == 0x37F803A1, "lua_settop")
    check(u32(LUA_PCALL + 0x14) == 0x39430674, "lua_pcall (hookmask byte)")
    check(u32(LUAL_LOADBUFFER + 0x14) == 0x91091021 and
          u32(LUAL_LOADBUFFER + 0x20) == branch(0x94000000, LUAL_LOADBUFFER + 0x20, 0x1D46BC), "luaL_loadbuffer")
    check(u32(LUA_TOINTEGER) == 0x7100043F and u32(LUA_TOLSTRING) == 0xD10143FF, "lua_tointeger / lua_tolstring entry")


def main():
    ap = argparse.ArgumentParser(description="Write the Farming Simulator guest load plan (and optionally check it).")
    ap.add_argument("--verify", metavar="NSO", help="main NSO (or decompressed image) to check the plan against")
    args = ap.parse_args()
    plan = make_plan()
    if args.verify:
        verify(args.verify, plan)
        print("verified against", args.verify)
    out = PKG / "guest" / BUILD / "load-plan.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(plan, indent=1) + "\n")
    print("wrote", out.relative_to(ROOT))


if __name__ == "__main__":
    main()
