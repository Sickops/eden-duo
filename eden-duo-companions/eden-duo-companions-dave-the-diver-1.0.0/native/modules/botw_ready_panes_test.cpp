// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
int main(){unsigned cases{};
 {botw::ReadyPanes p;assert(p.left()==0 && p.right()==0);assert(p.step(false,-1)&&p.left()==2);assert(p.step(false,1)&&p.left()==0);assert(p.step(true,-1)&&p.right()==1);assert(!p.step(true,0));++cases;}
 {botw::ReadyPanes p;p.step(false,1);p.observe_drawn(true,true);assert(p.left()==2&&p.right()==1);p.step(false,1);p.step(true,1);p.observe_drawn(true,true);assert(p.left()==0&&p.right()==0);p.observe_drawn(true,false);assert(p.left()==1&&p.right()==0);p.observe_drawn(true,true);assert(p.left()==2&&p.right()==1);p.observe_drawn(false,true);assert(p.left()==1&&p.right()==0);++cases;}
 {Fixture f;botw::Snapshot p{true,{{"Obj_HeroSoul_Zora",9,1,true,0x10000},{"Obj_DLC_HeroSoul_Rito",9,1,true,0x20000}}};botw::publish_champion_ownership(f.h,p);assert(f.values["botw.ready.champion.0.owned"]==1&&f.values["botw.ready.champion.1.owned"]==1&&!f.values["botw.ready.champion.2.owned"]);assert(f.values["botw.ready.champion.0.charges"]==-1&&f.values["botw.ready.champion.0.seconds"]==-1);++cases;
  p.items.push_back(p.items[0]);botw::publish_champion_ownership(f.h,p);assert(!f.values["botw.ready.champion.0.valid"]);++cases;
  botw::publish_champion_ownership(f.h,{});assert(!f.values["botw.ready.champions.ownership_valid"]&&f.texts["botw.ready.champion.0.status"]=="Unavailable");++cases;}
 {Fixture f;State s;f.texts["page"]="ready";f.values["aux"]=1;
  f.h.get_text=[](void* p,const char*){return static_cast<Fixture*>(p)->texts["page"].c_str();};
  f.h.get_i64=[](void* p,const char*,int64_t){return static_cast<Fixture*>(p)->values["aux"];};s.bridge.host=f.h;
  assert(on_action(&s,"ready_left_pane",1)&&s.ready_panes.left()==1);assert(on_action(&s,"ready_right_pane",1)&&s.ready_panes.right()==1);assert(!on_action(&s,"ready_left_pane",3));++cases;
  // The Korok filter lives in the Map layer popover (MapUi); the retired korok menu actions are refused.
  f.texts["page"]="map";assert(!on_action(&s,"ready_left_pane",1));for(int64_t a:{-1,0,1,3})assert(!on_action(&s,"map_korok_menu",a)&&!on_action(&s,"map_korok_filter",a));++cases;
  s.bridge.healthy=true;s.bridge.map_supported=true;s.map_palette=0;s.map_tap_deferred=true;s.map_marker_deferred=true;s.map_delete_deferred=true;
  assert(!on_action(&s,"map_korok_menu",0));assert(s.map_palette==0&&s.map_tap_deferred&&s.map_marker_deferred&&s.map_delete_deferred);
  assert(on_action(&s,"map_tool",0));++cases;
  f.values["aux"]=0;assert(!on_action(&s,"map_korok_menu",0));++cases;}
 {botw::ReadyPanes p;p.set_champions_available(false);assert(p.left()==0);
  assert(p.step(false,-1)&&p.left()==1);assert(p.step(false,-1)&&p.left()==0);assert(p.step(false,1)&&p.left()==1);++cases;
  // Mid-cycle loss of availability clamps off the Champions page immediately.
  botw::ReadyPanes q;q.step(false,-1);assert(q.left()==2);q.set_champions_available(false);assert(q.left()==0);
  assert(q.step(false,-1)&&q.left()==1);++cases;
  // Iteration 12 item 9: with no champion owned the drawn view has no Champions page either --
  // the draw edge's combat default (left 2) falls back to the quest page, and the drawn left
  // cycle is quest-only (2 values). The caller re-applies availability every frame.
  botw::ReadyPanes r;r.set_champions_available(false);r.observe_drawn(true,true);r.set_champions_available(false);assert(r.left()==0&&r.right()==1);
  assert(r.step(false,-1)&&r.left()==1);assert(r.step(false,1)&&r.left()==0);assert(r.step(false,1)&&r.left()==1);++cases;
  // Once one is owned the drawn default returns to Champions on the next draw edge.
  r.observe_drawn(true,false);r.set_champions_available(true);r.observe_drawn(true,true);r.set_champions_available(true);assert(r.left()==2);
  assert(r.step(false,1)&&r.left()==0);assert(r.step(false,-1)&&r.left()==2);++cases;}
 {Fixture f;
  // All four canonical slots stay visible, including powers not obtained yet.
  // Iteration 12 item 9: the return value is the OWNED count (gates the pane), not the row count.
  assert(botw::publish_champion_ownership(f.h,botw::Snapshot{true,{}})==0);
  assert(f.values["botw.ready.champions.count"]==4&&f.values["botw.ready.champions.owned"]==0&&f.values["botw.ready.champions.available"]==0);++cases;
  for(unsigned i=0;i<4;++i){const auto key="botw.ready.champion_slot."+std::to_string(i);
    assert(f.texts[key+".display"]=="-" && f.values[key+".display_kind"]==4);++cases;}
  botw::Snapshot owned2{true,{{"Obj_DLC_HeroSoul_Goron",9,1,false,0},{"Obj_HeroSoul_Zora",9,1,true,0}}};
  assert(botw::publish_champion_ownership(f.h,owned2)==2);
  assert(f.values["botw.ready.champions.count"]==4&&f.values["botw.ready.champions.owned"]==2&&f.values["botw.ready.champions.available"]==1);
  assert(f.texts["botw.ready.champion_slot.0.icon"].find("IconSinJuNew_01")!=std::string::npos);
  assert(f.texts["botw.ready.champion_slot.0.status"]=="Enabled");
  assert(f.texts["botw.ready.champion_slot.1.display"]=="-");
  assert(f.texts["botw.ready.champion_slot.2.icon"].find("IconSinJuNew_00")!=std::string::npos);
  // Iteration 14 item 12: obtained but pouch-unequipped is never "Off"; without a live sample it is "--".
  assert(f.texts["botw.ready.champion_slot.2.display"]=="--" && f.values["botw.ready.champion_slot.2.display_kind"]==0);
  assert(f.values["botw.ready.champion_slot.0.tint"]==0 && f.values["botw.ready.champion_slot.2.tint"]==2);
  assert(f.values["botw.ready.champion_slot.0.enabled"]==1 && f.values["botw.ready.champion_slot.2.enabled"]==0);
  assert(f.values["botw.ready.champion_slot.0.tint_level"]==1 && f.values["botw.ready.champion_slot.2.tint_level"]==4);
  assert(botw::publish_champion_ownership(f.h,{})==0&&f.values["botw.ready.champions.available"]==0);++cases;
  // Iteration 14 item 12: obtained + pouch-unequipped + a live ready sample shows its charges, and
  // a recharging one its countdown; only not-obtained shows "-".
  botw::ChampionAbilities live{};live[2]={true,false,3,0};live[0]={true,true,0,75};
  botw::publish_champion_ownership(f.h,owned2,live);
  assert(f.texts["botw.ready.champion_slot.2.display"]=="x3" && f.values["botw.ready.champion_slot.2.display_kind"]==1);
  assert(f.texts["botw.ready.champion_slot.0.display"]=="1:15" && f.values["botw.ready.champion_slot.0.display_kind"]==2);
  assert(f.texts["botw.ready.champion_slot.1.display"]=="-" && f.values["botw.ready.champion_slot.1.display_kind"]==4);
  for(unsigned i=0;i<4;++i)assert(f.texts["botw.ready.champion_slot."+std::to_string(i)+".display"]!="Off");
  ++cases;}

 std::cout<<cases<<" Ready pane/draw-model/ownership/filter action cases passed\n";
}
