// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "yoru_view.h"
#include <memory>
#include <string_view>
namespace yoru {
// Asset-worker-only effective RomFS decoder. No bundled game/patch resources.
class Assets {
public:
    explicit Assets(const EdenDsmodHostApi&);
    ~Assets();
    bool Ready();
    bool Cell(Image&, std::string_view map, std::string_view name,
              int x,int y,int w,int h,int opacity=255);
    bool Art(Image&, std::string_view name,int x,int y,int w,int h,int opacity=255);
    // center: `x` is each wrapped line's own centre instead of its left edge (the renderer is
    // effectively monospace per encoded codepoint, so a line's width is exactly
    // codepoint_count*pitch -- no separate measurement pass is needed to centre it).
    // out_width: when non-null, receives the widest drawn line's width in px.
    int Text(Image&, std::string_view utf8,int x,int y,int px,int maxw,
             uint32_t rgba=0xeee9dfff,bool english_label=false,bool center=false,
             int* out_width=nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace yoru
