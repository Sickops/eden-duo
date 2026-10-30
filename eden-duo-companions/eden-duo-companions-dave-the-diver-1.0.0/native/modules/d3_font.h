// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III Switch .fnt: pre-rasterised bitmap fonts (R8 XTX pages) at baked point sizes.
// Text is drawn 1:1 at a baked size, never scaled. Layout relative to the SNO body (file+0x10):
// 0xA8 SerializeData(page records, 40 B), 0xB8 u32 pages, 0xBC u32 sizes, 0xC0 SerializeData(size
// records: i32[8] 26.6 metrics, u32 ?, u32 glyphs, SerializeData(glyphs, 24 B), ..., f32 pt last).
// Reference: visual/tools/d3font.py.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "d3_img.h"

namespace d3_font {

struct Glyph {
    int uv = 0, bx = 0, by = 0, adv = 0, page = 0;
};

class Font {
public:
    bool Parse(std::span<const std::uint8_t> fnt);
    bool Ready() const {
        return !sizes.empty();
    }
    /// Index of the baked size `pt`, or -1.
    int SizeIndex(float pt) const;
    int Measure(int size, std::string_view utf8) const;
    int LineHeight(int size) const;
    int Ascent(int size) const;
    /// Visible glyph top/bottom relative to the line-box origin; bottom is exclusive.
    std::array<int, 2> InkBounds(int size, std::string_view utf8) const;
    /// Line-box origin that centres the actual visible glyph ink at `middle`.
    int CenteredY(int size, std::string_view utf8, int middle) const;
    /// Draws at the top-left (x, y) of the line box; returns the advance. `halo` > 0 draws an
    /// 8-direction dark halo of that many pixels first.
    int Draw(d3_img::Image& dst, int x, int y, int size, std::string_view utf8, std::uint32_t rgba,
             int halo = 0, std::uint32_t halo_rgba = 0x080808FFu) const;
    /// Word wrap to max_w; at most max_lines lines, the last one ellipsised ("..." via U+2026).
    std::vector<std::string> Wrap(int size, std::string_view utf8, int max_w, int max_lines,
                                  bool* cut = nullptr) const;
    std::string Ellipsis(int size, std::string_view utf8, int max_w) const;

private:
    struct Page {
        int w = 0, h = 0;
        std::vector<std::array<float, 4>> uvs;
        d3_img::Image img; // R8 replicated
    };
    struct Size {
        float pt = 0;
        int metrics[8] = {};
        std::vector<std::pair<std::uint32_t, Glyph>> glyphs; // sorted by codepoint
        const Glyph* Find(std::uint32_t cp) const;
    };
    std::vector<Page> pages;
    std::vector<Size> sizes;
};

std::vector<std::uint32_t> Utf8(std::string_view s);

} // namespace d3_font
