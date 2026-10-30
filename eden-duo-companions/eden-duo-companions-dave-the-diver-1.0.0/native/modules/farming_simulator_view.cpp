// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_view.h"
#include <algorithm>
#include <numeric>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>
namespace farming_simulator {
namespace {
constexpr uint32_t Black = 0x000000ff, Background = 0x2f2f2fff,
                   Row = 0x262626ff, Selected = 0x7fc031ff, White = 0xffffffff,
                   Muted = 0xc3c3c3ff, Rule = 0x707070ff;
// SnapshotKey() used to build one big formatted std::string every Sample()
// tick purely to detect whether anything visible changed since the last one
// -- expensive for pages with long lists (Shop/Finances/Statistics/Prices),
// and wasted even when nothing changed. This accumulates the exact same
// sequence of values (same fields, same Value()/FloorValue()/Number()
// fallback-formatted strings, so the same quantization/equivalence classes)
// into a 64-bit running hash instead, which is what the caller actually
// compares. A `char`/`bool`/integer feeds its bit pattern straight in;
// `double` only appears here for the handful of fields SnapshotKey streams
// directly with the iostream default formatting (~6 significant digits,
// `operator<<(double)`'s usual notation) rather than through one of the
// fixed-decimal helpers above, so it is formatted exactly as before and that
// short text is what gets hashed, to keep the same "close enough" equality.
class KeyHasher {
public:
  template <class T, class = std::enable_if_t<std::is_arithmetic_v<T>>>
  KeyHasher &operator<<(T v) {
    if constexpr (std::is_floating_point_v<T>) {
      std::ostringstream o;
      o << v;
      return *this << o.str();
    } else {
      int64_t n = static_cast<int64_t>(v);
      Bytes(&n, sizeof n);
      return *this;
    }
  }
  KeyHasher &operator<<(std::string_view s) {
    Bytes(s.data(), s.size());
    uint8_t sep = 0xff;
    Bytes(&sep, 1);
    return *this;
  }
  KeyHasher &operator<<(const std::string &s) { return *this << std::string_view{s}; }
  KeyHasher &operator<<(const char *s) { return *this << std::string_view{s}; }
  uint64_t Key() const { return hash_; }

private:
  void Bytes(const void *data, size_t n) {
    auto *p = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < n; ++i) {
      hash_ ^= p[i];
      hash_ *= 1099511628211ULL; // FNV-1a
    }
  }
  uint64_t hash_ = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
};
std::string Number(double value, int decimals = 0) {
  return FormatGrouped(value, decimals);
}
std::string Value(const std::optional<double> &v, int decimals = 0) {
  return v && std::isfinite(*v) ? Number(*v, decimals) : "-";
}
std::string FloorValue(const std::optional<double> &v) {
  return v && std::isfinite(*v) ? Number(std::floor(*v)) : "-";
}
bool CiContains(std::string_view hay, std::string_view needle) {
  if (needle.size() > hay.size())
    return false;
  for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool match = true;
    for (size_t j = 0; match && j < needle.size(); ++j)
      match = std::tolower(static_cast<unsigned char>(hay[i + j])) ==
               std::tolower(static_cast<unsigned char>(needle[j]));
    if (match)
      return true;
  }
  return false;
}
std::string Name(Page p) {
  switch (p) {
  case Page::Operations:
    return "MAP OVERVIEW";
  case Page::Shop:
    return "SHOP";
  case Page::Prices:
    return "PRICES / 1000 L";
  case Page::Vehicles:
    return "VEHICLE OVERVIEW";
  case Page::Finances:
    return "FINANCES";
  case Page::Animals:
    return "ANIMALS";
  case Page::Statistics:
    return "STATISTICS";
  case Page::Help:
    return "HELP";
  }
  return {};
}
} // namespace
std::string FormatGrouped(double value, int decimals) {
  std::ostringstream o;
  o << std::fixed << std::setprecision(decimals) << value;
  auto s = o.str();
  size_t end = s.find('.');
  if (end == s.npos)
    end = s.size();
  size_t begin = s.starts_with('-') ? 1 : 0;
  for (size_t p = end; p > begin + 3;) {
    p -= 3;
    s.insert(p, ",");
  }
  return s;
}
void View::Text(Image &out, std::string_view v, int x, int y, int size,
                uint32_t c, bool b) {
  font_.Draw(out, v, x, y, size, c, b);
}
void View::Unavailable(Image &out, bool loading, int x, int y, int size,
                       std::string_view unavailable_text) {
  Text(out, loading ? std::string_view{"Loading..."} : unavailable_text, x, y,
       size, Muted);
}
void View::RowText(Image &out, std::string_view value, int x, int y, int h,
                   int size, uint32_t colour, bool bold) {
  auto [top, bottom] = font_.VerticalBounds(value, size, bold);
  Text(out, value, x, y + (h - bottom + top) / 2 - top, size, colour, bold);
}
void View::RowLines(Image &out, const std::vector<std::string> &lines, int x,
                    int y, int h, int size, uint32_t colour, bool bold, int align) {
  size_t count = std::min(lines.size(), size_t(2));
  if (!count) return;
  const int step = size + 4;
  int top = size, bottom = 0;
  for (size_t i = 0; i < count; ++i) {
    auto bounds = font_.VerticalBounds(lines[i], size, bold);
    top = std::min(top, int(i) * step + bounds.first);
    bottom = std::max(bottom, int(i) * step + bounds.second);
  }
  int first = y + (h - bottom + top) / 2 - top;
  for (size_t i = 0; i < count; ++i) {
    int left = align == 1 ? x - font_.Measure(lines[i], size, bold) / 2
              : align == 2 ? x - font_.Measure(lines[i], size, bold) : x;
    Text(out, lines[i], left, first + int(i) * step, size, colour, bold);
  }
}
std::string View::Label(std::string_view key) {
  if (key.starts_with("$l10n_"))
    key.remove_prefix(6);
  auto native = assets_.Localize(key);
  if (!native.empty())
    return native;
  // An unresolved localization identifier is not a usable player-facing label.
  if (key.starts_with("ui_") || key.starts_with("finance_") ||
      key.starts_with("statistic_") || key.starts_with("unit_") ||
      key.starts_with("shop_") || key.starts_with("category_") ||
      key.starts_with("fillType_"))
    return "-";
  return std::string{key};
}
void View::Art(Image &out, std::string_view path, int x, int y, int w, int h,
               bool fit_alpha) {
  auto image = assets_.Texture(std::string{path});
  if (!image)
    return;
  int sx = 0, sy = 0, sw = image->w, sh = image->h;
  if (fit_alpha) {
    const auto &box = assets_.AlphaBBox(*image);
    int left = box[0], top = box[1], right = box[2], bottom = box[3];
    if (right >= left && bottom >= top) {
      sx = std::max(0, left - 2);
      sy = std::max(0, top - 2);
      sw = std::min(int(image->w), right + 3) - sx;
      sh = std::min(int(image->h), bottom + 3) - sy;
    }
  }
  double scale = std::min(double(w) / sw, double(h) / sh);
  int aw = std::lround(sw * scale), ah = std::lround(sh * scale);
  Blit(out, *image, x + (w - aw) / 2, y + (h - ah) / 2, aw, ah, sx, sy, sw, sh);
}
void View::Icon(Image &out, std::string_view name, int x, int y, int size,
                uint32_t colour) {
  if (name == "map") {
    // Authored folded-map mark, using the native UI's monochrome line material.
    // The installed GUI has no proven dedicated Map destination glyph.
    Image glyph;
    glyph.w = glyph.h = 44;
    glyph.rgba.assign(44 * 44 * 4, 0);
    auto line = [&](int ax, int ay, int bx, int by) {
      int steps = std::max(std::abs(bx - ax), std::abs(by - ay));
      for (int i = 0; i <= steps; ++i) {
        int px = ax + std::lround(double(bx - ax) * i / steps);
        int py = ay + std::lround(double(by - ay) * i / steps);
        Fill(glyph, px - 1, py - 1, 3, 3, colour);
      }
    };
    line(2, 10, 15, 4); line(15, 4, 28, 10); line(28, 10, 41, 4);
    line(41, 4, 41, 34); line(41, 34, 28, 40); line(28, 40, 15, 34);
    line(15, 34, 2, 40); line(2, 40, 2, 10);
    line(15, 4, 15, 34); line(28, 10, 28, 40);
    Blit(out, glyph, x, y, size, size);
    return;
  }
  if (name == "left" || name == "right") {
    auto atlas=assets_.Texture("dataS2/menu/hud/ui_elements_1080p.dds");
    if(!atlas)return;
    Image glyph;glyph.w=glyph.h=16;glyph.rgba.resize(16*16*4);
    Blit(glyph,*atlas,0,0,16,16,418,790,16,16,colour);
    if(name=="left") {
      auto original=glyph;
      for(int yy=0;yy<16;++yy)for(int xx=0;xx<16;++xx)
        std::copy_n(&original.rgba[((15-yy)*16+15-xx)*4],4,&glyph.rgba[(yy*16+xx)*4]);
    }
    Blit(out,glyph,x,y,size,size);
    return;
  }
  static const std::map<std::string, std::array<int, 4>> boxes = {
      {"shop", {51, 990, 33, 25}},   {"garage", {360, 990, 26, 25}},
      {"speed", {493, 945, 38, 38}}, {"recentre", {46, 371, 33, 32}},
      {"help", {360, 945, 36, 36}},
      // FILTERS popup row icons (UX-SPEC §A.1): native guiProfiles.xml
      // ingameMenuMapFilterButtonIcon{Vehicle,Combine,Trailer,Tool,Tipping,
      // FieldJobs,Other} px UVs against the same shared UI atlas the boxes
      // above already read from.
      {"filter_vehicle", {984, 461, 32, 32}},
      {"filter_combine", {944, 461, 32, 32}},
      {"filter_trailer", {904, 461, 32, 32}},
      {"filter_tool", {864, 461, 32, 32}},
      {"filter_tipping", {824, 461, 32, 32}},
      {"filter_fieldjobs", {259, 367, 32, 32}},
      {"filter_other", {784, 461, 32, 32}},
      // G3 critique, Finding 5 (native button grammar): guiProfiles.xml's own
      // ingameMenuMapButton{Enter,Reset,Visit,Tag} profiles (same shared
      // ui_elements_1080p.dds atlas, same pixel-UV convention as the filter
      // icons above), confirmed by reading the installed build's own GUI
      // definitions, not inferred from a screenshot crop:
      //   ingameMenuMapButtonEnter  iconUVs="954px 501px 27px 27px"
      //   ingameMenuMapButtonReset  iconUVs="605px 989px 27px 27px"
      //   ingameMenuMapButtonVisit  iconUVs="575px 424px 27px 27px"
      //   ingameMenuMapButtonTag    iconUVs="362px 711px 27px 27px"
      // pcButton (their shared parent profile) is imageColor=colorBlack2
      // (charcoal fill) with iconColor=colorGreen (lime icon) and white bold
      // text -- i.e. native Enter/Reset/Visit/Tag are NOT differentiated by
      // fill colour at all, only by icon+label. See NativeActionButton.
      {"icon_enter", {954, 501, 27, 27}},
      {"icon_reset", {605, 989, 27, 27}},
      {"icon_visit", {575, 424, 27, 27}},
      {"icon_tag", {362, 711, 27, 27}}};
  if (name == "fuel") {
    Art(out, "dataS2/menu/hud/fillTypes/hud_fill_fuel.dds", x, y, size, size);
    return;
  }
  auto box = boxes.find(std::string{name});
  if (box == boxes.end())
    return;
  auto atlas = assets_.Texture("dataS2/menu/hud/ui_elements_1080p.dds");
  if (!atlas)
    return;
  const auto &b = box->second;
  double scale = double(size) / std::max(b[2], b[3]);
  int w = std::lround(b[2] * scale), h = std::lround(b[3] * scale);
  Blit(out, *atlas, x + (size - w) / 2, y + (size - h) / 2, w, h, b[0], b[1],
       b[2], b[3], colour);
}
void View::Button(Image &out, std::string_view label, int x, int y, int w,
                  int h, bool enabled, std::string_view icon) {
  Fill(out, x, y, w, h, Black);
  int tw = font_.Measure(label, 32, true), iw = icon.empty() ? 0 : 44;
  int gap = icon.empty() || label.empty() ? 0 : 16;
  int left = x + (w - tw - iw - gap) / 2;
  if (!icon.empty())
    Icon(out, icon, left, y + (h - 44) / 2, 44, enabled ? White : Muted);
  RowText(out, label, left + iw + gap, y, h, 32, enabled ? White : Muted, true);
}
void View::TimeControl(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  // Right-aligned on the right pane's/footer's own right edge (x=1220), so the
  // badge lines up with the Map page's pane content and every other page's
  // scrollbars/right margins.
  constexpr int ButtonW = 110, ButtonH = 64, ButtonY = 8, RightEdge = 1220;
  const int button_x = RightEdge - ButtonW;
  if (s.world_ready && s.time_minutes) {
    int t = ((*s.time_minutes % 1440) + 1440) % 1440;
    std::ostringstream o;
    o << std::setw(2) << std::setfill('0') << t / 60 << ":" << std::setw(2)
      << t % 60;
    auto clock = o.str();
    Text(out, clock, button_x - 24 - font_.Measure(clock, 38, true), 23, 38,
         White, true);
  }
  // Weather is absent until a verified source exists. A sun glyph is not a
  // clock symbol.
  if (s.world_ready && s.time_scale) {
    auto factor = s.time_scale_pending ? "..." : Number(*s.time_scale) + "x";
    Fill(out, button_x, ButtonY, ButtonW, ButtonH,
         state.time_scale_open ? Selected : Row);
    RowText(out, factor, button_x + (ButtonW - font_.Measure(factor, 32, true)) / 2,
            ButtonY, ButtonH, 32,
            s.time_scale_change_allowed || s.time_scale_pending ? White : Muted,
            true);
  }
}
void View::Footer(Image &out, Page page, const Snapshot &snapshot, bool visible) {
  if (!visible)
    return;
  static constexpr std::array<Page, 8> pages{Page::Operations, Page::Shop,
      Page::Vehicles, Page::Prices, Page::Animals, Page::Finances,
      Page::Statistics, Page::Help};
  static constexpr std::array<std::string_view, 8> names{
      "Map", "Shop", "Garage", "Prices", "Animals", "Finance", "Stats", "Guide"};
  for (size_t i = 0; i < pages.size(); ++i) {
    int x = int(i) * 155;
    Fill(out, x, 968, 155, 112, page == pages[i] ? Selected : Black);
    if (i) Fill(out, x, 984, 1, 80, Rule);
    int ix = x + (155 - 44) / 2, iy = 980;
    if (i == 0) Icon(out, "map", ix, iy, 44);
    else if (i == 1) Icon(out, "shop", ix, iy, 44);
    else if (i == 2) Icon(out, "garage", ix, iy, 44);
    else if (i == 3) Art(out, "dataS2/menu/hud/fillTypes/hud_fill_wheat.dds", ix, iy, 44, 44, true);
    else if (i == 4) Art(out, "dataS2/menu/hud/fillTypes/hud_fill_cow.dds", ix, iy, 44, 44, true);
    else if (i == 5) {
      auto currency = snapshot.currency_symbol.empty() ? "€" : snapshot.currency_symbol;
      RowText(out, currency, x + (155 - font_.Measure(currency, 44, true)) / 2,
              976, 52, 44, White, true);
    } else if (i == 7) Icon(out, "help", ix, iy, 44);
    if (i == 6) {
      // An authored numerical mark in the native font, not a borrowed benchmark icon.
      RowText(out, "123", x + (155 - font_.Measure("123", 40, true)) / 2,
              976, 52, 40, White, true);
    }
    RowText(out, names[i], x + (155 - font_.Measure(names[i], 32, true)) / 2,
            1030, 40, 32, White, true);
  }
}
void View::Operations(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  Fill(out, 0, 0, 1240, 80, Black);
  Text(out,
       s.world_ready && s.money ? s.currency_symbol + " " + FloorValue(s.money)
                                : "-",
       28, 23, 38, White, true);
  TimeControl(out, state);
  bool map = s.world_ready && (s.map_id == 1 || s.map_id == 2);
  if (map) {
    // FILTERS button (UX-SPEC 2026-10-17 review §A.2): left of Expand.
    // Weight-class rule (BOTW owner rule): Filters opens a popup and never
    // leaves the Map tab, so it must read lighter than Expand, which
    // changes the whole screen composition -- outline only when closed,
    // filled lime (the existing selected-tab/active-toggle treatment) when
    // open. Placed immediately left of Expand's own existing box rather
    // than at the spec's literal 648px (which assumed a slightly different
    // Expand geometry than this build's existing 768,8,180,64) -- same
    // order/adjacency/weight-class intent, adapted to this build's actual
    // top-bar rects.
    constexpr int FilterX = 628, FilterW = 124, FilterH = 64;
    // G3 critique Finding 9/"F" (weight-class conflict): Expand's own button turns solid
    // lime-filled the instant it reads "Restore" (map_fullscreen), independent of Filters --
    // so a solid-lime Filters-open fill at the same time reads exactly as heavy as Restore,
    // undoing the BOTW weight-class rule this same button's accent bar exists to enforce. The
    // compact case (map_fullscreen false, Expand stays its normal dark Row fill) is unaffected
    // and keeps the spec's literal "filled lime when open" treatment, matching every other
    // open/active toggle in this companion; only while expanded does Filters-open fall back to
    // a lime border + lime text on its normal dark fill -- still a clear "on" state, just one
    // that cannot out-weigh Restore's solid fill.
    const bool filled_open = state.map_filters_open && !state.map_fullscreen;
    const bool bordered_open = state.map_filters_open && state.map_fullscreen;
    if (filled_open) {
      Fill(out, FilterX, 8, FilterW, FilterH, Selected);
    } else {
      Fill(out, FilterX, 8, FilterW, FilterH, Black);
      const uint32_t border = bordered_open ? Selected : Rule;
      Fill(out, FilterX, 8, FilterW, 2, border);
      Fill(out, FilterX, 8 + FilterH - 2, FilterW, 2, border);
      Fill(out, FilterX, 8, 2, FilterH, border);
      Fill(out, FilterX + FilterW - 2, 8, 2, FilterH, border);
    }
    const std::string filter_label = "Filters";
    RowText(out, filter_label,
            FilterX + (FilterW - font_.Measure(filter_label, 32, true)) / 2, 8,
            FilterH, 32, filled_open ? Black : bordered_open ? Selected : White, true);
    Fill(out, 768, 8, 180, 64, state.map_fullscreen ? Selected : Row);
    // Expand weight-class accent (UX-SPEC §A.2): a 4px lime bottom edge,
    // always on (not just hover/focus), so Expand still outweighs the new
    // same-size Filters button beside it. Flagged in the review for the
    // Art Director/owner as the minimal fix rather than a full top-bar
    // restyle; trivially revertible (this one Fill call).
    Fill(out, 768, 8 + 64 - 4, 180, 4, Selected);
    const std::string label = state.map_fullscreen ? "Restore" : "Expand";
    RowText(out, label, 768 + (180-font_.Measure(label,32,true))/2, 8, 64,32,White,true);
  }
  const int pane_width = state.map_fullscreen ? 1240 : 888;
  if (map) {
    // The map's own pixels (base texture, current layer, markers, player
    // arrow) are no longer drawn into this frame at all: they live in their
    // own "fs.ui.map_frame" image (RenderMapBase), shown through the
    // runtime's native pan_zoom Image widgets ("map-compact"/"map-expanded",
    // see tools/farming-simulator/gen_manifest.py). A drag or pinch is then
    // a pure runtime transform over the last-rendered map image -- no module
    // re-render, no new revision, no decode -- instead of recomputing and
    // re-encoding the whole 1240x1080 page on every touch event, which was
    // the lag: map_pan/map_pinch used to mint a revision per gesture tick
    // and wait for the asset worker to decode it before the next one could
    // even mint, so manipulation could only ever track as fast as that
    // decode (a few fps under load), not the touch itself.
    // Punch a fully-transparent hole where the pan_zoom widget shows
    // through; the layer selector and (native, manifest-drawn) Reset button
    // still sit on top of it, same as before.
    const int viewport_h = state.map_fullscreen ? int(ExpandedMapH) : int(CompactMapPx);
    Fill(out, 0, 80, pane_width, viewport_h, 0);
    const char *layers[]{"Map", "Crops", "Growth", "Soil"};
    constexpr int selector_width = 440, arrow_width = 88, selector_height = 88;
    const int selector_left = (pane_width - selector_width) / 2;
    // Expanded hides the footer (see Footer()'s `visible` argument below), so
    // the selector moves down to sit flush with the bottom edge there
    // instead of staying at the compact pane's fixed y=864.
    const int selector_y = state.map_fullscreen ? 1080 - 24 - selector_height : 864;
    Fill(out, selector_left, selector_y, selector_width, selector_height, Black);
    Fill(out, selector_left + arrow_width, selector_y + 16, 1, 56, Rule);
    Fill(out, selector_left + selector_width - arrow_width, selector_y + 16, 1, 56, Rule);
    const auto label = layers[std::clamp(state.map_layer, 0, 3)];
    RowText(out, label, selector_left + (selector_width - font_.Measure(label, 32, true)) / 2,
            selector_y, selector_height, 32, White, true);
    Icon(out, "left", selector_left + 24, selector_y + 24, 40, s.map_layers ? White : Muted);
    Icon(out, "right", selector_left + selector_width - 64, selector_y + 24, 40,
         s.map_layers ? White : Muted);
  } else
    map = false;
  if (!map) {
    Fill(out, 0, 80, 888, 888, Row);
    Text(out, "MAP OVERVIEW", 28, 120, 38, White, true);
    // Between the title/menu and a loaded save (Phase::Loading) there is no
    // farm yet to be unavailable -- the save is still coming up. Title
    // (not in a save at all) and a world that is ready but genuinely not on
    // a farm-capable map both keep the plain unavailable text.
    Unavailable(out, s.phase == Phase::Loading, 28, 188, 32,
                "Farm data unavailable");
  }
  // Map selection card content (UX-SPEC C.2), factored out so both the compact right pane
  // below and the expanded floating card (MapSelectionCard's own call, same pattern
  // MapFiltersCard established) draw the identical name/subtitle/art from the same origin
  // convention -- (x, name_y) for the (up to 2-line) label, subtitle_y below it, and the
  // vehicle art rect separately since its width/position differ between the narrower pane and
  // the card. Returns false for a stale/now-invalid selection (enabled/kind changed under the
  // player), same guard the pane branch used to inline.
  // content_bottom: top of the action buttons under this card (minus a margin) -- price
  // lines stop there instead of running under the buttons (show less, never shrink).
  auto selection_content = [&](int x, int name_y, int subtitle_y, int art_x, int art_y,
                               int art_w, int art_h, int name_w, int content_bottom) {
    if (state.map_marker_selected < 0 ||
        size_t(state.map_marker_selected) >= s.map_markers.size())
      return false;
    const auto &m = s.map_markers[size_t(state.map_marker_selected)];
    const bool is_vehicle = !m.can_visit && m.kind.starts_with("VEHICLE_");
    if (!m.enabled || !(m.can_visit || is_vehicle))
      return false;
    // G3 critique Finding 1 ("selection card shows engine identifiers instead of game
    // names"): the map hotspot's own label was reading as the Lua class name ("Steerable",
    // "Tool") for vehicles and the trigger's internal class/variable name ("tipTrigger") for
    // places. Vehicles: the SAME vehicle is already in owned_vehicles (matched by
    // root_node==object_id, used just below for art) with its real Garage model name --
    // prefer that over the marker's own label. Places: the reader now resolves a localized
    // stationName (farming_simulator_map_reader.inc) into m.label itself, so no override is
    // needed here, but an unresolved place still falls back to "-" rather than a raw
    // identifier (never show a Lua class/variable name as a player-facing title).
    const VehicleItem *veh = nullptr;
    if (is_vehicle) {
      auto it = std::find_if(s.owned_vehicles.begin(), s.owned_vehicles.end(),
                             [&](const auto &v2) { return v2.root_node == m.object_id; });
      if (it != s.owned_vehicles.end())
        veh = &*it;
    }
    // A place label is the station's localization key (e.g. "station_barn"), the same key
    // the Prices page passes through Label(); show its localized text, never the raw key.
    std::string title = veh && !veh->name.empty() ? Label(veh->name)
                        : m.label.empty()           ? std::string{}
                                                    : Label(m.label);
    auto lines = font_.Wrap(title.empty() ? "-" : title, 32, name_w, true);
    for (size_t i = 0; i < std::min<size_t>(2, lines.size()); ++i)
      Text(out, lines[i], x, name_y + int(i) * 38, 32, White, true);
    std::string subtitle;
    if (is_vehicle) {
      // Humanised from the marker's own category string (e.g. "VEHICLE_COMBINE" ->
      // "Combine") -- the map hotspot carries no closer per-marker category label than
      // this, and it already matches the Filters popup's own category wording.
      subtitle = m.kind.size() > 8 ? m.kind.substr(8) : m.kind;
      for (auto &c : subtitle)
        c = c == '_' ? ' ' : c;
      for (size_t i = 1; i < subtitle.size(); ++i)
        if (std::isupper(static_cast<unsigned char>(subtitle[i])) &&
            subtitle[i - 1] != ' ')
          subtitle[i] = char(std::tolower(static_cast<unsigned char>(subtitle[i])));
    } else {
      // G3 critique Finding 1: never the generic "Place" -- the same 7-category label the
      // Filters popup already shows (MapMarkerFilterIndex), singularised (drop a trailing
      // "s"; every label but "Other" is already plural and "Other" needs no change).
      static constexpr std::array<std::string_view, 7> kFilterLabels{
          "Vehicles",  "Harvesters",     "Trailers", "Tools",
          "Selling points", "Field jobs", "Other"};
      subtitle = std::string(kFilterLabels[size_t(MapMarkerFilterIndex(m))]);
      if (subtitle.ends_with('s'))
        subtitle.pop_back();
    }
    Text(out, subtitle, x, subtitle_y, 24, Muted);
    if (veh && !veh->image.empty())
      Art(out, veh->image, art_x, art_y, art_w, art_h, true);
    // G3 critique Finding 5 ("the place card is mostly empty"): the place's own map icon
    // (the exact same hotspot sprite drawn on the map itself -- RenderMapBase's marker loop
    // reads the same m.image/sx/sy/w/h/color) plus, for a selling point, the top prices it
    // pays -- the same data the Prices page already shows, matched by the station name this
    // card's title now resolves to (the stationName fix in map_reader.inc). Reuses the same
    // art rect the vehicle branch above uses; a hotspot icon is a small square sprite, not a
    // wide vehicle render, so it's capped well under that rect's own height instead of
    // stretched to fill it.
    if (!is_vehicle) {
      int icon_size = std::min(art_h, 160);
      if (!m.image.empty())
        if (auto atlas = assets_.Texture(m.image)) {
          const int sw = m.w > 0 ? m.w : int(atlas->w), sh = m.h > 0 ? m.h : int(atlas->h);
          Blit(out, *atlas, art_x + (art_w - icon_size) / 2, art_y, icon_size, icon_size,
               m.sx, m.sy, sw, sh, m.color);
        }
      if (MapMarkerFilterIndex(m) == 4) { // Selling points
        auto station = std::find_if(s.price_stations.begin(), s.price_stations.end(),
                                    [&](const auto &st) {
                                      // Compare display names: the Prices reader stores the
                                      // localized name, the marker may carry the raw station
                                      // key; Label() maps both to the same localized text.
                                      return !title.empty() &&
                                             Label(StationKey(st.name)) == title;
                                    });
        if (station != s.price_stations.end()) {
          int info_y = art_y + icon_size + 24, shown = 0;
          const int max_lines = std::clamp((content_bottom - info_y) / 44, 0, 3);
          for (size_t ci = 0; ci < station->quotes.size() && ci < s.price_crops.size() &&
                             shown < max_lines;
               ++ci) {
            const auto &q = station->quotes[ci];
            if (!q.accepted || !q.price_per1000)
              continue;
            std::string line = Label(s.price_crops[ci].name) + ": " + Value(q.price_per1000);
            if (!s.currency_symbol.empty())
              line += " " + s.currency_symbol;
            Text(out, line, x, info_y + shown * 44, 32, White);
            ++shown;
          }
        }
      }
    }
    return true;
  };
  const bool selection_valid = state.map_marker_selected >= 0 &&
                               size_t(state.map_marker_selected) < s.map_markers.size() &&
                               [&] {
                                 const auto &m =
                                     s.map_markers[size_t(state.map_marker_selected)];
                                 return m.enabled &&
                                        (m.can_visit || m.kind.starts_with("VEHICLE_"));
                               }();
  if (state.map_fullscreen && !state.time_scale_open && !state.map_filters_open &&
      !selection_valid)
    return;
  if (state.map_filters_open && state.map_fullscreen) {
    // Expanded mode has no right pane to replace (map is full-bleed) --
    // UX-SPEC §A.4 -- so FILTERS becomes a floating overlay card instead,
    // drawn before the pane's own Background fill below so the live map
    // keeps showing through everywhere else.
    MapFiltersCard(out, state);
    return;
  }
  if (state.map_fullscreen && selection_valid) {
    // Map selection card, expanded (UX-SPEC C review): the same floating-card pattern as
    // Filters above -- expanded has no right pane to replace, so a tapped marker's card
    // floats over the live map instead. The Enter/Reset or Visit/Tag action row under it is
    // still the separate served image positioned by the manifest (gen_manifest.py's
    // map-selection-card widgets), not drawn here. CardH grown 500->646 (G3 critique fix,
    // 2026-10-17) to fit the taller 304x236 stacked-row action row below the header/art
    // (16+56 title + 100 subtitle gap + 230 art + 20 gap + 236 buttons + 20 bottom margin).
    constexpr int CardX = 732, CardY = 96, CardW = 492, CardH = 646;
    Fill(out, CardX, CardY, CardW, CardH, Rule);
    Fill(out, CardX + 2, CardY + 2, CardW - 4, CardH - 4, Row);
    selection_content(CardX + 20, CardY + 16, CardY + 100, CardX + (CardW - 304) / 2,
                      CardY + 140, 304, 230, CardW - 40, CardY + 390 - 8);
    return;
  }
  Fill(out, 888, 80, 352, 888, Background);
  // Owner request: the TIME SCALE picker replaces the right pane's content
  // while open, instead of overlaying the map in the centre.
  if (state.time_scale_open && s.world_ready && s.time_scale) {
    RowText(out, "TIME SCALE", 908, 96, 56, 32, White, true);
    constexpr int factors[]{1, 5, 15, 30, 60, 120};
    for (int i = 0; i < 6; ++i) {
      int x = 908 + (i % 2) * 164, y = 184 + (i / 2) * 100;
      bool selected = *s.time_scale == factors[i];
      Fill(out, x, y, 148, 88, selected ? Selected : Black);
      std::string label = std::to_string(factors[i]) + "x";
      RowText(out, label, x + (148 - font_.Measure(label, 38, true)) / 2,
              y, 88, 38, White, true);
    }
    return;
  }
  // FILTERS popup (compact): reuses the exact right-pane-replace mechanism
  // as TIME SCALE above (UX-SPEC §A.3).
  if (state.map_filters_open) {
    RowText(out, "FILTERS", 908, 96, 56, 32, White, true);
    MapFiltersRows(out, state, 908, 172, 312, 92);
    return;
  }
  // Map selection card (UX-SPEC C.2): a tapped marker replaces the right pane's content the
  // same way the time-scale picker above does -- mutually exclusive with it and with the
  // legend below (OnViewAction clears whichever of the others was open when one of these three
  // opens). Only the header (name/category/art) is baked here; the Enter/Reset or Visit/Tag
  // action row under it is a separate, smaller served image (RenderMapSelectionButtons,
  // gen_manifest.py's map-selection widgets) positioned by the manifest, not drawn here.
  if (selection_content(908, 98, 180, 916, 224, 304, 230, 312, 732 - 8))
    return;
  // A selected layer (map_layer != 0) that failed to load (s.map_layers
  // null) used to fall straight through to whatever else Operations() draws
  // below with no indication at all -- fs.ui.layer_error was published but
  // no manifest widget ever showed it. Show it here instead, same
  // Unavailable() styling every other "Data unavailable" panel uses, in
  // the same header spot the HUD/Legend toggle row below would occupy.
  if (state.map_layer && !s.map_layers) {
    RowText(out, state.map_layer == 1 ? "CROP TYPES" : state.map_layer == 2 ? "GROWTH" : "SOIL",
            908, 96, 56, 32, White, true);
    Unavailable(out, false, 908, 160, 28,
               s.map_layer_error.empty() ? std::string_view{"Data unavailable"}
                                         : std::string_view{s.map_layer_error});
    return;
  }
  if (state.map_layer && s.map_layers) {
    for (int i = 0; i < 2; ++i) {
      int x = 908 + 164*i; std::string label = i ? "Legend" : "HUD";
      Fill(out, x, 888, 148, 64, state.map_legend == bool(i) ? Selected : Black);
      RowText(out, label, x + (148-font_.Measure(label,32,true))/2,888,64,32,White,true);
    }
    if (state.map_legend) {
      // The legend list itself is no longer baked here: it is the runtime's
      // native "map-legend" ScrollRegion now (RenderMapLegendRow below).
      // Only the header label (never scrolls) stays in this frame image.
      RowText(out, state.map_layer == 1 ? "CROP TYPES" : state.map_layer == 2 ? "GROWTH" : "SOIL",908,96,56,32,White,true);
      return;
    }
  }
  if (!s.world_ready || !s.vehicle_ready) {
    Text(out, "MAP OVERVIEW", 908, 102, 32, White, true);
    if (map)
      Text(out, s.map_id == 1 ? "Goldcrest Valley" : "Sosnovka", 908, 156, 32,
           Muted);
    if (!s.world_ready) {
      Text(out, "-", 908, 244, 32, Muted);
      return;
    }
    const std::array<std::pair<std::string, std::array<int, 2>>, 5> legend = {
        {{"Shop", {520, 8}},
         {"Selling points", {520, 520}},
         {"Sheep pasture", {264, 264}},
         {"Cow pasture", {520, 264}},
         {"Pig enclosure", {776, 264}}}};
    if (auto atlas = assets_.Texture("dataS2/menu/hud/mapHotspots_1024.dds"))
      for (size_t i = 0; i < legend.size(); ++i) {
        int y = 246 + int(i) * 132;
        Fill(out, 908, y - 15, 312, 1, Rule);
        Blit(out, *atlas, 908, y + 16, 44, 44, legend[i].second[0],
             legend[i].second[1], 240, 240);
        auto lines = font_.Wrap(legend[i].first, 32, 244);
        for (size_t l = 0; l < std::min<size_t>(2, lines.size()); ++l)
          Text(out, lines[l], 976, y + 10 + l * 36, 32);
      }
    return;
  }
  Text(out, "VEHICLE", 908, 98, 32, Muted, true);
  auto model = assets_.Vehicle(s.vehicle_config);
  auto name = s.vehicle_name.empty() ? model.name : s.vehicle_name;
  auto art = s.vehicle_image.empty() ? model.image : s.vehicle_image;
  auto lines = font_.Wrap(name.empty() ? "-" : name, 32, 312, true);
  for (size_t i = 0; i < std::min<size_t>(2, lines.size()); ++i)
    Text(out, lines[i], 908, 142 + int(i) * 36, 32, White, true);
  if (!art.empty())
    Art(out, art, 893, 224, 342, 170, true);
  Fill(out, 908, 405, 312, 1, Rule);
  Icon(out, "speed", 910, 442, 38);
  Text(out, Value(s.speed_kmh), 970, 432, 38, White, true);
  Text(out, "km/h", 1110, 482, 32, Muted);
  Icon(out, "fuel", 910, 554, 38);
  Text(out, FloorValue(s.fuel_l), 970, 544, 38, White, true);
  auto fuel = "/ " + Value(s.fuel_capacity_l) + " l";
  Text(out, fuel, 1220 - font_.Measure(fuel, 32), 594, 32, Muted);
  if (!s.fill_icon.empty())
    Art(out, s.fill_icon, 910, 676, 38, 38);
  bool fill = s.fill_l && s.fill_capacity_l && *s.fill_capacity_l > 0;
  Text(out, fill ? Number(*s.fill_l / *s.fill_capacity_l * 100) + "%" : "-",
       970, 676, 38, White, true);
  auto fill_lines =
      font_.Wrap(s.fill_name.empty() ? "-" : s.fill_name, 32, 312, true);
  for (size_t i = 0; i < std::min<size_t>(2, fill_lines.size()); ++i)
    Text(out, fill_lines[i], 908, 734 + i * 36, 32, Muted, true);
  Text(out, Value(s.fill_l) + " / " + Value(s.fill_capacity_l) + " l", 908, 816,
       32);
  Fill(out, 908, 864, 312, 14, 0x474747ff);
  if (fill)
    Fill(out, 908, 864,
         int(std::clamp(*s.fill_l / *s.fill_capacity_l, 0., 1.) * 312), 14,
         Selected);
}
int MapMarkerFilterIndex(const MapMarker &marker) {
  // Order matches UX-SPEC's native filter table (InGameMenu.xml line ~149):
  // Steerable, Combine, Trailer, Tool, TipStations, FieldJobs, Other.
  // The specific vehicle types first: the native kinds are VEHICLE_COMBINE / VEHICLE_TRAILER /
  // VEHICLE_TOOL / VEHICLE_STEERABLE, so a generic "VEHICLE" test must come last or every
  // trailer, tool and harvester lands in Vehicles.
  if (CiContains(marker.kind, "COMBINE") || CiContains(marker.kind, "HARVEST"))
    return 1;
  if (CiContains(marker.kind, "TRAILER"))
    return 2;
  if (CiContains(marker.kind, "TOOL"))
    return 3;
  if (CiContains(marker.kind, "STEER") || CiContains(marker.kind, "VEHICLE"))
    return 0;
  if (CiContains(marker.kind, "TIP") || CiContains(marker.kind, "SELL"))
    return 4;
  if (CiContains(marker.kind, "FIELDJOB") || CiContains(marker.kind, "JOB"))
    return 5;
  return 6; // Other: DEFAULT/TRIGGERS/unrecognized categories, matching the
            // native filterButtonOther catch-all.
}
int MapMarkerDrawRank(const MapMarker &marker) {
  switch (MapMarkerFilterIndex(marker)) {
  case 2: case 3: return 0;
  case 0: case 1: return 2;
  default: return 1;
  }
}
void View::MapFiltersRows(Image &out, const ViewState &state, int x, int y,
                          int w, int row_h) {
  static constexpr std::array<std::string_view, 7> labels{
      "Vehicles",  "Harvesters",     "Trailers", "Tools",
      "Selling points", "Field jobs", "Other"};
  static constexpr std::array<std::string_view, 7> icons{
      "filter_vehicle", "filter_combine", "filter_trailer",   "filter_tool",
      "filter_tipping", "filter_fieldjobs", "filter_other"};
  // G3 critique Finding 9/"E"+"F" (native tile style, not ON/OFF pills): guiProfiles.xml's
  // own ingameMenuMapFilterButtonIcon{X}/{X}Inactive profiles (confirmed by reading the
  // installed build's own GUI definitions) tint the SAME icon green when active and dark grey
  // when inactive -- the tile itself never changes fill, and there is no separate ON/OFF text
  // at all; the icon's own colour already carries the state, same as the spec's own "the icon
  // alone already carries meaning" note. Dropping the pill also frees the row width for a full
  // 32px label on every row (no more icon-size/label-size tradeoff against pill space).
  constexpr int IconSize = 44, TileSize = 56;
  for (size_t i = 0; i < 7; ++i) {
    int row_y = y + int(i) * row_h;
    if (i)
      Fill(out, x, row_y, w, 1, Rule);
    bool on = state.map_filters[i];
    int tile_x = x, tile_y = row_y + (row_h - TileSize) / 2;
    Fill(out, tile_x, tile_y, TileSize, TileSize, Row); // dark tile, same fill on or off
    Icon(out, icons[i], tile_x + (TileSize - IconSize) / 2, tile_y + (TileSize - IconSize) / 2,
         IconSize, on ? Selected : Muted);
    int label_x = x + TileSize + 16;
    RowText(out, std::string(labels[i]), label_x, row_y, row_h, 32, on ? White : Muted, false);
  }
}
void View::MapFiltersCard(Image &out, const ViewState &state) {
  // G3 critique Finding 9/"F" (LEGIBILITY.md dense-list floor): expanded rows grew from 70px
  // to the 88px floor (7*88=616, was 7*70=490); CardH grew 604->712 to match
  // (76 header + 616 rows + 20 bottom margin).
  constexpr int CardX = 732, CardY = 96, CardW = 492, CardH = 712;
  Fill(out, CardX, CardY, CardW, CardH, Rule);
  Fill(out, CardX + 2, CardY + 2, CardW - 4, CardH - 4, Row);
  RowText(out, "FILTERS", CardX + 20, CardY + 16, 48, 32, White, true);
  MapFiltersRows(out, state, CardX + 20, CardY + 76, CardW - 40, 88);
}
bool View::RenderMapBase(const ViewState &state, Image &out) {
  const auto &s = state.snapshot;
  if (!(s.world_ready && (s.map_id == 1 || s.map_id == 2)))
    return false;
  auto texture =
      assets_.Texture("data/maps/map0" + std::to_string(s.map_id) + "/pda_map_H.dds");
  if (!texture)
    return false;
  // texture is the complete MapWorldExtent x MapWorldExtent world, square.
  // Cap the working resolution (memory/render-cost bound, same convention as
  // the rest of this module's bounded caches) and derive the padded canvas
  // from it: MapCanvasW/H world units at the same px-per-world-unit, so the
  // compact and expanded pan_zoom widgets both read this one image at an
  // identical scale (see farming_simulator_view.h for why).
  const int map_px = std::min(int(texture->w), 1536);
  const int canvas_w = int(std::lround(map_px * MapCanvasW / MapWorldExtent));
  const int canvas_h = int(std::lround(map_px * MapCanvasH / MapWorldExtent));
  out.w = canvas_w;
  out.h = canvas_h;
  out.rgba.assign(size_t(canvas_w) * canvas_h * 4, 0);
  Fill(out, 0, 0, canvas_w, canvas_h, Background);
  const int off_x = (canvas_w - map_px) / 2, off_y = (canvas_h - map_px) / 2;
  Blit(out, *texture, off_x, off_y, map_px, map_px, 0, 0, int(texture->w), int(texture->h));
  if (state.map_layer && s.map_layers) {
    const auto &overlay = state.map_layer == 1 ? s.map_layers->crops
                          : state.map_layer == 2 ? s.map_layers->growth
                                                 : s.map_layers->soil;
    Blit(out, overlay, off_x, off_y, map_px, map_px, 0, 0, int(overlay.w), int(overlay.h));
  }
  auto screen = [&](double x, double z) {
    return std::pair{off_x + int(std::lround((x + MapWorldExtent / 2) / MapWorldExtent * map_px)),
                     off_y + int(std::lround((z + MapWorldExtent / 2) / MapWorldExtent * map_px))};
  };
  const auto &points = assets_.Map(s.map_id);
  for (auto f : points.fields) {
    auto [x, y] = screen(f.x, f.z);
    if (s.map_id == 2 && f.number == 9)
      y += int(map_px * .030);
    auto label = std::to_string(f.number);
    int left = x - font_.Measure(label, 32, true) / 2;
    Text(out, label, left + 1, y - 11, 32, Black, true);
    Text(out, label, left, y - 12, 32, White, true);
  }
  if (auto atlas = assets_.Texture("dataS2/menu/hud/mapHotspots_1024.dds"))
    for (auto h : points.hotspots) {
      // G3 critique Finding 9 (BUG): these static, map-XML-authored hotspots (Shop, animal
      // pastures, and named selling points -- confirmed by reading map02.xml's own "name"
      // attribute: "WoodSelling1"/"WoodSelling2"/"SilageHeap", none of which happen to carry
      // a "category" distinct enough to classify on its own) were never covered by the
      // FILTERS popup, so turning a category off still left a static hotspot of that category
      // on the map. Animal pastures (Cows/Sheep/Pigs/Chickens) are never gated here, matching
      // UX-SPEC §A.1's own finding that Animals has no Filters category at all (reached via
      // the MAP OVERVIEW destination chooser instead); Shop maps to "Other" per the same
      // section's flagged mapping. Everything else with "Sell"/"Silage"/"Lumberyard" in its
      // own name is treated as a selling point, the same category a live tip-trigger marker
      // of the same kind gets from MapMarkerFilterIndex.
      bool animal = CiContains(h.name, "Cow") || CiContains(h.name, "Sheep") ||
                    CiContains(h.name, "Pig") || CiContains(h.name, "Chicken");
      if (!animal) {
        int index = CiContains(h.name, "Sell") || CiContains(h.name, "Silage") ||
                            CiContains(h.name, "Lumberyard")
                        ? 4  // Selling points
                        : 6; // Other (covers Shop)
        if (!state.map_filters[size_t(index)])
          continue;
      }
      auto [x, y] = screen(h.x, h.z);
      Circle(out, x, y, 21, White);
      Circle(out, x, y, 19, Black);
      Blit(out, *atlas, x - 15, y - 15, 30, 30, h.sx, h.sy, h.w, h.h, h.colour);
    }
  // Tools/trailers first, drivable vehicles last: a harvester's header shares the
  // combine's position and used to cover its marker completely.
  std::vector<size_t> draw_order(s.map_markers.size());
  std::iota(draw_order.begin(), draw_order.end(), size_t{0});
  std::stable_sort(draw_order.begin(), draw_order.end(), [&](size_t a, size_t b) {
    return MapMarkerDrawRank(s.map_markers[a]) < MapMarkerDrawRank(s.map_markers[b]);
  });
  for (size_t marker_index : draw_order) {
    const auto &marker = s.map_markers[marker_index];
    if (!marker.enabled || marker.image.empty() || !std::isfinite(marker.x) ||
        !std::isfinite(marker.z))
      continue;
    // FILTERS popup (UX-SPEC §A): hide this marker's whole category when its
    // toggle is off. The static legend circles (points.hotspots, drawn
    // above) are filtered the same way now too -- G3 critique Finding 9.
    if (!state.map_filters[size_t(MapMarkerFilterIndex(marker))])
      continue;
    // Static map locations are already baked in above. A dynamic marker with
    // another sprite at the same location (e.g. a target) still draws.
    bool duplicate = false;
    for (const auto &h : points.hotspots)
      if (std::abs(h.x - marker.x) < 1 && std::abs(h.z - marker.z) < 1 &&
          h.sx == marker.sx && h.sy == marker.sy && h.w == marker.w && h.h == marker.h)
        duplicate = true;
    if (duplicate)
      continue;
    auto atlas = assets_.Texture(marker.image);
    if (!atlas)
      continue;
    const int sw = marker.w > 0 ? marker.w : int(atlas->w),
              sh = marker.h > 0 ? marker.h : int(atlas->h);
    const int fw = std::clamp(marker.draw_w * 2, 24, 64), fh = std::clamp(marker.draw_h * 2, 24, 64);
    const bool background =
        !marker.background_image.empty() && marker.background_w > 0 && marker.background_h > 0;
    const int gw = background ? int(std::lround(fw * 1.4)) : fw,
              gh = background ? int(std::lround(fh * 1.4)) : fh;
    Image glyph;
    glyph.w = gw;
    glyph.h = gh;
    glyph.rgba.resize(size_t(gw) * gh * 4);
    if (background)
      if (auto bg = assets_.Texture(marker.background_image))
        Blit(glyph, *bg, 0, 0, gw, gh, marker.background_sx, marker.background_sy,
             marker.background_w, marker.background_h, marker.background_color);
    Blit(glyph, *atlas, (gw - fw) / 2, (gh - fh) / 2, fw, fh, marker.sx, marker.sy, sw, sh,
         marker.color);
    const int side = int(std::ceil(std::hypot(gw, gh)));
    Image rotated;
    rotated.w = rotated.h = side;
    rotated.rgba.resize(size_t(side) * side * 4);
    const double c = std::cos(marker.rotation), sn = std::sin(marker.rotation);
    for (int yy = 0; yy < side; ++yy)
      for (int xx = 0; xx < side; ++xx) {
        const double dx = xx - (side - 1) / 2., dy = yy - (side - 1) / 2.;
        const int u = int(std::lround(c * dx - sn * dy + (gw - 1) / 2.)),
                  v = int(std::lround(sn * dx + c * dy + (gh - 1) / 2.));
        if (u >= 0 && u < gw && v >= 0 && v < gh)
          std::copy_n(&glyph.rgba[(v * gw + u) * 4], 4, &rotated.rgba[(yy * side + xx) * 4]);
      }
    auto [x, y] = screen(marker.x, marker.z);
    // G3 critique Finding 6: highlight the tapped marker on the map itself while its
    // selection card is open -- a lime ring around it, same accent colour the rest of the
    // companion already uses for a selected/active state, drawn OUTSIDE the marker's own
    // rotated sprite so the sprite's own art stays unobscured.
    if (int64_t(marker_index) == state.map_marker_selected) {
      Circle(out, x, y, side / 2 + 6, Selected);
      Circle(out, x, y, side / 2 + 2, 0);
    }
    Blit(out, rotated, x - side / 2, y - side / 2, side, side);
  }
  if (s.player_x && s.player_z && !s.player_heading) {
    // Heading known is the common case (on-foot and vehicle both read
    // playerRotation -- see the reader) and is now drawn as a separate,
    // unthrottled overlay Image widget (map-arrow-compact/expanded,
    // gen_manifest.py; RenderMapArrow below) turned live by the runtime's
    // rotate_bind every module tick, instead of being baked, pre-rotated,
    // into this ~5Hz MapSnapshotKey-gated raster -- that bake was the
    // reported "5fps" map motion. Only the rare heading-unknown fallback
    // (a plain recentre dot, no rotation to animate) still bakes here.
    auto [x, y] = screen(*s.player_x, *s.player_z);
    Circle(out, x, y, 21, White);
    Circle(out, x, y, 18, Selected);
    Icon(out, "recentre", x - 17, y - 17, 34);
  }
  return true;
}
bool View::RenderMapArrow(Image &out) {
  // Live ingameMap.playerMapArrowOverlay: 256px atlas UVs are
  // U=.2578125..4921875,V=.7578125..9921875. After verified texture storage
  // flip this is (66,2,60,60). Native overlay uses playerRotation and linear
  // RGB(.2705,.6514,.0802). No substitute cursor/arrow. Served unrotated --
  // the runtime's rotate_bind (fs.ui.map_arrow_heading_deg) turns it live, so
  // this constant sprite is rendered once and cached forever, the same
  // convention as RenderResetButton.
  //
  // G3 critique (2026-10-17, "player marker lost its native sprite and
  // heading"): the previous pass (see git history) kept widening the lime
  // ring by repeatedly shrinking the native glyph's own blit inside it, and
  // ended up drawing the ring over a plain black disc with no glyph blit at
  // all -- the native direction-arrow/eye glyph and its heading wedge
  // dropped out entirely, and the marker stopped matching the top-screen
  // minimap. UX-SPEC section D never asked for the glyph to disappear:
  // "pedestrian glyph fill unchanged... only what sits behind/around it
  // changes." Fix: blit the native glyph back on top of the same ring+disc
  // backing the prior pass already proved legible (lime ring, black
  // contrast disc -- UX-SPEC D's own "raise contrast" recommendation), sized
  // so it stays readable as the teardrop/eye pointer it actually is.
  //
  // A first attempt kept the 60x60 canvas and shrank the glyph to 28x28 (the
  // same corner-reach-safe size an even earlier pass used against a smaller
  // disc): the ring stayed clean, but at 28px the glyph's own diagonal
  // pointer anti-aliases away to almost nothing. A second attempt grew the
  // canvas to 84x84 and the glyph to 44x44 behind a BLACK contrast disc (the
  // disc UX-SPEC D itself recommended) -- but the atlas crop's own pointer
  // outline is black (verified by a raw pixel scan of the crop), so a black
  // outline on a black disc is black-on-black: only the small lime "eye"
  // ring stayed visible, same symptom as the first attempt. The native
  // "before" capture never had this problem because its backing was the
  // live map's own terrain, not a solid colour. Fix: drop the black
  // contrast disc and let the ring's own Lime fill be the glyph's backing
  // instead -- the glyph's black outline reads clearly against Lime (same
  // high contrast UX-SPEC D asked for), and its own internal Lime
  // highlights simply blend into that same fill, which is visually
  // harmless (they were never the part that reads as "pointer"). Net
  // result: one solid Lime disc (the "ring" IS the backing here, not a
  // separate band) with the native glyph's black silhouette -- arrow tip +
  // eye -- legible on top of it, matching the native glyph's own
  // black-outline convention. Canvas stays 84x84 (task note: "stay <=64px
  // if possible, else enlarge the overlay widget rects consistently" -- see
  // gen_manifest.py's map-arrow-{compact,expanded} rect and
  // farming_simulator_view.h's matching static_assert, both updated to
  // match) so the glyph renders at 44x44, large enough to stay recognisably
  // a pointer rather than shrinking to an anti-aliased blur.
  auto atlas = assets_.Texture("dataS2/menu/hud/mapHotspots_256.dds");
  if (!atlas)
    return false;
  // Owner feedback (2026-10-17, "is there no direction arrow for the player marker
  // now? make a direction back."): the previous pass's Lime-on-Lime treatment (a
  // Lime disc with the native glyph blitted back on top ALSO tinted Lime, see the
  // G3 critique above) made the heading unreadable again -- a lime pointer sitting
  // on a lime disc has no contrast at all, even though the glyph itself was no
  // longer missing. Two changes, same 84x84 canvas and same rotate_bind convention
  // (this whole image rotates live as one unit, so "up" in this unrotated canvas is
  // always "forward" -- unchanged):
  //  1. The native glyph is now blitted UNTINTED (default white tint = passthrough,
  //     see Blit()'s tint==0xffffffff fast path) at a larger size, so it keeps its
  //     own real colours -- the green icon + black heading pointer the top-screen
  //     minimap already shows -- instead of being recoloured Lime.
  //  2. A separate high-contrast wedge (dark outline, Lime fill) is drawn pointing
  //     "up" (the same forward direction the glyph itself already encodes) and
  //     sized to stick out past the backing disc's edge, so the heading reads at a
  //     glance even before the eye finds the glyph's own small pointer.
  constexpr int Canvas = 84, GlyphSize = 56, DiscRadius = 30;
  out.w = out.h = Canvas;
  out.rgba.assign(size_t(Canvas) * Canvas * 4, 0);
  constexpr uint32_t Lime = 0x8ed350ff, Outline = 0x14140aff;
  const int c = Canvas / 2;
  // Filled triangle (apex up), inclusive-edge scanline fill -- no antialiasing
  // needed at this size, matching the rest of this file's flat-fill primitives.
  auto triangle = [&](int apex_x, int apex_y, int base_half_w, int base_y, uint32_t colour) {
    for (int yy = std::max(0, apex_y); yy <= std::min(Canvas - 1, base_y); ++yy) {
      double t = base_y == apex_y ? 1. : double(yy - apex_y) / (base_y - apex_y);
      int half = int(std::lround(base_half_w * t));
      for (int xx = std::max(0, apex_x - half); xx <= std::min(Canvas - 1, apex_x + half); ++xx) {
        auto *d = &out.rgba[(size_t(yy) * Canvas + xx) * 4];
        d[0] = colour >> 24;
        d[1] = colour >> 16;
        d[2] = colour >> 8;
        d[3] = colour;
      }
    }
  };
  // Outline wedge first (slightly wider/taller), then the Lime fill inset inside
  // it, then the backing disc on top -- the disc's top edge (c - DiscRadius)
  // swallows the wedge's base, leaving only its pointed tip sticking out above
  // the disc, exactly the "wedge at the disc edge" the owner asked for.
  triangle(c, 0, 17, 36, Outline);
  triangle(c, 4, 12, 34, Lime);
  Circle(out, c, c, DiscRadius, Lime);
  Blit(out, *atlas, c - GlyphSize / 2, c - GlyphSize / 2, GlyphSize, GlyphSize, 66, 2, 60, 60);
  return true;
}
bool View::RenderResetButton(Image &out) {
  if (!font_.Load(assets_))
    return false;
  out.w = 180;
  out.h = 88;
  out.rgba.assign(size_t(180) * 88 * 4, 0);
  Button(out, "Reset", 0, 0, 180, 88);
  return true;
}
bool View::RenderMapLegendRow(const ViewState &state, int layer, size_t row_index,
                              Image &out) {
  const auto &s = state.snapshot;
  if (!s.map_layers)
    return false;
  const auto &legend = layer == 1 ? s.map_layers->crop_legend
                       : layer == 2 ? s.map_layers->growth_legend
                                    : s.map_layers->soil_legend;
  if (row_index >= std::min(legend.size(), MapLegendRowCap))
    return false;
  const auto &row = legend[row_index];
  out.w = MapLegendRowWidth;
  out.h = MapLegendRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, MapLegendRowWidth, 92, Row);
  for (size_t c = 0; c < row.colors.size(); ++c) {
    int left = int(c * 52 / row.colors.size()), right = int((c + 1) * 52 / row.colors.size());
    Fill(out, left, 20, right - left, 52, row.colors[c]);
  }
  if (!row.icon.empty()) {
    Image glyph;
    glyph.w = glyph.h = 44;
    glyph.rgba.resize(44 * 44 * 4);
    Art(glyph, row.icon, 0, 0, 44, 44, true);
    auto shadow = glyph;
    for (size_t px = 0; px < shadow.rgba.size(); px += 4)
      shadow.rgba[px] = shadow.rgba[px + 1] = shadow.rgba[px + 2] = 0;
    for (auto [dx, dy] : {std::pair{-2, 0}, std::pair{2, 0}, std::pair{0, -2}, std::pair{0, 2}})
      Blit(out, shadow, 4 + dx, 24 + dy, 44, 44);
    Blit(out, glyph, 4, 24, 44, 44);
  }
  auto lines = font_.Wrap(Label(row.label), 32, 236);
  RowLines(out, lines, 68, 0, 92, 32, White, false);
  return true;
}
void View::HelpPage(Image &out, const ViewState &state) {
  auto &topics = assets_.Help();
  if (topics.empty()) {
    Text(out, "Help unavailable", 28, 140, 28, Muted);
    return;
  }
  size_t topic = std::min(state.help_topic, topics.size() - 1);
  auto &selected = topics[topic];
  // The topics list and the article body are no longer baked here: they are
  // the runtime's native "help-topics" and "help-article"/"help-article-
  // static" ScrollRegions now (RenderHelpTopicRow/RenderHelpArticle below).
  // Only the list's background strip and the current topic's category/title
  // header stay in this frame image; neither scrolls on its own.
  Fill(out, 0, 80, 352, 888, Row);
  Text(out, selected.category, 380, 96, 32, Muted, true);
  auto title_lines = font_.Wrap(selected.title, 38, 640, true);
  for (size_t i = 0; i < std::min<size_t>(2, title_lines.size()); ++i)
    Text(out, title_lines[i], 380, 142 + i * 42, 38, White, true);
}
bool View::RenderHelpTopicRow(const ViewState &state, size_t index, bool active, Image &out) {
  auto &topics = assets_.Help();
  if (index >= topics.size())
    return false;
  out.w = 352;
  out.h = HelpTopicRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, 352, HelpTopicRowHeight, Row);
  Fill(out, 12, 0, 328, 136, active ? Selected : Background);
  auto lines = font_.Wrap(topics[index].title, 32, 296, true);
  RowLines(out, lines, 28, 0, 136, 32, White, true);
  (void)state;
  return true;
}
bool View::RenderHelpArticle(const ViewState &state, size_t topic_index, Image &out,
                             int *content_h_out) {
  auto &topics = assets_.Help();
  if (topic_index >= topics.size())
    return false;
  const auto &selected = topics[topic_index];
  constexpr int Width = 832;
  // Two passes: the first only measures the total content height (native
  // mixed illustration/text order, same math the old clipped-viewport
  // render used, just without a viewport to clip against); the second draws
  // the real content, top-aligned, into an image that is ALWAYS exactly
  // HelpArticleCap tall -- like Prices/Finance's fixed-cap images, a
  // scrolled repeat-1 image's declared rect is static, so a shorter
  // article's picture must not actually be a shorter picture (it would
  // stretch to fill the widget's rect instead of scrolling at 1:1 pixels).
  // content_h_out, when given, receives the REAL (uncapped-render, but
  // capped for sanity) content height for content_h_bind -- the scrollable
  // range, not the image's own allocated size.
  const auto walk = [&](Image *target) {
    int y = 0;
    for (const auto &item : selected.items) {
      if (item.image) {
        if (auto image = assets_.Texture(item.value)) {
          double factor = std::min(1., 780. / image->w);
          int w = std::lround(image->w * factor),
              // Native helpLineImage is 584x146. The DDS ratio already
              // encodes heightScale.
              h = std::lround(w * (146. / 584.) * item.height_scale);
          if (target)
            Blit(*target, *image, (Width - w) / 2, y, w, h);
          y += h + 24;
        }
      } else {
        std::string value = item.value;
        const std::string token = "$CURRENCY_SYMBOL";
        size_t p = 0;
        while ((p = value.find(token, p)) != value.npos) {
          if (state.snapshot.currency_symbol.empty()) {
            value = "Currency-dependent help text unavailable";
            break;
          }
          value.replace(p, token.size(), state.snapshot.currency_symbol);
          p += state.snapshot.currency_symbol.size();
        }
        for (const auto &line : font_.Wrap(value, 32, 780)) {
          if (target)
            Text(*target, line, 8, y, 32);
          y += 42;
        }
        y += 24;
      }
    }
    return y;
  };
  const int content_h = std::clamp(walk(nullptr), 1, int(HelpArticleCap));
  if (content_h_out)
    *content_h_out = content_h;
  out.w = Width;
  out.h = int(HelpArticleCap);
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, Width, content_h, Background);
  walk(&out);
  return true;
}
void View::Finances(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.finances_ready) {
    Unavailable(out, s.finances_loading, 28, 140, 32);
    return;
  }
  // The day header, row-label column, the row grid itself and the totals
  // row's day amounts are no longer baked here: they are the runtime's
  // native "fs-finance" two-axis ScrollRegion (gen_manifest.py), served as
  // separate module images (RenderFinanceHeader/RenderFinanceLabels/
  // RenderFinanceRow/RenderFinanceTotals below). Only the static chrome --
  // the three summary boxes, the "Income/Expenditure" heading, the rule
  // line and the "Total" label -- stays in this frame image; none of it
  // scrolls.
  for (int i = 0; i < 3; ++i) {
    int x = 20 + i * 400;
    Fill(out, x, 96, 396, 104, Row);
    RowText(out, Label(i == 0 ? "ui_money" : i == 1 ? "ui_loan" : "ui_saldo"),
            x + 16, 104, 40, 32, White, true);
    auto v = i == 0 ? s.money : i == 1 ? s.loan : s.finance_saldo;
    auto value = FloorValue(v) +
        (v && !s.currency_symbol.empty() ? " " + s.currency_symbol : "");
    RowText(out, value, x + 16, 146, 46, 32,
            v && *v < 0 ? 0xff7777ff : White, true);
  }
  Fill(out, 20, 216, 1200, 76, 0x474747ff);
  auto heading = Label("ui_incomeExpenditure");
  if (!s.currency_symbol.empty())
    heading += " (" + s.currency_symbol + ")";
  auto headings = font_.Wrap(heading, 32, FinanceLabelWidth - 32, true);
  RowLines(out, headings, 36, 216, 76, 32, White, true);
  Fill(out, 20, 904, 1200, 2, Selected);
  RowText(out, Label("ui_total"), 36, 908, 44, 32, White, true);
}
bool View::RenderFinanceHeader(const ViewState &state, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.finances_ready)
    return false;
  out.w = FinanceContentW;
  out.h = FinanceRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  for (int j = 0; j < FinanceDayCount; ++j) {
    if (size_t(j) >= s.finance_day_labels.size())
      continue;
    auto label = s.finance_day_labels[j].empty() ? "-" : Label(s.finance_day_labels[j]);
    int x = j * FinanceDayWidth;
    RowText(out, label, x + (FinanceDayWidth - font_.Measure(label, 32, true)) / 2,
            0, FinanceRowHeight, 32, White, true);
  }
  return true;
}
bool View::RenderFinanceLabels(const ViewState &state, size_t chunk_index, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.finances_ready || chunk_index >= FinanceLabelChunks)
    return false;
  size_t rows = std::min<size_t>(s.finance_rows.size(), FinanceRowCap);
  const size_t row_base = chunk_index * FinanceLabelChunk;
  out.w = FinanceLabelWidth;
  out.h = FinanceRowHeight * int(FinanceLabelChunk);
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  for (size_t i = row_base; i < std::min(rows, row_base + FinanceLabelChunk); ++i) {
    int y = int(i - row_base) * FinanceRowHeight;
    Fill(out, 0, y, FinanceLabelWidth, FinanceRowHeight, i % 2 ? Row : Background);
    auto lines = font_.Wrap(Label(s.finance_rows[i].label), 32, FinanceLabelWidth - 32, true);
    RowLines(out, lines, 16, y, FinanceRowHeight, 32, White, true);
  }
  return true;
}
bool View::RenderFinanceRow(const ViewState &state, size_t row_index, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.finances_ready || row_index >= s.finance_rows.size())
    return false;
  out.w = FinanceContentW;
  out.h = FinanceRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, out.w, out.h, row_index % 2 ? Row : Background);
  const auto &row = s.finance_rows[row_index];
  auto amount = [&](const std::optional<double> &v, int x, bool bold = false) {
    // The verified currency is shown in the column heading. Preserve large
    // numbers at 32px.
    auto value = FloorValue(v);
    RowText(out, value, x + FinanceDayWidth - 16 - font_.Measure(value, 32, bold),
            0, FinanceRowHeight, 32, v && *v < 0 ? 0xff7777ff : White, bold);
  };
  for (int j = 0; j < FinanceDayCount; ++j) {
    int x = j * FinanceDayWidth;
    Fill(out, x, 0, 1, FinanceRowHeight, Black);
    if (size_t(j) < row.amounts.size())
      amount(row.amounts[j], x);
  }
  return true;
}
bool View::RenderFinanceTotals(const ViewState &state, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.finances_ready)
    return false;
  out.w = FinanceContentW;
  out.h = 44;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  for (int j = 0; j < FinanceDayCount; ++j) {
    if (size_t(j) >= s.finance_totals.size())
      continue;
    auto value = FloorValue(s.finance_totals[j]);
    auto v = s.finance_totals[j];
    int x = j * FinanceDayWidth;
    RowText(out, value, x + FinanceDayWidth - 16 - font_.Measure(value, 32, true),
            0, 44, 32, v && *v < 0 ? 0xff7777ff : White, true);
  }
  return true;
}
void View::Statistics(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.statistics_ready) {
    Unavailable(out, s.statistics_loading, 28, 140, 32);
    return;
  }
  // The row list itself is no longer baked here: it is the runtime's native
  // "statistic-rows" ScrollRegion now (RenderStatisticRow below). Only the
  // static header row (never scrolls) stays in this frame image.
  Fill(out, 20, 94, 1200, 76, 0x474747ff);
  RowText(out, Label("ui_statistic"), 36, 94, 76, 32, White, true);
  auto session = Label("ui_session"), total = Label("ui_total");
  RowText(out, session, 930 - font_.Measure(session, 32, true), 94, 76, 32, White, true);
  RowText(out, total, 1204 - font_.Measure(total, 32, true), 94, 76, 32, White, true);
}
bool View::RenderStatisticRow(const ViewState &state, size_t row_index, Image &out) {
  const auto &rows = state.snapshot.statistic_rows;
  if (row_index >= std::min(rows.size(), StatisticRowCap))
    return false;
  out.w = StatisticRowWidth;
  out.h = StatisticRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, StatisticRowWidth, StatisticRowHeight,
       row_index % 2 ? Row : Background);
  const auto &r = rows[row_index];
  auto label = Label(r.label);
  if (!r.unit.empty() && Label(r.unit) != "-")
    label += " [" + Label(r.unit) + "]";
  auto lines = font_.Wrap(label, 32, 640, true);
  RowLines(out, lines, 16, 0, StatisticRowHeight, 32, White, true);
  auto a = r.session_text.value_or(
           Value(r.session, std::clamp(r.digits, 0, 4))),
       b = r.total_text.value_or(Value(r.total, std::clamp(r.digits, 0, 4)));
  RowText(out, a, 910 - font_.Measure(a, 32), 0, StatisticRowHeight, 32, White);
  RowText(out, b, 1184 - font_.Measure(b, 32), 0, StatisticRowHeight, 32, White);
  return true;
}
void View::Prices(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.prices_ready) {
    Unavailable(out, s.prices_loading, 28, 140, 32);
    return;
  }
  // The crop header row, station-name column and price grid itself are no
  // longer baked here: they are the runtime's native "fs-prices" two-axis
  // ScrollRegion (gen_manifest.py), served as separate module images
  // (RenderPriceHeader/RenderPriceStations/RenderPriceRow below). Only the
  // static title chrome stays in this frame image -- it never scrolls.
  Fill(out, 20, 104, 1200, 156, 0x474747ff);
  Text(out, Label("ui_sellingPoints"), 36, 164, 32, White, true);
  Text(out, Label("station_farmSilo"), 36, 286, 32, White, true);
}
bool View::RenderPriceHeader(const ViewState &state, size_t chunk_index, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.prices_ready || chunk_index >= PriceColumnChunks)
    return false;
  size_t crops = std::min<size_t>(s.price_crops.size(), PriceColumnCap);
  const size_t chunk_begin = chunk_index * PriceColumnChunk;
  const size_t chunk_end = std::min(chunk_begin + PriceColumnChunk, crops);
  out.w = PriceColumnWidth * int(PriceColumnChunk);
  out.h = 240;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  for (size_t j = chunk_begin; j < chunk_end; ++j) {
    const auto &crop = s.price_crops[j];
    int x = int(j - chunk_begin) * PriceColumnWidth;
    Fill(out, x, 0, 1, 240, Background);
    Art(out, crop.icon, x + 74, 8, 64, 64);
    auto lines = font_.Wrap(Label(crop.name), 32, PriceColumnWidth - 12);
    int y = lines.size() > 1 ? 74 : 90;
    for (size_t i = 0; i < std::min<size_t>(lines.size(), 2); ++i)
      Text(out, lines[i], x + (PriceColumnWidth - font_.Measure(lines[i], 32)) / 2,
           y + int(i) * 34, 32);
    auto stored = Value(crop.silo_l) + (crop.silo_l ? " l" : "");
    Text(out, stored, x + (PriceColumnWidth - font_.Measure(stored, 32)) / 2, 182,
         32);
  }
  return true;
}
bool View::RenderPriceStations(const ViewState &state, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.prices_ready)
    return false;
  size_t stations = std::min<size_t>(s.price_stations.size(), PriceStationCap);
  out.w = 352;
  out.h = PriceRowHeight * int(PriceStationCap);
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  for (size_t i = 0; i < stations; ++i) {
    int y = int(i) * PriceRowHeight;
    Fill(out, 0, y, 352, PriceRowHeight, i % 2 ? Row : Background);
    auto lines = font_.Wrap(Label(s.price_stations[i].name), 32, 352 - 28);
    RowLines(out, lines, 16, y, PriceRowHeight, 32);
  }
  return true;
}
bool View::RenderPriceRow(const ViewState &state, size_t station_index,
                          size_t chunk_index, Image &out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.prices_ready || station_index >= s.price_stations.size() ||
      chunk_index >= PriceColumnChunks)
    return false;
  out.w = PriceColumnWidth * int(PriceColumnChunk);
  out.h = PriceRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, out.w, out.h, station_index % 2 ? Row : Background);
  const auto &station = s.price_stations[station_index];
  size_t crops = std::min<size_t>(s.price_crops.size(), PriceColumnCap);
  const size_t chunk_begin = chunk_index * PriceColumnChunk;
  const size_t chunk_total = std::min(crops, chunk_begin + PriceColumnChunk);
  auto atlas = assets_.Texture("dataS2/menu/hud/ui_elements_1080p.dds");
  auto arrow = [&](const PriceQuote &quote, int x, int y) {
    if (!atlas || (!quote.great_demand && quote.trend != PriceTrend::Rising &&
                   quote.trend != PriceTrend::Falling))
      return;
    uint32_t colour = quote.great_demand                  ? 0x4db0d9ff
                      : quote.trend == PriceTrend::Rising ? 0x8ed350ff
                                                          : 0xff7777ff;
    int turns = quote.great_demand                  ? 2
                : quote.trend == PriceTrend::Rising ? 1
                                                    : 3;
    Image glyph;
    glyph.w = glyph.h = 16;
    glyph.rgba.assign(16 * 16 * 4, 0);
    Blit(glyph, *atlas, 0, 0, 16, 16, 418, 790, 16, 16, colour);
    Image rotated = glyph;
    for (int py = 0; py < 16; ++py)
      for (int px = 0; px < 16; ++px) {
        int sx = px, sy = py;
        for (int t = 0; t < turns; ++t) {
          int old = sx;
          sx = 15 - sy;
          sy = old;
        }
        for (int c = 0; c < 4; ++c)
          rotated.rgba[(py * 16 + px) * 4 + c] =
              glyph.rgba[(sy * 16 + sx) * 4 + c];
      }
    // G3 critique Finding 10 (LEGIBILITY.md wants information icons >=40px): grown from a
    // 16x16 blit (the glyph itself is still rotated at native 16x16 resolution, then scaled up
    // -- Blit() already supports a destination size larger than the source, the same way
    // Icon()/Art() scale up a small atlas glyph elsewhere in this module) to 40x40, the cell
    // allows it (84px row, 212px column).
    Blit(out, rotated, x, y, 40, 40);
  };
  for (size_t j = chunk_begin; j < chunk_total; ++j) {
    int x = int(j - chunk_begin) * PriceColumnWidth;
    Fill(out, x, 0, 1, PriceRowHeight, Black);
    if (j >= station.quotes.size()) {
      RowText(out, "-", x + 98, 0, PriceRowHeight, 32, Muted);
      continue;
    }
    const auto &quote = station.quotes[j];
    if (!quote.accepted)
      continue;
    uint32_t colour = quote.great_demand                        ? 0x4db0d9ff
                      : quote.highlight == PriceHighlight::High ? 0x8ed350ff
                      : quote.highlight == PriceHighlight::Low  ? 0xff7777ff
                                                                : White;
    bool valid_price =
        quote.price_per1000 && std::isfinite(*quote.price_per1000);
    if (!valid_price)
      colour = Muted;
    auto value = Value(quote.price_per1000);
    if (valid_price && !s.currency_symbol.empty())
      value += " " + s.currency_symbol;
    int size = 32;
    // 56px right margin (was 28): the trend icon grew from 16 to 40px and needs an 8px gap
    // from both the value text and the column's own right edge (40 + 8 + 8 = 56).
    RowText(out, value, x + PriceColumnWidth - 56 - font_.Measure(value, size), 0, PriceRowHeight, size, colour);
    if (valid_price)
      arrow(quote, x + PriceColumnWidth - 48, (PriceRowHeight - 40) / 2);
  }
  return true;
}
bool View::Render(const ViewState &state, Image &out) {
  if (!font_.Load(assets_))
    return false;
  out.w = 1240;
  out.h = 1080;
  // Fill() below overwrites every byte of the canvas unconditionally, so a
  // same-size resize() (the common case once `out` is a reused buffer) must
  // not pay to zero-fill it first; only an actual size change needs that.
  if (out.rgba.size() != size_t(1240) * 1080 * 4)
    out.rgba.resize(size_t(1240) * 1080 * 4);
  Fill(out, 0, 0, 1240, 1080, Background);
  const Page page = !state.snapshot.world_ready && state.page != Page::Help &&
                            (state.snapshot.phase == Phase::Title ||
                             state.snapshot.phase == Phase::Loading)
                        ? Page::Operations
                        : state.page;
  if (page == Page::Operations && (state.snapshot.phase == Phase::Title ||
                                   state.snapshot.phase == Phase::Loading)) {
    Art(out, "dataS2/menu/main_logo_en.dds", 160, 130, 560, 350);
    Art(out, "dataS2/menu/menuTractor_fendt.dds", 600, 270, 600, 640);
    Fill(out, 60, 730, 400, 120,
         state.snapshot.phase == Phase::Title ? Selected : Black);
    Text(out, state.snapshot.phase == Phase::Title ? "HELP" : "LOADING...", 100,
         770, 38, White, true);
  } else if (page == Page::Operations)
    Operations(out, state);
  else {
    if (page != Page::Animals) {
      Fill(out, 0, 0, 1240, 80, Black);
      auto name = Name(page);
      Text(out, name, (1240 - font_.Measure(name, 38, true)) / 2, 22, 38, White,
           true);
      TimeControl(out, state);
    }
    if (page == Page::Help)
      HelpPage(out, state);
    else if (page == Page::Animals)
      Animals(out, state);
    else if (page == Page::Shop)
      Shop(out, state);
    else if (page == Page::Vehicles)
      Vehicles(out, state);
    else if (page == Page::Prices)
      Prices(out, state);
    else if (page == Page::Finances)
      Finances(out, state);
    else if (page == Page::Statistics)
      Statistics(out, state);
    else {
      Text(out, "Data unavailable", 28, 140, 28, Muted);
    }
  }
  if (state.snapshot.world_ready) {
    // Hidden while the map is expanded (Operations only), so the map gets
    // the full y=80..1080 height instead of leaving a dead grey band under
    // it; the layer selector/Reset move down into the space it frees up
    // (see Operations() above).
    const bool footer_visible = !(state.map_fullscreen && page == Page::Operations);
    Footer(out, page, state.snapshot, footer_visible);
  } else if (page == Page::Help) {
    Fill(out, 0, 968, 352, 112, Black);
    Button(out, Label("button_back"), 12, 968, 328, 112);
  }
  // The Map/Operations page shows its picker inside the right pane (handled
  // in Operations() above, replacing that pane's content). Every other page
  // has no equivalent secondary pane -- its whole width is primary content --
  // so its picker anchors as a floating top-right card instead, clear of the
  // page's centre.
  if (page != Page::Operations && page != Page::Animals &&
      state.time_scale_open && state.snapshot.world_ready &&
      state.snapshot.time_scale) {
    constexpr int PopupW = 504, PopupH = 408, PopupY = 88, RightEdge = 1220;
    constexpr int PopupX = RightEdge - PopupW;
    Fill(out, PopupX, PopupY, PopupW, PopupH, Black);
    Fill(out, PopupX + 4, PopupY + 4, PopupW - 8, PopupH - 8, Background);
    RowText(out, "TIME SCALE", PopupX + 24, PopupY + 16, 48, 32, White, true);
    constexpr int factors[]{1, 5, 15, 30, 60, 120};
    for (int i = 0; i < 6; ++i) {
      int x = PopupX + 20 + (i % 2) * 236, y = PopupY + 80 + (i / 2) * 100;
      bool selected = *state.snapshot.time_scale == factors[i];
      Fill(out, x, y, 220, 88, selected ? Selected : Black);
      std::string label = std::to_string(factors[i]) + "x";
      RowText(out, label, x + (220 - font_.Measure(label, 38, true)) / 2,
              y, 88, 38, White, true);
    }
  }
  return true;
}
bool OnViewAction(ViewState &s, std::string_view action, int64_t argument,
                  size_t count, size_t animal_scroll_max) {
  // Available on every page's top bar except Animals, which has no top-bar
  // band to anchor the badge in (its full width is the species selector).
  if (action == "time_picker" && s.page != Page::Animals &&
      s.snapshot.world_ready && s.snapshot.time_scale &&
      (s.snapshot.time_scale_change_allowed || s.time_scale_open)) {
    s.time_scale_open = !s.time_scale_open;
    if (s.time_scale_open) {
      s.map_marker_selected = -1;
      s.map_filters_open = false;
    }
    return true;
  }
  // FILTERS popup toggle (UX-SPEC §A, owner request: "beside expand, add
  // filters that pop up"). Map-tab only, unlike the time-scale badge which
  // is available on every page -- checked here, ahead of the
  // s.time_scale_open gate below, so Filters stays tappable even while the
  // time-scale picker is open (closing it, same "last tap wins" rule).
  if (action == "map_filters_open" && s.page == Page::Operations &&
      s.snapshot.world_ready) {
    s.map_filters_open = !s.map_filters_open;
    if (s.map_filters_open) {
      s.time_scale_open = false;
      s.map_marker_selected = -1;
    }
    return true;
  }
  if (action == "map_filter_toggle" && s.map_filters_open && argument >= 0 &&
      argument < 7) {
    s.map_filters[size_t(argument)] = !s.map_filters[size_t(argument)];
    return true;
  }
  if (action == "page" && argument >= 0 && argument <= int(Page::Help)) {
    if (!s.snapshot.world_ready && argument != int(Page::Operations) &&
        argument != int(Page::Help))
      return false;
    s.page = Page(argument);
    s.time_scale_open = false;
    s.map_marker_selected = -1;
    // Standard page-navigation closes any open right-pane popup (UX-SPEC
    // §A.3's "tap any footer tab" close rule).
    s.map_filters_open = false;
    return true;
  }
  if (s.time_scale_open)
    return false;
  if (s.page == Page::Operations && s.snapshot.world_ready) {
    if (action == "map_fullscreen" && s.snapshot.map_id >= 1 && s.snapshot.map_id <= 2) {
      s.map_fullscreen = !s.map_fullscreen;
      // The selection card now has an expanded-mode equivalent too (UX-SPEC C review's
      // floating-card pattern, View::Operations' MapSelectionCard branch), so unlike the
      // time-scale picker/legend (both compact-right-pane-only) a marker selection survives
      // Expand/Restore instead of being cleared here.
      return true;
    }
    // map_legend_offset no longer zeroed on a layer/legend change: the
    // legend list is the runtime's own native "map-legend" ScrollRegion now
    // (RenderMapLegendRow), reset to the top by its own reset_bind (a hash
    // of map_layer/world_identity in 0100D04004176000.cpp) whenever either
    // action below actually fires.
    if(action=="map_cycle" && (argument==-1 || argument==1) && s.snapshot.map_layers) {
      s.map_layer=(s.map_layer+int(argument)+4)%4;
      s.map_legend=s.map_layer!=0;
      if (s.map_legend) s.map_marker_selected = -1;
      return true;
    }
    // A direct-index "map_layer" action (argument = the layer 0..3) used to
    // live here too, but gen_manifest.py never wires a widget to send it --
    // only "map_cycle" (+1/-1, above) has a button -- so it was dead code.
    if (action == "map_legend" && s.map_layer && s.snapshot.map_layers &&
        (argument == 0 || argument == 1)) {
      s.map_legend = bool(argument); return true;
    }
    // Map marker tap-select (UX-SPEC C): pure UI selection, like vehicle_pick/shop_item below
    // -- no game call here (garage_enter/garage_reset/map_visit/map_tag, which DO call the
    // game, stay in Module::Action/Reader, gated on the reader being alive). argument is the
    // tapped marker's absolute index (gen_manifest.py's per-marker hit widgets, payload
    // "$tap"-less direct int per marker). Only a selectable marker (enabled, and either a
    // place or a known vehicle category -- same rule View::Operations' card and
    // Module::Action's garage_enter/reset vehicle-marker lookup both re-check) is selectable.
    if (action == "map_marker_pick") {
      const auto &markers = s.snapshot.map_markers;
      if (argument < 0 || size_t(argument) >= markers.size())
        return false;
      const auto &m = markers[size_t(argument)];
      if (!m.enabled || !(m.can_visit || m.kind.starts_with("VEHICLE_")) ||
          !s.map_filters[size_t(MapMarkerFilterIndex(m))])
        return false;
      s.map_marker_selected = argument;
      // Mutually exclusive with the time-scale picker/legend right-pane content (UX-SPEC
      // C.4): a marker tap takes the pane, the same "last tap wins" rule the time-scale
      // picker's own OnViewAction branch above already established over MAP OVERVIEW.
      s.time_scale_open = false;
      s.map_filters_open = false;
      s.map_legend = false;
      return true;
    }
    // "map_legend_scroll" removed: the legend list is the runtime's own
    // native "map-legend" ScrollRegion now, and wheel support is
    // intentionally not kept for it either -- ScrollRegion has no wheel
    // path of its own, and Thor has no wheel to serve anyway.
  }
  if (s.page == Page::Shop && s.snapshot.world_ready && s.snapshot.shop_ready) {
    if (action == "shop_back") {
      s.shop_details = false;
      return true;
    }
    if (action == "shop_mode" && (argument == 0 || argument == 1)) {
      s.shop_filter_mode = int(argument);
      s.shop_details = false;
      return true;
    }
    if (action == "shop_clear") {
      s.shop_details = false;
      s.shop_category.clear();
      s.shop_brand.clear();
      s.shop_selected.clear();
      s.shop_spec_offset = 0;
      return true;
    }
    // "shop_filter_scroll" (the old wheel/drag step for the filter list) is
    // gone: the list is the runtime's own native "shop-filters" ScrollRegion
    // now (gen_manifest.py), so no module action is involved in scrolling it
    // at all, and Thor has no wheel to serve either. `argument` for
    // "shop_filter_pick" below is the tapped repeat template's absolute
    // index (payload "{i}"), not a 0..6 visible-window slot offset by a
    // module-tracked shop_category_offset/shop_brand_offset any more.
    if (action == "shop_filter_pick" && argument >= 0) {
      size_t index = size_t(argument);
      if (s.shop_filter_mode) {
        if (index >= s.snapshot.shop_brands.size())
          return false;
        s.shop_brand = s.snapshot.shop_brands[index].id;
      } else {
        if (index >= s.snapshot.shop_categories.size())
          return false;
        s.shop_category = s.snapshot.shop_categories[index].id;
      }
      s.shop_spec_offset = 0;
      s.shop_selected.clear();
      return true;
    }
    auto items = ShopItems(s);
    // "shop_grid" (the old wheel/drag step for the catalog grid) is gone:
    // the grid is the runtime's own native "shop-catalog" ScrollRegion now
    // (gen_manifest.py), so no module action is involved in scrolling it at
    // all, and the Thor handheld has no wheel to serve either. `argument`
    // for "shop_item" below is the tapped repeat template's absolute
    // filtered-item index (payload "{i}"), not a 0..5 visible-window slot
    // offset by a module-tracked grid_offset any more.
    if (action == "shop_item" && argument >= 0 && size_t(argument) < items.size()) {
      s.shop_selected = s.snapshot.shop_items[items[size_t(argument)]].id;
      s.shop_details = true;
      s.shop_spec_offset = 0;
      return true;
    }
    if (action == "shop_specs" && (argument == 1 || argument == -1)) {
      auto item = SelectedShop(s);
      if (!item)
        return false;
      size_t rows = std::min<size_t>(item->specs.size(), 64),
             max = rows * 104 > 728 ? rows * 104 - 728 : 0;
      s.shop_spec_offset = std::clamp(s.shop_spec_offset + (argument > 0 ? 72. : -72.), 0., double(max));
      return true;
    }
  }
  if (s.page == Page::Vehicles && s.snapshot.world_ready &&
      s.snapshot.vehicles_ready) {
    // "vehicle_list" (the old wheel/drag step for the Garage list) and
    // "vehicle_specs" (same for the specs list) are gone: both are the
    // runtime's own native "garage-list"/"garage-specs" ScrollRegions now
    // (gen_manifest.py), so no module action is involved in scrolling
    // either at all, and Thor has no wheel to serve anyway. `argument` for
    // "vehicle_pick" below is the tapped repeat template's absolute index
    // (payload "{i}"), not a 0..4 visible-window slot offset by a
    // module-tracked vehicles_offset any more.
    if (action == "vehicle_pick" && argument >= 0 &&
        size_t(argument) < s.snapshot.owned_vehicles.size()) {
      s.vehicle_selected = s.snapshot.owned_vehicles[size_t(argument)].id;
      return true;
    }
  }
  if (s.page == Page::Animals && s.snapshot.world_ready &&
      s.snapshot.animals_ready) {
    // animal_needs_offset no longer zeroed here: the needs pane is the
    // runtime's own native "animal-needs" ScrollRegion now
    // (RenderAnimalNeeds), reset to the top by its own reset_bind
    // (fs.ui.animal_identity, hashed from selected/feeding/world_identity
    // in 0100D04004176000.cpp) whenever either action below actually fires.
    if (action == "animal_species" && argument >= 0 && argument < 3) {
      s.animal_selected = argument;
      return true;
    }
    if (action == "animal_detail" && (argument == 0 || argument == 1)) {
      s.animal_feeding = argument == 1;
      return true;
    }
  }
  // "animal_scroll" removed: the needs pane is the runtime's own native
  // "animal-needs" ScrollRegion now, and wheel support is intentionally
  // not kept for it either -- ScrollRegion has no wheel path of its own,
  // and Thor has no wheel to serve anyway. animal_scroll_max is kept as a
  // parameter only to avoid changing this function's signature at every
  // other call site (same convention help_max used before its own
  // conversion).
  (void)animal_scroll_max;
  // "price_columns"/"price_stations" removed: the Prices matrix is the
  // runtime's native "fs-prices" ScrollRegion now (two-axis drag/fling), and
  // wheel support is intentionally not kept for it (see WheelAction in
  // 0100D04004176000.cpp) -- Thor has no wheel to serve anyway.
  // "finance_days"/"finance_scroll" removed: the Finance table is the
  // runtime's native "fs-finance" ScrollRegion now (two-axis drag/fling),
  // and wheel support is intentionally not kept for it -- Thor has no wheel.
  // "statistics_scroll" removed: the Statistics list is the runtime's own
  // native "statistic-rows" ScrollRegion now (RenderStatisticRow), and
  // wheel support is intentionally not kept for it either -- Thor has no
  // wheel to serve anyway.
  // "help_pick"/"help_list"/"help_scroll" removed: the topics list is a
  // native y-only "help-topics" ScrollRegion now (a repeat template of
  // RenderHelpTopicRow images, tapped by absolute index -- gen_manifest.py),
  // the same conversion the Shop catalog grid already made; the article
  // body is the native "help-article"/"help-article-static" ScrollRegions
  // (RenderHelpArticle, content_h_bind), with no module-tracked offset of
  // its own, reset to the top by its reset_bind whenever the selected
  // topic changes.
  if (s.page == Page::Help) {
    if (action == "help_topic" && argument >= 0 && size_t(argument) < count) {
      s.help_topic = argument;
      return true;
    }
  }
  // Map pan/zoom/follow/Reset are the runtime's own native pan_zoom view
  // state now (mod_input.cpp/mod_view_default.h in the Eden Duo runtime: a
  // drag, pinch or the Reset button never reach a module action at all).
  // No purchase/loan/sale/teleport or guest-input actions exist in this
  // slice.
  return false;
}
bool OnScrollAction(ViewState &s, std::string_view action, int64_t packed,
                    size_t topic_count, size_t help_max,
                    size_t animal_scroll_max) {
  if (!action.starts_with("scroll:") || s.time_scale_open)
    return false;
  const uint64_t payload = static_cast<uint64_t>(packed);
  // The payload's high word (dx) is unused: every remaining scroll target
  // below (shop_specs) is a y-only list, so only dy is ever read. See the
  // function's own doc comment (farming_simulator_view.h) for the packed
  // payload's layout.
  const double dy = static_cast<int32_t>(payload & 0xffffffffu);
  const std::string_view area = action.substr(7);
  auto move = [](double &offset, double delta, double max) {
    const double next = std::clamp(offset - delta, 0., std::max(0., max));
    if (next == offset) return false;
    offset = next;
    return true;
  };
  // "help_topics"/"help_article" removed: the Guide is the runtime's own
  // native "help-topics"/"help-article[-static]" ScrollRegions now
  // (gen_manifest.py); topic_count/help_max are no longer read here (kept as
  // parameters only to avoid changing this function's signature at every
  // other call site).
  if (s.page == Page::Shop && s.snapshot.world_ready && s.snapshot.shop_ready) {
    // "shop_filters" removed: the filter list never sends a
    // "scroll:shop_filters" module action now -- "shop-filters" is the
    // runtime's own native ScrollRegion (gen_manifest.py).
    // "shop_grid" removed: the catalog grid is the runtime's native
    // ScrollRegion now and never sends a "scroll:shop_grid" module action.
    if (area == "shop_specs" && s.shop_details) {
      auto item = SelectedShop(s);
      if (!item) return false;
      const size_t rows = std::min<size_t>(item->specs.size(), 64);
      return move(s.shop_spec_offset, dy, double(rows) * 104 - 728);
    }
  }
  // "garage_list"/"garage_specs" removed: both are the runtime's own native
  // "garage-list"/"garage-specs" ScrollRegions now (gen_manifest.py) and
  // never send either "scroll:" module action at all.
  // "animals" removed: the needs pane never sends a "scroll:animals" module
  // action now -- "animal-needs" is the runtime's own native ScrollRegion
  // (gen_manifest.py). animal_scroll_max is kept as a parameter only, same
  // convention as OnViewAction's above.
  (void)animal_scroll_max;
  // "prices"/"prices_x"/"prices_y" removed: the matrix never sends a
  // "scroll:prices*" module action now -- "fs-prices" is the runtime's own
  // native two-axis ScrollRegion (gen_manifest.py).
  // "finance"/"finance_x"/"finance_y" removed: the table never sends a
  // "scroll:finance*" module action now -- "fs-finance" is the runtime's own
  // native two-axis ScrollRegion (gen_manifest.py).
  // "stats" removed: the Statistics list never sends a "scroll:stats"
  // module action now -- "statistic-rows" is the runtime's own native
  // ScrollRegion (gen_manifest.py).
  // "map_legend" removed: the legend list never sends a "scroll:map_legend"
  // module action now -- "map-legend" is the runtime's own native
  // ScrollRegion (gen_manifest.py).
  return false;
}
uint64_t SnapshotKey(const ViewState &s) {
  KeyHasher k;
  // help_topic_offset/help_offset dropped: the topics list and article body
  // are the native "help-topics"/"help-article[-static]" ScrollRegions now
  // (RenderHelpTopicRow/RenderHelpArticle), not part of fs.ui.frame at all.
  // help_topic itself stays -- the frame still shows the current topic's
  // category/title header (View::HelpPage).
  k << int(s.page) << '|' << s.help_topic << '|' << s.snapshot.world_ready << '|'
    << int(s.snapshot.phase);
  if (s.page == Page::Help) {
    k << '|' << s.snapshot.currency_symbol;
    return k.Key();
  }
  auto &v = s.snapshot;
  if (s.page == Page::Shop) {
    k << '|' << v.world_ready << '|' << v.world_identity << '|' << v.shop_ready
      << '|' << v.currency_symbol;
    if (v.world_ready && v.shop_ready) {
      // No longer depends on either filter-list scroll offset, or walks
      // the visible window of categories/brands: the filter list (whichever
      // of the two shop_filter_mode selects) is the runtime's own native
      // "shop-filters" ScrollRegion now (RenderShopFilterRow), with its own
      // content hash/revision gating its images' cache-busting key suffix
      // (0100D04004176000.cpp) -- independent of this page's own
      // fs.ui.frame key.
      k << '|' << s.shop_filter_mode << '|' << s.shop_category << '|'
        << s.shop_brand << '|' << s.shop_selected << '|' << s.shop_spec_offset
        << '|' << s.shop_details;
      const auto &brands = v.shop_brands;
      auto items = ShopItems(s);
      // The catalog grid's own cards are no longer part of this frame image
      // (each is its own module image via RenderShopRow/ImageFor), so the
      // frame's key only needs the filtered count (for the empty-state
      // text) -- not every visible card's id/name/image/price any more.
      k << '|' << items.size();
      if (auto item = SelectedShop(s)) {
        k << '|' << item->id << '|' << item->name << '|' << item->image << '|'
          << FloorValue(item->price);
        for (const auto &b : brands)
          if (b.id == item->brand_id)
            k << '|' << b.image;
        for (const auto &spec : item->specs)
          k << '|' << spec.label << '|' << spec.value;
      }
    }
    return k.Key();
  }
  if (s.page == Page::Vehicles) {
    // No longer depends on either scroll offset or walks every owned
    // vehicle/spec: the Garage list and the selected vehicle's specs are
    // the runtime's own native "garage-list"/"garage-specs" ScrollRegions
    // now (RenderGarageRow/RenderGarageSpecRow), each with its own content
    // hash/revision gating their images' cache-busting key suffix
    // (0100D04004176000.cpp) -- independent of this page's own fs.ui.frame
    // key. Only the selected vehicle's own detail header (name/image/
    // leased/age/usage/maintenance/sale value) stays baked here.
    k << '|' << v.world_ready << '|' << v.world_identity << '|'
      << v.vehicles_ready << '|' << v.currency_symbol;
    if (v.world_ready && v.vehicles_ready) {
      k << '|' << s.vehicle_selected << '|' << v.owned_vehicles.size();
      if (auto item = SelectedVehicle(s)) {
        k << '|' << item->id << '|' << item->name << '|' << item->image << '|'
          << item->leased << '|' << Value(item->age_days) << '|'
          << Value(item->operating_hours, 1) << '|'
          << Value(item->maintenance_cost, 2) << '|'
          << FloorValue(item->sale_value) << '|'
          << item->operating_time_text.value_or("");
      }
      // Garage action row + call feedback (UX-SPEC B): can_enter/can_reset pick the row's
      // served image (ImageFor's "garage_action_row/<kind>"); game_call drives the toast
      // line baked directly into this frame (View::Vehicles).
      if (auto item = SelectedVehicle(s))
        k << '|' << item->can_enter << '|' << item->can_reset;
      k << '|' << v.game_call.kind << '|' << v.game_call.busy << '|' << v.game_call.serial
        << '|' << v.game_call.message;
    }
    return k.Key();
  }
  if (s.page == Page::Animals) {
    // No longer depends on the scroll offset, or any animal's needs/
    // help_text/help_image: the needs pane (plus its trailing help text/
    // illustration) is the runtime's own native "animal-needs" ScrollRegion
    // now (RenderAnimalNeeds), with its own content hash/revision gating
    // its image's cache-busting key suffix (0100D04004176000.cpp) --
    // independent of this page's own fs.ui.frame key. a.outputs stays: the
    // Overview tab's per-output rows are still baked directly into this
    // frame (few enough rows to always fit without scrolling).
    k << '|' << v.world_ready << '|' << v.world_identity << '|'
      << v.animals_ready << '|' << s.animal_selected << '|' << s.animal_feeding;
    if (v.world_ready && v.animals_ready)
      for (const auto &a : v.animals) {
        k << '|' << a.id << '|' << a.name << '|' << a.image << '|'
          << Value(a.count) << '|' << Value(a.productivity) << '|'
          << Value(a.cleanliness) << '|' << FloorValue(a.next_animal_minutes)
          << '|' << Value(a.reproduction_rate_per_day, 4) << '|'
          << FloorValue(a.reproduction_interval_minutes);
        for (const auto &n : a.outputs)
          k << '|' << n.label << '|' << n.unit << '|' << Value(n.level, 3)
            << '|' << Value(n.capacity, 3) << '|' << Value(n.percent, 1);
      }
    return k.Key();
  }
  if (s.page == Page::Prices) {
    // The crop header row, station column and price grid are no longer part
    // of this frame image (they are the module images RenderPriceHeader/
    // RenderPriceStations/RenderPriceRow, each requested and re-rendered by
    // the runtime on its own "fs-prices" ScrollRegion cadence -- see
    // price_grid_hash/price_grid_revision in 0100D04004176000.cpp), so this
    // key no longer depends on the scroll offsets or walks any crop/station
    // data: the frame itself is now just the static title chrome.
    k << '|' << v.world_ready << '|' << v.world_identity << '|'
      << v.prices_ready << '|' << v.currency_symbol;
    return k.Key();
  }
  if (s.page == Page::Finances || s.page == Page::Statistics) {
    k << '|' << v.world_ready << '|' << v.world_identity;
    if (s.page == Page::Finances) {
      // The day header, row-label column, the grid rows and the totals
      // row's day amounts are no longer part of this frame image: they are
      // the module images RenderFinanceHeader/RenderFinanceLabels/
      // RenderFinanceRow/RenderFinanceTotals, each requested and re-rendered
      // by the runtime on its own "fs-finance" ScrollRegion cadence (see
      // finance_grid_hash/finance_revision in 0100D04004176000.cpp). This
      // key no longer depends on the scroll offsets, finance_rows,
      // finance_day_labels or finance_totals -- the frame itself is now
      // just the three summary boxes and the static title/total chrome.
      k << '|' << v.finances_ready << '|' << v.currency_symbol;
      if (v.world_ready && v.finances_ready)
        k << '|' << FloorValue(v.money) << '|' << FloorValue(v.loan) << '|'
          << FloorValue(v.finance_saldo);
    } else {
      // No longer depends on the scroll offset or the row content itself
      // (label/unit/digits/session/total): the list is the runtime's own
      // native "statistic-rows" ScrollRegion now (RenderStatisticRow), bumped
      // by the runtime on its own cadence as each row is actually requested
      // -- the frame itself is now just the static header row above it.
      k << '|' << v.statistics_ready;
    }
    return k.Key();
  }
  if (s.page != Page::Operations)
    return k.Key();
  k << '|' << int(v.phase) << '|' << v.world_ready << '|' << v.vehicle_ready
    << '|' << v.map_id << '|' << v.world_identity;
  auto add = [&](const std::optional<double> &a, double quantum) {
    k << '|';
    if (a && std::isfinite(*a))
      k << std::llround(*a / quantum);
    else
      k << '-';
  };
  k << '|' << FloorValue(v.money);
  // player_x/z/heading and the map markers below no longer affect the frame
  // image: the map's own pixels (including the player arrow and markers) are
  // rendered into a separate "fs.ui.map_frame" image at their own pace
  // (RenderMapBase, MapSnapshotKey below), decoupled from the chrome frame.
  add(v.speed_kmh, .1);
  k << '|';
  if (v.fuel_l && std::isfinite(*v.fuel_l))
    k << std::floor(*v.fuel_l);
  else
    k << '-';
  add(v.fuel_capacity_l, 1);
  add(v.fill_l, 1);
  add(v.fill_capacity_l, 1);
  k << '|' << v.time_minutes.value_or(-1) << '|' << v.vehicle_name << '|'
    << v.vehicle_image << '|' << v.fill_name << '|' << v.fill_icon << '|'
    << v.vehicle_config;
  k << '|' << v.time_scale.value_or(-1) << '|' << v.time_scale_pending
    << '|' << v.time_scale_change_allowed << '|' << s.time_scale_open;
  k << '|' << v.currency_symbol << '|' << s.map_fullscreen;
  k << '|' << v.map_layer_error;
  // map_legend_offset dropped: the legend list is the runtime's own native
  // "map-legend" ScrollRegion now (RenderMapLegendRow), no module-tracked
  // scroll position left to hash.
  k << '|' << s.map_layer << '|' << s.map_legend;
  // Map selection card (UX-SPEC C): the card header (name/type/art) is baked directly into
  // this frame (Operations()), like every other right-pane replacement here, so a new
  // selection -- or that marker's own data changing under it -- must change this key. The
  // action row's own button faces are a separate, smaller, cacheable image
  // (RenderMapSelectionButtons) and do not need to be hashed here.
  k << '|' << s.map_marker_selected;
  if (s.map_marker_selected >= 0 &&
      size_t(s.map_marker_selected) < v.map_markers.size()) {
    const auto &m = v.map_markers[size_t(s.map_marker_selected)];
    k << '|' << m.object_id << '|' << m.label << '|' << m.station_key << '|' << m.kind << '|' << m.can_visit
      << '|' << m.enabled;
    // G3 critique Finding 5 (place card facility info): a selling point's card now shows its
    // own top prices (matched by station name == this marker's resolved label). Bounded to
    // the one matched station's own quotes -- not the whole price_stations table -- so this
    // stays cheap the same way the rest of this key avoids re-hashing big lists every tick.
    // The card's price lines come from the 1 Hz management snapshot (matched by localized
    // name in Operations()); its revision makes a card opened before prices loaded, or after
    // they change, re-render without hashing the whole price table here.
    if (m.can_visit)
      k << '|' << v.management_revision;
  }
  // FILTERS popup chrome (UX-SPEC §A): the open flag and each row's ON/OFF
  // pill both show up in this frame image (pane/card content), independent
  // of the map's own pixels (MapSnapshotKey below handles those).
  k << '|' << s.map_filters_open;
  for (bool on : s.map_filters) k << on;
  return k.Key();
}
// Change-detection key for the map's own image (RenderMapBase): everything
// that moves the map's pixels but none of the rest of the chrome frame, so
// the two images mint new revisions independently. Pan/zoom/follow are not
// here: they are the runtime's own native pan_zoom view state now, never the
// module's -- a drag or pinch never touches this key or re-renders anything.
uint64_t MapSnapshotKey(const ViewState &s) {
  KeyHasher k;
  const auto &v = s.snapshot;
  k << v.world_ready << '|' << v.map_id << '|' << v.world_identity
    << '|' << s.map_layer << '|' << (v.map_layers ? v.map_layers->revision : 0);
  // FILTERS (UX-SPEC §A): a toggle must re-render this map image (markers
  // hide/show) but must never be touched by movement -- it only needs to
  // gate this key, not per-marker state below.
  for (bool on : s.map_filters) k << on;
  // G3 critique Finding 6: the tapped marker's own lime selection ring is drawn into this
  // raster (RenderMapBase), so a new/cleared selection must mint a new map image too.
  k << '|' << s.map_marker_selected;
  auto add = [&](const std::optional<double> &a, double quantum) {
    k << '|';
    if (a && std::isfinite(*a))
      k << std::llround(*a / quantum);
    else
      k << '-';
  };
  // Position/heading no longer bake into this raster at all when heading is
  // known (the common case): the player arrow is a separate, unthrottled
  // overlay now (see RenderMapBase/RenderMapArrow), so walking/driving no
  // longer retriggers this ~5Hz-gated rebuild. The rare heading-unknown
  // fallback (a plain recentre dot) still bakes position here so it tracks.
  if (v.player_heading) {
    k << "|-|-";
  } else {
    add(v.player_x, 1);
    add(v.player_z, 1);
  }
  k << '|' << v.player_heading.has_value();
  k << '|' << v.map_markers.size();
  for (const auto &marker : v.map_markers) {
    k << marker.kind << '|' << marker.image << '|' << marker.sx << '|' << marker.sy
      << '|' << marker.w << '|' << marker.h << '|' << marker.draw_w << '|' << marker.draw_h << '|' << marker.color
      << '|' << marker.background_image << '|' << marker.background_sx << '|' << marker.background_sy
      << '|' << marker.background_w << '|' << marker.background_h << '|' << marker.background_color << '|' << marker.enabled;
    add(marker.x, 1);
    add(marker.z, 1);
    add(marker.rotation, .03);
  }
  return k.Key();
}
} // namespace farming_simulator
