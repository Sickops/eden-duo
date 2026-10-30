// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_world_runes.h"
#include "botw_world_runes_fixture.h"
#include <algorithm>
#include <cassert>
#include <iostream>
int main(){using F=WorldRuneFixture;unsigned cases{};
 {F f;const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==7 && r.list.selected==3 && r.list.ids[6]==7 && r.list.bombs_plus && r.list.stasis_plus);++cases;}
 {F f;f.put<uint32_t>(F::D+0xD0,0x300);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==8 && r.list.ids[6]==6);++cases;}
 {F f;f.flag(0x511872C2,false);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==6);++cases;}
 {F f;for(auto h:botw::RuneUnlockHashes)f.flag(h,false);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==0 && r.list.selected==-1);++cases;}
 for(unsigned i=0;i<8;++i){F f;for(auto h:botw::RuneUnlockHashes)f.flag(h,false);f.flag(botw::RuneUnlockHashes[i],true);f.put<uint32_t>(F::D+0xD0,0x300);f.put<int32_t>(F::R+0x240,i);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==(i<2?2:1) && r.list.ids[size_t(r.list.selected)]==int(i));++cases;}
 for(auto at:{F::M+0x2C9A668,F::M+0x2CC4A40,F::M+0x2CC5FE8,F::A+45*8,F::R,F::D,F::G,F::C}){F f;f.put<uint64_t>(at,0);assert(!botw::world_runes(f.h).valid);++cases;}
 for(auto at:{F::R+0x240,F::C+0x36E8}){F f;f.put<int32_t>(at,8);assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.put<uint8_t>(F::G+0xBE8,2);assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.put<uint32_t>(F::D+0xD0,0x10000);assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.put<uint32_t>(F::U+0x64C58,0x10000);assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.put<uint64_t>(F::U+0x64C30,0x1849000000ULL);assert(!botw::world_runes(f.h).valid);++cases;}
 for(auto at:{F::R+0x240,F::C+0x36E8,F::D+0xD0,F::M+0x2CC4A40,F::G+0xBE0,F::U+0x64C30}){F f;f.race=at;assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.race=f.flags[0x511872C2];assert(!botw::world_runes(f.h).valid);++cases;}
 for(auto at:{F::G+0xBE0,F::H,F::B+16}){F f;f.put<uint64_t>(at,0);assert(!botw::world_runes(f.h).valid);++cases;}
 for(auto [at,value]:{std::pair<uint64_t,int32_t>{F::B+8,0},{F::B+8,65537},{F::B+12,9}}){F f;f.put<int32_t>(at,value);assert(!botw::world_runes(f.h).valid);++cases;}
 {F f;f.put<uint8_t>(F::G+0xBE8,1);assert(botw::world_runes(f.h).list.count==7);++cases;}
 {F f;f.put<uint8_t>(F::G+0xBE8,1);f.put<uint8_t>(f.flags[0x511872C2]+9,0);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==6);++cases;}
 {F f;f.put<uint8_t>(F::G+0xBE8,1);f.put<uint8_t>(f.flags[0x191BCCF9]+9,0);const auto r=botw::world_runes(f.h);assert(r.valid && r.list.count==5 && r.list.ids[0]==2);++cases;}
 {F f;f.put<uint8_t>(F::G+0xBE8,1);f.put<uint8_t>(f.flags[0x604C3BAE]+9,0);const auto r=botw::world_runes(f.h);assert(r.valid && !r.list.bombs_plus);++cases;}
 std::cout<<cases<<" World Rune availability/DLC/option/identity/race cases passed\n";
}
