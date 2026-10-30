// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct DialFixture:Fixture {
 static constexpr uint64_t Hud=0xB000000,Pane=0xB010000,Animation=0xB020000;
 DialFixture(){put(A+37*8,Hud);put(Hud,M+0x2BA14E8);put(Hud+0x36D8,Pane);put(Pane,M+0x2B8F8D0);put(Pane+0x130,Animation);put(Animation,M+0x2BBE308);put<float>(Animation+0x20,74);
 for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL}){std::array<uint8_t,56>b{};uint64_t vt=M+0x2A701C8,p=M+off+20;int32_t cap=32;std::memcpy(b.data(),&vt,8);std::memcpy(b.data()+8,&p,8);std::memcpy(b.data()+16,&cap,4);std::memcpy(b.data()+20,"MainField",10);put(M+off,b);}
 }
};
int main(){using F=DialFixture;unsigned cases{};auto check=[&](auto change,bool valid){F f;change(f);auto s=botw::temperature_dial(f.h);assert(s.valid==valid);++cases;};
 check([](F&){},true);check([](F&f){f.h.build_id[0]^=1;},false);check([](F&f){f.h.main_size=1;},false);check([](F&f){f.h.main_base=UINT64_MAX-10;},false);
 for(uint64_t at:{F::A+37*8,F::Hud+0x36D8,F::Pane+0x130})for(uint64_t p:{0ULL,0x10001ULL,UINT64_MAX-7ULL})check([&](F&f){f.put(at,p);},false);
 for(uint64_t at:{F::Hud,F::Pane,F::Animation})check([&](F&f){f.put<uint64_t>(at,0);},false);
 for(float frame:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f,121.f})check([&](F&f){f.put(F::Animation+0x20,frame);},false);
 for(uint64_t flags:{0x1849000000ULL,0x0E09E00000ULL,0ULL})check([&](F&f){f.put(F::U+0x64C30,flags);},false);
 for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL})check([&](F&f){f.put<char>(F::M+off+20,'X');},false);
 for(uint64_t at:{F::A+37*8,F::Hud,F::Hud+0x36D8,F::Pane,F::Pane+0x130,F::Animation,F::Animation+0x20,F::U+0x64C30,F::M+0x2C9E868})check([&](F&f){f.race=at;},false);
 for(auto [frame,angle]:{std::pair{0.f,114},std::pair{20.f,114},std::pair{22.f,114},std::pair{60.f,0},std::pair{74.f,-42},std::pair{98.f,-114},std::pair{120.f,-114}}){F f;f.put(F::Animation+0x20,frame);auto s=botw::temperature_dial(f.h);assert(s.valid&&s.frame==frame&&botw::temperature_rotation(s)==angle);botw::publish_temperature_dial(f.h,s);assert(f.values["botw.ready.temperature.dial.valid"]==1 && f.values["botw.ready.temperature.dial.rotation"]==angle);++cases;}
 {F f;State s;sample(&s,&f.h);assert(f.values["botw.ready.temperature.dial.valid"]==1);f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);s.next={};sample(&s,&f.h);assert(!f.values["botw.ready.temperature.dial.valid"] && f.values["botw.ready.temperature.dial.rotation"]==0);++cases;}
 {F f;State s;sample(&s,&f.h);f.h.read_memory=[](void*,uint64_t,void*,size_t)->EdenDsmodBool{throw 1;};s.next={};sample(&s,&f.h);assert(!f.values["botw.ready.temperature.dial.valid"]);++cases;}
 std::cout<<cases<<" native temperature animation/bounds/race/curve/context cases passed\n";
}
