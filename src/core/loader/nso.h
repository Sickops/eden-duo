// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/patch_manager.h"
#include "core/loader/loader.h"

namespace Core {
class System;
}

namespace Core::NCE {
class Patcher;
}

namespace Kernel {
class KProcess;
}

namespace Loader {

struct NSOSegmentHeader {
    u32_le offset;
    u32_le location;
    u32_le size;
    union {
        u32_le alignment;
        u32_le bss_size;
    };
};
static_assert(sizeof(NSOSegmentHeader) == 0x10, "NsoSegmentHeader has incorrect size.");

struct NSOHeader {
    using SHA256Hash = std::array<u8, 0x20>;

    struct RODataRelativeExtent {
        u32_le data_offset;
        u32_le size;
    };

    u32_le magic;
    u32_le version;
    u32 reserved;
    u32_le flags;
    std::array<NSOSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    std::array<u8, 0x20> build_id;
    std::array<u32_le, 3> segments_compressed_size;
    std::array<u8, 0x1C> padding;
    RODataRelativeExtent api_info_extent;
    RODataRelativeExtent dynstr_extent;
    RODataRelativeExtent dynsyn_extent;
    std::array<SHA256Hash, 3> segment_hashes;

    bool IsSegmentCompressed(size_t segment_num) const;
    bool IsZBICCompressed() const {
        return ((flags >> 7) & 1) != 0;
    }
};
static_assert(sizeof(NSOHeader) == 0x100, "NSOHeader has incorrect size.");
static_assert(std::is_trivially_copyable_v<NSOHeader>, "NSOHeader must be trivially copyable.");

constexpr u32 NSO_ARGUMENT_DATA_ALLOCATION_SIZE = 0x9000;

struct NSOArgumentHeader {
    u32_le allocated_size;
    u32_le actual_size;
    INSERT_PADDING_BYTES(0x18);
};
static_assert(sizeof(NSOArgumentHeader) == 0x20, "NSOArgumentHeader has incorrect size.");

/// Loads an NSO file
class AppLoader_NSO final : public AppLoader {
public:
    explicit AppLoader_NSO(FileSys::VirtualFile file_);

    /**
     * Identifies whether or not the given file is a form of NSO file.
     *
     * @param in_file The file to be identified.
     *
     * @return FileType::NSO if found, or FileType::Error if some other type of file.
     */
    static FileType IdentifyType(const FileSys::VirtualFile& in_file);

    FileType GetFileType() const override {
        return IdentifyType(file);
    }

    // Where a dual-screen load plan's mailbox lives when the title has several modules: after the
    // LAST module, so no module moves. Mods such as Luminescent Platinum branch directly from main
    // into a later module (subsdk) and break if anything is inserted between modules (after main,
    // or after rtld, which also stops the game during start-up).
    struct DualScreenMailboxSlot {
        u64 size = 0;          // bytes to reserve (the plan's mailbox_size), 0 = no plan
        std::string host;      // the module that carries it (the last one loaded)
        VAddr layout_end = 0;  // where it starts, relative to the host's own load base (layout
                               // pass, NCE pre-text space excluded)
        VAddr address = 0;     // its address in the process, set before the load pass
    };

    // The module the selected dual-screen package's load plan targets ("main" unless the manifest
    // names another), nullopt when no package has a load plan. Only that module needs the title
    // ID passed to LoadModule, so the multi-MiB manifest is not parsed once per module.
    static std::optional<std::string> LoadPlanTargetModule(Core::System& system, u64 title_id);

    // The mailbox size of the load plan that applies to this NSO, 0 when there is none (an
    // invalid plan is reported when the module itself loads).
    static u64 PeekLoadPlanMailboxSize(Core::System& system, const FileSys::VfsFile& nso_file,
                                       u64 title_id);

    static std::optional<VAddr> LoadModule(Kernel::KProcess& process, Core::System& system,
                                           const FileSys::VfsFile& nso_file, VAddr load_base,
                                           bool should_pass_arguments, bool load_into_process,
                                           std::optional<FileSys::PatchManager> pm = {},
                                           std::vector<Core::NCE::Patcher>* patches = nullptr,
                                           s32 patch_index = -1,
                                           std::optional<u64> title_id = std::nullopt,
                                           DualScreenMailboxSlot* mailbox_slot = nullptr);

    LoadResult Load(Kernel::KProcess& process, Core::System& system) override;

    ResultStatus ReadNSOModules(Modules& out_modules) override;

private:
    Modules modules;
};

} // namespace Loader
