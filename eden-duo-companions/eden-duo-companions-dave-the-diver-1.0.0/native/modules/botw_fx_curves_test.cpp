// SPDX-License-Identifier: GPL-3.0-or-later
// Host tests for the native UI loops sampled as curves (botw_fx_curves.h) and their use by the Ready
// marks (botw_ready_pins.h native_ready_anim). Synthetic tracks only: the shapes below copy the native
// keys of the player's Layout/Common.sblarc loops named in botw_fx_curves.h.
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include "botw_ready_pins.h"
using namespace botw;
using namespace botw::fx;
namespace {
Track T(const char *entry, const char *magic, int index, int target, std::vector<fx::Key> keys) {
  Track t;
  t.entry = entry;
  t.magic = magic;
  t.index = u8(index);
  t.target = u8(target);
  t.curve = 2;
  t.keys = std::move(keys);
  return t;
}
Anim A(u16 frames, std::vector<Track> tracks) {
  Anim a;
  a.frames = frames;
  a.tracks = std::move(tracks);
  return a;
}
// Linear-looking hermite keys (slope 0 at the extremes, as the native loops are authored).
void Synthetic(Renderer &r, bool with_spinner = true) {
  r.anims_["PaMapIconBtn_00_Loop"] =
      A(1200, {T("P_Flash_08", "FLVC", 0, 16, {{0, 150, 0}, {30, 78, 0}, {60, 150, 0}}),
               T("GradationFire_00", "FLTS", 1, 1, {{0, 0, 1.f / 120}, {120, 1, 1.f / 120}})});
  r.anims_["PaMapIconChallenge_00_Loop"] =
      A(40, {T("P_Effect_00", "FLPA", 0, 6, {{0, 0.25f, 0}, {10, 0.7f, 0}, {30, 0.8f, 0}, {40, 0.8f, 0}}),
             T("P_Effect_00", "FLVC", 0, 16, {{0, 0, 0}, {1, 255, 0}, {20, 255, 0}, {30, 0, 0}, {40, 0, 0}}),
             T("P_Icon_00", "FLMC", 0, 6, {{2, 100, 0}, {20, 0, 0}, {40, 100, 0}})});
  if (with_spinner) {
    std::vector<Track> sp;
    for (int k = 0; k < 12; ++k) {
      char n[16];
      std::snprintf(n, sizeof n, "Circle8_%02d", k);
      const float add = k >= 6 ? 1.f : 0.f;
      if (k % 2 == 0)
        sp.push_back(T(n, "FLPA", 0, 6, {{0, 0.5f + add, 0}, {9, 1.f + add, 0}, {20, 0.5f + add, 0}}));
      else
        sp.push_back(T(n, "FLPA", 0, 6, {{-1, 1.f + add, 0}, {10, 0.5f + add, 0}, {19, 1.f + add, 0}, {30, 0.5f + add, 0}}));
    }
    r.anims_["PaLoadingIconNoWait_00_Loop"] = A(20, std::move(sp));
  }
  r.anims_["PaWeather_00_Loop"] = A(60, {T("N_Loop_00", "FLVC", 0, 16, {{0, 255, 0}, {30, 0, 0}, {60, 255, 0}})});
  r.anims_["PaTempMeterGaugeLoop_00_Loop"] =
      A(60, {T("P_Glow_00", "FLVC", 0, 16, {{0, 64, 0}, {6, 255, 0}, {30, 64, 0}, {36, 255, 0}, {60, 64, 0}})});
  r.anims_["PaAppAllRadarGauge_00_Loop"] =
      A(120, {T("N_Loop_01", "FLVC", 0, 16, {{0, 255, 0}, {30, 150, 0}, {60, 255, 0}, {90, 150, 0}, {120, 255, 0}})});
  r.anims_["PaSinJu_00_Loop"] = A(120, {T("N_Loop_00", "FLVC", 0, 16, {{0, 100, 0}, {60, 0, 0}, {120, 100, 0}})});
}
} // namespace
int main() {
  int cases = 0;
  // No curves (files unreadable / not loaded yet): every value is its fallback, the spinner still turns.
  {
    const auto v = Sample(nullptr, 1.25);
    assert(v.loaded == 0 && v.weather == kAlphaLevels - 1 && v.temp == kAlphaLevels - 1 &&
           v.sensor == kAlphaLevels - 1 && v.champ == kAlphaLevels - 1);
    for (int k = 0; k < 12; ++k) {
      const int s = v.spin[size_t(k)];
      assert(k < 6 ? (s >= 500 && s <= 1000) : (s >= 1500 && s <= 2000));
    }
    // odd arms run half a cycle (10 native frames = 1/3 s) behind the even ones
    assert(SpinFallback(1, 0.0) == SpinFallback(0, 10.0) && SpinFallback(0, 0.0) == 500 && SpinFallback(0, 10.0) == 1000);
    assert(Sample(nullptr, -5).spin[0] == Sample(nullptr, 0).spin[0]);
    ++cases;
  }
  // Resolution: every curve found by its native entry/magic/index/target; a missing anim degrades only its own use.
  {
    Renderer r;
    Synthetic(r);
    const auto n = ResolveAll(r);
    assert(n.pins && n.quest && n.spinner && n.weather.track && n.temp.track && n.sensor.track && n.champ.track);
    assert(n.weather.period == 60 && n.flash.period == 1200 && n.spin[0].period == 20);
    Renderer r2;
    Synthetic(r2, false);
    const auto m = ResolveAll(r2);
    assert(!m.spinner && m.pins && m.quest && m.weather.track);
    const auto v = Sample(&m, 0.0);
    assert(v.loaded == 1 && v.spin[0] == SpinFallback(0, 0.0));
    assert(!Resolve(r, "nope", "N_Loop_00", "FLVC", 0, 16).track && !Resolve(r, "PaWeather_00_Loop", "N_Loop_00", "FLVC", 0, 15).track);
    ++cases;
  }
  // Native values: alpha levels at the keys, scales x1000, period wrap at the anim's own length (30 fps).
  {
    Renderer r;
    Synthetic(r);
    const auto n = ResolveAll(r);
    const auto a = Sample(&n, 0.0), b = Sample(&n, 1.0), c = Sample(&n, 2.0); // frames 0, 30, 60
    assert(a.weather == 15 && b.weather == 0 && c.weather == 15);
    assert(a.champ == 15 && Sample(&n, 2.0).champ == 0 && Sample(&n, 4.0).champ == 15); // 100 -> 0 -> 100 over 120
    assert(a.sensor == 15 && b.sensor == Level(150.0 / 255.0));
    assert(a.temp == Level(64.0 / 255.0) && Sample(&n, 0.2).temp == 15); // frame 6
    assert(a.spin[0] == 500 && Sample(&n, 0.3).spin[0] == 1000 && a.spin[6] == 1500 && Sample(&n, 0.3).spin[6] == 2000);
    assert(Sample(&n, 0.25).spin[0] == Sample(&n, 0.25 + 20.0 / 30.0).spin[0]); // 20-frame loop
    for (double t = 0; t < 3; t += 0.07)
      assert(n.weather.At(t * 30) == n.weather.At(t * 30 + 60));
    ++cases;
  }
  // Ready marks: native timing (half the old approximation's rate) on the same 15 Hz steps; fallback unchanged.
  {
    Renderer r;
    Synthetic(r);
    const auto n = ResolveAll(r);
    for (double t = 0; t < 5; t += 0.31) {
      const auto f = native_ready_anim(nullptr, t);
      assert(f == ready_anim(t));
    }
    const auto a = native_ready_anim(&n, 0.0);
    assert(a.flash == ReadyAlphaLevels - 1 && a.flame == 0 && a.ring_scale == 313 && a.ring_alpha == 0 &&
           a.quest_tint == ReadyQuestTints - 1);
    const auto b = native_ready_anim(&n, 1.0); // frame 30: flash at 78, ring at 0.8
    assert(b.flash == 0 && b.ring_scale == 1000 && b.ring_alpha == 0);
    assert(native_ready_anim(&n, 2.0).flash == ReadyAlphaLevels - 1);              // 2 s period
    assert(native_ready_anim(&n, 2.0).flame == 8 && native_ready_anim(&n, 4.0).flame == 0); // 16 frames / 4 s
    assert(native_ready_anim(&n, 0.5).ring_alpha == ReadyAlphaLevels - 1);         // frame 15: held at 255
    for (double t = 0; t < 6; t += 0.13) {
      const auto x = native_ready_anim(&n, t);
      assert(x.flame >= 0 && x.flame < ReadyFlameFrames && x.flash >= 0 && x.flash < ReadyAlphaLevels &&
             x.ring_alpha >= 0 && x.ring_alpha < ReadyAlphaLevels && x.quest_tint >= 0 && x.quest_tint < ReadyQuestTints);
    }
    ++cases;
  }
  // Publish keys and the probe frame key.
  {
    struct P { std::map<std::string, int64_t> v; } p;
    EdenDsmodHostApi h{};
    h.userdata = &p;
    h.publish_i64 = [](void *u, const char *k, int64_t x) { static_cast<P *>(u)->v[k] = x; };
    Publish(h, Sample(nullptr, 0.0));
    assert(p.v.size() == 5 + 12 && p.v.at("botw.fx.loaded") == 0 && p.v.at("botw.fx.spin.11") >= 1500);
    std::string e;
    int f = -1;
    assert(ParseFrameKey("module:fx/probe/0", e, f) && e == "probe" && f == 0);
    assert(!ParseFrameKey("module:fx/probe/1", e, f) && !ParseFrameKey("module:fx/probes/0", e, f));
    Renderer r;
    std::vector<u8> px;
    int w = 0, hh = 0;
    assert(!r.Render("probe", 0, px, w, hh)); // nothing before the layouts load
    ++cases;
  }
  std::cout << cases << " native UI curve (fallback/resolve/values/ready marks/publish/probe) cases passed\n";
}
