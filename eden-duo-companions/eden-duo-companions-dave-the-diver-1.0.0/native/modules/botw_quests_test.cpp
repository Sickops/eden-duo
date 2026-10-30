// SPDX-License-Identifier: GPL-3.0-or-later
#include "01007EF00011E000.cpp"
#include "botw_test_fixture.h"
#include <cassert>
#include <iostream>
#include "botw_quest_test_fixture.h"
int main() {
  using F=QuestFixture;unsigned cases{};
  auto check=[&](auto change,bool valid,int size=-1){F f;change(f);auto q=botw::quests(f.h);if(q.valid!=valid)std::cerr<<"Quest case "<<cases+1<<"\n";assert(q.valid==valid);if(size>=0)assert(q.entries.size()==size_t(size));++cases;};
  {F f;const auto q=botw::quests(f.h);assert(q.valid&&q.entries.size()==1);const auto& e=q.entries[0];assert(e.name=="GanonQuest"&&e.message=="Desc"&&e.category==0&&e.active&&e.revealed&&e.index==0);++cases;}
  // Iteration 12 item 10: native Quest::mLocation (+0x38) is the row's place label; absent or
  // malformed labels leave the place empty without invalidating the journal.
  {F f;f.put<uint64_t>(F::Entry+0x38,0x9070000);f.string(0x9070000,"HopesPlateau");const auto q=botw::quests(f.h);assert(q.valid&&q.entries[0].place=="HopesPlateau");++cases;}
  {F f;f.put<uint64_t>(F::Entry+0x38,0x9070000);f.string(0x9070000,"");const auto q=botw::quests(f.h);assert(q.valid&&q.entries[0].place.empty());++cases;}
  {F f;f.put<uint64_t>(F::Entry+0x38,0x9070000);f.string(0x9070000,"Bad Place!");const auto q=botw::quests(f.h);assert(q.valid&&q.entries[0].place.empty());++cases;}
  {F f;const auto q=botw::quests(f.h);assert(q.valid&&q.entries[0].place.empty());++cases;}
  for(auto name:{"Main","Sub","Mini"})check([&](F& f){f.string(F::Type,name);},true,1);
  for(int state:{0,1,2})check([&](F&f){f.put<int32_t>(F::Entry+12,state);},true,state?1:0);
  check([](F&f){f.put<uint8_t>(F::Entry+0x30,0);},true,0);
  for(auto aoc:{2u,4u,6u}) {
    check([&](F&f){f.put<uint32_t>(F::Entry+0xD8,aoc);},true,0);
    check([&](F&f){f.put<uint32_t>(F::Entry+0xD8,aoc);f.put<uint8_t>(F::Dlc2+11,1);f.put<uint8_t>(F::Dlc3+11,1);},true,1);
  }
  check([](F&f){f.h.build_id[0]^=1;},false);
  check([](F&f){f.h.main_size=0x2CD1700;},false);
  for(auto at:{F::M+0x2CD1700,F::Q+0xC0,F::Array,F::Entry+0xA8,F::Steps}) {
    check([&](F&f){f.put<uint64_t>(at,0x10001);},false);
    check([&](F&f){f.put<uint64_t>(at,UINT64_MAX-7);},false);
  }
  for(auto at:{F::Q,F::Entry,F::Step})check([&](F&f){f.put<uint64_t>(at,0);},false);
  for(int count:{0,-1,513})check([&](F&f){f.put<int32_t>(F::Q+0xB8,count);},false);
  check([](F&f){f.put<int32_t>(F::Q+0xBC,0);},false);
  for(int state:{-1,3})check([&](F&f){f.put<int32_t>(F::Entry+12,state);},false);
  check([](F&f){f.put<uint8_t>(F::Entry+0x30,2);},false);
  check([](F&f){f.put<uint32_t>(F::Entry+0xD8,8);},false);
  for(int step:{-1,2})check([&](F&f){f.put<int32_t>(F::Entry+0x140,step);},false);
  // Iteration 14 item 7: no quest type is revealed at current_step==0 -- the native Adventure Log
  // on an early save (all Main quests at state1/step0, journal set) lists none of them.
  check([](F&f){f.put<int32_t>(F::Entry+0x140,0);},true,0);
  check([](F&f){f.string(F::Type,"Sub");f.put<int32_t>(F::Entry+0x140,0);},true,0);
  check([](F&f){f.string(F::Type,"Mini");f.put<int32_t>(F::Entry+0x140,0);},true,0);
  // A step-0 Main quest is unrevealed, so its missing current-step message is never read.
  check([](F&f){f.put<int32_t>(F::Entry+0x140,0);f.mem.erase(F::Message);},true,0);
  check([](F&f){f.string(F::Type,"Other");},false);
  check([](F&f){f.string(F::Entry+0x54,"../file");},false);
  check([](F&f){f.string(F::Message,"");},false);
  check([](F&f){f.string(F::Message,"Bad#Label");},false);
  check([](F&f){f.put<int32_t>(F::Q+0xB8,2);f.put<int32_t>(F::Q+0xBC,2);f.put(F::Array+8,F::Entry);},false);
  for(auto at:{F::M+0x2CD1700,F::Q+0xB8,F::Array,F::Entry,F::Steps,F::Step+0x48,F::Message,F::Type,F::Destination,F::Dlc2,F::U+0x64C30})check([&](F&f){f.race=at;},false);
  for(auto flags:{0x1849000000ULL,0x0E09E00000ULL,0x1E09000001ULL})check([&](F&f){f.put<uint64_t>(F::U+0x64C30,flags);},false);
  {F f;f.put<int32_t>(F::Destination+24,-1);const auto q=botw::quests(f.h);assert(q.valid&&!q.entries[0].active);++cases;}
  {F f;State s;sample(&s,&f.h);assert(f.values["botw.quest.valid"]==1&&f.values["botw.ready.quest.valid"]==1);assert(on_action(&s,"quest_pane",-1));assert(s.quest_pane==3);assert(on_action(&s,"quest_category",1));assert(!on_action(&s,"quest_choose",0));s.next={};f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);sample(&s,&f.h);assert(f.values["botw.quest.count"]==0&&f.values["botw.ready.quest.valid"]==0);assert(!on_action(&s,"quest_category",0));++cases;}
  // Side quests at step 0 remain invisible even with no message to read (unlike the
  // Main-quest case above, whose message *is* required -- see the "false" case there).
  check([](F&f){f.string(F::Type,"Sub");f.put<int32_t>(F::Entry+0x140,0);f.mem.erase(F::Message);},true,0);
  // Leaving World/menu clears the journal; re-entry must re-read it at once rather than
  // keep the cleared journal until the 500 ms cadence (the Map reader already recovers so).
  {F f;State s;sample(&s,&f.h);assert(f.values["botw.quest.valid"]==1);
   s.next={};f.put<uint64_t>(F::U+0x64C30,0x0E09E00000ULL);sample(&s,&f.h);assert(f.values["botw.quest.valid"]==0&&f.values["botw.ready.quest.valid"]==0);
   s.next={};f.put<uint64_t>(F::U+0x64C30,0x1E09000000ULL);sample(&s,&f.h);assert(f.values["botw.quest.valid"]==1&&f.values["botw.ready.quest.valid"]==1);++cases;}
 // Tracker-only catalog includes unrevealed entries without dereferencing their message.
  {F f;f.put<int32_t>(F::Entry+12,0);f.put<int32_t>(F::Entry+0x140,-1);f.mem.erase(F::Message);
   auto q=botw::quests(f.h,true);assert(q.valid&&q.entries.size()==1&&q.entries[0].state==0&&q.entries[0].message.empty()&&!q.entries[0].active&&!q.entries[0].revealed);++cases;}
  {F f;f.string(F::Type,"Sub");f.put<int32_t>(F::Entry+0x140,0);f.mem.erase(F::Message);auto q=botw::quests(f.h,true);
   assert(q.valid&&q.entries.size()==1&&q.entries[0].message.empty()&&!q.entries[0].active&&!q.entries[0].revealed);++cases;}
  // Main-quest counterpart: tracker mode lists it but it is not revealed at step 0 either.
  {F f;f.put<int32_t>(F::Entry+0x140,0);auto q=botw::quests(f.h,true);
   assert(q.valid&&q.entries.size()==1&&!q.entries[0].revealed&&q.entries[0].message.empty()&&!q.entries[0].active);++cases;}
  for(auto aoc:{2u,4u,6u}) {F f;f.put<int32_t>(F::Entry+12,0);f.put<uint32_t>(F::Entry+0xD8,aoc);
   auto q=botw::quests(f.h,true);assert(q.valid&&q.entries.empty());++cases;}
  {F f;f.put<int32_t>(F::Entry+12,0);f.put<uint8_t>(F::Entry+0x30,0);auto q=botw::quests(f.h,true);assert(q.valid&&q.entries.empty());++cases;}
  {F f;f.put<int32_t>(F::Entry+12,0);f.race=F::Type;assert(!botw::quests(f.h,true).valid);++cases;}
  {F f;f.put<int32_t>(F::Entry+12,0);f.string(F::Type,"Other");assert(!botw::quests(f.h,true).valid);++cases;}
  // Side/shrine quests: native state1 alone is not acceptance -- current_step0 remains
  // unrevealed (Main quests too since iteration 14; see above).
  {F f;f.string(F::Type,"Sub");f.put<int32_t>(F::Entry+12,1);f.put<int32_t>(F::Entry+0x140,0);
   const auto normal=botw::quests(f.h);const auto tracker=botw::quests(f.h,true);
   assert(normal.valid&&normal.entries.empty()&&tracker.valid&&tracker.entries.size()==1&&tracker.entries[0].state==1&&!tracker.entries[0].revealed&&!tracker.entries[0].active);++cases;}
  {F f;f.put<int32_t>(F::Entry+12,2);const auto tracker=botw::quests(f.h,true);
   assert(tracker.valid&&tracker.entries.size()==1&&tracker.entries[0].state==2&&tracker.entries[0].revealed);++cases;}
  // Iteration 14 item 7: native Adventure Log order = open quests, then completed DLC quests,
  // then completed base quests, each in quest-data order (live save B + DLC: Destroy Ganon,
  // EX Champions' Ballad..Revali, then Follow the Sheikah Slate, The Isolated Plateau, ...).
  {State s;s.quest_category=0;
   auto mk=[](int idx,int state,uint32_t dlc){botw::Quest q;q.index=idx;q.state=state;q.dlc=dlc;q.category=0;return q;};
   s.quests.entries={mk(0,2,0),mk(1,2,0),mk(2,1,0),mk(13,2,4),mk(14,2,4),mk(18,2,0),mk(20,1,4)};
   const auto j=quest_journal(s);std::vector<int> got;for(auto* q:j)got.push_back(q->index);
   assert((got==std::vector<int>{2,20,13,14,0,1,18}));++cases;}
 std::cout<<cases<<" quest cases passed\n";
}
