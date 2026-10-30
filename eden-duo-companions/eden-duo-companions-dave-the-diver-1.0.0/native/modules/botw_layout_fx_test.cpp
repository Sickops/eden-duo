// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the on-device native layout animations (botw_layout_fx.h): containers, BFLYT/BFLAN
// readers, curves, block decoders, compositor blending and the static-art fallback. Synthetic data only.
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include "botw_layout_fx.h"
using namespace botw::fx;
namespace {
struct W {
  std::vector<u8> b;
  void u8_(u8 v) { b.push_back(v); }
  void u16_(u16 v) {
    b.push_back(u8(v));
    b.push_back(u8(v >> 8));
  }
  void u32_(u32 v) {
    for (int i = 0; i < 4; ++i)
      b.push_back(u8(v >> (8 * i)));
  }
  void f32_(float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    u32_(v);
  }
  void str(const std::string &s, size_t n) {
    for (size_t i = 0; i < n; ++i)
      b.push_back(i < s.size() ? u8(s[i]) : 0);
  }
  void put32(size_t o, u32 v) {
    for (int i = 0; i < 4; ++i)
      b[o + i] = u8(v >> (8 * i));
  }
};
// One section: tag + size + body.
void Section(W &w, const char *tag, const std::vector<u8> &body) {
  w.str(tag, 4);
  w.u32_(u32(8 + body.size()));
  w.b.insert(w.b.end(), body.begin(), body.end());
}
std::vector<u8> Pane(const char *tag, const std::string &name, float x, float y, float sx, float wdt, float hgt,
                     u8 alpha, int mat) {
  W p;
  p.u8_(1 | 2);
  p.u8_(0);
  p.u8_(alpha);
  p.u8_(0); // flags (visible, influence alpha), origin, alpha
  p.str(name, 0x18);
  p.str("", 8); // user data: the pane body is 0x24 bytes up to its translation
  p.f32_(x);
  p.f32_(y);
  p.f32_(0);
  p.f32_(0);
  p.f32_(0);
  p.f32_(0);
  p.f32_(sx);
  p.f32_(sx);
  p.f32_(wdt);
  p.f32_(hgt);
  if (std::string(tag) == "pic1") {
    for (int i = 0; i < 16; ++i)
      p.u8_(255);
    p.u16_(u16(mat));
    p.u8_(0);
    p.u8_(0);
  }
  return p.b;
}
// Layout: RootPane > P_Test (64x64 at origin) with material M (1 texmap "T", optional additive blend).
std::vector<u8> Bflyt(bool additive, std::array<u8, 4> black, std::array<u8, 4> white, u32 extra_flags = 0,
                      u8 alpha = 255) {
  W w;
  w.str("FLYT", 4);
  w.u16_(0xFEFF);
  w.u16_(0x14);
  w.u32_(0x08000000);
  w.u32_(0);
  w.u16_(0);
  w.u16_(0);
  {
    W t;
    t.u16_(1);
    t.u16_(0);
    t.u32_(4);
    t.str("T", 2);
    t.str("", 2);
    Section(w, "txl1", t.b);
  }
  {
    W m;
    m.u16_(1);
    m.u16_(0);
    m.u32_(0x10);
    const u32 flags = 1 | (1 << 2) | (additive ? (1 << 10) : 0) | extra_flags;
    m.str("M", 0x1C);
    m.u32_(flags);
    m.u32_(0);
    for (u8 c : black)
      m.u8_(c);
    for (u8 c : white)
      m.u8_(c);
    m.u16_(0);
    m.u8_(0);
    m.u8_(0); // texmap 0, clamp
    m.f32_(0);
    m.f32_(0);
    m.f32_(0);
    m.f32_(1);
    m.f32_(1); // srt
    if (additive) {
      m.u8_(1);
      m.u8_(4);
      m.u8_(1);
      m.u8_(0);
    }
    Section(w, "mat1", m.b);
  }
  Section(w, "pan1", Pane("pan1", "RootPane", 0, 0, 1, 1280, 720, 255, -1));
  Section(w, "pas1", {});
  Section(w, "pic1", Pane("pic1", "P_Test", 0, 0, 1, 64, 64, alpha, 0));
  Section(w, "pae1", {});
  return w.b;
}
// Anim: entry P_Test, FLVC target 16 (pane alpha) hermite 0->255 over 10 frames; FLPA target 0 step.
std::vector<u8> Bflan() {
  W w;
  w.str("FLAN", 4);
  w.u16_(0xFEFF);
  w.u16_(0x14);
  w.u32_(0x08000000);
  w.u32_(0);
  w.u16_(0);
  w.u16_(0);
  W s; // pai1 body (offsets are from the section start = body start - 8)
  s.u16_(10);
  s.u8_(1);
  s.u8_(0);
  s.u16_(0);
  s.u16_(1);
  s.u32_(0x14); // frames, loop, ntex, nent, eoff
  s.u32_(0x18); // entry 0 at +0x18
  const size_t entry = s.b.size() + 8;
  s.str("P_Test", 0x1C);
  s.u8_(2);
  s.u8_(0);
  s.u16_(0);
  const size_t tagtab = s.b.size();
  s.u32_(0);
  s.u32_(0);
  // tag 1: FLVC with one target
  const size_t t1 = s.b.size() + 8;
  s.str("FLVC", 4);
  s.u8_(1);
  s.u8_(0);
  s.u16_(0);
  s.u32_(0xC);
  s.u8_(0);
  s.u8_(16);
  s.u8_(2);
  s.u8_(0);
  s.u16_(2);
  s.u16_(0);
  s.u32_(0xC);
  s.f32_(0);
  s.f32_(0);
  s.f32_(0);
  s.f32_(10);
  s.f32_(255);
  s.f32_(0);
  // tag 2: FLPA x step (curve 1, u16 values)
  const size_t t2 = s.b.size() + 8;
  s.str("FLPA", 4);
  s.u8_(1);
  s.u8_(0);
  s.u16_(0);
  s.u32_(0xC);
  s.u8_(0);
  s.u8_(0);
  s.u8_(1);
  s.u8_(0);
  s.u16_(2);
  s.u16_(0);
  s.u32_(0xC);
  s.f32_(0);
  s.u16_(0);
  s.u16_(0);
  s.f32_(5);
  s.u16_(7);
  s.u16_(0);
  s.put32(tagtab - 0, u32(t1 - entry));
  s.put32(tagtab + 4, u32(t2 - entry));
  Section(w, "pai1", s.b);
  return w.b;
}
Tex Solid(float r, float g, float b, float a) {
  Tex t;
  t.w = t.h = 4;
  t.px.assign(64, 0);
  for (int i = 0; i < 16; ++i) {
    t.px[i * 4] = r;
    t.px[i * 4 + 1] = g;
    t.px[i * 4 + 2] = b;
    t.px[i * 4 + 3] = a;
  }
  return t;
}
const float *At(const Frame &f, int x, int y) { return &f.c[(size_t(y) * f.w + x) * 4]; }
bool Near(float a, float b, float e = 0.02f) { return std::fabs(a - b) <= e; }
} // namespace
int main() {
  unsigned cases = 0;
  // Yaz0: one group of 8 literals, then a back-reference; non-Yaz0 input passes through.
  {
    std::vector<u8> in{'Y', 'a', 'z',  '0', 0,   0,   0,   12,  0,   0,   0,   0,    0,    0,
                       0,   0,   0xFF, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 0x00, 0x20, 0x06};
    std::vector<u8> out;
    assert(Yaz0(in, out) && std::string(out.begin(), out.end()) == "abcdefghbcde");
    std::vector<u8> raw{'S', 'A', 'R', 'C'}, pass;
    assert(Yaz0(raw, pass) && pass == raw);
    std::vector<u8> bad(in.begin(), in.begin() + 20);
    assert(!Yaz0(bad, out));
    ++cases;
  }
  // SARC: little-endian, two members, only the wanted one copied out; bad magic rejected.
  {
    W w;
    w.str("SARC", 4);
    w.u16_(0x14);
    w.u8_(0xFF);
    w.u8_(0xFE);
    w.u32_(0);
    w.u32_(0);
    w.u32_(0x01000000);
    w.str("SFAT", 4);
    w.u16_(0xC);
    w.u16_(2);
    w.u32_(0x65);
    w.u32_(0);
    w.u32_(0x01000000);
    w.u32_(0);
    w.u32_(3);
    w.u32_(0);
    w.u32_(0x01000002);
    w.u32_(3);
    w.u32_(5);
    w.str("SFNT", 4);
    w.u16_(8);
    w.u16_(0);
    w.str("a/x", 8);
    w.str("b/y", 8);
    const u32 data = u32(w.b.size());
    w.put32(0xC, data);
    w.str("abcde", 5);
    w.put32(8, u32(w.b.size()));
    std::map<std::string, std::vector<u8>, std::less<>> m;
    assert(Sarc(
               w.b, [](const std::string &n) { return n == "b/y"; }, m) &&
           m.size() == 1 && m["b/y"] == (std::vector<u8>{'d', 'e'}));
    std::vector<u8> bad = w.b;
    bad[0] = 'X';
    m.clear();
    assert(!Sarc(bad, [](const std::string &) { return true; }, m));
    ++cases;
  }
  // BC4/BC5 endpoint blocks.
  {
    const u8 blk[8]{255, 0, 0, 0, 0, 0, 0, 0};
    u8 v[16];
    detail::Bc4Block(blk, v);
    for (u8 x : v)
      assert(x == 255);
    const u8 blk2[8]{255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24};
    detail::Bc4Block(blk2, v);
    for (u8 x : v)
      assert(x == 0); // index 1 = a1
    const u8 blk3[8]{0, 255, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    detail::Bc4Block(blk3, v);
    for (u8 x : v)
      assert(x == 255); // a0<=a1: index 7 = 255
    ++cases;
  }
  // BFLYT v8: texture list, material flags/colours/texmap/srt/blend, pane tree and transforms.
  {
    Layout L;
    assert(ParseBflyt(Bflyt(true, {0, 0, 0, 0}, {255, 128, 0, 255}), L));
    assert(L.textures.size() == 1 && L.textures[0] == "T" && L.mats.size() == 1 && L.mats[0].tex[0] == "T");
    assert(L.mats[0].blend && (*L.mats[0].blend)[0] == 1 && (*L.mats[0].blend)[2] == 1 &&
           Near(L.mats[0].white[1], 128 / 255.f));
    assert(L.panes.size() == 2 && L.panes[0].name == "RootPane" && L.panes[0].children == std::vector<int>{1} &&
           L.panes[1].mat == 0 && L.panes[1].w == 64);
    Layout bad;
    assert(!ParseBflyt({'F', 'L', 'Y', 'X'}, bad));
    ++cases;
  }
  // BFLAN: tracks, hermite (flat slopes -> smoothstep midpoint) and step curves, clamping.
  {
    Anim A;
    assert(ParseBflan(Bflan(), A) && A.frames == 10 && A.tracks.size() == 2);
    const Track &vc = A.tracks[0];
    assert(vc.magic == "FLVC" && vc.target == 16 && vc.curve == 2);
    assert(Near(Eval(vc, 0), 0) && Near(Eval(vc, 5), 127.5f, 0.01f) && Near(Eval(vc, 10), 255) &&
           Near(Eval(vc, 99), 255) && Near(Eval(vc, -3), 0));
    const Track &st = A.tracks[1];
    assert(st.magic == "FLPA" && st.curve == 1 && Near(Eval(st, 4.9f), 0) && Near(Eval(st, 5), 7));
    Lookup lk;
    lk.anims.push_back({&A, 5});
    assert(lk.Get("P_Test", "FLVC", 0, 16) && !lk.Get("P_Test", "FLVC", 0, 15) && !lk.Get("Other", "FLVC", 0, 16));
    Anim bad;
    assert(!ParseBflan({'F', 'L', 'A', 'X'}, bad));
    ++cases;
  }
  // Compositor: normal blend (black/white interpolation of a white texture = white colour),
  // additive blend (adds colour, alpha from coverage), pane alpha from the animation, threshold alpha.
  {
    std::map<std::string, Tex, std::less<>> T;
    T["T"] = Solid(1, 1, 1, 1);
    Options o;
    o.size = 16;
    o.scale = 0.125f; // 64 layout units -> 8 px, centred
    {
      Layout L;
      ParseBflyt(Bflyt(false, {0, 0, 0, 0}, {255, 0, 0, 255}), L);
      Lookup lk;
      const Frame f = Composer(L, T, lk, o).Run();
      const float *c = At(f, 8, 8);
      assert(Near(c[0], 1) && Near(c[1], 0) && Near(c[3], 1));
      assert(At(f, 0, 0)[3] == 0);
    }
    {
      Layout L;
      ParseBflyt(Bflyt(true, {0, 0, 0, 0}, {0, 255, 0, 255}), L);
      Lookup lk;
      const Frame f = Composer(L, T, lk, o).Run();
      const float *c = At(f, 8, 8);
      assert(Near(c[1], 1) && Near(c[0], 0) && Near(c[3], 1));
    }
    {
      Layout L;
      ParseBflyt(Bflyt(false, {0, 0, 0, 0}, {255, 255, 255, 255}), L);
      Anim A;
      ParseBflan(Bflan(), A);
      Lookup lk;
      lk.anims.push_back({&A, 0});
      assert(Composer(L, T, lk, o).Run().c[(8 * 16 + 8) * 4 + 3] == 0); // alpha 0 at frame 0
      Lookup lk2;
      lk2.anims.push_back({&A, 10});
      const Frame f = Composer(L, T, lk2, o).Run();
      assert(Near(At(f, 8, 8)[3], 1));
    }
    {
      std::map<std::string, Tex, std::less<>> T2;
      T2["T"] = Solid(1, 1, 1, 0.5f);
      Layout lo;
      ParseBflyt(Bflyt(false, {0, 0, 0, 0}, {255, 255, 255, 12}, 0x40000), lo);
      Lookup lk;
      assert(Near(At(Composer(lo, T2, lk, o).Run(), 8, 8)[3], 1)); // dissolve: visible
      Layout hi;
      ParseBflyt(Bflyt(false, {0, 0, 0, 255}, {255, 255, 255, 255}, 0x40000), hi);
      assert(At(Composer(hi, T2, lk, o).Run(), 8, 8)[3] == 0);
    } // dissolved
    {
      Layout L;
      ParseBflyt(Bflyt(false, {0, 0, 0, 0}, {255, 255, 255, 255}, 0, 0), L);
      Lookup lk;
      assert(At(Composer(L, T, lk, o).Run(), 8, 8)[3] == 0);
    } // pane alpha 0
    ++cases;
  }
  // Frame keys and the actor -> effect table.
  {
    std::string e;
    int f = 0;
    assert(FrameKey("orb_zora_on", 12) == "module:fx/orb_zora_on/12" &&
           ParseFrameKey("module:fx/orb_zora_on/12", e, f) && e == "orb_zora_on" && f == 12);
    assert(!ParseFrameKey("module:fx/orb_zora_on/320", e, f) && !ParseFrameKey("module:fx/medallion/640", e, f) &&
           ParseFrameKey("module:fx/medallion/639", e, f));
    assert(!ParseFrameKey("module:fx/nope/1", e, f) && !ParseFrameKey("module:fx/orb_zora_on/", e, f) &&
           !ParseFrameKey("module:fx/orb_zora_on/1x", e, f) && !ParseFrameKey("file:fx/orb_zora_on/1", e, f));
    assert(EffectFor("Obj_HeroSoul_Rito", true) == "orb_rito_on" &&
           EffectFor("Obj_DLC_HeroSoul_Goron", false) == "orbdlc_goron_off");
    assert(EffectFor("Obj_WarpDLC", false) == "medallion" && EffectFor("Obj_KorokNuts", true).empty());
    for (const auto &x : kEffects)
      assert(FindEffect(x.id) == &x && x.frames20 > 0 && x.scale > 0);
    ++cases;
  }
  // Renderer: unreadable game files fail closed, retried, then mark static fallback; a bad key never renders.
  {
    Availability().store(0);
    Renderer r;
    int calls = 0;
    const auto fail = [&](const char *, std::vector<u8> &) {
      ++calls;
      return false;
    };
    assert(!r.Ensure(fail) && Availability().load() == 0 && !r.Ensure(fail) && !r.Ensure(fail) &&
           Availability().load() == 2 && calls == 3);
    assert(!r.Ensure(fail) && calls == 3);
    std::vector<u8> px;
    int w = 0, h = 0;
    assert(!r.Render("orb_zora_on", 0, px, w, h) && !r.Render("nope", 0, px, w, h));
    Availability().store(0);
    ++cases;
  }
  std::cout << cases << " native layout animation container/reader/curve/decoder/compositor/fallback cases passed\n";
}
