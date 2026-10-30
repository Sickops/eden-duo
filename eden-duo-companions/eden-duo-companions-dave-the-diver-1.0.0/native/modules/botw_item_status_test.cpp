// SPDX-License-Identifier: GPL-3.0-or-later
#define main consumable_original_test_main
#include "botw_consumables_test.cpp"
#undef main
#include "botw_item_status.h"
int main(){unsigned cases{};
 {FoodFixture f("Item_Fruit_I",7,10);assert(botw::consumable(f.h,f.item).effect==-1);assert(botw::item_badge(f.h,&f.item)=="status:Cool");++cases;}
 for(const auto& a:botw::armor_badges){Fixture f;botw::Item i{std::string(a.actor),4,1,false,0};assert(botw::item_badge(f.h,&i)==botw::badge_source(a.icon));++cases;}
 {FoodFixture f;f.put<int32_t>(f.P+0x80,7);const auto r=botw::recovery(f.h,&f.item);assert(r.valid&&r.quarters==7&&!r.full);botw::publish_recovery(f.h,"test",r);assert(f.texts["test.heart.0"].ends_with("Nt_HeartNew_00^f"));assert(f.texts["test.heart.1"].ends_with("Nt_HeartNew_05^f"));assert(f.texts["test.heart.2"].empty());++cases;
 f.put<float>(f.P+0x8C,2);f.put<float>(f.P+0x90,8);const auto full=botw::recovery(f.h,&f.item);assert(full.valid&&full.full&&full.extra==8);botw::publish_recovery(f.h,"test",full);assert(f.texts["test.heart.0"].empty()&&f.texts["test.label"]=="Full recovery"&&f.texts["test.extra_text"]=="+2"&&f.values["test.extra_valid"]==1);++cases;
 f.race=f.P;assert(!botw::recovery(f.h,&f.item).valid);++cases;}
 {Fixture f;botw::publish_recovery(f.h,"test",{true,false,120,0});assert(f.texts["test.heart.29"].ends_with("Nt_HeartNew_00^f"));botw::publish_recovery(f.h,"test",{});assert(f.values["test.valid"]==0&&f.texts["test.heart.29"].empty());++cases;}
 // Asset compliance + native layout animation (owner 2026-10-22): ordinary key items are named by their
 // actor; the 4 base + 4 Champions' Ballad orbs and the Travel Medallion animate through on-device
 // "module:fx/<effect>/<frame>" keys (20 fps, per-actor phase, on/off by the orb state), never packaged
 // files, and fall back to the actor (static romfs art) once the game files prove unusable.
 botw::fx::Availability().store(0);
 for(const char* actor:{"Obj_HeroSoul_Zora","Obj_HeroSoul_Rito","Obj_HeroSoul_Goron","Obj_HeroSoul_Gerudo",
     "Obj_DLC_HeroSoul_Zora","Obj_DLC_HeroSoul_Rito","Obj_DLC_HeroSoul_Goron","Obj_DLC_HeroSoul_Gerudo","Obj_WarpDLC"})
  for(bool on:{true,false}){botw::Item i{actor,9,1,on,0};
   const std::string a=botw::inventory_art(&i,0),b=botw::inventory_art(&i,50),c=botw::inventory_art(&i,49);
   assert(a.starts_with("module:fx/")&&a.find("file:")==std::string::npos);
   std::string effect;int fa=0,fb=0;assert(botw::fx::ParseFrameKey(a,effect,fa)&&botw::fx::ParseFrameKey(b,effect,fb));
   const int count=botw::fx::FindEffect(effect)->frames20;assert(fb==(fa+1)%count&&c==a);
   const bool medallion=std::string(actor)=="Obj_WarpDLC";
   assert(count==(medallion?640:on?160:80)); // one true period: orbs on 8 s, off 4 s; Medallion 32 s
   if(!medallion)assert(effect.ends_with(on?"_on":"_off"));
   std::string e2;int f2=0;assert(botw::fx::ParseFrameKey(botw::inventory_art(&i,int64_t(count)*50),e2,f2)&&f2==fa); // loops
   assert(botw::inventory_art(&i,-1000)==a);
   assert(botw::key_item_art_scale(&i)==(medallion?1700:1450)&&botw::detail_art_scale(&i)==1150&&botw::key_item_art_dy(&i)==(medallion?-15:0));
   ++cases;}
 {botw::Item korok{"Obj_KorokNuts",9,1,false,0};assert(botw::inventory_art(&korok,1234)=="Obj_KorokNuts"&&botw::key_item_art_scale(&korok)==1400&&botw::detail_art_scale(&korok)==0&&botw::key_item_art_dy(&korok)==0);
  botw::Item sword{"Weapon_Sword_070",0,1,false,0};assert(botw::key_item_art_scale(&sword)==0);++cases;}
 // Game files unusable: every key item falls back to its static actor art and static scale.
 botw::fx::Availability().store(2);
 for(const char* actor:{"Obj_HeroSoul_Zora","Obj_DLC_HeroSoul_Gerudo","Obj_WarpDLC"}){botw::Item i{actor,9,1,true,0};
  assert(botw::inventory_art(&i,500)==actor&&botw::key_item_art_scale(&i)==1400&&botw::detail_art_scale(&i)==0&&botw::key_item_art_dy(&i)==0);++cases;}
 botw::fx::Availability().store(0);
 assert(botw::inventory_art(nullptr,0).empty());
 for(const auto& a:botw::armor_badges)if(!a.icon.empty())assert(botw::badge_source(a.icon)=="status:"+std::string(a.icon));
 assert(botw::badge_source("").empty());++cases;
 std::cout<<cases<<" native item badge/recovery identity, fractions, full and clearing cases passed\n";
}
