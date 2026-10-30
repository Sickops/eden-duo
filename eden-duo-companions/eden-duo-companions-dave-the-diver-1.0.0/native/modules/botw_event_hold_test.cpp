// SPDX-License-Identifier: GPL-3.0-or-later
// Iteration 12 item 5: cutscene/dialogue/warp events keep the current page (last World/Menu
// record) under the dim instead of the "Waiting for game context" screen (botw_event_hold.h).
#include "01007EF00011E000.cpp"
#include "botw_states_fixtures.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <thread>
namespace bf=botw_states_fixture;
static uint8_t hexbyte(const char* s){auto v=[](char c){return c<='9'?c-'0':(c|32)-'a'+10;};return uint8_t(v(s[0])<<4|v(s[1]));}
// Same reconstruction as botw_states_test.cpp: live UI flags, screen slots, map-name strings.
struct LiveFixture:Fixture {
  explicit LiveFixture(const bf::Scenario& s):Fixture(s.flags,s.flags==0x1849000000ULL) {
    for(unsigned i=0;i<99;++i)put<uint64_t>(A+8*i,0);
    for(unsigned k=0;k<s.slot_count;++k){const auto& sl=s.slots[k];
      put<uint64_t>(A+8*sl.index,sl.ptr);put<uint64_t>(sl.ptr+0xD0,M+sl.name_off);
      for(size_t c=0;c<24;++c)put<uint8_t>(M+sl.name_off+c,c<std::strlen(sl.name)?uint8_t(sl.name[c]):0);
      for(unsigned b=0;b<16;++b)put<uint8_t>(sl.ptr+0xF8+b,hexbyte(sl.state16+2*b));
      put<uint64_t>(sl.ptr+0x150,sl.state_ptr?M+sl.state_ptr:0);}
    string(botw::states::MapType,s.map_type_raw);string(botw::states::MapName,s.map_name_raw);
  }
  void string(uint64_t off,const char* raw){
    if(!*raw)return;
    for(unsigned b=0;b<0x38;++b)put<uint8_t>(M+off+b,hexbyte(raw+2*b));
    put<uint64_t>(M+off,M+botw::states::StringVt);put<uint64_t>(M+off+8,M+off+20);
  }
  void flags(uint64_t v){put(U+0x64c30,v);}
  void clear(){values.clear();texts.clear();} // the runtime clears its snapshot every tick
};
static const bf::Scenario& scen(const char* id){for(const auto& s:bf::scenarios)if(!std::strcmp(s.id,id))return s;std::abort();}
// Live keys keep updating during a hold: state keys, and the native layout-fx animation clock
// values (botw_fx_curves.h publishes botw.fx.* every tick from the wall clock).
static bool is_state_key(const std::string& k){return k.rfind("botw.state.",0)==0||k.rfind("botw.fx.",0)==0;}
int main() {
  int cases=0;auto check=[&](bool ok){++cases;if(!ok){std::cerr<<"failure "<<cases<<"\n";std::abort();}};
  using botw::UiContext;
  // --- PublishReplay: forwards every publish, records only while recording, replays in order.
  {
    Fixture f;botw::PublishReplay r;
    const auto* w=r.wrap(&f.h);check(w!=&f.h&&w->userdata==f.h.userdata&&w->read_memory==f.h.read_memory);
    w->publish_i64(w->userdata,"a",1);check(f.values["a"]==1&&r.size()==0);        // not recording: forward only
    r.begin();w->publish_i64(w->userdata,"a",2);w->publish_text(w->userdata,"t","x");w->publish_i64(w->userdata,"a",3);
    check(!r.valid());r.commit();check(r.valid()&&r.size()==3&&!r.recording());
    f.values.clear();f.texts.clear();r.replay(f.h);check(f.values["a"]==3&&f.texts["t"]=="x"); // later publish wins, as in the host map
    r.begin();w->publish_text(w->userdata,"u","y");r.commit();check(r.size()==1);    // begin resets, capacity reused
    f.values.clear();f.texts.clear();r.replay(f.h);check(f.values.empty()&&f.texts.size()==1&&f.texts["u"]=="y");
    r.discard();f.texts.clear();r.replay(f.h);check(f.texts.empty());                  // discarded: replays nothing
    Fixture g;g.h.publish_text=nullptr;botw::PublishReplay r2;const auto* w2=r2.wrap(&g.h);check(w2->publish_text==nullptr);
  }
  // --- event_hold truth table.
  {
    botw::GameStates ev;ev.valid=true;ev.event=true;
    botw::GameStates clear;clear.valid=true;
    botw::GameStates load=ev;load.loading=true;load.event=false;
    botw::GameStates title=clear;title.title=true;
    check(botw::event_hold(UiContext::World,UiContext::Unknown,ev,true));
    check(botw::event_hold(UiContext::Menu,UiContext::Unknown,ev,true));            // memory playback from the menu
    check(!botw::event_hold(UiContext::World,UiContext::Unknown,ev,false));          // nothing recorded yet
    check(!botw::event_hold(UiContext::Title,UiContext::Unknown,ev,true));
    check(!botw::event_hold(UiContext::Transition,UiContext::Unknown,ev,true));
    check(!botw::event_hold(UiContext::Unknown,UiContext::Unknown,ev,true));
    check(!botw::event_hold(UiContext::World,UiContext::World,ev,true));             // World signature: normal path
    check(!botw::event_hold(UiContext::World,UiContext::Transition,load,true));      // loading: own screen
    check(!botw::event_hold(UiContext::World,UiContext::Unknown,load,true,true));
    check(!botw::event_hold(UiContext::World,UiContext::Unknown,title,true,true));
    check(!botw::event_hold(UiContext::World,UiContext::Unknown,botw::GameStates{},true,true)); // torn sample
    check(!botw::event_hold(UiContext::World,UiContext::Unknown,clear,true));        // unknown, no event: old path
    check(botw::event_hold(UiContext::World,UiContext::Unknown,clear,true,true));     // event tail under the dim latch
  }
  // --- Module: World -> event -> World / loading.
  {
    LiveFixture f(scen("a-world-1"));State s;
    sample(&s,&f.h);
    check(s.context==UiContext::World&&s.replay.valid());
    check(f.texts["botw.context"]=="World view"&&f.values["botw.state.unknown"]==0);
    const auto world_values=f.values;const auto world_texts=f.texts;
    // Event starts (0x1E09003800: bit 0x800, not the World signature): page values held.
    f.flags(0x1E09003800ULL);f.clear();s.next={};
    sample(&s,&f.h);
    check(s.event_holding&&s.context==UiContext::World);
    check(f.values["botw.state.unknown"]==0&&f.values["botw.state.event"]==1&&f.values["botw.state.cutscene"]==1);
    check(f.texts["botw.context"]=="World view"&&f.values["botw.state.dim"]==0); // dim enters after 200 ms
    bool same=true;
    for(const auto& [k,v]:world_values)if(!is_state_key(k)&&(!f.values.contains(k)||f.values.at(k)!=v))same=false;
    for(const auto& [k,v]:world_texts)if(!is_state_key(k)&&(!f.texts.contains(k)||f.texts.at(k)!=v))same=false;
    check(same&&f.values.size()==world_values.size());
    // Between reader passes: same record, no reader runs (no memory read at all).
    const auto reads=f.reads;f.clear();f.race=0;
    unsigned memreads=0;auto old=f.h.read_memory;
    static unsigned* counter;counter=&memreads;
    f.h.read_memory=[](void* p,uint64_t a,void* b,size_t n)->EdenDsmodBool{++*counter;auto& fx=*static_cast<Fixture*>(p);for(size_t i=0;i<n;++i)static_cast<uint8_t*>(b)[i]=fx.mem.at(a+i);return true;};
    sample(&s,&f.h);
    check(memreads==0&&f.texts["botw.context"]=="World view"&&f.values["botw.state.event"]==1&&reads==f.reads);
    f.h.read_memory=old;
    // Event held >= 200 ms: the dim latch turns on over the held page.
    std::this_thread::sleep_for(std::chrono::milliseconds(230));
    f.clear();s.next={};sample(&s,&f.h);f.clear();s.next={};sample(&s,&f.h);
    check(s.event_holding&&f.values["botw.state.dim"]==1&&f.values["botw.state.dim_kind"]==2&&f.values["botw.state.unknown"]==0);
    check(f.texts["botw.context"]=="World view");
    // Event tail (0x1E09004000: 0x800 clear, still not World) while the latch is on: still held.
    f.flags(0x1E09004000ULL);f.clear();s.next={};sample(&s,&f.h);
    check(s.event_holding&&f.values["botw.state.unknown"]==0&&f.values["botw.state.event"]==0);
    // Back to World: normal readers run again.
    f.flags(0x1E09000000ULL);f.clear();s.next={};sample(&s,&f.h);
    check(!s.event_holding&&s.context==UiContext::World&&f.values["botw.state.unknown"]==0&&s.replay.valid());
    // Event then loading (warp): loading ends the hold, its own screen, record dropped.
    f.flags(0x1E09003800ULL);f.clear();s.next={};sample(&s,&f.h);check(s.event_holding);
    f.flags(0x0E09E00000ULL);f.clear();s.next={};sample(&s,&f.h);
    check(!s.event_holding&&s.context==UiContext::Transition&&f.values["botw.state.loading"]==1&&!s.replay.valid());
    // Unknown after loading with no fresh World record: old "unknown" behaviour, never a stale page.
    f.flags(0x1E09003800ULL);f.clear();s.next={};sample(&s,&f.h);
    check(!s.event_holding&&f.values["botw.state.unknown"]==1&&f.texts["botw.context"]=="Waiting for game context");
  }
  // --- Menu context (memory playback launched from the Adventure Log): held too.
  {
    LiveFixture f(scen("a-menu-advlog"));State s;sample(&s,&f.h);check(s.context==UiContext::Menu&&s.replay.valid());
    f.flags(0x0E0902382CULL);f.clear();s.next={};sample(&s,&f.h);
    check(s.event_holding&&f.values["botw.state.event"]==1&&f.values["botw.state.unknown"]==0&&f.texts["botw.context"]=="Game menu open");
    check(f.values["botw.state.menu_open"]==0); // not the menu signature: the menu lock does not stack on the dim
  }
  // --- A non-event Unknown signature right after World (no dim latch): unchanged fail-closed path.
  {
    LiveFixture f(scen("a-world-1"));State s;sample(&s,&f.h);
    f.flags(0x1E09000008ULL);f.clear();s.next={};sample(&s,&f.h);
    check(!s.event_holding&&s.context==UiContext::Unknown&&f.values["botw.state.unknown"]==1);
  }
  // --- Fresh process during an event (nothing recorded): unknown screen as before.
  {
    LiveFixture f(scen("n3-msg"));State s;sample(&s,&f.h);
    check(!s.event_holding&&f.values["botw.state.unknown"]==1&&!s.replay.valid());
  }
  std::cout<<cases<<" event-hold replay/truth-table/World-event-World/loading/menu/fresh cases passed\n";
}
