// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_drawn.h"
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
int main(){using F=PlayerEquipmentFixture;unsigned cases=0;
for(uint8_t w:{0,1,2,255})for(uint8_t b:{0,1,2,255}){F f;f.put(F::Player+0xBA8,w);f.put(F::Player+0xBD8,b);auto s=botw::drawn_state(f.h);assert(s.valid==(w<=1&&b<=1));if(s.valid){assert(s.weapon==(w==0));assert(s.bow==(b==0));}++cases;}
for(uint64_t at:{F::Player+0xBA8,F::Player+0xBD8,F::Player,F::Info+0x60,F::U+0x64C30}){F f;f.put<uint8_t>(F::Player+0xBA8,1);f.put<uint8_t>(F::Player+0xBD8,1);f.race=at;assert(!botw::drawn_state(f.h).valid);++cases;}
{F f;f.put<uint8_t>(F::Player+0xBA8,0);f.put<uint8_t>(F::Player+0xBD8,1);f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);auto s=botw::drawn_state(f.h);assert(s.valid&&!s.weapon&&!s.bow);++cases;}
std::cout<<cases<<" native draw/sheath identity, bounds, empty-slot and race cases passed\n";}
