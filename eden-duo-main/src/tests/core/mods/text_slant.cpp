// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
// Leaning text (Widget::text_slant, Canvas::SetTextSlant): each glyph scanline shifts right by
// its height above the baseline; the baseline row stays put, advances and measuring are
// unchanged, and 0 keeps today's upright rendering pixel for pixel.
#include <catch2/catch_test_macros.hpp>
#include "core/mods/mod_ui.h"

using namespace Core::Mods;

namespace {

constexpr u32 Ink = 0xFFFFFFFFu;
constexpr u32 Bg = 0xFF000000u;

/// A one-glyph game font: a 4x10 solid bar ('I'), baseline at the bar's bottom.
struct BarFont {
    Image atlas;
    FontMetrics font;
    BarFont() {
        atlas.w = 4;
        atlas.h = 10;
        atlas.pixels.assign(40, 0xFFFFFFFFu);
        font.line_height = 10;
        font.ascent = 10;
        font.glyphs.assign(96, FontGlyph{0, 0, 4, 10, 0, 10, 6});
    }
};

s32 LeftInk(const Canvas& canvas, s32 y) {
    for (u32 x = 0; x < canvas.Width(); ++x) {
        if (canvas.Pixels()[static_cast<size_t>(y) * canvas.Width() + x] != Bg) {
            return static_cast<s32>(x);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("DSMod text slant: game-font scanlines lean about the baseline", "[dsmod][ui][text]") {
    BarFont f;
    Canvas canvas;
    canvas.Resize(64, 32);
    canvas.Clear(Bg);
    canvas.SetFont(&f.atlas, &f.font);
    // Scale 2 = 10 px cap height: the bar fills rows 4..13 (baseline at y + 10 = 14), x 10..13.
    canvas.SetTextSlant(45.0f); // tan = 1: one pixel of lean per pixel of height
    canvas.DrawText(10, 4, "I", 2, Ink);
    REQUIRE(LeftInk(canvas, 13) == 10); // the bottom row sits on the pen
    REQUIRE(LeftInk(canvas, 12) == 11);
    REQUIRE(LeftInk(canvas, 4) == 19); // the top row leans 9 px right
    REQUIRE(LeftInk(canvas, 3) == -1);
    REQUIRE(LeftInk(canvas, 14) == -1);
    // Width is unchanged: the pen advances the same, so a second glyph starts where it did.
    REQUIRE(canvas.MeasureText("II", 2) == 12);
    // Upright again: identical to a canvas that never leaned.
    canvas.SetTextSlant(0.0f);
    canvas.Clear(Bg);
    canvas.DrawText(10, 4, "I", 2, Ink);
    Canvas plain;
    plain.Resize(64, 32);
    plain.Clear(Bg);
    plain.SetFont(&f.atlas, &f.font);
    plain.DrawText(10, 4, "I", 2, Ink);
    REQUIRE(canvas.Pixels() == plain.Pixels());
    canvas.SetFont(nullptr, nullptr);
    plain.SetFont(nullptr, nullptr);
}

TEST_CASE("DSMod text slant: the built-in font leans too, and the widget key drives it",
          "[dsmod][ui][text]") {
    Canvas canvas;
    canvas.Resize(64, 32);
    canvas.Clear(Bg);
    canvas.SetTextSlant(45.0f);
    // 'l' in the 3x5 font is a vertical bar; at scale 2 its rows are 2 px tall, so row 0 (top)
    // leans 10 px and the bottom row 2 px (its height above the cell's baseline).
    canvas.DrawText(10, 4, "l", 2, Ink);
    const s32 top = LeftInk(canvas, 4);
    const s32 bottom = LeftInk(canvas, 13);
    REQUIRE(top >= 0);
    REQUIRE(bottom >= 0);
    REQUIRE(top - bottom == 8);
    // Through a page: a slanted label vs an upright one.
    Manifest manifest;
    manifest.background = Bg;
    Page page;
    Widget label;
    label.type = WidgetType::Label;
    label.rect = {10, 4, 40, 10};
    label.text = "l";
    label.text_scale = 2;
    label.color = Ink;
    page.widgets.push_back(label);
    Canvas upright;
    upright.Resize(64, 32);
    REQUIRE(RenderPage(upright, manifest, page, StateSnapshot{}));
    page.widgets[0].text_slant = 45.0f;
    Canvas leaning;
    leaning.Resize(64, 32);
    REQUIRE(RenderPage(leaning, manifest, page, StateSnapshot{}));
    REQUIRE(LeftInk(leaning, 4) - LeftInk(upright, 4) == 10);
    REQUIRE(LeftInk(leaning, 13) - LeftInk(upright, 13) == 2);
}
