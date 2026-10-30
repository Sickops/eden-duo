// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Hero and Gear tabs: sampling (reader every half second, views rebuilt only on visible change),
// picture jobs (two slots per page: previous below, current on top), module actions (item selection,
// backpack toggle, Details scrolling) and the published values the manifest binds:
//   d3.hero.k0/k1, d3.hero.on, d3.gear.k0/k1, d3.gear.on, d3.gear.bag, d3.gear.cards,
//   d3.perf.hg_read_us, d3.perf.hero_ms, d3.perf.gear_ms;
//   Gear actions: d3.gear.r<row> (AVAILABLE row shown), d3.gear.can_equip / can_unequip / can_drop /
//   confirm. Equip and Unequip call the game's own D3_PlaceItem instantly through the guest
//   load plan mailbox (d3_gear_mailbox.h); an Equip onto an occupied slot runs as two such calls
//   (MbKind::EvictForSwap then Equip, see below) rather than trusting D3_PlaceItem's own internal
//   eviction, which was never itself live-verified through this mailbox. Drop still drives the
//   native menu (d3_gear_drive.h, the drive press gates d3.drv.p<id> and the enforce gate d3.enf);
//   Equip/Unequip refuse outright while a Drop confirm is armed or the drive is running, and vice
//   versa, and all four gear actions share a ~300 ms debounce (owner bug 2026-10-17).

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <variant>

#include "core/mods/dsmod_module_extensions.h"
#include "d3_assets.h"
#include "d3_gear_drive.h"
#include "d3_gear_mailbox.h"
#include "d3_hg_mem.h"
#include "d3_hg_text.h"
#include "d3_img.h"
#include "d3_page_gear.h"
#include "d3_page_hero.h"
#include "d3_reader.h"

namespace d3hg {

class Tabs {
public:
    explicit Tabs(const EdenDsmodHostApi& h, const d3_build::Build& build) : reader{h, build}, drive{build} {}
    /// Called every sample after the main module's work. `world` = a world is loaded and readable;
    /// `s` = this sample's main reader snapshot (native menu, life) for the gear actions.
    void Sample(const EdenDsmodHostApi& h, bool assets_ready, bool world, const d3_reader::Snapshot& s);
    /// Pictures "hero/<seq>" and "gear/<seq>"; false when the key is not ours.
    bool Load(d3_assets::Assets& a, const EdenDsmodHostApi* h, std::string_view key, d3_img::Image& out);
    /// Module actions: gear_slot <slot>, gear_avail <row>, gear_equipped, gear_scroll <+-rows>,
    /// gear_equip, gear_unequip (both instant, through the guest mailbox), gear_drop (native-menu
    /// drive; first tap arms, second confirms), gear_cancel, gear_cell <y*10+x>, gear_bag,
    /// hero_scroll / hero_effects_scroll <+-rows>. `h` is used only to re-verify (ReadLiveItem) an
    /// item's acd+gbid live, immediately before a destructive gear_equip/gear_unequip mailbox call.
    bool Action(const EdenDsmodHostApi& h, std::string_view name, std::int64_t arg);
    /// Forwards the host's guest extensions (mailbox_address/size, load_u32/64, store_u32/64) to
    /// the Equip/Unequip mailbox; a null or guest-less `ext` leaves it absent (Start() refuses).
    void ConfigureGuest(const EdenDsmodHostExtensions* ext);
    /// A complete, guarded equipped-skill snapshot; false clears out during unavailable states.
    bool Skills(std::array<SkillData, 6>& out, PotionData* potion = nullptr);

private:
    struct HeroJob {
        HeroView v;
    };
    struct GearJob {
        GearView v;
        std::shared_ptr<const AttrMeta> meta;
    };
    using Job = std::variant<HeroJob, GearJob>;
    u64 Add(Job j);
    void Prune();
    /// Re-reads the ACD record at `addr` live right now (ReadLiveItem) and confirms it is still
    /// exactly `acd`+`gbid`, in `container`/`slot` -- the identity recheck Drop already does
    /// immediately before its destructive press (GearDrive::Step's Phase::Act), applied here to
    /// every Equip/Unequip mailbox Start() too.
    static bool LiveMatch(const EdenDsmodHostApi& h, u64 addr, u32 acd, u32 gbid, u32 container, int slot);

    Reader reader;
    Text text;
    std::mutex mutex; // jobs + selection state (actions arrive on the input thread)
    std::map<u64, std::shared_ptr<Job>> jobs;
    u64 next_seq = 0, tick = 0, last_read = 0;
    u64 hero_key[2] = {0, 0}, gear_key[2] = {0, 0};
    HeroData hero;
    std::vector<ItemData> items;
    bool have = false, want_read = false, hero_due = false, gear_due = false;
    double view_max_us = 0;
    u64 hero_sig = 0, hero_view_sig = 0, items_sig = 0;
    bool hero_forced = false;
    std::shared_ptr<const std::vector<ItemData>> items_ptr;
    HeroView hero_view;
    GearView gear_view;
    // selection state (companion's own selection; never mirrors the game cursor)
    u32 sel_acd = 0;  // selected item: a backpack item of AVAILABLE (or of the bag grid); 0 = equipped
    u32 sel_gbid = 0; // gbid captured alongside sel_acd, re-verified live before Equip/Unequip act
    int sel_slot = 4; // Main Hand by default
    bool bag = false;
    int avail_scroll = 0;
    // gear actions: Equip / Unequip (instant, guest mailbox) and Drop (native inventory menu drive)
    bool GearRefusal(bool busy, bool supported); // true (and a notice) when a gear action must be refused
    GearDrive drive;
    GearMailbox gear_mb;
    // the mailbox call in flight: Waiting for the stub's result, then Verify re-reads inventory
    // (two fresh cycles, same as the drive) before the page shows Done/Failed - mirrors the
    // drive's own Close/Verify so Equip/Unequip/Drop all confirm the same way.
    // EvictForSwap: Equip onto an already-occupied slot was never itself live-verified (only a
    // *native*-driven swap was traced; D3_PlaceItem's internal eviction step was seen to just zero
    // the displaced item's container/slot fields, with no confirmed relocation of its own - see
    // INVENTORY-ACTIONS.md). Equip onto an occupied slot therefore runs as two separately-verified
    // calls instead of trusting that single-call swap: first evict the occupant to a known free
    // backpack cell (identical to a plain Unequip), then - only once that is confirmed - place the
    // desired item, continuing on as an ordinary MbKind::Equip.
    enum class MbKind { Equip, Unequip, EvictForSwap };
    enum class MbPhase { Idle, Waiting, Verify };
    MbKind mb_kind = MbKind::Equip;
    MbPhase mb_phase = MbPhase::Idle;
    u32 mb_acd = 0;
    int mb_target_slot = 0; // Equip: the destination slot; Unequip/EvictForSwap: 0 (backpack)
    // EvictForSwap only: the desired item to place once the occupant is confirmed clear. acd+gbid
    // captured at the original tap, re-verified live (LiveMatch) immediately before the step-2
    // Start() in Sample() -- the step-1 call's own ~1 s+ round trip is a materially larger
    // staleness window than any other mailbox call here.
    u64 mb_swap_item_addr = 0;
    u32 mb_swap_item_acd = 0, mb_swap_item_gbid = 0;
    int mb_swap_target_slot = 0;
    u64 mb_close_cycles = 0, mb_started = 0;
    GearDrive::Result mb_result = GearDrive::Result::None;
    std::string mb_status;
    u64 mb_finished_at = 0;
    u64 now = 0, cycles = 0, confirm_at = 0;
    bool confirm = false, menu_none = false, alive = false;
    u32 confirm_acd = 0, confirm_gbid = 0; // identity captured when Drop was first tapped (armed)
    // Debounce: gear_equip/gear_unequip/gear_drop/gear_cancel are refused for ~300 ms after any one
    // of them ran (success or refusal), so a same-rect double dispatch across a confirm-state flip
    // can land on at most one of them even if the manifest's own gating is momentarily stale
    // (owner bug 2026-10-17: Cancel next to an armed Drop equipped a different item and dropped the
    // one that was equipped). Rects are also kept physically distinct (d3_page_gear.h
    // GearConfirmBtnY) as the primary fix; this is the second, independent layer.
    static constexpr u64 kGearActionDebounceTicks = 18; // ~300 ms at the ~60 Hz tick rate
    u64 last_gear_action_at = 0;
    std::string refusal;
    u64 refusal_at = 0, notice_sig = 0;
    int last_phase = -1;
    int scroll = 0;
    int effects_scroll = 0;
    std::atomic<bool> dirty{true};
    std::atomic<double> hero_ms{0}, gear_ms{0};
};

} // namespace d3hg
