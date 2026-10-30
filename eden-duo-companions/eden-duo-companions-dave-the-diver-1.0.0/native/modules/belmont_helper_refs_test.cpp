// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_helper_refs.h"
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
using belmont::U64;
using belmont_helper_refs::Slot;
void Check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
struct Memory {
  static constexpr U64 B = 0x80000000, H = 0x1200000000;
  std::map<U64, std::uint8_t> data;
  template <class T> void put(U64 p, T v) {
    for (unsigned i = 0; i < sizeof(T); ++i)
      data[p + i] = std::uint8_t(U64(v) >> (8 * i));
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
  void guid(U64 owner, U64 str, const char *g) {
    put(owner + 0x18, str);
    object(str, H + 0x90000, "String", "System");
    put<std::int32_t>(str + 0x10, 32);
    for (unsigned i = 0; i < 32; ++i)
      put<std::uint16_t>(str + 0x14 + 2 * i, std::uint16_t(g[i]));
  }
  bool operator()(U64 at, void *dst, std::size_t n) const {
    auto *out = static_cast<std::uint8_t *>(dst);
    for (std::size_t i = 0; i < n; ++i) {
      const auto found = data.find(at + i);
      if (found == data.end())
        return false;
      out[i] = found->second;
    }
    return true;
  }
};
constexpr char TypeGuid[] = "0123456789abcdef0123456789abcdef";
constexpr char LevelGuid[] = "794afe09823b0e04dac2044bf092cff2";

void World(Memory &m) {
  using M = Memory;
  // TypeInfo slots.
  m.put(M::B + belmont_helper_refs::MapMarkerDataTypeInfo, M::H + 0x1000);
  m.klass(M::H + 0x1000, "MapMarkerData");
  m.put(M::B + belmont_helper_refs::SpellItemDataTypeInfo, M::H + 0x2000);
  m.klass(M::H + 0x2000, "SpellItemData");
  m.put(M::B + belmont_helper_refs::WeaponItemDataTypeInfo, M::H + 0x3000);
  m.klass(M::H + 0x3000, "WeaponItemData");
  m.put(M::B + belmont_helper_refs::CharmItemDataTypeInfo, M::H + 0x4000);
  m.klass(M::H + 0x4000, "CharmItemData");
  m.put(M::B + belmont_helper_refs::InventoryTypeInfo, M::H + 0x5000);
  m.klass(M::H + 0x5000, "Inventory");
  // UIManager marker display dictionary -> one MapMarkerTypeID.
  const U64 uk = M::H + 0x10000, ui = M::H + 0x11000, dict = M::H + 0x12000,
            list = M::H + 0x13000, items = M::H + 0x14000,
            element = M::H + 0x15000, type = M::H + 0x16000;
  m.put(M::B + belmont_helper_refs::UIManagerTypeInfo, uk);
  m.klass(uk, "UIManager");
  m.put(uk + 0xb8, uk + 0x800);
  m.put(uk + 0x800, ui);
  m.object(ui, uk, "UIManager");
  m.put(ui + 0x58, dict);
  m.put(dict + 0x18, list);
  m.put(list + 0x10, items);
  m.put<std::int32_t>(list + 0x18, 1);
  m.put<U64>(items + 0x18, 4);
  m.put(items + 0x20, element);
  m.put(element + 0x18, type);
  m.object(type, M::H + 0x17000, "MapMarkerTypeID");
  m.guid(type, M::H + 0x18000, TypeGuid);
  // RuntimeServiceLocator -> VarLevelManager -> LevelManager.mainFlowLevelIDs.
  const U64 rk = M::H + 0x20000, loc = M::H + 0x21000, vars = M::H + 0x22000,
            varr = M::H + 0x23000, var = M::H + 0x24000, lm = M::H + 0x25000,
            arr = M::H + 0x26000, level = M::H + 0x27000;
  m.put(M::B + belmont_helper_refs::RuntimeServiceLocatorTypeInfo, rk);
  m.klass(rk, "RuntimeServiceLocator");
  m.put(rk + 0xb8, rk + 0x800);
  m.put(rk + 0x800, loc);
  m.object(loc, rk, "RuntimeServiceLocator");
  m.put(loc + 0x18, vars);
  m.put(vars + 0x10, varr);
  m.put<std::int32_t>(vars + 0x18, 1);
  m.put(varr + 0x20, var);
  m.object(var, M::H + 0x28000, "VarLevelManager");
  m.put(var + 0x28, lm);
  m.object(lm, M::H + 0x29000, "LevelManager");
  m.put(lm + 0x70, arr);
  m.put<U64>(arr + 0x18, 2);
  m.put<U64>(arr + 0x20, 0); // unassigned entry is legal
  m.put(arr + 0x28, level);
  m.object(level, M::H + 0x2a000, "LevelID");
  m.guid(level, M::H + 0x2b000, LevelGuid);
  // Player MapMarkersComponent.
  m.object(M::H + 0x30000, M::H + 0x31000, "MapMarkersComponent");
}
belmont::Identity Id() {
  belmont::Identity id;
  id.title = belmont::Title;
  id.build = belmont::Build;
  id.base = Memory::B;
  id.size = 0x7000000;
  return id;
}
} // namespace

int main() {
  try {
    using M = Memory;
    {
      Memory m;
      World(m);
      belmont_helper_refs::Decoder decoder(m);
      const auto r = decoder.Sample(Id(), M::H + 0x30000);
      Check(r.supported && r.reason == "ready", "ready");
      Check(r.PinsReady(), "pins ready");
      Check(r.Klass(Slot::MapMarkerData) == M::H + 0x1000, "marker data klass");
      Check(r.Klass(Slot::LevelID) == M::H + 0x2a000, "level klass");
      Check(r.Klass(Slot::MapMarkerTypeID) == M::H + 0x17000, "type klass");
      Check(r.Klass(Slot::MapMarkersComponent) == M::H + 0x31000, "component klass");
      Check(r.Klass(Slot::Inventory) == M::H + 0x5000, "inventory klass");
      Check(r.EquipReady(1) && r.EquipReady(2) && r.EquipReady(3), "equip ready");
      Check(!r.EquipReady(0) && !r.EquipReady(4), "equip category bounds");
      Check(r.MarkerType(TypeGuid) == M::H + 0x16000, "type by guid");
      Check(r.Level(LevelGuid) == M::H + 0x27000, "level by guid");
      Check(!r.Level(TypeGuid) && !r.MarkerType(LevelGuid), "no cross match");
    }
    {
      // Unfilled TypeInfo slot (lazy IL2CPP metadata) fails closed per class.
      Memory m;
      World(m);
      m.put<U64>(M::B + belmont_helper_refs::MapMarkerDataTypeInfo, 0x12345);
      m.put<U64>(M::B + belmont_helper_refs::CharmItemDataTypeInfo, 0);
      belmont_helper_refs::Decoder decoder(m);
      const auto r = decoder.Sample(Id(), M::H + 0x30000);
      // ADD needs no module-side MapMarkerData klass (the guest initializes it).
      Check(!r.Klass(Slot::MapMarkerData) && r.PinsReady(), "lazy marker data");
      Check(!r.EquipReady(3) && r.EquipReady(2), "lazy charm only");
      // A live pin object supplies the klass the lazy slot does not.
      m.object(M::H + 0x40000, M::H + 0x41000, "MapMarkerData");
      const auto live = decoder.Sample(Id(), M::H + 0x30000, M::H + 0x40000);
      Check(live.Klass(Slot::MapMarkerData) == M::H + 0x41000, "live marker klass");
      m.klass(M::H + 0x41000, "LevelID");
      Check(!decoder.Sample(Id(), M::H + 0x30000, M::H + 0x40000).Klass(Slot::MapMarkerData), "live marker class check");
    }
    {
      // A wrong class in the registry drops the whole marker-type list.
      Memory m;
      World(m);
      m.klass(M::H + 0x17000, "LevelID");
      belmont_helper_refs::Decoder decoder(m);
      const auto r = decoder.Sample(Id(), M::H + 0x30000);
      Check(r.marker_types.empty() && !r.PinsReady(), "registry class check");
    }
    {
      // No component yet, wrong build.
      Memory m;
      World(m);
      belmont_helper_refs::Decoder decoder(m);
      Check(!decoder.Sample(Id(), 0).PinsReady(), "component required");
      auto id = Id();
      id.build[0] ^= 1;
      Check(decoder.Sample(id, M::H + 0x30000).reason == "unsupported_build", "build");
    }
    std::cout << "belmont_helper_refs_test: pass\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "belmont_helper_refs_test: FAIL " << e.what() << '\n';
    return 1;
  }
}
