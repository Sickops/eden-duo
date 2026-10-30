// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_hg_tabs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace d3hg {

u64 Tabs::Add(Job j) {
    const u64 seq = ++next_seq;
    jobs[seq] = std::make_shared<Job>(std::move(j));
    return seq;
}

bool Tabs::LiveMatch(const EdenDsmodHostApi& h, u64 addr, u32 acd, u32 gbid, u32 container, int slot) {
    u32 r_acd = 0, r_gbid = 0, r_container = 0;
    int r_slot = -1;
    return ReadLiveItem(h, addr, r_acd, r_gbid, r_container, r_slot) && r_acd == acd && r_gbid == gbid &&
           r_container == container && r_slot == slot;
}

void Tabs::Prune() {
    if (jobs.size() <= 8)
        return;
    for (auto it = jobs.begin(); it != jobs.end();) {
        const u64 k = it->first;
        const bool used = k == hero_key[0] || k == hero_key[1] || k == gear_key[0] || k == gear_key[1];
        it = used ? std::next(it) : jobs.erase(it);
    }
}

void Tabs::Sample(const EdenDsmodHostApi& h, bool assets_ready, bool world, const d3_reader::Snapshot& s) {
    ++tick;
    static const bool off = std::getenv("D3_HG_OFF") != nullptr; // diagnostic baseline only
    if (off)
        world = false;
    auto pub_key = [&](const char* name, u64 seq) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "module:d3:%s/%llu", name[3] == 'h' ? "hero" : "gear",
                      static_cast<unsigned long long>(seq));
        h.publish_text(h.userdata, name, seq ? buf : "");
    };
    // One bounded unit of work per tick, never on the map's full-read ticks (every 15th): a hero
    // view build, a gear view build, or a reader slice (<= ~80 us of guest reads). A read cycle starts
    // half a second after the previous one completed, or right after an action.
    if (dirty.exchange(false))
        want_read = true, hero_due = gear_due = hero_forced = true;
    const auto tb = std::chrono::steady_clock::now();
    std::lock_guard lock{mutex};
    now = h.get_tick ? h.get_tick(h.userdata) : tick;
    menu_none = world && s.menu == d3_reader::Snapshot::Menu::None;
    alive = world && s.state == d3_reader::Snapshot::State::Ready && s.hp > 0;
    if (!world) {
        have = false;
        reader.Restart();
        hero_sig = hero_view_sig = items_sig = 0;
        items_ptr.reset();
        hero_due = gear_due = hero_forced = want_read = true;
        hero_key[0] = hero_key[1] = gear_key[0] = gear_key[1] = 0;
        effects_scroll = 0;
        // A hero switch (character select), return to main menu or any load must never let the
        // previous session's selection/confirm/pending state silently re-attach to an unrelated
        // item in the next hero/session purely by ACD-id coincidence (ids are commonly reissued
        // from a low range on a fresh load) -- clear every bit of it, same as the read-cycle state
        // above, and abort any drive/mailbox call that was in flight.
        sel_acd = sel_gbid = 0;
        bag = false;
        avail_scroll = 0;
        confirm = false;
        confirm_acd = confirm_gbid = 0;
        refusal.clear();
        drive.Abort();
        gear_mb.Cancel();
        mb_phase = MbPhase::Idle;
        mb_result = GearDrive::Result::None;
        mb_status.clear();
    }
    const bool can = world && assets_ready && tick % 15 != 0;
    const bool on = world && have && assets_ready && reader.Meta();
    if (can && on && hero_due && (hero_sig != hero_view_sig || hero_view_sig == 0 || hero_forced)) {
        hero_view_sig = hero_sig;
        hero_forced = false;
        hero_due = false;
        HeroView hv = BuildHeroView(*reader.Meta(), hero, items);
        scroll = std::clamp(scroll, 0, std::max(0, HeroRowCount(hv) - 1));
        hv.scroll = scroll;
        effects_scroll = std::clamp(effects_scroll, 0, std::max(0, HeroEffectsRowCount(hv) - 1));
        hv.effects_scroll = effects_scroll;
        if (!hero_key[1] || !(hv == hero_view)) {
            hero_view = std::move(hv);
            hero_key[0] = hero_key[1];
            hero_key[1] = Add(HeroJob{hero_view});
        }
    } else if (can && on && hero_due) {
        hero_due = false; // unchanged hero data: nothing to rebuild
    } else if (can && on && gear_due) {
        gear_due = false;
        const AttrMeta& m = *reader.Meta();
        GearView gv;
        gv.valid = true;
        gv.bag = bag;
        gv.items = items_ptr;
        const std::vector<ItemData>& its = items_ptr ? *items_ptr : items;
        for (const auto& it : its)
            gv.backpack += it.slot == 0;
        gv.backpack_max = static_cast<int>(Attr(m, hero.attrs, "Backpack_Slots", NoParam, 60));
        gv.gold = hero_view.gold;
        gv.shards = hero_view.shards;
        gv.shard_cap = hero_view.shard_cap;
        gv.slot = sel_slot;
        gv.equipped = GearFindSlot(its, sel_slot);
        const u32 cls = GearClassBit(hero.portrait_actor);
        const int mh = GearFindSlot(its, 4);
        const bool two = mh >= 0 && (its[mh].itype_flags & 0x400) != 0;
        gv.offhand_blocked = two && cls != 0x20000;
        for (size_t i = 0; i < its.size(); ++i)
            if (GearCompatible(its[i], sel_slot, cls, two))
                gv.avail.push_back(static_cast<int>(i));
        {
            // native list order as observed (2.7.7): set items first, then the higher quality, then
            // backpack order; the drive always walks the game's own list, never this order
            auto rank = [&](int i) {
                const ItemData& it = its[i];
                return std::pair{it.set ? 0 : 1, -static_cast<int>(Attr(m, it.attrs, "Item_Quality_Level", NoParam, 1))};
            };
            std::stable_sort(gv.avail.begin(), gv.avail.end(), [&](int x, int y) { return rank(x) < rank(y); });
        }
        avail_scroll = std::clamp(avail_scroll, 0, std::max(0, static_cast<int>(gv.avail.size()) - GearAvailRows));
        gv.scroll = avail_scroll;
        gv.sel = -1;
        if (bag) {
            // backpack grid: the chosen item while it exists, else the first equipped item
            for (size_t i = 0; i < its.size(); ++i)
                if (sel_acd && its[i].acd == sel_acd)
                    gv.sel = static_cast<int>(i);
            if (gv.sel < 0)
                for (int sl : GearSlotOrder)
                    if ((gv.sel = GearFindSlot(its, sl)) >= 0)
                        break;
        } else {
            for (int i : gv.avail)
                if (sel_acd && its[i].acd == sel_acd)
                    gv.sel = i;
            if (gv.sel < 0)
                gv.sel = gv.equipped;
        }
        const bool avail_sel = !bag && gv.sel >= 0 && gv.sel != gv.equipped;
        const bool idle = !drive.Busy() && mb_phase == MbPhase::Idle && menu_none && alive;
        const bool can_drop = avail_sel && drive.Supported() && idle;
        const bool can_eq = idle && gear_mb.Alive();
        gv.confirm = confirm && avail_sel && its[gv.sel].acd == confirm_acd;
        gv.can_equip = can_eq && avail_sel && !gv.confirm;
        gv.can_unequip = can_eq && gv.equipped >= 0 && !gv.confirm;
        gv.can_drop = can_drop;
        gv.busy = drive.Busy() || mb_phase != MbPhase::Idle;
        if (drive.Busy()) {
            gv.notice = drive.status;
        } else if (mb_phase != MbPhase::Idle) {
            gv.notice = mb_status;
        } else if (drive.result != GearDrive::Result::None && now - drive.finished_at < 4 * 60) {
            gv.notice = drive.status;
            gv.notice_bad = drive.result == GearDrive::Result::Failed;
        } else if (mb_result != GearDrive::Result::None && now - mb_finished_at < 4 * 60) {
            gv.notice = mb_status;
            gv.notice_bad = mb_result == GearDrive::Result::Failed;
        } else if (!refusal.empty() && now - refusal_at < 3 * 60) {
            gv.notice = refusal;
            gv.notice_bad = true;
        } else if (gv.confirm) {
            gv.notice = "Tap Drop again to drop";
        }
        if (!gear_key[1] || !(gv == gear_view)) {
            gear_view = std::move(gv);
            gear_key[0] = gear_key[1];
            gear_key[1] = Add(GearJob{gear_view, reader.Meta()});
        }
    } else if (can) {
        if (reader.Finished() && (want_read || tick - last_read >= 30))
            reader.Restart(), want_read = false;
        const int r = reader.Step(h, hero, items, 80.0);
        if (r == 1) {
            have = true;
            ++cycles;
            // content signatures: views are rebuilt only when what they show changed
            u64 hs = 1469598103934665603ull, is = hs;
            auto mix = [](u64& hh, const void* d, size_t n) {
                for (size_t k = 0; k < n; ++k)
                    hh = (hh ^ static_cast<const u8*>(d)[k]) * 1099511628211ull;
            };
            mix(hs, hero.name.data(), hero.name.size());
            mix(hs, &hero.cls, sizeof hero.cls), mix(hs, &hero.level, sizeof hero.level);
            mix(hs, &hero.portrait_actor, sizeof hero.portrait_actor);
            mix(hs, &hero.xp_left, sizeof hero.xp_left), mix(hs, &hero.xp_need, sizeof hero.xp_need);
            mix(hs, hero.attrs.data(), hero.attrs.size() * sizeof(AttrKV));
            for (const auto& b : hero.buffs) {
                mix(hs, &b.power, sizeof b.power), mix(hs, &b.icon, sizeof b.icon);
                mix(hs, &b.slot, sizeof b.slot), mix(hs, &b.stacks, sizeof b.stacks);
                mix(hs, &b.secs, sizeof b.secs), mix(hs, &b.harmful, sizeof b.harmful);
            }
            mix(hs, &hero.gold, sizeof hero.gold), mix(hs, &hero.shards, sizeof hero.shards);
            mix(hs, &hero.paragon, sizeof hero.paragon);
            mix(hs, &hero.cube_valid, sizeof hero.cube_valid);
            for (const auto& power : hero.cube) {
                mix(hs, &power.gbid, sizeof power.gbid), mix(hs, &power.icon, sizeof power.icon);
                mix(hs, power.gb_name.data(), power.gb_name.size());
            }
            for (const auto& it : items) {
                mix(is, &it.acd, 4), mix(is, &it.slot, 12);
                mix(is, it.attrs.data(), it.attrs.size() * sizeof(AttrKV));
                for (const auto& g2 : it.gems)
                    mix(is, &g2.acd, 4), mix(is, g2.attrs.data(), g2.attrs.size() * sizeof(AttrKV));
            }
            if (hs != hero_sig || !hero_view_sig)
                hero_sig = hs, hero_due = gear_due = true; // currencies also appear on Gear
            if (is != items_sig || !items_ptr) {
                items_sig = is;
                items_ptr = std::make_shared<const std::vector<ItemData>>(items);
                gear_due = true;
            }
            last_read = tick;
            h.publish_f64(h.userdata, "d3.perf.hg_read_us", reader.last_us);
            if (std::getenv("D3_HG_DEBUG") && h.log) {
                char b[160];
                std::snprintf(b, sizeof b, "D3 hg cycle: %.0f us total, slice max %.0f us, %zu items", reader.last_us,
                              reader.max_step_us, items.size());
                h.log(h.userdata, EDEN_DSMOD_LOG_INFO, b);
            }
        } else if (r < 0) {
            have = false;
        }
    }
    // ---- gear actions: one drive step per sample, at most one native press ----
    {
        const bool was_busy = drive.Busy();
        if (was_busy) {
            const u64 g = world ? reader.GameRoot(h) : 0;
            drive.Step(
                h, g, s, now, cycles,
                [&](u32 id) {
                    for (const auto& it : items)
                        if (it.acd == id)
                            return it.slot;
                    return -2;
                },
                [&](u32 id) -> u32 {
                    for (const auto& it : items)
                        if (it.acd == id)
                            return it.gbid;
                    return 0;
                });
            if (drive.WantRead()) {
                want_read = true;
                if (reader.Finished())
                    reader.Restart(), want_read = false;
            }
        }
        if (confirm && now - confirm_at > 5 * 60)
            confirm = false;
        if (was_busy && !drive.Busy() && drive.result == GearDrive::Result::Done)
            sel_acd = 0; // show what is equipped now
        if (drive.Busy() && drive.PhaseId() != last_phase && h.log) {
            char b[160];
            std::snprintf(b, sizeof b, "D3 gear drive: phase %d (ring code %u pos %u focus %d cursor %d rows %u list %zu)",
                          drive.PhaseId(), drive.menu.code, drive.menu.pos, drive.menu.ring_focus ? 1 : 0,
                          drive.menu.cursor, drive.menu.rows, drive.menu.list.size());
            h.log(h.userdata, EDEN_DSMOD_LOG_INFO, b);
        }
        last_phase = drive.Busy() ? drive.PhaseId() : -1;
        if (was_busy && !drive.Busy() && h.log) {
            char b[160];
            std::snprintf(b, sizeof b, "D3 gear drive: drop %08x -> %s", drive.acd, drive.status.c_str());
            h.log(h.userdata, EDEN_DSMOD_LOG_INFO, b);
        }
    }
    // ---- gear actions: Equip/Unequip mailbox poll (at most one call in flight) ----
    {
        const bool mb_was_busy = mb_phase != MbPhase::Idle;
        // EvictForSwap's own user-facing failures read as "Not equipped" (the user tapped Equip),
        // even though the call in flight is itself an Unequip-shaped move.
        const auto not_placed = [&] { return mb_kind == MbKind::Unequip ? "Not unequipped" : "Not equipped"; };
        gear_mb.Poll([&](bool accepted) {
            if (!accepted) {
                mb_status = not_placed();
                mb_result = GearDrive::Result::Failed;
                mb_finished_at = now;
                mb_phase = MbPhase::Idle;
                return;
            }
            mb_close_cycles = cycles;
            want_read = true;
            if (reader.Finished())
                reader.Restart(), want_read = false;
            mb_phase = MbPhase::Verify;
        });
        if (mb_phase == MbPhase::Verify) {
            if (cycles >= mb_close_cycles + 2) {
                int where = -2;
                for (const auto& it : items)
                    if (it.acd == mb_acd) {
                        where = it.slot;
                        break;
                    }
                const bool ok = mb_kind == MbKind::Equip ? where == mb_target_slot : where == 0;
                if (ok && mb_kind == MbKind::EvictForSwap) {
                    // Step 1 done (the occupant is confirmed clear of the slot, in the backpack
                    // cell this module itself picked). Before step 2 (place the desired item,
                    // continuing on as an ordinary Equip), re-verify that item's acd+gbid -- as
                    // captured at the original tap -- against a fresh live read: step 1's own
                    // mailbox round trip plus two read cycles puts well over a second between the
                    // tap and here, a materially larger staleness window than any other mailbox
                    // call. On any mismatch, stop here: the evicted item is already safely back in
                    // the backpack (step 1 succeeded), nothing has been lost.
                    if (!LiveMatch(h, mb_swap_item_addr, mb_swap_item_acd, mb_swap_item_gbid, hero.acd, 0)) {
                        mb_status = "Item changed, left in the backpack";
                        mb_result = GearDrive::Result::Failed;
                        mb_finished_at = now;
                        mb_phase = MbPhase::Idle;
                    } else if (gear_mb.Start(mb_swap_item_addr, hero.acd, static_cast<u32>(mb_swap_target_slot), 0, 0)) {
                        mb_kind = MbKind::Equip, mb_acd = mb_swap_item_acd, mb_target_slot = mb_swap_target_slot;
                        mb_status = "Equipping...", mb_started = now;
                        mb_phase = MbPhase::Waiting;
                    } else {
                        mb_status = gear_mb.TimedOut() ? "Still finishing the previous action" : "Not equipped";
                        mb_result = GearDrive::Result::Failed;
                        mb_finished_at = now;
                        mb_phase = MbPhase::Idle;
                    }
                } else {
                    mb_status = ok ? (mb_kind == MbKind::Equip ? "Equipped" : "Unequipped") : not_placed();
                    mb_result = ok ? GearDrive::Result::Done : GearDrive::Result::Failed;
                    mb_finished_at = now;
                    mb_phase = MbPhase::Idle;
                }
            } else if (now - mb_started > 6 * 60) {
                mb_status = "Inventory could not be re-read";
                mb_result = GearDrive::Result::Failed;
                mb_finished_at = now;
                mb_phase = MbPhase::Idle;
            }
        }
        if (mb_was_busy && mb_phase == MbPhase::Idle) {
            sel_acd = 0; // show the gear state the call produced
            if (h.log) {
                char b[160];
                std::snprintf(b, sizeof b, "D3 gear mailbox: %s %08x -> %s",
                              mb_kind == MbKind::Unequip ? "unequip" : "equip", mb_acd, mb_status.c_str());
                h.log(h.userdata, EDEN_DSMOD_LOG_INFO, b);
            }
        }
    }
    {
        // the page shows the drive / mailbox / notice / button state: rebuild when any changes
        const u64 ns = (drive.Busy() ? 1u : 0u) | (static_cast<u64>(drive.result) << 1) |
                       (mb_phase != MbPhase::Idle ? 0x4u : 0u) | (static_cast<u64>(mb_result) << 3) |
                       (confirm ? 0x10u : 0u) | (menu_none ? 0x20u : 0u) | (alive ? 0x40u : 0u) |
                       ((now - drive.finished_at < 4 * 60) ? 0x80u : 0u) |
                       ((now - mb_finished_at < 4 * 60) ? 0x100u : 0u) |
                       ((!refusal.empty() && now - refusal_at < 3 * 60) ? 0x200u : 0u) |
                       ((std::hash<std::string>{}(drive.status) << 10) ^ (std::hash<std::string>{}(mb_status) << 3));
        if (ns != notice_sig)
            notice_sig = ns, gear_due = true;
        const int press = drive.Emit(now);
        // Each sample's snapshot starts empty (host: "the snapshot is cleared every tick"); a
        // missing enabled_bind point fails its action closed (mod_actions.cpp's "fail closed: a
        // gate that cannot be read ... refuses the action"), the same as enforce_gate treating a
        // missing point as <= 0 (mod_state.cpp). An idle tick (no press) therefore needs none of
        // these 21 publishes at all -- only the one active button, if any, and the gate itself.
        if (press >= 0) {
            char n[24];
            std::snprintf(n, sizeof n, "d3.drv.p%d", press);
            h.publish_i64(h.userdata, n, 1);
            // enforce gate: open only on a press tick, so the drive's press rules run nothing idle
            h.publish_i64(h.userdata, "d3.enf", 1);
        }
        const bool gon = world && have && assets_ready && reader.Meta() && !bag;
        for (int r = 0; r < GearAvailRows; ++r) {
            char n[24];
            std::snprintf(n, sizeof n, "d3.gear.r%d", r);
            h.publish_i64(h.userdata, n, gon && gear_view.scroll + r < static_cast<int>(gear_view.avail.size()) ? 1 : 0);
        }
        h.publish_i64(h.userdata, "d3.gear.can_equip", gon && gear_view.can_equip ? 1 : 0);
        h.publish_i64(h.userdata, "d3.gear.can_unequip", gon && gear_view.can_unequip ? 1 : 0);
        h.publish_i64(h.userdata, "d3.gear.can_drop", gon && (gear_view.can_drop || gear_view.confirm) ? 1 : 0);
        h.publish_i64(h.userdata, "d3.gear.confirm", gon && gear_view.confirm ? 1 : 0);
        h.publish_i64(h.userdata, "d3.gear.more", gon && gear_view.avail.size() > static_cast<size_t>(GearAvailRows) ? 1 : 0);
    }
    view_max_us = std::max(view_max_us, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tb).count());
    h.publish_i64(h.userdata, "d3.hero.on", on ? 1 : 0);
    h.publish_i64(h.userdata, "d3.gear.on", on ? 1 : 0);
    h.publish_i64(h.userdata, "d3.gear.bag", on && bag ? 1 : 0);
    h.publish_i64(h.userdata, "d3.gear.cards", on && !bag ? 1 : 0);
    pub_key("d3.hero.k0", on ? hero_key[0] : 0);
    pub_key("d3.hero.k1", on ? hero_key[1] : 0);
    pub_key("d3.gear.k0", on ? gear_key[0] : 0);
    pub_key("d3.gear.k1", on ? gear_key[1] : 0);
    h.publish_f64(h.userdata, "d3.perf.hero_ms", hero_ms.load());
    h.publish_f64(h.userdata, "d3.perf.gear_ms", gear_ms.load());
    if (tick % 300 == 0 && h.log) {
        char b[160];
        std::snprintf(b, sizeof b, "D3 hero/gear: read cycle %.0f us in slices (max slice %.0f us); view build max %.0f us; hero picture %.1f ms; gear picture %.1f ms",
                      reader.last_us, reader.max_step_us, view_max_us, hero_ms.load(), gear_ms.load());
        view_max_us = 0;
        h.log(h.userdata, EDEN_DSMOD_LOG_INFO, b);
    }
    Prune();
}

bool Tabs::Load(d3_assets::Assets& a, const EdenDsmodHostApi* h, std::string_view key, d3_img::Image& out) {
    const bool hk = key.rfind("hero/", 0) == 0, gk = key.rfind("gear/", 0) == 0;
    if (!hk && !gk)
        return false;
    const u64 seq = std::strtoull(std::string{key.substr(5)}.c_str(), nullptr, 10);
    std::shared_ptr<Job> job;
    {
        std::lock_guard lock{mutex};
        auto it = jobs.find(seq);
        if (it != jobs.end())
            job = it->second;
    }
    if (!job)
        return true; // ours, but gone: empty picture
    const auto t0 = std::chrono::steady_clock::now();
    if (auto* hj = std::get_if<HeroJob>(job.get())) {
        out = HeroPage(a, text, h, hj->v);
        hero_ms.store(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    } else if (auto* gj = std::get_if<GearJob>(job.get())) {
        out = GearPage(a, text, h, *gj->meta, gj->v);
        gear_ms.store(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    return true;
}

bool Tabs::Action(const EdenDsmodHostApi& h, std::string_view name, std::int64_t arg) {
    std::lock_guard lock{mutex};
    auto avail_item = [&]() -> const ItemData* {
        if (!gear_view.items || gear_view.bag || gear_view.sel < 0 || gear_view.sel == gear_view.equipped ||
            gear_view.sel >= static_cast<int>(gear_view.items->size()))
            return nullptr;
        const ItemData& it = (*gear_view.items)[gear_view.sel];
        return it.slot == 0 && it.acd == sel_acd ? &it : nullptr;
    };
    // Busy refusal text: distinguishes a mailbox call that is still waiting on a merely-delayed
    // (not dead) earlier request the 2 s Poll() stall gave up on from any other availability
    // refusal, so that case is never a silent, indistinguishable "Not available right now".
    auto mb_start_refusal = [&] {
        return gear_mb.TimedOut() ? "Still finishing the previous action" : "Not available right now";
    };
    // Debounce: refuse a second gear_equip/gear_unequip/gear_drop/gear_cancel within ~300 ms of the
    // last one of these four, win or refuse, so a same-rect double dispatch across a confirm-state
    // flip can land on at most one of them even if a manifest gate briefly reads stale (see
    // d3_page_gear.h GearConfirmBtnY for the primary, rect-separation fix).
    if (name == "gear_equip" || name == "gear_unequip" || name == "gear_drop" || name == "gear_cancel") {
        if (last_gear_action_at && now - last_gear_action_at < kGearActionDebounceTicks) {
            refusal = "Too fast, try again", refusal_at = now;
            dirty.store(true);
            return false;
        }
        last_gear_action_at = now;
    }
    if (name == "gear_slot") {
        if (arg < 1 || arg > 13)
            return false;
        sel_slot = static_cast<int>(arg);
        sel_acd = sel_gbid = 0, avail_scroll = 0, confirm = false;
    } else if (name == "gear_avail") {
        const int i = gear_view.scroll + static_cast<int>(arg);
        if (!gear_view.items || arg < 0 || i >= static_cast<int>(gear_view.avail.size()))
            return false;
        sel_acd = (*gear_view.items)[gear_view.avail[i]].acd;
        sel_gbid = (*gear_view.items)[gear_view.avail[i]].gbid;
        confirm = false;
    } else if (name == "gear_equipped") {
        sel_acd = sel_gbid = 0, confirm = false;
    } else if (name == "gear_scroll") {
        avail_scroll = std::clamp(avail_scroll + static_cast<int>(std::clamp<std::int64_t>(arg, -5, 5)), 0,
                                  std::max(0, static_cast<int>(gear_view.avail.size()) - GearAvailRows));
    } else if (name == "gear_cancel") {
        confirm = false;
    } else if (name == "gear_equip") {
        const ItemData* it = avail_item();
        if (!it) {
            refusal = "Select an item first", refusal_at = now;
        } else if (confirm || drive.Busy()) {
            // A Drop confirm or drive is active: Equip/Unequip must refuse rather than ever act on
            // whatever Drop had armed (owner bug 2026-10-17).
            refusal = "Finish or cancel the pending drop first", refusal_at = now;
        } else if (GearRefusal(gear_mb.Busy() || mb_phase != MbPhase::Idle, gear_mb.Alive())) {
        } else if (!LiveMatch(h, it->addr, it->acd, sel_gbid, hero.acd, 0)) {
            // Final identity check right before the (destructive) mailbox call: re-reads the ACD
            // record at `it->addr` live, right now, and refuses unless it is still exactly the
            // acd+gbid captured at selection (gear_avail/gear_cell) and still a backpack item of
            // this hero -- closes the gap Drop already closed for its own destructive press
            // (GearDrive::Step's Phase::Act) but the mailbox path never had.
            refusal = "Item changed, try again", refusal_at = now;
        } else {
            const int eq = gear_view.items ? GearFindSlot(*gear_view.items, sel_slot) : -1;
            if (eq >= 0) {
                // The destination slot already holds a different item (avail_item() already
                // excludes gv.sel == gv.equipped, so `it` can never already be what's equipped
                // here): evict it to a known free backpack cell first (MbKind::EvictForSwap,
                // identical mechanism to a plain Unequip) and place `it` only once that is
                // confirmed - see the MbKind::EvictForSwap comment in d3_hg_tabs.h.
                const ItemData& occ = (*gear_view.items)[eq];
                bool occ_grid[6][10] = {};
                for (const auto& o : *gear_view.items)
                    if (o.slot == 0 && o.x >= 0 && o.x < 10 && o.y >= 0 && o.y < 6)
                        occ_grid[o.y][o.x] = true;
                int fx = -1, fy = -1;
                for (int y = 0; y < 6 && fx < 0; ++y)
                    for (int x = 0; x < 10; ++x)
                        if (!occ_grid[y][x]) {
                            fx = x, fy = y;
                            break;
                        }
                if (fx < 0) {
                    refusal = "Inventory full", refusal_at = now;
                } else if (!LiveMatch(h, occ.addr, occ.acd, occ.gbid, hero.acd, sel_slot)) {
                    // Same final check for the occupant being evicted (captured from the view,
                    // gear_view.items, rather than a user selection): confirm it is still live and
                    // still equipped in this exact slot before the first of the two destructive
                    // calls.
                    refusal = "Item changed, try again", refusal_at = now;
                } else if (gear_mb.Start(occ.addr, hero.acd, 0, static_cast<u32>(fx), static_cast<u32>(fy))) {
                    mb_kind = MbKind::EvictForSwap, mb_acd = occ.acd, mb_target_slot = 0;
                    mb_swap_item_addr = it->addr, mb_swap_item_acd = it->acd, mb_swap_item_gbid = sel_gbid;
                    mb_swap_target_slot = sel_slot;
                    mb_result = GearDrive::Result::Running, mb_status = "Equipping...", mb_started = now;
                    mb_phase = MbPhase::Waiting;
                    confirm = false;
                    dirty.store(true);
                    return true;
                } else {
                    refusal = mb_start_refusal(), refusal_at = now;
                }
            } else if (gear_mb.Start(it->addr, hero.acd, static_cast<u32>(sel_slot), 0, 0)) {
                mb_kind = MbKind::Equip, mb_acd = it->acd, mb_target_slot = sel_slot;
                mb_result = GearDrive::Result::Running, mb_status = "Equipping...", mb_started = now;
                mb_phase = MbPhase::Waiting;
                confirm = false;
                dirty.store(true);
                return true;
            } else {
                refusal = mb_start_refusal(), refusal_at = now;
            }
        }
        dirty.store(true);
        return false;
    } else if (name == "gear_unequip") {
        const int eq = gear_view.items ? GearFindSlot(*gear_view.items, sel_slot) : -1;
        if (eq < 0) {
            refusal = "Nothing equipped here", refusal_at = now;
        } else if (confirm || drive.Busy()) {
            refusal = "Finish or cancel the pending drop first", refusal_at = now;
        } else if (GearRefusal(gear_mb.Busy() || mb_phase != MbPhase::Idle, gear_mb.Alive())) {
        } else {
            const ItemData& it = (*gear_view.items)[eq];
            // best-effort free backpack cell (origin-only occupancy; the game's own slot-space
            // check in D3_PlaceItem is the real authority, so a wrong guess still just refuses,
            // never corrupts): scan the 10 x 6 grid for a cell no item currently originates at.
            bool occ[6][10] = {};
            for (const auto& o : *gear_view.items)
                if (o.slot == 0 && o.x >= 0 && o.x < 10 && o.y >= 0 && o.y < 6)
                    occ[o.y][o.x] = true;
            int fx = -1, fy = -1;
            for (int y = 0; y < 6 && fx < 0; ++y)
                for (int x = 0; x < 10; ++x)
                    if (!occ[y][x]) {
                        fx = x, fy = y;
                        break;
                    }
            if (fx < 0) {
                refusal = "Inventory full", refusal_at = now;
            } else if (!LiveMatch(h, it.addr, it.acd, it.gbid, hero.acd, sel_slot)) {
                // Final identity check right before the mailbox call, same guard as gear_equip:
                // `it` (captured from the view, gear_view.items) must still be live and still
                // equipped in this exact slot.
                refusal = "Item changed, try again", refusal_at = now;
            } else if (gear_mb.Start(it.addr, hero.acd, 0, static_cast<u32>(fx), static_cast<u32>(fy))) {
                mb_kind = MbKind::Unequip, mb_acd = it.acd, mb_target_slot = 0;
                mb_result = GearDrive::Result::Running, mb_status = "Unequipping...", mb_started = now;
                mb_phase = MbPhase::Waiting;
                confirm = false;
                dirty.store(true);
                return true;
            } else {
                refusal = mb_start_refusal(), refusal_at = now;
            }
        }
        dirty.store(true);
        return false;
    } else if (name == "gear_drop") {
        const ItemData* it = avail_item();
        if (!it) {
            refusal = "Select an item first", refusal_at = now;
        } else if (GearRefusal(drive.Busy() || mb_phase != MbPhase::Idle, drive.Supported())) {
            // mb_phase check: an Equip/Unequip mailbox call in flight must block Drop too, not just
            // the reverse (owner bug 2026-10-17: the two gear actions must never race each other).
        } else if (!confirm || confirm_acd != it->acd || confirm_gbid != it->gbid) {
            // first tap arms the drop: captures the exact item identity (acd + gbid), re-verified
            // against the live item on the second tap and again by the drive immediately before X.
            confirm = true, confirm_acd = it->acd, confirm_gbid = it->gbid, confirm_at = now;
            dirty.store(true);
            return true;
        } else {
            // second tap: confirm_acd/confirm_gbid already match it->acd/it->gbid (the branch
            // above would have re-armed otherwise), so this is provably the same item Drop armed.
            const int eq = GearFindSlot(*gear_view.items, sel_slot);
            const u32 eq_acd = eq >= 0 ? (*gear_view.items)[eq].acd : 0;
            confirm = false;
            if (drive.Start(it->acd, it->gbid, sel_slot, eq_acd, now)) {
                dirty.store(true);
                return true;
            }
            refusal = "Not available right now", refusal_at = now;
        }
        dirty.store(true);
        return false;
    } else if (name == "gear_cell") {
        if (!gear_view.items)
            return false;
        const int i = GearFindCell(*gear_view.items, static_cast<int>(arg % 10), static_cast<int>(arg / 10));
        if (i < 0)
            return true; // empty cell: nothing to select
        sel_acd = (*gear_view.items)[i].acd;
        sel_gbid = (*gear_view.items)[i].gbid;
        confirm = false;
    } else if (name == "gear_bag") {
        bag = !bag;
    } else if (name == "hero_scroll") {
        scroll = std::clamp(scroll + static_cast<int>(arg), 0, std::max(0, HeroRowCount(hero_view) - 1));
    } else if (name == "hero_effects_scroll") {
        effects_scroll = std::clamp(effects_scroll + static_cast<int>(std::clamp<std::int64_t>(arg, -5, 5)),
                                  0, std::max(0, HeroEffectsRowCount(hero_view) - 1));
    } else {
        return false;
    }
    dirty.store(true);
    return true;
}

void Tabs::ConfigureGuest(const EdenDsmodHostExtensions* ext) {
    std::lock_guard lock{mutex};
    if (!ext || ext->version != EDEN_DSMOD_EXT_VERSION || ext->abi_hash != EDEN_DSMOD_EXT_HASH ||
        ext->struct_size < sizeof(*ext) || !ext->mailbox_address || ext->mailbox_size < 0x1000 ||
        !ext->load_u32 || !ext->load_u64 || !ext->store_u32 || !ext->store_u64) {
        gear_mb.Configure({}); // absent (2.7.6, or a host build too old for the guest mailbox)
        return;
    }
    void* const userdata = ext->userdata;
    const auto load_u32 = ext->load_u32;
    const auto load_u64 = ext->load_u64;
    const auto store_u32 = ext->store_u32;
    const auto store_u64 = ext->store_u64;
    gear_mb.Configure({ext->mailbox_address,
                       [userdata, load_u32](u32 off, u32* v) { return load_u32(userdata, off, v) != 0; },
                       [userdata, store_u32](u32 off, u32 v) { return store_u32(userdata, off, v) != 0; },
                       [userdata, store_u64](u32 off, u64 v) { return store_u64(userdata, off, v) != 0; },
                       [userdata, load_u64](u32 off, u64* v) { return load_u64(userdata, off, v) != 0; }});
}

bool Tabs::GearRefusal(bool busy, bool supported) {
    const char* why = busy                   ? "Busy"
                      : !supported           ? "Not available on this game version"
                      : !alive               ? "Not available right now"
                      : !menu_none           ? "Close the open menu first"
                                             : nullptr;
    if (why)
        refusal = why, refusal_at = now;
    return why != nullptr;
}

bool Tabs::Skills(std::array<SkillData, 6>& out, PotionData* potion) {
    if (potion) *potion = {};
    std::lock_guard lock{mutex};
    if (!have || !hero.skills_valid) {
        out = {};
        return false;
    }
    out = hero.skills;
    if (potion) *potion = hero.potion;
    return true;
}

} // namespace d3hg
