// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "d3_hg_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "d3_tex.h"

namespace d3hg {
namespace {
u32 Le32(const u8* p) {
    return u32(p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24);
}
constexpr u32 GroupPower = 29, GroupTextures = 44, GroupStringList = 42;

struct AtlasLru {
    std::string name;
    std::shared_ptr<d3_tex::Texture> tex;
};
} // namespace

std::string Text::Get(d3_assets::Assets& a, const EdenDsmodHostApi* h, const std::string& list, u32 label) {
    {
        std::lock_guard lock{mutex};
        auto it = lists.find(list);
        if (it != lists.end()) {
            auto jt = it->second.find(label);
            return jt == it->second.end() ? std::string{} : jt->second;
        }
    }
    std::unordered_map<u32, std::string> m;
    std::vector<u8> b;
    if (a.ReadMember(h, "StringList\\" + list + ".stl|enUS", b) && b.size() > 0x30) {
        constexpr u64 B = 0x10;
        const u32 off = Le32(&b[0x28]), size = Le32(&b[0x2C]);
        for (u64 i = 0; i < size / 24; ++i) {
            const u64 e = B + off + i * 24;
            if (e + 24 > b.size())
                break;
            const u32 so = Le32(&b[e + 8]), sl = Le32(&b[e + 12]), hh = Le32(&b[e + 16]);
            if (sl == 0 || B + so + sl > b.size())
                continue;
            m[hh].assign(reinterpret_cast<const char*>(&b[B + so]),
                         strnlen(reinterpret_cast<const char*>(&b[B + so]), sl));
        }
    }
    std::lock_guard lock{mutex};
    auto& slot = lists[list];
    slot = std::move(m);
    auto jt = slot.find(label);
    return jt == slot.end() ? std::string{} : jt->second;
}

void Text::LoadToc(d3_assets::Assets& a, const EdenDsmodHostApi* h) {
    if (toc)
        return;
    std::vector<u8> b;
    if (!a.ReadMember(h, "CoreTOC.dat", b) || b.size() < 4 * 211)
        return;
    constexpr u32 N = 70, H = 4 * (3 * N + 1);
    for (u32 g : {GroupPower, GroupTextures, GroupStringList}) {
        const u32 cnt = Le32(&b[4 * g]), off = Le32(&b[4 * (N + g)]);
        const u64 base = u64{H} + off, names = base + u64{cnt} * 12;
        if (names > b.size())
            continue;
        for (u32 k = 0; k < cnt; ++k) {
            const u32 sno = Le32(&b[base + 12 * k + 4]), no = Le32(&b[base + 12 * k + 8]);
            if (names + no >= b.size())
                continue;
            const char* s = reinterpret_cast<const char*>(&b[names + no]);
            std::string n{s, strnlen(s, b.size() - names - no)};
            if (g == GroupPower)
                powers[sno] = std::move(n);
            else if (g == GroupStringList)
                stringlists[sno] = std::move(n);
            else if (n.rfind("2DInventory", 0) == 0)
                item_atlases.push_back(std::move(n));
            else if (n.rfind("2DUIBuffIcons", 0) == 0 || n.rfind("2DUI_Skills", 0) == 0 ||
                     n.rfind("2DUI_Traits", 0) == 0 || n.rfind("2DUIMiniMap_BuffIcons", 0) == 0)
                buff_atlases.push_back(std::move(n));
        }
    }
    // the buff icon atlases first, then skills/traits; item atlases after them for elixirs
    std::stable_sort(buff_atlases.begin(), buff_atlases.end(), [](const std::string& x, const std::string& y) {
        return (x.rfind("2DUIBuffIcons", 0) == 0) > (y.rfind("2DUIBuffIcons", 0) == 0);
    });
    toc = true;
}

std::string Text::ListEntry(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 sno, u32 index,
                            std::string* list_name) {
    std::string name;
    {
        std::lock_guard lock{mutex};
        LoadToc(a, h);
        auto it = stringlists.find(sno);
        if (it == stringlists.end())
            return {};
        name = it->second;
        if (list_name)
            *list_name = name;
        auto ot = ordered.find(sno);
        if (ot != ordered.end())
            return index < ot->second.size() ? ot->second[index] : std::string{};
    }
    std::vector<std::string> v;
    std::vector<u8> b;
    if (a.ReadMember(h, "StringList\\" + name + ".stl|enUS", b) && b.size() > 0x30) {
        constexpr u64 B = 0x10;
        const u32 off = Le32(&b[0x28]), size = Le32(&b[0x2C]);
        for (u64 i = 0; i < size / 24; ++i) {
            const u64 e = B + off + i * 24;
            if (e + 24 > b.size())
                break;
            const u32 so = Le32(&b[e + 8]), sl = Le32(&b[e + 12]);
            v.emplace_back(sl && B + so + sl <= b.size()
                               ? std::string(reinterpret_cast<const char*>(&b[B + so]),
                                             strnlen(reinterpret_cast<const char*>(&b[B + so]), sl))
                               : std::string{});
        }
    }
    std::lock_guard lock{mutex};
    auto& slot = ordered[sno];
    slot = std::move(v);
    return index < slot.size() ? slot[index] : std::string{};
}

std::string Text::PowerName(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 sno) {
    std::lock_guard lock{mutex};
    LoadToc(a, h);
    auto it = powers.find(sno);
    return it == powers.end() ? std::string{} : it->second;
}

bool Text::ScanAtlas(d3_assets::Assets& a, const EdenDsmodHostApi* h, const std::string& name) {
    std::vector<u8> d;
    if (!a.ReadMember(h, "Textures\\" + name + ".tex", d) || d.size() < 0x240)
        return false;
    constexpr u64 B = 0x10;
    const u32 nfr = Le32(&d[0x220]), foff = Le32(&d[0x224]);
    if (nfr >= 100000 || B + foff + u64{nfr} * 20 > d.size())
        return false;
    for (u32 i = 0; i < nfr; ++i) {
        const u32 hash = Le32(&d[B + foff + i * 20]);
        if (hash)
            frame_atlas.emplace(hash, name);
    }
    return true;
}

std::string Text::AtlasFor(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 hash, bool buff) {
    std::lock_guard lock{mutex};
    LoadToc(a, h);
    for (;;) {
        auto it = frame_atlas.find(hash);
        if (it != frame_atlas.end())
            return it->second;
        // scan the next unscanned atlas of the preferred family, then the other family
        if (buff && buff_next < buff_atlases.size()) {
            ScanAtlas(a, h, buff_atlases[buff_next++]);
        } else if (item_next < item_atlases.size()) {
            ScanAtlas(a, h, item_atlases[item_next++]);
        } else if (buff_next < buff_atlases.size()) {
            ScanAtlas(a, h, buff_atlases[buff_next++]);
        } else {
            return {};
        }
    }
}

d3_img::Image Text::Icon(d3_assets::Assets& a, const EdenDsmodHostApi* h, u32 hash, bool buff) {
    if (!hash)
        return {};
    const std::string atlas = AtlasFor(a, h, hash, buff);
    if (atlas.empty())
        return {};
    // a few decoded atlases stay cached (icons of one hero share a handful of atlases)
    static std::mutex lru_mutex;
    static std::vector<AtlasLru> lru;
    std::shared_ptr<d3_tex::Texture> tex;
    {
        std::lock_guard lock{lru_mutex};
        for (auto& e : lru)
            if (e.name == atlas)
                tex = e.tex;
    }
    if (!tex) {
        std::vector<u8> d;
        tex = std::make_shared<d3_tex::Texture>();
        if (!a.ReadMember(h, "Textures\\" + atlas + ".tex", d) || !d3_tex::DecodeTex(d, *tex))
            return {};
        std::lock_guard lock{lru_mutex};
        lru.insert(lru.begin(), AtlasLru{atlas, tex});
        if (lru.size() > 6)
            lru.pop_back();
    }
    const auto* f = d3_tex::FindFrame(*tex, hash);
    return f ? d3_tex::FrameImage(*tex, *f) : d3_img::Image{};
}

std::string StripTags(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e != std::string_view::npos) {
                const std::string_view tag = s.substr(i + 1, e - i - 1);
                if (tag.rfind("c", 0) == 0 || tag.rfind("/c", 0) == 0 || tag.rfind("icon:", 0) == 0) {
                    i = e + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    // collapse whitespace runs and trim
    std::string t;
    for (char c : out) {
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c == ' ' && (t.empty() || t.back() == ' '))
            continue;
        t += c;
    }
    while (!t.empty() && t.back() == ' ')
        t.pop_back();
    return t;
}

std::string Grouped(double v, int dec) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", dec, std::fabs(v));
    std::string s{buf};
    const size_t dot = s.find('.');
    std::string ip = s.substr(0, dot), fp = dot == std::string::npos ? "" : s.substr(dot);
    std::string g;
    for (size_t i = 0; i < ip.size(); ++i) {
        if (i && (ip.size() - i) % 3 == 0)
            g += ',';
        g += ip[i];
    }
    return (v < 0 && std::lround(std::fabs(v) * std::pow(10, dec)) != 0 ? "-" : "") + g + fp;
}

namespace {
// value of {VALUE}, {VALUE1}, {VALUE2}, {VALUE3} (case-insensitive)
bool ValueToken(std::string_view tok, const double* v, int nv, double& out) {
    std::string t;
    for (char c : tok)
        t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    int i = -1;
    if (t == "VALUE" || t == "VALUE1")
        i = 0;
    else if (t == "VALUE2")
        i = 1;
    else if (t == "VALUE3")
        i = 2;
    if (i < 0 || i >= nv)
        return false;
    out = v[i];
    return true;
}
std::string Num(double x, int dec) {
    if (std::fabs(x) >= 10000)
        return Grouped(x, dec); // the game groups five-digit values ("+15,333 Life from ...")
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", dec, x);
    std::string s{buf};
    if (s == "-0" || (s.rfind("-0.", 0) == 0 && std::strtod(buf, nullptr) == 0))
        s.erase(0, 1);
    return s;
}
// evaluates "a * b + c" left to right; tokens are numbers or {VALUEn}
bool Eval(std::string_view e, const double* v, int nv, double& out) {
    double acc = 0;
    char op = '+';
    bool any = false;
    size_t i = 0;
    while (i < e.size()) {
        const char c = e[i];
        if (c == ' ' || c == '}') {
            ++i;
            continue;
        }
        double x;
        if (c == '{') {
            const size_t end = e.find('}', i);
            if (end == std::string_view::npos || !ValueToken(e.substr(i + 1, end - i - 1), v, nv, x))
                return false;
            i = end + 1;
        } else if ((c >= '0' && c <= '9') || c == '.') {
            size_t j = i;
            while (j < e.size() && ((e[j] >= '0' && e[j] <= '9') || e[j] == '.'))
                ++j;
            x = std::strtod(std::string{e.substr(i, j - i)}.c_str(), nullptr);
            i = j;
        } else if (c == '*' || c == '/' || c == '+' || c == '-') {
            op = c;
            ++i;
            continue;
        } else {
            return false;
        }
        switch (op) {
        case '*': acc *= x; break;
        case '/': acc = x != 0 ? acc / x : 0; break;
        case '-': acc -= x; break;
        default: acc += x; break;
        }
        any = true;
    }
    out = acc;
    return any;
}
} // namespace

std::string Expand(std::string_view s, const double* v, int nv, const std::string& text1) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '[') {
            const size_t e = s.find(']', i);
            if (e == std::string_view::npos) {
                out += s[i++];
                continue;
            }
            // parts: expr | format | rest-of-expr
            std::vector<std::string_view> parts;
            std::string_view body = s.substr(i + 1, e - i - 1);
            for (size_t p = 0;;) {
                const size_t q = body.find('|', p);
                parts.push_back(body.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p));
                if (q == std::string_view::npos)
                    break;
                p = q + 1;
            }
            std::string expr{parts[0]};
            int dec = 0;
            bool plus = false;
            for (size_t k = 1; k < parts.size(); ++k) {
                std::string_view f = parts[k];
                if (f == "+")
                    plus = true;
                else if (!f.empty() && f.find_first_not_of("0123456789") == std::string_view::npos)
                    dec = std::atoi(std::string{f}.c_str());
                else if (k >= 2)
                    expr += std::string{f};
            }
            double x = 0;
            if (Eval(expr, v, nv, x)) {
                const std::string n = Num(x, dec);
                out += (plus && x >= 0 ? "+" : "") + n;
            }
            i = e + 1;
            continue;
        }
        if (s[i] == '{') {
            const size_t e = s.find('}', i);
            if (e != std::string_view::npos) {
                const std::string_view tok = s.substr(i + 1, e - i - 1);
                double x;
                if (!text1.empty() && (tok == "VALUE1" || tok == "value1")) {
                    out += text1;
                    i = e + 1;
                    continue;
                }
                if (ValueToken(tok, v, nv, x)) {
                    out += Num(x, 0);
                    i = e + 1;
                    continue;
                }
            }
        }
        out += s[i++];
    }
    return StripTags(out);
}

} // namespace d3hg
