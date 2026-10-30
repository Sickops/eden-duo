// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_gear.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct GearFixture : Fixture {
  static constexpr uint64_t Screen=0x7005000, One=0x8000000, Two=0x8000100;
  botw::Snapshot pouch{true,{}};
  explicit GearFixture(int type=0) {
    put(A+45*8,Screen);put(Screen,M+0x2BA1D40);put(Screen+0x150,M+0x2BA1C10);
    const int selector=type==3 ? 1 : type==1 ? 2 : type==2 ? 3 : 0;
    put<int32_t>(Screen+0x3720,selector);put<int32_t>(Screen+0x3724,selector);
    put<uint32_t>(U+0x64C24,3);put<uint32_t>(U+0x64C58,(type==3 || type==2) ? 0x40000 : 0x80000);
    put<uint64_t>(U+0x64C30,0x1E09000004ULL);
    put(Screen+0x37B0,std::array<uint64_t,20>{Two,One});
    put<int32_t>(Screen+0x3860,1);put<int32_t>(Screen+0x3864,0);
    put<int32_t>(Screen+0x3868,2);put<uint8_t>(Screen+0x38CC,3);
    for (uint64_t address:{One,Two}) {
      put(address,std::array<uint8_t,0x38>{});
      put<int32_t>(address+0x18,type);put<int32_t>(address+0x20,400);
      put<uint8_t>(address+0x24,address==One);put<uint8_t>(address+0x25,1);
      put<uint64_t>(address+0x30,address+0x80);
      const char actor[]="Weapon_Test";
      for (size_t i=0;i<sizeof(actor);++i) put<char>(address+0x80+i,actor[i]);
      pouch.items.push_back({actor,type,400,address==One,address});
    }
  }
  botw::GearSnapshot get() {return botw::gear(h,pouch);}
};
int main() {
  unsigned passed=0;
  for (int type:{0,1,2,3}) {
    GearFixture f(type);auto s=f.get();assert(s.valid && s.items.size()==2);
    assert(s.initial==1 && s.highlighted==0 && s.items[0].address==GearFixture::Two);
    assert(s.items[1].address==GearFixture::One && s.items[0].actor==s.items[1].actor);
    ++passed;
  }
  {GearFixture f;auto s=f.get();assert(!s.items[size_t(s.highlighted)].equipped && s.items[size_t(s.initial)].equipped);
   f.put<int32_t>(f.Screen+0x3864,1);auto after=f.get();
   assert(after.valid && after.initial==1 && after.highlighted==1 && after.items[size_t(after.highlighted)].address==f.One);
   assert(after.items[0].actor==after.items[1].actor && !after.items[0].equipped && after.items[1].equipped);++passed;}
  auto reject=[&](auto change) {GearFixture f;change(f);assert(!f.get().valid);++passed;};
  reject([](GearFixture& f){f.pouch.valid=false;});
  reject([](GearFixture& f){f.pouch.items[0].address=0;});
  reject([](GearFixture& f){f.pouch.items.push_back(f.pouch.items[0]);});
  reject([](GearFixture& f){f.pouch.items[0].actor="Wrong_Actor";});
  reject([](GearFixture& f){f.pouch.items[0].value=401;});
  reject([](GearFixture& f){f.pouch.items[0].equipped=false;});
  reject([](GearFixture& f){f.put<uint64_t>(f.Screen+0x37B0,f.One);});
  reject([](GearFixture& f){f.put<uint64_t>(f.Screen+0x37B0,f.Two+1);});
  reject([](GearFixture& f){f.put<int32_t>(f.Screen+0x3868,21);});
  reject([](GearFixture& f){f.put<int32_t>(f.Screen+0x3868,-1);});
  reject([](GearFixture& f){f.put<int32_t>(f.Screen+0x3860,2);});
  reject([](GearFixture& f){f.put<int32_t>(f.Screen+0x3864,2);});
  reject([](GearFixture& f){f.put<int32_t>(f.Screen+0x3864,-2);});
  reject([](GearFixture& f){f.put<uint8_t>(f.Screen+0x38CC,2);});
  reject([](GearFixture& f){f.put<uint8_t>(f.Two+0x25,0);});
  reject([](GearFixture& f){f.put<int32_t>(f.Two+0x18,1);});
  reject([](GearFixture& f){f.put<uint8_t>(f.Two+0x24,2);});
  reject([](GearFixture& f){f.put<uint64_t>(f.Two+0x30,UINT64_MAX-5);});
  reject([](GearFixture& f){f.mem.erase(f.Screen+0x37B0+19*8);});
  reject([](GearFixture& f){f.h.build_id[0]^=1;});
  reject([](GearFixture& f){f.put<uint64_t>(f.U+0x64C30,0x0E09E00000ULL);});
  reject([](GearFixture& f){f.race=f.Screen+0x37B0;});
  reject([](GearFixture& f){f.race=f.Two;});
  reject([](GearFixture& f){f.race=f.Two+0x80;});
  {GearFixture f;f.put<int32_t>(f.Screen+0x3860,-1);f.put<int32_t>(f.Screen+0x3864,-1);assert(f.get().valid);++passed;}
  {GearFixture f;f.put<int32_t>(f.Screen+0x3860,-1);f.put<int32_t>(f.Screen+0x3864,-1);f.put<int32_t>(f.Screen+0x3868,0);assert(f.get().valid && f.get().items.empty());++passed;}
  {GearFixture f(2);f.put<int32_t>(f.Screen+0x3868,7);assert(!f.get().valid);++passed;}
  std::cout<<passed<<" native gear identity/order/bounds/race/closing cases passed\n";
}
