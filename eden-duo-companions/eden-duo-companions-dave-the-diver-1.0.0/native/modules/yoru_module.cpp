// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only native Log/CG foundation. Navigation is companion-local.
//
// Owner feedback 2026-10-16 ("Scroll on Log seems not to work" / "smooth, not step by step" /
// Latest+Oldest at the scrollbar ends / Option page controls): the Log and CG bodies are no
// longer a single module-rendered raster the manifest panned with a pan_zoom image widget (every
// one-finger swipe on it was consumed by the host's own pan/zoom gesture, verified in the device
// log -- "aux gesture on 'yoru_log_body' ended: zoom 1.00 pan (129,293)" -- never reaching this
// module). They are native drag-to-scroll ScrollRegions (mod_types_page.h) that a repeat
// template fills with small, individually cached module images: fixed-height Log "strips"
// (module:yoru:log/<layout_rev>/<strip_index>) and per-tile CG thumbnails
// (module:yoru:cg/<gallery_index>/<unlocked>). Scrolling itself -- drag, fling, the native
// scrollbar, Latest/Oldest -- is entirely the host's; this module only ever renders a strip/tile
// image when the host actually asks for one (a cache miss), never once per scrolled frame. See
// tools/yoru-tomosu/gen_manifest.py and yoru_view.h's comments for the manifest side.
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "yoru_assets.h"
#include "yoru_reader.h"
#include "yoru_history.h"
#include "yoru_log_layout.h"
#include "yoru_anim.h"
#include "yoru_view.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <charconv>
#include <deque>
#include <mutex>
#include <string>

namespace yoru {
namespace {
bool Same(const Snapshot &a, const Snapshot &b, int page) {
  return a.compatible == b.compatible && a.ready == b.ready &&
         a.cg_ready == b.cg_ready && a.story == b.story && a.epoch == b.epoch &&
         (page != 0 || (a.rows == b.rows && a.text_visible == b.text_visible)) &&
         (page != 2 || a.cg_flags == b.cg_flags);
}
// Published strip/tile key caps: generous upper bounds on how many "log.strip.{i}.image" /
// "cg.tile.{i}.image" text points are published every Sample(). kMaxPublishedStrips must stay >=
// the largest strip_count ever reached in practice (worst-case estimate: 62 past rows at a
// minimum ~245px each / 512px strips =~ 30; 56 leaves a wide margin) -- a strip_count that
// somehow exceeded it would simply lose its tail (no crash: ExpandWidgets never asks for an
// index past "log.strip_count", but this module would have nothing to answer with past the cap).
// gen_manifest.py's Log repeat template uses the SAME constant (kept in sync there by hand; see
// its own header comment).
constexpr size_t kMaxPublishedStrips = 160;
// Owner item 3 ("fade" tab switch): whether tab `idx`'s own chrome composite (ui.image.0/1/2,
// rendered regardless of which tab is actually showing -- see the per-tab stream's own comment on
// why) needs a fresh frame, i.e. whether anything that tab's own View::Render path actually reads
// changed since the last one. Mirrors Same()'s own per-page narrowing above, just over ViewState
// (screen/font/manual_page are module-only state, never in Snapshot) instead of Snapshot alone.
bool TabChromeChanged(int idx, const ViewState &a, const ViewState &b) {
  if (a.snapshot.compatible != b.snapshot.compatible) {
    return true;
  }
  switch (idx) {
  case 0: // Log: Heading + LogTextSizeRow + start/loading/empty-history messages + Latest/Oldest.
    return a.screen != b.screen || a.log_font_size != b.log_font_size ||
           (PastCount(a.snapshot) > 0) != (PastCount(b.snapshot) > 0);
  case 1: // Option: the two OptionRows (static) + Manual. No text-size control here (2026-10-16:
          // moved to the Log header only, yoru_view.cpp's OptionPage draws nothing that depends
          // on log_font_size), so this tab's chrome does not need to watch it.
    return a.manual_page != b.manual_page;
  default: // CG: Heading + the "gallery opens once loaded" message.
    return a.snapshot.cg_ready != b.snapshot.cg_ready;
  }
}
struct Module {
  explicit Module(const EdenDsmodHostApi &host)
      : reader(host), view(host), measure_assets(host) {}
  Reader reader;
  View view;           // Used only on the host's serialized asset worker (RenderX via LoadImage).
  Assets measure_assets; // Tick-thread-only text measuring for Sample()'s strip_count/layout_rev
                         // publishing -- a second, independent instance (its own font/glyph
                         // cache) so Sample() never touches `view`, which stays asset-worker-only
                         // exactly as before this change.
  std::mutex mutex;
  ViewState state;
  uint64_t revision{};
  std::atomic_bool first_painted{false};
  std::chrono::steady_clock::time_point retry_after{};
  std::deque<std::pair<uint64_t, ViewState>> frames; // module:yoru:view/<revision>
  std::deque<std::pair<int, ViewState>> layout_frames; // module:yoru:log/<layout_rev>/<strip>
  LogLayoutResult last_layout;
  int layout_rev{0};
  int strip_count{0};
  // Owner item 5 (perf): the (epoch, content_rev, font_px, width) tuple `last_layout` was last
  // computed for -- see yoru_anim.h's LayoutCacheKey comment and Sample()'s own use of it.
  LayoutCacheKey layout_cache_key_;
  // state.screen bookkeeping: true once `snapshot.ready` has been observed at least once since
  // the last epoch reset (native load/new game), distinguishing "loading" (was ready, transiently
  // isn't) from "start" (title, never ready yet this epoch). Reset alongside the other
  // epoch-scoped UI state below.
  bool ever_ready_this_epoch{false};
  // "Latest" (manual tap, owner item 3): a monotonically-increasing nonce. gen_manifest.py sums
  // it with a derived, flag-gated copy of log_content_rev (below) into "opt.log_reset_combined",
  // each Log ScrollRegion's reset_bind: ANY change in the sum snaps that region's native scroll
  // offset to 0 (top) -- mod_input.cpp's PublishScroll, verified at mod_input.cpp:1012-1028.
  // Always incrementing (never conditionally) is what makes a repeated "Latest" tap keep working
  // even though the module cannot see whether the native offset is already 0 (see Action()'s own
  // comment on why not).
  int log_latest_nonce{0};
  // Automatic half of "Follow newest" (owner item 4): incremented unconditionally whenever a new
  // row is committed at the top of the retained history (see new_top_row above), REGARDLESS of
  // the "opt.follow_newest" flag's value -- this module cannot read that flag reliably. Live
  // verification (2026-10-16) found that mod_runtime.cpp's Tick() runs RunGameModule (this
  // module's Sample) at line 667, BEFORE PublishFlags at line 669, and clears the shared
  // StateSnapshot's ints every tick before that (line 650): a "@flag:" key this module tries to
  // get_i64() is therefore never populated when Sample() runs, forever -- it always reads the
  // caller's fallback, not the real value (confirmed: the Option page kept showing "Normal"/"On"
  // highlighted after tapping "Fast"/off, even though the flag itself DID change correctly). The
  // fix: this module only ever reports "new content arrived," unconditionally; gen_manifest.py's
  // derived "opt.log_reset_combined" expression -- evaluated by EvaluateDerived AFTER
  // PublishFlags, so it sees the flag fresh -- is what actually decides whether that counts.
  int log_content_rev{0};
  // "Oldest" (owner item 3) pulses the follow_bind target to the last strip index for exactly one
  // Sample() tick, then reverts to -1 -- a repeated tap must re-trigger the native follow even
  // when the target strip index hasn't changed (e.g. two taps with no scrolling in between), and
  // follow_bind only re-triggers on a VALUE CHANGE (mod_input.cpp:1043), so the steady -1 is what
  // makes the next real pulse a change again. This is the "publish -1 for one sample then the
  // index" scheme the task names explicitly.
  bool log_oldest_pulse{false};
  // Owner item 3 ("fade" tab switch): the companion page split into three manifest pages
  // (log/option/cg, gen_manifest.py), each driven by RunAction({"kind":"page","transition":
  // "fade"}) via a page_bind on the published "ui.page" value. mod_pages.cpp's DrivePageTransition
  // renders the "to" page exactly once right when the fade starts and blends it against a frozen
  // snapshot of the "from" page -- so every tab's own chrome composite has to already be current
  // BEFORE its tab is ever selected, or the incoming fade would start from a stale or blank frame.
  // One extra revision/frame-cache stream per tab, independent of `frames`/`revision` above (which
  // keeps its old single-stream meaning: it now serves ONLY the "cg-full" page, unchanged from
  // before this feature -- Render() already picks the full illustration vs. chrome purely from
  // ViewState.page/full_cg, so nothing about that stream needed to change).
  std::array<std::deque<std::pair<uint64_t, ViewState>>, 3> tab_frames;
  std::array<uint64_t, 3> tab_revision{};
  std::array<ViewState, 3> tab_last;
  std::array<bool, 3> tab_initialized{};
  // Hardening (2026-10-17): gates the CG tile publish loop below the same way tab_frames above
  // is already gated -- only redo the 53 "cg.tile.<i>.image" text() publishes (string-concat
  // allocations) when cg_ready or cg_flags actually changed since the last tick, not every tick.
  std::array<uint8_t, 53> cg_flags_published{};
  bool cg_ready_published{false};
  bool cg_flags_initialized{false};
  // Owner item 2 ("slide in from top for new log, below it slide down"): published every Sample()
  // as log.slide_dy, a float the strip repeat templates' y_bind (gen_manifest.py) adds to every
  // row's y uniformly -- the whole column eases from `slide_from_` (<=0) up to 0 (rest), so the
  // brand-new top entry (at the new layout's y=0) starts off-screen above the viewport exactly
  // `slide_from_` px up, and every older entry starts exactly where it looked before the insert
  // (see yoru_view.h's LogLayoutResult comment on why "h = the new entry's own top+height" is the
  // shift amount) and eases down to its own new resting position. gen_manifest.py's derived
  // "log.slide_dy_gated" multiplies this by "@flag:opt.follow_newest != 0" -- this module cannot
  // read that flag reliably (RunGameModule runs before PublishFlags every tick, same bug
  // log_content_rev's own comment already documents), so it always runs the animation
  // unconditionally and lets the manifest gate the VISIBLE result to exactly 0 whenever Follow
  // newest is off, which is the owner's "if the reader is scrolled away from the top, don't
  // animate (content just updates)" case this module can't otherwise tell apart.
  double slide_from_{0.0};
  double slide_offset_{0.0};
  std::chrono::steady_clock::time_point slide_start_{};
  bool slide_active_{false};
  static constexpr double kSlideMs = 260.0; // owner: "~250-300 ms ease-out".
  // Accumulated height of row(s) that committed but whose strip image has not loaded for this
  // layout_rev yet (owner: "gate the start on the new strips being loaded ... or start once the
  // image is available") -- applied, and cleared, the first Sample() tick after
  // top_strip_loaded_rev_ catches up to slide_pending_rev_. Accumulating (rather than overwriting)
  // means several lines arriving before the image loads still produce one correct combined shift
  // once it finally does.
  double slide_pending_h_{0.0};
  int slide_pending_rev_{-1};
  // Set by LoadImage() the first time strip 0 of a given layout_rev (which always contains at
  // least the top of the newest entry) decodes successfully -- read by Sample() on the tick
  // thread, written by the asset worker thread, hence atomic (same cross-thread contract as
  // `first_painted` below).
  std::atomic_int top_strip_loaded_rev_{-1};
  void Retain() {
    frames.emplace_back(++revision, state);
    retry_after = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (frames.size() > 2)
      frames.pop_front();
  }
  void Sample(const EdenDsmodHostApi &host) {
    reader.Sample(host);
    std::lock_guard lock{mutex};
    // Hardening (2026-10-17): was `reader.GetSnapshot()` (a `const Snapshot&`) bound to `auto`,
    // which silently deep-copied the whole retained-history Snapshot (up to 63 Rows, each with
    // two heap strings) every single Sample() tick -- even on an unchanged tick, before `changed`
    // below is even checked. TakeSnapshot() moves it out instead; see its own comment.
    auto next = reader.TakeSnapshot();
    const bool changed = !Same(state.snapshot, next, state.page);
    // Detected BEFORE state.snapshot is overwritten: has a genuinely new line been committed at
    // the top of the retained history since the last sample (same epoch -- a native load/reset is
    // handled separately below and never counts as "new", it is a reset)? Published as
    // log.content_rev unconditionally below -- see that publish's own comment for why this module
    // does not (and architecturally cannot) decide here whether "Follow newest" is On.
    bool new_top_row = false;
    if (next.ready && state.snapshot.ready && next.epoch == state.snapshot.epoch) {
      const auto old_top = PastIndex(state.snapshot, 0);
      const auto fresh_top = PastIndex(next, 0);
      new_top_row = fresh_top && (!old_top || next.rows[*fresh_top] != state.snapshot.rows[*old_top]);
    }
    // The past set can also change without a new top row (the newest line hides again when the
    // text window shows it, see Snapshot::text_visible): refresh the layout, but don't slide.
    const bool past_count_changed = next.ready && state.snapshot.ready &&
                                    next.epoch == state.snapshot.epoch &&
                                    PastCount(next) != PastCount(state.snapshot);
    if (new_top_row || past_count_changed)
      ++log_content_rev;
    if (next.epoch != state.snapshot.epoch) {
      state.cg_selected = -1;
      state.full_cg = false;
      ever_ready_this_epoch = false;
      // Hardening (2026-10-17): a native load/reset must always snap the Log's native scroll
      // offset back to the top, regardless of "Follow newest" -- the old pixel position belongs
      // to a different (possibly much longer) history and is meaningless after a load. Reuse
      // log_latest_nonce's existing unconditional-increment semantics (see its own comment above)
      // rather than log_content_rev, which is deliberately Follow-newest-gated for the "a new line
      // arrived during play" case; a reset is not that case; it must always jump, like a "Latest"
      // tap. Before this fix, new_top_row/past_count_changed were both gated on next.epoch ==
      // state.snapshot.epoch, so log_content_rev never moved on a reset and
      // opt.log_reset_combined's reset_bind never fired here at all -- the ScrollRegion could be
      // left scrolled into what looks like an empty or wrong position after loading a save.
      ++log_latest_nonce;
    }
    state.snapshot = std::move(next);
    if (state.snapshot.ready)
      ever_ready_this_epoch = true;
    if (state.cg_selected >= 0 &&
        (!state.snapshot.cg_ready ||
         !state.snapshot.cg_flags[size_t(state.cg_selected)])) {
      state.cg_selected = -1;
      state.full_cg = false;
    }
    // state.screen: see yoru_view.h's Screen comment. A frame can only ever have been rendered
    // once `compatible` AND the asset worker has delivered at least one successful frame
    // (first_painted) -- both are re-checked every sample, not just at startup, so losing
    // compatibility later correctly falls back to the manifest-native waiting page instead of
    // leaving a stale companion frame on screen.
    state.screen = (!state.snapshot.compatible ||
                    !first_painted.load(std::memory_order_relaxed))
                       ? Screen::Waiting
                   : !state.snapshot.ready
                       ? (ever_ready_this_epoch ? Screen::Loading : Screen::Start)
                       : Screen::Story;
    // Strip layout (owner items 1-2): layout_rev only bumps, and a fresh ViewState is only cached
    // for strip rendering, when the content actually differs ("rows/text size/width change", as
    // the task specifies). Scrolling itself never touches any of this: the native ScrollRegion
    // only ever changes its own pixel offset, which this module never sees.
    //
    // Owner item 5 (perf): BuildLogLayout used to run unconditionally every tick -- bounded (at
    // most ~62 past rows) but still a real text-measurement pass per row, which profiled at
    // ~600us/Sample() on desktop x86 with a full 62-row history (DSMod profile "sample
    // avg=600us", up from ~50us in dev.5 before this history length was reachable). Its result is
    // a pure function of (epoch, content_rev, font size, width) -- see yoru_anim.h's
    // LayoutCacheKey comment -- so it is cached here and only recomputed when that key changes; an
    // idle tick (the overwhelmingly common case) now costs one struct compare, not 62 glyph
    // measurements.
    const int px = std::clamp(state.log_font_size, 28, 52);
    LogLayoutResult layout;
    if (!state.snapshot.ready) {
      layout = LogLayoutResult{};
      layout_cache_key_ = LayoutCacheKey{};
    } else {
      const LayoutCacheKey key{state.snapshot.epoch, log_content_rev, px, kLogStripW};
      if (key == layout_cache_key_) {
        layout = last_layout; // cache hit: identical to last tick, skip BuildLogLayout entirely.
      } else if (measure_assets.Ready()) {
        layout = BuildLogLayout(measure_assets, state.snapshot, px);
        layout_cache_key_ = key;
      }
    }
    strip_count = std::min<int>(int(kMaxPublishedStrips),
                                LogStripCount(layout.total_height, kLogStripH));
    if (layout.entries != last_layout.entries ||
        layout.total_height != last_layout.total_height) {
      ++layout_rev;
      last_layout = std::move(layout);
      layout_frames.emplace_back(layout_rev, state);
      while (layout_frames.size() > 2)
        layout_frames.pop_front();
    }
    // Owner item 2 ("slide in from top for new log"): h = the brand-new top entry's own slot
    // height, including its own lead gap (its {top, height} in the FRESH layout just above --
    // yoru_view.h's LogLayoutResult comment). Accumulated rather than applied immediately: the
    // animation itself only starts once this layout_rev's strip 0 has actually loaded (below), and
    // several new rows can commit before that first decode lands.
    if (new_top_row && !last_layout.entries.empty()) {
      slide_pending_h_ += double(last_layout.entries[0].top + last_layout.entries[0].height);
      slide_pending_rev_ = layout_rev;
    }
    if (slide_pending_rev_ >= 0 &&
        top_strip_loaded_rev_.load(std::memory_order_relaxed) >= slide_pending_rev_) {
      // The pending shift's strip image is confirmed loaded: (re)start the ease-out from the
      // CURRENT eased offset minus the new shift, not from -slide_pending_h_ alone -- a line that
      // arrives mid-animation extends the existing slide smoothly instead of jumping.
      slide_from_ = slide_offset_ - slide_pending_h_;
      slide_offset_ = slide_from_;
      slide_start_ = std::chrono::steady_clock::now();
      slide_active_ = true;
      slide_pending_h_ = 0.0;
      slide_pending_rev_ = -1;
    }
    if (slide_active_) {
      const double elapsed_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - slide_start_)
                                     .count();
      slide_offset_ = SlideOffset(slide_from_, elapsed_ms, kSlideMs);
      if (elapsed_ms >= kSlideMs) {
        slide_active_ = false;
        slide_offset_ = 0.0;
      }
    }
    // "Oldest": publish the pulse (see log_oldest_pulse's own comment), then immediately consume
    // it -- the NEXT Sample() publishes -1 again unless another tap re-arms it.
    const int64_t follow_target = log_oldest_pulse ? int64_t(strip_count - 1) : -1;
    log_oldest_pulse = false;
    // A failed image key is cached by the host. Renew it at most once per
    // second until the asset worker completes the first real frame. ROMFS can
    // become available after the module is already compatible at cold boot.
    if (changed || !revision ||
        (!first_painted.load(std::memory_order_relaxed) &&
         std::chrono::steady_clock::now() >= retry_after))
      Retain();
    auto integer = [&](const std::string &name, int64_t value) {
      host.publish_i64(host.userdata, name.c_str(), value);
    };
    auto text = [&](const std::string &name, const std::string &value) {
      host.publish_text(host.userdata, name.c_str(), value.c_str());
    };
    const auto &s = state.snapshot;
    integer("state.compatible", s.compatible);
    integer("state.ready", s.ready);
    integer("state.native_mode", s.native_mode);
    integer("state.session_epoch", int64_t(s.epoch));
    integer("state.screen", int64_t(state.screen));
    integer("log.ready", s.ready);
    integer("log.session_epoch", int64_t(s.epoch));
    integer("log.count", int64_t(s.rows.size())); // Raw native ring, unchanged.
    integer("log.past_count", int64_t(PastCount(s)));
    integer("log.past_ready", PastCount(s) > 0);
    integer("log.head", s.head);
    integer("log.selected", s.selected);
    integer("log.strip_count", strip_count);
    integer("log.latest_nonce", log_latest_nonce);
    integer("log.content_rev", log_content_rev);
    integer("log.follow_target", follow_target);
    host.publish_f64(host.userdata, "log.slide_dy", slide_offset_);
    // Step 1 perf fix (kept): the strip/tile key strings are built exactly once (function-local
    // static, thread-safe init -- Sample() is always serialized on one host thread per the
    // module's own threading contract).
    static const auto kLogStripKeys = [] {
      std::array<std::string, kMaxPublishedStrips> out;
      for (size_t i = 0; i < kMaxPublishedStrips; ++i)
        out[i] = "log.strip." + std::to_string(i) + ".image";
      return out;
    }();
    static const std::string kEmptyText;
    for (size_t i = 0; i < kMaxPublishedStrips; ++i) {
      if (int(i) < strip_count)
        text(kLogStripKeys[i],
             "module:yoru:log/" + std::to_string(layout_rev) + "/" + std::to_string(i));
      else
        text(kLogStripKeys[i], kEmptyText);
    }
    integer("cg.ready", s.cg_ready);
    integer("cg.entry_count", int64_t(kCgGalleryCount));
    static const auto kCgTileKeys = [] {
      std::array<std::string, kCgGalleryCount> out;
      for (size_t i = 0; i < kCgGalleryCount; ++i)
        out[i] = "cg.tile." + std::to_string(i) + ".image";
      return out;
    }();
    if (!cg_flags_initialized || cg_ready_published != s.cg_ready ||
        cg_flags_published != s.cg_flags) {
      cg_flags_initialized = true;
      cg_ready_published = s.cg_ready;
      cg_flags_published = s.cg_flags;
      for (size_t i = 0; i < kCgGalleryCount; ++i) {
        const bool unlocked = s.cg_ready && s.cg_flags[i] != 0;
        text(kCgTileKeys[i],
             "module:yoru:cg/" + std::to_string(i) + "/" + (unlocked ? "1" : "0"));
      }
    }
    integer("ui.page", state.page);
    integer("ui.full_cg", state.full_cg);
    for (int i = 0; i < 3; ++i)
      integer("ui.page." + std::to_string(i), state.page == i);
    integer("ui.footer_visible", !state.full_cg);
    integer("ui.cg_grid", state.page == 2 && !state.full_cg);
    integer("ui.cg_selected", state.cg_selected);
    integer("ui.manual_page", state.manual_page);
    integer("ui.manual_count", 2);
    integer("ui.log_font_size", state.log_font_size);
    text("ui.image", "module:yoru:view/" + std::to_string(revision));
    // Owner item 3: one chrome frame per tab, kept current regardless of which tab is selected
    // (see tab_frames's own comment) -- `full_cg` is always forced false here, since the "cg-full"
    // illustration is a separate manifest page served by the `ui.image`/`frames` stream above,
    // never by this one.
    //
    // Perf (found live while verifying item 5): TabChromeChanged never reads `.page`/`.full_cg`
    // (see its own comment -- every tab's chrome depends only on shared scalar/Snapshot fields),
    // so it is checked directly against `state` -- no copy -- and the (heap-allocating, ~63-row
    // Snapshot) ViewState copy below only happens on an actual change, not every idle tick. The
    // unconditional `ViewState tv = state;` this loop originally did (copying the whole retained
    // history 3x every Sample(), regardless of whether anything changed) was itself large enough
    // to keep the idle steady-state average above the item 5 perf target; this is what fixed it.
    for (int i = 0; i < 3; ++i) {
      if (!tab_initialized[size_t(i)] || TabChromeChanged(i, tab_last[size_t(i)], state)) {
        tab_initialized[size_t(i)] = true;
        ViewState tv = state;
        tv.page = i;
        tv.full_cg = false;
        tab_last[size_t(i)] = std::move(tv);
        auto &deque = tab_frames[size_t(i)];
        deque.emplace_back(++tab_revision[size_t(i)], tab_last[size_t(i)]);
        while (deque.size() > 2)
          deque.pop_front();
      }
      text("ui.image." + std::to_string(i),
           "module:yoru:tab/" + std::to_string(i) + "/" + std::to_string(tab_revision[size_t(i)]));
    }
  }
  bool Action(std::string_view action, int64_t arg) {
    std::lock_guard lock{mutex};
    if (action == "page") {
      if (arg < 0 || arg > 2)
        return false;
      state.page = int(arg);
      state.full_cg = false;
    } else if (action == "manual_scroll") {
      if (state.page != 1 || (arg != -1 && arg != 1))
        return false;
      const int next = std::clamp(state.manual_page + int(arg), 0, 1);
      if (next == state.manual_page)
        return false; // Already at the first/last Manual page: refused.
      state.manual_page = next;
    } else if (action == "log_zoom") {
      // Owner item 4 (2026-10-16 round 2): text size is back on the Log page's own header too
      // (LogTextSizeRow, yoru_view.cpp), ALONGSIDE the Option page's copy -- both read/write the
      // same state.log_font_size, so the two stay in sync automatically; the gate below widens
      // from "Option only" to "Log or Option" to match.
      if ((state.page != 0 && state.page != 1) || (arg != -2 && arg != 2))
        return false;
      const int next = std::clamp(state.log_font_size + int(arg), 28, 52);
      if (next == state.log_font_size)
        return false; // Already at the smallest/largest size: refused.
      state.log_font_size = next;
    } else if (action == "log_zoom_reset") {
      if (state.page != 0 && state.page != 1)
        return false;
      if (state.log_font_size == 36)
        return false; // Already at the default: refused.
      state.log_font_size = 36;
    } else if (action == "log_latest") {
      // Owner item 3: jump the native scroll to the top. Always succeeds -- unlike every other
      // refusal in this function, this one genuinely cannot be checked: on_action has no host
      // parameter (dsmod_module_extensions.h), so the module cannot read the ScrollRegion's own
      // current offset ("@scroll:<id>", mod_input.cpp's PublishScroll) to tell whether it is
      // already at the top. Refusing only when there is nothing to scroll to at all:
      if (!PastCount(state.snapshot))
        return false;
      ++log_latest_nonce;
    } else if (action == "log_oldest") {
      // Same "cannot detect already-there" limitation as log_latest above.
      if (!PastCount(state.snapshot))
        return false;
      log_oldest_pulse = true;
    } else if (action == "cg_tile") {
      // Owner design: the scroll region now holds all 53 tiles natively, so `arg` is the
      // absolute gallery index directly (no more visible-slot + cg_offset combination).
      if (state.full_cg || arg < 0 || arg >= int64_t(kCgGalleryCount))
        return false;
      if (!state.snapshot.cg_ready || !state.snapshot.cg_flags[size_t(arg)])
        return false;
      state.cg_selected = int(arg);
      state.full_cg = true;
    } else if (action == "cg_back") {
      if (!state.full_cg)
        return false;
      state.full_cg = false;
    } else {
      return false; // Includes menu/voice operations: never inject blind guest
                    // inputs.
    }
    Retain();
    return true;
  }
  bool LoadImage(std::string_view key, void *receiver,
                 EdenDsmodImageSink sink) {
    constexpr std::string_view view_prefix = "module:yoru:view/";
    constexpr std::string_view tab_prefix = "module:yoru:tab/";
    constexpr std::string_view log_prefix = "module:yoru:log/";
    constexpr std::string_view cg_prefix = "module:yoru:cg/";
    constexpr std::string_view bar_prefix = "module:yoru:bar/";
    const auto parse_u64 = [](std::string_view s, uint64_t &out) {
      if (s.empty() || s.size() > 18)
        return false;
      const auto parsed = std::from_chars(s.data(), s.data() + s.size(), out);
      return parsed.ec == std::errc{} && parsed.ptr == s.data() + s.size();
    };
    if (!sink)
      return false;
    if (key.starts_with(view_prefix)) {
      uint64_t wanted{};
      if (!parse_u64(key.substr(view_prefix.size()), wanted))
        return false;
      ViewState copied;
      {
        std::lock_guard lock{mutex};
        auto at = std::find_if(frames.begin(), frames.end(),
                               [&](const auto &f) { return f.first == wanted; });
        if (at == frames.end())
          return false;
        copied = at->second;
      }
      Image image;
      if (!view.Render(copied, image) || image.w != 1240 || image.h != 1080 ||
          image.rgba.size() != size_t(image.w) * image.h * 4)
        return false;
      sink(receiver, image.w, image.h, image.rgba.data(), image.rgba.size());
      first_painted.store(true, std::memory_order_relaxed);
      return true;
    }
    if (key.starts_with(tab_prefix)) {
      // Owner item 3: one of the three tab chrome streams (tab_frames's own comment) --
      // "module:yoru:tab/<0..2>/<rev>", rendered through the SAME View::Render() the "cg-full"
      // stream above uses, just against a ViewState whose `page` was forced when it was cached.
      const auto suffix = key.substr(tab_prefix.size());
      const auto slash = suffix.find('/');
      uint64_t idx{}, rev{};
      if (slash == std::string_view::npos || !parse_u64(suffix.substr(0, slash), idx) ||
          !parse_u64(suffix.substr(slash + 1), rev) || idx >= 3)
        return false;
      ViewState copied;
      {
        std::lock_guard lock{mutex};
        auto &deque = tab_frames[size_t(idx)];
        auto at = std::find_if(deque.begin(), deque.end(),
                               [&](const auto &f) { return f.first == rev; });
        if (at == deque.end())
          return false; // A superseded tab revision: the host will re-request the fresh key.
        copied = at->second;
      }
      Image image;
      if (!view.Render(copied, image) || image.w != 1240 || image.h != 1080 ||
          image.rgba.size() != size_t(image.w) * image.h * 4)
        return false;
      sink(receiver, image.w, image.h, image.rgba.data(), image.rgba.size());
      first_painted.store(true, std::memory_order_relaxed);
      return true;
    }
    if (key.starts_with(log_prefix)) {
      const auto suffix = key.substr(log_prefix.size());
      const auto slash = suffix.find('/');
      uint64_t rev{}, strip{};
      if (slash == std::string_view::npos ||
          !parse_u64(suffix.substr(0, slash), rev) ||
          !parse_u64(suffix.substr(slash + 1), strip) ||
          strip >= kMaxPublishedStrips)
        return false;
      ViewState copied;
      {
        std::lock_guard lock{mutex};
        auto at = std::find_if(layout_frames.begin(), layout_frames.end(),
                               [&](const auto &f) { return f.first == int(rev); });
        if (at == layout_frames.end())
          return false; // A superseded layout_rev: the host will re-request the fresh key.
        copied = at->second;
      }
      Image image;
      if (!view.RenderLogStrip(copied, int(strip), image) ||
          image.w != uint32_t(kLogStripW) || image.h != uint32_t(kLogStripH) ||
          image.rgba.size() != size_t(image.w) * image.h * 4)
        return false;
      sink(receiver, image.w, image.h, image.rgba.data(), image.rgba.size());
      if (strip == 0) {
        // Owner item 2: records the newest layout_rev whose strip 0 (always covering at least the
        // top of the newest entry) has actually decoded -- Sample()'s own slide-start gate reads
        // this so the animation never starts before the new content is there to show. A monotonic
        // high-water mark: an out-of-order decode of an older, already-superseded rev must not
        // regress it.
        int prev = top_strip_loaded_rev_.load(std::memory_order_relaxed);
        while (int(rev) > prev &&
               !top_strip_loaded_rev_.compare_exchange_weak(prev, int(rev),
                                                            std::memory_order_relaxed)) {
        }
      }
      return true;
    }
    if (key.starts_with(cg_prefix)) {
      const auto suffix = key.substr(cg_prefix.size());
      const auto slash = suffix.find('/');
      uint64_t index{}, unlocked{};
      if (slash == std::string_view::npos ||
          !parse_u64(suffix.substr(0, slash), index) ||
          !parse_u64(suffix.substr(slash + 1), unlocked) ||
          index >= kCgGalleryCount || unlocked > 1)
        return false;
      Image image;
      if (!view.RenderCgTile(int(index), unlocked != 0, image) ||
          image.w != uint32_t(kCgTileW) || image.h != uint32_t(kCgTileH) ||
          image.rgba.size() != size_t(image.w) * image.h * 4)
        return false;
      sink(receiver, image.w, image.h, image.rgba.data(), image.rgba.size());
      return true;
    }
    if (key.starts_with(bar_prefix)) {
      const auto name = key.substr(bar_prefix.size());
      BarArt which;
      if (name == "log_track")
        which = BarArt::LogTrack;
      else if (name == "log_thumb")
        which = BarArt::LogThumb;
      else if (name == "cg_track")
        which = BarArt::CgTrack;
      else if (name == "cg_thumb")
        which = BarArt::CgThumb;
      else if (name == "oldest")
        which = BarArt::OldestLabel;
      else
        return false;
      Image image;
      if (!view.RenderBarArt(which, image) ||
          image.rgba.size() != size_t(image.w) * image.h * 4)
        return false;
      sink(receiver, image.w, image.h, image.rgba.data(), image.rgba.size());
      return true;
    }
    return false;
  }
};
EdenDsmodBool Supports(const char *build) {
  return build && SupportsBuild(build);
}
void *Create(const EdenDsmodHostApi *host, const char *) {
  try {
    if (!dsmod_sdk::HostAbiMatches(host) || host->title_id != TitleId ||
        !std::equal(BuildBytes.begin(), BuildBytes.end(), host->build_id) ||
        !host->read_memory || !host->is_mapped || !host->publish_i64 ||
        !host->publish_text || !host->publish_f64)
      return nullptr;
    return new Module(*host);
  } catch (...) {
    return nullptr;
  }
}
void Destroy(void *instance) { delete static_cast<Module *>(instance); }
void Sample(void *instance, const EdenDsmodHostApi *host) {
  if (instance && host)
    static_cast<Module *>(instance)->Sample(*host);
}
void Tick(void *, const EdenDsmodHostApi *) {}
void Configure(void *, const EdenDsmodHostExtensions *) {}
EdenDsmodBool Action(void *instance, const char *action, int64_t arg) {
  try {
    return instance && action &&
           static_cast<Module *>(instance)->Action(action, arg);
  } catch (...) {
    return false;
  }
}
EdenDsmodBool LoadImage(void *instance, const EdenDsmodHostApi *,
                        const char *key, void *receiver,
                        EdenDsmodImageSink sink) {
  try {
    return instance && key &&
           static_cast<Module *>(instance)->LoadImage(key, receiver, sink);
  } catch (...) {
    return false;
  }
}
const EdenDsmodModuleApi Api{EDEN_DSMOD_MODULE_ABI_VERSION,
                             sizeof(EdenDsmodModuleApi),
                             0,
                             EDEN_DSMOD_MODULE_ABI_HASH,
                             TitleId,
                             "Yoru Tomosu native Log and CG companion",
                             EDEN_DSMOD_CAP_EXTENSIONS |
                                 EDEN_DSMOD_CAP_ROMFS_READ |
                                 EDEN_DSMOD_CAP_NO_TICK_WHEN_HIDDEN,
                             Supports,
                             Create,
                             Destroy,
                             Sample,
                             Tick};
const EdenDsmodModuleExtensions Extensions{EDEN_DSMOD_EXT_VERSION,
                                           sizeof(EdenDsmodModuleExtensions),
                                           EDEN_DSMOD_EXT_HASH,
                                           Configure,
                                           Action,
                                           LoadImage};
} // namespace
} // namespace yoru
DSMOD_SDK_EXPORT_MODULE(yoru::Api)
DSMOD_SDK_EXPORT_EXTENSIONS(yoru::Extensions)
