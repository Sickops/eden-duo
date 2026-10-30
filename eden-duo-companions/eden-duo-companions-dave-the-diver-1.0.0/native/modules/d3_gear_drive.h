// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Gear Drop, carried out by the game's own console inventory menu (2.7.7.92380): the module
// presses the native buttons through manifest enforce rules (one d3.drv.p<id> gate per press, at
// most one press per sample, d3.enf as the enforce gate) and checks every step against a plain-read
// menu-state reader. Nothing here writes guest memory or calls guest code.
// Equip and Unequip no longer use this: they call the game's own D3_PlaceItem function directly
// through a load-plan guest mailbox (d3_gear_mailbox.h), instantly and with no native menu. Drop
// stays here because its native X-drop reaches a different, far larger request that the RE pass
// did not finish isolating (it is D3_PlaceItem with request type 0 and world-space drop points,
// still too incompletely understood to call directly without risking item loss).
//
// Menu-state reader (InvMenu):
//   UI control keys = FNV-1a 64 of the lower-cased full control path (main+0x347750: copy,
//   tolower main+0x7AB0, hash main+0x6C90); controls are found in the window map [[G+0xB80]]
//   (FNV-1a 32 over the key's low 4 bytes, node {next, key, control}). Control +0x18 bit 0x04 =
//   shown, +0x30 = its own key, +0x38 = its parent's key.
//   ...ConsoleInventoryRing_main.LayoutRoot.InventoryRing +0x48 -> the inventory handler
//   (vtable main+0x10A5EE0): +0xF8 ring position code, +0xFC 1 = ring focused / 0 = item list
//   focused, +0x150 list position, +0x278 + 0x18 * position = {u32* acd ids, u64 count, u64 cap}
//   the compatible backpack items in display order, +0xF30 the equipped item ACD shown.
//   ...LayoutRoot.InventoryList.ItemList +0x1E0 s32 cursor row, +0x218 u32 row count.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "d3_build.h"
#include "d3_reader.h"

namespace d3hg {
using u32 = std::uint32_t;
using u64 = std::uint64_t;

struct InvMenu {
    bool open = false;       // inventory window + ring shown, handler vtable matches
    u32 code = 0xFFFFFFFFu;  // ring position code (handler +0xF8)
    u32 pos = 0xFFFFFFFFu;   // list position (handler +0x150)
    bool ring_focus = false; // handler +0xFC
    u32 equipped = 0;        // handler +0xF30
    std::vector<u32> list;   // compatible backpack ACD ids of `pos`, display order
    bool list_ok = false;    // ItemList control readable
    int cursor = -1;
    u32 rows = 0;
};

/// Ring layout of an equipment slot (1..13): position k clockwise from the chest (stick angle
/// k * 360/14 - 360/28 from straight up), its handler code and list position.
struct RingSlot {
    int slot, k;
    u32 code, pos;
};
const RingSlot* RingFor(int slot);

/// Button ids of the drive gates d3.drv.p<id> (the manifest's d3drv_<id> actions).
enum DriveButton : int { BtnPlus = 0, BtnA, BtnB, BtnX, BtnUp, BtnDown, BtnStick0 };

class GearDrive {
public:
    enum class Result { None, Running, Done, Failed };
    explicit GearDrive(const d3_build::Build& b) : build{b} {}
    bool Supported() const;
    bool Busy() const {
        return phase != Phase::Idle;
    }
    /// Starts a drop (the caller has checked ownership and compatibility); false = refused. `gbid`
    /// is captured alongside `acd` (the identity captured when Drop was first tapped) so Step() can
    /// re-verify the exact item, not just a possibly-recycled ACD id, right before the X press.
    bool Start(u32 acd, u32 gbid, int slot, u32 equipped_acd, u64 now);
    /// One sample. `g` = CGame*, `s` = this tick's main reader snapshot, `cycles` = completed
    /// inventory read cycles; `owned_slot(acd)` answers where an item is now (-2 = not owned);
    /// `gbid_of(acd)` answers its current gbid (0 = not owned), re-checked against the gbid Start()
    /// captured immediately before every destructive press (Phase::Act).
    template <typename OwnedSlot, typename GbidOf>
    void Step(const EdenDsmodHostApi& h, u64 g, const d3_reader::Snapshot& s, u64 now, u64 cycles,
              OwnedSlot&& owned_slot, GbidOf&& gbid_of);
    /// The press due this tick (-1 none); presses are sent on even ticks only (the manifest's press
    /// rules run every second tick so that their refusal log stays quiet while idle).
    int Emit(u64 now);
    bool WantRead() {
        const bool w = want_read;
        want_read = false;
        return w;
    }
    /// Used only when the world itself unloads (hero switch, return to menu, any load): drop
    /// whatever phase/press state was in flight without finishing it, and clear the last
    /// result/status too, so nothing from the previous hero/session can surface as a notice in the
    /// next one. Never called mid-session; a live drive always reaches Idle through Step() itself.
    void Abort() {
        phase = Phase::Idle;
        pending = -1;
        retries = steps = stable = b_presses = 0;
        acted = can_close = false;
        want_read = false;
        failure = nullptr;
        result = Result::None;
        status.clear();
        acd = gbid = 0;
        equipped_acd = 0;
    }
    Result result = Result::None;
    std::string status; // short notice for the page (last outcome or progress)
    u32 acd = 0, gbid = 0; // identity captured at Start(); re-verified before every destructive press
    u64 finished_at = 0;
    InvMenu menu; // last read (diagnostics)
    int PhaseId() const {
        return static_cast<int>(phase);
    }

private:
    enum class Phase { Idle, Open, Select, Enter, Walk, Act, After, Close, Verify };
    void Press(int b, u64 now);
    void Fail(const char* why, u64 now);
    bool Read(const EdenDsmodHostApi& h, u64 g);
    const d3_build::Build& build;
    Phase phase = Phase::Idle;
    int slot = 0, row = -1, retries = 0, steps = 0, stable = 0, b_presses = 0;
    u32 equipped_acd = 0;
    int pending = -1;
    u64 sent = 0, started = 0, phase_at = 0, close_cycles = 0;
    bool want_read = false, acted = false, can_close = false;
    const char* failure = nullptr;
};

bool ReadInvMenu(const EdenDsmodHostApi& h, const d3_build::Build& build, u64 g, InvMenu& out);

template <typename OwnedSlot, typename GbidOf>
void GearDrive::Step(const EdenDsmodHostApi& h, u64 g, const d3_reader::Snapshot& s, u64 now, u64 cycles,
                     OwnedSlot&& owned_slot, GbidOf&& gbid_of) {
    using MS = d3_reader::Snapshot::Menu;
    if (phase == Phase::Idle)
        return;
    if (pending >= 0)
        return; // a press is queued, about to be emitted this/next tick: never Fail() out from
                // under it (Fail() does not clear `pending`, and Emit() does not check `phase`, so
                // a press left queued when phase goes Idle would still fire later as a stray input)
    const bool readable = Read(h, g);
    const RingSlot* rs = RingFor(slot);
    // B presses that close the menu are only ever sent into our own, readable inventory menu
    can_close = s.menu == MS::Inventory && readable && menu.open;
    const bool live = s.state == d3_reader::Snapshot::State::Ready && s.hp > 0;
    // Checked before the "still settling" return below (not folded into the same early-return as
    // `pending`, since the press has already been emitted by now and Fail() is safe here): the
    // hero dying or the game unloading mid-settle is detected at once instead of waiting out up to
    // 12 more ticks (~200 ms) for the next unsettled Step() call.
    if (!live)
        return Fail("Not available right now", now);
    if (now < sent + 12)
        return; // still settling after the last press
    if (now - started > 15 * 60 && phase != Phase::Close && phase != Phase::Verify)
        return Fail("The menu did not respond", now);
    const u64 waited = now - phase_at;
    auto go = [&](Phase p) { phase = p, phase_at = now; };
    // any other native menu or dialog: stop pressing at once
    if (phase != Phase::Open && phase != Phase::Verify && s.menu != MS::Inventory &&
        !(phase == Phase::Close && s.menu == MS::None))
        return Fail("Another menu is open", now);
    switch (phase) {
    case Phase::Idle:
        return;
    case Phase::Open:
        if (s.menu == MS::Inventory && readable && menu.open && menu.ring_focus) {
            if (++stable < 3)
                return;
            if (menu.code == rs->code)
                return go(Phase::Select);
            Press(BtnStick0 + rs->k, now);
            return go(Phase::Select);
        }
        stable = 0;
        // the HUD hides a few frames before the inventory window shows (Other -> Inventory):
        // only a pause / map menu, or another menu that stays, is a refusal
        if (s.menu == MS::Pause || s.menu == MS::WorldMap || (s.menu == MS::Other && waited > 60))
            return Fail("Another menu is open", now);
        if (waited > 120)
            return Fail("The inventory did not open", now);
        return;
    case Phase::Select:
        if (!readable || !menu.open || !menu.ring_focus)
            return waited > 60 ? Fail("Inventory state changed", now) : void();
        if (menu.code != rs->code) {
            if (waited > 40 && retries < 2) {
                ++retries;
                Press(BtnStick0 + rs->k, now);
                phase_at = now;
            } else if (waited > 40) {
                Fail("Slot could not be selected", now);
            }
            return;
        }
        if (menu.pos != rs->pos || (equipped_acd && menu.equipped != equipped_acd))
            return Fail("Inventory state changed", now);
        for (row = 0; row < static_cast<int>(menu.list.size()); ++row)
            if (menu.list[row] == acd)
                break;
        if (row >= static_cast<int>(menu.list.size()))
            return Fail("Item cannot go in this slot", now);
        retries = 0;
        Press(BtnA, now);
        return go(Phase::Enter);
    case Phase::Enter:
        if (readable && menu.open && !menu.ring_focus && menu.code == rs->code && menu.list_ok &&
            menu.rows == menu.list.size())
            return go(Phase::Walk);
        // a press the game did not take (still on the ring, same slot, half a second later) is
        // repeated once; it can only open the list, never act on an item
        if (readable && menu.open && menu.ring_focus && menu.code == rs->code && waited > 30 && retries < 3) {
            ++retries;
            Press(BtnA, now);
            phase_at = now;
            return;
        }
        return waited > 60 ? Fail("Inventory list did not open", now) : void();
    case Phase::Walk:
        if (!readable || !menu.open || menu.ring_focus || menu.code != rs->code || !menu.list_ok ||
            row >= static_cast<int>(menu.list.size()) || menu.list[row] != acd)
            return Fail("Inventory state changed", now);
        if (menu.cursor == row)
            return go(Phase::Act);
        if (++steps > 70 || waited > 50 * 60)
            return Fail("Inventory cursor did not move", now);
        Press(menu.cursor < row ? BtnDown : BtnUp, now);
        return;
    case Phase::Act:
        // Final identity check right before the destructive press: the exact item ACD captured at
        // Start() must still be the one the native list shows at `row`, its gbid must be unchanged
        // (an ACD id reused under us would otherwise read as "still the same item"), and it must
        // not be the slot's own equipped item (an equipped item is never droppable unless it was
        // the item Drop was actually armed on, which this module only ever arms on a backpack
        // selection - see Tabs::Action's avail_item()). Any mismatch aborts without pressing X.
        if (!readable || !menu.open || menu.ring_focus || menu.code != rs->code || menu.cursor != row ||
            row >= static_cast<int>(menu.list.size()) || menu.list[row] != acd || menu.equipped == acd ||
            gbid_of(acd) != gbid)
            return Fail("Inventory state changed", now);
        acted = true;
        Press(BtnX, now);
        return go(Phase::After);
    case Phase::After: {
        bool gone = readable && menu.open;
        for (u32 id : menu.list)
            gone = gone && id != acd;
        if (gone)
            return go(Phase::Close);
        return waited > 90 ? Fail("The game did not take the item", now) : void();
    }
    case Phase::Close:
        if (s.menu == MS::None) {
            if (failure && !acted) {
                status = failure;
                result = Result::Failed, phase = Phase::Idle, finished_at = now;
                return;
            }
            close_cycles = cycles;
            want_read = true;
            return go(Phase::Verify);
        }
        if (s.menu != MS::Inventory || ++b_presses > 4) {
            status = failure ? failure : "The inventory did not close";
            result = Result::Failed;
            phase = Phase::Idle;
            finished_at = now;
            return;
        }
        Press(BtnB, now);
        return;
    case Phase::Verify: {
        if (cycles < close_cycles + 2) {
            if (waited > 6 * 60) {
                status = "Inventory could not be re-read";
                result = Result::Failed, phase = Phase::Idle, finished_at = now;
            }
            return;
        }
        const int where = owned_slot(acd);
        if (failure) {
            status = failure;
            result = Result::Failed;
        } else if (where == -2) {
            status = "Dropped";
            result = Result::Done;
        } else {
            status = "Not dropped";
            result = Result::Failed;
        }
        phase = Phase::Idle;
        finished_at = now;
        return;
    }
    }
}

} // namespace d3hg
