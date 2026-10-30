// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Skill rail and information card on the explored Map. The caller supplies the native equipped
// records, bindings and selection; this renderer never injects inputs or casts a power (the
// optional tap-to-use mode presses buttons through manifest button actions, not the module).
#pragma once

#include <array>
#include <string>
#include <string_view>

#include "d3_build.h"
#include "d3_hg_text.h"

namespace d3_skills {
using Skill = d3hg::SkillData;
using Potion = d3hg::PotionData;

// Layout shared with tools/d3/gen_manifest.py, which parses these lines (keep them one per name).
// Rings drawn at 4x their 46 px native art (184 px); glyphs doubled to match (owner: "bottom 7
// usable button 2x bigger").
constexpr int RailX = 25, RailY = 720, SlotW = 170, RailH = 190;
// Ring + the button glyph under it, down to (but no longer overlapping) the nav bar's own now-taller
// tap rects: NavHitH (d3_view.h) extends upward to y 963 (NavCellY + NavCellH - NavHitH), so this
// stops exactly there (720 + 243 = 963) instead of the old 995.
constexpr int RailHitH = 243;
// Glyph size; centred 32 px below the ring band. The face-button badge's printed letter (Y/X/B/A)
// measured 21 px cap height at 60 -- 1 px under LEGIBILITY.md's 22 px essential floor -- so this is
// scaled up ~5% (the letter is a fixed proportion of the badge, so it scales with it); the shoulder
// badges (ZL/ZR/R) already clear 22 px through their own, separate 160x48 fit and are unaffected.
constexpr int GlyphPx = 63;
// Native HUD visual sequence: potion, primary, then the four face/shoulder skills, secondary.
constexpr std::array<int, 7> RailSlots{6, 0, 2, 3, 4, 5, 1};
constexpr int CardX = 20, CardW = 520, CardBottom = 710, CardMaxH = 440;

// Physical binding bit -> Switch button (verified live: the native HUD letters under the default
// profile, and traced B = bit 1, ZL = bit 6). gen_manifest.py names its button actions after it.
constexpr std::array<const char*, 8> ButtonNames{"A", "B", "Y", "X", "L", "R", "ZL", "ZR"};
// The same buttons' round prompt glyphs in Textures\2DUIFontIcons_NX64 (frame index).
constexpr std::array<int, 8> GlyphFrames{26, 25, 27, 28, 29, 30, 31, 32};

struct Input {
    std::uint32_t physical = 0, slots_down = 0;
    std::array<std::int32_t, 7> bindings{}; // six skills, then independent potion binding
    /// The bit each slot actually answers to (-1 none): the native lookup takes the first skill
    /// slot bound to a button, so a duplicate binding later in the list never fires.
    std::array<std::int32_t, 7> effective{-1, -1, -1, -1, -1, -1, -1};
};
/// Guarded console input and current account bindings; false never assumes a default profile
/// (including when `build.skills_input_verified` is false: an unconfirmed build fails closed).
bool ReadInput(const EdenDsmodHostApi& host, const d3_build::Build& build, std::uint64_t player,
              Input& out);

/// True when a skill slot has a real power bound (not just an unlocked-but-empty slot); shared by
/// Inspector::Available and RenderRail so the rail's "nothing usable here" look never drifts from
/// what inspection will actually accept.
inline bool SkillUsable(const Skill& skill) {
    return skill.available && skill.power && skill.power != 0xFFFFFFFFu;
}

/// Selection is independent of the game's cast result and of the game's own input (no auto-open):
/// the inspector only ever opens from Tap (manifest tap-to-inspect / hold-to-inspect), never from a
/// button edge, so a physical press or a tap-to-use tap never pops the card on its own.
struct Inspector {
    std::array<Skill, 6> skills{};
    Potion potion;
    bool enabled = false, pinned = false;
    int selected = -1, scroll = 0;
    std::uint32_t previous_down = 0, world = 0;
    std::uint64_t player = 0;
    void Clear() { selected = -1; scroll = 0; pinned = false; }
    void Update(const std::array<Skill, 6>& next, bool can, std::uint32_t next_world,
                std::uint64_t next_player, std::uint32_t down, const Potion& next_potion = {}) {
        const bool reset = can != enabled || next_world != world || next_player != player || skills != next || potion != next_potion;
        previous_down = down;
        skills = next; potion = next_potion; enabled = can; world = next_world; player = next_player;
        if (reset || !can) Clear();
    }
    bool Available(int slot) const {
        if (slot == 6) return potion.available;
        return slot >= 0 && slot < 6 && SkillUsable(skills[slot]);
    }
    bool Tap(int slot) {
        if (!enabled || !Available(slot)) return false;
        if (pinned && selected == slot) Clear();
        else { selected = slot; scroll = 0; pinned = true; }
        return true;
    }
};

/// Keeps complete native paragraphs only when every dynamic expression has been resolved.
/// In particular, a formula-bearing Cost/Generate prefix must never leak into the UI.
std::string SafeDescription(std::string_view text);

/// Transparent 1240 x 995 rail overlay: the seven native rings in HUD order, the selected ring
/// highlighted, and each slot's current button glyph under it (effective bindings, -1 = none).
d3_img::Image RenderRail(d3_assets::Assets& assets, const EdenDsmodHostApi* host, d3hg::Text& text,
                         const std::array<Skill, 6>& skills, bool valid, int selected,
                         const Potion& potion, const std::array<std::int32_t, 7>& effective);
/// The information card for `selected`, CardW x CardMaxH, bottom-aligned (drawn at
/// CardX, CardBottom - CardMaxH). Body scroll uses lines. Empty when nothing is selected.
d3_img::Image RenderCard(d3_assets::Assets& assets, const EdenDsmodHostApi* host, d3hg::Text& text,
                         const std::array<Skill, 6>& skills, int selected, int scroll,
                         const Potion& potion);
} // namespace d3_skills
