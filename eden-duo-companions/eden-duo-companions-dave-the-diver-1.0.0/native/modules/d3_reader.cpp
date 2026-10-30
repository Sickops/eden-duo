// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <unordered_set>

#include "core/mods/modules/dsmod_module_sdk.h"

namespace d3_reader {
namespace {
using namespace dsmod_sdk::int_types;

// Code pins (main-relative .text offsets) with their exact instruction words. Offset + words are
// per-build (d3_build::Build::Pin2); only the decode shape here (ADRP + LDR, `ldr_gap` apart) is
// shared, since INVENTORY.md does not call that shape itself build-specific.
struct Pin {
    u64 adrp_off;
    u32 adrp_word;
    u32 ldr_word;
};
Pin ToPin(const d3_build::Pin2& p) {
    return {p.off, p.w0, p.w1};
}
constexpr u64 GWindows = 0xB80;        // -> -> window map {u32 mask, buckets +0x10}; node {next, key, ctl}
// Native HUD consumers main+0x161DF0..F4 and +0x3D3B30..34 test bit 2, not bit 4.
constexpr u64 CtlFlags = 0x18;         // control flags byte: 0x04 = shown

// Structure offsets (CORE-READERS.md / EXPLORED-MAP.md).
constexpr u64 AppGame = 0x10;          // CGame* G = [AppGlobals + 0x10]
constexpr u64 GPlayers = 0x948;        // player array base
constexpr u64 GLocalIndex = 0xBA0;     // -> i32 local player index
constexpr u64 GAcdContainer = 0x9A0;   // -> -> container "ActorCommonData"
constexpr u64 GDifficulty = 0x808 + 4; // GCD + 4
constexpr u64 PlayerStride = 0xEBF8, PlayerBase = 0x60;
constexpr u64 PAcd = 0x4, PName = 0xB210, PArea = 0xD680, PClass = 0xD688, PLevel = 0xD68C,
              PParagon = 0xD690;
constexpr u64 AcdSize = 0x360, AcdPos = 0x60, AcdRot = 0x58, AcdWorld = 0xA0, AcdAttrs = 0x168;
constexpr u32 AttrHpCur = 0x73, AttrHpMax = 0x7C, AttrResPrimary = 0x94, AttrResSecondary = 0x95,
              AttrResCur = 0x96, AttrResMax = 0x99;
constexpr u32 NoParam = 0xFFFFF;
constexpr u32 AttrHighestSoloRift = 0xA7;
constexpr u32 AttrInTieredRiftLevel = 0x577, AttrIsLootRunBoss = 0x578, AttrTieredDeaths = 0x595;
constexpr u64 GFrameStep = 0x5C;
constexpr u64 GRiftUi = 0x800, RiftProgress = 0x10C, GTimers = 0xBF0, GTick = 0x808 + 0x120;
constexpr u32 TieredRiftTimerSno = 382698; // TimedEvent X1_LR_TieredRift_Timer
// Server side (in-process): sigma thread data is a .bss global (same offset as 2.7.6, d3hack
// sigma_thread_data; no code literal loads it directly). This module only runs on the exact build
// ID above, and the container name "SGames" is checked before use.
constexpr u64 SigmaThreadData = 0x191CA70, SigmaSGames = 0x1A0, SGamesFirst = 0x120;
constexpr u64 SGameQuests = 0x590; // quest list: elem 0x178, magic +0x28, head +0x30, count +0x40
constexpr u64 QuestRecSize = 0x178, QuestNext = 0x170;

struct Mem {
    const EdenDsmodHostApi& h;
    bool Read(u64 a, void* out, u64 n) const {
        return a && h.is_mapped(h.userdata, a, n) && h.read_memory(h.userdata, a, out, n);
    }
    template <typename T>
    bool Get(u64 a, T& v) const {
        return Read(a, &v, sizeof(T));
    }
    u64 Ptr(u64 a) const {
        u64 v = 0;
        return Get(a, v) ? v : 0;
    }
};

bool PlausibleHeap(u64 p) {
    return p >= 0x1000000 && p < (u64{1} << 40) && (p & 3) == 0; // bucket arrays are 4-aligned
}

// Decodes ADRP Xd + LDR Xt,[Xd,#imm] at main+off and returns the absolute literal address.
u64 DecodePin(const Mem& m, u64 main_base, const Pin& pin, u64 ldr_gap = 4) {
    u32 w[2];
    if (!m.Get(main_base + pin.adrp_off, w[0]) || !m.Get(main_base + pin.adrp_off + ldr_gap, w[1]) ||
        w[0] != pin.adrp_word || w[1] != pin.ldr_word)
        return 0;
    const u32 ins = w[0];
    const s64 immhi = (ins >> 5) & 0x7FFFF, immlo = (ins >> 29) & 3;
    s64 imm = (immhi << 2) | immlo;
    if (imm & (s64{1} << 20))
        imm -= s64{1} << 21;
    const u64 pc = main_base + pin.adrp_off;
    const u64 page = (pc & ~u64{0xFFF}) + static_cast<u64>(imm << 12);
    const u64 off = ((w[1] >> 10) & 0xFFF) * 8;
    return page + off;
}

// FastAttrib map lookup (main+0x69F510 shape): bucket = (key ^ key >> 12) & mask.
bool AttrRaw(const Mem& m, u64 acd, u32 attrib, u32 param, u32& out) {
    const u64 g = m.Ptr(acd + AcdAttrs);
    if (!PlausibleHeap(g))
        return false;
    u64 map = m.Ptr(g + 0x10);
    if (!map)
        map = g + 0x18;
    u32 mask = 0, nb = 0;
    u64 buckets = 0;
    if (!m.Get(map, mask) || !m.Get(map + 0x10, buckets) || !m.Get(map + 0x18, nb) ||
        nb != mask + 1 || nb > 0x10000 || !PlausibleHeap(buckets))
        return false;
    const u32 key = attrib | (param << 12);
    u64 n = m.Ptr(buckets + 8 * ((key ^ (key >> 12)) & mask));
    for (int guard = 0; n && guard < 256; ++guard) {
        u64 next = 0;
        u32 kv[2];
        if (!m.Get(n, next) || !m.Read(n + 8, kv, 8))
            return false;
        if (kv[0] == key) {
            out = kv[1];
            return true;
        }
        n = next;
    }
    return false; // absent: the descriptor default (0 / -1 for the attributes used here)
}
double AttrF(const Mem& m, u64 acd, u32 attrib, u32 param = NoParam) {
    u32 v = 0;
    if (!AttrRaw(m, acd, attrib, param, v))
        return 0;
    float f;
    std::memcpy(&f, &v, 4);
    return std::isfinite(f) ? f : 0;
}
int AttrI(const Mem& m, u64 acd, u32 attrib, int def) {
    u32 v = 0;
    return AttrRaw(m, acd, attrib, NoParam, v) ? static_cast<int>(v) : def;
}

u64 AcdById(const Mem& m, u64 g, u32 id) {
    const u64 c = m.Ptr(m.Ptr(g + GAcdContainer));
    char name[16] = {};
    if (!PlausibleHeap(c) || !m.Read(c, name, 16) || std::memcmp(name, "ActorCommonData", 16) != 0)
        return 0;
    u32 cap = 0, shift = 0;
    u64 blocks = 0;
    if (!m.Get(c + 0x100, cap) || !m.Get(c + 0x168, shift) || !m.Get(c + 0x120, blocks) ||
        shift > 16)
        return 0;
    const u32 idx = id & 0xFFFF;
    if (idx >= cap)
        return 0;
    const u64 blk = m.Ptr(blocks + 8 * (idx >> shift));
    if (!PlausibleHeap(blk))
        return 0;
    const u64 acd = blk + u64(idx & ((1u << shift) - 1)) * AcdSize;
    u32 echo = 0;
    return m.Get(acd, echo) && echo == id ? acd : 0;
}

// main+0x4769E0 client ANN -> ACD lookup. The index map is owned by the same wrapper
// as the ACD container; echo both the full ACD id and ANN to reject recycled entries.
u64 AcdByAnn(const Mem& m, u64 g, u32 ann) {
    if (ann == 0xFFFFFFFFu || ((ann >> 2) & 0x3FFFu) > 0xEE6u)
        return 0;
    const u64 wrapper = m.Ptr(g + GAcdContainer);
    const u64 indices = PlausibleHeap(wrapper) ? m.Ptr(wrapper + 8) : 0;
    u32 id = 0xFFFFFFFFu;
    if (!PlausibleHeap(indices) || !m.Get(indices + 4 * u64(ann & 0xFFFFu), id) ||
        id == 0xFFFFFFFFu)
        return 0;
    const u64 acd = AcdById(m, g, id);
    u32 echo = 0;
    return acd && m.Get(acd + 0x10, echo) && echo == ann ? acd : 0;
}

u64 RActorById(const Mem& m, u64 g, u32 id) {
    if (id == 0xFFFFFFFFu)
        return 0;
    const u64 c = m.Ptr(g + 0xA98);
    u32 cap = 0, shift = 0, stride = 0;
    u64 blocks = 0;
    if (!PlausibleHeap(c) || !m.Get(c + 0x100, cap) || cap > 0x10000 ||
        !m.Get(c + 0x104, stride) || stride != 0x410 ||
        !m.Get(c + 0x168, shift) || shift > 16 || !m.Get(c + 0x120, blocks) ||
        !PlausibleHeap(blocks))
        return 0;
    const u32 i = id & 0xFFFFu;
    if (i >= cap)
        return 0;
    const u64 block = m.Ptr(blocks + 8 * u64(i >> shift));
    if (!PlausibleHeap(block))
        return 0;
    const u64 actor = block + u64(i & ((1u << shift) - 1)) * 0x410;
    u32 echo = 0;
    return m.Get(actor, echo) && echo == id ? actor : 0;
}
} // namespace

bool SupportsBuildHex(const char* hex) {
    return d3_build::SelectBuildHex(hex) != nullptr;
}

Reader::Reader(const EdenDsmodHostApi& host, const d3_build::Build& build)
    : build{build}, main_base{host.main_base} {}

bool Reader::Resolve(const EdenDsmodHostApi& host, std::string& err) {
    Mem m{host};
    const Pin app_globals_pin = ToPin(build.app_globals), minimap_pin = ToPin(build.minimap);
    const u64 a = DecodePin(m, main_base, app_globals_pin), b = DecodePin(m, main_base, minimap_pin);
    if (!a || !b) {
        err = "code pin mismatch";
        return false;
    }
    s64 delta = 0;
    if (host.get_i64)
        delta = host.get_i64(host.userdata, "__relocation_delta", 0);
    app_literal = a + static_cast<u64>(delta);
    minimap_literal = b + static_cast<u64>(delta);
    sigma_global = main_base + SigmaThreadData + static_cast<u64>(delta);
    // map-marker list getter main+0x2CEA80 (adrp x8; ldr x8,[x8,#0x590]; ldr x0,[x8,#8]; ret);
    // optional: a mismatch only drops the markers of actors outside client range
    const u64 mk = DecodePin(m, main_base, ToPin(build.map_markers));
    map_markers_literal = mk ? mk + static_cast<u64>(delta) : 0;
    // window key GOT slots (optional: menu state stays unknown when a pin does not match)
    const d3_build::Pin2* kp[4] = {&build.hud_key, &build.pause_key, &build.inv_key, &build.waypoint_key};
    for (int i = 0; i < 4; ++i) {
        const u64 got = DecodePin(m, main_base, ToPin(*kp[i]), 8);
        window_got[i] = got ? got + static_cast<u64>(delta) : 0;
    }
    return true;
}

void Reader::Read(const EdenDsmodHostApi& host, bool full_map, Snapshot& out) {
    Mem m{host};
    out.state = Snapshot::State::NoRoot;
    out.player_address = 0;
    out.actor_markers.clear();
    if (!Resolved() && !Resolve(host, out.root_error))
        return;
    const u64 app = m.Ptr(app_literal);
    if (!PlausibleHeap(app)) {
        out.root_error = "AppGlobals not ready";
        return;
    }
    out.state = Snapshot::State::NoWorld;
    const u64 g = m.Ptr(app + AppGame);
    if (!PlausibleHeap(g))
        return;
    s32 idx = -1;
    if (!m.Get(m.Ptr(g + GLocalIndex), idx) || idx < 0 || idx > 3)
        return; // main menu / hero select: player index -1
    out.state = Snapshot::State::Loading;
    const u64 players = m.Ptr(g + GPlayers);
    if (!PlausibleHeap(players))
        return;
    const u64 p = players + u64(idx) * PlayerStride + PlayerBase;
    u32 acd_id = 0;
    if (!m.Get(p + PAcd, acd_id) || acd_id == 0xFFFFFFFFu)
        return;
    const u64 acd = AcdById(m, g, acd_id);
    if (!acd)
        return;
    char name[50] = {};
    m.Read(p + PName, name, 49);
    name[49] = 0;
    out.name = name;
    m.Get(p + PClass, out.cls);
    m.Get(p + PLevel, out.level);
    m.Get(p + PParagon, out.paragon);
    m.Get(p + PArea, out.area_sno);
    m.Get(g + GDifficulty, out.difficulty);
    float pos[3] = {};
    m.Read(acd + AcdPos, pos, 12);
    out.x = pos[0], out.y = pos[1];
    float rot[2] = {};
    out.facing_valid = false;
    if (m.Read(acd + AcdRot, rot, 8) && std::isfinite(rot[0]) && std::isfinite(rot[1]) &&
        std::fabs(rot[0] * rot[0] + rot[1] * rot[1] - 1.0f) < 0.05f) {
        // Z-axis quaternion (z, w): yaw = 2 * atan2(z, w).
        out.facing = 2.0f * std::atan2(rot[0], rot[1]);
        out.facing_valid = true;
    }
    m.Get(acd + AcdWorld, out.world);
    out.hp = AttrF(m, acd, AttrHpCur);
    out.hp_max = AttrF(m, acd, AttrHpMax);
    out.res_type[0] = AttrI(m, acd, AttrResPrimary, -1);
    out.res_type[1] = AttrI(m, acd, AttrResSecondary, -1);
    for (int i = 0; i < 2; ++i) {
        const int t = out.res_type[i];
        if (t >= 0 && t < 16) {
            out.res_cur[i] = AttrF(m, acd, AttrResCur, static_cast<u32>(t));
            out.res_max[i] = AttrF(m, acd, AttrResMax, static_cast<u32>(t));
        }
    }
    out.state = Snapshot::State::Ready;
    out.player_address = p;
    m.Get(g + GTick, out.tick);
    // G+0x5C: this frame's game time step; 0 while a full-screen native menu (map, inventory /
    // skills, pause menu, obelisk) has paused the single-player game (verified for those four)
    u32 step = 1;
    out.game_paused = m.Get(g + GFrameStep, step) && step == 0;
    // native menus: the shown flag of the top-level windows (main+0x3278C0 resolves a key in the
    // window map with FNV-1a 32 over the key's low 4 bytes)
    {
        const u64 wm = m.Ptr(m.Ptr(g + GWindows));
        u32 mask = 0;
        u64 buckets = 0;
        int shown[4] = {-1, -1, -1, -1};
        int raw_flags[4] = {-1, -1, -1, -1};
        if (PlausibleHeap(wm) && m.Get(wm, mask) && m.Get(wm + 0x10, buckets) && mask < 0x100000 &&
            PlausibleHeap(buckets))
            for (int i = 0; i < 4; ++i) {
                const u64 var = window_got[i] ? m.Ptr(window_got[i]) : 0;
                u64 key = 0;
                if (!var || !m.Get(var, key) || !key)
                    continue;
                u32 hsh = 0x811C9DC5u;
                for (int b = 0; b < 4; ++b)
                    hsh = (hsh ^ ((key >> (8 * b)) & 0xFF)) * 0x01000193u;
                u64 n = m.Ptr(buckets + 8 * u64(hsh & mask));
                for (int guard = 0; n && guard < 64; ++guard) {
                    u64 node[3];
                    if (!m.Read(n, node, sizeof node))
                        break;
                    if (node[1] == key) {
                        u8 f = 0;
                        if (PlausibleHeap(node[2]) && m.Get(node[2] + CtlFlags, f)) {
                            raw_flags[i] = f;
                            shown[i] = (f & 0x04) ? 1 : 0;
                        }
                        break;
                    }
                    n = node[0];
                }
            }
        using MS = Snapshot::Menu;
        if (shown[0] < 0)
            out.menu = MS::Unknown;
        else if (shown[1] == 1)
            out.menu = MS::Pause;
        else if (shown[2] == 1)
            out.menu = MS::Inventory;
        else if (shown[3] == 1)
            out.menu = MS::WorldMap;
        else if (shown[0] == 0)
            out.menu = MS::Other; // HUD hidden: zone map, vendor/artisan, skills, cube, ...
        else
            out.menu = MS::None;
        if (std::getenv("D3_SKILL_TRACE") && host.log) {
            char line[256];
            std::snprintf(line, sizeof line,
                          "D3 reader UI: menu=%d paused=%d shown=%d,%d,%d,%d flags=%d,%d,%d,%d",
                          static_cast<int>(out.menu), out.game_paused ? 1 : 0,
                          shown[0], shown[1], shown[2], shown[3],
                          raw_flags[0], raw_flags[1], raw_flags[2], raw_flags[3]);
            if (menu_trace != line) {
                menu_trace = line;
                host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line);
            }
        }
    }
    // rift progress: the object the rift HUD reads at main+0x3D3E9C ([G+0x800]+0x10C)
    {
        const u64 ui = m.Ptr(g + GRiftUi);
        float v = -1;
        if (PlausibleHeap(ui) && m.Get(ui + RiftProgress, v) && std::isfinite(v) && v >= 0 && v < 1e6)
            out.rift_progress = v;
    }
    {
        u32 lvl = 0;
        if (AttrRaw(m, acd, AttrInTieredRiftLevel, NoParam, lvl) && lvl < 1000)
            out.gr_tier = static_cast<int>(lvl) + 1; // stored 0-based ("Level 11" = 10)
        out.gr_deaths = AttrI(m, acd, AttrTieredDeaths, 0);
        out.best_solo = AttrI(m, acd, AttrHighestSoloRift, -1);
    }
    // client timers [[G+0xBF0]]: {u32 id, u32 start, u32 end, u32 paused, ..., next @+0x18}
    // (main+0xD2900 / 0xD2970: remaining = end - now - paused, shown min(remaining, end - start))
    for (u64 n = m.Ptr(m.Ptr(g + GTimers)), guard = 0; n && guard < 64; ++guard) {
        u32 r[4];
        if (!m.Read(n, r, 16))
            break;
        if (r[0] == TieredRiftTimerSno && r[2] >= r[1]) {
            const s64 left = s64(r[2]) - s64(out.tick) - s64(r[3]);
            out.timer_total = static_cast<int>(r[2] - r[1]);
            out.timer_left = static_cast<int>(std::clamp<s64>(left, 0, out.timer_total));
            break;
        }
        n = m.Ptr(n + 0x18);
    }
    if (!full_map)
        return;
    // Rift Guardian: a monster ACD with Is_Loot_Run_Boss (scanned on map ticks only). Batched like
    // d3hg::Reader's own ActorCommonData walk (d3_hg_mem.cpp Phase::Scan): up to 64 entries per
    // host read instead of one m.Read() per ACD.
    {
        const u64 c = m.Ptr(m.Ptr(g + GAcdContainer));
        u32 cap = 0, shift = 0, maxi = 0;
        u64 blocks = 0;
        if (PlausibleHeap(c) && m.Get(c + 0x100, cap) && m.Get(c + 0x108, maxi) &&
            m.Get(c + 0x168, shift) && m.Get(c + 0x120, blocks) && shift <= 16 && maxi < cap) {
            const u32 per = 1u << shift;
            std::vector<u8> blk;
            for (u32 i = 0; i <= maxi;) {
                const u64 base = m.Ptr(blocks + 8 * u64(i >> shift));
                const u32 in = i & (per - 1);
                const u32 n = std::min({64u, per - in, maxi + 1 - i});
                bool found = false;
                if (PlausibleHeap(base)) {
                    blk.resize(u64{n} * AcdSize);
                    if (m.Read(base + u64{in} * AcdSize, blk.data(), blk.size()))
                        for (u32 j = 0; j < n; ++j) {
                            const u8* r = &blk[u64{j} * AcdSize];
                            s32 type = 0;
                            u32 id = 0;
                            std::memcpy(&id, r, 4);
                            std::memcpy(&type, r + 0x14C, 4);
                            if (id == 0xFFFFFFFFu || type != 1)
                                continue;
                            const u64 a = base + u64(in + j) * AcdSize;
                            u32 boss = 0;
                            if (AttrRaw(m, a, AttrIsLootRunBoss, NoParam, boss) && boss) {
                                float hp = 0;
                                std::memcpy(&hp, r + 0x150, 4);
                                out.guardian = true;
                                out.guardian_hp = std::isfinite(hp) ? hp : 0;
                                out.guardian_hp_max = AttrF(m, a, AttrHpMax);
                                found = true;
                                break;
                            }
                        }
                }
                i += n;
                if (found)
                    break;
            }
        }
    }
    // bounties and other server quests
    out.quests_valid = false;
    out.quests.clear();
    {
        const u64 c = m.Ptr(m.Ptr(sigma_global) + SigmaSGames);
        char name[8] = {};
        const u64 sg = PlausibleHeap(c) && m.Read(c, name, 7) && std::memcmp(name, "SGames", 7) == 0
                           ? m.Ptr(c + SGamesFirst) : 0;
        const u64 ql = PlausibleHeap(sg) ? m.Ptr(sg + SGameQuests) : 0;
        u32 hdr[18] = {};
        if (PlausibleHeap(ql) && m.Read(ql, hdr, sizeof hdr) && hdr[0] == QuestRecSize &&
            hdr[10] == 0x600DF00Du && hdr[16] <= 256) {
            u64 n = u64(hdr[12]) | u64(hdr[13]) << 32;
            for (u32 k = 0; n && k < hdr[16]; ++k) {
                u32 r[8];
                if (!PlausibleHeap(n) || !m.Read(n, r, sizeof r))
                    break;
                out.quests.push_back({r[0], r[3], r[6]});
                n = m.Ptr(n + QuestNext);
            }
            out.quests_valid = out.quests.size() == hdr[16];
        }
    }
    out.scenes.clear();
    out.explored_hash = 0;
    out.explored_cells = 0;
    const u64 mm = m.Ptr(minimap_literal);
    if (!PlausibleHeap(mm))
        return;
    u64 e = m.Ptr(mm + 0x20);
    u64 h = dsmod_sdk::Fnv1a64Basis;
    for (int guard = 0; e && guard < 4096; ++guard) {
        u8 r[0x58];
        if (!m.Read(e, r, sizeof r))
            break;
        SceneMask s;
        u32 wid = 0;
        u64 mask = 0, next = 0;
        std::memcpy(&s.scene_sno, r + 0, 4);
        std::memcpy(&wid, r + 8, 4);
        std::memcpy(&s.tex_sno, r + 0xC, 4);
        std::memcpy(&s.x0, r + 0x10, 16);
        std::memcpy(&mask, r + 0x28, 8);
        std::memcpy(&s.w, r + 0x30, 4);
        std::memcpy(&s.h, r + 0x34, 4);
        std::memcpy(&next, r + 0x50, 8);
        e = next;
        if (wid != out.world || !(s.x1 > s.x0) || !(s.y1 > s.y0) || s.x1 - s.x0 > 4096 ||
            s.y1 - s.y0 > 4096 || s.w == 0 || s.h == 0 || u64{s.w} * s.h > 4096)
            continue;
        s.tracked = mask != 0;
        if (s.tracked) {
            s.mask.resize((u64{s.w} * s.h + 3) / 4);
            if (!m.Read(mask, s.mask.data(), s.mask.size()))
                continue;
            for (u8 b : s.mask)
                for (int k = 0; k < 4; ++k)
                    out.explored_cells += ((b >> (6 - 2 * k)) & 3) == 1;
            h = dsmod_sdk::Fnv1a64(s.mask.data(), s.mask.size(), h);
        }
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&s.scene_sno), 4, h);
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&s.x0), 16, h);
        out.scenes.push_back(std::move(s));
    }
    // markers: hash maps {u32 mask, u32 count, bucket pointer +0x10, bucket count +0x18};
    // buckets can move out of inline storage as the map grows. Node {next +0,
    // key +8, x/y/z +0xC, world +0x18, marker texture SNO +0x1C}
    out.markers.clear();
    auto explored_at = [&](float x, float y) {
        for (const auto& s : out.scenes) {
            if (x < s.x0 || y < s.y0 || x >= s.x1 || y >= s.y1)
                continue;
            if (!s.tracked)
                return true; // towns: the game keeps no fog and shows the whole map
            const u32 ix = std::min(s.w - 1, u32((x - s.x0) / ((s.x1 - s.x0) / s.w)));
            const u32 iy = std::min(s.h - 1, u32((y - s.y0) / ((s.y1 - s.y0) / s.h)));
            const u32 i = ix * s.h + iy;
            return ((s.mask[i >> 2] >> (6 - 2 * (i & 3))) & 3) == 1;
        }
        return false;
    };
    // Native actor markers are a third, separate list (main+0x3E8B30), not entries
    // in the static/dynamic texture tables below. Never scan unrelated client actors.
    u8 minimap_flags = 0;
    const bool flags_valid = m.Get(mm + 0x98, minimap_flags);
    const u64 actor_list = flags_valid && !(minimap_flags & 2) ? m.Ptr(mm) : 0;
    enum Exclusion { BadNode, BadAnn, BadRead, Location, World, Pet, Fog, Inactive,
                     BadRenderActor, Disabled, BadMarkerSet, BadDistance, ExclusionCount };
    u32 excluded[ExclusionCount] = {};
    u32 first_ann = 0xFFFFFFFFu;
    u64 first_acd = 0;
    std::unordered_set<u64> visited;
    for (u64 n = PlausibleHeap(actor_list) ? m.Ptr(actor_list) : 0;
         n && visited.size() < 512 && out.actor_markers.size() < 256;) {
        u32 ann = 0;
        u64 next = 0;
        if (!PlausibleHeap(n) || !visited.insert(n).second || !m.Get(n, ann) ||
            !m.Get(n + 0x10, next)) {
            ++excluded[BadNode];
            break;
        }
        n = next;
        const u64 a = AcdByAnn(m, g, ann);
        if (visited.size() == 1) {
            first_ann = ann;
            first_acd = a;
        }
        u32 location = 0, world = 0, flags = 0, rid = 0xFFFFFFFFu;
        u16 state = 0;
        Snapshot::ActorMarker mk;
        mk.ann = ann;
        if (!a) {
            ++excluded[BadAnn];
            continue;
        }
        if (!m.Get(a + 0x48, location) || !m.Get(a + AcdWorld, world) ||
            !m.Get(a + 0xC, flags) || !m.Get(a + 0x160, state) ||
            !m.Get(a + 0x14, rid) || !m.Get(a + 0x18, mk.actor_sno) ||
            !m.Get(a + 0x148, mk.gizmo) || !m.Get(a + 0x14C, mk.type) ||
            !m.Get(a + AcdPos, mk.x) || !m.Get(a + AcdPos + 4, mk.y) ||
            !std::isfinite(mk.x) || !std::isfinite(mk.y)) {
            ++excluded[BadRead];
            continue;
        }
        if (location != 0) {
            ++excluded[Location];
            continue;
        }
        if (world != out.world) {
            ++excluded[World];
            continue;
        }
        if (((state >> 3) & 7) != 7) { // native signed pet-owner field must be -1
            ++excluded[Pet];
            continue;
        }
        if (!explored_at(mk.x, mk.y)) {
            ++excluded[Fog];
            continue;
        }
        mk.quest_monster = AttrI(m, a, 0x1C4, 0) != 0;
        mk.hostile = (flags & 0x80) != 0;
        u32 conversation = 0xFFFFFFFFu;
        AttrRaw(m, a, 0x1EB, static_cast<u32>(idx), conversation);
        mk.conversation_icon = static_cast<s32>(conversation);
        if (!AttrI(m, a, 0x44F, 0) && !mk.quest_monster && !mk.hostile &&
            mk.conversation_icon < 1) {
            ++excluded[Inactive];
            continue;
        }
        const u64 ra = RActorById(m, g, rid);
        u8 actor_flags = 0;
        if (!ra || !m.Get(ra + 0x1CC, actor_flags)) {
            ++excluded[BadRenderActor];
            continue;
        }
        // main+0x3E8E30..50: disabled render actors/disabled ACDs are visible only
        // for the game's explicit gizmo family (main+0x4D4390), never all dead actors.
        if ((actor_flags & 4) || (state & 0x80)) {
            const bool special_gizmo = mk.gizmo == 2 || mk.gizmo == 11 || mk.gizmo == 33 ||
                                       mk.gizmo == 66 || mk.gizmo == 69 || mk.gizmo == 70 ||
                                       mk.gizmo == 75;
            if (!special_gizmo) {
                ++excluded[Disabled];
                continue;
            }
        }
        s16 marker_index = -1;
        if (!m.Get(ra + 0x3AC, mk.marker_set_sno) || !m.Get(ra + 0x3B0, marker_index)) {
            ++excluded[BadMarkerSet];
            continue;
        }
        mk.marker_set_index = marker_index;
        u32 override_tex = 0xFFFFFFFFu;
        AttrRaw(m, a, 0x450, NoParam, override_tex);
        mk.override_tex = override_tex;
        const float dx = mk.x - out.x, dy = mk.y - out.y;
        mk.distance_sq = dx * dx + dy * dy;
        if (std::isfinite(mk.distance_sq))
            out.actor_markers.push_back(mk);
        else
            ++excluded[BadDistance];
    }
    if (std::getenv("D3_SKILL_TRACE") && host.log) {
        char line[768];
        std::snprintf(line, sizeof line,
                      "D3 actor candidates: world=%u mm=%llx flags=%d list=%llx visited=%zu kept=%zu "
                      "first-ann=%08x first-acd=%llx excluded(node,ann,read,location,world,pet,fog,"
                      "inactive,ractor,disabled,marker,distance)=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                      out.world, static_cast<unsigned long long>(mm),
                      flags_valid ? int(minimap_flags) : -1,
                      static_cast<unsigned long long>(actor_list), visited.size(), out.actor_markers.size(),
                      first_ann, static_cast<unsigned long long>(first_acd),
                      excluded[BadNode], excluded[BadAnn], excluded[BadRead], excluded[Location],
                      excluded[World], excluded[Pet], excluded[Fog], excluded[Inactive],
                      excluded[BadRenderActor], excluded[Disabled], excluded[BadMarkerSet], excluded[BadDistance]);
        if (actor_trace != line) {
            actor_trace = line;
            host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line);
        }
    }
    for (u64 off : {u64{0x08}, u64{0x10}}) {
        const u64 mp = m.Ptr(mm + off);
        u32 mask = 0, bucket_count = 0;
        u64 buckets = 0;
        if (!PlausibleHeap(mp) || !m.Get(mp, mask) || mask > 0xFFF ||
            !m.Get(mp + 0x10, buckets) || !PlausibleHeap(buckets) ||
            !m.Get(mp + 0x18, bucket_count) || bucket_count != mask + 1)
            continue;
        for (u32 b = 0; b <= mask; ++b) {
            u64 n = m.Ptr(buckets + 8 * u64{b});
            for (int guard = 0; n && guard < 256; ++guard) {
                u8 r[0x20];
                if (!PlausibleHeap(n) || !m.Read(n, r, sizeof r))
                    break;
                Snapshot::Marker mk;
                u32 wid = 0;
                std::memcpy(&mk.x, r + 0xC, 4);
                std::memcpy(&mk.y, r + 0x10, 4);
                std::memcpy(&wid, r + 0x18, 4);
                std::memcpy(&mk.tex_sno, r + 0x1C, 4);
                mk.layer = off == 0x10 ? Snapshot::Layer::Dynamic : Snapshot::Layer::Static;
                std::memcpy(&n, r, 8);
                if (wid == out.world && mk.tex_sno != 0xFFFFFFFFu && std::isfinite(mk.x) &&
                    std::isfinite(mk.y) && explored_at(mk.x, mk.y) && out.markers.size() < 256)
                    out.markers.push_back(mk);
            }
        }
    }
    // The game's map-marker list (main+0x3E92A0 draws it on the minimap and the zone map): one
    // record per marked actor of the level (town NPCs and services, the rift obelisk, ...). The
    // game draws a record only while its actor is NOT a client actor; the actor-marker list above
    // covers those. Without it, icons vanished once their actor left client range although the
    // game's own map still showed them. Root [[[literal] + 8]]; list {head +0, count u32 +0x10}.
    // Record: ANN +0, x/y +8, world +0x14, kind +0x24, flags +0x28, icon {texture SNO +0x2C,
    // atlas frame +0x30}, squared min/max distance +0x3C/+0x40, next +0x78. Native filters
    // (main+0x3E93B0..0x3E9718): icon set; kind 0 or >= 8, not 9 (1..7: players, followers,
    // pets); current world; flags & 0xE clear; squared distance within the set bounds; explored
    // ground when flags & 0x220 == 0x20. Kinds 12..14 swap in a per-actor texture natively, which
    // is not reproduced, so they are left out. A malformed list (bad node, cycle, count mismatch)
    // is dropped whole.
    if (map_markers_literal) {
        const u64 obj = m.Ptr(map_markers_literal);
        const u64 list = PlausibleHeap(obj) ? m.Ptr(obj + 8) : 0;
        u64 n = 0;
        u32 count = 0, seen = 0;
        bool ok = PlausibleHeap(list) && m.Get(list, n) && m.Get(list + 0x10, count) && count <= 4096;
        std::vector<Snapshot::Marker> found;
        std::unordered_set<u64> nodes;
        while (ok && n) {
            u8 r[0x80];
            if (!PlausibleHeap(n) || !nodes.insert(n).second || ++seen > count ||
                !m.Read(n, r, sizeof r)) {
                ok = false;
                break;
            }
            u32 ann = 0, wid = 0, flags = 0, tex = 0, frame = 0;
            s32 kind = 0;
            float pos[2], bounds[2];
            std::memcpy(&ann, r + 0x0, 4);
            std::memcpy(pos, r + 0x8, 8);
            std::memcpy(&wid, r + 0x14, 4);
            std::memcpy(&kind, r + 0x24, 4);
            std::memcpy(&flags, r + 0x28, 4);
            std::memcpy(&tex, r + 0x2C, 4);
            std::memcpy(&frame, r + 0x30, 4);
            std::memcpy(bounds, r + 0x3C, 8);
            std::memcpy(&n, r + 0x78, 8);
            if (tex == 0xFFFFFFFFu || (kind >= 1 && kind <= 7) || kind == 9 ||
                (kind >= 12 && kind <= 14) || wid != out.world || (flags & 0xE) ||
                (frame != 0xFFFFFFFFu && frame > 4095) || !std::isfinite(pos[0]) ||
                !std::isfinite(pos[1]) || !std::isfinite(bounds[0]) || !std::isfinite(bounds[1]))
                continue;
            if (AcdByAnn(m, g, ann)) {
                // a client actor: drawn (or not) by the actor-marker list, which takes the
                // game's icon from here when its own definitions name none
                for (auto& am : out.actor_markers)
                    if (am.ann == ann) {
                        am.list_tex = tex;
                        am.list_frame = frame == 0xFFFFFFFFu ? -1 : static_cast<int>(frame);
                    }
                continue;
            }
            constexpr float Eps = 1.1920929e-7f; // main+0xEB0FB4
            const float dx = pos[0] - out.x, dy = pos[1] - out.y, d2 = dx * dx + dy * dy;
            if ((bounds[0] > Eps && bounds[0] > d2) || (bounds[1] > Eps && bounds[1] < d2))
                continue;
            if ((flags & 0x220) == 0x20 && !explored_at(pos[0], pos[1]))
                continue;
            Snapshot::Marker mk;
            mk.tex_sno = tex, mk.x = pos[0], mk.y = pos[1];
            mk.layer = Snapshot::Layer::Remembered;
            mk.frame = frame == 0xFFFFFFFFu ? -1 : static_cast<int>(frame);
            found.push_back(mk);
        }
        if (ok && seen == count) {
            for (const auto& mk : found)
                if (out.markers.size() < 256)
                    out.markers.push_back(mk);
        } else {
            for (auto& am : out.actor_markers)
                am.list_tex = 0xFFFFFFFFu, am.list_frame = -1;
        }
    }
    std::sort(out.markers.begin(), out.markers.end(), [](const auto& a, const auto& b) {
        return std::tie(a.tex_sno, a.frame, a.x, a.y) < std::tie(b.tex_sno, b.frame, b.x, b.y);
    });
    for (const auto& mk : out.markers) {
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&mk.tex_sno), 4, h);
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&mk.frame), 4, h);
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&mk.x), 4, h);
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(&mk.y), 4, h);
    }
    for (const auto& mk : out.actor_markers) {
        const u32 fields[] = {mk.actor_sno, mk.override_tex, mk.marker_set_sno, mk.list_tex,
                              static_cast<u32>(mk.list_frame),
                              static_cast<u32>(mk.marker_set_index), static_cast<u32>(mk.type),
                              static_cast<u32>(mk.gizmo), static_cast<u32>(mk.conversation_icon),
                              u32(mk.quest_monster), u32(mk.hostile)};
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(fields), sizeof fields, h);
        // position only: the hero's distance changes every step, and the module hashes which
        // radius-gated actors are in range instead (no new picture per step)
        const float pos[] = {mk.x, mk.y};
        h = dsmod_sdk::Fnv1a64(reinterpret_cast<const u8*>(pos), sizeof pos, h);
    }
    out.explored_hash = h;
}

} // namespace d3_reader
