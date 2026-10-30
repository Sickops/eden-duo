// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_armor_fit.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct ArmorFixture:Fixture {
 static constexpr uint64_t InfoData=0x9900000;
 botw::Snapshot pouch;
 botw::PlayerEquipmentSnapshot equipment;
 botw::Item target;
 ArmorFixture() {
  put(M+0x2CC4490,InfoData);put(InfoData,M+0x2BCAB60);
  pouch.valid=true;equipment.valid=true;
  botw::Item head;head.address=0x8000000;head.actor="Armor_002_Head";
  head.type=4;head.value=8;head.equipped=true;pouch.items.push_back(head);
  botw::Item upper;upper.address=0x8100000;upper.actor="Armor_007_Upper";
  upper.type=5;upper.value=-1;upper.equipped=true;pouch.items.push_back(upper);
  equipment.slots[3].name=head.actor;equipment.slots[4].name=upper.actor;
  target.address=0x8200000;target.actor="Armor_180_Upper";target.type=5;target.value=-1;
 }
 botw::ArmorFitPlan fit(){return botw::armor_fit(h,pouch,equipment,target);}
};
int main() {
 using F=ArmorFixture;unsigned cases{};
 {F f;auto p=f.fit();assert(p.valid && p.needs_b && p.refresh_head &&
  p.head_actor=="Armor_002_Head_B" && p.target_actor=="Armor_180_Upper");++cases;}
 {F f;f.pouch.items[1].actor="Armor_180_Upper";f.equipment.slots[4].name="Armor_180_Upper";
  f.equipment.slots[3].name="Armor_002_Head_B";f.target.actor="Armor_007_Upper";
  auto p=f.fit();assert(p.valid && !p.needs_b && p.refresh_head && p.head_actor=="Armor_002_Head");++cases;}
 {F f;f.equipment.slots[3].name="Armor_002_Head_B";auto p=f.fit();
  assert(p.valid && p.needs_b && !p.refresh_head);++cases;}
 {F f;f.target.type=4;f.target.actor="Armor_002_Head";auto p=f.fit();
  assert(p.valid && !p.needs_b && !p.refresh_head && p.target_actor=="Armor_002_Head");++cases;}
 {F f;f.target.type=4;f.target.actor="Armor_002_Head";f.pouch.items[1].actor="Armor_180_Upper";
  f.equipment.slots[4].name="Armor_180_Upper";auto p=f.fit();assert(p.valid && p.needs_b &&
  !p.refresh_head && p.target_actor=="Armor_002_Head_B");++cases;}
 {F f;f.pouch.items.clear();f.equipment.slots[3].name="Armor_Default_Head";
  f.equipment.slots[4].name="Armor_Default_Upper";auto p=f.fit();
  assert(p.valid && !p.needs_b && !p.refresh_head);++cases;}
 auto refuse=[&](auto change){F f;change(f);assert(!f.fit().valid);++cases;};
 refuse([](F&f){f.pouch.valid=false;});refuse([](F&f){f.equipment.valid=false;});
 refuse([](F&f){f.target.type=6;});refuse([](F&f){f.target.equipped=true;});
 refuse([](F&f){f.target.value=-2;});refuse([](F&f){f.target.value=16;});
 refuse([](F&f){f.target.actor="Armor_NotKnown_Upper";});
 refuse([](F&f){f.pouch.items.push_back(f.pouch.items[0]);});
 refuse([](F&f){f.pouch.items.push_back(f.pouch.items[1]);});
 refuse([](F&f){f.pouch.items[0].value=16;});
 refuse([](F&f){f.equipment.slots[3].name="Armor_073_Head";});
 refuse([](F&f){f.equipment.slots[4].name="Armor_151_Upper";});
 refuse([](F&f){f.put<uint64_t>(F::M+0x2CC4490,0);});
 refuse([](F&f){f.put<uint64_t>(F::InfoData,0);});
 std::cout<<cases<<" armor pairing/default/variant/identity cases passed\n";
}
