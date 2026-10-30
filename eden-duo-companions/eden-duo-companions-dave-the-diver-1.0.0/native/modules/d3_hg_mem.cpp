// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_hg_mem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace d3hg {
namespace {
using s32 = std::int32_t;
using s64 = std::int64_t;

// Offset + words of every pin below are per-build (d3_build::Build); only the decode shape here
// (ADRP + LDR/ADD) is shared, since INVENTORY.md does not call that shape itself build-specific.
struct Pin {
    u64 off;
    u32 w0, w1;
};
Pin ToPin(const d3_build::Pin2& p) {
    return {p.off, p.w0, p.w1};
}

constexpr u64 AppGame = 0x10, GPlayers = 0x948, GLocalIndex = 0xBA0, GAcdContainer = 0x9A0;
constexpr u64 GTick = 0x808 + 0x120;
constexpr u64 PlayerStride = 0xEBF8, PlayerBase = 0x60;
constexpr u64 PAcd = 0x4, PName = 0xB210, PPortrait = 0xD678, PClass = 0xD688, PLevel = 0xD68C,
              PParagon = 0xD690, PCurrencies = 0x9D28, PCubePowers = 0x13BC, PSkills = 0x134;
constexpr u64 AcdSize = 0x360, AcdAttrs = 0x168;
constexpr u32 ItemTypesFile = 19755, ParagonXpFile = 252616;

bool Heap(u64 p) {
    return p >= 0x1000000 && p < (u64{1} << 40);
}
} // namespace

u32 H33(std::string_view s, bool lower) {
    u32 h = 0;
    for (char c : s) {
        unsigned char b = static_cast<unsigned char>(c);
        if (lower && b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b + 32);
        h = h * 33 + b;
    }
    return h;
}

bool ReadLiveItem(const EdenDsmodHostApi& host, u64 addr, u32& acd, u32& gbid, u32& container, int& slot) {
    // Same offsets the Scan/Items phases below use: id +0x00, gbid +0x3C, container +0xA8, slot
    // +0xAC (ACD record, see the struct Acd comment). One direct guest read, no cycle state.
    if (!Heap(addr) || !host.is_mapped || !host.read_memory)
        return false;
    u8 r[0xB0];
    if (!host.is_mapped(host.userdata, addr, sizeof r) || !host.read_memory(host.userdata, addr, r, sizeof r))
        return false;
    std::memcpy(&acd, r + 0x00, 4);
    std::memcpy(&gbid, r + 0x3C, 4);
    std::memcpy(&container, r + 0xA8, 4);
    std::memcpy(&slot, r + 0xAC, 4);
    return true;
}

struct Reader::Mem {
    const EdenDsmodHostApi& h;
    bool Read(u64 a, void* out, u64 n) const {
        return a && n && h.is_mapped(h.userdata, a, n) && h.read_memory(h.userdata, a, out, n);
    }
    template <typename T>
    bool Get(u64 a, T& v) const {
        return Read(a, &v, sizeof(T));
    }
    u64 Ptr(u64 a) const {
        u64 v = 0;
        return Get(a, v) ? v : 0;
    }
    std::string Str(u64 a, size_t max) const {
        std::string s(max, '\0');
        if (!Read(a, s.data(), max))
            return {};
        s.resize(strnlen(s.data(), max));
        return s;
    }
};

double AttrMeta::Value(const AttrKV& a) const {
    const u32 id = a.key & 0xFFF;
    if (id < is_int.size() && is_int[id])
        return static_cast<double>(static_cast<s32>(a.raw));
    float f;
    std::memcpy(&f, &a.raw, 4);
    return std::isfinite(f) ? f : 0.0;
}

double Attr(const AttrMeta& m, const std::vector<AttrKV>& v, const std::string& name, u32 param,
            double def) {
    const u32 id = m.Id(name);
    if (id >= 0xFFF)
        return def;
    const u32 key = id | (param << 12);
    auto it = std::lower_bound(v.begin(), v.end(), key, [](const AttrKV& a, u32 k) { return a.key < k; });
    return it != v.end() && it->key == key ? m.Value(*it) : def;
}

double AttrOrDefault(const AttrMeta& m, const std::vector<AttrKV>& v, const std::string& name, u32 param) {
    const u32 id = m.Id(name);
    if (id >= m.def_raw.size())
        return 0;
    return Attr(m, v, name, param, m.Value(AttrKV{id, m.def_raw[id]}));
}

Reader::Reader(const EdenDsmodHostApi& host, const d3_build::Build& build)
    : build{build}, main_base{host.main_base} {}

bool Reader::Resolve(const EdenDsmodHostApi& host) {
    Mem m{host};
    auto pin = [&](const Pin& p, bool add) -> u64 {
        u32 w[2];
        if (!m.Read(main_base + p.off, w, 8) || w[0] != p.w0 || w[1] != p.w1)
            return 0;
        const s64 immhi = (w[0] >> 5) & 0x7FFFF, immlo = (w[0] >> 29) & 3;
        s64 imm = (immhi << 2) | immlo;
        if (imm & (s64{1} << 20))
            imm -= s64{1} << 21;
        const u64 pc = main_base + p.off;
        const u64 page = (pc & ~u64{0xFFF}) + static_cast<u64>(imm << 12);
        const u64 lo = (w[1] >> 10) & 0xFFF;
        return page + (add ? lo : lo * 8);
    };
    const u64 a = pin(ToPin(build.app_globals), false), s = pin(ToPin(build.sno_groups), false),
              t = pin(ToPin(build.attr_table), true);
    if (!a || !s || !t)
        return false;
    std::array<u32, 3> cube_code{};
    if (!m.Read(main_base + build.cube_getter.off, cube_code.data(), sizeof cube_code) ||
        cube_code != build.cube_getter.words)
        return false;
    std::array<u32, 2> skill_code{};
    skills_guarded = m.Read(main_base + build.skill_getter.off, skill_code.data(), sizeof skill_code) &&
        skill_code == build.skill_getter.words;
    std::array<u32, 2> potion_code{};
    std::array<u32, 4> potion_hud{};
    potion_guarded = m.Get(main_base + build.potion_getter.off, potion_code) &&
        potion_code == build.potion_getter.words &&
        m.Get(main_base + build.potion_hud.off, potion_hud) &&
        potion_hud == build.potion_hud.words;
    s64 delta = 0;
    if (host.get_i64)
        delta = host.get_i64(host.userdata, "__relocation_delta", 0);
    app_literal = a + static_cast<u64>(delta);
    sno_literal = s + static_cast<u64>(delta);
    attr_table = t + static_cast<u64>(delta);
    resolved = true;
    return true;
}

u64 Reader::Group(const Mem& m, const char* name) {
    const u64 table = m.Ptr(m.Ptr(sno_literal));
    if (!Heap(table))
        return 0;
    u64 g[80];
    if (!m.Read(table, g, sizeof g))
        return 0;
    for (u64 p : g) {
        if (!Heap(p))
            continue;
        char nm[32] = {};
        if (m.Read(p + 0x30, nm, 31) && std::strcmp(nm, name) == 0)
            return p;
    }
    return 0;
}

u32 Reader::IconFor(const Mem& m, u32 actor, int cls, bool female) {
    const int ci = std::clamp(cls, 0, 6) * 2 + (female ? 1 : 0);
    const u64 key = u64{actor} << 8 | u32(ci);
    if (auto it = icon_cache.find(key); it != icon_cache.end())
        return it->second;
    u32 v[14] = {};
    u32 echo = 0;
    auto it = actor_defs.find(actor);
    if (it == actor_defs.end() || !m.Get(it->second, echo) || echo != actor ||
        !m.Read(it->second + 0x160, v, sizeof v))
        return 0; // not cached: retried on the next read
    u32 icon = v[ci];
    if (!icon || icon == 0xFFFFFFFFu)
        icon = v[ci & ~1];
    for (int i = 0; i < 14 && (!icon || icon == 0xFFFFFFFFu); ++i)
        icon = v[i];
    if (icon == 0xFFFFFFFFu)
        icon = 0;
    icon_cache[key] = icon;
    return icon;
}

const Reader::PowerInfo* Reader::Power(const Mem& m, u32 sno) {
    if (auto it = power_cache.find(sno); it != power_cache.end())
        return &it->second;
    auto it = power_defs.find(sno);
    u32 hdr[2], echo = 0;
    if (it == power_defs.end() || !m.Get(it->second, echo) || echo != sno ||
        !m.Read(it->second + 0x20, hdr, 8) || hdr[1] > 0x20000)
        return nullptr;
    PowerInfo pi;
    u32 normal_icon = 0;
    std::vector<u8> d(hdr[1]);
    if (!d.empty() && m.Read(it->second + hdr[0], d.data(), d.size()))
        for (size_t o = 0; o + 12 <= d.size(); o += 4) {
            u32 t, k, v;
            std::memcpy(&t, &d[o], 4);
            std::memcpy(&k, &d[o + 4], 4);
            std::memcpy(&v, &d[o + 8], 4);
            if (t != 0)
                continue;
            if (k == 0xAF000 && v && v != 0xFFFFFFFFu)
                pi.round_icon = v;
            else if (k == 0x50700 && v && v != 0xFFFFFFFFu)
                normal_icon = v;
            else if (k >= 0x42100 && k < 0x42120 && v)
                pi.icon[k - 0x42100] = v;
            else if (k >= 0x42300 && k < 0x42320 && v == 1)
                pi.shown |= 1u << (k - 0x42300);
            else if (k >= 0x42200 && k < 0x42220 && v == 1)
                pi.harmful |= 1u << (k - 0x42200);
        }
    if (!pi.round_icon)
        pi.round_icon = normal_icon;
    return &(power_cache[sno] = pi);
}


namespace {
struct GbTable {
    u32 file;
    u64 field, stride;
    int kind; // 0 items, 1 item types, 2 paragon XP
};
constexpr GbTable GbTables[] = {
    {19750, 0x28, 800, 0}, {19752, 0x28, 800, 0}, {19753, 0x28, 800, 0}, {19754, 0x28, 800, 0},
    {170627, 0x28, 800, 0}, {1189, 0x28, 800, 0}, {130867, 0x28, 800, 0}, {245193, 0x28, 800, 0},
    {197880, 0x28, 800, 0}, {361240, 0x28, 800, 0}, {ItemTypesFile, 0x18, 96, 1}, {ParagonXpFile, 0x48, 256, 2}};
using Clock = std::chrono::steady_clock;
} // namespace

u64 Reader::GameRoot(const EdenDsmodHostApi& host) {
    Mem m{host};
    if (!resolved && !Resolve(host))
        return 0;
    const u64 app = m.Ptr(app_literal);
    const u64 gg = Heap(app) ? m.Ptr(app + AppGame) : 0;
    return Heap(gg) ? gg : 0;
}

int Reader::Step(const EdenDsmodHostApi& host, HeroData& hero, std::vector<ItemData>& items, double budget_us) {
    const auto t0 = Clock::now();
    const auto deadline = t0 + std::chrono::microseconds(static_cast<long long>(budget_us));
    auto over = [&] { return Clock::now() >= deadline; };
    Mem m{host};
    auto finish = [&](int r) {
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        cycle_us += us;
        cycle_max = std::max(cycle_max, us);
        if (r == 1) {
            last_us = cycle_us;
            max_step_us = cycle_max;
        }
        if (r != 0)
            cycle_us = cycle_max = 0;
        return r;
    };
    if (!resolved && !Resolve(host))
        return finish(-1);
    // world / hero identity, checked on every slice (a change restarts the cycle)
    const u64 app = m.Ptr(app_literal);
    const u64 gg = Heap(app) ? m.Ptr(app + AppGame) : 0;
    s32 idx = -1;
    if (!Heap(gg) || !m.Get(m.Ptr(gg + GLocalIndex), idx) || idx < 0 || idx > 3) {
        phase = Phase::Start;
        return finish(-1);
    }
    const u64 players = m.Ptr(gg + GPlayers);
    const u64 p = Heap(players) ? players + u64(idx) * PlayerStride + PlayerBase : 0;
    u32 id = 0xFFFFFFFFu;
    if (!p || !m.Get(p + PAcd, id) || id == 0xFFFFFFFFu) {
        phase = Phase::Start;
        return finish(-1);
    }
    if (phase != Phase::Start && (gg != g || id != pid))
        phase = Phase::Start;
    for (;;) {
        switch (phase) {
        case Phase::Start:
            g = gg, player = p, pid = id;
            phase = !meta ? Phase::Meta : !gb_done ? Phase::Gb : (!actor_done || !power_done) ? Phase::Defs : Phase::Scan;
            if (phase == Phase::Meta) {
                meta_raw.assign(0x5D0 * 0x40, 0);
                if (!m.Read(attr_table, meta_raw.data(), meta_raw.size()))
                    return finish(-1);
                meta_build = std::make_shared<AttrMeta>();
                meta_build->names.resize(0x5D0);
                meta_build->is_int.resize(0x5D0);
                meta_build->def_raw.resize(0x5D0);
                meta_i = 0;
            }
            gb_file = 0, gb_rec = 0, gb_buf_at = ~u64{0};
            defs_group = 0, defs_arr = 0, defs_i = 0;
            if (phase == Phase::Scan)
                continue;
            break;
        case Phase::Meta: {
            for (; meta_i < 0x5D0; ++meta_i) {
                if (over())
                    return finish(0);
                const u8* d = &meta_raw[meta_i * 0x40];
                u32 aid, type;
                u64 name;
                std::memcpy(&aid, d, 4);
                std::memcpy(&type, d + 0x10, 4);
                std::memcpy(&name, d + 0x28, 8);
                if (aid != meta_i)
                    break;
                meta_build->is_int[meta_i] = type == 1;
                std::memcpy(&meta_build->def_raw[meta_i], d + 4, 4);
                meta_build->names[meta_i] = m.Str(name, 64);
                if (!meta_build->names[meta_i].empty())
                    meta_build->ids[meta_build->names[meta_i]] = meta_i;
            }
            if (meta_build->ids.size() < 1000 || meta_build->Id("Buff_Icon_Count0") == 0xFFF)
                return finish(-1);
            meta = meta_build;
            meta_raw.clear();
            phase = Phase::Start;
            continue;
        }
        case Phase::Gb: {
            if (gb_file == 0 && gb_rec == 0 && gb_buf_at == ~u64{0}) {
                gb_files.clear();
                const u64 grp = Group(m, "GameBalance");
                const u64 c = grp ? m.Ptr(grp + 0x20) : 0;
                u32 hdr[4];
                if (!Heap(c) || !m.Read(c + 0x100, hdr, 16) || hdr[1] != 0x10 || hdr[2] >= hdr[0] || hdr[0] > 0x1000)
                    return finish(-1);
                std::vector<u8> raw(u64{hdr[2] + 1} * 0x10);
                if (!m.Read(m.Ptr(c + 0x120), raw.data(), raw.size()))
                    return finish(-1);
                for (u32 k = 0; k <= hdr[2]; ++k) {
                    u64 dp;
                    std::memcpy(&dp, &raw[k * 0x10 + 8], 8);
                    u32 sno = 0;
                    if (Heap(dp) && m.Get(dp, sno))
                        gb_files[sno] = dp;
                }
                gb_items.clear(), gb_itypes.clear(), paragon_xp.clear();
                gb_buf_at = 0;
                gb_body = 0;
            }
            for (; gb_file < std::size(GbTables); ++gb_file, gb_rec = 0, gb_body = 0) {
                const GbTable& tb = GbTables[gb_file];
                if (!gb_body) {
                    auto it = gb_files.find(tb.file);
                    u32 os[2];
                    if (it == gb_files.end() || !m.Read(it->second + tb.field, os, 8))
                        continue;
                    gb_body = it->second;
                    gb_ptr = m.Ptr(gb_body + tb.field + 8);
                    gb_size = os[1] <= 0x400000 && Heap(gb_ptr) ? os[1] : 0;
                }
                for (; gb_rec + tb.stride <= gb_size; gb_rec += tb.stride) {
                    if (over())
                        return finish(0);
                    u8 r[800];
                    if (!m.Read(gb_ptr + gb_rec, r, tb.stride))
                        continue;
                    u32 sz = 0, gid = 0;
                    u64 np = 0;
                    std::memcpy(&sz, r + 4, 4);
                    std::memcpy(&np, r + 8, 8);
                    std::memcpy(&gid, r + 0x10, 4);
                    if (tb.kind == 2) {
                        u64 v;
                        std::memcpy(&v, r, 8);
                        paragon_xp.push_back(v);
                        continue;
                    }
                    std::string n = sz && sz < 256 ? m.Str(np, sz) : std::string{};
                    if (n.empty() || H33(n, true) != gid)
                        continue;
                    if (tb.kind == 1) {
                        GbItype ti;
                        ti.name = std::move(n);
                        std::memcpy(&ti.classes, r + 0x28, 4);
                        std::memcpy(&ti.flags, r + 0x4C, 4);
                        for (int k = 0; k < 8; ++k) {
                            u32 sl;
                            std::memcpy(&sl, r + 0x2C + 4 * k, 4);
                            if (sl == 0xFFFFFFFFu)
                                break;
                            if (sl >= 1 && sl <= 13)
                                ti.slots |= 1u << sl;
                        }
                        gb_itypes[gid] = std::move(ti);
                        continue;
                    }
                    GbItem gi;
                    gi.name = std::move(n);
                    std::memcpy(&gi.actor, r + 0x18, 4);
                    std::memcpy(&gi.itype, r + 0x1C, 4);
                    std::memcpy(&gi.set, r + 0x84, 4);
                    std::memcpy(&gi.rare_prefix, r + 0x98, 4);
                    std::memcpy(&gi.rare_suffix, r + 0x9C, 4);
                    if (gi.set == 0xFFFFFFFFu)
                        gi.set = 0; // not a set item
                    gb_items[gid] = std::move(gi);
                }
            }
            gb_files.clear();
            if (gb_items.size() < 1000)
                return finish(-1);
            gb_done = true;
            phase = Phase::Start;
            continue;
        }
        case Phase::Defs: {
            for (; defs_group < 2; ++defs_group, defs_arr = 0, defs_i = 0) {
                auto& out = defs_group == 0 ? actor_defs : power_defs;
                bool& done = defs_group == 0 ? actor_done : power_done;
                if (done)
                    continue;
                if (!defs_arr) {
                    const u64 grp = Group(m, defs_group == 0 ? "Actor" : "Power");
                    const u64 c = grp ? m.Ptr(grp + 0x20) : 0;
                    u32 hdr[4];
                    if (!Heap(c) || !m.Read(c + 0x100, hdr, 16) || hdr[1] != 0x10 || hdr[2] >= hdr[0] || hdr[0] > 0x10000)
                        return finish(-1);
                    defs_arr = m.Ptr(c + 0x120);
                    defs_n = hdr[2] + 1;
                    out.clear();
                }
                while (defs_i < defs_n) {
                    if (over())
                        return finish(0);
                    u8 raw[64 * 0x10];
                    const u32 n = std::min<u32>(64, defs_n - defs_i);
                    if (m.Read(defs_arr + u64(defs_i) * 0x10, raw, n * 0x10))
                        for (u32 k = 0; k < n; ++k) {
                            u64 dp;
                            std::memcpy(&dp, &raw[k * 0x10 + 8], 8);
                            u32 sno = 0;
                            if (Heap(dp) && m.Get(dp, sno))
                                out[sno] = dp;
                        }
                    defs_i += n;
                }
                done = true;
            }
            phase = Phase::Start;
            continue;
        }
        case Phase::Scan: {
            if (scan_i == 0) {
                const u64 c = m.Ptr(m.Ptr(g + GAcdContainer));
                char cname[16] = {};
                if (!Heap(c) || !m.Read(c, cname, 16) || std::memcmp(cname, "ActorCommonData", 16) != 0 ||
                    !m.Get(c + 0x100, c_cap) || !m.Get(c + 0x108, c_max) || !m.Get(c + 0x168, c_shift) ||
                    !m.Get(c + 0x120, c_blocks) || c_shift > 12 || c_max >= c_cap)
                    return finish(-1);
                acds.clear();
                hero_acd = 0;
            }
            const u32 per = 1u << c_shift;
            std::vector<u8> blk;
            while (scan_i <= c_max) {
                if (over())
                    return finish(0);
                const u64 base = m.Ptr(c_blocks + 8 * u64(scan_i >> c_shift));
                const u32 in = scan_i & (per - 1);
                const u32 n = std::min({64u, per - in, c_max + 1 - scan_i});
                if (Heap(base)) {
                    blk.resize(u64{n} * AcdSize);
                    if (m.Read(base + u64{in} * AcdSize, blk.data(), blk.size()))
                        for (u32 i = 0; i < n; ++i) {
                            const u8* r = &blk[u64{i} * AcdSize];
                            Acd a{base + u64(in + i) * AcdSize};
                            u32 gbtype;
                            std::memcpy(&a.id, r, 4);
                            std::memcpy(&gbtype, r + 0x38, 4);
                            if (a.id == 0xFFFFFFFFu || gbtype != 2)
                                continue;
                            std::memcpy(&a.ann, r + 0x10, 4);
                            std::memcpy(&a.actor, r + 0x18, 4);
                            std::memcpy(&a.gbid, r + 0x3C, 4);
                            std::memcpy(&a.container, r + 0xA8, 4);
                            std::memcpy(&a.slot, r + 0xAC, 4);
                            std::memcpy(&a.x, r + 0xB0, 4);
                            std::memcpy(&a.y, r + 0xB4, 4);
                            acds.push_back(a);
                        }
                }
                scan_i += n;
            }
            scan_i = 0;
            // the hero's ACD by index (id & 0xFFFF), echo-checked, as the game's inline lookup
            const u32 i = pid & 0xFFFF;
            const u64 base = i < c_cap ? m.Ptr(c_blocks + 8 * u64(i >> c_shift)) : 0;
            u32 echo = 0;
            if (Heap(base) && m.Get(base + u64(i & (per - 1)) * AcdSize, echo) && echo == pid)
                hero_acd = base + u64(i & (per - 1)) * AcdSize;
            if (!hero_acd)
                return finish(-1);
            // Keep owned ANN candidates independently: the selected legendary potion can be in
            // the backpack, while slot16 is only the base potion's storage slot.
            potion_candidates.clear();
            for (const auto& a : acds)
                if (a.container == pid) potion_candidates.push_back(a);
            // keep only the hero's items and gems inside them
            std::vector<Acd> mine;
            for (const auto& a : acds)
                if (a.container == pid && (a.slot == 0 || (a.slot >= 1 && a.slot <= 13)))
                    mine.push_back(a);
            const size_t owned = mine.size();
            for (const auto& a : acds)
                for (size_t k = 0; k < owned; ++k)
                    if (a.container == mine[k].id) {
                        mine.push_back(a);
                        break;
                    }
            acds = std::move(mine);
            phase = Phase::Hero;
            continue;
        }
        case Phase::Hero: {
            if (over())
                return finish(0);
            HeroData& hd = work_hero;
            hd = {};
            hd.acd = pid, hd.addr = hero_acd;
            hd.name = m.Str(player + PName, 49);
            m.Get(player + PClass, hd.cls);
            m.Get(player + PLevel, hd.level);
            m.Get(player + PParagon, hd.paragon);
            m.Get(player + PPortrait, hd.portrait_actor);
            u64 cur[16 * 3];
            if (m.Read(player + PCurrencies, cur, sizeof cur))
                for (int k = 0; k < 16; ++k) {
                    if (cur[k * 3] == 0)
                        hd.gold = cur[k * 3 + 1];
                    else if (cur[k * 3] == 1)
                        hd.shards = cur[k * 3 + 1];
                }
            ReadAttrs(m, hero_acd, hd.attrs);
            const AttrMeta& mt = *meta;
            {
                const double lo = Attr(mt, hd.attrs, "Alt_Experience_Next_Lo"), hi = Attr(mt, hd.attrs, "Alt_Experience_Next_Hi");
                const double llo = Attr(mt, hd.attrs, "Experience_Next_Lo"), lhi = Attr(mt, hd.attrs, "Experience_Next_Hi");
                const int cap_level = static_cast<int>(Attr(mt, hd.attrs, "Level_Cap", NoParam, 70));
                if (hd.level >= cap_level) {
                    hd.xp_left = static_cast<double>(static_cast<u32>(lo)) + static_cast<double>(static_cast<u32>(hi)) * 4294967296.0;
                    if (hd.paragon >= 0 && size_t(hd.paragon) < paragon_xp.size())
                        hd.xp_need = static_cast<double>(paragon_xp[hd.paragon]);
                } else {
                    hd.xp_left = static_cast<double>(static_cast<u32>(llo)) + static_cast<double>(static_cast<u32>(lhi)) * 4294967296.0;
                }
            }
            {
                u32 tick = 0;
                m.Get(g + GTick, tick);
                const u32 c0 = mt.Id("Buff_Icon_Count0"), e0 = mt.Id("Buff_Icon_End_Tick0");
                for (const auto& kv : hd.attrs) {
                    const u32 aid = kv.key & 0xFFF, param = kv.key >> 12;
                    if (aid < c0 || aid >= c0 + 32 || param == NoParam || static_cast<int>(kv.raw) <= 0)
                        continue;
                    const int slot = static_cast<int>(aid - c0);
                    const PowerInfo* pi = Power(m, param);
                    if (!pi || !(pi->shown >> slot & 1) || !pi->icon[slot])
                        continue;
                    // an equipped passive (Trait[power]) keeps standing beneficial buffs off the buff
                    // holder (native Character Details in town lists none for the Leech/Frailty passive
                    // managers); its debuffs (e.g. a cheat-death cooldown) are shown. Town-checked only.
                    const bool harmful = pi->harmful >> slot & 1;
                    if (!harmful && Attr(mt, hd.attrs, "Trait", param) > 0)
                        continue;
                    BuffData b;
                    b.power = param, b.slot = slot, b.stacks = static_cast<int>(kv.raw);
                    b.icon = pi->icon[slot], b.harmful = harmful;
                    const double end = Attr(mt, hd.attrs, mt.names[e0 + slot], param, -1);
                    if (end > 0 && end >= tick)
                        b.secs = static_cast<int>((end - tick + 59) / 60);
                    hd.buffs.push_back(b);
                }
                std::sort(hd.buffs.begin(), hd.buffs.end(), [](const BuffData& a, const BuffData& b) {
                    return a.power != b.power ? a.power < b.power : a.slot < b.slot;
                });
            }
            switch (hd.portrait_actor) {
            case 3285: case 3301: cls_index = 0; break;
            case 74706: case 75207: cls_index = 1; break;
            case 4717: case 4721: cls_index = 2; break;
            case 6481: case 6485: cls_index = 3; break;
            case 6526: case 6544: cls_index = 4; break;
            case 238284: case 238286: cls_index = 5; break;
            case 454021: case 454402: cls_index = 6; break;
            default: cls_index = 0; break;
            }
            female = hd.portrait_actor == 3285 || hd.portrait_actor == 4717 || hd.portrait_actor == 6481 ||
                     hd.portrait_actor == 6526 || hd.portrait_actor == 74706 || hd.portrait_actor == 238286 ||
                     hd.portrait_actor == 454402;
            // Current equipped item GBIDs. +0x9520 can retain another hero's values; +0xBA50
            // belongs to indexed loadout records. Neither is a fallback for this array.
            std::array<u32, 3> powers{};
            hd.cube_valid = m.Read(player + PCubePowers, powers.data(), sizeof powers);
            if (hd.cube_valid)
                for (size_t k = 0; k < powers.size(); ++k) {
                    auto& power = hd.cube[k];
                    power.gbid = powers[k];
                    if (power.gbid == 0xFFFFFFFF)
                        continue;
                    if (auto gi = gb_items.find(power.gbid); gi != gb_items.end()) {
                        power.gb_name = gi->second.name;
                        power.icon = IconFor(m, gi->second.actor, cls_index, female);
                    }
                }
            // Native equipped skill getter returns Player+0x134+slot*0x10. Keep this optional
            // feature unavailable if its opcode guard fails; established Hero/Gear/Cube remain
            // usable. The raw record's other fields are retained only in private trace output.
            std::array<u32, 24> skill_raw{};
            hd.skills_valid = skills_guarded && m.Read(player + PSkills, skill_raw.data(), sizeof skill_raw);
            if (hd.skills_valid)
                for (size_t k = 0; k < hd.skills.size(); ++k) {
                    SkillData& skill = hd.skills[k];
                    skill.power = skill_raw[k * 4];
                    s32 rune = -1;
                    std::memcpy(&rune, &skill_raw[k * 4 + 1], sizeof rune);
                    skill.rune_valid = rune >= -1 && rune <= 4;
                    skill.rune = skill.rune_valid ? rune : -1;
                    if (!skill.power || skill.power == 0xFFFFFFFFu) {
                        skill.available = true; // explicit empty assignment
                    } else if (const PowerInfo* pi = Power(m, skill.power)) {
                        skill.available = true;
                        skill.icon = pi->round_icon;
                    }
                }
            if (std::getenv("D3_SKILL_TRACE") && host.log &&
                (!skill_trace_seen || skill_raw != skill_trace_raw)) {
                char line[256];
                std::snprintf(line, sizeof line, "D3 skills: getter=%d read=%d player=%llx", skills_guarded,
                              hd.skills_valid, static_cast<unsigned long long>(player));
                host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line);
                if (hd.skills_valid)
                    for (size_t k = 0; k < hd.skills.size(); ++k) {
                        const SkillData& skill = hd.skills[k];
                        std::snprintf(line, sizeof line,
                                      "D3 skill[%zu]: raw=%08x %08x %08x %08x available=%d icon=%08x rune=%d rune_range=%d",
                                      k, skill_raw[k * 4], skill_raw[k * 4 + 1], skill_raw[k * 4 + 2], skill_raw[k * 4 + 3],
                                      skill.available, skill.icon, skill.rune, skill.rune_valid);
                        host.log(host.userdata, EDEN_DSMOD_LOG_INFO, line);
                    }
                skill_trace_seen = true;
                skill_trace_raw = skill_raw;
            }
            hd.potion = ReadPotion(host);
            work_items.clear();
            item_i = 0;
            phase = Phase::Items;
            continue;
        }
        case Phase::Items: {
            for (; item_i < acds.size(); ++item_i) {
                if (over())
                    return finish(0);
                const Acd& a = acds[item_i];
                u8 r[0x148];
                u32 echo = 0;
                if (!m.Read(a.addr, r, sizeof r) || (std::memcpy(&echo, r, 4), echo != a.id))
                    continue; // moved since the scan: picked up by the next cycle
                ItemData it;
                it.acd = a.id, it.addr = a.addr, it.gbid = a.gbid, it.actor = a.actor, it.slot = a.slot,
                it.x = a.x, it.y = a.y;
                std::memcpy(&it.rare_flag, r + 0x138, 4);
                std::memcpy(&it.rare_list, r + 0x13C, 4);
                std::memcpy(&it.rare_a, r + 0x140, 4);
                std::memcpy(&it.rare_b, r + 0x144, 4);
                if (auto gi = gb_items.find(a.gbid); gi != gb_items.end()) {
                    it.gb_name = gi->second.name;
                    it.itype = gi->second.itype;
                    it.set = gi->second.set;
                    it.rare_prefix = gi->second.rare_prefix;
                    it.rare_suffix = gi->second.rare_suffix;
                    if (auto ti = gb_itypes.find(it.itype); ti != gb_itypes.end()) {
                        it.itype_name = ti->second.name;
                        it.itype_slots = ti->second.slots;
                        it.itype_classes = ti->second.classes;
                        it.itype_flags = ti->second.flags;
                    }
                }
                it.icon = IconFor(m, a.actor, cls_index, female);
                ReadAttrs(m, a.addr, it.attrs);
                if (a.container == pid) {
                    work_items.push_back(std::move(it));
                } else {
                    for (auto& owner : work_items)
                        if (owner.acd == a.container)
                            owner.gems.push_back(std::move(it));
                }
            }
            std::sort(work_items.begin(), work_items.end(), [](const ItemData& x, const ItemData& y) {
                return std::tie(x.slot, x.y, x.x, x.acd) < std::tie(y.slot, y.y, y.x, y.acd);
            });
            for (auto& it : work_items)
                std::sort(it.gems.begin(), it.gems.end(), [](const ItemData& x, const ItemData& y) {
                    return std::tie(x.x, x.y, x.acd) < std::tie(y.x, y.y, y.acd);
                });
            work_hero.valid = true;
            hero = std::move(work_hero);
            items = std::move(work_items);
            work_hero = {};
            work_items.clear();
            phase = Phase::Done;
            return finish(1);
        }
        case Phase::Done:
            return finish(0);
        }
    }
}

PotionData Reader::ReadPotion(const EdenDsmodHostApi& host) {
    PotionData out;
    Mem m{host};
    std::array<u32, 4> record{};
    if (!potion_guarded || !m.Get(player + 0x194, record) || record[0] != 0xFFFFFFFFu)
        return out; // the native potion setter accepts an item record, never a skill power
    u32 gbid = record[2], actor = 0;
    if (record[3] != 0xFFFFFFFFu) {
        for (const auto& a : potion_candidates) {
            if (a.ann != record[3]) continue;
            // Recheck full identity and ownership after the resumable scan. A moved/reused ACD
            // must not supply a different item's title or artwork.
            std::array<u8, 0xAC> raw{};
            auto word = [&](size_t offset) { u32 v; std::memcpy(&v, raw.data() + offset, 4); return v; };
            if (!m.Get(a.addr, raw) || word(0) != a.id || word(0x10) != record[3] ||
                word(0x38) != 2 || word(0xA8) != pid) continue;
            gbid = word(0x3C); actor = word(0x18);
            break;
        }
    }
    if (!gbid || gbid == 0xFFFFFFFFu) return out;
    const auto gi = gb_items.find(gbid);
    if (gi == gb_items.end()) return out;
    out.gbid = gbid; out.gb_name = gi->second.name;
    if (const auto ti = gb_itypes.find(gi->second.itype); ti != gb_itypes.end())
        out.itype_name = ti->second.name;
    out.icon = IconFor(m, actor ? actor : gi->second.actor, cls_index, female);
    out.available = true;
    return out;
}

void Reader::ReadAttrs(const Mem& m, u64 acd, std::vector<AttrKV>& out) {
    out.clear();
    const u64 grp = m.Ptr(acd + AcdAttrs);
    if (!Heap(grp))
        return;
    u64 map = m.Ptr(grp + 0x10);
    if (!map)
        map = grp + 0x18;
    u32 mask = 0, nb = 0;
    u64 buckets = 0;
    if (!m.Get(map, mask) || !m.Get(map + 0x10, buckets) || !m.Get(map + 0x18, nb) || nb != mask + 1 ||
        nb > 0x4000 || !Heap(buckets))
        return;
    std::vector<u64> bk(nb);
    if (!m.Read(buckets, bk.data(), bk.size() * 8))
        return;
    for (u64 n : bk)
        for (int guard = 0; Heap(n) && guard < 256; ++guard) {
            u8 node[16];
            if (!m.Read(n, node, 16))
                break;
            AttrKV kv;
            std::memcpy(&kv.key, node + 8, 4);
            std::memcpy(&kv.raw, node + 12, 4);
            out.push_back(kv);
            std::memcpy(&n, node, 8);
        }
    std::sort(out.begin(), out.end(), [](const AttrKV& a, const AttrKV& b) { return a.key < b.key; });
}

} // namespace d3hg
