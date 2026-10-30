// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_native_pins.h"
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
    const auto setup = [](Memory &m) {
      using M = Memory;
      auto h = M::H;
      m.object(h + 0xe000, h + 0xf000, "MapMarkersComponent");
      m.put<int>(h + 0xe020, 0);
      m.put<int>(h + 0xe024, 100);
      m.put(h + 0xe028, h + 0x40000);
      m.object(h + 0x40000, h + 0x41000, "Dictionary`2",
               "System.Collections.Generic");
      m.put(h + 0x40018, h + 0x42000);
      m.put<int>(h + 0x40020, 1);
      m.put<int>(h + 0x40028, 0);
      m.put<int>(h + 0x4002c, 2);
      m.put<U64>(h + 0x42018, 1);
      m.put<int>(h + 0x42020, 7);
      m.put<int>(h + 0x42024, -1);
      m.put(h + 0x42028, h + 0x43000);
      m.put(h + 0x42030, h + 0x44000);
      m.object(h + 0x43000, h + 0x45000, "LevelID");
      m.alive(h + 0x43000);
      m.item(h + 0x46000, h + 0x47000, "MapMarkerTypeID", 0,
             "0123456789abcdef0123456789abcdef");
      m.put(h + 0x43018, h + 0x48000);
      m.object(h + 0x44000, h + 0x49000, "LevelMarkersData");
      m.put(h + 0x44010, h + 0x43000);
      m.put(h + 0x44018, h + 0x4a000);
      m.put<int>(h + 0x44020, 8);
      m.object(h + 0x4a000, h + 0x4b000, "List`1",
               "System.Collections.Generic");
      m.put(h + 0x4a010, h + 0x4c000);
      m.put<int>(h + 0x4a018, 1);
      m.put<int>(h + 0x4a01c, 3);
      m.put<U64>(h + 0x4c018, 1);
      m.put(h + 0x4c020, h + 0x4d000);
      m.object(h + 0x4d000, h + 0x4e000, "MapMarkerData");
      m.put<int>(h + 0x4d010, 7);
      m.put<unsigned>(h + 0x4d014, 0x42c80000);
      m.put<unsigned>(h + 0x4d018, 0xc1200000);
      m.put<int>(h + 0x4d01c, 1);
      m.put(h + 0x4d020, h + 0x48000);
      m.bytes(M::B + 0x1c2f4cc, {0xa0, 0x16, 0x40, 0xf9});
      m.bytes(M::B + 0x1c2e8ec, {0x68, 0x22, 0x40, 0xb9, 0x88, 0x12, 0x00, 0xb9,
                                 0x60, 0x0e, 0x40, 0xf9});
      m.bytes(M::B + 0x1c311d4,
              {0xa9, 0x16, 0x00, 0xbd, 0xa8, 0x1a, 0x00, 0xbd});
      m.bytes(M::B + 0x1c311e0, {0xa8, 0x12, 0x00, 0xb9, 0xb4, 0x1e, 0x00, 0xb9,
                                 0xb3, 0x12, 0x00, 0xf9});
      m.bytes(M::B + 0x1e0fff0,
              {0x00, 0x0c, 0x40, 0xf9, 0xc0, 0x03, 0x5f, 0xd6});
    };
    Memory m;
    setup(m);
    auto id = m.identity();
    auto source = m.source();
    const auto sample = [&]() {
      belmont_native_pins::Decoder d(m);
      return d.Sample(id, source);
    };
    auto s = sample();
    Check(s.ready && s.pins.size() == 1 && s.pins[0].id == 7 &&
              s.pins[0].nav_index == 1 && s.pins[0].x == 100 &&
              s.pins[0].y == -10 &&
              s.pins[0].type_guid == "0123456789abcdef0123456789abcdef",
          "native pin fields");
    auto wrong = id;
    wrong.build[0] ^= 1;
    unsigned n = m.reads;
    belmont_native_pins::Decoder d(m);
    Check(!d.Sample(wrong, source).ready && m.reads == n,
          "wrong build zero reads");
    auto loading = source;
    loading.room_loading = 1;
    n = m.reads;
    Check(!d.Sample(id, loading).ready && m.reads == n, "loading zero reads");
    auto stale = source;
    stale.entity += 0x100;
    Check(!d.Sample(id, stale).ready, "stale entity");
    m.put<unsigned>(Memory::H + 0x4d014, 0x7fc00000);
    s = sample();
    Check(!s.ready && s.pins.empty(), "NaN clears");
    setup(m);
    m.put<int>(Memory::H + 0x4d01c, -1);
    Check(!sample().ready, "negative nav");
    setup(m);
    m.put<int>(Memory::H + 0x4d010, 8);
    Check(!sample().ready, "unallocated ID");
    setup(m);
    m.put<int>(Memory::H + 0x4a018, 2);
    Check(!sample().ready, "list over capacity");
    setup(m);
    m.put<int>(Memory::H + 0x4a018, 2);
    m.put<U64>(Memory::H + 0x4c018, 2);
    m.put(Memory::H + 0x4c028, Memory::H + 0x4d000);
    Check(!sample().ready, "duplicate pin ID");
    setup(m);
    m.put<std::uint16_t>(Memory::H + 0x48014, 'z');
    Check(!sample().ready, "malformed type GUID");
    setup(m);
    m.put<int>(Memory::H + 0x40020, 2);
    m.put<U64>(Memory::H + 0x42018, 2);
    m.put<int>(Memory::H + 0x42038, 8);
    m.put<int>(Memory::H + 0x4203c, -1);
    m.put(Memory::H + 0x42040, Memory::H + 0x43000);
    m.put(Memory::H + 0x42048, Memory::H + 0x44000);
    Check(!sample().ready, "duplicate level GUID");
    setup(m);
    m.put<int>(Memory::H + 0x40028, 1);
    Check(!sample().ready, "dictionary live count");
    setup(m);
    m.data[Memory::B + 0x1c311e0] ^= 1;
    Check(!sample().ready, "instruction mismatch");
    setup(m);
    unsigned seen = 0;
    m.observe = [&](U64 p) {
      if (p == Memory::H + 0x4a01c && ++seen == 2)
        m.put<int>(p, 4);
    };
    s = sample();
    Check(!s.ready && s.pins.empty(), "list race clears");
    m.observe = {};
    setup(m);
    seen = 0;
    m.observe = [&](U64 p) {
      if (p == Memory::H + 0x4d014 && ++seen == 2)
        m.put<unsigned>(p, 0x42c00000);
    };
    Check(!sample().ready, "position race");
    m.observe = {};
    setup(m);
    m.put<int>(Memory::H + 0x4a018, 0);
    s = sample();
    Check(s.ready && s.pins.empty(), "empty level list ready");
    m.put<int>(Memory::H + 0x40020, 0);
    m.put<U64>(Memory::H + 0x40018, 0);
    s = sample();
    Check(s.ready && s.pins.empty(), "empty native dictionary ready");
    std::cout << "native pin reader tests PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
