// SPDX-License-Identifier: GPL-3.0-or-later
// Shrine compass: nearest-undiscovered selection, bearing math (incl. wrap), the all-discovered
// close case, close-on-discovery, distance formatting and DLC gating. Pure-logic tests (no live
// memory) following the same `Flags` fixture pattern botw_tracker_data_test.cpp uses for
// tracker_data_records<Flags>.
#include "botw_shrine_compass.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_map>
struct Flags {
  std::unordered_map<uint32_t, bool> b;
  std::unordered_map<uint32_t, int32_t> n;
  std::optional<bool> boolean(uint32_t hash) {
    auto i = b.find(hash);
    return i == b.end() ? std::optional<bool>{} : std::optional<bool>(i->second);
  }
  std::optional<int32_t> integer(uint32_t hash) {
    auto i = n.find(hash);
    return i == n.end() ? std::optional<int32_t>{} : std::optional<int32_t>(i->second);
  }
};
int main() {
  unsigned cases{};
  auto check = [&](bool ok) { assert(ok); ++cases; };

  // --- nearest selection (pure, synthetic candidates) ---------------------------------------
  {
    std::vector<botw::ShrineCandidate> c = {
        {"Dungeon001", 100, 0, false},
        {"Dungeon002", 10, 0, false},   // nearest unfound
        {"Dungeon003", 1, 0, true},     // nearest overall, but already found -- must be skipped
    };
    auto out = botw::nearest_undiscovered(c, 0, 0, 0);
    check(out.valid && out.has_target && out.target_id == "Dungeon002" && std::abs(out.distance - 10) < 1e-9);
    check(!out.all_found);
  }
  // nearest selection, real shrine table + flags (mirrors tracker_data_test.cpp's style): mark
  // every base shrine found except two, verify the nearer of the two is picked and its real
  // coordinates/distance are used.
  {
    Flags f;
    for (const auto& e : botw::tracker_shrines) f.b[botw::tracker_crc(std::string("Clear_") + e.id)] = true;
    const auto& near = botw::tracker_shrines[5];
    const auto& far = botw::tracker_shrines[9];
    f.b[botw::tracker_crc(std::string("Clear_") + near.id)] = false;
    f.b[botw::tracker_crc(std::string("Clear_") + far.id)] = false;
    const auto cands = botw::shrine_candidates(f, std::optional<bool>(false));
    check(cands.size() == 120); // DLC excluded (dlc_on=false)
    // Player standing exactly on `near`'s position: `near` must win over `far` even if `far`
    // happens to be closer to the origin, because distance is computed from the player, not 0,0.
    auto out = botw::nearest_undiscovered(cands, near.x, -near.z, 0);
    check(out.valid && out.has_target && out.target_id == near.id && out.distance < 1e-3);
  }
  // DLC gating: unknown/false excludes the 16 DLC shrines; true includes them.
  {
    Flags f;
    check(botw::shrine_candidates(f, std::optional<bool>{}).size() == 120);
    check(botw::shrine_candidates(f, std::optional<bool>(false)).size() == 120);
    check(botw::shrine_candidates(f, std::optional<bool>(true)).size() == 136);
  }

  // --- bearing math, including wrap across 0/360 ------------------------------------------
  {
    // Target due north of the player (dx=0, dy=100): raw bearing 0.
    std::vector<botw::ShrineCandidate> north = {{"Dungeon001", 0, 100, false}};
    check(std::abs(botw::nearest_undiscovered(north, 0, 0, 0).bearing - 0.0) < 1e-6);
    check(std::abs(botw::nearest_undiscovered(north, 0, 0, 10).bearing - 10.0) < 1e-6);
    // Target due east (dx=100, dy=0): raw bearing 90. Rotation 280 wraps 90+280=370 -> 10.
    std::vector<botw::ShrineCandidate> east = {{"Dungeon001", 100, 0, false}};
    auto east_out = botw::nearest_undiscovered(east, 0, 0, 280);
    check(std::abs(east_out.bearing - 10.0) < 1e-6);
    // Target due south (dx=0, dy=-100): raw bearing 180. Rotation -190 -> -10 -> wraps to 350.
    std::vector<botw::ShrineCandidate> south = {{"Dungeon001", 0, -100, false}};
    auto south_out = botw::nearest_undiscovered(south, 0, 0, -190);
    check(std::abs(south_out.bearing - 350.0) < 1e-6);
    // Target due west (dx=-100, dy=0): raw bearing 270. Rotation 95 -> 365 -> wraps to 5.
    std::vector<botw::ShrineCandidate> west = {{"Dungeon001", -100, 0, false}};
    auto west_out = botw::nearest_undiscovered(west, 0, 0, 95);
    check(std::abs(west_out.bearing - 5.0) < 1e-6);
  }

  // --- all discovered: has_target false, all_found true, and the decision closes ------------
  {
    std::vector<botw::ShrineCandidate> all_found = {
        {"Dungeon001", 10, 0, true}, {"Dungeon002", -5, 5, true}, {"Dungeon003", 0, -20, true}};
    auto out = botw::nearest_undiscovered(all_found, 0, 0, 0);
    check(out.valid && !out.has_target && out.all_found);
    // Iteration 12 item 8: all found keeps the card open with "All shrines found" for
    // kShrineCompassAllFoundHold updates (no one-frame open/close blink), then closes.
    auto decision = botw::shrine_compass_decide("", false, out);
    check(!decision.close_requested && decision.all_found && decision.next_target.empty());
    check(!botw::shrine_compass_decide("", false, out, botw::kShrineCompassAllFoundHold - 1).close_requested);
    auto done = botw::shrine_compass_decide("", false, out, botw::kShrineCompassAllFoundHold);
    check(done.close_requested && done.all_found && done.next_target.empty());
  }
  // Empty candidate list (e.g. DLC-only build with DLC off and somehow zero base shrines) is
  // not treated as "all found" (there was nothing to find).
  {
    std::vector<botw::ShrineCandidate> none;
    auto out = botw::nearest_undiscovered(none, 0, 0, 0);
    check(out.valid && !out.has_target && !out.all_found);
    check(!botw::shrine_compass_decide("", false, out).close_requested);
  }

  // --- close-on-discovery: the shown target became discovered since the overlay opened ------
  {
    std::vector<botw::ShrineCandidate> c = {{"Dungeon001", 50, 0, false}, {"Dungeon002", 5, 0, false}};
    auto out = botw::nearest_undiscovered(c, 0, 0, 0);
    // Previous target "Dungeon999" (not even a candidate here) just got discovered -> close,
    // regardless of there being a perfectly good new target.
    auto decision = botw::shrine_compass_decide("Dungeon999", true, out);
    check(decision.close_requested && decision.next_target.empty());
    // Previous target still not found (previous_found=false) -> no forced close; the fresh
    // nearest target is remembered for next tick.
    auto keep = botw::shrine_compass_decide("Dungeon999", false, out);
    check(!keep.close_requested && keep.next_target == "Dungeon002");
  }

  // --- distance formatting -------------------------------------------------------------------
  {
    check(botw::shrine_compass_distance_text(0) == "0 m");
    check(botw::shrine_compass_distance_text(42.4) == "42 m");
    check(botw::shrine_compass_distance_text(240) == "240 m");
    check(botw::shrine_compass_distance_text(1240) == "1,240 m");
    check(botw::shrine_compass_distance_text(1234567) == "1,234,567 m");
    check(botw::shrine_compass_distance_text(-1).empty());
    check(botw::shrine_compass_distance_text(std::nan("")).empty());
  }

  // --- shrine_found(): Clear_ wins, then Location_ (seen) > 0, else unknown == not found ------
  {
    Flags f;
    check(!botw::shrine_found(f, "Dungeon001")); // no data at all -> not found
    f.n[botw::tracker_crc("Location_Dungeon001")] = 0;
    check(!botw::shrine_found(f, "Dungeon001")); // seen but 0 -> not found
    f.n[botw::tracker_crc("Location_Dungeon001")] = 1;
    check(botw::shrine_found(f, "Dungeon001")); // seen > 0 -> found
    f.b[botw::tracker_crc("Clear_Dungeon002")] = false;
    check(!botw::shrine_found(f, "Dungeon002")); // cleared=false, no seen data -> not found
    f.b[botw::tracker_crc("Clear_Dungeon002")] = true;
    check(botw::shrine_found(f, "Dungeon002")); // cleared=true -> found even without seen data
  }

  // --- guards: shrine_compass_live()/Controller never touch the host when the position is
  // invalid, so a zero-initialised host (no readable memory at all) is safe here. -------------
  {
    EdenDsmodHostApi h{};
    botw::MapPosition invalid;
    auto out = botw::shrine_compass_live(h, invalid, 0, "");
    check(!out.valid && !out.has_target);
    botw::ShrineCompassController ctl;
    auto closed = ctl.update(h, false, invalid, 0);
    check(!closed.valid && !ctl.close_requested);
    auto still_invalid = ctl.update(h, true, invalid, 0);
    check(!still_invalid.valid && !still_invalid.has_target);
  }

  std::cout << cases << " shrine compass cases passed\n";
}
