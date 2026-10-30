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
                   Green = 0x7fc031ff, Header = 0x474747ff;
constexpr int BodyY = 248, BodyHeight = 704, NeedHeight = 100;
bool Valid(const std::optional<double> &v) {
  return v && std::isfinite(*v) && *v >= 0;
}
std::string Amount(const std::optional<double> &v) {
  if (!Valid(v))
    return "-";
  return FormatGrouped(*v, 0);
}
std::string Duration(const std::optional<double> &v) {
  if (!Valid(v) || *v > 6000000)
    return "--:--h";
  auto n = static_cast<int64_t>(std::floor(*v));
  std::ostringstream o;
  o << std::setfill('0') << std::setw(2) << n / 60 << ":" << std::setw(2)
    << n % 60 << "h";
  return o.str();
}
const AnimalGroup *Find(const Snapshot &s, size_t index) {
  static const std::array<std::string_view, 3> ids{"sheep", "pig", "cow"};
  auto i = std::find_if(s.animals.begin(), s.animals.end(), [&](const auto &a) {
    return a.id == ids[std::min(index, size_t(2))];
  });
  return i == s.animals.end() ? nullptr : &*i;
}
} // namespace
void View::Animals(Image &out, const ViewState &state) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.animals_ready) {
    Fill(out, 0, 0, 1240, 80, Black);
    auto title = Label("ui_ingameMenuAnimals");
    Text(out, title, (1240 - font_.Measure(title, 38, true)) / 2, 22, 38, White,
         true);
    // Animals is excluded from the time control everywhere (see
    // OnViewAction): its ready state has no top-bar band to anchor the badge
    // in (the species selector fills that row), so this unavailable state
    // stays consistent with it rather than showing a badge only here.
    Unavailable(out, s.animals_loading, 28, 140, 32);
    return;
  }
  static const std::array<std::string_view, 3> ids{"sheep", "pig", "cow"};
  for (size_t i = 0; i < 3; ++i) {
    int x = 20 + int(i) * 404;
    Fill(out, x, 16, 392, 104, state.animal_selected == i ? Green : Black);
    auto a = Find(s, i);
    auto name = Label("ui_statisticView_" + std::string(ids[i]));
    int tw = font_.Measure(name, 32, true), left = x + (392 - 72 - 20 - tw) / 2;
    if (a && !a->image.empty())
      Art(out, a->image, left, 32, 72, 72, true);
    RowText(out, name, left + 92, 16, 104, 32, White, true);
  }
  for (int i = 0; i < 2; ++i) {
    int x = 20 + i * 606;
    Button(out, i ? "Feeding" : "Overview", x, 136, 594, 88);
    if (state.animal_feeding == bool(i))
      Fill(out, x, 216, 594, 8, Green);
  }
  const auto *a = Find(s, state.animal_selected);
  if (!a) {
    Text(out, "Data unavailable", 36, 280, 32, Muted);
    return;
  }
  auto quantity = [&](const AnimalNeed &n) {
    auto v = Amount(n.level);
    if (Valid(n.level) && !n.unit.empty())
      v += " " + Label(n.unit);
    if (Valid(n.percent))
      v += " (" + Amount(n.percent) + "%)";
    return v;
  };
  if (!state.animal_feeding) {
    struct Metric {
      std::string label, value;
      std::optional<double> percent;
    };
    std::vector<Metric> rows;
    auto owned = state.animal_selected == 0   ? "statistic_sheepOwned"
                 : state.animal_selected == 1 ? "statistic_pigOwned"
                                              : "statistic_cowOwned";
    rows.push_back({Label(owned), Amount(a->count), {}});
    rows.push_back(
        {Label("statistic_productivity"),
         Valid(a->productivity) ? Amount(a->productivity) + "%" : "-",
         {}});
    rows.push_back({Label("statistic_timeTillNextAnimal"),
                    Duration(a->next_animal_minutes),
                    {}});
    rows.push_back({Label("statistic_reproductionRate"),
                    Duration(a->reproduction_interval_minutes),
                    {}});
    for (auto &n : a->outputs)
      if (rows.size() < 7)
        rows.push_back({Label(n.label), quantity(n), {}});
    rows.push_back({Label("statistic_cleanliness"),
                    Valid(a->cleanliness) ? Amount(a->cleanliness) + "%" : "-",
                    a->cleanliness});
    for (size_t i = 0; i < rows.size(); ++i) {
      int y = BodyY + int(i) * 76;
      Fill(out, 20, y, 1200, 72, i % 2 ? Row : Background);
      int text_y = y, text_height = 72, gauge_y = y;
      if (Valid(rows[i].percent)) {
        auto a = font_.VerticalBounds(rows[i].label, 32);
        auto b = font_.VerticalBounds(rows[i].value, 38, true);
        text_height = std::max(a.second - a.first, b.second - b.first);
        text_y = y + (72 - text_height - 10 - 6) / 2;
        gauge_y = text_y + text_height + 10;
      }
      RowText(out, rows[i].label, 44, text_y, text_height, 32, White);
      RowText(out, rows[i].value, 1196 - font_.Measure(rows[i].value, 38, true),
              text_y, text_height, 38, Muted, true);
      if (Valid(rows[i].percent)) {
        Fill(out, 44, gauge_y, 1152, 6, Header);
        double ratio = std::clamp(*rows[i].percent / 100., 0., 1.);
        Fill(out, 44, gauge_y, int(1152 * ratio), 6,
             ratio < .15  ? 0xe91919ff
             : ratio < .5 ? 0xf7921eff
                          : Green);
      }
    }
    return;
  }
  // The needs list and its trailing help text/illustration are no longer
  // baked here: together they are the runtime's own native "animal-needs"
  // ScrollRegion now (RenderAnimalNeeds below), the same mixed row-list-
  // plus-free-form-content shape as the Guide article's own conversion
  // (RenderHelpArticle) -- a repeat template does not fit a pane whose tail
  // is free-form text and an optional illustration, not more fixed-size
  // rows.
}
bool View::RenderAnimalNeedsStrip(const ViewState &state, size_t strip_index, Image &out,
                                  int *content_h_out) {
  const auto &s = state.snapshot;
  if (!s.world_ready || !s.animals_ready || strip_index >= AnimalNeedsStripCap)
    return false;
  const auto *a = Find(s, state.animal_selected);
  if (!a)
    return false;
  std::vector<std::string> help;
  std::istringstream paragraphs(a->help_text.substr(0, 8192));
  std::string paragraph;
  while (std::getline(paragraphs, paragraph)) {
    auto lines = font_.Wrap(paragraph, 32, 1080);
    help.insert(help.end(), lines.begin(), lines.end());
    if (lines.empty())
      help.emplace_back();
  }
  const size_t needs = std::min(a->needs.size(), size_t(64));
  // The window this strip covers, in absolute (unwindowed) content rows --
  // every draw below targets absolute coordinates, then subtracts this so
  // only the portion that lands inside [0, AnimalNeedsStripHeight) actually
  // paints (Fill/Blit/Text all clip to `target`'s own w/h already, same as
  // every other draw call in this file -- see their own bounds checks).
  const int window_y0 = int(strip_index) * AnimalNeedsStripHeight;
  // Two passes, same shape as RenderHelpArticle: the first only measures
  // the total content height (needs rows + help text + help illustration),
  // the second draws the real content, top-aligned and windowed to this
  // strip, into an image that is ALWAYS AnimalNeedsStripHeight tall (see
  // AnimalNeedsStripCap's own comment for why the pane is strips at all).
  const auto walk = [&](Image *target) {
    for (size_t i = 0; i < needs; ++i) {
      const int abs_y = int(i) * NeedHeight;
      int y = target ? abs_y - window_y0 : abs_y;
      auto &n = a->needs[i];
      if (target)
        Fill(*target, 0, y, 1200, NeedHeight - 4, i % 2 ? Row : Background);
      auto lines = font_.Wrap(Label(n.label), 32, 640);
      auto v = Amount(n.level);
      if (Valid(n.capacity))
        v += " / " + Amount(n.capacity);
      if (Valid(n.level) && !n.unit.empty())
        v += " " + Label(n.unit);
      int text_top = 32, text_bottom = 0;
      for (size_t l = 0; l < std::min(lines.size(), size_t(2)); ++l) {
        auto bounds = font_.VerticalBounds(lines[l], 32);
        text_top = std::min(text_top, int(l) * 36 + bounds.first);
        text_bottom = std::max(text_bottom, int(l) * 36 + bounds.second);
      }
      auto value_bounds = font_.VerticalBounds(v, 32);
      int text_height = std::max(text_bottom - text_top,
                                 value_bounds.second - value_bounds.first);
      constexpr int GaugeGap = 12, GaugeHeight = 8, RowHeight = NeedHeight - 4;
      int composition_height = text_height + GaugeGap + GaugeHeight;
      int content_y = y + (RowHeight - composition_height) / 2;
      int gauge_y = content_y + text_height + GaugeGap;
      if (target) {
        RowLines(*target, lines, 24, content_y, text_height, 32, White);
        RowText(*target, v, 1176 - font_.Measure(v, 32), content_y, text_height,
                32, Muted);
        Fill(*target, 24, gauge_y, 1152, GaugeHeight, Header);
        if (Valid(n.level) && Valid(n.capacity) && *n.capacity > 0) {
          double ratio = std::clamp(*n.level / *n.capacity, 0., 1.);
          Fill(*target, 24, gauge_y, int(1152 * ratio), GaugeHeight,
               ratio < .15  ? 0xe91919ff
               : ratio < .5 ? 0xf7921eff
                            : Green);
        }
      }
    }
    const int abs_tail0 = int(needs * NeedHeight) + 28;
    int y = target ? abs_tail0 - window_y0 : abs_tail0;
    if (!help.empty() && target)
      if (auto atlas = assets_.Texture("dataS2/menu/hud/ui_elements_1080p.dds"))
        Blit(*target, *atlas, 24, y + 4, 36, 36, 360, 945, 36, 36, Green);
    for (const auto &line : help) {
      if (target)
        Text(*target, line, 84, y, 32, Muted);
      y += 42;
    }
    // Matches the original (pre-ScrollRegion) draw order exactly: the
    // illustration always sits 24px below wherever the help text (if any)
    // ended, even when there was no help text at all.
    y += 24;
    if (!a->help_image.empty()) {
      if (target)
        Art(*target, a->help_image, 84, y, 1080, 260);
      y += 284;
    }
    // `y` has been windowed throughout when `target` is set: undo that so
    // every exit path (measuring or drawing) returns the same absolute
    // content height, same as RenderHelpArticle's own measure/draw parity.
    return target ? y + window_y0 : y;
  };
  const int content_h = std::clamp(walk(nullptr), 1, int(AnimalNeedsCap));
  if (content_h_out)
    *content_h_out = content_h;
  out.w = 1200;
  out.h = AnimalNeedsStripHeight;
  out.rgba.assign(size_t(out.w) * out.h * 4, 0);
  // Background only over the portion of this strip that real content
  // actually reaches (content_h is absolute; clamp it into this strip's own
  // local [0, AnimalNeedsStripHeight) window).
  const int local_bg_h =
      std::clamp(content_h - window_y0, 0, AnimalNeedsStripHeight);
  if (local_bg_h > 0)
    Fill(out, 0, 0, 1200, local_bg_h, Background);
  walk(&out);
  return true;
}
} // namespace farming_simulator
