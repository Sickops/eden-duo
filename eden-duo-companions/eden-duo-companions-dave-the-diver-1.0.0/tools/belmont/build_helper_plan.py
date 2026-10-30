#!/usr/bin/env python3
"""Build the format-2 load plan for the Belmont GUEST HELPER (in-game pin/equip
bridge). Mirrors the BOTW companion's format-2 hook builder:
compile native/helper/belmont_guest_helper.S with clang (aarch64-none-elf),
pull its compiled .text bytes and ELF relocations, resolve every external
symbol it references to a (target, addend) pair, and emit a format-2 plan
JSON matching the schema `mod_load_plan.cpp` parses (module/layout/mailbox_*/
patches/guest_code).

Entirely offline: reads main.bin and the real `exefs/main` NSO header already
on disk, never touches a running emulator.

Usage:
    python3 tools/belmont/build_helper_plan.py [--output-dir .scratch/helper-out]

Exit non-zero (with an assertion/message) on any mismatch: compiler failure,
an `expected` byte that does not match main.bin at the hook offset, guest
code over 64 KiB, or an unexpected relocation count (a regression guard --
update it deliberately if the .S file's external-symbol references change).
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]  # .../castlevania-belmont-hardening
HELPER_SRC = REPO_ROOT / "native/helper/belmont_guest_helper.S"

# Title 0100BA4026E94000, Castlevania (Belmont's Curse). The game's own executable is never
# part of this repository: pass the player's dumped exefs/main (NSO) and its flat memory
# image via --nso/--main-bin or the BELMONT_NSO / BELMONT_MAIN_BIN environment variables.
import os
DEFAULT_NSO = Path(os.environ["BELMONT_NSO"]) if "BELMONT_NSO" in os.environ else None
DEFAULT_MAIN_BIN = Path(os.environ["BELMONT_MAIN_BIN"]) if "BELMONT_MAIN_BIN" in os.environ else None

TITLE_ID = 0x0100BA4026E94000
EXPECTED_BUILD_ID_UPPER = (
    "0F1169AA7D0FA4529C861F96ADEC306E00000000000000000000000000000000"
)

HOOK_OFFSET = 0x40D4F00
HOOK_EXPECTED_BYTES = bytes.fromhex("fd7bbda9")  # stp x29,x30,[sp,#-0x30]!
RESUME_OFFSET = HOOK_OFFSET + 4  # 0x40D4F04

MAILBOX_SIZE = 4096
MAILBOX_EPOCH_OFFSET = 4092  # last word of the mailbox; see native/helper/README.md

MAX_GUEST_CODE_BYTES = 64 * 1024

# Every external (undefined) symbol belmont_guest_helper.S references, resolved to
# (target, addend). "mailbox" addends are offsets into the mailbox; "main" addends
# are absolute file offsets into main == VA offsets from load_base (confirmed: this
# main.bin is a flat memory image, vaddr == file offset -- see README "Binary
# layout"). "code" (the hook's own replacement) is handled separately, not here.
SYMBOLS: dict[str, tuple[str, int]] = {
    "belmont_mailbox_base": ("mailbox", 0x0000),
    # ApplicationFlowManager klass-pointer cache slot, exactly as belmont_reader.h's
    # Flag(base, 0x6d3f780, "ApplicationFlowManager", ...) already reads it.
    "belmont_app_flow_loading_klass_cache": ("main", 0x6D3F780),
    # IL2CPP generic object-new, called with x0=klass exactly as TryPlaceMarker does
    # at main.bin file offset 0x1DF0320 (independently re-disassembled, see README).
    "belmont_il2cpp_object_new": ("main", 0x5BE780),
    # TryPlaceMarker's lazy-metadata init call and MapMarkerData_TypeInfo slot.
    "belmont_il2cpp_init_runtime_metadata": ("main", 0x5BE410),
    "belmont_map_marker_data_typeinfo": ("main", 0x6D444D8),
    "belmont_map_marker_data_ctor": ("main", 0x1C311A0),
    "belmont_markers_add_marker": ("main", 0x1C2E7A0),
    "belmont_markers_remove_marker": ("main", 0x1C2EF60),
    "belmont_markers_change_marker_type": ("main", 0x1C2FDB0),
    "belmont_inventory_equip_spell": ("main", 0x1F547B0),
    "belmont_inventory_equip_weapon": ("main", 0x1F546E0),
    "belmont_inventory_equip_charm": ("main", 0x1F54880),
    "belmont_resume": ("main", RESUME_OFFSET),
}

RELOC_KIND_MAP = {
    "R_AARCH64_CALL26": "aarch64_branch26",
    "R_AARCH64_JUMP26": "aarch64_branch26",
    "R_AARCH64_ADR_PREL_PG_HI21": "aarch64_adrp",
    "R_AARCH64_ADD_ABS_LO12_NC": "aarch64_add_lo12",
    "R_AARCH64_LDST64_ABS_LO12_NC": "aarch64_ldr64_lo12",
    "R_AARCH64_LDST32_ABS_LO12_NC": "aarch64_ldr32_lo12",
}

# Regression guard: the exact relocation count belmont_guest_helper.S produced the
# last time this script was run against it. Update deliberately (with a comment
# saying why) if the .S file's external-symbol references change.
EXPECTED_RELOCATION_COUNT = 18  # 2 (mailbox adrp+add) + 2 (loading-flag adrp+ldr64) + 4 (MapMarkerData slot adrp+add, adrp+ldr64) + 10 (bl/b)


class NSOHeader:
    """Minimal NSO0 header reader: just enough for the three segment
    location/size pairs + bss_size the load plan's `layout` needs, and the
    build id for a sanity cross-check. No decompression: main.bin (the
    IL2CPP dump) already holds the flat, decompressed memory image, which is
    what `expected` bytes and relocation addends are checked against."""

    def __init__(self, data: bytes):
        if data[:4] != b"NSO0":
            raise ValueError("not an NSO0 image")
        fields = struct.unpack_from("<16I", data, 0x10)
        (
            self.text_file_off, self.text_mem_off, self.text_size, _name_off,
            self.rodata_file_off, self.rodata_mem_off, self.rodata_size, _name_size,
            self.data_file_off, self.data_mem_off, self.data_size, self.bss_size,
        ) = fields[0:12]
        self.build_id = data[0x40:0x60]

    def layout(self) -> dict:
        return {
            "segments": [
                {"location": self.text_mem_off, "size": self.text_size},
                {"location": self.rodata_mem_off, "size": self.rodata_size},
                {"location": self.data_mem_off, "size": self.data_size},
            ],
            "bss_size": self.bss_size,
        }

    def main_size(self) -> int:
        return self.data_mem_off + self.data_size + self.bss_size


def run(cmd: list[str]) -> str:
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return result.stdout


def compile_helper(out_dir: Path, clang: str, objcopy: str, readelf: str) -> tuple[bytes, str]:
    obj = out_dir / "belmont_guest_helper.o"
    binf = out_dir / "belmont_guest_helper.bin"
    subprocess.run(
        [clang, "--target=aarch64-none-elf", "-c", str(HELPER_SRC), "-o", str(obj)],
        check=True,
    )
    subprocess.run(
        [objcopy, "-O", "binary", "--only-section=.text", str(obj), str(binf)],
        check=True,
    )
    code = binf.read_bytes()
    reloc_text = run([readelf, "-r", str(obj)])
    (out_dir / "relocations.txt").write_text(reloc_text)
    return code, reloc_text


def parse_relocations(reloc_text: str, code_len: int) -> list[dict]:
    relocations = []
    pattern = re.compile(
        r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_AARCH64_\S+)\s+[0-9a-fA-F]+\s+(\S+)\s+([+-])\s+([0-9a-fA-F]+)"
    )
    for line in reloc_text.splitlines():
        match = pattern.fullmatch(line.strip())
        if not match:
            assert not line.strip().startswith(("0000", "000000")) or "OFFSET" in line.upper() or not line.strip(), (
                f"unrecognised relocation line, update the parser: {line!r}"
            )
            continue
        offset_s, kind, symbol, sign, addend_s = match.groups()
        assert kind in RELOC_KIND_MAP, f"unhandled relocation kind {kind}"
        assert symbol in SYMBOLS, f"undeclared external symbol {symbol!r} in belmont_guest_helper.S"
        offset = int(offset_s, 16)
        assert offset % 4 == 0 and offset + 4 <= code_len, f"bad relocation offset {offset:#x}"
        extra = int(addend_s, 16) * (1 if sign == "+" else -1)
        assert extra >= 0, "negative addend unsupported by ModLoadPlan::Apply"
        target, base_addend = SYMBOLS[symbol]
        relocations.append(
            dict(offset=offset, kind=RELOC_KIND_MAP[kind], target=target, addend=base_addend + extra)
        )
    return relocations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=REPO_ROOT / ".scratch/helper-out")
    parser.add_argument("--nso", type=Path, default=DEFAULT_NSO)
    parser.add_argument("--main-bin", type=Path, default=DEFAULT_MAIN_BIN)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--objcopy", default="llvm-objcopy")
    parser.add_argument("--readelf", default="llvm-readelf")
    args = parser.parse_args()
    if args.nso is None or args.main_bin is None:
        parser.error("pass --nso and --main-bin (or set BELMONT_NSO / BELMONT_MAIN_BIN)")

    out_dir = args.output_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    # --- NSO header: layout + build id (offline, no decompression needed). ---
    nso_bytes = args.nso.read_bytes()[:0x100]
    header = NSOHeader(nso_bytes)
    build_id_upper = header.build_id.hex().upper()
    assert build_id_upper == EXPECTED_BUILD_ID_UPPER, (
        f"NSO build id {build_id_upper} != expected {EXPECTED_BUILD_ID_UPPER}; "
        "this plan would be built for the wrong game version"
    )

    # --- main.bin: vaddr == file offset sanity check, then the hook's `expected`. ---
    main_bin = args.main_bin.read_bytes()
    assert len(main_bin) == header.main_size(), (
        f"main.bin is {len(main_bin)} bytes, expected {header.main_size()} "
        "(text+rodata+data+bss per the real NSO header) -- is this the flat memory "
        "image this script assumes, or did the build change?"
    )
    live_hook_bytes = main_bin[HOOK_OFFSET:HOOK_OFFSET + 4]
    assert live_hook_bytes == HOOK_EXPECTED_BYTES, (
        f"main.bin[{HOOK_OFFSET:#x}:+4] = {live_hook_bytes.hex()}, expected "
        f"{HOOK_EXPECTED_BYTES.hex()} (stp x29,x30,[sp,#-0x30]!); ApplicationFlowManager.Update "
        "may have moved -- re-run the RE pass before trusting this plan"
    )

    # --- Compile the helper, pull its code bytes + relocations. ---
    code, reloc_text = compile_helper(out_dir, args.clang, args.objcopy, args.readelf)
    assert code and len(code) % 4 == 0, "guest code must be a whole number of instructions"
    assert len(code) <= MAX_GUEST_CODE_BYTES, (
        f"guest code is {len(code)} bytes, over the {MAX_GUEST_CODE_BYTES} byte cap "
        "(ModLoadPlan::GuestCodeSize / MaxGuestCodeBytes)"
    )
    relocations = parse_relocations(reloc_text, len(code))
    if EXPECTED_RELOCATION_COUNT is not None:
        assert len(relocations) == EXPECTED_RELOCATION_COUNT, (
            f"relocation count changed: {len(relocations)} vs expected "
            f"{EXPECTED_RELOCATION_COUNT}; update EXPECTED_RELOCATION_COUNT if this "
            "is an intentional edit to belmont_guest_helper.S"
        )

    plan = {
        "title_id": f"{TITLE_ID:016X}",
        "build_id": build_id_upper,
        "module": "main",
        "format": 2,
        "layout": header.layout(),
        "mailbox_size": MAILBOX_SIZE,
        "mailbox_epoch_offset": MAILBOX_EPOCH_OFFSET,
        "patches": [
            {
                "offset": HOOK_OFFSET,
                "expected": HOOK_EXPECTED_BYTES.hex(),
                "replacement": struct.pack("<I", 0x14000000).hex(),  # b <code+0>, imm filled by Apply()
                "relocations": [
                    {"offset": 0, "kind": "aarch64_branch26", "target": "code", "addend": 0}
                ],
            }
        ],
        "guest_code": {"bytes": code.hex(), "relocations": relocations},
    }

    plan_path = out_dir / "plan.json"
    plan_path.write_text(json.dumps(plan, indent=2) + "\n")

    print(
        json.dumps(
            dict(
                plan=str(plan_path),
                code_bytes=len(code),
                relocation_count=len(relocations),
                main_size=header.main_size(),
                build_id=build_id_upper,
            ),
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
