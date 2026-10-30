// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Image "pan_bounds": the part of a pan_zoom picture the view window may show (ClampPanAxis,
// mod_view_default.h) -- in the draw clamp, the default view, the view_custom comparison, the
// expanded copies and the manifest. Without the key everything behaves as before.
#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/mods/mod_runtime.h"
#include "core/mods/mod_ui.h"
#include "core/mods/mod_view_default.h"

using namespace Core::Mods;
using Catch::Approx;

namespace {
constexpr u32 Red = 0xFFFF0000u;
constexpr u32 Blue = 0xFF0000FFu;
constexpr u32 White = 0xFFFFFFFFu;
constexpr u32 Black = 0xFF000000u;

u32 PixelAt(const Canvas& canvas, s32 x, s32 y) {
    return canvas.Pixels()[static_cast<size_t>(y) * canvas.Width() + static_cast<size_t>(x)];
}

/// 20 x 20: the left half red, the right half blue.
std::shared_ptr<const Image> HalfAndHalf() {
    auto image = std::make_shared<Image>();
    image->w = 20;
    image->h = 20;
    image->pixels.resize(400);
    for (u32 y = 0; y < 20; ++y) {
        for (u32 x = 0; x < 20; ++x) {
            image->pixels[y * 20 + x] = x < 10 ? Red : Blue;
        }
    }
    return image;
}

/// A 1000 x 800 pan_zoom image widget, `bounds` as its pan_bounds.
Widget PanImage(std::optional<PanBounds> bounds = std::nullopt) {
    Widget w;
    w.type = WidgetType::Image;
    w.id = "pic";
    w.rect = {0, 0, 1000, 800};
    w.src = "file:pic.png";
    w.color = White;
    w.pan_zoom = true;
    w.pan_bounds = bounds;
    return w;
}

/// Draws `widget` (its rect at the page's top-left) on a 40 x 40 page with `view` as its view.
Canvas Draw(Widget widget, const ViewTransform& view) {
    Canvas canvas;
    canvas.Resize(40, 40);
    Manifest manifest;
    manifest.background = Black;
    Page page;
    page.id = "p";
    widget.rect = {0, 0, 20, 20};
    page.widgets.push_back(widget);
    ViewState views;
    views[widget.id] = view;
    const auto image = HalfAndHalf();
    RenderPage(canvas, manifest, page, {}, [image](const std::string&) { return image; }, views);
    return canvas;
}

/// The classic clamp: [0, size * (1 - 1 / zoom)].
float Classic(float pan, s32 size, float zoom) {
    const float s = static_cast<float>(size);
    return std::clamp(pan, 0.0f, std::max(0.0f, s * (1.0f - 1.0f / zoom)));
}
} // namespace

TEST_CASE("DSMod pan_bounds: a window that fits inside the bounds is kept within them",
          "[dsmod][pan_bounds]") {
    // 1000 px, zoom 2: a 500 px window; bounds u 0.25..1.0 = 250..1000 px, so the origin may be
    // 250..500.
    REQUIRE(ClampPanAxis(0.0f, 1000, 2.0f, 0.25f, 1.0f) == Approx(250.0f));
    REQUIRE(ClampPanAxis(-300.0f, 1000, 2.0f, 0.25f, 1.0f) == Approx(250.0f));
    REQUIRE(ClampPanAxis(400.0f, 1000, 2.0f, 0.25f, 1.0f) == Approx(400.0f)); // inside: kept
    REQUIRE(ClampPanAxis(900.0f, 1000, 2.0f, 0.25f, 1.0f) == Approx(500.0f));
    // a window exactly as wide as the bounds has one place to be
    REQUIRE(ClampPanAxis(0.0f, 1000, 2.0f, 0.3f, 0.8f) == Approx(300.0f));
    REQUIRE(ClampPanAxis(999.0f, 1000, 2.0f, 0.3f, 0.8f) == Approx(300.0f));
    // zoomed further in, the window roams the bounds
    REQUIRE(ClampPanAxis(0.0f, 1000, 4.0f, 0.3f, 0.8f) == Approx(300.0f));
    REQUIRE(ClampPanAxis(450.0f, 1000, 4.0f, 0.3f, 0.8f) == Approx(450.0f));
    REQUIRE(ClampPanAxis(700.0f, 1000, 4.0f, 0.3f, 0.8f) == Approx(550.0f));
    // an unsized axis has no pan
    REQUIRE(ClampPanAxis(50.0f, 0, 2.0f, 0.3f, 0.8f) == 0.0f);
}

TEST_CASE("DSMod pan_bounds: a window wider than the bounds is centred on them",
          "[dsmod][pan_bounds]") {
    // 1000 px, zoom 2 (500 px window); bounds 0.5..0.75 = 250 px: centred at 625 -> origin 375,
    // whatever the pan asked for.
    REQUIRE(ClampPanAxis(0.0f, 1000, 2.0f, 0.5f, 0.75f) == Approx(375.0f));
    REQUIRE(ClampPanAxis(375.0f, 1000, 2.0f, 0.5f, 0.75f) == Approx(375.0f));
    REQUIRE(ClampPanAxis(2000.0f, 1000, 2.0f, 0.5f, 0.75f) == Approx(375.0f));
    // ... but still on the picture: bounds at the far edge centre past it; the window stops at
    // size - window
    REQUIRE(ClampPanAxis(0.0f, 1000, 2.0f, 0.9f, 1.0f) == Approx(500.0f));
    REQUIRE(ClampPanAxis(0.0f, 1000, 2.0f, 0.0f, 0.1f) == Approx(0.0f));
    // zoom 1 shows the whole picture: origin 0, bounds or not
    REQUIRE(ClampPanAxis(300.0f, 1000, 1.0f, 0.5f, 0.75f) == Approx(0.0f));
    // zoomed out past the picture (min_zoom < 1): origin 0, as before
    REQUIRE(ClampPanAxis(300.0f, 1000, 0.5f, 0.5f, 0.75f) == Approx(0.0f));
}

TEST_CASE("DSMod pan_bounds: without the key the clamp is the classic one, and the stored pan "
          "is left alone",
          "[dsmod][pan_bounds]") {
    for (const float zoom : {0.5f, 1.0f, 1.5f, 2.0f, 4.0f, 8.0f}) {
        for (const float pan : {-100.0f, 0.0f, 123.0f, 500.0f, 875.0f, 2000.0f}) {
            REQUIRE(ClampPanAxis(pan, 1000, zoom, 0.0f, 1.0f) == Approx(Classic(pan, 1000, zoom)));
            REQUIRE(ClampPanAxis(pan, 800, zoom, 0.0f, 1.0f) == Approx(Classic(pan, 800, zoom)));
        }
    }
    // ClampViewPan: both axes, the widget's own bounds or the whole picture
    Widget plain = PanImage();
    float px = 900.0f, py = -50.0f;
    ClampViewPan(plain, 2.0f, px, py);
    REQUIRE(px == Approx(500.0f));
    REQUIRE(py == Approx(0.0f));
    Widget bounded = PanImage(PanBounds{0.25f, 0.5f, 1.0f, 1.0f});
    px = 0.0f, py = 0.0f;
    ClampViewPan(bounded, 2.0f, px, py);
    REQUIRE(px == Approx(250.0f));
    REQUIRE(py == Approx(400.0f)); // v 0.5..1.0 of 800 px = 400..800, a 400 px window
    // ClampViewToBounds: a gesture's stored pan is clamped only when the widget has bounds (the
    // draw clamp keeps a plain widget's picture on its frame, as before)
    ViewTransform v;
    v.zoom = 2.0f;
    v.pan_x = 900.0f;
    v.pan_y = -50.0f;
    ClampViewToBounds(plain, v);
    REQUIRE(v.pan_x == 900.0f);
    REQUIRE(v.pan_y == -50.0f);
    ClampViewToBounds(bounded, v);
    REQUIRE(v.pan_x == Approx(500.0f));
    REQUIRE(v.pan_y == Approx(400.0f));
}

TEST_CASE("DSMod pan_bounds: the default view is centred on the bound point within the bounds",
          "[dsmod][pan_bounds][view_default]") {
    // no bounds: as runtime 15 (window u 0.25..0.75 -> pan 250)
    auto h = DefaultView(1000, 800, 2.0, 0.5, 0.5, 1.0f, 4.0f);
    REQUIRE(h);
    REQUIRE(h->pan_x == Approx(250.0f));
    REQUIRE(h->pan_y == Approx(200.0f));
    // the window fits the bounds: pushed inside them
    h = DefaultView(1000, 800, 2.0, 0.5, 0.5, 1.0f, 4.0f, PanBounds{0.5f, 0.0f, 1.0f, 1.0f});
    REQUIRE(h);
    REQUIRE(h->pan_x == Approx(500.0f));
    REQUIRE(h->pan_y == Approx(200.0f));
    h = DefaultView(1000, 800, 2.0, 0.5, 0.5, 1.0f, 4.0f, PanBounds{0.0f, 0.0f, 0.6f, 0.4f});
    REQUIRE(h);
    REQUIRE(h->pan_x == Approx(100.0f)); // 0..600 px, a 500 px window: origin <= 100
    REQUIRE(h->pan_y == Approx(0.0f));   // 0..320 px < the 400 px window: centred, then on the
                                         // picture
    // the point already inside the bounds is kept
    h = DefaultView(1000, 800, 4.0, 0.5, 0.5, 1.0f, 4.0f, PanBounds{0.25f, 0.25f, 0.75f, 0.75f});
    REQUIRE(h);
    REQUIRE(h->pan_x == Approx(375.0f));
    REQUIRE(h->pan_y == Approx(300.0f));
    // a window wider than the bounds: centred on them
    h = DefaultView(1000, 800, 2.0, 0.1, 0.9, 1.0f, 4.0f, PanBounds{0.5f, 0.0f, 0.75f, 1.0f});
    REQUIRE(h);
    REQUIRE(h->pan_x == Approx(375.0f));
    REQUIRE(h->pan_y == Approx(400.0f));
}

TEST_CASE("DSMod pan_bounds: view_custom compares the pan as drawn",
          "[dsmod][pan_bounds][view_default]") {
    ViewTransform v;
    v.zoom = v.home_zoom = 2.0f;
    v.home_pan_x = 500.0f; // the bounds' right edge at zoom 2
    v.home_pan_y = 200.0f;
    v.pan_x = 700.0f; // overshoot past it (a view stored before the clamp, or by an older build)
    v.pan_y = 200.0f;
    REQUIRE(ViewAwayFromHome(v));
    const Widget bounded = PanImage(PanBounds{0.25f, 0.0f, 1.0f, 1.0f});
    REQUIRE_FALSE(ViewAwayFromHome(v, bounded)); // both draw the same window: home
    REQUIRE(ViewAwayFromHome(v, PanImage()));    // no bounds: the plain comparison
    v.pan_y = 150.0f;
    REQUIRE(ViewAwayFromHome(v, bounded)); // moved on the free axis
    v.pan_y = 200.0f;
    v.zoom = 3.0f;
    REQUIRE(ViewAwayFromHome(v, bounded)); // zoomed
    v.zoom = 2.0f;
    v.gliding = true;
    REQUIRE(ViewAwayFromHome(v, bounded));
}

TEST_CASE("DSMod pan_bounds: DrawImage keeps the window inside the bounds",
          "[dsmod][pan_bounds][ui]") {
    ViewTransform view;
    view.zoom = 2.0f; // the window is half the picture
    // no bounds, pan 0: the left half (red) fills the 20 px widget
    Canvas c = Draw(PanImage(), view);
    REQUIRE(PixelAt(c, 2, 10) == Red);
    REQUIRE(PixelAt(c, 17, 10) == Red);
    REQUIRE(PixelAt(c, 30, 10) == Black); // outside the widget
    // bounds u 0.5..1.0 (the blue half): the same pan is pushed to the right half
    c = Draw(PanImage(PanBounds{0.5f, 0.0f, 1.0f, 1.0f}), view);
    REQUIRE(PixelAt(c, 2, 10) == Blue);
    REQUIRE(PixelAt(c, 17, 10) == Blue);
    // a window wider than the bounds (u 0.4..0.6) is centred on them: u 0.25..0.75, red | blue,
    // whatever the pan
    for (const float pan : {-50.0f, 0.0f, 7.0f, 100.0f}) {
        view.pan_x = pan;
        c = Draw(PanImage(PanBounds{0.4f, 0.0f, 0.6f, 1.0f}), view);
        REQUIRE(PixelAt(c, 2, 10) == Red);
        REQUIRE(PixelAt(c, 8, 10) == Red);
        REQUIRE(PixelAt(c, 11, 10) == Blue);
        REQUIRE(PixelAt(c, 17, 10) == Blue);
    }
    // the plain clamp still applies without bounds: a pan past the picture shows its right edge
    view.pan_x = 100.0f;
    c = Draw(PanImage(), view);
    REQUIRE(PixelAt(c, 2, 10) == Blue);
    REQUIRE(PixelAt(c, 17, 10) == Blue);
}

TEST_CASE("DSMod pan_bounds: manifest key", "[dsmod][pan_bounds][manifest]") {
    const auto json = nlohmann::json::parse(R"({"format":1,"pages":[{"id":"p","widgets":[
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[0.1,0.2,0.9,0.8]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[0.9,0.0,0.1,1.0]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[0.0,0.5,1.0,0.5]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[-1,0.25,2,1.5]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":"0,0,1,1"},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[0,0,1]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true,
         "pan_bounds":[0,"0",1,1]},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_zoom":true},
        {"type":"image","src":"file:a.png","rect":[0,0,100,100],"pan_bounds":[0,0,0.5,0.5]},
        {"type":"rect","rect":[0,0,100,100],"pan_zoom":true,"pan_bounds":[0,0,0.5,0.5]},
        {"type":"image","src":"file:a_{i}.png","rect":[0,0,100,100],"repeat":2,"repeat_dy":120,
         "pan_zoom":true,"pan_bounds":[0,0,0.5,1]}]}]})");
    Manifest m;
    REQUIRE(ParseDualScreenManifest(json, m));
    const auto& ws = m.pages[0].widgets;
    REQUIRE(ws.size() == 11);
    REQUIRE(ws[0].pan_bounds);
    REQUIRE((*ws[0].pan_bounds)[0] == Approx(0.1f));
    REQUIRE((*ws[0].pan_bounds)[1] == Approx(0.2f));
    REQUIRE((*ws[0].pan_bounds)[2] == Approx(0.9f));
    REQUIRE((*ws[0].pan_bounds)[3] == Approx(0.8f));
    REQUIRE_FALSE(ws[1].pan_bounds); // inverted u
    REQUIRE_FALSE(ws[2].pan_bounds); // empty v
    REQUIRE(ws[3].pan_bounds);       // out of range: each value clamped to 0..1
    REQUIRE(*ws[3].pan_bounds == PanBounds{0.0f, 0.25f, 1.0f, 1.0f});
    REQUIRE_FALSE(ws[4].pan_bounds); // a string
    REQUIRE_FALSE(ws[5].pan_bounds); // three numbers
    REQUIRE_FALSE(ws[6].pan_bounds); // a non-number inside
    REQUIRE_FALSE(ws[7].pan_bounds); // absent: the whole picture, as before
    REQUIRE_FALSE(ws[8].pan_bounds); // not pan_zoom
    REQUIRE_FALSE(ws[9].pan_bounds); // not an image
    // the expanded copies of a repeat template carry the bounds
    const auto expanded = ExpandWidgets(m.pages[0], {});
    REQUIRE(expanded.size() == 12);
    REQUIRE(expanded[10].pan_bounds);
    REQUIRE(expanded[11].pan_bounds);
    REQUIRE(*expanded[11].pan_bounds == PanBounds{0.0f, 0.0f, 0.5f, 1.0f});
    REQUIRE(expanded[11].src == "file:a_1.png");
}
