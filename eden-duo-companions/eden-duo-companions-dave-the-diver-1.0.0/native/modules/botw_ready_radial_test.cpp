// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_ready_radial.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <tuple>
using H=botw::ReadyRadialHit;
int64_t tap(int32_t x,int32_t y){return static_cast<int64_t>((uint64_t(uint32_t(x))<<32)|uint32_t(y));}
int main(){
  int cases=0;
  for(int card:{0,1,2,3,4})for(int32_t x:{-1,0,400,799,800,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()})
    for(int32_t y:{-1,0,400,799,800}) {
      const auto h=botw::ready_radial_hit(card,tap(x,y));++cases;
      if(card!=2&&card!=3)assert(h.kind==H::Blocked);
      if(x<0||y<0||x>=800||y>=800)assert(h.kind==H::Blocked);
    }
  for(int card:{2,3})for(int x=0;x<800;++x)for(int y=0;y<800;++y){
    auto h=botw::ready_radial_hit(card,tap(x,y));++cases;
    const int dx=x-400,dy=y-400,r2=dx*dx+dy*dy;
    if(r2>160000)assert(h.kind==H::Blocked);
    else if(r2<=10816)assert(h.kind==H::Close);
    else if(h.kind!=H::Blocked){
      if(card==3){assert(h.kind==H::Food);assert(h.row==(dy<0?(dx<0?0:1):(dx<0?2:3)));}
      else{assert(h.kind==H::Clothing);assert(h.row>=1&&h.row<=3);}
    }
  }
  for(auto [x,y,row]:{std::tuple{200,200,0},{600,200,1},{200,600,2},{600,600,3}}){
    auto h=botw::ready_radial_hit(3,tap(x,y));assert(h.kind==H::Food&&h.row==row);++cases;
  }
  for(auto [x,y,row]:{std::tuple{200,300,1},{600,300,2},{400,650,3}}){
    auto h=botw::ready_radial_hit(2,tap(x,y));assert(h.kind==H::Clothing&&h.row==row);++cases;
  }
  // Shared seams never choose a neighbouring destructive action.
  for(int v=0;v<800;++v){
    if(std::abs(v-400)>104){assert(botw::ready_radial_hit(3,tap(400,v)).kind==H::Blocked);++cases;}
  }
  std::cout<<cases<<" radial input cases passed\n";
}
