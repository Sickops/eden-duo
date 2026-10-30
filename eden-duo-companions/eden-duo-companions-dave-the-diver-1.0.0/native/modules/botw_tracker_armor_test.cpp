// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_tracker_armor.h"
#include <cassert>
#include <iostream>
using namespace botw;using namespace botw::trackers;
int main() {
 int cases=0;auto check=[&](bool x){++cases;if(!x)std::cerr<<"failed "<<cases<<"\n";assert(x);};
 auto item=[](const char* a,int type,int qty,std::uint64_t p){return Item{a,type,qty,false,p};};
 Snapshot s{true,{item("Armor_001_Head",4,0,100),item("Armor_001_Head",4,0,200),item("Item_Enemy_00",7,7,300)}};
 ArmorPlanner planner;auto p=planner.plan(s,1);check(p.valid&&p.selected_count==0&&p.materials.empty()&&p.rows.size()==2);
 check(planner.choose(s,100,1,1));p=planner.plan(s,1);check(p.valid&&p.materials.size()==1&&p.materials[0].required==5&&p.materials[0].owned==7&&p.materials[0].missing==0);
 check(planner.choose(s,200,1,1));p=planner.plan(s,1);check(p.materials[0].required==10&&p.materials[0].owned==7&&p.materials[0].missing==3&&p.selected_count==2);
 s.items[2].value=0;p=planner.plan(s,1);check(p.valid&&p.materials[0].missing==10);
 s.items[2].value=-1;p=planner.plan(s,1);check(!p.valid);s.items[2].value=7;
 check(planner.choose(s,100,0,1));check(planner.plan(s,1).selected_count==1);
 check(!planner.choose(s,999,1,1));check(!planner.choose(s,100,5,1));check(!planner.choose(s,300,1,1));
 s.items[1].actor="Armor_002_Head";p=planner.plan(s,1);check(p.valid&&p.cancelled_count==1&&p.selected_count==0);
 check(planner.choose(s,200,4,1));p=planner.plan(s,1);check(p.valid&&p.rows[1].current_tier==1&&p.rows[1].target_tier==4);
 p=planner.plan(s,2);check(p.valid&&p.selected_count==0&&planner.targets().empty());
 check(planner.choose_all(s,4,2));p=planner.plan(s,2);check(p.selected_count==2);
 // Hylian hood native full chain: horns5 then horns8+fangs5, fangs10+guts5, guts15+amber15.
 std::map<std::string,int> required;for(const auto& m:p.materials)required[m.actor]=m.required;
 check(required["Item_Enemy_00"]==21);check(required["Item_Enemy_01"]==30);check(required["Item_Enemy_02"]==40);check(required["Item_Ore_F"]==30);
 s.items.erase(s.items.begin()+1);p=planner.plan(s,2);check(p.valid&&p.cancelled_count==1&&p.selected_count==1);
 s.items.push_back(item("Armor_172_Head",4,0,400));check(!planner.choose(s,400,4,2));p=planner.plan(s,2);check(p.valid&&!p.rows.back().upgradeable&&p.rows.back().max_tier==0);
 auto invalid=s;invalid.valid=false;check(!planner.plan(invalid,2).valid);
 invalid=s;invalid.items[1].address=100;check(!planner.plan(invalid,2).valid);
 invalid=s;invalid.items[0].actor="Armor_001_Head_B";check(!planner.plan(invalid,2).valid);
 invalid=s;invalid.items[0].type=5;check(!planner.plan(invalid,2).valid);
 check(!planner.plan(s,0).valid);check(planner.targets().empty());
 // Exhaust all284 native upgrade edges, preserving individual actor tier and cumulative recipe shape.
 int edges=0;
 for(const auto& meta:armor_tiers) if(meta.tracked&&!meta.next_actor.empty()) {
  ++edges;Snapshot one{true,{{std::string(meta.actor),int(meta.slot)+4,0,false,0x10000}}};ArmorPlanner test;
  check(test.choose(one,0x10000,meta.tier+1,3));auto result=test.plan(one,3);check(result.valid&&result.selected_count==1);
  int total=0;for(const auto& m:result.materials){total+=m.required;check(m.owned==0&&m.missing==m.required);}
  int expected=0;for(unsigned i=0;i<meta.cost_count;++i)expected+=armor_costs[meta.cost_begin+i].quantity;
  check(total==expected);
 }
 check(edges==284);
 std::cout<<cases<<" armor planner assertions passed\n";
}
