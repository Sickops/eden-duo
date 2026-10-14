// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import java.io.IOException

object EmulatorImportSources {
    data class Source(val label: String, val authority: String, val packageName: String) {
        val initialUri: Uri get() = DocumentsContract.buildRootUri(authority, "root")
    }

    /** Discover providers rather than maintaining a list that misses renamed/spoofed forks. */
    fun installed(context: Context): List<Source> = context.packageManager
        .queryIntentContentProviders(Intent(DocumentsContract.PROVIDER_INTERFACE), 0)
        .mapNotNull { result ->
            val info = result.providerInfo ?: return@mapNotNull null
            if (info.packageName == context.packageName || !info.exported ||
                !isCompatibleProvider(info.packageName, info.authority.orEmpty(), info.name)
            ) {
                return@mapNotNull null
            }
            val appName = context.packageManager.getApplicationLabel(info.applicationInfo).toString()
            val baseLabel = when {
                info.packageName.startsWith("dev.eden.eden_emulator.dualscreen") -> "Eden DS"
                info.packageName == "dev.eden.eden_emulator" -> "Eden Stable"
                info.packageName.startsWith("dev.eden.eden_emulator.nightly") -> "Eden Nightly"
                else -> appName
            }
            val version = runCatching {
                context.packageManager.getPackageInfo(info.packageName, 0).versionName
            }.getOrNull().orEmpty()
            Source("$baseLabel $version".trim(), info.authority, info.packageName)
        }.distinctBy { it.authority }.sortedWith(
            compareBy<Source> { !it.packageName.startsWith("dev.eden.") }.thenBy { it.label.lowercase() }
        )

    internal fun isCompatibleProvider(packageName: String, authority: String, className: String): Boolean =
        authority == "$packageName.user" &&
            (
                className.startsWith("org.yuzu.yuzu_emu.") && className.endsWith(".features.DocumentProvider") ||
                    listOf("eden", "yuzu", "suyu", "sudachi", "citron", "uzuy", "lemon", "torzu", "ziunx")
                        .any { packageName.contains(it, ignoreCase = true) }
                )

    fun copyTree(
        context: Context,
        tree: Uri,
        session: EmulatorDataImporter.Session,
        destination: String = "",
        progress: (Long, Long, String) -> Boolean
    ) {
        if (tree.authority == "${context.packageName}.user") {
            throw IOException("Choose another emulator or a backup folder")
        }
        val seen = mutableSetOf<String>()
        val resolver = context.contentResolver
        fun walk(documentId: String, path: String, depth: Int) {
            if (depth > 64 || !seen.add(documentId) || seen.size > 1_000_000) {
                throw IOException("The selected folder contains too many nested folders")
            }
            if (progress(session.files, session.bytes, path)) throw EmulatorDataImporter.Cancelled()
            if (path.isNotEmpty()) session.createFolder(path)
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId)
            val projection = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE
            )
            resolver.query(children, projection, null, null, null)?.use { cursor ->
                while (cursor.moveToNext()) {
                    val childId = cursor.getString(0)
                    val name = cursor.getString(1) ?: throw IOException("A source file has no name")
                    if ('/' in name) throw IOException("Invalid source file name")
                    val relative = if (path.isEmpty()) name else "$path/$name"
                    EmulatorDataImporter.checkedPath(relative)
                    // Skip generated caches before walking them, rather than reading them all.
                    if (path.isEmpty() && name in EmulatorDataImporter.ignoredRoots) continue
                    if (cursor.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) {
                        walk(childId, relative, depth + 1)
                    } else {
                        val uri = DocumentsContract.buildDocumentUriUsingTree(tree, childId)
                        val input = resolver.openInputStream(uri) ?: throw IOException(
                            "Cannot read $name"
                        )
                        session.copy(input, relative, progress)
                    }
                }
            } ?: throw IOException("Cannot read the selected folder")
        }
        walk(DocumentsContract.getTreeDocumentId(tree), destination, 0)
    }
}
