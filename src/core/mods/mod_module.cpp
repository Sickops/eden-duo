// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/mod_module.h"
#include "core/mods/mod_package_io.h"

namespace Core::Mods {
namespace {
std::mutex cache_mutex;
std::filesystem::path module_cache;

std::string_view Platform() {
#if defined(__ANDROID__) && defined(__aarch64__)
    return "android-arm64-v8a";
#elif defined(__ANDROID__) && defined(__x86_64__)
    return "android-x86_64";
#elif defined(_WIN32) && defined(_M_X64)
    return "windows-x86_64";
#elif defined(_WIN32) && defined(_M_ARM64)
    return "windows-arm64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux-aarch64";
#else
    return {};
#endif
}

/// The suffix a package uses for this platform's module image: shared objects (.so) on Linux and
/// Android, PE DLLs (.dll) on Windows. Windows packages may also carry the shared packaging
/// tools' ".so" spelling; the loader reads the image header, not the suffix, so both load.
constexpr std::string_view ModuleExtension() {
#ifdef _WIN32
    return ".dll";
#else
    return ".so";
#endif
}

/// Platform-specific detail for a failed library load, for the log and the error message.
std::string LoaderDetail() {
#ifdef _WIN32
    return fmt::format("Windows error {}", static_cast<unsigned long>(GetLastError()));
#else
    const char* detail = dlerror();
    return detail != nullptr ? std::string{detail} : std::string{"unknown loader error"};
#endif
}

bool IsHex(std::string_view value, size_t length) {
    return value.size() == length &&
           std::ranges::all_of(value, [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string Upper(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

/// One optional extension handshake (dsmod_module_extensions.h): a module that does not export
/// `symbol` offers no such extension (nullptr). Otherwise the getter is called with the host's
/// version and hash, and the returned table must echo both, have the host's sizeof() as
/// struct_size and a non-null `required` callback; anything else throws `error`. A null table is
/// "not offered" unless `null_is_error`.
template <typename Ext, typename GetFn, typename Member>
const Ext* NegotiateExtension(const Common::DynamicLibrary& library, const char* symbol,
                              u32 version, u64 hash, Member Ext::* required, bool null_is_error,
                              const char* error) {
    GetFn get{};
    if (!library.GetSymbol(symbol, &get)) {
        return nullptr;
    }
    const Ext* ext = get(version, hash);
    if (!ext) {
        if (null_is_error) {
            throw std::runtime_error(error);
        }
        return nullptr;
    }
    if (ext->version != version || ext->struct_size != sizeof(Ext) || ext->abi_hash != hash ||
        !(ext->*required)) {
        throw std::runtime_error(error);
    }
    return ext;
}

[[maybe_unused]] std::filesystem::path CacheDirectory() {
    std::scoped_lock lock{cache_mutex};
#ifdef __ANDROID__
    // External app storage is not an executable-code location. Do not silently fall back to it.
    if (module_cache.empty()) {
        throw std::runtime_error("private dual-screen code directory is not initialized");
    }
#endif
    return module_cache.empty()
               ? Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "dualscreen"
               : module_cache;
}

std::vector<u8> ReadBounded(const FileSys::VirtualFile& file, size_t limit) {
    auto bytes = ReadPackageBytes(file, limit);
    if (!bytes) {
        throw std::runtime_error("missing, oversized or incomplete package file");
    }
    return std::move(*bytes);
}

std::string Digest(const std::vector<u8>& bytes) {
    std::array<unsigned char, SHA256_DIGEST_LENGTH> hash{};
    SHA256(bytes.data(), bytes.size(), hash.data());
    std::string result;
    result.reserve(hash.size() * 2);
    for (const auto byte : hash) {
        result += fmt::format("{:02x}", byte);
    }
    return result;
}

void StageModule(const std::vector<u8>& bytes, const std::string& title,
                 std::filesystem::path& directory) {
#ifndef _WIN32
    const auto root = CacheDirectory();
    std::filesystem::create_directories(root);
    std::string temporary = (root / "module-XXXXXX").string();
    if (mkdtemp(temporary.data()) == nullptr) {
        throw std::runtime_error("cannot create private module staging directory");
    }
    directory = temporary;
    const auto filename = directory / (title + ".so");
    // The directory is private (0700), and the module is read-only from creation. The open
    // descriptor can finish writing it; no writable path is ever passed to the dynamic loader.
    const int fd =
        open(filename.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR);
    if (fd < 0) {
        throw std::runtime_error("cannot stage dual-screen module");
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = write(fd, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            close(fd);
            throw std::runtime_error("incomplete dual-screen module staging write");
        }
        offset += static_cast<size_t>(written);
    }
    if (close(fd) != 0) {
        throw std::runtime_error("cannot finish dual-screen module staging");
    }
#else
    // Windows has no mkdtemp/O_NOFOLLOW equivalent for this, but it does not need them: the image
    // is copied out of the package into a fresh directory of the private cache, so an in-place
    // package update can never swap the file under a running module. LoadLibrary maps it from
    // there; the directory is removed again once the library is unloaded.
    const auto root = CacheDirectory();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        throw std::runtime_error("cannot create the dual-screen module cache directory");
    }
    std::mt19937_64 random{std::random_device{}()};
    for (int attempt = 0; attempt < 32 && directory.empty(); ++attempt) {
        const auto candidate = root / fmt::format("module-{:016x}", random());
        error.clear();
        if (std::filesystem::create_directory(candidate, error) && !error) {
            directory = candidate;
        }
    }
    if (directory.empty()) {
        throw std::runtime_error("cannot create private module staging directory");
    }
    const auto filename = directory / (title + std::string{ModuleExtension()});
    std::ofstream out{filename, std::ios::binary | std::ios::trunc};
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("cannot stage dual-screen module");
    }
#endif
}
} // namespace

void SetModuleCacheDirectory(const std::filesystem::path& directory) {
    std::scoped_lock lock{cache_mutex};
    module_cache = directory;
    // A staging directory is removed when its module is unloaded, so any left here belong to a
    // process that crashed or was killed. Sweep those; a recent one may still be between staging
    // and dlopen in another process, so only directories older than a few minutes go.
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
        return;
    }
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::minutes{10};
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const auto name = entry.path().filename().string();
        std::error_code entry_error;
        if (!name.starts_with("module-") || !entry.is_directory(entry_error) ||
            entry.last_write_time(entry_error) > cutoff || entry_error) {
            continue;
        }
        std::filesystem::remove_all(entry.path(), entry_error);
    }
}

GameModule::~GameModule() {
    // The runtime destroys the module instance before releasing this library handle.
    library.Close();
    if (!staging_directory.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(staging_directory, ignored);
    }
}

std::unique_ptr<GameModule> GameModule::Load(FileSys::VirtualDir assets, u64 title_id,
                                             std::string_view build_id, std::string& error) {
    error.clear();
    if (!assets) {
        return nullptr;
    }
    try {
        const auto raw = ReadBounded(assets->GetFile("manifest.json"), MaxPackageMetadataBytes);
        const auto manifest = nlohmann::json::parse(raw);
        if (!manifest.contains("module")) {
            if (manifest.value("requires_module", false)) {
                throw std::runtime_error(
                    "this dual-screen package is missing its required reader module");
            }
            return nullptr;
        }
        const auto title = fmt::format("{:016X}", title_id);
        const auto declared_title = manifest.at("title_id").get<std::string>();
        if (!IsHex(declared_title, 16) || Upper(declared_title) != title) {
            throw std::runtime_error("module title ID does not match the running game");
        }
        const auto& module = manifest.at("module");
        if (module.at("abi").get<u32>() != EDEN_DSMOD_MODULE_ABI_VERSION) {
            throw std::runtime_error("unsupported dual-screen module ABI; update the package");
        }
        const std::string build = Upper(std::string{build_id});
        if (!IsHex(build, build.size()) || build.size() < 16 || build.size() > 64) {
            throw std::runtime_error("invalid running game build ID");
        }
        const auto builds = module.at("build_ids").get<std::vector<std::string>>();
        if (builds.empty() || builds.size() > 256 ||
            !std::ranges::all_of(builds,
                                 [](const auto& b) {
                                     return (b.size() == 16 || b.size() == 64) &&
                                            IsHex(b, b.size());
                                 }) ||
            !std::ranges::any_of(builds,
                                 [&](const auto& b) { return build.starts_with(Upper(b)); })) {
            throw std::runtime_error("dual-screen module does not support this game build");
        }
        const auto platform = Platform();
        if (platform.empty()) {
            throw std::runtime_error("native dual-screen modules are unsupported on this platform");
        }
        const auto& libraries = module.at("libraries");
        if (!libraries.contains(platform)) {
            throw std::runtime_error(fmt::format("package has no {} module", platform));
        }
        const auto& entry = libraries.at(platform);
        const auto declared_path = entry.at("path").get<std::string>();
        const auto canonical_path =
            fmt::format("modules/{}/{}{}", platform, title, ModuleExtension());
        bool path_ok = declared_path == canonical_path;
#ifdef _WIN32
        // Packaging tools shared with the Linux/Android builds always name the image ".so", so a
        // Windows package may keep that spelling for the same DLL.
        path_ok = path_ok || declared_path == fmt::format("modules/{}/{}.so", platform, title);
#endif
        if (!path_ok) {
            throw std::runtime_error("invalid title-ID module filename");
        }
        const auto expected_hash = entry.at("sha256").get<std::string>();
        const auto bytes = ReadBounded(assets->GetFileRelative(declared_path), MaxNativeModuleBytes);
        if (!IsHex(expected_hash, 64) || Upper(Digest(bytes)) != Upper(expected_hash)) {
            throw std::runtime_error("dual-screen module checksum mismatch; reinstall the package");
        }
        auto loaded = std::unique_ptr<GameModule>{new GameModule};
        StageModule(bytes, title, loaded->staging_directory);
        const auto filename = loaded->staging_directory / (title + std::string{ModuleExtension()});
        if (!loaded->library.Open(filename)) {
            const auto detail = LoaderDetail();
            LOG_ERROR(Core, "DSMod module loader: {}", detail);
            throw std::runtime_error(
                fmt::format("cannot load dual-screen module for this device ({})", detail));
        }
        EdenDsmodGetModuleFn entrypoint{};
        if (!loaded->library.GetSymbol("eden_dsmod_get_module", &entrypoint)) {
            throw std::runtime_error("missing dual-screen module entry point");
        }
        loaded->api = entrypoint(EDEN_DSMOD_MODULE_ABI_VERSION, EDEN_DSMOD_MODULE_ABI_HASH);
        const auto* api = loaded->api;
        if (!api || api->abi_version != EDEN_DSMOD_MODULE_ABI_VERSION ||
            api->struct_size != sizeof(EdenDsmodModuleApi) ||
            api->abi_hash != EDEN_DSMOD_MODULE_ABI_HASH || api->title_id != title_id ||
            !api->supports_build || !api->create || !api->destroy || !api->sample || !api->tick) {
            throw std::runtime_error("incompatible dual-screen module interface");
        }
        if (!api->supports_build(build.c_str())) {
            throw std::runtime_error("native module rejected this game build");
        }
        // Each optional extension is looked up and checked the same way. Only the base
        // extensions treat a null return as an error; font, save and write are independently
        // optional, so a module that offers none of them (MK8D's shipped module predates the
        // font symbol entirely) keeps loading unmodified.
        loaded->extensions =
            NegotiateExtension<EdenDsmodModuleExtensions, EdenDsmodGetExtensionsFn>(
                loaded->library, "eden_dsmod_get_extensions", EDEN_DSMOD_EXT_VERSION,
                EDEN_DSMOD_EXT_HASH, &EdenDsmodModuleExtensions::configure, true,
                "incompatible dual-screen module extensions");
        if ((api->capabilities & EDEN_DSMOD_CAP_EXTENSIONS) != 0 && !loaded->extensions) {
            throw std::runtime_error("module requires missing dual-screen extensions");
        }
        loaded->font_extensions =
            NegotiateExtension<EdenDsmodFontExtensions, EdenDsmodGetFontExtensionsFn>(
                loaded->library, "eden_dsmod_get_font_extensions", EDEN_DSMOD_FONT_EXT_VERSION,
                EDEN_DSMOD_FONT_EXT_HASH, &EdenDsmodFontExtensions::decode_font, false,
                "incompatible dual-screen font extensions");
        // A module that reads no save data simply doesn't export this symbol.
        loaded->save_extensions =
            NegotiateExtension<EdenDsmodModuleSaveExtensions, EdenDsmodGetSaveExtensionsFn>(
                loaded->library, "eden_dsmod_get_save_extensions", EDEN_DSMOD_SAVE_EXT_VERSION,
                EDEN_DSMOD_SAVE_EXT_HASH, &EdenDsmodModuleSaveExtensions::configure, false,
                "incompatible dual-screen save extensions");
        // Only a module that writes guest state as one unit (e.g. a list rotation the game must
        // never see half-done) exports this symbol.
        loaded->write_extensions =
            NegotiateExtension<EdenDsmodModuleWriteExtensions, EdenDsmodGetWriteExtensionsFn>(
                loaded->library, "eden_dsmod_get_write_extensions", EDEN_DSMOD_WRITE_EXT_VERSION,
                EDEN_DSMOD_WRITE_EXT_HASH, &EdenDsmodModuleWriteExtensions::configure, false,
                "incompatible dual-screen write extensions");
        // Only a module that generates package data ("module:" byte sources) exports this.
        loaded->data_extensions =
            NegotiateExtension<EdenDsmodModuleDataExtensions, EdenDsmodGetDataExtensionsFn>(
                loaded->library, "eden_dsmod_get_data_extensions", EDEN_DSMOD_DATA_EXT_VERSION,
                EDEN_DSMOD_DATA_EXT_HASH, &EdenDsmodModuleDataExtensions::load_data, false,
                "incompatible dual-screen data extensions");
        LOG_INFO(Core, "DSMod module: loaded {} for title {} build {} ({})",
                 api->name ? api->name : "unnamed", title, build, platform);
        return loaded;
    } catch (const std::exception& exception) {
        error = exception.what();
        LOG_ERROR(Core, "DSMod module: {}", error);
        return nullptr;
    } catch (...) {
        error = "dual-screen module threw while loading";
        LOG_ERROR(Core, "DSMod module: {}", error);
        return nullptr;
    }
}

ModuleActionOutcome CallModuleAction(const EdenDsmodModuleExtensions* extensions, void* instance,
                                     const std::string& name, s64 argument) {
    if (!extensions || !extensions->on_action || !instance || name.empty() ||
        name.size() > MaxModuleActionName) {
        return ModuleActionOutcome::NotRun;
    }
    try {
        if (extensions->on_action(instance, name.c_str(), argument)) {
            return ModuleActionOutcome::Accepted;
        }
        LOG_INFO(Core, "DSMod: module declined action '{}'", name);
    } catch (...) {
        LOG_ERROR(Core, "DSMod: module action '{}' threw", name);
    }
    return ModuleActionOutcome::Declined;
}

} // namespace Core::Mods
