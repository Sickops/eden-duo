// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Decoder/fit check: optional owned RomFS directory argument; outputs stay
// private.
#include "farming_simulator_view.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace farming_simulator;
namespace {
void Check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
size_t Read(void *ctx, const char *key, uint64_t offset, void *output,
            size_t size) {
  auto path = *static_cast<std::filesystem::path *>(ctx) / key;
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    return 0;
  auto length = in.tellg();
  if (!output)
    return length < 0 ? 0 : size_t(length);
  if (offset > uint64_t(length))
    return 0;
  in.seekg(offset);
  in.read(static_cast<char *>(output), size);
  return in.gcount();
}
void Save(const Image &image, const std::filesystem::path &path) {
  std::ofstream file(path, std::ios::binary);
  file << "P6\n" << image.w << " " << image.h << "\n255\n";
  for (size_t i = 0; i < image.rgba.size(); i += 4)
    file.write(reinterpret_cast<const char *>(image.rgba.data() + i), 3);
}
bool SameRegion(const Image &a, const Image &b, int left, int right,
                int top, int bottom) {
  if (a.w != b.w || a.h != b.h || left < 0 || right > int(a.w) ||
      top < 0 || bottom > int(a.h) || left > right || top > bottom)
    return false;
  for (int y = top; y < bottom; ++y) {
    auto begin = (size_t(y) * a.w + left) * 4;
    auto count = size_t(right - left) * 4;
    if (!std::equal(a.rgba.begin() + begin, a.rgba.begin() + begin + count,
                    b.rgba.begin() + begin))
      return false;
  }
  return true;
}
} // namespace
int main(int argc, char **argv) {
  try {
    ViewState state;
    ViewState layers;
    layers.page=Page::Operations;
    layers.snapshot.world_ready=true;
    layers.snapshot.map_layers=std::make_shared<MapLayers>();
    for(int expected : {1,2,3,0})
      Check(OnViewAction(layers,"map_cycle",1,0) && layers.map_layer==expected,
            "layer selector visits Crops, Growth, Soil and Map");
    Check(OnViewAction(layers,"map_cycle",-1,0) && layers.map_layer==3,
          "previous layer wraps Map to Soil");
    layers.snapshot.map_layers.reset();
    Check(!OnViewAction(layers,"map_cycle",1,0),"unavailable layers reject changes");
    // "scroll:map_legend" is gone: the legend list is the runtime's own
    // native "map-legend" ScrollRegion now and never sends that module
    // action at all.
    layers.map_layer = 1;
    layers.map_legend = true;
    Check(!OnScrollAction(layers, "scroll:map_legend",
                          int64_t((uint64_t(0) << 32) | uint32_t(-23)), 0, 0, 0),
          "Map legend drag is no longer a module action");
    auto scroll_payload = [](int32_t x, int32_t y) {
      return int64_t((uint64_t(uint32_t(x)) << 32) | uint32_t(y));
    };
    ViewState scroll;
    scroll.page = Page::Help;
    // "scroll:help_topics"/"scroll:help_article" are gone: the Guide is the
    // runtime's own native "help-topics"/"help-article[-static]"
    // ScrollRegions now (gen_manifest.py) and never sends either module
    // action, the same conversion Finance/Prices already made.
    Check(!OnScrollAction(scroll, "scroll:help_topics", scroll_payload(0, -37), 10, 900, 0),
          "Help topics drag is no longer a module action");
    Check(!OnScrollAction(scroll, "scroll:help_article", scroll_payload(0, -41), 10, 900, 0),
          "Help article drag is no longer a module action");
    scroll.page = Page::Finances;
    scroll.snapshot.world_ready = scroll.snapshot.finances_ready = true;
    scroll.snapshot.finance_rows.resize(20);
    // "scroll:finance"/"scroll:finance_x"/"scroll:finance_y" are gone: the
    // table is the runtime's own native two-axis "fs-finance" ScrollRegion
    // now (gen_manifest.py) and never sends any of those module actions.
    Check(!OnScrollAction(scroll, "scroll:finance", scroll_payload(-41, -27), 10, 900, 0),
          "Finance drag is no longer a module action");
    scroll.page = Page::Prices;
    scroll.snapshot.prices_ready = true;
    scroll.snapshot.price_crops.resize(8);
    scroll.snapshot.price_stations.resize(12);
    // "scroll:prices"/"scroll:prices_x"/"scroll:prices_y" are gone: the
    // matrix is the runtime's own native two-axis "fs-prices" ScrollRegion
    // now (gen_manifest.py) and never sends any of those module actions.
    Check(!OnScrollAction(scroll, "scroll:prices", scroll_payload(-50, -33), 10, 900, 0),
          "Prices drag is no longer a module action");
    ViewState areas;
    areas.snapshot.world_ready = true;
    areas.page = Page::Shop;
    areas.snapshot.shop_ready = true;
    areas.snapshot.shop_categories.resize(12);
    areas.snapshot.shop_items.resize(8);
    for (size_t i = 0; i < areas.snapshot.shop_items.size(); ++i) {
      areas.snapshot.shop_items[i].id = "scroll-item" + std::to_string(i);
      areas.snapshot.shop_items[i].specs.resize(12);
    }
    // "scroll:shop_filters" is gone: the filter list is the runtime's own
    // native "shop-filters" ScrollRegion now and never sends that module
    // action at all.
    Check(!OnScrollAction(areas, "scroll:shop_filters", scroll_payload(0, -23), 0, 0, 0),
          "Shop filters drag is no longer a module action");
    // "scroll:shop_grid" is gone: the catalog grid is the runtime's own
    // native ScrollRegion now and never sends that module action at all.
    Check(!OnScrollAction(areas, "scroll:shop_grid", scroll_payload(0, -29), 0, 0, 0),
          "Shop grid drag is no longer a module action");
    areas.shop_details = true;
    Check(OnScrollAction(areas, "scroll:shop_specs", scroll_payload(0, -31), 0, 0, 0) &&
              areas.shop_spec_offset == 31,
          "Shop specs drag in pixels");
    areas.page = Page::Vehicles;
    areas.snapshot.vehicles_ready = true;
    areas.snapshot.owned_vehicles.resize(8);
    for (auto &vehicle : areas.snapshot.owned_vehicles)
      vehicle.specs.resize(8);
    // "scroll:garage_list"/"scroll:garage_specs" are gone: both are the
    // runtime's own native "garage-list"/"garage-specs" ScrollRegions now
    // and never send either module action at all.
    Check(!OnScrollAction(areas, "scroll:garage_list", scroll_payload(0, -37), 0, 0, 0),
          "Garage list drag is no longer a module action");
    Check(!OnScrollAction(areas, "scroll:garage_specs", scroll_payload(0, -41), 0, 0, 0),
          "Garage specs drag is no longer a module action");
    areas.page = Page::Animals;
    areas.snapshot.animals_ready = true;
    areas.animal_feeding = true;
    // "scroll:animals" is gone: the needs pane is the runtime's own native
    // "animal-needs" ScrollRegion now and never sends that module action.
    Check(!OnScrollAction(areas, "scroll:animals", scroll_payload(0, -43), 0, 0, 300),
          "Animal feeding drag is no longer a module action");
    areas.page = Page::Statistics;
    areas.snapshot.statistics_ready = true;
    areas.snapshot.statistic_rows.resize(20);
    // "scroll:stats" is gone: the Statistics list is the runtime's own
    // native "statistic-rows" ScrollRegion now and never sends that module
    // action at all.
    Check(!OnScrollAction(areas, "scroll:stats", scroll_payload(0, -47), 0, 0, 0),
          "Stats drag is no longer a module action");
    areas.page = Page::Help;
    areas.time_scale_open = true;
    Check(!OnScrollAction(areas, "scroll:help_topics", scroll_payload(0, -1), 10, 900, 0),
          "Scroll gestures are refused while the time picker is open");
    Check(!OnViewAction(state, "buy", 0, 0), "transaction must refuse");
    Check(!OnViewAction(state, "zoom", 1, 0),
          "an unrecognised action (pan/zoom is runtime-native now) must refuse");
    Check(!OnViewAction(state, "page", 99, 0),
          "invalid destination must refuse");
    Check(!OnViewAction(state, "page", int(Page::Shop), 0),
          "no farm refuses management navigation");
    Check(OnViewAction(state, "page", int(Page::Help), 0) &&
              OnViewAction(state, "page", int(Page::Operations), 0),
          "static Help has a local Back destination");
    Check(XmlUnescape("A &amp; B &quot;C&quot;") == "A & B \"C\"",
          "XML entities");
    if (argc < 2) {
      std::cout << "Guard checks passed; native-asset checks skipped (no owned "
                   "RomFS path).\n";
      return 0;
    }
    std::filesystem::path assets_root{argv[1]};
    EdenDsmodHostApi host{};
    host.userdata = &assets_root;
    host.read_romfs = &Read;
    Assets assets{host};
    Font font;
    Check(font.Load(assets), "native font load");
    Check(font.Measure("AXIAL-FLOW 1660", 28, true) > 100,
          "native font metrics");
    std::vector<uint8_t> metrics;
    Check(assets.Read("shared/font/defaultFont.font", metrics),
          "font metrics source");
    metrics[8] = 255;
    metrics[9] = 255;
    Check(!font.Parse(metrics,
                      assets.Texture("shared/font/defaultFont.dds", false)),
          "corrupt font count must refuse");
    const auto &topics = assets.Help();
    Check(topics.size() == 61, "61 native Help topics");
    size_t text = 0, images = 0, scales = 0;
    std::set<std::string> categories;
    for (auto &topic : topics) {
      categories.insert(topic.category);
      for (auto &item : topic.items) {
        if (item.image) {
          ++images;
          Check(bool(assets.Texture(item.value)),
                "native Help illustration decode");
          if (item.height_scale != 1)
            ++scales;
        } else
          ++text;
      }
    }
    Check(categories.size() == 8 && text == 121 && images == 73 && scales == 5,
          "complete Help source counts");
    Check(assets.Map(1).fields.size() == 31,
          "native Goldcrest field coordinates");
    Check(assets.Map(2).fields.size() == 36,
          "native Sosnovka field coordinates");
    Check(!assets.Map(2).hotspots.empty(), "native map XML hotspots");
    View view{host};
    Image image;
    auto render = [&](std::string name) {
      auto start = std::chrono::steady_clock::now();
      Check(view.Render(state, image), "native page render");
      if (state.snapshot.world_ready && state.page == Page::Operations) {
        // The footer is hidden while the map is expanded, so the map gets
        // the full height instead of a dead grey band under it.
        const bool footer_expanded = state.map_fullscreen;
        bool text = false;
        for (int y = 1036; y < 1072; ++y)
          for (int x = 24; x < 132; ++x) {
            auto p = (size_t(y) * image.w + x) * 4;
            if (image.rgba[p] > 235 && image.rgba[p + 1] > 235 &&
                image.rgba[p + 2] > 235)
              text = true;
          }
        Check(footer_expanded ? !text : text,
              footer_expanded ? "expanded map must hide the footer"
                              : "ready-world MAP footer text visible");
      } else if (state.snapshot.world_ready) {
        bool text = false;
        for (int y = 1036; y < 1072; ++y)
          for (int x = 24; x < 132; ++x) {
            auto p = (size_t(y) * image.w + x) * 4;
            if (image.rgba[p] > 235 && image.rgba[p + 1] > 235 &&
                image.rgba[p + 2] > 235)
              text = true;
          }
        Check(text, "ready-world footer text visible");
      }
      auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count();
      std::cout << name << ": " << ms << " ms, " << image.rgba.size()
                << " output bytes\n";
      if (argc > 2)
        Save(image, std::filesystem::path{argv[2]} / (name + ".ppm"));
    };
    render("unavailable");
    state.snapshot.phase = Phase::Title;
    render("title");
    auto pixel = [&](int x, int y, unsigned char grey) {
      const auto p = (size_t(y) * image.w + x) * 4;
      return image.rgba[p] == grey && image.rgba[p + 1] == grey &&
             image.rgba[p + 2] == grey;
    };
    Check(pixel(500, 1010, 47), "title has no farm footer");
    state.page = Page::Shop;
    render("title-preserved-shop");
    Check(pixel(500, 1010, 47), "title hides a preserved management page");
    state.page = Page::Help;
    auto static_help_key = SnapshotKey(state);
    render("title-help");
    Check(pixel(20, 990, 0), "static Help provides Back footer");
    state.snapshot.world_ready = true;
    Check(static_help_key != SnapshotKey(state),
          "Help footer invalidates on farm readiness");
    state.snapshot.world_ready = false;
    Check(OnViewAction(state, "page", int(Page::Operations), 61),
          "title Help Back works");
    state.snapshot.phase = Phase::Loading;
    render("loading");
    Check(pixel(500, 1010, 47), "loading has no farm footer");
    state.snapshot.world_ready = false;
    state.page = Page::Help;
    render("help");
    // The topics list and article body are no longer part of fs.ui.frame:
    // they are the runtime's native "help-topics"/"help-article[-static]"
    // ScrollRegions now (gen_manifest.py), served as separate module images
    // (View::RenderHelpTopicRow/RenderHelpArticle). render("help") above
    // only exercises the static chrome that is left (list background strip
    // + the selected topic's category/title header); the list and article
    // are spot-checked directly below, same convention as
    // RenderShopRow/RenderPriceRow/RenderFinanceRow.
    Check(OnViewAction(state, "help_topic", 3, 61) && state.help_topic == 3,
          "help_topic picks by absolute index now (no fractional-offset "
          "slot math left)");
    state.help_topic = 0;
    Image help_topic_row;
    Check(view.RenderHelpTopicRow(state, 0, true, help_topic_row) &&
              help_topic_row.w == 352 && help_topic_row.h == HelpTopicRowHeight,
          "help topic row 0 (active) renders at the fixed row size");
    if (argc > 2)
      Save(help_topic_row,
           std::filesystem::path{argv[2]} / "help-topic-row-0-active-fixture.ppm");
    Check(view.RenderHelpTopicRow(state, 60, false, help_topic_row),
          "help topic row 60 (last, inactive) renders");
    if (argc > 2)
      Save(help_topic_row,
           std::filesystem::path{argv[2]} / "help-topic-row-60-fixture.ppm");
    Check(!view.RenderHelpTopicRow(state, 61, false, help_topic_row),
          "help topic row refuses an out-of-range index");
    Image help_article;
    int help_article_h = 0;
    Check(view.RenderHelpArticle(state, 0, help_article, &help_article_h) &&
              help_article.w == 832 && help_article.h == int(HelpArticleCap) &&
              help_article_h > 0 && help_article_h <= int(HelpArticleCap),
          "help article renders at the fixed cap size with a measured "
          "content height");
    if (argc > 2)
      Save(help_article,
           std::filesystem::path{argv[2]} / "help-article-0-fixture.ppm");
    Check(!view.RenderHelpArticle(state, 61, help_article, nullptr),
          "help article refuses an out-of-range topic index");
    auto help_topic_key = SnapshotKey(state);
    state.help_topic = 1;
    Check(help_topic_key != SnapshotKey(state),
          "picking a different topic still invalidates fs.ui.frame (the "
          "header still shows its category/title) -- only the list/article "
          "content itself moved out to RenderHelpTopicRow/RenderHelpArticle");
    state.help_topic = 0;
    // Explicit synthetic fixture only in this check executable, never the
    // module.
    state.page = Page::Operations;
    state.snapshot.phase = Phase::World;
    state.snapshot.world_ready = true;
    state.snapshot.map_id = 2;
    state.snapshot.money = 25000;
    state.snapshot.currency_symbol = "€";
    state.snapshot.time_minutes = 447;
    state.snapshot.time_scale = 5;
    state.snapshot.time_scale_change_allowed = true;
    state.snapshot.player_x = 348;
    state.snapshot.player_z = 348;
    state.snapshot.player_heading = 3.141592653589793;
    state.snapshot.world_ready = true;
    state.page = Page::Help;
    render("help-world");
    state.help_topic = 52;
    render("help-long-topic");
    int help_article_long_h = 0;
    Check(view.RenderHelpArticle(state, 52, help_article, &help_article_long_h) &&
              help_article_long_h > help_article_h,
          "a long topic measures a taller content height than topic 0");
    if (argc > 2)
      Save(help_article,
           std::filesystem::path{argv[2]} / "help-article-52-long-fixture.ppm");
    state.help_topic = 0;
    state.page = Page::Operations;
    state.snapshot.vehicle_ready = true;
    state.snapshot.vehicle_name = "1660 Axial-Flow";
    state.snapshot.vehicle_image = "data/store/store_caseIH1660.dds";
    state.snapshot.speed_kmh = 5;
    state.snapshot.fuel_l = 350;
    state.snapshot.fuel_capacity_l = 350;
    state.snapshot.fill_l = 3456;
    state.snapshot.fill_capacity_l = 6400;
    state.snapshot.fill_name = "WHEAT";
    state.snapshot.fill_icon = "dataS2/menu/hud/fillTypes/hud_fill_wheat.dds";
    render("operations-fixture");
    render("operations-warm");
    auto time_key = SnapshotKey(state);
    Check(OnViewAction(state, "time_picker", 0, 61), "time picker opens");
    Check(time_key != SnapshotKey(state), "picker invalidates rendered frame");
    render("time-picker-fixture");
    Check(OnViewAction(state, "time_picker", 0, 61), "time picker closes");
    state.snapshot.time_scale = 120;
    render("time-120-fixture");
    state.snapshot.time_scale = 5;
    state.snapshot.fuel_l = 349.999;
    auto fuel_key = SnapshotKey(state);
    state.snapshot.fuel_l = 350.001;
    Check(fuel_key != SnapshotKey(state),
          "fuel display floor boundary revision");
    state.snapshot.fuel_l = 350;

    // Map pan/zoom/follow/Reset are the runtime's native pan_zoom view state
    // now (never a module action or a ViewState field): see
    // farming_simulator_view.h/.cpp and 0100D04004176000.cpp. What remains
    // module-side is Expand/Restore (an independent layout toggle, unrelated
    // to pan/zoom) and the map's own image (RenderMapBase), on its own
    // change-detection key (MapSnapshotKey) so player/marker movement no
    // longer invalidates the chrome frame at all.
    auto compact_key = SnapshotKey(state);
    Check(OnViewAction(state, "map_fullscreen", 0, 61) && state.map_fullscreen,
          "map expansion toggles independent companion state");
    Check(SnapshotKey(state) != compact_key,
          "expansion repaints the chrome frame (selector/footer move)");
    render("operations-expanded-fixture");
    Check(OnViewAction(state, "map_fullscreen", 0, 61) && !state.map_fullscreen,
          "map restores the vehicle pane");

    Image map_image;
    Check(view.RenderMapBase(state, map_image), "map base image renders");
    Check(map_image.w > 0 && map_image.h > 0 &&
              std::abs(double(map_image.w) / map_image.h - MapCanvasW / MapCanvasH) < 0.01,
          "map base image keeps the padded canvas aspect (no stretch)");
    auto base_map_key = MapSnapshotKey(state);
    Check(SnapshotKey(state) == compact_key,
          "map's own content never touches the chrome frame's key");
    Image reset_image;
    Check(view.RenderResetButton(reset_image), "map Reset button image renders");
    Check(reset_image.w == 180 && reset_image.h == 88, "Reset button image is 180x88");
    Check(std::any_of(reset_image.rgba.begin(), reset_image.rgba.end(),
                      [](uint8_t b) { return b != 0; }),
          "Reset button image is non-empty (opaque black box + white text, not all-transparent)");
    // Player marker (G3 critique Finding 8/"G": the ring must sit OUTSIDE the native glyph,
    // not replace it -- grown from 60x60 so a ring at r=36..42 clears the glyph's own corner
    // reach; see RenderMapArrow's own doc comment).
    Image arrow_image;
    Check(view.RenderMapArrow(arrow_image), "map arrow image renders");
    Check(arrow_image.w == 84 && arrow_image.h == 84, "map arrow image is 84x84");
    if (argc > 2)
      Save(arrow_image, std::filesystem::path{argv[2]} / "player-marker-fixture.ppm");
    MapMarker marker;marker.kind="STEERABLE";marker.image="dataS2/menu/hud/mapHotspots_256.dds";
    marker.x=300;marker.z=200;marker.sx=2;marker.sy=2;marker.w=marker.h=60;
    marker.draw_w=marker.draw_h=28;marker.color=0x70c0ffff;
    state.snapshot.map_markers.push_back(marker);
    Check(MapSnapshotKey(state) != base_map_key, "native marker data repaints the map image");
    Check(SnapshotKey(state) == compact_key, "marker data still never touches the chrome frame");
    auto marker_key = MapSnapshotKey(state);
    state.snapshot.map_markers[0].x += 5;
    Check(MapSnapshotKey(state) != marker_key, "moving marker invalidates the map image");
    Check(view.RenderMapBase(state, map_image), "map base image renders with a marker");
    render("operations-markers-fixture");

    // A selected layer that failed to load used to silently fall through to
    // whatever Operations() draws next, with fs.ui.layer_error published but
    // never shown anywhere (ENGINEERING hardening #8). state.snapshot.map_layers
    // is still null/unset here (never populated for `state` above), so
    // state.map_layer=1 alone reproduces the "selected but unavailable" case.
    state.map_layer = 1;
    state.snapshot.map_layer_error = "Soil data unreadable";
    render("map-layer-error-fixture");
    state.snapshot.map_layer_error.clear();
    render("map-layer-error-no-message-fixture");
    state.map_layer = 0;

    // FILTERS popup (owner request, UX-SPEC 2026-10-17 review A: "beside
    // expand, add filters that pop up ... that can toggle map markers like
    // vehicle, field, etc."). state.snapshot.map_markers still holds the
    // STEERABLE marker from just above.
    Check(MapMarkerFilterIndex(state.snapshot.map_markers[0]) == 0,
          "STEERABLE marker classifies as the Vehicles filter");
    MapMarker field_job;
    field_job.kind = "FIELDJOBS";
    field_job.image = "dataS2/menu/hud/mapHotspots_256.dds";
    field_job.x = 100;field_job.z = 100;field_job.sx = 2;field_job.sy = 2;
    field_job.w = field_job.h = 60;field_job.draw_w = field_job.draw_h = 28;
    field_job.color = 0xffffffff;
    Check(MapMarkerFilterIndex(field_job) == 5,
          "FIELDJOBS marker classifies as the Field jobs filter");
    MapMarker unknown;
    unknown.kind = "DEFAULT";
    Check(MapMarkerFilterIndex(unknown) == 6,
          "an unrecognized category falls back to Other, same as the native filterButtonOther catch-all");
    for (auto [kind, expected] : {std::pair<const char *, int>{"VEHICLE_COMBINE", 1},
                                  {"VEHICLE_TRAILER", 2}, {"VEHICLE_TOOL", 3},
                                  {"VEHICLE_STEERABLE", 0}}) {
      MapMarker vehicle;
      vehicle.kind = kind;
      Check(MapMarkerFilterIndex(vehicle) == expected,
            "native VEHICLE_* kinds classify by their specific type, not as generic Vehicles");
    }
    {
      MapMarker combine, header;
      combine.kind = "VEHICLE_COMBINE";
      header.kind = "VEHICLE_TOOL";
      Check(MapMarkerDrawRank(combine) > MapMarkerDrawRank(header),
            "a harvester draws above the header attached at its own position");
    }
    state.snapshot.map_markers.push_back(field_job);
    Check(!state.map_filters_open, "filters start closed");
    Check(std::all_of(state.map_filters.begin(), state.map_filters.end(),
                      [](bool b) { return b; }),
          "filters default all ON, matching the native default");
    render("map-filters-closed-fixture"); // top bar with the new Filters button, popup closed
    auto chrome_key = SnapshotKey(state);
    Check(OnViewAction(state, "map_filters_open", 0, 61) && state.map_filters_open,
          "filters popup opens");
    Check(SnapshotKey(state) != chrome_key, "filters popup invalidates the chrome frame");
    render("map-filters-open-fixture");
    Check(OnViewAction(state, "time_picker", 0, 61) && state.time_scale_open &&
              !state.map_filters_open,
          "opening the time picker closes the filters popup (same pane slot, last tap wins)");
    Check(OnViewAction(state, "time_picker", 0, 61) && !state.time_scale_open,
          "time picker closes again");
    Check(OnViewAction(state, "map_filters_open", 0, 61) && state.map_filters_open,
          "filters popup reopens");
    Check(OnViewAction(state, "map_fullscreen", 0, 61) && state.map_fullscreen,
          "expand while the filters popup is open (orthogonal controls)");
    render("map-filters-expanded-fixture");
    Check(OnViewAction(state, "map_fullscreen", 0, 61) && !state.map_fullscreen,
          "restore while the filters popup is open");
    Check(!OnViewAction(state, "map_filter_toggle", 7, 61),
          "filter row index is bounds-checked");
    auto filters_map_key = MapSnapshotKey(state);
    Check(OnViewAction(state, "map_filter_toggle", 5, 61) && !state.map_filters[5],
          "Field jobs filter toggles off");
    Check(MapSnapshotKey(state) != filters_map_key,
          "toggling a filter repaints the map image (markers), not just the chrome frame");
    Check(view.RenderMapBase(state, map_image),
          "map base image renders with a filtered-off category marker present");
    render("map-filters-fieldjobs-off-fixture");
    Check(OnViewAction(state, "page", int(Page::Shop), 61) && !state.map_filters_open,
          "switching pages closes the filters popup");
    Check(OnViewAction(state, "page", int(Page::Operations), 61), "back to the Map tab");
    Check(!state.map_filters_open, "filters popup stayed closed across the round trip");
    Check(!state.map_filters[5],
          "filter choice itself persists across tab switches (view preference, not reset)");
    state.map_filters[5] = true;

    state.snapshot.map_markers.clear();
    state.snapshot.vehicle_ready = false;
    render("on-foot");
    auto model =
        assets.Vehicle("data/vehicles/steerable/caseIH/caseIH1660.xml");
    Check(model.name == "1660 Axial-Flow" &&
              model.image == "data/store/store_caseIH1660.png",
          "native vehicle XML resolver");
    Check(assets.Vehicle("data/../private.xml").name.empty(),
          "vehicle traversal must refuse");
    state.page = Page::Prices;
    state.snapshot.prices_ready = true;
    // 20 crops: more than one PriceColumnChunk(16) worth, so chunk 1 (crops
    // 16..19) actually has real content to exercise the second chunk image
    // (farming_simulator_view.h's PriceColumnChunks split) rather than
    // staying entirely transparent.
    for (int i = 0; i < 20; ++i) {
      PriceCrop crop;
      crop.id = "crop" + std::to_string(i);
      crop.name = i % 2 ? "Barley" : "Wheat";
      crop.icon = i % 2 ? "dataS2/menu/hud/fillTypes/hud_fill_barley.dds"
                        : "dataS2/menu/hud/fillTypes/hud_fill_wheat.dds";
      if (i < 3)
        crop.silo_l = i == 0 ? 7371 : 0;
      state.snapshot.price_crops.push_back(crop);
    }
    for (int i = 0; i < 11; ++i) {
      PriceStation station;
      station.id = "station" + std::to_string(i);
      station.name = i % 2 ? "Goldcrest Pacific Grain" : "Wright Agribusiness";
      for (int j = 0; j < 20; ++j) {
        PriceQuote quote;
        quote.accepted = j != 2;
        if (j != 3)
          quote.price_per1000 = i == 0 ? 0 : 660 + i;
        quote.trend = i % 2 ? PriceTrend::Rising : PriceTrend::Falling;
        quote.highlight = i % 2 ? PriceHighlight::High : PriceHighlight::Low;
        quote.great_demand = i == 4 && j == 0;
        station.quotes.push_back(quote);
      }
      state.snapshot.price_stations.push_back(station);
    }
    // The crop header row, station column and price grid itself are no
    // longer part of fs.ui.frame: they are the runtime's native "fs-prices"
    // two-axis ScrollRegion now (gen_manifest.py), served as separate module
    // images (View::RenderPriceHeader/RenderPriceStations/RenderPriceRow).
    // render("prices-fixture") below only exercises the static title chrome
    // that is left; the grid itself is spot-checked directly below.
    render("prices-fixture");
    // The header row and each price row are now served in PriceColumnChunks
    // column chunks (farming_simulator_view.h) -- each chunk a fixed
    // PriceColumnChunk(16)*212 = 3392px, comfortably under the runtime's
    // 4096px-per-axis module image limit that the full 32-column image
    // (6784px) was always silently refused by. Chunk 0 and chunk 1 are both
    // exercised below (the fixture has 20 crops, so chunk 1 has real
    // content: crops 16..19).
    Image price_header;
    state.snapshot.price_crops[0].silo_l = 987654321;
    Check(view.RenderPriceHeader(state, 0, price_header) &&
              price_header.w == 212 * 16 && price_header.h == 240,
          "price header chunk 0 renders at the fixed column chunk size");
    if (argc > 2)
      Save(price_header,
           std::filesystem::path{argv[2]} / "prices-header-large-silo-fixture.ppm");
    state.snapshot.price_crops[0].silo_l = 7371;
    Check(view.RenderPriceHeader(state, 0, price_header),
          "price header chunk 0 re-renders after a silo amount change");
    if (argc > 2)
      Save(price_header, std::filesystem::path{argv[2]} / "prices-header-fixture.ppm");
    Image price_header_chunk1;
    Check(view.RenderPriceHeader(state, 1, price_header_chunk1) &&
              price_header_chunk1.w == 212 * 16 && price_header_chunk1.h == 240,
          "price header chunk 1 (crops 16..19) renders at the fixed column chunk size");
    if (argc > 2)
      Save(price_header_chunk1,
           std::filesystem::path{argv[2]} / "prices-header-chunk1-fixture.ppm");
    Check(!view.RenderPriceHeader(state, 2, price_header_chunk1),
          "price header refuses an out-of-range chunk index");
    Image price_stations_column;
    Check(view.RenderPriceStations(state, price_stations_column) &&
              price_stations_column.w == 352 &&
              price_stations_column.h == 84 * 40,
          "price station column renders at the fixed row cap size");
    if (argc > 2)
      Save(price_stations_column,
           std::filesystem::path{argv[2]} / "prices-stations-fixture.ppm");
    Image price_row;
    Check(view.RenderPriceRow(state, 0, 0, price_row) &&
              price_row.w == 212 * 16 && price_row.h == 84,
          "price row 0 chunk 0 renders at the fixed column chunk size");
    if (argc > 2)
      Save(price_row, std::filesystem::path{argv[2]} / "prices-row-0-fixture.ppm");
    Image price_row_chunk1;
    Check(view.RenderPriceRow(state, 0, 1, price_row_chunk1) &&
              price_row_chunk1.w == 212 * 16 && price_row_chunk1.h == 84,
          "price row 0 chunk 1 (crops 16..19) renders at the fixed column chunk size");
    if (argc > 2)
      Save(price_row_chunk1,
           std::filesystem::path{argv[2]} / "prices-row-0-chunk1-fixture.ppm");
    Check(view.RenderPriceRow(state, 4, 0, price_row),
          "price row 4 (great demand quote) renders");
    if (argc > 2)
      Save(price_row, std::filesystem::path{argv[2]} / "prices-row-4-great-demand-fixture.ppm");
    Check(!view.RenderPriceRow(state, 11, 0, price_row),
          "price row refuses an out-of-range station index");
    Check(!view.RenderPriceRow(state, 0, 2, price_row),
          "price row refuses an out-of-range chunk index");
    auto price_key = SnapshotKey(state);
    state.snapshot.price_stations[0].quotes[0].great_demand = true;
    Check(price_key == SnapshotKey(state),
          "a quote change no longer invalidates fs.ui.frame -- the grid "
          "lives in RenderPriceRow's own image now, not this frame");
    // "price_columns"/"price_stations" are gone: the matrix never receives
    // either module action any more (OnViewAction has no Page::Prices case
    // left at all -- the runtime's own "fs-prices" ScrollRegion drag/fling
    // owns both axes).
    Check(!OnViewAction(state, "price_columns", 1, 61),
          "price columns is no longer a module action");
    Check(!OnViewAction(state, "price_stations", 1, 61),
          "price stations is no longer a module action");
    state.snapshot.prices_ready = false;
    render("prices-unavailable");
    state.page = Page::Finances;
    // Still warming up (world just loaded, the 1Hz management snapshot
    // hasn't produced finance data yet): "Loading..." in the same spot/
    // styling the unavailable text uses, not "Data unavailable".
    state.snapshot.finances_loading = true;
    render("finances-loading-fixture");
    state.snapshot.finances_loading = false;
    state.snapshot.finances_ready = true;
    state.snapshot.finance_day_labels = {"Thursday", "Friday", "Saturday",
                                         "Sunday", "ui_today"};
    for (int i = 0; i < 27; ++i) {
      FinanceRow row;
      row.label = i % 2 ? "Vehicle Running Costs" : "New Vehicles";
      row.amounts[4] = i == 0 ? -145000 : 0;
      state.snapshot.finance_rows.push_back(row);
    }
    state.snapshot.loan = 75000;
    state.snapshot.finance_saldo = -50000;
    // The day header, row-label column, grid rows and totals row's day
    // amounts are no longer part of fs.ui.frame: they are the runtime's
    // native "fs-finance" two-axis ScrollRegion now (gen_manifest.py),
    // served as separate module images. render("finances-fixture") below
    // only exercises the static chrome that is left (summary boxes,
    // heading, rule, "Total" label); the grid itself is spot-checked
    // directly below.
    render("finances-fixture");
    Image finance_header, finance_labels, finance_row, finance_totals;
    Check(view.RenderFinanceHeader(state, finance_header) &&
              finance_header.w == 1180 && finance_header.h == 76,
          "finance header renders at the fixed 5-day content size");
    if (argc > 2)
      Save(finance_header, std::filesystem::path{argv[2]} / "finances-header-fixture.ppm");
    Check(view.RenderFinanceLabels(state, 0, finance_labels) &&
              finance_labels.w == FinanceLabelWidth &&
              finance_labels.h == FinanceRowHeight * int(FinanceLabelChunk),
          "finance label column chunk 0 renders at the fixed chunk size");
    Check(finance_labels.w <= 4096 && finance_labels.h <= 4096 &&
              size_t(finance_labels.w) * finance_labels.h * 4 <= 16 * 1024 * 1024,
          "finance label chunk stays within the runtime's module image limits");
    if (argc > 2)
      Save(finance_labels, std::filesystem::path{argv[2]} / "finances-labels-fixture.ppm");
    Check(view.RenderFinanceLabels(state, FinanceLabelChunks - 1, finance_labels) &&
              finance_labels.w == FinanceLabelWidth,
          "finance label column renders its last chunk");
    Check(!view.RenderFinanceLabels(state, FinanceLabelChunks, finance_labels),
          "finance label column refuses an out-of-range chunk index");
    Check(view.RenderFinanceRow(state, 0, finance_row) &&
              finance_row.w == 1180 && finance_row.h == 76,
          "finance row 0 renders at the fixed 5-day content size");
    if (argc > 2)
      Save(finance_row, std::filesystem::path{argv[2]} / "finances-row-0-fixture.ppm");
    Check(!view.RenderFinanceRow(state, 27, finance_row),
          "finance row refuses an out-of-range row index");
    Check(view.RenderFinanceTotals(state, finance_totals) &&
              finance_totals.w == 1180 && finance_totals.h == 44,
          "finance totals row renders at the fixed 5-day content size");
    if (argc > 2)
      Save(finance_totals, std::filesystem::path{argv[2]} / "finances-totals-fixture.ppm");
    Check(OnViewAction(state, "time_picker", 0, 61),
          "time picker opens on a page with no right pane");
    render("finances-time-picker-fixture");
    Check(OnViewAction(state, "time_picker", 0, 61),
          "time picker closes on a page with no right pane");
    state.snapshot.finance_rows[0].amounts[4] = -987654321;
    Check(font.Measure("-987,654,321", 32) <= 220,
          "nine-digit signed money fits finance column without smaller type");
    Check(view.RenderFinanceRow(state, 0, finance_row),
          "finance row re-renders after a nine-digit amount");
    if (argc > 2)
      Save(finance_row, std::filesystem::path{argv[2]} / "finances-row-0-large-fixture.ppm");
    auto key = SnapshotKey(state);
    state.snapshot.finance_rows[0].amounts[4] = 0;
    Check(key == SnapshotKey(state),
          "a row amount change no longer invalidates fs.ui.frame -- the "
          "grid lives in RenderFinanceRow's own image now, not this frame");
    // "finance_scroll"/"finance_days" are gone: the table never receives
    // either module action any more (the runtime's own "fs-finance"
    // ScrollRegion drag/fling owns both axes).
    Check(!OnViewAction(state, "finance_scroll", 1, 61),
          "finance scroll is no longer a module action");
    Check(!OnViewAction(state, "finance_days", 1, 61),
          "finance days is no longer a module action");
    state.page = Page::Statistics;
    state.snapshot.statistics_ready = true;
    for (int i = 0; i < 23; ++i) {
      StatisticRow row;
      row.label = i % 2 ? "Worked time" : "Worked hectares";
      if (i % 2) {
        row.session_text = "00:05";
        row.total_text = "12:45";
      } else {
        row.unit = "ha";
        row.session = 0;
        row.total = 12.5;
        row.digits = 2;
      }
      state.snapshot.statistic_rows.push_back(row);
    }
    state.snapshot.statistic_rows[2].label = "Fertilized hectares";
    state.snapshot.statistic_rows[2].unit = "ha";
    // The row list itself is no longer part of fs.ui.frame: it is the
    // runtime's native "statistic-rows" ScrollRegion now (gen_manifest.py),
    // served as separate module images (View::RenderStatisticRow).
    // render("statistics-fixture") below only exercises the static header
    // that is left; the rows are spot-checked directly below, same
    // convention as RenderFinanceRow/RenderPriceRow.
    render("statistics-fixture");
    // "statistics_scroll" is gone: the list never receives that module
    // action any more (the runtime's own "statistic-rows" ScrollRegion
    // drag/fling owns it).
    Check(!OnViewAction(state, "statistics_scroll", 1, 61),
          "statistics scroll is no longer a module action");
    Image stat_row;
    Check(view.RenderStatisticRow(state, 0, stat_row) &&
              stat_row.w == StatisticRowWidth && stat_row.h == StatisticRowHeight,
          "statistic row 0 renders at the fixed row size");
    if (argc > 2)
      Save(stat_row, std::filesystem::path{argv[2]} / "statistics-row-0-fixture.ppm");
    Check(view.RenderStatisticRow(state, 22, stat_row),
          "statistic row 22 (last) renders");
    if (argc > 2)
      Save(stat_row, std::filesystem::path{argv[2]} / "statistics-row-22-fixture.ppm");
    Check(!view.RenderStatisticRow(state, 23, stat_row),
          "statistic row refuses an out-of-range index");
    auto stat_key = SnapshotKey(state);
    state.snapshot.statistic_rows[0].total = 999;
    Check(stat_key == SnapshotKey(state),
          "a row value change no longer invalidates fs.ui.frame -- the list "
          "lives in RenderStatisticRow's own image now, not this frame");
    state.snapshot.statistics_ready = false;
    render("statistics-unavailable");
    state.page = Page::Animals;
    state.snapshot.animals_ready = true;
    // Static layout fixtures use real native art/text. Their values are test
    // data, never presented as runtime evidence. Empty herds still have native
    // resources.
    for (auto id : {"sheep", "pig", "cow"}) {
      AnimalGroup animal;
      animal.id = id;
      animal.name = assets.Localize("ui_statisticView_" + std::string{id});
      animal.image = "data/store/store_" + std::string{id} + ".dds";
      animal.count = 0;
      animal.productivity = 0;
      animal.help_text = assets.Localize("statistic_animalRequirementsMain");
      auto need = [&](std::string label, double capacity) {
        animal.needs.push_back({label, "l", 0., capacity});
      };
      need("statistic_water", 3000);
      if (animal.id != "sheep")
        need("statistic_strawStorage", 3000);
      if (animal.id == "sheep") {
        need("Grass / Hay", 6000);
        animal.help_text += "\n100% Grass / Hay";
        animal.outputs.push_back({"Wool", "l", 0., {}, 0.});
      } else {
        animal.outputs.push_back({"statistic_liquidManureStorage", "l", 0.});
        animal.outputs.push_back({"statistic_manureStorage", "l", 0.});
        if (animal.id == "pig") {
          need("Base food (corn)", 6000);
          need("Grain (wheat, barley)", 6000);
          need("Protein food (soybeans, canola, sunflowers)", 6000);
          need("Root crops (potatoes, sugar beets)", 6000);
          animal.help_text +=
              "\n50% Base food (corn)\n25% Grain (wheat, barley)"
              "\n20% Protein food (soybeans, canola, sunflowers)"
              "\n5% Root crops (potatoes, sugar beets)";
        } else {
          need("Grass / Hay", 6000);
          need("Silage", 6000);
          need("Power food", 6000);
          animal.outputs.push_back({"statistic_milkStorage", "l", 0.});
          animal.help_text += "\n20% Grass / Hay\n30% Silage\n50% Power food";
        }
      }
      state.snapshot.animals.push_back(animal);
    }
    render("animals-empty-fixture");
    Check(!OnViewAction(state, "time_picker", 0, 61),
          "time picker stays unavailable on Animals (no top-bar band)");
    for (auto &animal : state.snapshot.animals) {
      animal.count = animal.id == "pig" ? 64 : 24;
      animal.productivity = 95;
      animal.cleanliness = 80;
      animal.reproduction_interval_minutes = 345;
      animal.next_animal_minutes = 123;
      for (size_t i = 0; i < animal.needs.size(); ++i)
        animal.needs[i].level = i == 0 ? 100 : 1800;
      for (auto &output : animal.outputs)
        output.level = 12678;
      if (animal.id == "sheep")
        animal.outputs[0].percent = 50;
    }
    render("animals-stress-fixture");
    Check(OnViewAction(state, "animal_species", 1, 61), "select native pigs");
    render("animals-pigs-overview");
    Check(OnViewAction(state, "animal_detail", 1, 61), "select Feeding view");
    // The needs list and its trailing help text/illustration are no longer
    // baked here: together they are the runtime's native "animal-needs"
    // ScrollRegion now (gen_manifest.py), served as AnimalNeedsStripCap
    // fixed-height strip images (View::RenderAnimalNeedsStrip -- a single
    // AnimalNeedsCap-tall image was always over the runtime's module image
    // limits, see AnimalNeedsStripCap's own comment in
    // farming_simulator_view.h). render("animals-pigs-feeding") below only
    // exercises the static species selector/tab chrome that is left; the
    // pane itself is spot-checked directly below, same convention as
    // RenderHelpArticle.
    render("animals-pigs-feeding");
    // "animal_scroll" is gone: the needs pane never receives that module
    // action any more (the runtime's own "animal-needs" ScrollRegion
    // drag/fling owns it).
    Check(!OnViewAction(state, "animal_scroll", 1, 61),
          "animal content scroll is no longer a module action");
    Image animal_needs;
    int animal_needs_h = 0;
    Check(view.RenderAnimalNeedsStrip(state, 0, animal_needs, &animal_needs_h) &&
              animal_needs.w == 1200 && animal_needs.h == AnimalNeedsStripHeight &&
              animal_needs_h > 0 && animal_needs_h <= int(AnimalNeedsCap),
          "animal needs pane strip 0 renders at the fixed strip size with a "
          "measured TOTAL content height");
    if (argc > 2)
      Save(animal_needs,
           std::filesystem::path{argv[2]} / "animals-pigs-needs-fixture.ppm");
    int animal_needs_h2 = 0;
    Image animal_needs_strip1;
    Check(view.RenderAnimalNeedsStrip(state, 1, animal_needs_strip1, &animal_needs_h2) &&
              animal_needs_strip1.w == 1200 &&
              animal_needs_strip1.h == AnimalNeedsStripHeight &&
              animal_needs_h2 == animal_needs_h,
          "animal needs pane strip 1 renders with the same measured total "
          "content height as strip 0");
    Check(!view.RenderAnimalNeedsStrip(state, AnimalNeedsStripCap, animal_needs, nullptr),
          "animal needs pane refuses an out-of-range strip index");
    auto animal_key = SnapshotKey(state);
    state.snapshot.animals[1].needs[0].level = 999;
    Check(animal_key == SnapshotKey(state),
          "a needs value change no longer invalidates fs.ui.frame -- the "
          "pane lives in RenderAnimalNeedsStrip's own image now, not this frame");
    state.snapshot.animals_ready = false;
    Check(!view.RenderAnimalNeedsStrip(state, 0, animal_needs, nullptr),
          "animal needs pane refuses when animals are not ready");
    render("animals-unavailable");
    state.page = Page::Shop;
    state.snapshot.shop_ready = true;
    for (int i = 0; i < 41; ++i)
      state.snapshot.shop_categories.push_back(
          {"category" + std::to_string(i),
           i == 2   ? "category_forageHarvestersCutters"
           : i == 3 ? "category_potatoHarvesting"
           : i % 2  ? "Harvesters"
                    : "Tractors",
           i == 2 || i == 3 ? ""
           : i % 2          ? "data/store/categories/category_harvesters.dds"
                            : "data/store/categories/category_tractors.dds"});
    for (int i = 0; i < 80; ++i)
      state.snapshot.shop_brands.push_back(
          {"brand" + std::to_string(i), i % 2 ? "New Holland" : "Case IH",
           i % 2 ? "data/store/brands/brand_newholland.dds"
                 : "data/store/brands/brand_caseIH.dds"});
    for (int i = 0; i < 301; ++i) {
      ShopItem item;
      item.id = "item" + std::to_string(i);
      item.name =
          i % 2
              ? "1660 Axial-Flow"
              : "Very long harvesting machine model name for native layout fit";
      item.brand_id = "brand" + std::to_string(i - i % 2);
      item.category_id = "category" + std::to_string(i % 41);
      item.image = "data/store/store_caseIH1660.dds";
      if (i % 2)
        item.price = 145000;
      for (int j = 0; j < 12; ++j)
        item.specs.push_back({j % 2 ? "shop_maxPower" : "shop_capacity",
                              j % 2 ? "139 kW / 190 hp" : "6,400 l"});
      state.snapshot.shop_items.push_back(item);
    }
    render("shop-fixture");
    Check(OnViewAction(state, "shop_item", 1, 61) &&
              state.shop_selected == "item1",
          "local shop selection");
    for (int i = 0; i < 100; ++i)
      Check(OnViewAction(state, "shop_specs", 1, 61), "shop spec scroll");
    Check(state.shop_spec_offset == 12 * 104 - 728, "all shop specs reachable");
    render("shop-detail-bottom-fixture");
    state.shop_spec_offset = 0;
    render("shop-detail-fixture");
    Check(OnViewAction(state, "shop_back", 0, 61), "shop detail Back");
    Check(OnViewAction(state, "shop_item", 0, 61),
          "long item detail accessible");
    render("shop-long-detail-fixture");
    Check(OnViewAction(state, "shop_back", 0, 61), "long detail Back");
    // Real CatalogSpecs display labels must retain their source-proved glyphs.
    auto original_specs = state.snapshot.shop_items[1].specs;
    state.snapshot.shop_items[1].specs = {
        {"Fuel", "100 l"},
        {"Maximum speed", "40 kph"},
        {"shop_maxPower", "65 kW / 88 hp"},
        {"shop_maintenance", "210.00 € / day"},
        {"shop_capacity", "6,400 l"},
        {"shop_neededPower", "139 kW / 190 hp"},
        {"shop_workingWidth", "6.0 m"}};
    Check(OnViewAction(state, "shop_item", 1, 61),
          "live label icon fixture selection");
    render("shop-native-attributes-fixture");
    for (int row = 0; row < 6; ++row) {
      bool glyph = false;
      for (int y = 248 + row * 104; y < 288 + row * 104; ++y)
        for (int x = 496; x < 536; ++x) {
          auto p = (size_t(y) * image.w + x) * 4;
          if (image.rgba[p + 1] > 120 &&
              image.rgba[p + 1] > image.rgba[p] + 25 &&
              image.rgba[p + 1] > image.rgba[p + 2] + 25)
            glyph = true;
        }
      Check(glyph, "native attribute glyph visible for emitted catalog label");
    }
    state.shop_spec_offset = 104;
    render("shop-native-attributes-tail-fixture");
    bool width_icon = false;
    for (int y = 768; y < 808; ++y)
      for (int x = 496; x < 536; ++x) {
        auto p = (size_t(y) * image.w + x) * 4;
        if (image.rgba[p + 1] > 120 && image.rgba[p + 1] > image.rgba[p] + 25 &&
            image.rgba[p + 1] > image.rgba[p + 2] + 25)
          width_icon = true;
      }
    Check(width_icon, "native working width glyph visible");
    state.snapshot.shop_items[1].specs = original_specs;
    Check(OnViewAction(state, "shop_back", 0, 61),
          "native attribute fixture Back");
    // The catalog grid is the runtime's native "shop-catalog" ScrollRegion
    // now (gen_manifest.py): "shop_item"'s argument is the tapped repeat
    // template's absolute filtered-item index directly (payload "{i}"), not
    // a 0..5 visible-window slot offset by a module-tracked grid_offset --
    // there is no grid_offset left to set here at all.
    Check(OnViewAction(state, "shop_item", 6, 61) &&
              state.shop_selected == "item6",
          "shop grid hit selects by absolute filtered index");
    Check(OnViewAction(state, "shop_back", 0, 61), "absolute-index detail Back");
    Check(!OnViewAction(state, "shop_item", 301, 61),
          "shop grid refuses an out-of-range absolute index");
    state.shop_selected.clear();
    Check(SelectedShop(state)->id == "item0",
          "shop grid falls back to the first filtered item with no selection "
          "(the runtime's own ScrollRegion reset_bind snaps the list to "
          "match, not a module-tracked offset)");
    // Each catalog card is now its own module image (View::RenderShopRow /
    // ImageFor's "shop_row/<master index>/<active>" key), served directly to
    // the runtime's native ScrollRegion -- never baked into this frame. Spot
    // check a handful of rows, including the selected highlight and the
    // final item, since the frame fixtures above no longer show them.
    Image row;
    Check(view.RenderShopRow(state, 0, false, row) && row.w == 412 &&
              row.h == 372,
          "shop row 0 renders at card size");
    if (argc > 2)
      Save(row, std::filesystem::path{argv[2]} / "shop-row-0-fixture.ppm");
    Check(view.RenderShopRow(state, 6, true, row), "shop row 6 (active) renders");
    if (argc > 2)
      Save(row, std::filesystem::path{argv[2]} / "shop-row-6-active-fixture.ppm");
    Check(view.RenderShopRow(state, 300, false, row), "shop row 300 (last) renders");
    if (argc > 2)
      Save(row, std::filesystem::path{argv[2]} / "shop-row-300-fixture.ppm");
    Check(!view.RenderShopRow(state, 301, false, row),
          "shop row refuses an out-of-range master index");
    // The filter list itself is no longer baked here either: it is the
    // runtime's native "shop-filters" ScrollRegion now (gen_manifest.py),
    // served as separate module images (View::RenderShopFilterRow).
    // "shop_filter_scroll" (the old wheel/drag step) is gone: the list
    // never receives that module action any more.
    Check(!OnViewAction(state, "shop_filter_scroll", 1, 61),
          "shop filter scroll is no longer a module action");
    // `argument` for "shop_filter_pick" is the tapped repeat template's
    // absolute index now (payload "{i}"), not a 0..6 visible-window slot
    // offset by a module-tracked shop_category_offset/shop_brand_offset.
    Check(OnViewAction(state, "shop_filter_pick", 2, 61) &&
              state.shop_category == "category2",
          "shop_filter_pick picks by absolute index now");
    Image filter_row;
    Check(view.RenderShopFilterRow(state, 0, false, filter_row) &&
              filter_row.w == ShopFilterRowWidth &&
              filter_row.h == ShopFilterRowPitch,
          "shop filter row 0 (category, inactive) renders at the fixed row size");
    if (argc > 2)
      Save(filter_row,
           std::filesystem::path{argv[2]} / "shop-filter-row-0-fixture.ppm");
    Check(view.RenderShopFilterRow(state, 2, true, filter_row),
          "shop filter row 2 (category, active) renders");
    if (argc > 2)
      Save(filter_row,
           std::filesystem::path{argv[2]} / "shop-filter-row-2-active-fixture.ppm");
    Check(view.RenderShopFilterRow(state, 40, false, filter_row),
          "shop filter row 40 (category, last) renders");
    Check(!view.RenderShopFilterRow(state, 41, false, filter_row),
          "shop filter row refuses an out-of-range category index");
    Check(OnViewAction(state, "shop_clear", 0, 61), "shop clear after a pick");
    Check(OnViewAction(state, "shop_filter_pick", 0, 61),
          "category filter picks actual id");
    Check(!ShopItems(state).empty(), "category filter native ID join");
    Check(OnViewAction(state, "shop_mode", 1, 61), "native brand filter mode");
    Check(view.RenderShopFilterRow(state, 79, false, filter_row) &&
              filter_row.w == ShopFilterRowWidth,
          "shop filter row 79 (brand, last) renders");
    Check(!view.RenderShopFilterRow(state, 80, false, filter_row),
          "shop filter row refuses an out-of-range brand index");
    render("shop-brands-fixture");
    Check(OnViewAction(state, "shop_clear", 0, 61) &&
              ShopItems(state).size() == 301,
          "clear native filters");
    state.snapshot.shop_ready = false;
    Check(!OnViewAction(state, "shop_item", 0, 61),
          "unknown shop refuses selection");
    render("shop-unavailable");
    state.page = Page::Vehicles;
    state.snapshot.vehicles_ready = true;
    for (int i = 0; i < 9; ++i) {
      VehicleItem item;
      item.id = "vehicle" + std::to_string(i);
      item.name =
          i % 2 ? "1660 Axial-Flow"
                : "Very long owned vehicle model name for native layout fit";
      item.image = "data/store/store_caseIH1660.dds";
      item.leased = i % 2;
      item.age_days = 48;
      item.operating_hours = 26.1;
      item.operating_time_text = "26:6 h";
      for (int j = 0; j < 6; ++j)
        item.specs.push_back({j % 2 ? "shop_maxPower" : "shop_capacity",
                              j % 2 ? "139 kW / 190 hp" : "6,400 l"});
      state.snapshot.owned_vehicles.push_back(item);
    }
    // The Garage list and the selected vehicle's specs are no longer baked
    // into fs.ui.frame: they are the runtime's native "garage-list"/
    // "garage-specs" ScrollRegions now (gen_manifest.py), served as
    // separate module images (View::RenderGarageRow/RenderGarageSpecRow).
    // render("vehicles-fixture") below only exercises the static divider/
    // detail-header chrome that is left; the lists are spot-checked
    // directly below, same convention as RenderShopRow/RenderFinanceRow.
    render("vehicles-fixture");
    // "vehicle_list"/"vehicle_specs" (the old wheel/drag steps) are gone:
    // neither pane receives either module action any more (the runtime's
    // own "garage-list"/"garage-specs" ScrollRegions own both now).
    Check(!OnViewAction(state, "vehicle_list", 1, 61),
          "garage list scroll is no longer a module action");
    Check(!OnViewAction(state, "vehicle_specs", 1, 61),
          "garage specs scroll is no longer a module action");
    // `argument` for "vehicle_pick" is the tapped repeat template's
    // absolute index now (payload "{i}"), not a 0..4 visible-window slot
    // offset by a module-tracked vehicles_offset.
    Check(OnViewAction(state, "vehicle_pick", 4, 61) &&
              state.vehicle_selected == "vehicle4",
          "vehicle_pick picks by absolute index now");
    Check(OnViewAction(state, "vehicle_pick", 1, 61) &&
              state.vehicle_selected == "vehicle1",
          "selected vehicle independent from controlled");
    auto vehicle_key = SnapshotKey(state);
    state.snapshot.vehicle_name = "OTHER CONTROLLED MACHINE";
    Check(vehicle_key == SnapshotKey(state),
          "controlled change preserves vehicle browser");
    Image garage_row;
    Check(view.RenderGarageRow(state, 0, false, garage_row) &&
              garage_row.w == GarageRowWidth && garage_row.h == GarageRowHeight,
          "garage row 0 (inactive) renders at the fixed row size");
    if (argc > 2)
      Save(garage_row, std::filesystem::path{argv[2]} / "garage-row-0-fixture.ppm");
    Check(view.RenderGarageRow(state, 1, true, garage_row),
          "garage row 1 (active) renders");
    if (argc > 2)
      Save(garage_row, std::filesystem::path{argv[2]} / "garage-row-1-active-fixture.ppm");
    Check(view.RenderGarageRow(state, 8, false, garage_row),
          "garage row 8 (last) renders");
    Check(!view.RenderGarageRow(state, 9, false, garage_row),
          "garage row refuses an out-of-range index");
    Check(OnViewAction(state, "vehicle_pick", 8, 61) &&
              state.vehicle_selected == "vehicle8",
          "last owned vehicle selectable");
    Image garage_spec_row;
    Check(view.RenderGarageSpecRow(state, 0, garage_spec_row) &&
              garage_spec_row.w == GarageSpecRowWidth &&
              garage_spec_row.h == GarageSpecRowHeight,
          "garage spec row 0 (selected vehicle8) renders at the fixed row size");
    if (argc > 2)
      Save(garage_spec_row,
           std::filesystem::path{argv[2]} / "garage-spec-row-0-fixture.ppm");
    Check(view.RenderGarageSpecRow(state, 5, garage_spec_row),
          "garage spec row 5 (last) renders");
    Check(!view.RenderGarageSpecRow(state, 6, garage_spec_row),
          "garage spec row refuses an out-of-range index");
    // Garage action row (UX-SPEC B, reviews/20261017-filters-garage-actions-11/
    // B1-garage-both-actions.png / B2-garage-reset-only.png / B3-garage-no-actions.png).
    Check(OnViewAction(state, "vehicle_pick", 1, 61) &&
              state.vehicle_selected == "vehicle1",
          "select vehicle1 for the action-row fixtures");
    Image action_row;
    state.snapshot.owned_vehicles[1].can_enter = true;
    state.snapshot.owned_vehicles[1].can_reset = true;
    Check(view.RenderGarageActionRow(1, action_row) && action_row.w == 736 &&
              action_row.h == 128,
          "garage action row (both) renders at the fixed row size");
    if (argc > 2)
      Save(action_row,
           std::filesystem::path{argv[2]} / "B1-garage-action-row-both-fixture.ppm");
    render("B1-garage-both-actions-fixture");
    state.snapshot.owned_vehicles[1].can_enter = false;
    state.snapshot.owned_vehicles[1].can_reset = true;
    Check(view.RenderGarageActionRow(3, action_row), "garage action row (reset only) renders");
    if (argc > 2)
      Save(action_row,
           std::filesystem::path{argv[2]} / "B2-garage-action-row-reset-only-fixture.ppm");
    render("B2-garage-reset-only-fixture");
    state.snapshot.owned_vehicles[1].can_enter = false;
    state.snapshot.owned_vehicles[1].can_reset = false;
    render("B3-garage-no-actions-fixture");
    Check(!view.RenderGarageActionRow(0, action_row),
          "garage action row refuses kind 0 (nothing to render -- the row has no picture)");
    // Call feedback toast (UX-SPEC B.4): fs.call.* -> a line baked above the action row.
    state.snapshot.game_call.kind = "reset";
    state.snapshot.game_call.busy = false;
    state.snapshot.game_call.status = 0;
    state.snapshot.game_call.code = 1;
    state.snapshot.game_call.message = "Vehicle reset";
    state.snapshot.game_call.serial = 1;
    render("garage-reset-toast-fixture");
    state.snapshot.game_call = {};
    auto spec_key = SnapshotKey(state);
    state.snapshot.owned_vehicles[8].specs[0].value = "999 kW / 999 hp";
    Check(spec_key == SnapshotKey(state),
          "a spec value change no longer invalidates fs.ui.frame -- the "
          "list lives in RenderGarageSpecRow's own image now, not this "
          "frame");
    render("vehicles-last-fixture");
    state.snapshot.vehicles_ready = false;
    Check(!OnViewAction(state, "vehicle_pick", 0, 61),
          "unknown vehicle selection refuses");
    render("vehicles-unavailable");
    // Map selection card (UX-SPEC C, reviews/20261017-filters-garage-actions-11/
    // C1-map-vehicle-selection.png / C2-map-place-selection.png): tapping a map marker.
    state.page = Page::Operations;
    state.snapshot.vehicles_ready = true;
    state.snapshot.world_ready = true;
    state.snapshot.map_id = 1;
    state.snapshot.player_x = 0.0;
    state.snapshot.player_z = 0.0;
    {
      MapMarker vehicle_marker;
      vehicle_marker.object_id = 4242;
      vehicle_marker.label = "8340";
      vehicle_marker.kind = "VEHICLE_STEERABLE";
      vehicle_marker.can_visit = false;
      vehicle_marker.enabled = true;
      vehicle_marker.x = 50.0;
      vehicle_marker.z = 50.0;
      MapMarker place_marker;
      place_marker.object_id = 7001;
      // Reuses the Prices section's own "Goldcrest Pacific Grain" station (set up above) so
      // the place card's facility-info lookup (G3 critique Finding 5, View::Operations'
      // selection_content) has real matching quotes to exercise, instead of needing a
      // separate fixture station.
      place_marker.label = "Goldcrest Pacific Grain";
      place_marker.kind = "SELL_POINT";
      place_marker.can_visit = true;
      place_marker.enabled = true;
      place_marker.x = 200.0;
      place_marker.z = 200.0;
      // Place icon (G3 critique Finding 5): the same hotspot atlas/swatch convention the
      // Operations() no-world legend array uses for "Selling points" (520,520).
      place_marker.image = "dataS2/menu/hud/mapHotspots_1024.dds";
      place_marker.sx = 520;
      place_marker.sy = 520;
      place_marker.w = 240;
      place_marker.h = 240;
      state.snapshot.map_markers = {vehicle_marker, place_marker};
      state.snapshot.owned_vehicles[1].root_node = vehicle_marker.object_id;
      state.snapshot.owned_vehicles[1].can_enter = true;
      state.snapshot.owned_vehicles[1].can_reset = true;
    }
    state.map_filters[0] = false; // Vehicles off: the steerable is not drawn
    Check(!OnViewAction(state, "map_marker_pick", 0, 61) && state.map_marker_selected != 0,
          "a marker hidden by its filter cannot be tapped");
    state.map_filters[0] = true;
    Check(OnViewAction(state, "map_marker_pick", 0, 61) &&
              state.map_marker_selected == 0,
          "tap a vehicle marker selects it");
    Check(view.RenderMapSelectionButtons(1, false, false, action_row) &&
              action_row.w == 304 && action_row.h == 236,
          "map selection buttons (vehicle, both) render at the fixed size");
    if (argc > 2)
      Save(action_row,
           std::filesystem::path{argv[2]} / "C1-map-selection-vehicle-fixture.ppm");
    render("C1-map-vehicle-selection-fixture");
    Check(OnViewAction(state, "map_marker_pick", 1, 61) &&
              state.map_marker_selected == 1,
          "tap a place marker replaces the selection");
    Check(view.RenderMapSelectionButtons(1, true, false, action_row),
          "map selection buttons (place, Visit/Tag place) render");
    if (argc > 2)
      Save(action_row,
           std::filesystem::path{argv[2]} / "C2-map-selection-place-fixture.ppm");
    render("C2-map-place-selection-fixture");
    Check(view.RenderMapSelectionButtons(1, true, true, action_row),
          "map selection buttons (place, Untag place label) render");
    if (argc > 2)
      Save(action_row,
           std::filesystem::path{argv[2]} / "C2-map-selection-place-tagged-fixture.ppm");
    Check(OnViewAction(state, "page", int64_t(int(Page::Shop)), 61) &&
              state.map_marker_selected == -1,
          "leaving the Map page clears the marker selection");
    state.page = Page::Operations;
    state.snapshot.world_ready = false;
    render("world-lost");
    std::cout << "Presentation checks passed. Synthetic fixture output is not "
                 "live evidence.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
