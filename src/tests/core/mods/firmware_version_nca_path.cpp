// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later
// Second, independent look at the GetFirmwareVersion memory report: exact heap bytes (glibc
// mallinfo2, not RSS) for the synthesized path AND the real-firmware NCA path that a device with
// readable firmware takes. Needs installed firmware + keys in the host eden data dir; skips
// otherwise.
#include <cstdlib>
#include <malloc.h>
#include <memory>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/path_util.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/system_archive/system_archive.h"
#include "core/file_sys/system_archive/system_version.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"

namespace {
constexpr u64 FirmwareVersionSystemDataId = 0x0100000000000809;

double HeapBytesPerRound(int rounds, auto&& body) {
    body(); // warm caches / one-time statics
    const auto before = mallinfo2();
    for (int i = 0; i < rounds; ++i) {
        body();
    }
    const auto after = mallinfo2();
    const double in_use = double(after.uordblks + after.hblkhd) - double(before.uordblks + before.hblkhd);
    return in_use / rounds;
}
} // namespace

TEST_CASE("firmware version: exact heap growth per synthesized GetFirmwareVersion", "[filesys][leak2]") {
    const double per_round = HeapBytesPerRound(20000, [] {
        auto romfs = FileSys::ExtractRomFS(
            FileSys::SystemArchive::SynthesizeSystemArchive(FirmwareVersionSystemDataId));
        (void)romfs->GetFile("file")->ReadAllBytes();
    });
    WARN("synthesized path: " << per_round << " heap bytes retained per call");
    CHECK(per_round < 8.0);

    auto dir_once = FileSys::SystemArchive::SystemVersion();
    const double builder_only = HeapBytesPerRound(20000, [&] {
        auto f = FileSys::CreateRomFS(dir_once);
        (void)f;
    });
    WARN("CreateRomFS only: " << builder_only << " heap bytes retained per call");
    CHECK(builder_only < 8.0);
}

TEST_CASE("firmware version: real firmware NCA path (what a device with readable firmware runs)",
          "[filesys][leak2]") {
    std::string registered = Common::FS::GetEdenPathString(Common::FS::EdenPath::NANDDir) +
                             "/system/Contents/registered";
    if (const char* override_dir = std::getenv("DUO_TEST_REGISTERED_DIR")) {
        registered = override_dir; // e.g. the firmware NCAs pulled from a device
    }
    auto vfs = std::make_shared<FileSys::RealVfsFilesystem>();
    auto dir = vfs->OpenDirectory(registered, FileSys::OpenMode::Read);
    if (dir == nullptr) {
        WARN("no installed firmware at " << registered << "; skipping");
        return;
    }
    FileSys::RegisteredCache cache(dir);
    auto nca = cache.GetEntry(FirmwareVersionSystemDataId, FileSys::ContentRecordType::Data);
    if (nca == nullptr) {
        WARN("SystemVersion NCA not indexed in " << registered << "; skipping");
        return;
    }
    auto nca_romfs = nca->GetRomFS();
    WARN("SystemVersion NCA status " << static_cast<int>(nca->GetStatus()) << ", key generation "
                                     << int(nca->GetKeyGeneration()) << ", romfs "
                                     << (nca_romfs ? "ok" : "NULL"));
    if (nca_romfs == nullptr) {
        WARN("firmware NCA unreadable with the host keys -> device falls back to synthesizing");
        return;
    }
    auto romfs = FileSys::ExtractRomFS(nca_romfs);
    REQUIRE(romfs != nullptr);
    auto data = romfs->GetFile("file")->ReadAllBytes();
    REQUIRE(data.size() == 0x100);
    WARN("firmware display title: " << std::string(reinterpret_cast<const char*>(data.data() + 0x88)));

    // Exactly what GetFirmwareVersionImpl does per call on the NCA path.
    const double per_round = HeapBytesPerRound(3000, [&] {
        auto n = cache.GetEntry(FirmwareVersionSystemDataId, FileSys::ContentRecordType::Data);
        auto r = FileSys::ExtractRomFS(n->GetRomFS());
        (void)r->GetFile("file")->ReadAllBytes();
    });
    WARN("NCA path: " << per_round << " heap bytes retained per call");
    CHECK(per_round < 8.0);
}
