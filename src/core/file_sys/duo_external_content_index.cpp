// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/file_sys/duo_external_content_index.h"

#include <algorithm>
#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "common/logging.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/loader/loader.h"

namespace FileSys::DuoExternalIndex {
namespace {

constexpr int FormatVersion = 2;

std::filesystem::path IndexPath() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "external_content_index.json";
}

bool IsTicket(const std::string& name) {
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".tik") == 0;
}

} // namespace

Index& Index::Instance() {
    static Index index;
    return index;
}

Index::Index() = default;

void Index::NoteScanStart() {
    if (!scanning) {
        scanning = true;
        scan_start = std::chrono::steady_clock::now();
        hits = 0;
        misses = 0;
    }
}

void Index::Load() {
    loaded = true;
    std::ifstream in(IndexPath());
    if (!in) {
        return;
    }
    try {
        const auto json = nlohmann::json::parse(in);
        if (json.value("format", 0) != FormatVersion) {
            return;
        }
        for (const auto& [key, value] : json.at("containers").items()) {
            Container container;
            container.size = value.at("size").get<u64>();
            for (const auto& part : value.at("parts")) {
                container.parts.push_back({part.at(0).get<std::string>(), part.at(1).get<u64>(),
                                           part.at(2).get<u64>()});
            }
            for (const auto& ticket : value.at("tickets")) {
                container.tickets.push_back(Common::HexStringToVector(ticket.get<std::string>(),
                                                                      false));
            }
            for (const auto& record : value.at("records")) {
                container.records.push_back({
                    record.at(0).get<u64>(),
                    static_cast<TitleType>(record.at(1).get<u32>()),
                    static_cast<ContentRecordType>(record.at(2).get<u32>()),
                    record.at(3).get<u32>(),
                    record.at(4).get<std::string>(),
                    record.at(5).get<std::string>(),
                });
            }
            containers.insert_or_assign(key, std::move(container));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(Service_FS, "Ignoring unreadable external content index: {}", e.what());
        containers.clear();
    }
}

bool Index::Restore(const VirtualFile& file, const AddEntry& add) {
    if (file == nullptr) {
        return false;
    }
    const auto key = file->GetFullPath();
    Container container;
    {
        std::scoped_lock lk{mutex};
        if (!loaded) {
            Load();
        }
        NoteScanStart();
        const auto it = containers.find(key);
        if (it == containers.end() || it->second.size != file->GetSize()) {
            ++misses;
            return false;
        }
        ++hits;
        container = it->second;
        seen.insert(key);
    }

    std::unordered_map<std::string, VirtualFile> files;
    for (const auto& part : container.parts) {
        if (part.offset + part.size > container.size) {
            return false;
        }
        files.emplace(part.name, std::make_shared<OffsetVfsFile>(file, part.size, part.offset,
                                                                 part.name));
    }
    for (const auto& record : container.records) {
        if (!files.contains(record.nca_name)) {
            return false;
        }
    }

    auto& keys = Core::Crypto::KeyManager::Instance();
    for (const auto& ticket : container.tickets) {
        if (!keys.AddTicket(Core::Crypto::Ticket::Read(ticket))) {
            LOG_WARNING(Common_Filesystem, "Could not load a cached NSP ticket");
        }
    }
    for (const auto& record : container.records) {
        add(record, files.at(record.nca_name));
    }
    return true;
}

void Index::Store(const VirtualFile& file, std::vector<Record> records) {
    // Without the base keys nothing decrypts, so an empty result then is not cached.
    if (file == nullptr ||
        (records.empty() && Core::Crypto::KeyManager::Instance().BaseDeriveNecessary())) {
        return;
    }
    const PartitionFilesystem pfs(file);
    if (pfs.GetStatus() != Loader::ResultStatus::Success) {
        return;
    }
    const auto offsets = pfs.GetFileOffsets();
    const auto sizes = pfs.GetFileSizes();

    Container container;
    container.size = file->GetSize();
    for (const auto& [name, offset] : offsets) {
        if (IsTicket(name)) {
            if (const auto ticket = pfs.GetFile(name); ticket != nullptr) {
                container.tickets.push_back(ticket->ReadAllBytes());
            }
            continue;
        }
        const bool needed = std::any_of(records.begin(), records.end(),
                                        [&name](const Record& record) {
                                            return record.nca_name == name;
                                        });
        if (needed) {
            container.parts.push_back({name, offset, sizes.at(name)});
        }
    }
    for (const auto& record : records) {
        if (!offsets.contains(record.nca_name)) {
            return; // the content did not come straight from this partition (nested container)
        }
    }
    container.records = std::move(records);

    std::scoped_lock lk{mutex};
    if (!loaded) {
        Load();
    }
    const auto key = file->GetFullPath();
    containers.insert_or_assign(key, std::move(container));
    seen.insert(key);
    dirty = true;
}

void Index::Flush() {
    std::scoped_lock lk{mutex};
    if (!loaded) {
        return;
    }
    for (auto it = containers.begin(); it != containers.end();) {
        if (!seen.contains(it->first)) {
            it = containers.erase(it);
            dirty = true;
        } else {
            ++it;
        }
    }
    seen.clear();
    if (scanning) {
        scanning = false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - scan_start);
        LOG_INFO(Service_FS, "External content: {} containers from the index, {} parsed, {} ms",
                 hits, misses, elapsed.count());
    }
    if (!dirty) {
        return;
    }

    nlohmann::json out_containers = nlohmann::json::object();
    for (const auto& [key, container] : containers) {
        nlohmann::json parts = nlohmann::json::array();
        for (const auto& part : container.parts) {
            parts.push_back({part.name, part.offset, part.size});
        }
        nlohmann::json out_records = nlohmann::json::array();
        for (const auto& record : container.records) {
            out_records.push_back({record.title_id, static_cast<u32>(record.title_type),
                                   static_cast<u32>(record.content_type), record.version,
                                   record.version_string, record.nca_name});
        }
        nlohmann::json tickets = nlohmann::json::array();
        for (const auto& ticket : container.tickets) {
            tickets.push_back(Common::HexToString(ticket));
        }
        out_containers[key] = {{"size", container.size},
                               {"parts", parts},
                               {"tickets", tickets},
                               {"records", out_records}};
    }

    const auto path = IndexPath();
    const auto temp = std::filesystem::path(path).concat(".tmp");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return;
        }
        out << nlohmann::json{{"format", FormatVersion}, {"containers", out_containers}}.dump();
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (!ec) {
        dirty = false;
    }
}

} // namespace FileSys::DuoExternalIndex
