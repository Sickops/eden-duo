// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
// Unreleased runtime 22 additions: the exact quarter-turn image path draws the same texels as the
// generic inverse map, animated map markers pick their frame and pulse from the clock, and the
// manifest "preload" list parses.
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "core/mods/mod_runtime.h"
#include "core/mods/mod_types_map.h"
#include "core/mods/mod_ui.h"

using namespace Core::Mods;

namespace {

Image Picture(u32 w, u32 h) {
    Image image;
    image.w = w;
    image.h = h;
    image.pixels.resize(static_cast<size_t>(w) * h);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            // Opaque, every texel distinct, a few translucent ones to exercise the blend.
            const u32 a = (x + y) % 5 == 0 ? 0x80u : 0xFFu;
            image.pixels[static_cast<size_t>(y) * w + x] = (a << 24) | (x * 37 + y * 11 + 1);
        }
    }
    return image;
}

/// The generic inverse map of Canvas::DrawImageTransformed at scale 1 (its own formulas), drawn
/// pixel by pixel onto `canvas`: the reference the exact quarter-turn path must match.
void ReferenceTurn(Canvas& canvas, s32 x, s32 y, s32 rw, s32 rh, const Image& image, s32 sx,
                   s32 sy, s32 sw, s32 sh, u32 tint, float deg, float pivot_x, float pivot_y,
                   bool flip_x, bool flip_y) {
    const auto box = TransformedImageBounds(x, y, rw, rh, deg, 1.0f, pivot_x, pivot_y);
    const float ax = static_cast<float>(x) + pivot_x, ay = static_cast<float>(y) + pivot_y;
    const float rad = deg * 3.14159265f / 180.0f;
    const float c = std::round(std::cos(rad)), s = std::round(std::sin(rad));
    const float fw = static_cast<float>(rw), fh = static_cast<float>(rh);
    for (s32 py = box[1]; py < box[3]; ++py) {
        for (s32 px = box[0]; px < box[2]; ++px) {
            if (px < 0 || py < 0 || px >= static_cast<s32>(canvas.Width()) ||
                py >= static_cast<s32>(canvas.Height())) {
                continue;
            }
            const float dx = static_cast<float>(px) + 0.5f - ax;
            const float dy = static_cast<float>(py) + 0.5f - ay;
            const float lx = (dx * c + dy * s) + pivot_x;
            const float ly = (-dx * s + dy * c) + pivot_y;
            if (!(lx >= 0.0f && lx < fw && ly >= 0.0f && ly < fh)) {
                continue;
            }
            const s32 off_x = static_cast<s32>(static_cast<s64>(std::min(static_cast<s32>(lx), rw - 1)) * sw / rw);
            const s32 off_y = static_cast<s32>(static_cast<s64>(std::min(static_cast<s32>(ly), rh - 1)) * sh / rh);
            const s32 src_x = flip_x ? sx + (sw - 1) - off_x : sx + off_x;
            const s32 src_y = flip_y ? sy + (sh - 1) - off_y : sy + off_y;
            if (src_x < 0 || src_y < 0 || src_x >= static_cast<s32>(image.w) ||
                src_y >= static_cast<s32>(image.h)) {
                continue;
            }
            const u32 texel = image.pixels[static_cast<size_t>(src_y) * image.w + src_x];
            canvas.DrawImageRegion(px, py, 1, 1, image, src_x, src_y, 1, 1, tint);
            (void)texel;
        }
    }
}

} // namespace

TEST_CASE("DSMod image: exact quarter turns match the generic inverse map", "[dsmod][ui][image]") {
    const Image pic = Picture(7, 4);
    for (const float deg : {90.0f, 180.0f, 270.0f}) {
        for (const bool flip : {false, true}) {
            for (const float pivot : {0.5f, 0.0f, 0.3f}) {
                Canvas fast, ref;
                fast.Resize(40, 40);
                ref.Resize(40, 40);
                fast.Clear(0xFF101010u);
                ref.Clear(0xFF101010u);
                const s32 rw = 14, rh = 8;
                const float px = pivot * static_cast<float>(rw), py = pivot * static_cast<float>(rh);
                fast.DrawImageTransformed(12, 16, rw, rh, pic, 0, 0, 7, 4, 0xFFFFFFFFu, deg, 1.0f,
                                          px, py, flip, !flip);
                ReferenceTurn(ref, 12, 16, rw, rh, pic, 0, 0, 7, 4, 0xFFFFFFFFu, deg, px, py,
                              flip, !flip);
                INFO("deg " << deg << " flip " << flip << " pivot " << pivot);
                REQUIRE(fast.Pixels() == ref.Pixels());
                // Something was drawn (the turned rect lands on the canvas).
                REQUIRE(fast.Pixels() != std::vector<u32>(40 * 40, 0xFF101010u));
            }
        }
    }
    // A tint and a clip both apply on the fast path as on the generic one.
    Canvas fast, ref;
    fast.Resize(40, 40);
    ref.Resize(40, 40);
    fast.Clear(0);
    ref.Clear(0);
    fast.SetClip(14, 10, 10, 10);
    ref.SetClip(14, 10, 10, 10);
    fast.DrawImageTransformed(12, 16, 14, 8, pic, 0, 0, 7, 4, 0x80FF8040u, 90.0f, 1.0f, 7.0f, 4.0f,
                              false, false);
    ReferenceTurn(ref, 12, 16, 14, 8, pic, 0, 0, 7, 4, 0x80FF8040u, 90.0f, 7.0f, 4.0f, false, false);
    REQUIRE(fast.Pixels() == ref.Pixels());
}

TEST_CASE("DSMod map: animated marker frame and pulse follow the clock", "[dsmod][map]") {
    REQUIRE(MarkerFrameAt(4, 100, 0) == 0);
    REQUIRE(MarkerFrameAt(4, 100, 99) == 0);
    REQUIRE(MarkerFrameAt(4, 100, 100) == 1);
    REQUIRE(MarkerFrameAt(4, 100, 450) == 0);
    REQUIRE(MarkerFrameAt(0, 100, 450) == 0);
    REQUIRE(MarkerFrameAt(3, 0, 5) == 2); // frame_ms 0 counts as 1 ms
    DynamicMarkerDef::Pulse none;
    REQUIRE(MarkerPulseAt(none, 123) == std::array<float, 2>{1.0f, 1.0f});
    DynamicMarkerDef::Pulse breathe;
    breathe.period_ms = 1000;
    breathe.alpha_lo = 0.5f;
    breathe.alpha_hi = 1.0f;
    breathe.scale_lo = 1.0f;
    breathe.scale_hi = 1.2f;
    auto v = MarkerPulseAt(breathe, 0);
    REQUIRE(std::fabs(v[0] - 0.5f) < 1e-4f);
    REQUIRE(std::fabs(v[1] - 1.0f) < 1e-4f);
    v = MarkerPulseAt(breathe, 500);
    REQUIRE(std::fabs(v[0] - 1.0f) < 1e-4f);
    REQUIRE(std::fabs(v[1] - 1.2f) < 1e-4f);
    v = MarkerPulseAt(breathe, 1250);
    REQUIRE(std::fabs(v[0] - 0.75f) < 1e-4f);
    DynamicMarkerDef::Pulse grow;
    grow.period_ms = 600;
    grow.grow = true;
    grow.scale_lo = 0.25f;
    grow.scale_hi = 0.8f;
    grow.alpha_hi = 1.0f;
    v = MarkerPulseAt(grow, 0);
    REQUIRE(std::fabs(v[1] - 0.25f) < 1e-4f);
    REQUIRE(std::fabs(v[0] - 1.0f) < 1e-4f);
    v = MarkerPulseAt(grow, 390); // 65 %: still growing, still opaque
    REQUIRE(std::fabs(v[1] - (0.25f + 0.55f * (0.65f / 0.75f))) < 1e-3f);
    REQUIRE(std::fabs(v[0] - 1.0f) < 1e-4f);
    v = MarkerPulseAt(grow, 450); // 75 %: full size, a quarter of the way through the fade
    REQUIRE(std::fabs(v[1] - 0.8f) < 1e-4f);
    REQUIRE(std::fabs(v[0] - 0.75f) < 1e-3f);
    v = MarkerPulseAt(grow, 500); // in the last third: fading
    REQUIRE(v[0] < 0.75f);
    REQUIRE(v[0] > 0.0f);
    v = MarkerPulseAt(grow, 599);
    REQUIRE(v[0] < 0.02f);
    DynamicMarkerDef def;
    REQUIRE_FALSE(def.Animated());
    def.frames = {"a", "b"};
    REQUIRE(def.Animated());
}

TEST_CASE("DSMod manifest: preload list and animated marker keys parse", "[dsmod][manifest]") {
    const auto json = nlohmann::json::parse(R"({"format":1,"pages":[{"id":"p","widgets":[]}],
        "preload": ["romfs:Pack/Bootup.pack#Layout/Common.sblarc#timg/Nt_MapMapPinIcon1_00.bntx", "", 7,
                    "file:ui/ring.png"],
        "map": {"areas": {"hyrule": {"image": "file:map.png", "dynamic_markers": [
            {"group": "pins", "count": 5, "x": "pin.{i}.x", "y": "pin.{i}.y",
             "frames": ["file:flame0.png", "file:flame1.png"], "frame_ms": 80,
             "pulse": {"period_ms": 666, "alpha": [0.5, 1], "scale": [0.25, 0.8], "mode": "grow"}},
            {"group": "quest", "count": 1, "x": "q.x", "y": "q.y"}]}}}
    })");
    Manifest m;
    REQUIRE(ParseDualScreenManifest(json, m));
    REQUIRE(m.preload.size() == 2);
    REQUIRE(m.preload[1] == "file:ui/ring.png");
    const auto area = m.map_areas.find("hyrule");
    REQUIRE(area != m.map_areas.end());
    REQUIRE(area->second.dynamic_markers.size() == 2);
    const auto& pins = area->second.dynamic_markers[0];
    REQUIRE(pins.frames.size() == 2);
    REQUIRE(pins.frame_ms == 80);
    REQUIRE(pins.pulse.period_ms == 666);
    REQUIRE(pins.pulse.grow);
    REQUIRE(std::fabs(pins.pulse.scale_lo - 0.25f) < 1e-6f);
    REQUIRE(std::fabs(pins.pulse.alpha_lo - 0.5f) < 1e-6f);
    REQUIRE(pins.Animated());
    REQUIRE_FALSE(area->second.dynamic_markers[1].Animated());
}
