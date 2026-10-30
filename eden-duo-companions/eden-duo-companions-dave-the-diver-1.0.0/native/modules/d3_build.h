// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-build constants for the Diablo III (Switch) companion. Two main.nso builds are supported:
// 2.7.7.92380 (k277, the module's original, live-verified target) and 2.7.6.90885 (k276, the
// d3hack target; every k277 pin re-located in it by a static diff of the two mains). The
// build is selected once in 01001B300B9BE000.cpp's Create() and threaded through by const
// reference: d3_reader::Reader, d3hg::Reader, d3_skills::ReadInput, d3_cpk::Archives::Open.
//
// Struct offsets, attribute ids, SNO ids and romfs formats are NOT here: INVENTORY.md expects
// them to be identical across the two builds (or self-healing, like attribute ids resolved by
// name from a live descriptor table), so they stay the existing file-scope constexprs in each
// reader. Only what INVENTORY.md actually calls build-specific lives in a Build: the opcode/
// content pins, the three raw (unguarded) account-manager globals, the CPK patch-archive list
// and the rift progress denominator.
//
// k276's values come from analysis/port-276/BUILD-276.tsv. A wrong pin fails its own opcode/
// content check against real memory, so the owning reader refuses cleanly (its existing
// "pin mismatch -> refuse" behaviour) instead of reading garbage. d3_skills::ReadInput's
// account-binding chain is the one exception worth a second gate: three of its literals
// (skill_manager_ref_off/skill_count_ref_off/skill_index_ref_off) are read raw with no opcode
// guard at all, so a placeholder there would not refuse, it would silently read the wrong
// binding. `skills_input_verified` covers that: false makes ReadInput refuse outright.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace d3_build {

// ADRP Xd + LDR/ADD Xt,[Xd,#imm] literal pin: a main-relative offset plus the two instruction
// words the reader's Resolve() opcode-checks before decoding the literal it points at.
struct Pin2 {
    std::uint64_t off;
    std::uint32_t w0, w1;
};

// Exact-byte guard of N words at a fixed main-relative offset: an instruction sequence (or, for
// `skill_defaulttable`, .rodata/.data content) checked for equality, with no literal to decode.
template <std::size_t N>
struct PinWords {
    std::uint64_t off;
    std::array<std::uint32_t, N> words;
};

struct Build {
    const char* name;          // short tag for logs and SelectBuild's table ("k277", "k276")
    const char* version;       // display version string ("2.7.7.92380")
    std::uint8_t build_id[20]; // full main build id; the remaining 12 bytes of the ABI's 32 are 0
    const char* build_hex;     // build_id as 40 uppercase hex chars (SupportsBuildHex prefix match)

    // ---- d3_reader.cpp / d3_hg_mem.cpp roots. app_globals is one field for both readers: before
    // this they each opcode-checked and decoded the identical literal from their own copy of the
    // same pin (INVENTORY.md: "AppGlobals pin is duplicated, not shared"); now there is exactly
    // one value to repin per build. ----
    Pin2 app_globals;                               // R-pin-appglobals / H-pin-appglobals
    Pin2 minimap;                                   // R-pin-minimap
    Pin2 map_markers; // R-pin-mapmarkers: the game's map-marker list getter (optional)
    Pin2 hud_key, pause_key, inv_key, waypoint_key; // R-pin-{hud,pause,inv,waypoint}key (optional)
    Pin2 sno_groups;                                // H-pin-snogroups
    Pin2 attr_table;                                // H-pin-attrtable (ADRP+ADD, not ADRP+LDR)

    // ---- d3_hg_mem.cpp exact-byte UI accessor guards ----
    PinWords<3> cube_getter;   // H-pin-cubegetter (gates the whole d3hg::Reader, not just Cube)
    PinWords<2> skill_getter;  // H-pin-skillgetter (optional: a mismatch only disables equipped skills)
    PinWords<2> potion_getter; // H-pin-potion, getter half (optional: ditto for the potion identity)
    PinWords<4> potion_hud;    // H-pin-potion, HUD-consumer half (same optionality as potion_getter)

    // ---- d3_skills.cpp ReadInput()'s account-binding chain ----
    PinWords<8> skill_physwrite;      // S-pin-physwrite
    PinWords<4> skill_defaultprofile; // S-pin-defaultprofile
    PinWords<4> skill_bindinglookup1; // S-pin-bindinglookup1
    PinWords<3> skill_bindinglookup2; // S-pin-bindinglookup2
    PinWords<4> skill_potiondefault;  // S-pin-potiondefault
    std::uint64_t skill_defaulttable_off;           // S-defaulttable: .rodata address (data, not code)
    std::array<std::int32_t, 8> skill_defaulttable; // S-defaulttable: its expected content
    // S-bindings-struct: the three account-manager pointers, read raw with NO opcode guard of
    // their own -- the least-guarded (riskiest) constants in the whole module: a wrong value
    // fails silently into garbage bindings rather than refusing.
    std::uint64_t skill_manager_ref_off, skill_count_ref_off, skill_index_ref_off;

    // False makes d3_skills::ReadInput refuse outright (no rail glyphs / tap-to-use), independent
    // of whether every pin above happens to match: the three S-bindings-struct pointers above
    // have no guard, so an unverified build must never rely on a lucky pin match alone.
    bool skills_input_verified;

    // ---- d3_cpk.cpp: the update's patch-archive mount list (content, not a code address) ----
    std::vector<std::string_view> patch_archives; // C-patch-archives

    // ---- 01001B300B9BE000.cpp: rift progress-bar denominator (M-rift-denominator; unresolved
    // even on k277 -- STATUS.md names main+0x6C4A40+0x214 as the still-pending real source) ----
    std::uint32_t rift_progress_denominator;
};

extern const Build k277; // 2.7.7.92380, live-verified (the module's original target)
extern const Build k276; // 2.7.6.90885, the d3hack target

/// Selects the build whose 20-byte id (+ 12 zero bytes) matches `build_id32`; null when neither
/// supported build matches.
const Build* SelectBuild(const std::uint8_t* build_id32);
/// As SelectBuild, but matches a build whose hex id is a prefix of `hex` (any trailing hex must be
/// all zero) -- the ABI's supports_build(const char*) shape.
const Build* SelectBuildHex(const char* hex);

} // namespace d3_build
