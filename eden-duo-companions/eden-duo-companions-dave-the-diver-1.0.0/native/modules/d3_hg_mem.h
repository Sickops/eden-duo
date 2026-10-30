// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III Hero and Gear tab reader for every build in d3_build.h (plain guest reads only; NCE-safe).
// Offsets below were derived from live memory reads and checked against the native Inventory
// and Character screens.
//
// Roots, all resolved from opcode-checked code pins (plus the host's __relocation_delta):
//   AppGlobals  main+0xB8750  adrp/ldr literal -> CGame G = [AppGlobals+0x10] (as d3_reader)
//   SNO groups  main+0x4C5A4C adrp/ldr literal 0x1154DD8 -> [[lit]] = table of SNO group objects
//               (group +0x20 = definition container, +0x30 = group name); the container items
//               (16 B: u32 index, u32, u64 definition) point at the loaded definitions (SNO id at +0).
//   Attributes  main+0x69AFF4 adrp/add -> descriptor table main+0x191EAD8, 0x40 B per attribute
//               (+0x10 data type, 1 = int; +0x28 name).
// GameBalance definitions keep their tables in memory: Items_* files (+0x28 table, 800 B records:
// +0 name {off,size,ptr}, +0x10 GBID, +0x18 actor SNO, +0x1C item type GBID, +0x84 set GBID),
// ItemTypes (+0x18 table, 96 B records: name, +0x10 GBID), ExperienceTableAlt (+0x48 table, 256 B
// records, u64 +0 = experience from paragon n to n+1). GBID = h33 of the lower-case name.
// Actor definitions: +0x160 u32[7][2] inventory image frame hashes (class, gender).
// Power definitions: tag map at +0x20 {off,size}; tags 0x42100+n = buff n icon frame hash,
// 0x42300+n = buff n shown in the buff holder.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "d3_build.h"

namespace d3hg {
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 NoParam = 0xFFFFF;

struct AttrKV {
    u32 key = 0, raw = 0; // key = attribute id | param << 12
    bool operator==(const AttrKV&) const = default;
};

/// Attribute descriptors read once from the game (names, data types).
struct AttrMeta {
    std::vector<std::string> names; // by id
    std::vector<u8> is_int;
    std::vector<u32> def_raw; // descriptor default value (raw), used when an attribute is absent
    std::unordered_map<std::string, u32> ids;
    u32 Id(const std::string& n) const {
        auto it = ids.find(n);
        return it == ids.end() ? 0xFFFu : it->second;
    }
    double Value(const AttrKV& a) const;
};

/// Value of attribute `name`[param] in a sorted attribute list; `def` when absent.
double Attr(const AttrMeta& m, const std::vector<AttrKV>& v, const std::string& name,
            u32 param = NoParam, double def = 0);
/// As Attr, but an absent attribute takes the game's descriptor default.
double AttrOrDefault(const AttrMeta& m, const std::vector<AttrKV>& v, const std::string& name,
                     u32 param = NoParam);

struct ItemData {
    u32 acd = 0, gbid = 0, actor = 0;
    u64 addr = 0; // the item's own live ACD address (gear_mailbox: the game's own D3_PlaceItem
                  // takes this, not the id); diagnostics only otherwise, never persisted
    int slot = -1, x = 0, y = 0;
    std::string gb_name;   // GameBalance internal name (Items string list label)
    u32 itype = 0, set = 0;
    std::string itype_name; // ItemTypes internal name (ItemTypeNames label)
    // ItemTypes record fields (2.7.7): +0x28 class mask (Barbarian 0x100, Wizard 0x200, Witch Doctor
    // 0x400, Demon Hunter 0x800, Monk 0x2000, Crusader 0x20000, Necromancer 0x100000), +0x2C u32[8]
    // inventory slots (-1 terminated; 1..13 = the hero's equipment slots), +0x4C flags (0x200
    // one-handed weapon, 0x400 two-handed weapon)
    u32 itype_slots = 0;   // bit s: equipment slot s (1..13) is listed
    u32 itype_classes = 0, itype_flags = 0;
    u32 icon = 0;           // inventory frame hash for the hero's class/gender
    // rare name (ACD +0x138 flag, +0x13C affix StringList SNO, +0x140 index in it, +0x144 index in
    // the item's own list; flag 0: affix list = prefix + item suffix list, 1: item prefix list +
    // affix list = suffix). GameBalance +0x98/+0x9C: the item's prefix/suffix StringList SNOs.
    u32 rare_flag = 0, rare_list = 0, rare_a = 0, rare_b = 0, rare_prefix = 0, rare_suffix = 0;
    std::vector<AttrKV> attrs; // sorted by key
    std::vector<ItemData> gems; // socketed into this item
    bool operator==(const ItemData&) const = default;
};

struct BuffData {
    u32 power = 0, icon = 0;
    int slot = 0, stacks = 0, secs = -1; // secs: -1 = no timer
    bool harmful = false;
    bool operator==(const BuffData&) const = default;
};

struct CubePowerData {
    u32 gbid = 0xFFFFFFFF, icon = 0;
    std::string gb_name; // item definition, independent of owned inventory ACDs
    bool operator==(const CubePowerData&) const = default;
};

struct SkillData {
    u32 power = 0xFFFFFFFFu, icon = 0;
    int rune = -1; // native rune A..E = 0..4; -1 = no rune
    bool available = false;
    bool rune_valid = false; // raw field is in range; independent of Power availability
    bool operator==(const SkillData&) const = default;
};

struct PotionData {
    u32 gbid = 0xFFFFFFFFu, icon = 0;
    std::string gb_name, itype_name;
    bool available = false; // guarded current item identity; no readiness/cooldown assertion
    bool operator==(const PotionData&) const = default;
};

struct HeroData {
    bool valid = false;
    std::string name;
    u32 acd = 0;  // the hero's own ACD id (gear_mailbox: the destination container of every
                  // equip/backpack move is the hero's own container, equipped or not)
    u64 addr = 0; // the hero's own live ACD address
    int cls = -1, level = 0, paragon = 0;
    u32 portrait_actor = 0;
    double xp_left = -1, xp_need = 0; // to the next paragon level (level 70) or level; need 0 = unknown
    u64 gold = 0, shards = 0;
    std::vector<AttrKV> attrs; // the hero's attributes, sorted
    std::vector<BuffData> buffs;
    bool cube_valid = false;
    std::array<CubePowerData, 3> cube;
    bool skills_valid = false;
    std::array<SkillData, 6> skills;
    PotionData potion;
    bool operator==(const HeroData&) const = default;
};

class Reader {
public:
    explicit Reader(const EdenDsmodHostApi& host, const d3_build::Build& build);
    /// One bounded slice of a read cycle (at most about `budget_us` of guest reads per call, so the
    /// tick thread is never held long): resolves roots, loads the attribute names, GameBalance
    /// tables and Actor/Power definition maps once, then scans the hero's items. Returns 1 when a
    /// complete snapshot was written to hero/items, 0 while the cycle continues, -1 when no world
    /// (the cycle is reset). Call Restart() to begin the next cycle.
    int Step(const EdenDsmodHostApi& host, HeroData& hero, std::vector<ItemData>& items, double budget_us);
    void Restart() {
        phase = Phase::Start;
    }
    bool Finished() const {
        return phase == Phase::Done;
    }
    std::shared_ptr<const AttrMeta> Meta() const {
        return meta;
    }
    /// CGame* G ([AppGlobals]+0x10) for the gear menu-state reader; 0 until resolved / no game.
    u64 GameRoot(const EdenDsmodHostApi& host);
    double last_us = 0;   // guest-read time of the last completed cycle (sum of its slices)
    double max_step_us = 0; // longest slice of the last completed cycle

private:
    struct Mem;
    bool Resolve(const EdenDsmodHostApi& host);
    PotionData ReadPotion(const EdenDsmodHostApi& host);
    u64 Group(const Mem& m, const char* name);
    void ReadAttrs(const Mem& m, u64 acd, std::vector<AttrKV>& out);
    u32 IconFor(const Mem& m, u32 actor, int cls, bool female);
    struct PowerInfo {
        std::array<u32, 32> icon{};
        u32 round_icon = 0;
        u32 shown = 0;   // bit n: tag 0x42300+n (buff n shown in the buff holder)
        u32 harmful = 0; // bit n: tag 0x42200+n (buff n is a debuff)
    };
    const PowerInfo* Power(const Mem& m, u32 sno);

    const d3_build::Build& build;
    u64 main_base = 0;
    u64 app_literal = 0, sno_literal = 0, attr_table = 0;
    bool resolved = false;
    bool potion_guarded = false;
    bool skills_guarded = false; // optional getter check, never blocks the established readers
    bool skill_trace_seen = false;
    std::array<u32, 24> skill_trace_raw{};
    std::shared_ptr<AttrMeta> meta;
    struct GbItem {
        std::string name;
        u32 actor = 0, itype = 0, set = 0, rare_prefix = 0, rare_suffix = 0;
    };
    std::unordered_map<u32, GbItem> gb_items;
    struct GbItype {
        std::string name;
        u32 slots = 0, classes = 0, flags = 0;
    };
    std::unordered_map<u32, GbItype> gb_itypes;
    std::vector<u64> paragon_xp;
    bool gb_done = false;
    std::unordered_map<u32, u64> actor_defs, power_defs;
    bool actor_done = false, power_done = false;
    std::unordered_map<u64, u32> icon_cache; // (actor << 8 | class*2+female) -> frame hash
    std::unordered_map<u32, PowerInfo> power_cache;

    // ---- resumable cycle state
    enum class Phase { Start, Meta, Gb, Defs, Scan, Hero, Items, Done };
    Phase phase = Phase::Start;
    u64 g = 0, player = 0;
    u32 pid = 0;
    // meta
    std::vector<u8> meta_raw;
    std::shared_ptr<AttrMeta> meta_build;
    u32 meta_i = 0;
    // GameBalance tables: (file, field, stride, kind)
    size_t gb_file = 0;
    u64 gb_rec = 0, gb_ptr = 0, gb_body = 0;
    u32 gb_size = 0;
    std::vector<u8> gb_buf;
    u64 gb_buf_at = ~u64{0};
    std::unordered_map<u32, u64> gb_files; // SNO -> file pointer, rebuilt fresh at each Gb pass
    // definition maps
    int defs_group = 0; // 0 Actor, 1 Power
    u64 defs_arr = 0;
    u32 defs_n = 0, defs_i = 0;
    // ACD scan
    struct Acd {
        u64 addr;
        u32 id, actor, gbid, container, ann;
        int slot, x, y;
    };
    u64 c_blocks = 0;
    u32 c_cap = 0, c_shift = 0, c_max = 0, scan_i = 0;
    std::vector<Acd> acds, potion_candidates;
    u64 hero_acd = 0;
    size_t item_i = 0;
    HeroData work_hero;
    std::vector<ItemData> work_items;
    double cycle_us = 0, cycle_max = 0;
    int cls_index = 0;
    bool female = false;
};

/// h = h * 33 + byte over `s` (StringList label hash; lower-cased it is the GBID).
u32 H33(std::string_view s, bool lower = false);

/// Live re-read of the ACD record at `addr` (the address `ItemData::addr` carries from the last
/// full scan): current item id, gbid, owning container id and slot, straight from the guest right
/// now -- never from a cached/possibly-stale snapshot. False when `addr` no longer maps a live ACD
/// (freed, moved, or simply unmapped). Callers re-verify the exact acd+gbid they captured earlier
/// matches this live read, and that the item is still in the expected container/slot, immediately
/// before issuing a destructive gear action off it (mirrors GearDrive::Step's own Phase::Act guard).
bool ReadLiveItem(const EdenDsmodHostApi& host, u64 addr, u32& acd, u32& gbid, u32& container, int& slot);

} // namespace d3hg
