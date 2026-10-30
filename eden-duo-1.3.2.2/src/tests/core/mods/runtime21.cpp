// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Runtime 21: the @device.* / @perf.* host values (mod_host_values.h) with injected sources, the
// manifest gating, and the non-resetting PerfStats totals they are computed from.
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "core/mods/mod_host_values.h"
#include "core/mods/mod_runtime.h"
#include "core/perf_stats.h"

using namespace Core::Mods;

namespace {
constexpr s64 Ms = 1'000'000;

/// A controllable clock, battery and perf counter set.
struct FakeHost {
    s64 now{1'000 * Ms};
    PowerReading power{.present = true, .pct = 80, .charging = false};
    std::optional<PerfSample> perf{PerfSample{}};
    u64 perf_reads{0};

    HostValueSource Source() {
        return {.now_ns = [this] { return now; },
                .power = [this] { return power; },
                .perf =
                    [this] {
                        ++perf_reads;
                        return perf;
                    }};
    }
    /// Advances the clock and the counters as a game at `fps` with `frametime_ms` per system
    /// frame (60 system frames a second) and `speed` emulated seconds per wall second. The
    /// counters are whole numbers rounded from exact running sums.
    void Run(s64 ms, f64 fps, f64 frametime_ms = 10.0, f64 speed = 1.0) {
        now += ms * Ms;
        if (!perf) {
            return;
        }
        const f64 s = static_cast<f64>(ms) / 1000.0;
        game += fps * s;
        system += 60.0 * s;
        frametime += 60.0 * s * frametime_ms * 1e6;
        emulated += s * speed * 1e6;
        perf->game_frames = static_cast<u64>(std::llround(game));
        perf->system_frames = static_cast<u64>(std::llround(system));
        perf->frametime_ns = std::llround(frametime);
        perf->emulated_us = std::llround(emulated);
    }
    /// A new PerfStats: every counter starts again from zero.
    void Restart() {
        perf = PerfSample{};
        game = system = frametime = emulated = 0.0;
    }

    f64 game{0.0};
    f64 system{0.0};
    f64 frametime{0.0};
    f64 emulated{0.0};
};

/// Ticks every 10 ms for `ms` milliseconds.
void TickFor(HostValues& values, FakeHost& host, s64 ms, f64 fps, f64 frametime_ms = 10.0,
             f64 speed = 1.0) {
    for (s64 t = 0; t < ms; t += 10) {
        host.Run(10, fps, frametime_ms, speed);
        values.Update();
    }
}
} // namespace

TEST_CASE("DSMod host values: fps, speed and frametime from counter deltas",
          "[dsmod][runtime21][host_values]") {
    FakeHost host;
    HostValues values;
    values.SetSource(host.Source());
    values.Update(); // first sample: the baseline only
    StateSnapshot s;
    values.Publish(s);
    REQUIRE(s.ints.at("@device.battery_present") == 1);
    REQUIRE(s.ints.at("@device.battery_pct") == 80);
    REQUIRE(s.ints.at("@device.charging") == 0);
    REQUIRE_FALSE(s.ints.contains("@perf.fps")); // nothing before a full interval
    REQUIRE_FALSE(values.GetI64("@perf.fps").has_value());

    host.Run(499, 60.0);
    values.Update();
    REQUIRE_FALSE(values.HasPerf()); // 499 ms: still nothing

    host.Run(1, 60.0);
    values.Update();
    REQUIRE(values.HasPerf());
    StateSnapshot p;
    values.Publish(p);
    REQUIRE(p.ints.at("@perf.fps") == 60);
    REQUIRE(p.floats.at("@perf.fps") == Catch::Approx(60.0).margin(0.01));
    REQUIRE(p.ints.at("@perf.speed_pct") == 100);
    REQUIRE(p.floats.at("@perf.frametime_ms") == Catch::Approx(10.0));
    REQUIRE(p.ints.at("@perf.frametime_ms") == 10);
    REQUIRE(p.ints.at("@perf.frametime_x10") == 100);

    // A slow stretch: 30 fps, 25.04 ms frames, 75 % speed.
    host.Run(500, 30.0, 25.04, 0.75);
    values.Update();
    values.Publish(p);
    REQUIRE(p.ints.at("@perf.fps") == 30);
    REQUIRE(p.ints.at("@perf.speed_pct") == 75);
    REQUIRE(p.floats.at("@perf.frametime_ms") == Catch::Approx(25.04));
    REQUIRE(p.ints.at("@perf.frametime_x10") == 250);
    REQUIRE(values.GetI64("@perf.fps") == 30);
    REQUIRE(values.GetF64("@perf.frametime_ms").value() == Catch::Approx(25.04));
    REQUIRE(values.GetF64("@device.battery_pct") == 80.0);
    REQUIRE_FALSE(values.GetI64("@perf.unknown").has_value());
}

TEST_CASE("DSMod host values: change at most twice a second at tick rate",
          "[dsmod][runtime21][host_values]") {
    FakeHost host;
    HostValues values;
    values.SetSource(host.Source());
    values.Update();
    TickFor(values, host, 3000, 58.0);
    // 300 ticks: one baseline read plus one read per 500 ms (the counters are read lazily).
    REQUIRE(host.perf_reads == 7);
    REQUIRE(values.GetI64("@perf.fps") == 58);
    REQUIRE(values.PowerReads() == 1); // the battery waits for its own interval
    host.Run(static_cast<s64>(HostValues::PowerIntervalNs / Ms), 58.0);
    host.power = {.present = true, .pct = 79, .charging = true};
    values.Update();
    REQUIRE(values.PowerReads() == 2);
    REQUIRE(values.GetI64("@device.battery_pct") == 79);
    REQUIRE(values.GetI64("@device.charging") == 1);
}

TEST_CASE("DSMod host values: a gap or counters going backwards start a new interval",
          "[dsmod][runtime21][host_values]") {
    FakeHost host;
    HostValues values;
    values.SetSource(host.Source());
    values.Update();
    TickFor(values, host, 1000, 60.0);
    REQUIRE(values.GetI64("@perf.fps") == 60);

    // Paused for 5 s: the counters stand still. The next tick rebaselines and keeps 60.
    host.now += 5000 * Ms;
    values.Update();
    REQUIRE(values.GetI64("@perf.fps") == 60);
    host.Run(500, 20.0);
    values.Update();
    REQUIRE(values.GetI64("@perf.fps") == 20); // measured over the 500 ms after the pause only

    // A new PerfStats: the counters restart from zero. Rebaseline, keep the last values.
    host.Restart();
    host.Run(500, 46.0);
    values.Update();
    REQUIRE(values.GetI64("@perf.fps") == 20);
    host.Run(500, 46.0);
    values.Update();
    REQUIRE(values.GetI64("@perf.fps") == 46);

    // No stats at all (between games): nothing new, the last values stay.
    host.perf.reset();
    host.Run(600, 0.0);
    values.Update();
    REQUIRE(values.GetI64("@perf.fps") == 46);
}

TEST_CASE("DSMod host values: no system frames keeps the previous frametime",
          "[dsmod][runtime21][host_values]") {
    FakeHost host;
    HostValues values;
    values.SetSource(host.Source());
    values.Update();
    host.Run(500, 60.0, 12.0);
    values.Update();
    REQUIRE(values.GetF64("@perf.frametime_ms").value() == Catch::Approx(12.0));
    // 500 ms in which only the emulated clock moved.
    host.now += 500 * Ms;
    host.perf->emulated_us += 500'000;
    values.Update();
    StateSnapshot s;
    values.Publish(s);
    REQUIRE(s.ints.at("@perf.fps") == 0);
    REQUIRE(std::isfinite(s.floats.at("@perf.frametime_ms")));
    REQUIRE(s.floats.at("@perf.frametime_ms") == Catch::Approx(12.0));
    REQUIRE(s.ints.at("@perf.speed_pct") == 100);

    // Never a system frame: no frametime key at all, fps still published.
    FakeHost idle;
    HostValues none;
    none.SetSource(idle.Source());
    none.Update();
    idle.now += 500 * Ms;
    none.Update();
    StateSnapshot n;
    none.Publish(n);
    REQUIRE(n.ints.at("@perf.fps") == 0);
    REQUIRE_FALSE(n.floats.contains("@perf.frametime_ms"));
    REQUIRE_FALSE(n.ints.contains("@perf.frametime_x10"));
}

TEST_CASE("DSMod host values: no battery and no perf source", "[dsmod][runtime21][host_values]") {
    FakeHost host;
    host.power = {.present = false, .pct = 42, .charging = true}; // normalised
    HostValues values;
    values.SetSource({.now_ns = [&] { return host.now; }, .power = [&] { return host.power; }});
    values.Update();
    host.now += 1000 * Ms;
    values.Update();
    StateSnapshot s;
    values.Publish(s);
    REQUIRE(s.ints.at("@device.battery_present") == 0);
    REQUIRE(s.ints.at("@device.battery_pct") == -1);
    REQUIRE(s.ints.at("@device.charging") == 0);
    REQUIRE_FALSE(s.ints.contains("@perf.fps"));
    REQUIRE(values.GetI64("@device.battery_pct") == -1);
}

TEST_CASE("DSMod host values: EDEN_DSMOD_FAKE_BATTERY parse", "[dsmod][runtime21][host_values]") {
    const auto charging = ParseFakeBattery("57,1");
    REQUIRE(charging.has_value());
    REQUIRE(charging->present);
    REQUIRE(charging->pct == 57);
    REQUIRE(charging->charging);
    const auto plain = ParseFakeBattery(" 100 ");
    REQUIRE(plain.has_value());
    REQUIRE(plain->pct == 100);
    REQUIRE_FALSE(plain->charging);
    REQUIRE(ParseFakeBattery("140,0")->pct == 100);
    const auto none = ParseFakeBattery("none");
    REQUIRE(none.has_value());
    REQUIRE_FALSE(none->present);
    REQUIRE(none->pct == -1);
    REQUIRE_FALSE(ParseFakeBattery("-1,1")->present);
    REQUIRE_FALSE(ParseFakeBattery("").has_value());
    REQUIRE_FALSE(ParseFakeBattery("abc").has_value());
    REQUIRE_FALSE(ParseFakeBattery("50,2").has_value());
    REQUIRE_FALSE(ParseFakeBattery("50,").has_value());
    // The device reading is always normalised (present 0 comes with -1).
    const PowerReading device = ReadDevicePower();
    if (!device.present) {
        REQUIRE(device.pct == -1);
        REQUIRE_FALSE(device.charging);
    } else {
        REQUIRE(device.pct >= 0);
        REQUIRE(device.pct <= 100);
    }
}

TEST_CASE("DSMod host values: only a package that names the keys gets them",
          "[dsmod][runtime21][host_values]") {
    const auto page = nlohmann::json::parse(
        R"({"pages":[{"id":"p","widgets":[{"type":"value","rect":[0,0,10,10],"bind":"hp"}]}]})");
    Manifest m;
    REQUIRE(ParseDualScreenManifest(page, m));
    REQUIRE_FALSE(m.uses_host_keys);
    REQUIRE_FALSE(JsonReferencesHostKeys(page));
    auto bound = page;
    bound["pages"][0]["widgets"][0]["bind"] = "@perf.fps";
    Manifest m2;
    REQUIRE(ParseDualScreenManifest(bound, m2));
    REQUIRE(m2.uses_host_keys);
    REQUIRE_FALSE(m2.uses_clock_keys);
    REQUIRE(JsonReferencesHostKeys(
        nlohmann::json::parse(R"({"derived":{"low":{"expr":"@device.battery_pct < 20"}}})")));
    REQUIRE(JsonReferencesHostKeys(nlohmann::json::parse(R"({"gates":{"@device.charging":1}})")));
    REQUIRE(JsonReferencesHostKeys(nlohmann::json::parse(R"([1, ["@perf.frametime_x10"]])")));
    REQUIRE_FALSE(JsonReferencesHostKeys(nlohmann::json::parse(R"({"a":["@flag:perf", 3]})")));
    REQUIRE_FALSE(JsonReferencesHostKeys(nlohmann::json::parse(R"({"device":"perf"})")));
}

TEST_CASE("DSMod host values: keys are host keys, not interaction", "[dsmod][runtime21]") {
    REQUIRE(IsHostValueKey("@device.battery_pct"));
    REQUIRE(IsHostValueKey("@perf.fps"));
    REQUIRE_FALSE(IsHostValueKey("@sel:list"));
    REQUIRE_FALSE(IsHostValueKey("perf.fps"));
    REQUIRE_FALSE(IsHostValueKey("@clock.hour"));
    REQUIRE_FALSE(IsClockKey("@perf.fps"));
    REQUIRE(DualScreenRuntimeVersion >= 21);
}

TEST_CASE("DSMod PerfStats totals do not disturb GetAndResetStats", "[dsmod][runtime21][perf]") {
    auto stats = std::make_unique<Core::PerfStats>(0); // title 0: no frame-time CSV on exit
    const auto t0 = stats->GetTotals();
    REQUIRE(t0.game_frames == 0);
    REQUIRE(t0.system_frames == 0);
    (void)stats->GetAndResetStats(std::chrono::microseconds{0});
    for (int i = 0; i < 3; ++i) {
        stats->BeginSystemFrame();
        stats->EndSystemFrame();
        stats->EndGameFrame();
        stats->EndGameFrame();
    }
    const auto t1 = stats->GetTotals();
    REQUIRE(t1.game_frames == 6);
    REQUIRE(t1.system_frames == 3);
    REQUIRE(t1.frametime >= Core::PerfStats::Clock::duration::zero());
    REQUIRE(stats->GetTotals().game_frames == 6); // reading does not reset

    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    const auto r = stats->GetAndResetStats(std::chrono::microseconds{1000});
    REQUIRE(r.system_fps > 0.0); // the reset reader still saw the 3 frames
    const auto t2 = stats->GetTotals();
    REQUIRE(t2.game_frames == 6); // ...and resetting did not touch the totals
    REQUIRE(t2.system_frames == 3);
    REQUIRE(t2.frametime == t1.frametime);
    stats->EndGameFrame();
    REQUIRE(stats->GetTotals().game_frames == 7);
}
