// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_effects.h"
#include <cassert>
#include <cstdio>
#include <iostream>
int main(){unsigned cases{};
 auto blank=[]{std::array<uint32_t,12> l{};std::array<float,36> t{};for(unsigned i=0;i<12;++i)t[i*3+2]=-1;return std::pair{l,t};};
 {auto [l,t]=blank();auto e=botw::active_effects_from(l,t);assert(e.valid && e.count==0);++cases;}
 // Captured Hasty: three speed slots level1 / 3596 frames -> one Speed row, 2:00.
 {auto [l,t]=blank();for(unsigned i=0;i<3;++i){l[i]=1;t[i*3]=3596;t[i*3+1]=3597;}auto e=botw::active_effects_from(l,t);
  assert(e.valid && e.count==1 && e.rows[0].kind==botw::EffectKind::Speed && e.rows[0].seconds==120);char s[16];botw::format_effect_time(e.rows[0].seconds,s);assert(std::string(s)=="2:00");++cases;}
 // Captured Attack level2 / 7496 and Chilled level1 / 1796 together.
 {auto [l,t]=blank();l[5]=2;t[15]=7496;t[16]=7497;l[7]=1;t[21]=1796;t[22]=1797;auto e=botw::active_effects_from(l,t);
  assert(e.count==2 && e.rows[0].kind==botw::EffectKind::Attack && e.rows[0].level==2 && e.rows[0].seconds==250 && e.rows[1].kind==botw::EffectKind::HeatResist && e.rows[1].seconds==60);++cases;}
 // Iteration 14 item 5: every slot -> its native HUD effect (live-probed); 3/4/10 have no native row.
 {using K=botw::EffectKind;const std::pair<unsigned,K> want[]={{0,K::Speed},{1,K::Speed},{2,K::Speed},{5,K::Attack},{6,K::Defense},{7,K::HeatResist},{8,K::ColdResist},{9,K::FlameGuard},{11,K::ShockResist}};
  for(auto [slot,kind]:want){auto [l,t]=blank();l[slot]=1;t[slot*3]=300;auto e=botw::active_effects_from(l,t);assert(e.valid && e.count==1 && e.rows[0].kind==kind);++cases;}
  for(unsigned slot:{3u,4u,10u}){auto [l,t]=blank();l[slot]=1;t[slot*3]=300;auto e=botw::active_effects_from(l,t);assert(e.valid && e.count==0);++cases;}}
 {auto [l,t]=blank();l[5]=2;t[15]=0;auto e=botw::active_effects_from(l,t);assert(e.valid && e.count==0);++cases;}
 {auto [l,t]=blank();t[5]=0;assert(!botw::active_effects_from(l,t).valid);++cases;}
 {auto [l,t]=blank();l[3]=99;assert(!botw::active_effects_from(l,t).valid);++cases;}
 {auto [l,t]=blank();t[0]=NAN;assert(!botw::active_effects_from(l,t).valid);++cases;}
 {auto [l,t]=blank();t[0]=-5;assert(!botw::active_effects_from(l,t).valid);++cases;}
 // All twelve active: Speed merged once + Attack/Defense/Heat/Cold/Flame/Shock = 7 rows, no "Other".
 {auto [l,t]=blank();for(unsigned i=0;i<12;++i){l[i]=1;t[i*3]=30;}auto e=botw::active_effects_from(l,t);assert(e.count==7);
  for(unsigned r=0;r<e.count;++r)assert(e.rows[r].kind!=botw::EffectKind::Other);
  ++cases;}
 {char s[16];botw::format_effect_time(100000,s);assert(std::string(s)=="99:59");botw::format_effect_time(-3,s);assert(std::string(s)=="0:00");++cases;}
 std::cout<<cases<<" active effect layout/sentinel/bounds/merge/format cases passed\n";
}
