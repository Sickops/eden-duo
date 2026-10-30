// SPDX-License-Identifier: GPL-3.0-or-later
#include "belmont_objective_labels.h"
#include <iostream>
#include <stdexcept>
void Check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Pass the source RomFS directory");
    auto directory = ce_unity::MakeDirectoryRangeReader(argv[1]);
    unsigned reads{};
    bool available = true;
    ce_unity::RangeReader source{
        [&](std::string_view p) -> std::optional<std::uint64_t> {
          ++reads;
          return available ? directory.size(p) : std::nullopt;
        },
        [&](std::string_view p, std::uint64_t o, std::uint8_t *b,
            std::size_t n) {
          ++reads;
          return available && directory.read(p, o, b, n);
        }};
    belmont_objectives::EnglishReader reader(source);
    belmont_objectives::Snapshot s;
    s.supported = s.coherent = s.ready = true;
    s.objective_guid = "native-guid";
    s.description.table_type = 2;
    s.description.table_name = "UI_MainObjectives";
    s.description.entry_type = 2;
    s.description.entry_id = 1452204884;
    auto denied = s;
    denied.supported = false;
    Check(!reader.Load(denied) && !reads, "unsupported does no source reads");
    denied = s;
    denied.ready = false;
    Check(!reader.Load(denied) && !reads, "unavailable does no source reads");
    denied = s;
    denied.description.table_name = "Unknown";
    Check(!reader.Load(denied) && !reads,
          "unknown collection does no source reads");
    available = false;
    Check(!reader.Load(s), "missing source fails closed");
    available = true;
    constexpr std::int64_t ids[] = {1452204884, 2551800967, 3921853089,
                                    3493127998, 3791934709, 3233296097,
                                    2375967214, 4194778030, 401220315};
    for (auto id : ids) {
      s.description.entry_id = id;
      auto text = reader.Load(s);
      Check(text && !text->empty(), "native main objective resolves");
      std::cout << id << '\t' << *text << '\n';
    }
    auto before = reads;
    Check(reader.Load(s).has_value() && reads == before,
          "repeated resolution has no source reads");
    s.description.entry_id = 999999999;
    Check(!reader.Load(s), "unknown entry fails closed");
    s.description.entry_type = 1;
    s.description.entry_key = "MainObjectives_FirstArrival";
    auto byname = reader.Load(s);
    Check(byname.has_value(), "native key resolution works");
    s.description.table_type = 1;
    // System.Guid layout for the source collection GUID.
    s.description.table_guid = {0x09, 0xcc, 0x45, 0xf3, 0x21, 0x50, 0x94, 0xa2,
                                0x98, 0x7c, 0x6f, 0xfd, 0xf0, 0x46, 0x72, 0x6b};
    Check(reader.Load(s) == byname,
          "managed GUID byte order resolves exact collection");
    s.description.table_guid[0] ^= 1;
    Check(!reader.Load(s), "foreign GUID fails closed");
    reader.ClearCache();
    s.description.table_guid[0] ^= 1;
    before = reads;
    Check(reader.Load(s) == byname && reads > before,
          "explicit clearing rereads native source");
    std::cout << "Belmont objective source label tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
