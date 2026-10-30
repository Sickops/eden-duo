// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_objectives.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

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

// Independent copies of the existing native state fixture keep malformed and
// race checks exercisable without adding a test-only reader entry point.
struct ObjectiveMemory : Memory {
  static constexpr auto Component = Heap + 0x40000, Objective = Heap + 0x42000,
                        Localized = Heap + 0x44000, Guid = Heap + 0x46000;
  void string(belmont::U64 at, const char *text) {
    put(at, Heap + 0x48000);
    klass(Heap + 0x48000, "String", "System");
    put<std::int32_t>(at + 0x10, std::strlen(text));
    for (unsigned i = 0; text[i]; ++i)
      put<std::uint16_t>(at + 0x14 + i * 2, text[i]);
  }
  ObjectiveMemory() {
    components();
    put<std::int32_t>(Heap + 0x701c, 7);
    bytes(Base + 0x1c33c64, {0x93, 0x26, 0, 0xf9});
    bytes(Base + 0x1c33a6c, {0x60, 0x2a, 0x40, 0xf9});
    bytes(Base + 0x1f1ef84, {0x21, 0x10, 0x40, 0xf9});
    bytes(Base + 0x1e0fff0, {0, 0x0c, 0x40, 0xf9});
    bytes(Base + 0x4af6f70, {0, 0x84, 0x40, 0xad});
    bytes(Base + 0x4af72c0, {0, 0x0c, 0xc0, 0x3d});
    bytes(Base + 0x4b03990, {8, 4, 0x41, 0xa9});
    bytes(Base + 0x4b052d0, {0, 0, 0x40, 0xf9});
    // Reuse the dictionary's removed slot as a genuine fourth component.
    put<std::int32_t>(Heap + 0xc028, 0);
    put<std::int32_t>(Heap + 0xd038, 12);
    put(Heap + 0xd048, Component);
    put(Component, Heap + 0x41000);
    klass(Heap + 0x41000, "ProgressionComponent", "");
    native(Component);
    put(Component + 0x50, Heap + 0xa000);
    put(Component + 0x48, Objective);
    put(Objective, Heap + 0x43000);
    klass(Heap + 0x43000, "MainObjective", "");
    native(Objective);
    put(Objective + 0x18, Guid);
    string(Guid, "source-objective-guid");
    put(Objective + 0x20, Localized);
    put<std::int32_t>(Objective + 0x28, 0);
    put(Localized, Heap + 0x45000);
    klass(Heap + 0x45000, "LocalizedString", "UnityEngine.Localization");
    put<belmont::U64>(Localized + 0x10, 0);
    put<std::uint8_t>(Localized + 0x18, 0);
    put<std::int32_t>(Localized + 0x1c, 1);
    for (unsigned i = 0; i < 16; ++i)
      put<std::uint8_t>(Localized + 0x20 + i, i + 1);
    put<std::int64_t>(Localized + 0x30, 987654321);
    put<belmont::U64>(Localized + 0x38, 0);
    put<std::uint8_t>(Localized + 0x40, 0);
    put<std::int32_t>(Localized + 0x44, 2);
  }
  belmont_objectives::Snapshot objective() {
    belmont_objectives::Decoder decoder(*this);
    return decoder.Sample(id());
  }
};
int main() {
  try {
    {
      ObjectiveMemory m;
      auto s = m.objective();
      Check(s.ready && s.coherent &&
                s.objective_guid == "source-objective-guid" &&
                s.description.entry_id == 987654321 &&
                s.description.table_guid[0] == 1,
            "native objective and localization reference resolve");
    }
    {
      ObjectiveMemory m;
      auto id = m.id();
      id.build[0] ^= 1;
      belmont_objectives::Decoder d(m);
      auto s = d.Sample(id);
      Check(!s.ready && m.reads == 0, "wrong build performs no memory read");
    }
    {
      ObjectiveMemory m;
      m.put<std::uint8_t>(Memory::Heap + 0x1400, 1);
      Check(!m.objective().ready, "loading refuses objective");
    }
    {
      ObjectiveMemory m;
      m.put<belmont::U64>(ObjectiveMemory::Component + 0x48, 0);
      Check(!m.objective().ready, "absent objective is unavailable");
    }
    {
      ObjectiveMemory m;
      m.put<std::uint8_t>(Memory::Base + 0x1c33c64, 0);
      Check(!m.objective().ready, "mismatched accessor refuses objective");
    }
    {
      ObjectiveMemory m;
      m.put<belmont::U64>(ObjectiveMemory::Component + 0x50,
                          Memory::Heap + 0xb000);
      Check(!m.objective().ready, "foreign entity refuses objective");
    }
    {
      ObjectiveMemory m;
      m.put<std::int32_t>(ObjectiveMemory::Localized + 0x44, 3);
      Check(!m.objective().ready,
            "unknown localized reference refuses objective");
    }
    {
      ObjectiveMemory m;
      m.put<std::uint16_t>(ObjectiveMemory::Guid + 0x14, 0xd800);
      Check(!m.objective().ready, "malformed UTF16 refuses objective");
    }
    {
      ObjectiveMemory m;
      unsigned reads{};
      m.observe = [&](auto at) {
        if (at == ObjectiveMemory::Component + 0x48 && ++reads == 2)
          m.put<belmont::U64>(at, 0);
      };
      Check(!m.objective().ready,
            "changing objective reference fails coherence");
    }
    {
      ObjectiveMemory m;
      unsigned reads{};
      m.observe = [&](auto at) {
        if (at == ObjectiveMemory::Localized + 0x30 && ++reads == 2)
          m.put<std::int64_t>(at, 123);
      };
      Check(!m.objective().ready, "changing localization ID fails coherence");
    }
    {
      ObjectiveMemory m;
      auto s = m.objective();
      Check(s.ready, "initial ready");
      m.put<belmont::U64>(ObjectiveMemory::Component + 0x48, 0);
      s = m.objective();
      Check(!s.ready && s.objective_guid.empty() && !s.objective &&
                !s.description.entry_id,
            "failure clears previously available objective");
    }
    {
      ObjectiveMemory m;
      const auto source = m.sample();
      m.put<belmont::U64>(Memory::Heap + 0x50b0, Memory::Heap + 0x70000);
      belmont_objectives::Decoder decoder(m);
      Check(!decoder.Sample(m.id(), source).ready,
            "supplied coherent snapshot with a stale entity is rejected");
    }
    for (const auto address : {Memory::Heap + 0x1400, Memory::Heap + 0x2400}) {
      ObjectiveMemory m;
      const auto source = m.sample();
      m.observe = [&](auto at) {
        if (at == ObjectiveMemory::Localized + 0x44)
          m.put<std::uint8_t>(address, 1);
      };
      belmont_objectives::Decoder decoder(m);
      const auto result = decoder.Sample(m.id(), source);
      Check(!result.ready && result.reason == "incoherent" &&
                result.objective_guid.empty(),
            "application/room loading beginning during copy clears objective");
    }
    for (const auto address : {Memory::Heap + 0x4400, Memory::Heap + 0x50b0,
                               Memory::Heap + 0x9018}) {
      ObjectiveMemory m;
      const auto source = m.sample();
      m.observe = [&](auto at) {
        if (at == ObjectiveMemory::Localized + 0x44)
          m.put<belmont::U64>(address, Memory::Heap + 0x70000);
      };
      belmont_objectives::Decoder decoder(m);
      const auto result = decoder.Sample(m.id(), source);
      Check(!result.ready && result.reason == "incoherent",
            "manager/entity/active replacement during copy fails closed");
    }
    {
      ObjectiveMemory m;
      const auto source = m.sample();
      unsigned reads{};
      m.observe = [&](auto at) {
        if (at == Memory::Heap + 0x50b0 && ++reads == 3)
          m.put<belmont::U64>(at, Memory::Heap + 0x70000);
      };
      belmont_objectives::Decoder decoder(m);
      Check(!decoder.Sample(m.id(), source).ready,
            "manager entity mutation during final journal replay is rejected");
    }
    {
      ObjectiveMemory m;
      const auto source = m.sample();
      m.put<std::uint8_t>(Memory::Base + 0x40d4b44, 0);
      belmont_objectives::Decoder decoder(m);
      Check(!decoder.Sample(m.id(), source).ready,
            "supplied snapshot cannot bypass native loading accessor pins");
    }
    {
      ObjectiveMemory m;
      const auto state = Memory::Heap + 0x4a000;
      m.put(state, Memory::Heap + 0x4b000);
      m.klass(Memory::Heap + 0x4b000, "EditorParameteredGameState", "");
      m.native(state);
      m.put(Memory::Heap + 0x9018, state);
      for (unsigned n = 0; n < 14; ++n)
        m.put<std::uint8_t>(state + 0x18 + n, 0);
      m.put<std::uint8_t>(state + 0x18, 1);
      m.put<std::uint8_t>(state + 0x19, 1);
      m.put<std::uint8_t>(state + 0x23, 1);
      const auto source = m.sample();
      belmont_objectives::Decoder decoder(m);
      Check(source.paused == 1 && decoder.Sample(m.id(), source).ready,
            "coherent native paused state retains objective");
      m.observe = [&](auto at) {
        if (at == ObjectiveMemory::Localized + 0x44)
          m.put<std::uint8_t>(state + 0x19, 0);
      };
      Check(!decoder.Sample(m.id(), source).ready,
            "pause-to-cinematic flags during copy clear objective");
    }
    unsigned baseline_reads{};
    for (const int rooms : {1, 64}) {
      ObjectiveMemory m;
      m.exploration();
      // Genuine bounded native exploration dictionaries of different sizes,
      // not merely a bigger already-copied source vector.
      m.put<std::int32_t>(Memory::Heap + 0xc028, 0);
      m.put<std::int32_t>(Memory::Heap + 0xd038, 12);
      m.put(Memory::Heap + 0xd048, ObjectiveMemory::Component);
      const auto entries = Memory::Heap + 0x200000;
      m.put(Memory::Heap + 0x15018, entries);
      m.put<std::int32_t>(Memory::Heap + 0x15020, rooms);
      m.put<std::int32_t>(Memory::Heap + 0x15028, 0);
      m.put<belmont::U64>(entries + 0x18, rooms);
      for (int i = 0; i < rooms; ++i) {
        const auto at = entries + 0x20 + 0x18 * i;
        const auto key = Memory::Heap + 0x400000 + 0x1000 * i;
        m.put<std::int32_t>(at, i);
        m.put<std::int32_t>(at + 4, -1);
        m.put(at + 8, key);
        m.put(at + 0x10, Memory::Heap + 0x21000);
        m.string(key, ("room" + std::to_string(i)).c_str());
      }
      const auto source = m.sample();
      Check(source.exploration_ready &&
                source.exploration.size() == std::size_t(rooms),
            "actual native exploration dictionary baseline is valid");
      unsigned exploration_reads{};
      m.reads = 0;
      m.observe = [&](auto at) {
        if (at == Memory::Heap + 0xe030 ||
            (at >= Memory::Heap + 0x15000 && at < Memory::Heap + 0x16000) ||
            (at >= Memory::Heap + 0x20000 && at < Memory::Heap + 0x30000) ||
            (at >= entries && at < entries + 0x20 + 0x18 * rooms) ||
            (at >= Memory::Heap + 0x400000 &&
             at < Memory::Heap + 0x400000 + 0x1000 * rooms))
          ++exploration_reads;
      };
      belmont_objectives::Decoder decoder(m);
      Check(decoder.Sample(m.id(), source).ready && exploration_reads == 0,
            "supplied objective snapshot never rewalks explored rooms");
      if (rooms == 1)
        baseline_reads = m.reads;
      Check(m.reads == baseline_reads && m.reads < 1000,
            "objective read count is bounded and independent of native room "
            "history");
      std::cout << "Supplied objective snapshot native rooms " << rooms
                << "; reads " << m.reads.load() << "; explored-room reads "
                << exploration_reads << '\n';
    }
    std::cout << "Belmont objective reader tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
