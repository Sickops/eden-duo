// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_hold.h"
#include "botw_hold_controller.h"
#include "botw_bridge.h"
#include "botw_test_fixture.h"
#include "core/mods/mod_input_hold.h"
#include <cassert>
#include <iostream>
struct HoldFixture:Fixture {
  static constexpr uint64_t Manager=0x11000000,Apple=0x12000000,Butter=0x12000100;
  static constexpr uint64_t Info=0x13000000,Player=0x14000000,Creator=0x15000000,Carry=0x16000000,
    InfoData=0x17000000,HeapRoot=0x18000000,Heap=0x19000000,CarryLink=0x1A000000,CarryActor=0x1B000000,CarryRequest=0x1C000000;
  std::array<uint8_t,4096> mailbox{};EdenDsmodHostExtensions extensions{};
  template<class T>void mail(unsigned at,T value){std::memcpy(mailbox.data()+at,&value,sizeof value);}
  template<class T>T mail(unsigned at){T value{};std::memcpy(&value,mailbox.data()+at,sizeof value);return value;}
  HoldFixture(){
    put(M+0x2CA6D50,Manager);put(Manager,M+0x2B87780);
    put(Manager+0x68,Apple+8);put(Manager+0x70,Butter+8);put<int32_t>(Manager+0x78,2);put<int32_t>(Manager+0x7C,8);
    for(const auto p:{Apple,Butter}){
      put(p,std::array<uint8_t,0x98>{});put<int32_t>(p+0x18,7);put<int32_t>(p+0x20,3);put<uint8_t>(p+0x25,1);
      put(p+0x28,M+0x2A6FAF0);put(p+0x30,p+0x3C);put<int32_t>(p+0x38,64);
      const char*name=p==Apple?"Item_Fruit_A":"Item_Material_06";
      for(size_t i=0;i<=std::strlen(name);++i)put<char>(p+0x3C+i,name[i]);
    }
    put(Apple+8,Butter+8);put(Apple+16,Manager+0x68);put(Butter+8,Manager+0x68);put(Butter+16,Apple+8);
    put(Manager+0x444A0,std::array<uint8_t,80>{});
    put<uint8_t>(Manager+0x49,1);for(unsigned i=0;i<50;++i)put<uint64_t>(Manager+0x44230+i*8,i==9?Butter:0);
    put(M+0x2CA1140,Info);put(Info,M+0x2B7BE80);put(Info+0x60,Player);put(Player,M+0x2B7BEA8);
    put(M+0x2C9A458,Creator);put(Creator,M+0x2B4D9C8);put(Player+0xB90,M+0x2BE40B0);put(Player+0x23E0,M+0x2BD75A8);
    for(unsigned i=0;i<6;++i){const auto at=Player+(i<3?0xB98+i*24:0x23F0+(i-3)*16);
      put<uint64_t>(at,0);put<uint32_t>(at+8,UINT32_MAX);put<uint8_t>(at+12,0);put<int32_t>(Creator+0xE8+i*0x68,0);}
    put(M+0x2CC4490,InfoData);put(InfoData,M+0x2BCAB60);put(M+0x2CA1B10,HeapRoot);put(HeapRoot+0x28,Heap);put<uint64_t>(Heap,0);
    put(M+0x2C9A2E8,Carry);put(Carry,M+0x2B4D938);put<uint64_t>(Carry+0x300,0);put<uint32_t>(Carry+0x308,UINT32_MAX);
    put<uint8_t>(Carry+0x30C,0);put<uint64_t>(Carry+0x310,0);put<uint32_t>(Carry+0xA78,0);
    texts["__page"]="inventory";values["__aux_present"]=1;values["__mailbox_epoch"]=1;
    h.get_text=[](void*p,const char*k){return static_cast<F*>(p)->texts[k].c_str();};
    h.get_i64=[](void*p,const char*k,int64_t fallback){const auto& values=static_cast<F*>(p)->values;auto i=values.find(k);return i==values.end()?fallback:i->second;};
    extensions.version=EDEN_DSMOD_EXT_VERSION;extensions.struct_size=sizeof(extensions);extensions.abi_hash=EDEN_DSMOD_EXT_HASH;extensions.mailbox_size=4096;extensions.userdata=this;
    extensions.load_u32=[](void*p,uint32_t at,uint32_t*out)->EdenDsmodBool{if(at>4092)return false;*out=static_cast<F*>(p)->mail<uint32_t>(at);return true;};
    extensions.store_u32=[](void*p,uint32_t at,uint32_t value)->EdenDsmodBool{if(at>4092)return false;static_cast<F*>(p)->mail(at,value);return true;};
    extensions.store_u64=[](void*p,uint32_t at,uint64_t value)->EdenDsmodBool{if(at>4088)return false;static_cast<F*>(p)->mail(at,value);return true;};
    mail<uint32_t>(0,0x474F5544);mail<uint32_t>(4,100);mail<uint32_t>(4092,1);
  }
  using F=HoldFixture;
  void slot(unsigned i,uint64_t item,uint8_t a=1,uint8_t b=0){put(Manager+0x444A0+i*16,item);put(Manager+0x444A8+i*16,a);put(Manager+0x444A9+i*16,b);}
  void active_carry(){put(Carry+0x300,CarryLink);put<uint32_t>(Carry+0x308,99);put(Carry+0x310,CarryRequest);put(CarryRequest,M+0x2B4D870);put<uint32_t>(Carry+0xA78,64);
    put<uint32_t>(CarryLink+0x40,99);put(CarryLink+0x48,CarryActor);put(CarryActor,M+0x2B60188);
    put(CarryActor+0x10,CarryActor+0x1C);put<int32_t>(CarryActor+0x18,64);std::array<char,9> name{'C','a','r','r','y','B','o','x',0};put(CarryActor+0x1C,name);
    put<uint32_t>(CarryActor+0x60,99);put<uint32_t>(CarryActor+0x64,0x201);put(CarryActor+0x68,CarryLink);}
  void empty_carry(){put<uint32_t>(Carry+0x308,UINT32_MAX);put<uint64_t>(Carry+0x310,0);put<uint32_t>(Carry+0xA78,0);}
};
int main(){using F=HoldFixture;unsigned cases{};
 {assert(botw::holdable_material({"Item_Fruit_A",7,1,false,F::Apple}));assert(!botw::holdable_material({"Arrow",2,1,false,F::Apple}));
  assert(!botw::holdable_material({"Item_Unknown",7,1,false,F::Apple}));assert(!botw::holdable_material({"Item_Fruit_A",7,0,false,F::Apple}));++cases;}
 {F f;const auto s=botw::held_tray(f.h);assert(s.valid&&!s.count);++cases;}
 {F f;assert(botw::held_carry(f.h).empty());f.active_carry();const auto carry=botw::held_carry(f.h);assert(carry.active()&&carry.actor==F::CarryActor&&carry.request==F::CarryRequest);f.empty_carry();assert(botw::held_carry(f.h).empty());++cases;}
 {F f;f.put(F::Carry+0x310,F::CarryRequest);f.put(F::CarryRequest,F::M+0x2B4D870);
  assert(botw::held_carry(f.h).empty());auto plan=botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h));
  assert(plan.valid&&plan.carry.request==F::CarryRequest);++cases;
  f.put<uint8_t>(F::Carry+0x30C,1);assert(!botw::held_carry(f.h).empty());assert(!botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;
  f.put<uint8_t>(F::Carry+0x30C,0);f.put<uint32_t>(F::Carry+0xA78,64);assert(!botw::held_carry(f.h).empty());assert(!botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;
  f.put<uint32_t>(F::Carry+0xA78,0);f.put<uint64_t>(F::CarryRequest,0);assert(botw::held_carry(f.h).empty());++cases;
 }
 // Live03: root+310 points to recycled storage in fully inactive state.
 {F f;f.put(F::Carry+0x310,F::CarryRequest);f.put(F::CarryRequest,F::M+0x2ACA720);
  assert(botw::held_carry(f.h).empty());assert(botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;
  f.put<uint8_t>(F::Carry+0x30C,1);assert(!botw::held_carry(f.h).valid);++cases;
  f.put<uint8_t>(F::Carry+0x30C,0);f.put<uint32_t>(F::Carry+0xA78,64);assert(!botw::held_carry(f.h).valid);++cases;
 }
 {F f;f.put(F::Carry+0x310,F::CarryRequest); // No mapped bytes at stale target.
  assert(botw::held_carry(f.h).empty());assert(botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;
  f.race=F::Carry+0x310;assert(!botw::held_carry(f.h).valid);++cases;
 }
 {F f;f.put(F::Carry+0x310,F::CarryRequest+1);assert(!botw::held_carry(f.h).valid);++cases;}
 {F f;f.active_carry();f.put(F::CarryRequest,F::M+0x2ACA720);assert(!botw::held_carry(f.h).valid);++cases;}
 {F f;f.active_carry();f.put<uint32_t>(F::CarryActor+0x60,100);assert(!botw::held_carry(f.h).valid);++cases;}
 {F f;f.active_carry();f.put<uint64_t>(F::CarryActor+0x68,0);assert(!botw::held_carry(f.h).valid);++cases;}
 {F f;auto plan=botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h));assert(plan.valid&&plan.column==9&&plan.index==1&&plan.heap==F::Heap);++cases;
  f.put<int32_t>(F::Butter+0x20,0);f.put<uint8_t>(F::Butter+0x25,0);plan=botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h));assert(plan.valid&&plan.index==1);++cases;}
 {F f;f.slot(0,F::Apple,0,0);f.put(F::Carry+0x310,F::CarryRequest);f.put(F::CarryRequest,F::M+0x2B4D870);
  assert(botw::inventory_hold(f.h,0,botw::pouch(f.h),true).valid);assert(!botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;}
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});assert(b.hold_supported&&b.cursor_supported);
  assert(b.request_hold(F::Apple,botw::pouch(f.h)));assert(f.mail<uint32_t>(20)==11&&f.mail<uint64_t>(40)==F::Apple&&f.mail<uint32_t>(960)==9&&f.mail<uint32_t>(964)==1);
  assert(!b.request_hold(F::Apple,botw::pouch(f.h)));++cases;
  f.slot(0,F::Apple);f.put<int32_t>(F::Apple+0x20,2);f.active_carry();f.mail<uint32_t>(12,1);f.mail<uint32_t>(16,1);b.poll(f.h,{});
  assert(!b.pending&&std::string(b.status)=="Holding");++cases;
  assert(b.request_hold(0,botw::pouch(f.h),true));assert(f.mail<uint32_t>(20)==12&&f.mail<uint64_t>(40)==0&&f.mail<uint64_t>(80)==0);
  f.slot(0,0,0);f.put<int32_t>(F::Apple+0x20,3);f.empty_carry();f.mail<uint32_t>(12,2);f.mail<uint32_t>(16,1);b.poll(f.h,{});
  assert(!b.pending&&std::string(b.status)=="Items returned");++cases;
 }
 {F f;f.slot(0,F::Apple);f.slot(1,F::Butter);const auto s=botw::held_tray(f.h);assert(s.valid&&s.count==2&&s.slots[0].actor=="Item_Fruit_A"&&s.slots[1].remaining==3);++cases;}
 {F f;f.put<int32_t>(F::Apple+0x20,0);f.put<uint8_t>(F::Apple+0x25,0);for(unsigned i=0;i<5;++i)f.slot(i,F::Apple,1,i<4);
  assert(botw::pouch(f.h).items.size()==1);const auto s=botw::held_tray(f.h);assert(s.valid&&s.count==5&&s.slots[4].remaining==0&&!s.slots[4].in_inventory);++cases;}
 {F f;f.slot(3,F::Apple);const auto s=botw::held_tray(f.h);assert(s.valid&&s.count==1&&s.slots[3].address==F::Apple);++cases;}
 {F f;auto before=botw::held_tray(f.h);const botw::Item apple{"Item_Fruit_A",7,3,false,F::Apple};
  f.slot(0,F::Apple);f.put<int32_t>(F::Apple+0x20,2);auto after=botw::held_tray(f.h);
  assert(botw::held_add_confirmed(before,after,apple));++cases;
  after.slots[0].flag8=0;assert(!botw::held_add_confirmed(before,after,apple));++cases;
  before=botw::held_tray(f.h);f.slot(0,F::Apple,1,1);f.slot(1,F::Apple);f.put<int32_t>(F::Apple+0x20,1);after=botw::held_tray(f.h);
  assert(botw::held_add_confirmed(before,after,{"Item_Fruit_A",7,2,false,F::Apple}));++cases;
  after.slots[0].remaining=2;assert(!botw::held_add_confirmed(before,after,{"Item_Fruit_A",7,2,false,F::Apple}));++cases;
  before=botw::held_tray(f.h);f.slot(0,0,0);f.slot(1,0,0);f.put<int32_t>(F::Apple+0x20,3);after=botw::held_tray(f.h);
  assert(botw::held_return_confirmed(before,after,botw::pouch(f.h)));++cases;
  f.put<int32_t>(F::Apple+0x20,2);assert(!botw::held_return_confirmed(before,after,botw::pouch(f.h)));++cases;
 }
 {F f;f.mail<uint32_t>(0,0x464F5544);botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});
  assert(!b.hold_supported&&!b.request_hold(F::Apple,botw::pouch(f.h)));++cases;}
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});f.values["__mailbox_epoch"]=2;
  assert(!b.request_hold(F::Apple,botw::pouch(f.h)));++cases;}
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});b.heartbeat_at-=std::chrono::seconds(1);
  assert(!b.request_hold(F::Apple,botw::pouch(f.h)));++cases;}
 {F f;f.active_carry();for(unsigned i=0;i<5;++i)f.slot(i,F::Apple,1,i<4);
  assert(!botw::inventory_hold(f.h,F::Butter,botw::pouch(f.h)).valid);
  assert(botw::inventory_hold(f.h,0,botw::pouch(f.h),true).valid);++cases;}
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});assert(b.request_hold(F::Apple,botw::pouch(f.h)));
  f.slot(0,F::Apple,0,0);f.put<int32_t>(F::Apple+0x20,2);f.mail<uint32_t>(12,1);f.mail<uint32_t>(16,1);
  b.poll(f.h,{});assert(b.pending&&std::string(b.status)=="Preparing to hold");
  assert(!b.request_hold(F::Apple,botw::pouch(f.h)));assert(b.hold_return_can_supersede());
  assert(b.request_hold(0,botw::pouch(f.h),true)&&f.mail<uint32_t>(20)==12);++cases;}
 {F f;f.put<int32_t>(F::Apple+0x20,1);botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});
  assert(b.request_hold(F::Apple,botw::pouch(f.h)));f.slot(0,F::Apple);f.put<int32_t>(F::Apple+0x20,0);f.put<uint8_t>(F::Apple+0x25,0);f.active_carry();
  f.mail<uint32_t>(12,1);f.mail<uint32_t>(16,1);b.poll(f.h,{});assert(!b.pending&&std::string(b.status)=="Holding");
  assert(b.request_hold(0,botw::pouch(f.h),true));f.slot(0,0,0);f.empty_carry();f.put<int32_t>(F::Apple+0x20,1);f.put<uint8_t>(F::Apple+0x25,1);
  f.mail<uint32_t>(12,2);f.mail<uint32_t>(16,1);b.poll(f.h,{});assert(!b.pending&&std::string(b.status)=="Items returned");++cases;}
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});assert(b.request_hold(F::Apple,botw::pouch(f.h)));
  f.slot(0,F::Apple,0,0);f.put<int32_t>(F::Apple+0x20,2);f.mail<uint32_t>(12,1);f.mail<uint32_t>(16,3);b.poll(f.h,{});
  assert(!b.pending&&std::string(b.status)=="Hold not confirmed");assert(!b.request_hold(F::Apple,botw::pouch(f.h)));
  assert(b.request_hold(0,botw::pouch(f.h),true));++cases;}
 // Fix round A, item 2: a repeat of a material after a DIFFERENT one (native mixed holding,
 // tray A,B then A) is a valid request, and the native result (new slot + earlier alias flag9)
 // confirms; the request carries both reservations exactly.
 {F f;f.slot(0,F::Apple);f.slot(1,F::Butter);f.active_carry();
  const auto pouch=botw::pouch(f.h);auto plan=botw::inventory_hold(f.h,F::Apple,pouch);
  assert(plan.valid&&plan.tray.count==2&&plan.item.address==F::Apple);++cases;
  botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});assert(b.request_hold(F::Apple,pouch));
  assert(f.mail<uint32_t>(832)==2&&f.mail<uint64_t>(840)==F::Apple&&f.mail<uint64_t>(864)==F::Butter&&f.mail<uint64_t>(40)==F::Apple);++cases;
  const auto before=botw::held_tray(f.h);
  f.slot(0,F::Apple,1,1);f.slot(2,F::Apple,1,0);f.put<int32_t>(F::Apple+0x20,2);
  const auto after=botw::held_tray(f.h);assert(after.valid&&after.count==3);
  assert(botw::held_add_confirmed(before,after,plan.item));++cases;
  // and a second repeat of B after it (A,B,A then B), then the cap.
  auto plan2=botw::inventory_hold(f.h,F::Butter,botw::pouch(f.h));assert(plan2.valid&&plan2.tray.count==3);
  f.slot(1,F::Butter,1,1);f.slot(3,F::Butter,1,0);f.put<int32_t>(F::Butter+0x20,2);
  assert(botw::held_add_confirmed(plan2.tray,botw::held_tray(f.h),plan2.item));
  f.slot(4,F::Apple,1,0);f.slot(2,F::Apple,1,1);f.put<int32_t>(F::Apple+0x20,1);
  assert(!botw::inventory_hold(f.h,F::Apple,botw::pouch(f.h)).valid);++cases;}
 // Fix round A: refusal diagnostics name the failing gate.
 {F f;botw::SelectionBridge b;b.configure(&f.extensions);b.poll(f.h,{});
  assert(!b.request_hold(0x12345670,botw::pouch(f.h)));assert(b.hold_refusal==(8u<<16));
  b.heartbeat_at-=std::chrono::milliseconds(500);assert(!b.request_hold(F::Apple,botw::pouch(f.h)));assert(b.hold_refusal&256u);
  f.texts["__page"]="ready";b.heartbeat_at=std::chrono::steady_clock::now();assert(!b.request_hold(F::Apple,botw::pouch(f.h)));assert(b.hold_refusal&8u);++cases;}
 auto bad=[&](auto mutate){F f;f.slot(0,F::Apple);mutate(f);assert(!botw::held_tray(f.h).valid);++cases;};
 bad([](F&f){f.h.build_id[0]^=1;});bad([](F&f){f.put<uint64_t>(F::Manager,0);});
 bad([](F&f){f.slot(0,0x15000000);});bad([](F&f){f.slot(0,F::Apple,2);});
 bad([](F&f){f.slot(0,F::Apple,1,2);});bad([](F&f){f.put<int32_t>(F::Apple+0x18,8);});
 bad([](F&f){f.put<int32_t>(F::Apple+0x20,-1);});bad([](F&f){f.put<int32_t>(F::Apple+0x20,1000000);});
 bad([](F&f){f.put<uint8_t>(F::Apple+0x24,1);});bad([](F&f){f.put<uint8_t>(F::Apple+0x25,2);});
 bad([](F&f){f.put<uint64_t>(F::Apple+0x28,0);});bad([](F&f){f.put(F::Apple+0x30,F::Butter+0x3C);});
 bad([](F&f){f.put<int32_t>(F::Apple+0x38,32);});bad([](F&f){f.put<char>(F::Apple+0x3C,'!');});
 bad([](F&f){f.put(F::Butter+8,F::Apple+8);});bad([](F&f){f.put(F::Butter+16,F::Manager+0x68);});
 bad([](F&f){f.put<int32_t>(F::Manager+0x78,421);});bad([](F&f){f.put<uint64_t>(F::U+0x64C30,0);});
 for(const auto at:{F::Manager+0x444A0,F::Manager+0x444A8,F::Manager+0x444A9,F::Apple+0x20,F::Apple+0x38,F::U+0x64C30})bad([&](F&f){f.race=at;});
 // Runtime's actual hold tracker: one native intent per long press, never a lift tap.
 {Core::Mods::HoldTracker h;unsigned actions{};h.Down(0,true,500,1);assert(!h.Update(499,false,1));if(h.Update(500,false,1))++actions;
  if(h.Update(999,false,1))++actions;
  assert(h.Up());assert(actions==1);++cases;}
 for(bool page_change:{false,true}){Core::Mods::HoldTracker h;h.Down(0,true,500,1);assert(!h.Update(300,!page_change,page_change?2:1));assert(!h.Update(600,false,1));assert(!h.Up());++cases;}
 {using Targets=botw::HoldTargets;using namespace std::chrono;Targets targets;const auto t=Targets::Clock::now();
  botw::Snapshot data{true,{{"Item_Fruit_A",7,2,false,F::Apple},{"Item_Material_06",7,2,false,F::Butter}}};
  targets.observe(data,4,1,t);const auto old=targets.cells[0].token;assert(old>0&&targets.target(old,1,t));
  assert(!targets.target(0,1,t)&&!targets.target(-1,1,t));++cases;
  Core::Mods::HoldTracker hold;hold.Down(0,true,500,1,old);
  std::swap(data.items[0],data.items[1]);
  for(int ms:{100,200,300,400,500,600,700})targets.observe(data,4,1,t+milliseconds(ms));
  assert(hold.Update(700,false,1)&&hold.Payload()==old);
  assert(!targets.target(*hold.Payload(),1,t+milliseconds(700)));assert(hold.Up());++cases;
  const auto fresh=targets.cells[0].token;assert(fresh!=old&&targets.target(fresh,1,t+milliseconds(700)));
  assert(!targets.target(fresh,2,t+milliseconds(700)));assert(!targets.target(fresh,1,t+milliseconds(801)));++cases;
  data.items[0].value--;targets.observe(data,4,1,t+milliseconds(750));assert(!targets.target(fresh,1,t+milliseconds(750)));++cases;
  const auto before_epoch=targets.cells[0].token;targets.observe(data,4,2,t+milliseconds(800));assert(!targets.target(before_epoch,2,t+milliseconds(800)));++cases;
  const auto before_exit=targets.cells[0].token;targets.clear();targets.observe(data,4,2,t+milliseconds(850));assert(!targets.target(before_exit,2,t+milliseconds(850)));++cases;
  const auto before_gap=targets.cells[0].token;targets.observe(data,4,2,t+milliseconds(1200));assert(!targets.target(before_gap,2,t+milliseconds(1200)));++cases;
  targets.observe(data,3,2,t+milliseconds(1250));assert(targets.cells.empty());++cases;
 }
 // Owner item 12 (iteration 5, 2026-10-18): the tray shows only while something is actually
 // held or a hold is in flight -- never just because the category being browsed is Materials.
 // A regression that reintroduces a category check would flip the first two lines below.
 assert(!botw::hold_tray_shown(true,true,false,false));++cases; // idle on Materials, nothing held: hidden
 assert(botw::hold_tray_shown(true,true,true,false));++cases;   // actively holding: shown
 assert(botw::hold_tray_shown(true,true,false,true));++cases;   // hold request pending: shown
 assert(!botw::hold_tray_shown(false,true,true,false));++cases; // native tray/carry not valid: hidden
 assert(!botw::hold_tray_shown(true,false,true,false));++cases; // host does not support holding: hidden

 // Item 8d, iteration 8 round 2 (2026-10-18): hold_unpick_keep() -- the compound put-away-then-
 // re-hold sequence's pure "what to replay" helper (botw_hold_controller.h).
 {botw::HeldTray t{};t.valid=true;t.count=3;
  t.slots[0]={0x1000,"Item_Fruit_A",3,1,0,1};t.slots[1]={0x1000,"Item_Fruit_A",2,1,1,1};
  t.slots[2]={0x2000,"Item_Material_06",5,1,0,1};
  // tap slot 1 (one of the two duplicate Item_Fruit_A units): keep = [slot0, slot2], in native
  // slot order, slot 1's own address dropped exactly once even though it duplicates slot 0's.
  auto keep=botw::hold_unpick_keep(t,1);
  assert((keep==std::vector<uint64_t>{0x1000,0x2000}));++cases;
  // tap slot 0 instead: keep = [slot1 (same address, still replayed once), slot2].
  keep=botw::hold_unpick_keep(t,0);
  assert((keep==std::vector<uint64_t>{0x1000,0x2000}));++cases;
  // out-of-range / empty-slot / invalid-tray targets all refuse (empty keep list signals refusal
  // to the caller, same as every other guarded helper in this module).
  assert(botw::hold_unpick_keep(t,5).empty());++cases;
  botw::HeldTray empty_slot=t;empty_slot.slots[3]={};
  assert(botw::hold_unpick_keep(empty_slot,3).empty());++cases;
  botw::HeldTray invalid{};assert(botw::hold_unpick_keep(invalid,0).empty());++cases;
  // single occupied slot -> keep is empty (put-away-all with nothing to replay, same end state
  // as the X cancel for a 1-item tray).
  botw::HeldTray one{};one.valid=true;one.count=1;one.slots[2]={0x3000,"Item_Fruit_A",1,1,0,1};
  assert(botw::hold_unpick_keep(one,2).empty());++cases;}
 // Iteration 12 item 14: the footer hint never shares the band with a status line or the tray.
 assert(botw::hold_hint_visible(true,4,false,false));assert(!botw::hold_hint_visible(true,4,false,true));
 assert(!botw::hold_hint_visible(true,4,true,false));assert(!botw::hold_hint_visible(true,0,false,false));
 assert(!botw::hold_hint_visible(false,4,false,false));cases+=5;
 std::cout<<cases<<" native tray/zero-stock/duplicate/race/gesture cases passed\n";
}
