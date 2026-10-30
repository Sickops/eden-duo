// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_assets.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace d3_assets {
namespace {
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
u32 Le32(const u8* p) {
    return u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
}
constexpr int GroupActor = 1, GroupLevelArea = 22, GroupMarkerSet = 24, GroupTextures = 44,
              GroupQuest = 31;

// StringList label hash (h = h * 33 + byte); labels are the SNO names for LevelAreaNames.
u32 LabelHash(const std::string& s) {
    u32 h = 0;
    for (char c : s)
        h = h * 33 + static_cast<u8>(c);
    return h;
}

// Bounds a per-key cache by clearing it outright once it reaches `cap`; the hot keys refill on the
// next few lookups, so a full clear is simpler than LRU bookkeeping and cheap enough at this size.
template <typename Map>
void CapCache(Map& m, std::size_t cap = 1024) {
    if (m.size() >= cap)
        m.clear();
}
} // namespace

bool Assets::Init(const EdenDsmodHostApi* host, const d3_build::Build& build) {
    if (ready)
        return true;
    if (!archives.Open(host, build))
        return false;
    std::vector<u8> b;
    // Fonts: Exocet (headers, titles) and the sans body face, baked sizes only.
    if (!archives.Read(host, "Font\\ExocetLight.fnt", b) || !exo.Parse(b))
        return false;
    if (!archives.Read(host, "Font\\SansSerif.fnt", b) || !sans.Parse(b))
        return false;
    // CoreTOC.dat: counts[70], offsets[70], unks[70], unk; entries {group, sno, name offset}.
    if (archives.Read(host, "CoreTOC.dat", b) && b.size() > 4 * 211) {
        constexpr u32 N = 70, H = 4 * (3 * N + 1);
        for (u32 g : {u32(GroupActor), u32(GroupLevelArea), u32(GroupMarkerSet), u32(GroupTextures), u32(GroupQuest)}) {
            const u32 cnt = Le32(&b[4 * g]), off = Le32(&b[4 * (N + g)]);
            const u64 base = u64{H} + off, names = base + u64{cnt} * 12;
            if (names > b.size())
                continue;
            for (u32 k = 0; k < cnt; ++k) {
                const u32 sno = Le32(&b[base + 12 * k + 4]), no = Le32(&b[base + 12 * k + 8]);
                const u64 at = names + no;
                if (at >= b.size())
                    continue;
                const char* s = reinterpret_cast<const char*>(&b[at]);
                sno_names[sno] = std::string{s, strnlen(s, b.size() - at)};
                if (g == GroupTextures)
                    texture_snos.insert(sno);
            }
        }
    }
    // LevelAreaNames (enUS): 24-byte items {u64 0, u32 off, u32 size, u32 label hash, u32 0}.
    if (archives.Read(host, "StringList\\LevelAreaNames.stl|enUS", b) && b.size() > 0x30) {
        constexpr u64 B = 0x10;
        const u32 off = Le32(&b[0x28]), size = Le32(&b[0x2C]);
        std::unordered_map<u32, std::string> by_hash;
        for (u64 i = 0; i + 1 <= size / 24; ++i) {
            const u64 e = B + off + i * 24;
            if (e + 24 > b.size())
                break;
            const u32 so = Le32(&b[e + 8]), sl = Le32(&b[e + 12]), hh = Le32(&b[e + 16]);
            if (B + so + sl > b.size() || sl == 0)
                continue;
            by_hash[hh] = std::string{reinterpret_cast<const char*>(&b[B + so]),
                                      strnlen(reinterpret_cast<const char*>(&b[B + so]), sl)};
        }
        for (const auto& [sno, name] : sno_names) {
            auto it = by_hash.find(LabelHash(name));
            if (it != by_hash.end())
                area_names[sno] = it->second;
        }
    }
    ready = true;
    return true;
}

std::shared_ptr<const d3_tex::Texture> Assets::Tex(const EdenDsmodHostApi* host,
                                                   const std::string& name) {
    {
        std::lock_guard lock{mutex};
        auto it = tex_cache.find(name);
        if (it != tex_cache.end())
            return it->second;
    }
    std::vector<u8> b;
    auto t = std::make_shared<d3_tex::Texture>();
    if (!archives.Read(host, "Textures\\" + name + ".tex", b) || !d3_tex::DecodeTex(b, *t))
        t.reset();
    std::lock_guard lock{mutex};
    tex_cache[name] = t;
    return t;
}

d3_img::Image Assets::Frame(const EdenDsmodHostApi* host, const std::string& atlas,
                            const std::string& frame) {
    auto t = Tex(host, atlas);
    if (!t)
        return {};
    if (frame.empty())
        return t->img;
    if (frame[0] == '[') { // "[n]": frame by index
        const size_t i = std::stoul(frame.substr(1));
        return i < t->frames.size() ? d3_tex::FrameImage(*t, t->frames[i]) : d3_img::Image{};
    }
    const auto* f = d3_tex::FindFrame(*t, frame);
    return f ? d3_tex::FrameImage(*t, *f) : d3_img::Image{};
}

std::shared_ptr<const d3_tex::Texture> Assets::SceneTile(const EdenDsmodHostApi* host, u32 sno) {
    {
        std::lock_guard lock{mutex};
        auto it = tile_cache.find(sno);
        if (it != tile_cache.end())
            return it->second;
    }
    std::shared_ptr<d3_tex::Texture> t;
    const std::string name = SnoName(sno);
    if (!name.empty()) {
        std::vector<u8> b;
        t = std::make_shared<d3_tex::Texture>();
        if (!archives.Read(host, "Textures\\" + name + ".tex", b) || !d3_tex::DecodeTex(b, *t))
            t.reset();
    }
    std::array<float, 4> box{0, 0, 1e9f, 1e9f};
    if (t) {
        // valid area: 0.8 texels per world unit from the top-left; rows = world X, cols = world Y.
        // Scene sizes vary (80..240+ units), so the box is in texels and converted by the caller.
        const int n = std::min(t->img.w, t->img.h);
        int r0 = n, r1 = -1, c0 = n, c1 = -1;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                if (t->img.At(c, r)[3] > 24)
                    r0 = std::min(r0, r), r1 = std::max(r1, r), c0 = std::min(c0, c), c1 = std::max(c1, c);
        box = r1 >= r0 ? std::array<float, 4>{r0 / 0.8f, c0 / 0.8f, (r1 + 1) / 0.8f, (c1 + 1) / 0.8f}
                       : std::array<float, 4>{-1, -1, -1, -1}; // nothing drawn (filler tiles)
    }
    std::lock_guard lock{mutex};
    CapCache(tile_cache);
    tile_cache[sno] = t;
    tile_content[sno] = box;
    return t;
}

bool Assets::TileContent(u32 sno, float out[4]) {
    std::lock_guard lock{mutex};
    auto it = tile_content.find(sno);
    if (it == tile_content.end())
        return false;
    for (int i = 0; i < 4; ++i)
        out[i] = it->second[i];
    return true;
}

std::string Assets::AreaName(u32 sno) const {
    auto it = area_names.find(sno);
    return it == area_names.end() ? std::string{} : it->second;
}
std::map<u32, u32> Assets::ActorMapTags(const EdenDsmodHostApi* host, u32 sno) {
    {
        std::lock_guard lock{mutex};
        if (auto it = actor_map_tags.find(sno); it != actor_map_tags.end())
            return it->second;
    }
    std::map<u32, u32> tags;
    const std::string name = SnoName(sno);
    std::vector<u8> b;
    // 2.7.7 Actor: tag descriptor at file+0x68; offsets are relative to file+0x10.
    if (!name.empty() && archives.Read(host, "Actor\\" + name + ".acr", b) && b.size() >= 0x70) {
        const u64 off = u64{Le32(&b[0x68])} + 0x10, bytes = Le32(&b[0x6C]);
        if (bytes >= 4 && off <= b.size() && bytes <= b.size() - off) {
            const u32 count = Le32(&b[off]);
            if (count <= 4096 && u64{count} * 12 <= bytes - 4)
                for (u32 i = 0; i < count; ++i) {
                    const u64 at = off + 4 + u64{i} * 12;
                    const u32 type = Le32(&b[at]), tag = Le32(&b[at + 4]);
                    const u32 expected = tag == 0x70000 || tag == 0x70006 ? 2 :
                                         tag == 0x70004 || tag == 0x70005 || tag == 0x70008 ? 1 : 0;
                    if (tag >= 0x70000 && tag <= 0x70008 && type == expected)
                        tags[tag] = Le32(&b[at + 8]);
                }
        }
    }
    std::lock_guard lock{mutex};
    CapCache(actor_map_tags);
    actor_map_tags[sno] = tags;
    return tags;
}
bool Assets::CachedMapTags(u32 actor_sno, u32 marker_set, int index, std::map<u32, u32>& actor,
                           std::map<u32, u32>& placement) {
    std::lock_guard lock{mutex};
    const auto a = actor_map_tags.find(actor_sno);
    if (a == actor_map_tags.end())
        return false;
    actor = a->second;
    placement.clear();
    if (marker_set == 0xFFFFFFFFu || index < 0)
        return true;
    const auto p = marker_map_tags.find((u64{marker_set} << 32) | u32(index));
    if (p == marker_map_tags.end())
        return false;
    placement = p->second;
    return true;
}
std::map<u32, u32> Assets::MarkerMapTags(const EdenDsmodHostApi* host, u32 sno, int index) {
    if (sno == 0xFFFFFFFFu || index < 0)
        return {};
    const u64 key = (u64{sno} << 32) | u32(index);
    {
        std::lock_guard lock{mutex};
        if (auto it = marker_map_tags.find(key); it != marker_map_tags.end())
            return it->second;
    }
    std::map<u32, u32> result;
    const std::string name = SnoName(sno);
    std::vector<u8> b;
    // Native MarkerSet entries have stride 0x58; both descriptors use file+0x10 offsets.
    // Presence of 0x86000 matters: an explicit invalid SNO suppresses the default icon.
    if (!name.empty() && archives.Read(host, "MarkerSet\\" + name + ".mrk", b) && b.size() >= 0x24) {
        const u64 off = u64{Le32(&b[0x1C])} + 0x10, bytes = Le32(&b[0x20]);
        if (bytes % 0x58 == 0 && off <= b.size() && bytes <= b.size() - off &&
            u64(index) < bytes / 0x58) {
            const u64 entry = off + u64(index) * 0x58;
            const u64 tags = u64{Le32(&b[entry + 0x30])} + 0x10;
            const u64 tag_bytes = Le32(&b[entry + 0x34]);
            if (tag_bytes >= 4 && tags <= b.size() && tag_bytes <= b.size() - tags) {
                const u32 count = Le32(&b[tags]);
                if (count <= 4096 && u64{count} * 12 <= tag_bytes - 4)
                    for (u32 i = 0; i < count; ++i) {
                        const u64 at = tags + 4 + u64{i} * 12;
                        const u32 type = Le32(&b[at]), tag = Le32(&b[at + 4]);
                        const u32 expected = tag == 0x86000 ? 2 :
                            tag == 0x86004 || tag == 0x86005 || tag == 0x86008 ? 1 : 0;
                        if (tag >= 0x86000 && tag <= 0x86008 && type == expected)
                            result[tag] = Le32(&b[at + 8]);
                    }
            }
        }
    }
    std::lock_guard lock{mutex};
    CapCache(marker_map_tags);
    marker_map_tags[key] = result;
    return result;
}
std::string Assets::QuestName(const EdenDsmodHostApi* host, u32 sno) {
    {
        std::lock_guard lock{mutex};
        auto it = quest_names.find(sno);
        if (it != quest_names.end())
            return it->second;
    }
    std::string out;
    const std::string name = SnoName(sno);
    std::vector<u8> b;
    if (!name.empty() && archives.Read(host, "StringList\\Quest_" + name + ".stl|enUS", b) &&
        b.size() > 0x30) {
        constexpr u64 B = 0x10;
        const u32 off = Le32(&b[0x28]), size = Le32(&b[0x2C]), want = LabelHash("Name");
        for (u64 i = 0; i < size / 24; ++i) {
            const u64 e = B + off + i * 24;
            if (e + 24 > b.size())
                break;
            const u32 so = Le32(&b[e + 8]), sl = Le32(&b[e + 12]), hh = Le32(&b[e + 16]);
            if (hh != want || sl == 0 || B + so + sl > b.size())
                continue;
            out.assign(reinterpret_cast<const char*>(&b[B + so]),
                       strnlen(reinterpret_cast<const char*>(&b[B + so]), sl));
            break;
        }
    }
    std::lock_guard lock{mutex};
    quest_names[sno] = out;
    return out;
}

std::string Assets::SnoName(u32 sno) const {
    auto it = sno_names.find(sno);
    return it == sno_names.end() ? std::string{} : it->second;
}

} // namespace d3_assets
