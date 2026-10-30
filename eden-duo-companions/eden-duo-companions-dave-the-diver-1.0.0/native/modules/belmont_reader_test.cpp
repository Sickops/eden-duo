// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_reader.h"
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

void Check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Memory {
  static constexpr belmont::U64 Base = 0x80000000, Heap = 0x1200000000;
  std::map<belmont::U64, std::uint8_t> data;
  std::function<void(belmont::U64)> observe;
  std::atomic<unsigned> reads{};
  void bytes(belmont::U64 at, std::initializer_list<std::uint8_t> v) {
    for (auto x : v)
      data[at++] = x;
  }
  template <class T> void put(belmont::U64 at, T value) {
    for (unsigned i = 0; i < sizeof(T); ++i)
      data[at + i] = std::uint8_t(belmont::U64(value) >> (8 * i));
  }
  void text(belmont::U64 at, const char *value) {
    do {
      data[at++] = *value;
    } while (*value++);
  }
  bool operator()(belmont::U64 at, void *dst, std::size_t n) {
    ++reads;
    if (observe)
      observe(at);
    for (std::size_t i = 0; i < n; ++i) {
      auto p = data.find(at + i);
      if (p == data.end())
        return false;
      static_cast<std::uint8_t *>(dst)[i] = p->second;
    }
    return true;
  }
  void klass(belmont::U64 k, const char *name, const char *ns) {
    put(k + 0x10, k + 0x201);
    put(k + 0x18, k + 0x301);
    for (unsigned i = 0; i < 128; ++i) {
      data[k + 0x201 + i] = 0;
      data[k + 0x301 + i] = 0;
    }
    text(k + 0x201, name);
    text(k + 0x301, ns);
  }
  void native(belmont::U64 object) {
    auto at = Heap + 0x100000 + (object - Heap);
    put(object + 0x10, at);
    put<std::uint8_t>(at, 0x55);
  }
  void root(belmont::U64 r, belmont::U64 k, const char *name, const char *ns) {
    put(Base + r, k);
    klass(k, name, ns);
    put(k + 0xb8, k + 0x400);
    put<std::uint8_t>(k + 0x400, 0);
  }
  Memory() {
    bytes(Base + 0x1d4bc74, {0x88, 0x2, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x8,
                             0x5d, 0x40, 0xf9, 0x0, 0x1, 0x40, 0xf9});
    bytes(Base + 0x40d4b44, {0x88, 0x2, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x8,
                             0x5d, 0x40, 0xf9, 0x0, 0x1, 0x40, 0x39});
    bytes(Base + 0x1f7e0bc, {0x8, 0x5c, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x0,
                             0x1, 0x40, 0x39});
    bytes(Base + 0x4107104, {0x88, 0x2, 0x40, 0xf9, 0xf4, 0x4f, 0x41, 0xa9, 0x8,
                             0x5d, 0x40, 0xf9, 0x0, 0x1, 0x40, 0x39});

    bytes(Base + 0x40e2d90,
          {0x60, 0x1a, 0x40, 0xf9, 0x0,  0x1,  0x0,  0xb4, 0xe8,
           0x58, 0x1,  0xb0, 0x8,  0x5d, 0x40, 0xf9, 0x2,  0x1,
           0x40, 0xf9, 0xf4, 0x4f, 0x42, 0xa9, 0xf6, 0x57, 0x41,
           0xa9, 0xfd, 0x7b, 0xc3, 0xa8, 0xa8, 0x6b, 0xdd, 0x17});
    bytes(Base + 0x383dc94,
          {0x9, 0x3, 0x80, 0x52, 0x8, 0x20, 0xa9, 0x9b, 0x0, 0x19, 0x40, 0xf9});
    bytes(Base + 0x383f714,
          {0x48, 0x3,  0x0,  0xb9, 0xe8, 0x26, 0x40, 0xb9, 0x69, 0x73,
           0xa9, 0x9b, 0x20, 0x0,  0x80, 0x52, 0x3f, 0x9,  0x0,  0xf9});
    bytes(Base + 0x3929c7c, {0x09, 0x03, 0x80, 0x52, 0x08, 0x20, 0xa9, 0x9b,
                             0x00, 0x19, 0x40, 0xf9});
    bytes(Base + 0x1c29074, {0x48, 0xc9, 0x28, 0x8b, 0x02, 0x21, 0x00, 0xb9});
    bytes(Base + 0x1c298a8, {0x80, 0x2a, 0x40, 0xf9, 0x20, 0x01, 0x00, 0xb4});
    bytes(Base + 0x1c296b0,
          {0x09, 0x1c, 0x40, 0xb9, 0x29, 0x05, 0x00, 0x11, 0x08, 0x01, 0x40,
           0xf9, 0x09, 0x1c, 0x00, 0xb9, 0x09, 0x08, 0x40, 0xf9, 0xa9, 0x02,
           0x00, 0xb4, 0x0a, 0x18, 0x80, 0xb9, 0x2b, 0x19, 0x40, 0xb9});
    bytes(Base + 0x1c296e0, {0x28, 0x09, 0x0a, 0x8b, 0x13, 0x21, 0x00, 0xb9});
    for (const auto &[offset, second] :
         std::array<std::pair<belmont::U64, std::uint8_t>, 4>{
             {{0x1d4ef20, 0x60},
              {0x1d4ef30, 0x64},
              {0x1d4ef40, 0x68},
              {0x1d4efd0, 0x8c}}})
      bytes(Base + offset, {0x00, second, 0x40, 0x39, 0xc0, 0x03, 0x5f, 0xd6});
    root(0x6d3f780, Heap + 0x1000, "ApplicationFlowManager",
         "MotherBase.ApplicationFlowManager");
    root(0x6d46228, Heap + 0x2000, "RoomManagerSystem", "RoomManager");
    root(0x6d44b20, Heap + 0x3000, "NXAccount", "MotherBase.GameServices");
    put<std::uint8_t>(Heap + 0x3400, 1);
    root(0x6d42540, Heap + 0x4000, "GameStateManager", "");
    put(Heap + 0x4400, Heap + 0x5000);
    put(Heap + 0x5000, Heap + 0x4000);
    put(Heap + 0x5020, Heap + 0x6000);
    put(Heap + 0x5068, Heap + 0x6100);
    put(Heap + 0x50a0, Heap + 0x7000);
    put(Heap + 0x7010, Heap + 0x8000);
    put<std::int32_t>(Heap + 0x7018, 1);
    put<belmont::U64>(Heap + 0x8018, 1);
    put(Heap + 0x8020, Heap + 0x9000);
    put(Heap + 0x9018, Heap + 0x6000);
    put(Heap + 0x50b0, Heap + 0xa000);
    put(Heap + 0xa000, Heap + 0xb000);
    klass(Heap + 0xb000, "Entity", "MotherBase.EntityFramework");
    put<std::uint8_t>(Heap + 0xa020, 0);
    put<std::uint8_t>(Heap + 0xa2c8, 1);
    native(Heap + 0xa000);
  }
  void components() {
    put(Heap + 0xa030, Heap + 0xc000);
    put(Heap + 0xc018, Heap + 0xd000);
    put<std::int32_t>(Heap + 0xc020, 4);
    put<std::int32_t>(Heap + 0xc028, 1);
    put<std::int32_t>(Heap + 0xc02c, 10);
    put<belmont::U64>(Heap + 0xd018, 4);
    for (unsigned i = 0; i < 4; ++i) {
      auto at = Heap + 0xd020 + i * 0x18;
      put<std::int32_t>(at, i == 1 ? -1 : i);
      put<std::int32_t>(at + 4, -1);
      put<std::int32_t>(at + 8, 123 + i);
      put<belmont::U64>(at + 0x10, i == 1 ? 0 : Heap + 0xe000 + i * 0x1000);
    }
    native(Heap + 0xe000);
    native(Heap + 0x10000);
    native(Heap + 0x11000);
    native(Heap + 0x17000);
    put(Heap + 0xe000, Heap + 0x14000);
    klass(Heap + 0x14000, "ExplorerComponent", "");
    put(Heap + 0xe020, Heap + 0xa000);
    put(Heap + 0xe030, Heap + 0x15000);
    put<belmont::U64>(Heap + 0x15018, 0);
    put<std::int32_t>(Heap + 0x15020, 0);
    put<std::int32_t>(Heap + 0x15028, 0);
    put<std::int32_t>(Heap + 0x1502c, 0);
    put(Heap + 0x10000, Heap + 0x16000);
    klass(Heap + 0x16000, "CurrentRoomInstance", "");
    put<std::int32_t>(Heap + 0x10020, 0);
    put(Heap + 0x10028, Heap + 0x17000);
    put(Heap + 0x17000, Heap + 0x18000);
    klass(Heap + 0x18000, "RoomInstance", "MotherBase.RoomDesigner");
    put(Heap + 0x17020, Heap + 0x19000);
    put(Heap + 0x19000, Heap + 0x1a000);
    klass(Heap + 0x1a000, "WorldRoomData", "MotherBase.RoomDesigner");
    put(Heap + 0x19048, Heap + 0x1b000);
    put(Heap + 0x1b000, Heap + 0x1d000);
    klass(Heap + 0x1d000, "String", "System");
    put<std::int32_t>(Heap + 0x1b010, 4);
    bytes(Heap + 0x1b014, {'r', 0, 'o', 0, 'o', 0, 'm', 0});
    put(Heap + 0x11000, Heap + 0x1c000);
    klass(Heap + 0x1c000, "InterpolatedTransform",
          "MotherBase.ActionFramework");
    put<std::uint32_t>(Heap + 0x11040, 0x3fc00000); // 1.5
    put<std::uint32_t>(Heap + 0x11044, 0xc0200000); // -2.5
    put<std::uint32_t>(Heap + 0x11048, 0);
  }
  void exploration() {
    components();
    put(Heap + 0x15018, Heap + 0x20000);
    put<std::int32_t>(Heap + 0x15020, 1);
    put<std::int32_t>(Heap + 0x15028, 0);
    put<std::int32_t>(Heap + 0x1502c, 7);
    put<belmont::U64>(Heap + 0x20018, 2);
    put<std::int32_t>(Heap + 0x20020, 42);
    put<std::int32_t>(Heap + 0x20024, -1);
    put(Heap + 0x20028, Heap + 0x24000);
    put(Heap + 0x24000, Heap + 0x25000);
    klass(Heap + 0x25000, "String", "System");
    put<std::int32_t>(Heap + 0x24010, 4);
    bytes(Heap + 0x24014, {'r', 0, 'o', 0, 'o', 0, 'm', 0});
    put(Heap + 0x20030, Heap + 0x21000);
    put(Heap + 0x21000, Heap + 0x22000);
    klass(Heap + 0x22000, "WorldRoomExplorationState", "");
    put<std::int32_t>(Heap + 0x21010, 2);
    put<std::int32_t>(Heap + 0x21014, 2);
    put(Heap + 0x21018, Heap + 0x23000);
    put<std::uint8_t>(Heap + 0x21020, 1);
    put<belmont::U64>(Heap + 0x23018, 4);
    for (unsigned i = 0; i < 4; ++i)
      put<std::int32_t>(Heap + 0x23020 + 4 * i, i);
    put(Heap + 0x21050, Heap + 0x26000);
    put(Heap + 0x26000, Heap + 0x27000);
    klass(Heap + 0x27000, "List`1", "System.Collections.Generic");
    put(Heap + 0x26010, Heap + 0x2e000);
    put<std::int32_t>(Heap + 0x26018, 2);
    put<std::int32_t>(Heap + 0x2601c, 5);
    put<belmont::U64>(Heap + 0x2e018, 4);
    put<std::int32_t>(Heap + 0x2e020, 0);
    put<std::int32_t>(Heap + 0x2e024, 3);
    // Capacity past allocated count is not an entry, even with zero hash.
    put<std::int32_t>(Heap + 0x20038, 0);
    put<belmont::U64>(Heap + 0x20040, 3);
    put<belmont::U64>(Heap + 0x20048, 3);
  }
  belmont::Identity id() {
    return {belmont::Title, Base, 0x7000000, belmont::Build};
  }
  belmont::Snapshot sample() {
    belmont::Decoder decoder(*this);
    return decoder.Sample(id());
  }
};

#ifndef BELMONT_READER_ONLY_TEST
struct Host {
  Memory memory;
  std::map<std::string, std::int64_t> values;
  std::string reason;
  std::map<std::string, std::string> texts;
  std::map<std::string, double> numbers;
  std::atomic<unsigned> romfs_reads{}, publications{}, writes{};
  static EdenDsmodBool Write(void* p,std::uint64_t,const void*,std::size_t){++static_cast<Host*>(p)->writes;return EDEN_DSMOD_FALSE;}
  static EdenDsmodBool Read(void *p, std::uint64_t at, void *dst,
                            std::size_t n) {
    return static_cast<Host *>(p)->memory(at, dst, n);
  }
  static EdenDsmodBool Mapped(void *p, std::uint64_t at, std::uint64_t n) {
    auto &bytes = static_cast<Host *>(p)->memory.data;
    if (n > 4096 || at > UINT64_MAX - n)
      return false;
    for (std::uint64_t i = 0; i < n; ++i)
      if (!bytes.contains(at + i))
        return false;
    return true;
  }
  static void Int(void *p, const char *key, std::int64_t value) {
    auto& h = *static_cast<Host *>(p);
    ++h.publications; h.values[key] = value;
  }
  static void Text(void *p, const char *key, const char *text) {
    auto& h = *static_cast<Host *>(p);
    ++h.publications; h.texts[key] = text;
    if (std::string_view(key) == "belmont.state_reason") h.reason = text;
  }
  static void Number(void *p, const char *key, double value) {
    auto& h = *static_cast<Host *>(p);
    ++h.publications; h.numbers[key] = value;
  }
  static std::size_t Romfs(void *p, const char *, std::uint64_t, void *, std::size_t) {
    ++static_cast<Host *>(p)->romfs_reads;
    return 0; // Delayed/missing source, never a fake native asset.
  }
  EdenDsmodHostApi api() {
    EdenDsmodHostApi a{};
    a.abi_version = EDEN_DSMOD_MODULE_ABI_VERSION;
    a.struct_size = sizeof(a);
    a.abi_hash = EDEN_DSMOD_MODULE_ABI_HASH;
    a.userdata = this;
    a.title_id = belmont::Title;
    std::memcpy(a.build_id, belmont::Build.data(), 32);
    a.main_base = Memory::Base;
    a.main_size = 0x7000000;
    a.is_mapped = &Mapped;
    a.read_memory = &Read;
    a.write_memory = &Write;
    a.publish_i64 = &Int;
    a.publish_text = &Text;
    a.publish_f64 = &Number;
    a.read_romfs = &Romfs;
    return a;
  }
};
#endif
int main() {
  try {
    using M = Memory;
    constexpr auto H = M::Heap;
    Check(belmont::Supports(belmont::BuildHex), "exact build");
    Check(!belmont::Supports("0F1169AA7D0FA452"), "short build rejected");
    auto altered = std::string(belmont::BuildHex);
    altered[63] = '1';
    Check(!belmont::Supports(altered.c_str()),
          "full trailing build bytes pinned");
    M good;
    auto s = good.sample();
    Check(s.coherent && s.entity_ready && s.initial_identity == 1 &&
              s.loading_identity == 0,
          "provisional gameplay identity");
    Check(s.room_loading == 0 && s.account_initialized == 1,
          "static first byte is flag");
    {
      M m;
      auto id = m.id();
      id.build[31] = 1;
      belmont::Decoder d(m);
      auto x = d.Sample(id);
      Check(!x.supported && m.reads == 0, "wrong build performs no reads");
    }
    {
      M m;
      m.put<belmont::U64>(M::Base + 0x6d46228, 0x2001050f);
      auto x = m.sample();
      Check(x.room_loading == -1 && x.reason == "loading_unavailable",
            "encoded TypeInfo is unavailable, not false");
    }
    {
      M m;
      m.text(H + 0x2201, "WrongType");
      Check(m.sample().room_loading == -1, "wrong root class rejected");
    }
    {
      M m;
      m.put<std::uint8_t>(H + 0x2400, 2);
      Check(m.sample().room_loading == -1, "invalid bool rejected");
    }
    {
      M m;
      m.put<std::uint8_t>(H + 0x2400, 1);
      Check(m.sample().reason == "loading", "loading invalidation");
    }
    {
      M m;
      m.put(H + 0x9018, H + 0x6200);
      auto x = m.sample();
      Check(x.initial_identity == 0 && x.reason == "non_gameplay_identity",
            "different state object, same possible type, is not gameplay");
    }
    for (auto count : {-1, 0, 2, 129}) {
      M m;
      m.put<std::int32_t>(H + 0x7018, count);
      Check(m.sample().initial_identity == -1,
            "bounded stack count and capacity");
    }
    {
      M m;
      m.put<belmont::U64>(H + 0x8018, UINT64_MAX);
      Check(m.sample().initial_identity == -1, "huge capacity rejected");
    }
    {
      M m;
      m.put<std::uint8_t>(H + 0xa020, 1);
      Check(!m.sample().entity_ready, "destroyed entity rejected");
    }
    {
      M m;
      m.put<std::uint8_t>(H + 0xa2c8, 0);
      Check(!m.sample().entity_ready, "disabled entity rejected");
    }
    {
      M m;
      unsigned count = 0;
      m.observe = [&](auto at) {
        if (at == H + 0x7018 && ++count == 2)
          m.put<std::int32_t>(at, 0);
      };
      auto x = m.sample();
      Check(!x.coherent && x.entity == 0 && x.initial_identity == -1,
            "torn stack clears decoded values");
    }
    {
      M m;
      m.put<std::uint8_t>(M::Base + 0x1d4bc74, 0);
      Check(m.sample().reason == "accessor_mismatch",
            "original instructions required");
    }
    {
      M m;
      m.put<belmont::U64>(H + 0x50b0, 0);
      Check(!m.sample().entity_ready, "title clears entity readiness");
    }
    {
      M m;
      m.components();
      auto x = m.sample();
      Check(x.components_ready && x.explorer_candidate &&
                x.exploration_count == 0,
            "valid cache skips deleted entry");
      Check(x.room_candidate && x.room_state == 0 && x.room_id == "room",
            "room identity candidate");
      Check(x.position_candidate && x.target_position[0] == 1.5f &&
                x.target_position[1] == -2.5f,
            "raw simulation position candidate");
    }
    {
      M m;
      m.components();
      m.put<std::uint8_t>(H + 0x2400, 1);
      auto x = m.sample();
      Check(!x.components_ready && !x.position_candidate && x.room_id.empty(),
            "loading hides candidates");
    }
    {
      M m;
      m.components();
      m.put(H + 0x9018, H + 0x6200);
      auto x = m.sample();
      Check(!x.components_ready && !x.position_candidate &&
                x.exploration_count == -1,
            "non gameplay identity hides candidates");
    }
    for (auto count : {-1, 257}) {
      M m;
      m.components();
      m.put<std::int32_t>(H + 0xc020, count);
      Check(!m.sample().components_ready, "component allocated count bounded");
    }
    {
      M m;
      m.components();
      m.put<std::int32_t>(H + 0xc028, 5);
      Check(!m.sample().components_ready,
            "free count cannot exceed allocated count");
    }
    {
      M m;
      m.components();
      m.put<belmont::U64>(H + 0xd018, 3);
      Check(!m.sample().components_ready,
            "component array capacity must cover allocated count");
    }
    {
      M m;
      m.components();
      m.put<std::int32_t>(H + 0xc028, 0);
      Check(!m.sample().components_ready,
            "live count must agree with deleted hashes");
    }
    {
      M m;
      m.components();
      m.put<belmont::U64>(H + 0xd030, 3);
      Check(!m.sample().components_ready,
            "misaligned active component pointer rejected");
    }
    {
      M m;
      m.components();
      m.put<std::int32_t>(H + 0xd024, 4);
      Check(!m.sample().components_ready,
            "active next index must be inside allocated range");
    }
    {
      M m;
      m.components();
      m.put<std::int32_t>(H + 0xd058, 123);
      Check(!m.sample().components_ready, "duplicate component keys rejected");
    }
    {
      M m;
      m.components();
      m.put(H + 0xd060, H + 0xe000);
      Check(!m.sample().components_ready,
            "duplicate expected component type rejected");
    }
    {
      M m;
      m.components();
      m.put<std::uint32_t>(H + 0x11040, 0x7fc00000);
      Check(!m.sample().position_candidate, "NaN position unavailable");
    }
    {
      M m;
      m.components();
      m.put(H + 0xe020, H + 0xa100);
      Check(!m.sample().explorer_candidate,
            "explorer must belong to sampled entity");
    }
    {
      M m;
      m.components();
      unsigned count = 0;
      m.observe = [&](auto at) {
        if (at == H + 0xc02c && ++count == 2)
          m.put<std::int32_t>(at, 11);
      };
      auto x = m.sample();
      Check(!x.coherent && !x.components_ready && !x.position_candidate &&
                x.room_id.empty(),
            "cache version race clears all candidates");
    }
    {
      M m;
      m.exploration();
      auto x = m.sample();
      Check(x.coherent && x.exploration_ready && x.exploration.size() == 1,
            "complete exploration dictionary exposed diagnostically");
      const auto &room = x.exploration.front();
      Check(room.room_id == "room" && room.width == 2 && room.height == 2 &&
                room.pending_validation &&
                room.cells == std::vector<std::int32_t>({0, 1, 2, 3}),
            "native no-chunk/unexplored/temporary/validated states remain "
            "distinct");
      Check(!x.map_ready, "synthetic exploration cannot enable map readiness");
    }
    {
      M m;
      m.exploration();
      auto x = m.sample();
      Check(x.exploration_zones_ready && x.exploration[0].entered_zones_ready &&
                x.exploration[0].entered_zone_ids ==
                    std::vector<std::int32_t>({0, 3}),
            "typed native entered zone IDs kept distinct from chunk states");
    }
    for (bool null_items : {false, true}) {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x26018, 0);
      if (null_items)
        m.put<belmont::U64>(H + 0x26010, 0);
      auto x = m.sample();
      Check(x.exploration_zones_ready && x.exploration[0].entered_zones_ready &&
                x.exploration[0].entered_zone_ids.empty(),
            "validated empty List distinct from unavailable entered zones");
    }
    for (unsigned malformed = 0; malformed < 12; ++malformed) {
      M m;
      m.exploration();
      switch (malformed) {
      case 0:
        m.put<belmont::U64>(H + 0x21050, 0);
        break;
      case 1:
        m.put<belmont::U64>(H + 0x21050, 3);
        break;
      case 2:
        m.text(H + 0x27201, "WrongList");
        break;
      case 3:
        m.text(H + 0x27301, "Wrong.Namespace");
        break;
      case 4:
        m.put<std::int32_t>(H + 0x26018, -1);
        break;
      case 5:
        m.put<std::int32_t>(H + 0x26018, 4097);
        break;
      case 6:
        m.put<belmont::U64>(H + 0x26010, 0);
        break;
      case 7:
        m.put<belmont::U64>(H + 0x2e018, 1);
        break;
      case 8:
        m.put<belmont::U64>(H + 0x2e018, 8193);
        break;
      case 9:
        m.put<std::int32_t>(H + 0x2e024, -1);
        break;
      case 10:
        m.put<std::int32_t>(H + 0x2e024, 65536);
        break;
      case 11:
        m.put<std::int32_t>(H + 0x2e024, 0);
        break;
      }
      auto x = m.sample();
      Check(x.coherent && x.exploration_ready && !x.exploration_zones_ready &&
                !x.exploration[0].entered_zones_ready &&
                x.exploration[0].entered_zone_ids.empty(),
            "malformed/duplicate/null zones unavailable without coercing empty "
            "or dropping valid cells");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x26018, 1025);
      m.put<belmont::U64>(H + 0x2e018, 1025);
      for (int i = 0; i < 1025; ++i)
        m.put<std::int32_t>(H + 0x2e020 + 4 * i, i);
      auto x = m.sample();
      Check(x.exploration_zones_ready &&
                x.exploration[0].entered_zone_ids.size() == 1025 &&
                x.exploration[0].entered_zone_ids.back() == 1024,
            "zone IDs span more than one bounded memory read");
    }
    for (auto at : {H + 0x2601c, H + 0x2e020, H + 0x26010, H + 0x21050}) {
      M m;
      m.exploration();
      unsigned reads = 0;
      m.observe = [&](auto address) {
        if (address == at && ++reads == 2)
          m.data[at] ^= 1;
      };
      auto x = m.sample();
      Check(!x.coherent && !x.exploration_zones_ready && !x.exploration_ready &&
                x.exploration.empty(),
            "list version/item/backing-pointer/state-pointer race clears whole "
            "snapshot");
    }
    for (auto offset : {0x1c298a8, 0x1c296b0, 0x1c296e0}) {
      M m;
      m.exploration();
      m.data[M::Base + offset] ^= 1;
      auto x = m.sample();
      Check(x.reason == "accessor_mismatch" && !x.exploration_zones_ready &&
                x.exploration.empty(),
            "entered-zone original accessor pins required");
    }
    {
      M m;
      m.exploration();
      auto read = [&](auto at, auto *dst, auto n) { return m(at, dst, n); };
      belmont::Decoder decoder(read);
      Check(decoder.Sample(m.id()).exploration_zones_ready,
            "reused decoder starts ready");
      m.put<belmont::U64>(H + 0x21050, 0);
      auto x = decoder.Sample(m.id());
      Check(x.exploration_ready && !x.exploration_zones_ready &&
                x.exploration[0].entered_zone_ids.empty(),
            "unavailable list clears prior IDs");
      m.put<std::uint8_t>(H + 0x2400, 1);
      x = decoder.Sample(m.id());
      Check(!x.exploration_zones_ready && x.exploration.empty(),
            "loading clears prior zone readiness");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x21010, 41);
      m.put<std::int32_t>(H + 0x21014, 25);
      m.put(H + 0x21018, H + 0x30000);
      m.put<belmont::U64>(H + 0x30018, 1025);
      for (unsigned i = 0; i < 1025; ++i)
        m.put<std::int32_t>(H + 0x30020 + 4 * i, i % 4);
      auto x = m.sample();
      Check(x.exploration_ready && x.exploration.size() == 1 &&
                x.exploration[0].cells.size() == 1025 &&
                x.exploration[0].cells[1023] == 3 &&
                x.exploration[0].cells[1024] == 0,
            "cell spans cross 4096-byte read limit without truncating tail");
    }
    {
      M m;
      m.components();
      auto x = m.sample();
      Check(x.exploration_ready && x.exploration.empty(),
            "known empty dictionary distinct from unavailable exploration");
    }
    for (auto state : {1, 2}) {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x10020, state);
      auto x = m.sample();
      Check(!x.room_candidate && !x.exploration_ready && x.exploration.empty(),
            "transit and undefined room hide exploration");
    }
    for (auto object : {H + 0xa000, H + 0xe000, H + 0x10000, H + 0x17000}) {
      M m;
      m.exploration();
      m.put<belmont::U64>(object + 0x10, 0);
      auto x = m.sample();
      Check(!x.exploration_ready && x.exploration.empty(),
            "dead native entity/component/room cannot expose exploration");
    }
    {
      M m;
      m.exploration();
      m.put<belmont::U64>(H + 0xe010, H + 0xdead00);
      Check(!m.sample().exploration_ready,
            "unreadable native cached pointer rejected");
    }
    {
      M m;
      m.exploration();
      m.put<belmont::U64>(H + 0x11010, 0);
      Check(!m.sample().position_candidate,
            "native position component liveness required");
    }
    for (auto value : {-1, 4, INT32_MAX}) {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x23028, value);
      auto x = m.sample();
      Check(!x.exploration_ready && x.exploration.empty(),
            "invalid enum clears complete mask instead of coercing reveal");
    }
    for (auto size : {0, 3, 5, 4097}) {
      M m;
      m.exploration();
      m.put<belmont::U64>(H + 0x23018, size);
      Check(!m.sample().exploration_ready,
            "exact bounded mask length required");
    }
    for (auto width : {-1, 0, 257, INT32_MAX}) {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x21010, width);
      Check(!m.sample().exploration_ready, "malformed dimensions unavailable");
    }
    {
      M m;
      m.exploration();
      m.put<std::uint8_t>(H + 0x21020, 2);
      Check(!m.sample().exploration_ready, "invalid pending bool unavailable");
    }
    {
      M m;
      m.exploration();
      m.text(H + 0x22201, "WrongStateClass");
      Check(!m.sample().exploration_ready, "exploration value class required");
    }
    {
      M m;
      m.exploration();
      m.text(H + 0x25301, "WrongNamespace");
      Check(!m.sample().exploration_ready, "key must be System.String");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x24010, 129);
      Check(!m.sample().exploration_ready, "overlong room ID unavailable");
    }
    {
      M m;
      m.exploration();
      m.put<std::uint16_t>(H + 0x24014, 0xd800);
      Check(!m.sample().exploration_ready, "malformed UTF16 ID unavailable");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x20024, 1);
      Check(!m.sample().exploration_ready,
            "exploration next index bounded by allocated");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x15020, 2);
      m.put<std::int32_t>(H + 0x20038, 1);
      m.put<std::int32_t>(H + 0x2003c, -1);
      m.put(H + 0x20040, H + 0x24000);
      m.put(H + 0x20048, H + 0x21000);
      auto x = m.sample();
      Check(!x.exploration_ready && x.exploration.empty(),
            "duplicate IDs discard all decoded room records");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x15020, 2);
      m.put<std::int32_t>(H + 0x15028, 1);
      m.put<std::int32_t>(H + 0x20038, -1);
      auto x = m.sample();
      Check(x.exploration_ready && x.exploration.size() == 1,
            "deleted exploration entries ignored before dereferencing values");
    }
    {
      M m;
      m.exploration();
      m.put<std::int32_t>(H + 0x15028, 1);
      Check(!m.sample().exploration_ready,
            "live exploration count matches hashes");
    }
    {
      M m;
      m.exploration();
      unsigned reads = 0;
      m.observe = [&](auto at) {
        if (at == H + 0x23020 && ++reads == 2)
          m.put<std::int32_t>(H + 0x23028, 3);
      };
      auto x = m.sample();
      Check(!x.coherent && !x.exploration_ready && x.exploration.empty(),
            "in-place cell mutation clears mask despite unchanged dictionary "
            "version");
    }
    {
      M m;
      m.exploration();
      unsigned reads = 0;
      m.observe = [&](auto at) {
        if (at == H + 0x1502c && ++reads == 3)
          m.put<std::int32_t>(at, 8);
      };
      auto x = m.sample();
      Check(!x.coherent && !x.exploration_ready && x.exploration.empty(),
            "dictionary version change clears exploration");
    }
    {
      M m;
      m.exploration();
      belmont::Decoder d(m);
      Check(d.Sample(m.id()).exploration_ready,
            "initial synthetic mask available");
      m.put<std::uint8_t>(H + 0x2400, 1);
      auto x = d.Sample(m.id());
      Check(!x.exploration_ready && x.exploration.empty() && !x.map_ready,
            "loading on reused decoder clears preceding mask");
      m.put<std::uint8_t>(H + 0x2400, 0);
      m.put<belmont::U64>(H + 0x50b0, 0);
      Check(!d.Sample(m.id()).exploration_ready,
            "entity loss clears preceding mask");
    }
    {
      M m;
      m.exploration();
      const auto state = H + 0x28000, klass = H + 0x29000;
      m.put(state, klass);
      m.klass(klass, "EditorParameteredGameState", "");
      m.native(state);
      m.put(H + 0x9018, state);
      for (unsigned i = 0; i < 14; ++i)
        m.put<std::uint8_t>(state + 0x18 + i, 0);
      m.put<std::uint8_t>(state + 0x18, 1);
      m.put<std::uint8_t>(state + 0x19, 1);
      m.put<std::uint8_t>(state + 0x23, 1);
      auto x = m.sample();
      Check(
          x.paused == 1 && x.initial_identity == 0 && x.exploration_ready &&
              x.exploration_zones_ready && !x.map_ready,
          "native paused state retains only freshly read bounded exploration");
      m.put<std::uint8_t>(state + 0x19, 0);
      x = m.sample();
      Check(x.paused == 0 && !x.exploration_ready && x.exploration.empty(),
            "cinematic-like state clears paused exploration");
      m.put<std::uint8_t>(state + 0x19, 2);
      Check(m.sample().paused == -1, "invalid pause flags unavailable");
      m.put<std::uint8_t>(state + 0x19, 1);
      m.put<std::uint8_t>(H + 0x1400, 1);
      x = m.sample();
      Check(!x.exploration_ready && x.exploration.empty(),
            "loading dominates paused state");
    }
#ifndef BELMONT_READER_ONLY_TEST
    {
      Host h;
      auto host = h.api();
      const auto *module = eden_dsmod_get_module(EDEN_DSMOD_MODULE_ABI_VERSION,
                                                 EDEN_DSMOD_MODULE_ABI_HASH);
      Check(module && module->capabilities == (EDEN_DSMOD_CAP_EXTENSIONS |
                                               EDEN_DSMOD_CAP_ROMFS_READ | EDEN_DSMOD_CAP_WRITE_MEMORY),
            "module declares assets and bounded native placement write capability without guest calls");
      void *instance = module->create(&host, nullptr);
      Check(instance, "create exact build");
      const auto* visibility=eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION,EDEN_DSMOD_EXT_HASH);
      Check(visibility&&visibility->on_action(instance,"equipment_visibility",1),"Equipment visibility handshake accepts entry");
      module->sample(instance,&host);
      Check(h.values["belmont.equipment_visible"]==1,"entry acknowledges visibility even before optional assets");
      Check(visibility->on_action(instance,"equipment_visibility",0),"Equipment visibility handshake accepts exit");
      module->sample(instance, &host);
      Check(h.writes==0,"sampling and Equipment visibility never write guest memory without an explicit pin placement");
      Check(h.values["belmont.equipment_visible"]==0&&!h.values.contains("belmont.picker_slot_0_ready"),
            "hidden Equipment acknowledges exit and omits unused picker outputs");
      Check(!visibility->on_action(instance,"equipment_visibility",2),"invalid visibility values rejected");
      Check(h.values["belmont.initial_identity"] == 1 &&
                h.values["belmont.entity_ready"] == 1,
            "ABI sample publishes state");
      Check(h.values["belmont.map_ready"] == 0 &&
                h.values["belmont.marker_ready"] == 0,
            "unverified map never available");
      h.values["belmont.map_ready"] = h.values["belmont.marker_ready"] = 1;
      h.texts["belmont.map_image"] = "stale"; h.texts["belmont.map_area"] = "stale";
      h.texts["belmont.area_name"] = "stale"; h.numbers["belmont.player_x"] = 999;h.numbers["belmont.marker_x"]=256000;h.numbers["belmont.marker_y"]=256000;
      for(const auto* key:{"belmont.equipment_ready","belmont.equipment_assets_ready","belmont.equipment_weapon_ready","belmont.equipment_arcana_ready","belmont.equipment_atk_ready","belmont.equipment_hp_ready"})h.values[key]=1;
      for(const auto* key:{"belmont.equipment_input_button","belmont.equipment_control_ready","belmont.picker_ready","belmont.pin_picker","belmont.pin_armed"})h.values[key]=4;
      h.texts["belmont.equipment_weapon_name"]="stale";h.texts["belmont.equipment_arcana_image"]="stale";
      h.texts["belmont.equipment_atk_text"]="stale";h.texts["belmont.equipment_hp_text"]="stale";
      h.memory.put<belmont::U64>(Memory::Heap + 0x50b0, 0);
      h.memory.put<std::uint8_t>(Memory::Heap + 0x1400, 1);
      module->sample(instance, &host);
      Check(h.values["belmont.entity_ready"] == 0 && h.reason == "loading",
            "new sample clears previous readiness");
      Check(h.values["belmont.map_ready"] == 0 && h.values["belmont.marker_ready"] == 0 &&
                h.values["belmont.ui_loading"] == 1 && h.values["belmont.ui_world"] == 0 &&
                h.texts["belmont.map_image"].empty() && h.texts["belmont.map_area"].empty() &&
                h.texts["belmont.area_name"].empty() && h.numbers["belmont.player_x"] == 0 &&
                h.numbers["belmont.view_x0"] == 0,
            "ABI loading actively clears stale texture/name/player/home fields");
      Check(h.numbers["belmont.marker_x"]==0&&h.numbers["belmont.marker_y"]==0,"loading clears scaled presentation marker coordinates");
      for(const auto* key:{"belmont.equipment_ready","belmont.equipment_assets_ready","belmont.equipment_weapon_ready","belmont.equipment_arcana_ready","belmont.equipment_atk_ready","belmont.equipment_hp_ready"})Check(h.values[key]==0,"ABI loading clears equipped assets/final-stat gates");
      Check(h.texts["belmont.equipment_weapon_name"].empty()&&h.texts["belmont.equipment_arcana_image"].empty()&&h.texts["belmont.equipment_atk_text"].empty()&&h.texts["belmont.equipment_hp_text"].empty(),"ABI loading clears equipped names/card keys/final text");
      for(const auto* key:{"belmont.equipment_input_button","belmont.equipment_control_ready","belmont.picker_ready","belmont.pin_picker","belmont.pin_armed"})Check(h.values[key]==0,"loading clears native controller pulses and interactive pickers");
      host.build_id[31] = 1;
      const auto reads = h.memory.reads.load();
      module->sample(instance, &host);
      Check(h.values["belmont.initial_identity"] == -1 &&
                h.values["belmont.build_supported"] == 0 &&
                h.memory.reads == reads,
            "changed runtime build clears state without reads");
      Check(!module->create(&host, nullptr), "create rejects changed build");
      const auto* images = eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
      const auto* data = eden_dsmod_get_data_extensions(EDEN_DSMOD_DATA_EXT_VERSION, EDEN_DSMOD_DATA_EXT_HASH);
      const auto* fonts = eden_dsmod_get_font_extensions(EDEN_DSMOD_FONT_EXT_VERSION, EDEN_DSMOD_FONT_EXT_HASH);
      Check(images && data && fonts && data->load_data, "asset/font/data ABI exports negotiate");
      Check(!eden_dsmod_get_data_extensions(EDEN_DSMOD_DATA_EXT_VERSION, 0),
            "wrong data-extension ABI hash rejected");
      unsigned sinks{};
      const auto image_sink = +[](void* p, std::uint32_t, std::uint32_t, const std::uint8_t*, std::size_t) { ++*static_cast<unsigned*>(p); };
      const auto data_sink = +[](void* p, const std::uint8_t*, std::size_t) { ++*static_cast<unsigned*>(p); };
      const auto font_sink = +[](void* p, std::uint32_t, std::uint32_t, const EdenDsmodFontGlyph*, std::uint32_t) { ++*static_cast<unsigned*>(p); };
      const auto romfs_before = h.romfs_reads.load();
      const std::string descriptor = "BELMONT_FONT_V1 LatinFont 51\n";
      Check(!images->load_image(instance, &host, "module:belmont:rose-marker", &sinks, image_sink) &&
                !data->load_data(instance, &host, "module:belmont:areas", &sinks, data_sink) &&
                !fonts->decode_font(instance, reinterpret_cast<const std::uint8_t*>(descriptor.data()),
                                    descriptor.size(), &sinks, font_sink) &&
                h.romfs_reads == romfs_before && sinks == 0 && h.memory.reads == reads,
            "changed-build callbacks do not read guest/RomFS or invoke any sink");
      module->destroy(instance);
    }
#endif
#ifndef BELMONT_READER_ONLY_TEST
    {
      Host h;h.memory.exploration();auto host=h.api();
      const auto* module=eden_dsmod_get_module(EDEN_DSMOD_MODULE_ABI_VERSION,EDEN_DSMOD_MODULE_ABI_HASH);
      const auto* actions=eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION,EDEN_DSMOD_EXT_HASH);
      void* instance=module->create(&host,nullptr);
      module->sample(instance,&host);
      Check(h.values["belmont.ui_world"]==1&&h.values["belmont.presentation_blocked"]==0,
            "coherent world is interactive");
      unsigned count{};
      h.memory.observe=[&](auto at){
        if(at==H+0x11040&&++count==2)h.memory.put<std::uint32_t>(at,0x40200000);
      };
      module->sample(instance,&host);h.memory.observe={};
      Check(h.texts["belmont.live_state_reason"]=="incoherent"&&
            h.values["belmont.live_world"]==0&&h.values["belmont.ui_world"]==1&&
            h.values["belmont.presentation_dim"]==0&&h.values["belmont.presentation_blocked"]==1&&
            h.values["belmont.diag.incoherent_samples"]==1&&
            h.values["belmont.equipment_input_button"]==0&&
            !actions->on_action(instance,"equipment_category",1)&&
            !actions->on_action(instance,"pin_begin",0),
            "movement race preserves appearance while fresh-read guards block actions");
      module->sample(instance,&host);
      Check(h.values["belmont.live_world"]==1&&h.values["belmont.presentation_blocked"]==0,
            "coherent recovery restores interaction");
      h.memory.put<std::uint8_t>(H+0x1400,1);module->sample(instance,&host);
      Check(h.values["belmont.ui_world"]==0&&h.values["belmont.ui_loading"]==1&&
            h.texts["belmont.map_image"].empty(),"explicit loading still clears retained gameplay");
      module->destroy(instance);
    }
    {
      Host h;
      h.memory.exploration();
      auto host = h.api();
      const auto* module = eden_dsmod_get_module(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
      const auto* images = eden_dsmod_get_extensions(EDEN_DSMOD_EXT_VERSION, EDEN_DSMOD_EXT_HASH);
      const auto* data = eden_dsmod_get_data_extensions(EDEN_DSMOD_DATA_EXT_VERSION, EDEN_DSMOD_DATA_EXT_HASH);
      void* instance = module->create(&host, nullptr);
      Check(instance, "second exact-build instance");
      module->sample(instance, &host);
      Check(h.values["belmont.ui_world"] == 1 && h.values["belmont.map_ready"] == 0 &&
                h.values["belmont.marker_ready"] == 0 && h.values["belmont.native_assets_ready"] == 0 &&
                h.texts["belmont.map_image"].empty() && h.texts["belmont.area_name"].empty(),
            "coherent synthetic world alone cannot promote unknown room or absent native renderer/assets");
      unsigned sinks{};
      const auto image_sink = +[](void* p, std::uint32_t, std::uint32_t, const std::uint8_t*, std::size_t) { ++*static_cast<unsigned*>(p); };
      const auto data_sink = +[](void* p, const std::uint8_t*, std::size_t) { ++*static_cast<unsigned*>(p); };
      const auto guest_before = h.memory.reads.load(), romfs_before = h.romfs_reads.load();
      Check(!images->load_image(instance, &host, "module:belmont:map:0", &sinks, image_sink) &&
                !images->load_image(instance, &host, "module:belmont:map:999999", &sinks, image_sink) &&
                !images->load_image(instance, &host, "module:belmont:equipment:999999:weapon", &sinks, image_sink) &&
                !data->load_data(instance, &host, "module:belmont:unknown", &sinks, data_sink) &&
                sinks == 0 && h.memory.reads == guest_before && h.romfs_reads == romfs_before,
            "unknown/unready map generations and unknown data keys fail before any I/O/sink");
      Check(!data->load_data(instance, &host, "module:belmont:areas", &sinks, data_sink) &&
                h.romfs_reads > romfs_before && h.memory.reads == guest_before && sinks == 0,
            "once-only area loader reads native RomFS only and declines missing sources without fake bounds");
      const auto reads_before_worker = h.memory.reads.load();
      const auto publications_before_worker = h.publications.load();
      const auto romfs_before_worker = h.romfs_reads.load();
      EdenDsmodHostExtensions extensions{};
      extensions.version = EDEN_DSMOD_EXT_VERSION;
      extensions.struct_size = sizeof(extensions);
      extensions.abi_hash = EDEN_DSMOD_EXT_HASH;
      images->configure(instance, &extensions);
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (h.romfs_reads == romfs_before_worker && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      Check(h.romfs_reads > romfs_before_worker && h.memory.reads == reads_before_worker &&
                h.publications == publications_before_worker,
            "configured background preparation uses RomFS, never guest-memory reads or host publication");
      h.memory.put<std::uint8_t>(Memory::Heap + 0x1400, 1);
      module->sample(instance, &host);
      Check(h.values["belmont.ui_world"] == 0 && h.values["belmont.ui_loading"] == 1 &&
                h.values["belmont.map_ready"] == 0 && h.texts["belmont.map_image"].empty(),
            "loading invalidates queued/preparing candidate while worker is active");
      module->destroy(instance); // joins preparation and idle map worker before userdata dies
    }
#endif
    {
      M m; m.components();
      m.bytes(M::Base+0x1c28230,{0x00,0x30,0x40,0xf9,0xc0,0x03,0x5f,0xd6});
      m.bytes(M::Base+0x1e0fff0,{0x00,0x0c,0x40,0xf9,0xc0,0x03,0x5f,0xd6});
      m.put<std::uint8_t>(H+0xe0a8,0);m.put(H+0xe060,H+0x60000);
      m.put(H+0x60000,H+0x70000);m.klass(H+0x70000,"List`1","System.Collections.Generic");
      m.put(H+0x60010,H+0x61000);m.put<std::int32_t>(H+0x60018,1);m.put<std::int32_t>(H+0x6001c,1);
      m.put<belmont::U64>(H+0x61018,2);m.put(H+0x61020,H+0x62000);
      m.put(H+0x62000,H+0x72000);m.klass(H+0x72000,"LandmarkData","");m.native(H+0x62000);
      m.put(H+0x62018,H+0x63000);m.put(H+0x63000,H+0x74000);m.klass(H+0x74000,"String","System");
      constexpr char guid[]="0123456789abcdef0123456789abcdef";
      m.put<std::int32_t>(H+0x63010,32);for(unsigned n=0;n<32;++n)m.put<std::uint16_t>(H+0x63014+2*n,guid[n]);
      auto x=m.sample();Check(x.landmarks_ready&&x.discovered_landmarks==std::vector<std::string>{guid},"native landmark discovery GUIDs decoded");
      m.put<std::uint8_t>(H+0xe0a8,1);x=m.sample();Check(!x.landmarks_ready&&x.discovered_landmarks.empty(),"debug reveal does not leak undiscovered landmarks");
      m.put<std::uint8_t>(H+0xe0a8,0);m.put<std::int32_t>(H+0x60018,2);m.put(H+0x61028,H+0x62000);
      x=m.sample();Check(!x.landmarks_ready&&x.discovered_landmarks.empty(),"duplicate discovered landmarks rejected");
      m.put<std::int32_t>(H+0x60018,1025);Check(!m.sample().landmarks_ready,"discovery list bounded");
      m.put<std::int32_t>(H+0x60018,1);m.put<std::uint8_t>(H+0x1400,1);x=m.sample();
      Check(!x.landmarks_ready&&x.discovered_landmarks.empty(),"loading clears discovery");
    }
    {
      M m; m.exploration(); m.put<std::uint8_t>(H + 0xe0a8, 0);
      auto full = m.sample();
      Check(full.coherent && full.exploration_ready && !full.motion_only,
            "motion source is full coherent exploration");
      unsigned history_reads{};
      m.observe = [&](auto at) {
        if (at >= H + 0x20020 && at < H + 0x30000) ++history_reads;
      };
      belmont::Decoder motion_decoder(m);
      const auto before = m.reads.load();
      m.put<std::uint32_t>(H + 0x11040, 0x40200000); // 2.5
      auto motion = motion_decoder.SampleMotion(m.id(), full);
      const auto motion_reads = m.reads.load() - before;
      Check(motion.coherent && motion.components_ready && motion.motion_only &&
            motion.target_position[0] == 2.5f && motion.room_id == full.room_id &&
            motion.exploration.size() == full.exploration.size() &&
            motion.exploration[0].entered_zone_ids == full.exploration[0].entered_zone_ids &&
            history_reads == 0,
            "motion uses fresh position and stable copied exploration without history reads");
      auto chained = motion_decoder.SampleMotion(m.id(), motion);
      Check(!chained.components_ready && !chained.position_candidate &&
            chained.reason == "motion_refresh_required" && chained.exploration.empty(),
            "motion snapshots cannot extend full-history freshness by chaining");
      std::cout << "Motion sample guest reads " << motion_reads << ", history reads " << history_reads << '\n';
      m.observe = {};
      m.put<std::int32_t>(H + 0xc02c, 11);
      auto version = motion_decoder.SampleMotion(m.id(), full);
      Check(!version.components_ready && version.exploration.empty() && version.target_position[0] == 0,
            "component dictionary version change forces full refresh");
      m.put<std::int32_t>(H + 0xc02c, 10);
      m.put<std::int32_t>(H + 0xd028, 999);
      Check(!motion_decoder.SampleMotion(m.id(), full).components_ready,
            "same-version component layout mutation forces refresh");
      m.put<std::int32_t>(H + 0xd028, 123);
      m.bytes(H + 0x1b014, {'n', 0, 'e', 0, 'w', 0, 'r', 0});
      auto room = motion_decoder.SampleMotion(m.id(), full);
      Check(!room.components_ready && room.room_id.empty() && room.exploration.empty(),
            "same-pointer room identity change rejects reused exploration");
      m.bytes(H + 0x1b014, {'r', 0, 'o', 0, 'o', 0, 'm', 0});
      m.put<belmont::U64>(H + 0x11010, 0);
      Check(!motion_decoder.SampleMotion(m.id(), full).components_ready,
            "destroyed position component cannot reuse cached pointer");
      m.native(H + 0x11000);
      m.put<std::uint8_t>(H + 0xe0a8, 1);
      Check(!motion_decoder.SampleMotion(m.id(), full).components_ready,
            "debug reveal mutation rejects cached exploration");
      m.put<std::uint8_t>(H + 0xe0a8, 0);
      m.put<std::uint8_t>(H + 0x1400, 1);
      auto loading = motion_decoder.SampleMotion(m.id(), full);
      Check(loading.coherent && loading.application_loading == 1 &&
            !loading.components_ready && loading.exploration.empty(),
            "motion reads current loading flags and clears history");
      m.put<std::uint8_t>(H + 0x1400, 0);
      const auto state = H + 0x28000, klass = H + 0x29000;
      m.put(state, klass); m.klass(klass, "EditorParameteredGameState", "");
      m.native(state); m.put(H + 0x9018, state);
      for (unsigned i = 0; i < 14; ++i) m.put<std::uint8_t>(state + 0x18 + i, 0);
      m.put<std::uint8_t>(state + 0x18, 1); m.put<std::uint8_t>(state + 0x19, 1);
      m.put<std::uint8_t>(state + 0x23, 1);
      auto pause = motion_decoder.SampleMotion(m.id(), full);
      Check(pause.coherent && pause.paused == 1 && pause.initial_identity == 0 &&
            pause.components_ready && pause.exploration_ready,
            "motion validates fresh native pause flags while retaining same world");
      m.put<std::uint8_t>(state + 0x19, 0);
      auto cinematic = motion_decoder.SampleMotion(m.id(), full);
      Check(cinematic.coherent && cinematic.paused == 0 &&
            !cinematic.components_ready && cinematic.exploration.empty(),
            "motion validates cinematic flags instead of inheriting cached pause");
      m.put(H + 0x9018, H + 0x6000);
      auto bad_identity = m.id(); bad_identity.build[31] = 1;
      const auto before_wrong = m.reads.load();
      Check(!motion_decoder.SampleMotion(bad_identity, full).supported &&
            m.reads == before_wrong, "motion wrong build performs no guest reads");
      unsigned version_observations{};
      m.observe = [&](auto at) {
        if (at == H + 0xc02c && ++version_observations == 3)
          m.put<std::int32_t>(H + 0xc02c, 11);
      };
      Check(!motion_decoder.SampleMotion(m.id(), full).coherent,
            "mid-sample component mutation rejected by journal");
    }
    {
      const auto save_fields=[](M& m){
        m.exploration();
        m.bytes(M::Base+0x1c28f7c,{0x88,0x02,0x01,0x39});
        m.bytes(M::Base+0x1c2b574,{0x08,0x05,0x41,0x39});
        m.bytes(M::Base+0x1dedc10,{0x00,0x45,0x40,0xf9});
        m.bytes(H+0x21040,{1,1});
        m.put(H+0xe088,H+0x24000);
        m.put<std::uint8_t>(H+0xe0a8,0);
      };
      M m;save_fields(m);
      auto full=m.sample();
      Check(full.coherent&&full.exploration_ready&&full.exploration[0].is_save_room==1&&
            full.exploration[0].used_save_point==1&&full.last_used_save_room_ready&&full.last_used_save_room=="room",
            "pinned native savepoint flags and last-used room are optional coherent data");
      belmont::Decoder motion(m);
      auto fast=motion.SampleMotion(m.id(),full);
      Check(fast.coherent&&fast.motion_only&&fast.exploration[0].used_save_point==1&&fast.last_used_save_room=="room",
            "motion retains optional metadata only from validated full provenance");
      m.put<belmont::U64>(H+0xe088,0);
      auto empty=m.sample();
      Check(empty.exploration_ready&&empty.last_used_save_room_ready&&empty.last_used_save_room.empty(),
            "native null last-used string is known empty rather than a fabricated active savepoint");
      m.put<belmont::U64>(H+0xe088,3);
      auto invalid=m.sample();
      Check(invalid.coherent&&invalid.exploration_ready&&!invalid.last_used_save_room_ready&&invalid.last_used_save_room.empty()&&invalid.exploration[0].used_save_point==1,
            "invalid optional last-used pointer hides its marker without discarding the map");
      // Use a separate managed String so malformed optional data does not
      // also corrupt the core fixture's room dictionary key.
      m.put(H+0xe088,H+0x31000);m.put(H+0x31000,H+0x25000);
      m.put<std::int32_t>(H+0x31010,1);m.put<std::uint16_t>(H+0x31014,0xd800);
      auto malformed=m.sample();
      Check(malformed.coherent&&malformed.exploration_ready&&!malformed.last_used_save_room_ready,
            "malformed optional UTF16 clears last-used value and preserves core room keys");
      save_fields(m);m.bytes(H+0x21040,{1,2});
      auto bad_bool=m.sample();
      Check(bad_bool.coherent&&bad_bool.exploration_ready&&bad_bool.exploration[0].is_save_room==-1&&bad_bool.exploration[0].used_save_point==-1,
            "invalid native optional bool pair remains unknown without affecting map availability");
      save_fields(m);m.data.erase(H+0x21041);
      auto unavailable=m.sample();
      Check(unavailable.coherent&&unavailable.exploration_ready&&unavailable.exploration[0].used_save_point==-1,
            "unmapped optional savepoint bools do not break the map");
      save_fields(m);m.data[M::Base+0x1c2b574]^=1;
      auto unpinned=m.sample();
      Check(unpinned.coherent&&unpinned.exploration_ready&&unpinned.exploration[0].used_save_point==-1&&!unpinned.last_used_save_room_ready,
            "optional accessor mismatch disables only savepoint metadata");
      save_fields(m);unsigned observed=0;
      m.observe=[&](auto at){if(at==H+0x21040&&++observed==2)m.put<std::uint8_t>(H+0x21041,0);};
      auto race=m.sample();m.observe={};
      Check(race.coherent&&race.exploration_ready&&race.exploration[0].used_save_point==-1&&!race.last_used_save_room_ready,
            "optional savepoint race clears all optional marker data without invalidating core map snapshot");
      save_fields(m);observed=0;
      m.observe=[&](auto at){if(at==H+0xe088&&++observed==2)m.put<belmont::U64>(at,0);};
      race=m.sample();m.observe={};
      Check(race.coherent&&race.exploration_ready&&!race.last_used_save_room_ready&&race.exploration[0].used_save_point==-1,
            "last-used room pointer race cannot produce a mixed native save marker snapshot");
    }
    {
      const auto setup=[](M& m){
        m.exploration();
        m.bytes(M::Base+0x1c28f7c,{0x88,0x02,0x01,0x39});m.bytes(M::Base+0x1c2b574,{0x08,0x05,0x41,0x39});m.bytes(M::Base+0x1dedc10,{0x00,0x45,0x40,0xf9});
        m.bytes(H+0x21040,{1,1});m.put(H+0xe088,H+0x24000);
        m.bytes(M::Base+0x1c281f0,{0x00,0x28,0x40,0xf9,0xc0,0x03,0x5f,0xd6});
        m.bytes(M::Base+0x1c29fc8,{0x88,0xa2,0x42,0x39,0xc8,0x00,0x00,0x34});
        m.bytes(M::Base+0x1c29fe4,{0x80,0x2a,0x40,0xf9,0x60,0xff,0xff,0xb4});
        m.bytes(M::Base+0x1e0fff0,{0x00,0x0c,0x40,0xf9,0xc0,0x03,0x5f,0xd6});
        m.put<std::uint8_t>(H+0xe0a8,0);m.put(H+0xe050,H+0x35000);m.put(H+0x35000,H+0x36000);m.klass(H+0x36000,"List`1","System.Collections.Generic");
        m.put(H+0x35010,H+0x37000);m.put<int>(H+0x35018,1);m.put<int>(H+0x3501c,7);m.put<belmont::U64>(H+0x37018,4);m.put(H+0x37020,H+0x39000);
        m.put(H+0x39000,H+0x3a000);m.klass(H+0x3a000,"LevelID","");m.native(H+0x39000);m.put(H+0x39018,H+0x3c000);m.put(H+0x3c000,H+0x25000);m.put<int>(H+0x3c010,32);
        const char* guid="946565bcb5d5f244e991e1790575a67c";for(unsigned i=0;i<32;++i)m.put<std::uint16_t>(H+0x3c014+2*i,guid[i]);
      };
      M m;setup(m);auto full=m.sample();
      Check(full.coherent&&full.exploration_ready&&full.unlocked_fast_travel_ready&&full.unlocked_fast_travel_levels==std::vector<std::string>{"946565bcb5d5f244e991e1790575a67c"},"native unlocked LevelID GUIDs are optional validated data");
      belmont::Decoder decoder(m);auto motion=decoder.SampleMotion(m.id(),full);
      Check(motion.motion_only&&motion.unlocked_fast_travel_ready&&motion.unlocked_fast_travel_levels==full.unlocked_fast_travel_levels,"motion copies unlocks only from validated full provenance");
      m.put<int>(H+0x35018,0);m.put<belmont::U64>(H+0x35010,0);auto empty=m.sample();
      Check(empty.exploration_ready&&empty.unlocked_fast_travel_ready&&empty.unlocked_fast_travel_levels.empty(),"empty native unlock list means no known unlocked points");
      setup(m);m.put<int>(H+0x35018,129);auto cap=m.sample();Check(cap.exploration_ready&&!cap.unlocked_fast_travel_ready&&cap.unlocked_fast_travel_levels.empty()&&cap.last_used_save_room_ready,"oversize optional list preserves core map and savepoints");
      setup(m);m.put<int>(H+0x35018,2);m.put(H+0x37028,H+0x39000);auto duplicate=m.sample();Check(duplicate.exploration_ready&&!duplicate.unlocked_fast_travel_ready,"duplicate unlock identity fails closed");
      setup(m);m.put<std::uint16_t>(H+0x3c014,'G');auto guid=m.sample();Check(guid.exploration_ready&&!guid.unlocked_fast_travel_ready,"malformed GUID is not an unlocked point");
      setup(m);m.put<std::uint8_t>(H+0xe0a8,1);auto debug=m.sample();Check(debug.exploration_ready&&!debug.unlocked_fast_travel_ready,"debug reveal-all cannot disclose fast travel points");
      setup(m);m.data[M::Base+0x1c281f0]^=1;auto pin=m.sample();Check(pin.exploration_ready&&!pin.unlocked_fast_travel_ready&&pin.last_used_save_room_ready,"unlock accessor mismatch is independent of map/savepoint readiness");
      setup(m);unsigned seen{};m.observe=[&](auto at){if(at==H+0x3501c&&++seen==2)m.put<int>(at,8);};auto race=m.sample();m.observe={};Check(race.coherent&&race.exploration_ready&&!race.unlocked_fast_travel_ready&&race.last_used_save_room_ready&&race.exploration[0].used_save_point==1,"optional unlock race hides blue points without hiding core/savepoints");
      setup(m);seen=0;m.observe=[&](auto at){if(at==H+0x21040&&++seen==2)m.put<std::uint8_t>(H+0x21041,0);};race=m.sample();m.observe={};Check(race.coherent&&race.exploration_ready&&race.unlocked_fast_travel_ready&&!race.last_used_save_room_ready,"savepoint race does not erase independently coherent fast travel");
      setup(m);m.put(H+0xe020,H+0xa008);auto owner=m.sample();Check(!owner.unlocked_fast_travel_ready,"stale Explorer ownership cannot expose unlocks");
    }
    std::cout << "Belmont synthetic state tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
