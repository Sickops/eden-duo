// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Included beside the build-pinned Lua reader. No guest calls or writes.
uint64_t MapColorTable(Lua &l, uint64_t table, bool colorblind) {
  for(auto [key,value]:l.Entries(table))
    if(key.Tag()==(colorblind?0x1fffd:0x1fffe))return value.Table();
  return table;
}
uint32_t MapColor(Lua &l,uint64_t table) {
  uint32_t color=0;
  for(unsigned i=1;i<=3;++i) {
    auto value=Bounded(l.Index(table,i),0,1);if(!value)throw Invalid{};
    double v=*value<=.0031308?12.92* *value:1.055*std::pow(*value,1./2.4)-.055;
    color=(color<<8)|uint32_t(std::lround(v*255));
  }
  return (color<<8)|255;
}
uint32_t MapColorValues(const std::array<double,4> &values) {
  uint32_t color=0;
  for(unsigned i=0;i<3;++i) {
    auto v=std::clamp(values[i],0.,1.);
    auto srgb=v<=.0031308?12.92*v:1.055*std::pow(v,1./2.4)-.055;
    color=(color<<8)|uint32_t(std::lround(srgb*255));
  }
  return (color<<8)|uint32_t(std::lround(std::clamp(values[3],0.,1.)*255));
}
uint32_t SoilMapColor(uint32_t raw_ground,uint32_t field_mask,
                      unsigned plough_first,unsigned plough_channels,
                      unsigned spray_first,unsigned spray_channels,
                      bool plowing_enabled,bool fertilizer_enabled,
                      uint32_t needs_plowing,
                      const std::vector<uint32_t> &fertilizer_colors) {
  // The native third argument to setFoliageStateOverlayGroundStateColor is
  // the terrainDetailType bit mask. It excludes untouched background cells.
  if((raw_ground&field_mask)==0)return 0;
  uint32_t color=0;
  auto plough_mask=(1u<<plough_channels)-1;
  if(plowing_enabled && ((raw_ground>>plough_first)&plough_mask)==0)
    color=needs_plowing;
  auto spray_mask=(1u<<spray_channels)-1;
  auto spray=(raw_ground>>spray_first)&spray_mask;
  if(spray && !fertilizer_colors.empty()) {
    auto index=fertilizer_enabled?spray-1:unsigned(fertilizer_colors.size()-1);
    if(index<fertilizer_colors.size())color=fertilizer_colors[index];
  }
  return color;
}
std::vector<farming_simulator::MapMarker> ReadMapMarkers(
    Lua &l,const EdenDsmodHostApi &host,uint64_t env,uint64_t map,uint64_t mission) {
  using namespace farming_simulator;
  std::vector<MapMarker> out;
  std::string stage="hotspot list";
  size_t marker_index=0;
  DensityMemory memory(host);
  try {
  auto list=l.Field(map,"hotspots_sorted").Table();
  if(!list)return out;
  // Selling-point/tip-trigger station correlation (owner-reported live bug, 2026-10-17):
  // the map hotspot for a real selling point can carry no usable
  // "stationName" of its own (seen live: empty field, title fell back to the trigger's
  // class name and was blanked to "-"), and its MapHotspot category constant is the
  // generic "CATEGORY_TRIGGER" (confirmed from the native InGameMenu disassembly -- there
  // is no CATEGORY_SELLING_POINT/TIP constant at all), so neither the raw name nor the
  // category string ever contains anything MapMarkerFilterIndex recognises as a selling
  // point. The ONE registry that reliably knows every selling point's real identity is
  // mission.tipTriggers -- the exact table the Prices reader above already walks for the
  // same isSellingPoint/appearsOnStats pair and the exact same localized "stationName" the
  // Prices tab shows (so a resolved name here is guaranteed to match a price_stations[]
  // entry by name, letting the selection card's existing top-prices block, keyed on that
  // name, "just work"). Correlate by position: the hotspot's own xMapPos/zMapPos vs. each
  // tip trigger's world position (via its node field -- the same "live node id -> world
  // transform" path the moving-marker/vehicle code below and in the reader already uses),
  // nearest within a small radius. Built once per ReadMapMarkers call, independent of the
  // hotspot loop below so an unmatched/zero-count registry never disturbs the rest of this
  // function (its own try/catch boundary matches every other stage here: a bad trigger
  // record skips only that trigger, never aborts the whole map).
  struct SellingStation{std::string name;double x{},z{};};
  std::vector<SellingStation> stations;
  if(auto registry=l.Field(mission,"tipTriggers").Table()) {
    auto entries=l.Entries(registry);
    if(entries.size()<=256)for(const auto &[key,value]:entries) {
      auto trigger=value.Table()?value.Table():key.Table();
      if(!trigger)continue;
      if(!l.Field(trigger,"isSellingPoint").True() || !l.Field(trigger,"appearsOnStats").True())
        continue;
      auto name=l.Text(l.Field(trigger,"stationName"));
      if(name.empty())continue;
      double x{},z{};bool have_position=false;
      for(auto node_field:{"triggerId","rootNode","triggerNode","nodeId","exactFillRootNode"}) {
        auto node=Bounded(l.Field(trigger,node_field),1,4294967295.);
        if(!node)continue;
        try {
          auto pose=memory.WorldTransform(uint32_t(*node));
          x=pose.x;z=pose.z;have_position=true;break;
        } catch(...) {}
      }
      if(have_position)stations.push_back({std::move(name),x,z});
    }
  }
  auto nearest_station=[&](double x,double z)->const SellingStation* {
    const SellingStation *best{};double best_d2=0;
    constexpr double Radius=15; // Goldcrest Valley stations sit tens of metres apart.
    for(auto &station:stations) {
      double dx=station.x-x,dz=station.z-z,d2=dx*dx+dz*dz;
      if(d2<=Radius*Radius && (!best || d2<best_d2)) {best=&station;best_d2=d2;}
    }
    return best;
  };
  auto numeric=[&](uint64_t table,std::initializer_list<const char*> names,
                   double low,double high)->std::optional<double>{
    for(auto name:names)if(auto value=Bounded(l.Field(table,name),low,high))return value;
    return {};
  };
  auto string=[&](uint64_t table,std::initializer_list<const char*> names){
    for(auto name:names){auto value=l.Text(l.Field(table,name));if(!value.empty())return value;}
    return std::string{};
  };
  auto classes=l.Field(env,"MapHotspot").Table();
  stage="category constants";
  std::map<double,std::string> category_names;
  if(classes)for(const auto &[key,value]:l.Entries(classes)) {
    auto name=l.Text(key);auto number=value.Number();
    if(name.starts_with("CATEGORY_") && number)category_names[*number]=name.substr(9);
  }
  // G3 critique (2026-10-17, "selection card shows engine identifiers instead of game names"):
  // a place hotspot's own "name"/"fullName" fields (read into `string(hotspot,{"name",
  // "fullName"})` below) return the Lua trigger's internal class/variable name (e.g.
  // "tipTrigger"), not a player-facing label -- confirmed by comparing against the ONE place in
  // this reader that already resolves a real display name for the same kind of trigger object:
  // the Prices reader (above in this file) reads a tip-trigger's "stationName" field (a key
  // into g_i18n.texts), not "name"/"fullName". Reused here, same lookup table, same convention.
  auto texts=l.Field(l.Field(env,"g_i18n").Table(),"texts").Table();
  auto rect=[&](uint64_t uv,uint64_t texture,int &sx,int &sy,int &w,int &h) {
    if(!uv || !texture)return;
    auto a=Bounded(l.Index(uv,1),0,1),b=Bounded(l.Index(uv,2),0,1),
         c=Bounded(l.Index(uv,3),0,1),d=Bounded(l.Index(uv,4),0,1),
         e=Bounded(l.Index(uv,5),0,1),f=Bounded(l.Index(uv,6),0,1),
         g=Bounded(l.Index(uv,7),0,1),hval=Bounded(l.Index(uv,8),0,1);
    if(!a||!b||!c||!d||!e||!f||!g||!hval)return;
    auto filename=l.Text(l.Field(texture,"filename"));
    auto marker=filename.rfind('_'),dot=filename.rfind('.');
    int size=1024;
    if(marker!=std::string::npos && dot>marker+1) {
      auto digits=filename.substr(marker+1,dot-marker-1);
      try{size=std::stoi(digits);}catch(...) {}
    }
    auto umin=std::min({*a,*c,*e,*g}),umax=std::max({*a,*c,*e,*g});
    auto vmin=std::min({*b,*d,*f,*hval}),vmax=std::max({*b,*d,*f,*hval});
    sx=int(std::lround(umin*size));
    // Assets::Texture presents the atlas with DDS storage vertically flipped;
    // convert normalized bottom-left UV corners to its top-left pixel space.
    sy=int(std::lround((1-vmax)*size));
    w=int(std::lround((umax-umin)*size));h=int(std::lround((vmax-vmin)*size));
    if(size<1 || size>4096 || w<1 || h<1 || sx+w>size || sy+h>size)
      sx=sy=w=h=0;
  };
  auto texture_size=[](std::string_view filename) {
    auto marker=filename.rfind('_'),dot=filename.rfind('.');
    if(marker!=filename.npos && dot>marker+1)try{return std::stoi(std::string(filename.substr(marker+1,dot-marker-1)));}catch(...){}
    return 0;
  };
  std::vector<uint64_t> groups;
  stage="boolean group list";
  for(const auto &[key,value]:l.Entries(list))
    if((key.Tag()==0x1fffe || key.Tag()==0x1fffd) && value.Table())
      groups.push_back(value.Table());
  if(groups.empty())groups.push_back(list); // older observed linear layout
  std::unordered_set<uint64_t> seen;
  for(auto group:groups) {
    stage="hotspot array length";
    auto count=l.Get<uint32_t>(group+48);
    if(count>513)throw std::runtime_error("native hotspot count");
    for(uint32_t i=1;i<count && out.size()<512;++i) {
      marker_index++;
      stage="hotspot record fields";
      auto hotspot=l.Index(group,i).Table();if(!hotspot || !seen.insert(hotspot).second)continue;
      auto category=numeric(hotspot,{"category"},0,65535);
      // Field definition objects are represented by the checked native field
      // labels in Assets::Map; rendering them again as generic icons duplicates
      // the native number markers (drawn as the whole icon atlas over each field
      // number when let through). Harvesters are not category 1: they were hidden
      // by draw order, see MapMarkerDrawRank.
      if(category && *category==1)continue;
      MapMarker marker;
      marker.object_id=uint32_t(numeric(hotspot,{"objectId"},1,4294967295.).value_or(0));
      auto x=numeric(hotspot,{"xMapPos"},-1024,1024),
           z=numeric(hotspot,{"zMapPos"},-1024,1024);
      marker.x=x.value_or(0);marker.z=z.value_or(0);
      marker.label=string(hotspot,{"name","fullName"});
      marker.enabled=l.Field(hotspot,"enabled").Tag()!=0x1fffe;
      marker.color=0xffffffff;
      if(auto color=l.Field(hotspot,"baseColor").Table())try {marker.color=MapColor(l,color);} catch(...) {}
      auto category_name=string(hotspot,{"category"});
      if(category) {
        marker.kind="category_"+std::to_string(unsigned(*category));
        if(auto found=category_names.find(*category);found!=category_names.end())marker.kind=found->second;
      } else if(!category_name.empty())marker.kind=category_name;
      else marker.kind=string(hotspot,{"name"});
      // InGameMenu:selectHotspot: Visit/Tag for places only (category named, not a vehicle
      // category, not an AI worker).
      marker.can_visit=category && category_names.count(*category) &&
                       !marker.kind.starts_with("VEHICLE_") && marker.kind!="AI";
      // Owner live bug (2026-10-17): a tip trigger's MapHotspot category is the generic
      // "CATEGORY_TRIGGER" -- there is no per-purpose category constant -- so marker.kind
      // above never contains anything MapMarkerFilterIndex recognises as a selling point,
      // and the "Selling points" filter misses it. isSellingPoint is the field the native
      // game itself (and the Prices reader above) uses to tell a selling point apart from
      // every other kind of trigger on this same hotspot table; trust it directly when
      // present, and otherwise fall back to the tipTriggers position correlation below.
      bool is_selling_point=l.Field(hotspot,"isSellingPoint").True();
      // Place marker display name (G3 critique, see the texts/g_i18n comment above): try the
      // same "stationName" field the Prices reader resolves for a tip trigger, localized the
      // same way. The hotspot table itself is the trigger object for a tip/selling-point
      // marker (it is what registered the hotspot), so this field, when present, is expected
      // to be the SAME key Prices already shows for that same facility. Only overrides the
      // raw name/fullName fallback for place markers (can_visit) -- vehicle markers' title is
      // resolved separately, from the matching owned vehicle's own Garage name (View::
      // Operations' selection_content), not from this hotspot field at all.
      if(marker.can_visit) {
        auto station_key=string(hotspot,{"stationName"});
        auto localized=station_key.empty()?std::string{}:l.Text(l.Field(texts,station_key));
        marker.station_key=station_key;
        if(!localized.empty())marker.label=localized;
        else if(!station_key.empty())marker.label.clear();
        // Live-verified gap (2026-10-17): the hotspot's
        // own "stationName" field above is empty for at least one real tip trigger seen in
        // play (a grain-elevator-style station on Goldcrest Valley), so marker.label still
        // falls back to the unresolved name/fullName read above the can_visit block -- which
        // for that same marker is the literal string "tipTrigger", confirming the G3 critique's
        // root cause (this hotspot record carries the trigger's generic class/constructor name,
        // not a per-instance display name) but NOT fully fixing it: the live station's real
        // name was never found under any field this reader tries. Flag for a follow-up RE task:
        // mission.tipTriggers (the same registry the Prices reader walks) likely holds this
        // marker's real identity under a different key than this hotspot record exposes --
        // correlate by a shared object/node id, not by re-reading "name" here. Until then, ship
        // the honest fallback: a camelCase, lowercase-first string with no spaces (name/
        // fullName's own value for unresolved triggers, observed as exactly "tipTrigger", never
        // a real display name in any capture on file) is recognized as the trigger's own class
        // name, not a label, and blanked rather than shown -- View::Operations' selection_content
        // already falls back to "-" for an empty label, so a blocked player still sees "-"
        // instead of a raw identifier, honouring UX-SPEC's "never show a Lua class name" rule
        // even where the real name remains unresolved.
        if(!marker.label.empty() && std::islower(static_cast<unsigned char>(marker.label[0])) &&
           marker.label.find(' ')==std::string::npos &&
           std::any_of(marker.label.begin()+1,marker.label.end(),
                       [](unsigned char c){return std::isupper(c);}))
          marker.label.clear();
        // Fix for the live gap the comment above flagged (now resolved): this hotspot's own
        // "stationName" field can be empty even for a genuine selling point (confirmed live
        // against a grain-elevator-style station on Goldcrest Valley). Correlate by position
        // against mission.tipTriggers -- the exact registry Prices walks for the exact same
        // isSellingPoint/appearsOnStats pair -- nearest within a small radius. This also
        // catches the case where isSellingPoint above was false/missing on this hotspot
        // record itself: a position match in the tipTriggers registry is itself proof the
        // hotspot IS a selling point, regardless of what this table's own fields say.
        if(marker.label.empty() || !is_selling_point) {
          if(auto *station=nearest_station(marker.x,marker.z)) {
            if(marker.label.empty())marker.label=station->name;
            if(marker.station_key.empty())marker.station_key=station->name;
            is_selling_point=true;
          }
        }
      }
      if(is_selling_point)marker.kind="SELLINGPOINT";
      stage="foreground overlay array";
      auto overlays=l.Field(hotspot,"overlays").Table();
      uint64_t foreground{};int best_size{};
      for(uint32_t oi=1;overlays && oi<=5;++oi) {
        auto overlay=l.Index(overlays,oi).Table();if(!overlay)continue;
        auto filename=l.Text(l.Field(overlay,"filename"));
        int size=texture_size(filename);
        if(!filename.empty() && size>=best_size) {foreground=overlay;best_size=size;}
        // The native atlas list is ordered from the largest texture down. A
        // 1024 entry is the maximum supported resolution, so avoid touching
        // four lower-resolution overlay tables for every hotspot.
        if(size>=1024)break;
      }
      if(foreground) {
        stage="foreground overlay fields";
        marker.image=l.Text(l.Field(foreground,"filename"));
        marker.rotation=numeric(foreground,{"rotation"},-100,100).value_or(0);
        auto foreground_uv=l.Field(hotspot,"imageUVs").Table();
        if(!foreground_uv)foreground_uv=l.Field(foreground,"uvs").Table();
        rect(foreground_uv,foreground,marker.sx,marker.sy,marker.w,marker.h);
        auto width=numeric(foreground,{"width"},0,1),height=numeric(foreground,{"height"},0,1);
        if(width)marker.draw_w=int(std::lround(*width*1280));
        if(height)marker.draw_h=int(std::lround(*height*720));
      }
      auto bg=l.Field(hotspot,"overlayBg").Table();
      if(bg) {
        stage="background overlay fields";
        marker.background_image=l.Text(l.Field(bg,"filename"));
        rect(l.Field(bg,"uvs").Table(),bg,marker.background_sx,marker.background_sy,
             marker.background_w,marker.background_h);
        std::array<double,4> bg_color{1,1,1,1};
        bool valid=true;const char *channels[]={"r","g","b","a"};
        for(unsigned i=0;i<4;++i) {
          auto value=Bounded(l.Field(bg,channels[i]),0,1);
          if(value)bg_color[i]=*value;else if(i<3)valid=false;
        }
        if(valid)marker.background_color=MapColorValues(bg_color);
      }
      if(!marker.draw_w)marker.draw_w=int(std::lround(numeric(hotspot,{"width"},0,1).value_or(.012)*1280));
      if(!marker.draw_h)marker.draw_h=int(std::lround(numeric(hotspot,{"height"},0,1).value_or(.02)*720));
      if(marker.object_id)try {
        stage="moving object transform";
        auto pose=memory.WorldTransform(marker.object_id);
        marker.x=pose.x;marker.z=pose.z;
      } catch(...) { /* static map position remains the safe fallback */ }
      stage="marker validation";
      if(marker.image.empty() || !marker.w || !marker.h)continue;
      out.push_back(std::move(marker));
    }
  }
  return out;
  } catch(const Invalid &) {
    throw std::runtime_error("invalid native hotspot data during "+stage+
        " at marker "+std::to_string(marker_index)+" (bytes="+
        std::to_string(l.bytes)+", calls="+std::to_string(l.calls)+")");
  }
}
std::shared_ptr<farming_simulator::MapLayers> ReadMapLayers(
    Lua &l,const EdenDsmodHostApi &host,uint64_t env,uint64_t mission) {
  using namespace farming_simulator;
  std::string stage="map dimensions";
  try {
  auto mapinfo=l.Field(mission,"ingameMap").Table();
  if(l.Field(mapinfo,"worldSizeX").Number()!=2048 ||
     l.Field(mapinfo,"worldSizeZ").Number()!=2048 ||
     l.Field(mapinfo,"worldCenterOffsetX").Number()!=1024 ||
     l.Field(mapinfo,"worldCenterOffsetZ").Number()!=1024)throw Invalid{};
  auto menu=l.Field(env,"InGameMenu").Table(),fruitutil=l.Field(env,"FruitUtil").Table();
  auto descs=l.Field(fruitutil,"fruitIndexToDesc").Table(),fruits=l.Field(mission,"fruits").Table();
  auto settings=l.Field(env,"g_gameSettings").Table();
  bool colorblind=l.Field(settings,"useColorblindMode").True();
  auto colors=[&](const char* key){return MapColorTable(l,l.Field(menu,key).Table(),colorblind);};
  stage="native map colors";
  auto one=[&](const char*key){return MapColor(l,colors(key));};
  std::vector<uint32_t> growing,harvest;
  for(unsigned i=1;i<=4;++i)growing.push_back(MapColor(l,l.Index(colors("FRUIT_COLORS_GROWING"),i).Table()));
  for(unsigned i=1;i<=3;++i)harvest.push_back(MapColor(l,l.Index(colors("FRUIT_COLORS_HARVEST"),i).Table()));
  auto cultivated=one("FRUIT_COLOR_CULTIVATED"),plowed=one("FRUIT_COLOR_PLOWED"),
       withered=one("FRUIT_COLOR_WITHERED"),tops=one("FRUIT_COLOR_REMOVE_TOPS"),cut=one("FRUIT_COLOR_CUTTED");
  auto out=std::make_shared<MapLayers>();
  out->growth_legend={
    {"ui_growthMapCultivated",{}, {cultivated}},
    {"ui_growthMapPlowed",{}, {plowed}},
    {"ui_growthMapGrowing",{}, growing},
    {"ui_growthMapReadyToPrepareForHarvest",{}, {tops}},
    {"ui_growthMapReadyToHarvest",{}, harvest},
    {"ui_growthMapWithered",{}, {withered}},
    {"ui_growthMapCutted",{}, {cut}}};
  std::array<uint32_t,512> crop_colors{},growth_colors{};
  DensityMemory memory(host);uint64_t fruit_map{};
  stage="fruit descriptors and density maps";
  for(unsigned i=1;i<=12;++i) {
    auto desc=l.Index(descs,i).Table(),fruit=l.Index(fruits,i).Table();
    if(!desc || !fruit)continue;
    auto id=Bounded(l.Field(fruit,"id"),1,1e8);if(!id)throw Invalid{};
    auto node=memory.Entity(uint32_t(*id));auto density=memory.Map(node);
    if(fruit_map && fruit_map!=density)throw Invalid{};
    fruit_map=density;
    auto type=memory.Get<uint32_t>(node+0x688);
    if(!type || type>15 || memory.Get<uint32_t>(node+0x68c)!=type)throw Invalid{};
    auto name=l.Text(l.Field(desc,"name"));
    if(!l.Field(desc,"shownOnMap").True() || name=="grass")continue;
    auto color=MapColor(l,l.Index(colors("FRUIT_COLORS"),i).Table());
    std::string label="fillType_"+name,icon;
    // Native display strings, rather than an English companion crop dictionary.
    auto fill=l.Field(env,"FillUtil").Table();auto fill_descs=l.Field(fill,"fillTypeIndexToDesc").Table();
    auto count=Bounded(l.Field(fill,"NUM_FILLTYPES"),1,128);
    if(count)for(unsigned f=1;f<=unsigned(*count);++f) {
      auto fd=l.Index(fill_descs,f).Table();
      if(l.Text(l.Field(fd,"name"))==name) {
        auto n=l.Text(l.Field(fd,"nameI18N"));if(!n.empty())label=n;
        icon=l.Text(l.Field(fd,"hudOverlayFilename"));
        break;
      }
    }
    out->crop_legend.push_back({label,icon,{color}});
    for(unsigned state=1;state<32;++state)crop_colors[type|(state<<4)]=color;
    auto integer=[&](const char *key){auto v=Bounded(l.Field(desc,key),-1,30);if(!v)throw Invalid{};return int(*v);};
    int min_h=integer("minHarvestingGrowthState"),max_h=integer("maxHarvestingGrowthState"),
        min_p=integer("minPreparingGrowthState"),max_p=integer("maxPreparingGrowthState"),
        cut_state=integer("cutState"),prepared=integer("preparedGrowthState");
    if(max_h<=0)continue;
    auto set=[&](int state,uint32_t c){if(state>=0 && state<31)growth_colors[type|((state+1)<<4)]=c;};
    int dead=(max_p>0?max_p:max_h)+1;
    if(dead!=cut_state && dead!=prepared && dead!=min_p)set(dead,withered);
    int last_g=min_h-1;if(min_p>0)last_g=std::min(last_g,min_p-1);
    for(int state=0;state<=last_g;++state)set(state,growing[std::min(state,3)]);
    if(min_p>0)for(int state=min_p;state<=max_p;++state)set(state,tops);
    set(cut_state,cut);
    for(int state=min_h;state<=max_h;++state)set(state,harvest[std::min(state-min_h,2)]);
  }
  auto ground_id=Bounded(l.Field(mission,"terrainDetailId"),1,1e8);if(!ground_id || !fruit_map)throw Invalid{};
  stage="terrain density maps";
  auto ground_map=memory.Map(memory.Entity(uint32_t(*ground_id)));
  auto raw_fruit=memory.Grid(fruit_map),raw_ground=memory.Grid(ground_map);
  auto channels=Bounded(l.Field(mission,"terrainDetailTypeNumChannels"),1,8),
       first=Bounded(l.Field(mission,"terrainDetailTypeFirstChannel"),0,8),
       cultivator=Bounded(l.Field(mission,"cultivatorValue"),0,255),
       plough=Bounded(l.Field(mission,"ploughValue"),0,255);
  if(!channels || !first || !cultivator || !plough)throw Invalid{};
  // MAP_SOIL is a separate native mode: it colors the plough-counter zero
  // state first, then spray states 1..(2^channels-1). The latter therefore
  // wins when both predicates match, just as the native overlay setup order.
  bool soil_ready=false,plowing_enabled=false,fertilizer_enabled=false;
  unsigned spray_channels=0,spray_first=0,plough_channels=0,plough_first=0;
  uint32_t field_mask=0,needs_plowing=0;
  std::vector<uint32_t> fertilizer_colors;
  std::string soil_stage="mission flags";
  try {
    auto soil_menu=l.Field(mission,"missionInfo").Table();
    plowing_enabled=l.Field(soil_menu,"plowingRequiredEnabled").True();
    fertilizer_enabled=l.Field(soil_menu,"fertilizerStatesEnabled").True();
    soil_stage="needs-plowing palette";
    needs_plowing=one("FRUIT_COLOR_NEEDS_PLOWING");
    soil_stage="fertilizer palette";
    auto fertilized_table=colors("FRUIT_COLORS_FERTILIZED");
    spray_channels=unsigned(Bounded(l.Field(mission,"sprayLevelNumChannels"),1,8).value_or(0));
    spray_first=unsigned(Bounded(l.Field(mission,"sprayLevelFirstChannel"),0,24).value_or(255));
    plough_channels=unsigned(Bounded(l.Field(mission,"ploughCounterNumChannels"),1,8).value_or(0));
    plough_first=unsigned(Bounded(l.Field(mission,"ploughCounterFirstChannel"),0,24).value_or(255));
    if(!spray_channels || !plough_channels || spray_first+spray_channels>32 ||
       plough_first+plough_channels>32 || unsigned(*first)+unsigned(*channels)>32)
      throw Invalid{};
    field_mask=((1u<<unsigned(*channels))-1)<<unsigned(*first);
    const unsigned spray_states=(1u<<spray_channels)-1;
    if(spray_states>16)throw Invalid{};
    for(unsigned state=1;state<=spray_states;++state) {
      // Native Lua uses #FRUIT_COLORS_FERTILIZED as the single color when
      // fertilizer states are disabled; it does not use the zero state.
      auto index=fertilizer_enabled?state:spray_states;
      fertilizer_colors.push_back(MapColor(l,l.Index(fertilized_table,index).Table()));
    }
    if(plowing_enabled)
      out->soil_legend.push_back({"ui_growthMapNeedsPlowing",{}, {needs_plowing}});
    out->soil_legend.push_back({"ui_growthMapFertilized",{}, fertilizer_colors});
    soil_ready=true;
  } catch(const Invalid &) {
    out->soil_legend.clear();
    fertilizer_colors.clear();
    out->soil_error="invalid soil data during "+soil_stage+" (bytes="+
        std::to_string(l.bytes)+", calls="+std::to_string(l.calls)+")";
  } catch(const std::exception &e) {
    out->soil_legend.clear();
    fertilizer_colors.clear();
    out->soil_error="soil data failed during "+soil_stage+": "+e.what()+
        " (bytes="+std::to_string(l.bytes)+", calls="+
        std::to_string(l.calls)+")";
  }
  out->crops.w=out->crops.h=out->growth.w=out->growth.h=512;
  if(soil_ready)out->soil.w=out->soil.h=512;
  out->crops.rgba.resize(512*512*4);out->growth.rgba.resize(512*512*4);
  if(soil_ready)out->soil.rgba.resize(512*512*4);
  auto put=[](Image &im,size_t at,uint32_t c){for(unsigned k=0;k<4;++k)im.rgba[at*4+k]=c>>(24-k*8);};
  uint64_t hash=1469598103934665603ULL;
  stage="map layer rasterization";
  for(size_t p=0;p<raw_fruit.size();++p) {
    auto fruit=raw_fruit[p];if(fruit>=512)throw Invalid{};
    auto ground=(raw_ground[p]>>unsigned(*first))&((1u<<unsigned(*channels))-1);
    uint32_t gc=growth_colors[fruit];
    if(!gc)gc=ground==unsigned(*cultivator)?cultivated:ground==unsigned(*plough)?plowed:0;
    uint32_t sc=soil_ready?SoilMapColor(raw_ground[p],field_mask,plough_first,
        plough_channels,spray_first,spray_channels,plowing_enabled,
        fertilizer_enabled,needs_plowing,fertilizer_colors):0;
    put(out->crops,p,crop_colors[fruit]);put(out->growth,p,gc);
    if(soil_ready)put(out->soil,p,sc);
    hash=(hash^crop_colors[fruit])*1099511628211ULL;hash=(hash^gc)*1099511628211ULL;
    hash=(hash^sc)*1099511628211ULL;
  }
  out->revision=hash;
  return out;
  } catch(const Invalid &) {
    throw std::runtime_error("invalid native map data during "+stage+" (bytes="+
        std::to_string(l.bytes)+", calls="+std::to_string(l.calls)+")");
  }
}
