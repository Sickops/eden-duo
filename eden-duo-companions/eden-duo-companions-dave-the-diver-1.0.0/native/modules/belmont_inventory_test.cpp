// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_inventory.h"
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
    bytes(B + 0x3f86180, {0x95, 0x4e, 0x40, 0xf9, 0x08, 0xe0, 0x40, 0xb9, 0x95,
                          0x4e, 0x09, 0xa9});
    bytes(B + 0x1ef46f4, {0x75, 0xda, 0x00, 0xf9});
    bytes(B + 0x1ef47dc,
          {0x60, 0x62, 0x40, 0xf9, 0xe0, 0x03, 0x00, 0xb4, 0xc8, 0x67,
           0x02, 0xd0, 0x61, 0x9a, 0x41, 0xb9, 0x08, 0xf1, 0x42, 0xf9});
    bytes(B + 0x1ef4800, {0x00, 0x1c, 0x40, 0xf9, 0xc0, 0x02, 0x00, 0xb4, 0xe1,
                          0x03, 0x1f, 0xaa, 0xd5, 0x9e, 0x00, 0x94});
    bytes(B + 0x1ef3a5c, {0x62, 0xa2, 0x41, 0xb9, 0x01, 0x2d, 0x40, 0xf9, 0x87,
                          0x83, 0x01, 0x94});
    bytes(B + 0x1ef4ec4, {0x60, 0x92, 0x40, 0xf9, 0x7f, 0xda, 0x00, 0xf9});
    bytes(B + 0x1ef4fd0,
          {0x68, 0x62, 0x40, 0xf9, 0x28, 0x01, 0x00, 0xb4, 0x08,
           0x19, 0x40, 0xb9, 0x08, 0x05, 0x00, 0x51, 0x69, 0x9a,
           0x41, 0xb9, 0xf4, 0x4f, 0x41, 0xa9, 0x3f, 0x01, 0x08,
           0x6b, 0xe0, 0xa7, 0x9f, 0x1a, 0xfd, 0x7b, 0xc2, 0xa8});
    bytes(B + 0x1ef52a0, {0x08, 0x98, 0x41, 0xb9, 0x1f, 0x01, 0x00, 0x71, 0xe0,
                          0xd7, 0x9f, 0x1a, 0xc0, 0x03, 0x5f, 0xd6});

    bytes(B + 0x1f55ca4, {0x73, 0x12, 0x40, 0xf9});
    bytes(B + 0x1f38488, {0x94, 0xc2, 0x42, 0xf9});
    bytes(B + 0x1f384a4,
          {0x88, 0x02, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x08, 0x5d,
           0x40, 0xf9, 0x00, 0x01, 0x40, 0xf9, 0xfd, 0x7b, 0xc2, 0xa8});
    bytes(B + 0x1f354b4, {0x80, 0x12, 0x40, 0xf9});
    bytes(B + 0x3293994, {0x08, 0x5c, 0x40, 0xf9, 0x15, 0x01, 0x40, 0xf9});
    bytes(B + 0x3290ab8, {0x00, 0x08, 0x40, 0xf9});
    bytes(B + 0x3290b20, {0x08, 0x09, 0x40, 0xf9});
    bytes(B + 0x3f85864, {0x28, 0x00, 0x80, 0x52, 0xe0, 0x03, 0x13, 0xaa, 0x68,
                          0x92, 0x02, 0x39});
    bytes(B + 0x1ef5c0c, {0x74, 0xa2, 0x00, 0xb9});
    bytes(B + 0x51015dc, {0x28, 0x00, 0x80, 0x52, 0xf3, 0x03, 0x00, 0xaa, 0x08,
                          0xc8, 0x03, 0x39});
    bytes(B + 0x1ef7b94, {0xb4, 0x2e, 0x40, 0xf9});

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
void MenuFixture(Memory &m) {
  using M = Memory;
  constexpr U64 H = M::H;
  auto object = [&](U64 at, const char *name, const char *ns = "") {
    m.object(H + at, H + at + 0x100000, name, ns);
    m.alive(H + at);
  };
  auto button = [&](U64 at) {
    object(at, "UIButton", "Doozy.Runtime.UIManager.Components");
    for (auto off : {0x20, 0xd8, 0xe8, 0xf2})
      m.put<std::uint8_t>(H + at + off, 1);
    m.put<std::uint8_t>(H + at + 0x198, 0);
    m.put<int>(H + at + 0x28, 4);
    for (auto off : {0x30, 0x38, 0x40, 0x48})
      m.put<U64>(H + at + off, 0);
  };
  m.put(M::B + 0x6d47f10, H + 0x40000);
  m.klass(H + 0x40000, "UIManager");
  m.put(H + 0x400b8, H + 0x40400);
  m.put(H + 0x40400, H + 0x41000);
  object(0x41000, "UIManager");
  m.put(H + 0x41020, H + 0x42000);
  m.object(H + 0x42000, H + 0x43000, "GameMenuVariable");
  m.put(H + 0x43058, H + 0x44000);
  m.klass(H + 0x44000, "ScriptableVariable`1", "MotherBase.ToolKit");
  m.put(H + 0x440b8, H + 0x44400);
  m.put(H + 0x44400, H + 0x45000);
  m.put(H + 0x45010, H + 0x46000);
  m.put(H + 0x46018, H + 0x47000);
  m.put<int>(H + 0x46020, 1);
  m.put<int>(H + 0x46028, 0);
  m.put<int>(H + 0x4602c, 1);
  m.put<U64>(H + 0x47018, 1);
  m.put<int>(H + 0x47020, 1);
  m.put<int>(H + 0x47024, -1);
  m.put(H + 0x47028, H + 0x47100);
  m.put(H + 0x47030, H + 0x48000);
  m.put(H + 0x48010, H + 0x49000);
  object(0x49000, "GameMenuManager");
  m.put(H + 0x49020, H + 0x42000);
  m.put(H + 0x49068, H + 0x4a000);
  object(0x4a000, "EquipmentMenuManager");
  m.put(H + 0x49040, H + 0x4b000);
  object(0x4b000, "BasicTab");
  m.put<std::uint8_t>(H + 0x4b079, 1);
  m.put<std::uint8_t>(H + 0x4b07a, 1);
  m.put(H + 0x49030, H + 0x4c000);
  object(0x4c000, "BasicTabView");
  m.put<std::uint8_t>(H + 0x4c051, 1);
  m.put(H + 0x4c020, H + 0x53000);
  m.put(H + 0x41030, H + 0x4d000);
  object(0x4d000, "MenuStack");
  m.put(H + 0x4d020, H + 0x4e000);
  m.put(H + 0x4e010, H + 0x4f000);
  m.put<int>(H + 0x4e018, 1);
  m.put<int>(H + 0x4e01c, 1);
  m.put<U64>(H + 0x4f018, 1);
  m.put(H + 0x4f020, H + 0x50000);
  object(0x50000, "MenuStackLayer");
  m.put(H + 0x50020, H + 0x51000);
  m.put<U64>(H + 0x51018, 1);
  m.put(H + 0x51020, H + 0x52000);
  object(0x52000, "MenuStackLayerUIContainer");
  m.put(H + 0x52020, H + 0x53000);
  m.put(H + 0x4a050, H + 0x54000);
  m.put<U64>(H + 0x54018, 1);
  m.put(H + 0x54020, H + 0x55000);
  object(0x55000, "EquipmentViewEquippedItemController");
  m.put<int>(H + 0x55050, 1);
  m.put<int>(H + 0x55054, -1);
  m.put(H + 0x55070, H + 0x56000);
  m.put(H + 0x55078, H + 0x12000);
  button(0x56000);
  const char *names[] = {"InventoryWeaponMenuManager",
                         "InventorySpellMenuManager",
                         "InventoryCharmsMenuManager"};
  const U64 offsets[] = {0x28, 0x38, 0x30};
  for (unsigned i = 0; i < 3; ++i) {
    const auto p = H + 0x57000 + 0x1000 * i;
    m.put(H + 0x4a000 + offsets[i], p);
    object(0x57000 + 0x1000 * i, names[i]);
    m.put<std::uint8_t>(p + 0xa4, 0);
    m.put(p + 0x28, H + 0x5a000 + 0x1000 * i);
  }
  // Spell picker with one source-selected slot.
  m.put(H + 0x58048, H + 0x5d000);
  m.put(H + 0x58098, H + 0x5f000);
  m.put<int>(H + 0x580a0, 0);
  m.put(H + 0x58080, H + 0xe000);
  m.put(H + 0x5d010, H + 0x5e000);
  m.put<int>(H + 0x5d018, 1);
  m.put<int>(H + 0x5d01c, 1);
  m.put<U64>(H + 0x5e018, 1);
  m.put(H + 0x5e020, H + 0x5f000);
  object(0x5f000, "InventorySpellSlotController");
  m.put(H + 0x5f020, H + 0x60000);
  m.put(H + 0x5f050, H + 0x10000);
  m.put(H + 0x5f058, H + 0x61000);
  button(0x60000);
  m.item(H + 0x61000, H + 0x62000, "SpellItemData", 2, "Spell_Test");
  m.put(H + 0x4c028, H + 0x64000);
  m.put<U64>(H + 0x64018, 1);
  m.put(H + 0x64020, H + 0x4b000);
  m.put<int>(H + 0x4b074, 0);
  m.put<int>(H + 0x4c038, 0);
  m.put<std::uint8_t>(H + 0x4c058, 1);
  object(0x52000, "MenuStackLayerInputs");
  m.put<std::uint8_t>(H + 0x52028, 1);
  m.put(H + 0x52020, H + 0x65000);
  m.put(H + 0x65010, H + 0x66000);
  m.put<int>(H + 0x65018, 2);
  m.put<int>(H + 0x6501c, 1);
  m.put<U64>(H + 0x66018, 2);
  for (unsigned i = 0; i < 2; ++i) {
    const U64 action = H + 0x67000 + 0x1000 * i,
              event = H + 0x69000 + 0x1000 * i,
              group = H + 0x6b000 + 0x1000 * i, list = H + 0x6d000 + 0x1000 * i,
              arr = H + 0x6f000 + 0x1000 * i, call = H + 0x71000 + 0x1000 * i,
              str = H + 0x73000 + 0x1000 * i;
    m.put(H + 0x66020 + 8 * i, action);
    object(action - H, "InputActionsToUnityEvent");
    m.put<std::uint8_t>(action + 0x88, 1);
    m.put(action + 0x80, event);
    object(event - H, "UnityEvent", "UnityEngine.Events");
    m.put(event + 0x18, group);
    object(group - H, "PersistentCallGroup", "UnityEngine.Events");
    m.put(group + 0x10, list);
    m.put(list + 0x10, arr);
    m.put<int>(list + 0x18, 1);
    m.put<int>(list + 0x1c, 1);
    m.put<U64>(arr + 0x18, 1);
    m.put(arr + 0x20, call);
    object(call - H, "PersistentCall", "UnityEngine.Events");
    m.put(call + 0x10, H + 0x4c000);
    m.put<int>(call + 0x38, 2);
    m.put(call + 0x20, str);
    m.object(str, H + 0x28000, "String", "System");
    const char *name = i ? "NextTab" : "PreviousTab";
    m.put<int>(str + 0x10, int(std::strlen(name)));
    for (unsigned j = 0; j < std::strlen(name); ++j)
      m.put<std::uint16_t>(str + 0x14 + 2 * j, std::uint16_t(name[j]));
  }
}
void CharmFixture(Memory &m, bool holding) {
  using M = Memory;
  constexpr U64 H = M::H;
  MenuFixture(m);
  auto object = [&](U64 p, const char *name, const char *ns = "") {
    m.object(H + p, H + p + 0x100000, name, ns);
    m.alive(H + p);
  };
  m.put(H + 0x10020, H + 0x29000);
  m.put(H + 0x29010, H + 0x2a000);
  m.put<int>(H + 0x29018, 3);
  m.put<int>(H + 0x2901c, 1);
  m.put<U64>(H + 0x2a018, 3);
  m.put(H + 0x2a020, H + 0x12000);
  m.put(H + 0x2a028, H + 0x16000);
  m.put(H + 0x2a030, H + 0x82000);
  m.klass(H + 0x2b000, "ItemData");
  m.put(H + 0x13058, H + 0x2b000);
  m.put(H + 0x17058, H + 0x2b000);
  m.item(H + 0x82000, H + 0x83000, "CharmItemData", 3, "Charm_NewOwned");
  m.put(H + 0x83058, H + 0x2b000);
  m.put<std::uint8_t>(H + 0x580a4, 0);
  m.put<std::uint8_t>(H + 0x590a4, 1);
  m.put(H + 0x59048, H + 0x5d000);
  m.put(H + 0x59080, H + 0xe000);
  m.put<U64>(H + 0x59098, holding ? 0 : H + 0x5f000);
  m.put<int>(H + 0x590a0, -1);
  m.put<int>(H + 0x591a0, holding ? 0 : -1);
  m.put<U64>(H + 0x591a8, holding ? H + 0x76000 : 0);
  m.put<U64>(H + 0x591b0, holding ? H + 0x5f000 : 0);
  m.put<std::uint8_t>(H + 0x591b8, 0);
  m.object(H + 0x5f000, H + 0x15f000, "InventoryCharmSlotController");
  m.put(H + 0x5f058, H + 0x82000);
  m.put<std::uint8_t>(H + 0x600f2, holding ? 0 : 1);
  m.put(H + 0x590c0, H + 0x7c000);
  m.put(H + 0x7c010, H + 0x7d000);
  m.put<int>(H + 0x7c018, 2);
  m.put<int>(H + 0x7c01c, 1);
  m.put<U64>(H + 0x7d018, 2);
  m.put(H + 0x7d020, H + 0x7e000);
  m.put(H + 0x7d028, H + 0x7f000);
  object(0x7e000, "InventoryCharmPage");
  object(0x7f000, "InventoryCharmPage");
  for (unsigned page = 0; page < 2; ++page) {
    const U64 list = H + 0x170000 + page * 0x1000;
    m.put(H + 0x7e030 + page * 0x1000, list);
    m.put(list + 0x10, list + 0x200);
    m.put<int>(list + 0x18, page == 0 ? 1 : 0);
    m.put<int>(list + 0x1c, 1);
    m.put<U64>(list + 0x218, page == 0 ? 1 : 0);
    m.put(list + 0x220, H + 0x5f000);
  }
  m.put(H + 0x7e038, H + 0x80000);
  m.put(H + 0x7f038, H + 0x81000);
  m.put<int>(H + 0x59198, 0);
  m.put(H + 0x4f020, H + (holding ? 0x80000 : 0x5c000));
  m.put(H + 0x590b8, H + 0x75000);
  m.put<U64>(H + 0x75018, 3);
  m.put<U64>(H + 0x15018, 3);
  m.put(H + 0x15020, H + 0x16000);
  m.put<U64>(H + 0x15028, 0);
  m.put<U64>(H + 0x15030, 0);
  for (unsigned i = 0; i < 3; ++i) {
    const U64 p = H + 0x76000 + 0x1000 * i, b = H + 0x79000 + 0x1000 * i;
    m.put(H + 0x75020 + 8 * i, p);
    object(p - H, "EquipmentViewEquippedItemController");
    m.put(p + 0x70, b);
    m.put<U64>(p + 0x78, 0);
    object(b - H, "UIButton", "Doozy.Runtime.UIManager.Components");
    for (auto off : {0x20, 0xd8, 0xe8})
      m.put<std::uint8_t>(b + off, 1);
    m.put<std::uint8_t>(b + 0xf2, holding && i == 0 ? 1 : 0);
    m.put<std::uint8_t>(b + 0x198, 0);
    m.put<int>(b + 0x28, 4);
    m.put<U64>(b + 0x30, 0);
    m.put<U64>(b + 0x38, 0);
    m.put<U64>(b + 0x40, i ? b - 0x1000 : 0);
    m.put<U64>(b + 0x48, i < 2 ? b + 0x1000 : 0);
  }
}
int main() {
  try {
    using M = Memory;
    Memory m;
    m.put(M::H + 0x10020, M::H + 0x29000);
    m.put(M::H + 0x29010, M::H + 0x2a000);
    m.put<int>(M::H + 0x29018, 2);
    m.put<int>(M::H + 0x2901c, 1);
    m.put<U64>(M::H + 0x2a018, 2);
    m.put(M::H + 0x2a020, M::H + 0x12000);
    m.put(M::H + 0x2a028, M::H + 0x16000);
    m.klass(M::H + 0x2b000, "ItemData");
    m.put(M::H + 0x13058, M::H + 0x2b000);
    m.put(M::H + 0x17058, M::H + 0x2b000);
    belmont_inventory::Decoder d(m);
    auto id = m.identity();
    auto source = m.source();
    auto s = d.Sample(id, source);
    Check(s.ready && s.items.size() == 2 && s.items[0].type == 1 &&
              s.items[0].id == "Weapon_Test" && s.items[1].type == 3,
          "bounded owned inventory");
    Check(!s.control.ready, "missing native menu cannot enable input");
    MenuFixture(m);
    s = d.Sample(id, source);
    Check(s.ready && s.control.ready && s.control.overview &&
              s.control.selected_type == 1 && s.control.confirm,
          "native overview root/tab/selectedbutton joins");
    {
      const auto previous = s;
      unsigned ownedReads = 0;
      m.observe = [&](U64 at) {
        if (at >= previous.owned_array + 0x20 &&
            at < previous.owned_array + 0x20 + 8 * U64(previous.owned_count))
          ++ownedReads;
      };
      auto fast = d.SampleControl(id, source, previous);
      m.observe = {};
      Check(fast.ready && fast.control.ready &&
                fast.items.size() == s.items.size() && !ownedReads,
            "fast control skips owned inventory walk");
      m.put<int>(M::H + 0x2901c, 2);
      fast = d.SampleControl(id, source, previous);
      Check(!fast.ready && fast.reason == "inventory_changed",
            "fast control invalidates inventory version");
      m.put<int>(M::H + 0x2901c, 1);
      m.put<int>(M::H + 0xc02c, 3);
      fast = d.SampleControl(id, source, previous);
      Check(!fast.ready,
            "fast control invalidates component dictionary version");
      m.put<int>(M::H + 0xc02c, 2);
    }
    m.put<std::uint8_t>(M::H + 0x52028, 0);
    s = d.Sample(id, source);
    Check(s.ready && !s.control.ready,
          "covered overview cannot enable controls");
    m.put<std::uint8_t>(M::H + 0x52028, 1);
    m.put<std::uint8_t>(M::H + 0x580a4, 1);
    m.put(M::H + 0x4f020, M::H + 0x5b000);
    s = d.Sample(id, source);
    Check(s.ready && s.control.ready && s.control.picker &&
              s.control.category == 2 && s.control.selected_id == "Spell_Test",
          "native selected spell picker");
    {
      const auto previous = s;
      unsigned idReads = 0;
      m.observe = [&](U64 at) {
        if (at == M::H + 0x63010)
          ++idReads;
      };
      auto fast = d.SampleControl(id, source, previous);
      m.observe = {};
      Check(fast.ready && fast.control.ready &&
                fast.control.selected_id == "Spell_Test" && !idReads,
            "fast picker reuses immutable IDs on unchanged list version");
      m.put<std::uint8_t>(M::H + 0x600d8, 0);
      fast = d.SampleControl(id, source, previous);
      Check(fast.control.ready && !fast.control.confirm,
            "fast picker refreshes current native disabled state");
      m.put<std::uint8_t>(M::H + 0x600d8, 1);
      m.put(M::H + 0x5f058, M::H + 0x12000);
      fast = d.SampleControl(id, source, previous);
      Check(fast.ready && !fast.control.ready,
            "fast picker item pointer mismatch clears controls");
      m.put(M::H + 0x5f058, M::H + 0x61000);
      m.put<U64>(M::H + 0x58098, 0);
      idReads = 0;
      m.observe = [&](U64 at) {
        if (at == M::H + 0x63010)
          ++idReads;
      };
      fast = d.SampleControl(id, source, previous);
      m.observe = {};
      Check(fast.control.ready && fast.control.selected_id == "Spell_Test" &&
                !idReads,
            "missing native selected-slot pointer uses cached UIButton flags "
            "without ID walk");
      m.put(M::H + 0x58098, M::H + 0x5f000);
    }
    m.put(M::H + 0x5f050, M::H + 0x10008);
    s = d.Sample(id, source);
    Check(s.ready && !s.control.ready,
          "foreign picker inventory cannot enable controls");
    m.put(M::H + 0x5f050, M::H + 0x10000);
    m.put(M::H + 0x4f020, M::H + 0x50000);
    s = d.Sample(id, source);
    Check(s.ready && !s.control.ready, "picker not top layer fails closed");
    m.put<std::uint8_t>(M::H + 0x580a4, 0);
    m.put<std::uint8_t>(M::H + 0x4c051, 0);
    m.put<int>(M::H + 0x4e018, 0);
    s = d.Sample(id, source);
    Check(s.ready && s.control.ready && !s.control.menu_open &&
              s.control.can_open,
          "source closed menu empty stack authorizes opening");

    m.put<int>(M::H + 0x4c038, -1);
    s = d.Sample(id, source);
    Check(s.ready && s.control.ready && s.control.can_open &&
              s.control.current_tab == -1,
          "closed first-open native unselected tab sentinel permits Plus only "
          "with empty stack");
    m.put<int>(M::H + 0x4c038, -2);
    s = d.Sample(id, source);
    Check(s.ready && !s.control.ready,
          "invalid closed negative tab fails closed");
    m.put<int>(M::H + 0x4c038, -1);
    m.put<std::uint8_t>(M::H + 0x4c051, 1);
    s = d.Sample(id, source);
    Check(s.ready && !s.control.ready,
          "open native menu cannot use unselected sentinel");
    m.put<std::uint8_t>(M::H + 0x4c051, 0);
    m.put<int>(M::H + 0x4c038, 0);
    m.put<int>(M::H + 0x29018, 513);
    s = d.Sample(id, source);
    Check(!s.ready && s.items.empty(), "reject oversized inventory");
    m.put<int>(M::H + 0x29018, 2);
    m.put(M::H + 0x10050, M::H + 0x3008);
    s = d.Sample(id, source);
    Check(!s.ready, "foreign inventory");
    m.put(M::H + 0x10050, M::H + 0x3000);
    m.put<int>(M::H + 0x12028, 3);
    s = d.Sample(id, source);
    Check(!s.ready, "mismatched class/type");
    m.put<int>(M::H + 0x12028, 1);
    m.put<int>(M::H + 0x14010, 129);
    s = d.Sample(id, source);
    Check(!s.ready, "bounded identifiers");
    m.put<int>(M::H + 0x14010, 11);
    unsigned seen = 0;
    m.observe = [&](U64 p) {
      if (p == M::H + 0x2901c && ++seen == 2)
        m.put<int>(p, 2);
    };
    s = d.Sample(id, source);
    Check(!s.ready && s.items.empty(), "inventory version race clears all");
    m.observe = {};
    auto bad = id;
    bad.build[0] ^= 1;
    auto n = m.reads;
    s = d.Sample(bad, source);
    Check(!s.ready && m.reads == n, "wrong build zero reads");
    source.room_loading = 1;
    n = m.reads;
    s = d.Sample(id, source);
    Check(!s.ready && s.items.empty() && m.reads == n,
          "loading clears without reads");

    {
      Memory native;
      CharmFixture(native, false);
      belmont_inventory::Decoder reader(native);
      auto fresh = reader.Sample(native.identity(), native.source());
      Check(fresh.ready && fresh.control.ready && fresh.control.category == 3 &&
                fresh.control.native_pages == 2 && fresh.control.can_next_page,
            "native relic page count/bounds read");
      Check(fresh.control.slots[0].native_page == 0,
            "native page membership attaches to stable grid index");
      Check(fresh.control.slots.size() == 4 &&
                fresh.control.slots[1].item.id == "Charm_Test",
            "actual equipped destination ID ignores null UI placeholder");
      CharmFixture(native, true);
      auto held = reader.Sample(native.identity(), native.source());
      Check(held.ready && held.control.ready && held.control.holding_charm &&
                held.control.held_item.id == "Charm_NewOwned" &&
                held.control.charm_slot == 0,
            "native header/held slot proves valid chooser while legacy boolean "
            "zero");
      auto fast =
          reader.SampleControl(native.identity(), native.source(), held);
      Check(fast.ready && fast.control.ready && fast.control.holding_charm &&
                fast.control.selected_id == "Charm_Test",
            "cached held chooser reads current equipped selection");
      Check(!fast.control.can_next_page && !fast.control.can_prev_page,
            "held chooser gates native page actions");
    }

    {
      Memory native;
      CharmFixture(native, false);
      auto H = M::H;
      // A real source-shaped second grid slot: native cached index/pointer
      // remains on old slot0 while UIButton selection is now slot1.
      native.put<int>(H + 0x5d018, 2);
      native.put<U64>(H + 0x5e018, 2);
      native.put(H + 0x5e028, H + 0x85000);
      native.put<int>(H + 0x170018, 2);
      native.put<U64>(H + 0x170218, 2);
      native.put(H + 0x170228, H + 0x85000);
      native.object(H + 0x85000, H + 0x185000, "InventoryCharmSlotController");
      native.alive(H + 0x85000);
      native.put(H + 0x85050, H + 0x10000);
      native.put(H + 0x85058, H + 0x16000);
      native.put(H + 0x85020, H + 0x89000);
      native.object(H + 0x89000, H + 0x189000, "UIButton",
                    "Doozy.Runtime.UIManager.Components");
      native.alive(H + 0x89000);
      for (auto off : {0x20, 0xd8, 0xe8, 0xf2})
        native.put<std::uint8_t>(H + 0x89000 + off, 1);
      native.put<std::uint8_t>(H + 0x89198, 0);
      native.put<int>(H + 0x89028, 4);
      for (auto off : {0x30, 0x38, 0x40, 0x48})
        native.put<U64>(H + 0x89000 + off, 0);
      native.put<std::uint8_t>(H + 0x600f2, 0);
      belmont_inventory::Decoder reader(native);
      auto prior = reader.Sample(native.identity(), native.source());
      Check(
          prior.control.ready && prior.control.selected_id == "Charm_Test",
          "source actual UIButton differs from stale native selected pointer");
      native.put<std::uint8_t>(H + 0x600e8, 0);
      unsigned rereadIds = 0;
      native.observe = [&](U64 at) {
        if (at == H + 0x18010 || at == H + 0x84010)
          ++rereadIds;
      };
      auto fast =
          reader.SampleControl(native.identity(), native.source(), prior);
      native.observe = {};
      Check(fast.control.ready && fast.control.selected_id == "Charm_Test" &&
                !rereadIds,
            "stale source page focus uses only selection flags plus fresh "
            "selected-slot validation");
      Check(!fast.control.slots[0].enabled,"cached unselected native group becoming disabled is observed");
      native.put<std::uint8_t>(H + 0x600e8,1);
      auto settled=reader.SampleControl(native.identity(),native.source(),fast);
      Check(settled.control.ready && settled.control.slots[0].enabled,
            "cached page target becomes enabled after native canvas transition");
    }
    std::cout << "inventory reader tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
