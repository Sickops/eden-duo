// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_page_gear.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "d3_hg_draw.h"

namespace d3hg {
namespace {
using namespace draw;

const char* const ElemName[7] = {"Physical", "Fire", "Lightning", "Cold", "Poison", "Arcane", "Holy"};

enum class Q { Normal, Magic, Rare, Legendary, Set };
Q Quality(const AttrMeta& m, const ItemData& it) {
    const int q = static_cast<int>(Attr(m, it.attrs, "Item_Quality_Level", NoParam, 1));
    if (q >= 9)
        return it.set ? Q::Set : Q::Legendary;
    if (q >= 6)
        return Q::Rare;
    if (q >= 3)
        return Q::Magic;
    return Q::Normal;
}
u32 QColour(Q q) {
    switch (q) {
    case Q::Magic: return Magic;
    case Q::Rare: return Rare;
    case Q::Legendary: return Legend;
    case Q::Set: return SetGreen;
    default: return White;
    }
}
const char* QStrip(Q q) {
    switch (q) {
    case Q::Magic: return "Console_ItemQualityMagic";
    case Q::Rare: return "Console_ItemQualityRare";
    case Q::Legendary: return "Console_ItemQualityLegendary";
    case Q::Set: return "#5fa4fdd6";
    default: return "Console_ItemQualityCommon";
    }
}

std::string ItemName(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const ItemData& it) {
    // rare items carry their generated name on the ACD: one affix list entry plus one entry of the
    // item's own prefix or suffix list (see ItemData::rare_*), as the native tooltip ("Sage's Plate")
    if (it.rare_list && it.rare_flag <= 1) {
        std::string ln;
        const std::string affix = StripTags(t.ListEntry(a, h, it.rare_list, it.rare_a, &ln));
        if (ln.rfind("RareNameStrings_", 0) == 0 && !affix.empty()) {
            const std::string own = StripTags(t.ListEntry(a, h, it.rare_flag == 0 ? it.rare_suffix : it.rare_prefix, it.rare_b));
            if (!own.empty())
                return it.rare_flag == 0 ? affix + " " + own : own + " " + affix;
        }
    }
    std::string n = StripTags(t.Get(a, h, "Items", std::string_view{it.gb_name}));
    return n.empty() ? it.gb_name : n;
}

// ConsoleInventory slot labels (Head, Shoulders, Neck, Chest, Hands, Wrists, Belt, Legs, Feet, Ring,
// Main Hand, Off Hand) and the native empty-slot ring icons
u32 SlotLabel(int slot) {
    switch (slot) {
    case 1: return 0x293746;
    case 2: return 0x4f72e17;
    case 3: return 0x52d48fe0;
    case 4: return 0x5da0e44a;
    case 5: return 0x54df64e;
    case 6: return 0x25ed07;
    case 7: return 0x281da4;
    case 8: return 0xea6f73e6;
    case 9: return 0x2b682b;
    case 10: return 0x666a4f9;
    case 11: case 12: return 0x2ec450;
    case 13: return 0x2c8061;
    default: return 0;
    }
}
const char* SlotIcon(int slot) {
    switch (slot) {
    case 1: return "Console_EquippedInventoryRingSlotHelm";
    case 2: return "Console_EquippedInventoryRingSlotChest";
    case 3: return "Console_EquippedInventoryRingSlotWeaponOffHandLarge";
    case 4: return "Console_EquippedInventoryRingSlotWeaponMainHandLarge";
    case 5: return "Console_EquippedInventoryRingSlotHands";
    case 6: return "Console_EquippedInventoryRingSlotBelt";
    case 7: return "Console_EquippedInventoryRingSlotFeet";
    case 8: return "Console_EquippedInventoryRingSlotShoulder";
    case 9: return "Console_EquippedInventoryRingSlotLegs";
    case 10: return "Console_EquippedInventoryRingSlotWrist";
    case 11: case 12: return "Console_EquippedInventoryRingSlotRing";
    case 13: return "Console_EquippedInventoryRingSlotNeck";
    default: return "";
    }
}

/// Native item card (Console_EquippedInventorySlot, ancient/primal variants) with the quality strip,
/// the item icon and the name (sans 30, max 3 lines, ellipsis). Returns the card height.
int Card(Image& img, d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
         const ItemData* it, int slot, int x, int y, int w) {
    if (!it) {
        Image e = a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventoryEmptyList");
        e = d3_img::Crop(e, 0, 0, 230, 115);
        d3_img::Blit(img, FitW(e, w, 70, 200), x, y);
        Image ic = a.Frame(h, "2DUIConsoleIcons", SlotIcon(slot));
        BlitC(img, ic, x + 40, y + GearCardH / 2, 1, 150);
        TxtMid(img, a.sans, 30, x + 80, y + GearCardH / 2,
            StripTags(t.Get(a, h, "ConsoleInventory", SlotLabel(slot))), Grey);
        return GearCardH;
    }
    const int anc = static_cast<int>(Attr(m, it->attrs, "Ancient_Rank"));
    Image card = a.Frame(h, "2DUIConsoleInventory",
                         anc >= 2 ? "Console_PrimalInventorySlot"
                                  : anc == 1 ? "Console_AncientInventorySlot" : "Console_EquippedInventorySlot");
    if (card.Empty())
        card = a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlot");
    card = FitW(card, w, 90, 200);
    d3_img::Blit(img, card, x, y);
    const Q q = Quality(m, *it);
    Image strip = FitW(a.Frame(h, "2DUIConsoleItemQuality", QStrip(q)), 76, 14, 38);
    const int sx = x + 10, sy = y + (card.h - strip.h) / 2;
    d3_img::Blit(img, strip, sx, sy);
    Image ic = IconFit(t.Icon(a, h, it->icon, false), strip.w - 10, strip.h - 12);
    BlitC(img, ic, sx + strip.w / 2, sy + strip.h / 2);
    const int tx = sx + strip.w + 12, tw = card.w - (tx - x) - 12;
    const auto lines = Wrap(a.sans, 30, ItemName(a, t, h, *it), tw, 3);
    int yy = BlockTop(a.sans, 30, lines, 34, y + card.h / 2);
    for (const auto& l : lines)
        Txt(img, a.sans, 30, tx, yy, l, QColour(q)), yy += 34;
    return card.h;
}

bool Hidden(const std::string& n) {
    static const char* const prefix[] = {"Durability", "Seed", "Item_", "Requirement", "DyeType", "Loot_", "Post_",
                                         "Skill", "Sockets", "Ancient_Rank", "Set_Item_Count", "Jewel_Rank",
                                         "GemQuality", "IsCrafted", "ItemStack", "Picked_Up", "Console_Promo",
                                         "Cosmetic", "Enchanted", "Damage_Weapon_", "Damage_Min", "Damage_Delta",
                                         "Damage_Average", "Armor_Item", "Block_Amount", "Block_Chance_Item",
                                         "Attacks_Per_Second_Item", "Unequipped", "Season", "Transmog",
                                         "Attacks_Per_Second_Total", "Movement_Scalar_", "Power_Cooldown_Reduction_Percent_All_Capped"};
    if (n == "Damage_Weapon_Percent_All")
        return false;
    for (const char* p : prefix)
        if (n.rfind(p, 0) == 0)
            return true;
    static const char* const suffix[] = {"_Total", "_Subtotal", "SubTotal", "_MainHand", "_OffHand", "_CurrentHand",
                                         "_Proc_Damage", "_Proc_Duration"};
    for (const char* s : suffix)
        if (n.size() >= std::strlen(s) && n.compare(n.size() - std::strlen(s), std::string::npos, s) == 0)
            return true;
    return false;
}
// native tooltip groups: primary affixes, secondary affixes, skill damage, then the starred
// special affixes (area damage, movement speed) in the legendary-affix colour
int Rank(const std::string& n) {
    static const char* const order[] = {"Damage_Weapon_Percent_All", "Strength_Item", "Dexterity_Item",
                                        "Intelligence_Item", "Vitality_Item", "Attacks_Per_Second_Percent",
                                        "Attacks_Per_Second_Item_Percent", "Hitpoints_Max_Percent_Bonus_Item",
                                        "Crit_Damage_Percent", "Crit_Percent_Bonus_Capped",
                                        "Power_Cooldown_Reduction_Percent_All", "Resource_Cost_Reduction_Percent_All",
                                        "Damage_Dealt_Percent_Bonus", "Damage_Percent_Bonus_Vs_Elites",
                                        "Damage_Percent_Reduction_From_Elites", "Resistance_All", "Armor_Bonus_Item",
                                        "Hitpoints_Regen_Per_Second", "Hitpoints_On_Hit",
                                        "Damage_Percent_Reduction_From_Melee", "Damage_Percent_Reduction_From_Ranged",
                                        "Block_Chance_Bonus_Item"};
    for (size_t i = 0; i < std::size(order); ++i)
        if (n == order[i])
            return static_cast<int>(i);
    if (n == "Power_Damage_Percent_Bonus")
        return 200;
    if (n == "Splash_Damage_Effect_Percent" || n == "Movement_Scalar")
        return 300;
    return 100;
}

struct Line {
    std::string text;
    int group; // 0 primary, 1 secondary
};
std::vector<Line> Affixes(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
                          const ItemData& it) {
    std::vector<std::pair<int, Line>> out;
    auto tmpl = [&](const std::string& label) { return t.Get(a, h, "AttributeDescriptions", std::string_view{label}); };
    // weapon / jewelry bonus damage ranges: "+{VALUE}-[{VALUE}+{VALUE2}] Damage"
    for (u32 e = 0; e < 7; ++e)
        for (auto [mn, dl] : {std::pair{"Damage_Weapon_Bonus_Min_X1", "Damage_Weapon_Bonus_Delta_X1"},
                              std::pair{"Damage_Min", "Damage_Delta"}}) {
            const double lo = Attr(m, it.attrs, mn, e), d = Attr(m, it.attrs, dl, e);
            if (lo <= 0)
                continue;
            const double v[2] = {lo, d};
            const std::string s = Expand(t.Get(a, h, "AttributeDescriptions", 0x31a934fc), v, 2);
            if (!s.empty())
                out.push_back({-1, {s, 0}});
        }
    for (const auto& kv : it.attrs) {
        const u32 id = kv.key & 0xFFF, p = kv.key >> 12;
        if (id >= m.names.size())
            continue;
        const std::string& n = m.names[id];
        if (n.empty() || Hidden(n))
            continue;
        const double val = m.Value(kv);
        if (val == 0)
            continue;
        std::string label = n, text1;
        double v[3] = {val, 0, 0};
        if (p != NoParam && p < 7) {
            label = n + "#" + ElemName[p];
        } else if (p != NoParam) {
            text1 = StripTags(t.Get(a, h, "Powers", t.PowerName(a, h, p) + "_name"));
            v[0] = 0, v[1] = val;
        }
        if (n.find("_Proc_Chance") != std::string::npos) {
            std::string base = n.substr(0, n.find("_Proc_Chance"));
            v[1] = Attr(m, it.attrs, base + "_Proc_Damage");
            v[2] = Attr(m, it.attrs, base + "_Proc_Duration");
        }
        std::string tp = tmpl(label);
        if (tp.empty() && n == "Crit_Percent_Bonus_Capped")
            tp = t.Get(a, h, "AttributeDescriptions", 0x214ce4cc);
        if (tp.empty() && n == "Movement_Scalar")
            tp = t.Get(a, h, "AttributeDescriptions", 0x61438d5b);
        if (tp.empty())
            continue;
        std::string s = Expand(tp, v, 3, text1);
        if (s.empty())
            continue;
        const int r = Rank(n);
        out.push_back({r, {s, r < 100 ? 0 : r < 200 ? 1 : r < 300 ? 2 : 3}});
    }
    std::stable_sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.first < y.first; });
    std::vector<Line> lines;
    for (auto& [r, l] : out)
        lines.push_back(l);
    return lines;
}

std::vector<std::string> Powers(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
                                const ItemData& it) {
    std::vector<std::string> out;
    const u32 id = m.Id("Item_Power_Passive");
    for (const auto& kv : it.attrs) {
        if ((kv.key & 0xFFF) != id || (kv.key >> 12) == NoParam)
            continue;
        const double v[1] = {m.Value(kv)};
        const std::string tp = t.Get(a, h, "ItemPassivePowerDescriptions",
                                     std::string_view{t.PowerName(a, h, kv.key >> 12)});
        std::string s = Expand(tp, v, 1);
        if (!s.empty())
            out.push_back(std::move(s));
    }
    return out;
}

/// Main card number: weapon damage per second (average x attacks per second, as the console card)
/// or armor; 0 when the item has neither. `armor` tells which.
double MainNumber(const AttrMeta& m, const ItemData& it, bool& armor) {
    const double dmg = Attr(m, it.attrs, "Damage_Weapon_Average_Total_All") *
                       Attr(m, it.attrs, "Attacks_Per_Second_Item_Total", NoParam, 1);
    armor = dmg <= 0;
    return dmg > 0 ? dmg : Attr(m, it.attrs, "Armor_Item_Total");
}
std::string Number(double v, bool armor) {
    if (armor)
        return Grouped(std::floor(v));
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", std::floor(v * 10) / 10);
    return b;
}
std::string Unit(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, bool armor) {
    std::string unit = t.Get(a, h, "ConsoleInventory", armor ? 0x72874587 : 0x946aedcd); // "{s1} Armor" / "{s1} Dmg"
    const size_t p = unit.find("{s1}");
    if (p != std::string::npos)
        unit.erase(p, 4);
    return StripTags(unit);
}
/// "+43" / "-35" (armor) or "+120.5" (damage); empty when equal.
std::string Delta(double d, bool armor) {
    if (std::fabs(d) < (armor ? 0.5 : 0.05))
        return {};
    return (d > 0 ? "+" : "-") + Number(std::fabs(d), armor);
}

/// A native frame stretched or shrunk to w x h (central bands repeated or removed).
Image FitBox(const Image& src, int w, int hh) {
    if (src.Empty())
        return src;
    Image o = FitW(src, w, src.w / 3, src.w * 2 / 3);
    if (hh == o.h)
        return o;
    if (hh > o.h)
        return d3_img::Extend(o, o.w, hh, 0, 0, o.h / 3, o.h * 2 / 3);
    Image r(o.w, hh);
    const int top = hh / 2, bot = hh - top;
    d3_img::Blit(r, d3_img::Crop(o, 0, 0, o.w, top), 0, 0);
    d3_img::Blit(r, d3_img::Crop(o, 0, o.h - bot, o.w, bot), 0, top);
    return r;
}

/// Native button (the console's tiered-rift button art) with an Exocet caption; `hot` adds the
/// orange selection frame (confirm state).
void Button(Image& img, d3_assets::Assets& a, const EdenDsmodHostApi* h, int x, int y, int w, int hh,
            const std::string& label, bool hot) {
    Image b;
    for (const char* n : {"TieredRifts_btn_Up", "TieredRifts_btn_Normal", "TieredRifts_btn", "TieredRifts_btn_Over"})
        if (!(b = a.Frame(h, "2DUITieredRifts", n)).Empty())
            break;
    if (b.Empty())
        b = a.Frame(h, "2DUITieredRifts", "TieredRifts_btn_Disabled");
    b = FitBox(b, w, hh);
    d3_img::Blit(img, b, x, y);
    if (hot)
        d3_img::Blit(img, FitBox(a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlotSelected"), w + 16, hh + 16),
                     x - 8, y - 8);
    std::string l = Upper(label);
    const auto fit = Wrap(a.exo, 36, l, w - 20, 1);
    l = fit.empty() ? l : fit.front();
    TxtMid(img, a.exo, 36, x + w / 2, y + hh / 2, l, hot ? Red : Gold, 1);
}

/// AVAILABLE card: native slot frame, quality strip + icon, name (2 lines) and the main number with
/// its difference to the equipped item.
void AvailCard(Image& img, d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
               const ItemData& it, const ItemData* eq, int x, int y, int w, bool selected) {
    const int anc = static_cast<int>(Attr(m, it.attrs, "Ancient_Rank"));
    Image card = a.Frame(h, "2DUIConsoleInventory",
                         anc >= 2 ? "Console_PrimalInventorySlot"
                                  : anc == 1 ? "Console_AncientInventorySlot" : "Console_EquippedInventorySlot");
    if (card.Empty())
        card = a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlot");
    card = FitBox(card, w, GearCardH);
    d3_img::Blit(img, card, x, y);
    const Q q = Quality(m, it);
    Image strip = FitW(a.Frame(h, "2DUIConsoleItemQuality", QStrip(q)), 76, 14, 38);
    const int sx = x + 10, sy = y + (GearCardH - strip.h) / 2;
    d3_img::Blit(img, strip, sx, sy);
    BlitC(img, IconFit(t.Icon(a, h, it.icon, false), strip.w - 10, strip.h - 12), sx + strip.w / 2, sy + strip.h / 2);
    const int tx = sx + strip.w + 12, tw = w - (tx - x) - 12;
    bool armor = true;
    const double v = MainNumber(m, it, armor);
    // one name line above the number (the ancient / primal frame's lower ornament sits at about
    // two thirds of the card), two when there is no number
    const auto lines = Wrap(a.sans, 30, ItemName(a, t, h, it), tw, v > 0 ? 1 : 2);
    int yy = y + 12;
    for (const auto& l : lines)
        Txt(img, a.sans, 30, tx, yy, l, QColour(q)), yy += 34;
    if (v > 0) {
        const int ny = y + 54;
        const int nw = Txt(img, a.sans, 30, tx, ny, Number(v, armor), White);
        Txt(img, a.sans, 30, tx + nw + 8, ny, Unit(a, t, h, armor), Grey);
        if (eq) {
            bool earmor = true;
            const double ev = MainNumber(m, *eq, earmor);
            if (earmor == armor) {
                const std::string d = Delta(v - ev, armor);
                if (!d.empty())
                    Txt(img, a.sans, 30, x + w - 14, ny, d, v > ev ? SetGreen : Red, 2);
            }
        }
    }
    if (selected)
        d3_img::Blit(img, FitBox(a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlotSelected"), w + 24, GearCardH + 24),
                     x - 12, y - 12);
}

void Tooltip(Image& img, d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
             const ItemData* sel, const ItemData* compare, const std::string& head, int x0, int x1) {
    const int pw = x1 - x0, tx = x0 + 30, tw = pw - 64, y1 = PageH;
    d3_img::Blit(img, DoublePanel(a, h, pw, PageH, "Red", false), x0, 0);
    TxtMid(img, a.exo, 36, x0 + pw / 2, 41, Upper(head), Gold, 1);
    if (!sel)
        return;
    const ItemData& it = *sel;
    int y = 66 + Card(img, a, t, h, m, &it, it.slot, x0 + 22, 66, pw - 44) + 10;
    // big number: weapon damage per second or armor with the native console unit ("{s1} Dmg" /
    // "{s1} Armor"); for a backpack item also its difference to the equipped one
    {
        bool armor = true;
        const double v = MainNumber(m, it, armor);
        if (v > 0) {
            Txt(img, a.sans, 30, tx, y + 14, Unit(a, t, h, armor), Grey);
            Txt(img, a.sans, 45, tx + tw, y, Number(v, armor), White, 2);
            y += LineH(a.sans, 45) + 4;
            if (compare) {
                bool carmor = true;
                const double cv = MainNumber(m, *compare, carmor);
                const std::string d = carmor == armor ? Delta(v - cv, armor) : std::string{};
                if (!d.empty()) {
                    const std::string eq = StripTags(t.Get(a, h, "ConsoleInventory", 0xc0ca249d)); // "Equipped"
                    Txt(img, a.sans, 30, tx, y, eq + " " + Number(cv, armor), Grey);
                    Txt(img, a.sans, 30, tx + tw, y, d, v > cv ? SetGreen : Red, 2);
                    y += 34;
                }
            }
            y += 12;
        }
    }
    const int bottom = y1 - 12;
    auto para = [&](const std::string& s, u32 col, int maxl, bool bul) {
        // the tooltip bullet glyph lives in the encrypted font icon atlas: indent only
        const int ind = bul ? 22 : 0;
        const auto ls = Wrap(a.sans, 30, s, tw - ind, maxl);
        for (size_t i = 0; i < ls.size(); ++i) {
            if (y + 34 > bottom)
                return false;
            Txt(img, a.sans, 30, tx + ind, y, ls[i], col);
            y += 34;
        }
        return true;
    };
    const auto lines = Affixes(a, t, h, m, it);
    int last_group = 0;
    for (const auto& l : lines) {
        if (l.group != last_group)
            y += 8, last_group = l.group;
        // Native affixes with a leading number become a label/value row. Descriptive powers
        // remain complete sentences. Reserve the number column even for short values.
        size_t end = 0;
        while (end < l.text.size() &&
               (std::isdigit(static_cast<unsigned char>(l.text[end])) ||
                std::string_view{"+-,.%"}.find(l.text[end]) != std::string_view::npos))
            ++end;
        if (end && end < l.text.size() && l.text[end] == ' ') {
            const std::string value = l.text.substr(0, end), label = l.text.substr(end + 1);
            const int vw = std::max(170, Measure(a.sans, 30, value));
            bool cut = false;
            auto ls = a.sans.Wrap(Size(a.sans, 30), label, tw - vw - 36, 3, &cut);
            if (cut) {
                ls = Wrap(a.sans, 30, label, tw, 3);
            }
            const int height = std::max(1, static_cast<int>(ls.size()) + (cut ? 1 : 0)) * 34 + 12;
            if (y + height > bottom)
                break;
            for (size_t i = 0; i < ls.size(); ++i)
                Txt(img, a.sans, 30, tx, y + static_cast<int>(i) * 34, ls[i], LGrey);
            Txt(img, a.sans, 30, tx + tw, y + (cut ? static_cast<int>(ls.size()) * 34 : 0),
                value, l.group == 3 ? LegAffix : Magic, 2);
            y += height;
        } else if (!para(l.text, l.group == 3 ? LegAffix : Magic, 3, false)) {
            break;
        }
    }
    y += 6;
    auto pw_lines = Powers(a, t, h, m, it);
    for (const auto& g : it.gems) // a socketed legendary gem's power is listed under the gem
        for (const auto& gp : Powers(a, t, h, m, g))
            pw_lines.erase(std::remove(pw_lines.begin(), pw_lines.end(), gp), pw_lines.end());
    for (const auto& s : pw_lines) {
        const int room = std::max(2, std::min(6, (bottom - y - 48 - 2 * 34) / 34));
        para(s, LegAffix, room, false);
        y += 4;
    }
    // sockets: socketed gems (icon + effect / name and rank), then the empty ones
    const int sockets = static_cast<int>(Attr(m, it.attrs, "Sockets"));
    for (const auto& g : it.gems) {
        if (y + 40 > bottom)
            break;
        // LEGIBILITY.md info-icon floor is 40 px; the row's own minimum pitch below (40, "y += 40" /
        // "y + 40") already matches, so this fits with no other layout change.
        Image ic = IconFit(t.Icon(a, h, g.icon, false), 40, 40);
        d3_img::Blit(img, ic, tx, y);
        std::string s;
        if (g.gb_name.rfind("Unique_Gem", 0) == 0) {
            // legendary gem: name, rank and its powers, as the native tooltip
            Txt(img, a.sans, 30, tx + 44, y, ItemName(a, t, h, g), Legend);
            y += 34;
            std::string rk = StripTags(t.Get(a, h, "ConsoleInventory", 0x94b310df)); // "{s1}\nRank {s2}"
            const size_t nl = rk.find("Rank");
            rk = nl == std::string::npos ? "Rank" : rk.substr(nl, 4);
            Txt(img, a.sans, 30, tx + 44, y, rk + " " + std::to_string(static_cast<int>(AttrOrDefault(m, g.attrs, "Jewel_Rank"))), White);
            y += 34;
            for (const auto& ps : Powers(a, t, h, m, g)) {
                for (const auto& l : Wrap(a.sans, 30, ps, tw - 44, 4)) {
                    if (y + 34 > bottom)
                        break;
                    Txt(img, a.sans, 30, tx + 44, y, l, LegAffix);
                    y += 34;
                }
            }
            y += 4;
            continue;
        } else {
            const auto gl = Affixes(a, t, h, m, g);
            s = gl.empty() ? ItemName(a, t, h, g) : gl.front().text;
        }
        const auto ls = Wrap(a.sans, 30, s, tw - 44, 2);
        int ly = y;
        for (const auto& l : ls)
            Txt(img, a.sans, 30, tx + 44, ly, l, White), ly += 34;
        y = std::max(ly, y + 40) + 4;
    }
    if (sockets > static_cast<int>(it.gems.size())) {
        Image sock = a.Frame(h, "2DUIFontIcons_NX64", "socket");
        const std::string empty = StripTags(t.Get(a, h, "ConsoleInventory", 0x51e4bef)) + " " +
                                  StripTags(t.Get(a, h, "ConsoleSocketing", 0xac0b01a5));
        for (int s = static_cast<int>(it.gems.size()); s < sockets && y + 40 <= bottom; ++s) {
            d3_img::Blit(img, sock, tx, y + 2, 2);
            Txt(img, a.sans, 30, tx + 44, y, empty, Grey);
            y += 40;
        }
    }
    // item type line: quality ("Ancient Legendary") + item type name
    {
        const Q q = Quality(m, it);
        const int anc = static_cast<int>(Attr(m, it.attrs, "Ancient_Rank"));
        u32 ql = 0;
        if (q == Q::Set)
            ql = anc >= 2 ? 0x666c7dd3 : anc == 1 ? 0xf7e03f6e : 0x16e8c;
        else if (q == Q::Legendary)
            ql = anc >= 2 ? 0x4d233e2 : anc == 1 ? 0xe3d57dbd : 0xb7310b5b;
        else if (q == Q::Rare)
            ql = 0x2ea2ca;
        else if (q == Q::Magic)
            ql = 0x5a853a1;
        std::string s = ql ? StripTags(t.Get(a, h, "ItemQuality", ql)) : std::string{};
        const std::string tn = StripTags(t.Get(a, h, "ItemTypeNames", std::string_view{it.itype_name}));
        s += (s.empty() || tn.empty() ? "" : " ") + tn;
        y += 4;
        para(s, QColour(q), 2, false);
    }
}

std::string Label(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const char* table, u32 id) {
    return StripTags(t.Get(a, h, table, id));
}
} // namespace

u32 GearClassBit(u32 portrait_actor) {
    switch (portrait_actor) {
    case 3285: case 3301: return 0x100;      // Barbarian
    case 74706: case 75207: return 0x800;    // Demon Hunter
    case 4717: case 4721: return 0x2000;     // Monk
    case 6481: case 6485: return 0x400;      // Witch Doctor
    case 6526: case 6544: return 0x200;      // Wizard
    case 238284: case 238286: return 0x20000; // Crusader
    case 454021: case 454402: return 0x100000; // Necromancer
    default: return 0;
    }
}

bool GearCompatible(const ItemData& it, int slot, u32 class_bit, bool main_two_handed) {
    if (it.slot != 0 || slot < 1 || slot > 13 || !(it.itype_slots >> slot & 1))
        return false;
    if (class_bit && it.itype_classes && !(it.itype_classes & class_bit))
        return false;
    if (slot == 3) {
        constexpr u32 OneHand = 0x200, TwoHand = 0x400;
        constexpr u32 DualWield = 0x100 | 0x800 | 0x2000; // Barbarian, Demon Hunter, Monk
        if (main_two_handed && class_bit != 0x20000)
            return false;
        if (it.itype_flags & TwoHand)
            return false;
        if ((it.itype_flags & OneHand) && !(class_bit & DualWield))
            return false;
    }
    return true;
}

int GearFindSlot(const std::vector<ItemData>& items, int slot) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].slot == slot)
            return static_cast<int>(i);
    return -1;
}
int GearFindCell(const std::vector<ItemData>& items, int x, int y) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].slot == 0 && items[i].x == x && items[i].y == y)
            return static_cast<int>(i);
    return -1;
}

d3_img::Image GearPage(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const AttrMeta& m,
                       const GearView& v) {
    Image img(PageW, PageH);
    d3_img::Fill(img, 0x060504FF);
    static const std::vector<ItemData> none;
    const std::vector<ItemData>& items = v.items ? *v.items : none;
    auto at = [&](int i) -> const ItemData* { return i >= 0 && i < static_cast<int>(items.size()) ? &items[i] : nullptr; };
    const ItemData* sel = at(v.sel);
    const ItemData* eq = at(v.equipped);
    const std::string inv = Label(a, t, h, "ConsoleInventory", 0x2abec52e); // "Inventory"
    if (v.bag) {
        // secondary view: the 10 x 6 backpack grid in the console's slot layout (read-only)
        constexpr int GW = 762;
        d3_img::Blit(img, DoublePanel(a, h, GW, PageH, "Blue", false), 0, 0);
        TxtMid(img, a.exo, 36, GW / 2, 41, Upper(inv), Gold, 1);
        constexpr int cw = GearBagCW, ch = GearBagCH, gx = GearBagX, gy = GearBagY;
        Image strip0 = a.Frame(h, "2DUIConsoleItemQuality", "Console_ItemQualityCommon");
        for (int yy = 0; yy < 6; ++yy)
            for (int xx = 0; xx < 10; ++xx) {
                const int cx = gx + xx * cw + cw / 2, cy = gy + yy * ch + ch / 2;
                const int idx = GearFindCell(items, xx, yy);
                if (idx < 0) {
                    BlitC(img, strip0, cx, cy, 1, 70);
                    continue;
                }
                const ItemData& it = items[idx];
                BlitC(img, a.Frame(h, "2DUIConsoleItemQuality", QStrip(Quality(m, it))), cx, cy);
                BlitC(img, IconFit(t.Icon(a, h, it.icon, false), cw - 12, ch - 20), cx, cy);
                const int stack = static_cast<int>(Attr(m, it.attrs, "ItemStackQuantityLo"));
                if (stack > 1)
                    Txt(img, a.sans, 30, cx + cw / 2 - 4, cy + ch / 2 - 36, std::to_string(stack), White, 2, 2);
            }
        if (sel && sel->slot == 0) {
            Image s = FitW(a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlotSelected"), cw + 16, 100, 150);
            const int cx = gx + sel->x * cw + cw / 2, cy = gy + sel->y * ch + ch / 2;
            BlitC(img, d3_img::Extend(s, s.w, ch + 16, 0, 0, 40, 98), cx, cy);
        }
        Txt(img, a.sans, 30, 32, 890, inv, LGrey);
        Txt(img, a.sans, 30, GW - 30, 890, std::to_string(v.backpack) + " / " + std::to_string(v.backpack_max), White, 2);
        Tooltip(img, a, t, h, m, sel, nullptr,
                sel && sel->slot >= 1 ? Label(a, t, h, "ConsoleInventory", SlotLabel(sel->slot)) : inv, GW + 4, PageW);
        return img;
    }
    // ---- left: the equipment slots ----
    d3_img::Blit(img, DoublePanel(a, h, GearColW, PageH, "Blue", false), 0, 0);
    TxtMid(img, a.exo, 36, GearColW / 2, 41, Upper(Label(a, t, h, "ConsoleInventory", 0xc0ca249d)), Gold, 1);
    for (int i = 0; i < 13; ++i) {
        const int slot = GearSlotOrder[i];
        const int y = GearRowY + i * GearRowH, cy = y + GearRowH / 2;
        const bool on = slot == v.slot;
        const bool blocked = slot == 3 && v.offhand_blocked;
        if (on)
            d3_img::Blit(img, FitBox(a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlotSelected"), GearColW - 20, GearRowH + 6),
                         10, y - 3);
        Image ic;
        if (on)
            ic = a.Frame(h, "2DUIConsoleIcons", std::string{SlotIcon(slot)} + "Selected");
        if (ic.Empty())
            ic = a.Frame(h, "2DUIConsoleIcons", SlotIcon(slot));
        BlitC(img, IconFit(ic, 56, 56), 48, cy, 1, blocked ? 110 : 255);
        const int idx = GearFindSlot(items, slot);
        const u32 col = blocked ? Grey : on ? Gold : idx >= 0 ? LGrey : Grey;
        const auto name = Wrap(a.sans, 30, Label(a, t, h, "ConsoleInventory", SlotLabel(slot)), GearColW - 150, 1);
        if (!name.empty())
            TxtMid(img, a.sans, 30, 86, cy, name.front(), col);
        if (idx >= 0) {
            // hint: the equipped item's native quality strip with its icon
            const ItemData& it = items[idx];
            Image strip = FitBox(a.Frame(h, "2DUIConsoleItemQuality", QStrip(Quality(m, it))), 28, 54);
            const int sx = GearColW - 54, sy = cy - strip.h / 2;
            d3_img::Blit(img, strip, sx, sy);
            BlitC(img, IconFit(t.Icon(a, h, it.icon, false), 24, 46), sx + strip.w / 2, cy);
        }
    }
    {
        // inventory count (tap: backpack grid) and currencies
        const int x = 24, y = GearFootY;
        Txt(img, a.sans, 30, x, y, inv, LGrey);
        Txt(img, a.sans, 30, GearColW - 24, y, std::to_string(v.backpack) + " / " + std::to_string(v.backpack_max), White, 2);
        BlitC(img, GoldIcon(a, h), x + 16, y + 36 + 17);
        Txt(img, a.sans, 30, x + 44, y + 36, v.gold, White);
        BlitC(img, ShardIcon(a, h), x + 16, y + 72 + 17);
        const int w = Txt(img, a.sans, 30, x + 44, y + 72, v.shards, White);
        Txt(img, a.sans, 30, x + 44 + w, y + 72, " / " + v.shard_cap, Grey);
    }
    // ---- middle: EQUIPPED card, AVAILABLE cards, actions ----
    d3_img::Blit(img, DoublePanel(a, h, GearMidW, PageH, "Red", false), GearMidX, 0);
    TxtMid(img, a.exo, 36, GearMidX + GearMidW / 2, 41, Upper(Label(a, t, h, "ConsoleInventory", SlotLabel(v.slot))), Gold, 1);
    Card(img, a, t, h, m, eq, v.slot, GearCardX, GearEqY, GearCardW);
    if (eq && sel == eq)
        d3_img::Blit(img, FitBox(a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventorySlotSelected"), GearCardW + 24, GearCardH + 24),
                     GearCardX - 12, GearEqY - 12);
    // UNEQUIP: bottom of the equipped pane (owner request), instant (d3_gear_mailbox). The native
    // StringList id for this caption was not isolated in the time available; English literal.
    if (v.can_unequip && !v.confirm)
        Button(img, a, h, GearCardX, GearUnequipY, GearCardW, GearUnequipH, "Unequip", false);
    {
        const int n = static_cast<int>(v.avail.size());
        TxtMid(img, a.sans, 30, GearCardX + 4, GearAvailHeadY, inv, Gold);
        if (n > GearAvailRows) {
            const int last = std::min(n, v.scroll + GearAvailRows);
            TxtMid(img, a.sans, 30, GearCardX + GearCardW - 4, GearAvailHeadY,
                   std::to_string(v.scroll + 1) + "-" + std::to_string(last) + " / " + std::to_string(n), Grey, 2);
        } else {
            TxtMid(img, a.sans, 30, GearCardX + GearCardW - 4, GearAvailHeadY, std::to_string(n), Grey, 2);
        }
    }
    if (v.avail.empty()) {
        Image e = a.Frame(h, "2DUIConsoleInventory", "Console_EquippedInventoryEmptyList");
        if (!e.Empty())
            d3_img::Blit(img, FitBox(e, GearCardW, GearAvailStep * GearAvailRows - 8), GearCardX, GearAvailY, 1, 0, 0, -1, -1, 160);
    }
    for (int r = 0; r < GearAvailRows; ++r) {
        const int i = v.scroll + r;
        if (i >= static_cast<int>(v.avail.size()))
            break;
        const ItemData* it = at(v.avail[i]);
        if (it)
            AvailCard(img, a, t, h, m, *it, eq, GearCardX, GearAvailY + r * GearAvailStep, GearCardW, it == sel);
    }
    const int bx0 = GearCardX + (GearCardW - 2 * GearBtnW - GearBtnGap) / 2, bx1 = bx0 + GearBtnW + GearBtnGap;
    if (v.confirm) {
        // Confirm state draws at a different Y than the normal Equip/Drop row (GearConfirmBtnY vs
        // GearBtnY): a tap can then never land on both a Cancel/Drop? rect and an Equip/Drop rect
        // across one state flip, regardless of gate timing (tools/d3/gen_manifest.py mirrors this).
        Button(img, a, h, bx0, GearConfirmBtnY, GearBtnW, GearBtnH, Label(a, t, h, "Console", 0xa362ef46), false); // Cancel
        Button(img, a, h, bx1, GearConfirmBtnY, GearBtnW, GearBtnH, Label(a, t, h, "ConsoleInventory", 0x273d75) + "?", true);
    } else {
        if (v.can_equip)
            Button(img, a, h, bx0, GearBtnY, GearBtnW, GearBtnH, Label(a, t, h, "ConsoleInventory", 0x5209144), false); // Equip
        if (v.can_drop)
            Button(img, a, h, bx1, GearBtnY, GearBtnW, GearBtnH, Label(a, t, h, "ConsoleInventory", 0x273d75), false); // Drop
    }
    if (!v.notice.empty())
        TxtMid(img, a.sans, 30, GearMidX + GearMidW / 2, GearBtnY + GearBtnH + 34, v.notice,
               v.notice_bad ? Red : LGrey, 1);
    // ---- right: tooltip of the selected item ----
    const bool from_bag = sel && sel->slot == 0;
    Tooltip(img, a, t, h, m, sel, from_bag ? eq : nullptr,
            from_bag ? inv : Label(a, t, h, "ConsoleInventory", 0xc0ca249d), GearTipX, PageW);
    return img;
}

} // namespace d3hg
