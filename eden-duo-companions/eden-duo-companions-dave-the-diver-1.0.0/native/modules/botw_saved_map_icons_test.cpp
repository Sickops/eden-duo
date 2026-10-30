// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct SavedFixture:Fixture {
 static constexpr uint64_t G=0xA000000,H=0xA001000,B=0xA002000,Groups=0xA003000,NoGroup=0xA004000,PosGroup=0xA005000,NoEntries=0xA006000,PosEntries=0xA007000,No=0xA010000,Pos=0xA020000;
 SavedFixture() {
  put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put(H,B);put<uint32_t>(G+0xC18,0);put<uint32_t>(G+0xC28,0);
  put<uint32_t>(M+0x2B7E7D8,0);put<uint32_t>(M+0x2B7E7DC,0);
  for(unsigned i=0;i<2;++i){auto off=i?0x108:0xA8;put<int32_t>(B+off,1);put<int32_t>(B+off+4,1);put(B+off+8,Groups+i*8);put(Groups+i*8,i?PosGroup:NoGroup);auto group=i?PosGroup:NoGroup;put<int32_t>(group,200);put<int32_t>(group+4,200);put(group+8,i?PosEntries:NoEntries);}
  for(unsigned i=0;i<200;++i){auto n=No+i*40,p=Pos+i*72;put(NoEntries+i*8,n);put(PosEntries+i*8,p);put(n,M+0x2BD1140);put<uint8_t>(n+8,10);put<int32_t>(n+0x18,i<9?27+i:(i==9?41:-1));put<uint32_t>(n+0x1C,0x9383490E);put(p,M+0x2BD15F0);put<uint8_t>(p+8,16);put(p+0x30,std::array<float,3>{float(i)*10,300,-float(i)*10});put<uint32_t>(p+0x3C,0xEA9DEF3F);}
  for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL}){std::array<uint8_t,56>b{};uint64_t vt=M+0x2A701C8,p=M+off+20;int32_t cap=32;std::memcpy(b.data(),&vt,8);std::memcpy(b.data()+8,&p,8);std::memcpy(b.data()+16,&cap,4);std::memcpy(b.data()+20,"MainField",10);put(M+off,b);}
 }
};
int main(){using F=SavedFixture;unsigned cases{};
 auto check=[&](auto change,bool valid){F f;change(f);auto v=botw::saved_map_icons(f.h);if(v.valid!=valid)std::cerr<<"case "<<cases+1<<'\n';assert(v.valid==valid);++cases;};
 check([](F&){},true);
 check([](F&f){f.h.build_id[0]^=1;},false);
 check([](F&f){f.h.main_size=1;},false);
 check([](F&f){f.h.main_base=UINT64_MAX-10;},false);
 for(auto at:{F::M+0x2CC5FE8,F::G+0xBE0,F::H,F::B+0xB0,F::Groups,F::NoGroup+8,F::NoEntries,F::PosEntries})for(uint64_t p:{0ULL,0x10001ULL,UINT64_MAX-7ULL})check([&](F&f){f.put(at,p);},false);
 for(auto at:{F::NoGroup,F::NoGroup+4,F::PosGroup,F::PosGroup+4})for(int n:{-1,0,199,201})check([&](F&f){f.put<int32_t>(at,n);},false);
 for(int n:{-2,45,100})check([&](F&f){f.put<int32_t>(F::No+0x18,n);},false);
 check([](F&f){f.put<uint32_t>(F::No+0x1C,0);},false);
 check([](F&f){f.put<uint32_t>(F::Pos+0x3C,0);},false);
 check([](F&f){f.put<uint8_t>(F::No+8,1);},false);
 check([](F&f){f.put<uint8_t>(F::Pos+8,7);},false);
 check([](F&f){f.put<uint64_t>(F::No,0);},false);
 check([](F&f){f.put<uint64_t>(F::Pos,0);},false);
 check([](F&f){f.put(F::NoEntries+8,F::No);},false);
 check([](F&f){f.put(F::PosEntries+8,F::Pos);},false);
 for(float n:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),6001.f,-6001.f})check([&](F&f){f.put(F::Pos+0x30,n);},false);
 check([](F&f){f.put<float>(F::Pos+0x34,10001);},false);
 check([](F&f){f.put<float>(F::Pos+0x38,5001);},false);
 check([](F&f){f.put<int32_t>(F::No+0x18,-1);f.put<float>(F::Pos+0x30,std::numeric_limits<float>::quiet_NaN());},true);
 check([](F&f){f.put<uint32_t>(F::G+0xC18,0x8000);f.put<uint32_t>(F::G+0xC28,4);f.put<uint32_t>(F::M+0x2B7E7D8,0x04000000);f.put<uint32_t>(F::M+0x2B7E7DC,0x04000000);},true);
 check([](F&f){f.put<uint32_t>(F::G+0xC18,0x8000);f.put<uint32_t>(F::G+0xC28,4);},false);
 check([](F&f){f.put<uint32_t>(F::M+0x2B7E7D8,UINT32_MAX);},false);
 check([](F&f){f.put<uint32_t>(F::M+0x2B7E7DC,1);},false);
 for(uint64_t flags:{0x1849000000ULL,0x0E09E00000ULL,0ULL})check([&](F&f){f.put(F::U+0x64C30,flags);},false);
 for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL})check([&](F&f){f.put<char>(F::M+off+20,'X');},false);
 for(auto at:{F::M+0x2CC5FE8,F::G,F::G+0xBE0,F::H,F::G+0xC18,F::G+0xC28,F::M+0x2B7E7D8,F::B+0xA8,F::Groups,F::NoGroup,F::NoGroup+8,F::NoEntries,F::No,F::No+0x18,F::No+0x1C,F::PosEntries,F::Pos,F::Pos+0x30,F::U+0x64C30,F::M+0x2C9E868})check([&](F&f){f.race=at;},false);
 {F f;auto v=botw::saved_map_icons(f.h);botw::publish_map_stamps(f.h,v);assert(f.values["botw.map.stamps.valid"]==1&&f.values["botw.map.stamps.count"]==9);for(unsigned i=0;i<9;++i){auto k="botw.map.stamps."+std::to_string(i);assert(f.values[k+".kind"]==i);assert(f.texts[k+".icon"]=="composite:native-map-stamp-"+std::to_string(i));}
  // Perf round 4: the tail (inactive) slot is no longer published at all
  // (dead sentinel removed -- no consumer ever read it, see botw_saved_map_icons.h). Check
  // absence, not an explicit -1.
  assert(!f.values.count("botw.map.stamps.9.kind"));++cases;
  f.values.clear();f.texts.clear();
  botw::publish_map_stamps(f.h,{});assert(!f.values["botw.map.stamps.valid"]&&!f.values["botw.map.stamps.count"]&&!f.texts.count("botw.map.stamps.0.icon")&&!f.values.count("botw.map.stamps.0.kind"));++cases;
  for(auto& i:v.icons){i.kind=27;}
  botw::publish_map_stamps(f.h,v);assert(!f.values["botw.map.stamps.valid"]&&!f.values["botw.map.stamps.count"]);++cases;
 }
 {F f;State s;sample(&s,&f.h);assert(f.values["botw.map.stamps.valid"]==1);f.values.clear();f.texts.clear();f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);s.next={};sample(&s,&f.h);assert(!f.values["botw.map.stamps.valid"]&&!f.texts.count("botw.map.stamps.0.icon"));++cases;}
 {F f;State s;sample(&s,&f.h);f.values.clear();f.texts.clear();f.h.read_memory=[](void*,uint64_t,void*,size_t)->EdenDsmodBool{throw 1;};s.next={};sample(&s,&f.h);assert(!f.values["botw.map.stamps.valid"]&&!f.texts.count("botw.map.stamps.0.icon"));++cases;}
 std::cout<<cases<<" saved native map icon bounds/race/context/publication cases passed\n";
}
