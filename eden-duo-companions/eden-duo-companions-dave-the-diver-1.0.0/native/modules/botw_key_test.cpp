// SPDX-License-Identifier: GPL-3.0-or-later
// Allocation-free output names and tile names must equal the former std::string forms.
#include "botw_key.h"
#include "botw_map.h"
#include <cassert>
#include <iostream>
#include <random>
#include <string>
static std::string tile_reference(const botw::MapSnapshot& s,unsigned tile,unsigned level) {
  if(!s.valid||tile>=120||level<1||level>3)return {};
  const auto mask=s.masks[tile],revealed=uint16_t(s.revealed&mask);
  std::string suffix;
  if(revealed!=mask)suffix="_"+std::to_string(revealed);
  if(revealed && (s.revealed&s.subfilters[tile])) {
    uint16_t sub{};for(unsigned i=0;i<6;++i)if(s.changes[i])sub|=uint16_t(1u<<i);
    sub&=s.submasks[tile];
    if(sub)suffix+="-"+std::to_string(sub);
  }
  const char col="ZABCDEFGHIJK"[tile%12],row=char('0'+tile/12);
  return "romfs:/UI/MapTex/MainField/MapTex"+std::to_string(level)+"_"+col+"-"+row+suffix+".sbmaptex";
}
int main() {
  unsigned cases{};
  for(uint64_t i:{0ull,1ull,9ull,10ull,99ull,419ull,899ull,4294967295ull,18446744073709551615ull}) {
    botw::Key key("botw.map.koroks.",i);const std::string base="botw.map.koroks."+std::to_string(i);
    assert(key.c_str()==base&&std::string(key(".kind"))==base+".kind"&&std::string(key(".x"))==base+".x");
    assert(std::string(key(".","halo"))==base+".halo"&&key.c_str()==base);
    assert(std::string(key(".",i,".y"))==base+"."+std::to_string(i)+".y");++cases;
  }
  {botw::Key key("botw.quest.selected");assert(std::string(key(".valid"))=="botw.quest.selected.valid"&&std::string(key.c_str())=="botw.quest.selected");++cases;}
  {const std::string longest(200,'k');botw::Key key(longest,123);const std::string out=key(".suffix");
   assert(out.size()==95&&out==longest.substr(0,95));++cases;} // Truncated, never overrun.
  std::mt19937 random(7);
  for(int round=0;round<4000;++round) {
    botw::MapSnapshot s;s.valid=random()%8!=0;s.revealed=uint16_t(random());
    for(auto& m:s.masks)m=round%3==0?0xFFFF:uint16_t(random());
    for(auto& m:s.submasks)m=uint16_t(random());
    for(auto& m:s.subfilters)m=random()%3?uint16_t(random()):0;
    for(auto& c:s.changes)c=random()%2;
    std::array<char,80> name{};
    for(unsigned tile:{0u,5u,11u,12u,119u,120u,unsigned(random()%121)})for(unsigned level:{0u,1u,2u,3u,4u}) {
      assert(botw::map_tile_name(s,tile,level,name)==tile_reference(s,tile,level));
      assert(botw::map_tile(s,tile,level)==tile_reference(s,tile,level));
    }
    ++cases;
  }
  std::cout<<cases<<" output-name and tile-name equivalence cases passed\n";
}
