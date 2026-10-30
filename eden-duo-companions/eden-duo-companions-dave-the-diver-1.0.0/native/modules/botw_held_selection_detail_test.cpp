// SPDX-License-Identifier: GPL-3.0-or-later
// Fix round 4, finding 5 (critique-2 #5): the held quick-picker's attack/durability row was
// reported as reading -1 for an item that reads correctly in touch mode. Live verification
// (runtime22-e5751ea0, native-holding-160) found the held path's botw::gear() output carries
// a correctly populated .address for every slot and botw::gear_modifier()/quick_detail() on it
// behave identically to the touch path for the same address -- 19/20 held-mode slots showed a
// valid stat/durability_pct live, and the one -1 slot was the Master Sword (actor hash
// 4123781303 == "Weapon_Sword_070"), which botw_quick_detail.h and botw_ready.h both exclude by
// design in *both* modes (no static power display for the dynamic special sword; no durability
// bar since it recharges instead of breaking). The critique's "regression" compared the Master
// Sword (held mode's native default highlight) against a different, unrelated weapon (touch
// mode's own durability-sorted default highlight) -- not the same instance in two modes.
// This test is a regression guard: it reproduces the held path's exact call sequence
// (botw::gear() -> highlighted item -> botw::gear_modifier() -> botw::quick_detail()) for a
// real PouchItem memory layout (vtable at +0x28, inline actor-name storage at +0x3C, matching
// the identity botw_gear_test.cpp's GearFixture does not need and botw_ready_test.cpp's
// GearModifier fixtures do) and asserts (a) an ordinary weapon gets a valid, non -1 stat and
// durability_pct through the held path, exactly as the touch path already gets for the same
// address, and (b) a Master Sword actor still correctly reports modifier.valid == true (the
// memory read itself succeeds) while stat/condition stay -1 -- the documented exclusion, not a
// lost read.
#include "botw_gear.h"
#include "botw_quick_detail.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
namespace {
struct HeldFixture : Fixture {
  static constexpr uint64_t Screen = 0x7005000, One = 0x8000000, Two = 0x8000100;
  botw::Snapshot pouch{true, {}};
  // actor must be <64 bytes incl. terminator (same bound botw::pouch()/quick_string() enforce).
  HeldFixture(const char* actor_one, const char* actor_two, int type = 0) {
    put(A + 45 * 8, Screen);
    put(Screen, M + 0x2BA1D40);
    put(Screen + 0x150, M + 0x2BA1C10);
    const int selector = type == 3 ? 1 : type == 1 ? 2 : type == 2 ? 3 : 0;
    put<int32_t>(Screen + 0x3720, selector);
    put<int32_t>(Screen + 0x3724, selector);
    put<uint32_t>(U + 0x64C24, 3);
    put<uint32_t>(U + 0x64C58, (type == 3 || type == 2) ? 0x40000 : 0x80000);
    put<uint64_t>(U + 0x64C30, 0x1E09000004ULL);
    put(Screen + 0x37B0, std::array<uint64_t, 20>{One, Two});
    // Held mode's native highlight (a.highlighted) points at slot 0 -- same field gear() reads.
    put<int32_t>(Screen + 0x3860, 0);
    put<int32_t>(Screen + 0x3864, 0);
    put<int32_t>(Screen + 0x3868, 2);
    put<uint8_t>(Screen + 0x38CC, 3);
    const char* actors[2] = {actor_one, actor_two};
    for (int i = 0; i < 2; ++i) {
      const uint64_t address = i == 0 ? One : Two;
      const std::string actor = actors[i];
      put(address, std::array<uint8_t, 0x38>{});
      put<int32_t>(address + 0x18, type);
      put<int32_t>(address + 0x20, 400);
      put<uint8_t>(address + 0x24, i == 0);
      put<uint8_t>(address + 0x25, 1);
      // Real PouchItem layout: vtable at +0x28, inline actor-name storage starting at +0x3C.
      put<uint64_t>(address + 0x28, M + 0x2A6FAF0);
      put<uint64_t>(address + 0x30, address + 0x3C);
      put<uint32_t>(address + 0x80, 0);
      put<uint32_t>(address + 0x88, 0);
      for (size_t k = 0; k <= actor.size(); ++k) put<char>(address + 0x3C + k, actor[k]);
      pouch.items.push_back({actor, type, 400, i == 0, address});
    }
  }
};
} // namespace
int main() {
  unsigned passed = 0;
  // (a) An ordinary weapon reaches the held row with a valid stat/durability, same as touch.
  {
    HeldFixture f("Weapon_Sword_001", "Weapon_Sword_002");
    const auto gear = botw::gear(f.h, f.pouch);
    assert(gear.valid && gear.highlighted == 0);
    const auto& item = gear.items[size_t(gear.highlighted)];
    assert(item.actor == "Weapon_Sword_001" && item.address == HeldFixture::One);
    const auto modifier = botw::gear_modifier(f.h, item.address);
    assert(modifier.valid);
    const auto detail = botw::quick_detail(item, modifier);
    assert(detail.stat == 5 && detail.condition == 20);
    ++passed;
  }
  // (b) The Master Sword: memory read succeeds (modifier.valid). Fix round 4 (critique-3): a
  // live native capture showed a stat badge on this exact weapon, so condition/durability
  // stays -1 (genuinely no life bar -- Master Sword recharges instead of breaking) but stat
  // is now a real value through the held path too, same as any other weapon.
  {
    HeldFixture f("Weapon_Sword_070", "Weapon_Sword_002");
    const auto gear = botw::gear(f.h, f.pouch);
    assert(gear.valid && gear.highlighted == 0);
    const auto& item = gear.items[size_t(gear.highlighted)];
    assert(item.actor == "Weapon_Sword_070");
    const auto modifier = botw::gear_modifier(f.h, item.address);
    assert(modifier.valid);
    const auto detail = botw::quick_detail(item, modifier);
    assert(detail.stat == 30 && detail.condition == -1);
    ++passed;
  }
  std::cout << passed << " held-picker attack/durability row cases passed (finding 5: no held-vs-touch divergence; Master Sword durability exclusion confirmed by design, stat exclusion was wrong and removed)\n";
}
