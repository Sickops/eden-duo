// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Minimal straight-RGBA8 image helpers for the Diablo III companion's composed pictures.
// Scaling is integer + nearest only (the owner's "sharp, never blurred" rule).

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace d3_img {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px; // RGBA8, straight alpha
    Image() = default;
    // w_/h_ may come from untrusted data (frame rects, cropped regions); clamp non-positive sizes to
    // 0x0 so the pixel buffer size can never wrap around through a negative-to-size_t conversion.
    Image(int w_, int h_)
        : w{w_ > 0 ? w_ : 0}, h{h_ > 0 ? h_ : 0}, px(static_cast<size_t>(w) * h * 4, 0) {}
    bool Empty() const {
        return w <= 0 || h <= 0;
    }
    std::uint8_t* At(int x, int y) {
        return &px[(static_cast<size_t>(y) * w + x) * 4];
    }
    const std::uint8_t* At(int x, int y) const {
        return &px[(static_cast<size_t>(y) * w + x) * 4];
    }
};

inline void Fill(Image& dst, std::uint32_t rgba) {
    for (size_t i = 0; i + 3 < dst.px.size(); i += 4) {
        dst.px[i] = rgba >> 24;
        dst.px[i + 1] = rgba >> 16;
        dst.px[i + 2] = rgba >> 8;
        dst.px[i + 3] = rgba;
    }
}

/// Source-over blend of one straight-alpha pixel; `alpha` scales the source alpha (0..255).
inline void BlendPx(std::uint8_t* d, const std::uint8_t* s, int alpha = 255) {
    const int sa = s[3] * alpha / 255;
    if (sa <= 0)
        return;
    if (sa >= 255) {
        d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = 255;
        return;
    }
    const int da = d[3];
    const int oa = sa + da * (255 - sa) / 255;
    if (oa <= 0)
        return;
    for (int c = 0; c < 3; ++c)
        d[c] = static_cast<std::uint8_t>((s[c] * sa + d[c] * da * (255 - sa) / 255) / oa);
    d[3] = static_cast<std::uint8_t>(oa);
}

/// Draws src (or its sub-rect sx,sy,sw,sh) at x,y scaled by an integer factor k.
inline void Blit(Image& dst, const Image& src, int x, int y, int k = 1, int sx = 0, int sy = 0,
                 int sw = -1, int sh = -1, int alpha = 255) {
    if (sw < 0)
        sw = src.w - sx;
    if (sh < 0)
        sh = src.h - sy;
    for (int yy = 0; yy < sh * k; ++yy) {
        const int dy = y + yy;
        if (dy < 0 || dy >= dst.h)
            continue;
        const int ty = sy + yy / k;
        if (ty < 0 || ty >= src.h)
            continue;
        for (int xx = 0; xx < sw * k; ++xx) {
            const int dx = x + xx;
            if (dx < 0 || dx >= dst.w)
                continue;
            const int tx = sx + xx / k;
            if (tx < 0 || tx >= src.w)
                continue;
            BlendPx(dst.At(dx, dy), src.At(tx, ty), alpha);
        }
    }
}

/// Copies a sub-rectangle; w <= 0 || h <= 0 yields an empty image.
inline Image Crop(const Image& src, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0)
        return {};
    Image out(w, h);
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const int sx = x + xx, sy = y + yy;
            if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h)
                continue;
            std::copy_n(src.At(sx, sy), 4, out.At(xx, yy));
        }
    return out;
}

/// Stretches a 9-band source to w x h by repeating the middle bands (the mockup's `extend`):
/// columns [bx0,bx1) and rows [by0,by1) are tiled; the borders keep their size. No smoothing.
inline Image Extend(const Image& src, int w, int h, int bx0, int bx1, int by0, int by1) {
    Image out(w, h);
    auto map = [](int v, int size, int target, int b0, int b1) {
        const int tail = size - b1;
        if (v < b0)
            return v;
        if (v >= target - tail)
            return v - (target - size);
        const int band = std::max(1, b1 - b0);
        return b0 + (v - b0) % band;
    };
    for (int y = 0; y < h; ++y) {
        const int sy = std::clamp(map(y, src.h, h, by0, by1), 0, src.h - 1);
        for (int x = 0; x < w; ++x) {
            const int sx = std::clamp(map(x, src.w, w, bx0, bx1), 0, src.w - 1);
            std::copy_n(src.At(sx, sy), 4, out.At(x, y));
        }
    }
    return out;
}

/// Hard-edged circular alpha mask centred in the image (the game draws globes in a round socket).
inline void CircleMask(Image& img, float r) {
    const float cx = img.w / 2.0f, cy = img.h / 2.0f;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy > r * r)
                img.At(x, y)[3] = 0;
        }
}

} // namespace d3_img
