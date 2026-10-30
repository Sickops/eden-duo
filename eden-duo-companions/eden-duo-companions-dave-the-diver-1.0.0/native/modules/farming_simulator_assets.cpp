// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "farming_simulator_assets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace farming_simulator {
std::string XmlUnescape(std::string_view value) {
    std::string out{value};
    for (auto [entity, character] : {std::pair{"&quot;", "\""},
                                     {"&apos;", "'"},
                                     {"&lt;", "<"},
                                     {"&gt;", ">"},
                                     {"&amp;", "&"}}) {
        size_t p = 0;
        while ((p = out.find(entity, p)) != std::string::npos) {
            out.replace(p, std::strlen(entity), character);
            p += std::strlen(character);
        }
    }
    return out;
}
std::string XmlAttribute(std::string_view tag, std::string_view name) {
    const std::string pattern = std::string{name} + "=\"";
    auto p = tag.find(pattern);
    if (p == tag.npos)
        return {};
    p += pattern.size();
    auto e = tag.find('"', p);
    return e == tag.npos ? std::string{} : XmlUnescape(tag.substr(p, e - p));
}
bool Assets::Read(std::string_view path, std::vector<uint8_t> &bytes, size_t limit) const {
    bytes.clear();
    if (!host_.read_romfs || path.empty() || path.size() > 240 || path.find("..") != path.npos ||
        path.find(':') != path.npos || path.front() == '/')
        return false;
    if (!(path.starts_with("data/") || path.starts_with("dataS/") || path.starts_with("dataS2/") ||
          path.starts_with("shared/font/")))
        return false;
    std::string key{path};
    auto n = host_.read_romfs(host_.userdata, key.c_str(), 0, nullptr, 0);
    if (!n || n > limit)
        return false;
    bytes.resize(n);
    if (host_.read_romfs(host_.userdata, key.c_str(), 0, bytes.data(), n) != n) {
        bytes.clear();
        return false;
    }
    return true;
}
std::shared_ptr<const Image> Assets::Texture(std::string path, bool flip) {
    if (path.ends_with(".png"))
        path.replace(path.size() - 4, 4, ".dds");
    const auto key = path + (flip ? "|flip" : "|raw");
    if (auto it = images_.find(key); it != images_.end())
        return it->second;
    std::vector<uint8_t> bytes;
    auto out = std::make_shared<Image>();
    if (!Read(path, bytes) || !p5r_assets::DecodeDDS(bytes, *out) || out->w > 4096 || out->h > 4096)
        return {};
    if (flip)
        for (size_t y = 0; y < out->h / 2; ++y)
            for (size_t x = 0; x < size_t(out->w) * 4; ++x)
                std::swap(out->rgba[y * out->w * 4 + x],
                          out->rgba[(out->h - 1 - y) * out->w * 4 + x]);
    // Source RGB already matches the game's display output; never gamma-convert
    // texture pixels. Bound residency; references retained by the current render
    // remain valid after eviction.
    size_t total = out->rgba.size();
    for (const auto &[k, v] : images_)
        total += v->rgba.size();
    if (total > 96 * 1024 * 1024) {
        images_.clear();
        // Keyed by Image address: an evicted entry's address could be reused
        // by a later decode, so this cache is cleared in the same step.
        alpha_bbox_.clear();
    }
    images_[key] = out;
    return out;
}
const std::array<int, 4> &Assets::AlphaBBox(const Image &image) {
    if (auto it = alpha_bbox_.find(&image); it != alpha_bbox_.end())
        return it->second;
    int left = int(image.w), top = int(image.h), right = -1, bottom = -1;
    for (int iy = 0; iy < int(image.h); ++iy)
        for (int ix = 0; ix < int(image.w); ++ix)
            if (image.rgba[(size_t(iy) * image.w + ix) * 4 + 3] > 16) {
                left = std::min(left, ix);
                top = std::min(top, iy);
                right = std::max(right, ix);
                bottom = std::max(bottom, iy);
            }
    return alpha_bbox_.emplace(&image, std::array<int, 4>{left, top, right, bottom}).first->second;
}
bool Assets::LoadText() {
    if (text_ready_)
        return true;
    std::vector<uint8_t> bytes;
    if (!Read("dataS/l10n_en.xml", bytes, 4 * 1024 * 1024))
        return false;
    std::string text(bytes.begin(), bytes.end());
    std::map<std::string, std::string> locale;
    size_t p = 0;
    while ((p = text.find("<e ", p)) != text.npos) {
        auto e = text.find('>', p);
        if (e == text.npos)
            return false;
        auto tag = std::string_view{text}.substr(p, e - p);
        auto key = XmlAttribute(tag, "k");
        auto value = XmlAttribute(tag, "v");
        if (!key.empty())
            locale[key] = value;
        p = e + 1;
    }
    if (locale.empty() || !Read("dataS/helpLine.xml", bytes, 2 * 1024 * 1024))
        return false;
    text.assign(bytes.begin(), bytes.end());
    std::vector<HelpTopic> topics;
    std::string category;
    p = 0;
    while ((p = text.find('<', p)) != text.npos) {
        auto e = text.find('>', p);
        if (e == text.npos)
            return false;
        auto tag = std::string_view{text}.substr(p, e - p + 1);
        if (tag.starts_with("<helpLineCategory "))
            category = XmlAttribute(tag, "title");
        else if (tag.starts_with("<helpLine ")) {
            HelpTopic topic;
            topic.category = locale[category];
            topic.title = locale[XmlAttribute(tag, "title")];
            auto end = text.find("</helpLine>", e);
            if (end == text.npos)
                return false;
            size_t item = e;
            while ((item = text.find("<item ", item)) < end) {
                auto ie = text.find('>', item);
                if (ie == text.npos || ie > end)
                    return false;
                auto it = std::string_view{text}.substr(item, ie - item);
                auto value = XmlAttribute(it, "value");
                if (XmlAttribute(it, "type") == "image") {
                    double scale = 1;
                    auto spec = XmlAttribute(it, "heightScale");
                    if (!spec.empty() && (std::sscanf(spec.c_str(), "%lf", &scale) != 1 ||
                                          !std::isfinite(scale) || scale <= 0 || scale > 2))
                        return false;
                    topic.items.push_back({true, value, scale});
                } else if (auto found = locale.find(value); found != locale.end())
                    topic.items.push_back({false, found->second});
                item = ie + 1;
            }
            if (!topic.title.empty())
                topics.push_back(std::move(topic));
            e = end + 11;
        }
        p = e + 1;
    }
    if (topics.empty())
        return false;
    locale_ = std::move(locale);
    help_ = std::move(topics);
    text_ready_ = true;
    return true;
}
std::string Assets::Localize(std::string_view key) {
    if (!LoadText())
        return {};
    auto i = locale_.find(std::string{key});
    return i == locale_.end() ? std::string{} : i->second;
}
VehiclePresentation Assets::Vehicle(std::string config) {
    if (config.starts_with("$data/"))
        config.erase(0, 1);
    if (!config.starts_with("data/") || !config.ends_with(".xml") || config.size() > 240 ||
        config.find("..") != config.npos || config.find(':') != config.npos)
        return {};
    if (auto found = vehicles_.find(config); found != vehicles_.end())
        return found->second;
    std::vector<uint8_t> bytes;
    if (!Read(config, bytes, 2 * 1024 * 1024))
        return {};
    std::string xml(bytes.begin(), bytes.end());
    auto begin = xml.find("<storeData>");
    auto end = xml.find("</storeData>", begin);
    if (begin == xml.npos || end == xml.npos)
        return {};
    std::string_view store{xml.data() + begin, end - begin};
    auto text = [](std::string_view block, std::string_view name) {
        auto open = "<" + std::string{name} + ">", close = "</" + std::string{name} + ">";
        auto p = block.find(open);
        if (p == block.npos)
            return std::string{};
        p += open.size();
        auto e = block.find(close, p);
        if (e == block.npos)
            return std::string{};
        auto value = XmlUnescape(block.substr(p, e - p));
        auto first = value.find_first_not_of(" \r\n\t"), last = value.find_last_not_of(" \r\n\t");
        return first == value.npos ? std::string{} : value.substr(first, last - first + 1);
    };
    VehiclePresentation result;
    result.name = text(store, "name");
    if (result.name.find('<') != result.name.npos)
        result.name = text(result.name, "en");
    if (result.name.starts_with("$l10n_"))
        result.name = Localize(std::string_view{result.name}.substr(6));
    auto image = text(store, "image");
    if (image.starts_with("$data/"))
        image.erase(0, 1);
    if (!image.empty() && image.find('/') == image.npos)
        image = "data/store/" + image;
    if ((image.starts_with("data/") || image.starts_with("dataS2/")) &&
        image.find("..") == image.npos && image.find(':') == image.npos &&
        (image.ends_with(".png") || image.ends_with(".dds")))
        result.image = std::move(image);
    if (vehicles_.size() >= 128)
        vehicles_.clear();
    vehicles_.emplace(config, result);
    return result;
}
const std::vector<HelpTopic> &Assets::Help() {
    LoadText();
    return help_;
}
const MapData &Assets::Map(int id) {
    static const MapData empty;
    if (id < 1 || id > 2)
        return empty;
    auto &out = maps_[id - 1];
    if (map_ready_[id - 1])
        return out;
    char path[80];
    std::snprintf(path, sizeof(path), "data/maps/map%02d.xml", id);
    std::vector<uint8_t> bytes;
    if (!Read(path, bytes, 4 * 1024 * 1024))
        return out;
    std::string text(bytes.begin(), bytes.end());
    MapData pending;
    size_t p = 0;
    while ((p = text.find("<hotspot ", p)) != text.npos) {
        auto e = text.find('>', p);
        if (e == text.npos)
            break;
        auto tag = std::string_view{text}.substr(p, e - p);
        Hotspot h;
        h.name = XmlAttribute(tag, "name");
        auto x = XmlAttribute(tag, "xMapPos"), z = XmlAttribute(tag, "zMapPos"),
             uv = XmlAttribute(tag, "imageUVs");
        auto colour = XmlAttribute(tag, "baseColor");
        double r = 1, g = 1, b = 1, a = 1;
        if (std::sscanf(colour.c_str(), "%lf %lf %lf %lf", &r, &g, &b, &a) >= 3) {
            auto srgb = [](double v) {
                v = std::clamp(v, 0., 1.);
                return uint32_t(std::lround(
                    255 * (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055)));
            };
            h.colour = srgb(r) << 24 | srgb(g) << 16 | srgb(b) << 8 | 255;
        }
        if (std::sscanf(x.c_str(), "%lf", &h.x) == 1 && std::sscanf(z.c_str(), "%lf", &h.z) == 1 &&
            std::sscanf(uv.c_str(), "%d %d %d %d", &h.sx, &h.sy, &h.w, &h.h) == 4 && h.w > 0 &&
            h.h > 0 && h.sx >= 0 && h.sy >= 0 && h.sx + h.w <= 1024 && h.sy + h.h <= 1024)
            pending.hotspots.push_back(h);
        p = e + 1;
    }
    // Field numbers require the native scene ancestry/world transform check. Read
    // source positions only for these two checked maps; no extracted field table
    // is compiled in.
    std::snprintf(path, sizeof(path), "data/maps/map%02d.i3d", id);
    if (Read(path, bytes, 24 * 1024 * 1024)) {
        text.assign(bytes.begin(), bytes.end());
        p = 0;
        while ((p = text.find("name=\"field", p)) != text.npos) {
            auto e = text.find('>', p);
            if (e == text.npos)
                break;
            auto tag = std::string_view{text}.substr(p, e - p);
            auto name = XmlAttribute(tag, "name");
            int n = 0;
            if (std::sscanf(name.c_str(), "field%d", &n) == 1 && n > 0 && n <= 128) {
                auto end = text.find("name=\"field", e);
                while (end != text.npos &&
                       (end + 11 >= text.size() || text[end + 11] < '0' || text[end + 11] > '9'))
                    end = text.find("name=\"field", end + 11);
                auto indicator = text.find("name=\"fieldMapIndicator\"", e);
                if (indicator != text.npos && (end == text.npos || indicator <= end)) {
                    auto ie = text.find('>', indicator);
                    auto it = std::string_view{text}.substr(indicator, ie - indicator);
                    double x = 0, y = 0, z = 0;
                    auto pos = XmlAttribute(it, "translation");
                    if (std::sscanf(pos.c_str(), "%lf %lf %lf", &x, &y, &z) == 3 &&
                        std::abs(x) <= 1024 && std::abs(z) <= 1024)
                        pending.fields.push_back({n, x, z});
                }
            }
            p = e + 1;
        }
    }
    out = std::move(pending);
    map_ready_[id - 1] = true;
    return out;
}
void Fill(Image &out, int x, int y, int w, int h, uint32_t c) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, int(out.h)); ++yy)
        for (int xx = std::max(x, 0); xx < std::min(x + w, int(out.w)); ++xx) {
            auto *d = &out.rgba[(size_t(yy) * out.w + xx) * 4];
            d[0] = c >> 24;
            d[1] = c >> 16;
            d[2] = c >> 8;
            d[3] = c;
        }
}
void Circle(Image &out, int x, int y, int radius, uint32_t colour) {
    for (int yy = std::max(0, y - radius - 1); yy < std::min(int(out.h), y + radius + 1); ++yy)
        for (int xx = std::max(0, x - radius - 1); xx < std::min(int(out.w), x + radius + 1);
             ++xx) {
            double coverage = std::clamp(radius + .5 - std::hypot(xx - x, yy - y), 0., 1.);
            auto a = unsigned(std::lround((colour & 255) * coverage));
            auto *d = &out.rgba[(size_t(yy) * out.w + xx) * 4];
            for (int c = 0; c < 3; ++c)
                d[c] = ((((colour >> (24 - c * 8)) & 255) * a + d[c] * (255 - a)) + 127) / 255;
            // Every other caller draws onto an already-opaque canvas (a Fill()'d background), so
            // this never mattered there -- but a caller compositing straight onto a freshly
            // zero-filled (fully transparent) Image got an invisible result: the coverage/alpha
            // was computed above but never actually blended into the destination alpha channel,
            // same "src over dst" formula Blit() already uses.
            d[3] = static_cast<uint8_t>(std::min(255u, a + unsigned(d[3]) * (255 - a) / 255));
        }
}
void Blit(Image &out, const Image &src, int x, int y, int w, int h, int sx, int sy, int sw, int sh,
          uint32_t tint) {
    if (w <= 0 || h <= 0 || src.rgba.size() != size_t(src.w) * src.h * 4)
        return;
    if (!sw)
        sw = src.w;
    if (!sh)
        sh = src.h;
    for (int dy = std::max(0, -y); dy < std::min(h, int(out.h) - y); ++dy)
        for (int dx = std::max(0, -x); dx < std::min(w, int(out.w) - x); ++dx) {
            if (w == sw && h == sh && tint == 0xffffffff) {
                int u = sx + dx, v = sy + dy;
                if (u < 0 || v < 0 || u >= int(src.w) || v >= int(src.h))
                    continue;
                auto *d = &out.rgba[(size_t(y + dy) * out.w + x + dx) * 4];
                auto *s = &src.rgba[(size_t(v) * src.w + u) * 4];
                if (s[3] == 255) {
                    std::copy_n(s, 4, d);
                    continue;
                }
                unsigned a = s[3];
                for (int c = 0; c < 3; ++c)
                    d[c] = (s[c] * a + d[c] * (255 - a) + 127) / 255;
                d[3] = std::min(255u, a + d[3] * (255 - a) / 255);
                continue;
            }
            double u = sx + std::clamp((dx + .5) * sw / w - .5, 0., double(sw - 1)),
                   v = sy + std::clamp((dy + .5) * sh / h - .5, 0., double(sh - 1));
            int ix = u, iy = v;
            if (ix < 0 || iy < 0 || ix >= int(src.w) || iy >= int(src.h))
                continue;
            double fu = u - ix, fv = v - iy;
            auto sample = [&](int channel) {
                auto at = [&](int xx, int yy) {
                    return src.rgba[(size_t(std::min(yy, int(src.h) - 1)) * src.w +
                                     std::min(xx, int(src.w) - 1)) *
                                        4 +
                                    channel];
                };
                return unsigned(
                    std::lround((at(ix, iy) * (1 - fu) + at(ix + 1, iy) * fu) * (1 - fv) +
                                (at(ix, iy + 1) * (1 - fu) + at(ix + 1, iy + 1) * fu) * fv));
            };
            auto *d = &out.rgba[(size_t(y + dy) * out.w + x + dx) * 4];
            const unsigned a = sample(3) * (tint & 255) / 255;
            for (int c = 0; c < 3; ++c) {
                const auto ink = sample(c) * ((tint >> (24 - 8 * c)) & 255) / 255;
                d[c] = (ink * a + d[c] * (255 - a) + 127) / 255;
            }
            d[3] = std::min(255u, a + d[3] * (255 - a) / 255);
        }
}
} // namespace farming_simulator
