// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_tex.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <bc_decoder.h>

namespace d3_tex {
namespace {
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 MaxDim = 4096;

u32 Le32(const u8* p) {
    return u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
}

struct Fmt {
    int kind; // 0 rgba8, 1 bgra8, 2 r8, 3 rg8, 4 a1rgb5, 10.. bcN
    int bpu;
    int blk;
};
bool GetFmt(u32 f, Fmt& o) {
    switch (f) {
    case 0x25:
    case 0x38:
        o = {0, 4, 1};
        return true;
    case 0x6D:
        o = {1, 4, 1};
        return true;
    case 0x01:
        o = {2, 1, 1};
        return true;
    case 0x0D:
        o = {3, 2, 1};
        return true;
    case 0x3B:
        o = {4, 2, 1};
        return true;
    case 0x42:
        o = {11, 8, 4};
        return true;
    case 0x43:
        o = {12, 16, 4};
        return true;
    case 0x44:
        o = {13, 16, 4};
        return true;
    case 0x49:
        o = {14, 8, 4};
        return true;
    case 0x4B:
        o = {15, 16, 4};
        return true;
    case 0x4D:
        o = {17, 16, 4};
        return true;
    default:
        return false;
    }
}

// Tegra block-linear (GOB 64 B x 8 rows) -> linear units.
void Deswizzle(std::span<const u8> src, u32 wu, u32 hu, u32 bpu, u32 bh, std::vector<u8>& out) {
    const u64 row = u64{wu} * bpu;
    const u64 gobs_w = (row + 63) / 64;
    out.assign(row * hu, 0);
    for (u32 y = 0; y < hu; ++y) {
        const u64 gy = (y / (8 * bh)) * (512 * bh * gobs_w) + ((y % (8 * bh)) / 8) * 512;
        const u32 yy = y % 8;
        for (u64 xb = 0; xb < row; ++xb) {
            const u64 x = xb % 64;
            const u64 off = gy + (xb / 64) * (512 * bh) + (x / 32) * 256 + (yy / 2) * 64 +
                            ((x % 32) / 16) * 32 + (yy % 2) * 16 + (x % 16);
            out[u64{y} * row + xb] = off < src.size() ? src[off] : 0;
        }
    }
}
} // namespace

bool DecodeXtx(std::span<const u8> blob, d3_img::Image& out) {
    const char sig[4] = {'D', 'F', 'v', 'N'};
    size_t at = 0;
    for (; at + 8 <= blob.size(); ++at)
        if (std::memcmp(&blob[at], sig, 4) == 0)
            break;
    if (at + 8 > blob.size())
        return false;
    u64 p = at + Le32(&blob[at + 4]);
    u32 w = 0, h = 0, fmt = 0, bh = 1;
    std::span<const u8> pix;
    bool have_info = false;
    while (p + 36 <= blob.size() && std::memcmp(&blob[p], "HBvN", 4) == 0) {
        u64 dsz = 0;
        std::int64_t doff = 0;
        std::memcpy(&dsz, &blob[p + 8], 8);
        std::memcpy(&doff, &blob[p + 16], 8);
        const u32 typ = Le32(&blob[p + 24]);
        const u64 da = p + static_cast<u64>(doff);
        if (doff < 0 || da > blob.size() || dsz > blob.size() - da)
            return false;
        if (typ == 2 && dsz >= 112) {
            w = Le32(&blob[da + 12]);
            h = Le32(&blob[da + 16]);
            fmt = Le32(&blob[da + 28]);
            bh = 1u << std::min<u32>(Le32(&blob[da + 108]), 5);
            have_info = true;
        } else if (typ == 3) {
            pix = blob.subspan(da, dsz);
        }
        p = da + dsz;
    }
    Fmt f;
    if (!have_info || pix.empty() || !w || !h || w > MaxDim || h > MaxDim || !GetFmt(fmt, f))
        return false;
    const u32 wu = (w + f.blk - 1) / f.blk, hu = (h + f.blk - 1) / f.blk;
    std::vector<u8> lin;
    Deswizzle(pix, wu, hu, f.bpu, bh, lin);
    out = d3_img::Image(static_cast<int>(w), static_cast<int>(h));
    const u64 n = u64{w} * h;
    if (f.kind < 10) {
        for (u64 i = 0; i < n; ++i) {
            const u8* s = &lin[i * f.bpu];
            u8* d = &out.px[i * 4];
            switch (f.kind) {
            case 0:
                std::memcpy(d, s, 4);
                break;
            case 1:
                d[0] = s[2], d[1] = s[1], d[2] = s[0], d[3] = s[3];
                break;
            case 2:
                d[0] = d[1] = d[2] = d[3] = s[0];
                break;
            case 3:
                d[0] = s[0], d[1] = s[1], d[2] = 0, d[3] = 255;
                break;
            case 4: {
                const u32 v = s[0] | s[1] << 8;
                d[0] = u8(((v >> 10) & 31) * 255 / 31);
                d[1] = u8(((v >> 5) & 31) * 255 / 31);
                d[2] = u8((v & 31) * 255 / 31);
                d[3] = (v >> 15) ? 255 : 0;
                break;
            }
            }
        }
        return true;
    }
    // BCn: decode block by block into a padded buffer, then crop.
    const u32 pw = wu * 4, ph = hu * 4;
    std::vector<u8> tmp(u64{pw} * ph * 4, 0);
    for (u32 by = 0; by < hu; ++by)
        for (u32 bx = 0; bx < wu; ++bx) {
            const u8* s = &lin[(u64{by} * wu + bx) * f.bpu];
            // the decoder writes relative to dst: pass the block's top-left corner (x/y only clip)
            u8* dst = tmp.data() + (u64{by} * 4 * pw + u64{bx} * 4) * 4;
            switch (f.kind) {
            case 11:
                bcn::DecodeBc1(s, dst, bx * 4, by * 4, pw, ph);
                break;
            case 12:
                bcn::DecodeBc2(s, dst, bx * 4, by * 4, pw, ph);
                break;
            case 13:
                bcn::DecodeBc3(s, dst, bx * 4, by * 4, pw, ph);
                break;
            case 17:
                bcn::DecodeBc7(s, dst, bx * 4, by * 4, pw, ph);
                break;
            default:
                return false; // BC4/BC5 not used by the companion's UI/map art
            }
        }
    for (u32 y = 0; y < h; ++y)
        std::memcpy(out.At(0, static_cast<int>(y)), &tmp[(u64{y} * pw) * 4], u64{w} * 4);
    return true;
}

bool DecodeTex(std::span<const u8> d, Texture& out) {
    constexpr u64 B = 0x10;
    if (d.size() < 0x240 || Le32(d.data()) != 0xDEADBEEFu)
        return false;
    const u32 w = Le32(&d[B + 0x14]), h = Le32(&d[B + 0x18]);
    const u32 moff = Le32(&d[0x40]), msz = Le32(&d[0x44]);
    if (u64{B} + moff + msz > d.size())
        return false;
    if (!DecodeXtx(d.subspan(B + moff, msz), out.img))
        return false;
    (void)w;
    (void)h;
    out.frames.clear();
    const u32 nfr = Le32(&d[0x220]), foff = Le32(&d[0x224]);
    if (nfr < 100000 && u64{B} + foff + u64{nfr} * 20 <= d.size()) {
        for (u32 i = 0; i < nfr; ++i) {
            const u8* r = &d[B + foff + i * 20];
            float uv[4];
            std::memcpy(uv, r + 4, 16);
            Frame f;
            f.hash = Le32(r);
            // uv is untrusted (float memcpy'd from the member): clamp into the decoded image so a
            // bad frame record can never describe a negative or out-of-bounds rect.
            f.x = std::clamp(static_cast<int>(std::lround(uv[0] * out.img.w)), 0, out.img.w);
            f.y = std::clamp(static_cast<int>(std::lround(uv[1] * out.img.h)), 0, out.img.h);
            const int x1 = std::clamp(static_cast<int>(std::lround(uv[2] * out.img.w)), f.x, out.img.w);
            const int y1 = std::clamp(static_cast<int>(std::lround(uv[3] * out.img.h)), f.y, out.img.h);
            f.w = x1 - f.x;
            f.h = y1 - f.y;
            out.frames.push_back(f);
        }
    }
    return true;
}

std::uint32_t FrameHash(std::string_view name) {
    u32 h = 0;
    for (char c : name)
        h = h * 33 + static_cast<u8>(c);
    return h;
}

const Frame* FindFrame(const Texture& t, std::uint32_t hash) {
    for (const auto& f : t.frames)
        if (f.hash == hash)
            return &f;
    return nullptr;
}
const Frame* FindFrame(const Texture& t, std::string_view name) {
    if (!name.empty() && name[0] == '#') {
        u32 v = 0;
        for (char c : name.substr(1))
            v = v * 16 + static_cast<u32>(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
        return FindFrame(t, v);
    }
    return FindFrame(t, FrameHash(name));
}

d3_img::Image FrameImage(const Texture& t, const Frame& f) {
    return d3_img::Crop(t.img, f.x, f.y, f.w, f.h);
}

} // namespace d3_tex
