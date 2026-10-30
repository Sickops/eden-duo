// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_quick_detail.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace botw;
  Item sword{"Weapon_Sword_001",0,400,false,0x10000};
  auto d=quick_detail(sword,{true,0,0});
  assert(d.stat==5 && d.stat_kind==1 && d.condition==20 && d.modifier==0);
  d=quick_detail(sword,{true,0x80000001,7});
  assert(d.stat==12 && d.modifier==1 && d.modifier_value==7 && d.yellow && d.condition==20);
  d=quick_detail(sword,{true,2,20});assert(d.condition==10 && d.stat==5);
  Item shield{"Weapon_Shield_030",3,79900,true,0x20000};
  d=quick_detail(shield,{true,0x100,15});assert(d.stat_kind==2 && d.stat==105 && d.condition==99);
  Item bow{"Weapon_Bow_032",1,4500,false,0x30000};
  d=quick_detail(bow,{true,0,0});assert(d.stat==32 && d.shots==3);
  d=quick_detail(bow,{true,0x80000010,5});assert(d.stat==32 && d.shots==5 && d.modifier==0x10);
  d=quick_detail(bow,{true,0x10,999});assert(d.shots==-1);
  d=quick_detail(sword,{true,3,10});assert(d.modifier==0 && d.stat==15);
  d=quick_detail(sword,{true,0x200,10});assert(d.modifier==0);
  d=quick_detail(sword,{true,0x20,10});assert(d.modifier==0 && d.stat==5);
  d=quick_detail(sword,{true,8,0x3FC00000});assert(d.modifier==8 && d.stat==5);
  d=quick_detail(sword,{false,0,0});assert(d.stat==-1 && d.condition==-1);
  d=quick_detail(sword,{true,1,0xFFFFFFFF});assert(d.stat==-1 && d.stat_kind==0);
  // Fix round 4 (critique-3): native quick-wheel captures show a stat badge on the Master
  // Sword; durability/condition stays -1 (genuinely no life bar, confirmed separately by
  // durability_percent()'s own exclusion), but the static attack value is now shown like any
  // other weapon.
  sword.actor="Weapon_Sword_070";sword.value=400;d=quick_detail(sword,{true,0,0});assert(d.stat==30 && d.condition==-1);
  sword.actor="Weapon_Sword_502";assert(quick_detail(sword,{true,0,0}).stat>0);
  sword.actor="unknown";assert(quick_detail(sword,{true,0,0}).stat==-1);
  sword.actor="Weapon_Sword_001";sword.type=2;assert(quick_detail(sword,{true,0,0}).stat==-1);
  // Iteration 11 D: Durability Up (bit 2) uses the native gem badge like every other modifier.
  assert(std::string(modifier_icon_for(2,0))=="modifier_durability");
  assert(std::string(modifier_icon_for(1,0))=="modifier_attack" && std::string(modifier_icon_for(512,0)).empty());
  std::cout<<"17 quick detail/native metadata guard cases passed\n";
}
