// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_tracker_view.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
namespace botw {
struct TrackerViewFixtureAccess {
 static void seed(TrackerView& v,TrackerData d,Snapshot s={},bool map=false) {
  v.clear();v.epoch=1;v.data=std::move(d);v.inventory=std::move(s);v.next=std::chrono::steady_clock::time_point::max();
  v.was_active=true;v.was_map=map;v.armor_rows();v.rebuild();
 }
 static void refresh(TrackerView& v,TrackerData d,Snapshot s) {
  v.data=std::move(d);v.inventory=std::move(s);v.armor_rows();v.rebuild();v.dirty=true;
 }
 static std::size_t size(const TrackerView& v){return v.filtered.size();}
 static int index(const TrackerView& v){return v.selected_index();}
 static int family(const TrackerView& v){return v.family;}
 static const std::string& id(const TrackerView& v){return v.selected_id;}
 static int planned(const TrackerView& v){return v.armor.selected_count;}
 static int missing(const TrackerView& v){int n=0;for(const auto& m:v.armor.materials)n+=m.missing;return n;}
};
}
struct TrackerFrameFixture:Fixture {
 std::map<std::string,double> numbers;
 TrackerFrameFixture(){h.publish_f64=[](void* p,const char* key,double value){static_cast<TrackerFrameFixture*>(p)->numbers[key]=value;};}
};
int main() {
 using namespace botw;using A=TrackerViewFixtureAccess;
 int checks=0;auto check=[&](bool b){++checks;if(!b)std::cerr<<"failed "<<checks<<"\n";assert(b);};
 auto row=[](std::string id,int group,int state,bool map=false){TrackerRecord r;r.id=id;r.name=id;r.group=group;r.state=state;r.status=state<0?"Unknown":"Known";r.detail="Detail "+id;r.location=map;r.x=100;r.y=200;return r;};
 TrackerData d;d.valid=true;
 for(int i=0;i<900;++i)d.categories[0].push_back(row("Korok"+std::to_string(i),0,i%3==0?-1:i%3==1?0:2,true));
 d.categories[0].push_back(row("Shrine",1,1,true));d.categories[0].push_back(row("Tower",2,0,true));d.categories[0].push_back(row("DLC Shrine",3,-1,true));
 d.categories[1].push_back(row("Talus",0,0,true));
 d.categories[3].push_back(row("Key",0,2));d.categories[3].push_back(row("Bridle",1,0));
 for(int i=0;i<3;++i)d.categories[4].push_back(row("Pouch"+std::to_string(i),i,0));
 d.categories[6].push_back(row("Not started",0,0));d.categories[6].push_back(row("Active",0,1));d.categories[6].push_back(row("Complete",0,2));d.categories[6].push_back(row("Unknown",0,-1));d.categories[6].push_back(row("SideQuest",2,0));d.categories[6].push_back(row("ShrineQuest",1,0));
 Snapshot inv{true,{{"Armor_001_Head",4,0,false,100},{"Armor_001_Head",4,0,false,200},{"Item_Enemy_00",7,7,false,300},{"Armor_172_Head",4,0,false,400}}};
 TrackerFrameFixture f;TrackerView v;A::seed(v,d,inv);auto render=[&](const char* page="trackers"){f.values.clear();f.texts.clear();f.numbers.clear();v.sample(f.h,inv,{},1,page);};
 // Iteration 5: one flat family index (no tabs), the whole filtered list published for scrolling.
 static_assert(TrackerView::FamilyCount==10&&TrackerView::MaxRows==900&&TrackerView::ArmorFamily==8);
 render();check(f.values["botw.tracker.family"]==0&&f.values["botw.tracker.count"]==900&&A::size(v)==900);
 check(f.texts["botw.tracker.family.0.label"]=="Koroks"&&f.texts["botw.tracker.family.3.label"]=="DLC shrines"&&f.texts["botw.tracker.family.9.label"]=="Quest progress");
 check(f.texts["botw.tracker.family.0.summary"]=="300 / 900"&&f.texts["botw.tracker.family.1.summary"]=="0 / 1"&&f.texts["botw.tracker.family.3.summary"]=="-");
 check(f.texts["botw.tracker.family.5.summary"]=="-"&&f.texts["botw.tracker.family.9.summary"]=="1 / 6"&&f.texts["botw.tracker.summary"]=="300 / 900");
 check(f.texts["botw.tracker.title"]=="Koroks"&&f.texts["botw.tracker.row.0.name"]=="Korok0"&&f.texts["botw.tracker.row.899.name"]=="Korok899");
 check(!f.texts.contains("botw.tracker.row.900.name")&&!f.values.contains("botw.tracker.row.0.valid")&&!f.texts.contains("botw.tracker.hint"));
 check(f.values["botw.tracker.selected.index"]==0&&f.texts["botw.tracker.selected.name"]=="Korok0"&&f.values["botw.tracker.list_empty"]==0&&f.texts["botw.tracker.empty_text"].empty());
 check(f.values["botw.tracker.map.valid"]==0&&!f.values.contains("botw.tracker.map.0.kind"));
 const auto first_values=f.values;const auto first_texts=f.texts;render();
 check(f.values==first_values&&f.texts==first_texts);render();check(f.values==first_values&&f.texts==first_texts);
 check(!v.action(nullptr,0));check(!v.action("other",0));check(!v.action("tracker_family",10));check(!v.action("tracker_family",-1));
 check(!v.action("tracker_select",900));check(!v.action("tracker_marker",900));
 // Removed paging and tab actions are refused.
 check(!v.action("tracker_next",1));check(!v.action("tracker_category",0));check(!v.action("tracker_group",1));
 // A row anywhere in the list selects by absolute index; the scroll region follows selected.index.
 check(v.action("tracker_select",896));render();check(A::id(v)=="Korok896"&&f.values["botw.tracker.selected.index"]==896&&f.texts["botw.tracker.detail"]=="Detail Korok896");
 // A filter keeps a selection that still matches, else falls back to the first row.
 check(v.action("tracker_filter",1));check(A::size(v)==300&&A::id(v)=="Korok1"&&A::index(v)==0);
 check(v.action("tracker_select",5));check(A::id(v)=="Korok16");check(v.action("tracker_filter",0));check(A::size(v)==900&&A::id(v)=="Korok16"&&A::index(v)==16);
 check(v.action("tracker_filter",2));check(A::size(v)==300&&A::id(v)=="Korok2");
 check(v.action("tracker_filter",0));check(A::size(v)==900);
 render();const auto token_all=f.values["botw.tracker.list_token"];check(v.action("tracker_filter",1));render();check(f.values["botw.tracker.list_token"]!=token_all);check(v.action("tracker_filter",0));
 render("tracker_map");check(f.values["botw.tracker.map.valid"]==1);check(f.values["botw.tracker.map.899.kind"]==0);check(!f.values.contains("botw.tracker.map.900.kind"));
 check(!f.texts.contains("botw.tracker.row.0.name")&&f.texts["botw.tracker.title"]=="Koroks"&&f.texts["botw.tracker.summary"]=="300 / 900");
 const auto map_values=f.values;const auto map_texts=f.texts;const auto map_numbers=f.numbers;
 render("tracker_map");check(f.values==map_values&&f.texts==map_texts&&f.numbers==map_numbers&&!f.numbers.empty());
 for(int i=0;i<TrackerView::FamilyCount;++i)check(f.texts["botw.tracker.family."+std::to_string(i)+".summary"].size()<=12);
 check(v.action("tracker_marker",899));render("tracker_map");check(A::id(v)=="Korok899"&&A::index(v)==899);
 render("trackers");check(f.values["botw.tracker.map.valid"]==0&&f.values["botw.tracker.selected.index"]==899);
 // Exploration groups are their own families.
 check(v.action("tracker_family",1));render("tracker_map");
 // Perf round 4: a non-visible tracker-map row is no longer published at
 // all (same discipline as this round's Map-page dynamic_markers fixes). Check absence, not an
 // explicit -1.
 check(A::size(v)==1&&f.values["botw.tracker.map.0.kind"]==1&&!f.values.contains("botw.tracker.map.1.kind"));
 check(f.texts["botw.tracker.title"]=="Shrines");
 check(v.action("tracker_family",2));render("tracker_map");check(f.values["botw.tracker.map.0.kind"]==2&&f.texts["botw.tracker.map.icon"].ends_with("Nt_MapTowerNew_00^t"));
 check(v.action("tracker_family",3));render("tracker_map");check(A::size(v)==1&&f.values["botw.tracker.map.0.kind"]==1&&A::id(v)=="DLC Shrine");check(f.texts["botw.tracker.map.icon"].ends_with("Nt_MapDungeonNew_00^t")&&f.values["botw.tracker.family"]==3);
 check(v.action("tracker_family",4));render("tracker_map");check(f.values["botw.tracker.map.0.kind"]==3);check(f.texts["botw.tracker.map.icon"]=="composite:native-map-stamp-6");
 // Key items: both groups in one list (key items first, then horse gear); no map.
 check(v.action("tracker_family",6));check(A::size(v)==2&&A::id(v)=="Key");check(v.action("tracker_select",1));check(A::id(v)=="Bridle");render("tracker_map");check(f.values["botw.tracker.map.valid"]==0&&!f.values["botw.tracker.map_available"]);
 check(v.action("tracker_family",7));check(A::size(v)==3);
 // Quest progress: one list sorted by group, status prefixed with the group the tab used to name.
 check(v.action("tracker_family",9));render();check(A::size(v)==6&&f.texts["botw.tracker.row.0.status"]=="Main · Known"&&f.texts["botw.tracker.row.4.status"]=="Shrine · Known"&&f.texts["botw.tracker.row.5.status"]=="Side · Known");
 check(v.action("tracker_filter",1));check(A::size(v)==4);check(v.action("tracker_filter",2));check(A::size(v)==1);check(v.action("tracker_filter",0));check(A::size(v)==6);
 // Empty family: a short centred state fact, never a prompt; no detail, no selection.
 check(v.action("tracker_family",5));render();check(A::size(v)==0&&f.values["botw.tracker.count"]==0&&f.values["botw.tracker.list_empty"]==1);
 check(f.texts["botw.tracker.empty_text"]=="Progress unavailable"&&f.texts["botw.tracker.detail"].empty()&&f.values["botw.tracker.selected.index"]==-1);
 check(v.action("tracker_family",1));check(v.action("tracker_filter",2));render();check(f.texts["botw.tracker.empty_text"]=="None complete yet");
 check(v.action("tracker_family",4));check(v.action("tracker_filter",2));render();check(f.texts["botw.tracker.empty_text"]=="None complete yet");
 check(v.action("tracker_family",6));check(v.action("tracker_filter",1));render();check(A::size(v)==1);
 // Long families are bounded: 1200 rows publish the first 900.
 {TrackerData big=d;for(int i=900;i<1200;++i)big.categories[0].push_back(row("Korok"+std::to_string(i),0,0,true));TrackerView bv;A::seed(bv,big,inv);
  TrackerFrameFixture b;b.values.clear();b.texts.clear();bv.sample(b.h,inv,{},1,"trackers");check(A::size(bv)==900&&b.values["botw.tracker.count"]==900&&!b.texts.contains("botw.tracker.row.900.name"));}
 // Iteration 12 item 11: the Quest page has no Progress block; sampling it publishes neither
 // progress nor Trackers page keys.
 {TrackerFrameFixture q;TrackerView pv;A::seed(pv,d,inv);q.values.clear();q.texts.clear();pv.sample(q.h,inv,{},1,"quest");
  check(!q.values.contains("botw.progress.valid")&&!q.texts.contains("botw.progress.0.label"));check(q.values["botw.tracker.valid"]==0);}
 // Armor row pointers are rebuilt immediately after every operation that reallocates the backing vector.
 check(v.action("tracker_family",8));check(A::size(v)==3);check(v.action("tracker_armor_target",1));render();check(f.texts["botw.tracker.detail"].find("plan tier 1")!=std::string::npos);check(f.values["botw.tracker.armor_controls"]==1&&f.texts["botw.tracker.title"]=="Armor upgrades");
 check(v.action("tracker_select",0));check(A::planned(v)==1&&A::missing(v)==0);check(v.action("tracker_select",1));check(A::planned(v)==2&&A::missing(v)==3);
 check(v.action("tracker_shopping",1));check(A::size(v)==1);render();check(f.texts["botw.tracker.row.0.status"].find("Missing 3")!=std::string::npos);check(f.texts["botw.tracker.title"]=="Shopping list");
 check(v.action("tracker_shopping",0));check(v.action("tracker_select",0));check(A::planned(v)==1);check(v.action("tracker_select",2));check(A::planned(v)==1);
 check(v.action("tracker_armor_clear",0));check(A::planned(v)==0);check(v.action("tracker_armor_all",0));check(A::planned(v)==2);
 inv.items[1].actor="Armor_002_Head";A::refresh(v,d,inv);check(A::planned(v)==1);render();check(f.values["botw.tracker.count"]==3&&f.texts.contains("botw.tracker.row.0.name"));
 check(v.action("tracker_shopping",1));check(v.action("tracker_armor_clear",0));render();check(A::size(v)==0&&!f.texts.contains("botw.tracker.row.0.name")&&f.texts["botw.tracker.selected.name"].empty()&&f.texts["botw.tracker.empty_text"]=="No armor planned");
 // An invalid native context clears stale live and unknown catalog rows alike.
 A::seed(v,d,inv);check(v.action("tracker_family",0));render("tracker_map");f.put<uint64_t>(Fixture::U+0x64C30,0);render("tracker_map");
 // Perf round 4: a non-visible tracker-map row is no longer published
 // at all (see the "tracker_family",1 case above); check absence, not an explicit -1.
 check(f.values["botw.tracker.valid"]==0&&f.values["botw.tracker.count"]==0&&!f.values.contains("botw.tracker.map.0.kind")&&f.texts["botw.tracker.selected.name"].empty()&&f.texts["botw.tracker.empty_text"]=="Progress unavailable");
 f.put<uint64_t>(Fixture::U+0x64C30,0x1e09000000ULL);d.valid=false;A::seed(v,d,{});render();f.put<uint64_t>(Fixture::U+0x64C30,0);render();check(f.values["botw.tracker.count"]==0&&A::size(v)==0);
 // Unknown-only filtering must not claim that every item is complete.
 f.put<uint64_t>(Fixture::U+0x64C30,0x1e09000000ULL);
 TrackerData unknown;unknown.valid=true;unknown.categories[0].push_back(row("UnknownOnly",0,-1,true));
 A::seed(v,unknown,inv);check(v.action("tracker_family",0));check(v.action("tracker_filter",1));render();
 check(f.texts["botw.tracker.empty_text"]=="Progress unknown"&&f.texts["botw.tracker.family.0.summary"]=="-");
 check(v.action("tracker_family",8));check(v.action("tracker_filter",2));render();check(A::size(v)==0);
 check(v.action("tracker_filter",1));check(A::size(v)==2);check(f.values["botw.tracker.armor_controls"]==1);
 // Session reset drops plans and pending selection before fresh data is available.
 f.put<uint64_t>(Fixture::U+0x64C30,0x1e09000000ULL);A::seed(v,d,inv);check(v.action("tracker_family",8));check(v.action("tracker_armor_all",0));
 f.values.clear();f.texts.clear();f.numbers.clear();v.sample(f.h,{}, {},2,"ready");check(A::planned(v)==0&&A::size(v)==0&&f.values["botw.tracker.map.valid"]==0);
 check(f.values.size()==3&&f.texts.empty()&&f.numbers.empty());
 f.values.clear();v.sample(f.h,{}, {},2,"inventory");check(f.values.size()==3&&f.values["botw.tracker.valid"]==0);
 std::cout<<checks<<" tracker view assertions passed\n";
}
