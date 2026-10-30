// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
#include "yoru_reader.h"
#include "core/mods/modules/dsmod_module_sdk.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace yoru {
namespace {
using dsmod_sdk::Le32;
using dsmod_sdk::Le64;
bool Utf32(const uint8_t *bytes, size_t size, std::string &output) {
  output.clear();
  for (size_t i = 0; i + 4 <= size; i += 4) {
    const uint32_t c = Le32(bytes + i);
    if (c == 0)
      return true;
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff) ||
        (c < 0x20 && c != '\n' && c != '\t' && c != '\r'))
      return false;
    if (c < 0x80)
      output.push_back(char(c));
    else if (c < 0x800) {
      output.push_back(char(0xc0 | (c >> 6)));
      output.push_back(char(0x80 | (c & 63)));
    } else if (c < 0x10000) {
      output.push_back(char(0xe0 | (c >> 12)));
      output.push_back(char(0x80 | ((c >> 6) & 63)));
      output.push_back(char(0x80 | (c & 63)));
    } else {
      output.push_back(char(0xf0 | (c >> 18)));
      output.push_back(char(0x80 | ((c >> 12) & 63)));
      output.push_back(char(0x80 | ((c >> 6) & 63)));
      output.push_back(char(0x80 | (c & 63)));
    }
  }
  return false; // Fixed buffers must contain a terminator.
}
} // namespace
bool SupportsBuild(std::string_view build) {
  if (build.size() != BuildId.size())
    return false;
  for (size_t i = 0; i < build.size(); ++i) {
    char c = build[i];
    if (c >= 'a' && c <= 'f')
      c = char(c - 'a' + 'A');
    if (c != BuildId[i])
      return false;
  }
  return true;
}
Snapshot ReadSnapshot(const ReadFn &read, uint64_t base, bool build_matches) {
  Snapshot out;
  if (!build_matches || !read || !base ||
      base > std::numeric_limits<uint64_t>::max() - 0x20c000)
    return out;
  std::array<uint8_t, 64> identity{};
  if (!read(base, identity.data(), identity.size()) || identity != MainIdentity)
    return out;
  out.compatible = true;
  auto missing = [&] {
    Snapshot blank;
    blank.compatible = out.compatible;
    return blank;
  };
  std::array<uint8_t, 8> root{};
  if (!read(base + RootOffset, root.data(), root.size()))
    return out;
  uint64_t node = Le64(root.data());
  std::array<uint64_t, 8> seen{};
  std::array<std::array<uint8_t, 0x30>, 8> links{};
  size_t visited = 0;
  std::array<uint8_t, 0x30> scene{};
  for (size_t i = 0; node && i < seen.size(); ++i) {
    if (node % 8 || node > std::numeric_limits<uint64_t>::max() - 0x23800 ||
        std::find(seen.begin(), seen.begin() + i, node) != seen.begin() + i ||
        !read(node, scene.data(), scene.size()))
      return missing();
    seen[i] = node;
    links[i] = scene;
    visited = i + 1;
    if (Le64(scene.data() + 8) != base + ManagerOffset)
      return missing();
    if (Le64(scene.data()) == base + StoryVtableOffset) {
      out.story = node;
      break;
    }
    node = Le64(scene.data() + 0x28);
  }
  if (!out.story)
    return out; // Title/loading/unknown scene does not grant CG readiness.
  std::array<uint8_t, 12> before{}, after{};
  std::array<uint8_t, RingSlots * RecordSize> ring{}, again{};
  std::array<uint8_t, 53> flags{}, flags_again{};
  std::array<uint8_t, 4> mode{};
  if (!read(out.story + HeaderOffset, before.data(), before.size()))
    return missing();
  int32_t head = int32_t(Le32(before.data())),
          count = int32_t(Le32(before.data() + 8));
  if (head < 0 || head >= int32_t(RingSlots) || count < 0 ||
      count > int32_t(MaxRows))
    return missing();
  if (!read(out.story + RingOffset, ring.data(), ring.size()) ||
      !read(base + FlagsOffset, flags.data(), flags.size()) ||
      !read(out.story + ModeOffset, mode.data(), mode.size()) ||
      !read(out.story + RingOffset, again.data(), again.size()) ||
      ring != again ||
      !read(base + FlagsOffset, flags_again.data(), flags_again.size()) ||
      flags != flags_again ||
      !read(out.story + HeaderOffset, after.data(), after.size()) ||
      before != after || !read(out.story, scene.data(), scene.size()) ||
      Le64(scene.data()) != base + StoryVtableOffset ||
      Le64(scene.data() + 8) != base + ManagerOffset)
    return missing();
  std::array<uint8_t, 8> root_after{};
  if (!read(base + RootOffset, root_after.data(), root_after.size()) ||
      root != root_after)
    return missing();
  // A scene may be removed through an intermediate link while the manager
  // root remains unchanged. Revalidate the bounded route, not only its ends.
  for (size_t i = 0; i < visited; ++i) {
    if (!read(seen[i], scene.data(), scene.size()) ||
        Le64(scene.data()) != Le64(links[i].data()) ||
        Le64(scene.data() + 8) != Le64(links[i].data() + 8) ||
        Le64(scene.data() + 0x28) != Le64(links[i].data() + 0x28))
      return missing();
  }
  out.head = head;
  out.selected = int32_t(Le32(before.data() + 4));
  out.native_mode = int32_t(Le32(mode.data()));
  const int32_t start =
      (head - count + int32_t(RingSlots)) % int32_t(RingSlots);
  for (int32_t i = 0; i < count; ++i) {
    const uint32_t slot = uint32_t((start + i) % int32_t(RingSlots));
    const auto *bytes = ring.data() + slot * RecordSize;
    Row row;
    row.physical_slot = slot;
    row.voice = int32_t(Le32(bytes + 0x280));
    if (!Utf32(bytes, 0x200, row.text) || row.text.empty() ||
        !Utf32(bytes + 0x200, 0x80, row.speaker))
      return missing();
    out.rows.push_back(std::move(row));
  }
  std::array<uint8_t, 4> text_total{};
  if (read(out.story + TextTotalOffset, text_total.data(), text_total.size()))
    out.text_visible = Le32(text_total.data()) != 0;
  out.ready = true;
  // A newly constructed scene can be linked before save/global loading
  // finishes. One committed native utterance is the currently verified
  // loaded-state witness.
  out.cg_ready = count > 0;
  if (out.cg_ready)
    out.cg_flags = flags;
  return out;
}
bool IsContinuation(const Snapshot &previous, const Snapshot &next) {
  if (!previous.ready || !next.ready || previous.story != next.story)
    return false;
  const size_t delta = size_t((next.head - previous.head + 64) % 64);
  if (std::min(MaxRows, previous.rows.size() + delta) != next.rows.size())
    return false;
  const size_t overlap =
      std::min(previous.rows.size(),
               next.rows.size() - std::min(delta, next.rows.size()));
  for (size_t i = 0; i < overlap; ++i) {
    if (previous.rows[previous.rows.size() - overlap + i] != next.rows[i])
      return false;
  }
  return delta != 0 || previous.rows == next.rows;
}
void Reader::Sample(const EdenDsmodHostApi &host) {
  const bool build =
      host.title_id == TitleId &&
      std::equal(BuildBytes.begin(), BuildBytes.end(), host.build_id);
  ReadFn read = [&](uint64_t address, void *bytes, size_t size) {
    return dsmod_sdk::ReadGuest<dsmod_sdk::MissingIsMapped::Reject>(
        host, address, bytes, size);
  };
  auto next = ReadSnapshot(read, host.main_base, build);
  if ((next.ready && !IsContinuation(snapshot_, next)) ||
      (snapshot_.ready && !next.ready))
    ++epoch_;
  next.epoch = epoch_;
  // An empty text window counts only after two consecutive samples agree, so a read taken in
  // the middle of the engine's line switch never flashes the current line into the Log.
  const bool raw_visible = next.text_visible;
  if (next.ready && !raw_visible && last_raw_text_visible_)
    next.text_visible = true;
  last_raw_text_visible_ = !next.ready || raw_visible;
  snapshot_ = std::move(next);
}
} // namespace yoru
