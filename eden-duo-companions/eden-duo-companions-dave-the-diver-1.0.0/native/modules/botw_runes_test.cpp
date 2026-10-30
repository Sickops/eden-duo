// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_runes.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct RuneFixture:Fixture {
 static constexpr uint64_t C=0x7005000,B=0x9000000,G=0xA000000,H=0xA001000,F=0xA002000,P=0xA003000,X=0xA004000,Y=0xA005000;
 RuneFixture() {
  put<uint64_t>(A+45*8,C);put(C,M+0x2BA1D40);put(C+0x150,M+0x2BA1C10);
  put<int32_t>(C+0x3720,4);put<int32_t>(C+0x3724,4);
  put<uint32_t>(U+0x64C24,3);put<uint32_t>(U+0x64C58,0x10000);put<uint64_t>(U+0x64C30,0x1E09000004ULL);
  put<int32_t>(C+0x3850,8);put(C+0x3858,B);put<int32_t>(C+0x3868,7);put<int32_t>(C+0x3860,3);put<int32_t>(C+0x3864,1);
  put(B,std::array<int32_t,8>{0,1,2,3,4,5,7,-1});
  put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put(H,F);
  put<int32_t>(F+8,2);put<int32_t>(F+12,2);put(F+16,P);put(P,X);put(P+8,Y);
  for (auto p:{X,Y}) {put(p,M+0x2BD1078);put<uint32_t>(p+8,0x01000F00);}
  put<uint32_t>(X+12,0x604C3BAE);put<uint32_t>(Y+12,0x9CA3D1C1);
 }
};
int main() {
 using R=RuneFixture;
 { R f;auto s=botw::runes(f.h);assert(s.valid && s.count==7 && s.selected==3 && s.highlighted==1 && s.bombs_plus && s.stasis_plus && s.ids[6]==7); }
 { R f;f.put<uint8_t>(R::X+11,0);f.put<uint8_t>(R::Y+11,0);auto s=botw::runes(f.h);assert(s.valid && !s.bombs_plus && !s.stasis_plus); }
 { R f;f.put<int32_t>(R::C+0x3868,8);f.put<int32_t>(R::B+28,6);assert(botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3868,0);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3868,9);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3850,9);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3864,7);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3864,-1);assert(!botw::runes(f.h).valid); }
 { R f;f.race=R::C+0x3864;assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3864,5);auto s=botw::runes(f.h);assert(s.valid && s.selected==3 && s.highlighted==5); }
 { R f;f.put<int32_t>(R::C+0x3860,7);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::B+4,0);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::B+4,8);assert(!botw::runes(f.h).valid); }
 { R f;f.mem.erase(R::B+31);assert(!botw::runes(f.h).valid); }
 { R f;f.put<uint32_t>(R::U+0x64C58,0);assert(!botw::runes(f.h).valid); }
 { R f;f.h.build_id[0]^=1;assert(!botw::runes(f.h).valid); }
 { R f;f.put(R::G,uint64_t(0));assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::F+8,65537);assert(!botw::runes(f.h).valid); }
 { R f;f.put(R::X,uint64_t(0));assert(!botw::runes(f.h).valid); }
 { R f;f.put<uint32_t>(R::Y+12,0x9CA3D1C2);assert(!botw::runes(f.h).valid); }
 { R f;f.race=R::X;assert(!botw::runes(f.h).valid); }
 { R f;f.race=R::B;assert(!botw::runes(f.h).valid); }
 { R f;f.race=R::G+0xBE0;assert(!botw::runes(f.h).valid); }
 { R f;f.put<uint64_t>(R::C+0x3858,UINT64_MAX-7);assert(!botw::runes(f.h).valid); }
 { R f;f.h.main_size=0x2CC5FE8;assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::F+12,1);assert(!botw::runes(f.h).valid); }
 { R f;f.put<uint64_t>(R::F+16,UINT64_MAX-7);assert(!botw::runes(f.h).valid); }
 { R f;f.put<int32_t>(R::C+0x3868,2);f.put<int32_t>(R::C+0x3860,1);f.put(R::B,std::array<int32_t,8>{2,4,-1,-1,-1,-1,-1,-1});auto s=botw::runes(f.h);assert(s.valid && s.count==2 && s.ids[0]==2 && s.ids[1]==4 && s.selected==1); }
 assert(std::string(botw::rune_actor(0,true,false))=="Obj_RemoteBombBall2");
 assert(std::string(botw::rune_actor(1,false,false))=="Obj_RemoteBombCube");
 assert(std::string(botw::rune_actor(3,false,true))=="Obj_StopTimerLv2");
 assert(std::string(botw::rune_actor(6,false,false))=="Obj_Motorcycle");
 assert(std::string(botw::rune_actor(7,false,false))=="Obj_AmiiboItem");
 std::cout<<"28 Rune availability/upgrade/bounds/race/build cases and 5 actor mapping assertions passed\n";
}
