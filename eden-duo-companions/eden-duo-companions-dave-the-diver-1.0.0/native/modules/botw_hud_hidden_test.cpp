// SPDX-License-Identifier: GPL-3.0-or-later
// Iteration 11 item E: hidden companion = native HUD back. tick() mirrors the host's
// __aux_present into mailbox+2048 (0 shown / 1 hidden) on edges only; the guest helper
// (prototype/companion-hud.inc, prototype/tests/hud-hook-test.py) acts on it.
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
struct HudHiddenFixture : Fixture {
  std::array<uint8_t,4096> mailbox{};
  std::vector<std::pair<uint32_t,uint32_t>> stores;
  unsigned loads{},publishes{},aux_queries{};
  int64_t aux=1;
  bool aux_key=true;
  State state;
  EdenDsmodHostExtensions ext{};
  uint32_t get(uint32_t at) const {uint32_t v{};std::memcpy(&v,mailbox.data()+at,4);return v;}
  void mb(uint32_t at,uint32_t v) {std::memcpy(mailbox.data()+at,&v,4);}
  HudHiddenFixture() {
    h.get_i64=[](void* p,const char* name,int64_t fallback)->int64_t{
      auto& f=*static_cast<HudHiddenFixture*>(p);
      if(std::strcmp(name,"__aux_present")==0){++f.aux_queries;return f.aux_key ? f.aux : fallback;}
      return fallback;
    };
    h.publish_i64=[](void* p,const char*,int64_t){++static_cast<HudHiddenFixture*>(p)->publishes;};
    h.publish_text=[](void* p,const char*,const char*){++static_cast<HudHiddenFixture*>(p)->publishes;};
    ext.version=EDEN_DSMOD_EXT_VERSION;ext.struct_size=sizeof(ext);ext.abi_hash=EDEN_DSMOD_EXT_HASH;
    ext.userdata=this;ext.mailbox_size=4096;
    ext.load_u32=[](void* p,uint32_t at,uint32_t* v)->EdenDsmodBool{auto& f=*static_cast<HudHiddenFixture*>(p);assert(at<=4092 && at%4==0);++f.loads;*v=f.get(at);return true;};
    ext.store_u32=[](void* p,uint32_t at,uint32_t v)->EdenDsmodBool{auto& f=*static_cast<HudHiddenFixture*>(p);assert(at<=4092 && at%4==0);f.mb(at,v);f.stores.emplace_back(at,v);return true;};
    ext.store_u64=[](void*,uint32_t,uint64_t)->EdenDsmodBool{return true;};
    state.bridge.configure(&ext);
    stores.clear();
  }
  unsigned hud_stores() const {unsigned n=0;for(auto& s:stores)if(s.first==2048)++n;return n;}
  void step() {tick(&state,&h);}
};
int main() {
  unsigned cases=0;
  {
    // Companion shown from boot: fresh mailbox is already 0 (hide rules), nothing written.
    HudHiddenFixture f;
    for(int i=0;i<5;++i)f.step();
    assert(f.hud_stores()==0 && f.get(2048)==0);++cases;
    // Hidden (lid closed / companion toggle / one screen): exactly one write of 1.
    f.aux=0;f.step();
    assert(f.hud_stores()==1 && f.get(2048)==1);++cases;
    // Hidden ticks stay cheap: no further HUD store, nothing published, one mailbox load each.
    const unsigned loads=f.loads,publishes=f.publishes;
    for(int i=0;i<100;++i)f.step();
    assert(f.hud_stores()==1 && f.publishes==publishes && f.loads==loads+100);++cases;
    // Hidden ticks keep retiring a pending request (existing behaviour).
    bool cancelled=false;
    for(auto& s:f.stores){if(s.first==72)cancelled=true;}
    assert(cancelled);++cases;
    // Re-shown: exactly one write back to 0 (re-apply our hide rules), then steady.
    f.aux=1;f.stores.clear();
    for(int i=0;i<10;++i)f.step();
    assert(f.hud_stores()==1 && f.get(2048)==0);++cases;
    // Rapid toggles: one write per edge, never more.
    f.stores.clear();
    for(int i=0;i<6;++i){f.aux=i%2==0 ? 0 : 1;f.step();f.step();}
    assert(f.hud_stores()==6 && f.get(2048)==0);++cases;
    // A fresh process / re-zeroed mailbox while hidden: re-asserted on the next tick.
    f.aux=0;f.step();f.mb(2048,0);f.stores.clear();f.step();
    assert(f.hud_stores()==1 && f.get(2048)==1);++cases;
    // The guest-owned latch (+2052) is never written by the module.
    for(auto& s:f.stores){assert(s.first!=2052);}
    ++cases;
  }
  {
    // Host without the __aux_present key (older runtime): fallback keeps the companion rules.
    HudHiddenFixture f;f.aux_key=false;
    for(int i=0;i<3;++i)f.step();
    assert(f.hud_stores()==0 && f.get(2048)==0);++cases;
  }
  {
    // Host without get_i64 at all: companion rules, no crash.
    HudHiddenFixture f;f.h.get_i64=nullptr;f.mb(2048,1);
    f.step();
    assert(f.get(2048)==0);++cases;
  }
  {
    // No host extensions (mailbox unavailable): nothing written, no crash.
    HudHiddenFixture f;f.state.bridge.configure(nullptr);f.stores.clear();f.aux=0;
    f.step();f.step();
    assert(f.stores.empty() && f.get(2048)==0);++cases;
  }
  {
    // Boot hidden (lid closed at launch): first tick writes 1.
    HudHiddenFixture f;f.aux=0;f.step();
    assert(f.get(2048)==1 && f.hud_stores()==1);++cases;
  }
  std::cout<<cases<<" hidden-companion native HUD cases passed\n";
}
