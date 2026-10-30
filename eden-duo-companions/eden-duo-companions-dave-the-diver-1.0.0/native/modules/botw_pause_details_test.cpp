// SPDX-License-Identifier: GPL-3.0-or-later
#define main consumable_original_test_main
#include "botw_consumables_test.cpp"
#undef main
#include "botw_pause_details.h"
#include <cstdlib>
int main(){unsigned n{};
 assert(botw::armor_badge("Armor_044_Upper")=="Cool");++n;
 assert(botw::armor_status("Armor_044_Upper")->defense==1);++n;
 assert(botw::food_badge(4)=="Hot"&&botw::food_badge(5)=="Cool");++n;
 assert(std::string(botw::food_effect_key(10))=="AttackUp"&&std::string(botw::food_effect_key(5))=="ResistCold");++n;
 botw::Snapshot p;p.valid=true;p.items={{"Armor_001_Head",4,1,true,8},{"Armor_044_Upper",5,1,true,16},{"Armor_001_Lower",6,1,true,24},{"Armor_004_Head",4,1,false,32}};
 auto d=botw::outfit_defense(p,&p.items[3]);assert(d.valid&&d.before==7&&d.after==16);++n;
 d=botw::outfit_defense(p,&p.items[1]);assert(d.valid&&d.before==d.after);++n;
 p.items[3].equipped=true;assert(!botw::outfit_defense(p,&p.items[1]).valid);++n;
 p.items[3].equipped=false;p.items[0].actor="Armor_Unknown";assert(!botw::outfit_defense(p,&p.items[1]).valid);++n;
 {Fixture f;botw::publish_pause_gear(f.h,"test",32,5,0,0,"",false);assert(f.texts["test.pause.power"]=="32 ×5"&&f.values["test.pause.stat_valid"]==1);++n;
  botw::publish_pause_gear(f.h,"test",-1,-1,0,0,"",false);assert(f.texts["test.pause.power"].empty()&&f.values["test.pause.stat_valid"]==0);++n;}
 {FoodFixture f;botw::publish_pause_item(f.h,&f.item,p);botw::publish_pause_item(f.h,nullptr,p);assert(f.texts["botw.inventory.selected.pause.name"].empty()&&f.texts["botw.inventory.selected.pause.timer"].empty()&&f.values["botw.inventory.selected.pause.effect_valid"]==0);++n;}
 // Iteration 11 D: an inventory modifier box that would close past the pane edge wraps.
 {Fixture f;botw::publish_pause_gear(f.h,"test",116,0,0,0,"",true);assert(f.values["test.pause.effect_dy"]==0&&f.values["test.pause.fits"]==1);++n;}
 // Iteration 13: value well content centred (margins differ by <= 1 px), quantised width.
 {Fixture f;botw::publish_pause_gear(f.h,"test",116,0,0,0,"",true);const auto w=f.values["test.pause.well_width"],x=f.values["test.pause.well_x"],px=f.values["test.pause.power_x"];
  const int sw=botw::MeasureTextPx("116",5);assert(w%16==0&&px-x>=8&&(x+w)-(px+sw)>=8&&std::abs((px-x)-((x+w)-(px+sw)))<=1);
  assert(f.values["test.pause.rows"]==1&&f.values["test.pause.dur_dy"]==0);++n;}
 // Iteration 13: Attack Up (+10) fits beside a 2-digit value -> one row; durability right below.
 {const auto g=botw::layout_gear(824,40,true,150,30,true);
  assert(g.effect_dy==0&&g.rows==1&&g.dur_dy==0&&g.effect_x==g.well_x+g.well_w+8&&g.fits);
  assert(g.effect_x+g.effect_w<=1208);
  // Content centred in the modifier well: left and right margins differ by <= 1 px.
  const int content=32+150+6+30,left=g.icon_x-g.effect_x,right=g.effect_x+g.effect_w-(g.icon_x+content);
  assert(left>=8&&right>=8&&std::abs(left-right)<=1);++n;}
 // Iteration 13: Inventory, long modifier -> wraps to its own row at the pane edge; durability
 // follows on the row after it (value, modifier, durability), description moves with dur_dy.
 {const auto g=botw::layout_gear(824,90,true,230,60,true);
  assert(g.effect_dy==49&&g.effect_x==816&&g.rows==2&&g.dur_dy==49&&g.fits);++n;}
 // Pop-ups never wrap: same long modifier stays on row 1.
 {const auto g=botw::layout_gear(100,90,true,230,60,false);assert(g.effect_dy==0&&g.rows==1&&g.dur_dy==0);++n;}
 // No modifier: one row, no well.
 {const auto g=botw::layout_gear(824,40,false,0,0,true);assert(g.effect_w==0&&g.rows==1&&g.dur_dy==0&&g.fits);++n;}
 // Iteration 13 K + native layout animation: static key items draw at x1.4; the animated champion orbs
 // (x1.45) and Travel Medallion (x1.7, lifted 15 px) keep the native 96 px framing, with a x1.15 detail
 // scale; once the game files prove unusable they fall back to the static x1.4 and no lift.
 {botw::fx::Availability().store(0);
  botw::Item k{"Obj_WarpDLC",9,1,false,8},o{"Obj_HeroSoul_Zora",9,1,true,16},g{"Obj_KorokNuts",9,1,false,24},w{"Weapon_Sword_001",0,1,false,32};
  assert(botw::key_item_art_scale(&k)==1700&&botw::key_item_art_scale(&o)==1450&&botw::key_item_art_scale(&g)==1400&&botw::key_item_art_scale(&w)==0&&botw::key_item_art_scale(nullptr)==0);
  assert(botw::detail_art_scale(&k)==1150&&botw::detail_art_scale(&o)==1150&&botw::detail_art_scale(&g)==0);
  assert(botw::key_item_art_dy(&k)==-15&&botw::key_item_art_dy(&o)==0&&botw::key_item_art_dy(&w)==0&&botw::key_item_art_dy(nullptr)==0);
  botw::fx::Availability().store(2);
  assert(botw::key_item_art_scale(&k)==1400&&botw::key_item_art_scale(&o)==1400&&botw::detail_art_scale(&k)==0&&botw::key_item_art_dy(&k)==0);
  botw::fx::Availability().store(0);++n;}
 {botw::Item w;w.type=0;w.actor="Obj_WarpDLC";assert(botw::key_item_art_dy(&w)==0);++n;}
 std::cout<<n<<" native pause temperature, armor replacement, bow multishot and stale-data cases passed\n";
}
