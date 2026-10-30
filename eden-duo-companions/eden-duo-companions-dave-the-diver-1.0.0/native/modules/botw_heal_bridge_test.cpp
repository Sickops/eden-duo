// SPDX-License-Identifier: GPL-3.0-or-later
// Heal advisor through the real module + guarded consume bridge: Ready-page Eat uses the same
// native request as Inventory Eat (same mailbox fields), only from the Ready scope.
// Reuse the Inventory bridge fixture verbatim (its own main() is renamed and never run).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main botw_inventory_bridge_test_main
#include "botw_inventory_bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
int main() {
  using F=InventoryBridgeFixture;unsigned cases{};
  using C=botw::LowHealth::Clock;
  // Low health on Ready: 2 hearts of 12 (Info+0xA0 = 48.0, Player+0x848 = 8, +0x1868 = 48).
  auto low=[](F& f,int life=8) {
    f.food(5);f.put<float>(F::Info+0xA0,48.f);f.put<int32_t>(F::Player+0x848,life);f.put<int32_t>(F::Player+0x1868,48);
    f.texts["page"]="ready";f.state.bridge.poll(f.h,{});
    const auto p=botw::player_health(f.h,F::Info,F::Player);assert(p.valid && p.life==life);
    const auto t=C::now();f.state.heal.low.observe(p,f.state.bridge.epoch,t-std::chrono::milliseconds(400));
    f.state.heal.low.observe(p,f.state.bridge.epoch,t);
    f.state.heal.update(t);f.state.heal.refresh(f.h,f.state.data);
  };
  {F f;low(f);assert(f.state.heal.page.available && f.state.heal.page.showing);
   assert(on_action(&f.state,"card_toggle",3) && f.state.cards.active==3 && f.state.heal.rows.size()==1);
   assert(f.state.heal.rows[0].actor=="Item_Fruit_A" && f.state.heal.rows[0].count==10 && f.state.heal.rows[0].heal==2);
   const uint64_t target=f.state.heal.rows[0].address;
   assert(on_action(&f.state,"heal_eat",0));
   // the same consume mailbox layout as inventory_consume (kind 8 / type 7 / target / cache row)
   assert(f.get<uint32_t>(20)==8 && f.get<uint32_t>(24)==7 && f.get<uint64_t>(40)==target && f.stores.back()==8);
   assert(f.state.bridge.pending_consume && f.state.bridge.pending_ready_consume);
   assert(!on_action(&f.state,"heal_eat",0));                       // one request at a time
   f.put<int32_t>(target+0x20,4);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.mb<uint32_t>(420,1);f.state.bridge.poll(f.h,{});
   assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Consumed");++cases;}
  // Ready-scope Eat is cancelled if the companion leaves Ready before the ack
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3) && on_action(&f.state,"heal_eat",0));
   f.texts["page"]="inventory";f.state.bridge.requested-=std::chrono::seconds(1);f.state.bridge.poll(f.h,{});
   assert(!f.get<uint32_t>(72));++cases;}
  // refusals: not on Ready, card closed, page unavailable, full health, row changed, bad index
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3));f.texts["page"]="inventory";assert(!on_action(&f.state,"heal_eat",0));assert(!f.get<uint32_t>(8));++cases;}
  {F f;low(f);assert(!on_action(&f.state,"heal_eat",0));assert(!f.get<uint32_t>(8));++cases;}
  {F f;low(f);f.state.heal.page.available=false;assert(on_action(&f.state,"card_toggle",3) && !on_action(&f.state,"heal_eat",0));++cases;}
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3));f.put<int32_t>(F::Player+0x848,48);
   assert(!on_action(&f.state,"heal_eat",0));assert(!f.get<uint32_t>(8) && std::string(f.state.bridge.status)=="Hearts full");++cases;}
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3));
   std::array<char,64> name{};std::memcpy(name.data(),"Item_Fruit_B",12);
   for(auto p:{F::One,F::Two})f.put(p+0x3C,name);
   f.state.data=botw::pouch(f.h);
   for(auto& i:f.state.data.items)i.actor="Item_Fruit_B";
   assert(!on_action(&f.state,"heal_eat",0));assert(!f.get<uint32_t>(8));++cases;}
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3));assert(!on_action(&f.state,"heal_eat",1) && !on_action(&f.state,"heal_eat",-1));++cases;}
  {F f;low(f);f.values["aux"]=0;assert(!on_action(&f.state,"card_toggle",3));++cases;}
  {F f;low(f);f.state.sensor=[]{botw::Sensor x{true,true,0,0.f};x.map_valid=true;x.map_enabled=true;x.map_mode=0;return x;}(); // compass card needs the game's sensor (owner bug 2026-10-23)
   assert(on_action(&f.state,"card_toggle",1) && f.state.cards.active==1 && !on_action(&f.state,"heal_eat",0));++cases;}  // another card open
  {F f;low(f);f.state.sensor=[]{botw::Sensor x{true,false,0,0.f};x.map_valid=true;x.map_enabled=false;x.map_mode=0;return x;}();assert(!on_action(&f.state,"card_toggle",1) && f.state.cards.active==0);++cases;}  // sensor not obtained: compass refused
  {F f;low(f);f.state.sensor=[]{botw::Sensor x{true,true,2,0.f};x.map_valid=true;x.map_enabled=true;x.map_mode=2;return x;}();assert(!on_action(&f.state,"card_toggle",1));++cases;}  // sensor switched off: compass refused
  // a second tap of the page closes the card; recovering health closes it via close_request
  {F f;low(f);assert(on_action(&f.state,"card_toggle",3) && f.state.cards.active==3);
   assert(on_action(&f.state,"card_toggle",3) && f.state.cards.active==0);
   assert(on_action(&f.state,"card_toggle",3) && f.state.cards.active==3);
   f.state.cards.close(3);assert(f.state.cards.active==0);
   assert(f.state.heal.page.right(f.state.ready_panes)==2);
   assert(on_action(&f.state,"ready_right_pane",1) && f.state.heal.page.right(f.state.ready_panes)==0);
   assert(on_action(&f.state,"ready_right_pane",1) && f.state.heal.page.right(f.state.ready_panes)==1);
   assert(on_action(&f.state,"ready_right_pane",1) && f.state.heal.page.right(f.state.ready_panes)==2);
   assert(on_action(&f.state,"ready_left_pane",1) && f.state.ready_panes.left()==1);++cases;}
  // Inventory Eat is unchanged and still Inventory-only
  {F f;f.food(5);assert(on_action(&f.state,"inventory_consume",0) && !f.state.bridge.pending_ready_consume);++cases;}
  {F f;f.food(5);f.texts["page"]="ready";f.state.bridge.poll(f.h,{});assert(!on_action(&f.state,"inventory_consume",0));++cases;}
  std::cout<<cases<<" heal advisor bridge cases passed\n";
}
