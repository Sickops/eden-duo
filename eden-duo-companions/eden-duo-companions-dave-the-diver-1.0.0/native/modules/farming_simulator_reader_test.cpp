// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic guest memory only; no game image, emulator or private asset
// required.
#include "farming_simulator_reader.cpp"
namespace farming_simulator_reader {
namespace {
#include "farming_simulator_catalog_reader.inc"
}
} // namespace farming_simulator_reader
#include <cstdio>
#include <functional>
#include <limits>

namespace {
using farming_simulator::Phase;
using farming_simulator::Snapshot;
using farming_simulator_reader::Reader;
constexpr uint64_t Main = 0x80000000;
constexpr uint64_t Nil = UINT64_MAX;
constexpr uint64_t True = UINT64_C(0xfffe800000000000);
constexpr uint64_t False = UINT64_C(0xffff000000000000);
uint64_t Table(uint64_t p) { return UINT64_C(0xfffa000000000000) | p; }
uint64_t String(uint64_t p) { return UINT64_C(0xfffd800000000000) | p; }
uint64_t Number(double n) {
  uint64_t b;
  std::memcpy(&b, &n, 8);
  return b;
}
void Require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}

struct Memory {
  std::map<uint64_t, std::array<uint8_t, 4096>> pages;
  std::map<std::pair<uint64_t, std::string>, uint64_t> slots;
  std::map<uint64_t, unsigned> next_slot;
  std::map<std::string, int64_t> published;
  std::map<std::string, std::string> published_text;
  std::vector<std::string> logs;
  uint64_t cursor = UINT64_C(0x2100000000);
  uint64_t object{}, state{}, env{}, mission{}, map{}, vehicle{}, player{};
  uint64_t tear_address{};
  std::function<void(uint64_t, size_t, void *)> read_hook;
  size_t reads{}, bytes{};
  bool fail_reads{};
  EdenDsmodHostApi host{};

  void PutBytes(uint64_t at, const void *source, size_t count) {
    auto *input = static_cast<const uint8_t *>(source);
    while (count) {
      size_t offset = at & 4095, take = std::min(count, 4096 - offset);
      std::memcpy(pages[at & ~UINT64_C(4095)].data() + offset, input, take);
      input += take;
      at += take;
      count -= take;
    }
  }
  template <class T> void Put(uint64_t at, T value) {
    PutBytes(at, &value, sizeof(value));
  }
  template <class T> T Get(uint64_t at) const {
    T value{};auto page=pages.find(at&~UINT64_C(4095));
    Require(page!=pages.end() && (at&4095)+sizeof(T)<=4096,"fixture read unmapped");
    std::memcpy(&value,page->second.data()+(at&4095),sizeof(value));return value;
  }
  uint64_t Allocate(size_t size, bool page = false) {
    if (page)
      cursor = (cursor + 4095) & ~UINT64_C(4095);
    auto address = cursor;
    cursor += (size + 7) & ~size_t(7);
    std::vector<uint8_t> zero(size);
    PutBytes(address, zero.data(), size);
    return address;
  }
  uint64_t NewString(std::string_view text, bool page = false) {
    auto p = Allocate(24 + text.size(), page);
    Put<uint8_t>(p + 9, 4);
    Put<uint32_t>(p + 16, text.size());
    PutBytes(p + 24, text.data(), text.size());
    return p;
  }
  uint64_t NewTable() {
    auto p = Allocate(4096, true);
    Put<uint8_t>(p + 9, 11);
    Put<uint64_t>(p + 40, p + 64);
    Put<uint32_t>(p + 52, 63);
    return p;
  }
  uint64_t CleanTable() {
    auto p = NewTable();
    for (unsigned i = 0; i < 64; ++i)
      Put<uint64_t>(p + 64 + 24 * i, Nil);
    return p;
  }
  uint64_t Array(const std::vector<uint64_t> &values) {
    auto t = CleanTable(), p = Allocate((values.size() + 1) * 8);
    Put<uint64_t>(t + 16, p);
    Put<uint32_t>(t + 48, values.size() + 1);
    Put<uint64_t>(p, Nil);
    for (size_t i = 0; i < values.size(); ++i)
      Put<uint64_t>(p + (i + 1) * 8, values[i]);
    return t;
  }
  void Entry(uint64_t table, uint64_t key, uint64_t value) {
    auto slot = table + 64 + 24 * next_slot[table]++;
    Require(next_slot[table] <= 64, "fixture entry overflow");
    Put<uint64_t>(slot, key == Nil ? Nil : value);
    Put<uint64_t>(slot + 8, key);
  }
  void Field(uint64_t table, std::string name, uint64_t value) {
    auto key = std::make_pair(table, name);
    auto found = slots.find(key);
    if (found == slots.end()) {
      auto slot = table + 64 + 24 * next_slot[table]++;
      Require(next_slot[table] <= 64, "fixture table overflow");
      Put<uint64_t>(slot + 8, String(NewString(name)));
      slots[key] = slot;
      found = slots.find(key);
    }
    Put<uint64_t>(found->second, value);
  }
  void Num(uint64_t table, const char *name, double value) {
    Field(table, name, Number(value));
  }
  bool Mapped(uint64_t address, uint64_t size) const {
    if (!size || address > UINT64_MAX - size)
      return false;
    for (uint64_t at = address & ~UINT64_C(4095);
         at <= ((address + size - 1) & ~UINT64_C(4095)); at += 4096)
      if (!pages.count(at))
        return false;
    return true;
  }
  Memory() {
    host.userdata = this;
    host.main_base = Main;
    host.main_size = 0xd18000;
    host.is_mapped = [](void *p, uint64_t a, uint64_t n) -> EdenDsmodBool {
      return static_cast<Memory *>(p)->Mapped(a, n);
    };
    host.read_memory = [](void *p, uint64_t a, void *output,
                          size_t n) -> EdenDsmodBool {
      auto &m = *static_cast<Memory *>(p);
      ++m.reads;
      m.bytes += n;
      if (m.fail_reads || !m.Mapped(a, n))
        return false;
      auto *out = static_cast<uint8_t *>(output);
      auto count = n;
      while (count) {
        auto offset = a & 4095, take = std::min(count, size_t(4096 - offset));
        std::memcpy(out, m.pages.at(a & ~UINT64_C(4095)).data() + offset, take);
        out += take;
        a += take;
        count -= take;
      }
      if (n == 16 && a - n == m.tear_address)
        std::memcpy(output, &Nil, 8);
      if (m.read_hook)
        m.read_hook(a - n, n, output);
      return true;
    };
    host.publish_i64 = [](void *p, const char *key, int64_t value) {
      static_cast<Memory *>(p)->published[key] = value;
    };
    host.publish_text = [](void *p,const char *key,const char *value) {
      static_cast<Memory *>(p)->published_text[key]=value?value:"";
    };
    host.log = [](void *p,uint32_t,const char *message) {
      static_cast<Memory *>(p)->logs.emplace_back(message?message:"");
    };
    object = Allocate(64);
    state = Allocate(128);
    env = NewTable();
    mission = NewTable();
    Put<uint64_t>(Main + 0x46cb40, object);
    Put<uint64_t>(object, state);
    Put<uint8_t>(state + 9, 6);
    Put<uint64_t>(state + 72, env);
    Field(env, "_G", Table(env));
    Field(env, "g_currentMission", Nil);
  }
  void World() {
    Field(env, "g_currentMission", Table(mission));
    Field(mission, "isRunning", True);
    Field(mission, "isLoaded", True);
    Field(mission, "isLoadingMap", Nil);
    Field(mission, "controlledVehicle", Nil);
    auto info = NewTable();
    Field(info, "mapId", String(NewString("Map02")));
    Field(mission, "missionInfo", Table(info));
    auto stats = NewTable();
    Num(stats, "money", 123456.75);
    Field(mission, "missionStats", Table(stats));
    auto time = NewTable();
    Num(time, "currentHour", 14);
    Num(time, "currentMinute", 32);
    Field(mission, "environment", Table(time));
    map = NewTable();
    Field(mission, "ingameMap", Table(map));
    Num(map, "normalizedPlayerPosX", .615234375);
    Num(map, "normalizedPlayerPosZ", .4736328125);
    Num(map, "worldSizeX", 2048);
    Num(map, "worldSizeZ", 2048);
    Num(map, "worldCenterOffsetX", 1024);
    Num(map, "worldCenterOffsetZ", 1024);
    Num(map, "playerRotation", .78539833);
    auto i18n = NewTable();
    Num(i18n, "moneyUnit", 2);
    Field(env, "g_i18n", Table(i18n));
    Num(env, "GS_MONEY_EURO", 1);
    Num(env, "GS_MONEY_DOLLAR", 2);
    Num(env, "GS_MONEY_POUND", 3);
  }
  uint64_t Vehicle(double fuel, double fill) {
    vehicle = NewTable();
    Field(vehicle, "isEntered", True);
    Field(vehicle, "isAddedToMission", True);
    Num(vehicle, "fuelFillLevel", fuel);
    Num(vehicle, "fuelCapacity", 200);
    auto units = NewTable(), unit = NewTable();
    Put<uint64_t>(units + 16, units + 2048);
    Put<uint32_t>(units + 48, 2);
    Put<uint64_t>(units + 2056, Table(unit));
    Num(unit, "fillLevel", fill);
    Num(unit, "capacity", 5000);
    Field(vehicle, "fillUnits", Table(units));
    Field(mission, "controlledVehicle", Table(vehicle));
    return vehicle;
  }
  void ScenePlayer(uint32_t id,double x,double z,double heading) {
    constexpr uint64_t manager=Main+0x46c918;constexpr uint64_t buckets_count=8;
    auto buckets=Allocate(buckets_count*8),predecessor=Allocate(8),entry=Allocate(32);
    auto node=Allocate(0x800);
    Put<uint64_t>(manager,buckets);Put<uint64_t>(manager+8,buckets_count);
    auto bucket=id%buckets_count;Put<uint64_t>(buckets+bucket*8,predecessor);
    Put<uint64_t>(predecessor,entry);Put<uint64_t>(entry+8,id);
    Put<uint32_t>(entry+16,id);Put<uint64_t>(entry+24,node);
    Put<uint64_t>(node+16,0x80);
    std::array<float,16> local{float(std::cos(heading)),0,float(-std::sin(heading)),0,
      0,1,0,0,float(std::sin(heading)),0,float(std::cos(heading)),0,
      float(x),0,float(z),1};
    Put<uint64_t>(node+0x20,0);PutBytes(node+0x78,local.data(),64);
    player=NewTable();Num(player,"rootNode",id);Num(player,"rotY",heading);
    Field(mission,"player",Table(player));
  }
  Snapshot Sample(Reader &r) {
    reads = bytes = 0;
    r.Sample(host);
    return r.GetSnapshot();
  }
};

void Clear(const Snapshot &s, Phase expected) {
  Require(s.phase == expected, "unexpected empty-state phase");
  Require(!s.world_ready && !s.vehicle_ready && !s.world_identity && !s.map_id,
          "stale validity/identity");
  Require(!s.player_x && !s.player_z && !s.player_heading && !s.money &&
              !s.time_minutes,
          "stale world values");
  Require(!s.fuel_l && !s.fill_l && !s.fuel_capacity_l && !s.fill_capacity_l &&
              !s.speed_kmh,
          "stale vehicle values");
  Require(s.currency_symbol.empty() && s.vehicle_name.empty() &&
              s.vehicle_image.empty(),
          "stale text/art");
}
void ValidWorld(const Snapshot &s, uint64_t identity) {
  Require(s.phase == Phase::World && s.world_ready &&
              s.world_identity == identity,
          "world root not resolved");
  Require(s.map_id == 2 && s.player_x == 236 && s.player_z == -54,
          "map coordinate conversion");
  Require(s.player_heading && std::abs(*s.player_heading - .78539833) < 1e-9,
          "native heading lost");
  Require(s.money == 123456.75 && s.time_minutes == 872 &&
              s.currency_symbol == "$",
          "HUD data mismatch");
}
} // namespace

int main() {
  unsigned passed{}, failed{};
  auto run = [&](const char *name, const std::function<void()> &test) {
    try {
      test();
      ++passed;
      std::printf("PASS %s\n", name);
    } catch (const std::exception &e) {
      ++failed;
      std::printf("FAIL %s: %s\n", name, e.what());
    }
  };
  run("root title and null/unmapped chain", [] {
    Memory m;
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    Clear(m.Sample(r), Phase::Title);
    m.Put<uint64_t>(Main + 0x46cb40, 0);
    Clear(m.Sample(r), Phase::Unknown);
    m.Put<uint64_t>(Main + 0x46cb40, 0x12345000);
    Clear(m.Sample(r), Phase::Unknown);
    m.host.main_size = 0x46cb40;
    Clear(m.Sample(r), Phase::Unknown);
    Require(!m.reads, "undersized main read");
  });
  run("loading gates and fresh on-foot HUD", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    m.Field(m.mission, "isLoaded", Nil);
    Clear(m.Sample(r), Phase::Loading);
    m.Field(m.mission, "isLoaded", True);
    m.Field(m.mission, "isLoadingMap", True);
    Clear(m.Sample(r), Phase::Loading);
    m.Field(m.mission, "isLoadingMap", Nil);
    auto s = m.Sample(r);
    ValidWorld(s, m.mission);
    Require(!s.vehicle_ready && !s.fuel_l && !s.fill_l,
            "on-foot vehicle leaked");
  });
  run("vehicle switch exit and lost world clear stale data", [] {
    Memory m;
    m.World();
    m.Vehicle(75, 1900);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto s = m.Sample(r);
    ValidWorld(s, m.mission);
    Require(s.vehicle_ready && s.fuel_l == 75 && s.fill_l == 1900,
            "first vehicle");
    m.Vehicle(12, 400);
    s = m.Sample(r);
    Require(s.vehicle_ready && s.fuel_l == 12 && s.fill_l == 400,
            "vehicle changed but old values remain");
    m.Field(m.mission, "controlledVehicle", Nil);
    s = m.Sample(r);
    ValidWorld(s, m.mission);
    Require(!s.vehicle_ready && !s.fuel_l && !s.fill_l && !s.fill_capacity_l,
            "vehicle exit retained values");
    m.Field(m.mission, "isRunning", Nil);
    Clear(m.Sample(r), Phase::Loading);
    m.Field(m.env, "g_currentMission", Nil);
    Clear(m.Sample(r), Phase::Title);
    m.fail_reads = true;
    Clear(m.Sample(r), Phase::Unknown);
  });
  run("malformed GC table string and self-global rejected", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto t0 = std::chrono::steady_clock::now();
    r.SetClockForTesting(t0);
    ValidWorld(m.Sample(r), m.mission);
    // Each of these corruptions throws out of the primary reader, but none
    // of them alone should blank a world that was already showing good
    // data -- the reader keeps the last good snapshot until the failures
    // have run past kStaleAfterGoodTimeout (see below).
    m.Put<uint8_t>(m.state + 9, 4);
    r.SetClockForTesting(t0 + std::chrono::milliseconds(100));
    ValidWorld(m.Sample(r), m.mission);
    m.Put<uint8_t>(m.state + 9, 6);
    m.Put<uint8_t>(m.env + 9, 6);
    r.SetClockForTesting(t0 + std::chrono::milliseconds(200));
    ValidWorld(m.Sample(r), m.mission);
    m.Put<uint8_t>(m.env + 9, 11);
    m.Put<uint32_t>(m.env + 52, 100);
    r.SetClockForTesting(t0 + std::chrono::milliseconds(300));
    ValidWorld(m.Sample(r), m.mission);
    m.Put<uint32_t>(m.env + 52, 63);
    m.Field(m.env, "_G", Table(m.mission));
    r.SetClockForTesting(t0 + std::chrono::milliseconds(400));
    ValidWorld(m.Sample(r), m.mission);
    m.Field(m.env, "_G", Table(m.env));
    auto key = m.slots.at({m.env, "_G"}) + 8;
    m.Put<uint64_t>(key, String(0x12345000));
    r.SetClockForTesting(t0 + std::chrono::milliseconds(500));
    ValidWorld(m.Sample(r), m.mission);
    // Past the staleness timeout the still-failing read finally reports an
    // empty state instead of showing the stale world forever.
    r.SetClockForTesting(t0 + std::chrono::seconds(6));
    Clear(m.Sample(r), Phase::Unknown);
  });
  run("nonfinite and out-of-range values withheld", [] {
    Memory m;
    m.World();
    m.Vehicle(-1, 600);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    m.Num(m.map, "normalizedPlayerPosX",
          std::numeric_limits<double>::quiet_NaN());
    auto s = m.Sample(r);
    Require(s.world_ready && s.vehicle_ready, "bad optional invalidated world");
    Require(!s.player_x && !s.player_z && !s.player_heading && !s.fuel_l &&
                s.fill_l == 600,
            "bad optionals accepted or unrelated lost");
    m.Num(m.map, "normalizedPlayerPosX", .5);
    m.Num(m.map, "normalizedPlayerPosZ", 1.1);
    s = m.Sample(r);
    Require(!s.player_x && !s.player_z, "out-of-range map position");
  });
  run("native dynamic map hotspots retain sprite and rotation contract", [] {
    Memory m;m.World();
    auto category=Number(10), classes=m.NewTable();
    m.Field(classes,"CATEGORY_VEHICLE_STEERABLE",category);
    m.Field(classes,"CATEGORY_VEHICLE_COMBINE",Number(9));
    m.Field(m.env,"MapHotspot",Table(classes));
    auto marker=m.NewTable();m.Num(marker,"xMapPos",125);m.Num(marker,"zMapPos",-42);
    m.Num(marker,"category",10);m.Num(marker,"objectId",0);
    auto uv=m.Array({Number(.25),Number(.5),Number(.25),Number(.75),
                     Number(.375),Number(.5),Number(.375),Number(.75)});
    m.Field(marker,"imageUVs",Table(uv));
    auto tint=m.Array({Number(.2705),Number(.6514),Number(.0802),Number(1)});
    m.Field(marker,"baseColor",Table(tint));
    m.Field(marker,"enabled",True);m.Num(marker,"width",.02);m.Num(marker,"height",.03);
    auto foreground=m.NewTable();m.Field(foreground,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_256.png")));
    m.Num(foreground,"rotation",1.25);m.Num(foreground,"width",.02);m.Num(foreground,"height",.03);
    auto foreground_hi=m.NewTable();m.Field(foreground_hi,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_1024.png")));
    m.Num(foreground_hi,"rotation",0);m.Num(foreground_hi,"width",.02);m.Num(foreground_hi,"height",.03);
    auto overlays=m.Array({Table(foreground),Table(foreground_hi)});m.Field(marker,"overlays",Table(overlays));
    auto bg=m.NewTable();m.Field(bg,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_256.png")));
    m.Field(bg,"uvs",Table(uv));m.Num(bg,"r",.0075);m.Num(bg,"g",.0075);
    m.Num(bg,"b",.0075);m.Num(bg,"a",1);m.Field(marker,"overlayBg",Table(bg));
    auto moving=m.NewTable();m.Num(moving,"xMapPos",900);m.Num(moving,"zMapPos",900);
    m.Num(moving,"category",9);m.Num(moving,"objectId",187);
    m.Field(moving,"name",String(m.NewString("Combine")));
    m.Field(moving,"imageUVs",Table(uv));m.Field(moving,"overlays",Table(overlays));
    auto no_render_last=m.Array({Table(marker)}),render_last=m.Array({Table(moving)});
    auto list=m.NewTable();m.Entry(list,False,Table(no_render_last));m.Entry(list,True,Table(render_last));
    m.Field(m.map,"hotspots_sorted",Table(list));
    m.ScenePlayer(187,143,-22,.5);
    Reader r(m.host,"");r.SetDiagnostics(true);auto s=m.Sample(r);
    Require(s.map_markers.size()==2,"boolean-keyed native hotspot arrays omitted");
    auto steerable=std::find_if(s.map_markers.begin(),s.map_markers.end(),
                                [](const auto &entry){return entry.kind=="VEHICLE_STEERABLE";});
    Require(steerable!=s.map_markers.end(),"steerable hotspot category missing");
    const auto &h=*steerable;
    Require(h.x==125 && h.z==-42 && h.rotation==0,
            "native category, position or radians lost");
    Require(h.image=="dataS2/menu/hud/mapHotspots_1024.png" && h.sx==256 && h.sy==256 && h.w==128 && h.h==256,
            "native atlas sprite rectangle lost");
    Require(h.draw_w==26 && h.draw_h==22,"native virtual-screen marker size lost");
    Require(h.color==0x8ed350ff,"native linear-RGB marker tint changed");
    Require(h.background_image=="dataS2/menu/hud/mapHotspots_256.png" &&
                h.background_w==32 && h.background_color==0x151515ff &&
                h.enabled && !h.object_id,
            "native background, enabled state or object id lost");
    auto found=std::find_if(s.map_markers.begin(),s.map_markers.end(),
                            [](const auto &m){return m.kind=="VEHICLE_COMBINE";});
    Require(found!=s.map_markers.end() && found->object_id==187 &&
                found->x==143 && found->z==-22 && found->rotation==0,
            "moving hotspot used frozen Lua coordinates instead of its scene node");
  });
  run("tip-trigger selling-point hotspot resolves its real station name and filter "
      "classification by correlating mission.tipTriggers",
      [] {
    // Owner-reported live bug (2026-10-17): a real selling point's own map hotspot
    // carries no "stationName"/"isSellingPoint" of its own and a generic CATEGORY_TRIGGER
    // category -- confirmed live against a grain-elevator-style station on Goldcrest
    // Valley. The fix correlates by world position against mission.tipTriggers, the exact
    // registry the Prices reader already walks for the exact same isSellingPoint/
    // appearsOnStats pair and the exact same localized stationName.
    Memory m;m.World();
    auto classes=m.NewTable();
    m.Field(classes,"CATEGORY_TRIGGER",Number(16));
    m.Field(m.env,"MapHotspot",Table(classes));
    // The real tip trigger: its own world position comes from its "triggerId" scene node,
    // the same "live node id -> world transform" path the moving-marker test above uses.
    m.ScenePlayer(901,300,-150,0);
    auto registry=m.NewTable(),trigger=m.NewTable();
    m.Field(m.mission,"tipTriggers",Table(registry));
    m.Entry(registry,Table(trigger),True);
    m.Field(trigger,"isSellingPoint",True);
    m.Field(trigger,"appearsOnStats",True);
    m.Num(trigger,"triggerId",901);
    m.Field(trigger,"stationName",String(m.NewString("Goldcrest Grain Elevator")));
    // The map hotspot for the SAME real-world station: no stationName/isSellingPoint of
    // its own (the live gap this test guards), only the generic CATEGORY_TRIGGER, close to
    // but not exactly at the trigger's own world position (nearest-within-radius match).
    auto hotspot=m.NewTable();
    m.Num(hotspot,"xMapPos",305);m.Num(hotspot,"zMapPos",-146);
    m.Num(hotspot,"category",16);
    auto uv=m.Array({Number(.25),Number(.5),Number(.25),Number(.75),
                     Number(.375),Number(.5),Number(.375),Number(.75)});
    m.Field(hotspot,"imageUVs",Table(uv));
    m.Field(hotspot,"enabled",True);
    auto foreground=m.NewTable();
    m.Field(foreground,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_256.png")));
    auto overlays=m.Array({Table(foreground)});
    m.Field(hotspot,"overlays",Table(overlays));
    auto list=m.NewTable();
    m.Entry(list,False,Table(m.Array({Table(hotspot)})));
    m.Field(m.map,"hotspots_sorted",Table(list));
    Reader r(m.host,"");r.SetDiagnostics(true);
    auto s=m.Sample(r);
    Require(s.map_markers.size()==1,"selling-point hotspot missing");
    const auto &marker=s.map_markers.front();
    Require(marker.label=="Goldcrest Grain Elevator",
            "position correlation against mission.tipTriggers lost the real station name");
    Require(marker.kind=="SELLINGPOINT",
            "tip-trigger kind not tagged for the Selling points filter/subtitle");
  });
  run("large hotspot arrays cannot exhaust primary reader budget", [] {
    Memory m;m.World();
    auto category=Number(10),classes=m.NewTable();
    m.Field(classes,"CATEGORY_VEHICLE_STEERABLE",category);
    m.Field(m.env,"MapHotspot",Table(classes));
    auto uv=m.Array({Number(.25),Number(.5),Number(.25),Number(.75),
                     Number(.375),Number(.5),Number(.375),Number(.75)});
    auto color=m.Array({Number(.2705),Number(.6514),Number(.0802),Number(1)});
    std::vector<uint64_t> entries;entries.reserve(450);
    for(unsigned i=0;i<450;++i) {
      auto hotspot=m.NewTable();m.Num(hotspot,"category",10);m.Num(hotspot,"xMapPos",i);
      m.Num(hotspot,"zMapPos",-int(i));m.Field(hotspot,"imageUVs",Table(uv));
      m.Field(hotspot,"baseColor",Table(color));m.Field(hotspot,"enabled",True);
      auto overlay=m.NewTable();m.Field(overlay,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_1024.png")));
      auto overlays=m.Array({Table(overlay)});m.Field(hotspot,"overlays",Table(overlays));
      auto bg=m.NewTable();m.Field(bg,"filename",String(m.NewString("dataS2/menu/hud/mapHotspots_256.png")));
      m.Field(bg,"uvs",Table(uv));m.Field(hotspot,"overlayBg",Table(bg));
      entries.push_back(Table(hotspot));
    }
    auto group=m.Array(entries),list=m.NewTable();m.Entry(list,False,Table(group));
    m.Field(m.map,"hotspots_sorted",Table(list));
    Reader r(m.host,"");r.SetDiagnostics(true);auto s=m.Sample(r);
    Require(s.world_ready && s.phase==Phase::World && s.map_id==2,
            "optional hotspot walk invalidated the primary world snapshot");
    if (!s.map_markers_error.empty()) {
      Require(s.map_markers.empty() &&
                  s.map_markers_error.find("invalid native hotspot data during ")==0 &&
                  s.map_markers_error.find("bytes=")!=std::string::npos &&
                  s.map_markers_error.find("calls=")!=std::string::npos,
              "bounded hotspot failure was not surfaced");
      Require(m.published_text.at("fs.markers.error")==s.map_markers_error,
              "hotspot failure diagnostic not published");
    } else {
      Require(s.map_markers.size()==450,
              "bounded hotspot walk dropped valid markers without an error");
    }
    Require(m.published.at("fs.markers.bytes")>0 && m.published.at("fs.markers.calls")>0,
            "hotspot read budget diagnostics missing");
  });
  run("map layer failure is explicit and published", [] {
    Memory m;m.World();Reader r(m.host,"");r.SetDiagnostics(true);
    auto s=m.Sample(r);
    Require(s.world_ready && s.map_id==2,
            "map-layer failure invalidated the primary world snapshot");
    Require(!s.map_layers &&
                s.map_layer_error.find("invalid native map data during ")==0 &&
                s.map_layer_error.find("bytes=")!=std::string::npos &&
                s.map_layer_error.find("calls=")!=std::string::npos,
            "map layer failure reason was swallowed");
    Require(m.published_text.at("fs.layers.error")==s.map_layer_error,
            "map layer diagnostic not published");
  });
  run("primary reader failures are rate-limited with stage and budget", [] {
    Memory m;m.Put<uint64_t>(Main+0x46cb40,0);
    Reader r(m.host,"");auto s=m.Sample(r);
    Require(!s.world_ready && m.logs.size()==1,
            "primary read failure was not logged");
    Require(m.logs.front().find("primary reader failed during script-root resolution")!=std::string::npos &&
                m.logs.front().find("bytes=")!=std::string::npos &&
                m.logs.front().find("calls=")!=std::string::npos,
            "primary failure diagnostic omitted stage or budget");
    r.Sample(m.host);
    Require(m.logs.size()==1,"primary read warning was not rate-limited");
  });
  run("soil map masks background and applies native soil options in order", [] {
    constexpr uint32_t field_mask=0x7;
    constexpr unsigned plough_first=9,plough_channels=2,
                       spray_first=7,spray_channels=2;
    constexpr uint32_t needs=0xffe04030;
    const std::vector<uint32_t> fertilizer={0xffd06040,0xff50b060,0xff4090d0};
    auto color=[&](uint32_t raw,bool plowing=true,bool fertilizer_enabled=true) {
      return farming_simulator_reader::SoilMapColor(raw,field_mask,plough_first,
          plough_channels,spray_first,spray_channels,plowing,
          fertilizer_enabled,needs,fertilizer);
    };
    Require(color(0)==0,"zero terrain type colored map background as a field");
    Require(color(1)==needs,"zero plough counter missed Needs plowing state");
    auto fertilized_field=1u|(2u<<spray_first);
    Require(color(fertilized_field)==fertilizer[1],
            "spray state did not color the field after plough overlay");
    Require(color(1u|(1u<<spray_first),true,false)==fertilizer.back(),
            "disabled fertilizer stages did not collapse to native final color");
    Require(color(1u|(1u<<plough_first),false)==0,
            "disabled plowing overlay colored a field");
    Require(color(1u|(1u<<plough_first)|(3u<<spray_first),false)==fertilizer[2],
            "fertilizer option did not remain independent of plowing toggle");
  });
  run("on-foot heading follows a live player.rotY update, ignoring a stale "
      "native playerRotation and the scene node's own rotation",
      [] {
    // Reproduces the owner report ("on foot it does not change direction")
    // and its root cause: live RE on the desktop harness (2026-10-16) found
    // the player's rootNode is a translate-only physics anchor (its matrix
    // rotation elements never change while walking), and, separately, that
    // ingameMap.playerRotation itself stops updating once the native
    // minimap is cycled to its hidden state (ingameMap.state == 2) -- a
    // live memory write to player.rotY was observed to propagate into
    // playerRotation only while the map's own update was still running.
    // player.rotY is therefore the correct always-live source.
    Memory m;m.World();m.ScenePlayer(187,236,-54,0);
    Reader r(m.host,"");r.SetDiagnostics(true);
    auto s=m.Sample(r);
    Require(s.player_heading && std::abs(*s.player_heading-0)<1e-6,
            "initial heading did not come from player.rotY");
    // Move the player (translation only) without ever touching the scene
    // node's rotation elements, matching the live capture where walking
    // changed fs.debug.player_x/z but left the node-derived heading at 0.
    std::array<float,16> moved{1,0,0,0, 0,1,0,0, 0,0,1,0, 235.7936f,0,-56.9658f,1};
    auto root=uint32_t(187);
    auto pred=m.Get<uint64_t>(Main+0x46c918);
    auto bucket=m.Get<uint64_t>(pred+(root%8)*8),entry=m.Get<uint64_t>(bucket);
    auto node=m.Get<uint64_t>(entry+24);m.PutBytes(node+0x78,moved.data(),64);
    // Turn the player (rotY) while leaving the native map's own
    // playerRotation stale/frozen at its old value, and the minimap marked
    // hidden -- exactly the owner-reported state.
    m.Num(m.player,"rotY",-1.5707963267948966);
    m.Field(m.map,"isVisible",Number(0));m.Num(m.map,"state",2);
    s=m.Sample(r);
    Require(s.player_x==235.7936f && s.player_z==-56.9658f,
            "scene translation did not apply");
    Require(s.player_heading &&
                std::abs(*s.player_heading-(-1.5707963267948966))<1e-6,
            "heading stuck at the stationary scene node's rotation or the "
            "stale native playerRotation instead of turning with "
            "player.rotY");
  });
  run("on-foot heading falls back to native playerRotation when rotY is "
      "absent, but only while the map's own update is fresh",
      [] {
    Memory m;m.World();m.ScenePlayer(187,236,-54,0);
    m.Field(m.player,"rotY",Nil);
    Reader r(m.host,"");r.SetDiagnostics(true);
    auto s=m.Sample(r);
    Require(s.player_heading && std::abs(*s.player_heading-.78539833)<1e-6,
            "fresh map did not fall back to native playerRotation when "
            "rotY was unavailable");
    m.Num(m.map,"state",2);
    s=m.Sample(r);
    Require(!s.player_heading,
            "stale/hidden native playerRotation was used as a fallback "
            "even though the map's own update was not fresh");
  });
  run("vehicle heading derives from the scene node's yaw plus pi, matching "
      "native playerRotation even though the node's own rotation is 180 "
      "degrees off",
      [] {
    // Reproduces the owner report ("in a tractor the arrow points the other "
    // way around"): live RE on the desktop harness (2026-10-16) found a
    // parked, carport-facing pickup truck whose rootNode matrix decoded to
    // heading 0 (atan2(world[8],world[10])) while native map.playerRotation
    // read pi (matching the visible facing) -- i.e. the node-derived value
    // was the exact opposite of the truth, so it is corrected with +pi.
    Memory m;m.World();m.Vehicle(75,1900);
    m.Field(m.vehicle,"rootNode",Number(187));
    // Vehicle rootNode matrix faces heading 0 (identity rotation); native
    // playerRotation says the truck is actually facing pi (reversed).
    std::array<float,16> local{1,0,0,0, 0,1,0,0, 0,0,1,0, 236,0,-54,1};
    constexpr uint64_t manager=Main+0x46c918;constexpr uint64_t buckets_count=8;
    auto buckets=m.Allocate(buckets_count*8),predecessor=m.Allocate(8),
         entry=m.Allocate(32),node=m.Allocate(0x800);
    m.Put<uint64_t>(manager,buckets);m.Put<uint64_t>(manager+8,buckets_count);
    auto root=uint32_t(187);auto bucket=root%buckets_count;
    m.Put<uint64_t>(buckets+bucket*8,predecessor);
    m.Put<uint64_t>(predecessor,entry);m.Put<uint64_t>(entry+8,root);
    m.Put<uint32_t>(entry+16,root);m.Put<uint64_t>(entry+24,node);
    m.Put<uint64_t>(node+16,0x80);
    m.Put<uint64_t>(node+0x20,0);m.PutBytes(node+0x78,local.data(),64);
    m.Num(m.map,"playerRotation",3.141590401739129);
    Reader r(m.host,"");r.SetDiagnostics(true);
    auto s=m.Sample(r);
    Require(s.player_heading && std::abs(*s.player_heading-M_PI)<1e-5,
            "vehicle heading took the node's own (reversed) rotation "
            "instead of node yaw + pi");
    // Now turn the parked truck (rotate the node 90 degrees) while marking
    // the native map hidden and its playerRotation stale -- the companion
    // arrow must keep tracking from the scene node, which updates on the
    // same cadence as position regardless of minimap visibility.
    std::array<float,16> turned{float(std::cos(-M_PI_2)),0,float(-std::sin(-M_PI_2)),0,
      0,1,0,0,float(std::sin(-M_PI_2)),0,float(std::cos(-M_PI_2)),0,
      236,0,-54,1};
    m.PutBytes(node+0x78,turned.data(),64);
    m.Field(m.map,"isVisible",Number(0));m.Num(m.map,"state",2);
    s=m.Sample(r);
    double expected=-M_PI_2+M_PI;
    Require(s.player_heading && std::abs(*s.player_heading-expected)<1e-6,
            "vehicle heading stuck on the stale native playerRotation "
            "instead of following the scene node while the minimap was "
            "hidden");
  });
  run("mission and vehicle lifetime races rejected", [] {
    Memory m;
    m.World();
    m.Vehicle(75, 1900);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    m.tear_address = m.slots.at({m.env, "g_currentMission"});
    Clear(m.Sample(r), Phase::Unknown);
    m.tear_address = m.slots.at({m.mission, "controlledVehicle"});
    Clear(m.Sample(r), Phase::Unknown);
  });
  run("rehash and ScriptSystem lifetime races rejected", [] {
    Memory m;
    m.World();
    m.Vehicle(75, 1900);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    for (unsigned race = 0; race < 4; ++race) {
      m.read_hook = [&](uint64_t address, size_t size, void *out) {
        if (race == 0 && address == m.env && size == 64) {
          uint64_t changed = 0x2101234000;
          std::memcpy(static_cast<uint8_t *>(out) + 40, &changed, 8);
        }
        if (race == 1 && address == m.mission && size == 64) {
          uint32_t changed = 127;
          std::memcpy(static_cast<uint8_t *>(out) + 52, &changed, 4);
        }
        if (race == 2 && address == Main + 0x46cb40 && size == 8)
          std::memcpy(out, &Nil, 8);
        if (race == 3 && address == m.object && size == 8)
          std::memcpy(out, &Nil, 8);
      };
      Clear(m.Sample(r), Phase::Unknown);
    }
    m.read_hook = {};
    ValidWorld(m.Sample(r), m.mission);
  });
  run("new Lua state and table storage replace cached lookups", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    ValidWorld(m.Sample(r), m.mission);
    auto new_state = m.Allocate(128);
    m.Put<uint8_t>(new_state + 9, 6);
    m.Put<uint64_t>(new_state + 72, m.env);
    m.Put<uint64_t>(m.object, new_state);
    ValidWorld(m.Sample(r), m.mission);
    auto nodes = m.Allocate(64 * 24, true);
    std::array<uint8_t, 64 * 24> contents{};
    for (size_t i = 0; i < contents.size(); ++i)
      contents[i] = m.pages.at((m.env + 64 + i) &
                               ~UINT64_C(4095))[(m.env + 64 + i) & 4095];
    m.PutBytes(nodes, contents.data(), contents.size());
    m.Put<uint64_t>(m.env + 40, nodes);
    ValidWorld(m.Sample(r), m.mission);
  });
  run("cached key pointer requires matching character contents", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    ValidWorld(m.Sample(r), m.mission);
    // Simulate a stale/reused GCstr address while preserving its tagged
    // pointer.
    uint64_t time{}, key{};
    auto slot = m.slots.at({m.mission, "environment"});
    std::memcpy(&time,
                m.pages.at(slot & ~UINT64_C(4095)).data() + (slot & 4095), 8);
    time &= (UINT64_C(1) << 47) - 1;
    slot = m.slots.at({time, "currentHour"}) + 8;
    std::memcpy(&key, m.pages.at(slot & ~UINT64_C(4095)).data() + (slot & 4095),
                8);
    key &= (UINT64_C(1) << 47) - 1;
    m.PutBytes(key + 24, "currentHoux", 11);
    auto s = m.Sample(r);
    Require(s.world_ready && s.money == 123456.75 && !s.time_minutes,
            "cached key content was not validated");
    m.Put<uint64_t>(slot, String(m.NewString("currentHour")));
    Require(m.Sample(r).time_minutes == 872,
            "replacement key remained negatively cached");
  });
  run("nil-valued dead string keys are never dereferenced", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    m.Put<uint64_t>(m.slots.at({m.map, "playerRotation"}) + 8, Nil);
    auto dead = m.map + 64 + 24 * 10;
    m.Put<uint64_t>(dead, Nil);
    m.Put<uint64_t>(dead + 8, String(0x12345000));
    auto s = m.Sample(r);
    Require(s.world_ready && s.player_x == 236 && s.player_z == -54 &&
                !s.player_heading,
            "dead nil-node key invalidated valid world");
  });
  run("negative cache observes nil to nonnil key resurrection", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    uint64_t i18n{};
    auto slot = m.slots.at({m.env, "g_i18n"});
    std::memcpy(&i18n,
                m.pages.at(slot & ~UINT64_C(4095)).data() + (slot & 4095), 8);
    i18n &= (UINT64_C(1) << 47) - 1;
    m.Num(i18n, "moneyUnit", 3);
    m.Field(m.env, "GS_MONEY_POUND", Nil);
    auto s = m.Sample(r);
    Require(s.world_ready && s.currency_symbol.empty(),
            "nil global was not absent");
    m.Num(m.env, "GS_MONEY_POUND", 3);
    Require(m.Sample(r).currency_symbol == "£",
            "negative cache hid resurrected existing key");
  });
  run("bounded normal reads and hostile table walk", [] {
    Memory m;
    m.World();
    m.Vehicle(75, 1900);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto t0 = std::chrono::steady_clock::now();
    r.SetClockForTesting(t0);
    ValidWorld(m.Sample(r), m.mission);
    Require(m.reads <= 64 && m.bytes <= 64 * 4096, "normal sample read budget");
    auto nodes = m.Allocate(16384 * 24, true);
    m.Put<uint64_t>(m.env + 40, nodes);
    m.Put<uint32_t>(m.env + 52, 16383);
    for (unsigned i = 0; i < 1100; ++i)
      m.Put<uint64_t>(nodes + i * 24 + 8,
                      String(m.NewString("different key", true)));
    r.SetClockForTesting(t0 + std::chrono::milliseconds(100));
    // Still within the staleness grace window: one hostile/budget-exhausting
    // sample keeps showing the last good world instead of blanking it.
    ValidWorld(m.Sample(r), m.mission);
    Require(m.reads == 1024 && m.bytes == 4 * 1024 * 1024,
            "hostile table did not stop at four MiB/page budget");
    Require(m.published.at("fs.reader.calls") == int64_t(m.reads),
            "page read metrics mismatch");
    // Past the grace window the persistent failure finally reports empty.
    r.SetClockForTesting(t0 + std::chrono::seconds(6));
    Clear(m.Sample(r), Phase::Unknown);
  });
  run("large cold globals warm negative cache and inserted key", [] {
    Memory m;
    m.World();
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto nodes = m.Allocate(2048 * 24, true);
    for (unsigned i = 0; i < 64; ++i) {
      std::array<uint8_t, 24> entry{};
      for (size_t j = 0; j < entry.size(); ++j) {
        auto at = m.env + 64 + i * 24 + j;
        entry[j] = m.pages.at(at & ~UINT64_C(4095))[at & 4095];
      }
      m.PutBytes(nodes + (1984 + i) * 24, entry.data(), entry.size());
    }
    // Match the observed cold-global footprint: hundreds of scattered GCstr
    // pages plus a 2048-slot hash array. Required native keys occur near the
    // end.
    for (unsigned i = 0; i < 424; ++i)
      m.Put<uint64_t>(
          nodes + i * 24 + 8,
          String(m.NewString("synthetic_global_" + std::to_string(i), true)));
    m.Put<uint64_t>(m.env + 40, nodes);
    m.Put<uint32_t>(m.env + 52, 2047);
    // A missing currency global exercises the negative key fingerprint cache.
    auto pound_old = m.slots.at({m.env, "GS_MONEY_POUND"});
    auto pound_new = nodes + 1984 * 24 + (pound_old - (m.env + 64));
    m.Put<uint64_t>(pound_new + 8, Nil);
    uint64_t i18n{};
    auto i18n_slot = m.slots.at({m.env, "g_i18n"});
    std::memcpy(
        &i18n,
        m.pages.at(i18n_slot & ~UINT64_C(4095)).data() + (i18n_slot & 4095), 8);
    i18n &= (UINT64_C(1) << 47) - 1;
    m.Num(i18n, "moneyUnit", 3);
    auto cold = m.Sample(r);
    Require(cold.world_ready && cold.map_id == 2 &&
                cold.currency_symbol.empty(),
            "large cold globals failed initialization");
    Require(m.published.at("fs.reader.bytes") > 1024 * 1024 &&
                m.published.at("fs.reader.bytes") < 4 * 1024 * 1024,
            "cold fixture failed to cover old budget regression");
    auto warm = m.Sample(r);
    Require(warm.world_ready && warm.currency_symbol.empty(),
            "negative cache changed absent global");
    Require(m.published.at("fs.reader.bytes") < 100 * 4096,
            "warm negative cache rescanned scattered strings");
    auto inserted = nodes + 800 * 24;
    m.Put<uint64_t>(inserted, Number(3));
    m.Put<uint64_t>(inserted + 8, String(m.NewString("GS_MONEY_POUND")));
    auto changed = m.Sample(r);
    Require(changed.world_ready && changed.currency_symbol == "£",
            "negative cache hid newly inserted key");
    Require(m.Sample(r).currency_symbol == "£",
            "positive cache lost inserted key");
    m.Put<uint64_t>(inserted + 8, Nil);
    Require(m.Sample(r).world_ready && r.GetSnapshot().currency_symbol.empty(),
            "removed positive key stayed cached");
  });
  run("on-foot world with absent controlledVehicle key", [] {
    Memory m;
    m.World();
    auto slot = m.slots.at({m.mission, "controlledVehicle"});
    m.Put<uint64_t>(slot + 8, Nil);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto s = m.Sample(r);
    ValidWorld(s, m.mission);
    Require(!s.vehicle_ready, "absent vehicle became ready");
  });

  run("catalog chunks commit transactionally and reset on identity change", [] {
    using namespace farming_simulator_reader;
    Memory m;
    auto clean = [&] {
      auto t = m.NewTable();
      for (unsigned i = 0; i < 64; ++i)
        m.Put<uint64_t>(t + 64 + 24 * i, Nil);
      return t;
    };
    auto array = [&](const std::vector<uint64_t> &values) {
      auto t = clean();
      auto p = m.Allocate((values.size() + 1) * 8);
      m.Put<uint64_t>(t + 16, p);
      m.Put<uint32_t>(t + 48, values.size() + 1);
      m.Put<uint64_t>(p, Nil);
      for (size_t i = 0; i < values.size(); ++i)
        m.Put<uint64_t>(p + (i + 1) * 8, values[i]);
      return t;
    };
    auto util = clean(), category = clean(), categories = clean(),
         brand = clean(), brandUtil = clean();
    m.Field(m.env, "StoreItemsUtil", Table(util));
    m.Field(m.env, "BrandUtil", Table(brandUtil));
    m.Field(category, "name", String(m.NewString("tractors")));
    m.Field(category, "title", String(m.NewString("Tractors")));
    m.Num(category, "orderId", 2);
    m.Field(categories, "tractors", Table(category));
    m.Field(util, "storeCategories", Table(categories));
    m.Num(brand, "index", 1);
    m.Field(brand, "nameI18N", String(m.NewString("Native Brand")));
    m.Field(brandUtil, "brandIndexToDesc", Table(array({Table(brand)})));
    std::vector<uint64_t> items;
    for (unsigned i = 0; i < 17; ++i) {
      auto t = clean();
      m.Field(t, "name", String(m.NewString("Native Item")));
      m.Field(t, "category", String(m.NewString("tractors")));
      m.Num(t, "brandIndex", 1);
      m.Num(t, "price", 1000 + i);
      items.push_back(Table(t));
    }
    auto store = array(items);
    m.Field(util, "storeItems", Table(store));
    CatalogCache cache;
    Lua lua;
    lua.Begin(m.host);
    CatalogStep(lua, m.env, cache);
    Require(cache.next == 17 && cache.data.shop_items.size() == 16 &&
                !cache.complete && !cache.data.shop_ready,
            "catalog first chunk not bounded/private");
    auto nameSlot =
        m.slots.at({items.back() & ((UINT64_C(1) << 47) - 1), "name"});
    m.Put<uint64_t>(nameSlot, String(0x12345000));
    lua.Begin(m.host);
    bool threw = false;
    try {
      CatalogStep(lua, m.env, cache);
    } catch (const Invalid &) {
      threw = true;
    }
    Require(threw && cache.next == 17 && cache.data.shop_items.size() == 16,
            "failed catalog chunk partially committed");
    m.Put<uint64_t>(nameSlot, String(m.NewString("Final native item")));
    lua.Begin(m.host);
    CatalogStep(lua, m.env, cache);
    Require(cache.complete && cache.data.shop_ready &&
                cache.data.shop_items.size() == 17 &&
                cache.data.shop_categories[0].id == "tractors" &&
                cache.data.shop_brands[0].id == "1",
            "catalog completion/metadata failed");
    auto replacement = array({items.front()});
    m.Field(util, "storeItems", Table(replacement));
    lua.Begin(m.host);
    CatalogStep(lua, m.env, cache);
    Require(cache.complete && cache.data.shop_items.size() == 1 &&
                cache.table == replacement,
            "catalog replacement retained stale items");
  });
  run("owned vehicle native sale upkeep time and configuration additions", [] {
    using namespace farming_simulator_reader;
    Memory m;
    auto clean = [&] {
      auto t = m.NewTable();
      for (unsigned i = 0; i < 64; ++i)
        m.Put<uint64_t>(t + 64 + 24 * i, Nil);
      return t;
    };
    auto array = [&](const std::vector<uint64_t> &values) {
      auto t = clean();
      auto p = m.Allocate((values.size() + 1) * 8);
      m.Put<uint64_t>(t + 16, p);
      m.Put<uint32_t>(t + 48, values.size() + 1);
      m.Put<uint64_t>(p, Nil);
      for (size_t i = 0; i < values.size(); ++i)
        m.Put<uint64_t>(p + (i + 1) * 8, values[i]);
      return t;
    };
    auto util = clean(), byConfig = clean(), item = clean(), economy = clean(),
         v = clean();
    m.Field(m.env, "StoreItemsUtil", Table(util));
    m.Field(util, "storeItemsByXMLFilename", Table(byConfig));
    m.Field(byConfig, "native.xml", Table(item));
    m.Field(item, "name", String(m.NewString("Native 8340")));
    m.Num(item, "lifetime", 600);
    m.Num(item, "dailyUpkeep", 200);
    m.Field(m.env, "EconomyManager", Table(economy));
    m.Num(economy, "LIFETIME_OPERATINGTIME_RATIO", .08333);
    m.Num(economy, "MAX_DAILYUPKEEP_MULTIPLIER", 4);
    m.Field(v, "configFileName", String(m.NewString("Native.XML")));
    m.Num(v, "propertyState", 1);
    m.Num(v, "age", 48);
    m.Num(v, "operatingTime", 96000000);
    m.Num(v, "price", 85000);
    m.Num(v, "id", 99);
    m.Field(m.mission, "vehicles", Table(array({Table(v)})));
    Lua lua;
    lua.Begin(m.host);
    Snapshot out;
    Vehicles(lua, m.env, m.mission, out);
    Require(out.vehicles_ready && out.owned_vehicles.size() == 1,
            "owned vehicle not resolved");
    auto row = out.owned_vehicles.front();
    Require(row.sale_value == 21793 && row.maintenance_cost &&
                std::abs(*row.maintenance_cost - 517.8786138112191) < 1e-8,
            "native valuation mismatch");
    Require(row.operating_time_text == "26.6 h" && row.id == "99",
            "native operating units/identity mismatch");
    auto selected = clean(), groups = clean(), option = clean();
    m.Num(selected, "motor", 1);
    m.Num(option, "dailyUpkeep", 25);
    m.Field(groups, "motor", Table(array({Table(option)})));
    m.Field(item, "configurations", Table(groups));
    m.Field(v, "configurations", Table(selected));
    lua.Begin(m.host);
    Snapshot configured;
    Vehicles(lua, m.env, m.mission, configured);
    Require(std::abs(*configured.owned_vehicles.front().maintenance_cost -
                     517.8786138112191 * 225 / 200) < 1e-8,
            "configured upkeep addition missing");
    m.Num(v, "propertyState", 0);
    lua.Begin(m.host);
    Snapshot missionOnly;
    Vehicles(lua, m.env, m.mission, missionOnly);
    Require(missionOnly.vehicles_ready && missionOnly.owned_vehicles.empty(),
            "unowned vehicle leaked");
    m.Num(v, "propertyState", 2);
    lua.Begin(m.host);
    Snapshot leased;
    Vehicles(lua, m.env, m.mission, leased);
    Require(leased.owned_vehicles.front().leased &&
                !leased.owned_vehicles.front().sale_value,
            "leased sale value exposed");
  });

  run("owned vehicle garage enter/reset eligibility follows the native rules", [] {
    // InGameMenu:onClickEnterVehicle / onYesNoReset and ResetVehicleEvent.run, decompiled
    // (reviews/20261017-game-calls-11/GAME-CALLS.md): Enter needs enterReferenceNode + exitPoint
    // and a vehicle neither broken nor controlled; Reset needs isVehicleSaved. Both need the
    // identity the game call re-checks (rootNode + id).
    using namespace farming_simulator_reader;
    Memory m;
    auto clean = [&] {
      auto t = m.NewTable();
      for (unsigned i = 0; i < 64; ++i)
        m.Put<uint64_t>(t + 64 + 24 * i, Nil);
      return t;
    };
    auto array = [&](const std::vector<uint64_t> &values) {
      auto t = clean();
      auto p = m.Allocate((values.size() + 1) * 8);
      m.Put<uint64_t>(t + 16, p);
      m.Put<uint32_t>(t + 48, values.size() + 1);
      m.Put<uint64_t>(p, Nil);
      for (size_t i = 0; i < values.size(); ++i)
        m.Put<uint64_t>(p + (i + 1) * 8, values[i]);
      return t;
    };
    auto util = clean(), byConfig = clean(), item = clean(), v = clean();
    m.Field(m.env, "StoreItemsUtil", Table(util));
    m.Field(util, "storeItemsByXMLFilename", Table(byConfig));
    m.Field(byConfig, "parked.xml", Table(item));
    m.Field(item, "name", String(m.NewString("Parked 740")));
    m.Field(v, "configFileName", String(m.NewString("Parked.XML")));
    m.Num(v, "propertyState", 1);
    m.Num(v, "id", 7);
    m.Num(v, "rootNode", 4242);
    m.Num(v, "enterReferenceNode", 4250);
    m.Num(v, "exitPoint", 4251);
    m.Field(v, "isVehicleSaved", True);
    m.Field(m.mission, "vehicles", Table(array({Table(v)})));
    auto read = [&] {
      Lua lua;
      lua.Begin(m.host);
      Snapshot out;
      Vehicles(lua, m.env, m.mission, out);
      Require(out.owned_vehicles.size() == 1, "one owned vehicle");
      return out.owned_vehicles.front();
    };
    auto parked = read();
    Require(parked.can_enter && parked.can_reset && parked.root_node == 4242 &&
                parked.network_id == 7,
            "parked drivable: enter and reset, with its identity");
    m.Field(v, "isControlled", True);
    auto driven = read();
    Require(!driven.can_enter && driven.can_reset,
            "controlled: no enter (native isControlled), reset still offered (native leaves first)");
    m.Field(v, "isControlled", False);
    m.Field(v, "isBroken", True);
    Require(!read().can_enter, "broken: no enter");
    m.Field(v, "isBroken", False);
    m.Field(v, "exitPoint", Nil);
    Require(!read().can_enter && read().can_reset, "no seat (implement): reset only");
    m.Field(v, "isVehicleSaved", False);
    Require(!read().can_reset, "not saved: the server refuses a reset");
  });

  run("native prices server client floor acceptance and trend bits", [] {
    using namespace farming_simulator_reader;
    Memory m;
    auto util = m.CleanTable(), first = m.CleanTable(), second = m.CleanTable(),
         trigger = m.CleanTable(), registry = m.CleanTable();
    m.Field(m.env, "FillUtil", Table(util));
    m.Num(util, "NUM_FILLTYPES", 2);
    for (auto desc : {first, second}) {
      m.Field(desc, "showOnPriceTable", True);
      m.Field(desc, "nameI18N",
              String(m.NewString(desc == first ? "Wheat" : "Barley")));
    }
    m.Field(util, "fillTypeIndexToDesc",
            Table(m.Array({Table(first), Table(second)})));
    m.Field(m.mission, "tipTriggers", Table(registry));
    m.Entry(registry, Table(trigger), True);
    m.Field(trigger, "isSellingPoint", True);
    m.Field(trigger, "appearsOnStats", True);
    m.Field(trigger, "isServer", True);
    m.Field(trigger, "stationName", String(m.NewString("Native station")));
    auto accepted = m.Array({True, Nil}),
         bases = m.Array({Number(1.23499), Number(8)}),
         deltas = m.Array({Number(.0008), Number(0)});
    m.Field(trigger, "acceptedFillTypes", Table(accepted));
    m.Field(trigger, "fillTypePrices", Table(bases));
    m.Field(trigger, "fillTypePriceRandomDelta", Table(deltas));
    m.Field(trigger, "priceMultipliers",
            Table(m.Array({Number(2), Number(1)})));
    m.Field(trigger, "fillTypePriceInfo",
            Table(m.Array({Number(52), Number(0)})));
    Lua lua;
    lua.Begin(m.host);
    Snapshot server;
    Prices(lua, m.env, m.mission, server);
    Require(server.prices_ready && server.price_crops.size() == 2 &&
                server.price_stations.size() == 1,
            "native price rows not resolved");
    auto q = server.price_stations[0].quotes[0];
    Require(q.accepted && q.price_per1000 == 2471,
            "server expression/floor wrong");
    Require(q.trend == farming_simulator::PriceTrend::Rising &&
                q.highlight == farming_simulator::PriceHighlight::High &&
                q.great_demand,
            "trend bit index masks wrong");
    Require(!server.price_stations[0].quotes[1].accepted &&
                !server.price_stations[0].quotes[1].price_per1000,
            "unaccepted crop acquired price");
    m.Field(trigger, "isServer", Nil);
    lua.Begin(m.host);
    Snapshot client;
    Prices(lua, m.env, m.mission, client);
    Require(client.price_stations[0].quotes[0].price_per1000 == 1234,
            "client base-only expression/floor wrong");
    m.Field(trigger, "isServer", True);
    uint64_t deltaArray{};
    std::memcpy(
        &deltaArray,
        m.pages.at(deltas & ~UINT64_C(4095)).data() + (deltas & 4095) + 16, 8);
    m.Put<uint64_t>(deltaArray + 8, Nil);
    lua.Begin(m.host);
    Snapshot missing;
    Prices(lua, m.env, m.mission, missing);
    Require(missing.price_stations[0].quotes[0].accepted &&
                !missing.price_stations[0].quotes[0].price_per1000,
            "missing operand fabricated an effective price");
  });
  run("finance signed totals oldest history mapping blanks and saldo", [] {
    using namespace farming_simulator_reader;
    Memory m;
    auto cls = m.CleanTable(), labels = m.CleanTable(), stats = m.CleanTable(),
         today = m.CleanTable(), old = m.CleanTable(), recent = m.CleanTable(),
         environment = m.CleanTable();
    std::vector<uint64_t> names;
    for (unsigned i = 0; i < 27; ++i) {
      auto key = "category" + std::to_string(i);
      names.push_back(String(m.NewString(key)));
      m.Field(labels, key, String(m.NewString("Native " + key)));
      m.Num(today, key.c_str(), i == 0 ? 100.8 : i == 1 ? -25.4 : 0);
      m.Num(old, key.c_str(), i == 0 ? 10 : i == 1 ? -2 : 0);
      m.Num(recent, key.c_str(), i == 0 ? 20 : i == 1 ? -2 : 0);
    }
    m.Field(m.env, "FinanceStats", Table(cls));
    m.Field(cls, "statNames", Table(m.Array(names)));
    m.Field(cls, "statNamesI18n", Table(labels));
    m.Field(m.mission, "missionStats", Table(stats));
    m.Field(stats, "financeStats", Table(today));
    m.Field(stats, "financeStatsHistory",
            Table(m.Array({Table(old), Table(recent)})));
    m.Num(stats, "money", 100);
    m.Num(stats, "loan", 40);
    m.Field(m.mission, "environment", Table(environment));
    m.Num(environment, "currentDay", 2);
    Lua lua;
    lua.Begin(m.host);
    Snapshot out;
    Management(lua, m.env, m.mission, out);
    Require(out.finances_ready && out.finance_rows.size() == 27,
            "finance category coverage wrong");
    Require(!out.finance_totals[0] && !out.finance_totals[1] &&
                out.finance_totals[2] == 8 && out.finance_totals[3] == 18 &&
                out.finance_totals[4] &&
                std::abs(*out.finance_totals[4] - 75.4) < 1e-9,
            "finance signed totals/history columns wrong");
    Require(out.finance_rows[1].amounts[4] == -25.4 &&
                out.finance_rows[0].amounts[2] == 10 &&
                out.finance_rows[0].amounts[3] == 20,
            "finance expense sign or archive direction changed");
    Require(out.finance_saldo == 60 &&
                out.finance_day_labels[3] == "ui_financesDay1" &&
                out.finance_day_labels[4] == "ui_today",
            "finance balance/weekday mismatch");
  });
  run("animal empty herd capacity native zero and nonzero reproduction", [] {
    using namespace farming_simulator_reader;
    Memory m;
    auto registry = m.CleanTable(), util = m.CleanTable(),
         i18n = m.CleanTable();
    m.Field(m.mission, "husbandries", Table(registry));
    m.Field(m.env, "FillUtil", Table(util));
    m.Field(m.env, "g_i18n", Table(i18n));
    m.Num(i18n, "fluidFactor", 1);
    m.Num(util, "FILLTYPE_WATER", 1);
    m.Num(util, "FILLTYPE_STRAW", 2);
    uint64_t cow{};
    for (auto id : {"sheep", "pig", "cow"}) {
      auto animal = m.CleanTable(), desc = m.CleanTable();
      m.Field(registry, id, Table(animal));
      m.Field(animal, "hasStatistics", True);
      m.Field(animal, "animalDesc", Table(desc));
      m.Field(animal, "tipTriggersFillLevels",
              Table(m.Array({Table(m.CleanTable()), Table(m.CleanTable())})));
      m.Field(desc, "title", String(m.NewString(id)));
      m.Num(desc, "waterPerDay", 10);
      m.Num(desc, "strawPerDay", 20);
      m.Num(animal, "totalNumAnimals", 0);
      m.Num(animal, "productivity", 0);
      m.Num(animal, "cleanlinessFactor", .9);
      m.Num(animal, "reproductionRatePerDay", 0);
      m.Num(animal, "newAnimalPercentage", 0);
      if (std::string_view(id) == "cow")
        cow = animal;
    }
    Lua lua;
    lua.Begin(m.host);
    Require(AnimalFill(lua, cow, 3) == 0,
            "valid animal fill map absent type did not defaultzero");
    Snapshot empty;
    Animals(lua, m.env, m.mission, empty);
    Require(empty.animals_ready && empty.animals.size() == 3,
            "native animal species not resolved");
    for (const auto &group : empty.animals) {
      Require(group.count == 0 && !group.cleanliness &&
                  !group.reproduction_interval_minutes &&
                  !group.next_animal_minutes,
              "empty herd fake cleanliness/reproduction");
      Require(group.needs.size() == 2 && group.needs[0].capacity == 900 &&
                  group.needs[1].capacity == 1800,
              "empty herd15animal capacity floor missing");
      Require(group.needs[0].level == 0 && group.needs[1].level == 0,
              "native absent fill type did not defaultzero");
    }
    m.Num(cow, "totalNumAnimals", 20);
    m.Num(cow, "productivity", .456);
    m.Num(cow, "cleanlinessFactor", .876);
    m.Num(cow, "reproductionRatePerDay", 2);
    m.Num(cow, "newAnimalPercentage", .25);
    lua.Begin(m.host);
    Snapshot growing;
    Animals(lua, m.env, m.mission, growing);
    const auto &group = growing.animals[2];
    Require(group.count == 20 && group.productivity == 45 &&
                group.cleanliness == 87,
            "native animal percentage floor mismatch");
    Require(group.reproduction_interval_minutes == 720 &&
                group.next_animal_minutes == 540,
            "animal perday to minute interval wrong");
    Require(group.needs[0].capacity == 1200 && group.needs[1].capacity == 2400,
            "nonzero animal capacity wrong");
  });

  run("native shop specs numeric strings units rounding and motor override",
      [] {
        using namespace farming_simulator_reader;
        Memory m;
        auto item = m.CleanTable(), spec = m.CleanTable(),
             capacity = m.CleanTable(), i18n = m.CleanTable(),
             texts = m.CleanTable();
        m.Field(item, "specs", Table(spec));
        m.Num(item, "dailyUpkeep", 200.03);
        m.Num(item, "lifetime", 600);
        m.Field(spec, "power", String(m.NewString("88")));
        m.Num(spec, "neededPower", 100);
        m.Num(spec, "fuel", 100);
        m.Num(spec, "maxSpeed", 40);
        m.Num(spec, "workingWidth", 5.47);
        m.Num(spec, "speedLimit", 20);
        m.Field(spec, "capacity", Table(capacity));
        m.Field(capacity, "value", String(m.NewString("1234")));
        m.Field(capacity, "unit", String(m.NewString("$l10n_unit_literShort")));
        m.Field(m.env, "g_i18n", Table(i18n));
        m.Num(i18n, "powerFactorHP", 1);
        m.Num(i18n, "powerFactorKW", .735499);
        m.Num(i18n, "moneyUnit", 1);
        m.Num(m.env, "GS_MONEY_EURO", 1);
        m.Field(i18n, "texts", Table(texts));
        m.Field(texts, "unit_literShort", String(m.NewString("l")));
        auto find = [](const std::vector<farming_simulator::ItemSpec> &rows,
                       std::string_view label) {
          for (const auto &row : rows)
            if (row.label == label)
              return row.value;
          return std::string{};
        };
        Lua lua;
        lua.Begin(m.host);
        std::vector<farming_simulator::ItemSpec> rows;
        CatalogSpecs(lua, m.env, item, rows);
        Require(find(rows, "Power") == "65 kW / 88 hp" &&
                    find(rows, "Required power") == "74 kW / 100 hp",
                "native power factor/order/rounding mismatch");
        Require(find(rows, "Fuel") == "100 l" &&
                    find(rows, "Capacity") == "1234 l" &&
                    find(rows, "Working width") == "5.5 m" &&
                    find(rows, "Maximum speed") == "40 kph",
                "native spec value/unit mismatch");
        Require(find(rows, "Maintenance") == "200.03 € / day" &&
                    find(rows, "Lifetime (days)").empty(),
                "native Shop maintenance/internal lifetime mismatch");
        auto vehicle = m.CleanTable(), selected = m.CleanTable(),
             groups = m.CleanTable(), motor = m.CleanTable();
        m.Field(vehicle, "configurations", Table(selected));
        m.Num(selected, "motor", 1);
        m.Num(motor, "power", 100);
        m.Num(motor, "fuel", 200);
        m.Num(motor, "maxSpeed", 50);
        m.Field(groups, "motor", Table(m.Array({Table(motor)})));
        m.Field(item, "configurations", Table(groups));
        lua.Begin(m.host);
        rows.clear();
        CatalogSpecs(lua, m.env, item, rows, vehicle, 517.88);
        Require(find(rows, "Power") == "74 kW / 100 hp" &&
                    find(rows, "Fuel") == "200 l" &&
                    find(rows, "Maximum speed") == "50 kph",
                "selected motor config did not override base specs");
        Require(find(rows, "Maintenance").empty(),
                "owned specs duplicate maintenance card");
      });
  run("warm-up shows loading before first data, then times out to unavailable",
      [] {
    Memory m;
    m.World(); // no FinanceStats/missionStats finance fields configured
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto t0 = std::chrono::steady_clock::now();
    r.SetClockForTesting(t0);
    auto s = m.Sample(r);
    Require(s.world_ready && !s.finances_ready && s.finances_loading &&
                !s.statistics_ready && s.statistics_loading,
            "fresh world without finance/stat tables should warm up, not "
            "blank, right after loading");
    r.SetClockForTesting(t0 + std::chrono::seconds(2));
    s = m.Sample(r);
    Require(!s.finances_ready && s.finances_loading,
            "still inside the warm-up window a few seconds in");
    r.SetClockForTesting(t0 + std::chrono::seconds(25));
    s = m.Sample(r);
    Require(!s.finances_ready && !s.finances_loading && !s.statistics_ready &&
                !s.statistics_loading,
            "warm-up that never produces data should give up and report "
            "unavailable, not loading forever");
  });
  run("transient read miss after good finance data is kept until it goes "
      "stale",
      [] {
    Memory m;
    m.World();
    auto cls = m.CleanTable(), labels = m.CleanTable(),
         stats = m.CleanTable(), today = m.CleanTable();
    std::vector<uint64_t> names;
    for (unsigned i = 0; i < 27; ++i) {
      auto key = "category" + std::to_string(i);
      names.push_back(String(m.NewString(key)));
      m.Field(labels, key, String(m.NewString("Native " + key)));
      m.Num(today, key.c_str(), i == 0 ? 100.0 : 0);
    }
    m.Field(m.env, "FinanceStats", Table(cls));
    m.Field(cls, "statNames", Table(m.Array(names)));
    m.Field(cls, "statNamesI18n", Table(labels));
    m.Field(m.mission, "missionStats", Table(stats));
    m.Field(stats, "financeStats", Table(today));
    m.Num(stats, "money", 100);
    m.Num(stats, "loan", 40);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto t0 = std::chrono::steady_clock::now();
    r.SetClockForTesting(t0);
    auto s = m.Sample(r);
    Require(s.finances_ready && !s.finances_loading &&
                s.finance_rows.size() == 27,
            "finance setup did not produce ready data");
    // Corrupt the native table a budget miss/lifetime race would also make
    // unreadable, without otherwise changing the world.
    m.Put<uint8_t>(stats + 9, 4);
    r.SetClockForTesting(t0 + std::chrono::milliseconds(1500));
    s = m.Sample(r);
    Require(s.finances_ready && !s.finances_loading &&
                s.finance_rows.size() == 27,
            "a single read miss must not blank data that was already good");
    // Still failing well past the staleness grace window: give up and
    // report unavailable (not loading -- the data existed and was lost,
    // which is different from never having appeared at all).
    r.SetClockForTesting(t0 + std::chrono::seconds(7));
    s = m.Sample(r);
    Require(!s.finances_ready && !s.finances_loading,
            "persistent failure after good data should report unavailable, "
            "not loading forever");
  });
  run("world change clears retained data and warm-up memory", [] {
    Memory m;
    m.World();
    auto cls = m.CleanTable(), labels = m.CleanTable(),
         stats = m.CleanTable(), today = m.CleanTable();
    std::vector<uint64_t> names;
    for (unsigned i = 0; i < 27; ++i) {
      auto key = "category" + std::to_string(i);
      names.push_back(String(m.NewString(key)));
      m.Field(labels, key, String(m.NewString("Native " + key)));
      m.Num(today, key.c_str(), i == 0 ? 100.0 : 0);
    }
    m.Field(m.env, "FinanceStats", Table(cls));
    m.Field(cls, "statNames", Table(m.Array(names)));
    m.Field(cls, "statNamesI18n", Table(labels));
    m.Field(m.mission, "missionStats", Table(stats));
    m.Field(stats, "financeStats", Table(today));
    m.Num(stats, "money", 100);
    m.Num(stats, "loan", 40);
    Reader r(m.host, "");
    r.SetDiagnostics(true);
    auto t0 = std::chrono::steady_clock::now();
    r.SetClockForTesting(t0);
    auto s = m.Sample(r);
    Require(s.finances_ready && !s.finance_rows.empty(),
            "world A finance setup failed");
    auto world_a = m.mission;
    // Load a different save: a brand new mission object, with none of
    // world A's finance tables configured.
    m.mission = m.NewTable();
    m.World();
    r.SetClockForTesting(t0 + std::chrono::milliseconds(50));
    s = m.Sample(r);
    Require(s.world_ready && s.world_identity != world_a,
            "world change not observed");
    Require(!s.finances_ready && s.finances_loading && s.finance_rows.empty(),
            "a new world must warm up fresh and never show the previous "
            "world's finance data");
  });
  std::printf("%u passed, %u failed\n", passed, failed);
  return failed ? 1 : 0;
}
