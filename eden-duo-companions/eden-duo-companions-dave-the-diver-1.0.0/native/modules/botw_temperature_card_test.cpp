// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for botw_temperature_card.h: native danger decision (live-captured values from
// 2026-10-18 gdbstub snapshots), latch hysteresis and the clothing card selection logic.
#include "botw_temperature_card.h"
#include <cassert>
#include <iostream>
using namespace botw;
namespace {
struct It {std::string actor;int32_t type{},value{};bool equipped{};uint64_t address{};};
TemperatureSample sample(float felt,int armor_heat,int armor_cold,int food_heat=0,int food_cold=0) {
  TemperatureSample s;s.valid=true;s.felt=felt;s.armor_heat=armor_heat;s.armor_cold=armor_cold;s.food_heat=food_heat;s.food_cold=food_cold;
  s.heat_limit=native_heat_limits[size_t(level_index(armor_heat+food_heat))];s.cold_limit=native_cold_limits[size_t(level_index(armor_cold+food_cold))];
  s.margin=felt>0 ? felt-s.heat_limit : felt-s.cold_limit;s.burn_resist=0;return s;
}
std::vector<FoodOption> no_food;
const ArmorOption* find(const std::vector<ArmorOption>& o,const std::string& a){for(const auto& x:o)if(x.actor==a)return &x;return nullptr;}
}
int main(){unsigned cases{};
 // --- native decision on live captures ---
 {auto s=sample(-47.969f,0,1);assert(s.cold_limit==-10.f && std::fabs(s.margin+37.969f)<1e-3f); // Hebra, To Quomo, 20:20
  assert(raw_danger(s)==TempDanger::Cold && needed_level(s,TempDanger::Cold)==2 && current_level(s,TempDanger::Cold)==1);++cases;}
 {auto s=sample(-5.77f,0,1);assert(std::fabs(s.margin-4.23f)<1e-3f && raw_danger(s)==TempDanger::None);++cases;} // desert night, Misae Suma
 {auto s=sample(39.f,0,1);assert(raw_danger(s)==TempDanger::None);++cases;} // Eldin shrine before the fire, no fire level
 {auto s=sample(23.f,0,1);assert(raw_danger(s)==TempDanger::None);++cases;} // Sha Warvo interior
 {auto s=sample(39.f,0,1);s.area_fire=1;s.contact_fire=0;assert(raw_danger(s)==TempDanger::Burn && needed_level(s,TempDanger::Burn)==1);++cases;} // Shae Mo'sah
 {auto s=sample(39.f,0,1);s.area_fire=1;s.contact_fire=2;assert(raw_danger(s)==TempDanger::Burn);++cases;} // burning
 {auto s=sample(39.f,0,1);s.area_fire=1;s.burn_resist=1;assert(raw_danger(s)==TempDanger::None);++cases;}
 {auto s=sample(80.f,0,0);s.area_fire=2;s.burn_resist=1;assert(raw_danger(s)==TempDanger::Burn && needed_level(s,TempDanger::Burn)==2);++cases;}
 {auto s=sample(80.f,0,0);s.area_fire=1;s.burn_resist=-1;s.armor_burn=1;assert(raw_danger(s)==TempDanger::None);s.armor_burn=0;s.food_burn=1;assert(raw_danger(s)==TempDanger::None);++cases;}
 {auto s=sample(80.f,0,0);s.contact_fire=2;s.area_fire=0;s.burn_resist=0;assert(raw_danger(s)==TempDanger::None);++cases;} // contact-only: HUD hides heat, area 0
 // heat thresholds 40 / 50 / immune
 {auto s=sample(40.f,0,0);assert(raw_danger(s)==TempDanger::None);s=sample(40.01f,0,0);assert(raw_danger(s)==TempDanger::Heat && needed_level(s,TempDanger::Heat)==1);++cases;}
 {auto s=sample(50.f,0,0);assert(needed_level(s,TempDanger::Heat)==1);s=sample(50.5f,0,0);assert(needed_level(s,TempDanger::Heat)==2);++cases;}
 {auto s=sample(55.f,1,0);assert(s.heat_limit==50.f && raw_danger(s)==TempDanger::Heat);s=sample(55.f,2,0);assert(s.heat_limit==9999.f && raw_danger(s)==TempDanger::None);++cases;}
 {auto s=sample(55.f,1,0,1,0);assert(raw_danger(s)==TempDanger::None && current_level(s,TempDanger::Heat)==2);++cases;} // food adds to armor natively
 {auto s=sample(150.f,5,0);assert(s.heat_limit==9999.f && raw_danger(s)==TempDanger::None);++cases;} // index clamps
 // cold thresholds 0 / -10 / immune, T<=0 side
 {auto s=sample(0.f,0,0);assert(raw_danger(s)==TempDanger::None);s=sample(-0.01f,0,0);assert(raw_danger(s)==TempDanger::Cold && needed_level(s,TempDanger::Cold)==1);++cases;}
 {auto s=sample(-10.f,0,0);assert(needed_level(s,TempDanger::Cold)==1);s=sample(-10.5f,0,0);assert(needed_level(s,TempDanger::Cold)==2);++cases;}
 {auto s=sample(-30.f,0,2);assert(raw_danger(s)==TempDanger::None);s=sample(-30.f,0,1,0,1);assert(raw_danger(s)==TempDanger::None);++cases;}
 {TemperatureSample s;assert(raw_danger(s)==TempDanger::None && needed_level(s,TempDanger::None)==0);++cases;}
 // --- latch ---
 {DangerLatch l;auto c=sample(-12.f,0,1);assert(l.update(c,0)==TempDanger::None);assert(l.update(c,399)==TempDanger::None);assert(l.update(c,400)==TempDanger::Cold);++cases;
  auto edge=sample(-9.8f,0,1);assert(l.update(edge,500)==TempDanger::Cold);assert(l.update(edge,2000)==TempDanger::Cold);++cases; // within 0.5 C bias
  auto out=sample(-9.4f,0,1);assert(l.update(out,2100)==TempDanger::Cold);assert(l.update(out,2999)==TempDanger::Cold);assert(l.update(out,3000)==TempDanger::None);++cases;}
 {DangerLatch l;auto c=sample(-12.f,0,1),ok=sample(-5.f,0,1);int64_t t=0; // a 1 s flicker never shows
  for(int i=0;i<20;++i){assert(l.update(i%2?ok:c,t)==TempDanger::None);t+=300;}++cases;}
 {DangerLatch l;auto h=sample(45.f,0,0);l.update(h,0);assert(l.update(h,400)==TempDanger::Heat);auto c=sample(-15.f,0,0);assert(l.update(c,500)==TempDanger::Heat);assert(l.update(c,900)==TempDanger::Cold);++cases;}
 {DangerLatch l;TemperatureSample bad;l.update(sample(45.f,0,0),0);l.update(sample(45.f,0,0),400);assert(l.update(bad,500)==TempDanger::Heat && l.update(bad,1400)==TempDanger::None);++cases;}
 // --- plans ---
 // Hebra (save A style): Warm Doublet worn (cold 1), need 2. Owned: Snowquill set 5*, Ruby circlet 5*, Snowquill headdress 1*, Ancient helm.
 const std::vector<It> pouch{{"Armor_001_Head",4,0,true,0x100},{"Armor_044_Upper",5,0,true,0x110},{"Armor_001_Lower",6,0,true,0x120},
   {"Armor_073_Head",4,0,false,0x130},{"Armor_073_Upper",5,0,false,0x140},{"Armor_073_Lower",6,0,false,0x150},
   {"Armor_124_Head",4,0,false,0x160},{"Armor_009_Head",4,0,false,0x170},{"Armor_182_Head",4,0,false,0x180},
   {"Armor_067_Head",4,0,false,0x190},{"Armor_067_Upper",5,0,false,0x1A0},{"Armor_067_Lower",6,0,false,0x1B0},
   {"Armor_076_Head",4,0,false,0x1C0},{"Armor_011_Upper",5,0,false,0x1D0},{"Item_Cook_C_17",8,1,false,0x1E0}};
 {auto o=armor_options(pouch,TempDanger::Cold);assert(o.size()==14);assert(find(o,"Armor_044_Upper")->level==1 && find(o,"Armor_001_Head")->level==0 && find(o,"Armor_182_Head")->level==1 && find(o,"Armor_067_Head")->level==0);++cases;
  auto p=plan_clothing(TempDanger::Cold,2,1,0,o,no_food);assert(p.valid && !p.empty);
  assert(p.quick.valid && p.quick.pieces==1 && p.quick.reaches && p.quick.level==2 && p.quick.equip==std::vector<uint64_t>{0x130});++cases; // head first, 5* Snowquill over 5* Ruby by name
  assert(p.best.valid && p.best.level==3 && p.best.set_bonus && p.best.pieces==3 && p.best.equip==(std::vector<uint64_t>{0x130,0x140,0x150}));++cases; // set bonus beats keeping the doublet
  assert(!p.food.valid);++cases;}
 {auto o=armor_options(pouch,TempDanger::Cold);auto p=plan_clothing(TempDanger::Cold,1,0,0,o,no_food);(void)p;++cases;}
 // Heat need 2 with nothing worn: quick takes two pieces (head, upper), best = Desert Voe 5* set.
 {auto o=armor_options(pouch,TempDanger::Heat);auto p=plan_clothing(TempDanger::Heat,2,0,0,o,no_food);
  assert(p.quick.pieces==2 && p.quick.equip==(std::vector<uint64_t>{0x190,0x1A0}) && p.quick.level==2);
  assert(p.best.level==3 && p.best.set_bonus && p.best.pieces==3);++cases;}
 // Heat need 2 with a level-1 heat elixir active: one piece is enough.
 {auto o=armor_options(pouch,TempDanger::Heat);auto p=plan_clothing(TempDanger::Heat,2,1,1,o,no_food);assert(p.quick.pieces==1 && p.quick.level==2);++cases;}
 // Flame guard: levels clamp at 2 natively; best outfit picks the 5* Flamebreaker head + the only upper.
 {auto o=armor_options(pouch,TempDanger::Burn);auto p=plan_clothing(TempDanger::Burn,1,0,0,o,no_food);
  assert(p.quick.pieces==1 && p.quick.equip==std::vector<uint64_t>{0x1C0} && p.quick.level==1);
  assert(p.best.level==2 && !p.best.set_bonus && p.best.pieces==2);++cases;
  auto q=plan_clothing(TempDanger::Burn,2,0,0,o,no_food);assert(q.quick.pieces==2 && q.quick.reaches && q.quick.level==2);++cases;}
 // Full Flamebreaker 5* set: set bonus detected, level stays 2.
 {std::vector<It> fb{{"Armor_076_Head",4,0,false,1},{"Armor_076_Upper",5,0,false,2},{"Armor_076_Lower",6,0,false,3},{"Armor_011_Head",4,0,false,4}};
  auto p=plan_clothing(TempDanger::Burn,2,0,0,armor_options(fb,TempDanger::Burn),no_food);assert(p.best.level==2 && p.best.set_bonus && p.best.pieces==3);++cases;}
 // Upgrades: equal level, higher star wins; worn equal piece is kept (no pointless swap).
 {std::vector<It> d{{"Armor_008_Head",4,0,false,1},{"Armor_066_Head",4,0,false,2},{"Armor_128_Head",4,0,false,3}};
  auto p=plan_clothing(TempDanger::Heat,1,0,0,armor_options(d,TempDanger::Heat),no_food);assert(p.quick.equip==std::vector<uint64_t>{3});++cases; // 5* Sapphire over 4* Desert Voe
  std::vector<It> w{{"Armor_008_Head",4,0,true,1},{"Armor_128_Head",4,0,false,3}};
  auto q=plan_clothing(TempDanger::Heat,1,1,0,armor_options(w,TempDanger::Heat),no_food);assert(!q.quick.valid && !q.best.worn && q.best.level==1 && q.best.equip==std::vector<uint64_t>{3});++cases; // best prefers the upgrade
  std::vector<It> same{{"Armor_128_Head",4,0,true,1},{"Armor_128_Head",4,0,false,3}};
  auto r=plan_clothing(TempDanger::Heat,1,1,0,armor_options(same,TempDanger::Heat),no_food);assert(r.best.worn && r.best.pieces==0);++cases;}
 // Ties between identical instances: lowest address.
 {std::vector<It> t{{"Armor_009_Head",4,0,false,0x50},{"Armor_009_Head",4,0,false,0x40}};
  auto p=plan_clothing(TempDanger::Cold,1,0,0,armor_options(t,TempDanger::Cold),no_food);assert(p.quick.equip==std::vector<uint64_t>{0x40} && p.best.equip==std::vector<uint64_t>{0x40});++cases;}
 // Combo effect: Hero's tunic (ResistHotAndWakeWind) counts as heat 1.
 {std::vector<It> t{{"Armor_175_Upper",5,0,false,9}};auto p=plan_clothing(TempDanger::Heat,1,0,0,armor_options(t,TempDanger::Heat),no_food);assert(p.quick.valid && p.quick.level==1);++cases;}
 // Not reachable with armor: partial fix reported with its level.
 {std::vector<It> t{{"Armor_009_Head",4,0,false,1}};auto p=plan_clothing(TempDanger::Cold,2,0,0,armor_options(t,TempDanger::Cold),no_food);assert(p.quick.valid && !p.quick.reaches && p.quick.level==1);++cases;}
 // Empty case + food: no armor helps; cold food picked.
 {std::vector<It> t{{"Armor_001_Head",4,0,true,1}};std::vector<FoodOption> f{{0x10,"Item_Cook_C_17",1,300},{0x20,"Item_Cook_C_18",2,120}};
  auto p=plan_clothing(TempDanger::Cold,2,0,0,armor_options(t,TempDanger::Cold),f);assert(p.empty && p.food.valid && p.food.food==0x20 && p.food.reaches && p.food.level==2);++cases; // only level 2 reaches
  auto q=plan_clothing(TempDanger::Cold,1,0,0,armor_options(t,TempDanger::Cold),f);assert(q.food.food==0x10 && q.food.seconds==300);++cases;} // both reach, longer wins
 {std::vector<FoodOption> f{{0x10,"A",1,300},{0x20,"B",1,300},{0x30,"C",2,300}};auto p=plan_clothing(TempDanger::Cold,1,0,0,{},f);assert(p.food.food==0x10);++cases; // lower level, then name
  std::vector<FoodOption> g{{0x10,"A",1,900},{0x20,"B",1,100}};auto q=plan_clothing(TempDanger::Cold,3,0,0,{},g);assert(!q.food.reaches && q.food.food==0x10);++cases;}
 {std::vector<FoodOption> f{{0x10,"Elixir",1,600}};std::vector<It> w{{"Armor_044_Upper",5,0,true,1}};auto p=plan_clothing(TempDanger::Cold,2,1,0,armor_options(w,TempDanger::Cold),f);assert(p.food.reaches);++cases;} // food + worn armor
 {auto p=plan_clothing(TempDanger::None,1,0,0,{},no_food);assert(!p.valid);++cases;}
 // Wrong-slot metadata guard: an Upper actor reported as a head is ignored for levels.
 {std::vector<It> t{{"Armor_044_Upper",4,0,false,1}};auto o=armor_options(t,TempDanger::Cold);assert(o.size()==1 && o[0].level==0);++cases;}
 std::cout<<cases<<" temperature danger/latch/clothing plan cases passed\n";
}
