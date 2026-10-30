// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Native InputBinding key lane. No time, crop clock, or save setting is written.
// Both leaf table identities, GC headers and roots stay in place. Only the
// hash-only, non-colocated numeric storage descriptors exchange ownership.
constexpr std::array<int, 6> TimeScales{1, 5, 15, 30, 60, 120};
int TimeScaleIndex(double value) {
  auto it = std::find(TimeScales.begin(), TimeScales.end(), value);
  return it == TimeScales.end() ? -1 : int(it - TimeScales.begin());
}
struct TimeGuard {
  uint64_t address{};
  std::vector<uint8_t> bytes;
};
struct TimeLeaf {
  uint64_t address{};
  std::array<uint8_t, 64> header{};
};
struct TimeContext {
  uint64_t object{}, state{}, env{}, mission{}, input{}, action{}, input_binding{}, actions{};
  uint32_t action_index{};
  TimeLeaf empty, binding;
  std::vector<TimeGuard> guards;
  TimeGuard numeric_nodes;
  int index{-1};
  bool key_down{};
};
struct TimeControl {
  EdenDsmodHostWriteApi writes{};
  EdenDsmodHostApi host{};
  TimeContext held;
  bool has_host{}, active{}, releasing{}, canceled{}, gap{}, last_up{};
  bool diagnostics_enabled{}; // off in normal runs; a test sets it directly
  int target{-1}, before{-1}, expected{-1};
  unsigned released_samples{};
  int reason{}, last_refusal{};
  uint64_t refusal_count{}, refusal_bytes{};
  std::chrono::steady_clock::time_point deadline{}, release_after{};

  bool Supported() const { return writes.write_batch != nullptr; }
  void Configure(const EdenDsmodHostWriteApi *api) {
    if (active)
      return; // Never discard the service needed to release an injected key.
    writes = {};
    if (api && api->version == EDEN_DSMOD_WRITE_EXT_VERSION &&
        api->struct_size == sizeof(*api) &&
        api->abi_hash == EDEN_DSMOD_WRITE_EXT_HASH && api->write_batch)
      writes = *api;
  }
  static TimeGuard Guard(Lua &l, uint64_t address, size_t size) {
    if (!address || !size || size > 64)
      throw Invalid{};
    TimeGuard out{address, std::vector<uint8_t>(size)};
    l.Read(address, out.bytes.data(), size);
    return out;
  }
  static TimeGuard FieldGuard(Lua &l, uint64_t table, const char *name) {
    l.Field(table, name);
    auto it = l.lookups.find({table, name});
    if (it == l.lookups.end() || !it->second.address)
      throw Invalid{};
    return Guard(l, it->second.address, 16); // existing TValue and string key
  }
  static uint64_t IndexAddress(Lua &l, uint64_t table, uint32_t index) {
    if (index < l.Get<uint32_t>(table + 48))
      return l.Get<uint64_t>(table + 16) + index * 8;
    auto nodes = l.Get<uint64_t>(table + 40);
    auto mask = l.Get<uint32_t>(table + 52);
    if (mask > 4095 || (mask & (mask + 1)))
      throw Invalid{};
    for (uint32_t i = 0; i <= mask; ++i) {
      auto at = nodes + i * 24;
      if (Value{l.Get<uint64_t>(at)}.Table() &&
          Value{l.Get<uint64_t>(at + 8)}.Number() == index)
        return at;
    }
    throw Invalid{};
  }
  static TimeLeaf Leaf(Lua &l, uint64_t address, bool empty, int key) {
    TimeLeaf out{address, l.Get<std::array<uint8_t, 64>>(address)};
    auto u64 = [&](size_t offset) {
      uint64_t v{};
      std::memcpy(&v, out.header.data() + offset, 8);
      return v;
    };
    auto u32 = [&](size_t offset) {
      uint32_t v{};
      std::memcpy(&v, out.header.data() + offset, 4);
      return v;
    };
    if (out.header[9] != 11 || out.header[11] != 0 || u64(16) ||
        u64(32) || u32(48) || u32(52) != (empty ? 0U : 1U))
      throw Invalid{};
    auto entries = l.Entries(address);
    if (empty) {
      if (!entries.empty())
        throw Invalid{};
    } else {
      if (entries.size() != 1 || entries[0].first.Number() != key ||
          entries[0].second.Number() != key)
        throw Invalid{};
      // Dead numeric keys are harmless, GC-valued keys are never moved.
      for (uint32_t i = 0; i != 2; ++i) {
        auto at = u64(40) + 24 * i;
        Value value{l.Get<uint64_t>(at)}, k{l.Get<uint64_t>(at + 8)};
        if ((value.Tag() != 0x1ffff && !value.Number()) ||
            (k.Tag() != 0x1ffff && !k.Number()))
          throw Invalid{};
      }
    }
    return out;
  }
  static bool Empty(Lua &l, uint64_t table) {
    return table && l.Entries(table).empty();
  }
  static TimeContext Context(Lua &l, const EdenDsmodHostApi &h, bool up,
                             int *diagnostic = nullptr) {
    auto stage = [&](int value) { if (diagnostic) *diagnostic = value; };
    stage(1); // ScriptSystem/global root
    TimeContext out;
    auto root = ResolveScriptRoot(l, h);
    out.object = root.object;
    out.state = root.state;
    out.env = root.env;
    CheckGlobalTable(l, out.env);
    stage(2); // Live mission gate
    out.mission = l.Field(out.env, "g_currentMission").Table();
    if (!out.mission || !l.Field(out.mission, "isRunning").True() ||
        !l.Field(out.mission, "isLoaded").True() ||
        l.Field(out.mission, "isLoadingMap").True())
      throw Invalid{};
    auto info = l.Field(out.mission, "missionInfo").Table();
    stage(3); // Recognized native factor
    auto factor = l.Field(info, "timeScale").Number();
    if (!factor || (out.index = TimeScaleIndex(*factor)) < 0)
      throw Invalid{};
    stage(4); // Empty native GUI
    auto gui = l.Field(out.env, "g_gui").Table();
    auto gui_name = l.Field(gui, "currentGuiName");
    if (gui_name.Tag() != 0x1fffb || !l.Text(gui_name).empty())
      throw Invalid{};
    stage(5); // Input/action identity
    out.input = l.Field(out.env, "Input").Table();
    auto binding = l.Field(out.env, "InputBinding").Table();
    auto actions = l.Field(binding, "actions").Table();
    const auto action_index = up ? 65U : 66U;
    out.input_binding = binding;
    out.actions = actions;
    out.action_index = action_index;
    const auto key = up ? 56 : 55;
    out.action = l.Index(actions, action_index).Table();
    if (!out.action || l.Text(l.Field(out.action, "name")) !=
                           (up ? "INCREASE_TIMESCALE" : "DECREASE_TIMESCALE"))
      throw Invalid{};
    stage(6); // Exact literal native key binding
    auto keys = l.Entries(l.Field(out.action, "keys1").Table());
    if (keys.size() != 1 || keys[0].first.Number() != 1 ||
        keys[0].second.Number() != key)
      throw Invalid{};
    // Reject rebinding/conflicts, real keyboard activity, and changed layouts.
    stage(7); // Conflicting native bindings
    for (auto [index_value, value] : l.Entries(actions)) {
      auto action = value.Table();
      if (!action)
        continue;
      for (const auto *field : {"keys1", "keys2", "keys3", "keys4"}) {
        for (auto [unused, bound] : l.Entries(l.Field(action, field).Table())) {
          if (bound.Number() == key && action != out.action)
            throw Invalid{};
        }
      }
    }
    stage(8); // Real keyboard activity
    if (!Empty(l, l.Field(out.input, "keyPressedThisFrame").Table()))
      throw Invalid{};
    stage(9); // Empty pressed-state storage layout
    out.empty = Leaf(l, l.Field(out.input, "keyPressedState").Table(), true, key);
    stage(10); // Numeric singleton reverse-lookup storage
    out.binding = Leaf(l, l.Field(out.action, "keys1Set").Table(), false, key);
    if (out.empty.address == out.binding.address)
      throw Invalid{};
    stage(11); // Existing native key edge
    out.key_down = l.Field(out.action, "areKeys1Pressed").True() ||
                   l.Field(out.action, "hasEvent").True();
    stage(12); // Atomic ancestry guards
    auto add = [&](TimeGuard guard) { out.guards.push_back(std::move(guard)); };
    add(Guard(l, h.main_base + 0x46cb40, 8));
    add(Guard(l, out.object, 8));
    add(FieldGuard(l, out.env, "g_currentMission"));
    add(FieldGuard(l, out.mission, "missionInfo"));
    add(FieldGuard(l, info, "timeScale"));
    add(FieldGuard(l, out.env, "Input"));
    add(FieldGuard(l, out.input, "keyPressedState"));
    add(FieldGuard(l, out.env, "InputBinding"));
    add(FieldGuard(l, binding, "actions"));
    add(Guard(l, IndexAddress(l, actions, action_index), 8));
    add(FieldGuard(l, out.action, "keys1Set"));
    add(FieldGuard(l, out.env, "g_gui"));
    add(FieldGuard(l, gui, "currentGuiName"));
    // Guard every ancestor's storage identity as well as each child TValue.
    // A native rehash must never leave a stale but still readable node accepted.
    add(Guard(l, out.state + 72, 8));
    for (auto table : {out.env, out.mission, info, out.input, binding,
                       actions, out.action, gui})
      add(Guard(l, table, 64));
    uint64_t nodes{};
    std::memcpy(&nodes, out.binding.header.data() + 40, 8);
    out.numeric_nodes = Guard(l, nodes, 48);
    stage(0);
    return out;
  }
  static void SwapStorage(std::array<uint8_t, 64> &a,
                          std::array<uint8_t, 64> &b) {
    for (auto [offset, size] : {std::pair{16, 8}, std::pair{40, 24}})
      for (int i = offset; i < offset + size; ++i)
        std::swap(a[i], b[i]);
  }
  bool Start(TimeContext context) {
    if (!Supported() || active || context.key_down)
      return false;
    auto a = context.empty.header, b = context.binding.header;
    SwapStorage(a, b);
    std::vector<EdenDsmodWriteOp> ops;
    ops.push_back({context.empty.address, 64, 0, context.empty.header.data(), a.data()});
    ops.push_back({context.binding.address, 64, 0, context.binding.header.data(), b.data()});
    for (const auto &guard : context.guards)
      ops.push_back({guard.address, uint32_t(guard.bytes.size()), 0,
                     guard.bytes.data(), guard.bytes.data()});
    ops.push_back({context.numeric_nodes.address, 48, 0,
                   context.numeric_nodes.bytes.data(), context.numeric_nodes.bytes.data()});
    if (ops.size() > EDEN_DSMOD_WRITE_BATCH_MAX_OPS ||
        !writes.write_batch(writes.userdata, ops.data(), uint32_t(ops.size())))
      return false;
    before = context.index;
    last_up = target > before;
    expected = before + (last_up ? 1 : -1);
    held = std::move(context);
    active = true;
    releasing = canceled = false;
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    return true;
  }
  bool Release() {
    if (!active)
      return true;
    if (!Supported() || !has_host || !host.read_memory)
      return false;
    uint64_t object{}, state{};
    if (!host.read_memory(host.userdata, host.main_base + 0x46cb40, &object, 8) ||
        !host.read_memory(host.userdata, held.object, &state, 8))
      return false;
    if (object != held.object || state != held.state) {
      // The entire old Lua state has gone; never write its freed allocations.
      active = false;
      target = -1;
      gap = false;
      return true;
    }
    auto read_header = [&](const TimeLeaf &leaf) {
      std::array<uint8_t, 64> h{};
      if (!host.read_memory(host.userdata, leaf.address, h.data(), 64))
        throw Invalid{};
      return h;
    };
    try {
      auto a = read_header(held.empty), b = read_header(held.binding);
      auto wanted_a = held.empty.header, wanted_b = held.binding.header;
      SwapStorage(wanted_a, wanted_b);
      for (auto [offset, size] : {std::pair{16, 8}, std::pair{40, 24}}) {
        if (std::memcmp(a.data() + offset, wanted_a.data() + offset, size) ||
            std::memcmp(b.data() + offset, wanted_b.data() + offset, size))
          return false;
      }
      if (a[9] != 11 || b[9] != 11 || a[11] || b[11] ||
          std::memcmp(a.data() + 32, held.empty.header.data() + 32, 8) ||
          std::memcmp(b.data() + 32, held.binding.header.data() + 32, 8))
        return false;
      auto restore_a = a, restore_b = b;
      SwapStorage(restore_a, restore_b); // preserve current GC color/links/nomm
      // Resolve only the permanent input ancestry afresh. Cleanup remains
      // independent of mission/GUI and accepts a legitimate parent rehash.
      Lua cleanup;
      cleanup.Begin(host);
      if (cleanup.Get<uint64_t>(held.state + 72) != held.env ||
          cleanup.Field(held.env, "Input").Table() != held.input ||
          cleanup.Field(held.env, "InputBinding").Table() != held.input_binding ||
          cleanup.Field(held.input_binding, "actions").Table() != held.actions ||
          cleanup.Index(held.actions, held.action_index).Table() != held.action ||
          cleanup.Field(held.input, "keyPressedState").Table() != held.empty.address ||
          cleanup.Field(held.action, "keys1Set").Table() != held.binding.address)
        return false;
      std::vector<TimeGuard> guards;
      guards.push_back(Guard(cleanup, held.state + 72, 8));
      for (auto table : {held.env, held.input, held.input_binding,
                         held.actions, held.action})
        guards.push_back(Guard(cleanup, table, 64));
      guards.push_back(FieldGuard(cleanup, held.env, "Input"));
      guards.push_back(FieldGuard(cleanup, held.env, "InputBinding"));
      guards.push_back(FieldGuard(cleanup, held.input_binding, "actions"));
      guards.push_back(Guard(cleanup,
                            IndexAddress(cleanup, held.actions, held.action_index), 8));
      guards.push_back(FieldGuard(cleanup, held.input, "keyPressedState"));
      guards.push_back(FieldGuard(cleanup, held.action, "keys1Set"));
      std::vector<EdenDsmodWriteOp> ops{
          {held.empty.address, 64, 0, a.data(), restore_a.data()},
          {held.binding.address, 64, 0, b.data(), restore_b.data()},
          {host.main_base + 0x46cb40, 8, 0, &held.object, &held.object},
          {held.object, 8, 0, &held.state, &held.state},
          {held.numeric_nodes.address, 48, 0, held.numeric_nodes.bytes.data(), held.numeric_nodes.bytes.data()}};
      for (const auto &guard : guards)
        ops.push_back({guard.address, uint32_t(guard.bytes.size()), 0,
                       guard.bytes.data(), guard.bytes.data()});
      if (ops.size() > EDEN_DSMOD_WRITE_BATCH_MAX_OPS ||
          !writes.write_batch(writes.userdata, ops.data(), uint32_t(ops.size())))
        return false;
      active = releasing = false;
      held = {};
      gap = !canceled;
      released_samples = 0;
      release_after = std::chrono::steady_clock::now() + std::chrono::milliseconds{80};
      deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
      if (canceled)
        target = -1;
      return true;
    } catch (...) {
      return false;
    }
  }
  bool Cancel() {
    canceled = releasing = true;
    gap = false;
    target = -1;
    return Release();
  }
  bool Request(Lua &l, int64_t value) {
    if (!Supported() || !has_host || active || gap || target >= 0)
      return false;
    const int wanted = TimeScaleIndex(double(value));
    if (wanted < 0)
      return false;
    try {
      l.Begin(host);
      auto initial = Context(l, host, true);
      if (initial.index == wanted)
        return true;
      if (wanted < initial.index) {
        l.Begin(host);
        initial = Context(l, host, false);
      }
      target = wanted;
      if (Start(std::move(initial)))
        return true;
      target = -1;
    } catch (...) {
      target = -1;
    }
    return false;
  }
  void Tick(Lua &l, const EdenDsmodHostApi &h, farming_simulator::Snapshot &next) {
    host = h;
    has_host = true;
    l.Begin(h);
    const auto now = std::chrono::steady_clock::now();
    if (active) {
      if (!next.world_ready || next.world_identity != held.mission ||
          !next.time_scale) {
        canceled = releasing = true;
      } else {
        const int index = TimeScaleIndex(*next.time_scale);
        if (index != before) {
          releasing = true;
          canceled = index != expected;
        }
        if (now >= deadline)
          canceled = releasing = true;
        try {
          l.pages.clear();
          auto gui = l.Field(held.env, "g_gui").Table();
          auto name = l.Field(gui, "currentGuiName");
          if (name.Tag() != 0x1fffb || !l.Text(name).empty())
            canceled = releasing = true;
        } catch (...) {
          canceled = releasing = true;
        }
      }
      if (releasing)
        Release();
    }
    bool allowed = false;
    reason = active ? 20 : !Supported() ? 21 :
             !next.world_ready ? 22 : !next.time_scale ? 23 : 0;
    if (!active && Supported() && next.world_ready && next.time_scale) {
      try {
        const bool up = gap ? last_up :
                        target < 0 || target > TimeScaleIndex(*next.time_scale);
        l.pages.clear();
        auto context = Context(l, h, up, &reason);
        if (gap) {
          if (!context.key_down)
            ++released_samples;
          else
            released_samples = 0;
          if (released_samples >= 2 && now >= release_after) {
            if (context.index != expected) {
              // Native settings changed independently between our key edges.
              target = -1;
              gap = false;
            } else if (context.index == target) {
              target = -1;
              gap = false;
            } else if (target >= 0) {
              if (Start(std::move(context)))
                gap = false;
              else if (now >= deadline) {
                target = -1;
                gap = false;
              }
              // Atomic pause/GC races refuse the whole batch. Keep a bounded
              // target request and retry after another fresh validation.
            }
          }
        }
        allowed = !active && !gap && target < 0 && !context.key_down;
        if (!allowed)
          reason = context.key_down ? 11 : 20;
      } catch (...) {
        if (l.bytes >= 4194304)
          reason = 90; // Dedicated input validation page-read budget exhausted.
        if (!reason)
          reason = 99;
        if (!active) {
          target = -1;
          gap = false;
        }
      }
    } else if (!active && !next.world_ready) {
      target = -1;
      gap = false;
    }
    next.time_scale_change_allowed = allowed;
    next.time_scale_pending = active || gap || target >= 0;
    if (!allowed && reason != 20) {
      last_refusal = reason;
      refusal_bytes = l.bytes;
      ++refusal_count;
    }
    // No manifest widget binds any fs.time.* key; they exist only so tests
    // can see this detail, so they are skipped in a normal run (diagnostics
    // off by default -- see Reader::SetDiagnostics).
    if (h.publish_i64 && diagnostics_enabled) {
      h.publish_i64(h.userdata, "fs.time.reason", reason);
      h.publish_i64(h.userdata, "fs.time.last_refusal", last_refusal);
      h.publish_i64(h.userdata, "fs.time.refusals", refusal_count);
      h.publish_i64(h.userdata, "fs.time.refusal_bytes", refusal_bytes);
      h.publish_i64(h.userdata, "fs.time.reader.bytes", l.bytes);
      h.publish_i64(h.userdata, "fs.time.reader.calls", l.calls);
    }
  }
};
