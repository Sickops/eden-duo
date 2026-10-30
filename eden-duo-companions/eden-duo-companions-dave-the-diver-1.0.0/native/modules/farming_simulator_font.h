// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "farming_simulator_assets.h"
#include <array>
namespace farming_simulator {
struct FontGlyph {
    uint16_t x{}, y{};
    uint8_t w{}, h{};
    int8_t bx{}, by{};
    float advance{};
};
class Font {
  public:
    bool Load(Assets &assets);
    bool Parse(std::span<const uint8_t> metrics, std::shared_ptr<const Image> atlas);
    int Measure(std::string_view text, int size, bool bold = false) const;
    std::pair<int, int> VerticalBounds(std::string_view text, int size, bool bold = false) const;
    void Draw(Image &out, std::string_view text, int x, int y, int size,
              uint32_t colour = 0xffffffff, bool bold = false) const;
    std::vector<std::string> Wrap(std::string_view text, int size, int width,
                                  bool bold = false) const;

  private:
    // The scan over a glyph's SDF cell that finds its visible (>=50% coverage)
    // row range: pure function of (codepoint, bold) -- the atlas never
    // changes once loaded, and the raw scan is done in the glyph's own pixel
    // space, before VerticalBounds applies the requested size's scale, so it
    // does not vary with `size` either. {bottom < top} means "no ink"
    // (missing glyph, or an empty/space cell), matching VerticalBounds' own
    // skip condition.
    std::pair<int, int> GlyphInk(uint32_t codepoint, bool bold) const;
    std::map<uint32_t, std::array<FontGlyph, 2>> glyphs_;
    std::shared_ptr<const Image> atlas_;
    mutable std::map<std::pair<uint32_t, bool>, std::pair<int, int>> ink_cache_;
};
} // namespace farming_simulator
