// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_page_hero.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "d3_hg_draw.h"

namespace d3hg {
namespace {
using namespace draw;

// StringList labels (hashes of the game's own label names; text is read from the player's romfs)
constexpr const char* HD = "HeroDetails";
constexpr const char* CCD = "ConsoleCharacterDetails";

std::string Fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string Int(double v) {
    return Fmt("%.0f", v);
}
std::string Num2(double v) {
    return std::fabs(v) >= 10000 ? Grouped(v, 2) : Fmt("%.2f", v);
}
std::string Pct(double frac, int dec = 2) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f%%", dec, frac * 100);
    return b;
}

struct ResLabels {
    u32 max, regen, cost;
};
bool ResourceLabels(int t, ResLabels& r) {
    switch (t) {
    case 0: r = {0x12300be3, 0x4239645c, 0x405e6401}; return true;   // Mana
    case 1: r = {0xb5dc548d, 0x669eb7c6, 0xe7ae812b}; return true;   // Arcane Power
    case 2: r = {0x122c8aec, 0xdb003fe5, 0x748357ea}; return true;   // Fury
    case 3: r = {0x6d6e8c61, 0x71973a5a, 0x9c3bf3f}; return true;    // Spirit
    case 5: r = {0x52bc657e, 0x8e3659f7, 0x2291c2bc}; return true;   // Hatred
    case 6: r = {0xac87a28a, 0xc04aaa03, 0xb8bc7348}; return true;   // Discipline
    case 7: r = {0x57b2ca52, 0xc852f85, 0x1b6575d0}; return true;    // Wrath
    case 8: r = {0xed3e276c, 0x4e5ce225, 0xd44e122a}; return true;   // Essence
    default: return false;
    }
}

// class name label (Bnet_Classes) and portrait frame from the hero actor
struct ClassArt {
    const char* label;
    const char* atlas;
    const char* portrait;
};
ClassArt ClassFor(u32 actor) {
    switch (actor) {
    case 3301: return {"Barbarian", "2DUIPortraits", "Portrait_Barbarian_Male"};
    case 3285: return {"Barbarian", "2DUIPortraits", "Portrait_Barbarian_Female"};
    case 75207: return {"DemonHunter", "2DUIPortraits", "Portrait_Demonhunter_Male"};
    case 74706: return {"DemonHunter", "2DUIPortraits", "Portrait_Demonhunter_Female"};
    case 4721: return {"Monk", "2DUIPortraits", "Portrait_Monk_Male"};
    case 4717: return {"Monk", "2DUIPortraits", "Portrait_Monk_Female"};
    case 6485: return {"WitchDoctor", "2DUIPortraits", "Portrait_Witchdoctor_Male"};
    case 6481: return {"WitchDoctor", "2DUIPortraits", "Portrait_Witchdoctor_Female"};
    case 6544: return {"Wizard", "2DUIPortraits", "Portrait_Wizard_Male"};
    case 6526: return {"Wizard", "2DUIPortraits", "Portrait_Wizard_Female"};
    case 238284: return {"Crusader", "2DUIPortraits", "Portrait_Crusader_Male"};
    case 238286: return {"Crusader", "2DUIPortraits", "Portrait_Crusader_Female"};
    case 454021: return {"Necromancer", "2DUIPortraits_p2", "Portrait_Necromancer_Male"};
    case 454402: return {"Necromancer", "2DUIPortraits_p2", "Portrait_Necromancer_Female"};
    default: return {"", "", ""};
    }
}
} // namespace

HeroView BuildHeroView(const AttrMeta& m, const HeroData& h, const std::vector<ItemData>& items) {
    HeroView v;
    if (!h.valid)
        return v;
    v.valid = true;
    v.name = h.name;
    v.portrait_actor = h.portrait_actor;
    v.level = h.level;
    v.paragon = h.paragon;
    v.cube_valid = h.cube_valid;
    v.cube = h.cube;
    if (h.xp_left >= 0 && h.xp_need > 0)
        v.xp_fill = static_cast<int>(std::clamp(1.0 - h.xp_left / h.xp_need, 0.0, 1.0) * 168);
    v.gold = Grouped(static_cast<double>(h.gold));
    v.shards = std::to_string(h.shards);
    // blood shard cap: 500 + 10 per level of the highest solo Greater Rift (as the native "n/500")
    const double best = Attr(m, h.attrs, "Highest_Solo_Rift_Level");
    v.shard_cap = std::to_string(500 + 10 * static_cast<int>(std::max(0.0, best)));
    auto A = [&](const char* n, u32 p = NoParam, double d = 0) { return Attr(m, h.attrs, n, p, d); };
    auto heading = [&](const char* list, u32 l) { v.rows.push_back({0, list, l, {}, {}}); };
    auto row = [&](const char* list, u32 l, std::string val, std::string arg = {}) {
        v.rows.push_back({1, list, l, std::move(arg), std::move(val)});
    };
    // ---- Primary Attributes
    const int main = static_cast<int>(A("Primary_Damage_Attribute", NoParam, -1));
    const double str = A("Strength_Total"), dex = A("Dexterity_Total"), intel = A("Intelligence_Total");
    const double mainv = main == 0 ? str : main == 1 ? dex : main == 2 ? intel : 0;
    double wmin = 0, wdelta = 0;
    for (u32 t = 0; t < 7; ++t)
        wmin += A("Damage_Min_Total", t), wdelta += A("Damage_Delta_Total", t);
    const double aps = A("Attacks_Per_Second_Total");
    const double chc = std::min(A("Crit_Percent_Cap", NoParam, 0.75), A("Crit_Percent_Base") + A("Crit_Percent_Bonus_Capped"));
    const double chd = A("Crit_Damage_Percent") + 0.5;
    const double skills = A("Damage_Percent_All_From_Skills");
    // the sheet's damage in single precision, as the game computes it (696,023.81 on the fixture)
    const float fdps = (static_cast<float>(wmin) + static_cast<float>(wdelta) / 2.0f) * static_cast<float>(aps) *
                       (1.0f + static_cast<float>(mainv) / 100.0f) *
                       (1.0f + static_cast<float>(chc) * static_cast<float>(chd)) * (1.0f + static_cast<float>(skills));
    const double dps = fdps;
    v.damage = Fmt("%.0fk", std::floor(dps / 1e3)); // the sheet prints thousands ("696k") at any magnitude
    heading(CCD, 0x8f0ca76b);
    row(CCD, 0x8c09662f, Int(str));
    row(CCD, 0x524de0a2, Int(dex));
    row(CCD, 0x2ee41493, Int(intel));
    row(CCD, 0x66f2796, Int(A("Vitality_Total")));
    row(CCD, 0x4d89a61, A("Armor_Total") >= 10000 ? Grouped(A("Armor_Total")) : Int(A("Armor_Total")));
    row(CCD, 0x17b7fc82, Grouped(dps, 2));
    // ---- Offense
    heading(HD, 0x201b7904);
    if (main >= 0 && main <= 2)
        row(HD, main == 0 ? 0xe6034e58 : main == 1 ? 0xeb84cdeb : 0x4677e93c, Pct(mainv / 100));
    row(HD, 0xec47a19b, Pct(skills));
    row(HD, 0x8669044c, Pct(A("Damage_Percent_Bonus_Vs_Elites")));
    row(HD, 0x2bd8834e, Fmt("%.2f", aps));
    const double apsi = A("Attacks_Per_Second_Item_Total");
    row(HD, 0x1af70d53, Pct(apsi > 0 ? aps / apsi - 1 : 0));
    row(HD, 0xc2d2e17b, Pct(chc));
    row(HD, 0xd64c0e78, "+" + Pct(chd));
    row(HD, 0x1c0570ea, Pct(A("Splash_Damage_Effect_Percent")));
    row(HD, 0x2a2ba992, Pct(A("Power_Cooldown_Reduction_Percent_All_Capped")));
    static const u32 Elem[7] = {0xc122b425, 0x2daeafce, 0x7d557d8c, 0x2dad236a, 0x6bd55f80, 0x490ab092, 0x2dafe164};
    for (u32 t = 0; t < 7; ++t) {
        const double e = A("Damage_Dealt_Percent_Bonus", t);
        if (e > 0)
            row(HD, Elem[t], Pct(e));
    }
    {
        // "<skill> Damage Increase" rows, largest first
        const u32 id = m.Id("Power_Damage_Percent_Bonus");
        std::vector<std::pair<double, u32>> sk;
        for (const auto& kv : h.attrs)
            if ((kv.key & 0xFFF) == id && (kv.key >> 12) != NoParam && m.Value(kv) > 0)
                sk.emplace_back(m.Value(kv), kv.key >> 12);
        std::stable_sort(sk.begin(), sk.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& [val, p] : sk)
            row(HD, 0x60d87186, Pct(val), std::to_string(p));
    }
    // ---- Defense
    heading(HD, 0xafbc378b);
    row(HD, 0xc1f049ff, Int(A("Block_Amount_Total_Min")) + "-" + Int(A("Block_Amount_Total_Max")));
    row(HD, 0xc638542d, Pct(A("Block_Chance_Capped_Total"), 1));
    row(HD, 0xbee27e05, Pct(A("Dodge_Chance_Bonus"), 1));
    const double armor = A("Armor_Total");
    const double mlvl = std::max(1.0, A("Level"));
    row(HD, 0x1a35f96c, Pct(armor / (armor + 50 * mlvl)));
    static const std::pair<u32, u32> Res[6] = {{0x56ae3ae, 0}, {0xb4427133, 3}, {0x34d9ca97, 1},
                                               {0xed5828d5, 2}, {0x2254bec9, 4}, {0x7ba80e5b, 5}};
    for (auto [l, t] : Res)
        row(HD, l, Int(A("Resistance_Total", t)));
    row(HD, 0xa5c8caf3, Pct(A("CrowdControl_Reduction")));
    row(HD, 0xcd23f637, Pct(A("Damage_Percent_Reduction_From_Ranged")));
    row(HD, 0xc8344c89, Pct(A("Damage_Percent_Reduction_From_Melee")));
    row(HD, 0xbfaef6bf, Pct(A("Damage_Percent_Reduction_From_Elites")));
    row(HD, 0x42f1e88e, Num2(A("Thorns_Fixed_Total", 0)));
    // ---- Life (native order and number style: two decimals, grouped from five digits)
    heading(HD, 0xa3569a3e);
    row(HD, 0x2b7900, Grouped(A("Hitpoints_Max_Total")));
    row(HD, 0xd20fd702, "+" + Fmt("%.0f%%", (A("Hitpoints_Max_Percent_Bonus") + A("Hitpoints_Max_Percent_Bonus_Item")) * 100));
    row(HD, 0xc66fb63, Num2(A("Hitpoints_Regen_Per_Second_Subtotal")));
    row(HD, 0x195e6159, Pct(A("Steal_Health_Percent")));
    row(HD, 0xe16f33b3, Num2(A("Hitpoints_On_Kill_Total")));
    row(HD, 0x3d2259ac, Num2(A("Hitpoints_On_Hit")));
    row(HD, 0x41c55586, Num2(A("Health_Globe_Bonus_Health")));
    row(HD, 0x8ef5de98, Num2(A("Gold_PickUp_Radius")));
    // ---- Resource
    heading(HD, 0xba61b326);
    for (const char* n : {"Resource_Type_Primary", "Resource_Type_Secondary"}) {
        const int t = static_cast<int>(A(n, NoParam, -1));
        ResLabels r;
        if (t < 0 || !ResourceLabels(t, r))
            continue;
        row(HD, r.max, Int(A("Resource_Max_Total", u32(t))));
        const double regen = A("Resource_Regen_Total", u32(t));
        if (regen > 0)
            row(HD, r.regen, Num2(regen));
        row(HD, r.cost, Pct(A("Resource_Cost_Reduction_Percent_Total", u32(t))));
    }
    // ---- Adventure (Gold Find and Experience omitted: their native values are not derived yet)
    heading(HD, 0xd34457be);
    row(HD, 0x2130dc3c, "+" + Pct(A("Movement_Scalar_Total", NoParam, 1) - 1));
    row(HD, 0x83236be2, "+" + Fmt("%.0f%%", A("Magic_Find_Total") * 100));
    row(HD, 0xe378ec2f, Pct(A("Experience_Bonus_Percent_Total"), 1));
    row(HD, 0x5ed35c02, Num2(A("Experience_Bonus")));
    // ---- buffs and legendary gems (socketed in equipped items)
    v.buffs = h.buffs;
    for (const auto& it : items) {
        if (it.slot < 1 || it.slot > 13)
            continue;
        for (const auto& g : it.gems) {
            if (g.gb_name.rfind("Unique_Gem", 0) != 0)
                continue;
            GemView gv{g.icon, g.gbid, static_cast<int>(AttrOrDefault(m, g.attrs, "Jewel_Rank"))};
            v.gems.push_back(gv);
        }
    }
    return v;
}

int HeroRowCount(const HeroView& v) {
    return static_cast<int>(v.rows.size());
}

int HeroEffectsRowCount(const HeroView& v) {
    return static_cast<int>(std::max(size_t{1}, v.buffs.size())) + (v.gems.empty() ? 0 : 2) + 4;
}

d3_img::Image HeroPage(d3_assets::Assets& a, Text& t, const EdenDsmodHostApi* h, const HeroView& v) {
    Image img(PageW, PageH);
    d3_img::Fill(img, 0x060504FF);
    constexpr int LW = 330;
    d3_img::Blit(img, Ringside(a, h, LW, PageH), 0, 0);
    const int cx = LW / 2;
    // ---- identity: portrait in the console equipment ring
    const ClassArt ca = ClassFor(v.portrait_actor);
    {
        Image base = a.Frame(h, "2DUIConsoleInventoryShared", "#8b42b80d");
        Image glow = a.Frame(h, "2DUIConsoleInventoryShared", "Console_EquippedInventoryRingGlow");
        BlitC(img, base, cx, 160);
        BlitC(img, glow, cx, 160);
        if (ca.atlas[0])
            BlitC(img, a.Frame(h, ca.atlas, ca.portrait), cx, 160);
    }
    int y = 316;
    {
        const std::string nm = Upper(v.name);
        const float pt = Measure(a.exo, 45, nm) <= LW - 40 ? 45.0f : 36.0f;
        Txt(img, a.exo, pt, cx, y, Wrap(a.exo, pt, nm, LW - 40, 1).front(), Gold, 1);
        y += 58;
    }
    {
        std::string lv = t.Get(a, h, "ConsoleInventory", 0xcae453e8); // "Level {s1}"
        const size_t p = lv.find("{s1}");
        lv = p == std::string::npos ? "Level " + std::to_string(v.level) : lv.replace(p, 4, std::to_string(v.level));
        Txt(img, a.sans, 30, cx, y, StripTags(lv), LGrey, 1);
        y += 38;
        Txt(img, a.sans, 30, 32, y, "Paragon", Grey);
        Txt(img, a.sans, 30, LW - 32, y, Grouped(v.paragon), Paragon, 2);
        y += 40;
    }
    if (ca.label[0]) {
        Txt(img, a.sans, 30, cx, y, t.Get(a, h, "Bnet_Classes", std::string_view{ca.label}), LGrey, 1);
        y += 38;
    }
    y += 8;
    if (v.xp_fill >= 0) {
        Txt(img, a.sans, 30, cx, y, t.Get(a, h, "X1_ConsoleParagon", 0x605f611a), Grey, 1); // To Next Level
        y += 38;
        Image e = a.Frame(h, "2DUIConsoleParagon", "Console_Paragon_XP_Empty");
        Image f = a.Frame(h, "2DUIConsoleParagon", "Console_Paragon_XP_Fill");
        if (!e.Empty()) {
            d3_img::Blit(img, e, cx - e.w / 2, y);
            if (!f.Empty() && v.xp_fill > 0)
                d3_img::Blit(img, f, cx - f.w / 2, y, 1, 0, 0, std::min(f.w, v.xp_fill), f.h);
            y += e.h;
        }
        y += 24;
    }
    // damage (the native inventory summary's damage medallion)
    {
        BlitC(img, a.Frame(h, "2DUIConsoleIcons", "#667a5eb9"), 48, y + 22);
        TxtMid(img, a.sans, 36, LW - 32, y + 22, v.damage, White, 2);
        y += 70;
    }
    {
        Image div = a.Frame(h, "2DUIConsoleMenu", "Console_QuestDivider");
        BlitC(img, div, cx, y + div.h / 2);
        y += 34;
    }
    {
        const int x = 24;
        BlitC(img, GoldIcon(a, h), x + 16, y + 20);
        TxtMid(img, a.sans, 30, LW - 24, y + 20, v.gold, White, 2);
        y += LineH(a.sans, 36) + 6;
        BlitC(img, ShardIcon(a, h), x + 16, y + 20);
        TxtMid(img, a.sans, 30, LW - 24, y + 20, v.shards + " / " + v.shard_cap, White, 2);
    }
    // ---- right panel: Details | Status Effects
    const int X0 = LW + 6, pw = PageW - X0, dw = pw / 2;
    d3_img::Blit(img, DoublePanel(a, h, pw, PageH, "Red", true), X0, 0);
    TxtMid(img, a.exo, 36, X0 + dw / 2, 41, Upper(t.Get(a, h, HD, 0x62b99e2)), Gold, 1);
    TxtMid(img, a.exo, 36, X0 + dw + (pw - dw) / 2, 41, Upper(t.Get(a, h, "ConsoleSkills", 0x5a428644)), Gold, 1);
    {
        const int x = X0 + 34, wcol = dw - 64, ymax = 912; // keep rows above the scroll-arrow band
        const int lh = LineH(a.sans, 30) + 12, step = 34;
        int yy = 72;
        if (v.scroll > 0) {
            Image up = FlipV(a.Frame(h, "2DUIConsoleOptions", "Console_Waypoint_ScrollArrow"));
            d3_img::Blit(img, up, x + wcol / 2 - up.w / 2, 60);
            yy = 72 + 20;
        }
        for (size_t i = std::max(0, v.scroll); i < v.rows.size(); ++i) {
            const StatRow& r = v.rows[i];
            std::string lab = t.Get(a, h, r.list, r.label);
            if (!r.arg.empty()) {
                std::string s1 = t.Get(a, h, "Powers", t.PowerName(a, h, static_cast<u32>(std::stoul(r.arg))) + "_name");
                const size_t p = lab.find("{s1}");
                if (p != std::string::npos)
                    lab.replace(p, 4, s1);
            }
            lab = StripTags(lab);
            if (lab.empty())
                continue;
            int need = lh;
            std::vector<std::string> ls;
            bool own_line = false;
            if (r.kind == 1) {
                const int vw = Measure(a.sans, 30, r.value);
                bool cut = false;
                ls = a.sans.Wrap(Size(a.sans, 30), lab, wcol - std::max(150, vw) - 40, 3, &cut);
                if (cut || ls.size() > 1) {
                    ls = Wrap(a.sans, 30, lab, wcol - 8, 3);
                    own_line = true;
                }
                need = lh + step * (static_cast<int>(ls.size()) - 1 + (own_line ? 1 : 0));
            }
            if (yy + need > ymax) {
                BlitC(img, a.Frame(h, "2DUIConsoleOptions", "Console_Waypoint_ScrollArrow"), x + wcol / 2, 936);
                break;
            }
            if (r.kind == 0) {
                Txt(img, a.sans, 30, x, yy, lab, Yellow);
                yy += lh + 8;
                continue;
            }
            for (size_t k = 0; k < ls.size(); ++k)
                Txt(img, a.sans, 30, x + 8, yy + static_cast<int>(k) * step, ls[k], Grey);
            if (own_line) {
                const int ly = yy + static_cast<int>(ls.size()) * step;
                Txt(img, a.sans, 30, x + wcol, ly, r.value, White, 2);
                yy = ly + lh;
            } else {
                Txt(img, a.sans, 30, x + wcol, yy, r.value, White, 2);
                yy += lh + (static_cast<int>(ls.size()) - 1) * step;
            }
        }
    }
    {
        const int sx = X0 + dw + 16, sw = pw - dw - 40;
        int yy = v.effects_scroll > 0 ? 92 : 72, entry = 0;
        bool more = false;
        auto arrow = [&](bool up, int y) {
            Image ic = a.Frame(h, "2DUIConsoleOptions", "Console_Waypoint_ScrollArrow");
            if (up)
                ic = FlipV(ic);
            BlitC(img, ic, sx + sw / 2, y);
        };
        if (v.effects_scroll > 0)
            arrow(true, 68);
        // Whole entries share one independently scrollable column. Check the complete entry
        // height before painting, so buff names, gems and powers never cross the bottom frame.
        auto row = [&](int height, auto paint) {
            if (entry++ < v.effects_scroll || more)
                return;
            if (yy + height > 922) {
                more = true;
                return;
            }
            paint(yy);
            yy += height;
        };
        auto heading = [&](const std::string& text) {
            const auto ls = Wrap(a.sans, 30, text, sw, 2);
            row(static_cast<int>(ls.size()) * 32 + 12, [&](int y) {
                for (const auto& line : ls)
                    Txt(img, a.sans, 30, sx, y, line, Yellow), y += 32;
            });
        };
        if (v.buffs.empty()) {
            const auto ls = Wrap(a.sans, 30, t.Get(a, h, CCD, 0x9ef5670c), sw, 3);
            row(static_cast<int>(ls.size()) * 34 + 20, [&](int y) {
                for (const auto& line : ls)
                    Txt(img, a.sans, 30, sx + sw / 2, y, line, Gold, 1), y += 34;
            });
        }
        for (const auto& b : v.buffs) {
            const std::string pn = t.PowerName(a, h, b.power);
            std::string nm = StripTags(t.Get(a, h, "BuffTooltips", pn + "_" + std::to_string(b.slot)));
            if (nm.empty())
                nm = StripTags(t.Get(a, h, "Powers", pn + "_name"));
            std::string tm;
            if (b.secs >= 0)
                tm = b.secs >= 60 ? std::to_string(b.secs / 60) + ":" + (b.secs % 60 < 10 ? "0" : "") +
                                        std::to_string(b.secs % 60)
                                  : std::to_string(b.secs) + "s";
            const int tw = tm.empty() ? 0 : Measure(a.sans, 30, tm) + 12;
            bool cut = false;
            auto ls = a.sans.Wrap(Size(a.sans, 30), nm, sw - 86 - tw, 1, &cut);
            if (cut)
                ls = Wrap(a.sans, 30, nm, sw - 86, 3);
            const int text_height = static_cast<int>(ls.size() + (cut && !tm.empty() ? 1 : 0)) * 32;
            row(std::max(76, text_height + 12), [&](int y) {
                Image ic = IconFit(t.Icon(a, h, b.icon, true), 64, 64);
                d3_img::Blit(img, ic, sx, y);
                if (b.stacks > 1)
                    Txt(img, a.sans, 30, sx + 62, y + 30, std::to_string(b.stacks), White, 2, 2);
                int ly = y + 4;
                for (const auto& line : ls)
                    Txt(img, a.sans, 30, sx + 76, ly, line, Gold), ly += 32;
                if (!tm.empty()) {
                    if (cut)
                        Txt(img, a.sans, 30, sx + 76, ly, tm, White);
                    else
                        Txt(img, a.sans, 30, sx + sw - 10, y + 4, tm, White, 2);
                }
            });
        }
        if (!v.gems.empty()) {
            heading(StripTags(t.Get(a, h, "ItemQuality", 0xb7310b5b)) + " " +
                    StripTags(t.Get(a, h, "ConsoleInventory", 0x839f7939)));
            const int cols = std::max(1, sw / 84);
            row(static_cast<int>((v.gems.size() + cols - 1) / cols) * 84 + 10, [&](int y) {
                for (size_t k = 0; k < v.gems.size(); ++k) {
                    const auto& g = v.gems[k];
                    const int xx = sx + static_cast<int>(k % cols) * 84;
                    const int gy = y + static_cast<int>(k / cols) * 84;
                    Image ic = IconFit(t.Icon(a, h, g.icon, false), 64, 64);
                    d3_img::Blit(img, ic, xx, gy);
                    if (g.rank >= 0)
                        Txt(img, a.sans, 30, xx + 62, gy + 34, std::to_string(g.rank), White, 2, 2);
                }
            });
        }
        heading(StripTags(t.Get(a, h, "ConsoleSkills", 0x882532a7))); // Powers
        for (const auto& power : v.cube) {
            const bool empty = v.cube_valid && power.gbid == 0xFFFFFFFF;
            std::string nm;
            if (empty)
                nm = StripTags(t.Get(a, h, "ConsoleSkills", 0x1403fdbe)); // None Assigned
            else if (v.cube_valid && !power.gb_name.empty())
                nm = StripTags(t.Get(a, h, "Items", std::string_view{power.gb_name}));
            if (nm.empty())
                nm = "—"; // unavailable is distinct from an explicitly empty slot
            const auto ls = Wrap(a.sans, 30, nm, sw - 76, 3);
            const int height = std::max(76, static_cast<int>(ls.size()) * 32 + 12);
            row(height, [&](int y) {
                if (v.cube_valid && !empty) {
                    Image ic = IconFit(t.Icon(a, h, power.icon, false), 64, 64);
                    BlitC(img, ic, sx + 32, y + height / 2);
                }
                y = BlockTop(a.sans, 30, ls, 32, y + height / 2);
                for (const auto& line : ls)
                    Txt(img, a.sans, 30, sx + 76, y, line, empty || nm == "—" ? Grey : Legend), y += 32;
            });
        }
        if (more)
            arrow(false, 940);
    }
    return img;
}

} // namespace d3hg
