// SPDX-FileCopyrightText: Copyright 2026 Eden Duo Windows fork
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu/duo/dsmod_installer.h"

#include <algorithm>
#include <array>
#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <quazip.h>
#include <quazipfile.h>

#include "common/fs/path_util.h"
#include "common/logging.h"

namespace DSModInstaller {
namespace {

constexpr qint64 MaxArchiveBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxExpandedBytes = 1024LL * 1024 * 1024;
constexpr qint64 MaxEntryBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxMetadataBytes = 4LL * 1024 * 1024;
constexpr int MaxEntries = 10000;

constexpr char PackageJsonName[] = "package.json";
constexpr char DualScreenPrefix[] = "dualscreen/";
constexpr char ManifestJsonName[] = "dualscreen/manifest.json";

Result Fail(const QString& message) {
    return Result{false, message, {}};
}

bool IsSafeEntryName(const QString& name) {
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
        name.contains(QLatin1Char(':')) || QDir::isAbsolutePath(name)) {
        return false;
    }
    QString trimmed = name;
    while (trimmed.endsWith(QLatin1Char('/'))) {
        trimmed.chop(1);
    }
    if (trimmed.isEmpty()) {
        return false;
    }
    const QStringList parts = trimmed.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..")) {
            return false;
        }
    }
    return true;
}

/// True for the two allowed top-level shapes: `package.json` or `dualscreen/...`.
bool IsAllowedEntry(const QString& name) {
    if (name == QLatin1String(PackageJsonName)) {
        return true;
    }
    return name.startsWith(QLatin1String(DualScreenPrefix));
}

QString SanitizeLabel(const QString& label) {
    QString out;
    out.reserve(std::min<qsizetype>(label.size(), 64));
    for (const QChar ch : label) {
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('_')) {
            out.append(ch);
            if (out.size() == 64) {
                break;
            }
        }
    }
    return out;
}

/// Opens an entry by name. `QuaZipFile::setFileName()` is *not* enough: for a QuaZipFile that was
/// not created "internally", `open()` ignores the name and reads whatever entry the zip is
/// currently positioned on, so the zip itself has to be moved first.
bool OpenEntry(QuaZip& zip, QuaZipFile& file, const QString& name) {
    if (!zip.setCurrentFile(name)) {
        LOG_WARNING(Frontend, "DSMod install: '{}' is not in the archive", name.toStdString());
        return false;
    }
    file.setFileName(name);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARNING(Frontend, "DSMod install: cannot open '{}' inside the archive",
                    name.toStdString());
        return false;
    }
    return true;
}

/// Reads a whole entry in bounded chunks. A single huge read() would return an empty buffer on a
/// decompression error, which is indistinguishable from valid-but-empty metadata; reading in
/// chunks lets us tell "empty" from "failed" and keeps the error message truthful.
bool ReadEntry(QuaZip& zip, const QString& name, qint64 limit, QByteArray* out) {
    QuaZipFile file(&zip);
    if (!OpenEntry(zip, file, name)) {
        return false;
    }

    QByteArray data;
    std::array<char, 64 * 1024> buffer{};
    while (true) {
        const qint64 got = file.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (got < 0) {
            LOG_WARNING(Frontend, "DSMod install: read error on '{}' (zip error {})",
                        name.toStdString(), file.getZipError());
            file.close();
            return false;
        }
        if (got == 0) {
            break;
        }
        if (data.size() + got > limit) {
            LOG_WARNING(Frontend, "DSMod install: '{}' is larger than the allowed metadata size",
                        name.toStdString());
            file.close();
            return false;
        }
        data.append(buffer.data(), static_cast<int>(got));
    }
    file.close();
    *out = data;
    return true;
}

/// Qt's JSON parser rejects a UTF-8 BOM, which some Windows tools write when re-saving a package.
QByteArray StripUtf8Bom(QByteArray data) {
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }
    return data;
}

/// Removes an earlier install of the same package (same type and name) for this title, so an
/// updated version is the one the runtime discovers (it takes the first enabled folder by name).
/// The staging folder of the install in progress must be left alone: it already holds the new
/// package.json, so it would match the criteria and delete the files just extracted.
void RemoveEarlierInstalls(const QString& title_load_dir, const QString& package_name,
                           const QString& keep_dir) {
    const QDir root(title_load_dir);
    const auto dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& dir : dirs) {
        if (dir.absoluteFilePath() == keep_dir ||
            dir.fileName().endsWith(QStringLiteral(".installing"))) {
            continue;
        }
        QFile package_file(dir.absoluteFilePath() + QStringLiteral("/package.json"));
        if (!package_file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(package_file.read(MaxMetadataBytes));
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("dual-screen-mod") &&
            obj.value(QStringLiteral("name")).toString() == package_name) {
            QDir(dir.absoluteFilePath()).removeRecursively();
        }
    }
}

} // namespace

Result InstallArchive(const QString& archive_path) {
    const QFileInfo archive_info(archive_path);
    if (!archive_info.isFile()) {
        return Fail(QStringLiteral("The selected file does not exist."));
    }
    if (archive_info.size() > MaxArchiveBytes) {
        return Fail(QStringLiteral("The package is larger than 512 MiB."));
    }

    QuaZip zip(archive_path);
    zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdUnzip)) {
        return Fail(QStringLiteral("This is not a readable .dsmod.zip archive."));
    }

    const QList<QuaZipFileInfo64> entries = zip.getFileInfoList64();
    if (entries.size() > MaxEntries) {
        return Fail(QStringLiteral("The archive contains too many entries."));
    }

    qint64 expanded = 0;
    bool has_package_json = false;
    bool has_manifest = false;
    QSet<QString> seen;
    for (const QuaZipFileInfo64& entry : entries) {
        if (!IsSafeEntryName(entry.name) || !IsAllowedEntry(entry.name)) {
            return Fail(QStringLiteral("Unsafe or unexpected archive entry: %1").arg(entry.name));
        }
        const QString key = entry.name.toCaseFolded();
        if (seen.contains(key)) {
            return Fail(QStringLiteral("Duplicate archive entry: %1").arg(entry.name));
        }
        seen.insert(key);
        if (entry.name == QLatin1String(PackageJsonName)) {
            has_package_json = true;
        } else if (entry.name == QLatin1String(ManifestJsonName)) {
            has_manifest = true;
        }
        if (!entry.name.endsWith(QLatin1Char('/'))) {
            if (entry.uncompressedSize > MaxEntryBytes) {
                return Fail(QStringLiteral("Archive entry is too large: %1").arg(entry.name));
            }
            expanded += static_cast<qint64>(entry.uncompressedSize);
            if (expanded > MaxExpandedBytes) {
                return Fail(QStringLiteral("The package expands to more than 1 GiB."));
            }
        }
    }

    if (!has_package_json) {
        return Fail(QStringLiteral("The archive has no package.json."));
    }
    if (!has_manifest) {
        return Fail(QStringLiteral("The archive has no dualscreen/manifest.json."));
    }

    QByteArray package_bytes;
    if (!ReadEntry(zip, QString::fromLatin1(PackageJsonName), MaxMetadataBytes, &package_bytes)) {
        return Fail(QStringLiteral("package.json could not be read from the archive."));
    }
    QJsonParseError package_error{};
    const QJsonDocument package_doc =
        QJsonDocument::fromJson(StripUtf8Bom(package_bytes), &package_error);
    if (!package_doc.isObject()) {
        LOG_WARNING(Frontend, "DSMod install: package.json is not valid JSON: {} at offset {} ({} bytes, starts with '{}')",
                    package_error.errorString().toStdString(), package_error.offset,
                    package_bytes.size(), package_bytes.left(24).toHex().toStdString());
        return Fail(QStringLiteral("package.json is not valid JSON (%1 at offset %2).")
                        .arg(package_error.errorString())
                        .arg(package_error.offset));
    }
    const QJsonObject package = package_doc.object();
    if (package.value(QStringLiteral("format")).toInt() != 1 ||
        package.value(QStringLiteral("type")).toString() != QStringLiteral("dual-screen-mod")) {
        return Fail(QStringLiteral("This is not a dual-screen mod package."));
    }

    QString title_id = package.value(QStringLiteral("title_id")).toString().toUpper();
    static const QRegularExpression title_id_re(QStringLiteral("^[0-9A-F]{16}$"));
    if (!title_id_re.match(title_id).hasMatch()) {
        return Fail(QStringLiteral("package.json has no valid title_id."));
    }

    const QString package_name = package.value(QStringLiteral("name")).toString();
    if (package_name.trimmed().isEmpty() || package_name.size() > 256) {
        return Fail(QStringLiteral("package.json has no valid name."));
    }
    const QString version = package.value(QStringLiteral("version")).toString();
    static const QRegularExpression version_re(
        QStringLiteral("^[0-9]+\\.[0-9]+\\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$"));
    if (!version_re.match(version).hasMatch()) {
        return Fail(QStringLiteral("package.json has no valid version."));
    }

    QString folder_name = SanitizeLabel(package_name);
    if (folder_name.isEmpty()) {
        folder_name = QStringLiteral("DualScreen-%1").arg(title_id);
    } else {
        folder_name += QLatin1Char('-') + version;
    }

    const QString load_dir = QString::fromStdString(
        Common::FS::PathToUTF8String(Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir)));
    const QString title_dir = QDir(load_dir).filePath(title_id);
    const QString target_dir = QDir(title_dir).filePath(folder_name);
    const QString staging_dir = target_dir + QStringLiteral(".installing");

    QDir().mkpath(title_dir);
    QDir(staging_dir).removeRecursively();
    if (!QDir().mkpath(staging_dir)) {
        return Fail(QStringLiteral("Cannot write to %1").arg(title_dir));
    }

    for (const QuaZipFileInfo64& entry : entries) {
        if (entry.name.endsWith(QLatin1Char('/'))) {
            QDir().mkpath(QDir(staging_dir).filePath(entry.name));
            continue;
        }
        const QString out_path = QDir(staging_dir).filePath(entry.name);
        if (!QDir().mkpath(QFileInfo(out_path).absolutePath())) {
            QDir(staging_dir).removeRecursively();
            return Fail(QStringLiteral("Cannot create %1").arg(out_path));
        }
        QuaZipFile file(&zip);
        if (!OpenEntry(zip, file, entry.name)) {
            QDir(staging_dir).removeRecursively();
            return Fail(QStringLiteral("Cannot read archive entry %1").arg(entry.name));
        }
        QFile out(out_path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.close();
            QDir(staging_dir).removeRecursively();
            return Fail(QStringLiteral("Cannot write %1").arg(out_path));
        }
        qint64 remaining = static_cast<qint64>(entry.uncompressedSize);
        bool ok = true;
        while (remaining > 0) {
            const QByteArray chunk = file.read(std::min<qint64>(remaining, 1 << 20));
            if (chunk.isEmpty() || out.write(chunk) != chunk.size()) {
                ok = false;
                break;
            }
            remaining -= chunk.size();
        }
        // A stream that ends early would silently produce a truncated file, so make the size
        // mismatch an error instead of leaving a broken package behind.
        if (ok && remaining != 0) {
            ok = false;
        }
        if (ok && file.getZipError() != UNZ_OK) {
            ok = false;
        }
        file.close();
        out.close();
        if (!ok) {
            LOG_WARNING(Frontend, "DSMod install: failed while extracting '{}' ({} of {} bytes)",
                        entry.name.toStdString(),
                        static_cast<long long>(entry.uncompressedSize) - remaining,
                        static_cast<long long>(entry.uncompressedSize));
            QDir(staging_dir).removeRecursively();
            return Fail(QStringLiteral("Failed while extracting %1").arg(entry.name));
        }
    }
    zip.close();

    // Replace the previous install of this exact package and the target folder.
    RemoveEarlierInstalls(title_dir, package_name, target_dir);
    QDir(target_dir).removeRecursively();
    if (!QDir().rename(staging_dir, target_dir)) {
        QDir(staging_dir).removeRecursively();
        return Fail(QStringLiteral("Cannot move the installed package into %1").arg(title_dir));
    }

    return Result{true,
                  QStringLiteral("Installed %1 %2 for title %3.")
                      .arg(package_name, version, title_id),
                  target_dir};
}

} // namespace DSModInstaller
