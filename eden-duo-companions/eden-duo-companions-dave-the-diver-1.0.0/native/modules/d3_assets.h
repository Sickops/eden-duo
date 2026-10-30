// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Game assets for the Diablo III companion, decoded at runtime from the player's own romfs:
// UI atlases (.tex + named frames), the baked bitmap fonts (.fnt), scene minimap tiles
// (mmap_<scene>.tex), LevelArea names (StringList) and SNO names (CoreTOC.dat).
// Thread-safety: one mutex guards the caches; decoding happens outside it.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/mods/dsmod_module_abi.h"
#include "d3_build.h"
#include "d3_cpk.h"
#include "d3_font.h"
#include "d3_tex.h"

namespace d3_assets {

// sno_names, texture_snos and area_names are populated once by Init() and never mutated again, so
// readers need no lock for them; the caller publishes an Assets instance to other threads only
// after Init() returns true, via its own atomic<bool> "ready" flag (see the module's init_thread),
// which is what makes that one-time publication safe without Assets::mutex covering these maps.
class Assets {
public:
    /// Loads `build`'s archives, fonts and string tables; false while the romfs is not ready
    /// (retry later).
    bool Init(const EdenDsmodHostApi* host, const d3_build::Build& build);
    bool Ready() const {
        return ready;
    }
    std::shared_ptr<const d3_tex::Texture> Tex(const EdenDsmodHostApi* host, const std::string& name);
    /// A named (or "#hash") frame of an atlas; empty image when missing.
    d3_img::Image Frame(const EdenDsmodHostApi* host, const std::string& atlas,
                        const std::string& frame);
    /// Scene minimap tile by texture SNO (cached, at most a few hundred entries).
    std::shared_ptr<const d3_tex::Texture> SceneTile(const EdenDsmodHostApi* host, std::uint32_t sno);
    /// Content box of a decoded scene tile (non-transparent texels) in world units from the scene
    /// origin along world X and Y; false until the tile has been decoded once.
    bool TileContent(std::uint32_t sno, float out[4]);
    std::string AreaName(std::uint32_t level_area_sno) const;
    std::string SnoName(std::uint32_t sno) const;
    bool IsTextureSno(std::uint32_t sno) const { return texture_snos.contains(sno); }
    /// Actor minimap tags, from the native Actor definition; absent tags stay absent.
    std::map<std::uint32_t, std::uint32_t> ActorMapTags(const EdenDsmodHostApi* host,
                                                     std::uint32_t actor_sno);
    /// Per-placement minimap override. Absent/invalid entries use the Actor's default texture.
    std::map<std::uint32_t, std::uint32_t> MarkerMapTags(const EdenDsmodHostApi* host,
                                                 std::uint32_t marker_set, int index);
    /// Both tag maps above from the cache only (never reads the archives): false until each was
    /// resolved once by a map picture, so the tick thread can test marker radii without blocking.
    bool CachedMapTags(std::uint32_t actor_sno, std::uint32_t marker_set, int index,
                       std::map<std::uint32_t, std::uint32_t>& actor,
                       std::map<std::uint32_t, std::uint32_t>& placement);
    /// Quest display name: label "Name" of StringList Quest_<SNO name> (enUS), e.g.
    /// "Bounty: Kill Rockgut"; cached; empty when missing.
    std::string QuestName(const EdenDsmodHostApi* host, std::uint32_t quest_sno);
    /// Raw archive member (Hero/Gear tabs: StringLists, CoreTOC, icon atlases). Thread-safe.
    bool ReadMember(const EdenDsmodHostApi* host, std::string_view name, std::vector<std::uint8_t>& out) {
        return archives.Read(host, name, out);
    }

    d3_font::Font exo, sans;

private:
    bool ready = false;
    d3_cpk::Archives archives;
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const d3_tex::Texture>> tex_cache;
    std::unordered_map<std::uint32_t, std::shared_ptr<const d3_tex::Texture>> tile_cache;
    std::unordered_map<std::uint32_t, std::array<float, 4>> tile_content;
    std::unordered_map<std::uint32_t, std::string> sno_names; // Actor + LevelArea + Textures + Quest
    std::unordered_set<std::uint32_t> texture_snos;
    std::unordered_map<std::uint32_t, std::string> area_names; // LevelArea SNO -> display name
    std::unordered_map<std::uint32_t, std::string> quest_names; // Quest SNO -> display name
    std::unordered_map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> actor_map_tags;
    std::unordered_map<std::uint64_t, std::map<std::uint32_t, std::uint32_t>> marker_map_tags;
};

} // namespace d3_assets
