// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Desktop check for the Diablo III views without the emulator: decodes the game's own archives
// from an extracted romfs directory (D3_ROMFS) and writes the composed pictures as PPM/PAM files.
// Usage: dsmod-d3-tool <romfs dir> <out dir> [explored.json from research tools/explored-live.py]

#include <cstdio>
#include <cstdlib>
#include "d3_build.h"
#include "d3_cpk.h"
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "d3_assets.h"
#include "d3_activities.h"
#include "d3_skills.h"
#include "d3_view.h"

namespace {
std::string g_root;
size_t ReadRomfs(void*, const char* path, uint64_t off, void* out, size_t size) {
    std::ifstream f(g_root + "/" + path, std::ios::binary);
    if (!f)
        return 0;
    f.seekg(0, std::ios::end);
    const uint64_t len = static_cast<uint64_t>(f.tellg());
    if (!out)
        return len;
    if (off >= len)
        return 0;
    f.seekg(static_cast<std::streamoff>(off));
    const size_t n = static_cast<size_t>(std::min<uint64_t>(size, len - off));
    f.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
    return n;
}
void Save(const d3_img::Image& img, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f << "P7\nWIDTH " << img.w << "\nHEIGHT " << img.h << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
    f.write(reinterpret_cast<const char*>(img.px.data()), static_cast<std::streamsize>(img.px.size()));
    std::cout << path << " " << img.w << "x" << img.h << "\n";
}
std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        v.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return v;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc < 3) {
        std::cerr << "usage: dsmod-d3-tool <romfs> <out> [explored.json]\n";
        return 2;
    }
    g_root = argv[1];
    const std::string out = argv[2];
    EdenDsmodHostApi host{};
    host.read_romfs = &ReadRomfs;
    if (getenv("D3_DEBUG")) {
        d3_cpk::Archives ar;
        std::cout << "open " << ar.Open(&host, d3_build::k277) << "\n";
        std::vector<uint8_t> b;
        for (const char* n : {"Font\\ExocetLight.fnt", "CoreTOC.dat", "Textures\\2DUIConsoleMenu.tex"})
            std::cout << n << " " << ar.Read(&host, n, b) << " " << b.size() << "\n";
    }
    d3_assets::Assets a;
    if (!a.Init(&host, d3_build::k277)) {
        std::cerr << "assets init failed\n";
        return 1;
    }
    for (const char* t : {"2DUIConsoleMenu", "2DMapMarker_Player", "2DUI_HealthGlobe_Classic"}) {
        auto tx = a.Tex(&host, t);
        if (!tx) { std::cout << t << " decode failed\n"; continue; }
        std::cout << t << " " << tx->img.w << "x" << tx->img.h << " frames " << tx->frames.size() << "\n";
        Save(tx->img, out + "/tex-" + t + ".pam");
    }
    std::cout << "area 168314: " << a.AreaName(168314) << "\n";
    Save(d3_view::RightColumn(), out + "/right.pam");
    Save(d3_view::NavBar(a, &host, 2, 0x1F), out + "/nav.pam");
    Save(d3_view::PlayerMarker(a, &host), out + "/marker.pam");
    Save(d3_view::ObjectiveRing(a, &host), out + "/qring.pam");
    {
        // Skill rail preview (dev-only, no emulator): synthetic equipped skills/potion using real
        // icon frame hashes borrowed from the player's own buff-icon atlas, so an offline review
        // can check the ring/band/rim proportions against a native capture without a live session.
        // Icon identity is not meaningful here, only the geometry RenderRail actually draws.
        d3hg::Text text;
        std::array<d3_skills::Skill, 6> skills{};
        d3_skills::Potion potion;
        if (auto buff = a.Tex(&host, "2DUIBuffIcons"); buff && !buff->frames.empty()) {
            const size_t n = buff->frames.size();
            for (int i = 0; i < 6; ++i) {
                skills[i].available = true;
                skills[i].power = 1; // nonzero, non-sentinel: SkillUsable() only gates on this
                skills[i].icon = buff->frames[(i * 13 + 1) % n].hash;
            }
            potion.available = true;
            potion.icon = buff->frames[(6 * 13 + 1) % n].hash;
        }
        // The game's default button profile (d3_skills::ReadInput's key == -1 branch): skill slots
        // 0-5 bound to A, ZR, Y, X, B, ZL; the potion (slot 6) to R -- same order the native HUD and
        // RailSlots show. Slot 3 ("X" visually) selected, to preview the selected-glow draw order.
        const std::array<std::int32_t, 7> effective{0, 7, 2, 3, 1, 6, 5};
        Save(d3_skills::RenderRail(a, &host, text, skills, true, 3, potion, effective), out + "/rail.pam");
    }
    Save(d3_view::Title(a, a.AreaName(168314), "Torment V"), out + "/title.pam");
    Save(d3_view::Title(a, "Realm of Unbending Fate: Pandemonium Fortress", "Torment XIII"),
         out + "/title-long.pam");
    d3_view::Vitals v;
    v.live = true, v.hp = 612000, v.hp_max = 745000, v.res_type[0] = 5, v.res_cur[0] = 120,
    v.res_type[1] = 6, v.res_cur[1] = 30;
    v.hp_fill = 128, v.res_fill = 77;
    Save(d3_view::VitalsColumn(a, &host, v), out + "/vitals-dh.pam");
    v.hp = 300000, v.hp_fill = 66;
    Save(d3_view::VitalsColumn(a, &host, v), out + "/vitals-half.pam");
    v.hp = 60000, v.hp_fill = 13, v.res_fill = 5;
    Save(d3_view::VitalsColumn(a, &host, v), out + "/vitals-low.pam");
    v.live = false;
    Save(d3_view::VitalsColumn(a, &host, v), out + "/vitals-stale.pam");
    {
        // review states (same inputs for before/after comparisons): GR 1 entry and 37 %,
        // town bounty list of boot 3 (Act II)
        d3_view::RiftView r;
        r.tier = 1, r.secs_left = 14 * 60 + 22, r.pct = 0, r.deaths = 0, r.guardian = 0;
        r.best_solo = 20, r.difficulty = 6;
        Save(d3_view::RiftColumn(a, &host, r), out + "/review-rift-gr1-entry.pam");
        Save(d3_view::RiftMeter(a, &host, 0), out + "/review-meter-0.pam");
        r.secs_left = -1, r.pct = 37;
        Save(d3_view::RiftColumn(a, &host, r), out + "/review-rift-gr1-37.pam");
        Save(d3_view::RiftMeter(a, &host, 37), out + "/review-meter-37.pam");
        r.guardian = 1, r.guardian_pct = 64;
        Save(d3_view::RiftColumn(a, &host, r), out + "/review-rift-guardian-alive.pam");
        d3_view::BountyView b;
        b.act = 2, b.total = 25;
        for (const char* n : {"Kill Razormouth", "Kill Belial", "Clear the Western Channel",
                              "The Cursed Moors", "Kill Balzhak"})
            b.list.push_back({n, 0});
        Save(d3_view::BountyColumn(a, &host, b), out + "/review-bounty-act2.pam");
        b.list[1].state = 2, b.list[2].state = 1, b.done[2] = 1;
        b.list[3].name = "Kill the Stinging Death Swarm in the Western Channel Level 1";
        Save(d3_view::BountyColumn(a, &host, b), out + "/review-bounty-act2-states-stress.pam");
        Save(d3_view::PauseOverlay(a, &host, 6), out + "/review-pause.pam");
        {
            // live game of 2026-10-12 after completing Kill Razormouth (quest state 2), Act II
            d3_view::BountyView c;
            c.act = 2, c.total = 25, c.done[2] = 1;
            c.list = {{"Kill Razormouth", 2}, {"Kill Belial", 0}, {"Clear the Mysterious Cave", 0},
                      {"The Cursed Archive", 0}, {"Kill Mage Lord Caustus", 0}};
            Save(d3_view::BountyColumn(a, &host, c), out + "/review-bounty-razormouth-done.pam");
            d3_activities::View cv;
            cv.b = c;
            Save(d3_activities::Body(a, &host, cv), out + "/review-activities-razormouth-done.pam");
        }
        d3_activities::View av;
        av.b = b, av.b.done[1] = 5, av.b.done[3] = 2;
        Save(d3_activities::Body(a, &host, av), out + "/review-activities-bounties.pam");
        av.rift = true, av.r = r, av.r.secs_left = 8 * 60 + 47, av.r.pct = 64;
        Save(d3_activities::Body(a, &host, av), out + "/review-activities-gr.pam");
        av.r.tier = 0, av.r.guardian = 2;
        Save(d3_activities::Body(a, &host, av), out + "/review-activities-nr.pam");
    }
    if (argc > 3) {
        // minimal reader for explored-live.py JSON (scenes[].{sno,tex,rect,w,h,mask}, player)
        std::ifstream f(argv[3]);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string j = ss.str();
        std::vector<d3_reader::SceneMask> scenes;
        size_t p = 0;
        uint32_t world = 0;
        std::vector<uint32_t> worlds;
        {
            // world of the majority: take the world of the last scene entry that has a mask
        }
        while ((p = j.find("\"sno\":", p)) != std::string::npos) {
            d3_reader::SceneMask s;
            s.scene_sno = static_cast<uint32_t>(std::stoul(j.substr(p + 6)));
            auto num = [&](const char* key) {
                const size_t q = j.find(key, p);
                return j.substr(q + std::string(key).size());
            };
            const uint32_t w = static_cast<uint32_t>(std::stoul(num("\"world\":")));
            s.tex_sno = static_cast<uint32_t>(std::stoul(num("\"tex\":")));
            const std::string r = num("\"rect\": [");
            std::sscanf(r.c_str(), "%f, %f, %f, %f", &s.x0, &s.y0, &s.x1, &s.y1);
            s.w = static_cast<uint32_t>(std::stoul(num("\"w\":")));
            s.h = static_cast<uint32_t>(std::stoul(num("\"h\":")));
            const std::string m = num("\"mask\": \"");
            s.mask = Hex(m.substr(0, m.find('"')));
            s.tracked = !s.mask.empty();
            if (s.tracked || (getenv("D3_WORLD") && std::to_string(w) == getenv("D3_WORLD")))
                world = w;
            worlds.push_back(w);
            scenes.push_back(std::move(s));
            p += 6;
        }
        std::vector<d3_reader::SceneMask> keep;
        for (size_t i = 0; i < scenes.size(); ++i)
            if (worlds[i] == world)
                keep.push_back(scenes[i]);
        scenes.swap(keep);
        float px = 0, py = 0;
        const size_t q = j.find("\"player\": [");
        if (q != std::string::npos)
            std::sscanf(j.c_str() + q + 11, "%f, %f", &px, &py);
        float sx, sy;
        d3_view::WorldToScreen(px, py, sx, sy);
        for (const auto& sc : scenes) {
            a.SceneTile(&host, sc.tex_sno);
            float b[4];
            if (a.TileContent(sc.tex_sno, b))
                std::cout << "tile " << sc.tex_sno << " rect " << sc.x0 << "," << sc.y0 << "-" << sc.x1 << "," << sc.y1
                          << " content " << b[0] << "," << b[1] << "-" << b[2] << "," << b[3] << "\n";
        }
        for (int k : {2, 4})
            Save(d3_view::MapPicture(a, &host, scenes, d3_view::MapPixel(sx, k), d3_view::MapPixel(sy, k), k),
                 out + "/map-k" + std::to_string(k) + ".pam");
    }
    return 0;
} catch (const std::exception& e) {
    // malformed romfs/explored.json input throws (std::stoul etc.); fail loudly instead of crashing.
    std::cerr << "error: " << e.what() << "\n";
    return 1;
} catch (...) {
    std::cerr << "error: unknown exception\n";
    return 1;
}
