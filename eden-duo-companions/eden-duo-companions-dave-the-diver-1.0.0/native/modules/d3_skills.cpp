// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_skills.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "d3_hg_draw.h"

namespace d3_skills {
namespace {
using namespace d3hg::draw;
constexpr d3hg::u32 Description = 0xC7B377FF;

// The ring art (Console_HUD_SinglePlayerSkillSlot) is a solid disc, not a hollow frame: measured
// opaque to radius ~21.5 of its own 46 px canvas (measured on the decoded atlas frame), i.e.
// ~172-176 px at RenderRail's own 4x
// BlitC scale. The native console HUD never lets the icon fill that whole disc: the icon sits in a
// dark band well inside the bronze rim (G3 revise 2026-10-16: the icon was 168 px over a 184 px
// ring, covering the rim on all six skills). 128 px leaves the ~12 px dark band and the rim visible
// all round, like the native slot.
constexpr int RingIconPx = 128;

// Scales the full native icon frame (padding kept, never ink-trimmed first) by the largest integer
// factor that still fits inside `max_px` square, nearest-neighbour: every rail icon gets the same
// sharp pixel scale instead of IconFit's per-icon fractional resize (G3 revise: ink-trimmed icons
// nearest-scaled ~2.6x gave uneven, blocky pixels). The native icon frames are 64 px, so this is a
// clean 2x into the new 128 px RingIconPx; a differently sized frame still gets the largest integer
// scale that fits, never a fractional one.
Image IntegerFit(const Image& src, int max_px) {
    if (src.Empty() || src.w <= 0 || src.h <= 0)
        return {};
    const int factor = std::min(max_px / src.w, max_px / src.h);
    if (factor >= 1) {
        const int w = src.w * factor, h = src.h * factor;
        Image out(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                std::copy_n(src.At(x / factor, y / factor), 4, out.At(x, y));
        return out;
    }
    // A frame already bigger than max_px (a larger item icon): no integer upscale fits, so fall
    // back to a fractional nearest fit -- still the whole untrimmed frame, never ink-trimmed.
    const double fit = std::min(double(max_px) / src.w, double(max_px) / src.h);
    const int w = std::max(1, int(src.w * fit)), h = std::max(1, int(src.h * fit));
    Image out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::copy_n(src.At(x * src.w / w, y * src.h / h), 4, out.At(x, y));
    return out;
}

std::string Trim(std::string s) {
    auto whitespace = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && whitespace(s.back()))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && whitespace(s[start]))
        ++start;
    return s.substr(start);
}

struct Info {
    std::string name, rune, body, rune_effect;
};

Info Resolve(d3_assets::Assets& a, const EdenDsmodHostApi* h, d3hg::Text& t, const Skill& skill) {
    Info info;
    if (!skill.available) {
        info.name = "—";
        info.body = "—";
        return info;
    }
    if (!skill.power || skill.power == 0xFFFFFFFFu) {
        info.name = d3hg::StripTags(t.Get(a, h, "ConsoleSkills", 0x1403fdbe)); // None Assigned
        if (info.name.empty())
            info.name = "—";
        return info;
    }
    const std::string internal = t.PowerName(a, h, skill.power);
    if (!internal.empty()) {
        info.name = d3hg::StripTags(t.Get(a, h, "Powers", internal + "_name"));
        // Native simple descriptions give the useful qualitative effect without requiring the
        // game's formula evaluator. Never manufacture a damage/cost/cooldown number here.
        info.body = SafeDescription(t.Get(a, h, "Powers", internal + "_simple_desc"));
        if (skill.rune_valid && skill.rune >= 0 && skill.rune < 5) {
            const std::string key = std::string{char('A' + skill.rune)} + "#" + internal;
            info.rune = d3hg::StripTags(t.Get(a, h, "AttributeDescriptions", "NameRune_" + key));
            info.rune_effect = SafeDescription(t.Get(a, h, "AttributeDescriptions", "Simple_Rune_" + key));
        }
    }
    if (info.name.empty())
        info.name = "—";
    if (info.body.empty())
        info.body = "—";
    return info;
}

std::vector<std::string> BodyLines(const d3_font::Font& f, const std::string& body, int width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < body.size()) {
        const size_t end = body.find("\n\n", start);
        const std::string paragraph = body.substr(start, end == std::string::npos ? end : end - start);
        auto wrapped = Wrap(f, 30, paragraph, width, 96);
        lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        if (end == std::string::npos)
            break;
        start = end + 2;
    }
    return lines;
}

struct BodyLine {
    std::string text;
    d3hg::u32 colour;
    int gap = 0;
};

std::vector<BodyLine> DescriptionLines(const d3_font::Font& f, const Info& info, int width) {
    std::vector<BodyLine> lines;
    for (auto& text : BodyLines(f, info.body, width))
        lines.push_back({std::move(text), Description});
    // Match the native Skills hierarchy: base effect, then the equipped rune's
    // name and grey modification. Keep both sections in the same scroll stream.
    bool first = true;
    for (auto& text : Wrap(f, 30, info.rune, width, 96)) {
        lines.push_back({std::move(text), Gold, first ? 10 : 0});
        first = false;
    }
    for (auto& text : BodyLines(f, info.rune_effect, width))
        lines.push_back({std::move(text), Grey});
    return lines;
}

void ScrollArrow(Image& out, d3_assets::Assets& a, const EdenDsmodHostApi* h, bool up, int x, int y) {
    Image arrow = a.Frame(h, "2DUIConsoleOptions", "Console_Waypoint_ScrollArrow");
    if (up)
        arrow = FlipV(arrow);
    BlitC(out, arrow, x, y);
}
} // namespace

bool ReadInput(const EdenDsmodHostApi& h, const d3_build::Build& build, std::uint64_t player,
              Input& out) {
    out = {};
    if (!player || !h.is_mapped || !h.read_memory || !build.skills_input_verified) return false;
    const auto read = [&](std::uint64_t address, auto& value) {
        return address >= 0x10000 && h.is_mapped(h.userdata, address, sizeof value) &&
               h.read_memory(h.userdata, address, &value, sizeof value);
    };
    const auto pin = [&](std::uint64_t offset, const auto& expected) {
        auto code = expected;
        return read(h.main_base + offset, code) && code == expected;
    };
    // Console physical bit write, account binding lookup and static default table: bytes of the
    // already-loaded game binary that cannot change at runtime once they have matched once, so a
    // successful check is cached per Build (like d3_reader::Reader / d3hg::Reader's own roots)
    // instead of re-verifying ~6 guest reads every tick; a failed check keeps re-trying.
    static const d3_build::Build* cached_build = nullptr;
    static bool cached_ok = false;
    if (cached_build != &build || !cached_ok) {
        cached_ok = pin(build.skill_physwrite.off, build.skill_physwrite.words) &&
                    pin(build.skill_defaultprofile.off, build.skill_defaultprofile.words) &&
                    pin(build.skill_bindinglookup1.off, build.skill_bindinglookup1.words) &&
                    pin(build.skill_bindinglookup2.off, build.skill_bindinglookup2.words) &&
                    pin(build.skill_potiondefault.off, build.skill_potiondefault.words) &&
                    pin(build.skill_defaulttable_off, build.skill_defaulttable);
        cached_build = &build;
    }
    if (!cached_ok) return false;
    std::int32_t key;
    if (!read(player + 0xDB84, key)) return false;
    if (key == -1) {
        out.bindings = {0,7,2,3,1,6,5};
    } else {
        // The account-binding lookup chain (manager -> count -> index -> entry -> bucket -> items
        // -> profile -> bindings, ~8 reads) resolves the same profile's bindings every tick as long
        // as `key` itself (the active profile index) has not changed, same spirit as the opcode-pin
        // cache above: re-walk only on a `key` change, not every tick.
        static std::int32_t cached_key = -2; // never a real key nor the key==-1 default handled above
        static std::array<std::int32_t, 7> cached_bindings{};
        static bool cached_bindings_ok = false;
        if (key != cached_key) {
            std::uint64_t manager_ref, manager, count_ref, index_ref, indices, bucket, items, profile;
            std::uint32_t count;
            std::int32_t entry;
            std::array<std::int32_t, 7> bindings{};
            cached_bindings_ok = key >= 0 && read(h.main_base + build.skill_manager_ref_off, manager_ref) &&
                read(manager_ref, manager) &&
                read(h.main_base + build.skill_count_ref_off, count_ref) && read(count_ref, count) &&
                count <= 4096 && std::uint32_t(key) < count &&
                read(h.main_base + build.skill_index_ref_off, index_ref) && read(index_ref, indices) &&
                read(indices + std::uint32_t(key) * 4ull, entry) && entry != -1 &&
                read(manager + 0x20, bucket) && read(bucket + 0x120, items) &&
                read(items + (std::uint32_t(entry) & 0x3FFFFu) * 16ull + 8, profile) &&
                read(profile + 0x194, bindings);
            if (cached_bindings_ok)
                cached_bindings = bindings;
            cached_key = key;
        }
        if (!cached_bindings_ok) return false;
        out.bindings = cached_bindings;
    }
    for (auto binding : out.bindings)
        if (binding < -1 || binding >= 8) return false;
    if (!read(player + 0xDDD8, out.physical)) return false;
    // Native lookup returns the first slot matching a physical button, even for duplicates.
    std::uint32_t consumed = 0;
    for (size_t slot = 0; slot < 6; ++slot) {
        const int bit = out.bindings[slot];
        if (bit < 0 || (consumed & (1u << bit))) continue;
        consumed |= 1u << bit;
        out.effective[slot] = bit;
        if (out.physical & (1u << bit)) out.slots_down |= 1u << slot;
    }
    // The game's potion predicate is independent from its first-matching skill lookup.
    const int potion_bit = out.bindings[6];
    out.effective[6] = potion_bit;
    if (potion_bit >= 0 && (out.physical & (1u << potion_bit))) out.slots_down |= 1u << 6;
    return true;
}

std::string SafeDescription(std::string_view text) {
    std::string out;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = text.find("\n\n", start);
        // Split before StripTags, which intentionally flattens whitespace for the existing
        // item tooltips. Skill paragraph boundaries distinguish costs from qualitative text.
        std::string paragraph = Trim(d3hg::StripTags(text.substr(start, end == std::string::npos ? end : end - start)));
        // Unknown dynamic tokens, formula expressions and plural-format syntax make the entire
        // paragraph unavailable. Removing just the token would leave a misleading sentence.
        const bool unresolved = paragraph.find_first_of("{}[]|") != std::string::npos ||
                                paragraph.find("%s") != std::string::npos ||
                                paragraph.find("%d") != std::string::npos;
        if (!paragraph.empty() && !unresolved) {
            if (!out.empty())
                out += "\n\n";
            out += paragraph;
        }
        if (end == std::string::npos)
            break;
        start = end + 2;
    }
    return out;
}

Image RenderRail(d3_assets::Assets& a, const EdenDsmodHostApi* h, d3hg::Text& t,
                 const std::array<Skill, 6>& skills, bool valid, int selected, const Potion& potion,
                 const std::array<std::int32_t, 7>& effective) {
    Image out(PageW, PageH);
    if (!valid)
        return out;
    const Image ring = a.Frame(h, "2DUIConsole_HUD", "Console_HUD_SinglePlayerSkillSlot");
    const Image selected_ring = a.Frame(h, "2DUIConsole_HUD", "Console_HUD_SinglePlayerSkillSlotSelected");
    // Native console HUD's own "nothing bound here" look: a flat, border-less disc (atlas
    // 2DUIConsole_HUD @ 218,181 46x46, unnamed in the survey but sitting right beside the named
    // slot/cover/selected frames at the same size) instead of the normal ringed slot. A
    // not-yet-unlocked slot (e.g. the secondary/ZR slot on a fresh low-level hero) and any slot
    // with nothing bound both read as this blank disc: no icon, no dash and no button glyph.
    const Image locked = a.Frame(h, "2DUIConsole_HUD", "#860b67e2");
    for (int visual = 0; visual < 7; ++visual) {
        const int slot = RailSlots[visual];
        const int cx = RailX + SlotW * visual + SlotW / 2;
        const int cy = RailY + RailH / 2;
        const bool is_potion = slot == 6;
        const bool available = is_potion ? potion.available : SkillUsable(skills[slot]);
        if (!available) {
            BlitC(out, locked, cx, cy, 4);
            continue;
        }
        const d3hg::u32 icon_hash = is_potion ? potion.icon : skills[slot].icon;
        const bool is_selected = slot == selected && !selected_ring.Empty();
        // Normal ring first, then the icon over it, then (below) the selected glow on top of both:
        // drawing the glow last keeps its own 128-184 px band visible on the selected slot instead
        // of being buried under the icon (G3 revise).
        BlitC(out, ring, cx, cy, 4);
        if (icon_hash) {
            // Full native frame (no ink trim) at the largest integer scale that fits RingIconPx: the
            // native 64 px frame's own padding around the icon is what leaves the dark band and
            // bronze rim showing, same as the native HUD (G3 revise; was an upscaled ink-trimmed
            // crop that filled the ring edge to edge).
            Image icon = IntegerFit(t.Icon(a, h, icon_hash, !is_potion), RingIconPx);
            // The potion bottle is drawn whole (cap and gems inside the rim, like the native R
            // slot); only the round skill icons are clipped to the ring's circle.
            if (!is_potion)
                ClipCircle(icon);
            BlitC(out, icon, cx, cy);
        } else {
            TxtMid(out, a.sans, 45, cx, cy, "—", Grey, 1);
        }
        if (is_selected)
            BlitC(out, selected_ring, cx, cy, 4);
        // the native HUD prints the slot's current button under each ring; same glyph art
        const int bit = effective[slot];
        if (bit >= 0 && bit < static_cast<int>(GlyphFrames.size())) {
            const Image glyph = a.Frame(h, "2DUIFontIcons_NX64", "[" + std::to_string(GlyphFrames[bit]) + "]");
            // IconFit trims and fits the glyph's own ink -- the whole pill/bar badge, not just the
            // printed letter. The face buttons' badge (bits 0-3, a round disc) is already native
            // size inside GlyphPx x GlyphPx and prints a 22 px letter; the shoulder bars' badge
            // (bits 4-7, a flat wide bar) is already taller than a 22 px box on its own (measured
            // ink ~24-29 px), so fitting it to 22 px tall shrank it further, leaving an even smaller
            // letter (G3 revise: first attempt measured 10 px, worse than the original 11-13).
            // The shoulder badge's letter is a fixed ~45 % of its own badge height, so the badge
            // itself has to land near 48 px tall for the letter inside it to come out at 22 px: a
            // clean 2x of R's own 24 px badge, ~1.7x of ZL/ZR's 29 px one, averaging to about the
            // 1.9x bump the critique asked for. The 160 px width bound stays well clear of the
            // scaled badge's own width (up to ~122 px) so it never becomes the binding side.
            const bool shoulder = bit >= 4;
            const Image fit = shoulder ? IconFit(glyph, 160, 48, true)
                                        : IconFit(glyph, GlyphPx * 8 / 5, GlyphPx);
            if (!fit.Empty())
                BlitC(out, fit, cx, RailY + RailH + 32);
        }
    }
    return out;
}

Image RenderCard(d3_assets::Assets& a, const EdenDsmodHostApi* h, d3hg::Text& t,
                 const std::array<Skill, 6>& skills, int selected, int scroll, const Potion& potion) {
    if (selected < 0 || selected > 6)
        return {};
    // drawn in rail-page coordinates, then cut to the card box the manifest places
    Image out(PageW, PageH);
    const bool is_potion = selected == 6;
    Info info;
    const d3hg::u32 icon_hash = is_potion ? potion.icon : skills[selected].icon;
    if (is_potion) {
        info.name = d3hg::StripTags(t.Get(a, h, "Items", potion.gb_name));
        info.body = d3hg::StripTags(t.Get(a, h, "ItemTypeNames", potion.itype_name));
        if (info.name.empty()) info.name = "—";
        // Native healing/cooldown and legendary bonus formulas are not evaluated here.
    } else {
        info = Resolve(a, h, t, skills[selected]);
    }
    const auto names = Wrap(a.sans, 36, info.name, CardW - 132, 2);
    const int name_step = LineH(a.sans, 36);
    const int body_step = LineH(a.sans, 30);
    const int name_h = std::max(68, static_cast<int>(names.size()) * name_step);
    const int body_top = 24 + name_h + 12;
    const auto body = DescriptionLines(a.sans, info, CardW - 60);
    // The ornament is excluded from the body and scroll indicators have their own band.
    const int capacity = CardMaxH - body_top - 84;
    int limit = static_cast<int>(body.size());
    int tail_h = 0;
    while (limit > 0) {
        const BodyLine& line = body[limit - 1];
        if (tail_h + body_step + line.gap > capacity)
            break;
        tail_h += body_step + line.gap;
        --limit;
    }
    scroll = std::clamp(scroll, 0, limit);
    int shown = 0, shown_h = 0;
    while (scroll + shown < static_cast<int>(body.size())) {
        const BodyLine& line = body[scroll + shown];
        if (shown_h + body_step + line.gap > capacity)
            break;
        shown_h += body_step + line.gap;
        ++shown;
    }
    const int height = std::clamp(body_top + shown_h + 84, 230, CardMaxH);
    const int top = CardBottom - height;
    // A header-free native Map window accommodates the whole wrapped name beside its icon.
    // The narrow menu title band would cut across two-line names.
    d3_img::Blit(out, Ringside(a, h, CardW, height), CardX, top);
    if (icon_hash)
        BlitC(out, IconFit(t.Icon(a, h, icon_hash, !is_potion), 64, 64), CardX + 60, top + 58);
    int y = BlockTop(a.sans, 36, names, name_step, top + 58);
    for (const auto& line : names) {
        Txt(out, a.sans, 36, CardX + 106, y, line, Gold);
        y += name_step;
    }
    y = top + body_top;
    for (int row = 0; row < shown; ++row) {
        const BodyLine& line = body[scroll + row];
        y += line.gap;
        Txt(out, a.sans, 30, CardX + 30, y, line.text, line.colour);
        y += body_step;
    }
    if (scroll > 0)
        ScrollArrow(out, a, h, true, CardX + CardW / 2 - 44, top + height - 62);
    if (scroll < limit)
        ScrollArrow(out, a, h, false, CardX + CardW / 2 + 44, top + height - 62);
    return d3_img::Crop(out, CardX, CardBottom - CardMaxH, CardW, CardMaxH);
}
} // namespace d3_skills
