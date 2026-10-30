// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III companion — Activities tab body (design A): full-width Greater/Nephalem Rift card
// inside rifts, the Adventure-mode bounty board elsewhere. Map-lane file (the Hero/Gear tabs live in
// their own files).

#pragma once

#include "d3_assets.h"
#include "d3_img.h"
#include "d3_view.h"

namespace d3_activities {

constexpr int BodyX = d3_view::ColW;                       // drawn right of the vitals column
constexpr int BodyW = d3_view::CanvasW - d3_view::ColW;    // 978
constexpr int BodyH = d3_view::NavY;                       // 995

struct View {
    bool rift = false;            // hero is in a rift level
    d3_view::RiftView r;          // valid when rift
    d3_view::BountyView b;        // valid otherwise
    bool adventure = true;        // false: campaign game (no bounties)
    bool operator==(const View&) const = default;
};

d3_img::Image Body(d3_assets::Assets& a, const EdenDsmodHostApi* h, const View& v);

} // namespace d3_activities
