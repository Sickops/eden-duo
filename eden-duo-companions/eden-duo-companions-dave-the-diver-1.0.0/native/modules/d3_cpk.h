// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Diablo III (Switch) CPK archives read through the host's romfs service, so the companion needs
// no shipped game data. Format: big-endian bit-packed tables (SortedFileInfo keyed by the FNV-1a
// 64 hash of the lower-case member name, Locations, CompSectorToDecompOffset), then compressed
// sectors of zlib chunks (optionally XOR-stream encrypted). Later archives override earlier ones
// (base CPKs, then the update's Patch*.cpk in version order). Reference: research
// diablo-iii-eternal-collection (d3cpk cpklib, visual/tools/d3arc.py).

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/mods/dsmod_module_abi.h"
#include "d3_build.h"

namespace d3_cpk {

class Archives {
public:
    Archives();
    ~Archives();
    /// Opens `build`'s archive set: the shared base archives, then `build.patch_archives`
    /// (idempotent; retried on a later call after a failure).
    bool Open(const EdenDsmodHostApi* host, const d3_build::Build& build);
    /// Reads one member ("Textures\\X.tex", "StringList\\Y.stl|enUS"). Thread-safe.
    bool Read(const EdenDsmodHostApi* host, std::string_view name, std::vector<std::uint8_t>& out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/// FNV-1a 64 of the lower-case name (the archive's member hash).
std::uint64_t NameHash(std::string_view name);

} // namespace d3_cpk
