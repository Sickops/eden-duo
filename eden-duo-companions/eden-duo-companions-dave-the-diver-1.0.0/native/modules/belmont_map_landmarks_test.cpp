// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_map_landmarks.h"
#include "belmont_pins.h"
#include <set>
#include <cassert>
#include <cmath>
#include <iostream>
int main(int argc, char **argv) {
  using namespace belmont_map_landmarks;
  Landmark l;
  l.guid = "known";
  l.unconditional_policy = true;
  l.navigation_zone_id = 7;
  std::vector<std::string> known{"known"}, none;
  std::vector<std::int64_t> cull{7};
  assert(!Visible(l, none, {}));
  assert(Visible(l, known, {}));
  assert(!Visible(l, known, cull));
  l.unconditional_policy = false;
  assert(!Visible(l, known, {}));
  SavePoint save;save.normal_art.navigation_zone_id=7;
  assert(!Visible(save,-1,1,7)&&!Visible(save,0,1,7));
  assert(!Visible(save,1,-1,7)&&!Visible(save,1,0,7));
  assert(!Visible(save,1,1,8)&&Visible(save,1,1,7));
  save.visible_in_unrevealed_map=true;assert(Visible(save,0,1,7));
  Elevator elevator;elevator.art.navigation_zone_id=7;
  assert(Visible(elevator,1,1,7));
  assert(!Visible(elevator,-1,1,7)&&!Visible(elevator,0,1,7));
  assert(!Visible(elevator,1,-1,7)&&!Visible(elevator,1,0,7)&&!Visible(elevator,1,1,8));
  elevator.secret_zone_id=0;assert(!Visible(elevator,1,1,7));
  FastTravelPoint fast;fast.unlocked_art.navigation_zone_id=7;
  assert(Visible(fast,1,1,7));
  assert(!Visible(fast,1,0,7)&&!Visible(fast,1,-1,7)&&!Visible(fast,-1,1,7)&&!Visible(fast,0,1,7)&&!Visible(fast,1,1,8));
  fast.visible_in_unrevealed_map=true;assert(Visible(fast,0,1,7));
  Reader missing(ce_unity::RangeReader{});
  assert(!missing.LoadForRoom("unknown"));
  if (argc != 2)
    return 0;
  Reader reader(ce_unity::MakeDirectoryRangeReader(argv[1]));
  const std::string room =
      "World_Biome_01_Streets_Poor_StreetsPoor_FirstRoom_uid73";
  auto data = reader.LoadForRoom(room);
  if (!data) {
    std::cerr << "native landmarks unavailable\n";
    return 1;
  }
  assert(data->landmarks.size() == 5);
  std::cout<<"native_save_points="<<data->save_points.size()<<" map_revealed_policy="<<data->map_revealed_policy<<'\n';
  assert(data->save_points.size()==5&&data->map_revealed_policy==1);
  for(const auto& point:data->save_points){
    assert(point.normal_art.sprites.size()==1&&point.last_used_art.sprites.size()==1);
    assert(point.normal_art.sprites[0].id==6230585524095202680LL);
    assert(point.last_used_art.sprites[0].id==9035961807606165270LL);
    assert(!point.visible_in_unrevealed_map);
    assert(std::abs(point.screen_percentage-.06f)<1e-7f);
    assert(std::abs(point.screen_percentage_at_zoom_max-.09f)<1e-7f);
    assert(point.icon_anchor_x==.5f&&point.icon_anchor_y==1.f&&point.icon_size_factor==1.f);
  }
  assert(data->level_guid == "946565bcb5d5f244e991e1790575a67c");
  assert(data->navigation_zone_ids.size() == 7 &&
         data->navigation_zone_ids[0] == 5763);
  assert(data->pin_types.size() == belmont_pins::KindCount);
  std::set<std::string> type_guids;
  for (const auto &type : data->pin_types) {
    assert(type.kind < belmont_pins::KindCount && type.guid.size() == 32 &&
           type_guids.insert(type.guid).second);
    assert(type.sprite_id == belmont_pins::Sprites[type.kind] && type.type_id &&
           type.display_id);
    std::cout << "native_pin_type " << type.kind << " " << type.guid << " "
              << type.sprite_id << " " << type.type_id << " " << type.display_id
              << "\n";
  }
  // Optional metadata may arrive late; cached map icons stay usable, then the
  // native GUID/type join retries instead of becoming permanently unavailable.
  bool managers_available = false;
  auto delayed_source = ce_unity::MakeDirectoryRangeReader(argv[1]);
  auto original_size = delayed_source.size;
  delayed_source.size =
      [&](std::string_view path) -> std::optional<std::uint64_t> {
    if (!managers_available &&
        path.find("afm_scenes_scenes_managers_") != std::string_view::npos)
      return {};
    return original_size(path);
  };
  Reader delayed(delayed_source);
  auto before = delayed.LoadMapItemsForRoom(room);
  assert(before && before->pin_types.empty() &&
         before->level_guid == data->level_guid &&
         before->save_points.size() == 5);
  managers_available = true;
  auto after = delayed.LoadMapItemsForRoom(room);
  assert(after && after->pin_types.size() == 9 &&
         after->navigation_zone_ids == data->navigation_zone_ids);
  delayed.ClearCache();
  assert(delayed.LoadMapItemsForRoom(room)->pin_types.size() == 9);
  unsigned all_save_points=0,all_elevators=0,nonsecret_elevators=0,all_fast_travel=0;
  const unsigned expected_counts[]={5,4,2,4,9,2,3,5,1,0};
  const unsigned expected_elevators[]={2,0,4,0,2,2,10,0,0,0};
  for(std::size_t index=0;index<belmont_map_sources::Recipes.size();++index){
    const auto& recipe=belmont_map_sources::Recipes[index];
    // This API selects a source by room prefix; no live position or invented
    // exploration is supplied by this static metadata-only fixture.
    const auto saves=reader.LoadSavePointsForRoom(std::string(recipe.world_name)+"_metadata_probe");
    assert(saves&&saves->landmarks.empty()&&saves->save_points.size()==expected_counts[index]);
    const auto items=reader.LoadMapItemsForRoom(std::string(recipe.world_name)+"_metadata_probe");
    assert(items&&items->elevators.size()==expected_elevators[index]);
    assert(items->level_guid.size() == 32 &&
           !items->navigation_zone_ids.empty() && items->pin_types.size() == 9);
    std::set<std::int64_t> source_zones(items->navigation_zone_ids.begin(),
                                        items->navigation_zone_ids.end());
    assert(source_zones.size() == items->navigation_zone_ids.size());
    std::cout << "native_pin_source " << index << " " << items->level_guid;
    for (auto id : items->navigation_zone_ids)
      std::cout << " " << id;
    std::cout << "\n";


    assert(items->elevators.size()==saves->elevators.size());
    assert(saves->map_revealed_policy==((index==7||index==9)?-1:1));
    assert(items->fast_travel_points.size()==(index<9?1:0));
    for(const auto& point:items->fast_travel_points){
      assert(point.level_guid.size()==32&&point.unlocked_art.sprites.size()==1&&point.unlocked_art.navigation_zone_id>0);
      const auto& icon=point.unlocked_art.sprites[0];
      assert(icon.id==8973591197645313780LL&&icon.name=="T_UI_FastTravel_CurrentLocationIcon");
      assert(icon.width==128&&icon.height==128&&icon.pixels_per_unit==128);
      assert(icon.bundle=="ui-commonsingame_assets_all_acb23e9b0608a368aad72d2fe488a679.bundle");
      assert(!point.visible_in_unrevealed_map);
      assert(Visible(point,1,1,point.unlocked_art.navigation_zone_id)&&!Visible(point,1,0,point.unlocked_art.navigation_zone_id));
      if(index==0)assert(point.level_guid=="946565bcb5d5f244e991e1790575a67c");
      std::cout<<"fast_travel_source "<<index<<" guid="<<point.level_guid<<" point="<<point.unlocked_art.x<<","<<point.unlocked_art.y<<" anchor="<<point.icon_anchor_x<<","<<point.icon_anchor_y<<" factor="<<point.icon_size_factor<<" percentage="<<point.screen_percentage<<","<<point.screen_percentage_at_zoom_max<<'\n';
      ++all_fast_travel;
    }
    for(const auto& point:saves->save_points){
      assert(point.normal_art.navigation_zone_id>0);
      assert(point.normal_art.sprites.size()==1&&point.last_used_art.sprites.size()==1);
      const auto& normal=point.normal_art.sprites[0];const auto& last=point.last_used_art.sprites[0];
      assert(normal.id==6230585524095202680LL&&last.id==9035961807606165270LL);
      assert(normal.width==128&&normal.height==128&&normal.pixels_per_unit==128);
      assert((normal.color==std::array<float,4>{1,1,1,1}));
      assert(last.color==normal.color&&last.pivot_x==.5f&&last.pivot_y==.5f);
      assert(point.icon_anchor_x==.5f&&point.icon_anchor_y==1.f&&point.icon_size_factor==1.f);
      assert(std::abs(point.screen_percentage-.06f)<1e-7f&&std::abs(point.screen_percentage_at_zoom_max-.09f)<1e-7f);
      assert(Visible(point,1,1,point.normal_art.navigation_zone_id));
      assert(!Visible(point,1,0,point.normal_art.navigation_zone_id));
      ++all_save_points;
    }
    for(const auto& point:items->elevators){
      assert(point.art.sprites.size()==1&&point.art.navigation_zone_id>0);
      const auto& glyph=point.art.sprites[0];
      assert(glyph.id==278879178657556255LL&&glyph.width==256&&glyph.height==256&&glyph.pixels_per_unit==256);
      assert((glyph.color==std::array<float,4>{1,1,1,1}));
      assert(glyph.pivot_x==.5f&&glyph.pivot_y==.5f);
      assert(point.icon_anchor_x==.5f&&point.icon_anchor_y==1.f&&point.icon_size_factor==1.f);
      assert(std::abs(point.screen_percentage-.05f)<1e-7f&&std::abs(point.screen_percentage_at_zoom_max-.08f)<1e-7f);
      assert(Visible(point,1,1,point.art.navigation_zone_id)==(point.secret_zone_id==-1));
      if(point.secret_zone_id==-1)++nonsecret_elevators;
      ++all_elevators;
    }
  }
  assert(all_fast_travel==9);
  assert(all_save_points==35);
  assert(all_elevators==20&&nonsecret_elevators==19);
  std::cout<<"all_native_save_points="<<all_save_points<<"\n";
  std::cout<<"all_native_elevators="<<all_elevators<<" nonsecret_subset="<<nonsecret_elevators<<"\n";
  bool docks = false;
  for (const auto &item : data->landmarks) {
    std::cout << item.source_name << " " << item.guid
              << " policy=" << item.unconditional_policy
              << " sprites=" << item.sprites.size() << "\n";
    if (item.room_id == room) {
      docks = true;
      assert(item.guid == "417a32b0b7028a347b0b367fb4ea46ea");
      assert(item.x == -347.5f && item.y == 4);
      assert(item.unconditional_policy);
      assert(item.sprites.size() == 1);
      const auto &s = item.sprites[0];
      assert(s.x == -350 && s.y == -1.5f);
      assert(s.name == "T_UI_Map_Landmark_Docks");
      assert(!(s.settings_raw & 1));
      assert(((s.settings_raw >> 2) & 15) == 0);
      assert(s.texture_rect_offset_x >= 0 && s.texture_rect_offset_y >= 0);
      assert(item.name_style.font_asset_id == 443565887117910916LL);
      assert(!item.name_style.font_asset_cab.empty());
      std::cout << "Docks fontCAB=" << item.name_style.font_asset_cab
                << " trimOffset=" << s.texture_rect_offset_x << ","
                << s.texture_rect_offset_y << " settings=" << s.settings_raw
                << "\n";
      assert(s.width == 512 && s.height == 512 && s.pixels_per_unit == 16);
      assert(item.name_key_id == 2799249536LL);
      assert(item.name_x == -350 && item.name_y == -1.5f);
      assert(item.name_style.font_size == 1);
      assert(item.name_style.font_style == 32);
      assert(item.name_style.scale_x == 1.5f &&
             item.name_style.scale_y == 1.5f);
      assert(std::abs(item.name_style.screen_percentage - .03f) < 1e-7f);
      assert(item.name_style.ribbon_sprite_id == 8769341631646220334LL);
      assert(item.name_style.ribbon_pixels_per_unit_multiplier == 12);
      assert(item.name_style.ribbon);
      assert(item.name_style.ribbon->border[0] == 156);
      assert(item.name_style.ribbon->border[2] == 153);
      assert(item.name_style.responsive_scale_x == 1.5f);
      assert(item.name_style.horizontal_alignment == 2);
      assert(item.name_style.vertical_alignment == 512);
      assert(item.name_style.margin[0] == 1 && item.name_style.margin[2] == 1);
      assert(!Visible(item, none, {}));
    }
  }
  assert(docks);
  reader.ClearCache();
  assert(reader.LoadForRoom(room));
  std::cout << "landmark source and discovery tests pass\n";
}
