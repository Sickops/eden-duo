// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_recent_gear.h"
#include <cassert>
#include <iostream>
int main(){unsigned cases{};
 for(int type:{0,1}){
  botw::Snapshot p{true,{{"Duplicate",type,100,true,0x10000},{"Duplicate",type,200,false,0x20000},{"Third",type,300,false,0x30000}}};botw::RecentGear r;
  r.update(p,type,"Duplicate");assert(!r.target(p,type));++cases;
  p.items[0].equipped=false;p.items[1].equipped=true;r.update(p,type,"Duplicate");assert(r.target(p,type) && r.target(p,type)->address==0x10000);++cases;
  p.items[0].equipped=true;p.items[1].equipped=false;r.update(p,type,"Duplicate");assert(r.target(p,type) && r.target(p,type)->address==0x20000);++cases;
  for(int reason=0;reason<8;++reason){auto q=p;auto s=r;
   if(reason==0)q.items.erase(q.items.begin()+1);
   if(reason==1)q.items[1].actor="Reused";
   if(reason==2)q.items[1].value=0;
   if(reason==3)q.items[1].type=3;
   if(reason==4)q.items[1].equipped=true;
   if(reason==5)q.valid=false;
   if(reason==6)q.items.push_back(q.items[1]);
   if(reason==7)q.items[0].equipped=false;
   s.update(q,type,"Duplicate");if(reason==7)assert(s.target(q,type)->address==0x20000);else assert(!s.target(q,type));++cases;
  }
  // Ambiguous attachment keeps the history (the call site hides the teardrop meanwhile via
  // recent_actor_valid); it was wiped before 2026-10-23, which lost the secondary on a break.
  {auto s=r;const auto* before=r.target(p,type);assert(before);s.update(p,type,"WrongActor");assert(s.target(p,type) && s.target(p,type)->address==before->address);++cases;}
  {auto q=p;auto s=r;q.items[0].equipped=false;s.update(q,type,"");q.items[2].equipped=true;s.update(q,type,"Third");assert(s.target(q,type)->address==0x20000);++cases;}
 }
 for(int type:{0,1}){
  auto score=[](const botw::Item& i){return i.value;};
  botw::Snapshot p{true,{{"Main",type,100,true,0x10000},{"Weak",type,50,false,0x20000},{"Strong",type,900,false,0x30000},{"Other",3,999,false,0x40000}}};botw::RecentGear r;
  r.update(p,type,"Main");assert(!r.target(p,type));r.seed(p,type,score);assert(r.target(p,type) && r.target(p,type)->address==0x30000);++cases;
  {auto q=p;q.items[2].value=0;botw::RecentGear s;s.update(q,type,"Main");s.seed(q,type,score);assert(s.target(q,type)->address==0x20000);++cases;}
  {auto q=p;q.items.push_back(q.items[2]);botw::RecentGear s;s.update(q,type,"Main");s.seed(q,type,score);assert(s.target(q,type)->address==0x20000);++cases;}
  {auto q=p;q.items.resize(1);botw::RecentGear s;s.update(q,type,"Main");s.seed(q,type,score);assert(!s.target(q,type));++cases;}
  {botw::RecentGear s;s.seed(p,type,score);assert(!s.target(p,type));++cases;}
  // Real history wins over the seed and seeding never replaces it.
  {auto q=p;botw::RecentGear s;s.update(q,type,"Main");q.items[0].equipped=false;q.items[1].equipped=true;s.update(q,type,"Weak");s.seed(q,type,score);assert(s.target(q,type)->address==0x10000);++cases;}
 }
 {
  botw::Snapshot p{true,{{"Duplicate",0,100,true,0x10000},{"Duplicate",0,200,false,0x20000},{"Third",0,300,false,0x30000}}};botw::RecentGear r;
  r.update(p,0,"Duplicate");assert(r.pin(p,{0x20000,"Duplicate"},0));assert(r.target(p,0)->address==0x20000);++cases;
  assert(!r.pin(p,{0x10000,"Duplicate"},0));assert(!r.pin(p,{0x20000,"Wrong"},0));++cases;
  p.items[0].equipped=false;p.items[2].equipped=true;r.update(p,0,"Third");assert(r.target(p,0)->address==0x20000);++cases;
  p.items[2].equipped=false;p.items[1].equipped=true;r.update(p,0,"Duplicate");assert(r.target(p,0)->address==0x30000);++cases;
  p.items[1].equipped=false;p.items[2].equipped=true;r.update(p,0,"Third");assert(r.target(p,0)->address==0x20000);++cases;
  auto q=p;q.items.push_back(q.items[1]);assert(!r.pin(q,{0x20000,"Duplicate"},0));++cases;
 }
 for(int type:{0,1}){
  botw::Snapshot p{true,{{"Main",type,100,true,0x10000},{"Backup",type,100,false,0x20000},{"Third",type,100,false,0x30000}}};botw::RecentGear r;
  r.update(p,type,"Main");assert(r.pin(p,{0x20000,"Backup"},type));
  p.items[0].equipped=false;r.update(p,type,"");assert(r.target(p,type) && r.target(p,type)->address==0x20000);++cases;
  p.items.erase(p.items.begin());r.update(p,type,"");assert(r.target(p,type) && r.target(p,type)->address==0x20000);++cases;
  p.items[1].equipped=true;r.update(p,type,"Third");assert(r.target(p,type) && r.target(p,type)->address==0x20000);++cases;
  p.items[1].equipped=false;r.update(p,type,"");assert(r.target(p,type) && r.target(p,type)->address==0x20000);++cases;
  p.items.erase(p.items.begin());r.update(p,type,"");assert(!r.target(p,type));++cases;
 }
 // 2026-10-23 owner (Thor): "if a weapon breaks, the secondary is gone too". A break passes through
 // ticks where the broken instance is still flagged equipped at 0 durability while the hand already
 // reads empty (ambiguous), a failed pouch read, then removal + pouch reindex. None of these may
 // drop the explicit secondary (or the remembered previous one).
 for(int type:{0,1}){
  botw::Snapshot p{true,{{"Main",type,10,true,0x10000},{"Backup",type,100,false,0x20000},{"Third",type,100,false,0x30000}}};botw::RecentGear r;
  r.update(p,type,"Main");assert(r.pin(p,{0x20000,"Backup"},type));
  auto q=p;q.items[0].value=0;r.update(q,type,"");assert(r.target(q,type) && r.target(q,type)->address==0x20000);++cases;   // broken, still flagged, hand empty
  q.valid=false;r.update(q,type,"");q.valid=true;assert(r.target(q,type) && r.target(q,type)->address==0x20000);++cases;     // one failed pouch read
  q.items.erase(q.items.begin());std::swap(q.items[0],q.items[1]);r.update(q,type,"");                   // removed + reindexed
  assert(r.target(q,type) && r.target(q,type)->address==0x20000);++cases;
  q.items[1].equipped=true;r.update(q,type,"Backup");assert(!r.target(q,type) || r.target(q,type)->address==0x30000);++cases; // swap in: backup becomes primary
 }
 for(int type:{0,1}){ // remembered previous (no explicit pin) also survives the same break sequence
  botw::Snapshot p{true,{{"Old",type,100,true,0x20000},{"Main",type,10,false,0x10000}}};botw::RecentGear r;
  r.update(p,type,"Old");p.items[0].equipped=false;p.items[1].equipped=true;r.update(p,type,"Main");assert(r.target(p,type) && r.target(p,type)->address==0x20000);
  auto q=p;q.items[1].value=0;r.update(q,type,"");q.valid=false;r.update(q,type,"");q.valid=true;q.items.erase(q.items.begin()+1);r.update(q,type,"");
  assert(r.target(q,type) && r.target(q,type)->address==0x20000);++cases;
 }
 std::cout<<cases<<" recent gear instance/history/break/removal cases passed\n";
}
