// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include <cassert>
#include <iostream>
#include <map>
#include "botw_test_fixture.h"

int main() {
  using botw::UiContext;
  for (auto [flags, title, want] : {
      std::tuple{0x1849000000ULL, true, UiContext::Title},
      std::tuple{0x0e09e00000ULL, false, UiContext::Transition},
      std::tuple{0x1e09000000ULL, false, UiContext::World},
      std::tuple{0x1e09000004ULL, false, UiContext::Menu},
      std::tuple{0x1e09000008ULL, false, UiContext::Unknown}}) {
    Fixture f(flags, title); assert(botw::context(f.h) == want);
  }
  { Fixture f; f.h.build_id[0] ^= 1; assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.h.title_id = 0; assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.h.main_base = UINT64_MAX - 1; assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.put<uint64_t>(Fixture::U, 0); assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.put<int32_t>(Fixture::S+0x28, 100); assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.mem.erase(Fixture::U+0x64c30); assert(botw::context(f.h) == UiContext::Unknown); }
  { Fixture f; f.put<uint64_t>(Fixture::A+8, 1); assert(botw::context(f.h) == UiContext::Unknown); }
  for (uint64_t addr : {Fixture::U+0x64c30, Fixture::M+0x2cbdb80, Fixture::A, Fixture::S+0x30}) {
    Fixture f; f.race = addr; assert(botw::context(f.h) == UiContext::Unknown);
  }
  // A previously published item must disappear when loading begins, even if a pouch exists.
  for (uint64_t flags : {0x0e09e00000ULL, 0x1e09000008ULL}) {
    Fixture f(flags); State s;
    s.data.valid = true; s.data.items.push_back({"Weapon_Sword_044",0,400,true});
    f.texts["botw.item.0"] = "Weapon_Sword_044";
    sample(&s, &f.h);
    assert(f.values["botw.pouch_valid"] == 0 && f.values["botw.pouch_count"] == 0);
    assert(f.texts["botw.item.0"].empty() && f.texts["botw.equipped_weapon"].empty());
    assert(f.values["botw.actions_ready"] == 0 && !s.data.valid);
  }
  std::cout << "18 context signature/build/bounds/race/stale-output cases passed\n";
}
