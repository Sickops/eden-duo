// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
// Two-axis scroll regions (ScrollRegion::axes, content_w/h[_bind], Widget::scroll_follow,
// "$tap" payloads): boot-free checks of the pieces the runtime and the renderer share.
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include "core/mods/mod_ui.h"

using namespace Core::Mods;

namespace {

/// A Prices-table-like page: a 600x400 viewport over one 1800x1200 pane picture (repeat 1), a
/// frozen header row (follows x only) and a frozen label column (follows y only).
Page TablePage(bool declared_extent = true) {
    Page page;
    ScrollRegion region;
    region.id = "tbl";
    region.rect = {100, 200, 600, 400};
    region.axes = ScrollRegion::AxisX | ScrollRegion::AxisY;
    region.bar_color = 0xFFFF0000u;
    region.bar_w = 4;
    if (declared_extent) {
        region.content_w = 1800;
        region.content_h_bind = "tbl.h";
    }
    page.scrolls.push_back(region);

    Widget pane;
    pane.type = WidgetType::Rect;
    pane.rect = {100, 200, 1800, 1200};
    pane.repeat = 1;
    pane.scroll = "tbl";
    pane.on_tap = "tbl.cell";
    pane.payload = "$tap";
    page.widgets.push_back(pane);

    Widget header;
    header.type = WidgetType::Rect;
    header.rect = {100, 160, 1800, 40};
    header.repeat = 1;
    header.scroll = "tbl";
    header.scroll_follow = ScrollRegion::AxisX;
    page.widgets.push_back(header);

    Widget labels;
    labels.type = WidgetType::Rect;
    labels.rect = {0, 200, 100, 1200};
    labels.repeat = 1;
    labels.scroll = "tbl";
    labels.scroll_follow = ScrollRegion::AxisY;
    page.widgets.push_back(labels);
    return page;
}

StateSnapshot State(s64 ox, s64 oy, s64 content_h = 1200) {
    StateSnapshot s;
    s.ints[ScrollOffsetXKey("tbl")] = ox;
    s.ints[ScrollOffsetKey("tbl")] = oy;
    s.ints["tbl.h"] = content_h;
    s.ints["@scroll_on:tbl"] = 1;
    return s;
}

} // namespace

TEST_CASE("DSMod scroll 2D: declared extents give both axes a range", "[dsmod][ui][scroll]") {
    const Page page = TablePage();
    const ScrollRegion& r = page.scrolls[0];
    const auto m = MeasureScroll(page, r, State(0, 0));
    REQUIRE(m.content_w == 1800);
    REQUIRE(m.content_h == 1200);
    REQUIRE(m.max_offset_x == 1800 - 600);
    REQUIRE(m.max_offset == 1200 - 400);
    // The bound height is live: shorter content, shorter range; content that fits has none.
    REQUIRE(MeasureScroll(page, r, State(0, 0, 700)).max_offset == 300);
    REQUIRE(MeasureScroll(page, r, State(0, 0, 400)).max_offset == 0);
    // Offsets clamp like the y axis does; a y-only region never reports an x offset.
    REQUIRE(ScrollOffsetX(r, State(-5, 0)) == 0);
    REQUIRE(ScrollOffsetX(r, State(250, 0)) == 250);
    ScrollRegion y_only = r;
    y_only.axes = ScrollRegion::AxisY;
    REQUIRE(ScrollOffsetX(y_only, State(250, 0)) == 0);
    REQUIRE(MeasureScroll(page, y_only, State(0, 0)).max_offset_x == 0);
    // An x-only region never moves its rows.
    ScrollRegion x_only = r;
    x_only.axes = ScrollRegion::AxisX;
    REQUIRE(MeasureScroll(page, x_only, State(0, 0)).max_offset == 0);
    REQUIRE(MeasureScroll(page, x_only, State(0, 0)).max_offset_x == 1200);
}

TEST_CASE("DSMod scroll 2D: x extent from the templates when none is declared",
          "[dsmod][ui][scroll]") {
    Page page = TablePage(false);
    // Three 1800-wide templates at the rect origin: their span is the content.
    auto m = MeasureScroll(page, page.scrolls[0], State(0, 0));
    REQUIRE(m.content_w == 1800);
    REQUIRE(m.max_offset_x == 1200);
    // Without a declared height, a repeat-1 template has no row pitch: nothing to scroll on y.
    REQUIRE(m.max_offset == 0);
    // A horizontal strip of cells: (n - 1) * repeat_dx + cell width.
    Page strip;
    ScrollRegion region;
    region.id = "s";
    region.rect = {0, 0, 300, 100};
    region.axes = ScrollRegion::AxisX;
    strip.scrolls.push_back(region);
    Widget cell;
    cell.type = WidgetType::Rect;
    cell.rect = {10, 0, 80, 100};
    cell.repeat = 12;
    cell.repeat_dx = 90;
    cell.scroll = "s";
    strip.widgets.push_back(cell);
    m = MeasureScroll(strip, strip.scrolls[0], StateSnapshot{});
    REQUIRE(m.content_w == 10 + 11 * 90 + 80);
    REQUIRE(m.max_offset_x == m.content_w - 300);
}

TEST_CASE("DSMod scroll 2D: scroll_follow moves a widget on its own axes only",
          "[dsmod][ui][scroll]") {
    const Page page = TablePage();
    const ScrollRegion& r = page.scrolls[0];
    REQUIRE(ScrollShift(page.widgets[0], r, 300, 500) == std::array<s32, 2>{300, 500});
    REQUIRE(ScrollShift(page.widgets[1], r, 300, 500) == std::array<s32, 2>{300, 0});
    REQUIRE(ScrollShift(page.widgets[2], r, 300, 500) == std::array<s32, 2>{0, 500});
    // A y-only region ignores every widget's x follow.
    ScrollRegion y_only = r;
    y_only.axes = ScrollRegion::AxisY;
    REQUIRE(ScrollShift(page.widgets[0], y_only, 300, 500) == std::array<s32, 2>{0, 500});

    const auto expanded = ExpandWidgets(page, State(300, 500));
    REQUIRE(expanded.size() == 3);
    REQUIRE(expanded[0].rect[0] == 100 - 300); // the pane moves on both axes
    REQUIRE(expanded[0].rect[1] == 200 - 500);
    REQUIRE(expanded[1].rect[0] == 100 - 300); // the header follows the columns only
    REQUIRE(expanded[1].rect[1] == 160);
    REQUIRE(expanded[2].rect[0] == 0); // the label column follows the rows only
    REQUIRE(expanded[2].rect[1] == 200 - 500);
    // The pane clips to the region's rect; the header and the label column are unbounded on
    // the axis they do not follow (they sit outside the rect on it).
    REQUIRE(expanded[0].scroll_clip == r.rect);
    REQUIRE(expanded[1].scroll_clip[0] == r.rect[0]);
    REQUIRE(expanded[1].scroll_clip[2] == r.rect[2]);
    REQUIRE(expanded[1].scroll_clip[1] < 0);
    REQUIRE(expanded[1].scroll_clip[1] + expanded[1].scroll_clip[3] > 100000);
    REQUIRE(expanded[2].scroll_clip[1] == r.rect[1]);
    REQUIRE(expanded[2].scroll_clip[3] == r.rect[3]);
    REQUIRE(expanded[2].scroll_clip[0] < 0);
    // The header at y 160 (above the viewport) is still drawn and tappable at its own place.
    REQUIRE(HitTest(page, State(300, 500), 150, 170).empty()); // no on_tap on it...
    Page tappable = page;
    tappable.widgets[1].on_tap = "tbl.sort";
    REQUIRE(HitTest(tappable, State(300, 500), 150, 170) == "tbl.sort");
    // An oversized offset clamps to the range.
    const auto clamped = ExpandWidgets(page, State(99999, 99999));
    REQUIRE(clamped[0].rect[0] == 100 - 1200);
    REQUIRE(clamped[0].rect[1] == 200 - 800);
}

TEST_CASE("DSMod scroll 2D: $tap carries the content-relative point", "[dsmod][ui][scroll]") {
    const Page page = TablePage();
    const auto s = State(300, 500);
    const auto expanded = ExpandWidgets(page, s);
    // A tap at canvas (150, 260) inside the viewport: content x = 150 - 100 + 300 = 350,
    // y = 260 - 200 + 500 = 560.
    const auto payload = TapPayload(page, expanded[0], s, 150, 260);
    REQUIRE(payload.has_value());
    const u64 bits = std::bit_cast<u64>(*payload);
    REQUIRE(static_cast<s32>(static_cast<u32>(bits >> 32)) == 350);
    REQUIRE(static_cast<s32>(static_cast<u32>(bits & 0xFFFFFFFFu)) == 560);
    // The hit test still resolves the pane through the region's clip at that point.
    REQUIRE(HitTest(page, s, 150, 260) == "tbl.cell");
    // Outside a region, "$tap" is relative to the widget's own rect.
    Widget plain;
    plain.rect = {40, 50, 100, 100};
    plain.payload = "$tap";
    const u64 b2 = std::bit_cast<u64>(*TapPayload(page, plain, s, 45, 70));
    REQUIRE(static_cast<s32>(static_cast<u32>(b2 >> 32)) == 5);
    REQUIRE(static_cast<s32>(static_cast<u32>(b2 & 0xFFFFFFFFu)) == 20);
    // Other payloads keep their meaning.
    plain.payload = "17";
    REQUIRE(TapPayload(page, plain, s, 45, 70) == 17);
}

TEST_CASE("DSMod scroll 2D: horizontal thumb along the bottom edge", "[dsmod][ui][scroll]") {
    const Page page = TablePage();
    const ScrollRegion& r = page.scrolls[0];
    const auto m = MeasureScroll(page, r, State(0, 0));
    // Track 600 over content 1800 -> thumb 200 px; at offset 600 of 1200 it starts mid-track.
    const auto t0 = ScrollBarThumbX(r, m, 0);
    REQUIRE(t0 == std::array<s32, 4>{100, 200 + 400 - 4, 200, 4});
    const auto t1 = ScrollBarThumbX(r, m, 600);
    REQUIRE(t1[0] == 100 + (600 - 200) * 600 / 1200);
    REQUIRE(t1[2] == 200);
    // None on a y-only region or without a bar colour.
    ScrollRegion y_only = r;
    y_only.axes = ScrollRegion::AxisY;
    REQUIRE(ScrollBarThumbX(y_only, MeasureScroll(page, y_only, State(0, 0)), 0)[2] == 0);
    ScrollRegion no_bar = r;
    no_bar.bar_color = 0;
    REQUIRE(ScrollBarThumbX(no_bar, m, 0)[2] == 0);
    // It renders: the thumb row is bar-coloured where the thumb is and not past it.
    Manifest manifest;
    manifest.background = 0xFF000000u;
    Canvas canvas;
    canvas.Resize(800, 700);
    REQUIRE(RenderPage(canvas, manifest, page, State(0, 0)));
    const auto px = [&](s32 x, s32 y) { return canvas.Pixels()[static_cast<size_t>(y) * 800 + x]; };
    REQUIRE(px(150, 598) == 0xFFFF0000u);
    REQUIRE(px(450, 598) != 0xFFFF0000u);
}
