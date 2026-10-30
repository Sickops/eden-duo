// SPDX-License-Identifier: GPL-3.0-or-later
#include "botw_tracker_data.h"
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
struct Flags {
 std::unordered_set<uint32_t> program_hidden;
 std::unordered_map<uint32_t,bool> b;std::unordered_map<uint32_t,int32_t> n;
 std::optional<bool> boolean(uint32_t hash){if(program_hidden.contains(hash))return {};auto i=b.find(hash);if(i==b.end())return {};return i->second;}
 std::optional<bool> observe_boolean(uint32_t hash){auto i=b.find(hash);if(i==b.end())return {};return i->second;}
 std::optional<int32_t> integer(uint32_t hash){auto i=n.find(hash);if(i==n.end())return {};return i->second;}
};
int main(){unsigned cases{};auto check=[&](bool b){assert(b);++cases;};
 Flags f;botw::MapLandmarks map;botw::QuestSnapshot q;
 auto data=[&](std::optional<bool> dlc=false,bool valid=true){return botw::tracker_data_records(f,map,q,dlc,valid);};
 auto d=data();check(d.valid&&d.categories[0].size()==1051&&d.categories[1].size()==84&&d.categories[3].size()==17&&d.categories[4].size()==3);
 for(int i=0;i<900;++i){assert(d.categories[0][i].detail.starts_with("Puzzle: ")&&d.categories[0][i].detail.size()>8);}++cases;
 check(d.categories[0][0].state==-1&&d.categories[1][0].state==-1&&d.categories[4][0].state==-1);
 check(d.categories[2].size()==390&&d.summary[2].find("Master Mode unknown")!=std::string::npos);
 f.b[0xE51256E2]=false;d=data();check(d.categories[2].size()==385);
 f.b[0xE51256E2]=true;d=data();check(d.categories[2].size()==390);d=data(true);check(d.categories[2].size()==394);
 f.b[botw::tracker_koroks[0].flag_hash]=true;d=data();check(d.categories[0][0].state==2&&d.categories[0][0].y==-botw::tracker_koroks[0].z);
 f.b[botw::tracker_bosses[0].flag_hash]=false;d=data();check(d.categories[1][0].state==0&&d.categories[1][0].status=="Not yet defeated");
 f.b[botw::tracker_bosses[0].flag_hash]=true;d=data();check(d.categories[1][0].state==2);
 const auto sid=std::string(botw::tracker_shrines[0].id);f.n[botw::tracker_crc("Location_"+sid)]=1;f.b[botw::tracker_crc("Clear_"+sid)]=false;d=data();check(d.categories[0][900].group==1&&d.categories[0][900].state==1&&d.categories[0][900].detail=="Active unknown · All chests unknown");
 f.b[botw::tracker_crc("Clear_"+sid)]=true;f.b[botw::tracker_crc("CompleteTreasure_"+sid)]=false;d=data();check(d.categories[0][900].state==2&&d.categories[0][900].detail.find("All chests no")!=std::string::npos);
 f.b[botw::tracker_crc("Clear_Dungeon120")]=true;d=data(false);check(d.categories[0][1020].state==-1&&d.categories[0][1020].location&&d.categories[0][1020].group==3&&d.categories[0][1020].detail=="Progress unavailable");d=data(true);check(d.categories[0][1020].state==2);
 for(int i=0;i<16;++i){const auto& r=d.categories[0][1020+i];const auto& l=botw::tracker_dlc_shrine_locations[i];assert(r.id==l.id&&r.location&&r.x==l.x&&r.y==-l.z&&r.group==3);}++cases;
 const auto& tower=botw::tracker_towers[0];map.valid=true;map.count=1;map.entries[0].kind=3;map.entries[0].xyz={tower.x,tower.y,tower.z};map.entries[0].opened=true;d=data();check(d.categories[0][1036].state==2);
 map.count=2;map.entries[1]=map.entries[0];d=data();check(d.categories[0][1036].state==-1);map.count=1;map.entries[0].xyz[0]+=2;d=data();check(d.categories[0][1036].state==-1);
 for(const auto& e:botw::tracker_hestu_upgrades)f.b[e.flag_hash]=false;
 d=data();check(d.categories[4][0].state==0&&d.categories[4][0].detail=="Spent: 0 seeds · Next: 1 seed");
 for(const auto& e:botw::tracker_hestu_upgrades)f.b[e.flag_hash]=true;
 d=data();check(d.categories[4][0].state==2&&d.categories[4][0].detail=="Spent: 208 seeds · Fully expanded");
 for(const auto& e:botw::tracker_hestu_upgrades)if(std::string_view(e.category)=="weapon"&&e.level==2)f.b[e.flag_hash]=false;
 d=data();check(d.categories[4][0].state==-1);
 for(const auto& e:botw::tracker_hestu_upgrades)if(std::string_view(e.category)=="bow"&&e.level==1)f.b.erase(e.flag_hash);
 d=data();check(d.categories[4][1].state==-1);
 q.valid=true;for(int state=0;state<3;++state){botw::Quest e;e.name="Fixture"+std::to_string(state);e.state=state;e.revealed=state>0;e.category=state;e.address=0x10000+state*8;q.entries.push_back(e);}d=data();check(d.categories[6].size()==3&&d.categories[6][0].state==0&&d.categories[6][1].state==1&&d.categories[6][2].state==2);
 q.entries[1].revealed=false;d=data();check(d.categories[6][1].state==0&&d.categories[6][1].status=="Not started");
 q.entries[1].revealed=true;d=data();check(d.categories[6][1].state==1&&d.categories[6][1].status=="Active");
 q.entries[2].revealed=false;d=data();check(d.categories[6][2].state==0);q.entries[2].revealed=true;
 d=data(false,false);check(!d.valid);for(auto& cat:d.categories)for(auto& r:cat)assert(r.state==-1&&r.status=="Unavailable");++cases;
 for(const auto& e:botw::tracker_hestu_upgrades){f.b[e.flag_hash]=true;f.program_hidden.insert(e.flag_hash);}
 f.program_hidden.insert(botw::tracker_koroks[0].flag_hash);d=data();
 check(d.categories[4][0].state==2&&d.categories[4][1].state==2&&d.categories[4][2].state==2&&d.categories[0][0].state==-1);
 check(botw::tracker_crc("AoC_HardMode_Enabled")==0xE51256E2u);
 std::cout<<cases<<" tracker data cases passed\n";
}
