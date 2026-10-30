// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_tracker_flags.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct FlagFixture:Fixture {
 static constexpr uint64_t G=0x9000000,H=0x9001000,B=0x9002000,BA=0x9003000,IA=0x9004000;
 static constexpr uint64_t BF=0x9005000,IF=0x9006000;
 FlagFixture() {
  put(M+0x2CC5FE8,G);put(G,M+0x2BD07E0);put(G+0xBE0,H);put(H,B);
  put<uint8_t>(G+0xBE8,1);put<uint32_t>(G+0xC18,0x8000);put<uint32_t>(G+0xC28,1);
  put<int32_t>(B+8,3);put<int32_t>(B+12,3);put(B+16,BA);
  put<int32_t>(B+24,3);put<int32_t>(B+28,3);put(B+32,IA);
  for(unsigned i=0;i<3;++i) {
   const uint64_t b=BF+i*0x40,n=IF+i*0x40;put(BA+i*8,b);put(IA+i*8,n);
   put(b,M+0x2BD1078);put<uint8_t>(b+8,0);put<uint8_t>(b+9,1);put<uint8_t>(b+10,0);put<uint8_t>(b+11,i==1?0:15);put<uint32_t>(b+12,10+i*10);
   put(n,M+0x2BD1140);put<uint8_t>(n+8,1);put<uint8_t>(n+9,1);put<uint16_t>(n+10,0);
   put<int32_t>(n+12,0);put<int32_t>(n+16,-1);put<int32_t>(n+20,100);put<int32_t>(n+24,int(i)-1);put<uint32_t>(n+28,10+i*10);put<uint64_t>(n+32,0);
  }
 }
};
int main() {
 using F=FlagFixture;using botw::TrackerFlagReader;
 int cases=0;auto check=[&](bool ok){++cases;if(!ok)std::cerr<<"failure "<<cases<<"\n";assert(ok);};
 {F f;TrackerFlagReader r;check(!r.boolean(10).has_value());check(!r.integer(10).has_value());check(!r.finish());check(r.begin(f.h));
  check(r.boolean(10)==true);check(r.boolean(20)==false);check(r.boolean(30)==true);check(r.integer(10)==-1);check(r.integer(20)==0);check(r.integer(30)==1);
  check(!r.boolean(0));check(!r.boolean(11));check(!r.boolean(UINT32_MAX));check(!r.integer(11));check(r.finish());check(r.begin(f.h));check(r.boolean(20)==false);check(r.finish());r.clear();check(!r.finish());}
 // A cached identity must revalidate both its array pointer and its hash.
 {F f;TrackerFlagReader r;check(r.begin(f.h));check(r.boolean(10)==true);check(r.finish());
  f.put<uint8_t>(F::BF+11,0);check(r.begin(f.h));check(r.boolean(10)==false);check(r.finish());
  f.put(F::BA,F::BF+0x80);check(r.begin(f.h));check(!r.boolean(10));check(r.finish());}
 {F f;TrackerFlagReader r;check(r.begin(f.h));check(r.boolean(10)==true);check(r.finish());
  f.put<uint32_t>(F::BF+12,9);check(r.begin(f.h));check(!r.boolean(10));check(r.boolean(9)==true);check(r.finish());}
 // Permission mode1 requires bit0. Mode0 bypasses it, matching native TriggerParamRef.
 {F f;TrackerFlagReader r;f.put<uint8_t>(F::BF+9,0);f.put<uint8_t>(F::IF+9,0);
  check(r.begin(f.h));check(!r.boolean(10));check(!r.integer(10));check(r.finish());
  check(r.begin(f.h));check(!r.boolean(10));check(r.observe_boolean(10)==true);check(r.finish());
  f.put<uint8_t>(F::BF+11,0);check(r.begin(f.h));check(r.observe_boolean(10)==false);check(r.finish());
  f.put<uint8_t>(F::BF+11,1);check(r.begin(f.h));check(r.observe_boolean(10)==true);f.put<uint8_t>(F::BF+11,0);check(!r.finish());
  f.put<uint8_t>(F::BF+11,1);
  f.put<uint8_t>(F::G+0xBE8,0);check(r.begin(f.h));check(r.boolean(10)==true);check(r.integer(10)==-1);check(r.finish());}
 // Reads are provisional until finish verifies every observed identity and value.
 for(int kind=0;kind<10;++kind) {
  F f;TrackerFlagReader r;check(r.begin(f.h));check(r.boolean(10)==true);check(r.integer(10)==-1);
  switch(kind) {
   case 0:f.put<uint8_t>(F::BF+11,0);break;
   case 1:f.put<int32_t>(F::IF+24,2);break;
   case 2:f.put(F::BA,F::BF+0x40);break;
   case 3:f.put(F::IA,F::IF+0x40);break;
   case 4:f.put<uint8_t>(F::G+0xBE8,0);break;
   case 5:f.put<uint32_t>(F::G+0xC28,2);break;
   case 6:f.put<uint32_t>(F::G+0xC18,0);break;
   case 7:f.put(F::H,F::B+8);break;
   case 8:f.put<uint64_t>(F::U+0x64C30,0);break;
   case 9:f.put<uint32_t>(F::BF+12,11);break;
  }
  check(!r.finish());check(!r.boolean(10));
 }
 // Struct identity, mapping and value constraints produce unknown, never false.
 for(int kind=0;kind<9;++kind) {
  F f;TrackerFlagReader r;
  switch(kind) {
   case 0:f.put<uint64_t>(F::BF,0);break;
   case 1:f.put<uint8_t>(F::BF+8,1);break;
   case 2:f.mem.erase(F::BF+15);break;
   case 3:f.put<uint64_t>(F::BA,UINT64_MAX-7);break;
   case 4:f.put<int32_t>(F::IF+24,101);break;
   case 5:f.put<int32_t>(F::IF+16,102);break;
   case 6:f.put<uint8_t>(F::IF+8,10);break;
   case 7:f.put<uint64_t>(F::IF,0);break;
   case 8:f.mem.erase(F::IF+39);break;
  }
  check(r.begin(f.h));check(kind<4?!r.boolean(10).has_value():!r.integer(10).has_value());check(r.finish());
 }
 for(int kind=0;kind<15;++kind) {
  F f;TrackerFlagReader r;
  switch(kind) {
   case 0:f.h.build_id[0]^=1;break;
   case 1:f.h.title_id=0;break;
   case 2:f.h.main_size=0x2CC5FE8;break;
   case 3:f.h.main_base=UINT64_MAX-0x20;break;
   case 4:f.put<uint64_t>(F::G,0);break;
   case 5:f.put<uint64_t>(F::G+0xBE0,F::H+1);break;
   case 6:f.put<int32_t>(F::B+8,0);break;
   case 7:f.put<int32_t>(F::B+12,2);break;
   case 8:f.put<int32_t>(F::B+28,65537);break;
   case 9:f.put<uint64_t>(F::B+32,UINT64_MAX-7);break;
   case 10:f.put<uint8_t>(F::G+0xBE8,2);break;
   case 11:f.put<uint32_t>(F::G+0xC28,256);break;
   case 12:f.put<uint64_t>(F::U+0x64C30,0);break;
   case 13:f.put<uint64_t>(F::A+8,0x123456);break;
   case 14:f.mem.erase(F::G+0xC2B);break;
  }
  check(!r.begin(f.h));check(!r.boolean(10));check(!r.observe_boolean(10));check(!r.integer(10));check(!r.finish());
 }
 {F f;TrackerFlagReader r;check(r.begin(f.h));for(int i=0;i<4096;++i)check(r.boolean(10)==true);check(!r.boolean(10));check(r.finish());}
 std::cout<<cases<<" tracker flag reader assertions passed\n";
}
