// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Gear Equip / Unequip through the guest load plan (tools/d3/gen_load_plan.py): a stub patched
// into the game thread's own top-level loop (main+0x964, a `bl` run every loop iteration
// regardless of menu state or combat) serves one placement request per iteration, on the game
// thread, with no native menu and no input. It calls main+0x308FD0 (D3_PlaceItem), the same
// function the native console inventory calls on
// A (equip) and on a swap's own "move the displaced item to a free backpack cell" step - this
// keeps every game-side validation (legal slot, backpack space, compatibility) the native menu
// has. NCE-safe: unlike `call`/`sequence` actions (the Dynarmic-only guest bridge, disabled under
// NCE), this is plain, aligned, atomic mailbox memory I/O plus load-time code patches.
// The request shape and hook site were found by tracing the native inventory menu's own equip
// and swap actions with breakpoints; see tools/d3/gen_load_plan.py for the stub.
//
// Drop is not implemented this way (native X-drop reaches a different, far larger request the RE
// pass did not finish isolating in the time available) and stays on the native-menu drive
// (d3_gear_drive.h).

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace d3hg {
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// mailbox layout (gen_load_plan.py)
constexpr u32 MbSeq = 0x00, MbTaken = 0x04, MbHeartbeat = 0x08, MbDone = 0x0C, MbItemAcd = 0x10,
              MbDestContainer = 0x18, MbDestSlot = 0x1C, MbDestX = 0x20, MbDestY = 0x24, MbResult = 0x28;

struct GearGuestIo {
    u64 address = 0; // guest address of the mailbox; 0 = no load plan (2.7.6, or a host build too old)
    std::function<bool(u32 off, u32* v)> load32;
    std::function<bool(u32 off, u32 v)> store32;
    std::function<bool(u32 off, u64 v)> store64;
    std::function<bool(u32 off, u64* v)> load64;
};

/// One in-flight D3_PlaceItem call. Never writes guest memory outside the package's reserved
/// mailbox; the actual item move happens entirely in the game's own code on the game thread.
class GearMailbox {
public:
    using Clock = std::chrono::steady_clock;

    void Configure(GearGuestIo new_io) {
        io = std::move(new_io);
    }
    bool Present() const {
        return io.address != 0;
    }
    /// The stub has run within the last second (it runs every rendered frame; a stall this long
    /// means the hook is not actually installed, e.g. the running build has no load plan).
    bool Alive() const {
        return Present() && last_beat_change.time_since_epoch().count() != 0 &&
               Clock::now() - last_beat_change < std::chrono::seconds(1);
    }
    bool Busy() const {
        return waiting;
    }
    /// Used only when the world itself unloads: drop the host's own wait for whatever call is in
    /// flight. Does not touch the guest stub's own mailbox memory, so a still-unserved request
    /// keeps `MbDone` behind its `seq` until the stub gets to it -- a Start() issued right after
    /// this still correctly refuses until it does, the same self-healing behavior the 2 s Poll()
    /// timeout below already relies on.
    void Cancel() {
        waiting = false;
    }
    /// True once, right after a Poll() 2 s stall cancelled a call the stub had not served yet,
    /// until a Start() actually gets through (served == current) again. Lets a caller whose
    /// Start() was refused purely because that earlier, merely-delayed call still has not been
    /// served distinguish it from any other "not available" refusal, instead of both looking the
    /// same silent "busy".
    bool TimedOut() const {
        return timed_out;
    }

    /// Starts a call: place item `item_addr` (a live ACD*) at container `dest_container`
    /// (always the hero's own ACD id), slot `dest_slot` (0 backpack, 1..13 equip) and backpack
    /// cell (`dest_x`, `dest_y`) when `dest_slot` is 0. False = refused (not present, busy, or a
    /// still-unserved prior request).
    bool Start(u64 item_addr, u32 dest_container, u32 dest_slot, u32 dest_x, u32 dest_y) {
        if (!Present() || Busy() || !item_addr || !io.load32 || !io.store32 || !io.store64)
            return false;
        u32 current = 0, served = 0;
        if (!io.load32(MbSeq, &current) || !io.load32(MbDone, &served) || served != current)
            return false; // a cancelled request the stub has not served yet still owns seq
        timed_out = false; // whatever earlier call Poll() gave up on has now actually been served
        bool ok = io.store64(MbItemAcd, item_addr);
        ok = ok && io.store32(MbDestContainer, dest_container);
        ok = ok && io.store32(MbDestSlot, dest_slot);
        ok = ok && io.store32(MbDestX, dest_x);
        ok = ok && io.store32(MbDestY, dest_y);
        seq = current + 1;
        ok = ok && io.store32(MbSeq, seq); // last: publishes the request
        waiting = ok;
        issued = Clock::now();
        return ok;
    }

    /// Once per sample: heartbeat, then the pending call's result. Calls `on_done(accepted)`
    /// exactly once, the sample the result lands (or on a 2 s stall). `accepted` is the call's own
    /// x0 return value being ZERO (D3_PlaceItem's own convention, read at its `ret`: the success
    /// path sets `w0 = 0` at main+0x3093a0 before writing the new location back; every refusal path
    /// sets `w0 = 1` - confirmed live by a software breakpoint at D3_PlaceItem's entry across two
    /// independent native actions, then by inverting an initially-backwards x0!=0 check here against
    /// the game's own after-the-fact item location). The module still re-reads the item's slot to
    /// confirm, same as the menu-drive path did.
    template <typename OnDone>
    void Poll(OnDone&& on_done) {
        u32 beat = 0;
        if (Present() && io.load32 && io.load32(MbHeartbeat, &beat) && beat != last_beat) {
            last_beat = beat;
            last_beat_change = Clock::now();
        }
        if (!waiting)
            return;
        u32 done = 0;
        if (io.load32 && io.load32(MbDone, &done) && done == seq) {
            waiting = false;
            u64 result = 0;
            const bool read_ok = io.load64 && io.load64(MbResult, &result);
            on_done(read_ok && result == 0);
            return;
        }
        if (Clock::now() - issued > std::chrono::seconds(2)) {
            // the per-frame dispatcher stopped serving (should not happen outside a genuine
            // pause): cancel so this does not block every later Start() forever.
            if (io.store64)
                io.store64(MbItemAcd, 0);
            waiting = false;
            timed_out = true;
            on_done(false);
        }
    }

private:
    GearGuestIo io;
    bool waiting = false, timed_out = false;
    u32 seq = 0, last_beat = 0;
    Clock::time_point issued{}, last_beat_change{};
};

} // namespace d3hg
