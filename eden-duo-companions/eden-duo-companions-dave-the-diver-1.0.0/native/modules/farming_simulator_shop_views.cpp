// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_view.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace farming_simulator {
namespace {
constexpr uint32_t Black = 0x000000ff, Background = 0x2f2f2fff,
                   Row = 0x262626ff, White = 0xffffffff, Muted = 0xc3c3c3ff,
                   Selected = 0x7fc031ff, Green = 0x8ed350ff, Rule = 0x707070ff;
std::string Number(const std::optional<double> &v, int digits = 0) {
  if (!v || !std::isfinite(*v))
    return "-";
  return FormatGrouped(*v, digits);
}
std::string Money(const std::optional<double> &v, const std::string &symbol) {
  auto amount = v;
  if (amount && std::isfinite(*amount))
    amount = std::floor(*amount);
  auto value = Number(amount);
  if (v && std::isfinite(*v) && !symbol.empty())
    value += " " + symbol;
  return value;
}
std::vector<std::string> FitLines(const Font &font, std::string_view text,
                                  int size, int width, bool bold = false) {
  auto lines = font.Wrap(text, size, width, bold);
  bool omitted = lines.size() > 2;
  if (omitted)
    lines.resize(2);
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!(omitted && i + 1 == lines.size()) &&
        font.Measure(lines[i], size, bold) <= width)
      continue;
    while (!lines[i].empty() &&
           font.Measure(lines[i] + "...", size, bold) > width) {
      size_t p = lines[i].size() - 1;
      while (p && (static_cast<unsigned char>(lines[i][p]) & 0xc0) == 0x80)
        --p;
      lines[i].resize(p);
    }
    lines[i] += "...";
  }
  return lines;
}
} // namespace
std::vector<size_t> ShopItems(const ViewState &s) {
  std::vector<size_t> result;
  if (!s.snapshot.world_ready || !s.snapshot.shop_ready)
    return result;
  for (size_t i = 0; i < std::min<size_t>(s.snapshot.shop_items.size(), 1024);
       ++i) {
    const auto &item = s.snapshot.shop_items[i];
    if ((s.shop_category.empty() || item.category_id == s.shop_category) &&
        (s.shop_brand.empty() || item.brand_id == s.shop_brand))
      result.push_back(i);
  }
  return result;
}
const ShopItem *SelectedShop(const ViewState &s) {
  auto items = ShopItems(s);
  if (items.empty())
    return nullptr;
  for (auto i : items)
    if (s.snapshot.shop_items[i].id == s.shop_selected)
      return &s.snapshot.shop_items[i];
  // No selection yet (e.g. right after a filter change): fall back to the
  // first item. The runtime's own native ScrollRegion owns the catalog's
  // scroll position now, so the module has no offset to fall back from --
  // this matches "scroll:" areas' reset_bind snapping the list to the top
  // on the same filter change.
  return &s.snapshot.shop_items[items[0]];
}
const VehicleItem *SelectedVehicle(const ViewState &s) {
  if (!s.snapshot.world_ready || !s.snapshot.vehicles_ready ||
      s.snapshot.owned_vehicles.empty())
    return nullptr;
  for (const auto &vehicle : s.snapshot.owned_vehicles)
    if (vehicle.id == s.vehicle_selected)
      return &vehicle;
  // No selection yet: fall back to the first vehicle, same reasoning as
  // SelectedShop's own fallback above -- the runtime's own native
  // "garage-list" ScrollRegion owns the list's scroll position now, so the
  // module has no offset left to fall back from.
  return &s.snapshot.owned_vehicles[0];
}
bool View::AttributeIcon(Image &out, std::string_view label, int x, int y,
                         int size) {
  // Source guiProfiles shopListAttributeIcon* roles, not inferred page
  // symbols. Each entry's loc-key (checked both as the raw key and its
  // localized text) and its CatalogSpecs English fallback(s) -- published
  // where native localization provides only a value template (fuel/speed),
  // or as its explicit English fallback -- share one atlas x-offset.
  struct Entry {
    const char *loc_key;
    std::array<const char *, 2> literals;
    int x;
  };
  static const std::array<Entry, 7> entries{{
      {"shop_maintenance", {"Maintenance", nullptr}, 8},
      {"shop_maxPower", {"Power", nullptr}, 52},
      {"shop_neededPower", {"Required power", nullptr}, 96},
      {"shop_workingWidth", {"Working width", nullptr}, 228},
      {"shop_capacity", {"Capacity", nullptr}, 272},
      {nullptr, {"Fuel", "Fuel capacity"}, 404},
      {nullptr, {"Maximum speed", nullptr}, 492},
  }};
  int source = -1;
  for (auto &e : entries) {
    bool match = e.loc_key && (label == e.loc_key || label == Label(e.loc_key));
    if (!match)
      for (auto *literal : e.literals)
        if (literal && label == literal) {
          match = true;
          break;
        }
    if (match)
      source = e.x;
  }
  if (source >= 0) {
    if (auto atlas = assets_.Texture("dataS2/menu/hud/ui_elements_1080p.dds")) {
      Blit(out, *atlas, x, y, size, size, source, 945, 36, 36, Selected);
      return true;
    }
  }
  if (label == "Working speed")
    if (auto art = assets_.Texture("dataS/menu/shopAttribute_workspeed.dds")) {
      Blit(out, *art, x, y, size, size, 14, 14, 36, 36, Selected);
      return true;
    }
  return false;
}
void View::Shop(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.shop_ready) {
    Unavailable(out, s.shop_loading, 28, 140, 32);
    return;
  }
  auto items = ShopItems(state);
  auto selected = SelectedShop(state);
  if (state.shop_details && selected) {
    Button(out, Label("button_back"), 20, 96, 192, 88);
    auto title = FitLines(font_, Label(selected->name), 38, 960, true);
    for (size_t i = 0; i < title.size(); ++i)
      Text(out, title[i], 244, 104 + i * 42, 38, White, true);
    Art(out, selected->image, 20, 224, 430, 360, true);
    auto brand =
        std::find_if(s.shop_brands.begin(), s.shop_brands.end(),
                     [&](const auto &b) { return b.id == selected->brand_id; });
    if (brand != s.shop_brands.end())
      Art(out, brand->image, 100, 624, 270, 120, true);
    auto price = Money(selected->price, s.currency_symbol);
    Text(out, price, 20 + (430 - font_.Measure(price, 38, true)) / 2, 792, 38,
         selected->price ? Green : Muted, true);
    constexpr int H = 728, R = 104;
    Image specs;
    specs.w = 740;
    specs.h = H;
    specs.rgba.assign(size_t(740) * H * 4, 0);
    Fill(specs, 0, 0, 740, H, Background);
    size_t rows = std::min<size_t>(selected->specs.size(), 64),
           max = rows * R > H ? rows * R - H : 0;
    double offset = std::clamp(state.shop_spec_offset, 0., double(max));
    for (size_t i = 0; i < rows; ++i) {
      int y = int(std::lround(i * R - offset));
      if (y + R <= 0 || y >= H)
        continue;
      Fill(specs, 0, y, 740, R, i % 2 ? Row : Background);
      AttributeIcon(specs, selected->specs[i].label, 16, y + (R - 40) / 2, 40);
      auto label = font_.Wrap(Label(selected->specs[i].label), 32, 292, true),
           value = font_.Wrap(selected->specs[i].value, 32, 344);
      RowLines(specs, label, 72, y, R, 32, White, true);
      RowLines(specs, value, 724, y, R, 32, White, false, 2);
    }
    Blit(out, specs, 480, 224, 740, H);
    if (max) {
      Fill(out, 1224, 224, 8, H, Black);
      int thumb = std::max(32, H * H / int(rows * R));
      Fill(out, 1224, 224 + int(std::lround((H - thumb) * offset / max)), 8, thumb, Selected);
    }
    return;
  }
  auto category = Label("shop_category"), brand = Label("shop_brand");
  for (int i = 0; i < 2; ++i) {
    int x = 20 + i * 176;
    Fill(out, x, 96, 168, 88, state.shop_filter_mode == i ? Selected : Black);
    auto label = i ? brand : category;
    RowText(out, label, x + (168 - font_.Measure(label, 32, true)) / 2, 96, 88, 32, White, true);
  }
  // The filter list itself is no longer baked here: it is the runtime's
  // native "shop-filters" ScrollRegion now (RenderShopFilterRow below).
  // Only the "All" button (never scrolls) stays in this frame image.
  Button(out, "All", 20, 880, 344, 80);
  // The catalog grid itself is no longer baked here: it is the runtime's
  // native "shop-catalog" ScrollRegion (gen_manifest.py), each card served
  // as its own module image by RenderShopRow/ImageFor below. Only the empty
  // state and the frozen vertical rule stay in this frame image.
  if (items.empty())
    Text(out, Label("shop_noVehicles"), 400, 280, 32, Muted);
  Fill(out, 374, 96, 1, 864, Rule);
}
bool View::RenderShopRow(const ViewState &state, size_t master_index,
                         bool active, Image &out) {
  const auto &s = state.snapshot;
  if (master_index >= s.shop_items.size())
    return false;
  const auto &item = s.shop_items[master_index];
  out.w = 412;
  out.h = 372;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, 412, 372, Row);
  Art(out, item.image, 16, 12, 380, 200, true);
  Fill(out, 0, 220, 412, 88, active ? Selected : Background);
  auto lines = FitLines(font_, Label(item.name), 32, 380, true);
  RowLines(out, lines, 206, 220, 88, 32, White, true, 1);
  auto price = Money(item.price, s.currency_symbol);
  Text(out, price, (412 - font_.Measure(price, 32, true)) / 2, 326, 32,
       item.price ? Green : Muted, true);
  return true;
}
bool View::RenderShopFilterRow(const ViewState &state, size_t index, bool active,
                               Image &out) {
  const auto &s = state.snapshot;
  std::string name, image;
  if (state.shop_filter_mode) {
    if (index >= s.shop_brands.size())
      return false;
    auto &b = s.shop_brands[index];
    name = b.name;
    image = b.image;
  } else {
    if (index >= s.shop_categories.size())
      return false;
    auto &c = s.shop_categories[index];
    name = c.name;
    image = c.image;
  }
  out.w = ShopFilterRowWidth;
  out.h = ShopFilterRowPitch;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, 344, 100, active ? Selected : Row);
  int art_size = state.shop_filter_mode ? 72 : 48;
  int label_x = state.shop_filter_mode ? 106 : 82;
  Art(out, image, 6, (100 - art_size) / 2, art_size, art_size, true);
  auto lines = FitLines(font_, Label(name), 32,
                        state.shop_filter_mode ? 250 : 278, true);
  RowLines(out, lines, label_x - 20, 0, 100, 32, White, true);
  return true;
}
void View::Vehicles(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.vehicles_ready) {
    Unavailable(out, s.vehicles_loading, 28, 140, 32);
    return;
  }
  // The Garage list itself is no longer baked here: it is the runtime's
  // native "garage-list" ScrollRegion now (RenderGarageRow below). Only the
  // static divider rule (never scrolls) stays in this frame image.
  Fill(out, 472, 104, 1, 848, Rule);
  auto selected = SelectedVehicle(state);
  if (!selected) {
    Text(out, Label("shop_noVehicles"), 500, 140, 32, Muted);
    return;
  }
  const auto &v = *selected;
  auto title = FitLines(font_, Label(v.name), 38, 716, true);
  for (size_t i = 0; i < title.size(); ++i)
    Text(out, title[i], 496, 112 + i * 42, 38, White, true);
  Art(out, v.image, 496, 204, 716, 160, true);
  Text(out, Label(v.leased ? "shop_youLease" : "shop_youOwn"), 496, 378, 32,
       White, true);
  auto row = [&](std::string label, std::string value, int y) {
    Fill(out, 484, y, 736, 64, Row);
    auto lines = font_.Wrap(label, 32, 320, true);
    RowLines(out, lines, 496, y, 64, 32, White, true);
    RowText(out, value, 1204 - font_.Measure(value, 32), y, 64, 32, White);
  };
  auto age = Label("shop_age");
  auto marker = age.find("%s");
  if (marker != age.npos)
    age.replace(marker, 2, Number(v.age_days));
  else
    age = Number(v.age_days);
  row("Age", age, 424);
  row(Label("shop_usage"),
      v.operating_time_text.value_or(Number(v.operating_hours, 1) +
                                     (v.operating_hours ? " h" : "")),
      496);
  row(Label("shop_maintenance"),
      Number(v.maintenance_cost, 2) +
          (v.maintenance_cost ? " " + s.currency_symbol + "/day" : ""),
      568);
  row(Label("ui_sellValue"), Money(v.sale_value, s.currency_symbol), 640);
  // The specs list itself is no longer baked here either: it is the
  // runtime's native "garage-specs" ScrollRegion now (RenderGarageSpecRow
  // below).
  // Call feedback (UX-SPEC B.4): a small, non-blocking line anchored just above the action
  // row, sourced straight from fs.call.* (farming_simulator_game_calls.h's OutcomeText) --
  // busy ("Entering vehicle...") while in flight, the finished outcome once it lands. Replaces
  // on the next call rather than stacking; there is no separate auto-dismiss timer (a
  // deliberate simplification -- see the module's own game-call feedback comment), so it
  // reads until the next Enter/Reset tap from anywhere (Garage or the map's own selection
  // card, both post through the same fs.call.* state).
  const auto &call = s.game_call;
  if ((call.kind == "enter" || call.kind == "reset") && (call.busy || !call.message.empty())) {
    auto text = call.busy ? std::string("Working...") : call.message;
    Fill(out, 484, 804, 756, 48, Background);
    RowText(out, text, 484 + (756 - font_.Measure(text, 32, true)) / 2, 804, 48, 32,
            call.busy ? Muted : (call.status == 0 && call.code == 1 ? Green : Muted), true);
  }
}
bool View::RenderGarageRow(const ViewState &state, size_t index, bool active,
                          Image &out) {
  const auto &vehicles = state.snapshot.owned_vehicles;
  if (index >= std::min(vehicles.size(), GarageRowCap))
    return false;
  const auto &v = vehicles[index];
  out.w = GarageRowWidth;
  out.h = GarageRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, GarageRowWidth, GarageRowHeight - 6, active ? Selected : Row);
  Art(out, v.image, 8, (GarageRowHeight - 128) / 2, 128, 128, true);
  auto lines = FitLines(font_, Label(v.name), 32, 286, true);
  int block = int(lines.size()) * 36 + 50;
  int top = (GarageRowHeight - block) / 2;
  // Black on the lime selected row (white on lime is 2.19:1; black is 8:1, as the open
  // Filters button already uses).
  const uint32_t ink = active ? Black : White;
  RowLines(out, lines, 146, top, int(lines.size()) * 36, 32, ink, true);
  RowText(out, Label(v.leased ? "shop_youLease" : "shop_youOwn"), 146,
          top + int(lines.size()) * 36 + 14, 36, 32, ink);
  return true;
}
// G3 critique, Finding 5: native Enter/Reset/Visit/Tag (guiProfiles.xml's shared pcButton
// parent, confirmed by reading the installed build's own GUI definitions -- see Icon()'s
// icon_enter/icon_reset/icon_visit/icon_tag comment) are NOT a lime-filled "primary" plus an
// outlined "secondary": all four are the SAME charcoal-filled, lime-icon, white-text button,
// differentiated only by icon+label. The companion's prior Primary/Secondary pair (lime fill
// for Enter/Visit) collided with this companion's own existing "lime fill = selected/active"
// meaning (selected footer tab, Filters open, ON pills) -- Enter read as "already on". Replaces
// PrimaryFace/SecondaryFace with the one native face both Garage and the map selection card use.
void View::NativeActionButton(Image &out, std::string_view label, std::string_view icon, int x,
                              int y, int w, int h, int size) {
  Fill(out, x, y, w, h, 0x2a2b2cff); // pcButton's own imageColor (preset_colorBlack2)
  // Visible 2px border (G3 Finding 5: the old SecondaryFace border was black-on-black,
  // effectively invisible, under DESIGN-TASTE "make interactive control borders clear").
  Fill(out, x, y, w, 2, Rule);
  Fill(out, x, y + h - 2, w, 2, Rule);
  Fill(out, x, y, 2, h, Rule);
  Fill(out, x + w - 2, y, 2, h, Rule);
  constexpr int IconSize = 36, Gap = 14;
  int tw = font_.Measure(label, size, true);
  int left = x + (w - tw - IconSize - Gap) / 2;
  Icon(out, icon, left, y + (h - IconSize) / 2, IconSize, Green); // native iconColor (preset_colorGreen)
  RowText(out, label, left + IconSize + Gap, y, h, size, White, true);
}
bool View::RenderGarageActionRow(int kind, Image &out) {
  if (kind < 1 || kind > 3)
    return false;
  // G3 critique Finding 2 (LEGIBILITY.md primary-control floor) + Finding 5 (right margin):
  // BtnH grown 80->112; W shrunk from a screen-flush 756 to 736 so the row keeps the same
  // 20px right margin every other Garage detail row already uses (484..1220, not 484..1240),
  // fixing the Reset button's clipped right border.
  constexpr int W = 736, H = 128, BtnY = 16, BtnH = 112;
  out.w = W;
  out.h = H;
  out.rgba.assign(size_t(W) * H * 4, 0);
  const auto enter = Label("action_enter");
  const auto reset = Label("button_reset");
  Fill(out, 0, 0, W, 2, 0x707070ff); // top rule, matching the detail pane's divider convention
  constexpr int Gap = 16, Half = (W - Gap) / 2;
  if (kind == 1) {
    NativeActionButton(out, enter, "icon_enter", 0, BtnY, Half, BtnH, 32);
    NativeActionButton(out, reset, "icon_reset", Half + Gap, BtnY, Half, BtnH, 32);
  } else if (kind == 2) {
    NativeActionButton(out, enter, "icon_enter", 0, BtnY, W, BtnH, 32);
  } else {
    NativeActionButton(out, reset, "icon_reset", 0, BtnY, W, BtnH, 32);
  }
  return true;
}
bool View::RenderMapSelectionButtons(int kind, bool place, bool tagged, Image &out) {
  if (kind < 1 || kind > 3)
    return false;
  // G3 critique Finding 2: stacked full-width 112px rows instead of a 148x80 side-by-side
  // split (below the 112px primary-control floor either way it's sliced). One constant
  // 304x236 canvas for every kind (112 row + 12 gap + 112 row) -- a single-action kind draws
  // into the top/primary slot only, same "re-flow to the primary slot" convention
  // RenderGarageActionRow's kind 2/3 already uses, and leaves the rest transparent.
  constexpr int W = 304, RowH = 112, Gap = 12, H = RowH * 2 + Gap;
  out.w = W;
  out.h = H;
  out.rgba.assign(size_t(W) * H * 4, 0);
  const auto left_label = place ? Label("action_visit") : Label("action_enter");
  const auto left_icon = place ? "icon_visit" : "icon_enter";
  const auto right_label = place ? Label(tagged ? "action_untag" : "action_tag") : Label("button_reset");
  const auto right_icon = place ? "icon_tag" : "icon_reset";
  if (kind == 1) {
    NativeActionButton(out, left_label, left_icon, 0, 0, W, RowH, 32);
    NativeActionButton(out, right_label, right_icon, 0, RowH + Gap, W, RowH, 32);
  } else if (kind == 2) {
    NativeActionButton(out, left_label, left_icon, 0, 0, W, RowH, 32);
  } else {
    NativeActionButton(out, right_label, right_icon, 0, 0, W, RowH, 32);
  }
  return true;
}
bool View::RenderGarageSpecRow(const ViewState &state, size_t spec_index,
                               Image &out) {
  auto vehicle = SelectedVehicle(state);
  if (!vehicle || spec_index >= std::min(vehicle->specs.size(), GarageSpecRowCap))
    return false;
  out.w = GarageSpecRowWidth;
  out.h = GarageSpecRowHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  Fill(out, 0, 0, GarageSpecRowWidth, GarageSpecRowHeight, Row);
  AttributeIcon(out, vehicle->specs[spec_index].label, 12, 20, 40);
  auto labels = font_.Wrap(Label(vehicle->specs[spec_index].label), 32, 284, true),
       values = font_.Wrap(vehicle->specs[spec_index].value, 32, 336);
  RowLines(out, labels, 68, 0, GarageSpecRowHeight, 32, White, true);
  RowLines(out, values, 720, 0, GarageSpecRowHeight, 32, White, false, 2);
  return true;
}
} // namespace farming_simulator
