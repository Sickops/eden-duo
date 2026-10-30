// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct EnvironmentFixture:Fixture {
  static constexpr uint64_t World=0x8000000,Array=0x8100000,Weather=0x8200000,Temp=0x8300000;
  EnvironmentFixture() {
    put(M+0x2CE1E30,World);put(World,M+0x2C02270);
    put<int32_t>(World+0x5C0,9);put<int32_t>(World+0x5C4,9);put(World+0x5C8,Array);
    put<int32_t>(World+0x780,3);put(Array+24,Weather);put(Array+32,Temp);
    put(Weather,M+0x2C01B60);put(Temp,M+0x2C018B8);
    put<float>(Temp+0x40,6.2463f);put<float>(Temp+0x20,6.2f);put<float>(Temp+0x60,6.2f);
    put(Weather+0x284,std::array<uint8_t,5>{0,1,2,6,4});
  }
};
int main() {
  using F=EnvironmentFixture;unsigned cases{};
  auto check=[&](auto change,bool valid) {
    F f;change(f);const auto e=botw::environment(f.h);assert(e.valid==valid);++cases;
  };
  {F f;const auto e=botw::environment(f.h);assert(e.valid&&e.temperature==6.2463f);assert((e.forecast==std::array<int32_t,5>{0,1,2,3,4}));++cases;}
  for(int raw=0;raw<9;++raw) {
    F f;for(unsigned i=0;i<5;++i)f.put<uint8_t>(F::Weather+0x284+i,uint8_t(raw));
    const auto e=botw::environment(f.h);constexpr int frames[]={0,1,2,2,4,4,3,3,2};
    assert(e.valid&&e.forecast[0]==frames[raw]);++cases;
  }
  for(float t:{-2.1f,-2.f,-1.99f})for(auto offset:{0x20,0x60}) {
    F f;f.put<float>(F::Temp+offset,t);const auto e=botw::environment(f.h);
    assert(e.valid&&e.forecast[2]==(t<=-2.f?4:2));++cases;
  }
  for(float t:{-100.f,100.f})check([&](F&f){f.put(F::Temp+0x40,t);},true);
  for(float t:{-100.1f,100.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})check([&](F&f){f.put(F::Temp+0x40,t);},false);
  for(auto offset:{0x20,0x60})check([&](F&f){f.put(F::Temp+offset,std::numeric_limits<float>::quiet_NaN());},false);
  check([](F&f){f.h.build_id[0]^=1;},false);
  check([](F&f){f.h.main_size=0x2CE1E30;},false);
  check([](F&f){f.h.main_base=UINT64_MAX-8;},false);
  for(auto at:{F::M+0x2CE1E30,F::World+0x5C8,F::Array+24,F::Array+32}) {
    check([&](F&f){f.put<uint64_t>(at,0x10001);},false);
    check([&](F&f){f.put<uint64_t>(at,UINT64_MAX-7);},false);
  }
  for(auto at:{F::World,F::Weather,F::Temp})check([&](F&f){f.put<uint64_t>(at,0);},false);
  for(int n:{0,8,10,INT32_MAX})check([&](F&f){f.put<int32_t>(F::World+0x5C0,n);},false);
  check([](F&f){f.put<int32_t>(F::World+0x5C4,10);},false);
  for(int n:{-1,20})check([&](F&f){f.put<int32_t>(F::World+0x780,n);},false);
  for(unsigned i=0;i<5;++i)check([&](F&f){f.put<uint8_t>(F::Weather+0x284+i,255);},false);
  for(auto at:{F::M+0x2CE1E30,F::World+0x5C0,F::World+0x780,F::Array+24,F::Array+32,F::Temp+0x40,F::Temp+0x20,F::Weather+0x284,F::U+0x64C30})check([&](F&f){f.race=at;},false);
  for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000001ULL})check([&](F&f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
  check([](F&f){f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);},true);
  {F f;State s;sample(&s,&f.h);assert(f.values["botw.ready.environment.valid"]==1);assert(f.texts["botw.ready.temperature.text"]=="43°F");s.next={};f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);sample(&s,&f.h);assert(f.values["botw.ready.environment.valid"]==0&&f.texts["botw.ready.temperature.text"].empty());for(unsigned i=0;i<5;++i)assert(f.values["botw.ready.weather."+std::to_string(i)]==-1);++cases;}
  std::cout<<cases<<" environment cases passed\n";
}
