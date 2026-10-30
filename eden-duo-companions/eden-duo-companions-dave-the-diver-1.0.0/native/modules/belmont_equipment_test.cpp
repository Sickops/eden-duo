// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_equipment.h"
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
using belmont::U64;
void Check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
struct Memory {
  static constexpr U64 B = 0x80000000, H = 0x1200000000;
  std::map<U64, std::uint8_t> data;
  std::function<void(U64)> observe;
  unsigned reads{};
  template <class T> void put(U64 p, T v) {
    for (unsigned i = 0; i < sizeof(T); ++i)
      data[p + i] = std::uint8_t(U64(v) >> (8 * i));
  }
  void bytes(U64 p, std::initializer_list<std::uint8_t> b) {
    for (auto x : b)
      data[p++] = x;
  }
  void text(U64 p, const char *s) {
    do {
      data[p++] = *s;
    } while (*s++);
  }
  void klass(U64 k, const char *s, const char *ns = "") {
    put(k + 0x10, k + 0x201);
    put(k + 0x18, k + 0x301);
    text(k + 0x201, s);
    text(k + 0x301, ns);
  }
  void object(U64 p, U64 k, const char *s, const char *ns = "") {
    put(p, k);
    klass(k, s, ns);
  }
  void alive(U64 p) {
    put(p + 0x10, p + 0x800000);
    put<std::uint8_t>(p + 0x800000, 1);
  }
  void flag(U64 r, U64 k, const char *s, const char *ns) {
    put(B + r, k);
    klass(k, s, ns);
    put(k + 0xb8, k + 0x400);
    put<std::uint8_t>(k + 0x400, 0);
  }
  void item(U64 p, U64 k, const char *kind, int type, const char *id) {
    object(p, k, kind);
    alive(p);
    put<int>(p + 0x28, type);
    put(p + 0x20, p + 0x2000);
    object(p + 0x2000, H + 0x28000, "String", "System");
    put<int>(p + 0x2010, int(std::strlen(id)));
    for (unsigned i = 0; i < std::strlen(id); ++i)
      put<std::uint16_t>(p + 0x2014 + 2 * i, std::uint16_t(id[i]));
  }
  bool operator()(U64 p, void *out, std::size_t n) {
    ++reads;
    if (observe)
      observe(p);
    for (std::size_t i = 0; i < n; ++i) {
      auto q = data.find(p + i);
      if (q == data.end())
        return false;
      static_cast<std::uint8_t *>(out)[i] = q->second;
    }
    return true;
  }
  Memory() {
    bytes(B + 0x1f52c90, {0x00, 0x28, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1f56f50, {0x00, 0x14, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1f56f60, {0x00, 0x18, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1f56f70, {0x00, 0x1c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1f56f80, {0x00, 0x20, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1cabc40, {0x00, 0x10, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1cab950, {0x00, 0x28, 0x40, 0xb9, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1c23194, {0x88, 0x16, 0x40, 0xf9, 0x88, 0x0e, 0x00, 0xb4, 0x09,
                          0x15, 0x40, 0xf9});
    bytes(B + 0x1f570a8, {0x75, 0x22, 0x40, 0xf9, 0xf5, 0x04, 0x00, 0xb4, 0xa8,
                          0x0e, 0x40, 0xf9});
    put(B + 0x6d42540, H + 0x1000);
    klass(H + 0x1000, "GameStateManager");
    put(H + 0x10b8, H + 0x1400);
    put(H + 0x1400, H + 0x2000);
    put(H + 0x2000, H + 0x1000);
    put(H + 0x20b0, H + 0x3000);
    object(H + 0x3000, H + 0x4000, "Entity", "MotherBase.EntityFramework");
    alive(H + 0x3000);
    put<std::uint8_t>(H + 0x3020, 0);
    put<std::uint8_t>(H + 0x32c8, 1);
    put(H + 0x2020, H + 0x5000);
    put(H + 0x2068, H + 0x6000);
    put(H + 0x20a0, H + 0x7000);
    put(H + 0x7010, H + 0x8000);
    put<int>(H + 0x7018, 1);
    put<int>(H + 0x701c, 7);
    put<U64>(H + 0x8018, 1);
    put(H + 0x8020, H + 0x9000);
    put(H + 0x9018, H + 0x5000);
    flag(0x6d3f780, H + 0xa000, "ApplicationFlowManager",
         "MotherBase.ApplicationFlowManager");
    flag(0x6d46228, H + 0xb000, "RoomManagerSystem", "RoomManager");
    put(H + 0x3030, H + 0xc000);
    put(H + 0xc018, H + 0xd000);
    put<int>(H + 0xc020, 1);
    put<int>(H + 0xc028, 0);
    put<int>(H + 0xc02c, 2);
    put<U64>(H + 0xd018, 1);
    put<int>(H + 0xd020, 4);
    put<int>(H + 0xd024, -1);
    put<int>(H + 0xd028, 99);
    put(H + 0xd030, H + 0xe000);
    object(H + 0xe000, H + 0xf000, "InventoryComponent");
    alive(H + 0xe000);
    put(H + 0xe028, H + 0x10000);
    object(H + 0x10000, H + 0x11000, "Inventory");
    put(H + 0x10050, H + 0x3000);
    put(H + 0x10028, H + 0x12000);
    put<U64>(H + 0x10030, 0);
    put<U64>(H + 0x10038, 0);
    put(H + 0x10040, H + 0x15000);
    put<U64>(H + 0x15018, 2);
    put<U64>(H + 0x15020, 0);
    put(H + 0x15028, H + 0x16000);
    item(H + 0x12000, H + 0x13000, "WeaponVampireKillerItemData", 1,
         "Weapon_Test");
    item(H + 0x16000, H + 0x17000, "CharmItemData", 3, "Charm_Test");
  }
  belmont::Identity identity() {
    return {belmont::Title, B, 0x730c000, belmont::Build};
  }
  belmont::Snapshot source() {
    belmont::Snapshot s;
    s.supported = s.coherent = s.manager_ready = s.entity_ready =
        s.components_ready = true;
    s.application_loading = s.room_loading = s.loading_identity = 0;
    s.initial_identity = 1;
    s.manager = H + 0x2000;
    s.entity = H + 0x3000;
    s.active = H + 0x5000;
    return s;
  }
};
} // namespace
int main() {
  try {
    Memory m;
    belmont_equipment::Decoder d(m);
    auto id = m.identity();
    auto source = m.source();
    auto s = d.Sample(id, source);
    Check(s.ready && s.coherent && s.weapon.id == "Weapon_Test" &&
              s.charms.size() == 2 && !s.charms[0].equipped &&
              s.charms[1].id == "Charm_Test" && !s.spell.equipped &&
              !s.skin.equipped,
          "equipped slots");
    {
      auto bad = id;
      bad.build[0] ^= 1;
      auto n = m.reads;
      s = d.Sample(bad, source);
      Check(!s.ready && m.reads == n, "wrong build zero reads");
    }
    {
      auto bad = source;
      bad.room_loading = 1;
      auto n = m.reads;
      s = d.Sample(id, bad);
      Check(!s.ready && s.weapon.id.empty() && m.reads == n,
            "loading clears without reads");
    }
    m.data[Memory::B + 0x1f56f50] ^= 1;
    s = d.Sample(id, source);
    Check(s.reason == "accessor_mismatch" && s.charms.empty(),
          "instruction pin");
    m.data[Memory::B + 0x1f56f50] ^= 1;
    m.put(Memory::H + 0x10050, Memory::H + 0x3008);
    s = d.Sample(id, source);
    Check(!s.ready && s.weapon.id.empty(), "foreign owner");
    m.put(Memory::H + 0x10050, source.entity);
    m.put<U64>(Memory::H + 0x15018, 33);
    s = d.Sample(id, source);
    Check(!s.ready, "bounded slots");
    m.put<U64>(Memory::H + 0x15018, 2);
    m.put<int>(Memory::H + 0x12028, 3);
    s = d.Sample(id, source);
    Check(!s.ready, "item type mismatch");
    m.put<int>(Memory::H + 0x12028, 1);
    m.put<U64>(Memory::H + 0x12010, 0);
    s = d.Sample(id, source);
    Check(!s.ready, "destroyed item");
    m.alive(Memory::H + 0x12000);
    m.put<std::uint8_t>(Memory::H + 0xb400, 1);
    s = d.Sample(id, source);
    Check(s.reason == "stale_source", "loading transition after source");
    m.put<std::uint8_t>(Memory::H + 0xb400, 0);
    m.put(Memory::H + 0x20b0, Memory::H + 0x3008);
    s = d.Sample(id, source);
    Check(s.reason == "stale_source", "entity transition");
    m.put(Memory::H + 0x20b0, source.entity);
    m.put<int>(Memory::H + 0x14010, 129);
    s = d.Sample(id, source);
    Check(!s.ready, "bounded ID length");
    m.put<int>(Memory::H + 0x14010, 11);
    m.put<std::uint16_t>(Memory::H + 0x14014, 0xd800);
    s = d.Sample(id, source);
    Check(!s.ready, "malformed identifier");
    m.put<std::uint16_t>(Memory::H + 0x14014, 'W');
    m.put<int>(Memory::H + 0xc020, 2);
    m.put<U64>(Memory::H + 0xd018, 2);
    m.put<int>(Memory::H + 0xd038, 5);
    m.put<int>(Memory::H + 0xd03c, -1);
    m.put<int>(Memory::H + 0xd040, 100);
    m.put(Memory::H + 0xd048, Memory::H + 0xe000);
    s = d.Sample(id, source);
    Check(!s.ready, "duplicate component class");
    m.put<int>(Memory::H + 0xc020, 1);
    m.put<U64>(Memory::H + 0xd018, 1);
    unsigned seen{};
    m.observe = [&](U64 at) {
      if (at == Memory::H + 0x10050 && ++seen == 2)
        m.put(Memory::H + 0x10050, Memory::H + 0x3008);
    };
    s = d.Sample(id, source);
    Check(s.reason == "incoherent" && s.weapon.id.empty() && s.charms.empty(),
          "owner race clears all");
    m.observe = {};
    m.put(Memory::H + 0x10050, source.entity);
    // A native pause is allowed while retaining exactly the same live entity.
    source.initial_identity = 0;
    source.paused = 1;
    source.active = Memory::H + 0x19000;
    m.put(Memory::H + 0x9018, source.active);
    m.object(source.active, Memory::H + 0x1a000, "EditorParameteredGameState");
    m.alive(source.active);
    for (unsigned i = 0; i < 14; ++i)
      m.put<std::uint8_t>(source.active + 0x18 + i, 0);
    m.put<std::uint8_t>(source.active + 0x18, 1);
    m.put<std::uint8_t>(source.active + 0x19, 1);
    m.put<std::uint8_t>(source.active + 0x23, 1);
    s = d.Sample(id, source);
    Check(s.ready, "native pause retains equipment");
    m.put<std::uint8_t>(source.active + 0x23, 0);
    s = d.Sample(id, source);
    Check(!s.ready, "non-pause state rejected");
    std::cout << "equipment reader tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
