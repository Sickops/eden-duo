// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Gear tab (owner request 2026-10-16, "like the main screen"): left, the 13 equipment slots as a
// vertical list (native ring slot icon, the slot name from ConsoleInventory, the equipped item's
// quality strip and icon); middle, the selected slot's EQUIPPED card and the AVAILABLE backpack
// items for that slot (native cards, scrollable, with the armor / damage difference to the equipped
// item) above the Equip / Drop buttons; right, the tooltip card of the selected item. Equip and
// Drop are carried out by the native inventory menu (d3_gear_drive); a drop needs a second tap.
// The 10 x 6 backpack grid stays as a secondary, read-only view (tap the inventory count).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "d3_hg_mem.h"
#include "d3_hg_text.h"
#include "d3_img.h"

namespace d3hg {

/// Slot list order (top to bottom): inventory slot ids (Head, Shoulders, Neck, Chest, Hands, Wrists,
/// Waist, Legs, Feet, Main Hand, Off Hand, Ring, Ring).
constexpr int GearSlotOrder[13] = {1, 8, 13, 2, 5, 10, 6, 9, 7, 4, 3, 11, 12};
constexpr int GearColW = 312;
constexpr int GearRowY = 62;
constexpr int GearRowH = 62;
constexpr int GearFootY = 880;
constexpr int GearMidX = 312;
constexpr int GearMidW = 452;
constexpr int GearCardX = 332;
constexpr int GearCardW = 412;
constexpr int GearCardH = 115;
constexpr int GearEqY = 66;
// UNEQUIP sits right under the equipped card, at the bottom of the equipped pane (owner request
// 2026-10-17): a slightly shorter button than the main Equip/Drop row below, same art/caption
// style. AVAILABLE drops to 4 visible rows (scroll / "more" indicator cover the rest) to make room.
constexpr int GearUnequipY = GearEqY + GearCardH + 14;
constexpr int GearUnequipH = 46;
constexpr int GearAvailHeadY = GearUnequipY + GearUnequipH + 16;
constexpr int GearAvailY = GearAvailHeadY + 28;
// Step = GearCardH (115) + a 12 px gap between AVAILABLE cards (LEGIBILITY.md dense-list >= 12 px
// gap; was 7).
constexpr int GearAvailStep = 127;
constexpr int GearAvailRows = 4;
constexpr int GearBtnY = 862;
constexpr int GearBtnH = 62;
constexpr int GearBtnW = 200;
constexpr int GearBtnGap = 12;
// Cancel / "Drop?" (the confirm state) must never share a screen rect with Equip / Drop (the
// normal state): a tap landing on a stale rect across a state flip must physically miss the other
// action, not just be refused by a gate that may itself be mid-update (owner bug 2026-10-17: a
// Cancel tap next to Goldskin's Drop confirm equipped Goldskin and dropped Inarius's Conviction).
// 782..844 sits clear of the AVAILABLE cards above it (the last one ends at GearAvailY +
// (GearAvailRows - 1) * GearAvailStep + GearCardH = 781) and the normal button row below it
// (862..924).
constexpr int GearConfirmBtnY = 782;
constexpr int GearTipX = 764;
// secondary backpack view: 10 x 6 cells
constexpr int GearBagX = 26;
constexpr int GearBagY = 66;
constexpr int GearBagCW = 71;
constexpr int GearBagCH = 136;

struct GearView {
    bool valid = false;
    bool bag = false;        // secondary view: the backpack grid
    int slot = 4;            // selected equipment slot (1..13)
    int sel = -1;            // index into items of the item whose tooltip shows (-1 = none)
    int equipped = -1;       // index into items of the item equipped in `slot`
    std::vector<int> avail;  // indices into items: backpack items for `slot`, native list order rules
    int scroll = 0;          // first visible AVAILABLE row
    bool offhand_blocked = false; // a two-handed main hand greys the off hand (native)
    bool can_equip = false, can_unequip = false, can_drop = false, confirm = false, busy = false;
    std::string notice;      // short native-style notice (drive progress / outcome / refusal)
    bool notice_bad = false;
    std::shared_ptr<const std::vector<ItemData>> items; // equipped (1..13) and backpack (0); shared, immutable
    int backpack = 0, backpack_max = 60;
    std::string gold, shards, shard_cap;
    bool operator==(const GearView&) const = default;
};

/// Item index for a card (slot id) or a backpack cell (x, y); -1 when empty.
/// Hero class bit of the ItemTypes class mask from the hero's portrait actor (0 = unknown).
u32 GearClassBit(u32 portrait_actor);
/// Whether a backpack item belongs in AVAILABLE for `slot`: its item type lists the slot and the
/// hero's class; off hand: no two-handed weapons, one-handed weapons only for the dual-wielding
/// classes, nothing while a two-handed main hand is equipped (Crusader excepted). The native list
/// remains the authority: a drive refuses an item the game does not list.
bool GearCompatible(const ItemData& it, int slot, u32 class_bit, bool main_two_handed);

int GearFindSlot(const std::vector<ItemData>& items, int slot);
int GearFindCell(const std::vector<ItemData>& items, int x, int y);

d3_img::Image GearPage(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
                       const GearView& v);

} // namespace d3hg
