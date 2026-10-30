// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/mods/dsmod_module_abi.h"
#include "p5r_romfs_assets.h"
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace farming_simulator {
using Image = p5r_assets::Image;
struct HelpItem {
    bool image{};
    std::string value;
    double height_scale{1};
};
struct HelpTopic {
    std::string category, title;
    std::vector<HelpItem> items;
};
struct Field {
    int number{};
    double x{}, z{};
};
struct Hotspot {
    // G3 critique Finding 9 (BUG, 2026-10-17): the FILTERS popup only ever hid markers from
    // the live reader's map_markers list; these static, map-XML-authored hotspots (Shop,
    // animal pastures, and -- confirmed by reading map02.xml -- named selling points like
    // "WoodSelling1"/"SilageHeap") were never covered, so turning "Selling points" off still
    // left a static selling-point hotspot on the map. `name` (the XML tag's own `name`
    // attribute, e.g. "Shop"/"WoodSelling1"/"Cows") lets RenderMapBase classify and filter
    // these the same way MapMarkerFilterIndex classifies a live marker's `kind`.
    std::string name;
    double x{}, z{};
    int sx{}, sy{}, w{}, h{};
    uint32_t colour{0xffffffff};
};
struct MapData {
    std::vector<Field> fields;
    std::vector<Hotspot> hotspots;
};
struct VehiclePresentation {
    std::string name, image;
};
// An asset-worker-owned cache. All reads use the player's current merged RomFS.
class Assets {
  public:
    explicit Assets(const EdenDsmodHostApi &host) : host_{host} {}
    bool Read(std::string_view path, std::vector<uint8_t> &bytes,
              size_t limit = 16 * 1024 * 1024) const;
    std::shared_ptr<const Image> Texture(std::string path, bool flip = true);
    // The tightest {left, top, right, bottom} opaque-ish box used by
    // View::Art's fit_alpha cropping, cached alongside the decoded texture it
    // was scanned from (keyed by that Image's address: stable for as long as
    // Texture()'s own cache entry -- and so this one, cleared in step with it
    // -- keeps it alive; the pixels an Image holds never change once decoded).
    const std::array<int, 4> &AlphaBBox(const Image &image);
    const std::vector<HelpTopic> &Help();
    const MapData &Map(int id);
    std::string Localize(std::string_view key);
    // Join the reader's native configFileName to installed storeData. No catalog/art IO in reader.
    VehiclePresentation Vehicle(std::string config);

  private:
    bool LoadText();
    EdenDsmodHostApi host_{};
    std::map<std::string, std::shared_ptr<const Image>> images_;
    std::map<const Image *, std::array<int, 4>> alpha_bbox_;
    std::map<std::string, std::string> locale_;
    std::vector<HelpTopic> help_;
    MapData maps_[2];
    bool text_ready_{}, map_ready_[2]{};
    std::map<std::string, VehiclePresentation> vehicles_;
};
std::string XmlAttribute(std::string_view tag, std::string_view name);
std::string XmlUnescape(std::string_view value);
void Fill(Image &out, int x, int y, int w, int h, uint32_t rgba);
void Circle(Image &out, int x, int y, int radius, uint32_t colour);
void Blit(Image &out, const Image &source, int x, int y, int w, int h, int sx = 0, int sy = 0,
          int sw = 0, int sh = 0, uint32_t tint = 0xffffffff);
} // namespace farming_simulator
