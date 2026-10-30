// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include "d3_font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "d3_tex.h"

namespace d3_font {
namespace {
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
u32 Le32(const u8* p) {
    return u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
}
std::int16_t Le16s(const u8* p) {
    return static_cast<std::int16_t>(p[0] | p[1] << 8);
}
std::uint16_t Le16(const u8* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}
} // namespace

std::vector<std::uint32_t> Utf8(std::string_view s) {
    std::vector<u32> out;
    for (size_t i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        u32 cp = c;
        int n = 0;
        if (c >= 0xF0)
            cp = c & 7, n = 3;
        else if (c >= 0xE0)
            cp = c & 15, n = 2;
        else if (c >= 0xC0)
            cp = c & 31, n = 1;
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<u8>(s[i]) & 63);
        out.push_back(cp);
    }
    return out;
}

const Glyph* Font::Size::Find(u32 cp) const {
    auto it = std::lower_bound(glyphs.begin(), glyphs.end(), cp,
                               [](const auto& g, u32 v) { return g.first < v; });
    return it != glyphs.end() && it->first == cp ? &it->second : nullptr;
}

bool Font::Parse(std::span<const u8> d) {
    constexpr u64 B = 0x10;
    pages.clear();
    sizes.clear();
    if (d.size() < B + 0xC8)
        return false;
    const u32 poff = Le32(&d[B + 0xA8]);
    const u32 np = Le32(&d[B + 0xB8]), ns = Le32(&d[B + 0xBC]);
    const u32 soff = Le32(&d[B + 0xC0]), ssz = Le32(&d[B + 0xC4]);
    if (np == 0 || np > 1024 || ns == 0 || ns > 256 || ssz % ns)
        return false;
    for (u32 i = 0; i < np; ++i) {
        const u64 r = B + poff + u64{i} * 40;
        if (r + 40 > d.size())
            return false;
        Page p;
        p.w = static_cast<int>(Le32(&d[r + 8]));
        p.h = static_cast<int>(Le32(&d[r + 12]));
        const u32 toff = Le32(&d[r + 16]), tsz = Le32(&d[r + 20]);
        const u32 nuv = Le32(&d[r + 28]), uoff = Le32(&d[r + 32]);
        if (B + toff + u64{tsz} > d.size() || B + uoff + u64{nuv} * 16 > d.size())
            return false;
        for (u32 k = 0; k < nuv; ++k) {
            std::array<float, 4> uv;
            std::memcpy(uv.data(), &d[B + uoff + k * 16], 16);
            p.uvs.push_back(uv);
        }
        if (!d3_tex::DecodeXtx(d.subspan(B + toff, tsz), p.img))
            return false;
        if (p.img.w != p.w || p.img.h != p.h) // malformed .fnt: declared size disagrees with the page
            return false;
        pages.push_back(std::move(p));
    }
    const u32 rec = ssz / ns;
    for (u32 k = 0; k < ns; ++k) {
        const u64 r = B + soff + u64{k} * rec;
        if (r + rec > d.size() || rec < 52)
            return false;
        Size s;
        for (int m = 0; m < 8; ++m)
            s.metrics[m] = static_cast<int>(Le32(&d[r + m * 4]));
        const u32 ng = Le32(&d[r + 36]), goff = Le32(&d[r + 40]);
        std::memcpy(&s.pt, &d[r + rec - 4], 4);
        if (ng > 100000 || B + goff + u64{ng} * 24 > d.size())
            return false;
        for (u32 g = 0; g < ng; ++g) {
            const u8* q = &d[B + goff + u64{g} * 24];
            Glyph gl;
            gl.uv = static_cast<int>(Le32(q + 4));
            gl.bx = Le16s(q + 8);
            gl.by = Le16s(q + 10);
            gl.adv = Le16(q + 14);
            gl.page = Le16(q + 16);
            s.glyphs.emplace_back(Le16(q + 18), gl);
        }
        std::sort(s.glyphs.begin(), s.glyphs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        sizes.push_back(std::move(s));
    }
    return true;
}

int Font::SizeIndex(float pt) const {
    for (size_t i = 0; i < sizes.size(); ++i)
        if (std::fabs(sizes[i].pt - pt) < 0.01f)
            return static_cast<int>(i);
    return -1;
}
int Font::LineHeight(int size) const {
    return size < 0 || size >= int(sizes.size()) ? 0 : (sizes[size].metrics[6] + 63) / 64;
}
int Font::Ascent(int size) const {
    return size < 0 || size >= int(sizes.size()) ? 0 : (sizes[size].metrics[0] + 63) / 64;
}
int Font::Measure(int size, std::string_view s) const {
    if (size < 0 || size >= int(sizes.size()))
        return 0;
    int x = 0;
    for (u32 cp : Utf8(s))
        if (const Glyph* g = sizes[size].Find(cp))
            x += g->adv;
    return x;
}

std::array<int, 2> Font::InkBounds(int size, std::string_view text) const {
    if (size < 0 || size >= int(sizes.size()))
        return {0, 0};
    int top = 0, bottom = 0;
    bool have_ink = false;
    const int ascent = Ascent(size);
    for (u32 cp : Utf8(text)) {
        const Glyph* glyph = sizes[size].Find(cp);
        if (!glyph || glyph->page < 0 || glyph->page >= int(pages.size()))
            continue;
        const auto& page = pages[glyph->page];
        if (glyph->uv < 0 || glyph->uv >= int(page.uvs.size()))
            continue;
        const auto& uv = page.uvs[glyph->uv];
        const int x0 = int(std::lround(uv[0] * page.w)), y0 = int(std::lround(uv[1] * page.h));
        const int x1 = int(std::lround(uv[2] * page.w)), y1 = int(std::lround(uv[3] * page.h));
        for (int y = std::max(0, y0); y < std::min(page.img.h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(page.img.w, x1); ++x)
                if (page.img.At(x, y)[0] > 8) {
                    const int ink_y = ascent - glyph->by + y - y0;
                    if (!have_ink) { top = ink_y; bottom = ink_y + 1; have_ink = true; }
                    else { top = std::min(top, ink_y); bottom = std::max(bottom, ink_y + 1); }
                }
    }
    return have_ink ? std::array<int, 2>{top, bottom} : std::array<int, 2>{0, LineHeight(size)};
}

int Font::CenteredY(int size, std::string_view text, int middle) const {
    const auto bounds = InkBounds(size, text);
    return middle - (bounds[0] + bounds[1]) / 2;
}

int Font::Draw(d3_img::Image& dst, int x0, int y0, int size, std::string_view s, u32 rgba, int halo,
               u32 halo_rgba) const {
    if (size < 0 || size >= int(sizes.size()))
        return 0;
    const Size& S = sizes[size];
    const int asc = Ascent(size);
    auto pass = [&](int ox, int oy, u32 col) {
        int x = x0;
        const u8 c[4] = {u8(col >> 24), u8(col >> 16), u8(col >> 8), u8(col)};
        for (u32 cp : Utf8(s)) {
            const Glyph* g = S.Find(cp);
            if (!g)
                continue;
            if (g->page < int(pages.size()) && g->uv < int(pages[g->page].uvs.size())) {
                const Page& p = pages[g->page];
                const auto& uv = p.uvs[g->uv];
                const int gx0 = int(std::lround(uv[0] * p.w)), gy0 = int(std::lround(uv[1] * p.h));
                const int gx1 = int(std::lround(uv[2] * p.w)), gy1 = int(std::lround(uv[3] * p.h));
                const int px = x + g->bx + ox, py = y0 + asc - g->by + oy;
                for (int yy = gy0; yy < gy1; ++yy)
                    for (int xx = gx0; xx < gx1; ++xx) {
                        const int dx = px + xx - gx0, dy = py + yy - gy0;
                        if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h || xx < 0 || yy < 0 ||
                            xx >= p.img.w || yy >= p.img.h)
                            continue;
                        const u8 a = p.img.At(xx, yy)[0];
                        if (!a)
                            continue;
                        const u8 src[4] = {c[0], c[1], c[2], u8(a * c[3] / 255)};
                        d3_img::BlendPx(dst.At(dx, dy), src);
                    }
            }
            x += g->adv;
        }
        return x - x0;
    };
    if (halo > 0)
        for (int oy = -halo; oy <= halo; oy += halo)
            for (int ox = -halo; ox <= halo; ox += halo)
                if (ox || oy)
                    pass(ox, oy, halo_rgba);
    return pass(0, 0, rgba);
}

std::string Font::Ellipsis(int size, std::string_view s, int max_w) const {
    if (Measure(size, s) <= max_w)
        return std::string{s};
    const std::string ell = "\xE2\x80\xA6";
    auto cps = Utf8(s);
    std::string out;
    for (size_t n = cps.size(); n > 0; --n) {
        out.clear();
        for (size_t i = 0; i < n; ++i) {
            const u32 cp = cps[i];
            if (cp < 0x80)
                out += char(cp);
            else if (cp < 0x800)
                out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 63));
            else
                out += char(0xE0 | cp >> 12), out += char(0x80 | ((cp >> 6) & 63)),
                    out += char(0x80 | (cp & 63));
        }
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        if (Measure(size, out + ell) <= max_w)
            return out + ell;
    }
    return ell;
}

std::vector<std::string> Font::Wrap(int size, std::string_view s, int max_w, int max_lines,
                                    bool* cut) const {
    std::vector<std::string> words;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty())
                words.push_back(cur), cur.clear();
        } else
            cur += c;
    }
    if (!cur.empty())
        words.push_back(cur);
    std::vector<std::string> lines;
    std::string line;
    bool truncated = false;
    for (size_t i = 0; i < words.size(); ++i) {
        const std::string trial = line.empty() ? words[i] : line + " " + words[i];
        if (Measure(size, trial) <= max_w) {
            line = trial;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = words[i];
        if (int(lines.size()) == max_lines) {
            truncated = true;
            break;
        }
    }
    if (!truncated && !line.empty())
        lines.push_back(line);
    if (int(lines.size()) > max_lines)
        truncated = true, lines.resize(max_lines);
    if (truncated && !lines.empty()) {
        std::string rest = lines.back();
        lines.back() = Ellipsis(size, rest + " \xE2\x80\xA6", max_w);
    }
    for (auto& l : lines)
        if (Measure(size, l) > max_w)
            l = Ellipsis(size, l, max_w), truncated = true;
    if (cut)
        *cut = truncated;
    return lines;
}

} // namespace d3_font
