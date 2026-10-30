// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_equipment_assets.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(int argc, char **argv) {
  using namespace belmont_equipment_assets;
  std::size_t reads{};
  ce_unity::RangeReader absent;
  absent.size = [&](std::string_view) -> std::optional<std::uint64_t> {
    ++reads;
    return {};
  };
  absent.read = [&](std::string_view, std::uint64_t, std::uint8_t *,
                    std::size_t) {
    ++reads;
    return false;
  };
  Reader empty(absent);
  assert(!empty.ResolveEnglish(Type::Weapon, ""));
  assert(!empty.ResolveEnglish(Type::Weapon, "../WeaponOneHandedTier1"));
  assert(!empty.ResolveEnglish(Type::Weapon, std::string(129, 'A')));
  assert(!empty.ResolveEnglish(static_cast<Type>(99), "WeaponOneHandedTier1"));
  assert(reads == 0);
  assert(!empty.ResolveEnglish(Type::Weapon, "WeaponOneHandedTier1"));
  empty.ClearCache();
  if (argc == 2) {
    auto source = ce_unity::MakeDirectoryRangeReader(argv[1]);
    Reader native(source);
    assert(!native.ResolveEnglish(Type::Charm, "WeaponOneHandedTier1"));
    assert(!native.ResolveEnglish(Type::Weapon, "UnregisteredCurrentItem"));
    // A malformed installed catalog cannot produce a name/icon or a last-good
    // item. The original input files are never changed by these tests.
    auto malformed = source;
    constexpr std::string_view catalog = "Data/StreamingAssets/aa/catalog.json";
    const std::string payload =
        "{\"m_InternalIds\":[],\"m_KeyDataString\":\"!\",\"m_"
        "BucketDataString\":\"!\",\"m_EntryDataString\":\"!\"}";
    malformed.size =
        [source, &payload](std::string_view p) -> std::optional<std::uint64_t> {
      if (p == catalog)
        return payload.size();
      return source.size(p);
    };
    malformed.read = [source, &payload](std::string_view p, std::uint64_t off,
                                        std::uint8_t *out, std::size_t n) {
      if (p != catalog)
        return source.read(p, off, out, n);
      if (off > payload.size() || n > payload.size() - off)
        return false;
      std::memcpy(out, payload.data() + off, n);
      return true;
    };
    Reader bad_catalog(malformed);
    assert(!bad_catalog.ResolveEnglish(Type::Weapon, "WeaponOneHandedTier1"));
    bad_catalog.ClearCache();
    assert(!bad_catalog.ResolveEnglish(Type::Spell, "CursedFireBall"));
    auto short_source = source;
    short_source.read = [](std::string_view, std::uint64_t, std::uint8_t *,
                           std::size_t) { return false; };
    Reader truncated(short_source);
    assert(!truncated.ResolveEnglish(Type::Weapon, "WeaponOneHandedTier1"));
  }
  std::cout << "equipment assets invalid ID, missing source, wrong type, "
               "malformed catalog and read-failure tests PASS\n";
}
