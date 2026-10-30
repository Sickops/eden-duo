// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
static int64_t point(int32_t x,int32_t y){return int64_t((uint64_t(uint32_t(x))<<32)|uint32_t(y));}
int main(){
  int cases=0;
  for(int row:{1,2,3}) {
    Fixture f;State s;f.texts["__page"]="ready";f.values["__aux_present"]=1;
    f.h.get_text=[](void*p,const char*k){return static_cast<Fixture*>(p)->texts[k].c_str();};
    f.h.get_i64=[](void*p,const char*k,int64_t){return static_cast<Fixture*>(p)->values[k];};
    s.bridge.host=f.h;s.context=botw::UiContext::World;s.cards.active=2;
    s.temp_card.plan.valid=true;
    s.temp_card.plan.quick.valid=true;s.temp_card.plan.quick.equip={11};
    s.temp_card.plan.best.valid=true;s.temp_card.plan.best.equip={22,23,24};
    s.temp_card.plan.food.valid=true;s.temp_card.plan.food.food=33;
    const auto packed=row==1?point(200,300):row==2?point(600,300):point(400,650);
    // Unknown page and the800px box's transparent corners cannot arm native work.
    f.texts["__page"]="inventory";assert(!on_action(&s,"ready_radial_tap",packed));
    f.texts["__page"]="ready";assert(!on_action(&s,"ready_radial_tap",point(0,0)));
    assert(s.temp_card.queue.empty());cases+=3;
    assert(on_action(&s,"ready_radial_tap",packed));
    assert(s.temp_card.queue==(row==1?std::vector<uint64_t>{11}:row==2?std::vector<uint64_t>{22,23,24}:std::vector<uint64_t>{33}));
    assert(s.temp_card.queue_eat==(row==3));cases+=3;
    // A second tap while queued must never duplicate a request.
    assert(!on_action(&s,"ready_radial_tap",packed));++cases;
    assert(on_action(&s,"ready_radial_tap",point(400,400)));assert(s.cards.active==0);cases+=2;
    assert(!on_action(&s,"ready_radial_tap",packed));++cases;
  }
  // New compact publications are actual plan data, not strings scraped by UI.
  Fixture f;botw::TemperatureCardController c;c.plan.valid=true;c.plan.need=2;
  c.plan.quick.valid=true;c.plan.quick.pieces=1;c.plan.best.valid=true;c.plan.best.pieces=3;
  c.publish(f.h,true,true);
  assert(f.texts["botw.card.temperature.need_level_text"]=="Need Lv 2");
  assert(f.texts["botw.card.temperature.row.1.pieces_text"]=="1 piece");
  assert(f.texts["botw.card.temperature.row.2.pieces_text"]=="3 pieces");cases+=3;
  // Iteration 13 item 5: the slice's single level/pieces line.
  assert(f.texts["botw.card.temperature.row.1.level_line"]=="Lv 0 of 2 \xC2\xB7 1 piece");
  assert(f.texts["botw.card.temperature.row.2.level_line"]=="Lv 0 of 2 \xC2\xB7 3 pieces");cases+=2;
  c.plan.best.reaches=true;c.plan.best.level=3;c.plan.best.worn=true;c.publish(f.h,true,true);
  assert(f.texts["botw.card.temperature.row.2.level_line"]=="Lv 3 \xC2\xB7 worn \xC2\xB7 3 pieces");++cases;
  c.plan={};c.publish(f.h,false,false);
  assert(f.texts["botw.card.temperature.need_level_text"].empty());
  assert(f.texts["botw.card.temperature.row.1.pieces_text"].empty());
  assert(f.texts["botw.card.temperature.row.1.level_line"].empty());cases+=3;
  std::cout<<cases<<" radial action scope/queue/duplicate/close/publication cases passed\n";
}
