// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
int main(){
  unsigned cases{};
  // Catalog sanity: every kind used is one of the owner-approved labelled
  // kinds (0,1,2,3,4,5,6,16) or a beast entry; no duplicate native ids.
  {std::set<std::string_view> ids;for(const auto& e:botw::map_label_catalog){
     assert(e.beast || (e.kind==0||e.kind==1||e.kind==2||e.kind==3||e.kind==4||e.kind==5||e.kind==6||e.kind==16));
     assert(!ids.count(e.id)||e.beast); // beast ids legitimately repeat (2 records/beast)
     ids.insert(e.id);}
   assert(botw::map_label_catalog.size()==193);++cases;}
  // Pure matching: a visible MapPlaces entry at the exact catalog xyz with the
  // right kind shows; wrong kind, not-visible, or off-position does not.
  auto find_place=[&](int kind)->size_t{for(size_t i=0;i<botw::map_label_catalog.size();++i)if(!botw::map_label_catalog[i].beast&&botw::map_label_catalog[i].kind==kind)return i;return SIZE_MAX;};
  {const size_t i=find_place(0);assert(i!=SIZE_MAX);const auto& e=botw::map_label_catalog[i];
   botw::MapPlaces p;p.valid=true;p.count=1;p.entries[0]={true,false,0,{e.x,e.y,e.z}};
   auto labels=botw::map_labels(p,{},{});assert(labels.valid&&labels.shown[i]);++cases;}
  {const size_t i=find_place(0);const auto& e=botw::map_label_catalog[i];
   botw::MapPlaces p;p.valid=true;p.count=1;p.entries[0]={false,false,0,{e.x,e.y,e.z}}; // not visible (undiscovered)
   auto labels=botw::map_labels(p,{},{});assert(labels.valid&&!labels.shown[i]);++cases;}
  {const size_t i=find_place(1);const auto& e=botw::map_label_catalog[i];
   botw::MapPlaces p;p.valid=true;p.count=1;p.entries[0]={true,false,5,{e.x,e.y,e.z}}; // wrong kind
   auto labels=botw::map_labels(p,{},{});assert(labels.valid&&!labels.shown[i]);++cases;}
  {const size_t i=find_place(6);const auto& e=botw::map_label_catalog[i];
   botw::MapPlaces p;p.valid=true;p.count=1;p.entries[0]={true,false,6,{e.x+5.f,e.y,e.z}}; // off-position
   auto labels=botw::map_labels(p,{},{});assert(labels.valid&&!labels.shown[i]);++cases;}
  // Landmarks (shrine kind 2 / tower kind 3) match through the same mechanism.
  {size_t i=SIZE_MAX;for(size_t k=0;k<botw::map_label_catalog.size();++k)if(botw::map_label_catalog[k].kind==3){i=k;break;}
   assert(i!=SIZE_MAX);const auto& e=botw::map_label_catalog[i];
   botw::MapLandmarks lm;lm.valid=true;lm.count=1;lm.entries[0]={true,true,false,3,{e.x,e.y,e.z}};
   auto labels=botw::map_labels({},lm,{});assert(labels.valid&&labels.shown[i]);++cases;}
  // Beasts: matched by position only against MapMoving, independent of kind
  // number (the live kind for a moving slot is not known statically).
  {size_t i=SIZE_MAX;for(size_t k=0;k<botw::map_label_catalog.size();++k)if(botw::map_label_catalog[k].beast){i=k;break;}
   assert(i!=SIZE_MAX);const auto& e=botw::map_label_catalog[i];
   botw::MapMoving mv;mv.valid=true;mv.entries[0]={true,8,{e.x,e.y,e.z}};
   auto labels=botw::map_labels({},{},mv);assert(labels.valid&&labels.shown[i]);++cases;}
  {size_t i=SIZE_MAX;for(size_t k=0;k<botw::map_label_catalog.size();++k)if(botw::map_label_catalog[k].beast){i=k;break;}
   botw::MapMoving mv;mv.valid=true;mv.entries[0]={false,8,{botw::map_label_catalog[i].x,botw::map_label_catalog[i].y,botw::map_label_catalog[i].z}}; // not visible
   auto labels=botw::map_labels({},{},mv);assert(labels.valid&&!labels.shown[i]);++cases;}
  // All-invalid inputs -> invalid output (no spurious labels).
  {auto labels=botw::map_labels({},{},{});assert(!labels.valid);++cases;}
  // Publish: booleans round-trip; invalid publishes nothing shown and no crash.
  {Fixture f;botw::MapLabels labels;labels.valid=true;labels.shown[0]=true;botw::publish_map_labels(f.h,labels);
   // Perf round 4: a hidden label is no longer published at all (missing
   // key reads as hidden the same as an explicit 0, same contract round 3 gave koroks/places/
   // landmarks) -- check absence, not an explicit 0.
   assert(f.values["botw.map.labels.valid"]==1&&f.values["botw.map.labels.0.show"]==1&&!f.values.count("botw.map.labels.1.show"));++cases;
   f.values.clear();botw::publish_map_labels(f.h,{});assert(!f.values["botw.map.labels.valid"]&&!f.values.count("botw.map.labels.0.show"));++cases;}
  // Integration: sample()/State wiring publishes botw.map.labels alongside
  // the other map outputs and clears it when the context closes (reuses the
  // existing MovingFixture-style raw-memory setup via map_places()'s own
  // test fixture would duplicate a lot of setup; here we only check the
  // field exists on State and the publish call fires without the full page
  // being open, exercising the context-closed clear path).
  {Fixture f;State s;sample(&s,&f.h);assert(f.values.count("botw.map.labels.valid"));++cases;}
  // --- Ready caption: arrival-radius selection (owner bug 2026-10-23), tapped override, spoiler gate ---
  auto find_place_idx=[&](int kind)->size_t{for(size_t i=0;i<botw::map_label_catalog.size();++i)if(!botw::map_label_catalog[i].beast&&botw::map_label_catalog[i].kind==kind)return i;return SIZE_MAX;};
  auto at=[](const botw::MapLabelEntry& e,float dz){return std::array<float,3>{e.x,e.y,e.z-dz};};
  // A village 100 m away (inside its 120 m arrival radius) -> named, whatever the heading.
  {const size_t i=find_place_idx(0);const auto& e=botw::map_label_catalog[i];
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=true;labels.xyz[i]=at(e,100.f);
   botw::MapUi ui;
   for(float heading:{0.f,90.f,180.f}){botw::MapPosition pos{true,e.x,-e.z,true,heading};
     auto c=botw::ready_caption(labels,pos,ui);assert(c.valid&&c.name_key==botw::actor_key(e.id));++cases;}
   // No heading needed either.
   assert(botw::ready_caption(labels,{true,e.x,-e.z,false,0.f},ui).valid);++cases;}
  // Outside the arrival radius (village at 130 m, stable at 60 m) -> no marker caption: the area name stands.
  {const size_t i=find_place_idx(0);const size_t j=find_place_idx(1);
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=true;labels.shown[j]=true;
   const auto& e=botw::map_label_catalog[i];labels.xyz[i]=at(e,130.f);labels.xyz[j]=at(e,60.f);
   botw::MapPosition pos{true,e.x,-e.z,true,0.f};botw::MapUi ui;
   assert(!botw::ready_caption(labels,pos,ui).valid);++cases;
   // The old cone rule would have named the village 400 m dead ahead; the new rule does not.
   labels.xyz[i]=at(e,400.f);labels.shown[j]=false;assert(!botw::ready_caption(labels,pos,ui).valid);++cases;}
  // Owner case: a shrine 300 m away across an empty field -> not named; at 20 m -> named.
  if(const size_t k=find_place_idx(3);k!=SIZE_MAX){const auto& e=botw::map_label_catalog[k];
   botw::MapLabels labels;labels.valid=true;labels.shown[k]=true;labels.xyz[k]=at(e,300.f);
   botw::MapPosition pos{true,e.x,-e.z,true,0.f};botw::MapUi ui;
   assert(!botw::ready_caption(labels,pos,ui).valid);++cases;
   labels.xyz[k]=at(e,20.f);auto c=botw::ready_caption(labels,pos,ui);assert(c.valid&&c.name_key==botw::actor_key(e.id));++cases;}
  // Nearest of two places inside their radii wins.
  {const size_t i=find_place_idx(0);const size_t j=find_place_idx(1);const auto& e=botw::map_label_catalog[i];
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=true;labels.shown[j]=true;
   labels.xyz[i]=at(e,100.f);labels.xyz[j]=at(e,30.f);
   botw::MapPosition pos{true,e.x,-e.z,true,0.f};botw::MapUi ui;
   auto c=botw::ready_caption(labels,pos,ui);assert(c.valid&&c.name_key==botw::actor_key(botw::map_label_catalog[j].id));++cases;}
  // Spoiler gate: not `shown` (undiscovered) -> never a caption even when standing on it.
  {const size_t i=find_place_idx(0);const auto& e=botw::map_label_catalog[i];
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=false;labels.xyz[i]=at(e,5.f);
   botw::MapPosition pos{true,e.x,-e.z,true,0.f};botw::MapUi ui;
   assert(!botw::ready_caption(labels,pos,ui).valid);++cases;}
  // No position -> no automatic caption.
  {const size_t i=find_place_idx(0);const auto& e=botw::map_label_catalog[i];
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=true;labels.xyz[i]=at(e,5.f);
   botw::MapUi ui;assert(!botw::ready_caption(labels,{},ui).valid);++cases;}
  // Tapped landmark override: wins over (and needs no) automatic pick.
  {botw::MapUi ui;ui.observed.valid=true;ui.observed.count=1;
   ui.observed.entries[0]={true,false,false,2,{-4446.018f,342.854f,-3802.419f}}; // Dungeon088 xyz (catalog)
   ui.selected=0;
   auto c=botw::ready_caption({},{},ui);assert(c.valid&&c.name_key==int64_t(botw::MapUi::name_key(ui.observed.entries[0])));++cases;
   ui.observed.entries[0].visible=false; // not discovered -> tapped override does not fire either
   assert(!botw::ready_caption({},{},ui).valid);++cases;}
  // Travel Medallion: named only within its 15 m arrival radius; not placed -> never; a nearer place wins.
  {botw::MapSpecial sp;sp.valid=true;sp.entries[3]={true,42,{-2692.56f,363.f,-918.41f}};
   botw::MapPosition pos{true,-2692.56f,918.41f-10.f,true,0.f};botw::MapUi ui;
   auto c=botw::ready_caption({},pos,ui,&sp);assert(c.valid&&c.name_key==botw::actor_key("Obj_WarpDLC"));++cases;
   pos.y=918.41f-100.f;assert(!botw::ready_caption({},pos,ui,&sp).valid);++cases;
   pos.y=918.41f-10.f;sp.entries[3].visible=false;assert(!botw::ready_caption({},pos,ui,&sp).valid);++cases;
   sp.entries[3].visible=true;assert(!botw::ready_caption({},pos,ui,nullptr).valid);++cases;
   const size_t i=find_place_idx(0);const auto& e=botw::map_label_catalog[i];
   botw::MapLabels labels;labels.valid=true;labels.shown[i]=true;labels.xyz[i]=at(e,50.f);
   botw::MapPosition near{true,e.x,-e.z,true,0.f};sp.entries[3].xyz={e.x,e.y,e.z-12.f};
   auto d=botw::ready_caption(labels,near,ui,&sp);assert(d.valid&&d.name_key==botw::actor_key("Obj_WarpDLC"));++cases;
   sp.entries[3].xyz={e.x,e.y,e.z-60.f};d=botw::ready_caption(labels,near,ui,&sp);assert(d.valid&&d.name_key==botw::actor_key(e.id));++cases;}
  {Fixture f;botw::publish_ready_caption(f.h,{true,12345});assert(f.values["botw.ready.marker.valid"]==1&&f.values["botw.ready.marker.name_key"]==12345);++cases;
   botw::publish_ready_caption(f.h,{});assert(!f.values["botw.ready.marker.valid"]&&f.values["botw.ready.marker.name_key"]==-1);++cases;}
  std::cout<<cases<<" map-icon name-label catalog/match/spoiler-gate/publish/wiring/ready-caption cases passed\n";
}
