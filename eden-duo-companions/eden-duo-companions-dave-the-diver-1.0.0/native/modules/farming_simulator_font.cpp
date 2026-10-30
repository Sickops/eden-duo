// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_font.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
namespace farming_simulator {
namespace {
uint32_t Codepoint(std::string_view text, size_t &p) {
    auto a = uint8_t(text[p++]);
    if (a < 128)
        return a;
    int n = a >= 0xf0 ? 3 : a >= 0xe0 ? 2 : a >= 0xc0 ? 1 : 0;
    uint32_t cp = a & ((1 << (6 - n)) - 1);
    if (!n || p + n > text.size())
        return '?';
    while (n--) {
        auto b = uint8_t(text[p++]);
        if ((b & 0xc0) != 0x80)
            return '?';
        cp = (cp << 6) | (b & 63);
    }
    return cp;
}
} // namespace
bool Font::Load(Assets &assets) {
    if (atlas_)
        return true;
    std::vector<uint8_t> metrics;
    return assets.Read("shared/font/defaultFont.font", metrics, 64 * 1024) &&
           Parse(metrics, assets.Texture("shared/font/defaultFont.dds", false));
}
bool Font::Parse(std::span<const uint8_t> bytes, std::shared_ptr<const Image> atlas) {
    using dsmod_sdk::Le16;
    using dsmod_sdk::Le32;
    if (bytes.size() < 12 || !atlas || atlas->w != 1024 || atlas->h != 1024)
        return false;
    float nominal;
    std::memcpy(&nominal, bytes.data(), 4);
    auto count = Le32(bytes.data() + 8);
    if (nominal != 40.f || count < 32 || count > 2048 || bytes.size() != 12 + size_t(count) * 28)
        return false;
    std::map<uint32_t, std::array<FontGlyph, 2>> parsed;
    for (size_t i = 0; i < count; ++i) {
        auto *p = bytes.data() + 12 + i * 28;
        const auto cp = Le32(p);
        if (cp > 0x10ffff || parsed.contains(cp))
            return false;
        std::array<FontGlyph, 2> forms;
        for (int b = 0; b < 2; ++b) {
            auto *q = p + 4 + b * 12;
            auto &g = forms[b];
            g.x = Le16(q);
            g.y = Le16(q + 2);
            g.w = q[4];
            g.h = q[5];
            g.bx = int8_t(q[6]);
            g.by = int8_t(q[7]);
            std::memcpy(&g.advance, q + 8, 4);
            if (g.x + g.w > 1024 || g.y + g.h > 1024 || !std::isfinite(g.advance) ||
                g.advance < 0 || g.advance > 128)
                return false;
        }
        parsed[cp] = forms;
    }
    if (!parsed.contains('?'))
        return false;
    glyphs_ = std::move(parsed);
    atlas_ = std::move(atlas);
    return true;
}
int Font::Measure(std::string_view value, int size, bool bold) const {
    float x = 0;
    for (size_t p = 0; p < value.size();) {
        auto cp = Codepoint(value, p);
        auto i = glyphs_.find(cp);
        if (i == glyphs_.end())
            i = glyphs_.find('?');
        if (i != glyphs_.end())
            x += i->second[bold].advance * size / 40.f;
    }
    return std::lround(x);
}
std::pair<int, int> Font::GlyphInk(uint32_t cp, bool bold) const {
    auto key = std::pair{cp, bold};
    if (auto it = ink_cache_.find(key); it != ink_cache_.end())
        return it->second;
    auto glyph_it = glyphs_.find(cp);
    if (glyph_it == glyphs_.end())
        glyph_it = glyphs_.find('?');
    std::pair<int, int> result{0, -1}; // "no ink" sentinel (bottom < top)
    if (glyph_it != glyphs_.end()) {
        const auto &g = glyph_it->second[bold];
        if (g.w && g.h) {
            // The font atlas stores a signed-distance margin. Measure the
            // visible 50% coverage contour instead of centering that
            // transparent margin.
            int visible_top = g.h, visible_bottom = -1;
            for (int yy = 0; yy < g.h; ++yy)
                for (int xx = 0; xx < g.w; ++xx)
                    if (atlas_->rgba[(size_t(g.y + yy) * atlas_->w + g.x + xx) * 4] >= 128) {
                        visible_top = std::min(visible_top, yy);
                        visible_bottom = std::max(visible_bottom, yy);
                    }
            result = {visible_top, visible_bottom};
        }
    }
    return ink_cache_.emplace(key, result).first->second;
}
std::pair<int, int> Font::VerticalBounds(std::string_view text, int size, bool bold) const {
    int top = size, bottom = 0;
    bool ink = false;
    const float scale = size / 40.f;
    const int baseline = std::lround(.8f * size);
    for (size_t p = 0; p < text.size();) {
        auto cp = Codepoint(text, p);
        if (cp == ' ') continue;
        auto it = glyphs_.find(cp);
        if (it == glyphs_.end()) it = glyphs_.find('?');
        if (it == glyphs_.end()) continue;
        const auto &g = it->second[bold];
        if (!g.w || !g.h) continue;
        auto [visible_top, visible_bottom] = GlyphInk(cp, bold);
        if (visible_bottom < visible_top) continue;
        int glyph_y = baseline - std::lround(g.by * scale);
        top = std::min(top, glyph_y + int(std::lround(visible_top * scale)));
        bottom = std::max(bottom, glyph_y + int(std::lround((visible_bottom + 1) * scale)));
        ink = true;
    }
    return ink ? std::pair{top, bottom} : std::pair{0, size};
}
void Font::Draw(Image &out, std::string_view value, int x, int y, int size, uint32_t colour,
                bool bold) const {
    if (!atlas_ || size < 1 || size > 80)
        return;
    float cursor = x, scale = size / 40.f;
    const float edge = std::clamp(5.f / scale, 2.f, 20.f);
    const int baseline = y + std::lround(.8f * size);
    for (size_t p = 0; p < value.size();) {
        auto cp = Codepoint(value, p);
        auto it = glyphs_.find(cp);
        if (it == glyphs_.end())
            it = glyphs_.find('?');
        if (it == glyphs_.end())
            continue;
        const auto &g = it->second[bold];
        const int gx = std::lround(cursor + g.bx * scale),
                  gy = baseline - std::lround(g.by * scale),
                  w = std::max(1, int(std::lround(g.w * scale))),
                  h = std::max(1, int(std::lround(g.h * scale)));
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx) {
                if (gx + dx < 0 || gy + dy < 0 || gx + dx >= int(out.w) || gy + dy >= int(out.h))
                    continue;
                float u = std::clamp((dx + .5f) / scale - .5f, 0.f, std::max(0.f, float(g.w) - 1)),
                      v = std::clamp((dy + .5f) / scale - .5f, 0.f, std::max(0.f, float(g.h) - 1));
                int ix = u, iy = v;
                float fu = u - ix, fv = v - iy;
                auto sample = [&](int a, int b) {
                    return atlas_->rgba[(size_t(g.y + std::min(b, int(g.h) - 1)) * atlas_->w + g.x +
                                         std::min(a, int(g.w) - 1)) *
                                        4];
                };
                if (!g.w || !g.h)
                    continue;
                float sdf = (sample(ix, iy) * (1 - fu) + sample(ix + 1, iy) * fu) * (1 - fv) +
                            (sample(ix, iy + 1) * (1 - fu) + sample(ix + 1, iy + 1) * fu) * fv;
                unsigned a = std::lround(std::clamp((sdf - 128.f + edge) / (edge * 2), 0.f, 1.f) *
                                         (colour & 255));
                auto *d = &out.rgba[(size_t(gy + dy) * out.w + gx + dx) * 4];
                for (int c = 0; c < 3; ++c)
                    d[c] = ((((colour >> (24 - c * 8)) & 255) * a + d[c] * (255 - a)) + 127) / 255;
                d[3] = std::min(255u, a + d[3] * (255 - a) / 255);
            }
        cursor += g.advance * scale;
    }
}
std::vector<std::string> Font::Wrap(std::string_view value, int size, int width, bool bold) const {
    std::vector<std::string> lines;
    std::istringstream words{std::string{value}};
    std::string word, line;
    while (words >> word) {
        auto next = line.empty() ? word : line + " " + word;
        if (!line.empty() && Measure(next, size, bold) > width) {
            lines.push_back(line);
            line = word;
        } else
            line = next;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}
} // namespace farming_simulator
