// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_assets.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

// Private installed-ROM integration test. No extracted game fixture is bundled.
// With a RomFS directory, emit an exact registry/cell TSV for independent
// comparison against a separately decoded native oracle. No golden geometry
// is embedded in this source. This tool is never part of the companion.
int main(int argc, char **argv) {
  using namespace belmont_map_assets;
  Reader missing(ce_unity::RangeReader{});
  assert(!missing.LoadStreetsPoor());
  if (argc != 2) {
    std::puts("missing-source unavailable: PASS (pass own RomFS root for "
              "integration)");
    return 0;
  }
  auto source = ce_unity::MakeDirectoryRangeReader(argv[1]);
  Reader reader(source);
  auto world = reader.LoadStreetsPoor();
  assert(world && !world->rooms.empty());
  std::size_t enabled = 0, slots = 0;
  for (const auto &room : world->rooms) {
    assert(room.chunks.size() == std::size_t(room.width) * room.height);
    std::printf("ROOM\t%s\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                room.id.c_str(), static_cast<long long>(room.source_path_id),
                room.width, room.height, room.edition_offset.x,
                room.edition_offset.y, room.world_origin.x, room.world_origin.y,
                room.simulation_origin.x, room.simulation_origin.y,
                room.chunk_size.x, room.chunk_size.y);
    for (const auto &chunk : room.chunks) {
      assert(chunk.local.x >= 0 && chunk.local.x < room.width &&
             chunk.local.y >= 0 && chunk.local.y < room.height);
      ++slots;
      enabled += chunk.enabled;
      std::printf("CELL\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", room.id.c_str(),
                  chunk.local.x, chunk.local.y, chunk.world.x, chunk.world.y,
                  int(chunk.enabled), chunk.doors, chunk.forced_walls,
                  chunk.removed_wall_parts);
    }
  }
  std::printf("TOTAL\t%zu\t%zu\t%zu\n", world->rooms.size(), slots, enabled);
  auto warm = reader.LoadStreetsPoor();
  assert(warm && warm->rooms.size() == world->rooms.size());
  reader.ClearCache();
  auto cold = reader.LoadStreetsPoor();
  assert(cold && cold->rooms.size() == world->rooms.size());
  // Fail a dependent settings bundle: no partial world/placeholder dimensions.
  auto absent = source;
  absent.size =
      [source](std::string_view path) -> std::optional<std::uint64_t> {
    if (path.find("rd-shared_") != std::string_view::npos)
      return std::nullopt;
    return source.size(path);
  };
  Reader missing_settings(absent);
  assert(!missing_settings.LoadStreetsPoor());
  // Reported-but-unreadable data must not become an empty/visible map.
  auto broken = source;
  broken.read = [](std::string_view, std::uint64_t, std::uint8_t *,
                   std::size_t) { return false; };
  Reader broken_reads(broken);
  assert(!broken_reads.LoadStreetsPoor());
  // A short source must fail safely without changing the installed files.
  auto truncated = source;
  truncated.size =
      [source](std::string_view path) -> std::optional<std::uint64_t> {
    auto size = source.size(path);
    return size && *size ? std::optional<std::uint64_t>(*size - 1)
                         : std::nullopt;
  };
  truncated.read = [source](std::string_view path, std::uint64_t off,
                            std::uint8_t *out, std::size_t n) {
    auto size = source.size(path);
    return size && *size && off <= *size - 1 && n <= *size - 1 - off &&
           source.read(path, off, out, n);
  };
  Reader short_source(truncated);
  assert(!short_source.LoadStreetsPoor());
  std::fprintf(stderr, "native registry, occupancy, cache reload and "
                       "unavailable-source checks: PASS\n");
}
