// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "botw_world_runes.h"
#include "botw_test_fixture.h"
#include <algorithm>
#include <vector>
struct WorldRuneFixture:Fixture {
 static constexpr uint64_t C=0x7005000,R=0x9000000,D=0x9010000,G=0xA000000,H=0xA0010000,B=0xA0020000,AR=0xA0030000,F=0xA0040000;
 std::map<uint32_t,uint64_t> flags;
 WorldRuneFixture() {
  put(A+45*8,C);put(C,M+0x2BA1D40);put(C+0x150,M+0x2BA1B50);put<int32_t>(C+0x3720,-1);put<int32_t>(C+0x3724,-1);put<int32_t>(C+0x36E8,3);put<uint32_t>(U+0x64C24,0);put<uint32_t>(U+0x64C58,0);
  put(M+0x2C9A668,R);put(R,M+0x2B4DD40);put<int32_t>(R+0x240,3);put(M+0x2CC4A40,D);put(D,M+0x2BCD5C0);put<uint32_t>(D+0xD0,0);
  put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put<uint8_t>(G+0xBE8,0);put(H,B);
  std::vector<uint32_t> hashes(botw::RuneUnlockHashes.begin(),botw::RuneUnlockHashes.end());hashes.insert(hashes.end(),{0x511872C2,0x604C3BAE,0x9CA3D1C1});std::sort(hashes.begin(),hashes.end());hashes.erase(std::unique(hashes.begin(),hashes.end()),hashes.end());
  put<int32_t>(B+8,hashes.size());put<int32_t>(B+12,hashes.size());put(B+16,AR);
  for(unsigned i=0;i<hashes.size();++i){auto p=F+i*32;flags[hashes[i]]=p;put(AR+i*8,p);put(p,M+0x2BD1078);put<uint32_t>(p+8,0x01000F00);put<uint32_t>(p+12,hashes[i]);}
 }
 void flag(uint32_t hash,bool value){put<uint8_t>(flags.at(hash)+11,value);}
};
