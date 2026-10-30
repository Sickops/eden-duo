// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_world_map.h"
#include "belmont_map_decoration.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#ifdef __linux__
#include <sys/resource.h>
#endif
namespace {
std::uint64_t Hash(const std::uint8_t *p, std::size_t n) {
  std::uint64_t v = 14695981039346656037ull;
  for (std::size_t i = 0; i < n; ++i)
    v = (v ^ p[i]) * 1099511628211ull;
  return v;
}
std::uint64_t Hash(const ce_unity::Image &im) {
  return Hash(im.rgba.data(), im.rgba.size());
}
long Peak() {
#ifdef __linux__
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  return usage.ru_maxrss;
#else
  return 0;
#endif
}
} // namespace
int main(int argc, char **argv) {
  using namespace belmont_world_map;
  {
    auto a=std::make_shared<Frame>();
    a->image={4,2,std::vector<std::uint8_t>(32,40)};
    for(std::size_t n=3;n<32;n+=4)a->image.rgba[n]=255;
    a->revealed_zones={{0,0}};
    auto b=std::make_shared<Frame>(*a);
    b->revealed_zones.push_back({0,1});b->updated_regions={{1,0,2,1}};
    for(unsigned x=1;x<3;++x)for(unsigned c=0;c<3;++c)b->image.rgba[x*4+c]=100;
    assert(IsNewReveal(*a,*b));
    assert(BlendReveal(a,b,0)==a&&BlendReveal(a,b,1)==b);
    auto half=BlendReveal(a,b,.5f);
    assert(half&&half->image.rgba[4]==70&&a->image.rgba[4]==40&&b->image.rgba[4]==100);
    auto successor=std::make_shared<Frame>(*b);
    successor->revealed_zones.push_back({0,2});successor->updated_regions={{0,1,1,1}};
    successor->image.rgba[16]=120;
    auto retarget=BlendReveal(half,successor,.5f);
    assert(retarget&&retarget->updated_regions.size()==2);
    assert(retarget->image.rgba[4]==85&&retarget->image.rgba[16]==80);
    for(std::size_t n=0;n<32;++n){
      const auto lo=std::min(a->image.rgba[n],b->image.rgba[n]);
      const auto hi=std::max(a->image.rgba[n],b->image.rgba[n]);
      assert(half->image.rgba[n]>=lo&&half->image.rgba[n]<=hi);
    }
    assert(!BlendReveal(a,b,NAN)&&!BlendReveal({},b,.5f));
    assert(!IsNewReveal(*b,*a)); // removal is never a discovery fade
    auto bad=*b;bad.navigation_zone_id=123;assert(!IsNewReveal(*a,bad));
    bad=*b;bad.updated_regions={{3,0,2,1}};assert(!IsNewReveal(*a,bad));
    bad=*b;bad.revealed_zones={{0,1},{0,2}};assert(!IsNewReveal(*a,bad));
    bad=*b;bad.revealed_zones={{0,0},{0,0}};assert(!IsNewReveal(*a,bad));
    bad=*b;bad.world_bounds.x=1;assert(!IsNewReveal(*a,bad));
    bad=*b;bad.image.rgba.clear();assert(!IsNewReveal(*a,bad));
  }
  {
    using namespace belmont_map_decoration;
    const Water p{.25f, .5f, .5f, .5f};
    assert(WaterMask(0.f, 1.f, 0.f, p) == 0.f);
    assert(WaterMask(1.f, 0.f, 0.f, p) == 0.f);
    assert(WaterMask(1.f, 1.f, 0.f, p) == 1.f);
    assert(std::abs(WaterMask(.75f, .5f, 0.f, p) - .25f) < 1e-6f);
    assert(std::abs(WaterMask(1.f, 1.f, 1.f, p) - .75f) < 1e-6f);
    assert(WaterMask(1.f, 1.f, 6.f, p) == -.5f);
    auto invalid = p; invalid.contrast = 1.f;
    assert(WaterMask(1.f, 1.f, 0.f, invalid) == 0.f);
    assert(WaterMask(NAN, 1.f, 0.f, p) == 0.f);
    assert(std::abs(WaterOffset(0.f, 1.f, .01f) + .03f) < 1e-6f);
    assert(std::abs(WaterOffset(1.f, 1.f, .01f) - .01f) < 1e-6f);
    assert(WaterOffset(-1.f, 1.f, .01f) == WaterOffset(1.f, 1.f, .01f));
    Lines lines{0.f, 0.f, .5f, .5f, 2.f, .5f, .5f,
                0.f, 0.f, 0.f, 0.f, {}, 0.f, 0.f, 0.f, 0.f, 0.f};
    const Noise bright{1.f, 1.f, 1.f, 1.f};
    assert(LineSignal(0.f, bright, bright, bright, lines) == 0.f);
    assert(std::abs(LineSignal(1.f, bright, bright, bright, lines) - Smooth(.49f)) < 1e-6f);
    assert(LineSignal(10.f, bright, bright, bright, lines) == 0.f);
    lines.main_noise_strength = lines.second_noise_strength = 2.f;
    assert(LineSignal(1.f, bright, bright, bright, lines) == 0.f);
    assert(Gate(0.f, 0.f) == 0.f && Gate(1.f, 0.f) == 1.f);
  }
  Renderer missing(ce_unity::RangeReader{});
  assert(!missing.Bounds());
  auto noncanonical = belmont_map_sources::StreetsPoor();
  ++noncanonical.map_context_id;
  assert(!missing.Bounds(noncanonical));
  Request invalid;
  assert(!missing.Render(invalid));
  invalid.current_room_id = "unknown";
  assert(!missing.Render(invalid));
  assert(!missing.Render(noncanonical, invalid));
  if (argc != 3 && argc != 4)
    return 0;
  // Optional actual-source test. ASTC callback fixtures are supplied
  // externally, never stored in source/packages; callbacks only stand in for
  // the host decoder.
  const auto peak_before = Peak();
  const std::string root = argv[1], fixtures = argv[2];
  auto dump = [&](const char *name, const ce_unity::Image &image) {
    if (argc != 4)
      return;
    std::ofstream output(std::string(argv[3]) + '/' + name,
                         std::ios::binary);
    assert(output.write(reinterpret_cast<const char *>(image.rgba.data()),
                        std::streamsize(image.rgba.size())));
  };
  ce_unity::AstcDecoder decoder = [&](const auto *bytes, auto n, auto w, auto h,
                                      auto, auto, auto &rgba) {
    std::ostringstream name;
    name << fixtures << '/' << std::hex << std::setfill('0') << std::setw(16)
         << Hash(bytes, n) << std::dec << '-' << w << '-' << h << ".rgba";
    std::ifstream input(name.str(), std::ios::binary | std::ios::ate);
    if (!input)
      input = std::ifstream(fixtures + "/../generic-tiles-samplers" +
                            name.str().substr(fixtures.size()),
                            std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != std::streamoff(std::size_t(w) * h * 4))
      return false;
    rgba.resize(std::size_t(w) * h * 4);
    input.seekg(0);
    return bool(input.read(reinterpret_cast<char *>(rgba.data()),
                           std::streamsize(rgba.size())));
  };
  auto range = ce_unity::MakeDirectoryRangeReader(root);
  belmont_map_fog::Reader fog(range);
  auto source = fog.LoadStreetsPoor();
  assert(source);
  auto first = std::find_if(
      source->rooms.begin(), source->rooms.end(), [](const auto &r) {
        return r.id.find("_FirstRoom_") != std::string::npos;
      });
  assert(first != source->rooms.end() && first->zones.size() >= 2);
  const auto main = first->zones.front().navigation_zone_id;
  Renderer renderer(range, decoder);
  // Every candidate bounds query stays metadata-only, even with no texture
  // decoder. A missing optional color source cannot block StreetsPoor bounds.
  Renderer metadata_only(range);
  unsigned metadata_worlds = 0;
  for (const auto &recipe : belmont_map_sources::Recipes) {
    const auto world_bounds = metadata_only.Bounds(recipe);
    assert(world_bounds && world_bounds->width > 0 && world_bounds->height > 0);
    assert(metadata_only.GetStatistics().baked_tiles == 0);
    ++metadata_worlds;
  }
  assert(metadata_worlds == 10);
  metadata_only.ClearCache();
  unsigned prepared_worlds = 0;
  std::ostringstream preparations;
  preparations << '[';
  {
    Renderer generic_color(range, decoder);
    for (std::size_t i = 0; i < belmont_map_sources::Recipes.size(); ++i) {
      const auto &recipe = belmont_map_sources::Recipes[i];
      auto native = fog.Load(recipe);
      assert(native);
      auto main_room = std::find_if(native->rooms.begin(), native->rooms.end(),
          [&](const auto &room) { return room.zones.front().navigation_zone_id ==
                                       native->main_navigation_zone_id; });
      assert(main_room != native->rooms.end() && !main_room->zones[0].shapes.empty());
      Request candidate;
      candidate.current_room_id = main_room->id;
      const auto &shape = main_room->zones[0].shapes[0];
      candidate.player_x = shape.bounds.x + shape.bounds.width * .5f;
      candidate.player_y = shape.bounds.y + shape.bounds.height * .5f;
      candidate.explored_rooms.push_back({main_room->id, {main_room->zones[0].id}});
      auto frame = generic_color.Render(recipe, candidate);
      if (recipe.id == belmont_map_sources::Id::Ending) assert(!frame);
      if (frame) {
        ++prepared_worlds;
        assert(frame->source_id == recipe.id && !frame->is_z_map &&
               frame->pixels_per_world >= 1 && frame->pixels_per_world <= 3 &&
               frame->image.width <= 4096 && frame->image.height <= 4096 &&
               frame->image.rgba.size() <= 16u * 1024u * 1024u);
        assert(generic_color.Render(candidate) == frame);
      }
      if (i) preparations << ',';
      preparations << "{\"source_index\":" << i << ",\"available\":"
                   << (frame ? "true" : "false")
                   << ",\"pixels_per_world\":" << (frame ? frame->pixels_per_world : 0)
                   << ",\"image_bytes\":" << (frame ? frame->image.rgba.size() : 0) << '}';
    }
  }
  preparations << ']';
  assert(prepared_worlds >= 2); // StreetsPoor + native cached Cemetery palette.
  const auto bounds = renderer.Bounds();
  assert(bounds && bounds->width > 0 && bounds->height > 0);
  Request request;
  request.current_room_id = first->id;
  request.player_x = first->zones[0].shapes[0].bounds.x + 12;
  request.player_y = first->zones[0].shapes[0].bounds.y + 6;
  request.explored_rooms.push_back({first->id, {first->zones[0].id}});
  auto one = renderer.Render(request);
  assert(one && one->source_id == belmont_map_sources::Id::StreetsPoor &&
         one->pixels_per_world == 3 &&
         one->revealed_zone_count == 1 && one->updated_tiles == 24 &&
         !one->is_z_map && one->navigation_zone_id == main);
  assert(one->image.width == 3072 && one->image.height == 1152 &&
         one->image.rgba.size() <= 16u * 1024u * 1024u);
  const auto one_hash = Hash(one->image);
  dump("first-room-one-zone.rgba", one->image);
  if (one_hash != 16350129225810602313ull)
    std::cerr << "first-room-one-zone hash " << one_hash << '\n';
  assert(one_hash == 16350129225810602313ull);
  const auto cold = renderer.GetStatistics();
  auto identical = renderer.Render(request);
  assert(identical == one && renderer.GetStatistics().unchanged_requests == 1);
  const auto stationary_stats = renderer.GetStatistics();
  request.player_x += .25f;
  assert(renderer.Render(request) == one);
  const auto moved_stats = renderer.GetStatistics();
  assert(moved_stats.generations == stationary_stats.generations &&
         moved_stats.composed_tiles == stationary_stats.composed_tiles &&
         moved_stats.baked_tiles == stationary_stats.baked_tiles);
  identical.reset();
  request.explored_rooms[0].entered_zone_ids.push_back(first->zones[1].id);
  auto two = renderer.Render(request);
  assert(two && two->revealed_zone_count == 2 && two->updated_tiles > 0 &&
         two->updated_tiles < 24 && Hash(two->image) != one_hash);
  assert(IsNewReveal(*one,*two));
  const auto fade_start=std::chrono::steady_clock::now();
  std::uint64_t changed_pixels=0;
  double blend_ms=0;
  for(unsigned step=1;step<5;++step){
    const auto step_start=std::chrono::steady_clock::now();
    const auto blended=BlendReveal(one,two,float(step)/5.f);
    blend_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-step_start).count();
    assert(blended&&blended->image.rgba.size()==one->image.rgba.size());
    for(std::size_t n=0;n<one->image.rgba.size();++n){
      const auto a=one->image.rgba[n],b=two->image.rgba[n],v=blended->image.rgba[n];
      assert(v>=std::min(a,b)&&v<=std::max(a,b));
      if(step==1&&n%4==0&&a!=b)++changed_pixels;
    }
  }
  std::cout<<"reveal_fade_four_steps_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-fade_start).count()
           <<" blend_only_ms="<<blend_ms<<" changed_red_pixels="<<changed_pixels<<" dirty_regions="<<two->updated_regions.size()<<'\n';
  const auto dirty = renderer.GetStatistics();
  dump("first-room-two-zones.rgba", two->image);
  assert(Hash(one->image) == one_hash);
  one.reset();
  request.explored_rooms[0].entered_zone_ids.pop_back();
  auto restored = renderer.Render(request);
  assert(restored && Hash(restored->image) == one_hash);
  two.reset();
  auto later = std::find_if(
      source->rooms.begin(), source->rooms.end(), [&](const auto &r) {
        return r.id != first->id && r.zones.front().navigation_zone_id == main;
      });
  assert(later != source->rooms.end());
  request.current_room_id = later->id;
  request.player_x = later->zones[0].shapes[0].bounds.x + 1;
  request.player_y = later->zones[0].shapes[0].bounds.y + 1;
  request.explored_rooms.push_back({later->id, {later->zones[0].id}});
  auto other = renderer.Render(request);
  assert(other && other->revealed_zone_count == 2);
  restored.reset();
  other.reset();
  Request all = request;
  all.explored_rooms.clear();
  unsigned main_rooms = 0, main_zones = 0;
  for (const auto &room : source->rooms)
    if (room.zones.front().navigation_zone_id == main) {
      ++main_rooms;
      ExploredRoom entry{room.id, {}};
      for (const auto &zone : room.zones) {
        entry.entered_zone_ids.push_back(zone.id);
        ++main_zones;
      }
      all.explored_rooms.push_back(std::move(entry));
    }
  auto complete_fixture = renderer.Render(all);
  assert(complete_fixture &&
         complete_fixture->revealed_zone_count == main_zones);
  const auto whole = renderer.GetStatistics();
  complete_fixture.reset();
  auto bad = all;
  bad.current_room_id = "unknown";
  assert(!renderer.Render(bad));
  bad = all;
  bad.current_room_id += "_missing";
  assert(!renderer.Render(bad));
  bad = all;
  bad.explored_rooms.front().entered_zone_ids.push_back(65535);
  assert(!renderer.Render(bad));
  bad = all;
  bad.culled_navigation_zone_ids = {main};
  assert(!renderer.Render(bad));
  bad = all;
  bad.explored_rooms.push_back(bad.explored_rooms.front());
  assert(!renderer.Render(bad));
  // All six side groups use source normal geometry, independently filtered
  // histories. Switching groups must clear prior reveal across the full atlas.
  Request side_request = all;
  std::set<std::int64_t> checked_side_groups;
  for (const auto &room : source->rooms)
    if (room.zones.front().navigation_zone_id != main) {
      ExploredRoom entry{room.id, {}};
      for (const auto &zone : room.zones)
        entry.entered_zone_ids.push_back(zone.id);
      side_request.explored_rooms.push_back(std::move(entry));
    }
  std::uint64_t side_bakes{};
  unsigned side_groups = 0, side_zones = 0;
  for (const auto &room : source->rooms) {
    const auto nav = room.zones.front().navigation_zone_id;
    if (nav == main || !checked_side_groups.insert(nav).second)
      continue;
    ++side_groups;
    side_request.current_room_id = room.id;
    side_request.player_x = room.zones[0].shapes[0].bounds.x + 1;
    side_request.player_y = room.zones[0].shapes[0].bounds.y + 1;
    unsigned expected = 0;
    for (const auto &r : source->rooms)
      if (r.zones.front().navigation_zone_id == nav)
        expected += unsigned(r.zones.size());
    side_zones += expected;
    auto side_frame = renderer.Render(side_request);
    assert(side_frame && side_frame->is_z_map &&
           side_frame->navigation_zone_id == nav &&
           side_frame->revealed_zone_count == expected &&
           side_frame->updated_tiles == 24);
    const auto side_hash = Hash(side_frame->image);
    if (side_groups == 1)
      dump("side-navigation-fixture.rgba", side_frame->image);
    const auto baked = renderer.GetStatistics().baked_tiles;
    if (!side_bakes)
      side_bakes = baked;
    assert(baked == side_bakes); // Z-to-Z shares the active color cache.
    assert(renderer.Render(side_request) == side_frame);
    auto blocked = side_request;
    blocked.culled_navigation_zone_ids = {nav};
    assert(!renderer.Render(blocked));
    // Z must not depend on skyline source/decoder. Use an independently cold
    // instance whose callback rejects every skyline ASTC12x12 slice (decoder receives padded dimensions).
    Renderer no_skyline(range, [&](const auto *bytes, auto n, auto w, auto h,
                                  auto bw, auto bh, auto &rgba) {
      return bw == 12 && bh == 12 ? false
          : decoder(bytes, n, w, h, bw, bh, rgba);
    });
    auto cold_side = no_skyline.Render(side_request);
    assert(cold_side && Hash(cold_side->image) == side_hash);
    assert(!no_skyline.Render(request)); // Main genuinely needs skyline.
  }
  assert(side_groups == 6 && side_zones == 55);
  const auto main_again = renderer.Render(all);
  assert(main_again && !main_again->is_z_map && main_again->updated_tiles == 24);
  assert(renderer.GetStatistics().baked_tiles == side_bakes + 24);
  // Verify removal of most historical zones against a clean cold composition.
  auto pruned = renderer.Render(request);
  assert(pruned && pruned->revealed_zone_count == 2);
  const auto pruned_hash = Hash(pruned->image);
  const auto previous_generation = pruned->generation;
  const auto cemetery_bounds = renderer.Bounds(belmont_map_sources::Recipes[1]);
  assert(cemetery_bounds && renderer.GetStatistics().current_frame_bytes == 0);
  const auto rebound = renderer.Render(belmont_map_sources::StreetsPoor(), request);
  assert(rebound && rebound->generation > previous_generation &&
         Hash(rebound->image) == pruned_hash);
  assert(!renderer.Render(belmont_map_sources::Recipes[1], request));
  auto forged = belmont_map_sources::StreetsPoor();
  ++forged.world_root_id;
  assert(!renderer.Bounds(forged) && !renderer.Render(forged, request));
  pruned.reset();
  renderer.ClearCache();
  auto clean = renderer.Render(request);
  assert(clean && Hash(clean->image) == pruned_hash);
  std::cout << "{\"status\":\"pass\",\"scope\":\"actual installed source "
               "fixtures; unplayed rooms are not live verified\",\"width\":"
            << clean->image.width << ",\"height\":" << clean->image.height
            << ",\"image_bytes\":" << clean->image.rgba.size()
            << ",\"source_color_bytes\":" << cold.source_color_bytes
            << ",\"main_source_rooms\":" << main_rooms
            << ",\"all_source_zone_fixture_count\":" << main_zones
            << ",\"cold_source_ms\":" << cold.cold_source_ms
            << ",\"cold_total_ms\":" << cold.last_render_ms
            << ",\"one_zone_dirty_ms\":" << dirty.last_render_ms
            << ",\"all_source_zones_ms\":" << whole.last_render_ms
            << ",\"initial_composed_tiles\":" << cold.composed_tiles
            << ",\"metadata_worlds\":" << metadata_worlds
            << ",\"prepared_worlds\":" << prepared_worlds
            << ",\"source_preparation\":" << preparations.str()
            << ",\"side_navigation_groups\":" << side_groups
            << ",\"side_source_zones\":" << side_zones
            << ",\"first_room_hash\":" << one_hash
            << ",\"baseline_peak_rss_kib\":" << peak_before
            << ",\"peak_rss_kib\":" << Peak()
            << ",\"immutable_reuse\":true,\"removed_zones_match_clean_"
               "rebuild\":true,\"all_six_side_navigations_verified\":true}"
            << '\n';
}
