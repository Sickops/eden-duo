// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "farming_simulator_animal_data.h"
#include "farming_simulator_font.h"
#include "farming_simulator_shop_data.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
namespace farming_simulator {
enum class Phase { Unknown, Title, Loading, World };
struct FinanceRow {
  std::string label;
  // Native five-day history, oldest to today. Missing values are not zero.
  std::array<std::optional<double>, 5> amounts;
};
struct StatisticRow {
  std::string label, unit;
  std::optional<double> session, total;
  // Native formatted durations (hh:mm), or other verified display values.
  std::optional<std::string> session_text, total_text;
  int digits{1};
};
enum class PriceTrend { Unknown, Stable, Rising, Falling };
enum class PriceHighlight { Neutral, High, Low };
struct PriceCrop {
  std::string id, name, icon;
  std::optional<double> silo_l;
};
struct PriceQuote {
  bool accepted{};
  std::optional<double> price_per1000;
  PriceTrend trend{PriceTrend::Unknown};
  PriceHighlight highlight{PriceHighlight::Neutral};
  bool great_demand{};
};
struct PriceStation {
  std::string id, name;
  // Crop-index aligned. A missing quote is unavailable, not unaccepted.
  std::vector<PriceQuote> quotes;
};
// Prices matrix (native "fs-prices" ScrollRegion, gen_manifest.py): the
// header row, station column and each price row are module images at a
// FIXED pixel size -- PriceColumnCap columns / PriceStationCap rows -- so
// the runtime's Image widgets (whose declared rect is static) never stretch
// them. Real data narrower/shorter than the cap draws into the left/top
// portion; the rest stays transparent and is never scrolled into view
// because the region's content_w_bind/count_bind clamp to the live
// (capped) crop/station counts, not to this cap. Chosen generously above
// any known Switch Edition map's selling-point/fill-type counts while
// keeping a single row image a few MB, not tens.
inline constexpr int PriceColumnWidth = 212, PriceRowHeight = 84;
inline constexpr size_t PriceColumnCap = 32, PriceStationCap = 40;
// The header row and each price row used to be rendered as ONE
// PriceColumnCap(32)*212 = 6784px-wide module image. The runtime's module
// image sink refuses any image over 4096px on either axis
// (mod_module_services.cpp's ModuleImage lambda) -- that image was silently
// dropped every time, which is why the live Prices page showed only the
// frozen station column (352px, under the limit) and nothing else. Both
// images are now split into PriceColumnChunks column chunks of
// PriceColumnChunk columns each (PriceColumnChunk*212 = 3392px, comfortably
// under 4096 with margin, and ~1.1-3.1MB per chunk, comfortably under the
// runtime's 16MB-per-image MaxImageBytes). Chunk j covers crop columns
// [j*PriceColumnChunk, (j+1)*PriceColumnChunk). The runtime's native
// "fs-prices" two-axis ScrollRegion only ever requests the row/header chunks
// actually on screen (gen_manifest.py's prices_rep repeat_cols grid).
inline constexpr size_t PriceColumnChunk = 16;
inline constexpr size_t PriceColumnChunks =
    (PriceColumnCap + PriceColumnChunk - 1) / PriceColumnChunk;
// Finance table (native "fs-finance" ScrollRegion, gen_manifest.py). The day
// axis is a fixed, always-exactly-5-wide history (FinanceRow::amounts is a
// std::array<5>), so FinanceDayCount/FinanceContentW are plain constants --
// unlike Prices' crop columns, there is no live count to cap or clamp here.
// FinanceRowCap bounds the row axis (label column / per-row image height)
// the same way PriceStationCap does.
// A station's identity for matching a map marker to its Prices row: the localization key with
// the game's optional "$l10n_" prefix removed (the map and Prices readers may see either form).
inline std::string_view StationKey(std::string_view key) {
  if (key.starts_with("$l10n_"))
    key.remove_prefix(6);
  return key;
}
inline constexpr int FinanceLabelWidth = 492, FinanceDayWidth = 236,
                     FinanceRowHeight = 76, FinanceDayCount = 5;
inline constexpr int FinanceContentW = FinanceDayWidth * FinanceDayCount;
inline constexpr size_t FinanceRowCap = 64;
// The frozen row-label column used to be ONE FinanceLabelWidth(492) x
// FinanceRowHeight(76)*FinanceRowCap(64) = 4864px-tall module image -- over
// the runtime's 4096px-per-axis module image limit (mod_module_services.cpp's
// ModuleImage lambda), silently refused on every request, which is why the
// live Finance page's label column rendered BLANK. Same failure mode and fix
// as Prices' column chunking / Animal needs' strip chunking above: the
// column is now FinanceLabelChunks fixed FinanceLabelChunk(32)-row chunks
// (32*76 = 2432px tall, comfortably under 4096 and ~4.8MB per chunk,
// comfortably under the runtime's 16MB-per-image MaxImageBytes), tiled
// vertically by the native "fs-finance" two-axis ScrollRegion
// (gen_manifest.py's finance-labels widget, now a repeat_dy template with
// scroll_follow "y") exactly like Prices' own header/row chunks -- only the
// chunk(s) actually on screen are ever requested.
inline constexpr size_t FinanceLabelChunk = 32;
inline constexpr size_t FinanceLabelChunks =
    (FinanceRowCap + FinanceLabelChunk - 1) / FinanceLabelChunk;
// Guide (native "help-topics" + "help-article"/"help-article-static"
// ScrollRegions). HelpTopicRowHeight is the per-topic cell pitch (one
// cheap, fixed-size image per topic -- no stretch-cap concern at all,
// unlike Prices/Finance, since a y-only list's cell width never varies with
// content). HelpArticleCap bounds the article image's height: content_h is
// genuinely dynamic per topic (mixed text/illustration, measured by a dry
// run -- see View::RenderHelpArticle), so, like Prices/Finance, the
// rendered image is always exactly this tall with real content top-aligned
// and the rest transparent; chosen well above any known Guide article.
inline constexpr int HelpTopicRowHeight = 144;
inline constexpr size_t HelpTopicCap = 64, HelpArticleCap = 4000;
// Statistics (native "statistic-rows" ScrollRegion). Width/row height match
// the old baked body exactly (View::Statistics' own Fill/RowText header
// stays baked, not scrollable); StatisticRowCap is the cap OnScrollAction's
// predecessor used for this list's row count already.
inline constexpr int StatisticRowWidth = 1200, StatisticRowHeight = 76;
inline constexpr size_t StatisticRowCap = 256;
// Animal needs (native "animal-needs" ScrollRegion). The same mixed
// row-list-plus-free-form-content shape as the Guide article (needs rows at
// a fixed NeedHeight=100 pitch, then free-form help text and an optional
// illustration) -- AnimalNeedsCap bounds the measured, top-aligned image
// the same way HelpArticleCap does, chosen well above any known
// needs-count/help-text/illustration combination (64 needs * 100 +
// ~8192-char wrapped help text + a 284px illustration).
inline constexpr size_t AnimalNeedsCap = 16000;
// AnimalNeedsCap(16000) alone would need a single 1200x16000 module image --
// over the runtime's 4096px-per-axis module image limit (always, by
// construction: 16000 > 4096) AND its 16MB MaxImageBytes (1200*16000*4 =
// ~76.8MB, always) -- the sink would silently refuse it on every request,
// same failure mode as the Prices matrix above. The pane is now served as
// AnimalNeedsStripCap fixed-height strips (AnimalNeedsStripHeight = 1600,
// 1200*1600*4 = ~7.3MB per strip, under the ~8MB per-image budget and the
// runtime's hard caps with margin), tiled vertically by the native
// "animal-needs" ScrollRegion (gen_manifest.py's animal_needs_rep) exactly
// like any other y-only row list -- only the strip(s) actually on screen are
// ever requested. Strip i covers content rows [i*AnimalNeedsStripHeight,
// (i+1)*AnimalNeedsStripHeight); View::RenderAnimalNeedsStrip renders the
// same measured, top-aligned content as before, just windowed to one strip.
inline constexpr int AnimalNeedsStripHeight = 1600;
inline constexpr size_t AnimalNeedsStripCap =
    (AnimalNeedsCap + size_t(AnimalNeedsStripHeight) - 1) / size_t(AnimalNeedsStripHeight);
// Map legend (native "map-legend" ScrollRegion). Width/row height match the
// old baked pane exactly (the Legend/HUD toggle buttons and header label
// stay baked, not scrollable); MapLegendRowCap is a sane cap -- no known
// legend has anywhere near this many rows.
inline constexpr int MapLegendRowWidth = 312, MapLegendRowHeight = 100;
inline constexpr size_t MapLegendRowCap = 64;
// Garage (native "garage-list" + "garage-specs" ScrollRegions). Width/row
// heights match the old baked list/specs panes exactly; GarageRowCap/
// GarageSpecRowCap are the caps OnViewAction's predecessor already used for
// these lists' counts.
inline constexpr int GarageRowWidth = 440, GarageRowHeight = 207;
inline constexpr size_t GarageRowCap = 512;
inline constexpr int GarageSpecRowWidth = 736, GarageSpecRowHeight = 80;
inline constexpr size_t GarageSpecRowCap = 64;
// Shop filter list (native "shop-filters" ScrollRegion). Width/row pitch
// match the old baked list exactly (344 wide, 108px pitch/100px drawn cell,
// same as View::Shop's own FilterRow); ShopFilterRowCap is the cap
// OnViewAction's predecessor already used for category/brand counts.
inline constexpr int ShopFilterRowWidth = 344, ShopFilterRowPitch = 108;
inline constexpr size_t ShopFilterRowCap = 256;
// Fresh display values only. False/absent fields never inherit a prior world's
// values. Reader validity gates are independent: on-foot is a ready world with
// vehicle_ready=false.
struct MapLegend {
  std::string label, icon;
  std::vector<uint32_t> colors;
};
struct MapLayers {
  uint64_t revision{};
  Image crops, growth, soil;
  std::vector<MapLegend> crop_legend, growth_legend, soil_legend;
  std::string soil_error;
};
// A live MapHotspot from the native ingameMap.hotspots_sorted list. Sprite
// coordinates are pixels in atlas (or the whole source image when w/h are 0).
struct MapMarker {
  std::string kind, label, image, background_image;
  // A selling point's raw station localization key ("station_barn"), the identity its Prices
  // row is keyed by; label may already be the localized name. Empty for other markers.
  std::string station_key;
  double x{}, z{}, rotation{};
  int sx{}, sy{}, w{}, h{};
  int background_sx{}, background_sy{}, background_w{}, background_h{};
  int draw_w{}, draw_h{};
  uint32_t object_id{};
  // Native InGameMenu:selectHotspot shows Visit/Tag only for places: not the
  // vehicle categories (COMBINE/STEERABLE/TOOL/TRAILER, which get Enter/Reset)
  // and not AI workers.
  bool can_visit{};
  uint32_t color{0xffffffff};
  uint32_t background_color{0xffffffff};
  bool enabled{true};
};
// Outcome of the last instant game call (farming_simulator_game_calls.h).
struct GameCallState {
  bool helper{}, busy{};  // load-plan heartbeat fresh; a request in flight
  uint32_t serial{};      // +1 per finished call
  std::string kind;       // enter / reset / visit / tag
  int status{};           // game_calls::Status
  int64_t code{};         // game_calls::Code when status == Ran
  std::string message;    // OutcomeText
  std::string detail;     // the chunk's text / the Lua error
};
struct Snapshot {
  Phase phase{Phase::Unknown};
  bool world_ready{}, vehicle_ready{};
  int map_id{};
  uint64_t world_identity{};
  // Bumped by the reader every time its 1Hz management snapshot actually
  // refreshes (farming_simulator_reader.cpp) -- i.e. whenever
  // finance_rows/price_stations/statistic_rows (and the rest of that same
  // batch) could have changed at all. Sample() in the generated module
  // (0100D04004176000.cpp) is called every tick (60Hz) while a page is
  // open, but there is no point re-serializing and re-hashing the whole
  // Prices/Finance/Statistics table on 59 out of every 60 of those calls
  // when the underlying data has not moved since the last management
  // refresh -- this is that narrower "did the data change" signal, cheaper
  // than hashing the content itself.
  uint64_t management_revision{};
  std::optional<double> player_x, player_z, player_heading;
  std::optional<double> money, speed_kmh, fuel_l, fuel_capacity_l, fill_l,
      fill_capacity_l;
  std::optional<int> time_minutes;
  std::optional<double> time_scale;
  bool time_scale_change_allowed{}, time_scale_pending{};
  bool native_menu_open{};
  std::shared_ptr<const MapLayers> map_layers;
  std::string map_layer_error;
  std::string map_markers_error;
  std::vector<MapMarker> map_markers;
  std::string vehicle_name, vehicle_image, fill_name, fill_icon,
      currency_symbol;
  std::string vehicle_config;
  bool finances_ready{}, statistics_ready{};
  std::vector<FinanceRow> finance_rows;
  std::array<std::string, 5> finance_day_labels;
  std::array<std::optional<double>, 5> finance_totals;
  std::optional<double> finance_saldo;
  std::vector<StatisticRow> statistic_rows;
  std::optional<double> loan, loan_max;
  bool prices_ready{};
  std::vector<PriceCrop> price_crops;
  std::vector<PriceStation> price_stations;
  bool animals_ready{};
  std::vector<AnimalGroup> animals;
  bool shop_ready{}, vehicles_ready{};
  std::vector<ShopCategory> shop_categories;
  std::vector<ShopBrand> shop_brands;
  std::vector<ShopItem> shop_items;
  std::vector<VehicleItem> owned_vehicles;
  GameCallState game_call;
  // Set alongside the matching *_ready flag above (never both true at once):
  // true while that category has not yet produced data for the current
  // world_identity and is still inside its warm-up window, or has lost a
  // previously-good read within the short grace window the reader keeps
  // showing the last good data under. The view shows "Loading..." for this
  // instead of "Data unavailable" (reader: farming_simulator_reader.cpp's
  // kWarmupTimeout/kStaleAfterGoodTimeout). false and !*_ready together mean
  // a definitive/timed-out failure (or the category is genuinely not
  // present), which is when "Data unavailable" is correct.
  bool finances_loading{}, statistics_loading{}, prices_loading{},
      animals_loading{}, shop_loading{}, vehicles_loading{};
};
// Menu (a destination-grid page) was superseded by the 8 fixed footer tabs
// and removed; the numeric value is intentionally left unused rather than
// reassigned, so every other page keeps the native-art footer index it has
// always had.
enum class Page {
  Operations,
  Shop,
  Prices = 3,
  Vehicles,
  Finances,
  Animals,
  Statistics,
  Help
};
inline constexpr double DefaultMapZoom = 2.;
// Map pan/zoom is handled entirely by the runtime's native pan_zoom widget
// (see mod_input.cpp/mod_view_default.h in the Eden Duo runtime): the module
// no longer tracks a live pan/zoom/follow state or redraws per gesture.
// MapWorldExtent is the world-unit span the compact viewport shows
// edge-to-edge at zoom 1 (unchanged from the previous pan/zoom behaviour).
// The expanded viewport is wider/taller in pixels than compact, so its
// underlying raster is padded out to MapCanvasW x MapCanvasH world units
// (same px-per-world-unit as compact, just more of it) rather than having
// the runtime stretch a square image into a non-square rect.
inline constexpr double CompactMapPx = 888., ExpandedMapW = 1240., ExpandedMapH = 1000.;
inline constexpr double MapWorldExtent = 2048.;
inline constexpr double MapCanvasW = MapWorldExtent * ExpandedMapW / CompactMapPx;
inline constexpr double MapCanvasH = MapWorldExtent * ExpandedMapH / CompactMapPx;
// Build-time guard (ENGINEERING hardening item #2): every module image this
// module can ever serve must stay at or under the runtime's hard module
// image limits -- 4096px on either axis, 16MB (w*h*4 RGBA bytes) total (see
// mod_module_services.cpp's ModuleImage lambda). A violation here is exactly
// the failure mode that silently blanked the Prices matrix, the Finance
// label column and the Animal needs pane before each was chunked (see those
// constants' own comments above) -- the runtime does not error, it just
// refuses the image, so this has to be caught at build time instead of
// being left to show up as a blank pane live. All of these are compile-time
// constants (the row/column pitches and caps above), so a static_assert
// catches a regression -- a cap raised without re-chunking, a widened row,
// a new fixed-size image added without this check extended -- at compile
// time rather than at runtime.
namespace detail {
inline constexpr size_t kMaxImageSide = 4096;
inline constexpr size_t kMaxImageBytes = 16 * 1024 * 1024;
constexpr bool ImageFits(size_t w, size_t h) {
  return w <= kMaxImageSide && h <= kMaxImageSide && w * h * 4 <= kMaxImageBytes;
}
// std::lround is not constexpr on every target (notably Android's Bionic
// libc, used by the NDK build) even though it is a compile-time-constant
// expression here -- a small constexpr round-half-up stands in for it,
// correct for the non-negative inputs this is ever used with.
constexpr size_t ConstexprRoundNonNegative(double x) {
  return size_t(x + 0.5);
}
} // namespace detail
// Prices matrix: header/row chunks (RenderPriceHeader/RenderPriceRow) and the
// frozen station column (RenderPriceStations).
static_assert(detail::ImageFits(size_t(PriceColumnWidth) * PriceColumnChunk, 240),
              "Prices header chunk exceeds the runtime's module image limits");
static_assert(detail::ImageFits(size_t(PriceColumnWidth) * PriceColumnChunk, size_t(PriceRowHeight)),
              "Prices row chunk exceeds the runtime's module image limits");
static_assert(detail::ImageFits(352, size_t(PriceRowHeight) * PriceStationCap),
              "Prices station column exceeds the runtime's module image limits");
// Finance table: header/row/totals (fixed FinanceContentW) and the chunked
// label column (RenderFinanceLabels).
static_assert(detail::ImageFits(size_t(FinanceContentW), size_t(FinanceRowHeight)),
              "Finance header/row exceeds the runtime's module image limits");
static_assert(detail::ImageFits(size_t(FinanceContentW), 44),
              "Finance totals row exceeds the runtime's module image limits");
static_assert(detail::ImageFits(size_t(FinanceLabelWidth), size_t(FinanceRowHeight) * FinanceLabelChunk),
              "Finance label chunk exceeds the runtime's module image limits");
// Guide: topic row (352 wide) and the article body, 832 wide x HelpArticleCap
// tall (RenderHelpArticle's local `Width` constant, farming_simulator_view.cpp).
static_assert(detail::ImageFits(352, size_t(HelpTopicRowHeight)),
              "Guide topic row exceeds the runtime's module image limits");
static_assert(detail::ImageFits(832, HelpArticleCap),
              "Guide article body exceeds the runtime's module image limits");
// Statistics list, one row image.
static_assert(detail::ImageFits(size_t(StatisticRowWidth), size_t(StatisticRowHeight)),
              "Statistics row exceeds the runtime's module image limits");
// Animal needs: one strip image (RenderAnimalNeedsStrip's fixed 1200 width).
static_assert(detail::ImageFits(1200, size_t(AnimalNeedsStripHeight)),
              "Animal needs strip exceeds the runtime's module image limits");
// Map legend, one row image.
static_assert(detail::ImageFits(size_t(MapLegendRowWidth), size_t(MapLegendRowHeight)),
              "Map legend row exceeds the runtime's module image limits");
// Garage list/specs, one row image each.
static_assert(detail::ImageFits(size_t(GarageRowWidth), size_t(GarageRowHeight)),
              "Garage row exceeds the runtime's module image limits");
static_assert(detail::ImageFits(size_t(GarageSpecRowWidth), size_t(GarageSpecRowHeight)),
              "Garage spec row exceeds the runtime's module image limits");
// Shop filter list, one row image (ShopFilterRowPitch is the per-row image
// height, matching View::Shop's own FilterRow pitch).
static_assert(detail::ImageFits(size_t(ShopFilterRowWidth), size_t(ShopFilterRowPitch)),
              "Shop filter row exceeds the runtime's module image limits");
// Shop catalog card (RenderShopRow's fixed 412x372, farming_simulator_shop_views.cpp).
static_assert(detail::ImageFits(412, 372),
              "Shop catalog card exceeds the runtime's module image limits");
// The map's own raster (RenderMapBase): a dynamic source-texture-dependent
// width clamped to map_px<=1536 (farming_simulator_view.cpp), scaled up to
// MapCanvasW/MapCanvasH world units at that same resolution -- the clamp
// makes the worst case a compile-time bound.
static_assert(detail::ImageFits(
                  detail::ConstexprRoundNonNegative(1536. * MapCanvasW / MapWorldExtent),
                  detail::ConstexprRoundNonNegative(1536. * MapCanvasH / MapWorldExtent)),
              "Map raster exceeds the runtime's module image limits");
// Small fixed chrome images (RenderResetButton/RenderMapArrow/
// RenderGarageActionRow/RenderMapSelectionButtons): all comfortably under
// the limits, included for completeness so a future resize is still caught.
static_assert(detail::ImageFits(180, 88),
              "Map Reset button exceeds the runtime's module image limits");
// G3 critique fix (2026-10-17): grown from 60x60 so the native glyph can
// render large enough (44x44) to still read as a pointer, with the lime
// selection ring strictly outside it -- see RenderMapArrow's own doc comment.
static_assert(detail::ImageFits(84, 84),
              "Map arrow sprite exceeds the runtime's module image limits");
// G3 critique fix: 112px-tall primary controls (LEGIBILITY.md's primary-
// control floor) and a 20px right margin matching every other Garage detail
// row (736 = 1240 - 484 - 20, not flush to the screen edge) -- see
// RenderGarageActionRow's own doc comment.
static_assert(detail::ImageFits(736, 128),
              "Garage action row exceeds the runtime's module image limits");
// G3 critique fix: stacked full-width 112px rows (both kinds share one
// constant 304x236 canvas -- a 112px row, a 12px gap, a 112px row -- so a
// single-action kind just leaves the lower half transparent) -- see
// RenderMapSelectionButtons' own doc comment.
static_assert(detail::ImageFits(304, 236),
              "Map selection action row exceeds the runtime's module image limits");
// Forward-compatible tracking of the player marker while a map viewport's
// pan_zoom view is away from its default follow view (the user panned or
// zoomed it). The arrow overlay (map-arrow-compact/expanded, gen_manifest.py)
// is fixed at its viewport's rect centre and relies on the runtime recentring
// that viewport's own default ("home") view on the player every tick -- true
// exactly while the view IS at home, which is why it is hidden once the user
// pans/zooms away (fs.view.map_arrow_{suffix}'s "!'view_custom:map-{suffix}'"
// term, gen_manifest.py): this module has no way to learn where the view
// moved to, so there is no way to re-aim the arrow at the player's true
// screen position from here.
//
// The runtime's pan_zoom Image widgets do not publish their live view back
// to a module the way a native "type: map" widget publishes
// @map_view_cx/cy/ppw/rot (mod_input.cpp's MapViewHostValue, runtime commit
// e5751ea0) -- confirmed by reading duo-1.3.0's mod_module_host.cpp get_f64:
// it only special-cases the "@map_view_" prefix, nothing for a pan_zoom
// Image's ViewTransform (mod_view_default.h). A runtime change was
// requested to add the same thing for these widgets ("@view_zoom:<id>",
// "@view_pan_x:<id>", "@view_pan_y:<id>", same zoom-1-widget-px units
// ClampViewPan already uses) -- see this cycle's runtime-change request for
// a pan-anchored map marker on the Farming Simulator companion.
//
// ArrowPanOffset answers nullopt on any runtime that doesn't support those
// keys yet: this module always queries with a NaN fallback, and `zoom`
// arrives NaN (or non-finite/non-positive) on an older host, which this
// function treats identically to "no answer". The caller then leaves
// fs.ui.map_arrow_pan_tracked_{suffix} at 0 and the offset at 0 -- i.e.
// exactly today's pinned-at-centre behaviour (gen_manifest.py's visibility
// expr falls back to the existing "!'view_custom:...'" term). Once a runtime
// answers finite values, the offset points the arrow at the player's live
// SCREEN position inside that viewport's rect (clamped to stay inside it),
// so the arrow can be shown unconditionally, including while the view is
// panned/zoomed away from home, and still rotate live via rotate_bind.
[[nodiscard]] inline std::optional<std::array<double, 2>> ArrowPanOffset(
    double home_cx, double home_cy, double rect_w, double rect_h, double zoom, double pan_x,
    double pan_y) {
  if (!std::isfinite(zoom) || !std::isfinite(pan_x) || !std::isfinite(pan_y) || zoom <= 0.)
    return std::nullopt;
  // zoom1_* is the player's position in the same zoom-1 widget-px units as
  // the runtime's own pan (mod_view_default.h / ClampViewPan): home_cx/cy are
  // already that fraction of the viewport's own picture (the same fraction
  // view_cx_bind/view_cy_bind use for the default view), so multiplying by
  // the rect size gives the zoom-1 position directly.
  const double zoom1_x = home_cx * rect_w, zoom1_y = home_cy * rect_h;
  const double screen_x = std::clamp((zoom1_x - pan_x) * zoom, 0., rect_w);
  const double screen_y = std::clamp((zoom1_y - pan_y) * zoom, 0., rect_h);
  // Offset from the arrow widget's authored rect, which is centred on the
  // viewport (gen_manifest.py: rect = [cx-30, cy-30, 60, 60]).
  return std::array<double, 2>{screen_x - rect_w / 2., screen_y - rect_h / 2.};
}
struct ViewState {
  Snapshot snapshot;
  Page page{Page::Operations};
  size_t help_topic{};
  // help_topic_offset / help_offset removed: the Guide topics list and
  // article body are now the runtime's own native "help-topics"/
  // "help-article[-static]" ScrollRegions (gen_manifest.py) -- the module
  // no longer tracks or clamps a scroll position for either.
  // statistic_offset removed: the Statistics list is now the runtime's own
  // native "statistic-rows" ScrollRegion (gen_manifest.py) -- the module no
  // longer tracks or clamps a scroll position for it. Each row is served as
  // its own module image (View::RenderStatisticRow).
  // finance_offset / finance_day_offset removed: the Finance table is now
  // the runtime's own native two-axis ScrollRegion ("fs-finance",
  // gen_manifest.py) -- the module no longer tracks or clamps a scroll
  // position for it. The day header, row-label column, each row and the
  // totals row are served as separate module images (View::RenderFinance*).
  // price_crop_offset / price_station_offset removed: the Prices matrix is
  // now the runtime's own native two-axis ScrollRegion ("fs-prices",
  // gen_manifest.py) -- the module no longer tracks or clamps a scroll
  // position for it. The header row, station column and each price row are
  // served as separate module images (View::RenderPriceHeader/
  // RenderPriceStations/RenderPriceRow), requested for whatever is on screen.
  // animal_needs_offset removed: the needs list (plus its trailing help
  // text/illustration) is now the runtime's own native "animal-needs"
  // ScrollRegion (gen_manifest.py) -- the module no longer tracks or clamps
  // a scroll position for it. Served as one module image
  // (View::RenderAnimalNeeds), content_h_bind'd to its measured height.
  size_t animal_selected{};
  bool animal_feeding{};
  bool shop_details{};
  int shop_filter_mode{}; // 0 category, 1 brand
  // shop_grid_offset removed: the Shop catalog grid is now the runtime's
  // own native ScrollRegion ("shop-catalog", gen_manifest.py) -- the module
  // no longer tracks or clamps a scroll position for it at all. Each card is
  // its own module image (View::RenderShopRow), requested directly by the
  // runtime for whichever rows are actually on screen.
  // shop_category_offset / shop_brand_offset removed: the filter list
  // (whichever of the two shop_filter_mode selects) is now the runtime's
  // own native "shop-filters" ScrollRegion (gen_manifest.py) -- the module
  // no longer tracks or clamps a scroll position for it. Each row is its
  // own module image (View::RenderShopFilterRow).
  double shop_spec_offset{};
  // vehicles_offset / vehicle_spec_offset removed: the Garage list and the
  // selected vehicle's specs are now the runtime's own native
  // "garage-list"/"garage-specs" ScrollRegions (gen_manifest.py) -- the
  // module no longer tracks or clamps a scroll position for either. Each
  // row is its own module image (View::RenderGarageRow/
  // RenderGarageSpecRow).
  std::string shop_category, shop_brand, shop_selected, vehicle_selected;
  bool map_fullscreen{};
  bool time_scale_open{};
  int map_layer{};
  bool map_legend{};
  // Map marker selection (UX-SPEC C): index into snapshot.map_markers the player last tapped,
  // or -1 for none. Drives the right pane's selection card (Operations()) and the real
  // Enter/Reset (vehicle marker) or Visit/Tag place (place marker) action row below it
  // (View::RenderMapSelectionButtons, ImageFor's "map_selection_buttons/<kind>/<place>/<tagged>"
  // key). World changes and opening the time-scale picker or legend both clear it (the same
  // "mutually exclusive right-pane content" rule the time-scale picker already established).
  int64_t map_marker_selected{-1};
  // map_legend_offset removed: the legend list is now the runtime's own
  // native "map-legend" ScrollRegion (gen_manifest.py) -- the module no
  // longer tracks or clamps a scroll position for it. Each row is served
  // as its own module image (View::RenderMapLegendRow).
  // FILTERS popup (owner request, UX-SPEC 2026-10-17 review A: "beside
  // expand, add filters that pop up ... that can toggle map markers like
  // vehicle, field, etc."). Reuses the exact pane-replace mechanism already
  // accepted for the time-scale picker in compact mode, plus a floating
  // card in expanded mode (the same top-right-card column convention the
  // time-scale picker already uses on every non-Map page) since expanded
  // has no right pane to replace. map_filters gates which of the 7 native
  // InGameMenu.xml filter categories (fixed order: Vehicles, Harvesters,
  // Trailers, Tools, Selling points, Field jobs, Other --
  // onClickMapFilterSteerable/Combine/Trailer/Tools/TipStations/FieldJobs/
  // Other) draw their live map markers (View::RenderMapBase's
  // s.map_markers loop; see View::MapMarkerFilter). Default all ON,
  // matching the native default.
  bool map_filters_open{};
  std::array<bool, 7> map_filters{true, true, true, true, true, true, true};
};
class View {
public:
  explicit View(const EdenDsmodHostApi &host) : assets_{host} {}
  bool Render(const ViewState &state, Image &out);
  // The map's own raster, independent of (and far lower frequency than) the
  // rest of the page: the full map (current layer, markers, player arrow)
  // padded to MapCanvasW x MapCanvasH world units (see above) so the
  // runtime's pan_zoom widgets can show it at the identical world scale in
  // both the compact and expanded viewports. false = no map to show (not in
  // a map-capable world); `out` is left untouched.
  bool RenderMapBase(const ViewState &state, Image &out);
  // A single static 180x88 picture of the map's Reset button, drawn with the
  // same game font/look as every other button (View::Button: black box,
  // "Reset" 32px bold white, centred) -- never the runtime's own pixel font.
  // Content never changes, so the module serves this once from a constant
  // "module:farming_simulator:reset_button" key and the runtime's module-
  // image cache keeps it forever; no revision, no re-render per call.
  bool RenderResetButton(Image &out);
  // The player map-arrow sprite, unrotated: a constant 60x60 crop of the
  // native mapHotspots_256 atlas (same UV/tint RenderMapBase used to bake),
  // served once from a constant "module:farming_simulator:map_arrow" key the
  // same way RenderResetButton is. Heading is no longer baked into any
  // raster -- the runtime's own Image rotate_bind turns this sprite live
  // from fs.ui.map_arrow_heading_deg (gen_manifest.py's map-arrow-compact/
  // expanded widgets), so the arrow can turn and (while following) stay
  // centred every module tick instead of only on the map image's own
  // ~5Hz MapSnapshotKey-gated rebuild.
  bool RenderMapArrow(Image &out);
  // One Shop catalog card (412x372), standalone -- served by ImageFor for the
  // runtime's native "shop-catalog" ScrollRegion (gen_manifest.py) keyed
  // "module:farming_simulator:shop_row/<master_index>/<active 0|1>".
  // `master_index` indexes state.snapshot.shop_items directly (the same raw
  // index ShopItems() returns), not the filtered/visible position. false =
  // index out of range; `out` is left untouched.
  bool RenderShopRow(const ViewState &state, size_t master_index, bool active,
                     Image &out);
  // Shop filter list (native "shop-filters" ScrollRegion): one
  // category/brand row, fixed ShopFilterRowWidth x ShopFilterRowPitch --
  // served by ImageFor for whichever rows are on screen. Whichever of
  // shop_filter_mode's two lists is active is read directly off `state`,
  // same mapping View::Shop's own switch already used. `active` is
  // whether this is the currently chosen filter (encoded directly in the
  // key, like RenderShopRow's active flag).
  bool RenderShopFilterRow(const ViewState &state, size_t index, bool active,
                          Image &out);
  // Garage list (native "garage-list" ScrollRegion): one vehicle row card,
  // fixed GarageRowWidth x GarageRowHeight -- served by ImageFor for
  // whichever rows are on screen. `active` is whether this is the selected
  // vehicle (encoded directly in the key, like RenderShopRow's active
  // flag).
  bool RenderGarageRow(const ViewState &state, size_t index, bool active,
                      Image &out);
  // Garage specs (native "garage-specs" ScrollRegion): one spec row for the
  // CURRENTLY selected vehicle (SelectedVehicle), fixed GarageSpecRowWidth x
  // GarageSpecRowHeight -- served by ImageFor for whichever rows are on
  // screen. false = no selected vehicle, or spec_index out of range.
  bool RenderGarageSpecRow(const ViewState &state, size_t spec_index,
                           Image &out);
  // Prices matrix (native "fs-prices" ScrollRegion): the frozen crop header
  // row, ONE PriceColumnChunk(16)*212 = 3392px-wide chunk -- icon/name/silo
  // amount for crop columns [chunk_index*PriceColumnChunk,
  // (chunk_index+1)*PriceColumnChunk), left-aligned within the chunk image
  // (see PriceColumnChunks' own comment: the full PriceColumnCap(32)*212 =
  // 6784px header was silently refused by the runtime's module image sink,
  // over its 4096px-per-axis limit). scroll_follow "x" in the manifest.
  // false = prices not ready or chunk_index out of range.
  bool RenderPriceHeader(const ViewState &state, size_t chunk_index, Image &out);
  // The frozen station-name column, fixed 352px wide, PriceRowHeight *
  // PriceStationCap tall -- one row per station from row 0, alternating
  // background to match RenderPriceRow's own striping. scroll_follow "y".
  // Unaffected by the column-chunking above: 352px is well under the
  // runtime's 4096px limit already.
  bool RenderPriceStations(const ViewState &state, Image &out);
  // One price row chunk (crop columns [chunk_index*PriceColumnChunk,
  // (chunk_index+1)*PriceColumnChunk) for one station), PriceColumnChunk(16)*
  // 212 = 3392px wide, PriceRowHeight tall -- served by ImageFor for
  // whichever row/chunk cells the "fs-prices" ScrollRegion currently has on
  // screen (gen_manifest.py's prices_rep repeat_cols grid: col = chunk_index,
  // row = station_index). `station_index` is the real (unfiltered) index
  // into state.snapshot.price_stations. false = out of range, chunk_index
  // out of range, or prices not ready.
  bool RenderPriceRow(const ViewState &state, size_t station_index,
                      size_t chunk_index, Image &out);
  // Finance table (native "fs-finance" ScrollRegion): the frozen day header
  // row -- FinanceContentW wide (all 5 days, never capped -- see
  // FinanceContentW above), FinanceRowHeight tall. scroll_follow "x".
  bool RenderFinanceHeader(const ViewState &state, Image &out);
  // The frozen row-label column: one FinanceLabelChunk(32)-row chunk,
  // FinanceLabelWidth wide, FinanceLabelChunk*FinanceRowHeight = 2432px tall
  // (see FinanceLabelChunks' own comment in farming_simulator_view.h for why
  // the old single FinanceRowCap-tall image was always refused). Chunk j
  // covers finance rows [j*FinanceLabelChunk, (j+1)*FinanceLabelChunk),
  // alternating background (continuing RenderFinanceRow's own striping
  // parity across chunk boundaries) to match RenderFinanceRow's own
  // striping. scroll_follow "y". false = chunk_index out of range.
  bool RenderFinanceLabels(const ViewState &state, size_t chunk_index, Image &out);
  // One finance row's day amounts (all 5 days, no label), FinanceContentW
  // wide, FinanceRowHeight tall -- served by ImageFor for whichever rows the
  // "fs-finance" ScrollRegion currently has on screen. `row_index` is the
  // real index into state.snapshot.finance_rows. false = out of range or
  // finances not ready.
  bool RenderFinanceRow(const ViewState &state, size_t row_index, Image &out);
  // The frozen totals row (day totals only, no "Total" label), FinanceContentW
  // wide, 44px tall. scroll_follow "x".
  bool RenderFinanceTotals(const ViewState &state, Image &out);
  // Guide (native "help-topics" ScrollRegion): one topic list cell, fixed
  // 352 wide x HelpTopicRowHeight tall -- served by ImageFor for whichever
  // rows are on screen. `active` is whether this is the selected topic
  // (encoded directly in the key, like RenderShopRow's active flag, since
  // nothing else about a topic row ever changes once the Guide is loaded).
  bool RenderHelpTopicRow(const ViewState &state, size_t index, bool active, Image &out);
  // The Guide article body (native "help-article"/"help-article-static"
  // ScrollRegions): native mixed illustration/text order, measured then
  // drawn top-aligned into an image that is ALWAYS HelpArticleCap tall (see
  // the constant's own comment for why). `content_h_out`, when non-null,
  // receives the real (measured, cap-clamped) content height for
  // content_h_bind. false = topic_index out of range.
  bool RenderHelpArticle(const ViewState &state, size_t topic_index, Image &out,
                         int *content_h_out = nullptr);
  const std::vector<HelpTopic> &Help() { return assets_.Help(); }
  // HelpMaxOffset() removed: the Guide article is the native "help-article"
  // ScrollRegion now (content_h_bind, RenderHelpArticle), no module-tracked
  // max offset left to report.
  // Statistics (native "statistic-rows" ScrollRegion): one row image,
  // fixed StatisticRowWidth x StatisticRowHeight -- served by ImageFor for
  // whichever rows are on screen, same convention as RenderFinanceRow
  // (zebra-striped background baked per row since there is no frozen
  // header/label image to separate it from here).
  bool RenderStatisticRow(const ViewState &state, size_t row_index, Image &out);
  // Animal needs (native "animal-needs" ScrollRegion): native needs rows
  // plus trailing help text/illustration, measured once then drawn
  // top-aligned, windowed to ONE AnimalNeedsStripHeight(1600)-tall strip
  // (strip_index covers content rows [strip_index*AnimalNeedsStripHeight,
  // (strip_index+1)*AnimalNeedsStripHeight)) -- see AnimalNeedsStripCap's
  // own comment: a single AnimalNeedsCap(16000)-tall image was always over
  // the runtime's 4096px-per-axis limit and its 16MB MaxImageBytes, and was
  // silently refused on every request. `content_h_out`, when non-null,
  // receives the real (measured, cap-clamped) TOTAL content height for
  // content_h_bind, independent of which strip was requested. false = no
  // world/selected animal/not ready.
  bool RenderAnimalNeedsStrip(const ViewState &state, size_t strip_index, Image &out,
                              int *content_h_out = nullptr);
  // AnimalsMaxOffset() removed: the needs pane is the native "animal-needs"
  // ScrollRegion now (content_h_bind, RenderAnimalNeeds), no module-tracked
  // max offset left to report.
  // Map legend (native "map-legend" ScrollRegion): one legend-row image,
  // fixed MapLegendRowWidth x MapLegendRowHeight -- served by ImageFor for
  // whichever rows are on screen. `layer` selects which of map_layers'
  // three legend lists (crop/growth/soil) row_index indexes into, same
  // mapping View::Operations' own map_layer switch already used.
  bool RenderMapLegendRow(const ViewState &state, int layer, size_t row_index,
                          Image &out);
  // Garage action row (UX-SPEC B.2/B.3): one of 3 states, each a fixed, finite picture --
  // button pixels depend only on which of Enter/Reset are available, never on which vehicle is
  // selected (the text is always "Enter"/"Reset", the game's own l10n strings). `kind` 1 = both
  // (Enter left, primary/filled; Reset right, secondary/outline), 2 = Enter only (re-flowed to
  // full width), 3 = Reset only (full width). Served by ImageFor from a constant
  // "module:farming_simulator:garage_action_row/<kind>" key, same cache-forever convention as
  // RenderResetButton/RenderMapArrow above. false = kind out of range.
  bool RenderGarageActionRow(int kind, Image &out);
  // Map selection card's action row (UX-SPEC C.2/C.4): the same fixed-row convention as
  // RenderGarageActionRow, smaller (304x80) to fit the card. `kind` means the same thing
  // (1 both / 2 left-only / 3 right-only); `place` selects the Visit/"Tag place" pair instead
  // of Enter/Reset (same underlying game calls as Garage, triggered from a tapped map marker
  // instead of the Garage list); `tagged` swaps the right button's label to "Untag place" once
  // that place is the player's current map target. false = kind out of range.
  bool RenderMapSelectionButtons(int kind, bool place, bool tagged, Image &out);

private:
  void Footer(Image &out, Page page, const Snapshot &snapshot, bool visible = true);
  // Top-right clock + time-scale badge, shared by every page that shows the
  // black top bar (the Map/Operations page draws its own money first).
  void TimeControl(Image &out, const ViewState &state);
  void RowText(Image &out, std::string_view value, int x, int y, int h, int size = 32, uint32_t colour = 0xffffffff, bool bold = false);
  void RowLines(Image &out, const std::vector<std::string> &lines, int x, int y, int h, int size = 32, uint32_t colour = 0xffffffff, bool bold = false, int align = 0);
  void Operations(Image &out, const ViewState &state);
  void HelpPage(Image &out, const ViewState &state);
  void Finances(Image &out, const ViewState &state);
  void Statistics(Image &out, const ViewState &state);
  void Prices(Image &out, const ViewState &state);
  void Animals(Image &out, const ViewState &state);
  void Shop(Image &out, const ViewState &state);
  void Vehicles(Image &out, const ViewState &state);
  std::string Label(std::string_view key);
  void Text(Image &out, std::string_view value, int x, int y, int size = 28,
            uint32_t colour = 0xffffffff, bool bold = false);
  // Shared by every page's "still not ready" panel: shows "Loading..." (no
  // native string fits this per-panel warm-up state; see
  // farming_simulator_reader.cpp's kWarmupTimeout/kStaleAfterGoodTimeout for
  // when a page's *_loading flag is set instead of its *_ready flag) at the
  // exact position/size/colour the unavailable text always used, or that
  // unavailable text itself once warm-up has genuinely timed out or failed.
  void Unavailable(Image &out, bool loading, int x, int y, int size = 32,
                   std::string_view unavailable_text = "Data unavailable");
  void Button(Image &out, std::string_view label, int x, int y, int w, int h,
              bool enabled = true, std::string_view icon = {});
  // Garage Enter/Reset and map-selection Enter/Reset/Visit/Tag face (G3
  // critique Finding 5): the game's own pcButton/ingameMenuMapButton* grammar
  // -- charcoal fill, a visible border, a lime native icon, white bold text --
  // not a Primary/Secondary pair distinguished by fill colour (native never
  // does that; see Icon()'s icon_enter/icon_reset/icon_visit/icon_tag entries
  // for the confirmed source). The label is never shrunk to fit (LEGIBILITY.md:
  // show less instead); callers pass the 32px button size.
  void NativeActionButton(Image &out, std::string_view label,
                          std::string_view icon, int x, int y, int w, int h,
                          int size = 32);
  void Icon(Image &out, std::string_view name, int x, int y, int size = 40,
            uint32_t colour = 0xffffffff);
  bool AttributeIcon(Image &out, std::string_view label, int x, int y,
                     int size = 40);
  void Art(Image &out, std::string_view path, int x, int y, int w, int h,
           bool fit_alpha = false);
  // FILTERS popup content, drawn into whichever chrome the caller already
  // prepared (compact pane fill or expanded card panel+border): `x`/`y`/`w`
  // are the first row's top-left corner and content width, `row_h` its
  // height (92 compact, 70 expanded, UX-SPEC §A.3/A.4).
  void MapFiltersRows(Image &out, const ViewState &state, int x, int y, int w,
                      int row_h);
  // Expanded-mode floating overlay card (UX-SPEC §A.4): the panel/border
  // chrome plus MapFiltersRows above, anchored top-right under the Filters
  // button since expanded has no right pane to replace.
  void MapFiltersCard(Image &out, const ViewState &state);
  Assets assets_;
  Font font_;
};
// Classifies a live map marker into one of the 7 native InGameMenu.xml
// filter categories (0=Vehicles/Steerable, 1=Harvesters/Combine,
// 2=Trailers, 3=Tools, 4=Selling points/TipStations, 5=Field jobs,
// 6=Other), by matching `marker.kind` (the native MapHotspot CATEGORY_*
// enum name the reader resolves live -- see farming_simulator_map_reader.inc's
// category_names) against each onClick handler's own name fragment
// (onClickMapFilterSteerable/Combine/Trailer/Tools/TipStations/FieldJobs/
// Other). No per-category l10n string exists to match against instead (see
// UX-SPEC's own finding), and the live game resolves these constants itself
// at runtime, so this is the same category the native filter row itself
// gates -- RE-pending confirmation of the exact enum spellings against a
// live capture is flagged in the companion review, not assumed silently.
// Falls back to 6 (Other) for anything unrecognized, matching the native
// filter row's own catch-all.
int MapMarkerFilterIndex(const MapMarker &marker);
// Stacking order on the map: 0 tools/trailers, 1 everything else, 2 vehicles/harvesters.
// An attached header or trailer sits on its tractor/combine's own position, so the
// drivable vehicle must draw (and take the tap) on top of it.
int MapMarkerDrawRank(const MapMarker &marker);
bool OnViewAction(ViewState &state, std::string_view action, int64_t argument,
                  size_t topic_count, size_t animal_scroll_max = size_t(-1));
// Runtime on_pan payload: signed32 dx in the high word, signed32 dy in the low
// word, both in logical canvas pixels. Positive drag moves content with the
// finger, so the stored content offset changes by -dx/-dy.
bool OnScrollAction(ViewState &state, std::string_view action,
                    int64_t packed_payload, size_t topic_count,
                    size_t help_max, size_t animal_scroll_max);
// A cheap (hashed, not formatted-text) change-detection key: equal inputs
// hash equal, and in practice so do two different inputs only astronomically
// rarely, which is the same trade every other change-detection hash makes.
uint64_t SnapshotKey(const ViewState &state);
// See MapSnapshotKey's definition (farming_simulator_view.cpp) for what this
// covers and why it is separate from SnapshotKey.
uint64_t MapSnapshotKey(const ViewState &state);
// Fixed-decimal formatting with thousands separators (e.g. "1,234.56"). Shared
// by view.cpp's Value()/FloorValue(), shop_views.cpp's Number() and
// animals.cpp's Amount(), each of which keeps its own optional/NaN/sign
// fallback convention and calls this for the actual digit grouping.
std::string FormatGrouped(double value, int decimals = 0);
} // namespace farming_simulator
