// SPDX-FileCopyrightText: Copyright 2026 Eden Duo Windows fork
// SPDX-License-Identifier: GPL-3.0-or-later

// Desktop installer for Eden Duo companion packages (`.dsmod.zip`).
//
// The Android app installs these from its Add-ons menu. The Qt frontend had no equivalent, so a
// Windows player could only unzip a package by hand. This mirrors the essential parts of
// DualScreenPackageInstaller.kt: identity from package.json, the `package.json` +
// `dualscreen/...` layout, zip-slip and duplicate-entry checks, size limits, and replacement of
// an earlier install of the same package. The heavy Android-only validation (native module
// checksums, per-platform library selection) is left to the runtime, which already rejects a
// package it cannot use.

#pragma once

#include <QString>

namespace DSModInstaller {

struct Result {
    bool success{};
    /// User-facing message, suitable for a message box.
    QString message;
    /// Absolute path of the installed add-on folder, when success is true.
    QString folder;
};

/// Installs a `.dsmod.zip` into <Eden data>/load/<TITLEID>/<Name>-<version>/.
Result InstallArchive(const QString& archive_path);

} // namespace DSModInstaller
