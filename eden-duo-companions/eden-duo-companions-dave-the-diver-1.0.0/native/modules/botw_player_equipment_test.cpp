// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_player_equipment.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct PlayerEquipmentFixture:Fixture {
  static constexpr uint64_t Info=0x8000000,Player=0x8010000,Creator=0x8020000,
                            Actors=0x9000000,Links=0xA000000;
  PlayerEquipmentFixture() {
    put(M+0x2CA1140,Info);put(Info,M+0x2B7BE80);put(Info+0x60,Player);
    put(Player,M+0x2B7BEA8);put(M+0x2C9A458,Creator);put(Creator,M+0x2B4D9C8);
    put(Player+0xB90,M+0x2BE40B0);put(Player+0x23E0,M+0x2BD75A8);
    for (unsigned i=0;i<6;++i) {
      uint64_t actor=Actors+i*0x1000,data=Links+i*0x1000,link=Player+(i<3?0xB98+i*24:0x23F0+(i-3)*16);
      put(link,data);put<uint32_t>(link+8,100+i);put<uint8_t>(link+12,0);
      put<uint32_t>(data+0x40,100+i);put(data+0x48,actor);
      put(actor,M+(i<3?0x2AE2348:0x2A6D608));put(actor+0x10,actor+0x1C);
      put<int32_t>(actor+0x18,64);put<uint32_t>(actor+0x60,100+i);
      put<uint8_t>(actor+0x64,1);put(actor+0x68,data);
      std::array<char,64> name{};std::string n="Equipment_"+std::to_string(i);std::memcpy(name.data(),n.data(),n.size());put(actor+0x1C,name);
    }
  }
};
int main() {
  using F=PlayerEquipmentFixture;unsigned cases{};
  auto check=[&](auto change,bool valid){F f;change(f);auto s=botw::player_equipment(f.h);if(s.valid!=valid)std::cerr<<"case "<<cases+1<<'\n';assert(s.valid==valid);++cases;};
  {F f;auto s=botw::player_equipment(f.h);assert(s.valid&&s.player==F::Player&&s.creator==F::Creator);for(unsigned i=0;i<6;++i)assert(s.slots[i].name=="Equipment_"+std::to_string(i)&&s.slots[i].id==100+i);++cases;}
  check([](F&f){f.h.build_id[0]^=1;},false);check([](F&f){f.h.title_id=0;},false);check([](F&f){f.h.main_size=1;},false);check([](F&f){f.h.main_base=UINT64_MAX-10;},false);
  for(uint64_t flags:{0ULL,0x1849000000ULL,0x0E09E00000ULL,0x1E09000004ULL})check([&](F&f){f.put(F::U+0x64C30,flags);},false);
  for(uint64_t at:{F::M+0x2CA1140,F::Info+0x60,F::M+0x2C9A458,F::Player+0xB98,F::Links+0x48})
    for(uint64_t value:{0ULL,0x10001ULL,UINT64_MAX-7ULL})check([&](F&f){f.put(at,value);},false);
  for(uint64_t at:{F::Info,F::Player,F::Creator,F::Player+0xB90,F::Player+0x23E0,F::Actors})check([&](F&f){f.put<uint64_t>(at,0);},false);
  check([](F&f){f.put<uint8_t>(F::Player+0xBA4,2);},false);
  check([](F&f){f.put<uint32_t>(F::Links+0x40,99);},false);
  check([](F&f){f.put<uint32_t>(F::Actors+0x60,99);},false);
  for(uint8_t state:{0,2,3,255})check([&](F&f){f.put(F::Actors+0x64,state);},false);
  check([](F&f){f.put<uint64_t>(F::Actors+0x68,0);},false);
  check([](F&f){f.put<uint64_t>(F::Actors+0x10,F::Actors+0x20);},false);
  check([](F&f){f.put<int32_t>(F::Actors+0x18,65);},false);
  check([](F&f){f.put<char>(F::Actors+0x1C,0);},false);
  check([](F&f){f.put<char>(F::Actors+0x1C,'/');},false);
  check([](F&f){std::array<char,64> name;name.fill('A');f.put(F::Actors+0x1C,name);},false);
  check([](F&f){f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);},true);
  check([](F&f){f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);f.put<uint64_t>(F::Player+0xB98,0);},true);
  for(uint64_t at:{F::M+0x2CA1140,F::Info,F::Info+0x60,F::Player,F::M+0x2C9A458,F::Creator,F::Player+0xB90,F::Player+0x23E0,F::Player+0xB98,F::Player+0xBA0,F::Links+0x40,F::Links+0x48,F::Actors,F::Actors+0x10,F::Actors+0x18,F::Actors+0x60,F::Actors+0x64,F::Actors+0x68,F::Actors+0x1C,F::U+0x64C30})check([&](F&f){f.race=at;},false);
  {F f;f.mem.erase(F::Actors+0x1C);assert(!botw::player_equipment(f.h).valid);++cases;}
  // Iteration 14 item 6: a broken held weapon/bow leaves the slot id with a released link record
  // (data_id UINT32_MAX, actor 0) -> an empty hand with the native id/link kept for the guest check.
  for(unsigned i=0;i<3;++i){F f;const uint64_t data=F::Links+i*0x1000;f.put<uint32_t>(data+0x40,UINT32_MAX);f.put<uint64_t>(data+0x48,0);
    auto s=botw::player_equipment(f.h);assert(s.valid&&s.slots[i].name.empty()&&s.slots[i].actor==0&&s.slots[i].id==100+i&&s.slots[i].link_data==data);
    for(unsigned j=0;j<6;++j){if(j!=i)assert(s.slots[j].name=="Equipment_"+std::to_string(j));}
    ++cases;}
  {F f;for(unsigned i:{0u,2u}){f.put<uint32_t>(F::Links+i*0x1000+0x40,UINT32_MAX);f.put<uint64_t>(F::Links+i*0x1000+0x48,0);}
    auto s=botw::player_equipment(f.h);assert(s.valid&&s.slots[0].name.empty()&&s.slots[2].name.empty()&&s.slots[1].name=="Equipment_1");++cases;}
  check([](F&f){f.put<uint32_t>(F::Links+0x40,UINT32_MAX);},false);           // released id but a live actor still linked
  check([](F&f){f.put<uint32_t>(F::Links+0x40,99);f.put<uint64_t>(F::Links+0x48,0);},false); // mismatched (not released) id
  std::cout<<cases<<" attached equipment identity/context/bounds/race cases passed\n";
}
