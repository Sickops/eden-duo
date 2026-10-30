// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III: Eternal Collection module (live slice: direction A, Map tab).
//   d3_reader.{h,cpp}  game-memory reader (roots from code pins; player, vitals, explored masks)
//   d3_cpk.{h,cpp}     the game's CPK archives through the host romfs service
//   d3_tex / d3_font   .tex (XTX) textures with named frames, baked bitmap fonts
//   d3_assets          caches + SNO / LevelArea names
//   d3_view            composed pictures (vitals column, map, title, tab bar, marker)
// Supported builds: 2.7.7.92380 and 2.7.6.90885 (d3_build.h).
//
// Outputs (manifest binds): d3.ready, d3.assets, d3.state.on/key, d3.map.k{0,1}/on/x/y,
// d3.map.k2on (hidden pre-decode slot, no x/y: see the map block in Sample()),
// d3.title.k1/on, d3.vit.k{0,1}, d3.mapvit.k{0,1}, d3.marker.rot, d3.rift.*, d3.bounty.*,
// d3.act.*, d3.menu.pause/native/pause_key, d3.skill.* + d3.rail.s<slot>b<bit> (Map rail),
// d3.qring<0..2>.on/x/y/scale/tint (map objective marker pulse overlay),
// d3.nav.mask/d3.nav.key (dynamic nav bar, debounced ~1 s per tab),
// d3.perf.* (sample cost telemetry, not bound).
// Pictures are module:d3:<kind>/<seq> keys rendered on the asset worker from snapshots kept here;
// each kind publishes the previous and the current key (current drawn on top), so the previous
// picture stays visible while the new one decodes.

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include "d3_activities.h"
#include "d3_assets.h"
#include "d3_build.h"
#include "d3_hg_tabs.h"
#include "d3_options.h"
#include "d3_reader.h"
#include "d3_skills.h"
#include "d3_view.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <variant>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/resource.h>
#endif

namespace {
using namespace dsmod_sdk::int_types;
constexpr u64 TitleId = UINT64_C(0x01001B300B9BE000);
// A pending map picture (Module::map_pending_seq) abandoned after this many Sample() ticks with
// no LoadImage report (never expected live; last_map_ms runs low-single-digit ms) so a dropped
// decode job cannot wedge the map forever.
constexpr u64 kMapPendingTimeoutTicks = 60;
// Quest/bounty objective ring pulse: one ping-pong-free loop (grow + fade, then snap back) every
// kQRingPeriodS seconds, alpha quantised to kQRingTintSteps manifest tint_colors steps (scale_bind
// itself is continuous, no quantising needed). Tuned by eye against a native capture.
constexpr double kQRingPeriodS = 1.1;
constexpr int kQRingTintSteps = 24;

struct MapJob {
    std::vector<d3_reader::SceneMask> scenes;
    std::vector<d3_reader::Snapshot::Marker> markers;
    std::vector<d3_reader::Snapshot::ActorMarker> actors;
    int ax, ay; // global map pixel at the picture centre (d3_view::MapPixel)
    int k;      // magnification: 0.8 * k px per world unit (k x the tiles' own resolution)
};
struct VitJob {
    d3_view::Vitals v;
};
struct TitleJob {
    std::string title, sub;
    int meter = -1; // >= 0: rift kill meter (percent) instead of the area title
};
struct RiftJob {
    d3_view::RiftView r;
};
struct BountyJob {
    d3_view::BountyView b;
};
struct ActJob {
    d3_activities::View v;
};
struct StateJob {
    std::string l1, l2;
};
struct RailJob {
    std::array<d3_skills::Skill, 6> skills{};
    d3_skills::Potion potion;
    bool valid = false;
    int selected = -1;
    std::array<std::int32_t, 7> effective{};
    bool operator==(const RailJob&) const = default;
};
struct CardJob {
    std::array<d3_skills::Skill, 6> skills{};
    d3_skills::Potion potion;
    int selected = -1, scroll = 0;
    bool operator==(const CardJob&) const = default;
};
using Job = std::variant<MapJob, VitJob, TitleJob, StateJob, RiftJob, BountyJob, ActJob, RailJob,
                         CardJob>;

/// Two-slot picture bookkeeping for one kind (previous below, current on top).
struct Slots {
    u64 key[2] = {0, 0}; // seq; 0 = none
    float cx[2] = {0, 0}, cy[2] = {0, 0};
    u32 world[2] = {0, 0};
    void Push(u64 seq, float x = 0, float y = 0, u32 w = 0) {
        key[0] = key[1], cx[0] = cx[1], cy[0] = cy[1], world[0] = world[1];
        key[1] = seq, cx[1] = x, cy[1] = y, world[1] = w;
    }
};

/// Act from a level area SNO name: "A<n>_..." (acts I-IV content) or "X1_"/"x1_" (Reaper of Souls,
/// act V). 0 when the name has neither form.
// Server quest record state (+0x18): 0 not started, 1 in progress, 2 completed (verified live on a
// completed Kill Razormouth bounty: step 1 -> 3031 -> 347795, state 0 -> 1 -> 2).
constexpr u32 QuestInProgress = 1, QuestCompleted = 2;

int ActFromAreaName(const std::string& n) {
    if (n.size() > 2 && (n[0] == 'A' || n[0] == 'a') && n[1] >= '1' && n[1] <= '5' && n[2] == '_')
        return n[1] - '0';
    if (n.rfind("X1_", 0) == 0 || n.rfind("x1_", 0) == 0)
        return 5;
    return 0;
}

struct Module {
    explicit Module(const EdenDsmodHostApi& h, const d3_build::Build& b)
        : host{h}, build{b}, reader{h, b}, tabs{h, b} {
        init_thread = std::thread([this] {
#if defined(__linux__) || defined(__ANDROID__)
            setpriority(PRIO_PROCESS, 0, 19);
#endif
            while (!stop.load()) {
                try {
                    if (assets.Init(&host, build)) {
                        // warm the static art the first pictures need
                        for (const char* t : {"2DUIConsoleMenu", "2DUIConsoleNavMenu",
                                              "2DUIConsoleGameNotify", "2DUI_HealthGlobe_Classic",
                                              "2DMapGeneric"})
                            assets.Tex(&host, t);
                        ready.store(true);
                        return;
                    }
                } catch (...) {
                }
                std::unique_lock lock{wake_mutex};
                wake.wait_for(lock, std::chrono::seconds{1}, [this] { return stop.load(); });
            }
        });
    }
    ~Module() {
        {
            std::lock_guard lock{wake_mutex};
            stop.store(true);
        }
        wake.notify_all();
        if (init_thread.joinable())
            init_thread.join();
    }

    u64 AddJob(Job j) {
        std::lock_guard lock{jobs_mutex};
        const u64 seq = ++next_seq;
        jobs[seq] = std::make_shared<Job>(std::move(j));
        return seq;
    }
    std::shared_ptr<Job> GetJob(u64 seq) {
        std::lock_guard lock{jobs_mutex};
        auto it = jobs.find(seq);
        return it == jobs.end() ? nullptr : it->second;
    }

    /// Drops snapshots no published key refers to (the host may re-request a visible key after
    /// its own cache eviction, so every referenced snapshot stays available).
    void Prune() {
        std::lock_guard lock{jobs_mutex};
        if (jobs.size() <= 12)
            return;
        for (auto it = jobs.begin(); it != jobs.end();) {
            const u64 k = it->first;
            const bool used = k == state_seq || k == vit.key[0] || k == vit.key[1] ||
                              k == title.key[0] || k == title.key[1] || k == map.key[0] ||
                              k == map.key[1] || k == map_pending_seq || k == rift.key[0] ||
                              k == rift.key[1] || k == bounty.key[0] || k == bounty.key[1] ||
                              k == act.key[0] || k == act.key[1] || k == rail_seq ||
                              k == card_seq;
            it = used ? std::next(it) : jobs.erase(it);
        }
    }

    void Publish(const EdenDsmodHostApi& h, const char* name, s64 v) {
        h.publish_i64(h.userdata, name, v);
    }
    void PublishKey(const EdenDsmodHostApi& h, const char* name, const char* kind, u64 seq) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "module:d3:%s/%llu", kind, static_cast<unsigned long long>(seq));
        h.publish_text(h.userdata, name, seq ? buf : "");
    }

    void Sample(const EdenDsmodHostApi& h) {
        const auto t0 = std::chrono::steady_clock::now();
        ++tick;
        const bool map_tick = tick % 15 == 0;
        d3_reader::Snapshot s;
        reader.Read(h, map_tick, s);
        const bool live = s.state == d3_reader::Snapshot::State::Ready;
        if (live)
            was_live_at = tick;
        else {
            // Never carry a previous game's activities or title through menus/loading.
            rift = {}, bounty = {}, act = {};
            rift_state = {}, bounty_state = {}, act_state = {};
            bounty_act = 0;
            guardian_seen = false;
            title_world = title_area = seen_world = 0;
            in_rift_resolved = false;
        }
        const bool assets_ready = ready.load();
        Publish(h, "d3.ready", live && assets_ready ? 1 : 0);
        Publish(h, "d3.assets", assets_ready ? 1 : 0);
        tabs.Sample(h, assets_ready, live, s); // Hero and Gear tabs (d3_hg_tabs) + gear actions
        SampleSkills(h, s, assets_ready);
        if (!assets_ready) {
            PublishCost(h, t0);
            return;
        }
        // ---- state card (no world / loading) ----
        int state_id = live ? 0 : (s.state == d3_reader::Snapshot::State::Loading ? 1 : 2);
        if (s.state == d3_reader::Snapshot::State::NoRoot && !reader.Resolved())
            state_id = 3; // code pins did not match: refuse every reader (no guessing)
        if (state_id != last_state_id) {
            last_state_id = state_id;
            state_seq = 0;
            if (state_id == 1)
                state_seq = AddJob(StateJob{"LOADING", "Waiting for the game world"});
            else if (state_id == 2)
                state_seq = AddJob(StateJob{"DIABLO III", "No game loaded"});
            else if (state_id == 3)
                state_seq = AddJob(StateJob{"UNAVAILABLE", "Game data not recognised"});
        }
        Publish(h, "d3.state.on", state_seq ? 1 : 0);
        PublishKey(h, "d3.state.key", "state", state_seq);
        // ---- vitals column (stale = greyed when no world) ----
        if (live)
            last_vit = d3_view::Quantise(s, true);
        else
            last_vit.live = false;
        if (!vit.key[1] || !(last_vit == vit_state)) {
            if (tick - vit_tick >= 6 || !vit.key[1]) { // <= 10 Hz
                vit_state = last_vit;
                vit.Push(AddJob(VitJob{last_vit}));
                vit_tick = tick;
            }
        }
        PublishKey(h, "d3.vit.k0", "vit", vit.key[0]);
        PublishKey(h, "d3.vit.k1", "vit", vit.key[1]);
        PublishKey(h, "d3.mapvit.k0", "mapvit", vit.key[0]);
        PublishKey(h, "d3.mapvit.k1", "mapvit", vit.key[1]);
        // ---- title ----
        // After a world change the hero's area SNO is updated a little later than the world id:
        // hold the title until the area changes too (or 1.5 s pass, for same-area transitions).
        if (live && s.world != seen_world) {
            seen_world = s.world;
            world_tick = tick;
            area_at_world = s.area_sno;
        }
        const bool title_ok = live && (s.area_sno != area_at_world || tick - world_tick > 90 ||
                                       title_world == s.world);
        if (title_ok && (s.area_sno != title_area || s.difficulty != title_diff ||
                         title_world != s.world)) {
            title_world = s.world;
            title_area = s.area_sno;
            title_diff = s.difficulty;
            std::string t = assets.AreaName(s.area_sno);
            // Rift levels carry placeholder names ("[TEMP] Loot Run Level 01"); the game shows a
            // generated name instead (prefix + noun tables, not read yet).
            if (InRift(s.area_sno) || t.rfind("[TEMP]", 0) == 0)
                t = "Nephalem Rift";
            title_meter = -1;
            title.Push(AddJob(TitleJob{t, d3_view::DifficultyName(s.difficulty)}));
            title_tick = tick;
        }
        // design A: inside a rift the native kill meter replaces the area title
        if (title_ok && title_world == s.world && InRift(s.area_sno)) {
            const int pct = s.rift_progress >= 0
                                ? std::min(100, int(s.rift_progress * 100 / build.rift_progress_denominator))
                                : 0;
            if (pct != title_meter) {
                title_meter = pct;
                title.Push(AddJob(TitleJob{"", "", pct}));
            }
        }
        Publish(h, "d3.title.on", title_ok && title_world == s.world ? 1 : 0);
        // the title is transparent: only the current one is shown (two transparent titles would
        // overlap while the new one decodes, which takes a few ms)
        PublishKey(h, "d3.title.k1", "title", title.key[1]);
        // ---- right column: rift panel while the hero is in a rift level ----
        {
            const bool in_rift = live && InRift(s.area_sno);
            if (in_rift) {
                d3_view::RiftView r;
                r.tier = s.gr_tier;
                r.secs_left = s.timer_left >= 0 ? (s.timer_left + 59) / 60 : -1;
                // build.rift_progress_denominator: the rift's required progress (HUD fraction
                // verified at 7/24/31 % and 3 % on 650, the k277 value; still unresolved, see
                // d3_build.h)
                r.pct = s.rift_progress >= 0
                            ? std::min(100, int(s.rift_progress * 100 / build.rift_progress_denominator))
                            : 0;
                r.deaths = s.gr_deaths;
                r.best_solo = s.gr_tier > 0 ? s.best_solo : -1;
                r.difficulty = s.difficulty;
                if (s.guardian) {
                    guardian_seen = true;
                    r.guardian = s.guardian_hp > 0 ? 1 : 2;
                    r.guardian_pct = s.guardian_hp_max > 0
                                         ? int(std::ceil(s.guardian_hp * 100 / s.guardian_hp_max)) : 0;
                } else if (map_tick) {
                    // out of client range is not death: only an ACD last read at 0 HP counts as
                    // slain; otherwise keep the last reading (live kill not verified yet)
                    r.guardian = !guardian_seen ? 0 : rift_state.guardian_pct <= 0 ? 2 : 1;
                    r.guardian_pct = rift_state.guardian_pct;
                } else {
                    r.guardian = rift_state.guardian, r.guardian_pct = rift_state.guardian_pct;
                }
                if (!rift.key[1] || !(r == rift_state)) {
                    rift_state = r;
                    rift.Push(AddJob(RiftJob{r}));
                }
            } else {
                guardian_seen = false;
            }
            Publish(h, "d3.rift.on", in_rift ? 1 : 0);
            PublishKey(h, "d3.rift.k0", "rift", rift.key[0]);
            PublishKey(h, "d3.rift.k1", "rift", rift.key[1]);
            // ---- bounties (server quest list, full-map ticks): the hero's act, else a summary ----
            if (live && map_tick && s.quests_valid) {
                d3_view::BountyView b;
                std::vector<std::tuple<int, u32, u32, u32>> list; // (act, SNO, area, state), list order
                for (const auto& q : s.quests) {
                    const std::string n = assets.SnoName(q.sno);
                    const auto p = n.find("Bounty_A");
                    if (p == std::string::npos || p + 9 >= n.size() || n[p + 9] != '_' ||
                        n[p + 8] < '1' || n[p + 8] > '5')
                        continue;
                    const int act = n[p + 8] - '0';
                    list.emplace_back(act, q.sno, q.area, q.state);
                    if (q.area == s.area_sno)
                        b.act = act; // the hero stands in a bounty's level area
                }
                b.total = static_cast<int>(list.size());
                if (!b.act)
                    b.act = ActFromAreaName(assets.SnoName(s.area_sno));
                if (!b.act)
                    b.act = bounty_act; // e.g. a dungeon level without an act prefix
                bounty_act = b.act;
                for (const auto& [act, sno, area, state] : list) {
                    if (state == QuestCompleted)
                        ++b.done[act];
                    if (act != b.act)
                        continue;
                    std::string name = assets.QuestName(&h, sno);
                    if (name.rfind("Bounty: ", 0) == 0)
                        name.erase(0, 8);
                    d3_view::BountyView::Entry e{name.empty() ? assets.SnoName(sno) : name, 0};
                    e.state = state == QuestCompleted ? 2
                              : (state == QuestInProgress || area == s.area_sno) ? 1 : 0;
                    b.list.push_back(std::move(e));
                }
                if (!bounty.key[1] || !(b == bounty_state)) {
                    bounty_state = b;
                    bounty.Push(AddJob(BountyJob{b}));
                }
            }
            Publish(h, "d3.bounty.on", live && !in_rift && bounty.key[1] ? 1 : 0);
            PublishKey(h, "d3.bounty.k0", "bounty", bounty.key[0]);
            PublishKey(h, "d3.bounty.k1", "bounty", bounty.key[1]);
            // ---- Activities tab body (rift card or bounty board) ----
            if (live) {
                d3_activities::View av;
                av.rift = in_rift;
                if (in_rift)
                    av.r = rift_state;
                else
                    av.b = bounty_state;
                av.adventure = bounty_state.total > 0;
                if (!act.key[1] || !(av == act_state)) {
                    act_state = av;
                    act.Push(AddJob(ActJob{av}));
                }
            }
            Publish(h, "d3.act.on", live && act.key[1] ? 1 : 0);
            PublishKey(h, "d3.act.k0", "act", act.key[0]);
            PublishKey(h, "d3.act.k1", "act", act.key[1]);
        }
        // ---- nav bar: tabs whose content is unavailable are dropped (owner: exclude, not dim).
        // Map and Options are always present; Hero/Gear read back Tabs' own d3.hero.on/d3.gear.on
        // (d3_hg_tabs.cpp publishes them above, via tabs.Sample()) through the host's get_i64
        // instead of approximating them here, and Activities reuses the exact act.key[1] gate
        // above. Each disappearance is held for ~1 s so a one-tick gap while data is still
        // arriving at boot does not flicker a tab in and out. ----
        {
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (live && !nav_was_live) {
                // World just loaded: Hero/Gear/Activities each catch up over the next few seconds
                // on their own schedule (their own async read/picture pipelines), independently of
                // each other -- hold the bar at whatever it showed just before the load instead of
                // letting the mask change tab-by-tab underneath a tap (owner: a live smoke tapped
                // Hero, landed on Activities, right after a world load).
                nav_hold = true;
                nav_hold_mask = last_nav_mask;
                nav_hold_deadline = now + 3.0;
            }
            nav_was_live = live;
            auto Debounce = [&](bool raw, bool& shown, double& off_since) {
                if (raw) {
                    shown = true;
                    off_since = 0;
                } else if (shown) {
                    if (!off_since) off_since = now;
                    if (now - off_since >= 1.0) shown = false;
                }
            };
            const bool hero_on = h.get_i64 && h.get_i64(h.userdata, "d3.hero.on", 0) != 0;
            const bool gear_on = h.get_i64 && h.get_i64(h.userdata, "d3.gear.on", 0) != 0;
            Debounce(hero_on, nav_hero_on, nav_hero_off_since);
            Debounce(gear_on, nav_gear_on, nav_gear_off_since);
            Debounce(live && act.key[1] != 0, nav_act_on, nav_act_off_since);
            const int mask = (nav_hero_on ? 1 << 0 : 0) | (nav_act_on ? 1 << 1 : 0) | (1 << 2) /* map */ |
                             (nav_gear_on ? 1 << 3 : 0) | (1 << 4) /* options */;
            if (nav_hold && ((nav_hero_on && nav_gear_on && nav_act_on) || now >= nav_hold_deadline))
                nav_hold = false; // all three settled, or ~3 s passed: switch to the settled mask in one step
            const int pub_mask = nav_hold ? nav_hold_mask : mask;
            last_nav_mask = pub_mask;
            Publish(h, "d3.nav.mask", pub_mask);
            const char* page = h.get_text ? h.get_text(h.userdata, "__page") : nullptr;
            char key[48];
            std::snprintf(key, sizeof key, "module:d3:nav/%d/%d", d3_view::NavIndex(page ? page : "map"), pub_mask);
            h.publish_text(h.userdata, "d3.nav.key", key);
        }
        // ---- map: the hero marker stays at (ViewCX, ViewCY) and the pictures move under it.
        // Every picture of one magnification samples the same global pixel grid (MapPixel), so a
        // picture swap is pixel-stable; the offsets are integer pixel differences against each
        // picture's anchor, so panning is continuous without a new picture per step. A new
        // picture comes only for new explored ground/markers, a new world or magnification, or
        // when the hero nears the current picture's overscan edge.
        //
        // A freshly minted picture is decoded off-thread (the host's module asset worker: see
        // LoadImage), so its key is first handed to a hidden, fully-transparent slot (d3.map.k2,
        // manifest-side) purely to start that decode, and is only promoted into the visible k1
        // slot -- key and offset together, in the same tick -- once this module's own LoadImage
        // has actually composed it (map_decoded_seq, written there, read back here). Runtime 21's
        // per-widget "last-known-good" image fallback (mod_ui.cpp DrawImage) draws a widget's
        // PREVIOUS picture at whatever x/y its binds currently read while a new src key is still
        // decoding; since this module used to publish k1's new offset in the same tick as its new
        // key, the fallback drew the OLD map picture at the NEW offset for a few frames -- the
        // reported "blip" (verified against runtime 21 source; beta.7/runtime 20 had no fallback,
        // which is why 0.3.0 looked fine there). Holding the new key+offset back in map_pending_*
        // until decode is confirmed removes that gap. A pending job that never reports back
        // (should not happen; last_map_ms is low-single-digit ms) is abandoned after
        // kMapPendingTimeoutTicks so the map cannot wedge. ----
        if (live) {
            px = s.x, py = s.y;
            if (map_tick) {
                UpdateView(s);
                map_scenes = s.scenes;
                map_markers = s.markers;
                map_actors = s.actor_markers;
                map_world = s.world;
                // which radius-gated actor icons the hero is in range of (cached tags only; an
                // actor whose tags are still unresolved counts once the picture resolved them)
                u64 range = dsmod_sdk::Fnv1a64Basis;
                for (const auto& am : s.actor_markers) {
                    std::map<u32, u32> tags, placement;
                    const u8 in = assets.CachedMapTags(am.actor_sno, am.marker_set_sno,
                                                       am.marker_set_index, tags, placement)
                                      ? (d3_view::ActorInRadius(tags, placement, am.distance_sq) ? 1 : 2)
                                      : 0;
                    range = dsmod_sdk::Fnv1a64(&in, 1, range);
                }
                map_content = s.explored_hash ^ (range * 0x9E3779B97F4A7C15ull);
                // quest/bounty "go here" destination markers (MapIconMarkerDestination) among
                // this tick's markers, world position only: the manifest's own pulsing ring
                // overlay tracks them every tick (see the qring block below) without needing
                // another name lookup meanwhile. d3_view::MapPicture excludes this same texture
                // name from its static bake, so it is never drawn twice.
                map_qring_wld.clear();
                for (const auto& mk : map_markers) {
                    if (static_cast<int>(map_qring_wld.size()) >= d3_view::QRingSlots)
                        break;
                    if (assets.SnoName(mk.tex_sno) == "MapIconMarkerDestination")
                        map_qring_wld.push_back({mk.x, mk.y});
                }
            }
            // promote a pending picture the moment it is confirmed decoded, before deciding
            // whether another one is needed below (key and offset swap into k1 together)
            if (map_pending_seq) {
                if (map_decoded_seq.load(std::memory_order_acquire) == map_pending_seq) {
                    map.Push(map_pending_seq, static_cast<float>(map_pending_ax),
                             static_cast<float>(map_pending_ay), map_pending_world);
                    map_k[0] = map_k[1];
                    map_k[1] = map_pending_k;
                    map_pending_seq = 0;
                } else if (tick - map_pending_since > kMapPendingTimeoutTicks) {
                    map_pending_seq = 0; // stuck or dropped job: let near_edge/changed retry
                }
            }
            float sx, sy;
            d3_view::WorldToScreen(px, py, sx, sy);
            const int hx = d3_view::MapPixel(sx, view_k), hy = d3_view::MapPixel(sy, view_k);
            // picture offset that puts global pixel (hx, hy) under the marker
            const int ox = static_cast<int>(map.cx[1]) - hx + (d3_view::ViewCX - d3_view::MapImgCX);
            const int oy = static_cast<int>(map.cy[1]) - hy + (d3_view::ViewCY - d3_view::MapImgCY);
            const int left = d3_view::MapImgCX - d3_view::MapImgW / 2,
                      top = d3_view::MapImgCY - d3_view::MapImgH / 2;
            // the picture must cover x 0..CanvasW and y 0..NavY
            const bool near_edge =
                left + ox > -d3_view::MapPanMargin ||
                left + ox + d3_view::MapImgW < d3_view::CanvasW + d3_view::MapPanMargin ||
                top + oy > -d3_view::MapPanMargin ||
                top + oy + d3_view::MapImgH < d3_view::NavY + d3_view::MapPanMargin;
            const bool changed = map_content != map_hash || map_world != map.world[1] ||
                                 view_k != map_k[1];
            if (!map_pending_seq && view_k && !map_scenes.empty() &&
                ((changed && map_tick && tick - map_tick_at >= 15) ||
                 ((near_edge || !map.key[1]) && tick - map_tick_at >= 4))) {
                map_hash = map_content;
                const int ax = hx - (d3_view::ViewCX - d3_view::MapImgCX);
                const int ay = hy - (d3_view::ViewCY - d3_view::MapImgCY);
                map_pending_seq = AddJob(MapJob{map_scenes, map_markers, map_actors, ax, ay, view_k});
                map_pending_ax = ax;
                map_pending_ay = ay;
                map_pending_k = view_k;
                map_pending_world = map_world;
                map_pending_since = tick;
                map_tick_at = tick;
                ++map_jobs;
            }
            if (s.facing_valid) {
                float fx, fy;
                d3_view::WorldToScreen(std::cos(s.facing), std::sin(s.facing), fx, fy);
                rot = std::atan2(fy, fx) * 57.29578f + 90.0f; // marker art points up
            }
        }
        {
            float sx, sy;
            d3_view::WorldToScreen(px, py, sx, sy);
            const int hx = d3_view::MapPixel(sx, map_k[1]), hy = d3_view::MapPixel(sy, map_k[1]);
            for (int i = 0; i < 2; ++i) {
                // a previous picture is only shown under the current one at the same scale
                const bool on = live && map.key[i] && map.world[i] == s.world && map_k[i] == map_k[1];
                char n[32];
                std::snprintf(n, sizeof n, "d3.map.k%d", i);
                PublishKey(h, n, "map", on ? map.key[i] : 0);
                std::snprintf(n, sizeof n, "d3.map.k%don", i);
                Publish(h, n, on ? 1 : 0);
                std::snprintf(n, sizeof n, "d3.map.k%dx", i);
                Publish(h, n, static_cast<s64>(map.cx[i]) - hx + (d3_view::ViewCX - d3_view::MapImgCX));
                std::snprintf(n, sizeof n, "d3.map.k%dy", i);
                Publish(h, n, static_cast<s64>(map.cy[i]) - hy + (d3_view::ViewCY - d3_view::MapImgCY));
            }
            // a pending picture is decoded through a hidden, fully-transparent slot purely to
            // start its async decode before it is promoted into k1 (see the block above)
            const bool pend_on = live && map_pending_seq != 0;
            PublishKey(h, "d3.map.k2", "map", pend_on ? map_pending_seq : 0);
            Publish(h, "d3.map.k2on", pend_on ? 1 : 0);
            // quest/bounty objective marker pulse: a ring overlay per tracked dot, positioned as
            // an offset relative to the centred hero exactly like the map pictures above (so it
            // pans in lock-step with them), looping entirely through manifest scale_bind/tint_bind
            // off a wall-clock phase -- no picture re-render, a handful of int publishes per tick.
            const double qnow = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const double qphase = std::fmod(std::max(0.0, qnow), kQRingPeriodS) / kQRingPeriodS;
            const s64 qscale = static_cast<s64>(std::lround(1000.0 + qphase * (d3_view::QRingMaxScaleMilli - 1000)));
            const s64 qtint = std::min<s64>(kQRingTintSteps - 1,
                                            static_cast<s64>(qphase * kQRingTintSteps));
            for (int i = 0; i < d3_view::QRingSlots; ++i) {
                bool on = false;
                int ox = 0, oy = 0;
                if (live && i < static_cast<int>(map_qring_wld.size())) {
                    float qsx, qsy;
                    d3_view::WorldToScreen(map_qring_wld[i][0], map_qring_wld[i][1], qsx, qsy);
                    const int qx = d3_view::MapPixel(qsx, map_k[1]), qy = d3_view::MapPixel(qsy, map_k[1]);
                    ox = d3_view::ViewCX + (qx - hx) - d3_view::QRingSize / 2;
                    oy = d3_view::ViewCY + (qy - hy) - d3_view::QRingSize / 2;
                    // keep clear of the side columns, skill rail and nav bar at the ring's own
                    // worst-case grown size (QRingMargin), not just its resting footprint
                    on = ox - d3_view::QRingMargin >= d3_view::ColW &&
                         ox + d3_view::QRingSize + d3_view::QRingMargin <= d3_view::CanvasW - d3_view::ColW &&
                         oy - d3_view::QRingMargin >= 0 &&
                         oy + d3_view::QRingSize + d3_view::QRingMargin <= d3_skills::RailY;
                }
                char n[32];
                std::snprintf(n, sizeof n, "d3.qring%don", i);
                Publish(h, n, on ? 1 : 0);
                if (on) {
                    std::snprintf(n, sizeof n, "d3.qring%dx", i);
                    Publish(h, n, ox);
                    std::snprintf(n, sizeof n, "d3.qring%dy", i);
                    Publish(h, n, oy);
                    std::snprintf(n, sizeof n, "d3.qring%dscale", i);
                    Publish(h, n, qscale);
                    std::snprintf(n, sizeof n, "d3.qring%dtint", i);
                    Publish(h, n, qtint);
                }
            }
        }
        Prune();
        h.publish_f64(h.userdata, "d3.marker.rot", rot);
        {
            using MS = d3_reader::Snapshot::Menu;
            const MS menu = live ? s.menu : MS::Unknown;
            // design A: pause menu = page dimmed with GAME PAUSED; any other native menu = pages stay
            // readable, actions locked (d3.menu.native, for the Gear tab)
            Publish(h, "d3.menu.pause", menu == MS::Pause ? 1 : 0);
            Publish(h, "d3.menu.native", menu != MS::None && menu != MS::Unknown ? 1 : 0);
            if (menu != last_menu) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "D3 native menu: %d -> %d (paused %d)", static_cast<int>(last_menu),
                              static_cast<int>(menu), s.game_paused ? 1 : 0);
                h.log(h.userdata, EDEN_DSMOD_LOG_INFO, buf);
                last_menu = menu;
            }
            if (live)
                pause_diff = s.difficulty;
            {
                // the host clears published values every tick: publish the key every tick
                char k[32];
                std::snprintf(k, sizeof k, "module:d3:pause/%d", pause_diff);
                h.publish_text(h.userdata, "d3.menu.pause_key", k);
            }
        }
        PublishCost(h, t0);
    }

    void SampleSkills(const EdenDsmodHostApi& h, const d3_reader::Snapshot& s, bool assets_ready) {
        std::array<d3_skills::Skill, 6> data{};
        d3_skills::Potion potion;
        const bool have = tabs.Skills(data, &potion);
        d3_skills::Input input;
        const bool input_read = d3_skills::ReadInput(h, build, s.player_address, input);
        const u32 down = input_read ? input.slots_down : 0;
        const char* page = h.get_text ? h.get_text(h.userdata, "__page") : "";
        const bool can = have && assets_ready && s.state == d3_reader::Snapshot::State::Ready &&
            s.menu == d3_reader::Snapshot::Menu::None && s.hp > 0 && page && std::strcmp(page, "map") == 0;
        std::lock_guard lock{skill_mutex};
        if (input_read != skill_input_valid || (input_read && input.bindings != skill_bindings)) {
            inspector.Clear();
            inspector.previous_down = down;
            skill_bindings = input.bindings;
        }
        if (!input_read)
            input.effective.fill(-1);
        skill_input_valid = input_read;
        // edges must be read from inspector.previous_down before Update() below overwrites it, but
        // it (like `physical`, read straight out of `input` at the log line instead) is otherwise
        // only ever consumed by the D3_SKILL_TRACE print -- cheaply short-circuited to 0 behind the
        // same cached trace flag, matching the gated-compute pattern in d3_reader.cpp/d3_hg_mem.cpp.
        static const bool skill_trace = std::getenv("D3_SKILL_TRACE") != nullptr;
        const u32 edges = skill_trace ? (down & ~inspector.previous_down & 127u) : 0;
        inspector.Update(data, can, s.world, s.player_address, input_read ? down : 0, potion);
        if (skill_trace && edges && h.log) {
            char line[160];
            std::snprintf(line, sizeof line, "D3 skill input: physical=%02x down=%02x edges=%02x menu=%d enabled=%d selected=%d pinned=%d",
                          input.physical & 255u, down & 127u, edges, int(s.menu), can, inspector.selected, inspector.pinned);
            h.log(h.userdata, EDEN_DSMOD_LOG_INFO, line);
        }
        const RailJob rail{inspector.skills, inspector.potion, inspector.enabled, inspector.selected, input.effective};
        if (!rail_seq || !(rail == rail_state)) {
            rail_state = rail;
            rail_seq = AddJob(rail);
        }
        if (inspector.enabled && inspector.selected >= 0) {
            const CardJob card{inspector.skills, inspector.potion, inspector.selected, inspector.scroll};
            if (!card_seq || !(card == card_state)) {
                card_state = card;
                card_seq = AddJob(card);
            }
        }
        Publish(h, "d3.skill.on", inspector.enabled);
        Publish(h, "d3.skill.selected", inspector.enabled && inspector.selected >= 0);
        PublishKey(h, "d3.skill.rail", "rail", rail_seq);
        // the last card stays published while closed so its fade-out has a picture to fade
        PublishKey(h, "d3.skill.card", "card", card_seq);
        // Tap-to-use gates: d3.rail.s<slot>b<bit> = 1 for the button each usable slot answers
        // to right now (absent otherwise, so the manifest's matching button widget fails closed).
        if (inspector.enabled)
            for (int slot = 0; slot < 7; ++slot) {
                const int bit = input.effective[slot];
                if (bit >= 0 && bit < 8 && inspector.Available(slot)) {
                    char n[32];
                    std::snprintf(n, sizeof n, "d3.rail.s%db%d", slot, bit);
                    Publish(h, n, 1);
                }
            }
    }

    bool SkillAction(std::string_view action, s64 arg) {
        if (!action.starts_with("skill_")) return false;
        std::lock_guard lock{skill_mutex};
        if (!inspector.enabled) return true; // consume stale hits during native menus/loading
        if (action == "skill_inspect") inspector.Tap(int(arg));
        else if (action == "skill_close") inspector.Clear();
        else if (action == "skill_scroll" && inspector.selected >= 0) {
            inspector.pinned = true;
            inspector.scroll = std::clamp(inspector.scroll + int(std::clamp<s64>(arg, -3, 3)), 0, 256);
        }
        return true;
    }

    void UpdateView(const d3_reader::Snapshot& s) {
        // explored extent in screen units: explored cells of tracked scenes, whole untracked scenes
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        auto add = [&](float wx, float wy) {
            float sx, sy;
            d3_view::WorldToScreen(wx, wy, sx, sy);
            x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
        };
        for (const auto& sc : s.scenes) {
            if (!sc.tracked) {
                // whole scene, narrowed to the tile's drawn content once it has been decoded
                float b[4] = {0, 0, 1e9f, 1e9f};
                assets.TileContent(sc.tex_sno, b);
                if (b[2] < 0)
                    continue; // tile draws nothing
                const float ax = sc.x0 + b[0], bx = std::min(sc.x1, sc.x0 + b[2]);
                const float ay = sc.y0 + b[1], by = std::min(sc.y1, sc.y0 + b[3]);
                add(ax, ay), add(bx, ay), add(ax, by), add(bx, by);
                continue;
            }
            const float cw = (sc.x1 - sc.x0) / sc.w, ch = (sc.y1 - sc.y0) / sc.h;
            for (u32 i = 0; i < sc.w * sc.h; ++i)
                if (((sc.mask[i >> 2] >> (6 - 2 * (i & 3))) & 3) == 1) {
                    const float wx = sc.x0 + (i / sc.h + 0.5f) * cw, wy = sc.y0 + (i % sc.h + 0.5f) * ch;
                    add(wx - cw / 2, wy - ch / 2), add(wx + cw / 2, wy + ch / 2);
                    add(wx - cw / 2, wy + ch / 2), add(wx + cw / 2, wy - ch / 2);
                }
        }
        add(px, py);
        const float bw = x1 - x0, bh = y1 - y0;
        auto fits = [&](int k, float margin) {
            const float sc = d3_view::ScaleFor(k);
            return bw * sc * margin <= d3_view::ViewW && bh * sc * margin <= d3_view::ViewH;
        };
        int k = d3_view::MinK;
        for (int c = d3_view::MaxK; c >= d3_view::MinK; --c)
            if (fits(c, 1.0f)) {
                k = c;
                break;
            }
        // hysteresis: keep the current zoom unless it stops fitting or one step larger fits easily
        if (view_k && k != view_k && fits(view_k, 1.0f) && !(k > view_k && fits(k, 1.08f)))
            k = view_k;
        view_k = k;
    }

    /// Whether `area_sno` is a Nephalem/Greater Rift level ("X1_LR_" SNO name prefix), cached
    /// per area so the SnoName lookup + string copy runs once per area change instead of every
    /// tick (title, kill-meter and right-column all test this on the hero's current area).
    bool InRift(u32 area_sno) {
        if (!in_rift_resolved || area_sno != in_rift_area) {
            in_rift_area = area_sno;
            in_rift_value = assets.SnoName(area_sno).rfind("X1_LR_", 0) == 0;
            in_rift_resolved = true;
        }
        return in_rift_value;
    }

    void PublishCost(const EdenDsmodHostApi& h, std::chrono::steady_clock::time_point t0) {
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        cost_sum += us;
        cost_max = std::max(cost_max, us);
        if (++cost_n == 300) {
            cost_avg_pub = cost_sum / cost_n;
            cost_max_pub = cost_max;
            cost_sum = cost_max = 0;
            cost_n = 0;
            static const bool trace = std::getenv("D3_HG_DEBUG") != nullptr; // same switch as d3hg::Tabs's own cycle-cost log
            if (trace && h.log) {
                char buf[160];
                std::snprintf(buf, sizeof buf,
                              "D3 sample cost: avg %.1f us max %.1f us (300 ticks); map jobs %lld; "
                              "last map %.1f ms",
                              cost_avg_pub, cost_max_pub, static_cast<long long>(map_jobs),
                              last_map_ms.load());
                h.log(h.userdata, EDEN_DSMOD_LOG_INFO, buf);
            }
        }
        h.publish_f64(h.userdata, "d3.perf.sample_avg_us", cost_avg_pub);
        h.publish_f64(h.userdata, "d3.perf.sample_max_us", cost_max_pub);
        h.publish_f64(h.userdata, "d3.perf.map_ms", last_map_ms.load());
    }

    EdenDsmodBool LoadImage(const EdenDsmodHostApi* ih, const char* key, void* receiver,
                            EdenDsmodImageSink sink) {
        if (!ih || !key || !sink || !ready.load())
            return EDEN_DSMOD_FALSE;
        std::string_view k{key};
        if (k.starts_with("module:"))
            k.remove_prefix(7);
        if (!k.starts_with("d3:"))
            return EDEN_DSMOD_FALSE;
        k.remove_prefix(3);
        d3_img::Image img;
        if (tabs.Load(assets, ih, k, img)) {
            if (img.Empty())
                return EDEN_DSMOD_FALSE;
        } else if (k == "right")
            img = d3_view::RightColumn();
        else if (k.starts_with("options/"))
            img = d3_options::Page(assets, ih, k.back() == '1');
        else if (k.starts_with("nav/")) {
            // "nav/<active>/<mask>": active = NavIndex of the current page, mask = d3.nav.mask
            std::string_view nk = k.substr(4);
            const size_t slash = nk.find('/');
            const int active = std::atoi(std::string{nk.substr(0, slash)}.c_str());
            const int mask = slash == std::string_view::npos ? 0x1F
                                                              : std::atoi(std::string{nk.substr(slash + 1)}.c_str());
            img = d3_view::NavBar(assets, ih, active, mask);
        }
        else if (k == "none")
            img = d3_img::Image(1, 1);
        else if (k.starts_with("pause/"))
            img = d3_view::PauseOverlay(assets, ih, std::atoi(std::string{k.substr(6)}.c_str()));
        else if (k == "marker")
            img = d3_view::PlayerMarker(assets, ih);
        else if (k == "qring")
            img = d3_view::ObjectiveRing(assets, ih);
        else {
            const size_t slash = k.find('/');
            if (slash == std::string_view::npos)
                return EDEN_DSMOD_FALSE;
            const u64 seq = std::strtoull(std::string{k.substr(slash + 1)}.c_str(), nullptr, 10);
            auto job = GetJob(seq);
            if (!job)
                return EDEN_DSMOD_FALSE;
            const auto t0 = std::chrono::steady_clock::now();
            if (auto* m = std::get_if<MapJob>(job.get())) {
                img = d3_view::MapPicture(assets, ih, m->scenes, m->ax, m->ay, m->k, m->markers, m->actors);
                last_map_ms.store(std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - t0)
                                      .count());
                // Sample() holds a freshly minted map key+offset out of the visible k1 slot until
                // it observes the decode land here (map_pending_seq / map_decoded_seq) -- the map
                // block's comment above explains why (runtime 21's per-widget image fallback).
                if (!img.Empty())
                    map_decoded_seq.store(seq, std::memory_order_release);
            } else if (auto* v = std::get_if<VitJob>(job.get()))
                img = d3_view::VitalsColumn(assets, ih, v->v, k.starts_with("mapvit/"));
            else if (auto* t = std::get_if<TitleJob>(job.get()))
                img = t->meter >= 0 ? d3_view::RiftMeter(assets, ih, t->meter)
                                    : d3_view::Title(assets, t->title, t->sub);
            else if (auto* rj = std::get_if<RiftJob>(job.get()))
                img = d3_view::RiftColumn(assets, ih, rj->r);
            else if (auto* aj = std::get_if<ActJob>(job.get()))
                img = d3_activities::Body(assets, ih, aj->v);
            else if (auto* bj = std::get_if<BountyJob>(job.get()))
                img = d3_view::BountyColumn(assets, ih, bj->b);
            else if (auto* st = std::get_if<StateJob>(job.get()))
                img = d3_view::StateCard(assets, st->l1, st->l2);
            else if (auto* rl = std::get_if<RailJob>(job.get()))
                img = d3_skills::RenderRail(assets, ih, skill_text, rl->skills, rl->valid, rl->selected, rl->potion, rl->effective);
            else if (auto* cd = std::get_if<CardJob>(job.get()))
                img = d3_skills::RenderCard(assets, ih, skill_text, cd->skills, cd->selected, cd->scroll, cd->potion);
        }
        if (img.Empty())
            return EDEN_DSMOD_FALSE;
        sink(receiver, static_cast<u32>(img.w), static_cast<u32>(img.h), img.px.data(),
             img.px.size());
        return EDEN_DSMOD_TRUE;
    }

    EdenDsmodHostApi host;
    const d3_build::Build& build;
    d3_reader::Reader reader;
    d3_assets::Assets assets;
    d3hg::Tabs tabs;
    d3hg::Text skill_text;
    std::mutex skill_mutex;
    d3_skills::Inspector inspector;
    RailJob rail_state;
    CardJob card_state;
    u64 rail_seq = 0, card_seq = 0;
    std::array<std::int32_t, 7> skill_bindings{};
    bool skill_input_valid = false;
    std::atomic<bool> ready{false}, stop{false};
    std::mutex wake_mutex;
    std::condition_variable wake;
    std::thread init_thread;

    std::mutex jobs_mutex;
    std::map<u64, std::shared_ptr<Job>> jobs;
    u64 next_seq = 0;

    u64 tick = 0, was_live_at = 0, vit_tick = 0, map_tick_at = 0, title_tick = 0;
    int last_state_id = -1;
    u64 state_seq = 0;
    Slots vit, title, map, rift, bounty, act;
    d3_activities::View act_state;
    d3_view::RiftView rift_state;
    d3_view::BountyView bounty_state;
    int bounty_act = 0;
    bool guardian_seen = false;
    // dynamic nav bar: debounced per-tab availability (see Sample's nav block)
    bool nav_hero_on = false, nav_act_on = false, nav_gear_on = false;
    double nav_hero_off_since = 0, nav_act_off_since = 0, nav_gear_off_since = 0;
    // A fresh world load: Hero/Gear/Activities each join the bar on their own async schedule, a few
    // seconds apart, independently of the per-tab 1 s disappearance debounce above -- held at
    // `nav_hold_mask` (the mask shown just before the load) until all three settle or ~3 s pass; see
    // the nav bar block in Sample().
    bool nav_was_live = false, nav_hold = false;
    double nav_hold_deadline = 0;
    int nav_hold_mask = (1 << 2) | (1 << 4), last_nav_mask = (1 << 2) | (1 << 4); // Map + Options only
    d3_view::Vitals last_vit, vit_state;
    u32 title_area = 0, title_world = 0, seen_world = 0, area_at_world = 0;
    u64 world_tick = 0;
    u32 in_rift_area = 0;
    bool in_rift_resolved = false, in_rift_value = false;
    int title_diff = -2, title_meter = -1, pause_diff = -100;
    d3_reader::Snapshot::Menu last_menu = d3_reader::Snapshot::Menu::Unknown;
    u64 map_hash = 0;
    s64 map_jobs = 0;
    float px = 0, py = 0;
    int view_k = 0, map_k[2] = {0, 0};
    // a freshly minted map key, held out of the visible k1 slot until LoadImage confirms it
    // decoded (map_decoded_seq, written there under jobs_mutex's happens-before via AddJob/GetJob,
    // read here with acquire/release): see the map block comment in Sample() for why.
    u64 map_pending_seq = 0, map_pending_since = 0;
    int map_pending_ax = 0, map_pending_ay = 0;
    u32 map_pending_k = 0, map_pending_world = 0;
    std::atomic<u64> map_decoded_seq{0};
    // last full-map read (scenes and markers are only read on map ticks)
    std::vector<d3_reader::SceneMask> map_scenes;
    std::vector<d3_reader::Snapshot::Marker> map_markers;
    std::vector<d3_reader::Snapshot::ActorMarker> map_actors;
    u32 map_world = 0;
    u64 map_content = 0;
    // quest/bounty objective dot world positions this map tick (d3_view::QRingSlots cap)
    std::vector<std::array<float, 2>> map_qring_wld;
    double rot = 0;
    std::atomic<double> last_map_ms{0};
    double cost_sum = 0, cost_max = 0, cost_avg_pub = 0, cost_max_pub = 0;
    int cost_n = 0;
};

EdenDsmodBool SupportsBuild(const char* build_hex) {
    return d3_reader::SupportsBuildHex(build_hex) ? EDEN_DSMOD_TRUE : EDEN_DSMOD_FALSE;
}
void* Create(const EdenDsmodHostApi* host, const char*) {
    try {
        if (!dsmod_sdk::HostAbiMatches(host) || host->title_id != TitleId || !host->is_mapped ||
            !host->read_memory || !host->read_romfs || !host->publish_text)
            return nullptr;
        const d3_build::Build* build = d3_build::SelectBuild(host->build_id);
        if (!build)
            return nullptr;
        if (host->log) {
            char line[96];
            std::snprintf(line, sizeof line, "D3 build: %s (%s)", build->name, build->version);
            host->log(host->userdata, EDEN_DSMOD_LOG_INFO, line);
        }
        return new Module{*host, *build};
    } catch (...) {
        return nullptr;
    }
}
void Destroy(void* p) {
    try {
        delete static_cast<Module*>(p);
    } catch (...) {
    }
}
void SampleCallback(void* p, const EdenDsmodHostApi* host) {
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    try {
        if (p && host)
            static_cast<Module*>(p)->Sample(*host);
    } catch (...) {
        if (!reported.test_and_set() && host && host->log)
            host->log(host->userdata, EDEN_DSMOD_LOG_ERROR, "Diablo III DSMod sample failed");
    }
}
void TickCallback(void*, const EdenDsmodHostApi*) {}
void ConfigureCallback(void* p, const EdenDsmodHostExtensions* ext) {
    try {
        if (p)
            static_cast<Module*>(p)->tabs.ConfigureGuest(ext);
    } catch (...) {
    }
}
EdenDsmodBool ActionCallback(void* p, const char* action, std::int64_t arg) {
    // page-local actions (Gear selection, backpack view, Details scroll, skill card), the Gear
    // Equip / Unequip requests (instant, guest load-plan mailbox, d3_gear_mailbox) and Drop
    // (carried out by the native inventory menu, d3_gear_drive); tap-to-use presses are manifest
    // button actions and never reach the module
    try {
        if (p && action && (static_cast<Module*>(p)->SkillAction(action, arg) ||
                            static_cast<Module*>(p)->tabs.Action(static_cast<Module*>(p)->host, action, arg)))
            return EDEN_DSMOD_TRUE;
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}
EdenDsmodBool LoadImageCallback(void* p, const EdenDsmodHostApi* host, const char* key,
                                void* receiver, EdenDsmodImageSink sink) {
    try {
        if (p)
            return static_cast<Module*>(p)->LoadImage(host, key, receiver, sink);
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}

const EdenDsmodModuleApi ModuleApi{EDEN_DSMOD_MODULE_ABI_VERSION,
                                   sizeof(EdenDsmodModuleApi),
                                   0,
                                   EDEN_DSMOD_MODULE_ABI_HASH,
                                   TitleId,
                                   "Diablo III DSMod",
                                   EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_ROMFS_READ,
                                   &SupportsBuild,
                                   &Create,
                                   &Destroy,
                                   &SampleCallback,
                                   &TickCallback};
const EdenDsmodModuleExtensions ModuleExtensions{
    EDEN_DSMOD_EXT_VERSION, sizeof(EdenDsmodModuleExtensions),
    EDEN_DSMOD_EXT_HASH,    &ConfigureCallback,
    &ActionCallback,        &LoadImageCallback};
} // namespace

DSMOD_SDK_EXPORT_MODULE(ModuleApi)
DSMOD_SDK_EXPORT_EXTENSIONS(ModuleExtensions)
