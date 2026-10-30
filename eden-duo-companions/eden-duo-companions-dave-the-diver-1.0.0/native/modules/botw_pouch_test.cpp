// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_pouch.h"
#include <cassert>
#include <iostream>
#include <map>
struct Fixture {
  std::map<uint64_t, uint8_t> mem;
  bool changed = false;
  int heads = 0;
  template <class T> void put(uint64_t p, const T &value) {
    auto b = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(T); i++)
      mem[p + i] = b[i];
  }
  EdenDsmodHostApi host{};
  Fixture() {
    host.title_id = 0x01007EF00011E000ULL;
    host.main_base = 0x100000;
    host.main_size = 0x3000000;
    host.userdata = this;
    for (unsigned i = 0; i < 32; i++) {
      auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      host.build_id[i] =
          (hex(botw::Build[2 * i]) << 4) | hex(botw::Build[2 * i + 1]);
    }
    host.is_mapped = [](void *u, uint64_t p, uint64_t n) -> EdenDsmodBool {
      auto &f = *static_cast<Fixture *>(u);
      for (uint64_t i = 0; i < n; i++)
        if (!f.mem.contains(p + i))
          return false;
      return true;
    };
    host.read_memory = [](void *u, uint64_t p, void *out,
                          size_t n) -> EdenDsmodBool {
      auto &f = *static_cast<Fixture *>(u);
      for (size_t i = 0; i < n; i++)
        static_cast<uint8_t *>(out)[i] = f.mem.at(p + i);
      if (p == 0x200068 && ++f.heads == 2 && f.changed)
        static_cast<uint8_t *>(out)[16] ^= 1;
      return true;
    };
    put<uint64_t>(host.main_base + 0x2CA6D50, 0x200000);
    for (uint64_t i = 0; i < 24; i++)
      mem[0x200068 + i] = 0;
    put<uint64_t>(0x200068, 0x300008);
    put<uint64_t>(0x200070, 0x300008);
    put<int32_t>(0x200078, 1);
    put<int32_t>(0x20007c, 8);
    for (uint64_t i = 0; i < 0x38; i++)
      mem[0x300000 + i] = 0;
    put<uint64_t>(0x300008, 0x200068);
    put<uint64_t>(0x300010, 0x200068);
    put<int32_t>(0x300020, 400);
    mem[0x300024] = 1;
    mem[0x300025] = 1;
    put<uint64_t>(0x300030, 0x400000);
    const char *name = "Weapon_Sword_044";
    for (size_t i = 0; i <= std::strlen(name); i++)
      mem[0x400000 + i] = name[i];
  }
};
int main() {
  {
    Fixture f;
    auto s = botw::pouch(f.host);
    assert(s.valid && s.items.size() == 1 &&
           s.items[0].actor == "Weapon_Sword_044" && s.items[0].value == 400 &&
           s.items[0].address == 0x300000);
  }
  {
    Fixture f;
    f.host.build_id[0] ^= 1;
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.put<int32_t>(0x200078, 421);
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.put<uint64_t>(0x300008, 0x300008);
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.put<uint64_t>(0x300010, 0x123456);
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.mem.erase(0x400005);
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.changed = true;
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.put<uint64_t>(0x200068, 0x200068);
    f.put<uint64_t>(0x200070, 0x200068);
    f.put<int32_t>(0x200078, 0);
    auto s = botw::pouch(f.host);
    assert(s.valid && s.items.empty());
  }
  {
    Fixture f;
    f.host.main_base = UINT64_MAX - 1;
    assert(!botw::pouch(f.host).valid);
  }
  {
    Fixture f;
    f.mem[0x300025] = 0;
    f.put<int32_t>(0x300020, 0);
    auto s = botw::pouch(f.host);
    assert(s.valid && s.items.empty());
  }
  {
    Fixture f;
    f.mem[0x300025] = 2;
    assert(!botw::pouch(f.host).valid);
  }
  std::cout << "11 pouch integrity/build/transition cases passed\n";
}
