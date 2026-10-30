// SPDX-License-Identifier: GPL-3.0-or-later
// Heal advisor (owner item 3): ranking, heal reads, low-health hysteresis, page cycle, publish.
#include "botw_heal_advisor.h"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include <limits>
using botw::HealOption;
static HealOption opt(const char* actor,int heal,unsigned count,uint64_t address=0,bool full=false,int bonus=0) {
  HealOption o;o.actor=actor;o.heal=heal;o.count=count;o.address=address;o.full=full;o.bonus=bonus;return o;
}
static std::vector<std::string> order(const std::vector<HealOption>& v) {
  std::vector<std::string> out;for(const auto& o:v)out.push_back(o.actor);return out;
}
// Pouch memory for heal_candidates/still_matches: one 0x98-byte item record per food.
struct PouchFixture:Fixture {
  botw::Snapshot pouch;uint64_t next=0x9000000;
  uint64_t add(const char* actor,int type,int value,int health=0,float effect=-1,float level=0,bool equipped=false) {
    const uint64_t p=next;next+=0x1000;const bool recipe=std::string(actor).rfind("Item_Cook_",0)==0;
    put(p,std::array<uint8_t,0x98>{});put<int32_t>(p+0x18,type);put<int32_t>(p+0x1C,recipe ? 8 : 10);put<int32_t>(p+0x20,value);
    put<uint8_t>(p+0x25,1);put<uint64_t>(p+0x28,M+0x2A6FAF0);put<uint64_t>(p+0x30,p+0x3C);put<int32_t>(p+0x38,64);
    std::array<char,64> text{};std::memcpy(text.data(),actor,std::strlen(actor));put(p+0x3C,text);
    put<int32_t>(p+0x80,health);put<int32_t>(p+0x84,recipe && effect>=0 ? 300 : 0);put<int32_t>(p+0x88,10);
    put<float>(p+0x8C,effect);put<float>(p+0x90,level);
    pouch.valid=true;pouch.items.push_back({actor,type,value,equipped,p});return p;
  }
};
struct HealthFixture:Fixture {
  static constexpr uint64_t Info=0x9100000,Player=0x9200000;
  HealthFixture(int life=12,int max=48,float base=48.f) {
    put(Info,std::array<uint8_t,0xA8>{});put<float>(Info+0xA0,base);
    put<int32_t>(Player+0x848,life);put<int32_t>(Player+0x1868,max);
  }
  botw::PlayerHealth read(){return botw::player_health(h,Info,Player);}
};
int main() {
  unsigned cases{};
  // --- band rank: exact, +1, -1, +2, -2 ... (one heart = 4 quarters per band)
  assert(botw::heal_band_rank(16,16)==0);
  for(int q=1;q<=4;++q){assert(botw::heal_band_rank(16+q,16)==1);assert(botw::heal_band_rank(16-q,16)==2);}
  for(int q=5;q<=8;++q){assert(botw::heal_band_rank(16+q,16)==3);assert(botw::heal_band_rank(16-q,16)==4);}
  assert(botw::heal_band_rank(0,16)==8 && botw::heal_band_rank(40,16)==11);cases+=4;
  // --- owner example: need 4 hearts -> 4-heart foods, then 5, 3, 6, 2 ...
  {
    auto r=botw::rank_heal_options({opt("six",24,1),opt("three",12,1),opt("four",16,1),opt("two",8,1),opt("five",20,1),opt("seven",28,1)},16);
    assert((order(r)==std::vector<std::string>{"four","five","three","six","two"}));assert(r.size()==5);++cases;
  }
  // tie-break 1: fewest quarters wasted within a band (4¼ before 5 for +1; 3¾ before 3 for -1)
  {
    auto r=botw::rank_heal_options({opt("five",20,9),opt("fourq",17,1),opt("three",12,9),opt("threeq",15,1)},16);
    assert((order(r)==std::vector<std::string>{"fourq","five","threeq","three"}));++cases;
  }
  // tie-break 2: the item the player has most of
  {
    auto r=botw::rank_heal_options({opt("few",16,2,1),opt("many",16,12,2),opt("mid",16,5,3)},16);
    assert((order(r)==std::vector<std::string>{"many","mid","few"}));++cases;
  }
  // deterministic final tie (actor then address)
  {auto r=botw::rank_heal_options({opt("b",8,1,2),opt("a",8,1,3),opt("a",8,1,1)},8);assert(r[0].address==1 && r[1].address==3 && r[2].actor=="b");++cases;}
  // full-recovery (hearty) after all regular food, least bonus first; still listed when nothing else heals
  {
    auto r=botw::rank_heal_options({opt("hearty8",0,1,0,true,8),opt("apple",2,30),opt("hearty4",0,1,0,true,4)},40);
    assert((order(r)==std::vector<std::string>{"apple","hearty4","hearty8"}));++cases;
    auto only=botw::rank_heal_options({opt("hearty",0,2,0,true,12)},40);assert(only.size()==1 && only[0].full);++cases;
  }
  // at most 5, from more
  {std::vector<HealOption> v;for(int i=1;i<=12;++i)v.push_back(opt("f",i*2,1,uint64_t(i)));assert(botw::rank_heal_options(v,10).size()==5);
   assert(botw::rank_heal_options(v,10,3).size()==3 && botw::rank_heal_options({},10).empty());cases+=2;}
  // max health 30 hearts: need 29¾ -> 30-heart meal is +¼ (band +1), 20-heart meal far below
  {auto r=botw::rank_heal_options({opt("big",120,1),opt("mid",80,3),opt("hearty",0,1,0,true,4)},119);
   assert((order(r)==std::vector<std::string>{"big","mid","hearty"}));++cases;}
  // --- hearts text (game font carries the fraction glyphs)
  assert(botw::hearts_text(0)=="0" && botw::hearts_text(1)=="\xC2\xBC" && botw::hearts_text(2)=="\xC2\xBD" && botw::hearts_text(3)=="\xC2\xBE");
  assert(botw::hearts_text(16)=="4" && botw::hearts_text(18)=="4\xC2\xBD" && botw::hearts_text(120)=="30" && botw::hearts_text(-3)=="0");cases+=2;
  // --- thresholds (quarters): 3 hearts max -> on 1½; 5 -> 2½; 12 -> 3; 16 -> 4; 30 -> 7½
  assert(botw::low_health_on(12)==6 && botw::low_health_off(12)==12);
  assert(botw::low_health_on(16)==8 && botw::low_health_off(16)==16);
  assert(botw::low_health_on(20)==10 && botw::low_health_off(20)==18);
  assert(botw::low_health_on(48)==12 && botw::low_health_off(48)==20);
  assert(botw::low_health_on(64)==16 && botw::low_health_off(64)==24);
  assert(botw::low_health_on(120)==30 && botw::low_health_off(120)==38);
  for(int base=12;base<=120;base+=4){assert(botw::low_health_on(base)<base && botw::low_health_off(base)>botw::low_health_on(base) && botw::low_health_off(base)<=base);}
  cases+=7;
  // --- player health reads (Player+0x848 life, Player+0x1868 max incl. yellow, PlayerInfo+0xA0 red max float)
  {HealthFixture f(10,48,48.f);auto p=f.read();assert(p.valid && p.life==10 && p.base_max==48 && p.max_life==48 && p.missing()==38);++cases;}
  {HealthFixture f(56,56,48.f);auto p=f.read();assert(p.valid && p.missing()==0);++cases;}       // 2 yellow hearts
  {HealthFixture f(0,48,48.f);assert(f.read().valid && f.read().missing()==48);++cases;}
  {HealthFixture f(120,120,120.f);assert(f.read().valid && f.read().missing()==0);++cases;}
  for(auto [life,max,base]:{std::tuple{49,48,48.f},{-1,48,48.f},{10,44,48.f},{10,48,47.f},{10,48,48.5f},{10,48,0.f},{10,48,124.f},{10,300,48.f}}) {
    HealthFixture f(life,max,base);assert(!f.read().valid);++cases;}
  {HealthFixture f;f.put<float>(HealthFixture::Info+0xA0,std::numeric_limits<float>::quiet_NaN());assert(!f.read().valid);++cases;}
  {HealthFixture f;f.race=HealthFixture::Player+0x848;assert(!f.read().valid);++cases;}
  {HealthFixture f;f.mem.erase(HealthFixture::Player+0x1868);assert(!f.read().valid);++cases;}
  {HealthFixture f;assert(!botw::player_health(f.h,HealthFixture::Info+1,HealthFixture::Player).valid && !botw::player_health(f.h,HealthFixture::Info,0).valid);++cases;}
  {HealthFixture f;f.h.build_id[0]^=1;assert(!f.read().valid);++cases;}
  // --- low-health latch with hysteresis and debounce
  {
    using C=botw::LowHealth::Clock;auto t=C::time_point{}+std::chrono::hours(1);
    auto hp=[](int life,int base=48){botw::PlayerHealth p;p.valid=true;p.life=life;p.max_life=base;p.base_max=base;return p;};
    botw::LowHealth l;
    l.observe(hp(12),1,t);assert(!l.low());                          // debounce: not yet
    l.observe(hp(12),1,t+std::chrono::milliseconds(200));assert(!l.low());
    l.observe(hp(12),1,t+std::chrono::milliseconds(310));assert(l.low() && l.shown(t+std::chrono::milliseconds(310)));
    assert(l.take_rising() && !l.take_rising());++cases;
    l.observe(hp(16),1,t+std::chrono::milliseconds(400));assert(l.low());        // inside band: stays
    l.observe(hp(20),1,t+std::chrono::milliseconds(500));assert(l.low());        // off reached, debouncing
    l.observe(hp(18),1,t+std::chrono::milliseconds(900));assert(l.low());        // dipped back: timer reset
    l.observe(hp(20),1,t+std::chrono::milliseconds(1000));
    l.observe(hp(20),1,t+std::chrono::milliseconds(1900));assert(l.low());
    l.observe(hp(20),1,t+std::chrono::milliseconds(2001));assert(!l.low() && !l.take_rising());++cases;
    // a single torn/low sample does not latch
    l.observe(hp(4),1,t+std::chrono::milliseconds(2100));l.observe(hp(40),1,t+std::chrono::milliseconds(2200));
    l.observe(hp(40),1,t+std::chrono::milliseconds(2500));assert(!l.low());++cases;
    // invalid readings keep the latch but the page goes stale after 2 s
    botw::LowHealth m;m.observe(hp(8),1,t);m.observe(hp(8),1,t+std::chrono::milliseconds(300));assert(m.low());
    m.observe({},1,t+std::chrono::milliseconds(1000));assert(m.low() && m.shown(t+std::chrono::milliseconds(2300)) && !m.shown(t+std::chrono::milliseconds(2301)));
    m.observe(hp(8),1,t+std::chrono::seconds(30));assert(m.low() && m.shown(t+std::chrono::seconds(30)));
    assert(m.take_rising() && !m.take_rising());++cases;            // one auto-show per episode
    // death hides, session change resets
    botw::LowHealth d;d.observe(hp(4),1,t);d.observe(hp(4),1,t+std::chrono::milliseconds(300));assert(d.low());
    d.observe(hp(0),1,t+std::chrono::milliseconds(400));assert(!d.shown(t+std::chrono::milliseconds(400)));
    d.observe(hp(0),1,t+std::chrono::milliseconds(1400));assert(!d.low());++cases;
    botw::LowHealth e;e.observe(hp(4),1,t);e.observe(hp(4),1,t+std::chrono::milliseconds(300));assert(e.low());
    e.observe(hp(40),2,t+std::chrono::milliseconds(400));assert(!e.low());++cases;
    // 3-heart Link: full health and 1¾ hearts are not low; 1½ hearts is
    botw::LowHealth s;s.observe(hp(12,12),1,t);s.observe(hp(12,12),1,t+std::chrono::seconds(1));assert(!s.low());
    s.observe(hp(7,12),1,t+std::chrono::seconds(2));s.observe(hp(7,12),1,t+std::chrono::milliseconds(2300));assert(!s.low());
    s.observe(hp(6,12),1,t+std::chrono::seconds(3));s.observe(hp(6,12),1,t+std::chrono::milliseconds(3300));assert(s.low());++cases;
  }
  // --- page cycle: weather(0) buffs(1) heal(2), auto-show once, swipe away, hide restores
  {
    botw::ReadyPanes panes;botw::HealPage page;
    page.set_available(false,false);assert(page.right(panes)==0 && page.count()==2);
    assert(page.step(panes,1) && page.right(panes)==1);
    page.set_available(true,true);assert(page.right(panes)==2 && page.count()==3);
    page.set_available(true,false);assert(page.right(panes)==2);
    assert(page.step(panes,1) && page.right(panes)==0);
    page.set_available(true,false);assert(page.right(panes)==0);            // not re-stolen
    assert(page.step(panes,-1) && page.right(panes)==2);
    assert(page.step(panes,-1) && page.right(panes)==1);
    assert(page.step(panes,1) && page.right(panes)==2);
    page.set_available(false,false);assert(page.right(panes)==1 && !page.showing);
    assert(page.step(panes,1) && page.right(panes)==0 && page.step(panes,1) && page.right(panes)==1);
    assert(!page.step(panes,0) && !page.step(panes,2));cases+=4;
    botw::ReadyPanes drawn;drawn.observe_drawn(true,true);botw::HealPage p2;
    p2.set_available(true,true);assert(p2.right(drawn)==2);p2.set_available(false,false);assert(p2.right(drawn)==1);++cases;
    botw::ReadyPanes q;botw::HealPage k;k.set_available(true,true,true);assert(k.right(q)==2);
    k.set_available(false,false,true);assert(k.right(q)==0 && k.count()==2);
    k.set_available(true,false,true);assert(k.right(q)==2);
    k.set_available(false,false,false);k.set_available(true,false,true);assert(k.right(q)==0);++cases;
  }
  // --- advisor update: page follows the latch and auto-shows once; card closes when health recovers
  {
    using C=botw::LowHealth::Clock;auto t=C::time_point{}+std::chrono::hours(1);
    botw::PlayerHealth p;p.valid=true;p.max_life=48;p.base_max=48;
    botw::HealAdvisor a;p.life=8;a.low.observe(p,1,t);a.update(t);assert(!a.page.available);
    a.low.observe(p,1,t+std::chrono::milliseconds(300));a.update(t+std::chrono::milliseconds(300));assert(a.page.available && a.page.showing);
    assert(!a.close_request());a.update(t+std::chrono::seconds(1));assert(!a.close_request());
    a.update(t+std::chrono::seconds(5));assert(!a.page.available && a.page.showing && !a.close_request());  // stale: hidden, kept
    p.life=40;a.low.observe(p,1,t+std::chrono::seconds(6));a.low.observe(p,1,t+std::chrono::seconds(7));
    a.update(t+std::chrono::seconds(7));assert(!a.page.available && !a.page.showing && a.close_request() && a.rows.empty());cases+=2;
  }
  // --- candidates from the pouch: raw metadata heal, cooked +0x80 heal, hearty full, merge, filters
  {
    PouchFixture f;
    const auto apple=f.add("Item_Fruit_A",7,12);                   // raw: ActorInfo 2 quarters
    f.add("Item_Roast_02",8,3);                                    // roasted: 6 quarters
    const auto meal=f.add("Item_Cook_A_01",8,1,20,-1,0);           // cooked: 5 hearts
    f.add("Item_Cook_A_01",8,1,20,-1,0);                           // identical meal -> merged x2
    f.add("Item_Cook_A_01",8,1,12,-1,0);                           // different cook data -> own row
    f.add("Item_Cook_C_17",8,1,0,13,1);                            // stamina elixir, no hearts -> excluded
    f.add("Item_Cook_C_17",8,1,0,2,8);                             // hearty elixir -> full +2
    f.add("Item_Fruit_A",7,0);                                     // exhausted stack -> excluded
    f.add("Item_Enemy_01",7,5);                                    // not edible -> excluded
    auto c=botw::heal_candidates(f.h,f.pouch);
    assert(c.size()==5);
    assert(c[0].actor=="Item_Fruit_A" && c[0].heal==2 && c[0].count==12 && c[0].address==apple);
    assert(c[1].actor=="Item_Roast_02" && c[1].heal==6 && c[1].count==3);
    assert(c[2].heal==20 && c[2].count==2 && c[2].address==meal && c[2].cook[0]==20);
    assert(c[3].heal==12 && c[3].count==1);
    assert(c[4].full && c[4].bonus==8 && c[4].heal==0);cases+=5;
    auto r=botw::rank_heal_options(c,16);                          // need 4 hearts
    assert(r[0].heal==20 && r[1].heal==12 && r[2].heal==6 && r[3].heal==2 && r[4].full);++cases;
    // only elixirs without hearts -> nothing to list
    PouchFixture e;e.add("Item_Cook_C_17",8,2,0,13,1);e.add("Item_Cook_C_17",8,1,0,10,2);
    assert(botw::heal_candidates(e.h,e.pouch).empty());++cases;
    // only elixirs, one hearty -> that one, as a full row
    PouchFixture g;g.add("Item_Cook_C_17",8,1,0,13,1);g.add("Item_Cook_C_17",8,1,0,2,4);
    auto gr=botw::heal_candidates(g.h,g.pouch);assert(gr.size()==1 && gr[0].full && gr[0].bonus==4);++cases;
    // no food at all
    PouchFixture n;n.pouch.valid=true;assert(botw::heal_candidates(n.h,n.pouch).empty());++cases;
    botw::Snapshot invalid;assert(botw::heal_candidates(f.h,invalid).empty());++cases;
    // fresh identity re-check before eating
    assert(botw::HealAdvisor::still_matches(f.h,f.pouch,c[2]));
    {auto renamed=c[2];renamed.actor="Item_Cook_A_02";assert(!botw::HealAdvisor::still_matches(f.h,f.pouch,renamed));}
    {auto gone=f.pouch;gone.items.erase(gone.items.begin()+2);assert(!botw::HealAdvisor::still_matches(f.h,gone,c[2]));}
    {auto eaten=f.pouch;eaten.items[0].value=0;assert(!botw::HealAdvisor::still_matches(f.h,eaten,c[0]));}
    f.put<int32_t>(meal+0x80,24);assert(!botw::HealAdvisor::still_matches(f.h,f.pouch,c[2]));
    cases+=5;
  }
  // --- advisor publication (page + overlay rows)
  {
    PouchFixture f;f.add("Item_Fruit_A",7,12);f.add("Item_Cook_A_01",8,1,16,-1,0);f.add("Item_Cook_C_17",8,1,0,2,8);
    botw::HealAdvisor a;using C=botw::LowHealth::Clock;auto t=C::time_point{}+std::chrono::hours(1);
    botw::PlayerHealth p;p.valid=true;p.life=9;p.max_life=48;p.base_max=48;  // 2¼ of 12 -> need 9¾
    a.low.observe(p,1,t);a.low.observe(p,1,t+std::chrono::milliseconds(300));
    a.update(t+std::chrono::milliseconds(300));
    a.publish(f.h,false);
    // i9 item 5: Rows=4 (was 3) -- row 3 is now ALWAYS published (one per quadrant), valid=0
    // here because only 3 items are in the pouch yet.
    assert(f.values["botw.card.food.close_request"]==0 && f.values["botw.ready.heal.available"]==1 && f.values["botw.ready.right.heal"]==1 && f.values["botw.ready.right.dots"]==3 && f.values["botw.ready.heal.row.3.valid"]==0);
    assert(f.texts["botw.ready.heal.hearts_text"]=="2\xC2\xBC" && f.texts["botw.ready.heal.need_text"]=="Need 9\xC2\xBE");
    assert(f.values["botw.ready.heal.row.0.valid"]==0);cases+=3;
    a.refresh(f.h,f.pouch);a.publish(f.h,true);
    assert(f.values["botw.ready.heal.count"]==3 && f.values["botw.ready.heal.busy"]==1);
    assert(f.texts["botw.ready.heal.row.0.actor"]=="Item_Cook_A_01" && f.texts["botw.ready.heal.row.0.heal_text"]=="+4");
    assert(f.values["botw.ready.heal.row.0.name"]==botw::actor_key("Item_Cook_A_01") && f.texts["botw.ready.heal.row.0.count_text"]=="\xC3\x97" "1");
    assert(f.texts["botw.ready.heal.row.1.actor"]=="Item_Fruit_A" && f.texts["botw.ready.heal.row.1.heal_text"]=="+\xC2\xBD" && f.values["botw.ready.heal.row.1.count"]==12);
    assert(f.values["botw.ready.heal.row.2.full"]==1 && f.texts["botw.ready.heal.row.2.heal_text"]=="Full" && f.texts["botw.ready.heal.row.2.bonus_text"]=="+2 extra");
    assert(f.values["botw.ready.heal.row.3.valid"]==0 && f.values["botw.ready.heal.empty"]==0);cases+=6;
    assert(a.row(0) && a.row(2) && !a.row(3) && !a.row(-1));++cases;
    // card holds at most 4 rows (i9 item 5: one per circular-card quadrant)
    for(const char* x:{"Item_Fruit_B","Item_Fruit_G","Item_Meat_01","Item_Meat_02","Item_Mushroom_E"}){f.add(x,7,2);}
    a.refresh(f.h,f.pouch);assert(a.rows.size()==4);++cases;
    a.page.available=false;a.publish(f.h,false);assert(f.values["botw.ready.heal.count"]==0 && !a.row(0));a.page.available=true;++cases;
    // empty state: low but no food
    PouchFixture n;n.pouch.valid=true;a.refresh(n.h,n.pouch);a.publish(n.h,false);
    assert(n.values["botw.ready.heal.empty"]==1 && n.values["botw.ready.heal.count"]==0);++cases;
  }
  // --- H-2: a cooked dish that heals AND carries a non-Hearty bonus effect surfaces BOTH, instead
  // of silently dropping the effect the way the pre-fix row only ever showed .bonus_text for Hearty.
  {
    PouchFixture f;f.add("Item_Cook_B_05",8,1,20,10,1);  // heals +5 hearts, Mighty (Attack Up) level 1
    botw::HealAdvisor a;using C=botw::LowHealth::Clock;auto t=C::time_point{}+std::chrono::hours(1);
    botw::PlayerHealth p;p.valid=true;p.life=4;p.max_life=48;p.base_max=48;  // low (<=12) but alive
    a.low.observe(p,1,t);a.low.observe(p,1,t+std::chrono::milliseconds(300));
    a.update(t+std::chrono::milliseconds(300));a.refresh(f.h,f.pouch);a.publish(f.h,false);
    assert(f.texts["botw.ready.heal.row.0.heal_text"]=="+5");
    assert(f.values["botw.ready.heal.row.0.has_effect"]==1 && f.texts["botw.ready.heal.row.0.effect_text"]=="Attack Up");
    // Owner item 3 (iteration 10 redesign): effect row icon family -- Attack Up (id 10) -> kind 1.
    assert(f.values["botw.ready.heal.row.0.effect_icon_kind"]==1);
    assert(f.values["botw.ready.heal.row.0.full"]==0);cases+=3;
    // a plain health-only dish (no cook effect) publishes no effect line
    PouchFixture g;g.add("Item_Cook_B_05",8,1,16,-1,0);
    botw::HealAdvisor b;b.low.observe(p,1,t);b.low.observe(p,1,t+std::chrono::milliseconds(300));
    b.update(t+std::chrono::milliseconds(300));b.refresh(g.h,g.pouch);b.publish(g.h,false);
    assert(g.values["botw.ready.heal.row.0.has_effect"]==0 && g.texts["botw.ready.heal.row.0.effect_text"]=="");
    assert(g.values["botw.ready.heal.row.0.effect_icon_kind"]==3);++cases;
    // merge: identical actor+cook+heal+effect merge; a differing effect keeps its own row
    PouchFixture h2;h2.add("Item_Cook_B_05",8,1,20,10,1);h2.add("Item_Cook_B_05",8,1,20,10,1);
    h2.add("Item_Cook_B_05",8,1,20,11,1);  // same heal, different effect (Defense Up) -> own row
    auto cands=botw::heal_candidates(h2.h,h2.pouch);assert(cands.size()==2);
    for(auto& o:cands)if(o.effect==10)assert(o.count==2);else assert(o.effect==11 && o.count==1);
    ++cases;
  }
  std::cout<<cases<<" heal advisor ranking/read/hysteresis/page/candidate/publish cases passed\n";
}
