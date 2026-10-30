// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared drawing helpers for the Hero and Gear tabs (native frames, baked fonts, integer scaling).
// Colours and per-field text rules: research design/mockups-20261011-text/QUALITY.md.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "d3_assets.h"
#include "d3_img.h"

namespace d3hg::draw {
using u32 = std::uint32_t;
using d3_img::Image;

constexpr u32 White = 0xFDFDFDFF, Gold = 0xFBC85AFF, Grey = 0x969696FF, LGrey = 0xC8C8C8FF,
              Yellow = 0xFFFF64FF, Magic = 0x8080FFFF, Legend = 0xFF8000FF, SetGreen = 0x00FF00FF,
              Rare = 0xFFFF00FF, Paragon = 0x5789FFFF, LegAffix = 0xCF9A46FF, Halo = 0x080808FF,
              Red = 0xFF3030FF;
constexpr int PageW = 1240, PageH = 995;

inline int Size(const d3_font::Font& f, float pt) {
    return f.SizeIndex(pt);
}
inline int LineH(const d3_font::Font& f, float pt) {
    const int s = f.SizeIndex(pt);
    return s < 0 ? 0 : f.LineHeight(s);
}
inline int Measure(const d3_font::Font& f, float pt, const std::string& s) {
    const int si = f.SizeIndex(pt);
    return si < 0 ? 0 : f.Measure(si, s);
}
/// anchor: 0 left, 1 centre, 2 right. Returns the advance.
inline int Txt(Image& dst, const d3_font::Font& f, float pt, int x, int y, const std::string& s, u32 col,
               int anchor = 0, int halo = 0) {
    const int si = f.SizeIndex(pt);
    if (si < 0 || s.empty())
        return 0;
    const int w = f.Measure(si, s);
    const int xx = anchor == 1 ? x - w / 2 : anchor == 2 ? x - w : x;
    f.Draw(dst, xx, y, si, s, col, halo, Halo);
    return w;
}
inline int TxtMid(Image& dst, const d3_font::Font& f, float pt, int x, int middle,
                  const std::string& s, u32 col, int anchor = 0, int halo = 0) {
    return Txt(dst, f, pt, x, f.CenteredY(f.SizeIndex(pt), s, middle), s, col, anchor, halo);
}
/// Keeps multiline names on a shared baseline grid while centring the entire visible block.
inline int BlockTop(const d3_font::Font& f, float pt, const std::vector<std::string>& lines,
                    int step, int middle) {
    if (lines.empty()) return middle;
    int top = 0, bottom = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto bounds = f.InkBounds(f.SizeIndex(pt), lines[i]);
        const int offset = static_cast<int>(i) * step;
        top = i == 0 ? bounds[0] : std::min(top, bounds[0] + offset);
        bottom = std::max(bottom, bounds[1] + offset);
    }
    return middle - (top + bottom) / 2;
}
inline std::vector<std::string> Wrap(const d3_font::Font& f, float pt, const std::string& s, int w, int lines) {
    const int si = f.SizeIndex(pt);
    return si < 0 ? std::vector<std::string>{} : f.Wrap(si, s, w, lines);
}
inline std::string Upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
    return s;
}
/// Integer 1/2 downscale. `average`: false (default) nearest-samples the 2x2 block's top-left texel
/// (the Hero/Gear tabs' icons); true box-filters the 2x2 block with rounding (the Map/Activities
/// tabs' icons) — callers keep whichever one they already relied on.
inline Image Half(const Image& s, bool average = false) {
    Image o(s.w / 2, s.h / 2);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x)
            if (average)
                for (int c = 0; c < 4; ++c)
                    o.At(x, y)[c] = static_cast<std::uint8_t>(
                        (s.At(2 * x, 2 * y)[c] + s.At(2 * x + 1, 2 * y)[c] + s.At(2 * x, 2 * y + 1)[c] +
                         s.At(2 * x + 1, 2 * y + 1)[c] + 2) / 4);
            else
                std::copy_n(s.At(x * 2, y * 2), 4, o.At(x, y));
    return o;
}
/// Flips an image vertically (row order reversed); used to turn the native down-pointing scroll
/// arrow into an up-pointing one.
inline Image FlipV(const Image& s) {
    Image o(s.w, s.h);
    for (int row = 0; row < s.h; ++row)
        std::copy_n(s.At(0, s.h - 1 - row), s.w * 4, o.At(0, row));
    return o;
}
/// Scales every pixel's alpha by `f` (0..1).
inline void ScaleAlpha(Image& img, float f) {
    for (std::size_t i = 3; i < img.px.size(); i += 4)
        img.px[i] = static_cast<std::uint8_t>(img.px[i] * f);
}
/// Darkens to greyscale in place: each channel replaced by luma(R,G,B) scaled by `f` (0..1). The
/// Map tab's unavailable-icon look (f 0.55, the default) and the Activities tab's (f 0.85).
inline void Desaturate(Image& img, float f = 0.55f) {
    for (std::size_t i = 0; i + 3 < img.px.size(); i += 4) {
        const int l = (img.px[i] * 30 + img.px[i + 1] * 59 + img.px[i + 2] * 11) / 100;
        img.px[i] = img.px[i + 1] = img.px[i + 2] = static_cast<std::uint8_t>(l * f);
    }
}
/// Fits the visible artwork uniformly; transparent atlas padding is not part of its size.
inline Image IconFit(const Image& src, int width, int height, bool upscale = false) {
    if (src.Empty())
        return {};
    int x0 = src.w, y0 = src.h, x1 = 0, y1 = 0;
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            if (src.At(x, y)[3] > 8) {
                x0 = std::min(x0, x), y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
            }
    if (x1 <= x0 || y1 <= y0)
        return {};
    const int sw = x1 - x0, sh = y1 - y0;
    const double fit = std::min(double(width) / sw, double(height) / sh);
    const double scale = upscale ? fit : std::min(1.0, fit);
    const int w = std::max(1, int(sw * scale)), h = std::max(1, int(sh * scale));
    Image out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::copy_n(src.At(x0 + x * sw / w, y0 + y * sh / h), 4, out.At(x, y));
    return out;
}
/// Zeroes alpha outside the circle inscribed in `img`'s own canvas: the native console HUD's
/// ring buttons show a perfectly round icon, never a square peeking past the ring's edge.
inline void ClipCircle(Image& img) {
    if (img.Empty())
        return;
    const float cx = img.w / 2.0f - 0.5f, cy = img.h / 2.0f - 0.5f;
    const float r = std::min(img.w, img.h) / 2.0f, r2 = r * r;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const float dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > r2)
                img.At(x, y)[3] = 0;
        }
}
/// Draws `src` centred at (cx, cy).
inline void BlitC(Image& dst, const Image& src, int cx, int cy, int k = 1, int alpha = 255) {
    if (!src.Empty())
        d3_img::Blit(dst, src, cx - src.w * k / 2, cy - src.h * k / 2, k, 0, 0, -1, -1, alpha);
}
/// Extends a frame along x with repeated bands [b0, b1) (the mockup's `extend`), or narrows it by
/// removing its central columns (`shrink_h`) when w is smaller.
inline Image FitW(const Image& src, int w, int b0, int b1) {
    if (src.Empty() || w == src.w)
        return src;
    if (w > src.w)
        return d3_img::Extend(src, w, src.h, b0, b1, 0, 0);
    Image o(w, src.h);
    const int l = w / 2, r = w - l;
    d3_img::Blit(o, d3_img::Crop(src, 0, 0, l, src.h), 0, 0);
    d3_img::Blit(o, d3_img::Crop(src, src.w - r, 0, r, src.h), l, 0);
    return o;
}
/// Native two-pane menu window (Console_MenuBase_Double_<colour>[_Undivided]) grown to w x h.
inline Image DoublePanel(d3_assets::Assets& a, const EdenDsmodHostApi* h, int w, int hh, const char* colour,
                         bool divided) {
    std::string nm = std::string{"Console_MenuBase_Double_"} + colour + (divided ? "" : "_Undivided");
    Image src = a.Frame(h, "2DUIConsoleMenu", nm);
    if (src.Empty())
        src = a.Frame(h, "2DUIConsoleMenu", std::string{"Console_MenuBase_Double_"} + colour);
    Image out(w, hh);
    d3_img::Fill(out, 0x060504FF);
    if (src.Empty())
        return out;
    // two stretch bands per axis like the mockup (xbands 130-230, 343-443; ybands 160-450): grow
    // the left band to half the extra width, then the right band
    const int extra = std::max(0, w - src.w);
    Image a1 = d3_img::Extend(src, src.w + extra / 2, src.h, 130, 230, 0, 0);
    const int off = extra / 2;
    Image a2 = d3_img::Extend(a1, w, a1.h, 343 + off, 443 + off, 0, 0);
    Image a3 = d3_img::Extend(a2, a2.w, hh, 0, 0, 160, 450);
    d3_img::Blit(out, a3, 0, 0);
    return out;
}
/// The vitals-column frame (Console_MenuBase_RingSide_Long) grown to w x h over the dark backing.
/// `two_stage`: true (the Hero/Gear tabs) grows the x bands in two steps, 40-95 then 180-235 offset
/// by half the extra width each; false (the Map tab) stretches the whole 40-235 band in one Extend
/// call. `missing` is the flat fill used when the frame art itself could not be loaded.
inline Image Ringside(d3_assets::Assets& a, const EdenDsmodHostApi* h, int w, int hh,
                      bool two_stage = true, std::uint32_t missing = 0x060504FFu) {
    Image out(w, hh);
    d3_img::Fill(out, 0x060504FF);
    Image src = a.Frame(h, "2DUIConsoleMenu", "Console_MenuBase_RingSide_Long");
    if (src.Empty()) {
        d3_img::Fill(out, missing);
        return out;
    }
    src = d3_img::Crop(src, 28, 0, std::min(275, src.w - 28), std::min(572, src.h));
    Image art;
    if (two_stage) {
        Image e1 = d3_img::Extend(src, src.w + (w - src.w) / 2, src.h, 40, 95, 0, 0);
        const int off = (w - src.w) / 2;
        Image e2 = d3_img::Extend(e1, w, e1.h, 180 + off, 235 + off, 0, 0);
        art = d3_img::Extend(e2, e2.w, hh, 0, 0, 150, 420);
    } else {
        art = d3_img::Extend(src, w, hh, 40, 235, 150, 420);
    }
    d3_img::Blit(out, art, 0, 0);
    return out;
}

/// Currency glyphs: the console vendor's gold bag (cropped to the bag, half size) and the blood
/// shard inventory icon (2DInventoryShardsIcons_x1 frame 3, half size); nearest, whole factors.
/// (The font icon atlas with the inline coin glyph is stored encrypted in Patch2_7_6 and cannot be
/// decoded yet.)
inline Image GoldIcon(d3_assets::Assets& a, const EdenDsmodHostApi* h) {
    Image g = a.Frame(h, "2DUIConsoleInventory", "Console_VendorGoldBag");
    return g.Empty() ? g : Half(d3_img::Crop(g, 76, 22, 96, 96));
}
inline Image ShardIcon(d3_assets::Assets& a, const EdenDsmodHostApi* h) {
    Image s = a.Frame(h, "2DInventoryShardsIcons_x1", "[3]");
    return s.Empty() ? s : Half(s);
}

} // namespace d3hg::draw
