// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_presentation.h"
#include <stdexcept>
#include <iostream>
struct Bundle {belmont::Snapshot base;int content{};};
void Check(bool x){if(!x)throw std::runtime_error("presentation retention contract");}
int main(){
 using Clock=std::chrono::steady_clock; auto now=Clock::now();
 belmont_presentation::Retainer<Bundle> r;Bundle b;
 b.base.supported=b.base.coherent=b.base.manager_ready=b.base.entity_ready=true;
 b.base.manager=100;b.base.entity=200;b.base.application_loading=b.base.room_loading=b.base.loading_identity=0;
 b.base.initial_identity=1;b.base.paused=0;b.content=42;
 Check(!r.Select(b,true,now).dim);
 auto cinematic=b;cinematic.base.initial_identity=0;cinematic.content=0;
 auto held=r.Select(cinematic,false,now+std::chrono::seconds(10));Check(held.dim&&held.value.content==42);
 auto race=cinematic;race.base.coherent=false;race.base.reason="incoherent";Check(r.Select(race,false,now+std::chrono::seconds(10)+std::chrono::milliseconds(100)).dim);
 Check(!r.Select(b,true,now+std::chrono::seconds(11)).dim);
 auto bad=b;bad.base.coherent=false;bad.base.reason="incoherent";
 auto held_race=r.Select(bad,false,now+std::chrono::seconds(11)+std::chrono::milliseconds(100));
 Check(!held_race.dim&&held_race.blocked&&held_race.value.content==42);
 // Repeated short reader races must never alternate bright/dim appearance.
 for(unsigned frame=1;frame<=60;++frame){
  auto t=now+std::chrono::seconds(11)+std::chrono::milliseconds(frame*16);
  if(frame%2){auto live=r.Select(b,true,t);Check(!live.dim&&!live.blocked);}
  else {auto torn=r.Select(bad,false,t);Check(!torn.dim&&torn.blocked&&torn.value.content==42);}
 }
 r.Select(b,true,now+std::chrono::seconds(11));
 Check(!r.Select(bad,false,now+std::chrono::seconds(11)+std::chrono::milliseconds(181)).dim);
 Check(!r.Select(cinematic,false,now+std::chrono::seconds(12)).dim);
 r.Select(b,true,now);auto loading=cinematic;loading.base.room_loading=1;
 Check(!r.Select(loading,false,now).dim);Check(!r.Select(cinematic,false,now).dim);
 r.Select(b,true,now);auto other=cinematic;other.base.entity++;Check(!r.Select(other,false,now).dim);
 r.Select(b,true,now);auto title=cinematic;title.base.entity_ready=false;Check(!r.Select(title,false,now).dim);
 r.Select(b,true,now);auto unsupported=cinematic;unsupported.base.supported=false;Check(!r.Select(unsupported,false,now).dim);
 std::vector<belmont::ExplorationRoom> before(1);before[0].room_id="R";before[0].width=before[0].height=2;before[0].entered_zones_ready=true;before[0].entered_zone_ids={1};
 auto after=before;after[0].entered_zone_ids.push_back(2);Check(belmont_presentation::ExplorationGrows(before,after));
 Check(!belmont_presentation::ExplorationGrows(after,before));after[0].width=3;Check(!belmont_presentation::ExplorationGrows(before,after));
 std::cout<<"presentation retention tests passed\n";
}
