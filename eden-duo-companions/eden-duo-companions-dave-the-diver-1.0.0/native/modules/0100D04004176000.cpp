// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Accepted-A Farming Simulator Nintendo Switch Edition presentation.
// Asset-free. The reader supplies verified snapshots and the bounded native
// time-scale input action; no economy, inventory or save transactions.
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "farming_simulator_view.h"
#ifdef FARMING_SIMULATOR_HAS_READER
#include "farming_simulator_reader.h"
#endif
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {
using namespace farming_simulator;
constexpr uint64_t TitleId = UINT64_C(0x0100D04004176000);
constexpr std::string_view Build =
    "7D0651174A65DD848CB60BE3304FD62900000000000000000000000000000000";
EdenDsmodBool Supports(const char *raw) {
  if (!raw)
    return EDEN_DSMOD_FALSE;
  std::string id{raw};
  for (auto &c : id)
    if (c >= 'a' && c <= 'f')
      c -= 32;
  return id == Build ? EDEN_DSMOD_TRUE : EDEN_DSMOD_FALSE;
}
// shop_grid_offset dropped (was slot 10): the catalog grid is the runtime's
// native ScrollRegion now and has no module-tracked scroll position to
// animate/wheel-step at all. price_crop_offset/price_station_offset dropped
// the same way: the Prices matrix is the runtime's native "fs-prices"
// two-axis ScrollRegion now (gen_manifest.py). finance_offset/
// finance_day_offset dropped too: the Finance table is "fs-finance".
// help_topic_offset/help_offset dropped the same way: the Guide topics list
// and article body are "help-topics"/"help-article[-static]". statistic_offset
// dropped too: the Statistics list is "statistic-rows". animal_needs_offset
// dropped too: the needs pane is "animal-needs". map_legend_offset dropped
// too: the legend list is "map-legend". vehicles_offset/vehicle_spec_offset
// dropped too: the Garage list and specs are "garage-list"/"garage-specs".
// shop_category_offset/shop_brand_offset dropped too: the filter list is
// "shop-filters".
using ScrollPosition = std::array<double,1>;
ScrollPosition ScrollValues(const ViewState &s) {
  return {s.shop_spec_offset};
}
void SetScrollValues(ViewState &s,const ScrollPosition &v) {
  s.shop_spec_offset=v[0];
}
bool WheelAction(std::string_view verb) {
  // "shop_grid", "price_columns"/"price_stations", "finance_days"/
  // "finance_scroll", "help_list"/"help_scroll", "statistics_scroll",
  // "animal_scroll", "map_legend_scroll", "vehicle_list", "vehicle_specs"
  // and "shop_filter_scroll" dropped: wheel support for a converted-to-
  // native-ScrollRegion pane is intentionally not kept -- ScrollRegion has
  // no wheel path of its own (UpdateWheel only drives on_swipe_* widgets),
  // and Thor has no wheel to serve anyway.
  constexpr std::string_view verbs[]{"shop_specs"};
  return std::find(std::begin(verbs),std::end(verbs),verb)!=std::end(verbs);
}
struct Module {
  explicit Module(const EdenDsmodHostApi &h, const char *config)
      : view{h}
#ifdef FARMING_SIMULATOR_HAS_READER
        ,
        reader{h, config}
#endif
  {
    (void)config;
  }
  View view; // Asset worker only; no IO/decoding in Sample or OnAction.
#ifdef FARMING_SIMULATOR_HAS_READER
  farming_simulator_reader::Reader reader;
#endif
  std::mutex mutex, render_mutex;
  ViewState state;
  std::deque<std::pair<uint64_t, ViewState>> retained;
  uint64_t last_key{};
  // Reused across ImageFor() calls (under render_mutex, below): same-size
  // canvas every time, so View::Render()'s own resize() stays a no-op past
  // the very first call instead of re-zero-filling a fresh buffer every time.
  Image render_image_;
  uint64_t revision{};
  // Revision-delivery pacing (map-flicker mitigation): the highest revision
  // ImageFor() has actually decoded and handed to the host, and when the
  // current `revision` was minted. A routine (non-forced) mint waits for the
  // previous one to be delivered, so the runtime is never asked to decode a
  // second key before it has caught up on the first -- the root cause of the
  // blank-frame flicker while the player moves (revision minted up to 5x/s,
  // decode is async and draws nothing for a key not yet cached). A ~1s
  // timeout breaks the wait if a revision is never requested at all (e.g. the
  // image widget is hidden), so routine updates cannot deadlock forever.
  uint64_t served_revision{};
  std::chrono::steady_clock::time_point revision_minted_at{};
  // The map's own image, fully decoupled from the frame above: pan/zoom is
  // the runtime's native pan_zoom view state (never a module action), so
  // nothing here is paced by or sensitive to manipulation. This track only
  // mints a new revision when the map's own content changes (layer, world,
  // player position, markers -- MapSnapshotKey), at the same
  // delivery-paced-mint discipline as the frame (served_map_revision /
  // map_revision_minted_at), just on its own clock.
  std::deque<std::pair<uint64_t, ViewState>> retained_map;
  uint64_t last_map_key{};
  Image render_map_image_;
  uint64_t map_revision{};
  uint64_t served_map_revision{};
  std::chrono::steady_clock::time_point map_revision_minted_at{};
  std::chrono::steady_clock::time_point next_map_frame{};
  // The map Reset button's own static picture (game-font "Reset", same look
  // as every other button): rendered once under render_mutex, then served
  // from this cache forever -- the runtime's own module-image cache already
  // keeps the decoded result past that, but RenderResetButton is cheap
  // enough that this is mostly just avoiding a redundant Fill/Draw.
  Image render_reset_image_;
  bool reset_image_rendered_{false};
  // The player map-arrow sprite (View::RenderMapArrow): also a constant key,
  // cached forever the same way -- the runtime's own rotate_bind
  // (fs.ui.map_arrow_heading_deg, published every Sample() below) is what
  // turns it, so the sprite pixels themselves never change.
  Image render_map_arrow_image_;
  bool map_arrow_image_rendered_{false};
  // Garage action row / map selection action row button faces
  // (View::RenderGarageActionRow/RenderMapSelectionButtons): also a small, finite set of
  // constant keys (button pixels depend only on a kind 1..3, plus place/tagged for the map
  // variant -- never on which vehicle/place is selected), cached forever the same way as
  // reset_image_rendered_/map_arrow_image_rendered_ above, just keyed by the full ImageFor key
  // instead of one dedicated member per picture.
  std::map<std::string, Image> action_image_cache_;
  // Local belief about which map marker currently carries the player's one map tag (UX-SPEC
  // C.4's "Tag place"/"Untag place" label swap): the reader has no live read of the game's own
  // g_currentMission.currentMapTargetHotspot (an RE gap, not implemented here), so this instead
  // follows the Tag/Untag game call's own completion -- game_call.serial (+1 per finished call,
  // farming_simulator_view.h) changing with kind=="tag" and detail=="tagged"/"untagged" updates
  // it to/clears it from whichever marker was selected when that call finished. A world change
  // clears it. This can drift from the game's own true state in the one case the task notes:
  // tagging a place the player is already standing on reads back as tagged (the game clears a
  // reached target on its own, out of band from this belief).
  int64_t tagged_marker_belief_{-1};
  uint32_t last_seen_call_serial_{0};
  // Bumped on a meaningful (navigation-like) view change and published as
  // "fs.ui.transition", so the runtime's Image widget can cross-fade into
  // the revision that shows it instead of swapping instantly, the same way
  // it already treats a page change. Not bumped for routine data-only
  // updates (clock, money, player movement, prices).
  int64_t transition{};
  // Bumped whenever the world identity changes, published as
  // "fs.ui.map_identity" (the pan_zoom widgets' view_reset_bind): switching
  // farms snaps both map views back to following the player, same as the
  // old world_identity-change block above used to snap the module's own
  // pan/zoom/follow state.
  int64_t map_identity{};
  // Prices matrix: native "fs-prices" ScrollRegion (gen_manifest.py).
  // price_identity is this pane's own reset_bind (bumped on a world change,
  // same place map_identity is); price_grid_hash/price_revision gate the
  // header/stations/row module images' own cache-busting key suffix on a
  // content change (crop/station data), independent of this page's own
  // fs.ui.frame revision -- see Sample() below.
  int64_t price_identity{};
  size_t price_grid_hash{};
  uint64_t price_revision{};
  // Cached row_src/header_src (ENGINEERING hardening #7): serializing and
  // hashing the whole Prices/Finance/Statistics table to decide
  // price_grid_hash/finance_grid_hash/stat_grid_hash is only worth doing
  // when the data could have changed at all, i.e. when the reader's 1Hz
  // management snapshot actually refreshed (Snapshot::management_revision)
  // -- not on every one of Sample()'s own 60Hz ticks while the page stays
  // open. *_seen_management_revision remembers the revision this cache was
  // last built from; price_row_src/price_header_src below are only
  // rebuilt (and only then re-serialized+hashed) when it has moved, and are
  // otherwise just republished unchanged from here.
  uint64_t price_seen_management_revision{~uint64_t{}};
  std::vector<std::string> price_row_src, price_header_src;
  // Finance table: native "fs-finance" ScrollRegion (gen_manifest.py), same
  // convention as the Prices fields immediately above.
  int64_t finance_identity{};
  size_t finance_grid_hash{};
  uint64_t finance_revision{};
  uint64_t finance_seen_management_revision{~uint64_t{}};
  std::vector<std::string> finance_row_src, finance_labels_src;
  // Statistics list: native "statistic-rows" ScrollRegion (gen_manifest.py),
  // same convention as the Prices/Finance fields above. stat_identity is
  // this pane's own reset_bind.
  int64_t stat_identity{};
  size_t stat_grid_hash{};
  uint64_t stat_seen_management_revision{~uint64_t{}};
  uint64_t stat_revision{};
  std::vector<std::string> stat_row_src;
  // Garage list: native "garage-list" ScrollRegion, same convention as
  // Prices/Finance/Statistics above. garage_identity is this pane's own
  // reset_bind.
  int64_t garage_identity{};
  size_t garage_grid_hash{};
  uint64_t garage_revision{};
  // Animal needs pane: native "animal-needs" ScrollRegion, same hash/
  // revision convention as Prices/Finance/Statistics above (no identity
  // counter here -- see animal_identity_value's own comment in Sample()).
  size_t animal_grid_hash{};
  uint64_t animal_revision{};
  // Map legend: native "map-legend" ScrollRegion, same hash/revision
  // convention as Statistics/Animal needs above.
  size_t legend_grid_hash{};
  uint64_t legend_revision{};
  // Garage specs: native "garage-specs" ScrollRegion, same hash/revision
  // convention (no identity counter -- garage_spec_identity_value is a
  // hash of vehicle_selected/world_identity, same reasoning as
  // animal_identity_value's own comment).
  size_t garage_spec_grid_hash{};
  uint64_t garage_spec_revision{};
  // Shop filter list: native "shop-filters" ScrollRegion, same hash/
  // revision convention (no identity counter -- shop_filter_identity_value
  // is a hash of shop_filter_mode/world_identity, same reasoning as
  // animal_identity_value's own comment).
  size_t shop_filter_grid_hash{};
  uint64_t shop_filter_revision{};
  std::atomic<size_t> topic_count{0};
  std::atomic<bool> art_ready{false};
  std::chrono::steady_clock::time_point next_frame{};
  std::chrono::steady_clock::time_point interacting_until{}, wheel_started{};
  ScrollPosition wheel_from{},wheel_to{};
  bool wheel_animating{};
  // Guide article content height: like the old help_max, only ever set
  // after an actual RenderHelpArticle call in ImageFor (under render_mutex)
  // -- Sample() just reads the last known value to publish content_h_bind,
  // the same lag-one-tick pattern help_max used before this conversion.
  size_t help_article_h{};
  size_t rendered_help_topic{size_t(-1)};
  // Animal needs pane content height: the same lag-one-tick pattern as
  // help_article_h above, only ever set after an actual RenderAnimalNeeds
  // call in ImageFor (under render_mutex). animal_max/rendered_animal_*
  // (the old stale-render scroll guard) are gone along with the
  // "animal_scroll"/"scroll:animals" module actions they guarded -- the
  // needs pane is the runtime's own native "animal-needs" ScrollRegion now.
  size_t animal_needs_h{};
  void Sample(const EdenDsmodHostApi &h) {
#ifdef FARMING_SIMULATOR_HAS_READER
    reader.Sample(h);
#endif
    std::string image, map_image;
    Page page;
    bool map = false, title = false, world = false, shop_details = false,
         animal_feeding = false, time_picker = false, time_badge = false,
         time_available = false, layer_active = false, map_legend = false,
         map_fullscreen = false, map_filters_open = false;
    int64_t transition_value{}, map_identity_value{};
    // Shop catalog grid: native ScrollRegion ("shop-catalog", gen_manifest.py).
    // shop_grid_count is the filtered item count (count_bind); shop_grid_identity
    // changes whenever the category/brand filter changes, so the runtime's
    // reset_bind snaps the grid back to the top the same way the module used
    // to zero its own offset on a filter pick/clear. shop_grid_src holds one
    // "module:farming_simulator:shop_row/<master_index>/<active>" text key per
    // filtered item (published as fs.ui.shop_grid.src_<i>, {i} substitution in
    // the manifest's repeat templates) -- the runtime only ever asks ImageFor
    // for the ones actually on screen, so this list being longer than one
    // visible window costs nothing but a handful of cheap string builds here.
    int64_t shop_grid_count{}, shop_grid_identity{};
    std::vector<std::string> shop_grid_src;
    // Prices matrix: native "fs-prices" ScrollRegion (gen_manifest.py).
    // price_grid_count is the (capped) station count, the region's
    // count_bind; price_grid_width is the (capped) crop count's content
    // width in canvas px, content_w_bind. The full PriceColumnCap(32)*212 =
    // 6784px row/header would be over the runtime's 4096px module image
    // limit (see PriceColumnChunks' own comment in farming_simulator_view.h),
    // so each row is split into PriceColumnChunks column chunks: price_row_src
    // holds one "module:farming_simulator:price_row/<station>/<chunk>/
    // <revision>" text key per (station, chunk) cell, in row-major order
    // (station * PriceColumnChunks + chunk) matching gen_manifest.py's
    // prices_rep repeat_cols grid (col = chunk, row = station) --
    // fs.ui.prices.row_src_<i>, {i} substitution. The header/stations keys
    // carry the same shared revision suffix -- see price_grid_hash/
    // price_revision below for how it is minted.
    int64_t price_grid_count{}, price_grid_width{}, price_identity_value{};
    uint64_t price_revision_value{};
    // price_row_src/price_header_src are now Module members (see their own
    // comment there), not locals: they are only rebuilt when
    // price_seen_management_revision moves, and otherwise reused unchanged
    // from the previous tick (ENGINEERING hardening #7).
    // Finance table: native "fs-finance" ScrollRegion. finance_grid_count is
    // the (capped) row count, the region's count_bind; finance_row_src holds
    // one "module:farming_simulator:finance_row/<row index>/<revision>" text
    // key per row (fs.ui.finance.row_src_<i>). The day axis has no live
    // count to publish at all -- FinanceContentW is a compile-time constant
    // (always exactly 5 days), so the manifest's "fs-finance" region sets
    // content_w directly instead of binding it.
    int64_t finance_grid_count{}, finance_identity_value{};
    uint64_t finance_revision_value{};
    // finance_row_src/finance_labels_src: Module members too, same reason.
    // Statistics list: native "statistic-rows" ScrollRegion, same convention
    // as the Finance fields immediately above (shared content hash/revision
    // gates stat_row_src's own cache-busting key suffix, independent of this
    // page's own fs.ui.frame revision).
    int64_t stat_grid_count{}, stat_identity_value{};
    // stat_row_src: Module member too, same reason.
    // Guide: native "help-topics" + "help-article"/"help-article-static"
    // ScrollRegions. help_topic_row_src holds one
    // "module:farming_simulator:help_topic_row/<index>/<active>" text key
    // per topic (fs.ui.help.topic_row_src_<i>) -- "active" (whether it is
    // the selected topic) is encoded directly in the key, the same way
    // RenderShopRow's active flag is, since nothing else about a topic row
    // ever changes once the Guide is loaded. help_article_h is the article's
    // real (cap-clamped) content height, content_h_bind for both regions.
    std::vector<std::string> help_topic_row_src;
    int64_t help_topic_value{}, help_topic_count{}, help_article_value{};
    // Animal needs pane: native "animal-needs" ScrollRegion. animal_identity
    // is this pane's own reset_bind -- a hash of selected/feeding/
    // world_identity (not an incrementing counter like price_identity/
    // finance_identity/stat_identity: computed fresh from `state` every
    // tick, since any one of the three changing should reset it, and all
    // three are already readily at hand here, unlike those panes' world-
    // change-only resets). animal_grid_hash/animal_revision gate the pane
    // image's own cache-busting key suffix on a needs/help content change,
    // same convention as price_grid_hash/finance_grid_hash/stat_grid_hash.
    int64_t animal_identity_value{}, animal_needs_value{};
    // Map legend: native "map-legend" ScrollRegion, same convention as the
    // Statistics list above (no identity counter -- legend_identity_value
    // is a hash of map_layer/world_identity, computed fresh from `state`
    // every tick, same reasoning as animal_identity_value's own comment).
    int64_t legend_grid_count{}, legend_identity_value{};
    std::vector<std::string> legend_row_src;
    // Garage: native "garage-list" + "garage-specs" ScrollRegions, same
    // convention as Prices/Finance/Animal needs above.
    int64_t garage_grid_count{}, garage_identity_value{};
    std::vector<std::string> garage_row_src;
    int64_t garage_spec_count{}, garage_spec_identity_value{};
    std::vector<std::string> garage_spec_row_src;
    // Shop filter list: native "shop-filters" ScrollRegion.
    int64_t shop_filter_count{}, shop_filter_identity_value{};
    std::vector<std::string> shop_filter_row_src;
    // The pan_zoom widgets' bound default ("home") view: zoom is always the
    // same constant for both viewports (see farming_simulator_view.h for why
    // compact/expanded can share it); cx/cy are the player's position as a
    // fraction of each viewport's own padded canvas, so "follow" (the
    // runtime tracks home while the view hasn't been moved away from it)
    // recentres on the player without the module doing anything per-frame.
    double map_home_zoom{}, map_home_cx_compact{}, map_home_cy_compact{},
        map_home_cx_expanded{}, map_home_cy_expanded{};
    // Map marker tap-select (UX-SPEC C): see the computation site below for why these are
    // home-view-only. kMarkerCap bounds the manifest's per-marker widget instances
    // (gen_manifest.py); comfortably above any known Switch Edition map's marker count.
    static constexpr size_t kMarkerCap = 48;
    std::vector<double> marker_dx, marker_dy;
    std::vector<bool> marker_selectable;
    // Pan-tracked per-viewport marker offsets (fix for the hit regions only
    // working at the default view, UX-SPEC C review): the same ArrowPanOffset
    // math the player arrow uses (farming_simulator_view.h), applied per
    // marker's own home fraction instead of the player's. marker_dx/dy above
    // stay the home-view-only fallback used when the runtime doesn't answer
    // the pan_zoom view query (map_marker_pan_tracked_* false) -- same
    // fallback convention as map_arrow_dx/dy_* vs map_arrow_pan_tracked_*.
    std::vector<double> marker_dx_compact, marker_dy_compact, marker_dx_expanded,
        marker_dy_expanded;
    // Garage action row (UX-SPEC B) / map selection card (UX-SPEC C): see the computation
    // sites below. kind is 0 none / 1 both / 2 left-only (Enter/Visit) / 3 right-only
    // (Reset/Tag place), the same meaning View::RenderGarageActionRow/RenderMapSelectionButtons
    // give it.
    int64_t garage_action_kind{};
    bool map_selection_active{}, map_selection_place{}, map_selection_tagged{};
    int64_t map_selection_kind{};
    // The player arrow's rotate_bind target: published every Sample() tick,
    // never gated on MapSnapshotKey/next_map_frame, so the runtime turns the
    // overlay arrow (map-arrow-compact/expanded, gen_manifest.py) smoothly
    // while walking/driving instead of only on the map raster's own ~5Hz
    // rebuild. Sign matches Canvas::DrawImageTransformed's inverse-rotation
    // convention (runtime mods/mod_ui_image.cpp): negated so the live sprite
    // turns the same visual direction the old baked bake used.
    double map_arrow_heading_deg{};
    bool map_arrow_visible{};
    // Forward-compatible player-marker-stays-visible-while-panned support
    // (ArrowPanOffset, farming_simulator_view.h): the arrow's x_bind/y_bind
    // offset within its own viewport, and whether this tick's runtime
    // actually answered the pan_zoom view query (0/0 and false on an older
    // runtime -- the arrow then keeps its prior pinned-at-centre behaviour,
    // see gen_manifest.py's fs.view.map_arrow_{suffix}).
    double map_arrow_dx_compact{}, map_arrow_dy_compact{}, map_arrow_dx_expanded{},
        map_arrow_dy_expanded{};
    bool map_arrow_pan_tracked_compact{}, map_arrow_pan_tracked_expanded{};
    // scroll_phases/phase_keys removed entirely: shop_grid_phase/
    // help_phase/garage_phase/shop_filter_phase were the last
    // y_bind-shifted hit grids left to phase-correct, and the Shop filter
    // list (the last of the four to convert) is a native "shop-filters"
    // ScrollRegion now too -- every repeat template's taps carry an
    // absolute index directly (payload "{i}") across the whole module.
    std::string layer_error;
    {
      std::lock_guard lock{mutex};
#ifdef FARMING_SIMULATOR_HAS_READER
      auto next = reader.GetSnapshot();
      if (next.world_identity != state.snapshot.world_identity) {
        wheel_animating = false;
        ++map_identity;
        state.map_fullscreen = false;
        state.time_scale_open = false;
        state.map_layer = 0;
        state.map_legend = false;
        // FILTERS popup (UX-SPEC §A.5): "World changes invalidate entity
        // selections and old HUD data" -- reset to the native default (all
        // ON) and close the popup, the same way the other view toggles
        // above reset on a new world.
        state.map_filters_open = false;
        state.map_filters.fill(true);
        // map_legend_offset no longer zeroed here: the legend list is the
        // runtime's own native "map-legend" ScrollRegion now
        // (RenderMapLegendRow). Its reset_bind (fs.ui.map_legend_identity,
        // published below) is a hash of map_layer/world_identity, so
        // resetting map_layer here already changes it (world_identity
        // itself changed too) -- no separate counter needed, same
        // convention as the Animal needs pane's own reset_bind.
        // Guide: the topics list and article body are the runtime's own
        // native "help-topics"/"help-article[-static]" ScrollRegions now --
        // nothing left to zero here. help_topic itself is untouched by a
        // world change (same as before this conversion: Guide content is
        // world-independent), so the article's reset_bind (fs.ui.help_topic,
        // published below) only fires on an actual topic pick, not here.
        // Prices/Finance/Statistics: the runtime's own native "fs-prices"/
        // "fs-finance"/"statistic-rows" ScrollRegions now own their scroll
        // offsets entirely -- nothing left to zero here. Their reset_binds
        // (fs.ui.prices.identity/fs.ui.finance.identity/fs.ui.stats.identity,
        // bumped below) snap them back to the top on a new world the same
        // way this block used to.
        ++price_identity;
        ++finance_identity;
        ++stat_identity;
        ++garage_identity;
        // animal_needs_offset no longer zeroed here: the needs pane is the
        // runtime's own native "animal-needs" ScrollRegion now
        // (RenderAnimalNeeds). Its reset_bind (fs.ui.animal_identity,
        // published below) is a hash of selected/feeding/world_identity, so
        // resetting animal_selected/animal_feeding here already changes it
        // (world_identity itself changed too) -- no separate counter needed.
        state.animal_selected = 0;
        state.animal_feeding = false;
        state.shop_details = false;
        // shop_category_offset/shop_brand_offset no longer zeroed here: the
        // filter list is the runtime's own native "shop-filters"
        // ScrollRegion now (RenderShopFilterRow). Its reset_bind
        // (fs.ui.shop_filters_identity, hashed below) is a hash of
        // shop_filter_mode/world_identity, so resetting shop_filter_mode
        // elsewhere (shop_mode action) or this world change already
        // changes it -- no separate counter needed.
        // vehicles_offset/vehicle_spec_offset no longer zeroed here: the
        // Garage list and specs are the runtime's own native
        // "garage-list"/"garage-specs" ScrollRegions now (RenderGarageRow/
        // RenderGarageSpecRow). Their reset_binds (fs.ui.garage.identity/
        // fs.ui.garage_spec_identity, bumped/hashed below) snap them back
        // to the top on a new world the same way this block used to.
        state.shop_spec_offset = 0;
        state.shop_category.clear();
        state.shop_brand.clear();
        state.shop_selected.clear();
        state.vehicle_selected.clear();
        // Map marker selection (UX-SPEC C.3/C.5): a new world invalidates the old marker
        // indices outright (map_markers is rebuilt from scratch per world), same rule as every
        // other entity selection above. The local tag belief (tagged_marker_belief_) is about a
        // specific marker in the OLD world too.
        state.map_marker_selected = -1;
        tagged_marker_belief_ = -1;
      }
      state.snapshot = std::move(next);
      if (!state.snapshot.world_ready || state.snapshot.native_menu_open) {
        state.time_scale_open = false;
      }
      if (!state.snapshot.map_layers) {
        state.map_layer = 0;
        state.map_legend = false;
      }
#endif
      auto now = std::chrono::steady_clock::now();
      if(wheel_animating) {
        const double t=std::clamp(std::chrono::duration<double,std::milli>(now-wheel_started).count()/140.,0.,1.);
        const double ease=1-std::pow(1-t,3);
        ScrollPosition current{};
        for(size_t i=0;i<current.size();++i) current[i]=wheel_from[i]+(wheel_to[i]-wheel_from[i])*ease;
        SetScrollValues(state,current);
        interacting_until=now+std::chrono::milliseconds{250};
        if(t>=1)wheel_animating=false;
      }
      auto key = SnapshotKey(state);
      // Local navigation publishes immediately; changing HUD/position is capped
      // at5Hz.
      bool page_changed =
          !retained.empty() && retained.back().second.page != state.page;
      bool invalidated = false;
      if (!retained.empty()) {
        const auto &old = retained.back().second.snapshot;
        const auto &current = state.snapshot;
        invalidated = old.world_identity != current.world_identity ||
                      old.phase != current.phase ||
                      old.world_ready != current.world_ready ||
                      old.vehicle_ready != current.vehicle_ready ||
                      old.map_id != current.map_id ||
                      old.finances_ready != current.finances_ready ||
                      old.statistics_ready != current.statistics_ready ||
                      old.prices_ready != current.prices_ready ||
                      old.animals_ready != current.animals_ready ||
                      old.shop_ready != current.shop_ready ||
                      old.vehicles_ready != current.vehicles_ready;
        auto lost = [](const auto &a, const auto &b) {
          return a.has_value() && !b.has_value();
        };
        invalidated = invalidated || lost(old.money, current.money) ||
                      lost(old.time_minutes, current.time_minutes) ||
                      lost(old.player_x, current.player_x) ||
                      lost(old.player_z, current.player_z) ||
                      lost(old.fuel_l, current.fuel_l) ||
                      lost(old.fuel_capacity_l, current.fuel_capacity_l) ||
                      lost(old.fill_l, current.fill_l) ||
                      lost(old.fill_capacity_l, current.fill_capacity_l) ||
                      lost(old.speed_kmh, current.speed_kmh);
        // The Page::Prices "lost a value" checks that used to live here are
        // gone: fs.ui.frame no longer renders any crop/station content (it
        // is just the static title chrome now -- see View::Prices and
        // SnapshotKey's Page::Prices case). The header/stations/row module
        // images that DO show that content have their own, independent
        // change signal (price_grid_hash/price_revision, Sample() above),
        // gated on the "fs-prices" ScrollRegion's own cadence instead of
        // this page's frame revision.
        // The Page::Finances "lost a value" checks for finance_rows/
        // finance_totals that used to live here are gone the same way: this
        // frame no longer renders the day header, row-label column, grid
        // rows or totals row amounts (just the three summary boxes and the
        // static title/total chrome -- see View::Finances and SnapshotKey's
        // Page::Finances case). loan/finance_saldo stay, since those two ARE
        // still drawn into the frame as summary boxes; money's own lost()
        // check already lives in the generic block above.
        if (state.page == Page::Finances) {
          invalidated = invalidated || lost(old.loan, current.loan) ||
                        lost(old.finance_saldo, current.finance_saldo);
        }
        if (state.page == Page::Statistics) {
          invalidated = invalidated || old.statistic_rows.size() >
                                           current.statistic_rows.size();
          for (size_t i = 0; i < std::min(old.statistic_rows.size(),
                                          current.statistic_rows.size());
               ++i) {
            const auto &a = old.statistic_rows[i],
                       &b = current.statistic_rows[i];
            invalidated = invalidated || lost(a.session, b.session) ||
                          lost(a.total, b.total) ||
                          lost(a.session_text, b.session_text) ||
                          lost(a.total_text, b.total_text);
          }
        }
      }
      // A meaningful (navigation-like) view change, for the cross-fade
      // transition bind below. Compared against the last *minted* state
      // (not necessarily the previous Sample() tick's), same as
      // page_changed/invalidated above -- and, unlike them, not required to
      // also change `key`: some of these (time_scale_open, in particular)
      // are not part of every page's SnapshotKey branch, since the picker's
      // floating card draws over any page, not just the one whose key
      // tracks it.
      bool transition_changed = false;
      if (!retained.empty()) {
        const auto &prev = retained.back().second;
        transition_changed = prev.page != state.page ||
                             prev.time_scale_open != state.time_scale_open ||
                             prev.map_layer != state.map_layer ||
                             prev.map_fullscreen != state.map_fullscreen ||
                             prev.shop_details != state.shop_details ||
                             prev.vehicle_selected != state.vehicle_selected ||
                             prev.animal_selected != state.animal_selected ||
                             prev.animal_feeding != state.animal_feeding ||
                             prev.help_topic != state.help_topic ||
                             prev.map_marker_selected != state.map_marker_selected;
      }
      // Invalidated world/vehicle/data clears immediately; only routine value
      // changes wait -- and, additionally, wait for the previous revision to
      // actually be delivered (or for it to have gone stale), so routine
      // minting never outpaces decode. page_changed/invalidated/
      // transition_changed still mint immediately either way: the newest
      // revision simply supersedes whatever earlier one is still undelivered.
      // While a scroll gesture is live (now < interacting_until), the
      // undelivered-revision wait is bypassed too: a drag/wheel step is
      // superseded by the next one just like a page change, so there is
      // nothing to protect by waiting, and the wait is what capped
      // interactive scrolling to the decode rate (a few fps on slower
      // hardware) even though next_frame's own throttle had already been
      // relaxed to ~60Hz (16ms) for this same window. The one-undelivered
      // mitigation this bypasses was written against the *map* raster,
      // whose base-layer content does not visibly move on its own between
      // revisions (only the runtime's native pan_zoom moves it) -- so a
      // skipped/stale map decode reads as a flicker. A scrolling list/table
      // has no such independent motion: every mint already carries the
      // finger-tracking offset, so the worst case here is a dropped
      // intermediate frame, not a flicker.
      bool revision_delivered = served_revision >= revision;
      bool revision_stalled =
          !revision_delivered &&
          now - revision_minted_at >= std::chrono::seconds{1};
      bool interacting = now < interacting_until;
      bool should_mint =
          transition_changed ||
          (key != last_key &&
           (page_changed || invalidated || revision == 0 || interacting ||
            ((revision_delivered || revision_stalled) && now >= next_frame)));
      if (should_mint) {
        last_key = key;
        if (transition_changed)
          ++transition;
        ++revision;
        revision_minted_at = now;
        retained.emplace_back(revision, state);
        // Bumped from 8: with the undelivered-revision wait bypassed while
        // interacting (above), a fast drag can now mint up to ~60/s for the
        // ~250ms window instead of being paced to decode, so the backlog of
        // not-yet-served revisions this needs to hold is deeper.
        while (retained.size() > 20)
          retained.pop_front();
        next_frame = now + std::chrono::milliseconds{now < interacting_until ? 16 : 200};
      }
      image = "module:farming_simulator:frame/" + std::to_string(revision);
      page = state.page;
      map = state.snapshot.world_ready && state.snapshot.map_id >= 1 &&
            state.snapshot.map_id <= 2;
      title = state.snapshot.phase == Phase::Title;
      world = state.snapshot.world_ready;
      shop_details = state.shop_details;
      animal_feeding = state.animal_feeding;
      // The time control's top-right badge/picker is available on every page
      // except Animals, which has no top-bar band to host it (its full width
      // is the species selector).
      time_picker = state.time_scale_open && page != Page::Animals && world;
      // The badge itself renders whenever this holds (see View::TimeControl
      // and View::Render's `page != Page::Animals` guard); time_available
      // additionally requires a change to actually be allowed right now, so
      // tapping a visible-but-muted badge reaches the "time_picker" action's
      // own enabled_bind (manifest) and gets refused there, rather than the
      // tap having no hit target to land on in the first place.
      time_badge = world && page != Page::Animals &&
                  state.snapshot.time_scale.has_value();
      time_available = time_badge && state.snapshot.time_scale_change_allowed;
      map_filters_open = state.map_filters_open;
      layer_active = map && page == Page::Operations && state.map_layer &&
                     bool(state.snapshot.map_layers) && !time_picker && !state.map_fullscreen &&
                     !map_filters_open;
      map_legend = layer_active && state.map_legend;
      map_fullscreen = state.map_fullscreen;
      layer_error = state.snapshot.map_layer_error;
      map_home_zoom = DefaultMapZoom;
      {
        const double px = std::clamp(state.snapshot.player_x.value_or(0), -MapWorldExtent / 2,
                                     MapWorldExtent / 2),
                     pz = std::clamp(state.snapshot.player_z.value_or(0), -MapWorldExtent / 2,
                                     MapWorldExtent / 2);
        map_home_cx_compact = (px + MapWorldExtent / 2) / MapWorldExtent;
        map_home_cy_compact = (pz + MapWorldExtent / 2) / MapWorldExtent;
        map_home_cx_expanded = (px + MapCanvasW / 2) / MapCanvasW;
        map_home_cy_expanded = (pz + MapCanvasH / 2) / MapCanvasH;
        // Map marker tap-select hit regions (UX-SPEC C, gen_manifest.py's per-marker
        // "map-marker-hit-{compact,expanded}-<i>" widgets): the runtime exposes no live
        // pan_zoom transform back to a module (same limitation RenderMapArrow's own comment
        // documents for the player arrow overlay), so a marker's screen offset from the
        // viewport centre can only be computed for the DEFAULT (not panned/zoomed) view --
        // fs.ui.map_marker_show_{compact,expanded}_<i> (derived, manifest) is false whenever
        // that viewport has moved away from home, hiding the hit region until Reset restores
        // it. ppw matches RenderMapBase's own world-units-per-pixel; identical for both
        // viewports by construction (farming_simulator_view.h's CompactMapPx/MapWorldExtent).
        constexpr double kMarkerPpw = CompactMapPx / MapWorldExtent;
        const auto &markers = state.snapshot.map_markers;
        marker_dx.assign(kMarkerCap, 0.0);
        marker_dy.assign(kMarkerCap, 0.0);
        marker_selectable.assign(kMarkerCap, false);
        for (size_t i = 0; i < kMarkerCap && i < markers.size(); ++i) {
          const auto &m = markers[i];
          marker_dx[i] = (m.x - px) * kMarkerPpw;
          marker_dy[i] = (m.z - pz) * kMarkerPpw;
          // A marker its Filters toggle hides is not drawn, so it must not be tappable either.
          marker_selectable[i] =
              m.enabled && (m.can_visit || m.kind.starts_with("VEHICLE_")) &&
              state.map_filters[size_t(MapMarkerFilterIndex(m))];
        }
        // A marker drawn under a higher-ranked one (a header under its combine) gives up its
        // hit region, so a tap on the stack picks the marker the player actually sees.
        constexpr double kCoverPx = 24;
        for (size_t i = 0; i < kMarkerCap && i < markers.size(); ++i) {
          if (!marker_selectable[i])
            continue;
          for (size_t j = 0; j < kMarkerCap && j < markers.size(); ++j)
            if (j != i && marker_selectable[j] &&
                MapMarkerDrawRank(markers[j]) > MapMarkerDrawRank(markers[i]) &&
                std::hypot(marker_dx[j] - marker_dx[i], marker_dy[j] - marker_dy[i]) < kCoverPx) {
              marker_selectable[i] = false;
              break;
            }
        }
      }
      // Garage action row (UX-SPEC B): the served image key is purely a function of which of
      // can_enter/can_reset the CURRENTLY selected vehicle has -- 0 none (row collapses), 1
      // both, 2 Enter only, 3 Reset only.
      if (auto *selected = SelectedVehicle(state))
        garage_action_kind = selected->can_enter ? (selected->can_reset ? 1 : 2)
                             : selected->can_reset ? 3
                                                    : 0;
      // Map selection card (UX-SPEC C): resolves the tapped marker to either a VehicleItem
      // (Enter/Reset, same rule/kind as Garage above) or a place (Visit/Tag place). kMarkerCap
      // only bounds the per-marker HIT REGIONS (manifest); the selection itself can be any
      // index the reader produced, so this reads state.snapshot.map_markers directly rather
      // than the capped marker_selectable vector above.
      if (state.map_marker_selected >= 0 &&
          size_t(state.map_marker_selected) < state.snapshot.map_markers.size()) {
        const auto &m = state.snapshot.map_markers[size_t(state.map_marker_selected)];
        if (m.enabled && !m.can_visit && m.kind.starts_with("VEHICLE_")) {
          map_selection_place = false;
          const auto &vehicles = state.snapshot.owned_vehicles;
          auto it = std::find_if(vehicles.begin(), vehicles.end(),
                                 [&](const auto &x) { return x.root_node == m.object_id; });
          if (it != vehicles.end()) {
            map_selection_active = true;
            // Map card offers Enter only. Reset sends the vehicle to the shop lot across the
            // map, so it lives in Garage alone (owner, 2026-10-18: one accidental tap on the
            // map card teleported the selected vehicle away).
            map_selection_kind = it->can_enter ? 2 : 0;
          }
        } else if (m.enabled && m.can_visit) {
          map_selection_place = true;
          map_selection_active = true;
          // Visit is hidden at (approximately) the player's own position -- a heuristic (not
          // RE-verified; UX-SPEC C.3 flags the native behaviour here as unconfirmed), not a
          // guess about Tag: Tag place/Untag place is always offered for any selectable place.
          const double dx = m.x - state.snapshot.player_x.value_or(m.x),
                      dz = m.z - state.snapshot.player_z.value_or(m.z);
          const bool at_player = (dx * dx + dz * dz) < 4.0;
          map_selection_kind = at_player ? 3 : 1;
          map_selection_tagged = tagged_marker_belief_ == state.map_marker_selected;
        }
      }
      // Tag/Untag completion -> local tag belief (tagged_marker_belief_, see its own
      // declaration comment). game_call.serial (+1 per finished call, any kind) only means
      // "a call just finished" once; this reacts the one time it changes to that value.
      if (state.snapshot.game_call.serial != last_seen_call_serial_) {
        last_seen_call_serial_ = state.snapshot.game_call.serial;
        if (state.snapshot.game_call.kind == "tag" && state.snapshot.game_call.status == 0 &&
            state.snapshot.game_call.code == 1) {
          if (state.snapshot.game_call.detail == "tagged")
            tagged_marker_belief_ = state.map_marker_selected;
          else if (state.snapshot.game_call.detail == "untagged")
            tagged_marker_belief_ = -1;
        }
      }
      if (map && state.snapshot.player_heading) {
        map_arrow_visible = true;
        map_arrow_heading_deg =
            -(*state.snapshot.player_heading) * (180.0 / 3.14159265358979323846);
      }
      // ArrowPanOffset (farming_simulator_view.h): on a runtime that answers
      // "@view_zoom/@view_pan_x/@view_pan_y:<id>" for a pan_zoom Image widget
      // (requested, not yet in beta.17 -- see that function's comment), keep
      // the arrow glued to the player's true screen position inside the
      // viewport even while the user has panned/zoomed it away from home.
      // h.get_f64 is itself optional (older module_worker_host copies leave
      // it null); the NaN fallback also covers an unanswered/unknown key on
      // a host that does provide get_f64.
      map_arrow_pan_tracked_compact = map_arrow_pan_tracked_expanded = false;
      map_arrow_dx_compact = map_arrow_dy_compact = 0.;
      map_arrow_dx_expanded = map_arrow_dy_expanded = 0.;
      // Default to the home-view-only fallback (marker_dx/dy) so gen_manifest.py's per-marker
      // hit widgets can bind to these per-viewport keys UNCONDITIONALLY -- correct already on
      // an older runtime that never answers the pan_zoom view query (never overwritten below),
      // and overwritten with the real tracked offset per viewport once it does.
      marker_dx_compact = marker_dx;
      marker_dy_compact = marker_dy;
      marker_dx_expanded = marker_dx;
      marker_dy_expanded = marker_dy;
      if (h.get_f64) {
        // Cached once per viewport per tick (not re-queried per marker below):
        // the same live pan_zoom view the arrow above just read.
        const double zoom_compact = h.get_f64(h.userdata, "@view_zoom:map-compact", std::nan("")),
                     pan_x_compact =
                         h.get_f64(h.userdata, "@view_pan_x:map-compact", std::nan("")),
                     pan_y_compact =
                         h.get_f64(h.userdata, "@view_pan_y:map-compact", std::nan("")),
                     zoom_expanded =
                         h.get_f64(h.userdata, "@view_zoom:map-expanded", std::nan("")),
                     pan_x_expanded =
                         h.get_f64(h.userdata, "@view_pan_x:map-expanded", std::nan("")),
                     pan_y_expanded =
                         h.get_f64(h.userdata, "@view_pan_y:map-expanded", std::nan(""));
        if (auto off = ArrowPanOffset(map_home_cx_compact, map_home_cy_compact, CompactMapPx,
                                      CompactMapPx, zoom_compact, pan_x_compact, pan_y_compact)) {
          map_arrow_dx_compact = (*off)[0];
          map_arrow_dy_compact = (*off)[1];
          map_arrow_pan_tracked_compact = true;
        }
        if (auto off = ArrowPanOffset(map_home_cx_expanded, map_home_cy_expanded, ExpandedMapW,
                                      ExpandedMapH, zoom_expanded, pan_x_expanded,
                                      pan_y_expanded)) {
          map_arrow_dx_expanded = (*off)[0];
          map_arrow_dy_expanded = (*off)[1];
          map_arrow_pan_tracked_expanded = true;
        }
        // Per-marker tap-select hit regions (fix for them only working at the default view,
        // UX-SPEC C review): the exact same ArrowPanOffset math, per marker's own home
        // fraction of each viewport's padded canvas rather than the player's. Markers keep the
        // same live world x/z regardless of viewport; only the canvas extent (and so the home
        // fraction) differs between compact (MapWorldExtent) and expanded (MapCanvasW/H), same
        // as map_home_cx/cy_{compact,expanded} above.
        if (map_arrow_pan_tracked_compact || map_arrow_pan_tracked_expanded) {
          const auto &markers = state.snapshot.map_markers;
          for (size_t i = 0; i < kMarkerCap && i < markers.size(); ++i) {
            const auto &m = markers[i];
            const double frac_x_compact = (m.x + MapWorldExtent / 2) / MapWorldExtent,
                         frac_y_compact = (m.z + MapWorldExtent / 2) / MapWorldExtent,
                         frac_x_expanded = (m.x + MapCanvasW / 2) / MapCanvasW,
                         frac_y_expanded = (m.z + MapCanvasH / 2) / MapCanvasH;
            if (map_arrow_pan_tracked_compact) {
              if (auto off = ArrowPanOffset(frac_x_compact, frac_y_compact, CompactMapPx,
                                            CompactMapPx, zoom_compact, pan_x_compact,
                                            pan_y_compact)) {
                marker_dx_compact[i] = (*off)[0];
                marker_dy_compact[i] = (*off)[1];
              }
            }
            if (map_arrow_pan_tracked_expanded) {
              if (auto off = ArrowPanOffset(frac_x_expanded, frac_y_expanded, ExpandedMapW,
                                            ExpandedMapH, zoom_expanded, pan_x_expanded,
                                            pan_y_expanded)) {
                marker_dx_expanded[i] = (*off)[0];
                marker_dy_expanded[i] = (*off)[1];
              }
            }
          }
        }
      }
      map_identity_value = map_identity;
      // The map's own image: fully independent revision/pacing from the
      // frame above, keyed only on what actually changes its pixels
      // (MapSnapshotKey) -- never on pan/zoom/follow, which the module no
      // longer tracks at all.
      if (map) {
        auto map_key = MapSnapshotKey(state);
        bool map_revision_delivered = served_map_revision >= map_revision;
        bool map_revision_stalled =
            !map_revision_delivered &&
            now - map_revision_minted_at >= std::chrono::seconds{1};
        bool should_mint_map =
            map_key != last_map_key &&
            (map_revision == 0 ||
             ((map_revision_delivered || map_revision_stalled) && now >= next_map_frame));
        if (should_mint_map) {
          last_map_key = map_key;
          ++map_revision;
          map_revision_minted_at = now;
          retained_map.emplace_back(map_revision, state);
          while (retained_map.size() > 4)
            retained_map.pop_front();
          next_map_frame = now + std::chrono::milliseconds{200};
        }
        map_image = "module:farming_simulator:map/" + std::to_string(map_revision);
      }
      transition_value = transition;
      if (state.page == Page::Shop && state.snapshot.world_ready &&
          state.snapshot.shop_ready) {
        const auto shop_items = ShopItems(state);
        constexpr size_t ShopGridCap = 512;
        shop_grid_count = int64_t(std::min(shop_items.size(), ShopGridCap));
        shop_grid_identity =
            int64_t(std::hash<std::string>{}(state.shop_category + '\x01' + state.shop_brand));
        shop_grid_src.reserve(size_t(shop_grid_count));
        for (size_t i = 0; i < size_t(shop_grid_count); ++i) {
          const size_t master_index = shop_items[i];
          const bool active = state.snapshot.shop_items[master_index].id == state.shop_selected;
          shop_grid_src.push_back("module:farming_simulator:shop_row/" +
                                  std::to_string(master_index) + '/' +
                                  (active ? '1' : '0'));
        }
      }
      // fs.ui.help_phase/fs.ui.garage_phase/fs.ui.shop_filter_phase dropped:
      // none of the Guide topics list, Garage list or Shop filter list has
      // a y_bind-shifted hit grid left to phase-correct -- every repeat
      // template's taps carry the absolute index directly (payload "{i}")
      // now, the same way the Shop catalog grid's cards do.
      price_identity_value = price_identity;
      if (state.page == Page::Prices && state.snapshot.world_ready &&
          state.snapshot.prices_ready) {
        const size_t crops = std::min(state.snapshot.price_crops.size(), PriceColumnCap);
        const size_t stations = std::min(state.snapshot.price_stations.size(), PriceStationCap);
        price_grid_count = int64_t(stations);
        price_grid_width = int64_t(crops) * PriceColumnWidth;
        // The underlying data (price_crops/price_stations) only ever
        // changes when the reader's 1Hz management snapshot refreshes
        // (Snapshot::management_revision's own comment) -- re-serializing
        // and re-hashing the whole matrix on every one of Sample()'s own
        // 60Hz ticks while the Prices page stays open is wasted work on 59
        // out of every 60 calls (ENGINEERING hardening #7). Skip straight
        // to republishing the unchanged price_row_src/price_header_src
        // (Module members, not locals -- see their own comment) when the
        // revision has not moved.
        if (state.snapshot.management_revision != price_seen_management_revision) {
          price_seen_management_revision = state.snapshot.management_revision;
          // A cheap content hash (crop/station identity, name, art and
          // every visible quote), NOT this page's own fs.ui.frame
          // SnapshotKey: the header/stations/row images live entirely
          // outside that frame now, so they need their own, narrower change
          // signal to decide when the revision suffix in their keys
          // (price_row_src below, plus fs.ui.prices.header_src/
          // stations_src) has to move -- otherwise a price tick while a row
          // is on screen would never re-fetch it, the same staleness risk
          // View::RenderShopRow's own comment calls out for an unchanging
          // key.
          std::string content;
          for (size_t j = 0; j < crops; ++j) {
            const auto &crop = state.snapshot.price_crops[j];
            content += crop.id;
            content += '\x01';
            content += crop.name;
            content += '\x01';
            content += crop.icon;
            content += '\x01';
            content += std::to_string(crop.silo_l.value_or(-1.));
            content += '\x02';
          }
          for (size_t i = 0; i < stations; ++i) {
            const auto &station = state.snapshot.price_stations[i];
            content += station.id;
            content += '\x01';
            content += station.name;
            for (size_t j = 0; j < crops; ++j) {
              content += '\x01';
              if (j >= station.quotes.size()) {
                content += '?';
                continue;
              }
              const auto &quote = station.quotes[j];
              content += quote.accepted ? '1' : '0';
              content += std::to_string(quote.price_per1000.value_or(-1.));
              content += std::to_string(int(quote.trend));
              content += std::to_string(int(quote.highlight));
              content += quote.great_demand ? '1' : '0';
            }
            content += '\x02';
          }
          const size_t hash = std::hash<std::string>{}(content);
          if (hash != price_grid_hash) {
            price_grid_hash = hash;
            ++price_revision;
          }
          price_row_src.clear();
          price_row_src.reserve(stations * PriceColumnChunks);
          for (size_t i = 0; i < stations; ++i) {
            for (size_t chunk = 0; chunk < PriceColumnChunks; ++chunk) {
              price_row_src.push_back("module:farming_simulator:price_row/" +
                                      std::to_string(i) + '/' +
                                      std::to_string(chunk) + '/' +
                                      std::to_string(price_revision));
            }
          }
          price_header_src.clear();
          price_header_src.reserve(PriceColumnChunks);
          for (size_t chunk = 0; chunk < PriceColumnChunks; ++chunk) {
            price_header_src.push_back("module:farming_simulator:price_header/" +
                                       std::to_string(chunk) + '/' +
                                       std::to_string(price_revision));
          }
        }
      }
      price_revision_value = price_revision;
      finance_identity_value = finance_identity;
      if (state.page == Page::Finances && state.snapshot.world_ready &&
          state.snapshot.finances_ready) {
        const size_t rows = std::min(state.snapshot.finance_rows.size(), FinanceRowCap);
        finance_grid_count = int64_t(rows);
        // Same reasoning as the Prices block above (ENGINEERING hardening
        // #7): only re-serialize+hash when the reader's management
        // snapshot actually refreshed, otherwise reuse
        // finance_row_src/finance_labels_src unchanged.
        if (state.snapshot.management_revision != finance_seen_management_revision) {
          finance_seen_management_revision = state.snapshot.management_revision;
          // Shared content hash for the header (day labels), the row-label
          // column and every row (labels/amounts) and the totals row --
          // mirrors price_grid_hash's reasoning: a repeat template's key
          // does not change just by staying on screen, so RenderFinanceRow
          // needs its own narrower change signal, independent of this
          // page's own (now much smaller) fs.ui.frame SnapshotKey.
          std::string content;
          for (const auto &day : state.snapshot.finance_day_labels) {
            content += day;
            content += '\x01';
          }
          content += '\x02';
          for (const auto &total : state.snapshot.finance_totals) {
            content += std::to_string(total.value_or(-1.));
            content += '\x01';
          }
          content += '\x02';
          for (size_t i = 0; i < rows; ++i) {
            const auto &row = state.snapshot.finance_rows[i];
            content += row.label;
            for (const auto &amount : row.amounts) {
              content += '\x01';
              content += std::to_string(amount.value_or(-1.));
            }
            content += '\x02';
          }
          const size_t hash = std::hash<std::string>{}(content);
          if (hash != finance_grid_hash) {
            finance_grid_hash = hash;
            ++finance_revision;
          }
          finance_row_src.clear();
          finance_row_src.reserve(rows);
          for (size_t i = 0; i < rows; ++i) {
            finance_row_src.push_back("module:farming_simulator:finance_row/" +
                                      std::to_string(i) + '/' +
                                      std::to_string(finance_revision));
          }
          finance_labels_src.clear();
          finance_labels_src.reserve(FinanceLabelChunks);
          for (size_t chunk = 0; chunk < FinanceLabelChunks; ++chunk) {
            finance_labels_src.push_back("module:farming_simulator:finance_labels/" +
                                         std::to_string(chunk) + '/' +
                                         std::to_string(finance_revision));
          }
        }
      }
      finance_revision_value = finance_revision;
      stat_identity_value = stat_identity;
      if (state.page == Page::Statistics && state.snapshot.world_ready &&
          state.snapshot.statistics_ready) {
        const size_t rows = std::min(state.snapshot.statistic_rows.size(), StatisticRowCap);
        stat_grid_count = int64_t(rows);
        // Same reasoning as the Prices/Finance blocks above (ENGINEERING
        // hardening #7): only re-serialize+hash when the reader's
        // management snapshot actually refreshed, otherwise reuse
        // stat_row_src unchanged.
        if (state.snapshot.management_revision != stat_seen_management_revision) {
          stat_seen_management_revision = state.snapshot.management_revision;
          // Shared content hash for every row (label/unit/digits/session/
          // total) -- mirrors finance_grid_hash's reasoning: a repeat
          // template's key does not change just by staying on screen, so
          // RenderStatisticRow needs its own narrower change signal.
          std::string content;
          for (size_t i = 0; i < rows; ++i) {
            const auto &r = state.snapshot.statistic_rows[i];
            content += r.label;
            content += '\x01';
            content += r.unit;
            content += '\x01';
            content += std::to_string(r.digits);
            content += '\x01';
            content += r.session_text.value_or(std::to_string(r.session.value_or(-1.)));
            content += '\x01';
            content += r.total_text.value_or(std::to_string(r.total.value_or(-1.)));
            content += '\x02';
          }
          const size_t hash = std::hash<std::string>{}(content);
          if (hash != stat_grid_hash) {
            stat_grid_hash = hash;
            ++stat_revision;
          }
          stat_row_src.clear();
          stat_row_src.reserve(rows);
          for (size_t i = 0; i < rows; ++i) {
            stat_row_src.push_back("module:farming_simulator:stat_row/" +
                                   std::to_string(i) + '/' +
                                   std::to_string(stat_revision));
          }
        }
      }
      // Animal needs pane: native "animal-needs" ScrollRegion.
      animal_identity_value = int64_t(std::hash<std::string>{}(
          std::to_string(state.animal_selected) + '\x01' +
          (state.animal_feeding ? "1" : "0") + '\x01' +
          std::to_string(state.snapshot.world_identity)));
      if (state.page == Page::Animals && state.snapshot.world_ready &&
          state.snapshot.animals_ready && state.animal_feeding) {
        static constexpr std::array<std::string_view, 3> kAnimalIds{"sheep", "pig", "cow"};
        const auto &animals = state.snapshot.animals;
        auto found = std::find_if(animals.begin(), animals.end(), [&](const auto &a) {
          return a.id == kAnimalIds[std::min(state.animal_selected, size_t(2))];
        });
        if (found != animals.end()) {
          // Shared content hash for the needs rows plus help text/
          // illustration -- mirrors stat_grid_hash's reasoning: the pane's
          // own image key does not change just by staying on screen, so
          // RenderAnimalNeeds needs its own narrower change signal.
          std::string content = found->help_text + '\x02' + found->help_image;
          for (const auto &n : found->needs) {
            content += '\x01';
            content += n.label;
            content += '\x01';
            content += n.unit;
            content += '\x01';
            content += std::to_string(n.level.value_or(-1.));
            content += '\x01';
            content += std::to_string(n.capacity.value_or(-1.));
          }
          const size_t hash = std::hash<std::string>{}(content);
          if (hash != animal_grid_hash) {
            animal_grid_hash = hash;
            ++animal_revision;
          }
        }
      }
      animal_needs_value = int64_t(animal_revision);
      // Map legend: native "map-legend" ScrollRegion.
      legend_identity_value = int64_t(std::hash<std::string>{}(
          std::to_string(state.map_layer) + '\x01' +
          std::to_string(state.snapshot.world_identity)));
      if (map_legend && state.snapshot.map_layers) {
        const auto &legend = state.map_layer == 1
                                  ? state.snapshot.map_layers->crop_legend
                              : state.map_layer == 2
                                  ? state.snapshot.map_layers->growth_legend
                                  : state.snapshot.map_layers->soil_legend;
        const size_t rows = std::min(legend.size(), MapLegendRowCap);
        legend_grid_count = int64_t(rows);
        // Shared content hash for every row (colors/icon/label) -- mirrors
        // stat_grid_hash's reasoning: a repeat template's key does not
        // change just by staying on screen, so RenderMapLegendRow needs its
        // own narrower change signal.
        std::string content;
        for (size_t i = 0; i < rows; ++i) {
          const auto &row = legend[i];
          content += row.label;
          content += '\x01';
          content += row.icon;
          for (auto color : row.colors) {
            content += '\x01';
            content += std::to_string(color);
          }
          content += '\x02';
        }
        const size_t hash = std::hash<std::string>{}(content);
        if (hash != legend_grid_hash) {
          legend_grid_hash = hash;
          ++legend_revision;
        }
        legend_row_src.reserve(rows);
        for (size_t i = 0; i < rows; ++i) {
          legend_row_src.push_back("module:farming_simulator:map_legend_row/" +
                                   std::to_string(i) + '/' +
                                   std::to_string(legend_revision));
        }
      }
      garage_identity_value = garage_identity;
      if (state.page == Page::Vehicles && state.snapshot.world_ready &&
          state.snapshot.vehicles_ready) {
        const size_t rows = std::min(state.snapshot.owned_vehicles.size(), GarageRowCap);
        garage_grid_count = int64_t(rows);
        std::string content;
        for (size_t i = 0; i < rows; ++i) {
          const auto &v = state.snapshot.owned_vehicles[i];
          content += v.id;
          content += '\x01';
          content += v.name;
          content += '\x01';
          content += v.image;
          content += '\x01';
          content += (v.leased ? "1" : "0");
          content += '\x02';
        }
        content += '\x03';
        content += state.vehicle_selected;
        const size_t hash = std::hash<std::string>{}(content);
        if (hash != garage_grid_hash) {
          garage_grid_hash = hash;
          ++garage_revision;
        }
        garage_row_src.reserve(rows);
        // G3 critique (2026-10-17, "Garage list has no selected-row state"): an exact
        // id==vehicle_selected match is never true before the player's first tap (state.
        // vehicle_selected starts empty, cleared again on every world change), yet
        // SelectedVehicle(state) -- which drives the detail pane AND the action row/Reset
        // target -- already falls back to owned_vehicles[0] in that case. The list highlight
        // must use the exact same fallback, or Reset acts on a vehicle with no visible
        // lime-bar target in the list.
        const bool any_selected = std::any_of(
            state.snapshot.owned_vehicles.begin(), state.snapshot.owned_vehicles.end(),
            [&](const auto &v) { return v.id == state.vehicle_selected; });
        for (size_t i = 0; i < rows; ++i) {
          const bool active = any_selected ? state.snapshot.owned_vehicles[i].id ==
                                                  state.vehicle_selected
                                            : i == 0;
          garage_row_src.push_back("module:farming_simulator:garage_row/" +
                                   std::to_string(i) + '/' + (active ? '1' : '0') + '/' +
                                   std::to_string(garage_revision));
        }
        // Garage specs: the CURRENTLY selected vehicle's specs only.
        auto selected = std::find_if(state.snapshot.owned_vehicles.begin(),
                                     state.snapshot.owned_vehicles.end(),
                                     [&](const auto &v) { return v.id == state.vehicle_selected; });
        if (selected == state.snapshot.owned_vehicles.end() && !state.snapshot.owned_vehicles.empty())
          selected = state.snapshot.owned_vehicles.begin();
        if (selected != state.snapshot.owned_vehicles.end()) {
          const size_t spec_rows = std::min(selected->specs.size(), GarageSpecRowCap);
          garage_spec_count = int64_t(spec_rows);
          std::string spec_content = selected->id;
          for (size_t i = 0; i < spec_rows; ++i) {
            spec_content += '\x01';
            spec_content += selected->specs[i].label;
            spec_content += '\x01';
            spec_content += selected->specs[i].value;
          }
          const size_t spec_hash = std::hash<std::string>{}(spec_content);
          if (spec_hash != garage_spec_grid_hash) {
            garage_spec_grid_hash = spec_hash;
            ++garage_spec_revision;
          }
          garage_spec_row_src.reserve(spec_rows);
          for (size_t i = 0; i < spec_rows; ++i) {
            garage_spec_row_src.push_back("module:farming_simulator:garage_spec_row/" +
                                          std::to_string(i) + '/' +
                                          std::to_string(garage_spec_revision));
          }
        }
      }
      garage_spec_identity_value = int64_t(std::hash<std::string>{}(
          state.vehicle_selected + '\x01' + std::to_string(state.snapshot.world_identity)));
      // Shop filter list: native "shop-filters" ScrollRegion. Whichever of
      // shop_filter_mode's two lists (category/brand) is active.
      shop_filter_identity_value = int64_t(std::hash<std::string>{}(
          std::to_string(state.shop_filter_mode) + '\x01' +
          std::to_string(state.snapshot.world_identity)));
      if (state.page == Page::Shop && state.snapshot.world_ready &&
          state.snapshot.shop_ready) {
        const size_t rows = std::min(state.shop_filter_mode
                                         ? state.snapshot.shop_brands.size()
                                         : state.snapshot.shop_categories.size(),
                                     ShopFilterRowCap);
        shop_filter_count = int64_t(rows);
        std::string content = state.shop_filter_mode ? "brand" : "category";
        for (size_t i = 0; i < rows; ++i) {
          if (state.shop_filter_mode) {
            const auto &b = state.snapshot.shop_brands[i];
            content += '\x01';
            content += b.id;
            content += '\x01';
            content += b.name;
            content += '\x01';
            content += b.image;
          } else {
            const auto &c = state.snapshot.shop_categories[i];
            content += '\x01';
            content += c.id;
            content += '\x01';
            content += c.name;
            content += '\x01';
            content += c.image;
          }
        }
        content += '\x02';
        content += state.shop_filter_mode ? state.shop_brand : state.shop_category;
        const size_t hash = std::hash<std::string>{}(content);
        if (hash != shop_filter_grid_hash) {
          shop_filter_grid_hash = hash;
          ++shop_filter_revision;
        }
        shop_filter_row_src.reserve(rows);
        for (size_t i = 0; i < rows; ++i) {
          const std::string &id = state.shop_filter_mode
                                      ? state.snapshot.shop_brands[i].id
                                      : state.snapshot.shop_categories[i].id;
          const bool active = id == (state.shop_filter_mode ? state.shop_brand
                                                              : state.shop_category);
          shop_filter_row_src.push_back("module:farming_simulator:shop_filter_row/" +
                                        std::to_string(i) + '/' + (active ? '1' : '0') +
                                        '/' + std::to_string(shop_filter_revision));
        }
      }
      // Guide: native "help-topics"/"help-article[-static]" ScrollRegions.
      // Row keys need no revision suffix at all (unlike Prices/Finance):
      // once the Guide is loaded from the player's romfs, a topic's title
      // never changes mid-session, so "active" (already part of the key,
      // like RenderShopRow's) is the only thing that ever varies.
      // topic_count (populated by ImageFor under render_mutex) rather than
      // view.Help() directly -- Sample() only holds `mutex`, not
      // render_mutex.
      help_topic_value = int64_t(state.help_topic);
      if (state.page == Page::Help) {
        const size_t topics = std::min(topic_count.load(), HelpTopicCap);
        help_topic_count = int64_t(topics);
        help_topic_row_src.reserve(topics);
        for (size_t i = 0; i < topics; ++i) {
          const bool active = i == state.help_topic;
          help_topic_row_src.push_back("module:farming_simulator:help_topic_row/" +
                                       std::to_string(i) + '/' + (active ? '1' : '0'));
        }
      }
      // help_article_h is only ever set after an actual RenderHelpArticle
      // call (ImageFor's "help_article/" case, under render_mutex) -- same
      // lag-one-tick pattern help_max used before this conversion.
      help_article_value = int64_t(help_article_h);
    }
    // Garage action row (UX-SPEC B) / map selection card action row (UX-SPEC C): the served
    // image keys are pure functions of the small ints computed above (RenderGarageActionRow/
    // RenderMapSelectionButtons, both cached forever per exact key -- see ImageFor).
    const std::string garage_action_src = garage_action_kind
                                              ? "module:farming_simulator:garage_action_row/" +
                                                    std::to_string(garage_action_kind)
                                              : std::string{};
    const std::string map_selection_buttons_src =
        map_selection_active && map_selection_kind
            ? "module:farming_simulator:map_selection_buttons/" +
                  std::to_string(map_selection_kind) + '/' +
                  (map_selection_place ? '1' : '0') + '/' +
                  (map_selection_tagged ? '1' : '0')
            : std::string{};
    if (h.publish_text) {
      h.publish_text(h.userdata, "fs.ui.frame", image.c_str());
      h.publish_text(h.userdata, "fs.ui.map_frame", map_image.c_str());
      // A constant key: the Reset button's picture never changes, so there is
      // no revision to mint here, unlike fs.ui.frame/fs.ui.map_frame above.
      h.publish_text(h.userdata, "fs.ui.reset_button",
                      "module:farming_simulator:reset_button");
      // Constant key, same convention as fs.ui.reset_button: the sprite
      // pixels never change, only the runtime's own rotate_bind below.
      h.publish_text(h.userdata, "fs.ui.map_arrow",
                      "module:farming_simulator:map_arrow");
      h.publish_text(h.userdata, "fs.ui.layer_error", layer_error.c_str());
      // Shop catalog grid rows, see shop_grid_src above. Named "src_<i>" so
      // the manifest's repeat image template's src_bind "fs.ui.shop_grid.src_{i}"
      // substitutes straight to each one.
      for (size_t i = 0; i < shop_grid_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.shop_grid.src_" + std::to_string(i)).c_str(),
                        shop_grid_src[i].c_str());
      }
      // Prices matrix ("fs-prices" ScrollRegion). header_src_<i>/
      // stations_src carry the same shared price_revision_value suffix as
      // every row_src_<i> (price_row_src above), so a price/crop/station
      // content change re-fetches all of them together. header_src is now
      // PriceColumnChunks separate chunk images (fs.ui.prices.header_src_<i>,
      // {i} = chunk index) instead of one over-wide image -- see
      // PriceColumnChunks' own comment.
      for (size_t i = 0; i < price_header_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.prices.header_src_" + std::to_string(i)).c_str(),
                        price_header_src[i].c_str());
      }
      h.publish_text(h.userdata, "fs.ui.prices.stations_src",
                      ("module:farming_simulator:price_stations/" +
                       std::to_string(price_revision_value)).c_str());
      for (size_t i = 0; i < price_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.prices.row_src_" + std::to_string(i)).c_str(),
                        price_row_src[i].c_str());
      }
      // Finance table ("fs-finance" ScrollRegion), same convention as
      // Prices immediately above: header_src/totals_src/labels_src and
      // every row_src_<i> carry the same shared finance_revision_value
      // suffix, so a finance content change re-fetches all of them together.
      h.publish_text(h.userdata, "fs.ui.finance.header_src",
                      ("module:farming_simulator:finance_header/" +
                       std::to_string(finance_revision_value)).c_str());
      h.publish_text(h.userdata, "fs.ui.finance.totals_src",
                      ("module:farming_simulator:finance_totals/" +
                       std::to_string(finance_revision_value)).c_str());
      for (size_t i = 0; i < finance_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.finance.row_src_" + std::to_string(i)).c_str(),
                        finance_row_src[i].c_str());
      }
      // The frozen row-label column: FinanceLabelChunks chunk images
      // (fs.ui.finance.labels_src_<i>), same chunking reason/convention as
      // Prices' header_src above (farming_simulator_view.h's
      // FinanceLabelChunks comment).
      for (size_t i = 0; i < finance_labels_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.finance.labels_src_" + std::to_string(i)).c_str(),
                        finance_labels_src[i].c_str());
      }
      // Statistics list ("statistic-rows" ScrollRegion), same convention as
      // Finance immediately above.
      for (size_t i = 0; i < stat_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.stats.row_src_" + std::to_string(i)).c_str(),
                        stat_row_src[i].c_str());
      }
      // Animal needs pane ("animal-needs" ScrollRegion), now served as
      // AnimalNeedsStripCap fixed-height strips (see AnimalNeedsStripCap's
      // own comment in farming_simulator_view.h) instead of one over-cap
      // image. The trailing revision only busts the runtime's own
      // module-image cache on a content change (animal_revision, see
      // Sample() above); it is never parsed in ImageFor, same convention as
      // price_row/finance_row/stat_row.
      for (size_t i = 0; i < AnimalNeedsStripCap; ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.animal.needs_strip_src_" + std::to_string(i)).c_str(),
                        ("module:farming_simulator:animal_needs/" + std::to_string(i) +
                         '/' + std::to_string(animal_needs_value)).c_str());
      }
      // Map legend ("map-legend" ScrollRegion), same convention as
      // Statistics above.
      for (size_t i = 0; i < legend_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.map_legend.row_src_" + std::to_string(i)).c_str(),
                        legend_row_src[i].c_str());
      }
      // Garage list/specs ("garage-list"/"garage-specs" ScrollRegions),
      // same convention as Prices/Finance above.
      for (size_t i = 0; i < garage_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.garage.row_src_" + std::to_string(i)).c_str(),
                        garage_row_src[i].c_str());
      }
      for (size_t i = 0; i < garage_spec_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.garage.spec_row_src_" + std::to_string(i)).c_str(),
                        garage_spec_row_src[i].c_str());
      }
      // Shop filter list ("shop-filters" ScrollRegion), same convention as
      // Statistics/map legend above.
      for (size_t i = 0; i < shop_filter_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.shop_filters.row_src_" + std::to_string(i)).c_str(),
                        shop_filter_row_src[i].c_str());
      }
      // Guide ("help-topics"/"help-article[-static]" ScrollRegions).
      // article_src carries the topic index and the live currency symbol
      // (for its $CURRENCY_SYMBOL substitution) directly in the key -- no
      // separate revision counter needed, since nothing else about an
      // article's content ever changes mid-session.
      h.publish_text(h.userdata, "fs.ui.help.article_src",
                      ("module:farming_simulator:help_article/" +
                       std::to_string(help_topic_value) + '/' +
                       state.snapshot.currency_symbol).c_str());
      for (size_t i = 0; i < help_topic_row_src.size(); ++i) {
        h.publish_text(h.userdata,
                        ("fs.ui.help.topic_row_src_" + std::to_string(i)).c_str(),
                        help_topic_row_src[i].c_str());
      }
      // Garage action row / map selection card action row (UX-SPEC B/C): empty when there is
      // nothing to show (garage_action_kind/map_selection_kind 0) -- the manifest's image
      // widget has no src to request then, same as any other hidden (need_bind false) image.
      h.publish_text(h.userdata, "fs.ui.garage.action_src", garage_action_src.c_str());
      h.publish_text(h.userdata, "fs.ui.map_selection.buttons_src",
                      map_selection_buttons_src.c_str());
    }
    if (h.publish_f64) {
      h.publish_f64(h.userdata, "fs.ui.map_home_zoom", map_home_zoom);
      h.publish_f64(h.userdata, "fs.ui.map_home_cx_compact", map_home_cx_compact);
      h.publish_f64(h.userdata, "fs.ui.map_home_cy_compact", map_home_cy_compact);
      h.publish_f64(h.userdata, "fs.ui.map_home_cx_expanded", map_home_cx_expanded);
      h.publish_f64(h.userdata, "fs.ui.map_home_cy_expanded", map_home_cy_expanded);
      h.publish_f64(h.userdata, "fs.ui.map_identity", double(map_identity_value));
      // Unthrottled, every Sample() tick (same cadence as map_home_cx/cy
      // above): the overlay arrow widget's rotate_bind, so it turns smoothly
      // while walking/driving instead of only on the map raster's ~5Hz
      // rebuild.
      h.publish_f64(h.userdata, "fs.ui.map_arrow_heading_deg", map_arrow_heading_deg);
      // Map marker tap-select (UX-SPEC C, now pan-tracked -- fix for the hit regions only
      // working at the default view): per-viewport screen offset from that viewport's rect
      // centre, x_bind/y_bind for gen_manifest.py's per-marker hit widgets. marker_dx/dy
      // (home-view-only, same for both viewports -- see the computation site's own comment)
      // stay the fallback published under the OLD shared key, kept for an older runtime that
      // never answers the pan_zoom view query and so never sets map_marker_pan_tracked_*; once
      // tracked, the per-viewport keys below carry the real, panned/zoomed-correct offset.
      for (size_t i = 0; i < marker_dx.size(); ++i) {
        h.publish_f64(h.userdata, ("fs.ui.map_marker_dx_" + std::to_string(i)).c_str(),
                      marker_dx[i]);
        h.publish_f64(h.userdata, ("fs.ui.map_marker_dy_" + std::to_string(i)).c_str(),
                      marker_dy[i]);
      }
      for (size_t i = 0; i < marker_dx_compact.size(); ++i) {
        h.publish_f64(h.userdata,
                      ("fs.ui.map_marker_dx_compact_" + std::to_string(i)).c_str(),
                      marker_dx_compact[i]);
        h.publish_f64(h.userdata,
                      ("fs.ui.map_marker_dy_compact_" + std::to_string(i)).c_str(),
                      marker_dy_compact[i]);
        h.publish_f64(h.userdata,
                      ("fs.ui.map_marker_dx_expanded_" + std::to_string(i)).c_str(),
                      marker_dx_expanded[i]);
        h.publish_f64(h.userdata,
                      ("fs.ui.map_marker_dy_expanded_" + std::to_string(i)).c_str(),
                      marker_dy_expanded[i]);
      }
      // ArrowPanOffset's x_bind/y_bind offsets (see the Sample() block
      // above and gen_manifest.py's map-arrow-{compact,expanded} widgets):
      // 0 (no-op offset) on a runtime that doesn't answer the pan_zoom view
      // query, which keeps the arrow at its authored, centred rect.
      h.publish_f64(h.userdata, "fs.ui.map_arrow_dx_compact", map_arrow_dx_compact);
      h.publish_f64(h.userdata, "fs.ui.map_arrow_dy_compact", map_arrow_dy_compact);
      h.publish_f64(h.userdata, "fs.ui.map_arrow_dx_expanded", map_arrow_dx_expanded);
      h.publish_f64(h.userdata, "fs.ui.map_arrow_dy_expanded", map_arrow_dy_expanded);
    }
    if (h.publish_i64) {
      // Shop catalog grid ScrollRegion binds (count_bind/reset_bind).
      h.publish_i64(h.userdata, "fs.ui.shop_grid.count", shop_grid_count);
      h.publish_i64(h.userdata, "fs.ui.shop_grid.identity", shop_grid_identity);
      // Prices matrix ScrollRegion binds: count_bind (station rows),
      // content_w_bind (crop columns' content width, px) and reset_bind
      // (price_identity_value, bumped on a world change -- see the
      // world_identity block above). row_cells is the row/chunk repeat
      // template's own element count (station rows * PriceColumnChunks column
      // chunks, row-major) -- NOT the same as count (station rows alone),
      // needed separately because the repeat template's cells and the
      // ScrollRegion's own row_h-based scroll range are no longer 1:1 (see
      // PriceColumnChunks' own comment).
      h.publish_i64(h.userdata, "fs.ui.prices.count", price_grid_count);
      h.publish_i64(h.userdata, "fs.ui.prices.row_cells",
                    price_grid_count * int64_t(PriceColumnChunks));
      h.publish_i64(h.userdata, "fs.ui.prices.w", price_grid_width);
      h.publish_i64(h.userdata, "fs.ui.prices.identity", price_identity_value);
      // Finance table ScrollRegion binds: count_bind (rows) and reset_bind
      // (finance_identity_value). The day axis has no bind at all -- the
      // manifest's "fs-finance" region sets content_w to the FinanceContentW
      // constant directly (always exactly 5 days).
      h.publish_i64(h.userdata, "fs.ui.finance.count", finance_grid_count);
      h.publish_i64(h.userdata, "fs.ui.finance.identity", finance_identity_value);
      // Statistics list ScrollRegion binds: count_bind (rows) and
      // reset_bind (stat_identity_value, bumped on a world change -- see
      // the world_identity block above).
      h.publish_i64(h.userdata, "fs.ui.stats.count", stat_grid_count);
      h.publish_i64(h.userdata, "fs.ui.stats.identity", stat_identity_value);
      // Animal needs pane ScrollRegion binds: content_h_bind (the measured
      // content height, lag-one-tick from the last RenderAnimalNeeds call
      // in ImageFor, same pattern help_article_h/fs.ui.help.article_h
      // uses) and reset_bind (animal_identity_value).
      h.publish_i64(h.userdata, "fs.ui.animal.needs_h", int64_t(animal_needs_h));
      h.publish_i64(h.userdata, "fs.ui.animal_identity", animal_identity_value);
      // Map legend ScrollRegion binds: count_bind (rows) and reset_bind
      // (legend_identity_value).
      h.publish_i64(h.userdata, "fs.ui.map_legend.count", legend_grid_count);
      h.publish_i64(h.userdata, "fs.ui.map_legend_identity", legend_identity_value);
      // Garage list/specs ScrollRegion binds: count_bind (rows) and
      // reset_bind (garage_identity_value bumped on a world change;
      // garage_spec_identity_value hashed from vehicle_selected/
      // world_identity, so picking a different vehicle also resets its
      // specs list).
      h.publish_i64(h.userdata, "fs.ui.garage.count", garage_grid_count);
      h.publish_i64(h.userdata, "fs.ui.garage.identity", garage_identity_value);
      h.publish_i64(h.userdata, "fs.ui.garage.spec_count", garage_spec_count);
      h.publish_i64(h.userdata, "fs.ui.garage_spec_identity", garage_spec_identity_value);
      // Garage action row (UX-SPEC B): gen_manifest.py's image/hit widgets gate on
      // fs.ui.garage.action_kind directly (derived exprs compare it to 1/2/3); the row
      // collapses to nothing when it is 0 (no vehicle selected, or neither action available).
      h.publish_i64(h.userdata, "fs.ui.garage.action_kind", garage_action_kind);
      // Map selection card (UX-SPEC C): fs.ui.map_selection.active gates the card's whole
      // action row (both the served button image and its hit regions); kind/place/tagged pick
      // which of gen_manifest.py's six "both/left-only/right-only x vehicle/place" derived
      // gates fires, same convention as garage.action_kind above.
      h.publish_i64(h.userdata, "fs.ui.map_selection.active", map_selection_active);
      h.publish_i64(h.userdata, "fs.ui.map_selection.kind", map_selection_kind);
      h.publish_i64(h.userdata, "fs.ui.map_selection.place", map_selection_place);
      // map_selection_tagged itself is still read above (map_selection_buttons_src):
      // only this standalone publish was dead -- no manifest widget reads
      // fs.ui.map_selection.tagged directly, the flag is already baked into that key.
      // Map marker tap-select (UX-SPEC C): per-marker "is this a selectable (discovered,
      // Enter/Reset- or Visit/Tag-eligible) marker" flag -- gen_manifest.py's derived section
      // ANDs this with the per-viewport "still at the default view" gate (the module cannot
      // compute that part itself; see the dx/dy computation's own comment) into the actual
      // need_bind each per-marker hit widget uses.
      for (size_t i = 0; i < marker_selectable.size(); ++i) {
        h.publish_i64(h.userdata, ("fs.ui.map_marker_selectable_" + std::to_string(i)).c_str(),
                      marker_selectable[i]);
      }
      // Shop filter list ScrollRegion binds: count_bind (rows) and
      // reset_bind (shop_filter_identity_value, hashed from
      // shop_filter_mode/world_identity -- switching mode or a world
      // change both reset it).
      h.publish_i64(h.userdata, "fs.ui.shop_filters.count", shop_filter_count);
      h.publish_i64(h.userdata, "fs.ui.shop_filters_identity", shop_filter_identity_value);
      // Guide ScrollRegion binds: "help-topics" count_bind (topic rows, no
      // reset_bind -- the list never changes mid-session); "help-article"/
      // "help-article-static" share content_h_bind (help_article_value,
      // the article's measured content height) and reset_bind
      // (help_topic_value, bumped whenever the selected topic changes --
      // see OnViewAction's "help_topic" case).
      h.publish_i64(h.userdata, "fs.ui.help.topic_count", help_topic_count);
      h.publish_i64(h.userdata, "fs.ui.help.article_h", help_article_value);
      h.publish_i64(h.userdata, "fs.ui.help_topic", help_topic_value);
      // fs.ui.time_actions/fs.ui.time_closed_unavailable/fs.ui.map_ready/
      // fs.ui.layers_ready dropped: no gen_manifest.py widget ever read
      // any of the four (their backing time_open_actions/
      // time_closed_unavailable/layers_ready counters, which existed only
      // to feed these publishes, are removed below too; `map` itself stays
      // -- it still gates the map image minting block above).
      h.publish_i64(h.userdata, "fs.ui.map_fullscreen", map_fullscreen);
      h.publish_i64(h.userdata, "fs.ui.map_arrow_visible", map_arrow_visible);
      h.publish_i64(h.userdata, "fs.ui.map_arrow_pan_tracked_compact",
                    map_arrow_pan_tracked_compact);
      h.publish_i64(h.userdata, "fs.ui.map_arrow_pan_tracked_expanded",
                    map_arrow_pan_tracked_expanded);
      h.publish_i64(h.userdata, "fs.ui.map_compact", map && page == Page::Operations && !map_fullscreen && !time_picker);
      h.publish_i64(h.userdata, "fs.ui.map_expanded", map && page == Page::Operations && map_fullscreen && !time_picker);
      // The map picture itself stays visible under the time-scale picker
      // (map_compact/map_expanded above also gate the map controls, which the
      // picker replaces); without this the map area showed blank.
      h.publish_i64(h.userdata, "fs.ui.map_view_compact", map && page == Page::Operations && !map_fullscreen);
      h.publish_i64(h.userdata, "fs.ui.map_view_expanded", map && page == Page::Operations && map_fullscreen);
      // Whether the map is expanded on the Operations page: the footer tabs
      // hide then (View::Render's footer_visible), so this also gates the
      // footer's own hit regions in the manifest.
      h.publish_i64(h.userdata, "fs.ui.footer_visible",
                    !(map_fullscreen && page == Page::Operations) && world);
      h.publish_i64(h.userdata, "fs.ui.static_help",
                    !world && page == Page::Help);
      // "help-article" ScrollRegion's own show_bind (gen_manifest.py):
      // fs.ui.page.8 alone is true for BOTH the static (title/no-farm) and
      // in-world Guide, since the old swipe()'s hide_bind/need_bind split
      // on fs.ui.static_help has no scroll-region equivalent -- a region
      // needs its own exclusive show_bind, not a hide_bind layered on a
      // wider one.
      h.publish_i64(h.userdata, "fs.ui.help_dynamic",
                    world && page == Page::Help);
      h.publish_i64(h.userdata, "fs.ui.help_landing",
                    title && !world && page != Page::Help);
      h.publish_i64(h.userdata, "fs.ui.map_active",
                    map && page == Page::Operations && !time_picker);
      // FILTERS button (UX-SPEC §A.2): Map-tab only, unlike Expand/the time
      // badge it sits beside it stays tappable even while the time-scale
      // picker is open (tapping it then closes the picker and opens
      // Filters instead -- see OnViewAction's own "last tap wins" handling).
      h.publish_i64(h.userdata, "fs.ui.map_filters_active",
                    map && page == Page::Operations);
      // FILTERS popup: compact replaces the right pane (fs.ui.map_filters_pane);
      // expanded floats as a card (fs.ui.map_filters_card), same split as
      // fs.ui.time_picker_map/fs.ui.time_picker_panel above.
      h.publish_i64(h.userdata, "fs.ui.map_filters_pane",
                    map_filters_open && page == Page::Operations && !map_fullscreen);
      h.publish_i64(h.userdata, "fs.ui.map_filters_card",
                    map_filters_open && page == Page::Operations && map_fullscreen);
      h.publish_i64(h.userdata, "fs.ui.time_picker", time_picker);
      // Split by on-screen picker layout: the Map page replaces its own
      // right pane; every other eligible page gets the floating top-right
      // card. Touch regions are gated on whichever one is actually drawn.
      h.publish_i64(h.userdata, "fs.ui.time_picker_map",
                    time_picker && page == Page::Operations);
      h.publish_i64(h.userdata, "fs.ui.time_picker_panel",
                    time_picker && page != Page::Operations);
      h.publish_i64(h.userdata, "fs.ui.time_badge", time_badge);
      h.publish_i64(h.userdata, "fs.ui.time_available", time_available);
      // Published in the same Sample() that mints the revision showing
      // whatever changed, so the src_fade_bind value the runtime reads for
      // that revision's swap already reflects it.
      h.publish_i64(h.userdata, "fs.ui.transition", transition_value);
      h.publish_i64(h.userdata, "fs.ui.layer_active", layer_active);
      h.publish_i64(h.userdata, "fs.ui.map_legend", map_legend && !map_fullscreen);
      h.publish_i64(h.userdata, "fs.ui.art_ready", art_ready.load());
      h.publish_i64(h.userdata, "fs.ui.shop_browse",
                    world && page == Page::Shop && !shop_details);
      h.publish_i64(h.userdata, "fs.ui.shop_details",
                    world && page == Page::Shop && shop_details);
      h.publish_i64(h.userdata, "fs.ui.animal_feeding",
                    world && page == Page::Animals && animal_feeding);
      for (int i = 0; i <= int(Page::Help); ++i) {
        auto key = "fs.ui.page." + std::to_string(i);
        h.publish_i64(h.userdata, key.c_str(),
                      int(page) == i && (world || page == Page::Help));
      }
    }
  }
  bool Action(const char *action, int64_t argument) {
    if (!action)
      return false;
    std::lock_guard lock{mutex};
#ifdef FARMING_SIMULATOR_HAS_READER
    if (std::string_view{action} == "time_scale") {
      if (!state.time_scale_open || state.page == Page::Animals ||
          !state.snapshot.time_scale_change_allowed ||
          !reader.OnAction(action, argument))
        return false;
      state.time_scale_open = false;
      next_frame = {};
      return true;
    }
    // Instant game calls (farming_simulator_game_calls.h). garage_enter / garage_reset act on
    // either the selected Garage vehicle (state.vehicle_selected, UX-SPEC B) or a tapped map
    // vehicle marker resolved to the same VehicleItem list by root_node (UX-SPEC C's "same
    // underlying action, same capability flags" as Garage). map_visit / map_tag act on a
    // tapped map place marker (state.map_marker_selected, UX-SPEC C). The reader refuses
    // unless the load plan's helper is alive, no call is in flight and the target is eligible.
    if (std::string_view{action} == "garage_enter" || std::string_view{action} == "garage_reset") {
      const auto &vehicles = state.snapshot.owned_vehicles;
      const VehicleItem *v = nullptr;
      if (state.page == Page::Vehicles) {
        // SelectedVehicle(state) -- not a raw state.vehicle_selected lookup: the Garage
        // detail pane and this action row both fall back to owned_vehicles[0] when nothing
        // has been explicitly tapped yet (farming_simulator_shop_views.cpp's own "no
        // selection yet" comment), so the row-0 Enter/Reset buttons shown on first opening
        // Garage must act on that same fallback vehicle instead of silently refusing (a
        // raw x.id == state.vehicle_selected lookup finds nothing when vehicle_selected is
        // still the default empty string).
        v = SelectedVehicle(state);
      } else if (state.page == Page::Operations && state.map_marker_selected >= 0 &&
                 std::string_view{action} == "garage_enter") {
        // Reset is Garage-only; never reset a vehicle from the map card.
        const auto &markers = state.snapshot.map_markers;
        if (size_t(state.map_marker_selected) < markers.size()) {
          const auto &m = markers[size_t(state.map_marker_selected)];
          auto it = std::find_if(vehicles.begin(), vehicles.end(),
                                 [&](const auto &x) { return x.root_node == m.object_id; });
          if (it != vehicles.end())
            v = &*it;
        }
      }
      if (!v)
        return false;
      return reader.OnAction(action, int64_t(v->network_id));
    }
    // map_marker_pick (pure UI selection, no game call) is handled by OnViewAction below, the
    // same reader-independent, testable path "vehicle_pick"/"shop_item" already use -- see its
    // own comment there.
    if (std::string_view{action} == "map_visit" || std::string_view{action} == "map_tag") {
      if (state.page != Page::Operations || state.map_marker_selected < 0)
        return false;
      const auto &markers = state.snapshot.map_markers;
      if (size_t(state.map_marker_selected) >= markers.size())
        return false;
      return reader.RequestPlaceCall(std::string_view{action} == "map_tag",
                                     markers[size_t(state.map_marker_selected)]);
    }
#endif
    const std::string_view verb{action};
    const bool scrolling = verb.starts_with("scroll:");
    const bool wheel = WheelAction(verb);
    const auto visible_scroll=ScrollValues(state);
    if(wheel && wheel_animating) SetScrollValues(state,wheel_to);
    if(!wheel) wheel_animating=false;
    // help_max/animal_max dropped from the OnScrollAction/OnViewAction
    // calls (0 in their place): the Guide article and the Animal needs
    // pane have no module-tracked max offset left (native "help-article"/
    // "animal-needs" ScrollRegions, content_h_bind).
    auto result = scrolling ? OnScrollAction(state, verb, argument, topic_count.load(), 0, 0)
                            : OnViewAction(state, verb, argument, topic_count.load(), 0);
    if(wheel) {
      if(result) {
        wheel_from=visible_scroll;wheel_to=ScrollValues(state);
        wheel_started=std::chrono::steady_clock::now();
        wheel_animating=wheel_from!=wheel_to;
      }
      SetScrollValues(state,visible_scroll);
    }
    if (result) {
      // Map drag/pinch never reach here: they are the runtime's own native
      // pan_zoom gesture handling now (see farming_simulator_view.h), not a
      // module action, so they cannot land on this list.
      if (scrolling || wheel)
        interacting_until = std::chrono::steady_clock::now()+std::chrono::milliseconds{250};
      next_frame = {};
    }
    return result;
  }
  // Shared by ImageFor() for both the "frame/<revision>" and
  // "map/<revision>" keys: parses the trailing revision number.
  static bool ParseRevision(std::string_view key, std::string_view prefix, uint64_t &out) {
    if (!key.starts_with(prefix))
      return false;
    auto number = key.substr(prefix.size());
    auto parsed = std::from_chars(number.data(), number.data() + number.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size();
  }
  bool ImageFor(std::string_view key, void *receiver, EdenDsmodImageSink sink) {
    if (!sink)
      return false;
    constexpr std::string_view reset_key = "module:farming_simulator:reset_button";
    if (key == reset_key) {
      std::lock_guard rendering{render_mutex};
      if (!reset_image_rendered_) {
        if (!view.RenderResetButton(render_reset_image_))
          return false;
        reset_image_rendered_ = true;
      }
      sink(receiver, render_reset_image_.w, render_reset_image_.h,
           render_reset_image_.rgba.data(), render_reset_image_.rgba.size());
      return true;
    }
    constexpr std::string_view map_arrow_key = "module:farming_simulator:map_arrow";
    if (key == map_arrow_key) {
      std::lock_guard rendering{render_mutex};
      if (!map_arrow_image_rendered_) {
        if (!view.RenderMapArrow(render_map_arrow_image_))
          return false;
        map_arrow_image_rendered_ = true;
      }
      sink(receiver, render_map_arrow_image_.w, render_map_arrow_image_.h,
           render_map_arrow_image_.rgba.data(), render_map_arrow_image_.rgba.size());
      return true;
    }
    // Garage action row (UX-SPEC B), "module:farming_simulator:garage_action_row/<kind>" --
    // see garage_action_src (Sample()) and View::RenderGarageActionRow. A small, finite set of
    // constant keys (kind 1..3 only), cached forever the same way reset_button/map_arrow are
    // above, just keyed generically by the full ImageFor key (action_image_cache_) instead of
    // one dedicated member per picture.
    constexpr std::string_view garage_action_prefix = "module:farming_simulator:garage_action_row/";
    if (key.starts_with(garage_action_prefix)) {
      auto num = key.substr(garage_action_prefix.size());
      int kind = 0;
      auto parsed = std::from_chars(num.data(), num.data() + num.size(), kind);
      if (parsed.ec != std::errc{} || parsed.ptr != num.data() + num.size())
        return false;
      std::lock_guard rendering{render_mutex};
      auto it = action_image_cache_.find(std::string{key});
      if (it == action_image_cache_.end()) {
        Image img;
        if (!view.RenderGarageActionRow(kind, img))
          return false;
        it = action_image_cache_.emplace(std::string{key}, std::move(img)).first;
      }
      sink(receiver, it->second.w, it->second.h, it->second.rgba.data(), it->second.rgba.size());
      return true;
    }
    // Map selection card action row (UX-SPEC C), "module:farming_simulator:
    // map_selection_buttons/<kind>/<place 0|1>/<tagged 0|1>" -- see map_selection_buttons_src
    // (Sample()) and View::RenderMapSelectionButtons. Same cache-forever-by-key convention as
    // garage_action_row immediately above (kind x place x tagged is a small, finite set).
    constexpr std::string_view map_selection_prefix =
        "module:farming_simulator:map_selection_buttons/";
    if (key.starts_with(map_selection_prefix)) {
      auto rest = key.substr(map_selection_prefix.size());
      auto slash1 = rest.find('/');
      if (slash1 == std::string_view::npos)
        return false;
      int kind = 0;
      auto kind_text = rest.substr(0, slash1);
      auto parsed = std::from_chars(kind_text.data(), kind_text.data() + kind_text.size(), kind);
      if (parsed.ec != std::errc{} || parsed.ptr != kind_text.data() + kind_text.size())
        return false;
      auto rest2 = rest.substr(slash1 + 1);
      auto slash2 = rest2.find('/');
      if (slash2 == std::string_view::npos)
        return false;
      const bool place = rest2.substr(0, slash2) == "1";
      const bool tagged = rest2.substr(slash2 + 1) == "1";
      std::lock_guard rendering{render_mutex};
      auto it = action_image_cache_.find(std::string{key});
      if (it == action_image_cache_.end()) {
        Image img;
        if (!view.RenderMapSelectionButtons(kind, place, tagged, img))
          return false;
        it = action_image_cache_.emplace(std::string{key}, std::move(img)).first;
      }
      sink(receiver, it->second.w, it->second.h, it->second.rgba.data(), it->second.rgba.size());
      return true;
    }
    constexpr std::string_view map_prefix = "module:farming_simulator:map/";
    uint64_t map_wanted = 0;
    if (ParseRevision(key, map_prefix, map_wanted)) {
      ViewState copy;
      bool found = false;
      {
        std::lock_guard lock{mutex};
        for (const auto &[rev, value] : retained_map)
          if (rev == map_wanted) {
            copy = value;
            found = true;
            break;
          }
      }
      if (!found)
        return false;
      std::lock_guard rendering{render_mutex};
      if (!view.RenderMapBase(copy, render_map_image_))
        return false;
      sink(receiver, render_map_image_.w, render_map_image_.h,
           render_map_image_.rgba.data(), render_map_image_.rgba.size());
      std::lock_guard lock{mutex};
      served_map_revision = std::max(served_map_revision, map_wanted);
      return true;
    }
    // Shop catalog grid row card, "module:farming_simulator:shop_row/<master
    // index>/<active 0|1>" -- see shop_grid_src (Sample()) and
    // View::RenderShopRow. Rendered fresh on every request rather than
    // cached here: a single 412x372 card is cheap (one Art blit, a couple of
    // Fills, two short text runs), and the runtime's own module-image cache
    // already keeps the decoded result past that, so a second module-side
    // cache would only add bookkeeping for no measurable win.
    constexpr std::string_view shop_row_prefix = "module:farming_simulator:shop_row/";
    if (key.starts_with(shop_row_prefix)) {
      auto rest = key.substr(shop_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t master_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), master_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      const bool active = rest.substr(slash + 1) == "1";
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderShopRow(copy, master_index, active, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Prices matrix ("fs-prices" ScrollRegion). The trailing "/<revision>"
    // on every one of these keys only busts the runtime's own module-image
    // cache on a content change (price_revision, see Sample()) -- it is
    // never parsed here: like RenderShopRow, each is rendered fresh from the
    // current state on every request rather than cached in the module, and
    // that current state is always what price_revision's own hash already
    // settled on, so there is nothing the number itself would add.
    constexpr std::string_view price_header_prefix = "module:farming_simulator:price_header/";
    constexpr std::string_view price_stations_prefix = "module:farming_simulator:price_stations/";
    constexpr std::string_view price_row_prefix = "module:farming_simulator:price_row/";
    // Parses one leading "<N>/" off `rest`, advancing it past the slash;
    // false on a malformed/missing number (caller bails the whole request).
    const auto take_index = [](std::string_view &rest, size_t &out) {
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      auto text = rest.substr(0, slash);
      auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return false;
      rest.remove_prefix(slash + 1);
      return true;
    };
    if (key.starts_with(price_header_prefix) || key.starts_with(price_stations_prefix)) {
      const bool header = key.starts_with(price_header_prefix);
      size_t chunk_index = 0;
      if (header) {
        // "module:farming_simulator:price_header/<chunk>/<revision>" -- the
        // trailing revision busts the runtime's own module-image cache on a
        // content change (never parsed here, same as price_row below).
        auto rest = key.substr(price_header_prefix.size());
        if (!take_index(rest, chunk_index))
          return false;
      }
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image pane;
      const bool ok = header ? view.RenderPriceHeader(copy, chunk_index, pane)
                             : view.RenderPriceStations(copy, pane);
      if (!ok)
        return false;
      sink(receiver, pane.w, pane.h, pane.rgba.data(), pane.rgba.size());
      return true;
    }
    if (key.starts_with(price_row_prefix)) {
      // "module:farming_simulator:price_row/<station>/<chunk>/<revision>" --
      // the trailing revision is never parsed (see the prefix block's own
      // comment above).
      auto rest = key.substr(price_row_prefix.size());
      size_t station_index = 0, chunk_index = 0;
      if (!take_index(rest, station_index) || !take_index(rest, chunk_index))
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderPriceRow(copy, station_index, chunk_index, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Finance table ("fs-finance" ScrollRegion), same convention as the
    // Prices block immediately above (trailing "/<revision>" only busts the
    // runtime's module-image cache; never parsed).
    constexpr std::string_view finance_header_prefix = "module:farming_simulator:finance_header/";
    constexpr std::string_view finance_labels_prefix = "module:farming_simulator:finance_labels/";
    constexpr std::string_view finance_totals_prefix = "module:farming_simulator:finance_totals/";
    constexpr std::string_view finance_row_prefix = "module:farming_simulator:finance_row/";
    if (key.starts_with(finance_header_prefix) || key.starts_with(finance_labels_prefix) ||
        key.starts_with(finance_totals_prefix)) {
      const bool labels = key.starts_with(finance_labels_prefix);
      size_t chunk_index = 0;
      if (labels) {
        // "module:farming_simulator:finance_labels/<chunk>/<revision>" --
        // the trailing revision is never parsed (see the Prices block's own
        // comment above).
        auto rest = key.substr(finance_labels_prefix.size());
        if (!take_index(rest, chunk_index))
          return false;
      }
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image pane;
      const bool ok = key.starts_with(finance_header_prefix) ? view.RenderFinanceHeader(copy, pane)
                      : labels ? view.RenderFinanceLabels(copy, chunk_index, pane)
                                                               : view.RenderFinanceTotals(copy, pane);
      if (!ok)
        return false;
      sink(receiver, pane.w, pane.h, pane.rgba.data(), pane.rgba.size());
      return true;
    }
    if (key.starts_with(finance_row_prefix)) {
      auto rest = key.substr(finance_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t row_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), row_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderFinanceRow(copy, row_index, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Statistics row ("statistic-rows" ScrollRegion), "module:
    // farming_simulator:stat_row/<row index>/<revision>" -- see stat_row_src
    // (Sample()) and View::RenderStatisticRow. The trailing revision only
    // busts the runtime's own module-image cache (stat_revision, see
    // Sample()); it is never parsed here, same convention as price_row/
    // finance_row above.
    constexpr std::string_view stat_row_prefix = "module:farming_simulator:stat_row/";
    if (key.starts_with(stat_row_prefix)) {
      auto rest = key.substr(stat_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t row_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), row_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderStatisticRow(copy, row_index, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Shop filter row ("shop-filters" ScrollRegion), "module:
    // farming_simulator:shop_filter_row/<index>/<active 0|1>/<revision>" --
    // see shop_filter_row_src (Sample()) and View::RenderShopFilterRow. The
    // trailing revision only busts the runtime's own module-image cache
    // (shop_filter_revision, see Sample()); it is never parsed here.
    constexpr std::string_view shop_filter_row_prefix =
        "module:farming_simulator:shop_filter_row/";
    if (key.starts_with(shop_filter_row_prefix)) {
      auto rest = key.substr(shop_filter_row_prefix.size());
      auto slash1 = rest.find('/');
      if (slash1 == std::string_view::npos)
        return false;
      size_t index = 0;
      auto index_text = rest.substr(0, slash1);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      auto rest2 = rest.substr(slash1 + 1);
      auto slash2 = rest2.find('/');
      if (slash2 == std::string_view::npos)
        return false;
      const bool active = rest2.substr(0, slash2) == "1";
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderShopFilterRow(copy, index, active, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Garage list row ("garage-list" ScrollRegion), "module:
    // farming_simulator:garage_row/<index>/<active 0|1>/<revision>" -- see
    // garage_row_src (Sample()) and View::RenderGarageRow. The trailing
    // revision only busts the runtime's own module-image cache
    // (garage_revision, see Sample()); it is never parsed here.
    constexpr std::string_view garage_row_prefix = "module:farming_simulator:garage_row/";
    if (key.starts_with(garage_row_prefix)) {
      auto rest = key.substr(garage_row_prefix.size());
      auto slash1 = rest.find('/');
      if (slash1 == std::string_view::npos)
        return false;
      size_t index = 0;
      auto index_text = rest.substr(0, slash1);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      auto rest2 = rest.substr(slash1 + 1);
      auto slash2 = rest2.find('/');
      if (slash2 == std::string_view::npos)
        return false;
      const bool active = rest2.substr(0, slash2) == "1";
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderGarageRow(copy, index, active, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Garage specs row ("garage-specs" ScrollRegion), "module:
    // farming_simulator:garage_spec_row/<spec index>/<revision>" -- see
    // garage_spec_row_src (Sample()) and View::RenderGarageSpecRow. The
    // trailing revision only busts the runtime's own module-image cache
    // (garage_spec_revision, see Sample()); it is never parsed here.
    constexpr std::string_view garage_spec_row_prefix =
        "module:farming_simulator:garage_spec_row/";
    if (key.starts_with(garage_spec_row_prefix)) {
      auto rest = key.substr(garage_spec_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t spec_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), spec_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderGarageSpecRow(copy, spec_index, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Map legend row ("map-legend" ScrollRegion), "module:
    // farming_simulator:map_legend_row/<row index>/<revision>" -- see
    // legend_row_src (Sample()) and View::RenderMapLegendRow. The trailing
    // revision only busts the runtime's own module-image cache
    // (legend_revision, see Sample()); it is never parsed here, same
    // convention as stat_row above. The legend list (crop/growth/soil) is
    // selected by the state's own current map_layer, matching
    // View::Operations' own switch.
    constexpr std::string_view map_legend_row_prefix =
        "module:farming_simulator:map_legend_row/";
    if (key.starts_with(map_legend_row_prefix)) {
      auto rest = key.substr(map_legend_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t row_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), row_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderMapLegendRow(copy, copy.map_layer, row_index, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Animal needs pane ("animal-needs" ScrollRegion), "module:
    // farming_simulator:animal_needs/<strip>/<revision>" -- see Sample()'s
    // fs.ui.animal.needs_strip_src_<i>. A single AnimalNeedsCap(16000)-tall
    // image was always over the runtime's 4096px limit and 16MB
    // MaxImageBytes (see AnimalNeedsStripCap's own comment in
    // farming_simulator_view.h), so the pane is served as fixed-height
    // strips now; <strip> is parsed (unlike the trailing revision, which
    // only busts the runtime's own module-image cache on a content change
    // and is never parsed). animal_needs_h is updated here (under `mutex`,
    // like help_article_h above) so the next Sample() tick publishes the
    // real measured TOTAL content height for content_h_bind, independent of
    // which strip was requested.
    constexpr std::string_view animal_needs_prefix =
        "module:farming_simulator:animal_needs/";
    if (key.starts_with(animal_needs_prefix)) {
      auto rest = key.substr(animal_needs_prefix.size());
      size_t strip_index = 0;
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), strip_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image needs;
      int content_h = 0;
      if (!view.RenderAnimalNeedsStrip(copy, strip_index, needs, &content_h))
        return false;
      {
        std::lock_guard lock{mutex};
        animal_needs_h = size_t(content_h);
      }
      sink(receiver, needs.w, needs.h, needs.rgba.data(), needs.rgba.size());
      return true;
    }
    // Guide topic row ("help-topics" ScrollRegion), "module:farming_simulator:
    // help_topic_row/<index>/<active 0|1>" -- see help_topic_row_src
    // (Sample()) and View::RenderHelpTopicRow. Rendered fresh on every
    // request, same convention as RenderShopRow (a 352xHelpTopicRowHeight
    // cell is cheap, and the runtime's own module-image cache already keeps
    // the decoded result past that).
    constexpr std::string_view help_topic_row_prefix =
        "module:farming_simulator:help_topic_row/";
    if (key.starts_with(help_topic_row_prefix)) {
      auto rest = key.substr(help_topic_row_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      const bool active = rest.substr(slash + 1) == "1";
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image row;
      if (!view.RenderHelpTopicRow(copy, index, active, row))
        return false;
      sink(receiver, row.w, row.h, row.rgba.data(), row.rgba.size());
      return true;
    }
    // Guide article body ("help-article"/"help-article-static" ScrollRegions
    // share the same image), "module:farming_simulator:help_article/<topic
    // index>/<currency symbol>" -- see Sample()'s fs.ui.help.article_src.
    // The currency symbol only busts the runtime's own module-image cache
    // on a change (RenderHelpArticle re-substitutes $CURRENCY_SYMBOL fresh
    // from `copy` either way); it is never parsed here. help_article_h is
    // updated here (under `mutex`, like animal_max below) so the next
    // Sample() tick publishes the real measured content height for
    // content_h_bind -- the same lag-one-tick pattern help_max used before
    // this conversion.
    constexpr std::string_view help_article_prefix =
        "module:farming_simulator:help_article/";
    if (key.starts_with(help_article_prefix)) {
      auto rest = key.substr(help_article_prefix.size());
      auto slash = rest.find('/');
      if (slash == std::string_view::npos)
        return false;
      size_t topic_index = 0;
      auto index_text = rest.substr(0, slash);
      auto parsed = std::from_chars(index_text.data(),
                                    index_text.data() + index_text.size(), topic_index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != index_text.data() + index_text.size())
        return false;
      ViewState copy;
      {
        std::lock_guard lock{mutex};
        copy = state;
      }
      std::lock_guard rendering{render_mutex};
      Image article;
      int content_h = 0;
      if (!view.RenderHelpArticle(copy, topic_index, article, &content_h))
        return false;
      {
        std::lock_guard lock{mutex};
        help_article_h = size_t(content_h);
        rendered_help_topic = topic_index;
      }
      sink(receiver, article.w, article.h, article.rgba.data(), article.rgba.size());
      return true;
    }
    constexpr std::string_view prefix = "module:farming_simulator:frame/";
    uint64_t wanted = 0;
    if (!ParseRevision(key, prefix, wanted))
      return false;
    ViewState copy;
    bool found = false;
    {
      std::lock_guard lock{mutex};
      for (const auto &[rev, value] : retained)
        if (rev == wanted) {
          copy = value;
          found = true;
          break;
        }
    }
    if (!found)
      return false;
    std::lock_guard rendering{render_mutex};
    if (!view.Render(copy, render_image_))
      return false;
    topic_count.store(view.Help().size());
    art_ready.store(true);
    sink(receiver, render_image_.w, render_image_.h, render_image_.rgba.data(),
         render_image_.rgba.size());
    {
      // Record the highest revision actually served, so Sample() can pace
      // routine minting to decode/delivery (see served_revision above).
      std::lock_guard lock{mutex};
      served_revision = std::max(served_revision, wanted);
    }
    return true;
  }
};
void *Create(const EdenDsmodHostApi *h, const char *config) {
  try {
    if (!dsmod_sdk::HostAbiMatches(h) || h->title_id != TitleId ||
        !h->read_romfs)
      return nullptr;
    std::string id;
    for (auto b : h->build_id) {
      constexpr char hex[] = "0123456789ABCDEF";
      id += hex[b >> 4];
      id += hex[b & 15];
    }
    if (!Supports(id.c_str()))
      return nullptr;
    return new Module{*h, config};
  } catch (...) {
    return nullptr;
  }
}
void Destroy(void *p) { delete static_cast<Module *>(p); }
void Sample(void *p, const EdenDsmodHostApi *h) {
  try {
    if (p && h)
      static_cast<Module *>(p)->Sample(*h);
  } catch (...) {
    if (h && h->log)
      h->log(h->userdata, EDEN_DSMOD_LOG_ERROR,
             "Farming Simulator presentation sample failed");
  }
}
void Tick(void *, const EdenDsmodHostApi *) {}
void Configure(void *p, const EdenDsmodHostExtensions *ext) {
#ifdef FARMING_SIMULATOR_HAS_READER
  if (p) static_cast<Module *>(p)->reader.ConfigureCalls(ext);
#else
  (void)p; (void)ext;
#endif
}
void ConfigureWrites(void *p, const EdenDsmodHostWriteApi *host) {
#ifdef FARMING_SIMULATOR_HAS_READER
  if (p) static_cast<Module *>(p)->reader.ConfigureWrites(host);
#else
  (void)p; (void)host;
#endif
}
EdenDsmodBool Action(void *p, const char *action, int64_t argument) {
  try {
    return p && static_cast<Module *>(p)->Action(action, argument)
               ? EDEN_DSMOD_TRUE
               : EDEN_DSMOD_FALSE;
  } catch (...) {
    return EDEN_DSMOD_FALSE;
  }
}
EdenDsmodBool ImageCallback(void *p, const EdenDsmodHostApi *, const char *key,
                            void *receiver, EdenDsmodImageSink sink) {
  try {
    return p && key && static_cast<Module *>(p)->ImageFor(key, receiver, sink)
               ? EDEN_DSMOD_TRUE
               : EDEN_DSMOD_FALSE;
  } catch (...) {
    return EDEN_DSMOD_FALSE;
  }
}
const EdenDsmodModuleApi Api{
    EDEN_DSMOD_MODULE_ABI_VERSION,
    sizeof(EdenDsmodModuleApi),
    0,
    EDEN_DSMOD_MODULE_ABI_HASH,
    TitleId,
    "Farming Simulator Nintendo Switch Edition companion",
    EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_ROMFS_READ,
    &Supports,
    &Create,
    &Destroy,
    &Sample,
    &Tick};
const EdenDsmodModuleExtensions Extensions{EDEN_DSMOD_EXT_VERSION,
                                           sizeof(EdenDsmodModuleExtensions),
                                           EDEN_DSMOD_EXT_HASH,
                                           &Configure,
                                           &Action,
                                           &ImageCallback};
const EdenDsmodModuleWriteExtensions WriteExtensions{
    EDEN_DSMOD_WRITE_EXT_VERSION, sizeof(EdenDsmodModuleWriteExtensions),
    EDEN_DSMOD_WRITE_EXT_HASH, &ConfigureWrites};
} // namespace
DSMOD_SDK_EXPORT_MODULE(Api)
DSMOD_SDK_EXPORT_EXTENSIONS(Extensions)
DSMOD_SDK_EXPORT_WRITE_EXTENSIONS(WriteExtensions)
