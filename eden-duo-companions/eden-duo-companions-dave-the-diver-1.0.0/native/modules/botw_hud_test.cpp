// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct HudFixture:Fixture {
 static constexpr uint64_t Player=0x8000000,G=0x9000000,H=0x9001000,B=0x9002000,Arr=0x9003000,F=0x9004000,Flags=0x9005000,Bomb=0x9006000,Stasis=0x9007000;
 bool race_value{};unsigned flag_reads{};decltype(EdenDsmodHostApi::read_memory) original{};
 HudFixture() {
  original=h.read_memory;h.read_memory=[](void* p,uint64_t at,void* out,size_t n)->EdenDsmodBool {
   auto& f=*static_cast<HudFixture*>(p);const auto ok=f.original(p,at,out,n);
   if (ok && f.race_value && at==F && ++f.flag_reads==2)static_cast<uint8_t*>(out)[24]^=1;
   return ok;
  };
  put(M+0x2C9A668,Player);put(Player,M+0x2B4DD40);put<int32_t>(Player+0x240,3);
  put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put(H,B);
  put<int32_t>(B+0x18,1);put<int32_t>(B+0x1C,1);put(B+0x20,Arr);put(Arr,F);
  for (unsigned i=0;i<40;++i) put<uint8_t>(F+i,0);
  put(F,M+0x2BD1140);put<uint8_t>(F+8,1);put<int32_t>(F+20,999999);put<int32_t>(F+24,347549);put<uint32_t>(F+28,0x23149BF8);
  put<int32_t>(B+8,2);put<int32_t>(B+12,2);put(B+16,Flags);put(Flags,Bomb);put(Flags+8,Stasis);
  for (auto p:{Bomb,Stasis}) {put(p,M+0x2BD1078);put<uint32_t>(p+8,0x01000700);}
  put<uint32_t>(Bomb+12,0x604C3BAE);put<uint32_t>(Stasis+12,0x9CA3D1C1);
 }
};
int main() {
 using F=HudFixture;unsigned cases=0;
 auto currency=[&](auto change,bool valid,int value=-1) {F f;change(f);auto s=botw::currency(f.h);if(s.valid!=valid)std::cerr<<"Currency case "<<cases+1<<" expected valid="<<valid<<" got="<<s.valid<<"\n";assert(s.valid==valid);if(valid)assert(s.value==value);++cases;};
 currency([](F&){},true,347549);
 for(int n:{0,999999}) currency([&](F& f){f.put<int32_t>(F::F+24,n);},true,n);
 for(int n:{-1,1000000}) currency([&](F& f){f.put<int32_t>(F::F+24,n);},false);
 currency([](F& f){f.h.build_id[0]^=1;},false);
 currency([](F& f){f.h.main_size=0x2CC5FE8;},false);
 currency([](F& f){f.h.main_base=UINT64_MAX-8;},false);
 for(auto p:{F::M+0x2CC5FE8,F::G+0xBE0,F::H,F::B+0x20,F::Arr}) {
  currency([&](F& f){f.put<uint64_t>(p,0x10001);},false);
  currency([&](F& f){f.put<uint64_t>(p,UINT64_MAX-7);},false);
 }
 currency([](F& f){f.put<uint64_t>(F::G,0);},false);
 currency([](F& f){f.put<uint64_t>(F::F,0);},false);
 currency([](F& f){f.put<uint8_t>(F::F+8,0);},false);
 currency([](F& f){f.put<uint32_t>(F::F+28,0x23149BF9);},false);
 for(int n:{0,-1,65537})currency([&](F& f){f.put<int32_t>(F::B+0x18,n);},false);
 for(int n:{0,65537})currency([&](F& f){f.put<int32_t>(F::B+0x1C,n);},false);
 currency([](F& f){f.put<int32_t>(F::F+16,-1);},false);
 currency([](F& f){f.put<int32_t>(F::F+20,1000000);},false);
 currency([](F& f){f.mem.erase(F::F+27);},false);
 currency([](F& f){f.race_value=true;},false);
 for(auto p:{F::F,F::G+0xBE0,F::M+0x2CC5FE8,F::U+0x64C30})currency([&](F& f){f.race=p;},false);
 for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000001ULL})currency([&](F& f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
 currency([](F& f){f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);},true,347549);
 currency([](F& f){
  f.put<int32_t>(F::B+0x18,3);f.put<int32_t>(F::B+0x1C,3);f.put(F::Arr+8,F::F+0x100);f.put(F::Arr+16,F::F+0x200);
  for(unsigned n:{1u,2u})for(unsigned i=0;i<40;++i)f.put<uint8_t>(F::F+n*0x100+i,0);
  for(unsigned n:{1u,2u}){f.put(F::F+n*0x100,F::M+0x2BD1140);f.put<uint8_t>(F::F+n*0x100+8,1);}
  f.put<uint32_t>(F::F+0x100+28,0x80000000);f.put<uint32_t>(F::F+0x200+28,0xFFFFFFFF);
 },true,347549);
 auto rune=[&](auto change,bool valid,int id=-1,bool upgrade=false) {F f;change(f);auto s=botw::current_rune(f.h);if(s.valid!=valid)std::cerr<<"Rune case "<<cases+1<<" expected valid="<<valid<<" got="<<s.valid<<"\n";assert(s.valid==valid);if(valid)assert(s.id==id&&s.upgraded==upgrade);++cases;};
 for(int id=0;id<8;++id)rune([&](F& f){f.put<int32_t>(F::Player+0x240,id);},true,id,id<=1||id==3);
 for(int id:{-1,8,INT32_MAX})rune([&](F& f){f.put<int32_t>(F::Player+0x240,id);},false);
 rune([](F& f){f.put<uint8_t>(F::Stasis+11,0);},true,3,false);
 rune([](F& f){f.h.build_id[0]^=1;},false);
 rune([](F& f){f.put<uint64_t>(F::Player,0);},false);
 rune([](F& f){f.put<uint64_t>(F::M+0x2C9A668,UINT64_MAX-7);},false);
 rune([](F& f){f.mem.erase(F::Player+0x243);},false);
 for(auto p:{F::M+0x2C9A668,F::Player+0x240,F::Stasis,F::U+0x64C30})rune([&](F& f){f.race=p;},false);
 rune([](F& f){f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);},false);
 rune([](F& f){f.put<int32_t>(F::Player+0x240,2);f.mem.erase(F::M+0x2CC5FE8);},true,2,false);
 auto clock=[&](auto change,bool valid,int minutes=-1) {
  F f;f.put<int32_t>(F::B+0x28,1);f.put<int32_t>(F::B+0x2C,1);f.put(F::B+0x30,F::Arr+0x100);f.put(F::Arr+0x100,F::F+0x300);
  for(unsigned i=0;i<40;++i)f.put<uint8_t>(F::F+0x300+i,0);
  f.put(F::F+0x300,F::M+0x2BD1208);f.put<uint8_t>(F::F+0x308,2);f.put<float>(F::F+0x314,360);f.put<float>(F::F+0x318,186.66638f);f.put<uint32_t>(F::F+0x31C,0x9C79A188);
  change(f);auto result=botw::world_clock(f.h);assert(result.valid==valid);if(valid)assert(result.minutes==minutes);++cases;
 };
 clock([](F&){},true,745);
 for(const auto& [value,minutes]:std::array<std::pair<float,int>,5>{{{0.f,0},{360.f,0},{359.999f,1435},{78.75f,315},{180.f,720}}})
  clock([&](F& f){f.put<float>(F::F+0x318,value);},true,minutes);
 for(float value:{-1.f,360.1f,NAN,INFINITY})clock([&](F& f){f.put<float>(F::F+0x318,value);},false);
 clock([](F& f){f.h.build_id[0]^=1;},false);
 clock([](F& f){f.h.main_size=1;},false);
 clock([](F& f){f.h.main_base=UINT64_MAX-8;},false);
 clock([](F& f){f.put<uint64_t>(F::F+0x300,0);},false);
 clock([](F& f){f.put<uint8_t>(F::F+0x308,1);},false);
 clock([](F& f){f.put<uint32_t>(F::F+0x31C,0);},false);
 clock([](F& f){f.put<float>(F::F+0x310,-1.f);},false);
 clock([](F& f){f.put<float>(F::F+0x314,361.f);},false);
 for(int count:{-1,0,65537})clock([&](F& f){f.put<int32_t>(F::B+0x28,count);},false);
 for(int cap:{0,65537})clock([&](F& f){f.put<int32_t>(F::B+0x2C,cap);},false);
 for(auto at:{F::M+0x2CC5FE8,F::G+0xBE0,F::H,F::B+0x30,F::Arr+0x100})
  for(uint64_t ptr:std::array<uint64_t,2>{0x10001,UINT64_MAX-7})clock([&](F& f){f.put<uint64_t>(at,ptr);},false);
 for(auto at:{F::F+0x300,F::G+0xBE0,F::U+0x64C30})clock([&](F& f){f.race=at;},false);
 clock([](F& f){f.mem.erase(F::F+0x327);},false);
 for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000001ULL})clock([&](F& f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
 auto stamina=[&](auto change,bool valid,int percent=-1,bool depleted=false) {
  F f;constexpr uint64_t info=0xA000000,actor=0xA010000;
  f.put(F::M+0x2CA1140,info);f.put(info,F::M+0x2B7BE80);f.put(info+0x60,actor);
  f.put(actor,F::M+0x2B7BEA8);f.put<float>(info+0xA4,2745.f);f.put<float>(info+0xA8,3000.f);f.put<float>(actor+0x186C,3000.f);
  change(f);const auto result=botw::stamina(f.h);assert(result.valid==valid);if(valid)assert(result.percent==percent&&result.depleted==depleted);++cases;
 };
 stamina([](F&){},true,91,true);
 for(const auto& [value,percent]:std::array<std::pair<float,int>,3>{{{0.f,0},{1500.f,50},{3000.f,100}}})
  stamina([&](F& f){f.put<float>(0xA0000A4,value);},true,percent,percent!=100);
 stamina([](F& f){f.put<float>(0xA0000A4,1000.f);f.put<float>(0xA0000A8,1000.f);f.put<float>(0xA01186C,1000.f);},true,100,false);
 for(float value:{-1.f,3000.1f,NAN,INFINITY})stamina([&](F& f){f.put<float>(0xA0000A4,value);},false);
 for(float value:{0.f,999.f,3001.f,NAN,INFINITY})stamina([&](F& f){f.put<float>(0xA0000A8,value);},false);
 stamina([](F& f){f.put<float>(0xA01186C,2999.f);},false);
 stamina([](F& f){f.put<float>(0xA01186C,NAN);},false);
 stamina([](F& f){f.h.build_id[0]^=1;},false);
 for(uint64_t at:{F::M+0x2CA1140,0xA000060UL})
  for(uint64_t pointer:std::array<uint64_t,3>{0,0x10001,UINT64_MAX-7})stamina([&](F& f){f.put(at,pointer);},false);
 for(uint64_t at:{0xA000000,0xA010000})stamina([&](F& f){f.put<uint64_t>(at,0);},false);
 for(uint64_t at:{0xA0000A4,0xA0000A8,0xA01186C})stamina([&](F& f){f.mem.erase(at);},false);
 for(uint64_t at:{F::M+0x2CA1140,0xA000060UL,0xA0000A4UL,0xA0000A8UL,0xA01186CUL,F::U+0x64C30})stamina([&](F& f){f.race=at;},false);
 for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000004ULL,0x1E09000001ULL})stamina([&](F& f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
 for(bool exception:{false,true}) {
  F f;State s;s.clock={true,745};s.stamina={true,true,91};s.context=botw::UiContext::World;
  s.next=std::chrono::steady_clock::now()+std::chrono::seconds(10);sample(&s,&f.h);
  assert(f.texts["botw.ready.clock.text"]=="12:25 PM"&&f.values["botw.ready.stamina.depleted"]==1);  // 745 min = 12:25 PM, 12-hour (critic finding 10)
  if(exception)f.h.get_text=[](void*,const char*)->const char*{throw 7;};
  else {s.next={};f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);}
  sample(&s,&f.h);
  assert(!f.values["botw.ready.clock.valid"]&&f.values["botw.ready.clock.minutes"]==-1&&f.texts["botw.ready.clock.text"].empty());
  assert(!f.values["botw.ready.stamina.valid"]&&!f.values["botw.ready.stamina.depleted"]&&f.values["botw.ready.stamina.percent"]==-1);++cases;
 }
 {F f;State s;sample(&s,&f.h);assert(f.values["botw.ready.rupees"]==347549&&f.values["botw.ready.rune.id"]==3);f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);s.next={};sample(&s,&f.h);assert(f.values["botw.ready.rupees"]==-1&&!f.values["botw.ready.rupees.valid"]&&f.texts["botw.ready.rupees.text"].empty());assert(!f.values["botw.ready.rune.valid"]&&f.texts["botw.ready.rune"].empty());++cases;}
 auto noise=[&](auto change,bool valid,int percent=-1) {
  F f;f.put<float>(F::U+0x70,.5f);change(f);const auto result=botw::noise(f.h);
  assert(result.valid==valid);if(valid)assert(result.percent==percent);++cases;
 };
 for(const auto& [level,percent]:std::array<std::pair<float,int>,4>{{{0.f,0},{.5f,25},{1.f,50},{2.f,100}}})
  noise([&](F& f){f.put<float>(F::U+0x70,level);},true,percent);
 for(float level:{-1.f,2.01f,NAN,INFINITY})noise([&](F& f){f.put<float>(F::U+0x70,level);},false);
 noise([](F& f){f.h.build_id[0]^=1;},false);
 noise([](F& f){f.h.main_size=1;},false);
 noise([](F& f){f.h.main_base=UINT64_MAX-8;},false);
 noise([](F& f){f.mem.erase(F::U+0x70);},false);
 for(auto at:{F::M+0x2CBDB80,F::U+0x70,F::U+0x64C30})noise([&](F& f){f.race=at;},false);
 for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000001ULL})
  noise([&](F& f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
 noise([](F& f){f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);},true,25);
 for(bool exception:{false,true}) {
  F f;f.put<float>(F::U+0x70,1.f);State s;sample(&s,&f.h);
  assert(f.values["botw.ready.noise.valid"]&&f.values["botw.ready.noise.percent"]==50);
  if(exception)f.h.get_text=[](void*,const char*)->const char*{throw 7;};
  else {s.next={};f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);}
  sample(&s,&f.h);assert(!f.values["botw.ready.noise.valid"]&&f.values["botw.ready.noise.percent"]==-1);++cases;
 }
 std::cout<<cases<<" currency/current-Rune/clock/stamina/noise/context/bounds/race/publication cases passed\n";
}
