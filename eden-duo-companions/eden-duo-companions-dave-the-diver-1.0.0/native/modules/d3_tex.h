// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III Switch .tex SNOs (2.7.7): 0x10 SNO body; body+0x10 d3 format, +0x14 w, +0x18 h,
// +0x30 SerializeData(mip blob = NVN XTX "DFvN"/"HBvN", block-linear GOB swizzle), +0x210 frame
// count, +0x214 SerializeData(frames: u32 name hash, f32 u0, v0, u1, v1). Frame names hash as
// h = h * 33 + byte (case-sensitive). Reference: visual/tools/d3tex.py.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "d3_img.h"

namespace d3_tex {

struct Frame {
    std::uint32_t hash = 0;
    int x = 0, y = 0, w = 0, h = 0; // pixels in the decoded texture
};

struct Texture {
    d3_img::Image img;
    std::vector<Frame> frames;
};

/// Decodes a whole .tex member. R8 textures come back as grey with the value in every channel.
bool DecodeTex(std::span<const std::uint8_t> tex, Texture& out);
/// Decodes an XTX blob (also used by the font pages).
bool DecodeXtx(std::span<const std::uint8_t> blob, d3_img::Image& out);

std::uint32_t FrameHash(std::string_view name);
const Frame* FindFrame(const Texture& t, std::uint32_t hash);
const Frame* FindFrame(const Texture& t, std::string_view name);
d3_img::Image FrameImage(const Texture& t, const Frame& f);

} // namespace d3_tex
