// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_gear_drive.h"

#include <string_view>

namespace d3hg {
namespace {
constexpr u64 Key(std::string_view path) {
    u64 h = 0xCBF29CE484222325ull;
    for (char c : path) {
        const unsigned char b = static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        h = (h ^ b) * 0x100000001B3ull;
    }
    return h;
}
constexpr u64 KeyWindow = Key("Root.NormalLayer.ConsoleInventoryRing_main");
constexpr u64 KeyRing = Key("Root.NormalLayer.ConsoleInventoryRing_main.LayoutRoot.InventoryRing");
constexpr u64 KeyItemList = Key("Root.NormalLayer.ConsoleInventoryRing_main.LayoutRoot.InventoryList.ItemList");
static_assert(KeyWindow == 0x4939B92D4327A926ull, "UI key hash (live-verified window key)");

constexpr u64 GWindows = 0xB80, CtlFlags = 0x18, CtlKey = 0x30, RingHandler = 0x48;
constexpr u64 HCode = 0xF8, HRingFocus = 0xFC, HPos = 0x150, HLists = 0x278, HListStride = 0x18,
              HEquipped = 0xF30;
constexpr u64 ListCursor = 0x1E0, ListRows = 0x218;
constexpr u64 HandlerVtable277 = 0x10A5EE0; // .data offset; NCE adds __relocation_delta

// position k clockwise from the chest; code = (4 - k) mod 14 (handler +0xF8); pos = handler +0x150
constexpr RingSlot Ring[13] = {
    {1, 1, 3, 8},   {2, 0, 4, 7},   {3, 4, 0, 1},   {4, 11, 7, 0},  {5, 12, 6, 5},
    {6, 10, 8, 9},  {7, 8, 10, 11}, {8, 13, 5, 6},  {9, 9, 9, 10},  {10, 3, 1, 12},
    {11, 6, 12, 2}, {12, 5, 13, 4}, {13, 2, 2, 3}};

bool Heap(u64 p) {
    return p >= 0x1000000 && p < (u64{1} << 40);
}
struct Mem {
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
};
/// Control for `key` in the window map, 0 when absent; `shown` = flags bit 0x04.
u64 Control(const Mem& m, u64 g, u64 key, bool& shown) {
    shown = false;
    const u64 wm = m.Ptr(m.Ptr(g + GWindows));
    u32 mask = 0;
    u64 buckets = 0;
    if (!Heap(wm) || !m.Get(wm, mask) || !m.Get(wm + 0x10, buckets) || mask >= 0x100000 || !Heap(buckets))
        return 0;
    u32 hsh = 0x811C9DC5u;
    for (int b = 0; b < 4; ++b)
        hsh = (hsh ^ ((key >> (8 * b)) & 0xFF)) * 0x01000193u;
    u64 n = m.Ptr(buckets + 8 * u64(hsh & mask));
    for (int guard = 0; Heap(n) && guard < 64; ++guard) {
        u64 node[3];
        if (!m.Read(n, node, sizeof node))
            return 0;
        if (node[1] == key) {
            u64 echo = 0;
            unsigned char f = 0;
            if (!Heap(node[2]) || !m.Get(node[2] + CtlKey, echo) || echo != key || !m.Get(node[2] + CtlFlags, f))
                return 0;
            shown = (f & 0x04) != 0;
            return node[2];
        }
        n = node[0];
    }
    return 0;
}
} // namespace

const RingSlot* RingFor(int slot) {
    for (const auto& r : Ring)
        if (r.slot == slot)
            return &r;
    return nullptr;
}

bool ReadInvMenu(const EdenDsmodHostApi& h, const d3_build::Build& build, u64 g, InvMenu& out) {
    out = {};
    if (std::string_view{build.name} != "k277" || !Heap(g))
        return false;
    Mem m{h};
    bool win = false, ring_shown = false, list_shown = false;
    const u64 w = Control(m, g, KeyWindow, win);
    const u64 ring = Control(m, g, KeyRing, ring_shown);
    if (!w || !win || !ring || !ring_shown)
        return true; // readable: the inventory is not open
    const u64 hd = m.Ptr(ring + RingHandler);
    std::int64_t delta = h.get_i64 ? h.get_i64(h.userdata, "__relocation_delta", 0) : 0;
    const u64 vt_want = h.main_base + HandlerVtable277 + static_cast<u64>(delta);
    u64 vt = 0;
    if (!Heap(hd) || !m.Get(hd, vt) || vt != vt_want)
        return false;
    u32 focus = 0;
    if (!m.Get(hd + HCode, out.code) || !m.Get(hd + HRingFocus, focus) || !m.Get(hd + HPos, out.pos) ||
        !m.Get(hd + HEquipped, out.equipped) || out.code >= 14 || out.pos >= 14 || focus > 1)
        return false;
    out.ring_focus = focus == 1;
    u64 vec[3];
    if (!m.Read(hd + HLists + HListStride * out.pos, vec, sizeof vec) || vec[1] > 64 || vec[1] > vec[2] ||
        (vec[1] && !Heap(vec[0])))
        return false;
    out.list.resize(vec[1]);
    if (vec[1] && !m.Read(vec[0], out.list.data(), 4 * vec[1]))
        return false;
    const u64 il = Control(m, g, KeyItemList, list_shown);
    std::int32_t cur = -1;
    if (il && m.Get(il + ListCursor, cur) && m.Get(il + ListRows, out.rows) && out.rows <= 64) {
        out.list_ok = true;
        out.cursor = cur;
    }
    out.open = true;
    return true;
}

bool GearDrive::Supported() const {
    return std::string_view{build.name} == "k277";
}

bool GearDrive::Start(u32 item, u32 item_gbid, int sl, u32 equipped, u64 now) {
    if (Busy() || !Supported() || !RingFor(sl) || !item)
        return false;
    acd = item, gbid = item_gbid, slot = sl, equipped_acd = equipped;
    row = -1, retries = steps = stable = b_presses = 0;
    acted = false, want_read = false, failure = nullptr;
    result = Result::Running;
    status = "Dropping...";
    started = phase_at = now;
    phase = Phase::Open;
    Press(BtnPlus, now);
    return true;
}

void GearDrive::Press(int b, u64 now) {
    pending = b;
    sent = now; // re-stamped when the press is actually emitted
}

int GearDrive::Emit(u64 now) {
    if (pending < 0 || now % 2 != 0)
        return -1;
    const int b = pending;
    pending = -1;
    sent = now;
    return b;
}

void GearDrive::Fail(const char* why, u64 now) {
    if (!failure)
        failure = why;
    if (can_close && phase != Phase::Verify) {
        phase = Phase::Close, phase_at = now; // leave our own menu the way it was found
        return;
    }
    status = failure;
    result = Result::Failed;
    phase = Phase::Idle;
    finished_at = now;
}

bool GearDrive::Read(const EdenDsmodHostApi& h, u64 g) {
    return ReadInvMenu(h, build, g, menu);
}

} // namespace d3hg
