// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_build.h"

#include <cstring>

namespace d3_build {
namespace {
bool MatchId(const std::uint8_t* id, const std::uint8_t (&want)[20]) {
    if (!id || std::memcmp(id, want, 20) != 0)
        return false;
    for (int i = 20; i < 32; ++i)
        if (id[i])
            return false;
    return true;
}
bool MatchHex(const char* hex, const char* want) {
    if (!hex)
        return false;
    const std::string_view s{hex}, w{want};
    if (s.size() < w.size() || s.substr(0, w.size()) != w)
        return false;
    for (char c : s.substr(w.size()))
        if (c != '0')
            return false;
    return true;
}

// Shared by both builds (C-base-archives): disc content, present in every build including
// 2.7.6; the real per-build surface is which PATCH archives come after it (patch_archives).
constexpr std::array<std::string_view, 14> PatchArchives277{
    "Patch2_6_5", "Patch2_6_6", "Patch2_6_7", "Patch2_6_7a", "Patch2_6_8", "Patch2_6_9",
    "Patch2_6_10", "Patch2_7_0", "Patch2_7_1", "Patch2_7_3", "Patch2_7_4", "Patch2_7_5",
    "Patch2_7_6", "Patch2_7_7"};
} // namespace

// 2.7.7.92380: the module's original target, moved here verbatim from d3_reader.cpp/d3_hg_mem.cpp/
// d3_skills.cpp/d3_cpk.cpp/01001B300B9BE000.cpp (every value below was already live-verified there).
const Build k277{
    .name = "k277",
    .version = "2.7.7.92380",
    .build_id = {0x26, 0x07, 0xA7, 0x4F, 0x5D, 0xF7, 0x75, 0x4C, 0xC0, 0x35,
                 0x7B, 0x5D, 0xF7, 0xE4, 0x96, 0x93, 0x13, 0x55, 0xD8, 0xCA},
    .build_hex = "2607A74F5DF7754CC0357B5DF7E496931355D8CA",
    .app_globals = {0xB8750, 0xD0008488u, 0xF9442108u},     // adrp x8; ldr x8,[x8,#0x840]
    .minimap = {0x3E8A00, 0x9000A497u, 0xF947F6E8u},        // adrp x23; ldr x8,[x23,#0xfe8]
    .map_markers = {0x2CEA80, 0xB000AC68u, 0xF942C908u},    // adrp x8; ldr x8,[x8,#0x590]
    .hud_key = {0x336C34, 0xD00070A0u, 0xF947A400u},        // Root.NormalLayer.ConsoleHUD_main
    .pause_key = {0x33E368, 0xF0007060u, 0xF9425800u},      // ConsolePause_main
    .inv_key = {0x33B01C, 0xD0007080u, 0xF9444000u},        // ConsoleInventoryRing_main
    .waypoint_key = {0x346D60, 0xB0007040u, 0xF9474400u},   // WaypointMap_main (act/world)
    .sno_groups = {0x4C5A4C, 0xF0006468u, 0xF946ED08u},     // adrp x8; ldr x8,[x8,#0xdd8]
    .attr_table = {0x69AFF4, 0x90009429u, 0x912B6129u},     // adrp x9; add x9,x9,#0xad8
    .cube_getter = {0x524140, {0x8B21C808u, 0xB953BD00u, 0xD65F03C0u}},
    .skill_getter = {0x521018, {0x8B33D288u, 0x9104D100u}},
    .potion_getter = {0x5210B0, {0x91065000u, 0xD65F03C0u}},
    .potion_hud = {0x418390, {0x94042348u, 0xAA0003FBu, 0xB9400C00u, 0x94017991u}},
    .skill_physwrite = {0xC97A4, {0xAA1503F4u, 0xB842CE8Au, 0xB9426728u, 0x320003EBu, 0x91099329u,
                                  0x1ACA216Au, 0x2A0A0108u, 0xB9000128u}},
    .skill_defaultprofile = {0x407780, {0x529B7088u, 0xB8686801u, 0x3100043Fu, 0x54000180u}},
    .skill_bindinglookup1 = {0x6C4A6C, {0xF9401009u, 0xF9409129u, 0x92404508u, 0xAB081128u}},
    .skill_bindinglookup2 = {0x4077C4, {0x90005568u, 0x912D8108u, 0xB8737900u}},
    .skill_potiondefault = {0x407310, {0xB941AC08u, 0x14000002u, 0x528000A8u, 0xB9402E69u}},
    .skill_defaulttable_off = 0xEB3B60,
    .skill_defaulttable = {0, 4, 2, 3, -1, -1, 5, 1},
    .skill_manager_ref_off = 0x11547B8,
    .skill_count_ref_off = 0x11572E8,
    .skill_index_ref_off = 0x11572F0,
    .skills_input_verified = true,
    .patch_archives = {PatchArchives277.begin(), PatchArchives277.end()},
    .rift_progress_denominator = 650,
};

// 2.7.6.90885 (d3hack target). Values from a static diff of the two main.nso builds, each pin
// re-located by its surrounding instruction bytes (comments give the shift from k277). A wrong
// pin still fails its owning reader's opcode check and refuses instead of reading garbage.
const Build k276{
    .name = "k276",
    .version = "2.7.6.90885",
    .build_id = {0x9B, 0x9E, 0x18, 0x29, 0x91, 0xF3, 0xF5, 0xB5, 0xF2, 0x17,
                 0x46, 0x64, 0x68, 0x54, 0xFA, 0x80, 0x3B, 0x6D, 0x39, 0xA5},
    .build_hex = "9B9E182991F3F5B5F21746646854FA803B6D39A5",
    .app_globals = {0xB8750, 0xD0008488u, 0xF9442108u},     // same
    .minimap = {0x3E8990, 0x9000A497u, 0xF947F6E8u},        // -0x70
    .map_markers = {0x2CEA80, 0xB000AC68u, 0xF942C908u},    // same words, same offset
    .hud_key = {0x336C24, 0xD00070A0u, 0xF947A400u},        // -0x10
    .pause_key = {0x33E358, 0xF0007060u, 0xF9425800u},      // -0x10
    .inv_key = {0x33B00C, 0xD0007080u, 0xF9444000u},        // -0x10
    .waypoint_key = {0x346D50, 0xB0007040u, 0xF9474400u},   // -0x10
    .sno_groups = {0x4C594C, 0xF0006468u, 0xF946ED08u},     // -0x100
    .attr_table = {0x69AEF4, 0x90009429u, 0x912B6129u},     // -0x100
    .cube_getter = {0x524040, {0x8B21C808u, 0xB953BD00u, 0xD65F03C0u}}, // -0x100
    .skill_getter = {0x520F18, {0x8B33D288u, 0x9104D100u}},             // -0x100
    .potion_getter = {0x520FB0, {0x91065000u, 0xD65F03C0u}},            // -0x100
    // -0x70; the first BL's displacement differs (its callee moved by another amount)
    .potion_hud = {0x418320, {0x94042324u, 0xAA0003FBu, 0xB9400C00u, 0x94017991u}},
    .skill_physwrite = {0xC97A4, {0xAA1503F4u, 0xB842CE8Au, 0xB9426728u, 0x320003EBu, 0x91099329u,
                                  0x1ACA216Au, 0x2A0A0108u, 0xB9000128u}},              // same
    .skill_defaultprofile = {0x407710, {0x529B7088u, 0xB8686801u, 0x3100043Fu, 0x54000180u}}, // -0x70
    .skill_bindinglookup1 = {0x6C496C, {0xF9401009u, 0xF9409129u, 0x92404508u, 0xAB081128u}}, // -0x100
    .skill_bindinglookup2 = {0x407754, {0x90005568u, 0x912D8108u, 0xB8737900u}},              // -0x70
    .skill_potiondefault = {0x4072A0, {0xB941AC08u, 0x14000002u, 0x528000A8u, 0xB9402E69u}},  // -0x70
    .skill_defaulttable_off = 0xEB3B60,
    .skill_defaulttable = {0, 4, 2, 3, -1, -1, 5, 1},
    // account-manager globals: same offsets, confirmed at two 2.7.6 code sites each
    .skill_manager_ref_off = 0x11547B8,
    .skill_count_ref_off = 0x11572E8,
    .skill_index_ref_off = 0x11572F0,
    .skills_input_verified = true,
    // not in main; LoadIndex skips archives a build does not ship, so 2.7.6 uses the 2.7.7 list
    .patch_archives = {PatchArchives277.begin(), PatchArchives277.end()},
    .rift_progress_denominator = 650, // unresolved on both builds (HUD-fraction calibrated)
};

const Build* SelectBuild(const std::uint8_t* build_id32) {
    if (!build_id32)
        return nullptr;
    if (MatchId(build_id32, k277.build_id))
        return &k277;
    if (MatchId(build_id32, k276.build_id))
        return &k276;
    return nullptr;
}

const Build* SelectBuildHex(const char* hex) {
    if (!hex)
        return nullptr;
    if (MatchHex(hex, k277.build_hex))
        return &k277;
    if (MatchHex(hex, k276.build_hex))
        return &k276;
    return nullptr;
}

} // namespace d3_build
