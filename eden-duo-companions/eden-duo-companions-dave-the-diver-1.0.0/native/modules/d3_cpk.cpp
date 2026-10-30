// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_cpk.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

#include "dread_mapgen_util.h"

namespace d3_cpk {
namespace {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 Magic = 0xA1B2C3D4u;
constexpr u64 MaxTableBytes = 64ull << 20;
constexpr u64 MaxMember = 32ull << 20;

// Base archives (alphabetical, as the game mounts them): disc content, identical across builds
// per INVENTORY.md's C-base-archives (every build including 2.7.6 ships them). The update's
// PATCH archives are the real per-build surface (C-patch-archives) -- that list comes from
// `build.patch_archives` instead.
constexpr std::array<std::string_view, 23> BaseArchives{
    "Act1", "Act2", "Act3", "Act4", "Act5", "ClientCommon", "Common", "CoreCommon",
    "enUS_Act1", "enUS_Act1Cutscene", "enUS_Act2", "enUS_Act2Cutscene", "enUS_Act3",
    "enUS_Act3Cutscene", "enUS_Act4", "enUS_Act4Cutscene", "enUS_Act5", "enUS_Act5Cutscene",
    "enUS_ClientCommon", "enUS_Common", "enUS_CommonCutscene", "enUS_CoreCommon", "ServerCommon"};

u64 ReadBits(const std::vector<u8>& buf, u64 bit, u32 n) {
    // MSB-first bit stream (n <= 64)
    u64 v = 0;
    for (u32 i = 0; i < n; ++i) {
        const u64 b = bit + i;
        const u64 byte = b >> 3;
        const u8 x = byte < buf.size() ? buf[byte] : 0;
        v = (v << 1) | ((x >> (7 - (b & 7))) & 1);
    }
    return v;
}

struct Sfi {
    u64 hash;
    u64 size;
    u32 loc_count;
    u32 loc_idx;
};

struct Archive {
    std::string path;
    u64 file_size = 0;
    bool le = false;
    u32 fmt = 2;
    u32 comp_sector = 0x4000;
    u64 first_sec = 0;
    std::vector<Sfi> sfi; // sorted by hash
    std::vector<u64> locs;
    std::vector<u64> cs2do;

    u16 U16(const u8* p) const {
        return le ? u16(p[0] | p[1] << 8) : u16(p[0] << 8 | p[1]);
    }
    u32 U32(const u8* p) const {
        return le ? u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24)
                  : u32(u32(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]);
    }
};

bool ReadRomfs(const EdenDsmodHostApi* host, const std::string& path, u64 off, void* out,
               u64 size) {
    if (!host || !host->read_romfs)
        return false;
    return host->read_romfs(host->userdata, path.c_str(), off, out, size) == size;
}

bool LoadIndex(const EdenDsmodHostApi* host, Archive& a) {
    a.file_size = host->read_romfs(host->userdata, a.path.c_str(), 0, nullptr, 0);
    if (a.file_size < 72)
        return false;
    u8 h[72];
    if (!ReadRomfs(host, a.path, 0, h, sizeof h))
        return false;
    const u32 mbe = u32(h[0]) << 24 | h[1] << 16 | h[2] << 8 | h[3];
    const u32 mle = u32(h[0] | h[1] << 8 | h[2] << 16 | u32(h[3]) << 24);
    if (mbe == Magic)
        a.le = false;
    else if (mle == Magic)
        a.le = true;
    else
        return false;
    const u32 ver = a.U32(h + 4);
    const u64 decomp_fs = a.le ? (u64(a.U32(h + 8)) | u64(a.U32(h + 12)) << 32)
                               : (u64(a.U32(h + 8)) << 32 | a.U32(h + 12));
    a.fmt = a.U32(h + 16);
    const u32 file_count = a.U32(h + 20), loc_count = a.U32(h + 24), header_sector = a.U32(h + 28);
    const u32 fs_bc = a.U32(h + 32), flcnt_bc = a.U32(h + 36), flidx_bc = a.U32(h + 40),
              loc_bc = a.U32(h + 44), cs2do_bc = a.U32(h + 48);
    u32 read_sector = 0x10000, header_size = 64;
    a.comp_sector = 0x4000;
    if (ver == 7) {
        read_sector = a.U32(h + 60);
        a.comp_sector = a.U32(h + 64);
        header_size = 72;
    } else if (ver != 6) {
        return false;
    }
    if (file_count > 10'000'000 || fs_bc > 64 || flcnt_bc > 32 || flidx_bc > 32 || loc_bc > 64 ||
        cs2do_bc > 64 || !a.comp_sector || !read_sector || decomp_fs > (u64{1} << 40))
        return false;
    if (a.fmt == 10) {
        a.first_sec = header_sector;
    } else {
        a.first_sec = (u64{read_sector} * header_sector) & 0xFFFF0000u;
        if (a.first_sec % read_sector)
            a.first_sec += read_sector;
    }
    if (a.first_sec >= a.file_size)
        return false;
    const u64 entry_bits = 64ull + fs_bc + flcnt_bc + flidx_bc;
    const u64 sfi_bytes = (u64{file_count} * entry_bits + 7) / 8;
    const u64 loc_bytes = (u64{loc_bc} * loc_count + 7) / 8;
    const u64 comp_count = (a.comp_sector + a.file_size - 1 - a.first_sec) / a.comp_sector;
    const u64 cs_bytes = (comp_count * cs2do_bc + 7) / 8;
    const u64 total = sfi_bytes + loc_bytes + cs_bytes;
    if (total > MaxTableBytes || header_size + total > a.file_size)
        return false;
    std::vector<u8> t(total);
    if (!ReadRomfs(host, a.path, header_size, t.data(), total))
        return false;
    std::vector<u8> sfi_blk(t.begin(), t.begin() + sfi_bytes);
    std::vector<u8> loc_blk(t.begin() + sfi_bytes, t.begin() + sfi_bytes + loc_bytes);
    std::vector<u8> cs_blk(t.begin() + sfi_bytes + loc_bytes, t.end());
    a.sfi.resize(file_count);
    for (u32 i = 0; i < file_count; ++i) {
        u64 bp = u64{i} * entry_bits;
        Sfi& s = a.sfi[i];
        s.hash = ReadBits(sfi_blk, bp, 64);
        bp += 64;
        s.size = ReadBits(sfi_blk, bp, fs_bc);
        bp += fs_bc;
        s.loc_count = u32(ReadBits(sfi_blk, bp, flcnt_bc));
        bp += flcnt_bc;
        s.loc_idx = u32(ReadBits(sfi_blk, bp, flidx_bc));
    }
    std::sort(a.sfi.begin(), a.sfi.end(), [](const Sfi& x, const Sfi& y) { return x.hash < y.hash; });
    a.locs.resize(loc_count);
    for (u32 i = 0; i < loc_count; ++i)
        a.locs[i] = ReadBits(loc_blk, u64{i} * loc_bc, loc_bc);
    a.cs2do.resize(comp_count);
    for (u64 i = 0; i < comp_count; ++i)
        a.cs2do[i] = ReadBits(cs_blk, i * cs2do_bc, cs2do_bc);
    return !a.sfi.empty();
}

void Decrypt(std::vector<u8>& c) {
    u64 state = 0x872DCDA7A97A7EE1ull;
    for (auto& b : c) {
        const u8 ct = b;
        b = static_cast<u8>(ct ^ (state & 0xFF));
        state = (u64{ct} << 56) | (state >> 8);
    }
}

bool DecompressSector(const EdenDsmodHostApi* host, const Archive& a, u64 sec,
                      std::vector<u8>& out) {
    out.clear();
    const u64 start = a.first_sec + sec * a.comp_sector;
    if (start >= a.file_size)
        return false;
    const u64 len = std::min<u64>(a.comp_sector, a.file_size - start);
    std::vector<u8> d(len);
    if (!ReadRomfs(host, a.path, start, d.data(), len))
        return false;
    u64 p = 0;
    if (sec == 0 && a.fmt != 10) {
        if (len < 6)
            return false;
        p = 6 + a.U16(&d[4]);
    }
    std::vector<u8> chunk, dec;
    while (p + 6 <= len) {
        const u32 dsz = u32(a.U16(&d[p])) | u32(a.U16(&d[p + 2])) << 16;
        const u16 zr = a.U16(&d[p + 4]);
        if (zr == 0)
            break;
        const u64 zs = zr & 0x7FFF;
        if (p + 6 + zs > len || dsz > 0x100000)
            break;
        chunk.assign(d.begin() + p + 6, d.begin() + p + 6 + zs);
        if (zr & 0x8000)
            Decrypt(chunk);
        dec.clear();
        // zlib wrapper: 2-byte header, raw deflate; full-sector chunks carry no final block, so a
        // "truncated" stream still yields the whole chunk (zlib.decompressobj semantics).
        if (chunk.size() > 2)
            dread_mapgen::Inflate(std::span<const u8>(chunk).subspan(2), dec, dsz);
        dec.resize(dsz, 0);
        out.insert(out.end(), dec.begin(), dec.end());
        p += 6 + zs;
    }
    return true;
}

bool ReadEntry(const EdenDsmodHostApi* host, const Archive& a, const Sfi& s, std::vector<u8>& out) {
    out.clear();
    if (s.size == 0 || s.loc_count == 0 || s.loc_idx >= a.locs.size() || s.size > MaxMember)
        return false;
    const u64 start = a.locs[s.loc_idx], end = start + s.size;
    const auto& cs = a.cs2do;
    if (cs.empty())
        return false;
    auto it = std::upper_bound(cs.begin(), cs.end(), start);
    u64 sec = it == cs.begin() ? 0 : u64(it - cs.begin() - 1);
    const u64 base = cs[sec];
    std::vector<u8> buf, data;
    for (; sec < cs.size() && cs[sec] < end; ++sec) {
        if (!DecompressSector(host, a, sec, data))
            return false;
        const u64 at = cs[sec] - base;
        if (buf.size() < at + data.size())
            buf.resize(at + data.size(), 0);
        std::copy(data.begin(), data.end(), buf.begin() + at);
    }
    if (start - base > buf.size())
        return false;
    out.assign(buf.begin() + (start - base),
               buf.begin() + std::min<u64>(buf.size(), start - base + s.size));
    out.resize(s.size, 0);
    return true;
}
} // namespace

u64 NameHash(std::string_view name) {
    u64 h = 0xCBF29CE484222325ull;
    for (char c : name) {
        u8 b = static_cast<u8>(c);
        if (b >= 'A' && b <= 'Z')
            b = static_cast<u8>(b + 32);
        h = (h ^ b) * 0x100000001B3ull;
    }
    return h;
}

struct Archives::Impl {
    std::mutex mutex;
    std::vector<Archive> list; // mount order
    bool open = false;
};

Archives::Archives() : impl{std::make_unique<Impl>()} {}
Archives::~Archives() = default;

bool Archives::Open(const EdenDsmodHostApi* host, const d3_build::Build& build) {
    std::lock_guard lock{impl->mutex};
    if (impl->open)
        return true;
    std::vector<Archive> list;
    auto add = [&](std::string path) {
        Archive a;
        a.path = std::move(path);
        if (LoadIndex(host, a))
            list.push_back(std::move(a));
    };
    for (auto n : BaseArchives)
        add("CPKs/" + std::string{n} + ".cpk");
    for (auto n : build.patch_archives)
        add("Update/CPKs/" + std::string{n} + ".cpk");
    // ClientCommon (UI textures) and CoreCommon (strings) are required.
    if (list.size() < 8)
        return false;
    impl->list = std::move(list);
    impl->open = true;
    return true;
}

bool Archives::Read(const EdenDsmodHostApi* host, std::string_view name, std::vector<u8>& out) {
    const u64 h = NameHash(name);
    const Archive* src = nullptr;
    Sfi entry{};
    {
        std::lock_guard lock{impl->mutex};
        for (auto it = impl->list.rbegin(); it != impl->list.rend(); ++it) {
            auto f = std::lower_bound(it->sfi.begin(), it->sfi.end(), h,
                                      [](const Sfi& s, u64 v) { return s.hash < v; });
            if (f != it->sfi.end() && f->hash == h && f->size && f->loc_count) {
                src = &*it;
                entry = *f;
                break;
            }
        }
    }
    // Archives are immutable after Open(); reading needs no lock.
    return src && ReadEntry(host, *src, entry, out);
}

} // namespace d3_cpk
