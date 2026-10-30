// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Game strings and icon atlases for the Hero and Gear tabs, read from the player's own romfs on the
// asset worker: StringLists by label (labels are hashed with h = h * 33 + byte), the SNO names of the
// Power and Textures groups (CoreTOC.dat), a frame-hash -> atlas index over the item, buff and skill
// icon atlases, and the game's attribute description templates ("[{VALUE}*100|1|]% ...").

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "d3_assets.h"
#include "d3_hg_mem.h"

namespace d3hg {

class Text {
public:
    /// Text of `label` in StringList `list` (enUS); empty when missing. Loads the list once.
    std::string Get(d3_assets::Assets& a, const EdenDsmodHostApi* h, const std::string& list, u32 label);
    std::string Get(d3_assets::Assets& a, const EdenDsmodHostApi* h, const std::string& list,
                    std::string_view label) {
        return Get(a, h, list, H33(label));
    }
    /// Entry `index` (file order) of the StringList with SNO `sno` (group 42); empty when missing.
    std::string ListEntry(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 sno, u32 index,
                          std::string* list_name = nullptr);
    /// SNO name of a power (group 29).
    std::string PowerName(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 sno);
    /// Atlas holding frame `hash` (item / buff / skill icon atlases), empty when not found.
    std::string AtlasFor(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 hash, bool buff);
    /// Icon image for a frame hash (nearest, unscaled); empty when unknown.
    d3_img::Image Icon(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 hash, bool buff);

private:
    void LoadToc(d3_assets::Assets& a, const EdenDsmodHostApi* h);
    bool ScanAtlas(d3_assets::Assets& a, const EdenDsmodHostApi* h, const std::string& name);
    std::mutex mutex;
    std::unordered_map<std::string, std::unordered_map<u32, std::string>> lists;
    bool toc = false;
    std::unordered_map<u32, std::string> powers, stringlists;
    std::unordered_map<u32, std::vector<std::string>> ordered;
    std::vector<std::string> item_atlases, buff_atlases;
    size_t item_next = 0, buff_next = 0;
    std::unordered_map<u32, std::string> frame_atlas;
};

/// Removes {c...}/{/c...} colour tags and {icon:...} tokens from a game string.
std::string StripTags(std::string_view s);

/// Expands an attribute description template: {VALUE}/{VALUE1}/{VALUE2} numbers, {VALUE1} as text
/// when `text1` is set, and [expr|decimals|sign] blocks (expr = numbers and values joined by * / + -).
std::string Expand(std::string_view tmpl, const double* v, int nv, const std::string& text1 = {});

/// Number with thousands separators and `dec` decimals ("696,023.81").
std::string Grouped(double v, int dec = 0);

} // namespace d3hg
