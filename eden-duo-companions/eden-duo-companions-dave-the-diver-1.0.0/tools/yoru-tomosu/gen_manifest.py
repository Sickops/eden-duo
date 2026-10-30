#!/usr/bin/env python3
"""Accepted A: independent Log, native controls manual and unlocked CG viewer.

2026-10-16 revision (owner feedback on the 2026-10-15 build): native drag-to-scroll (the
runtime's ScrollRegion, mod_types_page.h) replaces the pan_zoom raster Log body and its
log_scroll/log_pan/log_seek* step actions -- every one-finger swipe on that body was being
consumed by the host's own pan/zoom gesture (device log: "aux gesture on 'yoru_log_body' ended:
zoom 1.00 pan (...)"), never reaching the module. The Log is now a repeat-template column of
small, individually cached module strip images inside a ScrollRegion (smooth drag + fling,
native scrollbar); the CG grid is the same idea with one module image per tile. Latest/Oldest
moved to the scrollbar's own ends (reset_bind / follow_bind). The "Manual" tab is renamed
"Option" and gained text size / scroll speed / follow-newest controls above the (now slightly
smaller) controller manual. Pinch-to-zoom on the Log was tried and dropped: mod_input.cpp's
CallbackAt() claims a single-finger gesture for ANY widget that declares on_pan OR on_pinch
*before* ScrollRegionAt() is ever consulted (gesture_target/scroll_target logic, mod_input.cpp
~line 530-545) -- a pinch-capable overlay on the Log body would silently swallow every
one-finger drag the same way pan_zoom did. Text size now lives on the Option page instead.
Each of these decisions was verified against the runtime's gesture code (mod_input.cpp) on a
live desktop run before it was kept.

2026-10-16 round 2 (further owner feedback, 0.1.0-dev.8): (2) a new past row sliding in from
above the viewport with every older row sliding down by its height, when the reader is at the
top; (3) a fade cross-fade when switching Log/Option/CG tabs; (4) the SAME text-size controls
duplicated onto the Log page's own header (shared state with the Option page's copy); (5) a
BuildLogLayout re-measurement cache (yoru_module.cpp/yoru_anim.h, not this file). Item 2 binds
each Log strip repeat template's "y_bind" to a module-published "log.slide_dy_gated" derived
value (see its own comment below) -- mod_ui_expand.cpp's ExpandTemplateInto resolves y_bind
against the template's base rect for every cell, so one float moves the whole scrolled column
uniformly, and VisibleElementRange (mod_ui_scroll.cpp) already folds the same y_bind into its
culling math ("y_bind moves a template as a whole; take it into the visibility maths too"), so
nothing extra is needed for clipping -- the repeat template's existing `scroll` membership
already gives it a scroll_clip (mod_ui_expand.cpp ~line 284-287), clipped at draw time
(mod_ui.cpp ~line 1243-1254) regardless of any y_bind offset. Item 3 splits the single
"companion" manifest page into three pages (log/option/cg), each with its OWN module image key
(ui.image.0/1/2, yoru_module.cpp's tab_frames) kept current regardless of which tab is visible --
required because mod_pages.cpp's DrivePageTransition renders the "to" page exactly once right
when a transition starts, so a tab that was never the current one must still have a fresh,
correct chrome frame waiting for it. Tab taps still only call the module's existing "page"
action (unchanged); a page_bind per tab (on the module-published "ui.page") drives the actual
manifest page switch with "transition": "fade", the same ParsePageBindTarget/DrivePageBinds
machinery the waiting<->companion and cg-full binds already used.

Geometry/keys here MUST be kept in sync by hand with native/modules/yoru_view.h's constants
(kLogStripW/H, kCgTileW/H, kCgGalleryCount) and yoru_module.cpp's kMaxPublishedStrips -- there is
no shared build-time source of truth between the C++ module and this generator.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "packages/YoruTomosu/dualscreen"

# Kept in sync with native/modules/yoru_view.h / yoru_module.cpp (see module header comment).
LOG_STRIP_W, LOG_STRIP_H = 1240, 128
MAX_STRIPS = 160  # yoru_module.cpp's kMaxPublishedStrips
CG_TILE_W, CG_TILE_H = 520, 329
CG_COLS, CG_COUNT = 2, 53
CG_DX, CG_ROW_DY = 565, 414  # tile grid pitch (matches the pre-2026-10-16 absolute positions)

# Log/CG ScrollRegion viewport. The Log's is a little shorter than the full 100..960 content band
# so "Latest"/"Oldest" labels fit directly above/below the track without overlapping the heading
# or the footer (owner item 3 round 1: "Latest and Oldest should be at the top and bottom of the
# scroll bar"); the CG grid uses the full band since it has no such labels.
LOG_RECT = [0, 144, 1240, 816]
CG_RECT = [0, 105, 1240, 835]
BAR_W = 34


def main():
    actions = {}

    def module(name, action, argument=0):
        actions[name] = {"kind": "module", "action": action, "argument": argument}

    def flag(name, flag_name, value):
        actions[name] = {"kind": "flag", "flag": flag_name, "value": value}

    for i in range(3):
        module(f"page_{i}", "page", i)
    module("log_smaller", "log_zoom", -2)
    module("log_larger", "log_zoom", 2)
    module("log_font_reset", "log_zoom_reset", 0)
    module("log_latest", "log_latest", 0)
    module("log_oldest", "log_oldest", 0)
    module("manual_previous", "manual_scroll", -1)
    module("manual_next", "manual_scroll", 1)
    module("cg_back", "cg_back", 0)
    for i in range(CG_COUNT):
        module(f"cg_tile_{i}", "cg_tile", i)
    # Scroll speed / follow newest (owner item 4, round 1): manifest "flag" actions, not module
    # actions -- the speed/follow BEHAVIOUR lives entirely in which of the 3 duplicated
    # ScrollRegions below is the live one (their own friction/fling), never in module code
    # (mod_actions.cpp's ActionKind::Flag, verified at mod_actions.cpp:754-776). Both flags are
    # "persist_flags" (mod_persist.h), so the choice survives a session restart; see the manifest
    # dict below.
    flag("speed_slow", "opt.scroll_speed", 0)
    flag("speed_normal", "opt.scroll_speed", 1)
    flag("speed_fast", "opt.scroll_speed", 2)
    flag("follow_on", "opt.follow_newest", True)
    flag("follow_off", "opt.follow_newest", False)
    # No more "log_view_reset"/pan_zoom hold gesture: the Log body is no longer a pan_zoom
    # widget (owner item 1), so there is no view to glide home -- removed along with the widget
    # it used to target ("yoru_log_body").

    def widgets_for(page_id):
        return pages_widgets.setdefault(page_id, [])

    pages_widgets = {}

    def image(page_id, rect, **extra):
        widgets_for(page_id).append({
            "type": "image", "rect": rect, "src_bind": "ui.image", "color": "#FFFFFFFF",
            "need_bind": "module_ready", "keep_max": 0, **extra,
        })

    def touch(page_id, rect, **extra):
        widgets_for(page_id).append({
            "type": "rect", "rect": rect, "color": "#00000000", "bg": "#00000000", "frame": 0,
            **extra,
        })

    def label(page_id, rect, **extra):
        widgets_for(page_id).append({"type": "label", "rect": rect, **extra})

    def add_footer(page_id):
        for i in range(3):
            touch(page_id, [i * 413, 960, 414, 120], need_bind="ui.footer_visible",
                  on_tap=f"page_{i}", haptic="click")

    # ---- "waiting" page (manifest-native; cannot rely on module art) -- unchanged ------------
    image("waiting", [0, 0, 2, 2], need_bind="state.compatible")
    touch("waiting", [370, 380, 500, 260], bg="#20FFCA50", color="#60FFCA50", frame=3, pulse=True,
          need_bind="!module_error")
    label("waiting", [620, 410, 1000, 90], text="Yoru, Tomosu", text_scale=7, align="center",
          color="#FFEEE9DF", need_bind="!module_error")
    label("waiting", [620, 520, 1000, 60], text="Waiting for the game...", text_scale=3,
          align="center", color="#FFB3AD9D", need_bind="!module_error")
    label("waiting", [620, 440, 1000, 80], text="Companion unavailable", text_scale=5,
          align="center", color="#FFEEE9DF", need_bind="module_error")
    label("waiting", [620, 540, 1000, 200], bind_text="module_error_message", text_scale=2,
          align="center", color="#FFB3AD9D", need_bind="module_error")

    # ---- "log" / "option" / "cg" pages (owner item 3, round 2: split from the old single
    # "companion" page so each tab can have its own module image and fade in/out independently --
    # see this file's own module docstring for why). Each gets its own full-canvas chrome image
    # (ui.image.0/1/2 -- yoru_module.cpp's tab_frames, kept current regardless of which tab is
    # showing) and its own footer (tapping a tab still only calls the "page_i" module action,
    # which publishes "ui.page"; a page_bind further down drives the actual fade).
    image("log", [0, 0, 1240, 1080], src_bind="ui.image.0")
    add_footer("log")
    image("option", [0, 0, 1240, 1080], src_bind="ui.image.1")
    add_footer("option")
    image("cg", [0, 0, 1240, 1080], src_bind="ui.image.2")
    add_footer("cg")

    # ---- Log: native-scrolled strip column (owner items 1-2, round 1) ------------------------
    # 3 duplicated ScrollRegions, one per scroll-speed option (owner item 4 round 1: "Scroll
    # speed: Slow / Normal / Fast"), differing only in friction/fling -- the runtime parses
    # friction/fling as fixed literals (mod_manifest.cpp's ParseScrollRegion, no bind support), so
    # switching speed at runtime means switching which region is live, gated by one derived
    # boolean per variant (ANDed with "is the Log tab even open", so a region never answers for a
    # touch on another tab's widgets).
    SPEEDS = [("slow", 0, 0.03, True), ("normal", 1, 0.135, True), ("fast", 2, 0.45, True)]
    log_scrolls = []
    log_live = {}
    for name, value, friction, fling in SPEEDS:
        live = f"log.live_{name}"
        log_live[name] = live
        log_scrolls.append({
            "id": f"log_scroll_{name}", "rect": LOG_RECT, "row_h": LOG_STRIP_H, "cols": 1,
            "show_bind": live, "reset_bind": "opt.log_reset_combined",
            "follow_bind": "log.follow_target",
            "fling": fling, "friction": friction,
            "bar_src": "module:yoru:bar/log_thumb", "bar_track_src": "module:yoru:bar/log_track",
            "bar": "#FFFFCA50", "bar_track": "#30EEE9DF", "bar_w": BAR_W,
        })
        row_rect = [LOG_RECT[0], LOG_RECT[1], LOG_RECT[2], LOG_STRIP_H]
        # Owner item 2 (round 2), "slide in from top ... slide down": y_bind adds the eased
        # "log.slide_dy_gated" derived value (see the derived list below) to this template's base
        # y for every cell uniformly -- the whole strip column slides as one; mod_ui_expand.cpp's
        # VisibleElementRange already folds y_bind into its own culling, and the scroll_clip this
        # template already gets from `scroll` (being inside a ScrollRegion) already clips the
        # drawn pixels to LOG_RECT, so the viewport-clipping half of the owner's instructions needs
        # no separate change here.
        image("log", row_rect, src_bind="log.strip.{i}.image", repeat=MAX_STRIPS,
              repeat_bind="log.strip_count", repeat_dy=LOG_STRIP_H, scroll=f"log_scroll_{name}",
              need_bind=live, id="yoru_log_strip_{i}",
              y_bind="log.slide_dy_gated", y_scale=1.0)
    # "Latest" above the track, "Oldest" below it (owner item 3, round 1), compact native-font
    # labels in the right margin the ScrollRegion's own rect leaves clear (LOG_RECT starts 32px
    # below the heading and ends 40px above the footer -- see LOG_RECT's own comment). Repeated
    # taps always work even if the target didn't move: "Latest" bumps a monotonic nonce
    # (reset_bind fires on ANY change, mod_input.cpp:1012-1028, combined below with "Follow
    # newest"'s own automatic trigger into "opt.log_reset_combined"); "Oldest" pulses the follow
    # target to -1 then the real index for one sample (follow_bind only re-fires on a value CHANGE,
    # mod_input.cpp:1043) -- both verified in yoru_module.cpp's own comments on
    # log_latest_nonce/log_oldest_pulse. The labels themselves are drawn by the module (LogChrome)
    # in the patched game font on the native topmenue_focus plate, so only generous transparent
    # touch areas live here.
    touch("log", [990, 92, 250, 52], hide_bind="log.past_ready",
          keep_min=1, on_tap="log_latest", haptic="light")
    # "Oldest" overlay (owner 2026-10-16: let the history fill down to the footer, the label in
    # front of it): a static module image left of the scrollbar, drawn after the strips.
    widgets_for("log").append({
        "type": "image", "rect": [972, 908, 226, 46], "src": "module:yoru:bar/oldest",
        "color": "#FFFFFFFF", "need_bind": "module_ready", "hide_bind": "log.past_ready",
        "keep_min": 1})
    touch("log", [962, 900, 246, 60], hide_bind="log.past_ready",
          keep_min=1, on_tap="log_oldest", haptic="light")
    # Owner item 4 (round 2), "In Log top right, add text size with - and + buttons": the SAME
    # module actions the Option page's own text-size row already uses (log_smaller/log_larger/
    # log_font_reset) -- state.log_font_size is shared, so both pages' controls and both pages'
    # displayed number stay in sync automatically; refusal at the 28/52 clamp limits plays the
    # existing "refused" haptic the same way the Option page's copy already does. See
    # yoru_view.cpp's LogTextSizeRow for the drawn plate positions these touch rects cover (hand
    # -synced, generous margins, same convention the Option page's own rects already use).
    # Hardening (2026-10-17, LEGIBILITY.md dense-list floor 88px): the drawn plates sit at y=18
    # h=76, but nothing else occupies y=0..18 above them (Latest's own rect starts at y=92, well
    # below), so the touch rects extend up to the canvas edge (y=0) for a 94px-tall hit area --
    # a pure hit-box change, no visual change.
    touch("log", [942, 0, 92, 94], on_tap="log_smaller")
    touch("log", [1122, 0, 92, 94], on_tap="log_larger")
    touch("log", [1034, 0, 88, 94], on_hold="log_font_reset", hold_ms=650)

    # ---- CG grid: native-scrolled 2-column tile grid (owner item C, round 1) -------------------
    cg_scrolls = []
    for name, value, friction, fling in SPEEDS:
        live = f"cg.live_{name}"
        cg_scrolls.append({
            "id": f"cg_scroll_{name}", "rect": CG_RECT, "row_h": CG_ROW_DY, "cols": CG_COLS,
            "pad": 20, "show_bind": live,
            "fling": fling, "friction": friction,
            "bar_src": "module:yoru:bar/cg_thumb", "bar_track_src": "module:yoru:bar/cg_track",
            "bar": "#FFFFCA50", "bar_track": "#30EEE9DF", "bar_w": BAR_W,
        })
        image("cg", [55, 125, CG_TILE_W, CG_TILE_H], src_bind="cg.tile.{i}.image",
              repeat=CG_COUNT, repeat_cols=CG_COLS, repeat_dx=CG_DX, repeat_row_dy=CG_ROW_DY,
              scroll=f"cg_scroll_{name}", need_bind=live, on_tap="cg_tile_{i}", haptic="confirm",
              id="yoru_cg_tile_{i}")
    # "Gallery opens once a story is loaded" is drawn by View::Impl::GridChrome as part of the
    # same full-canvas "ui.image.2" composite above -- no separate manifest label needed (the CG
    # tile repeat templates above are need_bind-gated on cg.live_*, which is already 0 whenever
    # ui.cg_grid is 0, so nothing scrolled is drawn over that message either).

    # ---- Option page (owner item 4, round 1: renamed from "Manual", options on top) -----------
    # Text size lives in the Log header only (owner 2026-10-16).
    # Scroll speed / Follow newest: a native gold outline highlights the selected choice. The
    # module draws every choice identically (yoru_view.cpp's OptionRow); it was found, live, that
    # the module cannot reliably read a manifest "flag" back (RunGameModule runs before
    # PublishFlags every tick, mod_runtime.cpp:667-669 -- see yoru_module.cpp's own comment), so
    # module-rendered highlighting of the SELECTED choice was permanently stale. A native widget,
    # gated on a pure "@flag:..." derived point (evaluated well after PublishFlags), is instead
    # always correct the same tick the flag changes.
    def option_choice(rect, act, selected_point):
        x, y, w, h = rect
        touch("option", [x - 4, y - 4, w + 8, h + 8], color="#FFFFCA50", bg="#00000000",
              frame=3, hide_bind=selected_point, keep_min=1)
        touch("option", rect, on_tap=act)
    # Scroll speed: 3-way (Slow / Normal / Fast). Hardening (2026-10-17, LEGIBILITY.md dense-list
    # floor 88px): height raised 65 -> 88 (row ends at 230, the Follow-newest row below still
    # starts at 242 -- a 12px gap, LEGIBILITY.md's own minimum gap); width was already >= 112.
    for rect, act, sel in (([414, 142, 140, 88], "speed_slow", "opt.sel.speed_slow"),
                           ([552, 142, 170, 88], "speed_normal", "opt.sel.speed_normal"),
                           ([720, 142, 140, 88], "speed_fast", "opt.sel.speed_fast")):
        option_choice(rect, act, sel)
    # Follow newest: 2-way (On / Off). Same 65 -> 88 height raise; next control below (the
    # controller-manual touch areas) starts at y=850, far clear.
    for rect, act, sel in (([414, 242, 140, 88], "follow_on", "opt.sel.follow_on"),
                           ([552, 242, 140, 88], "follow_off", "opt.sel.follow_off")):
        option_choice(rect, act, sel)
    # Controller manual (Joy-Con / Pro Controller), scaled to fit under the new controls.
    touch("option", [150, 850, 480, 88], on_tap="manual_previous")
    touch("option", [670, 850, 480, 88], on_tap="manual_next")
    touch("option", [28, 378, 1184, 460], on_swipe_left="manual_next",
          on_swipe_right="manual_previous")

    # ---- "cg-full" page (module-rendered full illustration) ----------------------------------
    image("cg-full", [0, 0, 1240, 1080])
    touch("cg-full", [0, 0, 1240, 1080], on_tap="cg_back", haptic="light")

    # ---- page-level transitions, driven by published state -----------------------------------
    page_binds = [
        {"point": "state.screen", "equals": 0,
         "when_equal": {"page": "waiting", "transition": "fade", "duration_ms": 260,
                        "easing": "ease_in_out"},
         # Lands on "log" (not whatever tab was last selected -- there is no way to change tabs
         # while on "waiting", so this only ever fires with ui.page already at its default, 0).
         "when_not_equal": {"page": "log", "transition": "fade", "duration_ms": 260,
                            "easing": "ease_in_out"}},
        {"point": "ui.full_cg", "equals": 1,
         "when_equal": {"page": "cg-full", "transition": "fade", "duration_ms": 220,
                        "easing": "ease_in_out"},
         # cg-full can only be entered from the CG tab, so returning always goes back to "cg".
         "when_not_equal": {"page": "cg", "transition": "fade", "duration_ms": 180,
                            "easing": "ease_in_out"}},
        {"point": "module_error", "equals": 1,
         "when_equal": {"page": "waiting", "transition": "fade", "duration_ms": 220,
                        "easing": "ease_in_out"}},
        # Owner item 3 (round 2), "fade" tab switch: one one-directional bind per tab -- "ui.page"
        # is the module's own published current-tab index (0 Log / 1 Option / 2 CG, unchanged
        # from round 1). Each bind fires only its own "when_equal" (the edge INTO that tab);
        # "when_not_equal" is left out on purpose (ParsePageBindTarget/DrivePageBinds,
        # mod_manifest.cpp/mod_pages.cpp: an absent target is one-directional, not a warning --
        # the OTHER tabs' own binds cover every other edge). ~150-200ms owner ask; 180ms here,
        # matching the existing cg-full "when_not_equal" duration.
        {"point": "ui.page", "equals": 0,
         "when_equal": {"page": "log", "transition": "fade", "duration_ms": 180,
                        "easing": "ease_in_out"}},
        {"point": "ui.page", "equals": 1,
         "when_equal": {"page": "option", "transition": "fade", "duration_ms": 180,
                        "easing": "ease_in_out"}},
        {"point": "ui.page", "equals": 2,
         "when_equal": {"page": "cg", "transition": "fade", "duration_ms": 180,
                        "easing": "ease_in_out"}},
    ]

    pages = [{"id": pid, "name": pid.capitalize(), "widgets": widgets,
              **({"scrolls": log_scrolls} if pid == "log" else {}),
              **({"scrolls": cg_scrolls} if pid == "cg" else {})}
             for pid, widgets in pages_widgets.items()]

    # Derived booleans (mod_expr.h's expression language, runtime 17): one per (tab, speed)
    # combination, ANDing "is this tab even open" with "is this the selected speed" -- used as
    # BOTH each ScrollRegion's show_bind (which region answers drag/fling/scrollbar touches) and
    # its matching repeat template's need_bind (which rows actually draw), so exactly one of the 3
    # Log variants and one of the 3 CG variants is ever live/visible at a time. "ui.page.0" /
    # "ui.cg_grid" and "@flag:opt.scroll_speed" are both already published every tick (the first
    # by the module, the second by the runtime's own PublishFlags) -- confirmed readable by the
    # expr compiler's "@flag:" name form (mod_expr.h's grammar comment).
    derived = []
    for name, value, *_ in SPEEDS:
        derived.append({"name": log_live[name],
                        "expr": f"(ui.page.0 != 0) && (@flag:opt.scroll_speed == {value})"})
        derived.append({"name": f"cg.live_{name}",
                        "expr": f"(ui.cg_grid != 0) && (@flag:opt.scroll_speed == {value})"})
        # Option page selection highlight (see option_choice's own comment): pure flag reads,
        # with no page/tab gate -- these only drive a native widget's hide_bind, always correct
        # the same tick the flag changes (EvaluateDerived runs after PublishFlags).
        derived.append({"name": f"opt.sel.speed_{name}",
                        "expr": f"@flag:opt.scroll_speed == {value}"})
    derived.append({"name": "opt.sel.follow_on", "expr": "@flag:opt.follow_newest != 0"})
    derived.append({"name": "opt.sel.follow_off", "expr": "@flag:opt.follow_newest == 0"})
    # "Follow newest" owner item 4 (round 1, automatic half): log.content_rev (published
    # unconditionally by the module whenever a new past row arrives, see yoru_module.cpp's own
    # comment on why it cannot gate this itself) only actually resets the Log to the top when the
    # flag is On -- summed with the manual "Latest" tap's own nonce so EITHER changing resets
    # (reset_bind fires on any value change, direction included; summing two non-decreasing
    # counters can only ever change when at least one of them does). Toggling the flag itself also
    # changes the sum once (since the ternary's value jumps), so flipping Follow newest either way
    # causes one incidental jump to the top -- acceptable (and arguably expected on the "On"
    # direction); noted in REVIEW.md.
    derived.append({"name": "opt.log_reset_combined",
                    "expr": "log.latest_nonce + ((@flag:opt.follow_newest != 0) * log.content_rev)"})
    # Owner item 2 (round 2), "slide in from top ... slide down": yoru_module.cpp's Sample()
    # always runs the ease-out unconditionally (it cannot read "@flag:opt.follow_newest" reliably,
    # same RunGameModule-before-PublishFlags ordering "opt.log_reset_combined" above already works
    # around) -- this derived value is what actually decides whether the animation is ever VISIBLE,
    # multiplying the module's raw log.slide_dy by the flag (1/0) exactly the way
    # "opt.log_reset_combined" above multiplies log.content_rev by it. Follow newest Off is exactly
    # the owner's "reader scrolled away from the top: don't animate, content just updates" case.
    derived.append({"name": "log.slide_dy_gated",
                    "expr": "log.slide_dy * (@flag:opt.follow_newest != 0)"})

    manifest = {
        "format": 1, "title_id": "0100C2901153C000", "name": "Yoru Tomosu companion",
        # min_runtime 20: originally set for the dev.4 on_pan/on_pinch Log body (beta.7 retarget);
        # that widget is gone as of dev.6 (replaced by ScrollRegion, see this file's module
        # docstring). Hardening (2026-10-17) re-verified the CURRENT feature set's actual floor:
        # persist_flags (runtime 15), derived `expr` (runtime 17), ScrollRegion/haptics (<=
        # runtime 18-19) -- all comfortably under 20, so 20 is not a hard requirement any more, but
        # left as-is (not lowered) since nothing currently needs it raised and the owner has not
        # asked for a lower floor.
        "min_runtime": 20, "canvas_w": 1240, "canvas_h": 1080,
        "background": "#FF17151E", "requires_module": True, "nav": False,
        "anim_hz": 60,
        # Provenance (hardening 2026-10-17): the base (v0) NSP's exefs main NSO build ID -- the
        # same 32 bytes are hand-duplicated as
        # native/modules/yoru_reader.h's BuildId/BuildBytes (which carries this same note). Update
        # all three together if a different base build is ever supported.
        "module": {"abi": 1, "build_ids": [
            "3A573656D869F5786166FCFF376C93AE434C2B38000000000000000000000000"]},
        "_about": "2026-10-16 round 2 (owner feedback): a new Log row slides in from above with "
                  "older rows sliding down (Follow newest only); Log/Option/CG tabs are now "
                  "separate manifest pages that cross-fade on switch; Log's own header gained "
                  "the same text-size controls as the Option page (shared state). Round 1: "
                  "native drag-to-scroll Log/CG (ScrollRegion) replaces the pan_zoom raster "
                  "body; Latest/Oldest at the scrollbar ends. No game/patch assets bundled. "
                  "Independent local interactions only.",
        "_development": "Private development slice. Pinch-to-zoom on the Log was tried and "
                        "dropped (mod_input.cpp's gesture priority gives any on_pinch widget the "
                        "whole single-finger gesture before the ScrollRegion is ever consulted, "
                        "the same bug the owner reported for the old pan_zoom body) -- text size "
                        "lives on both the Log and Option pages instead. Android device "
                        "validation pending.",
        "haptics": {
            "enabled": True, "respect_system": True,
            "tap": "light", "select": "light", "swipe": "light", "hold": "click",
            "refused": "light", "drag": "light",
        },
        "flags": {"opt.scroll_speed": 1, "opt.follow_newest": 1},
        "persist_flags": ["opt.scroll_speed", "opt.follow_newest"],
        "derived": derived,
        "actions": actions, "pages": pages, "page_binds": page_binds,
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(OUT / "manifest.json")


if __name__ == "__main__":
    main()
