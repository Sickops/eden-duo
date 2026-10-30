// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_equipment_stats.h"
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
    bytes(B + 0x1e2f1b0, {0x1f, 0x14, 0x00, 0x71, 0x48, 0x02, 0x00, 0x54});
    bytes(B + 0x52c9144, {0x00, 0x03, 0x05, 0x07, 0x01, 0x09});
    bytes(B + 0x1e2f1f8, {0x56, 0x5b, 0xf4, 0x17});
    bytes(B + 0x1b45f50, {0x10, 0x89, 0x02, 0x90, 0x11, 0x36, 0x40, 0xf9, 0x10, 0xa2, 0x01, 0x91, 0x20, 0x02, 0x1f, 0xd6});

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
namespace {
struct Fixture : Memory {
  static constexpr U64 Component = H + 0x40000, Dict = H + 0x42000,
                       Entries = H + 0x43000;
  std::map<std::string, U64> objects, lists;
  Fixture() {
    bytes(B + 0x4deae1c, {0x81, 0xf6, 0x47, 0xf9, 0xe1, 0x00, 0x00, 0xb4});
    bytes(B + 0x1279e54,
          {0x00, 0x09, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4, 0x00, 0x41,
           0x20, 0x1e, 0xe1, 0x03, 0x1f, 0xaa, 0x1d, 0xed, 0xd7, 0x97});
    bytes(B + 0x8752ec,
          {0x08, 0x24, 0x40, 0xf9, 0xf4, 0x03, 0x00, 0xaa, 0x1f, 0x05,
           0x00, 0x71, 0x81, 0x00, 0x00, 0x54, 0x88, 0x1e, 0x40, 0xf9});
    bytes(B + 0x87536c,
          {0xa0, 0x12, 0x00, 0x54, 0x8c, 0x03, 0x80, 0x52, 0x0a, 0x29,
           0x0c, 0x9b, 0x4a, 0x41, 0x5e, 0xb8, 0x7f, 0x01, 0x2a, 0x6a});
    bytes(B + 0x87539c, {0x88, 0x5a, 0x40, 0xb9, 0x68, 0x06, 0x00, 0x34,
                         0x1f, 0x05, 0x00, 0x71, 0x60, 0x0c, 0x00, 0x54,
                         0x1f, 0x09, 0x00, 0x71, 0x81, 0x07, 0x00, 0x54});
    bytes(B + 0x8753dc, {0x89, 0x5e, 0x40, 0xb9, 0x69, 0x04, 0x00, 0x34,
                         0x3f, 0x05, 0x00, 0x71, 0xe0, 0x08, 0x00, 0x54,
                         0x3f, 0x09, 0x00, 0x71, 0x81, 0x05, 0x00, 0x54});
    bytes(B + 0x875744, {0x0b, 0x05, 0x00, 0x51, 0xf8, 0xff, 0xff, 0x17});
    bytes(B + 0x1e307ec, {0xa0, 0x16, 0x40, 0xf9, 0x20, 0x02, 0x00, 0xb4,
                          0xa0, 0x1f, 0x40, 0xbd, 0xe1, 0x03, 0x1f, 0xaa,
                          0x81, 0xe9, 0xbe, 0x94, 0x60, 0x02, 0x00, 0xbd});
    bytes(B + 0x1effc44, {0x62, 0x52, 0x40, 0xb9, 0xe3, 0x03, 0x1f, 0xaa,
                          0x61, 0x3a, 0x40, 0xf9, 0x30, 0xd0, 0xfc, 0x97,
                          0x88, 0x02, 0x00, 0x0b, 0xa8, 0x1f, 0x00, 0xb9});
    bytes(B + 0x591b48, {0x08, 0x00, 0x40, 0xf9, 0x00, 0x81, 0x00, 0x91, 0xf9,
                         0xa4, 0xff, 0x17});
    bytes(B + 0x57af88, {0xa8, 0x0f, 0x40, 0xf9, 0xe2, 0x03, 0x00, 0xaa, 0xa1,
                         0x63, 0x00, 0x91, 0x08, 0x08, 0x00, 0xf9});
    bytes(B + 0x1e295d4, {0x80, 0x16, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4});
    bytes(B + 0x1e33bdc, {0x89, 0x1a, 0x40, 0xbd});
    bytes(B + 0x1e33c30, {0xf5, 0x17, 0x40, 0xf9, 0xf5, 0x03, 0x00, 0xb4, 0xa8,
                          0x02, 0x40, 0xf9, 0x96, 0x0a, 0x40, 0xf9});
    bytes(B + 0x1e33b20, {0x00, 0x18, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x439b424, {0x28, 0x08, 0x40, 0xf9, 0x08, 0x00, 0x00, 0xf9, 0xc0,
                          0x03, 0x5f, 0xd6});
    bytes(B + 0x1e2eed0, {0x00, 0x14, 0x40, 0xbd, 0x20, 0x00, 0x80, 0x52, 0x40,
                          0x00, 0x00, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1e41610, {0x00, 0x10, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1e2f18c, {0x60, 0x12, 0x40, 0xb9, 0x08, 0x00, 0x00, 0x94});
    bytes(B + 0x1e33d28, {0x82, 0xff, 0xff, 0x97, 0x08, 0x40, 0x20, 0x1e});
    bytes(B + 0x1e29600, {0x22, 0x00, 0x80, 0x52, 0xc3, 0x29, 0x00, 0x14});

    bytes(B + 0x591b48, {0x08, 0x00, 0x40, 0xf9, 0x00, 0x81, 0x00, 0x91, 0xf9,
                         0xa4, 0xff, 0x17});
    bytes(B + 0x57af88, {0xa8, 0x0f, 0x40, 0xf9, 0xe2, 0x03, 0x00, 0xaa, 0xa1,
                         0x63, 0x00, 0x91, 0x08, 0x08, 0x00, 0xf9});
    put<U64>(B + 0x6d34358, 0);
    put<U64>(B + 0x6d34360, 0);
    put<U64>(B + 0x6d34370, 0);
    put<U64>(B + 0x6d343d0, 0);
    put<U64>(B + 0x6d343d8, 0);
    put<U64>(B + 0x6d34598, 0);
    put<U64>(B + 0x6d34728, 0);
    put<U64>(B + 0x6d34730, 0);
    put<U64>(B + 0x6d34870, 0);
    put<U64>(B + 0x6d34a78, 0);
    put<U64>(B + 0x6d34a80, 0);
    put<U64>(B + 0x6d34bb0, 0);
    put<U64>(B + 0x6d34bb8, 0);
    put<U64>(B + 0x6d34dd8, 0);
    put<U64>(B + 0x6d34de0, 0);
    put<U64>(B + 0x6d34de8, 0);
    put<U64>(B + 0x6d34df0, 0);
    put<U64>(B + 0x6d34df8, 0);
    put<U64>(B + 0x6d34e00, 0);
    put<U64>(B + 0x6d34e08, 0);
    put<U64>(B + 0x6d34e10, 0);
    put<U64>(B + 0x6d34e18, 0);
    put<U64>(B + 0x6d34e20, 0);
    put<U64>(B + 0x6d34e28, 0);
    put<U64>(B + 0x6d34e30, 0);
    put<U64>(B + 0x6d34e38, 0);
    put<U64>(B + 0x6d35200, 0);
    put<U64>(B + 0x6d352c0, 0);
    put<U64>(B + 0x6d353a8, 0);
    put<U64>(B + 0x6d353d0, 0);
    put<U64>(B + 0x6d353d8, 0);
    put<U64>(B + 0x6d353e0, 0);
    put<U64>(B + 0x6d353e8, 0);
    put<U64>(B + 0x6d353f0, 0);
    put<U64>(B + 0x6d354c8, 0);
    put<U64>(B + 0x6d357d0, 0);
    put<U64>(B + 0x6d357d8, 0);
    put<U64>(B + 0x6d357e0, 0);
    put<U64>(B + 0x6d357e8, 0);
    put<U64>(B + 0x6d357f0, 0);
    put<U64>(B + 0x6d357f8, 0);
    put<U64>(B + 0x6d35c28, 0);
    put<U64>(B + 0x6d35c30, 0);
    put<U64>(B + 0x6d35c38, 0);
    put<U64>(B + 0x6d35c40, 0);
    put<U64>(B + 0x6d35c48, 0);
    put<U64>(B + 0x6d35d18, 0);
    put<U64>(B + 0x6d35e68, 0);
    put<U64>(B + 0x6d35ed8, 0);
    put<U64>(B + 0x6d362b8, 0);
    put<U64>(B + 0x6d36348, 0);
    bytes(B + 0x1e295d4, {0x80, 0x16, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4});
    bytes(B + 0x1e33bdc, {0x89, 0x1a, 0x40, 0xbd});
    bytes(B + 0x1e33c30, {0xf5, 0x17, 0x40, 0xf9, 0xf5, 0x03, 0x00, 0xb4, 0xa8,
                          0x02, 0x40, 0xf9, 0x96, 0x0a, 0x40, 0xf9});
    bytes(B + 0x1e33b20, {0x00, 0x18, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x439b424, {0x28, 0x08, 0x40, 0xf9, 0x08, 0x00, 0x00, 0xf9, 0xc0,
                          0x03, 0x5f, 0xd6});
    bytes(B + 0x1e2eed0, {0x00, 0x14, 0x40, 0xbd, 0x20, 0x00, 0x80, 0x52, 0x40,
                          0x00, 0x00, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1e41610, {0x00, 0x10, 0x40, 0xbd, 0xc0, 0x03, 0x5f, 0xd6});
    bytes(B + 0x1e2f18c, {0x60, 0x12, 0x40, 0xb9, 0x08, 0x00, 0x00, 0x94});
    bytes(B + 0x1e33d28, {0x82, 0xff, 0xff, 0x97, 0x08, 0x40, 0x20, 0x1e});
    bytes(B + 0x1e29600, {0x22, 0x00, 0x80, 0x52, 0xc3, 0x29, 0x00, 0x14});
    put<int>(H + 0xc020, 2);
    put<U64>(H + 0xd018, 2);
    put<U64>(H + 0xd028, 99);
    put<int>(H + 0xd038, 5);
    put<int>(H + 0xd03c, -1);
    put<U64>(H + 0xd040, 100);
    put(H + 0xd048, Component);
    object(Component, H + 0x45000, "StatsStateComponent");
    alive(Component);
    put(Component + 0x28, Dict);
    put(Dict + 0x18, Entries);
    put<int>(Dict + 0x20, 9);
    put<int>(Dict + 0x28, 0);
    put<int>(Dict + 0x2c, 1);
    put<U64>(Entries + 0x18, 9);
    add("PowerAttack", 0, 0x6d357e0, 10.8f);
    add("PowerAttackBonus", 1, 0x6d357e8, 2.5f);
    add("PowerDefense", 2, 0x6d357f0, 7.9f);
    add("PowerDefenseBonus", 3, 0x6d357f8, 0.f);
    add("HealthMax", 4, 0x6d34df0, 100.9f);
    add("SpellManaMax", 5, 0x6d35c38, 40.5f);
    add("Level", 6, 0x6d352c0, 1.5f);
    add("HealthCurrent", 7, 0x6d34de8, 89.5f);
    add("SpellManaCurrent", 8, 0x6d35c30, 12.2f);
  }
  void add(const char *name, unsigned i, U64 rva, float value) {
    const U64 type = H + 0x50000 + i * 0x1000, key = H + 0x60000 + i * 0x100,
              obj = H + 0x70000 + i * 0x100, list = H + 0x80000 + i * 0x100,
              at = Entries + 0x20 + 24 * i;
    klass(type - 0x20, name);
    put(type - 0x20 + 0x78, type - 0x20);
    put(type - 0x20 + 0x58, H + 0x5f000);
    klass(H + 0x5f000, "AFloatStat");
    put(B + rva, type);
    object(key, H + 0x46000, "RuntimeType", "System");
    put(key + 0x10, type);
    object(obj, H + 0x47000, "StatObject");
    put(obj + 0x10, key);
    std::uint32_t bits{};
    std::memcpy(&bits, &value, 4);
    put(obj + 0x18, bits);
    put(obj + 0x20, list);
    object(list, H + 0x48000, "List`1", "System.Collections.Generic");
    put<U64>(list + 0x10, 0);
    put<int>(list + 0x18, 0);
    put<int>(list + 0x1c, 1);
    put<int>(at, int(i));
    put<int>(at + 4, -1);
    put(at + 8, key);
    put(at + 16, obj);
    objects[name] = obj;
    lists[name] = list;
  }
  void f32(U64 p, float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, 4);
    put(p, bits);
  }
  void modifier(const char *target, const char *kind) {
    const U64 list = lists[target], items = H + 0x90000, p = H + 0x91000;
    object(p, H + 0x92000, kind);
    put(list + 0x10, items);
    put<int>(list + 0x18, 1);
    put<int>(list + 0x1c, 2);
    put<U64>(items + 0x18, 1);
    put(items + 0x20, p);
  }
};
} // namespace
int main() {
  using belmont_equipment_stats::UpgradePieces;
  using belmont_equipment_stats::Value;
  const Value six{true,6,"ready"}, zero{true,0,"ready"}, four{true,4,"ready"},
      two{true,2,"ready"}, three{true,3,"ready"};
  for(unsigned i=0;i<6;++i) Check(UpgradePieces(six,zero,four,i).value==4,"late full upgrade icons");
  Check(UpgradePieces(two,three,four,0).value==4 && UpgradePieces(two,three,four,2).value==3 && UpgradePieces(two,three,four,3).value==0,"completed partial and outline native upgrade states");
  Check(!UpgradePieces({},zero,four,0).ready && !UpgradePieces(two,four,four,2).ready && !UpgradePieces(six,three,four,5).ready,"missing or inconsistent fragments never invent display");
  try {
    Fixture f;
    belmont_equipment_stats::Decoder d(f);
    auto id = f.identity();
    auto base = f.source();
    auto s = d.Sample(id, base);
    Check(s.ready && s.coherent && s.atk.ready && s.atk.value == 10 &&
              s.atk_bonus.value == 2 && s.atk_display.value == 12 &&
              s.defense.value == 7 && s.max_hp.value == 100 &&
              s.max_mp.value == 40 && s.level.value == 2 &&
              s.current_hp.value == 89,
          "native per-display rounding and unmodified finals");
    auto wrong = id;
    wrong.build[0] ^= 1;
    auto reads = f.reads;
    s = d.Sample(wrong, base);
    Check(!s.ready && f.reads == reads, "wrong build no reads");
    auto loading = base;
    loading.room_loading = 1;
    reads = f.reads;
    s = d.Sample(id, loading);
    Check(!s.ready && f.reads == reads && !s.atk.ready, "loading clears");
    f.modifier("PowerAttack", "OperationBruteValueStatModifier");
    f.put<int>(Memory::H + 0x91010, 0);
    f.f32(Memory::H + 0x91014, 2.7f);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 13 && s.atk_display.value == 15,
          "ordered derived add differs from base");
    f.put<int>(Memory::H + 0x91010, 2);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 29, "derived multiply");
    f.put<int>(Memory::H + 0x91010, 3);
    f.f32(Memory::H + 0x91014, 0);
    s = d.Sample(id, base);
    Check(!s.atk.ready && s.level.ready, "division zero hides affected field");
    f.put<int>(Memory::H + 0x91010, 5);
    f.f32(f.objects["PowerAttack"] + 0x18, 24.f);
    f.f32(Memory::H + 0x91014, 4.f);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 0, "native full upgrade fragment total modulo merge count");
    f.f32(f.objects["PowerAttack"] + 0x18, 27.f);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 3, "native partial fragment remainder");
    f.f32(f.objects["PowerAttack"] + 0x18, -27.f);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == -3, "native fmodf keeps dividend sign rather than positive modulus");
    f.f32(Memory::H + 0x91014, 0.f);
    s = d.Sample(id, base);
    Check(!s.atk.ready && s.atk.reason == "modulo_by_zero" && s.level.ready,
          "zero modulo hides only affected field");
    f.f32(f.objects["PowerAttack"] + 0x18, 10.8f);
    f.modifier("PowerAttack", "UseStatModifier");
    f.put<int>(Memory::H + 0x91010, 0);
    f.object(Memory::H + 0x93000, Memory::H + 0x94000, "Level");
    f.put(Memory::H + 0x91018, Memory::H + 0x93000);
    f.put<int>(Memory::H + 0x91020, 0);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 12,
          "recursive final stat uses float before UI rounding");
    f.klass(Memory::H + 0x94000, "PowerAttack");
    s = d.Sample(id, base);
    Check(!s.atk.ready && s.atk.reason == "dependency_cycle" && s.level.ready,
          "cycle bounded unavailable");
    f.modifier("PowerAttack", "UseStatCurveFormulaStatModifier");
    s = d.Sample(id, base);
    Check(!s.atk.ready &&
              s.atk.reason ==
                  "unsupported_modifier:UseStatCurveFormulaStatModifier" &&
              s.max_hp.ready,
          "unknown formula never treated as base");
    f.modifier("PowerAttack", "ToIntStatModifier");
    f.put<int>(Memory::H + 0x91010, 2);
    s = d.Sample(id, base);
    Check(s.atk.ready && s.atk.value == 11,
          "native ceil modifier before native display floor");
    f.f32(f.objects["HealthMax"] + 0x18,
          std::numeric_limits<float>::infinity());
    s = d.Sample(id, base);
    Check(!s.ready && !s.level.ready, "nonfinite stat clears all");
    f.f32(f.objects["HealthMax"] + 0x18, 100.9f);
    f.put<int>(f.lists["PowerAttack"] + 0x18, 0);
    f.put<int>(Fixture::Dict + 0x20, 10);
    f.put<U64>(Fixture::Entries + 0x18, 10);
    f.add("HealthUpgradeCount", 9, 0x6d34e20, 6.f);
    f.modifier("HealthMax", "UseStatCurveModifier");
    f.put<int>(Memory::H + 0x91010, 0);
    f.object(Memory::H + 0x93000, Memory::H + 0x94000, "HealthUpgradeCount");
    f.put(Memory::H + 0x91018, Memory::H + 0x93000);
    f.put<int>(Memory::H + 0x91020, 0);
    f.object(Memory::H + 0x95000, Memory::H + 0x96000, "AnimationCurve",
             "UnityEngine");
    f.put(Memory::H + 0x91028, Memory::H + 0x95000);
    f.put(Memory::H + 0x95010, Memory::H + 0x97000);
    f.put(Memory::B + 0x6f7efe8, Memory::B + 0x1279e20);
    f.put(Memory::H + 0x97038, Memory::H + 0x98000);
    f.put<U64>(Memory::H + 0x97048, 2);
    f.put<U64>(Memory::H + 0x97050, 4);
    f.put<int>(Memory::H + 0x97058, 2);
    f.put<int>(Memory::H + 0x9705c, 2);
    for (unsigned i = 0; i < 56; ++i)
      f.put<std::uint8_t>(Memory::H + 0x98000 + i, 0);
    f.f32(Memory::H + 0x98000, 0);
    f.f32(Memory::H + 0x98004, 0);
    f.f32(Memory::H + 0x98008, std::numeric_limits<float>::infinity());
    f.f32(Memory::H + 0x9800c, 20);
    f.f32(Memory::H + 0x9801c, 6);
    f.f32(Memory::H + 0x98020, 120);
    f.f32(Memory::H + 0x98024, 20);
    f.f32(Memory::H + 0x98028, std::numeric_limits<float>::infinity());
    f.f32(f.objects["HealthMax"] + 0x18, 100.f);
    s = d.Sample(id, base);
    Check(s.max_hp.ready && s.max_hp.value == 220,
          "native clamp curve end produces true max");
    f.f32(f.objects["HealthUpgradeCount"] + 0x18, 3.f);
    s = d.Sample(id, base);
    Check(s.max_hp.ready && s.max_hp.value == 160,
          "native linear curve interior");
    f.f32(f.objects["HealthUpgradeCount"] + 0x18, -1.f);
    s = d.Sample(id, base);
    Check(s.max_hp.ready && s.max_hp.value == 100, "native clamp before start");
    f.f32(f.objects["HealthUpgradeCount"] + 0x18, 10.f);
    s = d.Sample(id, base);
    Check(s.max_hp.ready && s.max_hp.value == 220, "native clamp after end");
    f.put<int>(Memory::H + 0x98010, 1);
    s = d.Sample(id, base);
    Check(!s.max_hp.ready && s.level.ready,
          "weighted curve refused even at endpoint");
    f.put<int>(Memory::H + 0x98010, 0);
    f.f32(Memory::H + 0x98024, 10.f);
    s = d.Sample(id, base);
    Check(!s.max_hp.ready && s.level.ready, "nonlinear curve refused");
    f.f32(Memory::H + 0x98024, 20.f);
    f.put<int>(Memory::H + 0x9705c, 1);
    s = d.Sample(id, base);
    Check(!s.max_hp.ready, "unknown wrap refused");
    f.put<int>(Memory::H + 0x9705c, 2);
    f.put<U64>(Memory::H + 0x97048, 129);
    s = d.Sample(id, base);
    Check(!s.ready, "key count cap");
    f.put<U64>(Memory::H + 0x97048, 2);
    unsigned curve_reads{};
    f.observe = [&](U64 at) {
      if (at == Memory::H + 0x98000 && ++curve_reads == 2)
        f.f32(Memory::H + 0x98004, 1.f);
    };
    s = d.Sample(id, base);
    Check(!s.ready && s.reason == "incoherent",
          "curve key mutation clears every stat");
    f.observe = {};
    f.f32(Memory::H + 0x98004, 0.f);
    unsigned seen{};
    f.observe = [&](U64 at) {
      if (at == Fixture::Dict + 0x2c && ++seen == 2)
        f.put<int>(at, 2);
    };
    s = d.Sample(id, base);
    Check(!s.ready && s.reason == "incoherent" && !s.atk.ready,
          "dictionary race clears all");
    f.observe = {};
    Check(belmont_equipment_stats::RoundEven(2.5f) == 2 &&
              belmont_equipment_stats::RoundEven(3.5f) == 4 &&
              belmont_equipment_stats::RoundEven(-2.5f) == -2,
          "native banker rounding independent of fenv");
    std::cout << "equipment stats tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
