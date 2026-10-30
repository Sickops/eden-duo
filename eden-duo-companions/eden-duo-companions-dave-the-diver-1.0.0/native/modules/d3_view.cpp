// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_view.h"
#include "d3_hg_draw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace d3_view {
namespace {
using d3_img::Image;
using u8 = std::uint8_t;
using u32 = std::uint32_t;

// Colours sampled by the UI lane (STYLE-SHEET / QUALITY.md), RGBA; shared with the Hero/Gear tabs
// via d3_hg_draw.h except TabDim, which only the nav bar uses.
using d3hg::draw::White;
using d3hg::draw::Gold;
using d3hg::draw::Grey;
using d3hg::draw::LGrey;
constexpr u32 TabDim = 0xD6B270FF;

// Resource type (Resource_Type_Primary/Secondary value) -> label colour and globe atlas.
struct Res {
    const char* name;
    u32 colour;
    const char* globe;
};
Res ResourceInfo(int t) {
    switch (t) {
    case 0:
        return {"Mana", 0x5A78FFFF, "2DUI_WitchDoctorResourceGlobe"};
    case 1:
        return {"Arcane Power", 0x8C64FFFF, "2DUI_WizardResourceGlobe"};
    case 2:
        return {"Fury", 0xE6503CFF, "2DUI_BarbarianResourceGlobe"};
    case 3:
        return {"Spirit", 0xFFE68CFF, "2DUI_MonkResourceGlobe"};
    case 5:
        return {"Hatred", 0xEB5A3CFF, "2DUI_DemonHunterResourceGlobe"};
    case 6:
        return {"Discipline", 0x6E96FFFF, "2DUI_DemonHunterResourceGlobe"};
    case 7:
        return {"Wrath", 0xFFD25AFF, "2DUI_CrusaderResourceGlobe"};
    case 8:
        return {"Essence", 0x3CC8BEFF, "2DUI_NecromancerResourceGlobe"};
    default:
        return {"", White, ""};
    }
}

using d3hg::draw::Txt;
using d3hg::draw::TxtMid;
using d3hg::draw::Half;
using d3hg::draw::ScaleAlpha;
using d3hg::draw::Ringside;
using d3hg::draw::Desaturate;

// The Map tab's own band math and missing-art fallback colour, vs. the Hero/Gear tabs' two_stage
// default (d3hg::draw::Ringside): single Extend over the whole 40-235 band, flat 0x0E0B08FF when
// the frame art itself could not be loaded.
Image MapRingside(d3_assets::Assets& a, const EdenDsmodHostApi* h, int w, int hh) {
    return Ringside(a, h, w, hh, false, 0x0E0B08FFu);
}

Image ContentPane(d3_assets::Assets& a, const EdenDsmodHostApi* h, const Image& content, int height) {
    Image out(ColW, ColH); // The rest stays transparent so the map is visible below the pane.
    // The native lower ornament occupies the final 110 pixels; keep content above it.
    d3_img::Blit(out, MapRingside(a, h, ColW, std::clamp(height + 80, 270, ColH)), 0, 0);
    d3_img::Blit(out, content, 0, 0);
    return out;
}

// Globe: frame 0 of the game's globe atlas at the integer scale whose size is >= px, circle
// masked, inside the native Console_LevelUpCircleSlot ring at 2x.
// fill: liquid rows from the bottom (0..GlobePx); above the level the liquid is drawn at 22 %
// (an empty glass), like the HUD globes draining. boost brightens the deep-red health art.
void RingGlobe(Image& dst, d3_assets::Assets& a, const EdenDsmodHostApi* h, int cx, int cy,
               const std::string& atlas, bool live, int fill, float boost = 1.0f) {
    Image ring = a.Frame(h, "2DUIConsoleGameNotify", "Console_LevelUpCircleSlot");
    Image g = a.Frame(h, atlas, "[0]");
    const int px = 128;
    if (!g.Empty()) {
        const int k = std::max(1, (px + g.w - 1) / g.w);
        Image big(g.w * k, g.h * k);
        d3_img::Blit(big, g, 0, 0, k);
        Image c = d3_img::Crop(big, (big.w - px) / 2, (big.h - px) / 2, px, px);
        d3_img::CircleMask(c, px / 2.0f - 1);
        const int top = px - std::clamp(fill, 0, px);
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                u8* q = c.At(x, y);
                const float f = (y < top ? 0.22f : 1.0f) * boost;
                for (int ch = 0; ch < 3; ++ch)
                    q[ch] = static_cast<u8>(std::min(255.0f, q[ch] * f));
            }
        if (!live)
            Desaturate(c);
        d3_img::Blit(dst, c, cx - px / 2, cy - px / 2);
    }
    if (!ring.Empty())
        d3_img::Blit(dst, ring, cx - ring.w, cy - ring.h, 2);
}
} // namespace

std::string Short(double v) {
    char buf[32];
    if (v >= 1e9)
        std::snprintf(buf, sizeof buf, "%.1fB", v / 1e9);
    else if (v >= 1e6)
        std::snprintf(buf, sizeof buf, v >= 1e7 ? "%.0fM" : "%.1fM", v / 1e6);
    else if (v >= 1e4)
        std::snprintf(buf, sizeof buf, "%.0fK", v / 1e3);
    else
        std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

Vitals Quantise(const d3_reader::Snapshot& s, bool live) {
    Vitals v;
    v.live = live;
    v.cls = s.cls;
    auto q = [](double x) { return std::floor(x + 0.5); };
    v.hp = q(s.hp);
    v.hp_max = q(s.hp_max);
    // the column prints Short(); quantise to what is shown so redraws follow visible changes
    auto shown = [](double x) { return x >= 1e4 ? std::floor(x / 1e3) * 1e3 : std::floor(x + 0.5); };
    v.hp = shown(v.hp);
    v.hp_max = shown(v.hp_max);
    auto level = [](double cur, double mx) {
        return mx > 0 ? static_cast<int>(std::lround(std::clamp(cur / mx, 0.0, 1.0) * GlobePx)) : 0;
    };
    v.hp_fill = level(s.hp, s.hp_max);
    v.res_fill = level(s.res_cur[0], s.res_max[0]);
    for (int i = 0; i < 2; ++i) {
        v.res_type[i] = s.res_type[i];
        v.res_cur[i] = std::floor(s.res_cur[i] + 0.5);
        v.res_max[i] = std::floor(s.res_max[i] + 0.5);
    }
    return v;
}

Image VitalsColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const Vitals& v, bool compact) {
    if (compact) {
        if (!v.live)
            return Image(ColW, ColH);
        Image content(ColW, ColH);
        const int cx = ColW / 2;
        RingGlobe(content, a, h, cx, 105, "2DUI_HealthGlobe_Classic", v.live,
                  v.live ? v.hp_fill : 0, 1.9f);
        Txt(content, a.sans, 30, cx, 186,
             v.live ? Short(v.hp) + " / " + Short(v.hp_max) : "— / —", White, 1);
        int y = 238;
        for (int i = 0; i < 2; ++i) {
            const Res r = ResourceInfo(v.res_type[i]);
            if (!r.name[0])
                continue;
            Txt(content, a.sans, 30, cx, y, r.name, v.live ? r.colour : Grey, 1);
            const std::string value = v.live ? Short(v.res_cur[i]) + " / " + Short(v.res_max[i]) : "— / —";
            Txt(content, a.sans, 30, cx, y + 36, value, v.live ? White : Grey, 1);
            y += 84;
        }
        return ContentPane(a, h, content, y + 24);
    }
    Image img = MapRingside(a, h, ColW, ColH);
    const int cx = ColW / 2;
    int y = 30;
    RingGlobe(img, a, h, cx, y + 75, "2DUI_HealthGlobe_Classic", v.live, v.live ? v.hp_fill : 0, 1.9f);
    y += 150 + 6;
    const u32 val = v.live ? White : Grey;
    Txt(img, a.sans, 36, cx, y, v.live ? Short(v.hp) : "-", val, 1);
    y += 40;
    Txt(img, a.sans, 30, cx, y, "/ " + (v.live ? Short(v.hp_max) : std::string{"-"}), Grey, 1);
    y += 46;
    const Res r0 = ResourceInfo(v.res_type[0]);
    RingGlobe(img, a, h, cx, y + 75, r0.globe[0] ? r0.globe : "2DUI_HealthGlobe_Classic",
              v.live && r0.globe[0], v.live ? v.res_fill : 0);
    y += 150 + 6;
    for (int i = 0; i < 2; ++i) {
        const Res r = ResourceInfo(v.res_type[i]);
        if (!r.name[0])
            continue;
        Txt(img, a.sans, 30, 30, y, r.name, v.live ? r.colour : Grey, 0);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", v.res_cur[i]);
        Txt(img, a.sans, 30, ColW - 30, y, v.live ? buf : "-", val, 2);
        y += 36;
    }
    return img;
}

Image RightColumn() {
    return Image(ColW, ColH); // no placeholder panel before the activity snapshot is available
}

Image RiftMeter(d3_assets::Assets& a, const EdenDsmodHostApi* h, int pct) {
    Image img(MapX1 - MapX0, 150);
    const int cx = img.w / 2;
    Image base = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerBase");
    Image back = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerBack");
    Image meter = a.Frame(h, "2DUITieredRifts", "TieredRifts_KillMeter");
    Image top = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerTop");
    const int y0 = 2;
    int bh = 106;
    if (!base.Empty()) {
        d3_img::Blit(img, base, cx - base.w / 2, y0);
        bh = base.h;
    }
    const int my = y0 + 52; // meter centre inside the base frame
    if (!back.Empty())
        d3_img::Blit(img, back, cx - back.w / 2, my - back.h / 2);
    if (!meter.Empty()) {
        const int fw = meter.w * std::clamp(pct, 0, 100) / 100;
        if (fw > 0)
            d3_img::Blit(img, meter, cx - meter.w / 2, my - meter.h / 2, 1, 0, 0, fw, meter.h);
    }
    if (!top.Empty())
        d3_img::Blit(img, top, cx - top.w / 2, my - top.h / 2);
    Txt(img, a.sans, 36, cx, y0 + bh - 2, std::to_string(std::clamp(pct, 0, 100)) + "%", White, 1, 2);
    return img;
}

Image RiftColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const RiftView& r) {
    // design A gr_column: box (6, 10) .. (ColW - 6, 980) inside the column
    Image img(ColW, ColH);
    const int x0 = 6, y0 = 10, x1 = ColW - 6, cw = x1 - x0, cx = x0 + cw / 2;
    int y = y0;
    if (r.tier > 0) {
        // RULE panel title: Exocet 36, max 2 lines in the column
        const auto tl = a.exo.Wrap(a.exo.SizeIndex(36), "GREATER RIFT", cw - 24, 2);
        for (size_t j = 0; j < tl.size(); ++j)
            Txt(img, a.exo, 36, cx, y0 + 20 + int(j) * 40, tl[j], Gold, 1, 2);
        const int ty = y0 + 20 + 40 * int(tl.size());
        Txt(img, a.exo, 60, cx, ty + 4, std::to_string(r.tier), White, 1, 2);
        if (r.secs_left >= 0) {
            char t[16];
            std::snprintf(t, sizeof t, "%d:%02d", r.secs_left / 60, r.secs_left % 60);
            Txt(img, a.exo, 45, cx, ty + 78, t, r.secs_left > 0 ? White : 0xFF5050FFu, 1, 2);
        }
        y = ty + (r.secs_left >= 0 ? 150 : 88);
    } else {
        const auto tl = a.exo.Wrap(a.exo.SizeIndex(36), "NEPHALEM RIFT", cw - 24, 2);
        y += 20;
        for (const auto& l : tl) {
            Txt(img, a.exo, 36, cx, y, l, Gold, 1, 2);
            y += 40;
        }
        y += 20;
    }
    if (r.tier > 0) {
        Image skull = a.Frame(h, "2DUIConsole_HUD", "Console_HUD_Death");
        if (!skull.Empty())
            d3_img::Blit(img, Half(skull, true), x0 + 14, y);
        TxtMid(img, a.sans, 30, x0 + 70, y + 24, "Deaths", Grey, 0);
        TxtMid(img, a.sans, 36, x1 - 14, y + 24, std::to_string(r.deaths), White, 2);
        y += 60;
    }
    {
        Image boss = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBoss");
        if (!boss.Empty()) {
            boss = Half(boss, true);
            if (r.guardian == 0)
                Desaturate(boss, 0.85f);
            d3_img::Blit(img, boss, x0 + 14, y + 6);
        }
        // RULE: wrap to 2 lines inside the column
        const auto gl = a.sans.Wrap(a.sans.SizeIndex(30), "Rift Guardian", x1 - 14 - (x0 + 70), 2);
        for (size_t j = 0; j < gl.size(); ++j)
            Txt(img, a.sans, 30, x0 + 70, y + 8 + int(j) * 32 - (gl.size() > 1 ? 16 : 0), gl[j],
                 r.guardian == 1 ? White : Grey, 0);
        y += 66;
        // state on its own right-aligned line (label + value do not fit side by side)
        std::string st = r.guardian == 2 ? "Slain" : r.guardian == 1
                             ? "Alive " + std::to_string(r.guardian_pct) + "%" : "Not yet";
        Txt(img, a.sans, 30, x1 - 14, y, st, r.guardian == 2 ? 0x82DC5AFFu : r.guardian == 1 ? 0xFF5050FFu : Grey, 2);
        y += 44;
    }
    // Best Solo belongs on Activities; keep only the current run in the map pane.
    if (r.difficulty >= 0) {
        Txt(img, a.sans, 30, ColW / 2, y, DifficultyName(r.difficulty), LGrey, 1);
        y += 44;
    }
    return ContentPane(a, h, img, y + 26);
}

Image BountyColumn(d3_assets::Assets& a, const EdenDsmodHostApi* h, const BountyView& b) {
    // design A bounty_column: box (18, 22) .. (ColW - 16, 980) inside the column
    Image img(ColW, ColH);
    const int x0 = 18, y0 = 22, x1 = ColW - 16, cw = x1 - x0, cx = x0 + cw / 2, y1 = 910;
    Txt(img, a.exo, 36, cx, y0 + 12, "BOUNTIES", Gold, 1);
    const bool act_ok = b.act >= 1 && b.act <= 5;
    if (act_ok) {
        Txt(img, a.sans, 30, cx, y0 + 62, std::string{"Act "} + RomanNumerals[b.act], Gold, 1);
        Image on = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_BountyIconComplete");
        Image off = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_BountyIcon");
        if (!on.Empty() && !off.Empty()) {
            on = Half(on, true), off = Half(off, true);
            const int px = cx - (5 * 40) / 2 + 20;
            for (int i = 0; i < 5; ++i) {
                const Image& p = i < b.done[b.act] ? on : off;
                d3_img::Blit(img, p, px + i * 40 - p.w / 2, y0 + 120 - p.h / 2);
            }
        }
    }
    int y = y0 + 154;
    if (!act_ok) {
        const std::string s = b.total > 0 ? std::to_string(b.total) + " in this game" : "None";
        Txt(img, a.sans, 30, cx, y, s, LGrey, 1);
        return ContentPane(a, h, img, y + 70);
    }
    if (b.list.empty()) {
        Txt(img, a.sans, 30, cx, y, "None in this act", LGrey, 1);
        y += 40;
    }
    Image mk_open = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBounty");
    Image mk_done = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBountyComplete");
    if (!mk_open.Empty())
        mk_open = Half(mk_open, true);
    if (!mk_done.Empty())
        mk_done = Half(mk_done, true);
    Image mk_dim = mk_open;
    ScaleAlpha(mk_dim, 0.55f);
    const int si = a.sans.SizeIndex(30);
    for (const auto& e : b.list) {
        // RULE bounty title: sans 30, max 4 lines, ellipsis
        const auto lines = a.sans.Wrap(si, e.name, cw - 40, 4);
        if (y + 34 * int(lines.size()) > y1 - 40) {
            // RULE list overflow: whole entries only + the native scroll arrow
            Image arrow = a.Frame(h, "2DUIConsoleOptions", "Console_Waypoint_ScrollArrow");
            if (!arrow.Empty())
                d3_img::Blit(img, arrow, cx - arrow.w / 2, y1 - 10 - arrow.h);
            y = ColH - 28;
            break;
        }
        const Image& ic = e.state == 2 ? mk_done : e.state == 1 ? mk_open : mk_dim;
        if (!ic.Empty())
            d3_img::Blit(img, ic, x0 - 4, y + 2);
        const u32 col = e.state == 2 ? Grey : e.state == 1 ? 0xFFEA89FFu : LGrey;
        int yy = y;
        for (const auto& l : lines) {
            Txt(img, a.sans, 30, x0 + 34, yy, l, col, 0);
            yy += 34;
        }
        y = yy + 12;
    }
    return ContentPane(a, h, img, y + 28);
}

int NavIndex(std::string_view page_id) {
    static constexpr std::array<std::string_view, 5> kIds{"hero", "activities", "map", "gear", "options"};
    for (size_t i = 0; i < kIds.size(); ++i)
        if (page_id == kIds[i])
            return static_cast<int>(i);
    return 2; // map
}

Image NavBar(d3_assets::Assets& a, const EdenDsmodHostApi* h, int active, int mask) {
    Image img(CanvasW, CanvasH - NavY);
    d3_img::Fill(img, 0x060504FF);
    Image center = a.Frame(h, "2DUIConsoleNavMenu", "Console_NavBar_Center");
    Image lcap = a.Frame(h, "2DUIConsoleNavMenu", "#6054c9d6");
    Image rcap = a.Frame(h, "2DUIConsoleNavMenu", "#6b5ccc49");
    Image glow = a.Frame(h, "2DUIConsoleNavMenu", "Console_NavBar_Btn_Straight");
    const int y = 1003 - NavY;
    const int x0 = (CanvasW - (center.w + lcap.w + rcap.w)) / 2;
    d3_img::Blit(img, lcap, x0, y);
    d3_img::Blit(img, center, x0 + lcap.w, y);
    d3_img::Blit(img, rcap, x0 + lcap.w + center.w, y);
    // Options is a regular cell at the right of the centre piece, styled like the other tabs (no
    // more cog in the end cap, which read as an arrow); the end caps are decoration only, no hit.
    static const char* labels[5] = {"HERO", "ACTIVITIES", "MAP", "GEAR", "OPTIONS"};
    int present[5], n = 0;
    for (int i = 0; i < 5; ++i)
        if (mask & (1 << i))
            present[n++] = i;
    // equal space per present label across the centre piece (same arithmetic as gen_manifest nav_layout)
    int labels_w = 0;
    for (int slot = 0; slot < n; ++slot)
        labels_w += NavLabelW[present[slot]];
    const int span = NavCellsX1 - NavCellsX0, gap = n ? std::max(0, span - labels_w) / n : 0;
    int cx0 = NavCellsX0 + (span - labels_w - gap * n) / 2;
    for (int slot = 0; slot < n; ++slot) {
        const int i = present[slot];
        const int w = NavLabelW[i] + gap, cx = cx0 + w / 2;
        if (i == active && !glow.Empty())
            d3_img::Blit(img, glow, cx - glow.w / 2, y + 35 - glow.h / 2);
        TxtMid(img, a.exo, 36, cx, y + 35, labels[i], i == active ? White : TabDim, 1);
        cx0 += w;
    }
    return img;
}

Image Title(d3_assets::Assets& a, const std::string& title, const std::string& sub) {
    Image img(MapX1 - MapX0, 150);
    const int maxw = img.w - 40, cx = img.w / 2;
    int y = 20;
    int si = a.exo.SizeIndex(45);
    std::vector<std::string> lines;
    if (si >= 0 && a.exo.Measure(si, title) <= maxw) {
        lines = {title};
    } else {
        si = a.exo.SizeIndex(36);
        lines = a.exo.Wrap(si, title, maxw, 2);
    }
    const float pt = si == a.exo.SizeIndex(45) ? 45.0f : 36.0f;
    for (const auto& l : lines) {
        Txt(img, a.exo, pt, cx, y, l, Gold, 1, 2);
        y += a.exo.LineHeight(si);
    }
    if (!sub.empty())
        Txt(img, a.sans, 30, cx, y + 4, sub, LGrey, 1, 2);
    return img;
}

Image PlayerMarker(d3_assets::Assets& a, const EdenDsmodHostApi* h) {
    // The native minimap draws the local hero with icon MarkerLocalPlayer (main+0x3E9D5C), frame 0
    // of 2DMapGeneric: a grey compass ring with a blue core, pointer up. The raw 64 px frame carries
    // a lot of transparent padding around that small badge (ink only ~26 x 34): a flat integer Blit
    // of the whole frame scaled the padding right along with it and the marker still read no
    // heavier than the other map markers beside it (G3 revise). Fit (ink-trimmed, upscaled) to
    // PlayerMarkerPx instead, same technique as the rail's own icon fit: the ink alone lands near
    // the ~85 px nominal frame every other marker already uses, so the hero reads as the densest,
    // heaviest marker without the old padding diluting it; centred so the page's rotation turns it
    // about the core.
    Image out(160, 160);
    Image f = a.Frame(h, "2DMapGeneric", "MarkerLocalPlayer");
    if (f.Empty())
        f = a.Frame(h, "2DMapMarker_Player", ""); // the game's player dot (atlas missing)
    const Image fit = d3hg::draw::IconFit(f, PlayerMarkerPx, PlayerMarkerPx, true);
    d3_img::Blit(out, fit, (out.w - fit.w) / 2, (out.h - fit.h) / 2);
    return out;
}

Image PauseOverlay(d3_assets::Assets& a, const EdenDsmodHostApi* h, int difficulty) {
    Image img(CanvasW, CanvasH);
    d3_img::Fill(img, 0x000000A6); // 65 % dim over the page (alpha blended by the host)
    if (auto logo = a.Tex(h, "2DUIConsoleLogo_x1_1"); logo && !logo->img.Empty()) {
        const Image& t = logo->img;
        int x0 = t.w, y0 = t.h, x1 = -1, y1 = -1; // alpha bbox
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x)
                if (t.At(x, y)[3])
                    x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
        if (x1 >= x0) {
            Image half((x1 - x0 + 1) / 2, (y1 - y0 + 1) / 2);
            for (int y = 0; y < half.h; ++y)
                for (int x = 0; x < half.w; ++x)
                    for (int ch = 0; ch < 4; ++ch)
                        half.At(x, y)[ch] = static_cast<u8>(
                            (t.At(x0 + 2 * x, y0 + 2 * y)[ch] + t.At(x0 + 2 * x + 1, y0 + 2 * y)[ch] +
                             t.At(x0 + 2 * x, y0 + 2 * y + 1)[ch] + t.At(x0 + 2 * x + 1, y0 + 2 * y + 1)[ch] + 2) / 4);
            d3_img::Blit(img, half, CanvasW / 2 - half.w / 2, 440 - half.h / 2);
        }
    }
    Txt(img, a.exo, 45, CanvasW / 2, 780, "GAME PAUSED", White, 1, 2);
    std::string mode = "Adventure Mode";
    if (difficulty >= 0)
        mode += " - " + DifficultyName(difficulty);
    Txt(img, a.sans, 36, CanvasW / 2, 840, mode, Gold, 1, 2);
    return img;
}

Image StateCard(d3_assets::Assets& a, const std::string& l1, const std::string& l2) {
    Image img(MapX1 - MapX0, 160);
    Txt(img, a.exo, 36, img.w / 2, 30, l1, Gold, 1, 2);
    if (!l2.empty())
        Txt(img, a.sans, 30, img.w / 2, 90, l2, LGrey, 1, 2);
    return img;
}

void WorldToScreen(float wx, float wy, float& sx, float& sy) {
    constexpr float r = 0.70710678f;
    sx = (-wx + wy) * r;
    sy = (wx + wy) * r;
}

namespace {
// Area-weighted resize (premultiplied, so transparent texels add no dark fringe).
Image ResizeArea(const Image& s, int w, int h) {
    Image out(w, h);
    const double fx = double(s.w) / w, fy = double(s.h) / h;
    for (int y = 0; y < h; ++y) {
        const double y0 = y * fy, y1 = (y + 1) * fy;
        for (int x = 0; x < w; ++x) {
            const double x0 = x * fx, x1 = (x + 1) * fx;
            double acc[4] = {0, 0, 0, 0}, area = 0;
            for (int sy = int(y0); sy < std::min(s.h, int(std::ceil(y1))); ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                for (int sx = int(x0); sx < std::min(s.w, int(std::ceil(x1))); ++sx) {
                    const double wgt = wy * (std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                    const u8* p = s.At(sx, sy);
                    const double pa = p[3] * wgt;
                    acc[0] += p[0] * pa, acc[1] += p[1] * pa, acc[2] += p[2] * pa, acc[3] += pa;
                    area += wgt;
                }
            }
            u8* d = out.At(x, y);
            if (acc[3] > 0)
                for (int ch = 0; ch < 3; ++ch)
                    d[ch] = static_cast<u8>(std::lround(acc[ch] / acc[3]));
            d[3] = static_cast<u8>(std::lround(area > 0 ? acc[3] / area : 0));
        }
    }
    return out;
}

// An icon at IconScaleNum / IconScaleDen (whole textures first normalised to IconQuadTexels):
// integer nearest enlargement, then an area-weighted reduction to the exact size, so edges stay
// crisp without the uneven doubled rows of a fractional nearest scale. Centred like the native
// draw (frame centre on the marker position).
Image IconScaled(const Image& src, bool whole_texture) {
    if (src.Empty())
        return {};
    const int base = whole_texture ? std::max(src.w, src.h) : IconQuadTexels;
    const int num = IconScaleNum * IconQuadTexels, den = IconScaleDen * base;
    const int w = std::max(1, src.w * num / den), h = std::max(1, src.h * num / den);
    const int up = (num + den - 1) / den;
    Image big(src.w * up, src.h * up);
    d3_img::Blit(big, src, 0, 0, up);
    return big.w == w && big.h == h ? big : ResizeArea(big, w, h);
}
} // namespace

Image ObjectiveRing(d3_assets::Assets& a, const EdenDsmodHostApi* h) {
    // MapIconMarkerDestination: the quest/bounty "go here" marker the native minimap/full map
    // pulses (confirmed by decoding the game's own SNO name table: group 44 id 81058, a
    // glowing yellow ring, not MarkerQuestRing -- that one decodes blue). IconScaled is the same
    // whole-texture normalisation every other marker in MapPicture draws at, so the resting
    // (scale_bind=1) size matches what a static bake of this marker would have looked like.
    return IconScaled(a.Frame(h, "MapIconMarkerDestination", ""), true);
}

bool ActorInRadius(const std::map<u32, u32>& tags, const std::map<u32, u32>& placement,
                   float distance_sq) {
    for (u32 key : {0x70004u, 0x70005u}) {
        const u32 marker_key = key == 0x70004 ? 0x86004 : 0x86005;
        u32 raw = 0;
        if (auto it = placement.find(marker_key); it != placement.end())
            raw = it->second;
        else if (auto at = tags.find(key); at != tags.end())
            raw = at->second;
        float distance;
        std::memcpy(&distance, &raw, sizeof distance);
        if (!std::isfinite(distance) || distance < 0)
            return false;
        if (distance > 0.00001f && (key == 0x70004 ? distance_sq > distance * distance
                                                   : distance_sq < distance * distance))
            return false;
    }
    return true;
}

Image MapPicture(d3_assets::Assets& a, const EdenDsmodHostApi* h,
                 const std::vector<d3_reader::SceneMask>& scenes, int ax, int ay, int k,
                 const std::vector<d3_reader::Snapshot::Marker>& markers,
                 const std::vector<d3_reader::Snapshot::ActorMarker>& actors) {
    Image img(MapImgW, MapImgH);
    constexpr float r = 0.70710678f;
    const float MapScale = ScaleFor(k);
    // global map pixel of the picture's top-left pixel; pixel g samples screen units (g + 0.5) / s
    const int gx0 = ax - MapImgW / 2, gy0 = ay - MapImgH / 2;
    for (const auto& s : scenes) {
        auto tile = a.SceneTile(h, s.tex_sno);
        if (!tile || tile->img.Empty())
            continue;
        const Image& t = tile->img;
        // valid tile area: 0.8 px per world unit from the top-left (192 of 256 for 240 units)
        const float sw = s.x1 - s.x0, sh = s.y1 - s.y0;
        const float tex_per_unit = 0.8f;
        // picture-pixel bbox of the scene rect
        float xs[4], ys[4];
        const float wx[4] = {s.x0, s.x1, s.x0, s.x1}, wy[4] = {s.y0, s.y0, s.y1, s.y1};
        for (int c = 0; c < 4; ++c) {
            WorldToScreen(wx[c], wy[c], xs[c], ys[c]);
            xs[c] = xs[c] * MapScale - gx0;
            ys[c] = ys[c] * MapScale - gy0;
        }
        const int bx0 = std::max(0, int(std::floor(*std::min_element(xs, xs + 4))));
        const int bx1 = std::min(MapImgW, int(std::ceil(*std::max_element(xs, xs + 4))));
        const int by0 = std::max(0, int(std::floor(*std::min_element(ys, ys + 4))));
        const int by1 = std::min(MapImgH, int(std::ceil(*std::max_element(ys, ys + 4))));
        if (bx0 >= bx1 || by0 >= by1)
            continue;
        const float cw = sw / s.w, ch = sh / s.h; // world units per mask cell
        auto cell = [&](int ix, int iy) -> float {
            ix = std::clamp(ix, 0, int(s.w) - 1);
            iy = std::clamp(iy, 0, int(s.h) - 1);
            const u32 i = u32(ix) * s.h + u32(iy);
            return ((s.mask[i >> 2] >> (6 - 2 * (i & 3))) & 3) == 1 ? 1.0f : 0.0f;
        };
        for (int py = by0; py < by1; ++py)
            for (int px = bx0; px < bx1; ++px) {
                // pixel -> screen units -> world (the transform is its own inverse)
                const float ssx = (gx0 + px + 0.5f) / MapScale;
                const float ssy = (gy0 + py + 0.5f) / MapScale;
                const float wxp = (-ssx + ssy) * r, wyp = (ssx + ssy) * r;
                const float lx = wxp - s.x0, ly = wyp - s.y0;
                if (lx < 0 || ly < 0 || lx >= sw || ly >= sh)
                    continue;
                if (s.tracked) {
                    // bilinear mask at cell centres, thresholded: crisp contour, no blur
                    const float fx = lx / cw - 0.5f, fy = ly / ch - 0.5f;
                    const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
                    const float tx = fx - ix, ty = fy - iy;
                    const float m = cell(ix, iy) * (1 - tx) * (1 - ty) + cell(ix + 1, iy) * tx * (1 - ty) +
                                    cell(ix, iy + 1) * (1 - tx) * ty + cell(ix + 1, iy + 1) * tx * ty;
                    if (m < 0.5f)
                        continue;
                }
                // tile texel: row = world X, column = world Y (the game's transposed layout); every
                // scene tile has 0.8 texels per world unit from its top-left corner
                const int trow = int(lx * tex_per_unit), tcol = int(ly * tex_per_unit);
                if (trow >= t.h || tcol >= t.w)
                    continue;
                const u8* src = t.At(tcol, trow);
                if (src[3] == 0)
                    continue;
                d3_img::BlendPx(img.At(px, py), src);
            }
    }
    static bool traced_actors = false;
    if (!traced_actors && !actors.empty() && std::getenv("D3_SKILL_TRACE") && h && h->log) {
        traced_actors = true;
        for (const auto& actor : actors) {
            const auto tags = a.ActorMapTags(h, actor.actor_sno);
            char line[512];
            std::snprintf(line, sizeof line,
                          "D3 map actor: sno=%u name=%s set=%08x index=%d conversation=%d quest=%d hostile=%d override=%08x tags=%zu icon=%08x enabled=%08x",
                          actor.actor_sno, a.SnoName(actor.actor_sno).c_str(), actor.marker_set_sno,
                          actor.marker_set_index, actor.conversation_icon, actor.quest_monster,
                          actor.hostile, actor.override_tex, tags.size(),
                          tags.contains(0x70000) ? tags.at(0x70000) : 0xFFFFFFFFu,
                          tags.contains(0x70006) ? tags.at(0x70006) : 0xFFFFFFFFu);
            h->log(h->userdata, EDEN_DSMOD_LOG_INFO, line);
        }
    }
    struct Icon {
        Image art;
        float x, y;
        bool whole = true; // a whole texture (false: an atlas frame)
    };
    std::vector<Icon> icons;
    for (const auto& mk : markers) {
        const std::string name = a.SnoName(mk.tex_sno);
        if (name.empty())
            continue;
        // The quest/bounty objective "go here" marker (MapIconMarkerDestination, a pulsing yellow
        // ring in the native UI) is drawn instead as a manifest overlay animated by scale_bind /
        // tint_bind off the module's own published offsets (ObjectiveRing, Sample()'s map block
        // in 01001B300B9BE000.cpp) -- baking a second, static copy here would double it up.
        if (name == "MapIconMarkerDestination")
            continue;
        Image art = a.Frame(h, name, mk.frame >= 0 ? "[" + std::to_string(mk.frame) + "]" : "");
        if (!art.Empty())
            icons.push_back({std::move(art), mk.x, mk.y, mk.frame < 0});
    }
    // Dedup key for "an actor already has a map icon from `markers`" (same texture, same spot
    // within 0.1 world units): a hash set built once instead of an O(actors x markers) any_of
    // scan per actor.
    auto quant_key = [](u32 tex, float x, float y) -> std::uint64_t {
        const u32 qx = static_cast<u32>(std::lround(x * 10.0f));
        const u32 qy = static_cast<u32>(std::lround(y * 10.0f));
        return (std::uint64_t(tex) * 0x9E3779B97F4A7C15ull) ^
               (std::uint64_t(qx) * 0xC2B2AE3D27D4EB4Full) ^ std::uint64_t(qy);
    };
    std::unordered_set<std::uint64_t> marker_keys;
    marker_keys.reserve(markers.size());
    for (const auto& m : markers)
        marker_keys.insert(quant_key(m.tex_sno, m.x, m.y));
    for (const auto& actor : actors) {
        // Hostile and quest-monster icons stay out (elites only as the game reveals them).
        if (actor.quest_monster || actor.hostile)
            continue;
        const auto tags = a.ActorMapTags(h, actor.actor_sno);
        const auto placement = a.MarkerMapTags(h, actor.marker_set_sno, actor.marker_set_index);
        // Nonempty native Condition definitions need evaluation; do not guess their result.
        if (auto it = tags.find(0x70006); it != tags.end() && it->second != 0xFFFFFFFFu)
            continue;
        if (!ActorInRadius(tags, placement, actor.distance_sq))
            continue;
        // main+0x6F6020: a texture (live MinimapIconOverride, MarkerSet 0x86000, Actor 0x70000)
        // wins; else an NPC's Conversation_Icon picks a 2DMapGeneric icon (1 !, 2 ?, 3 *, 5 blue !).
        u32 texture = 0xFFFFFFFFu;
        if (a.IsTextureSno(actor.override_tex))
            texture = actor.override_tex;
        else if (auto it = placement.find(0x86000); it != placement.end())
            texture = it->second;
        else if (auto at = tags.find(0x70000); at != tags.end())
            texture = at->second;
        Image art;
        bool whole = true;
        if (texture && texture != 0xFFFFFFFFu) {
            const bool duplicate = marker_keys.contains(quant_key(texture, actor.x, actor.y));
            const std::string name = a.SnoName(texture);
            if (!duplicate && !name.empty())
                art = a.Frame(h, name, "");
        } else if (const std::string list = a.SnoName(actor.list_tex); !list.empty()) {
            // the icon the game's own map-marker list resolved for this actor
            art = a.Frame(h, list, actor.list_frame >= 0 ? "[" + std::to_string(actor.list_frame) + "]" : "");
            whole = actor.list_frame < 0;
        } else {
            static const char* const talk[] = {nullptr, "MarkerExclamation", "MarkerQuestion",
                                               "MarkerAsterisk", nullptr, "MarkerExclamation_Blue"};
            const int c = actor.conversation_icon;
            if (c >= 1 && c <= 5 && talk[c])
                art = a.Frame(h, "2DMapGeneric", talk[c]), whole = false;
        }
        if (!art.Empty())
            icons.push_back({std::move(art), actor.x, actor.y, whole});
    }
    for (const auto& icon : icons) {
        const Image t = IconScaled(icon.art, icon.whole);
        if (t.Empty())
            continue;
        float sx, sy;
        WorldToScreen(icon.x, icon.y, sx, sy);
        d3hg::draw::BlitC(img, t, MapPixel(sx, k) - gx0, MapPixel(sy, k) - gy0);
    }
    return img;
}

std::string DifficultyName(int d) {
    static const char* roman[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII",
                                  "IX", "X", "XI", "XII", "XIII", "XIV", "XV", "XVI"};
    switch (d) {
    case 0:
        return "Normal";
    case 1:
        return "Hard";
    case 2:
        return "Expert";
    case 3:
        return "Master";
    default:
        break;
    }
    if (d >= 4 && d - 3 <= 16)
        return std::string{"Torment "} + roman[d - 3];
    return {};
}

} // namespace d3_view
