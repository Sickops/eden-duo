// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_consumables.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct FoodFixture:Fixture {
 static constexpr uint64_t P=0x9000000;
 botw::Item item;
 FoodFixture(const char* name="Item_Cook_B_01",int type=8,int use=8) {
  item={name,type,1,false,P};put(P,std::array<uint8_t,0x98>{});
  put<int32_t>(P+0x18,type);put<int32_t>(P+0x1C,use);put<int32_t>(P+0x20,1);put<uint8_t>(P+0x25,1);
  put<uint64_t>(P+0x28,M+0x2A6FAF0);put<uint64_t>(P+0x30,P+0x3C);put<int32_t>(P+0x38,64);
  std::array<char,64> text{};std::memcpy(text.data(),name,std::strlen(name));put(P+0x3C,text);
  put<int32_t>(P+0x80,4);put<int32_t>(P+0x84,120);put<int32_t>(P+0x88,40);put<float>(P+0x8C,13);put<float>(P+0x90,1);
 }
 botw::Consumable sample(){return botw::consumable(h,item);}
};
int main(){using F=FoodFixture;unsigned cases{};
 {F f;const auto c=f.sample();assert(c.valid && c.recipe && c.effect==13 && c.level==1 && c.frames==3600 && c.health==4 && !c.health_only);++cases;}
 for(const auto name:{"Item_Fruit_A","Item_Mushroom_E"}) {F f(name,7,10);auto c=f.sample();assert(c.valid && !c.recipe && c.health>0 && c.health_only && c.effect==-1 && c.frames==0);++cases;}
 {F f("Item_Roast_02",8,10);auto c=f.sample();assert(c.valid && c.health==6 && c.health_only);++cases;}
 {F f("Item_Chilled_01",8,10);auto c=f.sample();assert(c.valid && c.health==4 && c.effect==4 && c.level==1 && c.frames==1800 && !c.health_only);++cases;}
 {F f("Item_Enemy_01",7,8);assert(!f.sample().valid);++cases;}
 for(int effect:{-1,1,2,4,5,6,10,11,12,13,14,15,16}){F f;f.put<float>(F::P+0x8C,float(effect));f.put<float>(F::P+0x90,float(effect==14 ? 1000 : effect==15 ? 5 : effect==2 ? 4 : 1));assert(f.sample().valid);++cases;}
 for(int effect:{0,3,7,8,9,17}){F f;f.put<float>(F::P+0x8C,float(effect));assert(!f.sample().valid);++cases;}
 for(float v:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-2.f,17.f,13.5f}){F f;f.put<float>(F::P+0x8C,v);assert(!f.sample().valid);++cases;}
 for(float v:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f,3001.f,1.5f,4.f}){F f;f.put<float>(F::P+0x90,v);assert(!f.sample().valid);++cases;}
 for(auto [at,value]:{std::pair{0x18,7}, {0x1C,10},{0x20,0},{0x38,63},{0x80,-1},{0x80,121},{0x84,-1},{0x84,86401},{0x88,-1},{0x88,1000000}}){F f;f.put<int32_t>(F::P+at,value);assert(!f.sample().valid);++cases;}
 for(unsigned at:{0x24u,0x25u}){F f;f.put<uint8_t>(F::P+at,at==0x24 ? 1 : 0);assert(!f.sample().valid);++cases;}
 for(unsigned at:{0x28u,0x30u}){F f;f.put<uint64_t>(F::P+at,0);assert(!f.sample().valid);++cases;}
 {F f;f.h.build_id[0]^=1;assert(!f.sample().valid);++cases;}
 {F f;f.item.actor="Item_Cook_A_01";assert(!f.sample().valid);++cases;}
 {F f;f.race=F::P;assert(!f.sample().valid);++cases;}
 {F f;f.mem.erase(F::P+0x90);assert(!f.sample().valid);++cases;}
 {F f;f.item.address+=1;assert(!f.sample().valid);++cases;}
 std::cout<<cases<<" consumable metadata/eligibility/identity/bounds/race cases passed\n";
 return 0;
}
