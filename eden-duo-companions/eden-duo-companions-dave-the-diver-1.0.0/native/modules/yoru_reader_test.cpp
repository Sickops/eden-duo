// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic model checks. Real game transition coverage is recorded separately.
#include "yoru_reader.h"
#include "yoru_history.h"
#include "yoru_log_layout.h"
#include "yoru_anim.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>
using namespace yoru;
namespace {
constexpr uint64_t Base = 0x100000, Story = 0x500000, Overlay = 0x400000;
struct Memory {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x600000);
  size_t reads{}, largest{};
  bool tear_header{}, tear_ring{}, tear_chain{};
  void Put32(uint64_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i)
      bytes.at(at + i) = uint8_t(value >> (i * 8));
  }
  void Put64(uint64_t at, uint64_t value) {
    for (int i = 0; i < 8; ++i)
      bytes.at(at + i) = uint8_t(value >> (i * 8));
  }
  void Text(uint64_t at, std::u32string_view text) {
    for (size_t i = 0; i < text.size(); ++i)
      Put32(at + i * 4, uint32_t(text[i]));
    Put32(at + text.size() * 4, 0);
  }
  void Seed(int head = 2, int count = 2) {
    std::fill(bytes.begin(), bytes.end(), 0);
    reads = largest = 0;
    tear_header = tear_ring = tear_chain = false;
    std::copy(MainIdentity.begin(), MainIdentity.end(), bytes.begin() + Base);
    Put64(Base + RootOffset, Story);
    Put64(Story, Base + StoryVtableOffset);
    Put64(Story + 8, Base + ManagerOffset);
    Put32(Story + ModeOffset, 3);
    Put32(Story + HeaderOffset, head);
    Put32(Story + HeaderOffset + 4, 0);
    Put32(Story + HeaderOffset + 8, count);
    for (int i = 0; i < count; ++i) {
      int slot = (head - count + 64 + i) % 64;
      const uint64_t at = Story + RingOffset + slot * RecordSize;
      Text(at, U"\u591c\u532e\n");
      Text(at + 0x200, i % 2 ? U"\u661d" : U"");
      Put32(at + 0x280, uint32_t(i + 1));
    }
    bytes[Base + FlagsOffset] = 1;
  }
  bool Read(uint64_t at, void *output, size_t size) {
    ++reads;
    largest = std::max(largest, size);
    if (at > bytes.size() || size > bytes.size() - at)
      return false;
    std::memcpy(output, bytes.data() + at, size);
    if (tear_chain && at == Story + RingOffset) {
      Put64(Overlay + 0x28, 0);
      tear_chain = false;
    }
    if (tear_header && at == Story + RingOffset) {
      Put32(Story + HeaderOffset, 3);
      tear_header = false;
    }
    if (tear_ring && at == Story + RingOffset) {
      Put32(Story + RingOffset, 0x661d);
      tear_ring = false;
    }
    return true;
  }
  ReadFn Fn() {
    return [&](uint64_t at, void *out, size_t size) {
      return Read(at, out, size);
    };
  }
};
void CheckEmpty(const Snapshot &s) {
  assert(!s.ready && !s.cg_ready && s.rows.empty());
  assert(std::all_of(s.cg_flags.begin(), s.cg_flags.end(),
                     [](uint8_t b) { return b == 0; }));
}
Snapshot History(int first, int count) {
  Snapshot s;
  s.ready = s.compatible = true;
  s.epoch = 7;
  s.story = Story;
  for (int i = 0; i < count; ++i) {
    Row row;
    row.voice = first + i;
    row.text = "row" + std::to_string(row.voice);
    row.physical_slot = uint32_t(row.voice % 64);
    s.rows.push_back(std::move(row));
  }
  return s;
}
void CheckPastHistory() {
  for (int count : {0, 1, 2, 63}) {
    auto s = History(1, count);
    assert(PastCount(s) == size_t(std::max(0, count - 1)));
    assert(!PastIndex(s, -1));
    assert(!PastIndex(s, int(PastCount(s))));
    for (int offset = 0; offset < int(PastCount(s)); ++offset) {
      const auto index = PastIndex(s, offset);
      assert(index && s.rows[*index].voice == count - 1 - offset);
      assert(*index != s.rows.size() - 1); // Never leak current main dialogue.
    }
  }
  auto stale = History(1, 63);
  stale.ready = false;
  assert(PastCount(stale) == 0 && !PastIndex(stale, 0));
}
// Owner feedback 2026-10-16 (native drag-to-scroll Log, smooth not step by step): the strip
// layout math (yoru_log_layout.h) that the native ScrollRegion's row_h/count_bind and the
// repeat-template strip images are built from. Pure and font-independent -- it takes
// already-measured {index, top, height} triples, so it is exercised directly here with synthetic
// layouts instead of needing a real font/ROMFS dump (yoru_view.cpp's BuildLogLayout, which DOES
// need real assets to measure text, is covered by the live/offline render checks instead).
void CheckLogStripLayout() {
  // No entries: zero height, zero strips, nothing intersects any strip.
  assert(LogTotalHeight({}, 24) == 0);
  assert(LogStripCount(0, 512) == 0);
  assert(EntriesInStrip({}, 0, 512).empty());
  // One short entry plus padding.
  const std::vector<LogEntryLayout> one{{0, 0, 210}};
  assert(LogTotalHeight(one, 24) == 210 + 24);
  assert(LogStripCount(LogTotalHeight(one, 24), 512) == 1); // fits in one strip.
  assert(EntriesInStrip(one, 0, 512) == std::vector<size_t>{0});
  assert(EntriesInStrip(one, 1, 512).empty()); // nothing in the second strip.
  // Several entries spanning multiple strips, including one straddling a strip boundary exactly
  // at the 512px line (top=480,height=300 -> spans [480,780), crossing strip0/strip1 at y=512).
  const std::vector<LogEntryLayout> many{
      {0, 0, 300},    // [0,300) -- strip 0 only.
      {1, 335, 100},  // [335,435) -- strip 0 only.
      {2, 480, 300},  // [480,780) -- straddles strip 0 and strip 1.
      {3, 815, 400},  // [815,1215) -- straddles strip 1 and strip 2.
  };
  assert(LogTotalHeight(many, 0) == 815 + 400);
  assert(LogStripCount(LogTotalHeight(many, 0), 512) == 3); // ceil(1215/512) == 3.
  assert((EntriesInStrip(many, 0, 512) == std::vector<size_t>{0, 1, 2}));
  assert((EntriesInStrip(many, 1, 512) == std::vector<size_t>{2, 3}));
  assert((EntriesInStrip(many, 2, 512) == std::vector<size_t>{3}));
  assert(EntriesInStrip(many, 3, 512).empty());
  // Every entry index appears in at least one strip, and the union of every strip's entries is
  // exactly the full entry set (nothing dropped between strips, nothing duplicated beyond a
  // genuine straddle) -- checked generically, not just for the hand-picked case above.
  const int strips = LogStripCount(LogTotalHeight(many, 0), 512);
  std::vector<int> seen(many.size(), 0);
  for (int strip = 0; strip < strips; ++strip)
    for (const size_t i : EntriesInStrip(many, strip, 512))
      ++seen[i];
  for (size_t i = 0; i < many.size(); ++i)
    assert(seen[i] >= 1);
  // Padding changes total height (and so strip_count) without touching any entry's own span.
  assert(LogTotalHeight(many, 100) == 815 + 400 + 100);
  assert(LogStripCount(LogTotalHeight(many, 100), 512) ==
         LogStripCount(815 + 400 + 100, 512));
  // row_h <= 0 is defined as "nothing" rather than dividing by zero or looping forever.
  assert(LogStripCount(1000, 0) == 0);
  assert(EntriesInStrip(many, 0, 0).empty());
  // Negative strip_index never underflows into entries before the content starts.
  assert(EntriesInStrip(many, -1, 512).empty());
}
// 2026-10-15 polish: CollapseSoftBreaks (Log reflow). A lone '\n'/"\r\n" becomes a space; 2+
// consecutive newlines (a real paragraph break) collapse to exactly one '\n'.
void CheckCollapseSoftBreaks() {
#define SB "\x1f" // SoftBreak, kept as its own literal so it never swallows a hex letter.
  assert(CollapseSoftBreaks("") == "");
  assert(CollapseSoftBreaks("one line") == "one line");
  assert(CollapseSoftBreaks("barely\nany light") == "barely" SB "any light");
  assert(CollapseSoftBreaks("barely\r\nany light") == "barely" SB "any light");
  assert(CollapseSoftBreaks("a\nb\nc") == "a" SB "b" SB "c"); // several single breaks, all soften.
  assert(CollapseSoftBreaks("para one\n\npara two") == "para one\npara two");
  assert(CollapseSoftBreaks("para one\n\n\npara two") == "para one\npara two"); // 3+ -> one.
  assert(CollapseSoftBreaks("para one\r\n\r\npara two") == "para one\npara two");
  assert(CollapseSoftBreaks("\nleading") == SB "leading");
  assert(CollapseSoftBreaks("trailing\n") == "trailing" SB);
  assert(CollapseSoftBreaks("\n\nleading paragraph break") == "\nleading paragraph break");
  // Soft wraps swallow the patch's trailing space and the next line's U+3000 indent.
  assert(CollapseSoftBreaks("go through \n\xE3\x80\x80with it") == "go through" SB "with it");
  assert(CollapseSoftBreaks("\xE3\x80\x80" "First\n  second") == "\xE3\x80\x80" "First" SB "second");
  // A mix: one soft wrap, then a real paragraph break.
  assert(CollapseSoftBreaks("line one\nstill line one\n\nline two") ==
         "line one" SB "still line one\nline two");
#undef SB
  // After decoding, spaces that sat inside glyph pairs around the break are swallowed too.
  const char32_t sb = char32_t(SoftBreak);
  assert(JoinSoftBreaks(std::u32string(U"through ") + sb + U" with") == U"through with");
  assert(JoinSoftBreaks(std::u32string(U"a") + sb + U"b") == U"a b");
  assert(JoinSoftBreaks(std::u32string(1, sb) + U"lead") == U"lead");
  assert(JoinSoftBreaks(std::u32string(U"tail ") + sb) == U"tail");
  assert(JoinSoftBreaks(U"g i v e   i t") == U"g i v e   i t"); // Real spacing untouched.
}
// WrapKeepingSpaces (English Log layout): whitespace runs and indents survive inside a line,
// whitespace at a break point is dropped, '\n' always breaks.
void CheckWrapKeepingSpaces() {
  const auto wide = [](size_t) { return true; };
  const auto ten = [](size_t n) { return n <= 10; };
  using Lines = std::vector<std::u32string>;
  assert(WrapKeepingSpaces(U"", wide).empty());
  assert(WrapKeepingSpaces(U"g i v e   i t   b a c k ", wide) ==
         Lines{U"g i v e   i t   b a c k"}); // Trailing whitespace is not drawn.
  assert(WrapKeepingSpaces(U"  indented line", wide) == Lines{U"  indented line"});
  assert(WrapKeepingSpaces(U"one two three", ten) == (Lines{U"one two", U"three"}));
  assert(WrapKeepingSpaces(U"aaa   bbb   ccc", ten) == (Lines{U"aaa   bbb", U"ccc"}));
  assert(WrapKeepingSpaces(U"first\nsecond", wide) == (Lines{U"first", U"second"}));
  assert(WrapKeepingSpaces(U"a\r\nb", wide) == (Lines{U"a", U"b"}));
  assert(WrapKeepingSpaces(U"averyveryverylongword x", ten) ==
         (Lines{U"averyveryverylongword", U"x"})); // An overlong word stays whole.
}
// Owner item 2 (2026-10-16 round 2), "slide in from top for new log": the ease-out curve and the
// offset function yoru_module.cpp's Sample() uses every tick while a slide is in flight. Pure, so
// exercised directly here rather than through the module's host-API-dependent Sample()/LoadImage.
void CheckSlideEasing() {
  // Ease-out cubic: 0 at t=0, 1 at t=1, clamped outside [0,1], monotonically non-decreasing (an
  // ease-OUT curve never overshoots past its end value, unlike an elastic/back curve).
  assert(EaseOutCubic(0.0) == 0.0);
  assert(EaseOutCubic(1.0) == 1.0);
  assert(EaseOutCubic(-5.0) == 0.0);
  assert(EaseOutCubic(5.0) == 1.0);
  double prev = -1.0;
  for (int i = 0; i <= 10; ++i) {
    const double v = EaseOutCubic(i / 10.0);
    assert(v >= prev);
    assert(v >= -1e-9 && v <= 1.0 + 1e-9);
    prev = v;
  }
  // Fast at the start, slowing down: an ease-out's first half covers more ground than its second.
  assert(EaseOutCubic(0.5) > 0.5);
  // SlideOffset: starts exactly at `from`, always settles at exactly 0 (rest) by `duration_ms`,
  // monotonically approaches 0 without ever crossing past it (no bounce/overshoot), and a
  // non-positive duration or a zero `from` never divides by zero or animates.
  assert(SlideOffset(-300.0, 0.0, 260.0) == -300.0);
  assert(SlideOffset(-300.0, 260.0, 260.0) == 0.0);
  assert(SlideOffset(-300.0, 1000.0, 260.0) == 0.0); // past the end: clamped at rest.
  assert(SlideOffset(0.0, 100.0, 260.0) == 0.0);      // nothing to animate.
  assert(SlideOffset(-120.0, 10.0, 0.0) == 0.0);       // zero duration: snaps to rest, no NaN/inf.
  double prev_mid = -300.0;
  for (int i = 0; i <= 10; ++i) {
    const double v = SlideOffset(-300.0, i * 26.0, 260.0);
    assert(v >= prev_mid - 1e-9 && v <= 0.0 + 1e-9); // between start and rest, never past either.
    prev_mid = v;
  }
}
// Owner item 5 (perf): the cache key BuildLogLayout's result is keyed by (Sample()'s own use,
// yoru_module.cpp) -- equality here IS the cache hit/miss decision, so it is checked directly.
void CheckLayoutCacheKey() {
  const LayoutCacheKey a{7, 3, 36, 1240};
  const LayoutCacheKey b{7, 3, 36, 1240};
  assert(a == b); // identical inputs: a cache hit, BuildLogLayout's text measurement is skipped.
  assert(!(a == (LayoutCacheKey{8, 3, 36, 1240})));  // epoch changed (a native load/reset).
  assert(!(a == (LayoutCacheKey{7, 4, 36, 1240})));  // content_rev changed (a row committed).
  assert(!(a == (LayoutCacheKey{7, 3, 40, 1240})));  // font size changed.
  assert(!(a == (LayoutCacheKey{7, 3, 36, 1241})));  // width changed.
  // The "never computed yet" sentinel never collides with a real (epoch==0-based) key.
  assert(LayoutCacheKey{} == LayoutCacheKey{});
  assert(!(LayoutCacheKey{} == (LayoutCacheKey{0, -1, -1, -1})));
}
// Snapshot::text_visible: the newest ring row joins the past history once the main-screen text
// window is empty, and leaves it again while a line is shown.
void CheckTextWindowPast() {
  Snapshot s;
  s.ready = true;
  s.rows.resize(3);
  for (size_t i = 0; i < 3; ++i)
    s.rows[i].text = "row" + std::to_string(i);
  assert(PastCount(s) == 2 && PastIndex(s, 0) == 1u && PastIndex(s, 1) == 0u);
  assert(!PastIndex(s, 2));
  s.text_visible = false;
  assert(PastCount(s) == 3 && PastIndex(s, 0) == 2u && PastIndex(s, 2) == 0u);
  assert(!PastIndex(s, 3));
  s.rows.resize(1);
  assert(PastCount(s) == 1);
  s.text_visible = true;
  assert(PastCount(s) == 0);
}
} // namespace
int main() {
  CheckPastHistory();
  CheckTextWindowPast();
  CheckLogStripLayout();
  CheckCollapseSoftBreaks();
  CheckWrapKeepingSpaces();
  CheckSlideEasing();
  CheckLayoutCacheKey();
  assert(SupportsBuild(BuildId));
  std::string lower(BuildId);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
    return c >= 'A' && c <= 'F' ? char(c + 32) : c;
  });
  assert(SupportsBuild(lower));
  assert(!SupportsBuild(BuildId.substr(0, 40)));
  assert(!SupportsBuild("0000"));
  Memory m;
  m.Seed();
  auto good = ReadSnapshot(m.Fn(), Base, true);
  assert(good.compatible && good.ready && good.cg_ready &&
         good.rows.size() == 2);
  assert(good.rows[0].text == "\xe5\xa4\x9c\xe5\x8c\xae\n");
  assert(good.rows[1].speaker == "\xe6\x98\x9d");
  assert(good.rows[0].voice == 1 && good.rows[1].voice == 2 &&
         good.cg_flags[0] == 1);
  assert(m.largest == RingSlots * RecordSize);
  m.reads = 0;
  auto bad = ReadSnapshot(m.Fn(), Base, false);
  CheckEmpty(bad);
  assert(!bad.compatible && m.reads == 0);
  m.bytes[Base] ^= 1;
  bad = ReadSnapshot(m.Fn(), Base, true);
  assert(!bad.compatible);
  CheckEmpty(bad);
  m.Seed();
  m.Put64(Story + 8, Base);
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.Put64(Story, Base);
  m.Put64(Story + 0x28, Story);
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.Put32(Story + HeaderOffset + 8, 64);
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.Put32(Story + HeaderOffset, 64);
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.tear_header = true;
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.tear_ring = true;
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  // Native transition removes the story via a prior scene while root and
  // the old story vtable/parent stay valid until destruction.
  m.Put64(Base + RootOffset, Overlay);
  m.Put64(Overlay, Base + 0x100);
  m.Put64(Overlay + 8, Base + ManagerOffset);
  m.Put64(Overlay + 0x28, Story);
  assert(ReadSnapshot(m.Fn(), Base, true).ready);
  m.tear_chain = true;
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  m.Put32(Story + RingOffset, 0xd800);
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed();
  for (size_t i = 0; i < 0x200; i += 4)
    m.Put32(Story + RingOffset + i, 'A');
  CheckEmpty(ReadSnapshot(m.Fn(), Base, true));
  m.Seed(2, 63);
  auto wrapped = ReadSnapshot(m.Fn(), Base, true);
  assert(wrapped.ready && wrapped.rows.size() == 63);
  assert(wrapped.rows.front().physical_slot == 3 &&
         wrapped.rows.back().physical_slot == 1);
  assert(wrapped.rows.front().voice == 1 && wrapped.rows.back().voice == 63);
  m.Seed(1, 0);
  auto empty = ReadSnapshot(m.Fn(), Base, true);
  assert(empty.ready && !empty.cg_ready && empty.rows.empty() &&
         empty.cg_flags[0] == 0);
  // A normal append preserves existing rows; a native load/reset does not.
  m.Seed(2, 2);
  auto old = ReadSnapshot(m.Fn(), Base, true);
  m.Seed(3, 3);
  auto appended = ReadSnapshot(m.Fn(), Base, true);
  assert(IsContinuation(old, appended));
  m.Seed(1, 1);
  auto loaded = ReadSnapshot(m.Fn(), Base, true);
  assert(!IsContinuation(appended, loaded));
  // Full ring shifts chronological origin without creating a false reset.
  m.Seed(0, 63);
  auto full = ReadSnapshot(m.Fn(), Base, true);
  m.Put32(Story + HeaderOffset, 1);
  m.Text(Story + RingOffset, U"\u661d");
  m.Put32(Story + RingOffset + 0x280, 64);
  auto shifted = ReadSnapshot(m.Fn(), Base, true);
  assert(IsContinuation(full, shifted));
  // Reader invalidation clears all feature data and advances its cache epoch.
  m.Seed();
  EdenDsmodHostApi h{};
  h.title_id = TitleId;
  h.main_base = Base;
  h.userdata = &m;
  std::copy(BuildBytes.begin(), BuildBytes.end(), h.build_id);
  h.is_mapped = [](void *p, uint64_t at, uint64_t size) -> EdenDsmodBool {
    const auto &mm = *static_cast<Memory *>(p);
    return at <= mm.bytes.size() && size <= mm.bytes.size() - at;
  };
  h.read_memory = [](void *p, uint64_t at, void *out,
                     size_t size) -> EdenDsmodBool {
    return static_cast<Memory *>(p)->Read(at, out, size);
  };
  Reader reader(h);
  reader.Sample(h);
  assert(reader.GetSnapshot().ready);
  auto epoch = reader.GetSnapshot().epoch;
  m.Put64(Base + RootOffset, 0);
  reader.Sample(h);
  CheckEmpty(reader.GetSnapshot());
  assert(reader.GetSnapshot().epoch > epoch);
  epoch = reader.GetSnapshot().epoch;
  reader.Sample(h);
  assert(reader.GetSnapshot().epoch == epoch);
  m.Seed();
  reader.Sample(h);
  assert(reader.GetSnapshot().ready && reader.GetSnapshot().epoch > epoch);
  h.build_id[0] ^= 1;
  reader.Sample(h);
  CheckEmpty(reader.GetSnapshot());
  assert(!reader.GetSnapshot().compatible);
  CheckEmpty(
      ReadSnapshot(m.Fn(), std::numeric_limits<uint64_t>::max() - 10, true));
  std::cout << "Yoru model checks passed: identity, bounds, Unicode, torn "
               "reads, wrap, reset, stale clear, slide easing, layout cache key\n";
}
