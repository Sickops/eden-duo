// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_ready.h"
#include <cassert>
#include <iostream>
int main() {
  botw::Snapshot s;
  s.items={{"Weapon_Sword_044",0,400,true,0x1000},
           {"Weapon_Bow_001",1,120,true,0x2000},
           {"NormalArrow",2,42,true,0x3000},
           {"Weapon_Shield_001",3,100,true,0x4000},
           {"Weapon_Sword_001",0,100,false,0x5000}};
  assert(botw::ready_equipment(s)==(std::array<std::string,4>{}));
  s.valid=true;
  auto a=botw::ready_equipment(s);
  assert(a[0]=="Weapon_Sword_044" && a[1]=="Weapon_Bow_001" &&
         a[2]=="NormalArrow" && a[3]=="Weapon_Shield_001");
  s.items.push_back(s.items.front());s.items.back().address=0x6000;
  a=botw::ready_equipment(s);assert(a[0].empty() && a[1]=="Weapon_Bow_001");
  s.items.push_back({"Armor_001_Head",4,0,true,0x7000});
  assert(botw::ready_equipment(s)==a);
  s.items.clear();assert(botw::ready_equipment(s)==(std::array<std::string,4>{}));
  s.valid=false;s.items={{"Weapon_Bow_001",1,100,true,0x2000}};
  assert(botw::ready_equipment(s)==(std::array<std::string,4>{}));
  { // Condition is life x100 at full; unknown actors, arrows and negatives are unavailable.
  botw::Item sword{"Weapon_Sword_001",0,2000,true,0x10};assert(botw::durability_percent(sword)==100);
  sword.value=400;assert(botw::durability_percent(sword)==20);
  sword.value=9000;assert(botw::durability_percent(sword)==100);
  sword.value=-1;assert(botw::durability_percent(sword)==-1);
  botw::Item bow{"Weapon_Bow_001",1,1100,true,0x20};assert(botw::durability_percent(bow)==50);
  botw::Item shield{"Weapon_Shield_001",3,0,true,0x30};assert(botw::durability_percent(shield)==0);
  botw::Item arrow{"NormalArrow",2,30,true,0x40};assert(botw::durability_percent(arrow)==-1);
  botw::Item unknown{"Weapon_Unknown",0,100,true,0x50};assert(botw::durability_percent(unknown)==-1);
  {botw::Item worn{"Weapon_Sword_001",0,2000,false,0x70};assert(botw::durability_percent(worn,0x2,20)==50);
   assert(botw::durability_percent(worn,0x80000001,15)==100);assert(botw::durability_percent(worn,0x80000002,20)==50);}
  {botw::Item master{"Weapon_Sword_070",0,7440,true,0x80};assert(botw::durability_percent(master)==-1);
   master.actor="Weapon_Sword_071";assert(botw::durability_percent(master)==-1);master.actor="Weapon_Sword_073";
   assert(botw::durability_percent(master)!=-1 || !botw::weapon_info("Weapon_Sword_073"));}
  botw::Snapshot p{true,{sword,bow,{"Weapon_Sword_002",0,100,true,0x60}}};auto r=botw::ready_items(p);
  assert(!r[0] && r[1]==&p.items[1] && !r[3]);
  {botw::Snapshot a{true,{{"NormalArrow",2,20,true,0x100},{"FireArrow",2,5,false,0x200},{"IceArrow",2,0,false,0x300},{"BombArrow_A",2,3,false,0x400},{"Weapon_Bow_001",1,100,true,0x500}}};unsigned k{};
   assert(botw::arrow_step(a,1,&k)->address==0x200 && k==3);assert(botw::arrow_step(a,-1)->address==0x400);
   a.items[0].equipped=false;a.items[3].equipped=true;assert(botw::arrow_step(a,1)->address==0x100);
   a.items[1].equipped=true;assert(!botw::arrow_step(a,1));a.items[1].equipped=false;
   assert(!botw::arrow_step(a,2));botw::Snapshot one{true,{{"NormalArrow",2,9,true,0x100}}};assert(!botw::arrow_step(one,1,&k) && k==1);
   a.valid=false;assert(!botw::arrow_step(a,1,&k) && k==0);}
  std::cout<<"durability percent/ready item cases passed\n";
 }
 std::cout<<"6 Ready equipment cases passed\n";
}
