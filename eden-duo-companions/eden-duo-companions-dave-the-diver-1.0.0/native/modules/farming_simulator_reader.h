// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "farming_simulator_view.h"
#include "core/mods/dsmod_module_extensions.h"
#include <chrono>
#include <memory>
namespace farming_simulator_reader {
class Reader {
public:
  Reader(const EdenDsmodHostApi &, const char *);
  ~Reader();
  // Off by default: a handful of fs.reader.*/fs.catalog.*/fs.time.* keys exist
  // only so a test can observe reader-internal detail no manifest widget
  // binds; a normal run never calls this, so they stay unpublished.
  void SetDiagnostics(bool enabled);
  // Test-only: pins the clock Sample() uses for its warm-up/last-good-data
  // staleness timers to `now` instead of the real steady_clock, so tests can
  // advance those windows deterministically without sleeping. A normal run
  // never calls this.
  void SetClockForTesting(std::chrono::steady_clock::time_point now);
  void Sample(const EdenDsmodHostApi &);
  void ConfigureWrites(const EdenDsmodHostWriteApi *);
  // The guest load plan's mailbox (farming_simulator_game_calls.h); without it every game-call
  // action is refused.
  void ConfigureCalls(const EdenDsmodHostExtensions *);
  // "garage_enter" / "garage_reset" take the vehicle's network id (VehicleItem::network_id).
  bool OnAction(const char *, int64_t);
  // Map Visit / Tag place for a marker of the snapshot the caller shows (re-checked in the game).
  bool RequestPlaceCall(bool tag, const farming_simulator::MapMarker &);
  // Call before live reload; false means the native key still needs release.
  // This only cancels the native TIME_SCALE key write pending in `impl_->time`
  // (TimeControl::Cancel) -- it does NOT drain an in-flight game call
  // (farming_simulator_game_calls.h's CallMailbox). That mailbox needs no
  // drain here: its own request state is keyed by the mailbox's seq/done
  // counters in guest memory, not by anything this reader holds, so a
  // pending call simply keeps resolving itself (CallMailbox::Poll) against
  // whatever guest state exists after the reload -- self-healing the same
  // way Poll's own taken-but-never-done recovery is, with nothing for this
  // method to cancel.
  bool CancelPending();
  const farming_simulator::Snapshot &GetSnapshot() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace farming_simulator_reader
