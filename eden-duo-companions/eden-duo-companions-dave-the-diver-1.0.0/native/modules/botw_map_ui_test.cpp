// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_map_ui.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
int main(){
 Fixture f;botw::MapUi ui;botw::MapPosition pos{true,100,200};botw::MapLandmarks map;map.valid=true;map.count=2;
 const auto& shrine=botw::tracker_shrines[0];map.entries[0]={true,true,false,2,{shrine.x,shrine.y,shrine.z}};
 const auto& tower=botw::tracker_towers[0];map.entries[1]={true,true,true,3,{tower.x,tower.y,tower.z}};
 botw::SavedMapIcons icons;icons.valid=true;icons.icons[0]={27,{10,20,30}};
 auto pub=[&](bool active=true,bool allowed=true,uint32_t epoch=1){ui.publish(f.h,active,allowed,epoch,pos,map,icons);};
 pub();assert(ui.entered&&ui.view_valid&&ui.cx==100);assert(std::abs(ui.span_x-1240.f*900/824)<.01);
 assert(f.values["botw.map.ui.hit.200.kind"]==2);assert(f.values["botw.map.ui.hit.201.kind"]==3);assert(f.values["botw.map.ui.hit.0.kind"]==27);
 assert(ui.action("map_marker",200,true));pub();assert(f.values["botw.map.ui.inspect"]==1);assert(f.values["botw.map.ui.inspect.name"]!=0);assert(f.texts["botw.map.ui.inspect.status"]=="Travel gate activated");
 assert(!ui.action("map_marker",0,true));assert(ui.action("map_ui_layer",0,true));pub();assert(f.values["botw.map.ui.hit.200.kind"]==-1);assert(f.values["botw.map.ui.poi.0.kind"]==-1);assert(f.values["botw.map.ui.hit.201.kind"]==3);
 assert(ui.action("map_ui_popup",1,true));pub();assert(f.values["botw.map.ui.popup.1"]==1);assert(!ui.action("map_tool",6,true));pub();assert(f.values["botw.map.ui.popup.1"]==0);
 assert(ui.action("map_ui_layer",4,true));assert(ui.korok_filter()==botw::KorokFilter::Found);assert(ui.action("map_ui_layer",5,true));assert(ui.korok_filter()==botw::KorokFilter::All);assert(ui.action("map_ui_layer",4,true));assert(ui.korok_filter()==botw::KorokFilter::Unfound);
 pos.x=500;pub();assert(ui.cx==100);pub(false);pub();assert(ui.cx==500);
 ui.action("map_marker",201,true);pub();assert(f.values["botw.map.ui.inspect"]==1);pub(true,false);assert(f.values["botw.map.ui.inspect"]==0);assert(f.values["botw.map.ui.hit.201.kind"]==-1);
 ui.action("map_ui_popup",3,true);pub(true,true,2);assert(ui.popup==0);assert(f.values["botw.map.ui.travel.available"]==0);assert(f.reads==0);
 // Iteration 12 item 13a: placed Travel Medallion = hit 351, inspect names the native item.
 {Fixture g;botw::MapUi m;botw::MapSpecial sp;sp.valid=true;sp.entries[3]={true,42,{-2692.56f,363.f,-918.41f}};
  auto mp=[&](bool allowed=true){m.publish(g.h,true,allowed,1,pos,map,icons,824.f,900.f,&sp);};
  mp();assert(g.values["botw.map.ui.hit.351.kind"]==42);
  assert(m.action("map_marker",351,true)&&m.medallion_selected);mp();assert(g.values["botw.map.ui.inspect"]==1);assert(g.values["botw.map.ui.inspect.name"]==4255962846);assert(g.texts["botw.map.ui.inspect.status"]=="");
  assert(m.action("map_marker",200,true)&&!m.medallion_selected&&m.selected==0);mp();assert(g.values["botw.map.ui.inspect.name"]!=4255962846);
  assert(m.action("map_marker",351,true)&&m.medallion_selected&&m.selected==-1);assert(m.action("map_marker",351,true)&&!m.medallion_selected);  // second tap deselects
  m.action("map_marker",351,true);sp.entries[3].visible=false;mp();assert(!m.medallion_selected&&g.values["botw.map.ui.hit.351.kind"]==-1&&g.values["botw.map.ui.inspect"]==0);
  assert(m.action("map_marker",351,true)&&!m.medallion_selected);  // not placed: tap ignored
  sp.entries[3].visible=true;mp(false);assert(g.values["botw.map.ui.hit.351.kind"]==-1);
  sp.entries[3].kind=19;mp();assert(g.values["botw.map.ui.hit.351.kind"]==-1);  // slot 3 must be the kind-42 record
  m.publish(g.h,true,true,1,pos,map,icons);assert(g.values["botw.map.ui.hit.351.kind"]==-1);assert(g.reads==0);}
 std::cout<<"Map UI guards, marker routing, layers, first-entry scale, epoch cancellation and zero guest reads passed\n";
}
