// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>

struct BridgeFixture : Fixture {
  static constexpr uint64_t Screen=0x7005000,Pouch=0x8000000,One=0x8100000,Two=0x8100100;
  std::array<uint8_t,4096> mailbox{};
  std::vector<uint32_t> stores;
  uint32_t fail=UINT32_MAX,change_epoch_at=UINT32_MAX;
  State state;
  template<class T> T get(uint32_t at) {T v{};std::memcpy(&v,mailbox.data()+at,sizeof(v));return v;}
  template<class T> void mb(uint32_t at,T v) {std::memcpy(mailbox.data()+at,&v,sizeof(v));}
  BridgeFixture() {
    texts["page"]="ready";values["aux"]=1;values["epoch"]=1;mb<uint32_t>(4092,1);
    h.get_text=[](void* p,const char*)->const char*{return static_cast<BridgeFixture*>(p)->texts["page"].c_str();};
    h.get_i64=[](void* p,const char* name,int64_t)->int64_t{auto& f=*static_cast<BridgeFixture*>(p);return f.values[std::strcmp(name,"__mailbox_epoch")==0 ? "epoch" : "aux"];};
    put(M+0x2CA6D50,Pouch);put<uint64_t>(Pouch+0x68,One+8);put<uint64_t>(Pouch+0x70,Two+8);
    put<int32_t>(Pouch+0x78,2);put<int32_t>(Pouch+0x7C,8);
    for (uint64_t address:{One,Two}) {
      put(address,std::array<uint8_t,0x38>{});put<int32_t>(address+0x20,400);
      put<uint8_t>(address+0x24,address==One);put<uint8_t>(address+0x25,1);
      put<uint64_t>(address+0x30,address+0x80);
      const char actor[]="Weapon_Test";for(size_t i=0;i<sizeof(actor);++i)put<char>(address+0x80+i,actor[i]);
    }
    put<uint64_t>(One+8,Two+8);put<uint64_t>(One+16,Pouch+0x68);
    put<uint64_t>(Two+8,Pouch+0x68);put<uint64_t>(Two+16,One+8);
    put(A+360,Screen);put(Screen,M+0x2BA1D40);put(Screen+0x150,M+0x2BA1C10);
    put<int32_t>(Screen+0x3720,0);put<int32_t>(Screen+0x3724,0);
    put<uint32_t>(U+0x64C24,3);put<uint32_t>(U+0x64C58,0x80000);put<uint64_t>(U+0x64C30,0x1E09000004ULL);
    put(Screen+0x37B0,std::array<uint64_t,20>{Two,One});put<int32_t>(Screen+0x3860,1);
    put<int32_t>(Screen+0x3864,1);put<int32_t>(Screen+0x3868,2);put<uint8_t>(Screen+0x38CC,3);
    EdenDsmodHostExtensions ext{};ext.version=EDEN_DSMOD_EXT_VERSION;ext.struct_size=sizeof(ext);
    ext.abi_hash=EDEN_DSMOD_EXT_HASH;ext.userdata=this;ext.mailbox_size=4096;
    ext.load_u32=[](void* p,uint32_t at,uint32_t* v)->EdenDsmodBool{auto& f=*static_cast<BridgeFixture*>(p);assert(at<=4092 && at%4==0);*v=f.get<uint32_t>(at);return true;};
    ext.store_u32=[](void* p,uint32_t at,uint32_t v)->EdenDsmodBool{auto& f=*static_cast<BridgeFixture*>(p);assert(at<=4092 && at%4==0);if(at==f.fail)return false;f.mb(at,v);f.stores.push_back(at);if(at==f.change_epoch_at){f.values["epoch"]++;f.mb(4092,uint32_t(f.values["epoch"]));}return true;};
    ext.store_u64=[](void* p,uint32_t at,uint64_t v)->EdenDsmodBool{auto& f=*static_cast<BridgeFixture*>(p);assert(at<=4088 && at%8==0);if(at==f.fail)return false;f.mb(at,v);f.stores.push_back(at);if(at==f.change_epoch_at){f.values["epoch"]++;f.mb(4092,uint32_t(f.values["epoch"]));}return true;};
    mb<uint32_t>(0,0x334F5544);mb<uint32_t>(4,100);state.bridge.configure(&ext);
    state.bridge.poll(h,botw::native_quick(h));state.quick.armed=true;state.quick.shown=botw::QuickKind::Weapon;
    state.gear=botw::gear(h,botw::pouch(h));assert(state.gear.valid);
  }
  bool choose(int64_t index=0) {return on_action(&state,"select",index);}
};

struct InventoryBridgeFixture:BridgeFixture {
  static constexpr uint64_t Info=0x9000000,Player=0x9010000,Creator=0x9020000,
                            Actor=0x9030000,Link=0x9040000;
  InventoryBridgeFixture() {
    texts["page"]="inventory";put<uint64_t>(U+0x64C30,0x1E09000000ULL);
    put(Pouch,M+0x2B87780);
    for(auto p:{One,Two}) {
      put(p+0x28,M+0x2A6FAF0);put(p+0x30,p+0x3C);put<int32_t>(p+0x38,64);
      std::array<char,64> name{};std::memcpy(name.data(),"Weapon_Test",11);put(p+0x3C,name);
      put<uint32_t>(p+0x80,12);put<uint32_t>(p+0x88,4);
    }
    put(M+0x2CA1140,Info);put(Info,M+0x2B7BE80);put(Info+0x60,Player);
    put(Player,M+0x2B7BEA8);put(M+0x2C9A458,Creator);put(Creator,M+0x2B4D9C8);
    put(Player+0xB90,M+0x2BE40B0);put(Player+0x23E0,M+0x2BD75A8);
    for(unsigned i=0;i<6;++i) {
      auto at=Player+(i<3?0xB98+i*24:0x23F0+(i-3)*16);
      put(at,uint64_t(0));put<uint32_t>(at+8,UINT32_MAX);put<uint8_t>(at+12,0);
      put<int32_t>(Creator+0xE8+i*0x68,0);
    }
    attach(100,"Weapon_Old");
    state.data=botw::pouch(h);assert(state.data.valid);
    state.inventory.selected=Two;mb<uint32_t>(0,0x354F5544);mb<uint32_t>(4,101);
    state.bridge.poll(h,{});assert(state.bridge.inventory_supported);
  }
  void attach(uint32_t id,const char* text,unsigned slot=0) {
    const auto at=Player+(slot<3 ? 0xB98+slot*24 : 0x23F0+(slot-3)*16);
    put(at,Link);put<uint32_t>(at+8,id);
    put<uint32_t>(Link+0x40,id);put(Link+0x48,Actor);
    put(Actor,M+(slot>=3?0x2A6D608:0x2AE2348));put(Actor+0x10,Actor+0x1C);put<int32_t>(Actor+0x18,64);
    put<uint32_t>(Actor+0x60,id);put<uint8_t>(Actor+0x64,1);put(Actor+0x68,Link);
    std::array<char,64> name{};std::memcpy(name.data(),text,std::strlen(text));put(Actor+0x1C,name);
  }
  void lower(int dye=-1) {
    put<uint64_t>(Player+0xB98,0);put<uint32_t>(Player+0xBA0,UINT32_MAX);
    for(auto p:{One,Two}) {put<int32_t>(p+0x18,6);put<int32_t>(p+0x20,dye);}
    attach(100,"Armor_Old_Lower",5);state.data=botw::pouch(h);assert(state.data.valid);
    mb<uint32_t>(0,0x364F5544);mb<uint32_t>(4,102);state.bridge.poll(h,{});
  }
  static constexpr uint64_t Third=One+0x200,InfoData=0x9050000;
  void armor_attach(unsigned slot,uint32_t id,const char* text) {
    const uint64_t at=Player+0x23F0+(slot-3)*16,link=Link+slot*0x100,actor=Actor+slot*0x200;
    put(at,link);put<uint32_t>(at+8,id);put<uint32_t>(link+0x40,id);put(link+0x48,actor);
    put(actor,M+0x2A6D608);put(actor+0x10,actor+0x1C);put<int32_t>(actor+0x18,64);
    put<uint32_t>(actor+0x60,id);put<uint8_t>(actor+0x64,1);put(actor+0x68,link);
    std::array<char,64> name{};std::memcpy(name.data(),text,std::strlen(text));put(actor+0x1C,name);
  }
  void pair(bool head_target=false) {
    put<uint64_t>(Player+0xB98,0);put<uint32_t>(Player+0xBA0,UINT32_MAX);
    put<int32_t>(Pouch+0x78,3);put<uint64_t>(Pouch+0x70,Third+8);
    for(auto p:{One,Two,Third}) {
      put(p,std::array<uint8_t,0x90>{});put<uint8_t>(p+0x24,p!=Two);put<uint8_t>(p+0x25,1);
      put(p+0x28,M+0x2A6FAF0);put(p+0x30,p+0x3C);put<int32_t>(p+0x38,64);
      std::array<char,64> name{};
      const auto nm=p==One ? "Armor_002_Head" : p==Third ? "Armor_007_Upper" : head_target ? "Armor_002_Head" : "Armor_180_Upper";
      std::memcpy(name.data(),nm,std::strlen(nm));put(p+0x3C,name);
      put<int32_t>(p+0x18,p==One || (p==Two && head_target) ? 4 : 5);put<int32_t>(p+0x20,-1);
    }
    put<uint64_t>(Pouch+0x68,One+8);put<uint64_t>(One+8,Two+8);put<uint64_t>(One+16,Pouch+0x68);
    put<uint64_t>(Two+8,Third+8);put<uint64_t>(Two+16,One+8);put<uint64_t>(Third+8,Pouch+0x68);put<uint64_t>(Third+16,Two+8);
    armor_attach(3,100,"Armor_002_Head");armor_attach(4,101,"Armor_007_Upper");
    put(M+0x2CC4490,InfoData);put(InfoData,M+0x2BCAB60);
    state.data=botw::pouch(h);assert(state.data.valid);state.inventory.selected=Two;
    mb<uint32_t>(0,0x374F5544);mb<uint32_t>(4,103);state.bridge.poll(h,{});
  }
  void removable(unsigned slot=0,uint32_t magic=0x394F5544) {
    const int type=slot==0 ? 0 : slot==1 ? 3 : 1;
    for(auto p:{One,Two}) {put<int32_t>(p+0x18,type);put<uint8_t>(p+0x24,p==Two);}
    put<uint64_t>(Player+0xB98,0);put<uint32_t>(Player+0xBA0,UINT32_MAX);
    for(unsigned col=0;col<50;++col)put<uint64_t>(Pouch+0x44230+col*8,0);
    attach(100,"Weapon_Test",slot);put<uint8_t>(Pouch+0x49,1);put<uint64_t>(Pouch+0x44230,Two);
    for(auto [at,name]:{std::pair<uint64_t,uint64_t>{0x2CA68F0,0x265BC1E},{0x2CA6920,0x2640BC8},{0x2CA6900,0x2635EDB}}) {
      put(M+at,M+0x2A6E6C0);put(M+at+8,M+name);
    }
    state.data=botw::pouch(h);assert(state.data.valid);
    mb<uint32_t>(0,magic);mb<uint32_t>(4,105);state.bridge.poll(h,{});
  }
  void arrows(uint32_t magic=0x384F5544) {
    for(auto p:{One,Two}) {put<int32_t>(p+0x18,2);put<int32_t>(p+0x20,42);}
    state.data=botw::pouch(h);assert(state.data.valid);
    mb<uint32_t>(0,magic);mb<uint32_t>(4,104);state.bridge.poll(h,{});
  }
  void armor_removable(unsigned slot,bool fitted=false,uint32_t magic=0x414F5544) {
    if(slot==5){lower(8);put<uint8_t>(One+0x24,0);put<uint8_t>(Two+0x24,1);attach(100,"Weapon_Test",5);}
    else {
      pair(slot==3);put<uint8_t>(Two+0x24,1);put<uint8_t>((slot==3 ? One : Third)+0x24,0);
      if(slot==3) {
        std::array<char,64> nm{};std::memcpy(nm.data(),"Armor_180_Upper",15);put(Third+0x3C,nm);
        armor_attach(4,101,"Armor_180_Upper");
      } else armor_attach(4,101,"Armor_180_Upper");
      armor_attach(3,100,fitted ? "Armor_002_Head_B" : "Armor_002_Head");
    }
    for(unsigned col=0;col<50;++col)put<uint64_t>(Pouch+0x44230+col*8,0);
    put<uint8_t>(Pouch+0x49,1);put<uint64_t>(Pouch+0x44230,Two);
    for(auto [at,name]:{std::pair<uint64_t,uint64_t>{0x2CA6930,0x262092C},{0x2CA6940,0x2667123},{0x2CA6950,0x2661AFE}}) {
      put(M+at,M+0x2A6E6C0);put(M+at+8,M+name);
    }
    state.data=botw::pouch(h);assert(state.data.valid);state.inventory.selected=Two;
    mb<uint32_t>(0,magic);mb<uint32_t>(4,106);state.bridge.poll(h,{});
  }
  static constexpr uint64_t FoodScreen=0x9060000,Column=0x9070000,EatRoot=0x9080000,Preview=0x9090000;
  void food(int value=5,int type=7,const char* actor="Item_Fruit_A") {
    for(auto p:{One,Two}) {
      put(p,std::array<uint8_t,0x98>{});put<int32_t>(p+0x18,type);put<int32_t>(p+0x1C,type==7 ? 10 : 8);
      put<int32_t>(p+0x20,value);put<uint8_t>(p+0x25,1);
      put(p+0x28,M+0x2A6FAF0);put(p+0x30,p+0x3C);put<int32_t>(p+0x38,64);
      std::array<char,64> name{};std::memcpy(name.data(),actor,std::strlen(actor));put(p+0x3C,name);
      put<float>(p+0x8C,-1.0f);
    }
    put<uint64_t>(One+8,Two+8);put<uint64_t>(One+16,Pouch+0x68);
    put<uint64_t>(Two+8,Pouch+0x68);put<uint64_t>(Two+16,One+8);
    for(unsigned col=0;col<50;++col)put<uint64_t>(Pouch+0x44230+col*8,0);
    put<uint8_t>(Pouch+0x49,1);put<uint64_t>(Pouch+0x44230+9*8,Two);
    put(M+0x2CA4A38,EatRoot);put(EatRoot,M+0x2B81128);put(EatRoot+0x48,Preview);put(Preview,M+0x2B60C20);put<int32_t>(Player+0x848,119);
    put(A+46*8,FoodScreen);put(FoodScreen,M+0x2BA7240);put(FoodScreen+0x3610,Pouch);
    put<uint64_t>(FoodScreen+0x3630,0);put<int32_t>(FoodScreen+0x3638,0);put(FoodScreen+0x3A08,Column);
    put(Column,M+0x2B8AEE8);put<int32_t>(Column+0x134,29);put<int32_t>(Column+0xCD0,20);
    put(Column+0xCD8,Column+0x1000);put(Column+0x140,Column+0x1100);put(Column+0x1120,Column+0x1200);
    state.data=botw::pouch(h);assert(state.data.valid);state.inventory.selected=Two;
    mb<uint32_t>(0,0x434F5544);mb<uint32_t>(4,107);state.bridge.poll(h,{});assert(state.bridge.consume_supported);
  }
  bool equip(){return on_action(&state,"inventory_equip",0);}
};
int main() {
 using F=InventoryBridgeFixture;unsigned cases{};
 {F f;assert(f.equip());assert(f.get<uint32_t>(20)==6 && f.get<uint32_t>(8)==1 &&
  f.get<uint64_t>(40)==F::Two && f.stores.back()==8);assert(!f.equip());++cases;}
 auto refuse=[&](auto change){F f;change(f);assert(!f.equip());assert(f.get<uint32_t>(8)==0);++cases;};
 refuse([](F&f){f.texts["page"]="ready";});refuse([](F&f){f.values["aux"]=0;});
 refuse([](F&f){f.values["epoch"]=2;});refuse([](F&f){f.state.bridge.healthy=false;});
 refuse([](F&f){f.state.bridge.inventory_supported=false;});
 refuse([](F&f){f.state.bridge.sampled-=std::chrono::seconds(1);});
 refuse([](F&f){f.state.bridge.heartbeat_at-=std::chrono::seconds(1);});
 refuse([](F&f){f.state.inventory.selected=F::One;});
 refuse([](F&f){f.put<uint8_t>(F::Two+0x25,0);});
 refuse([](F&f){f.put<int32_t>(F::Two+0x20,399);});
 refuse([](F&f){f.state.data.items[1].actor="Different";});
 for(int t:{2,4,5,6,7,8,9})refuse([&](F&f){f.put<int32_t>(F::Two+0x18,t);f.state.data.items[1].type=t;});
 for(unsigned i=0;i<6;++i)refuse([&](F&f){f.put<int32_t>(F::Creator+0xE8+i*0x68,1);});
 for(uint64_t at:{F::Pouch,F::Two+0x28,F::M+0x2CA1140,F::Player,F::Creator})
  refuse([&](F&f){f.put<uint64_t>(at,0);});
 for(uint32_t at:{16,20,24,28,32,40,48,56,64,76,80,88,96,104,112,120,124,128,132,136,144,72,8})
  refuse([&](F&f){f.fail=at;});
 refuse([](F&f){f.change_epoch_at=88;});
 {F f;assert(f.equip());f.put<uint8_t>(F::Two+0x24,1);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);
  f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipping");
  f.attach(101,"Weapon_Test");f.state.bridge.poll(f.h,{});assert(!f.state.bridge.pending &&
  std::string(f.state.bridge.status)=="Equipped" && f.get<uint32_t>(72)==0);++cases;}
 {F f;assert(f.equip());f.put<uint8_t>(F::Two+0x24,1);f.attach(100,"Weapon_Test");
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  f.state.bridge.requested-=std::chrono::seconds(4);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equip not confirmed");++cases;}
 {F f;assert(f.equip());f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,2);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equip refused");++cases;}
 {F f;assert(f.equip());f.values["epoch"]=2;f.mb<uint32_t>(4092,2);f.state.bridge.poll(f.h,{});
  assert(f.get<uint32_t>(72)==0);++cases;}
 for(int dye:{-1,0,8,15}) {F f;f.lower(dye);assert(f.equip());
  assert(f.get<uint32_t>(24)==5 && f.get<uint32_t>(28)==6 && f.get<uint32_t>(76)==uint32_t(dye));
  f.put<uint8_t>(F::Two+0x24,1);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);
  f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  f.attach(101,"Weapon_Test",5);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipped");++cases;}
 for(int dye:{-2,16,400}) {F f;f.lower(dye);assert(!f.equip());++cases;}
 {F f;f.lower();f.mb<uint32_t>(0,0x354F5544);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.lower_armor_supported && !f.equip());++cases;}
 {F f;f.pair();assert(f.equip());assert(f.get<uint32_t>(24)==4 && f.get<uint32_t>(248)==1 && f.get<uint32_t>(252)==1);
  f.put<uint8_t>(F::Two+0x24,1);f.put<uint8_t>(F::Third+0x24,0);
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.armor_attach(4,102,"Armor_180_Upper");
  f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  f.armor_attach(3,103,"Armor_002_Head_B");f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipped");++cases;}
 {F f;f.pair(true);assert(f.equip());assert(f.get<uint32_t>(24)==3 && f.get<uint32_t>(248)==0);
  f.put<uint8_t>(F::Two+0x24,1);f.put<uint8_t>(F::One+0x24,0);f.armor_attach(3,102,"Armor_002_Head");
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipped");++cases;}
 {F f;f.pair();f.state.bridge.armor_supported=false;assert(!f.equip());++cases;}
 for(uint32_t at:{224,232,240,244,248,252,256,264,268,272,280,288,292,296}) {
  F f;f.pair();f.fail=at;assert(!f.equip());assert(f.get<uint32_t>(8)==0);++cases;}
 for(uint32_t magic:{0x354F5544u,0x364F5544u,0x374F5544u}) {
  F f;f.arrows(magic);assert(!f.state.bridge.arrows_supported && !f.equip());++cases;}
 {F f;f.arrows();assert(f.equip());assert(f.get<uint32_t>(24)==6 && f.get<uint32_t>(28)==2);
  f.put<uint8_t>(F::One+0x24,0);f.put<uint8_t>(F::Two+0x24,1);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);
  f.state.bridge.poll(f.h,{});assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipped");++cases;}
 for(bool duplicate:{false,true}) {F f;f.arrows();assert(f.equip());f.put<uint8_t>(F::Two+0x24,1);
  if(!duplicate){f.put<uint8_t>(F::One+0x24,0);f.attach(101,"Weapon_Changed");}
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  f.state.bridge.requested-=std::chrono::seconds(4);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equip not confirmed");++cases;}
 for(int value:{0,-1}) {F f;f.arrows();f.put<int32_t>(F::Two+0x20,value);f.state.data=botw::pouch(f.h);assert(!f.equip());++cases;}
 {F f;f.removable();f.put<uint8_t>(F::One+0x24,1);f.put<uint8_t>(F::Two+0x24,0);
  f.put<uint64_t>(F::Pouch+0x44230,0);f.put<uint64_t>(F::Pouch+0x44230+9*8,F::Two);
  f.state.inventory.selected=F::One;f.state.data=botw::pouch(f.h);
  assert(on_action(&f.state,"inventory_remove",0));assert(f.get<uint32_t>(304)==9 && f.get<uint32_t>(308)==1);++cases;}
 for(unsigned slot=0;slot<3;++slot) {F f;f.removable(slot);assert(on_action(&f.state,"inventory_remove",0));
  assert(f.get<uint32_t>(20)==7 && f.get<uint32_t>(24)==slot && f.get<uint32_t>(304)==0 && f.get<uint32_t>(308)==0);
  f.put<uint8_t>(F::Two+0x24,0);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  auto at=F::Player+(slot==0 ? 0xB98 : slot==1 ? 0xBB0 : 0xBC8);f.put<uint32_t>(at+8,UINT32_MAX);f.put<uint64_t>(at,0);
  f.state.bridge.poll(f.h,{});assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Removed");++cases;}
 for(uint32_t magic:{0x354F5544u,0x364F5544u,0x374F5544u,0x384F5544u}) {
  F f;f.removable(0,magic);assert(!on_action(&f.state,"inventory_remove",0));++cases;}
 for(uint64_t at:{F::Pouch+0x49,F::Pouch+0x44230,F::M+0x2CA68F0,F::M+0x2CA68F8}) {
  F f;f.removable();f.put<uint64_t>(at,0);assert(!on_action(&f.state,"inventory_remove",0));++cases;}
 for(uint32_t at:{304,308}) {F f;f.removable();f.fail=at;assert(!on_action(&f.state,"inventory_remove",0));assert(f.get<uint32_t>(8)==0);++cases;}
 {F f;f.removable();f.attach(100,"Other_Actor");assert(!on_action(&f.state,"inventory_remove",0));++cases;}
 for(unsigned slot:{3u,4u,5u})for(bool fitted:{false,true}) {
  F f;f.armor_removable(slot,fitted);assert(on_action(&f.state,"inventory_remove",0));
  assert(f.get<uint32_t>(20)==7 && f.get<uint32_t>(24)==slot);
  f.put<uint8_t>(F::Two+0x24,0);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);
  f.armor_attach(slot,102,slot==3 ? "Armor_Default_Head" : slot==4 ? "Armor_Default_Upper" : "Armor_Default_Lower");
  if(slot==4 && fitted){f.state.bridge.poll(f.h,{});assert(f.state.bridge.pending);f.armor_attach(3,103,"Armor_002_Head");}
  f.state.bridge.poll(f.h,{});assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Removed");++cases;
 }
 {F f;f.armor_removable(4);f.put<uint8_t>(F::One+0x24,0);f.armor_attach(3,100,"Armor_Default_Head");
  f.state.data=botw::pouch(f.h);assert(on_action(&f.state,"inventory_remove",0));++cases;}
 {F f;f.armor_removable(4);f.put<uint8_t>(F::Third+0x24,1);
  f.state.data=botw::pouch(f.h);assert(!on_action(&f.state,"inventory_remove",0));assert(f.get<uint32_t>(8)==0);++cases;}
 for(unsigned slot:{3u,4u,5u}) {F f;f.armor_removable(slot,false,0x394F5544);assert(!on_action(&f.state,"inventory_remove",0));++cases;}
 for(uint32_t at:{224,232,240,244,248,252,256,264,268,272,280,288,292,296,304,308,320}) {
  F f;f.armor_removable(4,true);f.fail=at;assert(!on_action(&f.state,"inventory_remove",0));assert(f.get<uint32_t>(8)==0);++cases;}
 for(uint32_t job_result:{0u,1u,2u}) {F f;f.food(5);assert(on_action(&f.state,"inventory_consume",0));
  f.put<int32_t>(F::Two+0x20,4);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);
  f.mb<uint32_t>(416,1);f.mb<uint32_t>(420,job_result);f.state.bridge.poll(f.h,{});
  assert(f.state.bridge.pending && std::string(f.state.bridge.status)=="Eating");
  f.mb<uint32_t>(416,0);f.mb<uint32_t>(420,job_result);f.state.bridge.poll(f.h,{});
  if(job_result==1)assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Consumed");
  else {assert(f.state.bridge.pending);f.state.bridge.requested-=std::chrono::seconds(4);f.state.bridge.poll(f.h,{});
   assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Eat not confirmed");}
  ++cases;}
 for(int value:{1,5}) {F f;f.food(value);assert(on_action(&f.state,"inventory_consume",0));
  assert(f.get<uint32_t>(20)==8 && f.get<uint32_t>(24)==7 && f.get<uint32_t>(304)==9 && f.get<uint32_t>(308)==0 && f.stores.back()==8);
  assert(f.get<uint64_t>(368)==F::FoodScreen && f.get<uint64_t>(376)==F::Column);
  f.put<int32_t>(F::Two+0x20,value-1);f.put<uint8_t>(F::Two+0x25,value>1);
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.mb<uint32_t>(420,1);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Consumed");++cases;}
 {F f;f.food(1,8,"Item_Cook_B_01");f.put<float>(F::Two+0x8C,13);f.put<float>(F::Two+0x90,1);f.put<int32_t>(F::Two+0x84,120);
  assert(on_action(&f.state,"inventory_consume",0));assert(f.get<uint32_t>(336)==120 && f.get<uint32_t>(340)==0x41500000);++cases;}
 for(uint32_t at:{16,20,24,28,32,40,48,56,64,76,80,88,96,104,112,124,128,132,304,308,336,340,344,348,368,376,384,392,72,8}) {
  F f;f.food();f.fail=at;assert(!on_action(&f.state,"inventory_consume",0));assert(f.get<uint32_t>(8)==0 && f.get<uint32_t>(72)==0);++cases;}
 for(uint32_t at:{336,368,72}) {F f;f.food();f.change_epoch_at=at;
  assert(!on_action(&f.state,"inventory_consume",0));assert(f.get<uint32_t>(8)==0 && f.get<uint32_t>(72)==0);++cases;}
 for(uint32_t magic:{0x334F5544u,0x374F5544u,0x394F5544u,0x414F5544u}) {F f;f.food();f.mb<uint32_t>(0,magic);f.state.bridge.poll(f.h,{});
  assert(!on_action(&f.state,"inventory_consume",0));++cases;}
 for(auto [at,value]:{std::pair<uint64_t,uint64_t>{F::Player+0x848,0},{F::FoodScreen+0x3630,F::Two},
  {F::FoodScreen+0x3638,29},{F::Column+0x134,9},{F::Column+0xCD0,19},
  {F::Pouch+0x49,0},{F::Two+0x1C,8},{F::Two+0x20,6},{F::Two+0x24,1}}) {
  F f;f.food();if(at==F::FoodScreen+0x3630)f.put(at,value);else if(at==F::Pouch+0x49 || at==F::Two+0x24)f.put<uint8_t>(at,value);else f.put<uint32_t>(at,value);
  assert(!on_action(&f.state,"inventory_consume",0));assert(f.get<uint32_t>(8)==0);++cases;}
 for(auto at:{F::EatRoot,F::EatRoot+0x48,F::Preview,F::FoodScreen,F::FoodScreen+0x3610,F::FoodScreen+0x3A08,F::Column,F::Column+0xCD8,F::Column+0x140,F::Column+0x1120}) {
  F f;f.food();f.put<uint64_t>(at,0);assert(!on_action(&f.state,"inventory_consume",0));++cases;}
 for(int result:{1,2,3}) {F f;f.food();assert(on_action(&f.state,"inventory_consume",0));
  f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,result);f.state.bridge.requested-=std::chrono::seconds(4);f.state.bridge.poll(f.h,{});
  assert(!f.state.bridge.pending && std::string(f.state.bridge.status)==(result==2 ? "Eat refused" : "Eat not confirmed"));++cases;}
 {F f;f.food();f.state.inventory.selected=F::One;assert(on_action(&f.state,"inventory_consume",0));
  assert(f.get<uint64_t>(40)==F::One && f.get<uint32_t>(308)==1);++cases;}
 {F f;f.food();f.put<uint8_t>(F::Two+0x25,0);f.put<int32_t>(F::Two+0x20,0);
  f.state.data=botw::pouch(f.h);f.state.inventory.selected=F::One;
  assert(on_action(&f.state,"inventory_consume",0));assert(f.get<uint64_t>(40)==F::One && f.get<uint32_t>(308)==1);++cases;}
 auto ready=[](F& f) {
  f.texts["page"]="ready";f.put<uint32_t>(F::U+0x64C24,0);f.put<uint32_t>(F::U+0x64C58,0);f.put(F::Screen+0x150,F::M+0x2BA1B50);
  f.state.bridge.poll(f.h,{});
 };
 auto inspect_confirm=[](F& f,int index){
  assert(on_action(&f.state,"select",index));
  assert(!f.state.bridge.pending&&!f.state.touch_select_deferred&&!f.get<uint32_t>(8));
  assert(on_action(&f.state,"select",index)); // Inspection is repeatable.
  return on_action(&f.state,"touch_confirm",0);
 };
 for(int corner:{0,1,2}) {F f;ready(f);const int type=corner==0?0:corner==1?3:1;
  for(auto p:{F::One,F::Two})f.put<int32_t>(p+0x18,type);
  f.state.data=botw::pouch(f.h);assert(on_action(&f.state,"quick_help",corner));sample(&f.state,&f.h);
  // Iteration 8 item 1: Weapon/Bow/Shield (all three corners here) get a synthetic None entry at
  // index 0, shifting the two real items (ranked One, Two) to indices 1 and 2.
  assert(f.state.touch_gear.valid && f.state.touch_gear.has_none && f.state.touch_gear.items.size()==3 &&
    f.state.touch_gear.items[0].address==0 && f.state.touch_gear.items[1].address==F::One &&
    f.state.touch_gear.items[2].address==F::Two);
  assert(inspect_confirm(f,2));sample(&f.state,&f.h);assert(f.state.bridge.pending_inventory && f.state.bridge.pending_ready_equip && f.get<uint64_t>(40)==F::Two);
  assert(f.state.touch_kind==botw::QuickKind::None);++cases;}
 {F f;ready(f);assert(!f.state.bridge.request_inventory(F::Two,f.state.data));assert(!f.get<uint32_t>(8));++cases;}
 // Iteration 8 item 1: the None cell's unequip, end to end, for all three Weapon/Bow/Shield
 // corners. Reuses INVENTORY-REMOVE.md's existing proven native route (DUO9 kind7) through
 // request_inventory(remove=true, from_ready=true) -- a real bridge fix was needed here (found
 // live): request_inventory previously hard-refused remove whenever from_ready was true (a
 // companion-side routing restriction predating the None cell, not a native/runtime one),
 // because Ready-origin requests used to be equip-only. Extended symmetrically to allow remove
 // for the same 3 gear types (0/1/3) the Inventory page's own Remove button already allows.
 for(int corner:{0,1,2}) {F f;ready(f);
  // removable() sets up the full native identity inventory_equip()'s remove path needs
  // (SafeString pointers, equipment-manager recursive-lock byte, attached actor name match --
  // INVENTORY-REMOVE.md's own evidence trail) with Two as the sole equipped instance; corner's
  // Weapon/Shield/Bow order matches removable()'s own slot->type mapping exactly.
  f.removable(unsigned(corner));
  assert(on_action(&f.state,"quick_help",corner));sample(&f.state,&f.h);
  assert(f.state.touch_gear.has_none && f.state.touch_gear.items[0].address==0);
  assert(inspect_confirm(f,0));sample(&f.state,&f.h);
  assert(f.state.bridge.pending_inventory && f.get<uint32_t>(20)==7 && f.get<uint64_t>(40)==F::Two);
  assert(f.state.touch_kind==botw::QuickKind::None);++cases;}
 // Nothing equipped of this type -- confirming None is a no-op close, not a native request.
 // (BridgeFixture's constructor defaults One to equipped=1; clear it so neither item of this
 // type is equipped.)
 for(int corner:{0,1,2}) {F f;ready(f);const int type=corner==0?0:corner==1?3:1;
  for(auto p:{F::One,F::Two})f.put<int32_t>(p+0x18,type);
  f.put<uint8_t>(F::One+0x24,0);
  f.state.data=botw::pouch(f.h);assert(on_action(&f.state,"quick_help",corner));sample(&f.state,&f.h);
  assert(inspect_confirm(f,0));assert(!f.state.bridge.pending && !f.get<uint32_t>(8));
  assert(f.state.touch_kind==botw::QuickKind::None);++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(on_action(&f.state,"quick_close",0));assert(!f.state.bridge.pending && f.state.touch_kind==botw::QuickKind::None);++cases;}
 // Owner decision 2026-10-19: the D-pad HELD wheel (never opened via quick_help -- it mirrors the
 // native wheel directly) also gets the None cell as its first entry on Weapon/Bow/Shield.
 // Unlike touch, the held wheel has no confirm button: releasing the physical direction commits
 // whatever the companion last tracked, and that commit has no separate observable signal in this
 // codebase -- only the native wheel's own open -> closed transition is (sample()'s own capture of
 // the previous tick's s.gear/s.held_picker_open, read right before this tick overwrites them).
 // Weapon only here (this fixture's native category selector, F::U+0x64C58, is fixed to the
 // Weapon bits by the base BridgeFixture constructor; Bow/Shield reuse identical module logic,
 // already exercised by item 1's touch-side corner loop above).
 // Native -1 is mapped to cell zero; validate the exact native map before queueing.
 for(int bad=0;bad<5;++bad) {F f;f.removable(0,0x464F5544);f.texts["page"]="ready";
  f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);
  constexpr uint64_t ctl=0x8500000;
  f.put(F::Screen+0x3790,ctl);f.put(ctl,F::M+0x2B858B0);
  f.put<uint32_t>(ctl+0x2AC,3);f.put<uint32_t>(ctl+0x2DC,2);
  f.put(F::Screen+0x386C,std::array<int32_t,3>{-1,0,1});
  if(bad==1)f.put<int32_t>(F::Screen+0x386C,0);
  if(bad==2)f.put<int32_t>(F::Screen+0x3870,-1);
  if(bad==3)f.put<uint32_t>(ctl+0x2DC,3);
  if(bad==4)f.put<uint64_t>(ctl,0);
  sample(&f.state,&f.h);
  assert(f.state.held_picker_open&&f.state.gear.has_none);
  assert(on_action(&f.state,"select",0)==(bad==0));
  assert(!f.state.held_focus_none);
  if(!bad){assert(f.get<int32_t>(24)==-1&&f.get<uint64_t>(40)==0&&f.get<uint32_t>(8)==1);
   f.put<int32_t>(F::Screen+0x3864,-1);f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);
   sample(&f.state,&f.h);assert(f.state.gear.highlighted==0&&!f.state.bridge.pending);
  }else assert(f.get<uint32_t>(8)==0);
  ++cases;
 }
 // A real item is still selectable with the None cell present -- argument is shifted by one
 // (index 0 is None) and the native cursor-move request() must see the UN-augmented snapshot
 // (its own anti-replay check re-reads the native array and compares it item-for-item).
 {F f;f.removable(0,0x464F5544);f.texts["page"]="ready";f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);sample(&f.state,&f.h);
  assert(f.state.gear.has_none && f.state.gear.items[1].address==F::Two && f.state.gear.items[2].address==F::One);
  assert(on_action(&f.state,"select",1));assert(!f.state.held_focus_none);
  assert(f.get<uint64_t>(40)==F::Two && f.get<uint32_t>(8)==1);++cases;}
 {F f;f.removable(0,0x464F5544);f.texts["page"]="ready";f.put<uint64_t>(F::U+0x64C30,0x1E09000004ULL);sample(&f.state,&f.h);
  assert(on_action(&f.state,"select",2));assert(!f.state.held_focus_none);
  assert(f.get<uint64_t>(40)==F::One);++cases;}
 // Iteration 8 item 1: ready(f)'s default pouch has 2 real Weapon-type items, plus the
 // synthetic None entry inserted at index 0 -- valid indices are now 0..2, not 0..1.
 for(int64_t i:{-1,3,20}) {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(!on_action(&f.state,"select",i));assert(!f.get<uint32_t>(8));++cases;}
 for(const char* page:{"inventory","map","quest","unknown"}) {F f;ready(f);f.texts["page"]=page;assert(!on_action(&f.state,"quick_help",0));assert(!f.get<uint32_t>(8));++cases;}
 for(int reason=0;reason<5;++reason) {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);
  if(reason==0)f.texts["page"]="map";
  if(reason==1)f.values["aux"]=0;
  if(reason==2){f.values["epoch"]=2;f.mb<uint32_t>(4092,2);}
  if(reason==3)f.state.last_sample-=std::chrono::seconds(1);
  if(reason==4)f.put<uint64_t>(F::U+0x64C30,0x1849000000ULL);
  sample(&f.state,&f.h);assert(f.state.touch_kind==botw::QuickKind::None && !f.state.touch_gear.valid && !f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(on_action(&f.state,"select",0));assert(!f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(inspect_confirm(f,2));sample(&f.state,&f.h);f.texts["page"]="inventory";f.state.bridge.poll(f.h,{});assert(!f.get<uint32_t>(72));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(inspect_confirm(f,2));sample(&f.state,&f.h);f.put<uint8_t>(F::One+0x24,0);f.put<uint8_t>(F::Two+0x24,1);f.attach(101,"Weapon_Test");f.mb<uint32_t>(12,1);f.mb<uint32_t>(16,1);f.state.bridge.poll(f.h,{});assert(!f.state.bridge.pending && std::string(f.state.bridge.status)=="Equipped");++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);f.put<uint32_t>(F::U+0x64C58,0x80000);assert(!on_action(&f.state,"select",1));sample(&f.state,&f.h);assert(f.state.touch_kind==botw::QuickKind::None && !f.state.quick.armed && !f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);f.put<int32_t>(F::Two+0x20,399);assert(inspect_confirm(f,2));sample(&f.state,&f.h);assert(!f.state.touch_select_deferred && !f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(inspect_confirm(f,2));f.state.bridge.heartbeat_at-=std::chrono::milliseconds(150);sample(&f.state,&f.h);assert(f.state.touch_select_deferred && !f.state.bridge.pending);f.mb<uint32_t>(4,102);sample(&f.state,&f.h);assert(!f.state.touch_select_deferred && f.state.bridge.pending);++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(inspect_confirm(f,2));f.state.touch_action_at-=std::chrono::seconds(1);sample(&f.state,&f.h);assert(!f.state.touch_select_deferred && !f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(inspect_confirm(f,2));assert(on_action(&f.state,"quick_close",0));sample(&f.state,&f.h);assert(!f.state.touch_select_deferred && !f.get<uint32_t>(8));++cases;}
 // Touch Arrow uses the ordinary ready inventory equip path; held Arrow stays cursor-only.
 auto ready_arrow=[&](F& f) {f.arrows();ready(f);sample(&f.state,&f.h);};
 {F f;ready_arrow(f);assert(on_action(&f.state,"quick_help",4));sample(&f.state,&f.h);
  assert(f.state.touch_kind==botw::QuickKind::Arrow && f.state.quick.shown==botw::QuickKind::Arrow);
  assert(f.state.touch_gear.valid && f.state.touch_gear.items.size()==2 && f.texts["botw.quick.title"]=="Arrows");
  assert(f.values["botw.quick.touch_gear"]==1 && f.values["botw.quick.held_gear"]==0);
  assert(botw::item_quantity(f.state.touch_gear.items[0])==42 && f.values["botw.quick.item.0.durability_pct"]==-1);
  assert(on_action(&f.state,"select",1) && on_action(&f.state,"select",1));assert(!f.state.bridge.pending&&!f.state.touch_select_deferred&&!f.get<uint32_t>(8));assert(on_action(&f.state,"touch_confirm",0));assert(!on_action(&f.state,"touch_confirm",0));f.mb<uint32_t>(4,105);sample(&f.state,&f.h);
  assert(f.state.bridge.pending_inventory && f.state.bridge.pending_ready_equip && f.get<uint32_t>(24)==6 && f.get<uint64_t>(40)==F::Two);
  assert(f.state.touch_kind==botw::QuickKind::None);++cases;}
 {F f;ready_arrow(f);assert(on_action(&f.state,"quick_help",4));sample(&f.state,&f.h);
  assert(on_action(&f.state,"select",0));assert(f.state.touch_kind==botw::QuickKind::Arrow&&!f.state.touch_select_deferred&&!f.get<uint32_t>(8));assert(on_action(&f.state,"touch_confirm",0));assert(f.state.touch_kind==botw::QuickKind::None && !f.get<uint32_t>(8));++cases;}
 {F f;ready_arrow(f);
  // Reverse native pouch order against address order; Arrow must never condition-sort it.
  f.put<uint64_t>(F::Pouch+0x68,F::Two+8);f.put<uint64_t>(F::Pouch+0x70,F::One+8);
  f.put<uint64_t>(F::Two+8,F::One+8);f.put<uint64_t>(F::Two+16,F::Pouch+0x68);
  f.put<uint64_t>(F::One+8,F::Pouch+0x68);f.put<uint64_t>(F::One+16,F::Two+8);
  f.state.data=botw::pouch(f.h);assert(f.state.data.valid && f.state.data.items[0].address==F::Two);
  assert(on_action(&f.state,"quick_help",4));sample(&f.state,&f.h);
  assert(f.state.touch_gear.items[0].address==F::Two && f.state.touch_gear.items[1].address==F::One);++cases;}
 {F f;ready_arrow(f);assert(on_action(&f.state,"quick_help",4));
  auto item=f.state.data.items.back();for(unsigned i=2;i<7;++i){item.address+=0x100;f.state.data.items.push_back(item);}
  f.state.next=std::chrono::steady_clock::now()+std::chrono::seconds(5);sample(&f.state,&f.h);
  assert(!f.state.touch_gear.valid && f.state.quick.shown==botw::QuickKind::None && !on_action(&f.state,"select",0));++cases;}
 for(int reason=0;reason<6;++reason) {F f;ready_arrow(f);
  if(reason==0)f.state.bridge.arrows_supported=false;
  if(reason==1)f.texts["page"]="inventory";
  if(reason==2)f.values["aux"]=0;
  if(reason==3)f.put<uint64_t>(F::U+0x64C30,0x1849000000ULL);
  if(reason==4)f.put<uint32_t>(F::U+0x64C58,0x40000);
  if(reason==5)f.state.bridge.healthy=false;
  assert(!on_action(&f.state,"quick_help",4) && !f.get<uint32_t>(8));++cases;}
 for(int reason=0;reason<6;++reason){F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);
  assert(on_action(&f.state,"select",1));assert(!f.state.touch_select_deferred&&!f.get<uint32_t>(8));
  if(reason==0)f.texts["page"]="map";
  if(reason==1)f.values["aux"]=0;
  if(reason==2){f.values["epoch"]=2;f.mb<uint32_t>(4092,2);}
  if(reason==3)f.state.touch_epoch=0;
  if(reason==4)f.put<uint32_t>(F::U+0x64C58,0x80000);
  if(reason==5)assert(on_action(&f.state,"quick_close",0));
  assert(!on_action(&f.state,"touch_confirm",0));assert(!f.get<uint32_t>(8));++cases;
 }
 // Pagination ("visible_select"/"quick_page") was removed 2026-10-16 (owner: scroll, not
 // pages, in the picker popups). The grid now binds every cell straight to "select" with a
 // full-range, offset-free index (0..19 for gear, 0..count-1 for runes); see
 // the bridge contract in botw_bridge.h.
 {F f;ready(f);assert(!on_action(&f.state,"touch_confirm",0));assert(!on_action(&f.state,"quick_page",1));assert(!on_action(&f.state,"visible_select",0));++cases;}
 for(int index:{-1,9,10}){F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(!on_action(&f.state,"select",index));assert(!f.get<uint32_t>(8));++cases;}
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);
  // Iteration 8 item 1: index 0 is the synthetic None entry; One/Two shift to indices 1/2.
  assert(on_action(&f.state,"select",2));assert(f.state.touch_focus.address==F::Two&&!f.state.touch_select_deferred&&!f.get<uint32_t>(8));
  assert(!on_action(&f.state,"select",3));++cases;
 }
 {F f;ready(f);assert(on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);
  // Every slot is reachable by its own index in one step -- no page state, no 9-wide window.
  const auto item=f.state.touch_gear.items.back();for(unsigned i=2;i<20;++i){auto copy=item;copy.address+=i*0x100;f.state.touch_gear.items.push_back(copy);}
  assert(on_action(&f.state,"select",18));assert(f.state.touch_focus.address==f.state.touch_gear.items[18].address);
  assert(on_action(&f.state,"select",20));assert(f.state.touch_focus.address==f.state.touch_gear.items[20].address);
  assert(!on_action(&f.state,"select",21));assert(!f.get<uint32_t>(8));
  sample(&f.state,&f.h);
  // The windowed contract is gone; item.{i} is the only publish, for every slot.
  assert(!f.values.count("botw.quick.visible.0.valid") && !f.values.count("botw.quick.previous_available") &&
         !f.values.count("botw.quick.next_available") && !f.texts.count("botw.quick.window_text"));
  assert(f.values.count("botw.quick.item.17.valid") && f.values.count("botw.quick.item.19.valid"));
  ++cases;
 }
 auto recent_ready=[&](F& f) {
  ready(f);f.attach(100,"Weapon_Test");sample(&f.state,&f.h);assert(f.state.recent_actor_valid[0]);
  auto other=f.state.data;other.items[0].equipped=false;other.items[1].equipped=true;
  f.state.recent[0].update(other,0,"Weapon_Test");f.state.recent[0].update(f.state.data,0,"Weapon_Test");assert(f.state.recent[0].target(f.state.data,0)->address==F::Two);
 };
 {F f;recent_ready(f);assert(on_action(&f.state,"quick_recent",0));assert(!on_action(&f.state,"quick_recent",0) && !on_action(&f.state,"quick_help",0));sample(&f.state,&f.h);assert(!f.state.recent_deferred && f.state.bridge.pending_ready_equip && f.get<uint64_t>(40)==F::Two && f.values["botw.quick.gear"]==0);++cases;}
 for(int i:{-1,1,2}){F f;recent_ready(f);assert(!on_action(&f.state,"quick_recent",i) && !f.get<uint32_t>(8));++cases;}
 for(int reason=0;reason<8;++reason){F f;recent_ready(f);assert(on_action(&f.state,"quick_recent",0));
  if(reason==0)f.texts["page"]="map";
  if(reason==1)f.values["aux"]=0;
  if(reason==2){f.values["epoch"]=2;f.mb<uint32_t>(4092,2);}
  if(reason==3)f.state.last_sample-=std::chrono::seconds(1);
  if(reason==4)f.put<uint32_t>(F::U+0x64C58,0x80000);
  if(reason==5)f.state.recent_action_at-=std::chrono::seconds(1);
  if(reason==6)f.put<int32_t>(F::Two+0x20,399);
  if(reason==7){f.values["aux"]=0;tick(&f.state,&f.h);f.values["aux"]=1;}
  sample(&f.state,&f.h);assert(!f.state.recent_deferred && !f.get<uint32_t>(8));++cases;
 }
 {F f;recent_ready(f);assert(on_action(&f.state,"quick_recent",0));f.state.bridge.heartbeat_at-=std::chrono::milliseconds(150);sample(&f.state,&f.h);assert(f.state.recent_deferred && !f.get<uint32_t>(8));f.mb<uint32_t>(4,102);sample(&f.state,&f.h);assert(!f.state.recent_deferred && f.state.bridge.pending_ready_equip);++cases;}
 for(const char* page:{"map","inventory","quest","unknown"}){F f;recent_ready(f);f.texts["page"]=page;assert(!on_action(&f.state,"quick_recent",0));++cases;}
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);
  assert(f.values["botw.quick.secondary"]==1 && f.texts["botw.quick.title"]=="Secondary Weapons");
  // Iteration 8 item 1: index 0 is the synthetic None entry; One (equipped, index 1) and Two
  // (not equipped, index 2) shift by one.
  assert(on_action(&f.state,"select",2));assert(!f.get<uint32_t>(8) && !f.state.touch_select_deferred);sample(&f.state,&f.h);
  assert(f.values["botw.quick.highlighted"]==2 && f.values["botw.quick.secondary.confirm_available"]==1);
  assert(on_action(&f.state,"secondary_confirm",0));assert(f.state.recent[0].pinned.address==F::Two && !f.get<uint32_t>(8));
  f.mb<uint32_t>(4,110);assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);assert(f.state.bridge.pending_ready_equip && f.get<uint64_t>(40)==F::Two);++cases;}
 for(int reason=0;reason<7;++reason){F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"select",2));
  if(reason==0)f.values["epoch"]=2;
  if(reason==1)f.values["aux"]=0;
  if(reason==2)f.texts["page"]="map";
  if(reason==3)f.put<int32_t>(F::Two+0x20,0);
  if(reason==4)f.put<uint8_t>(F::Two+0x24,1);
  if(reason==5)assert(on_action(&f.state,"quick_close",0));
  if(reason==6)f.put<char>(F::Two+0x3C,'X');
  assert(!on_action(&f.state,"secondary_confirm",0) && !f.state.recent[0].pinned.address && !f.get<uint32_t>(8));++cases;}
 // Index 1 (One) is the currently-equipped item -- pin() refuses equipping your own main
 // weapon as its own secondary. (Index 0, the None entry, now legitimately succeeds via
 // secondary_confirm's new None branch -- see the dedicated case below.)
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"select",1));assert(!on_action(&f.state,"secondary_confirm",0));assert(!f.get<uint32_t>(8));++cases;}
 // Iteration 8 item 1: selecting the None cell (index 0) and confirming it is the secondary
 // picker's "None" route -- same end state as the dedicated secondary_none action, reached
 // through the generic grid select/confirm path instead.
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"select",0));
  assert(on_action(&f.state,"secondary_confirm",0));assert(f.state.secondary_none && !f.state.secondary_picker && !f.get<uint32_t>(8));++cases;}
 // 2026-10-18 owner: "None" secondary weapon -- clears the Ready teardrop, blocks its flick
 // swap, and persists (companion-only state, never in the sample-gap/epoch_changed reset
 // lists) until the player picks a real secondary again via secondary_confirm. No native write.
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);
  assert(on_action(&f.state,"secondary_none",0));assert(f.state.secondary_none && !f.state.secondary_picker && !f.get<uint32_t>(8));
  sample(&f.state,&f.h);assert(f.values["botw.quick.secondary.none"]==1 && f.values["botw.ready.recent.weapon.valid"]==0);++cases;}
 // Refused outside the secondary picker (no state change, no native write).
 {F f;ready(f);assert(!on_action(&f.state,"secondary_none",0));assert(!f.state.secondary_none);++cases;}
 for(int reason=0;reason<4;++reason){F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);
  if(reason==0)f.values["epoch"]=2;
  if(reason==1)f.values["aux"]=0;
  if(reason==2)f.texts["page"]="map";
  if(reason==3)assert(on_action(&f.state,"quick_close",0));
  assert(!on_action(&f.state,"secondary_none",0) && !f.state.secondary_none && !f.get<uint32_t>(8));++cases;}
 // None never equips the hidden backup through the flick (quick_recent 0), even though a valid
 // backup exists. Iteration 12 item 3: the flick with None now means "primary -> secondary" via
 // the native Remove, which this bridge level (no remove support) cannot do, so it is refused.
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);
  assert(on_action(&f.state,"secondary_none",0));assert(!f.state.bridge.remove_supported && !on_action(&f.state,"quick_recent",0));assert(!f.get<uint32_t>(8));++cases;}
 // Iteration 12 item 3 (owner 2026-10-22). Helpers: the native results of Remove/Equip on the
 // weapon slot (pouch equipped flag + the attached slot), then a fresh inventory sample.
 auto weapon_off=[](F& f,uint64_t a){f.put<uint8_t>(a+0x24,0);f.put<uint64_t>(F::Player+0xB98,0);f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);f.state.next={};sample(&f.state,&f.h);};
 auto weapon_on=[](F& f,uint64_t a){f.put<uint8_t>(a+0x24,1);f.attach(100,"Weapon_Test");f.state.next={};sample(&f.state,&f.h);};
 auto acked=[](F& f){f.mb<uint32_t>(12,f.get<uint32_t>(8));f.mb<uint32_t>(16,1);f.state.bridge.pending=0;f.state.bridge.cancel();f.mb<uint32_t>(4,f.get<uint32_t>(4)+1);};
 // (b) secondary None + primary equipped: the flick removes the primary through the native
 // Remove (kind 7) and, once the game shows the hand empty, pins it as the secondary.
 for(int variant=0;variant<2;++variant){F f;ready(f);f.removable(0);sample(&f.state,&f.h);
  assert(f.state.recent_actor_valid[0] && f.state.recent[0].current.address==F::Two);
  if(variant==0)f.state.secondary_none=true;          // explicit None
  else {assert(f.state.recent[0].pin(f.state.data,{F::One,"Weapon_Test"},0));f.state.next={};sample(&f.state,&f.h);
    assert(f.values["botw.ready.recent.weapon.valid"]==1);
    f.put<int32_t>(F::One+0x20,0);f.state.next={};sample(&f.state,&f.h);}  // the secondary broke -> no valid secondary
  if(variant==0){f.state.next={};sample(&f.state,&f.h);}
  assert(f.values["botw.ready.recent.weapon.valid"]==0 && f.values["botw.ready.recent.weapon.swap"]==1);
  assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);
  assert(f.state.bridge.pending_inventory && f.get<uint32_t>(20)==7 && f.get<uint64_t>(40)==F::Two && f.state.swap_demote.address==F::Two);
  weapon_off(f,F::Two);
  assert(!f.state.secondary_none && f.state.recent[0].pinned.address==F::Two && !f.state.swap_demote.address);
  assert(f.values["botw.ready.recent.weapon.valid"]==1 && f.texts["botw.ready.weapon"].empty());++cases;}
 // (b) refused without a primary and without a secondary (nothing to swap), and the demote
 // expectation is dropped if the primary is still equipped later (removal refused natively).
 {F f;ready(f);f.removable(0);sample(&f.state,&f.h);f.state.secondary_none=true;
  f.put<uint8_t>(F::Two+0x24,0);f.put<uint64_t>(F::Player+0xB98,0);f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);f.state.next={};sample(&f.state,&f.h);
  assert(f.values["botw.ready.recent.weapon.swap"]==0 && !on_action(&f.state,"quick_recent",0) && !f.get<uint32_t>(8));++cases;}
 {F f;ready(f);f.removable(0);sample(&f.state,&f.h);f.state.secondary_none=true;
  assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);assert(f.state.swap_demote.address==F::Two);
  f.state.swap_at-=std::chrono::seconds(7);f.state.next={};sample(&f.state,&f.h);assert(!f.state.swap_demote.address && f.state.secondary_none);++cases;}
 // (a) primary empty + secondary set: the flick equips the secondary natively (kind 6) and the
 // secondary then STAYS EMPTY (None) -- no previous/seed auto-pick of another weapon.
 {F f;ready(f);f.removable(0);sample(&f.state,&f.h);assert(f.state.recent[0].pin(f.state.data,{F::One,"Weapon_Test"},0));
  assert(f.state.recent[0].target(f.state.data,0)->address==F::One);
  weapon_off(f,F::Two);assert(!f.state.recent[0].current.address && f.values["botw.ready.recent.weapon.valid"]==1);
  assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);
  assert(f.state.bridge.pending_inventory && f.get<uint32_t>(20)==6 && f.get<uint64_t>(40)==F::One && f.state.swap_promote.address==F::One);
  weapon_on(f,F::One);
  assert(f.state.secondary_none && !f.state.recent[0].pinned.address && !f.state.swap_promote.address && f.values["botw.ready.recent.weapon.valid"]==0);
  weapon_on(f,F::One);assert(f.state.secondary_none && f.values["botw.ready.recent.weapon.valid"]==0 && f.values["botw.ready.recent.weapon.swap"]==1);
  // Full cycle: flick again -> (b) moves One back to the secondary, primary empty hand.
  acked(f);assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);
  assert(f.get<uint32_t>(20)==7 && f.get<uint64_t>(40)==F::One);
  weapon_off(f,F::One);assert(!f.state.secondary_none && f.state.recent[0].pinned.address==F::One && f.values["botw.ready.recent.weapon.valid"]==1);
  // ...and once more -> (a) again.
  acked(f);assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);assert(f.get<uint32_t>(20)==6 && f.get<uint64_t>(40)==F::One);
  weapon_on(f,F::One);assert(f.state.secondary_none && f.values["botw.ready.recent.weapon.valid"]==0);++cases;}
 // (a) does not fire when a DIFFERENT weapon shows up equipped (native change elsewhere): the
 // secondary keeps its existing history behaviour.
 {F f;ready(f);f.removable(0);sample(&f.state,&f.h);assert(f.state.recent[0].pin(f.state.data,{F::One,"Weapon_Test"},0));weapon_off(f,F::Two);
  assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);assert(f.state.swap_promote.address==F::One);
  weapon_on(f,F::Two);assert(!f.state.secondary_none && !f.state.swap_promote.address);++cases;}
 // Normal swap (both sides filled) is unchanged: Equip of the secondary, no None afterwards.
 {F f;ready(f);f.removable(0);sample(&f.state,&f.h);assert(f.state.recent[0].pin(f.state.data,{F::One,"Weapon_Test"},0));
  assert(on_action(&f.state,"quick_recent",0));sample(&f.state,&f.h);
  assert(f.get<uint32_t>(20)==6 && f.get<uint64_t>(40)==F::One && !f.state.swap_promote.address && !f.state.swap_demote.address);
  f.put<uint8_t>(F::Two+0x24,0);weapon_on(f,F::One);
  assert(!f.state.secondary_none && f.state.recent[0].target(f.state.data,0)->address==F::Two);++cases;}
 // Survives a sample gap (app backgrounded) and an epoch change (a reload) -- it is not in
 // either reset list.
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"secondary_none",0));
  f.state.last_sample-=std::chrono::seconds(1);sample(&f.state,&f.h);assert(f.state.secondary_none);
  f.values["epoch"]=2;f.mb<uint32_t>(4092,2);sample(&f.state,&f.h);assert(f.state.secondary_none);++cases;}
 // Picking a real secondary afterwards clears None (teardrop goes back to target()).
 {F f;recent_ready(f);assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"secondary_none",0));
  assert(on_action(&f.state,"secondary_open",0));sample(&f.state,&f.h);assert(on_action(&f.state,"select",2));sample(&f.state,&f.h);
  assert(on_action(&f.state,"secondary_confirm",0));assert(!f.state.secondary_none && f.state.recent[0].pinned.address==F::Two);
  sample(&f.state,&f.h);assert(f.values["botw.ready.recent.weapon.valid"]==1);++cases;}
 {F f;ready(f);
  for(auto address:{F::One,F::Two}){std::array<char,64> name{};std::strcpy(name.data(),address==F::One?"Weapon_Sword_001":"Weapon_Sword_002");f.put(address+0x3C,name);}
  // Owner 2026-10-18: auto-equip of the secondary on break is CANCELLED, and the disabled watcher
  // (botw::SecondaryBreak) was removed outright in the 2026-10-19 refactor/dead-code pass -- its
  // observe() was never called from the dispatch, so these two blocks (originally asserting the
  // watcher's own fields stayed cleared) now assert the externally-visible invariant directly:
  // simulating a weapon breaking while attached never produces a native equip-mailbox write.
  f.attach(100,"Weapon_Sword_001");f.state.next={};sample(&f.state,&f.h);
  f.put<int32_t>(F::One+0x20,0);f.state.next={};f.mb<uint32_t>(4,110);sample(&f.state,&f.h);assert(!f.get<uint32_t>(8));
  f.put<uint64_t>(F::Pouch+0x68,F::Two+8);f.put<uint64_t>(F::Pouch+0x70,F::Two+8);f.put<int32_t>(F::Pouch+0x78,1);
  f.put<uint64_t>(F::Two+8,F::Pouch+0x68);f.put<uint64_t>(F::Two+16,F::Pouch+0x68);
  f.put<uint64_t>(F::Player+0xB98,0);f.put<uint32_t>(F::Player+0xBA0,UINT32_MAX);
  f.state.next={};f.mb<uint32_t>(4,111);sample(&f.state,&f.h);assert(!f.state.bridge.pending_ready_equip && !f.get<uint32_t>(8));
  sample(&f.state,&f.h);assert(!f.get<uint32_t>(8) && std::strcmp(f.state.bridge.status,"Equipping secondary weapon")!=0);++cases;
 }
 // 2026-10-18 drag shortcuts: dropping a grid tile on the action slot selects that tile and runs
 // the request the slot shows for it (here Equip); invalid indices, a non-World context or a
 // tile whose slot has no action are refused without a mailbox write.
 {F f;f.state.context=botw::UiContext::World;f.state.inventory.selected=0;
  const auto items=botw::inventory_items(f.state.data,f.state.inventory.category);int64_t two=-1;
  for(size_t i=0;i<items.size();++i){if(items[i]->address==F::Two)two=int64_t(i);}
  assert(two>=0);
  assert(on_action(&f.state,"inventory_drag_use",two));assert(f.state.inventory.selected==F::Two);
  assert(f.get<uint32_t>(20)==6 && f.get<uint32_t>(8)==1 && f.get<uint64_t>(40)==F::Two);++cases;}
 for(int64_t bad:{-1,2,99}) {F f;f.state.context=botw::UiContext::World;assert(!on_action(&f.state,"inventory_drag_use",bad));assert(f.get<uint32_t>(8)==0);++cases;}
 {F f;const auto items=botw::inventory_items(f.state.data,0);int64_t two=-1;for(size_t i=0;i<items.size();++i)if(items[i]->address==F::Two)two=int64_t(i);
  f.state.context=botw::UiContext::Unknown;assert(!on_action(&f.state,"inventory_drag_use",two));assert(f.get<uint32_t>(8)==0);++cases;}
 {F f;f.state.context=botw::UiContext::World;const auto items=botw::inventory_items(f.state.data,0);int64_t one=-1;
  for(size_t i=0;i<items.size();++i)if(items[i]->address==F::One)one=int64_t(i);
  f.state.bridge.remove_supported=false;assert(!on_action(&f.state,"inventory_drag_use",one));assert(f.get<uint32_t>(8)==0);++cases;}
 {F f;f.state.context=botw::UiContext::World;assert(!on_action(&f.state,"inventory_drag_hold",0));assert(!on_action(&f.state,"inventory_drag_hold",-1));assert(f.get<uint32_t>(8)==0);++cases;}
 std::cout<<cases<<" inventory bridge identity/publication/actor-confirmation cases passed\n";
}
