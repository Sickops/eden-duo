// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Hero tab (approved design A, A-open-hero / A-gr-hero): portrait, name, level / paragon, class,
// experience bar, damage, gold and blood shards (left column); the native Character Details list
// (Details) with whole-row scrolling, the buffs the native buff holder shows (Status Effects) and the
// legendary gems socketed in the equipped items (right panel).

#pragma once

#include <string>
#include <vector>

#include "d3_hg_mem.h"
#include "d3_hg_text.h"
#include "d3_img.h"

namespace d3hg {

struct StatRow {
    int kind = 0;      // 0 section heading, 1 row
    std::string list;  // StringList holding the label
    u32 label = 0;     // label hash
    std::string arg;   // {s1} of the label ("{s1} Damage Increase": power SNO as decimal)
    std::string value; // formatted value
    bool operator==(const StatRow&) const = default;
};
struct GemView {
    u32 icon = 0, gbid = 0;
    int rank = -1;
    bool operator==(const GemView&) const = default;
};
struct HeroView {
    bool valid = false;
    std::string name;
    u32 portrait_actor = 0;
    int level = 0, paragon = 0;
    int xp_fill = -1; // 0..168 bar pixels; -1 = unknown (bar omitted)
    std::string damage;
    std::string gold, shards, shard_cap;
    std::vector<StatRow> rows;
    int scroll = 0;          // first visible row
    std::vector<BuffData> buffs;
    std::vector<GemView> gems;
    bool cube_valid = false;
    std::array<CubePowerData, 3> cube;
    int effects_scroll = 0; // independent whole-entry scroll in the right column
    bool operator==(const HeroView&) const = default;
};

/// Builds the view from a reader snapshot (no I/O; runs on the sampling thread).
HeroView BuildHeroView(const AttrMeta& m, const HeroData& h, const std::vector<ItemData>& items);
/// Number of rows of the Details list (for scroll bounds).
int HeroRowCount(const HeroView& v);
int HeroEffectsRowCount(const HeroView& v);
/// Rows visible at once (whole rows only).
constexpr int HeroRowsVisible = 22;

d3_img::Image HeroPage(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const HeroView& v);

} // namespace d3hg
