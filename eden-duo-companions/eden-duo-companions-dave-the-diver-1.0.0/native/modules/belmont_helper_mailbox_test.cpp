// SPDX-License-Identifier: GPL-3.0-or-later
// Host-only unit test, no device/emulator: a fake in-memory mailbox standing in
// for the real EdenDsmodHostExtensions load_u32/load_u64/store_u32/store_u64,
// exercising belmont_helper_mailbox::Client's request/response protocol end to
// end. Build: g++ -std=c++20 -Wall -Wextra -Werror belmont_helper_mailbox_test.cpp
// -o belmont_helper_mailbox_test && ./belmont_helper_mailbox_test
#include "belmont_helper_mailbox.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {

// Mirrors the host extension contract exactly (dsmod_module_extensions.h):
// offset-addressed, bounds-checked to the mailbox, no further semantics
// assumed. The test is single-threaded, so plain memcpy stands in for the
// real implementation's acquire/release atomics -- that contract is the
// runtime's to uphold; this test is about Client's offset bookkeeping and
// request/response protocol, not cross-thread memory ordering.
class FakeMailbox {
public:
  FakeMailbox() {
    bytes_.fill(0);
  }

  bool Load32(std::uint32_t offset, std::uint32_t* out) const {
    if (offset % 4 != 0 || offset + 4 > bytes_.size()) {
      return false;
    }
    std::memcpy(out, bytes_.data() + offset, 4);
    return true;
  }
  bool Load64(std::uint32_t offset, std::uint64_t* out) const {
    if (offset % 8 != 0 || offset + 8 > bytes_.size()) {
      return false;
    }
    std::memcpy(out, bytes_.data() + offset, 8);
    return true;
  }
  bool Store32(std::uint32_t offset, std::uint32_t value) {
    if (offset % 4 != 0 || offset + 4 > bytes_.size()) {
      return false;
    }
    std::memcpy(bytes_.data() + offset, &value, 4);
    return true;
  }
  bool Store64(std::uint32_t offset, std::uint64_t value) {
    if (offset % 8 != 0 || offset + 8 > bytes_.size()) {
      return false;
    }
    std::memcpy(bytes_.data() + offset, &value, 8);
    return true;
  }

  // Test-only direct peeks/pokes, standing in for what belmont_guest_helper.S
  // would really do on the guest side.
  std::uint32_t PeekU32(std::uint32_t offset) const {
    std::uint32_t value{};
    assert(Load32(offset, &value));
    return value;
  }
  std::uint64_t PeekU64(std::uint32_t offset) const {
    std::uint64_t value{};
    assert(Load64(offset, &value));
    return value;
  }
  void PokeU32(std::uint32_t offset, std::uint32_t value) {
    assert(Store32(offset, value));
  }

private:
  std::array<std::uint8_t, belmont_helper_mailbox::MailboxSize> bytes_{};
};

auto MakeClientFor(FakeMailbox& mailbox) {
  return belmont_helper_mailbox::MakeClient(
      [&mailbox](std::uint32_t offset, std::uint32_t* out) { return mailbox.Load32(offset, out); },
      [&mailbox](std::uint32_t offset, std::uint64_t* out) { return mailbox.Load64(offset, out); },
      [&mailbox](std::uint32_t offset, std::uint32_t value) { return mailbox.Store32(offset, value); },
      [&mailbox](std::uint32_t offset, std::uint64_t value) { return mailbox.Store64(offset, value); });
}

} // namespace

int main() {
  using namespace belmont_helper_mailbox;

  // Initialize() seeds magic + epoch + both sequence numbers.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.Initialize(7));
    assert(mailbox.PeekU32(OffsetMagic) == MagicValue);
    assert(mailbox.PeekU32(EpochOffset) == 7);
    assert(mailbox.PeekU32(OffsetReqSeq) == 0);
    assert(mailbox.PeekU32(OffsetResultSeq) == 0);
    // A zero epoch is rejected in favour of a safe non-zero seed (the guest
    // treats epoch 0 at offset 0xFFC as "never initialized" and refuses).
    FakeMailbox mailbox_zero;
    auto client_zero = MakeClientFor(mailbox_zero);
    assert(client_zero.Initialize(0));
    assert(mailbox_zero.PeekU32(EpochOffset) == 1);
  }

  // SetClass writes each of the 8 class_table slots this design uses at the
  // documented offset (0x70 + slot*8), independent of the others.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.SetClass(ClassSlot::MapMarkerData, 0x1000));
    assert(client.SetClass(ClassSlot::LevelID, 0x2000));
    assert(client.SetClass(ClassSlot::MapMarkerTypeID, 0x3000));
    assert(client.SetClass(ClassSlot::SpellItemData, 0x4000));
    assert(client.SetClass(ClassSlot::WeaponItemData, 0x5000));
    assert(client.SetClass(ClassSlot::CharmItemData, 0x6000));
    assert(client.SetClass(ClassSlot::MapMarkersComponent, 0x7000));
    assert(client.SetClass(ClassSlot::Inventory, 0x8000));
    assert(mailbox.PeekU64(OffsetClassTable + 0 * 8) == 0x1000);
    assert(mailbox.PeekU64(OffsetClassTable + 1 * 8) == 0x2000);
    assert(mailbox.PeekU64(OffsetClassTable + 2 * 8) == 0x3000);
    assert(mailbox.PeekU64(OffsetClassTable + 3 * 8) == 0x4000);
    assert(mailbox.PeekU64(OffsetClassTable + 4 * 8) == 0x5000);
    assert(mailbox.PeekU64(OffsetClassTable + 5 * 8) == 0x6000);
    assert(mailbox.PeekU64(OffsetClassTable + 6 * 8) == 0x7000);
    assert(mailbox.PeekU64(OffsetClassTable + 7 * 8) == 0x8000);
  }

  // SetContext.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.SetContext(0xAAAA, 0xBBBB));
    assert(mailbox.PeekU64(OffsetCtxMarkersComponent) == 0xAAAA);
    assert(mailbox.PeekU64(OffsetCtxInventory) == 0xBBBB);
  }

  // SyncEpoch: 0 keeps the current seed; non-zero overwrites it.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.Initialize(3));
    assert(client.SyncEpoch(0));
    assert(mailbox.PeekU32(EpochOffset) == 3);
    assert(client.SyncEpoch(9));
    assert(mailbox.PeekU32(EpochOffset) == 9);
  }

  // IsAlive(): true only once the guest-written heartbeat word actually moves.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(!client.IsAlive()); // 0 -> 0, no movement yet
    mailbox.PokeU32(OffsetHeartbeat, 1); // guest ticked once
    assert(client.IsAlive());
    assert(!client.IsAlive()); // unchanged since the previous observation
    mailbox.PokeU32(OffsetHeartbeat, 2);
    assert(client.IsAlive());
  }

  // Full request/response round trip, ADD_PIN-shaped payload.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.Initialize());
    assert(!client.Busy());

    Request request;
    request.op = Op::AddPin;
    request.epoch = 1;
    request.primary_ptr = 0x1111;   // LevelID*
    request.secondary_ptr = 0x2222; // marker-type guid string*
    request.arg_a = 0x40A00000;     // x, float bits
    request.arg_b = 0x40C00000;     // y, float bits
    request.arg_c = 5;              // nav_index

    assert(client.TryStage(request));
    assert(client.Busy());
    assert(mailbox.PeekU32(OffsetReqOp) == static_cast<std::uint32_t>(Op::AddPin));
    assert(mailbox.PeekU64(OffsetReqPrimaryPtr) == 0x1111);
    assert(mailbox.PeekU64(OffsetReqSecondaryPtr) == 0x2222);
    assert(mailbox.PeekU32(OffsetReqArgA) == 0x40A00000);
    assert(mailbox.PeekU32(OffsetReqArgC) == 5);
    const std::uint32_t staged_seq = mailbox.PeekU32(OffsetReqSeq);
    assert(staged_seq == 1); // first stage after a fresh Initialize()

    // Not answered yet: Poll() must not claim an outcome.
    auto outcome = client.Poll();
    assert(!outcome.answered);
    assert(client.Busy());

    // A second TryStage must be refused while the first is still in flight
    // (H6: the helper calls at most one native mutator per tick).
    Request second = request;
    second.arg_c = 9;
    assert(!client.TryStage(second));
    assert(mailbox.PeekU32(OffsetReqArgC) == 5); // untouched: the refused stage did not write

    // Simulate the guest helper acknowledging the request: result_code/value
    // first, result_seq (release) last -- the same order belmont_guest_helper.S
    // uses in acknowledge_common.
    mailbox.PokeU32(OffsetResultCode, static_cast<std::uint32_t>(Result::Ok));
    assert(mailbox.Store64(OffsetResultValue, 0x2A));
    mailbox.PokeU32(OffsetResultSeq, staged_seq);

    outcome = client.Poll();
    assert(outcome.answered);
    assert(outcome.code == Result::Ok);
    assert(outcome.value == 0x2A);
    assert(!client.Busy());

    // Now that it is free, a new stage must get the NEXT sequence number, not
    // seq 0 and not a repeat of the first.
    assert(client.TryStage(second));
    assert(mailbox.PeekU32(OffsetReqSeq) == staged_seq + 1);
  }

  // A refused outcome is reported through Outcome::code, not as answered=false.
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.Initialize());
    Request request;
    request.op = Op::EquipCharm;
    request.primary_ptr = 0xCAFE;
    request.arg_a = static_cast<std::uint32_t>(-1);
    assert(client.TryStage(request));
    const std::uint32_t seq = mailbox.PeekU32(OffsetReqSeq);
    mailbox.PokeU32(OffsetResultCode, static_cast<std::uint32_t>(Result::NotOwned));
    mailbox.Store64(OffsetResultValue, 0);
    mailbox.PokeU32(OffsetResultSeq, seq);
    const auto outcome = client.Poll();
    assert(outcome.answered);
    assert(outcome.code == Result::NotOwned);
  }

  // Staging Op::None is always refused (there is nothing for the guest to do).
  {
    FakeMailbox mailbox;
    auto client = MakeClientFor(mailbox);
    assert(client.Initialize());
    Request request;
    assert(request.op == Op::None);
    assert(!client.TryStage(request));
    assert(!client.Busy());
  }

  std::cout << "belmont helper mailbox: layout offsets, stage/poll round trip, "
               "busy-gating and refusal reporting PASS\n";
  return 0;
}
