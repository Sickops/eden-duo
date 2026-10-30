// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
int main() {
 unsigned cases=0;
 constexpr int cats[]={0,1,1,2,3,3,3,4,5,6};
 for(int i=-1;i<=10;++i){assert(botw::inventory_category(i)==(i>=0&&i<10?cats[i]:-1));++cases;}
 botw::Snapshot data{true,{{"same",0,400,true,0x10000},{"same",0,500,false,0x20000},{"NormalArrow",2,42,true,0x30000},{"Armor",4,1,true,0x40000},{"Food",8,3,false,0x50000}}};
 botw::InventoryView v;v.update(data);assert(v.selected==0x10000);++cases;
 assert(v.choose(data,1)&&v.selected==0x20000);v.update(data);assert(v.selected==0x20000);++cases;
 assert(!v.choose(data,-1)&&!v.choose(data,2)&&!v.choose(data,INT64_MAX));++cases;
 data.items.erase(data.items.begin()+1);v.update(data);assert(v.selected==0x10000);++cases;
 v.category=1;v.update(data);assert(v.selected==0x30000);++cases;
 v.category=6;v.update(data);assert(!v.selected&&!v.choose(data,0));++cases;
 assert(botw::equipped_arrows(data)==42);++cases;
 data.items[1].value=0;assert(botw::equipped_arrows(data)==0);++cases;
 data.items[1].value=-1;assert(botw::equipped_arrows(data)==-1);++cases;
 data.items[1].value=1000000;assert(botw::equipped_arrows(data)==-1);++cases;
 data.items[1].value=10;data.items.push_back(data.items[1]);assert(botw::equipped_arrows(data)==-1);++cases;
 data.items.pop_back();data.items[1].equipped=false;assert(botw::equipped_arrows(data)==-1);++cases;
 for(int t=0;t<10;++t){botw::Item item{"x",t,123,false,0};assert(botw::item_quantity(item)==(t==2||t==7||t==8?123:-1));++cases;}
 data.valid=false;assert(botw::inventory_items(data,0).empty()&&botw::equipped_arrows(data)==-1);v.update(data);assert(!v.selected);++cases;
 assert(botw::actor_key("")==-1&&botw::actor_key("NormalArrow")==botw::actor_key("NormalArrow"));++cases;
 // actor_key2 must match hashing the concatenated string byte-for-byte (cleanup: avoids
 // allocating a temporary std::string for quest description hashing on every sample).
 assert(botw::actor_key2("",'_',"")==-1);++cases;
 assert(botw::actor_key2("Quest_A",'_',"msg")==botw::actor_key(std::string("Quest_A")+"_"+"msg"));++cases;
 assert(botw::actor_key2("",'_',"msg")==botw::actor_key(std::string("")+"_"+"msg"));++cases;
 assert(botw::actor_key2("Quest_A",'_',"")==botw::actor_key(std::string("Quest_A")+"_"));++cases;
 {Fixture f;State s;s.data={true,{{"same",0,400,true,0x10000},{"same",0,500,false,0x20000}}};
  assert(on_action(&s,"inventory_choose",1)&&s.inventory.selected==0x20000);
  assert(on_action(&s,"inventory_category",3)&&s.inventory.category==3&&!s.inventory.selected);
  assert(!on_action(&s,"inventory_category",7)&&!on_action(&s,"inventory_category",-1));
  s.data.valid=false;assert(!on_action(&s,"inventory_choose",0)&&!on_action(&s,"inventory_category",0));++cases;}

 // native_order: CRITIQUE-3 fix round B. Reproduces the manager+column structure botw_inventory_actions.h
 // already uses for removal lookup (same manager root, vtable, sentinel, "prev"-chain walk), to prove the
 // reordering it drives matches the game's own per-category grid order and fails closed otherwise.
 {
  Fixture f;
  constexpr uint64_t Manager = Fixture::M + 0x900000;
  constexpr uint64_t W0 = Fixture::M + 0x910000, W1 = W0 + 0x100, W2 = W0 + 0x200, W3 = W0 + 0x300, W4 = W0 + 0x400;
  constexpr uint64_t Ah = Fixture::M + 0x920000, Au = Ah + 0x100;
  const uint64_t sentinel = Manager + 0x68;
  f.put(Fixture::M + 0x2CA6D50, Manager);
  f.put(Manager, Fixture::M + 0x2B87780);
  for (unsigned col = 0; col < 50; ++col) f.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
  // col0: native weapon order W4,W3,W2,W1,W0 -- the exact reverse of the cached pouch order below.
  f.put<uint64_t>(Manager + 0x44230 + 0 * 8, W4);
  f.put<uint64_t>(W4 + 16, W3 + 8); f.put<uint64_t>(W3 + 16, W2 + 8);
  f.put<uint64_t>(W2 + 16, W1 + 8); f.put<uint64_t>(W1 + 16, W0 + 8);
  f.put<uint64_t>(W0 + 16, sentinel);
  // col1: one armor set, head then upper, sharing a single column.
  f.put<uint64_t>(Manager + 0x44230 + 1 * 8, Ah);
  f.put<uint64_t>(Ah + 16, Au + 8); f.put<uint64_t>(Au + 16, sentinel);
  botw::Snapshot pouch{true, {
      {"Weapon_0", 0, 10, false, W0}, {"Weapon_1", 0, 20, false, W1}, {"Weapon_2", 0, 30, false, W2},
      {"Weapon_3", 0, 40, false, W3}, {"Weapon_4", 0, 50, false, W4},
      {"Armor_Head", 4, -1, true, Ah}, {"Armor_Upper", 5, -1, false, Au}}};
  auto n = botw::native_order(f.h, pouch);
  assert(n.valid && n.items.size() == 7); ++cases;
  const uint64_t expect[] = {W4, W3, W2, W1, W0, Ah, Au};
  for (size_t i = 0; i < 7; ++i) assert(n.items[i].address == expect[i]);
  ++cases;
  auto weapons = botw::inventory_items(n, 0);
  assert(weapons.size() == 5 && weapons[0]->value == 50 && weapons[4]->value == 10); ++cases;
  auto armor = botw::inventory_items(n, 3);
  assert(armor.size() == 2 && armor[0]->address == Ah && armor[1]->address == Au); ++cases;
  // Not valid: no pouch to reorder.
  assert(!botw::native_order(f.h, {}).valid); ++cases;
  // Wrong manager vtable: fails closed (every pre-existing fixture without this scaffold hits this path).
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87781);
   assert(!botw::native_order(g.h, pouch).valid); ++cases;}
  // Unknown column node (not a cached pouch instance): fails closed.
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87780);
   for (unsigned col = 0; col < 50; ++col) g.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
   g.put<uint64_t>(Manager + 0x44230, Fixture::M + 0x999000);
   assert(!botw::native_order(g.h, pouch).valid); ++cases;}
  // Cycle within a column (never reaches the sentinel): fails closed.
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87780);
   for (unsigned col = 0; col < 50; ++col) g.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
   g.put<uint64_t>(Manager + 0x44230, W0); g.put<uint64_t>(W0 + 16, W0 + 8);
   assert(!botw::native_order(g.h, pouch).valid); ++cases;}
  // Same node claimed by two columns: fails closed.
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87780);
   for (unsigned col = 0; col < 50; ++col) g.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
   g.put<uint64_t>(Manager + 0x44230, W0); g.put<uint64_t>(W0 + 16, sentinel);
   g.put<uint64_t>(Manager + 0x44230 + 1 * 8, W0);
   assert(!botw::native_order(g.h, pouch).valid); ++cases;}
  // A column with more than 20 real nodes before the sentinel: the 20-slot bound belongs only to
  // the unrelated removal-lookup search; a full column enumeration must not cap there (a single
  // weapon/material kind can have many more duplicates than that).
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87780);
   for (unsigned col = 0; col < 50; ++col) g.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
   std::vector<uint64_t> long_chain; botw::Snapshot many{true, {}};
   for (unsigned i = 0; i < 37; ++i) {
     uint64_t addr = Fixture::M + 0x930000 + i * 0x100; long_chain.push_back(addr);
     many.items.push_back({"Weapon_Long", 0, int32_t(i), false, addr});
   }
   g.put<uint64_t>(Manager + 0x44230, long_chain[0]);
   for (size_t i = 0; i + 1 < long_chain.size(); ++i) g.put<uint64_t>(long_chain[i] + 16, long_chain[i + 1] + 8);
   g.put<uint64_t>(long_chain.back() + 16, sentinel);
   auto many_native = botw::native_order(g.h, many);
   assert(many_native.valid && many_native.items.size() == 37);
   for (size_t i = 0; i < 37; ++i) assert(many_native.items[i].address == long_chain[i]);
   ++cases;}
  // A chain that leaves the known pouch entirely (dangling into memory that is not a cached
  // instance) fails closed instead of walking forever or guessing an order for unknown data.
  {Fixture g; g.put(Fixture::M + 0x2CA6D50, Manager); g.put(Manager, Fixture::M + 0x2B87780);
   for (unsigned col = 0; col < 50; ++col) g.put<uint64_t>(Manager + 0x44230 + col * 8, 0);
   g.put<uint64_t>(Manager + 0x44230, W0); g.put<uint64_t>(W0 + 16, Fixture::M + 0x999000 + 8);
   assert(!botw::native_order(g.h, pouch).valid); ++cases;}
 }

 std::cout<<cases<<" inventory category/duplicate identity/quantity/action cases passed\n";
}
