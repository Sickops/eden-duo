#!/usr/bin/env python3
"""lua_symbols.py <main NSO | decompressed image> [--json OUT] - the LuaJIT C API of Farming Simulator
Nintendo Switch Edition (0100D04004176000, main build 7D0651174A65DD84, the v65536 update's main; the v0
main is byte-identical), named statically and self-checked.

The game embeds LuaJIT 2.1.0-beta2 ("LuaJIT 2.1.0-beta2" / "Lua 5.1" / LUA_PATH ".../luajit-2.1.0-beta2/..."
in .rodata) built for arm64: GC64 (64-bit tagged TValues, tag = itype << 47), LJ_FR2, interpreter only
(no JIT on arm64 in beta2), LJ_DUALNUM (integers carry the LJ_TISNUM tag 0xFFF90000 in the high word),
LUA_NUMBER = double. lj_api.c was compiled as one unit with index2adr inlined into every API function,
so the API sits contiguous in source order (main+0x1CEAD4 lua_status .. main+0x1D4068 lua_cpcall),
followed by lj_load.c (lua_loadx .. luaL_loadstring).

Method (no address below is a guess; every one is pinned by the checks in CHECKS):
  1. Anchor the region by index2adr's inlined pseudo-index ladder: MOVN -9999 / -10000 / -10001 /
     -10002 (LUA_GLOBALSINDEX reads L->env at +0x48, LUA_REGISTRYINDEX is &G->registrytv).
  2. Walk the region in lj_api.c source order and confirm each function by a behaviour only it has:
     lua_gettop is (top-base)>>3; lua_settop fills new slots with memset(..., 0xFF) (nil = all-ones);
     lua_type returns 3 for the number tags and -1 for "none"; lua_tonumber/tointeger call
     lj_strscan_num with STRSCAN_OPT_TONUM (2) / TOINT (1); lua_toboolean is `itype < ~1`;
     lua_pushnumber canonicalises NaN; lua_pushinteger stores the LJ_TISNUM tag; lua_pushstring calls
     strlen then lj_str_new and tags LJ_TSTR; lua_getfield / lua_setfield call strlen, lj_str_new and
     lj_meta_tget / lj_meta_tset, then lj_vm_call for a metamethod; lua_call tail-calls lj_vm_call;
     lua_pcall calls lj_vm_pcall and restores G->hookmask (+0xC1); lua_loadx's default chunk name is
     "?"; luaL_loadbuffer passes reader_string and a null mode to lua_loadx.
  3. Library imports (strlen, memset) are named from the NSO's own JUMP_SLOT relocations.
  4. The engine side: the ScriptSystem holder is main+0x46CB40 (the GOT slot main+0x466008 the
     getter main+0x3C760 returns), the ScriptSystem's first word is the lua_State (its constructor
     stores luaL_newstate's result there), +0x218 is the "yielded" byte (async GC thread owns the
     state) and +0x219 disables the async GC. The main loop brackets its render with
     beginAsyncGC(main+0x3A82C) at main+0x8440 and endAsyncGC(main+0x3A848) at main+0x886C.

The reference shapes were compared against LuaJIT 2.1.0-beta2 (git 3e4a196) lj_api.c / lj_load.c built
with clang -O2/-O3 for aarch64; nothing from that build is needed to run this script.

usage: lua_symbols.py IMAGE [--json OUT]    exit status 1 if any check fails
"""
import argparse
import json
import pathlib
import re
import struct
import sys

try:
    import capstone
except ImportError:  # pragma: no cover
    sys.exit("lua_symbols.py needs python capstone (pip install capstone)")

BUILD = "7D0651174A65DD848CB60BE3304FD62900000000000000000000000000000000"
TEXT_END = 0x3577B8

# name -> (entry, end). End is the next function's entry (index2adr is inlined, so the leaf functions
# have no prologue: the ranges come from the source-order walk, and the checks below confirm them).
API = {
    "lua_status":        (0x1CEAD4, 0x1CEADC),
    "lua_checkstack":    (0x1CEADC, 0x1CEB54),
    "lua_xmove":         (0x1CEC30, 0x1CECC0),
    "lua_gettop":        (0x1CECC0, 0x1CECD0),
    "lua_settop":        (0x1CECD0, 0x1CED80),
    "lua_remove":        (0x1CED80, 0x1CEDE4),
    "lua_insert":        (0x1CEDE4, 0x1CEE50),
    "lua_replace":       (0x1CEE50, 0x1CF058),
    "lua_pushvalue":     (0x1CF058, 0x1CF1B4),
    "lua_type":          (0x1CF1B4, 0x1CF2BC),
    "luaL_checktype":    (0x1CF2BC, 0x1CF304),
    "lua_typename":      (0x1CF414, 0x1CF428),
    "lua_tonumber":      (0x1CFEF4, 0x1D0014),
    "lua_tointeger":     (0x1D028C, 0x1D03D8),
    "lua_toboolean":     (0x1D069C, 0x1D0768),
    "lua_tolstring":     (0x1D0768, 0x1D09D4),
    "lua_pushnil":       (0x1D1534, 0x1D15D8),
    "lua_pushnumber":    (0x1D15D8, 0x1D1690),
    "lua_pushinteger":   (0x1D1690, 0x1D1750),
    "lua_pushstring":    (0x1D1750, 0x1D1844),
    "lua_pushvfstring":  (0x1D1844, 0x1D18AC),
    "lua_pushcclosure":  (0x1D18AC, 0x1D1A5C),
    "lua_pushboolean":   (0x1D1A5C, 0x1D1B0C),
    "lua_gettable":      (0x1D22D4, 0x1D23EC),
    "lua_getfield":      (0x1D23EC, 0x1D25B8),
    "lua_rawget":        (0x1D25B8, 0x1D26A4),
    "lua_rawgeti":       (0x1D26A4, 0x1D28B4),
    "lua_settable":      (0x1D3494, 0x1D35BC),
    "lua_setfield":      (0x1D35BC, 0x1D3718),
    "lua_call":          (0x1D3F5C, 0x1D3FA4),
    "lua_pcall":         (0x1D3FA4, 0x1D4068),
    "lua_cpcall":        (0x1D4068, 0x1D40A4),
    "lua_loadx":         (0x1D46BC, 0x1D47C0),
    "lua_load":          (0x1D5054, 0x1D505C),
    "luaL_loadbufferx":  (0x1D5218, 0x1D5244),
    "reader_string":     (0x1D5244, 0x1D5264),
    "luaL_loadbuffer":   (0x1D5264, 0x1D5294),
    "luaL_loadstring":   (0x1D5294, 0x1D52E4),
}
# LuaJIT internals the API calls (named by the API functions that call them, see CHECKS).
INTERNAL = {
    "lj_str_new": 0x1C7450, "lj_meta_tget": 0x1C9298, "lj_meta_tset": 0x1C9560, "lj_vm_call": 0x1F8754,
    "lj_vm_pcall": 0x1F871C, "lj_vm_cpcall": 0x1F8800, "lj_strscan_num": 0x1D9358, "lj_tab_get": 0x1C7FA0,
    "lj_state_growstack": 0x1CBDC0, "lj_state_realloc": 0x1CBF24, "lj_err_argt": 0x1CF304, "cpparser": 0x1D47C0,
}
# The engine's script system (Giants), see the module docstring.
ENGINE = {
    "script_holder": 0x46CB40,          # bss: ScriptSystem*
    "script_holder_got": 0x466008,      # GOT slot holding &script_holder
    "get_script_holder": 0x3C760,       # returns the GOT slot's value
    "script_system_ctor": 0x380FC,      # stores luaL_newstate() at this+0
    "luaL_newstate": 0x1D6118,
    "begin_async_gc": 0x3A82C,          # this+0x218 = 1; if !this+0x219 signal the GC thread
    "end_async_gc": 0x3A848,            # this+0x218 = 0; if !this+0x219 wait for the GC thread
    "call_function": 0x39DAC,           # ScriptSystem::callFunction: refuses while +0x218, lua_pcall
    "main_loop_begin_gc_call": 0x8440,  # bl begin_async_gc (x0 = ScriptSystem*)
    "main_loop_end_gc_call": 0x886C,    # bl end_async_gc
}


class Image:
    def __init__(self, path):
        raw = pathlib.Path(path).read_bytes()
        self.img = bytearray(self._load(raw))
        self.md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
        self.md.skipdata = True

    @staticmethod
    def _load(d):
        if d[:4] != b"NSO0":
            return d  # an already decompressed image whose offset X is main+X (research main.reloc.bin)
        import lz4.block
        if d[0x40:0x60].hex().upper() != BUILD:
            sys.exit("not the Farming Simulator main this table describes (build id)")
        flags = struct.unpack_from("<I", d, 0xC)[0]
        csz = struct.unpack_from("<III", d, 0x60)
        segs = [struct.unpack_from("<III", d, 0x10 + i * 0x10) for i in range(3)]  # file off, mem off, size
        bss = struct.unpack_from("<I", d, 0x3C)[0]
        out = bytearray(segs[2][1] + segs[2][2] + bss)
        for i, (foff, moff, size) in enumerate(segs):
            blob = d[foff:foff + csz[i]]
            if flags & (1 << i):
                blob = lz4.block.decompress(blob, uncompressed_size=size)
            out[moff:moff + size] = blob
        Image._relocate(out)
        return out

    @staticmethod
    def _relocate(img):
        """Apply R_AARCH64_RELATIVE at base 0, so pointers read the same as in the decompressed image."""
        mod0 = struct.unpack_from("<I", img, 4)[0]
        dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
        tags = {}
        while True:
            tag, val = struct.unpack_from("<qQ", img, dyn)
            if tag == 0:
                break
            tags[tag] = val
            dyn += 16
        rela, size = tags.get(7, 0), tags.get(8, 0)
        for at in range(rela, rela + size, 24):
            off, info, add = struct.unpack_from("<QQq", img, at)
            if info & 0xFFFFFFFF == 1027:
                struct.pack_into("<Q", img, off, add)

    def u32(self, a):
        return struct.unpack_from("<I", self.img, a)[0]

    def u64(self, a):
        return struct.unpack_from("<Q", self.img, a)[0]

    def cstr(self, a, limit=128):
        end = self.img.find(b"\0", a, a + limit)
        return bytes(self.img[a:end]).decode("latin1")

    def dis(self, lo, hi):
        return [(i.address, i.mnemonic, i.op_str) for i in self.md.disasm(bytes(self.img[lo:hi]), lo)]

    def plt_name(self, stub):
        """The import a PLT stub (adrp x16 / ldr x17, [x16, #lo] / add / br x17) jumps through."""
        ins = self.dis(stub, stub + 16)
        if [m for _, m, _ in ins] != ["adrp", "ldr", "add", "br"]:
            return None
        page = int(ins[0][2].split("#")[1], 16)
        slot = page + int(re.search(r"#(0x[0-9a-f]+)", ins[1][2]).group(1), 16)
        mod0 = self.u32(4)
        dyn = mod0 + struct.unpack_from("<i", self.img, mod0 + 4)[0]
        tags = {}
        while True:
            tag, val = struct.unpack_from("<qQ", self.img, dyn)
            if tag == 0:
                break
            tags[tag] = val
            dyn += 16
        jmprel, size, symtab, strtab = tags[23], tags[2], tags[6], tags[5]
        for at in range(jmprel, jmprel + size, 24):
            off, info, _ = struct.unpack_from("<QQq", self.img, at)
            if off == slot:
                name = self.u32(symtab + (info >> 32) * 24)
                return self.cstr(strtab + name)
        return None


class Fn:
    def __init__(self, image, lo, hi):
        self.lo, self.hi = lo, hi
        self.ins = image.dis(lo, hi)
        self.text = [f"{m} {o}".strip() for _, m, o in self.ins]

    def has(self, pattern):
        return any(re.fullmatch(pattern, t) for t in self.text)

    def calls(self, kind="bl"):
        out = []
        for a, m, o in self.ins:
            if m == kind and o.startswith("#"):
                out.append(int(o[1:], 16))
        return out

    def tail(self):
        return [t for t in self.calls("b") if not self.lo <= t < self.hi]


def check(results, name, ok, evidence):
    results.append({"name": name, "ok": bool(ok), "evidence": evidence})


def run_checks(img):
    R = []
    f = {name: Fn(img, lo, hi) for name, (lo, hi) in API.items()}
    I = INTERNAL

    # Library imports, named from the NSO's own JUMP_SLOT relocations.
    strlen = next(t for t in f["lua_pushstring"].calls() if img.plt_name(t))
    memset = next(t for t in f["lua_settop"].calls() if img.plt_name(t))
    check(R, "import strlen", img.plt_name(strlen) == "strlen", f"PLT {strlen:#x} -> {img.plt_name(strlen)}")
    check(R, "import memset", img.plt_name(memset) == "memset", f"PLT {memset:#x} -> {img.plt_name(memset)}")

    # Flavour.
    rodata = bytes(img.img[0x358000:0x44C0B4])
    check(R, "flavour LuaJIT 2.1.0-beta2", b"LuaJIT 2.1.0-beta2\0" in rodata and b"luajit-2.1.0-beta2" in rodata,
          "rodata strings 'LuaJIT 2.1.0-beta2' and the LUA_PATH .../luajit-2.1.0-beta2/...")

    # index2adr inlined everywhere: every index-taking function carries the pseudo-index ladder.
    for name in ("lua_pushvalue", "lua_type", "lua_tonumber", "lua_tointeger", "lua_toboolean",
                 "lua_tolstring", "lua_getfield", "lua_rawgeti", "lua_setfield"):
        fn = f[name]
        ok = all(fn.has(rf"mov w\d+, #{c}") for c in ("-0x270f", "-0x2710", "-0x2711", "-0x2712")) and \
            fn.has(r"ldr x\d+, \[x\d+, #0x48\]")
        check(R, f"{name}: index2adr", ok, "MOVN -9999/-10000/-10001/-10002; LUA_GLOBALSINDEX reads L->env (+0x48)")

    fn = f["lua_status"]
    check(R, "lua_status", fn.text == ["ldrb w0, [x0, #0xb]", "ret"], "return L->status (+0xB)")
    fn = f["lua_checkstack"]
    check(R, "lua_checkstack", fn.has(r"mov w\d+, #0x1f40") and fn.calls() == [I["lj_state_growstack"]],
          "LUAI_MAXCSTACK 8000, then lj_state_growstack")
    fn = f["lua_gettop"]
    check(R, "lua_gettop", fn.text == ["ldp x8, x9, [x0, #0x20]", "sub x8, x9, x8", "lsr x0, x8, #3", "ret"],
          "(L->top - L->base) >> 3 with base +0x20, top +0x28")
    fn = f["lua_settop"]
    check(R, "lua_settop", fn.has(r"tbnz w1, #0x1f, #0x[0-9a-f]+") and fn.has("mov w1, #0xff") and memset in fn.calls(),
          "idx < 0 path; new slots filled with memset(.., 0xFF, ..) = nil (all-ones TValue)")
    fn = f["lua_pushvalue"]
    check(R, "lua_pushvalue", fn.calls()[:1] == [I["lj_state_realloc"]] and fn.has(r"str x9, \[x8\]"),
          "copy *index2adr(idx) to top; incr_top's lj_state_growstack1 inlined down to lj_state_realloc")
    fn = f["lua_type"]
    check(R, "lua_type", fn.has(r"cmn w9, #0xd") and fn.has("mov w0, #3") and fn.has("mov w0, #-1"),
          "number itypes (>= LJ_TISNUM) -> 3 (LUA_TNUMBER); niltv -> -1 (LUA_TNONE); others by nibble table")
    fn = f["luaL_checktype"]
    check(R, "luaL_checktype", fn.calls() == [API["lua_type"][0], I["lj_err_argt"]],
          "lua_type(L, idx) != t -> lj_err_argt")
    fn = f["lua_tonumber"]
    check(R, "lua_tonumber", fn.calls() == [I["lj_strscan_num"]] and fn.has("mov w2, #2") and
          fn.has("fmov d8, xzr") and fn.has(r"scvtf d0, w8"),
          "double result in d0 (LUA_NUMBER = double); int tag -> scvtf; strings via lj_strscan_num(.., STRSCAN_OPT_TONUM=2); else 0.0")
    fn = f["lua_tointeger"]
    check(R, "lua_tointeger", fn.calls() == [I["lj_strscan_num"]] and fn.has("mov w2, #1") and fn.has("fcvtzs x0, d0") and
          fn.has(r"sxtw x0, w9"), "int tag -> sxtw; number -> fcvtzs; strings with STRSCAN_OPT_TOINT=1")
    fn = f["lua_toboolean"]
    i = fn.text.index("cset w0, lo") if "cset w0, lo" in fn.text else 0
    check(R, "lua_toboolean", i and fn.text[i - 1:i + 2] == ["cmn w8, #2", "cset w0, lo", "ret"] and not fn.calls(),
          "tvistruecond: itype < LJ_TISTRUECOND (~1)")
    fn = f["lua_tolstring"]
    check(R, "lua_tolstring", fn.has("cmn w10, #5") and fn.has(r"ldr w8, \[x0, #0x10\]") and fn.has("add x0, x0, #0x18")
          and fn.has(r"str x8, \[x19\]") and I["lj_str_new"] in fn.calls() and fn.has("mov x8, #-0x3000000000000"),
          "LJ_TSTR (itype ~4): *len = GCstr.len (+0x10), returns strdata (+0x18); numbers are formatted, "
          "lj_str_new'd and retagged LJ_TSTR in place")
    fn = f["lua_pushnil"]
    check(R, "lua_pushnil", fn.has("mov x9, #-1") and fn.calls()[0] == I["lj_state_realloc"], "setnilV (all-ones)")
    fn = f["lua_pushnumber"]
    check(R, "lua_pushnumber", fn.has("fcmp d0, d0") and fn.has("mov x9, #-0x8000000000000"),
          "setnumV(d0), NaN canonicalised to 0xFFF8000000000000")
    fn = f["lua_pushinteger"]
    check(R, "lua_pushinteger", fn.has("mov w9, #-0x70000") and fn.has(r"stp w1, w9, \[x8\]") and fn.has("scvtf d0, x1"),
          "LJ_DUALNUM: int32 stored with the LJ_TISNUM tag 0xFFF90000, wider values as double")
    fn = f["lua_pushstring"]
    check(R, "lua_pushstring", fn.calls()[:2] == [strlen, I["lj_str_new"]] and fn.has("mov x9, #-0x3000000000000") and
          fn.has("movk x9, #0x8000, lsl #32") and fn.has(r"cbz x20, #0x[0-9a-f]+"),
          "NULL -> nil; else lj_str_new(L, s, strlen(s)) tagged 0xFFFD8000.. (itype ~4 = LJ_TSTR)")
    fn = f["lua_pushcclosure"]
    check(R, "lua_pushcclosure", fn.has("mov x9, #-0x5000000000000") and fn.has(r"blr x9"),
          "allocates a C closure (g->allocf) and tags 0xFFFB8000.. (itype ~8 = LJ_TFUNC)")
    fn = f["lua_pushboolean"]
    check(R, "lua_pushboolean", fn.has(r"mov x\d+, #-0x1000000000001") or fn.has(r"mov x\d+, #-0x800000000001"),
          "LJ_TFALSE / LJ_TTRUE tags")
    fn = f["lua_gettable"]
    check(R, "lua_gettable", fn.calls() == [I["lj_meta_tget"], I["lj_vm_call"]], "lj_meta_tget, metamethod via lj_vm_call")
    fn = f["lua_getfield"]
    c = fn.calls()
    check(R, "lua_getfield", [x for x in c if x in (strlen, I["lj_str_new"], I["lj_meta_tget"], I["lj_vm_call"])][:4] ==
          [strlen, I["lj_str_new"], I["lj_meta_tget"], I["lj_vm_call"]],
          "lj_str_new(k, strlen(k)) key, lj_meta_tget, metamethod via lj_vm_call")
    fn = f["lua_rawget"]
    check(R, "lua_rawget", fn.calls() == [I["lj_tab_get"]], "lj_tab_get, no metamethods")
    fn = f["lua_rawgeti"]
    check(R, "lua_rawgeti", fn.has(r"ldr x9, \[x8, #0x30\]") and fn.has(r"ldr x8, \[x8, #0x10\]") and
          fn.has("mov w10, #0x18") and not fn.has(r"bl #0x1c9298"),
          "lj_tab_getint inline: array part (asize +0x30, array +0x10), hash nodes of 0x18 bytes")
    fn = f["lua_settable"]
    check(R, "lua_settable", fn.calls() == [I["lj_meta_tset"], I["lj_vm_call"]], "lj_meta_tset, metamethod via lj_vm_call")
    fn = f["lua_setfield"]
    c = fn.calls()
    check(R, "lua_setfield", [x for x in c if x in (strlen, I["lj_str_new"], I["lj_meta_tset"], I["lj_vm_call"])][:4] ==
          [strlen, I["lj_str_new"], I["lj_meta_tset"], I["lj_vm_call"]],
          "lj_str_new(k, strlen(k)) key, lj_meta_tset, metamethod via lj_vm_call")
    fn = f["lua_call"]
    check(R, "lua_call", fn.tail() == [I["lj_vm_call"]] and fn.has("add w2, w2, #1") and fn.has("mov x8, #-1"),
          "api_call_base (shift args, nil frame slot) then tail lj_vm_call(L, base, nresults + 1)")
    fn = f["lua_pcall"]
    check(R, "lua_pcall", fn.calls() == [I["lj_vm_pcall"]] and fn.has(r"ldrb w20, \[x19, #0xc1\]") and
          fn.has(r"strb w8, \[x19, #0xc1\]") and fn.has(r"ldr x11, \[x0, #0x38\]"),
          "errfunc slot offset from L->stack (+0x38), lj_vm_pcall, restore G->hookmask (+0xC1) on error")
    fn = f["lua_cpcall"]
    check(R, "lua_cpcall", fn.calls() == [I["lj_vm_cpcall"]] and fn.has(r"bfxil w20, w8, #0, #4"),
          "lj_vm_cpcall(cpcall); hookmask restore")
    fn = f["lua_loadx"]
    chunk = re.search(r"#(0x[0-9a-f]+)", fn.text[fn.text.index(next(t for t in fn.text if t.startswith("add x8, x8")))]).group(1)
    page = int(re.search(r"#(0x[0-9a-f]+)", next(t for t in fn.text if t.startswith("adrp x8"))).group(1), 16)
    default_name = img.cstr(page + int(chunk, 16))
    check(R, "lua_loadx", fn.calls()[:1] == [I["lj_vm_cpcall"]] and default_name == "?" and
          fn.has(rf"add x3, x3, #{I['cpparser'] & 0xFFF:#x}"),
          f"chunkname ? chunkname : \"{default_name}\"; lj_vm_cpcall(cpparser main+{I['cpparser']:#x}); lj_lex_cleanup inline")
    fn = f["lua_load"]
    check(R, "lua_load", fn.text == ["mov x4, xzr", f"b #{API['lua_loadx'][0]:#x}"], "lua_loadx(.., mode = NULL)")
    fn = f["reader_string"]
    check(R, "reader_string", fn.text == ["ldr x8, [x1, #8]", f"cbz x8, #{fn.lo + 0x18:#x}", "ldr x0, [x1]",
                                          "str x8, [x2]", "str xzr, [x1, #8]", "ret", "mov x0, xzr", "ret"],
          "returns ctx->str once, then NULL")
    for name, mode_null in (("luaL_loadbufferx", False), ("luaL_loadbuffer", True)):
        fn = f[name]
        check(R, name, fn.calls() == [API["lua_loadx"][0]] and fn.has(rf"add x1, x1, #{API['reader_string'][0] & 0xFFF:#x}")
              and fn.has("mov x4, xzr") == mode_null,
              "lua_loadx(L, reader_string, &{buf, size}, name, " + ("NULL)" if mode_null else "mode)"))
    fn = f["luaL_loadstring"]
    check(R, "luaL_loadstring", fn.calls() == [strlen, API["lua_loadx"][0]] and fn.has("mov x3, x19"),
          "luaL_loadbuffer(L, s, strlen(s), s)")

    # Engine anchors.
    E = ENGINE
    check(R, "script holder GOT", img.u64(E["script_holder_got"]) == E["script_holder"],
          f"main+{E['script_holder_got']:#x} = main+{img.u64(E['script_holder_got']):#x}")
    g = Fn(img, E["get_script_holder"], E["get_script_holder"] + 12)
    check(R, "script holder getter", g.text[0].startswith("adrp x0") and g.text[1] == "ldr x0, [x0, #8]",
          "main+0x3C760 returns the GOT slot (the engine then reads ScriptSystem* = *holder)")
    ctor = Fn(img, E["script_system_ctor"], E["script_system_ctor"] + 0x48)
    check(R, "lua_State = *ScriptSystem", E["luaL_newstate"] in ctor.calls() and ctor.has(r"str x0, \[x19\]"),
          "ScriptSystem ctor: this->L = luaL_newstate()")
    b = Fn(img, E["begin_async_gc"], E["begin_async_gc"] + 0x1C)
    e = Fn(img, E["end_async_gc"], E["end_async_gc"] + 0x1C)
    check(R, "begin/end async GC", b.has(r"strb w8, \[x0, #0x218\]") and b.has(r"ldrb w8, \[x0, #0x219\]") and
          e.has(r"strb wzr, \[x0, #0x218\]") and e.has(r"add x0, x0, #0x204"),
          "+0x218 yielded (GC thread steps the state), +0x219 async GC disabled, semaphores +0x1F8 / +0x204")
    cf = Fn(img, E["call_function"], E["call_function"] + 0x2E0)
    check(R, "callFunction guards +0x218", cf.has(r"ldrb w8, \[x19, #0x218\]") and API["lua_pcall"][0] in cf.calls() and
          img.cstr(0x394D9F).startswith("Error: LUA script system called while in yielded state"),
          "refuses (logs 'called while in yielded state') when +0x218 is set; protected lua_pcall")
    for site, callee in ((E["main_loop_begin_gc_call"], E["begin_async_gc"]), (E["main_loop_end_gc_call"], E["end_async_gc"])):
        prev = Fn(img, site - 8, site + 4)
        check(R, f"main loop bl {callee:#x} at {site:#x}", prev.text == ["bl #0x3c760", "bl #0x3c834", f"bl #{callee:#x}"],
              "x0 = *get_script_holder() then the async-GC bracket")
    return R


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image", help="main NSO (exefs/main) or the decompressed, relocated image")
    ap.add_argument("--json", help="also write {symbols, checks} here")
    args = ap.parse_args()
    img = Image(args.image)
    results = run_checks(img)
    width = max(len(r["name"]) for r in results)
    for r in results:
        print(f"{'ok  ' if r['ok'] else 'FAIL'} {r['name']:<{width}}  {r['evidence']}")
    for name, (lo, _) in API.items():
        print(f"  {name:<20} main+{lo:#08x}")
    failed = [r for r in results if not r["ok"]]
    if args.json:
        out = {"build_id": BUILD, "flavour": "LuaJIT 2.1.0-beta2, GC64/FR2, LJ_DUALNUM, LUA_NUMBER double, no JIT",
               "api": {k: hex(v[0]) for k, v in API.items()}, "internal": {k: hex(v) for k, v in INTERNAL.items()},
               "engine": {k: hex(v) for k, v in ENGINE.items()}, "checks": results}
        pathlib.Path(args.json).write_text(json.dumps(out, indent=1) + "\n")
    print(f"{len(results) - len(failed)}/{len(results)} checks passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
