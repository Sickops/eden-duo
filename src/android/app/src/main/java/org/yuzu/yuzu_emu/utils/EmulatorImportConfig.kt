// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File
import java.io.IOException
import java.net.URI
import java.net.URLDecoder
import java.net.URLEncoder

/** Rebase copied paths; Android URI grants from the source app are never assumed to transfer. */
object EmulatorImportConfig {
    enum class Kind { GAMES, CONTENT, NAND, SDMC, SAVES }
    data class Folder(val original: String, val destination: String, val kind: Kind) {
        val selectionKey get() = if (kind == Kind.SAVES) "save:$original" else original
    }
    private val arrayPath = Regex("(gamedirs|external_content_dirs)\\\\([0-9]+)\\\\path")
    private val appDirectory = Regex(
        "(?:/storage/emulated/[0-9]+|/storage/[^/]+|/sdcard)/Android/data/[^/]+/files(?=/|$)"
    )
    private val scalarPaths = mapOf(
        "nand_directory" to Kind.NAND,
        "sdmc_directory" to Kind.SDMC,
        "save_directory" to Kind.SAVES
    )

    fun requestedFolders(payload: File, sourceAuthority: String?): List<Folder> {
        val folders = linkedMapOf<String, Folder>()
        configs(payload).forEach { file ->
            parsePaths(file.readText()).forEach path@{ (key, value) ->
                val match = arrayPath.matchEntire(key)
                val kind = match?.let { if (it.groupValues[1] == "gamedirs") Kind.GAMES else Kind.CONTENT }
                    ?: scalarPaths[key] ?: return@path
                if (kind == Kind.SAVES) return@path
                if (value.isNotBlank() && internalRelative(value, sourceAuthority) == null) {
                    folders.getOrPut(value) {
                        Folder(
                            value,
                            "imported/folder-${folders.size + 1}",
                            kind
                        )
                    }
                }
            }
            val paths = parsePaths(file.readText())
            val saveRoot = paths["save_directory"].orEmpty().ifBlank {
                paths["nand_directory"].orEmpty().ifBlank { "default:saves" }
            }
            if ((file == File(payload, "config/config.ini") && File(payload, "nand").exists()) || paths["save_directory"].orEmpty().isNotBlank()) {
                val relative = internalRelative(saveRoot, sourceAuthority)
                val folder = Folder(saveRoot, relative ?: if (saveRoot == "default:saves") "nand" else "imported/saves-${folders.size + 1}", Kind.SAVES)
                folders.putIfAbsent(folder.selectionKey, folder)
            }
        }
        return folders.values.toList()
    }

    fun rewrite(
        payload: File,
        userDirectory: File,
        ownAuthority: String,
        sourceAuthority: String?,
        copied: Map<String, String>,
        additional: List<Pair<Kind, String>> = emptyList()
    ) {
        configs(payload).forEach { file ->
            file.writeText(
                rewriteText(
                    file.readText(),
                    userDirectory,
                    ownAuthority,
                    sourceAuthority,
                    copied,
                    if (file == File(payload, "config/config.ini")) additional else emptyList()
                )
            )
        }
    }

    internal fun rewriteText(
        text: String,
        userDirectory: File,
        ownAuthority: String,
        sourceAuthority: String?,
        copied: Map<String, String>,
        additional: List<Pair<Kind, String>> = emptyList()
    ): String {
        val paths = parsePaths(text)
        val games = sortedMapOf<Int, Pair<String, String>>()
        val content = sortedMapOf<Int, String>()
        paths.forEach { (key, value) ->
            arrayPath.matchEntire(key)?.let { match ->
                val index = match.groupValues[2].toIntOrNull() ?: throw IOException(
                    "Invalid game folder index"
                )
                val relative = internalRelative(value, sourceAuthority) ?: copied[value]
                if (relative != null) {
                    val uri = if (relative.startsWith("content://")) relative else treeUri(ownAuthority, relative)
                    if (match.groupValues[1] == "gamedirs") {
                        val defaultScan = paths["gamedirs\\$index\\deep_scan\\default"] == "true"
                        games[index] = uri to if (defaultScan) "false" else (paths["gamedirs\\$index\\deep_scan"] ?: "false")
                    } else {
                        content[index] = uri
                    }
                }
            }
        }
        additional.forEach { (kind, relative) ->
            val uri = if (relative.startsWith("content://")) relative else treeUri(ownAuthority, relative)
            if (kind == Kind.GAMES) games[(games.keys.maxOrNull() ?: 0) + 1] = uri to "true"
            if (kind == Kind.CONTENT) content[(content.keys.maxOrNull() ?: 0) + 1] = uri
        }
        var section = ""
        val lines = mutableListOf<String>()
        text.removePrefix("\uFEFF").lineSequence().forEach { line ->
            val trimmed = line.trim()
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                section = trimmed.drop(1).dropLast(
                    1
                )
            }
            val key = line.substringBefore('=').trim()
            val pathKey = pathKey(section, key)
            if (pathKey != null && (pathKey.startsWith("gamedirs\\") ||
                    pathKey.startsWith("external_content_dirs\\") ||
                    pathKey.substringBefore('\\') in scalarPaths)) {
                return@forEach
            }
            if ('=' in line && !trimmed.startsWith(';') && !trimmed.startsWith('#')) {
                val value = line.substringAfter('=').trim().removeSurrounding("\"")
                val relative = internalRelative(value, sourceAuthority)
                lines += if (relative != null) "$key = ${File(userDirectory, relative).path}" else line
            } else {
                lines += line
            }
        }
        // Insert into the existing Paths section, or create it for older backups.
        val generated = mutableListOf<String>()
        generated += "gamedirs\\size = ${games.size}"
        games.values.forEachIndexed { index, (uri, deepScan) ->
            generated += "gamedirs\\${index + 1}\\path = $uri"
            generated += "gamedirs\\${index + 1}\\deep_scan = $deepScan"
            generated += "gamedirs\\${index + 1}\\deep_scan\\default = false"
        }
        generated += "external_content_dirs\\size = ${content.size}"
        content.values.forEachIndexed { index, uri -> generated += "external_content_dirs\\${index + 1}\\path = $uri" }
        scalarPaths.forEach { (key, _) ->
            val original = paths[key].orEmpty().let { value ->
                if (key == "save_directory" && value.isBlank()) paths["nand_directory"].orEmpty().ifBlank { "default:saves" } else value
            }
            val selected = if (key == "save_directory") copied["save:$original"] ?: copied[original] else copied[original]
            val relative = if (original.isBlank()) {
                null
            } else {
                internalRelative(
                    original,
                    sourceAuthority
                ).let { selected ?: it }
            }
            generated += "$key = ${relative?.let { if (it.startsWith("content://")) it else File(userDirectory, it).path }.orEmpty()}"
        }
        fun insert(group: String, values: List<String>) {
            val position = lines.indexOfFirst { it.trim() == "[$group]" }
            if (position == -1) {
                lines += "\n[$group]"
                lines += values
            } else {
                lines.addAll(position + 1, values)
            }
        }
        // Current AndroidConfig nests ReadPathValues inside [UI]. Older forks used [Paths].
        if (lines.any { it.trim() == "[Paths]" }) insert("Paths", generated)
        insert("UI", generated.map { "Paths\\$it" })
        val storage = lines.firstOrNull { it.trim() in setOf("[Data%20Storage]", "[Data Storage]") }
        if (storage != null) {
            insert(storage.trim().removeSurrounding("[", "]"), generated.filter {
                it.substringBefore(" = ") in scalarPaths
            })
        }
        return lines.joinToString("\n")
    }

    internal fun internalRelative(value: String, sourceAuthority: String?): String? {
        val absolute = appDirectory.find(value)?.takeIf { it.range.first == 0 }
        if (absolute != null) {
            val relative = value.substring(absolute.range.last + 1).trimStart('/')
            return relative.takeIf { it.isNotEmpty() }?.let { EmulatorDataImporter.checkedPath(it) } ?: ""
        }
        return try {
            val uri = URI(value)
            if (uri.scheme != "content" || !uri.authority.endsWith(".user") ||
                (sourceAuthority != null && uri.authority != sourceAuthority)
            ) {
                return null
            }
            val parts = uri.rawPath.split('/')
            val position = parts.indexOf("tree").takeIf { it >= 0 } ?: parts.indexOf("document")
            if (position < 0 || position + 1 >= parts.size) return null
            val document = URLDecoder.decode(parts[position + 1], "UTF-8").trimEnd('/')
            when {
                document == "root" -> ""
                document.startsWith("root/") -> EmulatorDataImporter.checkedPath(
                    document.removePrefix("root/")
                )
                else -> null
            }
        } catch (_: Exception) {
            null
        }
    }

    fun treeUri(authority: String, relative: String): String {
        val document = if (relative.isBlank()) {
            "root"
        } else {
            "root/${EmulatorDataImporter.checkedPath(
                relative
            )}"
        }
        return "content://$authority/tree/" + URLEncoder.encode(document, "UTF-8").replace(
            "+",
            "%20"
        )
    }

    private fun configs(payload: File) = File(payload, "config").walkTopDown()
        .filter { it.isFile && it.extension.equals("ini", ignoreCase = true) }
        .onEach {
            if (it.length() > 8L * 1024 * 1024) {
                throw IOException(
                    "Emulator settings file is too large"
                )
            }
        }

    private fun pathKey(section: String, key: String): String? = when {
        section == "UI" && key.startsWith("Paths\\") -> key.removePrefix("Paths\\")
        section == "Paths" -> key
        section in setOf("Data%20Storage", "Data Storage") &&
            key.substringBefore('\\') in scalarPaths -> key
        else -> null
    }

    private fun parsePaths(text: String): Map<String, String> {
        var section = ""
        val groups = mutableMapOf<String, MutableMap<String, String>>()
        text.removePrefix("\uFEFF").lineSequence().forEach { line ->
            val value = line.trim()
            if (value.startsWith('[') && value.endsWith(']')) section = value.drop(1).dropLast(1)
            if ('=' in value && !value.startsWith(';') && !value.startsWith('#')) {
                val key = pathKey(section, value.substringBefore('=').trim()) ?: return@forEach
                groups.getOrPut(section) { linkedMapOf() }[key] =
                    value.substringAfter('=').trim().removeSurrounding("\"")
            }
        }
        return linkedMapOf<String, String>().apply {
            listOf("Data%20Storage", "Data Storage", "Paths", "UI").forEach {
                putAll(groups[it].orEmpty())
            }
        }
    }
}
