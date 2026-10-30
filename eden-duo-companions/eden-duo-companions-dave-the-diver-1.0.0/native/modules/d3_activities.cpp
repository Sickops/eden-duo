// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_activities.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "d3_hg_draw.h"

namespace d3_activities {
namespace {
using d3_img::Image;
using u8 = std::uint8_t;
using u32 = std::uint32_t;

// Colours from the approved A text revision (QUALITY.md); shared with the Hero/Gear tabs via
// d3_hg_draw.h except the three below, which only this file's bounty/rift cards use.
using d3hg::draw::White;
using d3hg::draw::Gold;
using d3hg::draw::Grey;
using d3hg::draw::LGrey;
constexpr u32 Bounty = 0xFFEA89FFu, Green = 0x82DC5AFFu, Red = 0xFF5050FFu;
using d3_view::RomanNumerals;

using d3hg::draw::Txt;
using d3hg::draw::TxtMid;
using d3hg::draw::Half;
using d3hg::draw::ScaleAlpha;
using d3hg::draw::Desaturate;
void Center(Image& dst, const Image& s, int cx, int y) {
    if (!s.Empty())
        d3_img::Blit(dst, s, cx - s.w / 2, y);
}

// Native GR meter at 1x (TimerBase + TimerBack + KillMeter clipped + TimerTop), then k x nearest.
Image Meter(d3_assets::Assets& a, const EdenDsmodHostApi* h, int pct, int k) {
    Image base = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerBase");
    if (base.Empty())
        return {};
    Image back = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerBack");
    Image meter = a.Frame(h, "2DUITieredRifts", "TieredRifts_KillMeter");
    Image top = a.Frame(h, "2DUITieredRifts", "TieredRifts_TimerTop");
    Image m = base;
    const int cx = m.w / 2, cy = 52;
    if (!back.Empty())
        d3_img::Blit(m, back, cx - back.w / 2, cy - back.h / 2);
    if (!meter.Empty()) {
        const int fw = meter.w * std::clamp(pct, 0, 100) / 100;
        if (fw > 0)
            d3_img::Blit(m, meter, cx - meter.w / 2, cy - meter.h / 2, 1, 0, 0, fw, meter.h);
    }
    if (!top.Empty())
        d3_img::Blit(m, top, cx - top.w / 2, cy - top.h / 2);
    if (k <= 1)
        return m;
    Image o(m.w * k, m.h * k);
    d3_img::Blit(o, m, 0, 0, k);
    return o;
}

// design A gr_big: box (6, 6) .. (BodyW - 6, 700)
void RiftCard(Image& img, d3_assets::Assets& a, const EdenDsmodHostApi* h, const d3_view::RiftView& r) {
    const int x0 = 6, y0 = 6, x1 = BodyW - 6, cw = x1 - x0;
    Image art = a.Frame(h, "2DUITieredRifts", "#c73bbba6");
    const int artw = art.Empty() ? 480 : art.w, arth = art.Empty() ? 530 : art.h;
    if (!art.Empty())
        d3_img::Blit(img, art, x0, y0);
    const int rx = x0 + artw + 10, rw = x1 - rx, rc = rx + rw / 2;
    int y = y0 + 20;
    const bool gr = r.tier > 0;
    // RULE header: Exocet 45, else 36, 1 line in the right block
    {
        const std::string t = gr ? "GREATER RIFT" : "NEPHALEM RIFT";
        const float pt = a.exo.Measure(a.exo.SizeIndex(45), t) <= rw - 20 ? 45.0f : 36.0f;
        Txt(img, a.exo, pt, rc, y, t, Gold, 1, 2);
    }
    if (gr) {
        Txt(img, a.exo, 75, rc, y0 + 82, std::to_string(r.tier), White, 1, 2);
        if (r.secs_left >= 0) {
            char t[16];
            std::snprintf(t, sizeof t, "%d:%02d", r.secs_left / 60, r.secs_left % 60);
            Txt(img, a.exo, 60, rc, y0 + 186, t, r.secs_left > 0 ? White : Red, 1, 2);
        }
        y = y0 + 290;
        Image skull = a.Frame(h, "2DUIConsole_HUD", "Console_HUD_Death");
        if (!skull.Empty())
            d3_img::Blit(img, Half(skull, true), rx, y);
        TxtMid(img, a.sans, 36, rx + 64, y + 24, "Deaths", Grey, 0);
        TxtMid(img, a.sans, 36, x1 - 10, y + 24, std::to_string(r.deaths), White, 2);
        y += 66;
    } else {
        y = y0 + 120;
    }
    Image boss = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBoss");
    if (!boss.Empty()) {
        boss = Half(boss, true);
        if (r.guardian == 0)
            Desaturate(boss, 0.85f);
        d3_img::Blit(img, boss, rx + 2, y + 6);
    }
    TxtMid(img, a.sans, 36, rx + 64, y + 24, "Rift Guardian", r.guardian == 1 ? White : Grey, 0);
    y += 56;
    {
        const std::string st = r.guardian == 2 ? "Slain" : r.guardian == 1
                                   ? "Alive " + std::to_string(r.guardian_pct) + "%" : "Not yet";
        Txt(img, a.sans, 36, x1 - 10, y, st, r.guardian == 2 ? Green : r.guardian == 1 ? Red : Grey, 2);
        y += 56;
    }
    if (gr && r.best_solo >= 0) {
        Txt(img, a.sans, 36, rx, y, "Best Solo", Grey, 0);
        Txt(img, a.sans, 36, x1 - 10, y, std::to_string(r.best_solo), White, 2);
        y += 56;
    }
    // kill meter at 2x under the art, percentage below
    const int my = y0 + arth + 16;
    Image m = Meter(a, h, r.pct, 2);
    int mh = 212;
    if (!m.Empty()) {
        if (m.w > cw) // 784 px fits the 966 px card; keep the guard for narrower canvases
            m = Meter(a, h, r.pct, 1);
        Center(img, m, x0 + cw / 2, my);
        mh = m.h;
    }
    Txt(img, a.sans, 36, x0 + cw / 2, my + mh + 4, std::to_string(std::clamp(r.pct, 0, 100)) + "%", White, 1, 2);
    if (r.difficulty >= 0)
        Txt(img, a.sans, 30, x0 + cw / 2, my + mh + 60, d3_view::DifficultyName(r.difficulty), LGrey, 1);
}

// design A bounty_board, full body height: left half acts I-V (pips, cache), right half the
// current act's bounties (RULE: sans 30, max 2 lines, row height follows).
void BountyBoard(Image& img, d3_assets::Assets& a, const EdenDsmodHostApi* h, const d3_view::BountyView& b) {
    const int x0 = 6, y0 = 0, x1 = BodyW - 6, cw = x1 - x0, hh = BodyH - y0;
    d3_img::Blit(img, d3hg::draw::DoublePanel(a, h, cw, hh, "Red", true), x0, y0);
    const int lw = cw / 2;
    TxtMid(img, a.exo, 36, x0 + lw / 2, y0 + 41, "BOUNTIES", Gold, 1);
    const bool act_ok = b.act >= 1 && b.act <= 5;
    TxtMid(img, a.exo, 36, x0 + lw + lw / 2, y0 + 41,
         act_ok ? std::string{"ACT "} + RomanNumerals[b.act] : std::string{"ACT"}, Gold, 1);
    Image on = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_BountyIconComplete");
    Image off = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_BountyIcon");
    Image cache = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerReward");
    Image cache_done = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerRewardComplete");
    Image sel = a.Frame(h, "2DUIConsoleWaypoint", "Console_Waypoint_Selection");
    for (Image* p : {&on, &off, &cache, &cache_done})
        if (!p->Empty())
            *p = Half(*p, true);
    int yy = y0 + 64;
    const int pip_x0 = x0 + 30 + a.exo.Measure(a.exo.SizeIndex(36), "ACT III") + 14;
    for (int act = 1; act <= 5; ++act) {
        const bool cur = act == b.act;
        if (cur && !sel.Empty())
            d3_img::Blit(img, d3_img::Extend(sel, lw - 30, 60, 40, 300, 14, 30), x0 + 16, yy - 4);
        TxtMid(img, a.exo, 36, x0 + 30, yy + 26, std::string{"ACT "} + RomanNumerals[act], cur ? Gold : LGrey, 0);
        for (int i = 0; i < 5; ++i) {
            const Image& p = i < b.done[act] ? on : off;
            if (!p.Empty())
                d3_img::Blit(img, p, pip_x0 + i * 33, yy + 14);
        }
        const Image& c = b.done[act] >= 5 ? cache_done : cache;
        if (!c.Empty())
            d3_img::Blit(img, c, pip_x0 + 5 * 33 + 2, yy + 12);
        yy += 58;
    }
    // current act list
    yy = y0 + 62;
    const int lx = x0 + lw + 20;
    Image mk_open = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBounty");
    Image mk_done = a.Frame(h, "2DUIConsoleWaypoint", "ConsoleWaypointMap_MarkerBountyComplete");
    if (!mk_open.Empty())
        mk_open = Half(mk_open, true);
    if (!mk_done.Empty())
        mk_done = Half(mk_done, true);
    Image mk_dim = mk_open;
    ScaleAlpha(mk_dim, 0.55f);
    const int si = a.sans.SizeIndex(30);
    if (!act_ok || b.list.empty())
        Txt(img, a.sans, 30, lx, yy, act_ok ? "None in this act" : "Act unknown", LGrey, 0);
    for (const auto& e : b.list) {
        const auto ls = a.sans.Wrap(si, e.name, lw - 84, 2);
        if (yy + 32 * int(ls.size()) > y0 + hh - 60)
            break; // whole rows only (5 bounties per act always fit at 2 lines each)
        const Image& ic = e.state == 2 ? mk_done : e.state == 1 ? mk_open : mk_dim;
        if (!ic.Empty())
            d3_img::Blit(img, ic, lx, yy + 2);
        const u32 col = e.state == 2 ? Grey : e.state == 1 ? Bounty : LGrey;
        for (size_t j = 0; j < ls.size(); ++j)
            Txt(img, a.sans, 30, lx + 38, yy + int(j) * 32, ls[j], col, 0);
        yy += 32 * int(ls.size()) + 10;
    }
}
} // namespace

Image Body(d3_assets::Assets& a, const EdenDsmodHostApi* h, const View& v) {
    Image img(BodyW, BodyH);
    d3_img::Fill(img, 0x060504FF);
    if (v.rift) {
        RiftCard(img, a, h, v.r);
    } else if (!v.adventure) {
        Txt(img, a.exo, 36, BodyW / 2, 400, "BOUNTIES", Gold, 1);
        Txt(img, a.sans, 30, BodyW / 2, 460, "Adventure Mode only", LGrey, 1);
    } else {
        BountyBoard(img, a, h, v.b);
    }
    return img;
}

} // namespace d3_activities
