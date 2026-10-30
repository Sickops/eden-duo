// SPDX-License-Identifier: GPL-3.0-or-later
// Map page overlay (fix round 2, critique item E): player projected via the host\'s own
// @map_view_* floats for the area ("screen = rect centre + (world - c) * ppw, y up"), on-view
// flag, and the off-view quest edge indicator (clamped to the view rect, with a bearing).
#include <limits>
#include <cmath>
#include "botw_map_page_overlay.h"
#include <cassert>
#include <iostream>
#include <map>
int main(){unsigned n=0;
using botw::map_page_overlay;using botw::map_label_tier;using botw::publish_map_label_tier;using botw::MapViewRect;using botw::MapLabelTiers;using botw::MapLabelTierDefault;using botw::MapLabelTierBasePpw;using botw::MapPosition;using botw::QuestMapMarkers;
struct Host{std::map<std::string,double> f;};
auto make_h=[](Host& host){
  EdenDsmodHostApi h{};h.userdata=&host;
  h.get_f64=[](void* u,const char* k,double d)->double{auto it=static_cast<Host*>(u)->f.find(k);return it==static_cast<Host*>(u)->f.end()?d:it->second;};
  return h;
};
// No view published yet (host never drew this area): overlay stays invalid, not a crash/garbage.
{Host host;auto h=make_h(host);MapPosition pos{true,0,0,false,0};QuestMapMarkers q{};
 auto o=map_page_overlay(h,"FullHyrule",pos,q);assert(!o.player_valid&&!o.quest_valid);++n;}
// View centred on the player (world 0,0 at canvas rect centre 620,540 of a 1240x1080 rect,
// 2 px/world-unit): player lands exactly on the centre, on-view.
{Host host;host.f={{"@map_view_cx:FullHyrule",0},{"@map_view_cy:FullHyrule",0},{"@map_view_ppw:FullHyrule",2},
  {"@map_view_x:FullHyrule",0},{"@map_view_y:FullHyrule",0},{"@map_view_w:FullHyrule",1240},{"@map_view_h:FullHyrule",1080}};
 auto h=make_h(host);MapPosition pos{true,0,0,false,0};QuestMapMarkers q{};
 auto o=map_page_overlay(h,"FullHyrule",pos,q);
 assert(o.player_valid&&o.player_on_view&&o.player_x==620&&o.player_y==540);++n;}
// Player panned off the drawn view (world position far from the view centre): not on-view.
{Host host;host.f={{"@map_view_cx:FullHyrule",0},{"@map_view_cy:FullHyrule",0},{"@map_view_ppw:FullHyrule",2},
  {"@map_view_x:FullHyrule",0},{"@map_view_y:FullHyrule",0},{"@map_view_w:FullHyrule",1240},{"@map_view_h:FullHyrule",1080}};
 auto h=make_h(host);MapPosition pos{true,2000,0,false,0};QuestMapMarkers q{};
 auto o=map_page_overlay(h,"FullHyrule",pos,q);assert(o.player_valid&&!o.player_on_view);++n;}
// Quest target on-view: drawn at its own true projected position, no edge clamp.
{Host host;host.f={{"@map_view_cx:FullHyrule",0},{"@map_view_cy:FullHyrule",0},{"@map_view_ppw:FullHyrule",2},
  {"@map_view_x:FullHyrule",0},{"@map_view_y:FullHyrule",0},{"@map_view_w:FullHyrule",1240},{"@map_view_h:FullHyrule",1080}};
 auto h=make_h(host);MapPosition pos{true,0,0,false,0};QuestMapMarkers q{};q.valid=true;q.count=1;q.positions[0]={50,0,-50};
 auto o=map_page_overlay(h,"FullHyrule",pos,q);
 assert(o.quest_valid&&o.quest_on_view&&o.quest_x==720&&o.quest_y==440);++n;}
// Quest target off-view (world y convention: native z south -> map y = -z, "y up"): clamped to
// the view rect edge, pointing the right way (east stays east, south-of-view stays below centre
// but inside the rect), never escaping the rect.
{Host host;host.f={{"@map_view_cx:FullHyrule",0},{"@map_view_cy:FullHyrule",0},{"@map_view_ppw:FullHyrule",2},
  {"@map_view_x:FullHyrule",0},{"@map_view_y:FullHyrule",0},{"@map_view_w:FullHyrule",1240},{"@map_view_h:FullHyrule",1080}};
 auto h=make_h(host);MapPosition pos{true,0,0,false,0};QuestMapMarkers q{};q.valid=true;q.count=1;q.positions[0]={5000,0,0};
 auto o=map_page_overlay(h,"FullHyrule",pos,q);
 assert(o.quest_valid&&!o.quest_on_view);
 assert(o.quest_x>=0&&o.quest_x<=1240&&o.quest_y>=0&&o.quest_y<=1080);
 assert(o.quest_x>620); // east of centre stays on the east edge
 assert(o.quest_bearing>0.&&o.quest_bearing<180.); // pointing roughly east/right, clockwise from up
 ++n;}
// Owner 2026-10-23: icon-label zoom tier (tools/mapfix2.py keeps the same constants).
assert(map_label_tier(0.05)==0);++n;
assert(map_label_tier(0.05*std::sqrt(2.0))==1);++n;
assert(map_label_tier(0.1)==2&&map_label_tier(0.2)==4&&map_label_tier(1.6)==10);++n;
assert(map_label_tier(0.01)==0&&map_label_tier(50.0)==MapLabelTiers-1);++n;            // clamped
assert(map_label_tier(0.0)==MapLabelTierDefault&&map_label_tier(-1.0)==MapLabelTierDefault);++n;
assert(map_label_tier(std::numeric_limits<double>::quiet_NaN())==MapLabelTierDefault);++n;
for(int t=0;t<MapLabelTiers;++t){const double ppw=MapLabelTierBasePpw*std::pow(2.0,t/2.0);
 assert(map_label_tier(ppw*1.18)==t&&map_label_tier(ppw/1.18)==t);} ++n;               // +-19 % stays in tier
{static long long got=-1;Host host;auto h=make_h(host);
 h.publish_i64=[](void*,const char* k,int64_t v){if(std::string(k)=="botw.map.label_tier")got=v;};
 publish_map_label_tier(h,MapViewRect{});assert(got==MapLabelTierDefault);++n;
 MapViewRect v;v.valid=true;v.ppw=0.2;publish_map_label_tier(h,v);assert(got==4);++n;}
std::cout<<n<<" map page overlay projection/on-view/edge-clamp cases passed\n";}
