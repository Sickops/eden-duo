// SPDX-License-Identifier: GPL-3.0-or-later
// Ready circle marks: true position inside the circle, pinned on the rim (radius 398) outside it,
// native z orientation, finite inputs, the turn, and the native-derived animation steps.
#include "botw_ready_pins.h"
#include <cassert>
#include <iostream>
#include <map>
#include <set>
int main(){unsigned n=0;
using botw::ready_mark;
// Inside: true position (900 world units across 800 px), never on the rim.
{auto m=ready_mark(0,0,0,0);assert(m.visible&&!m.rim&&m.x==620&&m.y==464);++n;}
{auto m=ready_mark(0,0,225,0);assert(m.visible&&!m.rim&&m.x==820&&m.y==464);++n;}
{auto m=ready_mark(100,-50,100,-200);assert(m.visible&&!m.rim&&m.x==620&&std::abs(m.y-242)<=1);++n;} // north = up
{auto m=ready_mark(0,0,447,0);assert(m.visible&&!m.rim&&m.x==1017);++n;}
// Outside: centred ON the rim line at the true bearing.
for(const auto& v:std::array<std::array<float,2>,8>{{{1000,0},{-1000,0},{0,1000},{0,-1000},{1000,1000},{-1000,1000},{1000,-1000},{-1000,-1000}}}){
  auto r=ready_mark(0,0,v[0],v[1]);assert(r.visible&&r.rim);assert(std::abs(std::hypot(r.x-620.,r.y-464.)-398.)<1.);++n;}
assert(ready_mark(0,0,1000,0).x>620);assert(ready_mark(0,0,0,1000).y>464);assert(ready_mark(0,0,0,-1000).y<464);n+=3;
assert(!ready_mark(0,0,NAN,0).visible);assert(!ready_mark(0,0,INFINITY,0).visible);n+=2;
// The turn: clockwise degrees about the centre, as the map widget turns its view.
{auto a=ready_mark(0,0,1000,0,0),b=ready_mark(0,0,1000,0,90),c=ready_mark(0,0,0,-1000,90),d=ready_mark(0,0,225,0,180);
 assert(a.x>620&&a.y==464);assert(b.y>464&&b.x==620);assert(c.x>620&&c.y==464);assert(!d.rim&&d.x==420&&d.y==464);n+=4;}
for(double r:{0.,37.5,190.,359.5}){auto q=ready_mark(10,20,700,-300,r);assert(q.visible&&q.rim&&std::abs(std::hypot(q.x-620.,q.y-464.)-398.)<1.);assert(std::abs(q.clamped_distance-398.)<1e-6);++n;}
// Finding 3 (quest beam escapes the circle): clamped_distance never exceeds the rim, and is the
// true (sub-rim) distance when not pinned.
{auto in_=ready_mark(0,0,100,0);assert(!in_.rim&&std::abs(in_.clamped_distance-100.*botw::ReadyPxPerUnit)<1e-6);
 auto out_=ready_mark(0,0,1000,0);assert(out_.rim&&out_.clamped_distance==botw::ReadyRim);n+=2;}
// Animation: bounded, stepped at 15 Hz, every flame frame used, the ring grows then fades.
{std::set<int> flames;std::map<int,int> steps;botw::ReadyAnim last{};unsigned changes=0;
 for(int ms=0;ms<4000;++ms){auto a=botw::ready_anim(ms/1000.);
  assert(a.flame>=0&&a.flame<16&&a.flash>=0&&a.flash<8&&a.ring_alpha>=0&&a.ring_alpha<8&&a.quest_tint>=0&&a.quest_tint<4);
  assert(a.ring_scale>=312&&a.ring_scale<=1000);flames.insert(a.flame);if(!(a==last))++changes;last=a;}
 assert(flames.size()==16);assert(changes<=4*15+1);n+=3;
 auto f0=botw::ready_anim(0.),f1=botw::ready_anim(10./60.),f2=botw::ready_anim(25./60.),f3=botw::ready_anim(35./60.);
 assert(f0.ring_scale<f1.ring_scale&&f1.ring_scale<f2.ring_scale);assert(f1.ring_alpha==7&&f3.ring_alpha==0);n+=2;
 assert(botw::ready_anim(NAN).flame==0&&botw::ready_anim(-5).flame==0);++n;}
// Publication: hidden marks hold the animation still; a visible one animates.
{struct P{std::map<std::string,int64_t> v;} p;EdenDsmodHostApi h{};h.userdata=&p;
 h.publish_i64=[](void* u,const char* k,int64_t x){static_cast<P*>(u)->v[k]=x;};
 botw::MapPins pins{};botw::QuestMapMarkers q{};botw::MapPosition pos{true,0,0};
 const auto t0=std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(12345);
 botw::publish_ready_marks(h,pins,q,pos,true,0.,t0);
 assert(p.v["botw.ready.pin.0.visible"]==0&&p.v["botw.ready.quest.mark.0.visible"]==0&&p.v["botw.ready.mark.flame"]==0&&p.v["botw.ready.mark.ring.scale"]==1000);
 pins.valid=true;pins.active[2]=true;pins.positions[2]={2000,0,0};q.valid=true;q.count=1;q.positions[0]={100,0,-100};
 botw::publish_ready_marks(h,pins,q,pos,true,0.,t0);
 assert(p.v["botw.ready.pin.2.visible"]==1&&p.v["botw.ready.pin.2.rim"]==1&&p.v["botw.ready.pin.2.x"]==1018&&p.v["botw.ready.pin.0.visible"]==0);
 assert(p.v["botw.ready.quest.mark.0.visible"]==1&&p.v["botw.ready.quest.mark.0.rim"]==0&&p.v["botw.ready.quest.mark.1.visible"]==0);
 // Finding 3: a quest mark well inside the circle gets the full (uncapped) ring scale; one
 // pinned to the rim (budget 0) gets its ring fully collapsed, not a fixed size.
 assert(p.v["botw.ready.quest.mark.0.ring_scale"]==p.v["botw.ready.mark.ring.scale"]);
 // Iteration 12 item 6: the cap follows the 172 px ring (half-width 86): a mark 340 px from the
 // centre has 58 px of budget -> scale 674 (58/86), below the animation's 1000 peak.
 {botw::QuestMapMarkers near{};near.valid=true;near.count=1;near.positions[0]={float(340./botw::ReadyPxPerUnit),0,0};
  botw::publish_ready_marks(h,pins,near,pos,true,0.,t0);
  const auto full=p.v["botw.ready.mark.ring.scale"];
  assert(full>674&&p.v["botw.ready.quest.mark.0.ring_scale"]==674);}
 q.positions[0]={4000,0,0};
 botw::publish_ready_marks(h,pins,q,pos,true,0.,t0);
 assert(p.v["botw.ready.quest.mark.0.rim"]==1&&p.v["botw.ready.quest.mark.0.ring_scale"]==0);
 n+=2;
 assert(p.v["botw.ready.mark.flame"]==botw::ready_anim(12.345).flame);
 botw::publish_ready_marks(h,pins,q,pos,false,0.,t0);assert(p.v["botw.ready.pin.2.visible"]==0&&p.v["botw.ready.quest.mark.0.visible"]==0&&p.v["botw.ready.mark.flame"]==0);
 n+=4;}
std::cout<<n<<" ready mark projection, rim, turn, animation and publication cases passed\n";}
