// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Pure, asset-independent helpers for two owner-feedback items (2026-10-16 round 2): the Log's
// new-row slide-in animation (item 2) and the BuildLogLayout re-measurement cache (item 5). Kept
// header-only and dependency-free, like yoru_log_layout.h, so yoru_reader_test.cpp can exercise
// the actual math without a font/ROMFS dump.
#pragma once
#include <algorithm>
#include <cstdint>

namespace yoru {
// Ease-out cubic: 0 at t=0, 1 at t=1, monotonically increasing, never overshoots past 1 -- the
// curve yoru_module.cpp's slide animation (and SlideOffset below) uses throughout.
inline double EaseOutCubic(double t) {
  t = std::clamp(t, 0.0, 1.0);
  const double inv = 1.0 - t;
  return 1.0 - inv * inv * inv;
}
// The Log column's published "log.slide_dy" value `elapsed_ms` into a `duration_ms` ease-out that
// starts at `from` (<= 0: the new top entry's own slot height, negated -- see yoru_module.cpp's
// own comment on why) and always settles at 0 (rest, no offset -- gen_manifest.py's y_bind adds
// this to every strip row's y, so 0 means "exactly where the layout already puts them"). A
// non-positive `duration_ms` snaps to rest immediately rather than dividing by zero.
inline double SlideOffset(double from, double elapsed_ms, double duration_ms) {
  if (duration_ms <= 0.0) {
    return 0.0;
  }
  const double t = std::clamp(elapsed_ms / duration_ms, 0.0, 1.0);
  return from * (1.0 - EaseOutCubic(t));
}
// Owner item 5 (perf): BuildLogLayout's result is a pure function of exactly these four inputs --
// the session epoch (a native load/reset clears retained history without necessarily changing
// `content_rev`'s own running total, so it must invalidate on its own), the row content identity
// (`content_rev` bumps exactly once per row actually committed -- yoru_module.cpp's
// log_content_rev -- never on a mere scroll/tab switch), the font size, and the strip width
// (currently the fixed constant kLogStripW, folded in so this key stays correct if that is ever
// made configurable). Comparing four integers is O(1); re-measuring every retained row's text
// (BuildLogLayout, one assets.Text() call per row plus a PlateHeight() per named row) is not --
// that was the ~600us/Sample() regression this cache fixes. The default-constructed key is a
// sentinel distinct from any real combination of epoch==0, so the very first Sample() always
// misses the cache.
struct LayoutCacheKey {
  uint64_t epoch{~uint64_t{0}};
  int64_t content_rev{-1};
  int32_t font_px{-1};
  int32_t width{-1};
  bool operator==(const LayoutCacheKey &) const = default;
};
} // namespace yoru
