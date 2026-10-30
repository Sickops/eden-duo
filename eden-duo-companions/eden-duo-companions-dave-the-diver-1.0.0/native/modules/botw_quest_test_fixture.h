// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "botw_test_fixture.h"
struct QuestFixture:Fixture {
  static constexpr uint64_t G=0x8000000,H=0x8010000,B=0x8020000,Bools=0x8030000,Dlc3=0x8040000,Dlc2=0x8050000,Ints=0x8060000,Destination=0x8070000;
  static constexpr uint64_t Q=0x9000000,Array=0x9010000,Entry=0x9020000,Type=0x9030000,Steps=0x9040000,Step=0x9050000,Message=0x9060000;
  void string(uint64_t p,std::string_view text) {for(unsigned i=0;i<text.size();++i)put<char>(p+i,text[i]);put<char>(p+text.size(),0);}
  QuestFixture() {
    put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put(H,B);
    put<int32_t>(B+8,2);put<int32_t>(B+12,2);put(B+16,Bools);put(Bools,Dlc3);put(Bools+8,Dlc2);
    for(auto p:{Dlc3,Dlc2}){put(p,M+0x2BD1078);put<uint32_t>(p+8,0);}
    put<uint32_t>(Dlc3+12,0x010EEEBA);put<uint32_t>(Dlc2+12,0x7609DE2C);
    put<int32_t>(B+0x18,1);put<int32_t>(B+0x1C,1);put(B+0x20,Ints);put(Ints,Destination);
    for(unsigned i=0;i<40;++i)put<uint8_t>(Destination+i,0);
    put(Destination,M+0x2BD1140);put<uint8_t>(Destination+8,1);put<int32_t>(Destination+16,-1);put<int32_t>(Destination+20,65535);put<int32_t>(Destination+24,0);put<uint32_t>(Destination+28,0x5ED42D75);
    put(M+0x2CD1700,Q);put(Q,M+0x2BEBA70);put<int32_t>(Q+0xB8,1);put<int32_t>(Q+0xBC,1);put(Q+0xC0,Array);put(Array,Entry);
    for(unsigned i=0;i<0x158;++i)put<uint8_t>(Entry+i,0);
    put(Entry,M+0x2BEBA90);put<int32_t>(Entry+12,1);put<uint8_t>(Entry+0x30,1);string(Entry+0x54,"GanonQuest");put(Entry+0xC0,Type);string(Type,"Main");
    put<int32_t>(Entry+0xA0,2);put<int32_t>(Entry+0xA4,2);put(Entry+0xA8,Steps);put<int32_t>(Entry+0x140,1);put(Steps,Step);put(Step,M+0x2BEBAF0);put(Step+0x48,Message);string(Message,"Desc");
  }
};
