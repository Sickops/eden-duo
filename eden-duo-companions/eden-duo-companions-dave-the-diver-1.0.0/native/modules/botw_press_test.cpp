// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_press.h"
#include <cassert>
#include <cstdio>
int main() {
  using namespace std::chrono;
  int cases = 0;
  // Values pinned against tools/i13-buttons.py press_id() (python reference).
  static_assert(botw::press_id("inventory_equip", 0) == 61308590);
  static_assert(botw::press_id("select", -3) == 1354264609);
  static_assert(botw::press_id("", 0) != 0 && botw::press_id(nullptr, 0) != 0);
  ++cases;
  botw::PressFlash f;
  const auto t0 = steady_clock::now();
  assert(f.value(t0) == 0); ++cases;
  f.note("inventory_equip", 0, t0);
  assert(f.value(t0) == botw::press_id("inventory_equip", 0)); ++cases;
  assert(f.value(t0 + milliseconds(219)) != 0); ++cases;
  assert(f.value(t0 + milliseconds(220)) == 0); ++cases;
  f.note(nullptr, 0, t0 + milliseconds(500));  // ignored
  assert(f.value(t0 + milliseconds(500)) == 0); ++cases;
  f.note("quest_set_destination", 7, t0 + milliseconds(600));
  assert(f.value(t0 + milliseconds(650)) == botw::press_id("quest_set_destination", 7)); ++cases;
  std::printf("%d cases\n", cases);
  return 0;
}
