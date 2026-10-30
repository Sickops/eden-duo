// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Pure, font-independent strip layout math for the native-scrolled Log (owner feedback
// 2026-10-16: smooth drag-to-scroll instead of the old pan_zoom raster body). The row heights
// themselves depend on the patched font (measured in yoru_view.cpp's BuildLogLayout, which needs
// real ROMFS/Assets access and so cannot be unit tested without a game dump); everything here
// takes already-measured {index, top, height} triples and is fully deterministic, so it is
// exercised directly by yoru_reader_test.cpp without any font/asset dependency.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace yoru {
// One laid-out past-history row: its row index (yoru_history.h's PastIndex() result, not the
// physical ring slot), and its vertical span in the Log's single virtual content column
// (top..top+height), newest row at top=0, growing downward (oldest at the bottom).
struct LogEntryLayout {
  int32_t index{-1};
  int32_t top{0};
  int32_t height{0};
  bool operator==(const LogEntryLayout &) const = default;
};

// The column's total height including the trailing `pad` (so the last strip ends cleanly
// instead of exactly on the last entry's bottom edge). 0 when there are no entries.
inline int32_t LogTotalHeight(const std::vector<LogEntryLayout> &entries, int32_t pad) {
  if (entries.empty()) {
    return 0;
  }
  const auto &last = entries.back();
  return last.top + last.height + std::max<int32_t>(0, pad);
}

// How many fixed-height strip images cover `total_height` (ceiling division; 0 when either is
// non-positive). Matches the ScrollRegion's own row_h-based content-height math
// (mod_ui_scroll.cpp's MeasureScroll: rows = ceil(count/cols), content_h = rows*row_h + pad) with
// cols=1: strip_count plays the same role as `count_bind` there.
inline int32_t LogStripCount(int32_t total_height, int32_t strip_h) {
  if (strip_h <= 0 || total_height <= 0) {
    return 0;
  }
  return (total_height + strip_h - 1) / strip_h;
}

// The indices (into `entries`) of every entry intersecting strip `strip_index`'s
// [strip_top, strip_top + strip_h) span, in their original (top-to-bottom) order. An entry
// straddling two strips' boundary is returned by both calls -- each strip draws it clipped to its
// own span, offset by -strip_top, so the two halves line up seamlessly when composited back to
// back by the native ScrollRegion.
inline std::vector<size_t> EntriesInStrip(const std::vector<LogEntryLayout> &entries,
                                          int32_t strip_index, int32_t strip_h) {
  std::vector<size_t> out;
  if (strip_h <= 0 || strip_index < 0) {
    return out;
  }
  const int32_t strip_top = strip_index * strip_h;
  const int32_t strip_bottom = strip_top + strip_h;
  for (size_t i = 0; i < entries.size(); ++i) {
    const int32_t top = entries[i].top;
    const int32_t bottom = top + entries[i].height;
    if (bottom > strip_top && top < strip_bottom) {
      out.push_back(i);
    }
  }
  return out;
}
} // namespace yoru
