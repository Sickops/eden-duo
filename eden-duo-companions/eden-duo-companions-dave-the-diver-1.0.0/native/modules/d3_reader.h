// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III: Eternal Collection live reader. Two main.nso builds are supported (see
// native/modules/d3_build.h): 2.7.7.92380 (k277, module DiabloIIINX64r.nss, the module's original
// live-verified target) and 2.7.6.90885 (k276, the d3hack target). Every read was verified live
// against the native HUD, minimap and full map before use.
//
// Roots are resolved from code, not from absolute data addresses: each build's own ADRP + LDR
// pairs for AppGlobals (LocalPlayerGet; main+0xB8750 on both builds) and the minimap globals
// (main+0x3E8A00 on k277, main+0x3E8990 on k276; d3_build.cpp) are opcode-checked and decoded at
// runtime; the data address gets the host's __relocation_delta (0 on Dynarmic; NCE may place
// .data/.bss at a constant offset).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "d3_build.h"

namespace d3_reader {

struct SceneMask {
    std::uint32_t scene_sno = 0, tex_sno = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::uint32_t w = 0, h = 0;
    bool tracked = false;            // false: the game keeps no fog for this scene (towns)
    std::vector<std::uint8_t> mask;  // 2 bits/cell, MSB first, column-major (cx*h + cy)
};

struct Snapshot {
    enum class State { NoRoot, NoWorld, Loading, Ready };
    State state = State::NoRoot; // NoWorld: main menu / hero select (player index -1);
                                 // Loading: a local player slot exists but its actor is not ready
    std::string root_error;
    std::uint64_t player_address = 0; // validated local Player; zero outside Ready
    // hero / vitals (valid when Ready)
    std::string name;
    int cls = -1, level = 0, paragon = 0, difficulty = -1;
    double hp = 0, hp_max = 0;
    int res_type[2] = {-1, -1};
    double res_cur[2] = {0, 0}, res_max[2] = {0, 0};
    std::uint32_t area_sno = 0, world = 0;
    float x = 0, y = 0, facing = 0; // facing: radians, world frame (atan2(dy, dx))
    bool facing_valid = false;
    // rifts (valid when Ready): progress numerator of the rift HUD bar, Greater Rift tier (0 = not a
    // Greater Rift), its timer (client timer X1_LR_TieredRift_Timer) and the Rift Guardian
    std::uint32_t tick = 0;
    bool game_paused = false;
    // native menu owning the top screen (top-level window shown flags; HUD hidden = some menu)
    enum class Menu { Unknown, None, Pause, Inventory, WorldMap, Other };
    Menu menu = Menu::Unknown;     // a native full-screen menu paused the game (G+0x5C step == 0)
    float rift_progress = -1;     // -1 = no rift progress object
    int gr_tier = 0, gr_deaths = 0;
    int best_solo = -1; // Highest_Solo_Rift_Level
    int timer_left = -1, timer_total = 0; // ticks (60 per second); -1 = no GR timer
    bool guardian = false;        // a Rift Guardian actor is in the client ACD list
    double guardian_hp = 0, guardian_hp_max = 0;
    // server quest list [SGame+0x590] (in-process server: single player / local co-op host),
    // read on full-map ticks: every bounty of the game (5 per act), turn-ins and rift quests
    struct Quest {
        std::uint32_t sno = 0, area = 0;
        std::uint32_t state = 0; // +0x18: 0 not started, 1 in progress, 2 completed (live-verified)
    };
    bool quests_valid = false;
    std::vector<Quest> quests;
    // map markers the game itself placed on its minimap (MinimapGlobals +0x08: static markers such
    // as waypoints / dungeon entrances and exits; +0x10: dynamic ones such as shrines, pylons,
    // quest destinations; the game's map-marker list for actors outside client range, e.g. town
    // NPCs and services across the map), kept only in the player's world and on explored ground
    // (or in towns)
    enum class Layer : std::uint8_t { Static, Dynamic, Remembered };
    struct Marker {
        std::uint32_t tex_sno = 0; // the marker SNO is its icon texture (Textures group)
        float x = 0, y = 0;
        Layer layer = Layer::Static;
        int frame = -1; // frame index in the texture's atlas (2DMapGeneric icons); -1 = whole texture
        bool operator==(const Marker&) const = default;
    };
    std::vector<Marker> markers;
    // Separate native actor-marker list [MinimapGlobals+0]. Icon selection comes from the
    // actor's native tags/MarkerSet overrides, never an actor-name classification.
    struct ActorMarker {
        std::uint32_t actor_sno = 0, override_tex = 0xFFFFFFFFu;
        std::uint32_t marker_set_sno = 0xFFFFFFFFu;
        int marker_set_index = -1, type = -1, gizmo = -1, conversation_icon = -1;
        std::uint32_t ann = 0xFFFFFFFFu;
        // the icon the game's map-marker list holds for this actor (texture SNO, atlas frame or
        // -1); used when no Actor/MarkerSet texture resolves
        std::uint32_t list_tex = 0xFFFFFFFFu;
        int list_frame = -1;
        float x = 0, y = 0, distance_sq = 0;
        bool quest_monster = false, hostile = false;
        bool operator==(const ActorMarker&) const = default;
    };
    std::vector<ActorMarker> actor_markers;
    // explored map of the player's world
    std::vector<SceneMask> scenes;
    std::uint64_t explored_hash = 0;
    int explored_cells = 0;
};

class Reader {
public:
    explicit Reader(const EdenDsmodHostApi& host, const d3_build::Build& build);
    /// Cheap per-tick read: state, position, vitals. `full_map` also re-reads the explored masks.
    void Read(const EdenDsmodHostApi& host, bool full_map, Snapshot& out);
    /// Whether the roots resolved (false keeps everything unavailable).
    bool Resolved() const {
        return app_literal != 0 && minimap_literal != 0;
    }

private:
    bool Resolve(const EdenDsmodHostApi& host, std::string& err);
    const d3_build::Build& build;
    std::uint64_t main_base = 0;
    std::uint64_t app_literal = 0, minimap_literal = 0, sigma_global = 0;
    std::uint64_t map_markers_literal = 0; // 0: pin mismatch (no remembered markers)
    std::uint64_t window_got[4] = {0, 0, 0, 0}; // HUD, pause, inventory, world map key GOT slots
    std::string menu_trace, actor_trace; // opt-in, changed-state diagnostics only
};

bool SupportsBuildHex(const char* hex);

} // namespace d3_reader
