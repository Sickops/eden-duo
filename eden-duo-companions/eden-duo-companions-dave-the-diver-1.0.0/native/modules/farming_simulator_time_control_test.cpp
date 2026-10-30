// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the synthetic Lua memory fixture and reader evidence checks.
#define main ReaderEvidenceMain
#include "farming_simulator_reader_test.cpp"
#undef main

namespace {
using farming_simulator_reader::Lua;
using farming_simulator_reader::TimeControl;
using farming_simulator_reader::TimeScales;
using farming_simulator_reader::Value;
struct TimeFixture {
  Memory memory;
  Lua lua;
  TimeControl control;
  EdenDsmodHostWriteApi writes{};
  uint64_t input{}, empty{}, keyset[2]{}, action[2]{}, info{}, gui{};
  bool refuse{}, native_down[2]{};
  unsigned batches{}, native_setters{};
  std::function<void()> before_batch;

  uint64_t Leaf(unsigned mask) {
    auto table = memory.CleanTable();
    memory.Put<uint32_t>(table + 52, mask);
    memory.Put<uint64_t>(table + 56, table + 64 + 24 * (mask + 1));
    return table;
  }
  TimeFixture() {
    auto &m = memory;
    m.World();
    lua.Begin(m.host);
    info = lua.Field(m.mission, "missionInfo").Table();
    m.Num(info, "timeScale", 5);
    gui = m.CleanTable();
    m.Field(m.env, "g_gui", Table(gui));
    m.Field(gui, "currentGuiName", String(m.NewString("")));
    input = m.CleanTable();
    m.Field(m.env, "Input", Table(input));
    empty = Leaf(0);
    m.Field(input, "keyPressedState", Table(empty));
    m.Field(input, "keyPressedThisFrame", Table(Leaf(0)));
    auto binding = m.CleanTable(), actions = m.CleanTable();
    m.Field(m.env, "InputBinding", Table(binding));
    m.Field(binding, "actions", Table(actions));
    for (int i = 0; i != 2; ++i) {
      const int key = i ? 55 : 56;
      action[i] = m.CleanTable();
      m.Entry(actions, Number(65 + i), Table(action[i]));
      m.Field(action[i], "name", String(m.NewString(
          i ? "DECREASE_TIMESCALE" : "INCREASE_TIMESCALE")));
      m.Field(action[i], "keys1", Table(m.Array({Number(key)})));
      keyset[i] = Leaf(1);
      m.Entry(keyset[i], Number(key), Number(key));
      m.Field(action[i], "keys1Set", Table(keyset[i]));
      m.Field(action[i], "areKeys1Pressed", Nil);
      m.Field(action[i], "hasEvent", Nil);
    }
    writes = {EDEN_DSMOD_WRITE_EXT_VERSION, sizeof(writes),
              EDEN_DSMOD_WRITE_EXT_HASH, this,
              [](void *opaque, const EdenDsmodWriteOp *ops, uint32_t count)
                  -> EdenDsmodBool {
                auto &self = *static_cast<TimeFixture *>(opaque);
                ++self.batches;
                if (self.before_batch) {
                  auto hook = std::move(self.before_batch);
                  self.before_batch = {};
                  hook();
                }
                if (self.refuse || !count || count > EDEN_DSMOD_WRITE_BATCH_MAX_OPS)
                  return false;
                std::array<uint8_t, 64> current{};
                for (unsigned i = 0; i != count; ++i) {
                  if (!ops[i].expect || ops[i].size > 64 ||
                      !self.memory.host.read_memory(self.memory.host.userdata,
                          ops[i].address, current.data(), ops[i].size) ||
                      std::memcmp(current.data(), ops[i].expect, ops[i].size))
                    return false;
                }
                for (unsigned i = 0; i != count; ++i)
                  self.memory.PutBytes(ops[i].address, ops[i].value, ops[i].size);
                return true;
              }};
    control.Configure(&writes);
    Tick();
  }
  int Factor() {
    lua.Begin(memory.host);
    return int(*lua.Field(info, "timeScale").Number());
  }
  void Tick() {
    Snapshot next;
    next.world_ready = true;
    next.world_identity = memory.mission;
    next.time_scale = Factor();
    lua.Begin(memory.host);
    control.Tick(lua, memory.host, next);
  }
  void NativeFrame() {
    lua.Begin(memory.host);
    for (int i = 0; i != 2; ++i) {
      const bool down = lua.Index(empty, i ? 55 : 56).Number().has_value();
      if (down && !native_down[i]) {
        int index = farming_simulator_reader::TimeScaleIndex(Factor());
        index = std::clamp(index + (i ? -1 : 1), 0, 5);
        memory.Num(info, "timeScale", TimeScales[index]);
        ++native_setters; // Models the native handler, never the companion.
      }
      native_down[i] = down;
      memory.Field(action[i], "areKeys1Pressed", down ? True : Nil);
      memory.Field(action[i], "hasEvent", down ? True : Nil);
    }
  }
  void Advance() {
    NativeFrame();
    control.release_after = {};
    Tick();
  }
  void Restored() {
    lua.Begin(memory.host);
    Require(lua.Entries(empty).empty(), "native pressed state not restored");
    for (int i = 0; i != 2; ++i) {
      const int key = i ? 55 : 56;
      auto entries = lua.Entries(keyset[i]);
      Require(entries.size() == 1 && entries[0].first.Number() == key &&
                  entries[0].second.Number() == key,
              "binding numeric storage damaged");
    }
  }
};
} // namespace

int main() {
  try {
    Require(farming_simulator_reader::TimeScaleIndex(1) == 0 &&
                farming_simulator_reader::TimeScaleIndex(120) == 5 &&
                farming_simulator_reader::TimeScaleIndex(2000) == -1 &&
                farming_simulator_reader::TimeScaleIndex(
                    std::numeric_limits<double>::quiet_NaN()) == -1,
            "native scale choice validation");
    {
      TimeFixture f;
      Require(!f.control.Request(f.lua, 7), "unsupported scale accepted");
      Require(f.control.Request(f.lua, 120), "native key not accepted");
      Require(!f.control.Request(f.lua, 1), "pending transaction overwritten");
      for (unsigned i = 0; i < 30 &&
                           (f.control.active || f.control.gap || f.control.target >= 0); ++i)
        f.Advance();
      Require(f.Factor() == 120 && f.native_setters == 4,
              "target selection skipped/duplicated native setter edges");
      f.Restored();
      Require(!f.native_down[0], "last increase edge not released");
      Require(f.control.Request(f.lua, 1), "reverse request refused after release");
      for (unsigned i = 0; i < 40 &&
                           (f.control.active || f.control.gap || f.control.target >= 0); ++i)
        f.Advance();
      Require(f.Factor() == 1 && f.native_setters == 9,
              "reverse native key edges incorrect");
      f.Restored();
    }
    {
      TimeFixture f;
      f.before_batch = [&] {
        f.memory.Put<uint64_t>(f.input + 40, f.input + 128);
      };
      Require(!f.control.Request(f.lua, 15), "parent rehash race accepted");
      f.Restored();
      Require(f.Factor() == 5, "rejected atomic start changed factor");
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 15), "cancel setup failed");
      f.memory.Put<uint8_t>(f.empty + 8, 4); // GC color may change during hold.
      f.memory.Put<uint8_t>(f.keyset[0] + 8, 1);
      f.refuse = true;
      Require(!f.control.Cancel() && f.control.active,
              "refused release forgot pending cleanup");
      f.refuse = false;
      Require(f.control.Cancel(), "retry could not restore native key");
      f.Restored();
      Require(f.Factor() == 5, "cancel wrote time scalar");
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 15), "timeout setup failed");
      f.control.deadline = {};
      f.Tick();
      Require(!f.control.active && f.control.target < 0,
              "unacknowledged native input not canceled");
      f.Restored();
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 15), "menu cancel setup failed");
      f.memory.Field(f.gui, "currentGuiName", String(f.memory.NewString("Shop")));
      f.Tick();
      Require(!f.control.active, "native menu did not release held key");
      f.Restored();
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 15), "rehash cleanup setup failed");
      // A normal native parent rehash moves TValue nodes while preserving roots.
      const auto previous = f.input + 64;
      const auto replacement = f.memory.Allocate(64 * 24);
      std::array<uint8_t, 64 * 24> nodes{};
      Require(f.memory.host.read_memory(f.memory.host.userdata, previous,
                                       nodes.data(), nodes.size()), "rehash fixture read");
      f.memory.PutBytes(replacement, nodes.data(), nodes.size());
      f.memory.Put<uint64_t>(f.input + 40, replacement);
      for (auto &[key, address] : f.memory.slots)
        if (key.first == f.input)
          address += replacement - previous;
      Require(f.control.Cancel(), "legitimate parent rehash blocked cleanup");
      f.Restored();
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 15), "world loss cleanup setup failed");
      f.memory.Field(f.memory.env, "g_currentMission", Nil);
      Snapshot lost_world;
      f.lua.Begin(f.memory.host);
      f.control.Tick(f.lua, f.memory.host, lost_world);
      Require(!f.control.active && !lost_world.time_scale_pending,
              "world loss left native key held");
      f.Restored();
    }
    {
      TimeFixture f;
      Require(f.control.Request(f.lua, 30), "retry target setup failed");
      f.Advance(); // Native5→15 acknowledged; original storage restored.
      Require(!f.control.active && f.control.gap, "missing release gap");
      f.refuse = true;
      f.Advance();
      f.Advance(); // The next pulse is atomically refused.
      Require(f.Factor() == 15 && f.control.gap && f.control.target == 3,
              "transient host refusal abandoned target selection");
      f.refuse = false;
      for (unsigned i = 0; i < 12 &&
                           (f.control.active || f.control.gap || f.control.target >= 0); ++i)
        f.Advance();
      Require(f.Factor() == 30 && f.native_setters == 2,
              "bounded retry did not finish native target");
      f.Restored();
    }
    {
      TimeFixture f;
      f.lua.pages.clear();
      f.lua.bytes = 4194304;
      Snapshot next;
      next.world_ready = true;
      next.world_identity = f.memory.mission;
      next.time_scale = 5;
      f.control.Tick(f.lua, f.memory.host, next);
      Require(next.time_scale_change_allowed && f.control.reason == 0 &&
                  f.lua.bytes < 4194304,
              "previous sample read budget starved fresh input validation");
      Reader reader(f.memory.host, "");
      reader.SetDiagnostics(true);
      reader.ConfigureWrites(&f.writes);
      reader.Sample(f.memory.host);
      Require(reader.GetSnapshot().time_scale_change_allowed &&
                  !reader.GetSnapshot().native_menu_open,
              "actual reader did not expose allowed native time control");
      Require(f.memory.published["fs.reader.bytes"] >
                  f.memory.published["fs.time.reader.bytes"],
              "aggregate reader metrics omitted main/input work");
      f.memory.Field(f.gui, "currentGuiName", String(f.memory.NewString("Shop")));
      reader.Sample(f.memory.host);
      Require(reader.GetSnapshot().native_menu_open &&
                  !reader.GetSnapshot().time_scale_change_allowed,
              "known native menu gate not published");
    }
    std::puts("PASS native time input: targets, edges, ancestry race/rehash, cancellation, GC colors, timeout, menu/world gates");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL native time input: %s\n", e.what());
    return 1;
  }
}
