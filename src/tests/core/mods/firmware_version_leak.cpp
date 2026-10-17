// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Does the SystemVersion archive that set:sys GetFirmwareVersion synthesizes and extracts on every
// call get freed again? (User report: a game polling GetFirmwareVersion at kHz rates ran the
// process out of memory.)
#include <fstream>
#include <memory>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/vfs/vfs.h"

namespace {
long RssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            return std::stol(line.substr(6));
        }
    }
    return -1;
}
constexpr u64 FirmwareVersionSystemDataId = 0x0100000000000809;
} // namespace

TEST_CASE("firmware version: the extracted SystemVersion tree is released when dropped",
          "[filesys][leak]") {
    std::weak_ptr<FileSys::VfsDirectory> watch;
    std::weak_ptr<FileSys::VfsFile> watch_file;
    {
        auto romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId));
        REQUIRE(romfs != nullptr);
        auto file = romfs->GetFile("file");
        REQUIRE(file != nullptr);
        REQUIRE(file->GetSize() == 0x100);
        watch = romfs;
        watch_file = file;
    }
    CHECK(watch.expired());      // false = the directory tree keeps itself alive (shared_ptr cycle)
    CHECK(watch_file.expired()); // the 256-byte version file inside it
}

TEST_CASE("firmware version: 50k synthesize+extract rounds, RSS growth", "[filesys][leak]") {
    const long before = RssKb();
    for (int i = 0; i < 50000; ++i) {
        auto romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId));
        auto file = romfs->GetFile("file");
        (void)file->ReadAllBytes();
    }
    const long after = RssKb();
    WARN("RSS before " << before << " kB, after " << after << " kB, growth " << (after - before)
                       << " kB for 50000 rounds = " << ((after - before) * 1024.0 / 50000.0)
                       << " bytes/round");
    CHECK(after - before < 2048); // a leak-free loop grows by (almost) nothing
}

#include "common/logging.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/file_sys/system_archive/system_version.h"

TEST_CASE("firmware version: isolate the growth", "[filesys][leak][isolate]") {
    const auto measure = [](const char* what, auto&& body) {
        const long before = RssKb();
        for (int i = 0; i < 50000; ++i) {
            body();
        }
        const long after = RssKb();
        WARN(what << ": " << ((after - before) * 1024.0 / 50000.0) << " bytes/round (" << before
                  << " -> " << after << " kB)");
    };
    measure("synthesize only (logs 2 lines)", [] {
        auto f = FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId);
        (void)f;
    });
    auto once = FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId);
    measure("extract only (no logs)", [&] {
        auto d = FileSys::ExtractRomFS(once);
        (void)d->GetFile("file")->ReadAllBytes();
    });
    measure("3 LOG_INFO lines only", [] {
        LOG_INFO(Service_FS, "Synthesizing system archive '{}' ({:#016x}).", "SystemVersion", 0x809);
        LOG_INFO(Service_FS, "    - System archive generation successful!");
        LOG_WARNING(Common_Filesystem, "called, using hardcoded firmware version: {}, {}", "x", "y");
    });
    measure("supplier only: SystemVersion() dir", [] {
        auto d = FileSys::SystemArchive::SystemVersion();
        (void)d;
    });
    auto dir_once = FileSys::SystemArchive::SystemVersion();
    measure("CreateRomFS only on a fixed dir", [&] {
        auto f = FileSys::CreateRomFS(dir_once);
        (void)f;
    });
    measure("nothing", [] {});
}
