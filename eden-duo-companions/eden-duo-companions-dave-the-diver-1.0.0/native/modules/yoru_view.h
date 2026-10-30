// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "yoru_log_layout.h"
#include "yoru_reader.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
namespace yoru {
class Assets; // yoru_assets.h; forward-declared so Assets-only callers skip the include.
struct Image {
  uint32_t w{}, h{};
  std::vector<uint8_t> rgba;
};
// Step 1 cleanup: one rounded, clamped alpha-compositing pair shared by yoru_view.cpp's
// Composite() and yoru_assets.cpp's Blend(), instead of two near-duplicate implementations with
// inconsistent (truncating vs rounding) division.
inline uint8_t BlendChannel(unsigned src, unsigned dst, unsigned alpha) {
  return uint8_t((src * alpha + dst * (255 - alpha) + 127) / 255);
}
inline uint8_t BlendAlpha(unsigned src_a, unsigned dst_a) {
  return uint8_t(std::min(255u, src_a + (dst_a * (255 - src_a) + 127) / 255));
}
// state.screen (published every Sample): what the companion currently has to show, independent
// of which footer tab (page) is selected. 0 waiting (no module-rendered frame exists yet: not
// compatible, or compatible but the asset worker has never completed one), 1 start (compatible,
// never seen a story this epoch), 2 loading (was ready this epoch, transiently not ready now -- a
// scene/story-object rebuild), 3 story (ready, has retained history).
enum class Screen : int { Waiting = 0, Start = 1, Loading = 2, Story = 3 };
struct ViewState {
  Snapshot snapshot;
  int page = 0; // 0 Log, 1 Option, 2 CG (owner feedback 2026-10-16: "Manual" tab renamed).
  int cg_selected = -1;
  int manual_page = 0, log_font_size = 36;
  bool full_cg = false;
  Screen screen = Screen::Waiting;
  // No "which scroll-speed/follow-newest choice is selected" field here: that cannot be read back
  // reliably from the module (mod_runtime.cpp runs a module's Sample() BEFORE PublishFlags, every
  // tick -- see yoru_view.cpp's OptionRow comment for how this was found, live, and worked
  // around). The Option page's selection highlight is a native manifest widget instead.
};
// Owner feedback 2026-10-16 ("Scroll on Log seems not to work" / "smooth, not step by step"):
// native drag-to-scroll (mod_types_page.h's ScrollRegion) replaces the old pan_zoom raster body
// and its log_scroll/log_pan/log_seek* step actions. The module now lays out the WHOLE retained
// past history once into one virtual content column and serves it as fixed-height strip images
// (module:yoru:log/<layout_rev>/<strip_index>) a repeat template scrolls natively; a strip is
// re-rendered only when requested (e.g. evicted from the host's image cache and scrolled back
// to), never on every drag frame. BuildLogLayout is shared by the asset-worker renderer
// (RenderLogStrip, below) and the tick-thread publisher (yoru_module.cpp's Module::Sample, which
// owns a second, measurement-only Assets instance so it never touches the asset-worker-only
// `View` -- see its own comment) so both always agree on exactly the same geometry.
struct LogLayoutResult {
  std::vector<LogEntryLayout> entries; // newest (top) to oldest (bottom); see PastIndex().
  int total_height{0};                 // padded; see LogTotalHeight.
};
LogLayoutResult BuildLogLayout(Assets &assets, const Snapshot &snapshot, int font_px);
// Fixed strip/tile geometry, shared with yoru_module.cpp (strip/tile image keys and the
// log.strip_count/cg.entry_count points) and tools/yoru-tomosu/gen_manifest.py (ScrollRegion
// rect/row_h/cols -- kept in sync there by hand; gen_manifest.py's own header says where).
// 128 px strips keep the blank tail below the oldest entry under one strip, so "Oldest" lands
// on the true end; the bottom pad keeps that entry clear of the "Oldest" overlay label.
inline constexpr int kLogStripW = 1240, kLogStripH = 128, kLogBottomPad = 72;
inline constexpr int kCgTileW = 520, kCgTileH = 329, kCgGalleryCount = 53;
// Scrollbar art extracted once from the game's own ui_avg/ui_extra atlases, published under
// fixed (revision-less) keys: it never changes, so the host's module-image cache serves it
// straight from cache after the first decode.
enum class BarArt { LogThumb, LogTrack, CgThumb, CgTrack, OldestLabel };
// Constructor stores the host only; rendering/asset IO runs on the single asset
// worker. No guest actions or asset publication here.
class View {
public:
  explicit View(const EdenDsmodHostApi &host);
  ~View();
  // Full-page chrome: backdrop, heading, footer, the Option page (text size / scroll speed /
  // follow newest / controller manual), start/loading/empty-history messages, and the CG-full
  // illustration. On the Log/CG pages the scrolled content's own area is left to the backdrop --
  // the native ScrollRegion's repeat-template rows (RenderLogStrip/RenderCgTile) are drawn over
  // it by the manifest, not composited in here.
  bool Render(const ViewState &, Image &);
  bool RenderLogStrip(const ViewState &, int strip_index, Image &);
  bool RenderCgTile(int gallery_index, bool unlocked, Image &);
  bool RenderBarArt(BarArt, Image &);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace yoru
