// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_tiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

std::uint32_t Bits(float f) {
  std::uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
std::uint64_t Hash(const std::vector<std::uint8_t> &bytes) {
  std::uint64_t h = 14695981039346656037ULL;
  for (auto b : bytes) {
    h ^= b;
    h *= 1099511628211ULL;
  }
  return h;
}
int main(int argc, char **argv) {
  using namespace belmont_map_tiles;
  Reader missing(ce_unity::RangeReader{});
  assert(!missing.LoadStreetsPoorLayout());
  assert(!missing.LoadStreetsPoorLayer(Source::Geometry, 0));
  if (argc != 2) {
    std::puts("missing sources unavailable: PASS");
    return 0;
  }
  auto source = ce_unity::MakeDirectoryRangeReader(argv[1]);
  Reader reader(source);
  auto tiles = reader.LoadStreetsPoorLayout();
  assert(tiles && !tiles->empty());
  for (const auto &t : *tiles) {
    std::printf("TILE\t%lld\t%lld\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
                static_cast<long long>(t.renderer_id),
                static_cast<long long>(t.mesh_id), t.layer, Bits(t.bounds.x),
                Bits(t.bounds.y), Bits(t.bounds.width), Bits(t.bounds.height),
                Bits(t.origin.x), Bits(t.origin.y), Bits(t.origin.z));
    assert(t.vertices.size() == t.uv0.size());
    for (std::size_t i = 0; i < t.vertices.size(); ++i) {
      const auto &p = t.vertices[i];
      const auto &uv = t.uv0[i];
      std::printf("VERTEX\t%u\t%zu\t%u\t%u\t%u\t%u\t%u\t%u\n", t.layer, i,
                  Bits(p.x), Bits(p.y), Bits(p.z), Bits(uv.x), Bits(uv.y),
                  Bits(uv.z));
    }
    for (std::size_t i = 0; i < t.triangles.size(); ++i)
      std::printf("INDEX\t%u\t%zu\t%u\n", t.layer, i, t.triangles[i]);
    auto layer = reader.LoadStreetsPoorLayer(Source::Geometry, t.layer);
    assert(layer && layer->index == t.layer && layer->depth > t.layer &&
           layer->image.width && layer->image.height &&
           layer->image.rgba.size() ==
               std::size_t(layer->image.width) * layer->image.height * 4);
    std::printf("LAYER\t%u\t%u\t%u\t%u\t%d\t%llu\n", layer->index,
                layer->image.width, layer->image.height, layer->depth,
                layer->graphics_format,
                static_cast<unsigned long long>(Hash(layer->image.rgba)));
  }
  assert(!reader.LoadStreetsPoorLayer(Source::Geometry, UINT32_MAX));
  assert(!reader.LoadStreetsPoorLayer(static_cast<Source>(255), 0));
  assert(
      !reader.LoadStreetsPoorLayer(Source::Background, tiles->front().layer));
  auto secret =
      reader.LoadStreetsPoorLayer(Source::SecretGeometry, tiles->front().layer);
  assert(secret);
  auto bad = source;
  bad.read = [](std::string_view, std::uint64_t, std::uint8_t *, std::size_t) {
    return false;
  };
  Reader broken(bad);
  assert(!broken.LoadStreetsPoorLayout());
  assert(!broken.LoadStreetsPoorLayer(Source::Geometry, 0));
  reader.ClearCache();
  auto fresh = reader.LoadStreetsPoorLayout();
  assert(fresh && fresh->size() == tiles->size());
  std::fprintf(stderr, "native tile geometry/layers, missing ASTC, bad "
                       "source/index and cache reload: PASS\n");
}
