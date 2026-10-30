// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct LandmarkFixture:Fixture {
 static constexpr uint64_t Map=0xA000000,Array=0xA001000,Records=0xA010000,Sources=0xA100000;
 LandmarkFixture() {
  put(M+0x2CA63B0,Map);put(Map,M+0x2B86848);
  for(unsigned a=0;a<2;++a){auto off=a?0x310:0x2F0;put<int32_t>(Map+off,a?15:120);put<int32_t>(Map+off+4,a?15:120);put(Map+off+8,Array+a*0x1000);}
  for(unsigned i=0;i<135;++i){const bool tower=i>=120;const uint64_t p=Records+i*0x100,source=Sources+i*0x40;put(Array+(tower?0x1000:0)+(tower?i-120:i)*8,p);put(p,M+(tower?0x2B86A00:0x2B86010));put<int32_t>(p+0x1C,tower?3:2);put(p+0x28,std::array<float,3>{float(i),300,-float(i)});put(p+0x58,source);put<int32_t>(source+0x1C,1);put<int32_t>(p+0x60,1);put<uint32_t>(p+0x3C,0xA0000);put<uint8_t>(p+0x64,1);}
  for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL}){std::array<uint8_t,56>b{};uint64_t vt=M+0x2A701C8,p=M+off+20;int32_t cap=32;std::memcpy(b.data(),&vt,8);std::memcpy(b.data()+8,&p,8);std::memcpy(b.data()+16,&cap,4);std::memcpy(b.data()+20,"MainField",10);put(M+off,b);}
 }
};
int main(){using F=LandmarkFixture;unsigned cases{};auto check=[&](auto change,bool valid){F f;change(f);auto v=botw::map_landmarks(f.h);if(v.valid!=valid)std::cerr<<"case "<<cases+1<<'\n';assert(v.valid==valid);++cases;};
 check([](F&){},true);check([](F&f){f.h.build_id[0]^=1;},false);check([](F&f){f.h.main_size=1;},false);check([](F&f){f.h.main_base=UINT64_MAX-10;},false);
 for(uint64_t at:{F::M+0x2CA63B0,F::Map+0x2F8,F::Map+0x318,F::Array,F::Records+0x58})for(uint64_t p:{0ULL,0x10001ULL,UINT64_MAX-7ULL})check([&](F&f){f.put(at,p);},false);
 for(uint64_t at:{F::Map,F::Records,F::Records+120*0x100})check([&](F&f){f.put<uint64_t>(at,0);},false);
 for(int32_t n:{-1,0,119,137,INT32_MAX})check([&](F&f){f.put(F::Map+0x2F0,n);},false);
 for(int32_t n:{-1,0,14,16,INT32_MAX})check([&](F&f){f.put(F::Map+0x310,n);},false);
 check([](F&f){f.put<int32_t>(F::Map+0x2F4,137);},false);check([](F&f){f.put<int32_t>(F::Map+0x314,16);},false);
 check([](F&f){f.put(F::Array+8,F::Records);},false);check([](F&f){f.put(F::Records+0x100+0x58,F::Sources);},false);
 check([](F&f){f.put<int32_t>(F::Records+0x1C,3);},false);check([](F&f){f.put<int32_t>(F::Sources+0x1C,-1);},false);check([](F&f){f.put<int32_t>(F::Records+0x60,2);},false);check([](F&f){f.put<uint8_t>(F::Records+120*0x100+0x64,2);},false);
 for(float n:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),6001.f,-6001.f})check([&](F&f){f.put(F::Records+0x28,n);},false);
 check([](F&f){f.put<float>(F::Records+0x2C,10001);},false);check([](F&f){f.put<float>(F::Records+0x30,5001);},false);
 for(uint64_t flags:{0x1849000000ULL,0x0E09E00000ULL,0ULL})check([&](F&f){f.put(F::U+0x64C30,flags);},false);
 for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL})check([&](F&f){f.put<char>(F::M+off+20,'X');},false);
 for(uint64_t at:{F::M+0x2CA63B0,F::Map,F::Map+0x2F0,F::Map+0x318,F::Array,F::Records,F::Records+0x1C,F::Records+0x28,F::Records+0x58,F::Sources+0x1C,F::Records+0x60,F::U+0x64C30,F::M+0x2C9E868})check([&](F&f){f.race=at;},false);
 {F f;auto v=botw::map_landmarks(f.h);assert(v.count==135&&v.entries[0].visible&&v.entries[0].opened&&v.entries[0].cleared&&v.entries[120].kind==3);botw::publish_map_landmarks(f.h,v);assert(f.values["botw.map.poi.count"]==135&&!f.values.count("botw.map.poi.135.kind"));++cases;
  // Perf round 3b: publish_map_landmarks no longer self-clears a slot's keys once it goes
  // inactive (runtime-per-tick-clear contract, see botw_map_landmarks.h); simulate that clear.
  f.values.clear();f.texts.clear();
  botw::publish_map_landmarks(f.h,{});assert(!f.values["botw.map.poi.valid"]&&f.values["botw.map.poi.count"]==0&&!f.values.count("botw.map.poi.0.kind"));++cases;}
 {F f;for(unsigned i=0;i<135;++i){f.put<int32_t>(F::Sources+i*0x40+0x1C,0);f.put<int32_t>(F::Records+i*0x100+0x60,0);}auto v=botw::map_landmarks(f.h);assert(v.valid&&v.count==135);botw::publish_map_landmarks(f.h,v);assert(f.values["botw.map.poi.count"]==0);++cases;}
 {F f;f.put<uint32_t>(F::Records+0x3C,0x20000);auto v=botw::map_landmarks(f.h);assert(v.entries[0].opened&&!v.entries[0].cleared);botw::publish_map_landmarks(f.h,v);assert(f.values["botw.map.poi.0.border_tint"]==0xFF00BFFF&&f.values["botw.map.poi.0.core_tint"]==0xFFFF8A19);++cases;}
 // Item 15 (owner 2026-10-20): same cache-through-Transition behavior as places (see
 // botw_map_places_test.cpp) -- landmarks/poi also keep their last-valid snapshot.
 {F f;State s;sample(&s,&f.h);assert(f.values["botw.map.poi.valid"]==1);auto kind0=f.values["botw.map.poi.0.kind"];f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);s.next={};sample(&s,&f.h);assert(f.values["botw.map.poi.valid"]==1&&f.values["botw.map.poi.0.kind"]==kind0);++cases;}
 {F f;State s;sample(&s,&f.h);f.h.read_memory=[](void*,uint64_t,void*,size_t)->EdenDsmodBool{throw 1;};s.next={};f.values.clear();f.texts.clear();sample(&s,&f.h);assert(!f.values["botw.map.poi.valid"]&&!f.values.count("botw.map.poi.0.kind"));++cases;}
 std::cout<<cases<<" native shrine/tower bounds/race/progress/context/publication cases passed\n";
}
