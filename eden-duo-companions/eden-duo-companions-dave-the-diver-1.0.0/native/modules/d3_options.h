// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Companion options page (nav-bar cog), drawn with the game's own console options art and fonts
// instead of the runtime's generic settings page. One toggle today: tap a Map rail slot to use it.
#pragma once

#include "d3_assets.h"
#include "d3_img.h"

namespace d3_options {
// Layout shared with tools/d3/gen_manifest.py, which parses these lines (keep them one per name).
constexpr int OptW = 820, OptH = 430, OptX = (1240 - OptW) / 2, OptY = 220;
constexpr int OptRowX = OptX + 30, OptRowY = OptY + 120, OptRowW = OptW - 60, OptRowH = 110;

/// 1240 x 995 page body; `tap_use` picks the checked or unchecked native checkbox.
d3_img::Image Page(d3_assets::Assets& assets, const EdenDsmodHostApi* host, bool tap_use);
} // namespace d3_options
