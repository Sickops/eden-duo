// SPDX-License-Identifier: GPL-3.0-or-later
// Fix round A (2026-10-18): fps-aware bridge freshness, Master Sword displayed power, Ready
// counters held through the native pause menu.
#include "botw_bridge.h"
#include "botw_quick_detail.h"
#include "botw_ready_counters.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
using namespace std::chrono_literals;
// GameData with the three Master Sword booleans and three integers (sorted by hash, as native).
struct CounterFixture : Fixture {
  static constexpr uint64_t G = 0x9000000, H = 0x9001000, B = 0x9002000, BA = 0x9003000, IA = 0x9004000;
  static constexpr uint64_t BF = 0x9005000, IF = 0x9006000;
  static constexpr uint32_t Beast = 0x462B092Fu, Full = 0x8FCFFC54u, Castle = 0x986F0046u;
  static constexpr uint32_t Add = 0x7B248982u, Shrines = 0xE605CE62u, Orbs = 0xF11FF7E1u;
  explicit CounterFixture(uint64_t ui = 0x1e09000000ULL) : Fixture(ui) {
    put(M + 0x2CC5FE8, G); put(G, M + 0x2BD07E0); put(G + 0xBE0, H); put(H, B);
    put<uint8_t>(G + 0xBE8, 1); put<uint32_t>(G + 0xC18, 0x8000); put<uint32_t>(G + 0xC28, 1);
    put<int32_t>(B + 8, 3); put<int32_t>(B + 12, 3); put(B + 16, BA);
    put<int32_t>(B + 24, 3); put<int32_t>(B + 28, 3); put(B + 32, IA);
    const uint32_t bools[] = {Beast, Full, Castle}, ints[] = {Add, Shrines, Orbs};
    for (unsigned i = 0; i < 3; ++i) {
      const uint64_t b = BF + i * 0x40, n = IF + i * 0x40;
      put(BA + i * 8, b); put(IA + i * 8, n);
      put(b, M + 0x2BD1078); put<uint8_t>(b + 8, 0); put<uint8_t>(b + 9, 1); put<uint8_t>(b + 10, 0);
      put<uint8_t>(b + 11, 0); put<uint32_t>(b + 12, bools[i]);
      put(n, M + 0x2BD1140); put<uint8_t>(n + 8, 1); put<uint8_t>(n + 9, 1); put<uint16_t>(n + 10, 0);
      put<int32_t>(n + 12, 0); put<int32_t>(n + 16, 0); put<int32_t>(n + 20, 1000); put<int32_t>(n + 24, 0);
      put<uint32_t>(n + 28, ints[i]); put<uint64_t>(n + 32, 0);
    }
    // MasterSword_Add_Power treated as not program-readable (native getFlag path does not gate it).
    put<uint8_t>(IF + 9, 0);
    set_int(Shrines, 77); set_int(Orbs, 14);
  }
  void set_bool(uint32_t hash, bool v) {
    const uint32_t bools[] = {Beast, Full, Castle};
    for (unsigned i = 0; i < 3; ++i) if (bools[i] == hash) put<uint8_t>(BF + i * 0x40 + 11, v ? 1 : 0);
  }
  void set_int(uint32_t hash, int32_t v) {
    const uint32_t ints[] = {Add, Shrines, Orbs};
    for (unsigned i = 0; i < 3; ++i) if (ints[i] == hash) put<int32_t>(IF + i * 0x40 + 24, v);
  }
};
int main() {
  int cases = 0;
  auto check = [&](bool ok) { ++cases; if (!ok) std::cerr << "failure " << cases << "\n"; assert(ok); };
  using Clock = std::chrono::steady_clock;
  // 1. Heartbeat window follows the measured game frame: 1.5 frames, clamped to [100, 240] ms.
  {
    botw::SelectionBridge b;const auto t0 = Clock::now();
    check(b.beat_window() == Clock::duration(100ms));
    check(b.ack_window() == Clock::duration(250ms));
    for (uint32_t i = 1; i <= 8; ++i) b.note_beat(i, t0 + i * 125ms);  // ~8 game fps
    const auto last = t0 + 8 * 125ms;
    check(b.frame_interval >= 120ms && b.frame_interval <= 130ms);
    check(b.beat_window() >= 180ms && b.beat_window() <= 195ms);
    check(b.beat_fresh(last + 150ms));          // the old fixed 100 ms window refused this
    check(!b.beat_fresh(last + 200ms));         // still stale after more than 1.5 frames
    check(b.ack_window() >= 290ms && b.ack_window() <= 310ms);
    botw::SelectionBridge fast;
    for (uint32_t i = 1; i <= 8; ++i) fast.note_beat(i, t0 + i * 16ms);  // 60 fps: unchanged
    check(fast.beat_window() == Clock::duration(100ms));
    check(!fast.beat_fresh(t0 + 8 * 16ms + 101ms));
    check(fast.ack_window() == Clock::duration(250ms));
    botw::SelectionBridge slow;
    for (uint32_t i = 1; i <= 8; ++i) slow.note_beat(i, t0 + i * 400ms);  // 2.5 fps: capped
    check(slow.beat_window() == Clock::duration(400ms));
    check(slow.health_window() == Clock::duration(750ms));
    check(slow.ack_window() == Clock::duration(800ms));
    botw::SelectionBridge four;
    for (uint32_t i = 1; i <= 8; ++i) four.note_beat(i, t0 + i * 225ms);  // ~4.4 fps
    check(four.beat_window() >= 330ms && four.beat_window() <= 345ms);
    check(four.health_window() >= 555ms && four.health_window() <= 570ms);
    check(four.beat_window() < four.health_window());
    check(fast.health_window() == Clock::duration(250ms));  // 60 fps: the original limit
    botw::SelectionBridge same;same.note_beat(5, t0);same.note_beat(5, t0 + 50ms);  // no new beat
    check(same.heartbeat_at == t0);
    botw::SelectionBridge gap;gap.note_beat(1, t0);gap.note_beat(2, t0 + 5s);  // pause: not a frame
    check(gap.frame_interval == Clock::duration::zero());
  }
  // 2. Master Sword: the displayed power of ui::getWeaponStats.
  {
    const botw::GearModifier none{true, 0, 0};
    botw::Item sword;sword.actor = "Weapon_Sword_070";sword.type = 0;sword.value = 40;
    check(botw::quick_detail(sword, none).stat == 30);  // no GameData sample: static base kept
    botw::MasterSwordState ms{true, true, 30};
    check(botw::quick_detail(sword, none, &ms).stat == 60);  // true form (Trial cleared / castle)
    ms = {true, false, 30};
    check(botw::quick_detail(sword, none, &ms).stat == 60);  // base 30 + Trial upgrades 30
    ms = {true, false, 0};
    check(botw::quick_detail(sword, none, &ms).stat == 30);
    ms = {true, false, 10};
    check(botw::quick_detail(sword, botw::GearModifier{true, 1, 5}, &ms).stat == 45);  // + AddAtk
    ms = {true, true, 0};sword.value = 0;
    check(botw::quick_detail(sword, none, &ms).stat == 0);  // recharging
    ms = {false, true, 30};sword.value = 40;
    check(botw::quick_detail(sword, none, &ms).stat == 30);  // invalid sample ignored
    botw::Item other;other.actor = "Weapon_Sword_001";other.type = 0;other.value = 10;
    const auto base = botw::quick_detail(other, none).stat;ms = {true, true, 30};
    check(base > 0 && botw::quick_detail(other, none, &ms).stat == base);  // other swords unaffected
  }
  // 3. Ready counters: Master Sword flags read like the native accessors; the pause menu keeps
  //    the last counts instead of blanking them.
  {
    CounterFixture f;botw::ReadyCounters c;
    f.set_bool(CounterFixture::Full, true);f.set_int(CounterFixture::Add, 30);
    c.publish(f.h, 7, true);
    check(f.values.at("botw.ready.shrine_count") == 77 && f.values.at("botw.ready.spirit_orbs") == 14);
    check(c.master_sword().valid && c.master_sword().true_form && c.master_sword().add_power == 30);
    // Native pause menu (UI flags ...04): reads may fail; the counts must stay.
    CounterFixture menu(0x1e09000004ULL);menu.put<uint64_t>(CounterFixture::G, 0);  // GameData torn
    botw::ReadyCounters held = c;menu.values.clear();
    held.publish(menu.h, 7, false, true);
    check(menu.values.at("botw.ready.shrine_count") == 77 && menu.values.at("botw.ready.shrine_count.valid") == 1);
    check(menu.values.at("botw.ready.spirit_orbs") == 14);
    check(held.master_sword().valid && held.master_sword().true_form);
    // ...and still clear outside World/Menu (loading, title) or on a new session.
    held.publish(menu.h, 7, false, false);
    check(menu.values.at("botw.ready.shrine_count") == -1 && !held.master_sword().valid);
    botw::ReadyCounters fresh;CounterFixture g;fresh.publish(g.h, 1, true);
    check(g.values.at("botw.ready.shrine_count") == 77);
    check(fresh.master_sword().valid && !fresh.master_sword().true_form && fresh.master_sword().add_power == 0);
    g.set_bool(CounterFixture::Castle, true);botw::ReadyCounters castle;castle.publish(g.h, 1, true);
    check(castle.master_sword().true_form);
    g.set_int(CounterFixture::Add, 5000);botw::ReadyCounters bad;bad.publish(g.h, 1, true);
    check(!bad.master_sword().valid);  // out of the flag's own bounds: unknown, not a value
  }
  std::cout << cases << " fix-round-A cases passed\n";
}
