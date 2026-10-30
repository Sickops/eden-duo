// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
struct SensorFixture:Fixture {
 static constexpr uint64_t Map=0xA000000,Sensor=0xA010000,Hud=0xA020000,Pane=0xA030000,Animation=0xA040000;
 SensorFixture(){
  put(M+0x2CA63B0,Map);put(Map,M+0x2B86848);put<int32_t>(Map+0x38A8,0);put<uint8_t>(Map+0x38AC,1);put(M+0x2CA66D0,Sensor);put(Sensor,M+0x2B87700);put(A+37*8,Hud);put(Hud,M+0x2BA14E8);put(Hud+0x3D68,Pane);put(Pane,M+0x2B8D980);put<float>(Pane+0x144,75);put(Pane+0x158,Animation);put(Animation,M+0x2BBE308);put<float>(Animation+0x20,75);
  for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL}){std::array<uint8_t,56>b{};uint64_t vt=M+0x2A701C8,p=M+off+20;int32_t cap=32;std::memcpy(b.data(),&vt,8);std::memcpy(b.data()+8,&p,8);std::memcpy(b.data()+16,&cap,4);std::memcpy(b.data()+20,"MainField",10);put(M+off,b);}
 }
};
int main(){using F=SensorFixture;unsigned cases{};auto check=[&](auto change,bool valid){F f;change(f);auto v=botw::sensor(f.h);if(v.valid!=valid)std::cerr<<"case "<<cases+1<<'\n';assert(v.valid==valid);++cases;};
 check([](F&){},true);check([](F&f){f.h.build_id[0]^=1;},false);check([](F&f){f.h.main_size=1;},false);check([](F&f){f.h.main_base=UINT64_MAX-10;},false);
 for(uint64_t at:{F::M+0x2CA63B0,F::M+0x2CA66D0,F::A+37*8,F::Hud+0x3D68,F::Pane+0x158})for(uint64_t p:{0ULL,0x10001ULL,UINT64_MAX-7ULL})check([&](F&f){f.put(at,p);},false);
 for(uint64_t at:{F::Map,F::Sensor,F::Hud,F::Pane,F::Animation})check([&](F&f){f.put<uint64_t>(at,0);},false);
 for(int32_t mode:{-1,3,INT32_MAX})check([&](F&f){f.put(F::Map+0x38A8,mode);},false);
 check([](F&f){f.put<uint8_t>(F::Map+0x38AC,2);},false);
 for(uint64_t at:{F::Pane+0x144,F::Animation+0x20})for(float v:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f,101.f})check([&](F&f){f.put(at,v);},false);
 check([](F&f){f.put<float>(F::Animation+0x20,74);},false);
 for(uint64_t flags:{0x1849000000ULL,0x0E09E00000ULL,0ULL})check([&](F&f){f.put(F::U+0x64C30,flags);},false);
 for(uint64_t off:{0x2C9E868ULL,0x2CA4DA0ULL})check([&](F&f){f.put<char>(F::M+off+20,'X');},false);
 for(uint64_t at:{F::M+0x2CA63B0,F::Map,F::Map+0x38A8,F::Map+0x38AC,F::M+0x2CA66D0,F::Sensor,F::A+37*8,F::Hud,F::Hud+0x3D68,F::Pane,F::Pane+0x144,F::Pane+0x158,F::Animation,F::Animation+0x20,F::U+0x64C30,F::M+0x2C9E868})check([&](F&f){f.race=at;},false);
 check([](F&f){f.put<int32_t>(F::Map+0x38A8,2);f.put<float>(F::Animation+0x20,0);},true);
 check([](F&f){f.put<uint8_t>(F::Map+0x38AC,0);f.put<float>(F::Animation+0x20,0);},true);
 for(int32_t mode=0;mode<3;++mode){F f;f.put(F::Map+0x38A8,mode);auto s=botw::sensor(f.h);assert(s.valid&&s.mode==mode);botw::publish_sensor(f.h,s);assert(f.values["botw.ready.sensor.enabled"]==(mode!=2));assert(f.values["botw.ready.sensor.palette"]==mode);assert(f.values["botw.ready.sensor.ring_palette.0"]==(mode==2?0:mode==1?511:255));assert(f.texts["botw.ready.sensor.text"]==(mode==0?"Shrine":mode==1?"Sensor +":"Off"));assert(f.values["botw.ready.sensor.ring.0"]>>24==(mode==2?0:255));for(unsigned i=0;i<4;++i)assert(f.texts["botw.ready.sensor.art."+std::to_string(i)]=="romfs:/Pack/Bootup.pack#Layout/Common.sblarc#timg/__Combined.bntx#Nt_SeekSensorIcon"+std::string(mode==1?"P":"")+"_0"+std::to_string(i)+"^s");++cases;}
 {F f;auto s=botw::sensor(f.h);botw::publish_sensor(f.h,s);assert(f.values["botw.ready.sensor.ring.1"]>>24==128&&f.values["botw.ready.sensor.ring.2"]>>24==0);++cases;}
 // Owner bug 2026-10-23: the shrine compass follows the game's own sensor (obtained + switched on).
 // Thor 2026-10-23: native HUD pane chain unreadable (companion re-poses the HUD) while the game's
 // sensor is obtained and on -> full reading invalid, but the compass gate stays available.
 {F f;f.put<uint64_t>(F::Pane,0);auto s=botw::sensor(f.h);assert(!s.valid&&s.map_valid&&botw::sensor_available(s));++cases;
  f.put(F::Map+0x38A8,int32_t(2));s=botw::sensor(f.h);assert(s.map_valid&&!botw::sensor_available(s));++cases;
  f.put(F::Map+0x38A8,int32_t(0));f.put<uint8_t>(F::Map+0x38AC,0);s=botw::sensor(f.h);assert(s.map_valid&&!botw::sensor_available(s));++cases;}
 {botw::Sensor s{};assert(!botw::sensor_available(s));++cases;
  s={true,false,0,0.f};assert(!botw::sensor_available(s));++cases;   // not obtained / disabled
  s={true,true,2,0.f};assert(!botw::sensor_available(s));++cases;    // switched off
  s={true,true,0,0.f};assert(!botw::sensor_available(s));++cases;    // HUD read only: gate needs map state
  s={true,true,1,0.f};assert(!botw::sensor_available(s));++cases;   // HUD read only: gate needs map state
  s={};s.map_valid=true;s.map_enabled=true;s.map_mode=0;assert(botw::sensor_available(s));++cases;  // HUD chain failed, sensor on
  s.map_mode=1;assert(botw::sensor_available(s));++cases;              // Sensor +
  s.map_mode=2;assert(!botw::sensor_available(s));++cases;             // switched off
  s.map_mode=0;s.map_enabled=false;assert(!botw::sensor_available(s));++cases; // not obtained
  s.map_enabled=true;s.map_valid=false;assert(!botw::sensor_available(s));++cases;} // unreadable
 for(float frame:{0.f,50.f,70.f,85.f,100.f}){F f;f.put(F::Pane+0x144,frame);f.put(F::Animation+0x20,frame);auto s=botw::sensor(f.h);assert(s.valid&&s.frame==frame);botw::publish_sensor(f.h,s);assert(f.values["botw.ready.sensor.ring.2"]>>24==(frame==100?255:frame==85?64:0));++cases;}
 {F f;f.put<uint8_t>(F::Map+0x38AC,0);auto s=botw::sensor(f.h);assert(s.valid&&!s.enabled);botw::publish_sensor(f.h,s);assert(!f.values["botw.ready.sensor.enabled"]&&f.values["botw.ready.sensor.ring.0"]>>24==0);++cases;botw::publish_sensor(f.h,{});assert(!f.values["botw.ready.sensor.valid"]&&f.texts["botw.ready.sensor.text"]=="Sensor");++cases;}
 {F f;State s;sample(&s,&f.h);assert(f.values["botw.ready.sensor.valid"]==1);f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);s.next={};sample(&s,&f.h);assert(!f.values["botw.ready.sensor.valid"]&&f.values["botw.ready.sensor.ring.0"]>>24==0);++cases;}
 {F f;State s;sample(&s,&f.h);f.h.read_memory=[](void*,uint64_t,void*,size_t)->EdenDsmodBool{throw 1;};s.next={};sample(&s,&f.h);assert(!f.values["botw.ready.sensor.valid"]&&f.values["botw.ready.sensor.ring.0"]>>24==0);++cases;}
 std::cout<<cases<<" native sensor animation bounds/race/mode/ramp/context cases passed\n";
}
