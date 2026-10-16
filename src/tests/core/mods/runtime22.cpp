// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime 22: the map widget's turned view ("view_rotate_bind"): parsing and normalisation, the
// base picture and overlays drawn turned about the widget centre and clipped to it, markers placed
// through the turn but upright, marker_rotate_bind relative to the turned map, the draw record's
// inverse mapping (taps) and the live-marker dirty prediction.
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/mods/mod_runtime.h"
#include "core/mods/mod_ui.h"

using namespace Core::Mods;

namespace {
constexpr u32 Red = 0xFFFF0000u, Blue = 0xFF0000FFu, Green = 0xFF00FF00u, White = 0xFFFFFFFFu;
constexpr u32 Black = 0xFF000000u;

std::shared_ptr<const Image> Solid(u32 argb, u32 w = 8, u32 h = 8) {
    auto img = std::make_shared<Image>();
    img->w = w;
    img->h = h;
    img->pixels.assign(static_cast<size_t>(w) * h, argb);
    return img;
}

/// Area "c": world (0,0)-(100,100) with a red picture; widget "m" 100x100 at the origin (canvas
/// x = world x, canvas y = 100 - world y when unturned). One green overlay at world
/// (30,60)-(50,80) -> unturned canvas x 30..50, y 20..40. The view turns by the "turn" value.
struct CompassMap {
    Manifest manifest;
    Page page;
    std::map<std::string, std::shared_ptr<const Image>> pics{{"img.red", Solid(Red)},
                                                             {"img.green", Solid(Green)},
                                                             {"img.blue", Solid(Blue)},
                                                             {"img.white", Solid(White)}};
    CompassMap() {
        manifest.background = Black;
        MapArea area;
        area.min_x = 0.0f;
        area.max_x = 100.0f;
        area.min_y = 0.0f;
        area.max_y = 100.0f;
        area.image = "img.red";
        DynamicMarkerDef dm;
        dm.group = "pins";
        dm.count = 1;
        dm.x = "pin{i}.x";
        dm.y = "pin{i}.y";
        dm.icon_src_bind = "pin{i}.img";
        dm.size = 10;
        area.dynamic_markers.push_back(dm);
        manifest.map_areas.emplace("c", area);
        Widget w;
        w.type = WidgetType::Map;
        w.id = "m";
        w.area = "c";
        w.rect = {0, 0, 100, 100};
        w.pan_zoom = true;
        w.max_zoom = 8.0f;
        w.area_label = false;
        auto extras = std::make_shared<MapWidgetExtras>();
        extras->view_rotate_bind = "turn";
        MapWidgetExtras::Overlay ov;
        ov.src = "img.green";
        ov.x0 = 30.0f;
        ov.y0 = 60.0f;
        ov.x1 = 50.0f;
        ov.y1 = 80.0f;
        extras->overlays.push_back(ov);
        w.map_extras = extras;
        page.id = "p";
        page.widgets.push_back(w);
    }
    Widget& widget() {
        return page.widgets[0];
    }
    ImageProvider Images() const {
        return [this](const std::string& key) -> std::shared_ptr<const Image> {
            const auto it = pics.find(key);
            return it != pics.end() ? it->second : nullptr;
        };
    }
    Canvas Draw(const StateSnapshot& s, MapDrawRecords* records = nullptr,
                AuxDrawList* draw_list = nullptr, s32 size = 100) const {
        Canvas canvas;
        canvas.Resize(static_cast<u32>(size), static_cast<u32>(size));
        canvas.Clear(Black);
        ViewState views;
        views["m"].zoom = 1.0f;
        RenderPage(canvas, manifest, page, s, Images(), views, nullptr, {}, {}, draw_list, nullptr,
                   {}, records);
        return canvas;
    }
};

u32 Px(const Canvas& c, s32 x, s32 y) {
    return c.Pixels()[static_cast<size_t>(y) * c.Width() + static_cast<size_t>(x)];
}

/// Canvas point of world (wx, wy) on the 100x100 fixture, turned `deg` clockwise about (50,50).
std::array<float, 2> Turned(float wx, float wy, float deg) {
    const float dx = wx - 50.0f, dy = 50.0f - wy;
    const double r = deg * 3.14159265358979323846 / 180.0;
    const float c = static_cast<float>(std::cos(r)), s = static_cast<float>(std::sin(r));
    return {50.0f + dx * c - dy * s, 50.0f + dx * s + dy * c};
}
} // namespace

TEST_CASE("DSMod runtime 22: version", "[dsmod][runtime22]") {
    REQUIRE(DualScreenRuntimeVersion >= 22);
}

TEST_CASE("DSMod map view turn: optional key, normalised value", "[dsmod][runtime22][map-turn]") {
    const auto json = nlohmann::json::parse(R"({"format":1,"pages":[{"id":"p","widgets":[
      {"type":"map","rect":[0,0,100,100],"view_rotate_bind":"cam.turn"},
      {"type":"map","rect":[0,0,100,100],"marker_rotate_bind":"heading"}]}]})");
    Manifest manifest;
    REQUIRE(ParseDualScreenManifest(json, manifest));
    const auto& turned = manifest.pages[0].widgets[0];
    const auto& classic = manifest.pages[0].widgets[1];
    REQUIRE(turned.map_extras != nullptr);
    REQUIRE(turned.map_extras->view_rotate_bind == "cam.turn");
    REQUIRE(classic.map_extras->view_rotate_bind.empty());

    StateSnapshot s;
    REQUIRE(MapViewTurnDegrees(turned, s) == 0.0f); // missing: north-up
    REQUIRE(MapViewTurnDegrees(classic, s) == 0.0f);
    s.floats["cam.turn"] = 90.0;
    REQUIRE(MapViewTurnDegrees(turned, s) == 90.0f);
    REQUIRE(MapViewTurnDegrees(classic, s) == 0.0f); // the key is per widget
    s.floats["cam.turn"] = -90.0;
    REQUIRE(MapViewTurnDegrees(turned, s) == 270.0f);
    s.floats["cam.turn"] = 450.0;
    REQUIRE(MapViewTurnDegrees(turned, s) == 90.0f);
    s.floats["cam.turn"] = -720.0;
    REQUIRE(MapViewTurnDegrees(turned, s) == 0.0f);
    s.floats["cam.turn"] = -1e-9; // rounds to 360 in float: still [0, 360)
    REQUIRE(MapViewTurnDegrees(turned, s) >= 0.0f);
    REQUIRE(MapViewTurnDegrees(turned, s) < 360.0f);
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
        s.floats["cam.turn"] = bad;
        REQUIRE(MapViewTurnDegrees(turned, s) == 0.0f);
    }
    s.floats.erase("cam.turn");
    s.ints["cam.turn"] = -45; // an integer point works too
    REQUIRE(MapViewTurnDegrees(turned, s) == 315.0f);
}

TEST_CASE("DSMod map view turn: overlays turn about the widget centre, clipped to the widget",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    StateSnapshot s;
    // Unturned: the classic picture (overlay at x 30..50, y 20..40).
    auto c = map.Draw(s);
    REQUIRE(Px(c, 40, 30) == Green);
    REQUIRE(Px(c, 70, 40) == Red);
    s.floats["turn"] = 0.0;
    REQUIRE(Px(map.Draw(s), 40, 30) == Green);
    // 90 clockwise: centre (40,30) -> (70,40); the overlay is now x 60..80, y 30..50.
    s.floats["turn"] = 90.0;
    c = map.Draw(s);
    REQUIRE(Px(c, 70, 40) == Green);
    REQUIRE(Px(c, 62, 32) == Green);
    REQUIRE(Px(c, 78, 48) == Green);
    REQUIRE(Px(c, 40, 30) == Red);
    REQUIRE(Px(c, 70, 55) == Red);
    // 180: centre -> (60,70).
    s.floats["turn"] = 180.0;
    c = map.Draw(s);
    REQUIRE(Px(c, 60, 70) == Green);
    REQUIRE(Px(c, 40, 30) == Red);
    // An arbitrary angle: the overlay's centre lands where the turn says.
    for (const float deg : {30.0f, 137.0f, 251.0f, 333.0f}) {
        s.floats["turn"] = deg;
        c = map.Draw(s);
        const auto at = Turned(40.0f, 70.0f, deg);
        REQUIRE(Px(c, static_cast<s32>(at[0]), static_cast<s32>(at[1])) == Green);
    }
    // A widget inset in a larger canvas: the turned base (whose corners swing out past the widget)
    // never paints outside the widget rect.
    map.widget().rect = {20, 20, 60, 60};
    for (const float deg : {0.0f, 45.0f, 100.0f}) {
        s.floats["turn"] = deg;
        c = map.Draw(s, nullptr, nullptr, 100);
        for (s32 y = 0; y < 100; ++y) {
            for (s32 x = 0; x < 100; ++x) {
                const bool inside = x >= 20 && x < 80 && y >= 20 && y < 80;
                if (!inside) {
                    REQUIRE(Px(c, x, y) == Black);
                }
            }
        }
        REQUIRE(Px(c, 50, 50) != Black);
    }
}

TEST_CASE("DSMod map view turn: tiled overlays still abut (no seams)",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    auto extras = std::make_shared<MapWidgetExtras>(*map.widget().map_extras);
    extras->overlays.clear();
    // Four tiles covering the whole area with fractional canvas edges.
    for (const auto& [x0, x1] : {std::pair{0.0f, 33.3f}, std::pair{33.3f, 100.0f}}) {
        for (const auto& [y0, y1] : {std::pair{0.0f, 61.7f}, std::pair{61.7f, 100.0f}}) {
            MapWidgetExtras::Overlay ov;
            ov.src = "img.green";
            ov.x0 = x0;
            ov.x1 = x1;
            ov.y0 = y0;
            ov.y1 = y1;
            extras->overlays.push_back(ov);
        }
    }
    map.widget().map_extras = extras;
    StateSnapshot s;
    for (const float deg : {0.0f, 17.0f, 37.0f, 90.0f, 211.5f}) {
        s.floats["turn"] = deg;
        const auto c = map.Draw(s);
        // Every pixel within the inscribed circle shows a tile (the base's red never leaks).
        for (s32 y = 0; y < 100; ++y) {
            for (s32 x = 0; x < 100; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - 50.0f;
                const float dy = static_cast<float>(y) + 0.5f - 50.0f;
                if (dx * dx + dy * dy < 46.0f * 46.0f) {
                    REQUIRE(Px(c, x, y) == Green);
                }
            }
        }
    }
}

TEST_CASE("DSMod map view turn: markers follow the turn and stay upright",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    auto& w = map.widget();
    // A wide live-marker picture (24x6) without a heading: always drawn upright.
    w.marker_src = "img.white";
    w.marker_size = {24, 6};
    w.marker_anchor = {0.5f, 0.5f};
    w.marker_x_bind = "me.x";
    w.marker_y_bind = "me.y";
    StateSnapshot s;
    s.floats["me.x"] = 50.0;
    s.floats["me.y"] = 80.0; // unturned canvas (50,20)
    s.ints["pin0.x"] = 20;
    s.ints["pin0.y"] = 50; // unturned canvas (20,50)
    s.texts["pin0.img"] = "img.blue";
    s.floats["turn"] = 90.0;
    MapDrawRecords records;
    const auto c = map.Draw(s, &records);
    // The live marker at (80,50), still 24 wide and 6 tall.
    REQUIRE(Px(c, 80, 50) == White);
    REQUIRE(Px(c, 69, 50) == White);
    REQUIRE(Px(c, 91, 50) == White);
    REQUIRE(Px(c, 80, 45) != White);
    REQUIRE(Px(c, 80, 55) != White);
    // The pin (a 10 px picture) at (50,20), square and upright.
    REQUIRE(Px(c, 50, 20) == Blue);
    REQUIRE(Px(c, 46, 16) == Blue);
    REQUIRE(Px(c, 20, 50) != Blue);
    // Its tap hit point is the turned position.
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].hits.size() == 1);
    REQUIRE(records[0].hits[0].sx == Catch::Approx(50.0f).margin(0.01));
    REQUIRE(records[0].hits[0].sy == Catch::Approx(20.0f).margin(0.01));
}

TEST_CASE("DSMod map view turn: a marker heading is relative to the turned map",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    auto& w = map.widget();
    auto extras = std::make_shared<MapWidgetExtras>(*w.map_extras);
    extras->overlays.clear();
    extras->marker_rotate_bind = "heading";
    w.map_extras = extras;
    w.marker_src = "img.white";
    w.marker_size = {24, 6};
    w.marker_anchor = {0.5f, 0.5f};
    w.marker_x_bind = "me.x";
    w.marker_y_bind = "me.y";
    StateSnapshot s;
    s.floats["me.x"] = 50.0;
    s.floats["me.y"] = 50.0;
    s.floats["heading"] = 0.0;
    s.floats["turn"] = 90.0;
    // Heading 0 (map north) on a map turned 90 clockwise: the marker turns 90 too (tall).
    auto c = map.Draw(s);
    REQUIRE(Px(c, 50, 40) == White);
    REQUIRE(Px(c, 50, 60) == White);
    REQUIRE(Px(c, 40, 50) != White);
    // Heading 90 on the same turn: 180, wide again.
    s.floats["heading"] = 90.0;
    c = map.Draw(s);
    REQUIRE(Px(c, 40, 50) == White);
    REQUIRE(Px(c, 50, 40) != White);
    // Heading -90 cancels the turn: wide.
    s.floats["heading"] = -90.0;
    c = map.Draw(s);
    REQUIRE(Px(c, 40, 50) == White);
}

TEST_CASE("DSMod map view turn: the draw record maps taps back and predicts the marker box",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    auto& w = map.widget();
    auto extras = std::make_shared<MapWidgetExtras>(*w.map_extras);
    extras->marker_rotate_bind = "heading";
    w.map_extras = extras;
    w.marker_src = "img.white";
    w.marker_size = {18, 8};
    w.marker_anchor = {0.25f, 0.5f};
    w.marker_x_bind = "me.x";
    w.marker_y_bind = "me.y";
    StateSnapshot s;
    s.floats["me.x"] = 65.0;
    s.floats["me.y"] = 30.0;
    s.floats["heading"] = 20.0;
    for (const float deg : {0.0f, 45.0f, 90.0f, 200.0f, 359.0f}) {
        s.floats["turn"] = deg;
        MapDrawRecords records;
        const auto c = map.Draw(s, &records);
        REQUIRE(records.size() == 1);
        const auto& r = records[0];
        REQUIRE(r.rotation == Catch::Approx(deg));
        REQUIRE(r.view_key == "m");
        // Taps: the world point under a turned canvas point.
        for (const auto& [wx, wy] : {std::pair{40.0f, 70.0f}, std::pair{10.0f, 90.0f},
                                     std::pair{65.0f, 30.0f}}) {
            const auto at = Turned(wx, wy, deg);
            const auto back = r.World(at[0], at[1]);
            REQUIRE(back[0] == Catch::Approx(wx).margin(0.01));
            REQUIRE(back[1] == Catch::Approx(wy).margin(0.01));
        }
        if (deg == 0.0f) {
            REQUIRE(r.World(30.0f, 20.0f)[0] == Catch::Approx(r.WorldX(30.0f)));
            REQUIRE(r.World(30.0f, 20.0f)[1] == Catch::Approx(r.WorldY(20.0f)));
        }
        // The marker-only redraw's prediction contains every painted marker pixel.
        const auto box = PredictMapMarkerBox(map.manifest, w, s, r, map.Images());
        unsigned painted = 0;
        for (s32 y = 0; y < 100; ++y) {
            for (s32 x = 0; x < 100; ++x) {
                if (Px(c, x, y) != White) {
                    continue;
                }
                ++painted;
                REQUIRE(x >= box[0]);
                REQUIRE(x < box[0] + box[2]);
                REQUIRE(y >= box[1]);
                REQUIRE(y < box[1] + box[3]);
            }
        }
        REQUIRE(painted > 60);
    }
    // Unturn is the inverse of the forward turn used for drags / pinches.
    MapDrawRecord r;
    r.rotation = 63.0f;
    const double rad = 63.0 * 3.14159265358979323846 / 180.0;
    const float fx = static_cast<float>(10.0 * std::cos(rad) - 4.0 * std::sin(rad));
    const float fy = static_cast<float>(10.0 * std::sin(rad) + 4.0 * std::cos(rad));
    const auto v = r.Unturn(fx, fy);
    REQUIRE(v[0] == Catch::Approx(10.0f).margin(1e-4));
    REQUIRE(v[1] == Catch::Approx(4.0f).margin(1e-4));
}

TEST_CASE("DSMod map view turn: the GPU path draws the turned base into the canvas",
          "[dsmod][runtime22][map-turn]") {
    CompassMap map;
    StateSnapshot s;
    AuxDrawList unturned;
    map.Draw(s, nullptr, &unturned);
    bool had_map_quad = false;
    for (const auto& q : unturned.quads) {
        had_map_quad = had_map_quad || (!q.solid && q.slot == 0u);
    }
    REQUIRE(had_map_quad);
    s.floats["turn"] = 90.0;
    AuxDrawList turned;
    const auto c = map.Draw(s, nullptr, &turned);
    REQUIRE(turned.active);
    for (const auto& q : turned.quads) {
        REQUIRE((q.solid || q.slot != 0u)); // no axis-aligned map quad for a turned view
    }
    REQUIRE(Px(c, 70, 40) == Green);
    REQUIRE(Px(c, 40, 30) == Red);
}
