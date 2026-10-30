// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
// Include the implementation to exercise the private serialized recipe with
// generated malformed inputs; no game geometry fixtures are shipped.
#include "belmont_map_fog.cpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
using namespace belmont_map_fog;
namespace {
struct Bytes {
  std::vector<std::uint8_t> b;
  void U32(std::uint32_t n) {
    for (int i = 0; i < 4; ++i)
      b.push_back(static_cast<std::uint8_t>(n >> (i * 8)));
  }
  void I64(std::int64_t n) {
    U32(static_cast<std::uint32_t>(n));
    U32(static_cast<std::uint32_t>(std::uint64_t(n) >> 32));
  }
  void Float(float f) {
    std::uint32_t n{};
    std::memcpy(&n, &f, 4);
    U32(n);
  }
  void Text(const std::string &s) {
    U32(s.size());
    b.insert(b.end(), s.begin(), s.end());
    while (b.size() % 4)
      b.push_back(0);
  }
  void Ref(std::int64_t n) {
    U32(0);
    I64(n);
  }
};
Bytes Synthetic(const std::string &klass = "ExplorationShapeBox",
                std::int64_t reference = 9) {
  Bytes b;
  b.Ref(947);
  b.U32(1);
  b.U32(1);
  b.I64(4040120242753893301LL);
  b.Text("");
  b.U32(1);
  b.Text("synthetic_room");
  b.U32(1);
  b.Text("synthetic_room");
  b.U32(2);
  b.Ref(12);
  b.U32(1);
  b.I64(reference);
  b.U32(0);
  b.U32(0);
  b.U32(2);
  b.U32(1);
  b.I64(9);
  b.Text(klass);
  b.Text("");
  b.Text("Assembly-CSharp");
  for (float f : {-5.f, 2.f, 4.f, 6.f, -3.f, 5.f, 4.f, 6.f})
    b.Float(f);
  return b;
}
} // namespace
int main(int argc, char **argv) {
  auto good = Synthetic();
  auto parsed = DecodeHolder(good.b);
  assert(parsed && parsed->size() == 1 && (*parsed)[0].zones[0].id == 2 &&
         (*parsed)[0].zones[0].shapes[0].size.x == 4);
  auto quad = Synthetic("ExplorationShapeQuad");
  quad.b.resize(quad.b.size() - 16); // retain bounds; replace box payload.
  quad.U32(4);
  for (float f : {-5.f, 2.f, -1.f, 8.f, -1.f, 2.f, -1.f, 2.f})
    quad.Float(f);
  auto q = DecodeHolder(quad.b);
  assert(q && (*q)[0].zones[0].shapes[0].kind == ShapeKind::Quad &&
         (*q)[0].zones[0].shapes[0].vertices[1].y == 8);
  quad.b[quad.b.size() - 36] = 3;
  assert(!DecodeHolder(quad.b));
  for (std::size_t n = 0; n < good.b.size(); ++n)
    assert(!DecodeHolder(std::span(good.b.data(), n)));
  auto bad = good;
  bad.b.push_back(0);
  assert(!DecodeHolder(bad.b));
  assert(!DecodeHolder(Synthetic("FutureShape").b));
  assert(!DecodeHolder(Synthetic("ExplorationShapeBox", 10).b));
  bad = good;
  bad.b[32] = 255;
  assert(!DecodeHolder(bad.b));
  bad = good;
  bad.b[bad.b.size() - 4] = 0;
  bad.b[bad.b.size() - 3] = 0;
  bad.b[bad.b.size() - 2] = 128;
  bad.b[bad.b.size() - 1] = 127;
  assert(!DecodeHolder(bad.b)); // infinity
  bad = good;
  bad.b[bad.b.size() - 4] = 0;
  bad.b[bad.b.size() - 3] = 0;
  bad.b[bad.b.size() - 2] = 128;
  bad.b[bad.b.size() - 1] = 191;
  assert(!DecodeHolder(bad.b)); // negative box size
  ce_unity::RangeReader missing{
      [](std::string_view) -> std::optional<std::uint64_t> { return {}; },
      [](std::string_view, std::uint64_t, std::uint8_t *, std::size_t) {
        return false;
      }};
  Reader absent(missing);
  assert(!absent.LoadStreetsPoor());
  if (argc > 1) {
    auto source = ce_unity::MakeDirectoryRangeReader(argv[1]);
    Reader reader(source);
    auto d = reader.LoadStreetsPoor();
    assert(d);
    std::size_t zones = 0, shapes = 0;
    for (const auto &r : d->rooms)
      for (const auto &z : r.zones) {
        ++zones;
        shapes += z.shapes.size();
      }
    assert(d->rooms.size() == 49 && zones == 225 && shapes == 817 &&
           d->districts.size() == 30 && d->tiles.size() == 24);
    auto first =
        std::find_if(d->rooms.begin(), d->rooms.end(), [](const Room &r) {
          return r.id.ends_with("FirstRoom_uid73");
        });
    assert(first != d->rooms.end() && first->zones.size() == 4 &&
           first->zones[0].shapes.size() == 5);
    assert(d->empty_neighbor_thickness == 3 && d->tile_bounds_inflate == 8 &&
           d->tiles[0].smoothness == 3);
    reader.ClearCache();
    assert(reader.LoadStreetsPoor());
    bool fail = false;
    ce_unity::RangeReader intermittent{
        source.size,
        [&](std::string_view p, std::uint64_t o, std::uint8_t *b,
            std::size_t n) { return !fail && source.read(p, o, b, n); }};
    Reader changing(intermittent);
    assert(changing.LoadStreetsPoor());
    fail = true;
    changing.ClearCache();
    assert(!changing.LoadStreetsPoor());
    std::cout << "actual source: 49 rooms / 225 zones / 817 shapes / 30 "
                 "districts / 24 SDG settings PASS\n";
  }
  std::cout << "malformed/truncated/unknown schema, unresolved ref, invalid "
               "floats, source failure and cache clearing PASS\n";
}
