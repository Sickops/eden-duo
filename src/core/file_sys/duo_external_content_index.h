// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace FileSys::DuoExternalIndex {

/// One content entry of an external update/DLC container, as ExternalContentProvider records it.
struct Record {
    u64 title_id{};
    TitleType title_type{};
    ContentRecordType content_type{};
    u32 version{};
    std::string version_string;
    std::string nca_name;
};

using AddEntry = std::function<void(const Record&, const VirtualFile&)>;

/**
 * Eden Duo: a persistent index of parsed external content containers (NSP), so a launch does not
 * decrypt and parse hundreds of update/DLC packages again. A container is keyed by its full path
 * and size; the index keeps each content file's offset and size inside it and its ticket files,
 * so a hit only reads the tickets. Stored in the cache directory; delete the file to rebuild.
 */
class Index {
public:
    static Index& Instance();

    /// Replays a cached, unchanged container through `add` (and loads its tickets).
    bool Restore(const VirtualFile& file, const AddEntry& add);
    /// Records a freshly parsed container. `records` must come from its content entries.
    void Store(const VirtualFile& file, std::vector<Record> records);
    /// Drops containers not seen since the last flush and writes the index when it changed.
    void Flush();

private:
    struct Part {
        std::string name;
        u64 offset{};
        u64 size{};
    };
    struct Container {
        u64 size{};
        std::vector<Part> parts; // content files and tickets inside the container
        std::vector<Record> records;
    };

    Index();
    void Load();
    void NoteScanStart();

    std::mutex mutex;
    bool loaded = false;
    bool dirty = false;
    std::unordered_map<std::string, Container> containers;
    std::set<std::string> seen;
    std::chrono::steady_clock::time_point scan_start{};
    bool scanning = false;
    u32 hits = 0;
    u32 misses = 0;
};

} // namespace FileSys::DuoExternalIndex
