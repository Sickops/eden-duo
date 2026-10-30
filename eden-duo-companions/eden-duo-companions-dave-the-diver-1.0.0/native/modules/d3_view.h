// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Composed pictures for direction A (map-first), built only from the game's own art and baked
// fonts: the Map tab's left vitals column, map view, area title, right column, tab bar and the
// player marker. Layout constants follow research design/mockups-20261011-text (QUALITY.md rules).

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "d3_assets.h"
#include "d3_img.h"
#include "d3_reader.h"

namespace d3_view {

constexpr int CanvasW = 1240, CanvasH = 1080;
constexpr int ColW = 262, ColH = 995;          // side columns
constexpr int MapX0 = ColW, MapX1 = CanvasW - ColW; // title/meter band between the compact panes
constexpr int NavY = 995;
// Nav bar cells (Hero, Activities, Map, Gear, Options; d3.nav.mask bit i = that cell is present):
// the present labels share NavCellsX0..NavCellsX1 (the centre piece) with equal space around each
// label, so the row never spills over the end caps and short labels get no extra dead space.
// Each cell (and its tap hit) is its label plus that space. NavLabelW is each label's rendered
// width at Exocet 36 (measured against the loaded font with dsmod-d3-tool), fixed so
// tools/d3/gen_manifest.py's hits (nav_layout) use the same integer arithmetic as NavBar.
constexpr int NavCellsX0 = 170, NavCellsX1 = 1070, NavCellY = 1000, NavCellH = 75;
// Tap height for the nav cells (LEGIBILITY.md: primary controls >= 112 px): the hit rect alone
// grows to this, extended upward over NavCellH's own drawn strip (art unchanged, gen_manifest.py
// nav_hits()); the skill rail's own tap rects (RailHitH, d3_skills.h) are shrunk to the same
// boundary so the two never overlap.
constexpr int NavHitH = 112;
// HERO, ACTIVITIES, MAP, GEAR, OPTIONS (NavIndex order).
constexpr std::array<int, 5> NavLabelW{103, 223, 79, 101, 170};
constexpr int GlobePx = 128;                      // tab bar strip 1240 x 85
constexpr int MapImgW = 1536, MapImgH = 1280;  // overscan across the full-width lower map
// Map picture centre on the canvas at zero offset: the middle of the area above the tab bar, so
// the picture overscans the whole visible map (148 px each side across, 142 px up and down).
constexpr int MapImgCX = CanvasW / 2;
constexpr int MapImgCY = NavY / 2;
// A new picture is centred on the hero once the published offset comes this close to the overscan.
constexpr int MapPanMargin = 24;
// Map magnification: k x the scene tiles' own 0.8 px per world unit (sharp nearest sampling).
constexpr int MinK = 2, MaxK = 6;
inline float ScaleFor(int k) {
    return 0.8f * (k < MinK ? MinK : k);
}
// Map pan bounds below the title band; the map also shows beneath the compact panes. The hero
// marker stays at (ViewCX, ViewCY); the map pictures move under it. ViewCY/ViewH keep the marker
// at the visible centre of the area between the title band (bottom y 150) and the skill rail
// (top RailY 720, d3_skills.h): centre y = (150 + 720) / 2.
constexpr int ViewW = 1180, ViewH = 570, ViewCX = CanvasW / 2, ViewCY = 435;
// Map icons: every icon frame at IconScaleNum / IconScaleDen of its native texel size (one scale
// for all, so the game's own size hierarchy holds: a 64 px marker frame draws 85 px, about twice
// the earlier icons); the hero marker is the game's own frame fit (ink-trimmed, upscaled) to
// PlayerMarkerPx so it is never diluted by its own native frame's padding (G3 revise 2026-10-16:
// a flat 2x of the untrimmed 64 px frame -- ink only ~52 x 68 of that, the rest transparent
// padding -- still read no heavier than the other markers beside it). 80 keeps the hero's ink
// above its old flat-2x size on both axes (~61 x 80) while landing on the same ~85 px nominal
// frame every other marker already uses (IconScaleNum/Den of IconQuadTexels, QRingSize), so it
// reads as the densest, heaviest marker without dwarfing the map.
constexpr int IconScaleNum = 4;
constexpr int IconScaleDen = 3;
constexpr int PlayerMarkerPx = 80;
// A whole marker texture draws on one quad whatever its resolution (the 32 px Marker_KanaisCube
// and Marker_Vendor as large as the 64 px Marker_Stash, as the native map shows them): textures
// are normalised to this many texels first; atlas frames keep their own size.
constexpr int IconQuadTexels = 64;
constexpr int PlayerMarkerK = 2;
// Quest/bounty objective marker pulse: the native minimap/full map animates the "go here"
// destination marker (MapIconMarkerDestination) growing outward and fading, repeating; ours used
// to bake it into the map picture as a static frame like any other marker. It is now excluded
// from that bake (d3_view::MapPicture) and drawn instead as a manifest overlay: ObjectiveRing
// decodes the ring once, and scale_bind/tint_bind loop it off a module-published phase, never by
// re-rendering the map picture. QRingSlots bounds how many simultaneous destination markers get a
// ring (an extra one past the cap simply does not show, instead of double-drawing).
constexpr int QRingSlots = 3, QRingSize = 85; // 85 = IconScaled's own output size for this texture
// scale_bind reads "milli" (value / 1000, see mod_ui_widget_state.cpp WidgetImageTransform): 1000
// = 1x. QRingMargin is the worst-case extra radius the ring's growth can draw outside its own
// QRingSize box, so the manifest can keep that much clearance from the side columns/rail/nav.
constexpr int QRingMaxScaleMilli = 1800;
constexpr int QRingMargin = QRingSize * (QRingMaxScaleMilli - 1000) / 1000 / 2;

struct Vitals {
    bool live = false; // false: stale/unavailable (greyed)
    int cls = -1;
    double hp = 0, hp_max = 0;
    int res_type[2] = {-1, -1};
    double res_cur[2] = {0, 0}, res_max[2] = {0, 0};
    int hp_fill = 0, res_fill = 0; // liquid level in globe pixel rows (0..GlobePx)
    bool operator==(const Vitals&) const = default;
};

/// Short number like the game's HUD ("612K", "1.2M").
std::string Short(double v);
/// Values as the vitals column prints them (used to decide when to redraw).
Vitals Quantise(const d3_reader::Snapshot& s, bool live);

d3_img::Image VitalsColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const Vitals& v,
                          bool compact = false);
/// No placeholder panel before the activity snapshot is available (transparent right column).
d3_img::Image RightColumn();

/// Rift panel for the right column (design A): Greater Rift (tier, timer) or Nephalem Rift,
/// progress, deaths, Rift Guardian. Values are already quantised to what is shown.
struct RiftView {
    int tier = 0;            // 0 = Nephalem Rift
    int secs_left = -1;      // -1 = no timer
    int pct = 0;             // 0..100 (drawn as the native kill meter on top of the map)
    int deaths = 0;
    int guardian = 0;        // 0 not yet, 1 alive, 2 slain
    int guardian_pct = 0;
    int best_solo = -1;      // Highest_Solo_Rift_Level; -1 = unknown
    int difficulty = -1;
    bool operator==(const RiftView&) const = default;
};
d3_img::Image RiftColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const RiftView& r);

/// Bounty column (design A right column outside rifts) and Activities bounty board: the hero's act
/// and its bounties as the game lists them on the world map (Adventure mode).
struct BountyView {
    struct Entry {
        std::string name;    // display name without the "Bounty: " prefix
        int state = 0;       // 0 open, 1 active (hero in its area / in progress), 2 done
        bool operator==(const Entry&) const = default;
    };
    int act = 0;             // 1..5; 0 = act not known (summary only)
    std::vector<Entry> list; // the act's bounties
    int done[6] = {0, 0, 0, 0, 0, 0}; // completed bounties per act (index 1..5)
    int total = 0;           // bounties in the game (all acts)
    bool operator==(const BountyView&) const = default;
};
/// Act roman numerals (index 1..5; [0] unused), shared by the Map tab's BountyColumn and the
/// Activities tab's bounty board.
constexpr const char* const RomanNumerals[] = {"", "I", "II", "III", "IV", "V"};
d3_img::Image BountyColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const BountyView& b);
/// Nav order (NavIndex / d3.nav.mask bit index): 0 hero, 1 activities, 2 map, 3 gear, 4 options.
/// Returns 2 (map) for an unrecognised id.
int NavIndex(std::string_view page_id);
/// `active`: NavIndex of the current page. `mask`: which of the 5 cells are present (bit i = that
/// NavIndex is shown); Map (bit 2) and Options (bit 4) are always set by the caller. Present cells
/// are spread evenly across NavCellsX0..NavCellsX1; the end caps are decoration only (no hit).
d3_img::Image NavBar(d3_assets::Assets& a, const EdenDsmodHostApi* h, int active, int mask);
/// Area title (Exocet 45 one line, else Exocet 36 up to 2 lines, then ellipsis) + subtitle.
d3_img::Image Title(d3_assets::Assets& a, const std::string& title, const std::string& sub);
/// Rift top element of the Map tab (design A): native GR kill meter + percentage, replacing the title.
d3_img::Image RiftMeter(d3_assets::Assets& a, const EdenDsmodHostApi* h, int pct);
/// The game's own minimap hero marker (2DMapGeneric frame MarkerLocalPlayer, pointing up) fit to
/// PlayerMarkerPx; the page rotates it to the hero's facing.
d3_img::Image PlayerMarker(d3_assets::Assets& a, const EdenDsmodHostApi* h);
/// The quest/bounty "go here" destination marker (MapIconMarkerDestination), decoded once at the
/// same whole-texture scale every other marker in MapPicture uses (IconScaled), so scale_bind=1
/// matches the size a static bake of it would have drawn.
d3_img::Image ObjectiveRing(d3_assets::Assets& a, const EdenDsmodHostApi* h);
/// Pause menu open (design A state): the whole page dimmed, game logo, GAME PAUSED + mode line.
d3_img::Image PauseOverlay(d3_assets::Assets& a, const EdenDsmodHostApi* h, int difficulty);
d3_img::Image StateCard(d3_assets::Assets& a, const std::string& line1, const std::string& line2);

/// World -> screen units (isometric, like the game camera: screen right = world (-1, +1), screen
/// down = world (+1, +1)).
void WorldToScreen(float wx, float wy, float& sx, float& sy);
/// Global map pixel of screen-unit coordinate `s` at magnification k: pixel g covers screen units
/// [g, g + 1) / ScaleFor(k). Every picture of one magnification samples this one grid.
inline int MapPixel(float s, int k) {
    return static_cast<int>(std::floor(s * ScaleFor(k)));
}
/// Map picture (MapImgW x MapImgH) whose centre pixel is global map pixel (ax, ay) at
/// magnification k: scene minimap tiles masked by the game's explored grid, with the game's map
/// icons. Two pictures of one magnification agree pixel for pixel where they overlap.
d3_img::Image MapPicture(d3_assets::Assets& a, const EdenDsmodHostApi* h,
                         const std::vector<d3_reader::SceneMask>& scenes, int ax, int ay, int k,
                         const std::vector<d3_reader::Snapshot::Marker>& markers = {},
                         const std::vector<d3_reader::Snapshot::ActorMarker>& actors = {});
/// Native actor-marker distance gate (main+0x3E8E68..EF4): the minimum and maximum distance from
/// the hero (MarkerSet placement 0x86005 / 0x86004 before Actor 0x70005 / 0x70004; 0 = none).
/// False also for a malformed (negative or non-finite) bound.
bool ActorInRadius(const std::map<std::uint32_t, std::uint32_t>& actor_tags,
                   const std::map<std::uint32_t, std::uint32_t>& placement, float distance_sq);

std::string DifficultyName(int d);
} // namespace d3_view
