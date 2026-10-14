// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.InputStream
import java.util.UUID
import java.util.zip.ZipInputStream

/** File-only staging and replacement; reading Android documents and reloading native state are separate. */
object EmulatorDataImporter {
    private const val PREFIX = ".eden-import-"
    private const val MAX_CONFIG_BYTES = 8L * 1024 * 1024
    private const val FREE_SPACE_RESERVE = 32L * 1024 * 1024
    private val ignoredRoots = setOf("cache", "log", "crash_dumps", "shader", "import-backups")

    class Cancelled : IOException()

    class Session(val directory: File) {
        val payload = File(directory, "payload")
        val previous = File(directory, "previous")
        var files = 0L
        var bytes = 0L
        private val copiedPaths = mutableSetOf<String>()

        fun copy(
            input: InputStream,
            relativePath: String,
            progress: (files: Long, bytes: Long, path: String) -> Boolean
        ) {
            input.use {
                val path = checkedPath(relativePath)
                if (path.substringBefore('/') in ignoredRoots) return
                if (!copiedPaths.add(path)) throw IOException("Duplicate file: $path")
                if (copiedPaths.size > 1_000_000) {
                    throw IOException(
                        "Too many files in the selected data"
                    )
                }
                val destination = File(payload, path)
                if (!destination.parentFile!!.isDirectory && !destination.parentFile!!.mkdirs()) {
                    throw IOException("Cannot create the import folder")
                }
                destination.outputStream().use { output ->
                    val buffer = ByteArray(256 * 1024)
                    while (true) {
                        if (progress(files, bytes, path)) throw Cancelled()
                        val count = input.read(buffer)
                        if (count == -1) break
                        if (destination.usableSpace < count + FREE_SPACE_RESERVE) {
                            throw IOException("Not enough free space to copy emulator data")
                        }
                        output.write(buffer, 0, count)
                        bytes += count
                    }
                }
                files++
            }
        }

        fun createFolder(relativePath: String) {
            val path = checkedPath(relativePath)
            if (path.substringBefore('/') in ignoredRoots) return
            val folder = File(payload, path)
            if (!folder.isDirectory && !folder.mkdirs()) {
                throw IOException(
                    "Cannot create the import folder"
                )
            }
        }

        fun discard() {
            if (!previous.exists() && File(directory, "state").readTextOrEmpty() != "applying") {
                directory.deleteRecursively()
            }
        }

        fun removeFolder(relative: String) {
            val prefix = checkedPath(relative)
            File(payload, prefix).deleteRecursively()
            copiedPaths.removeAll { it == prefix || it.startsWith("$prefix/") }
            val remaining = payload.walkTopDown().filter { it.isFile }.toList()
            files = remaining.size.toLong()
            bytes = remaining.sumOf { it.length() }
        }
    }

    fun create(userDirectory: File): Session {
        val directory = File(userDirectory.parentFile, PREFIX + UUID.randomUUID())
        if (!File(directory, "payload").mkdirs()) {
            throw IOException(
                "Cannot create the import folder"
            )
        }
        return Session(directory)
    }

    internal fun checkedPath(path: String): String {
        // Older Yuzu exports start entries with one slash. Do not permit absolute/drive paths.
        val relative = path.removePrefix("/")
        val parts = relative.split('/')
        if (relative.isBlank() || '\\' in relative || ':' in relative || '\u0000' in relative ||
            parts.any { it.isEmpty() || it == "." || it == ".." } || parts.size > 64
        ) {
            throw IOException("Invalid path in emulator data")
        }
        return relative
    }

    fun unpack(session: Session, input: InputStream, progress: (Long, Long, String) -> Boolean) {
        ZipInputStream(input.buffered()).use { zip ->
            var entries = 0
            while (true) {
                val entry = zip.nextEntry ?: break
                if (++entries > 1_000_000) throw IOException("Too many entries in the backup")
                checkedPath(entry.name.trimEnd('/'))
                if (progress(session.files, session.bytes, entry.name)) throw Cancelled()
                if (entry.isDirectory) {
                    session.createFolder(entry.name.trimEnd('/'))
                } else {
                    // Session.copy closes its input. Keep the archive stream open between entries.
                    session.copy(
                        object : InputStream() {
                            override fun read() = zip.read()
                            override fun read(buffer: ByteArray, offset: Int, length: Int) = zip.read(
                                buffer,
                                offset,
                                length
                            )
                            override fun close() = Unit
                        },
                        entry.name,
                        progress
                    )
                }
                zip.closeEntry()
            }
        }
        if (!File(session.payload, "config/config.ini").isFile) {
            val nested = session.payload.listFiles().orEmpty().filter {
                it.isDirectory && File(it, "config/config.ini").isFile
            }
            if (nested.size == 1 && session.payload.listFiles().orEmpty().size == 1) {
                val wrapper = nested.single()
                wrapper.listFiles().orEmpty().forEach {
                    if (!it.renameTo(File(session.payload, it.name))) {
                        throw IOException(
                            "Cannot unpack the backup"
                        )
                    }
                }
                wrapper.delete()
            }
        }
        validate(session)
    }

    fun validate(session: Session): String {
        val config = File(session.payload, "config/config.ini")
        if (!config.isFile || config.length() > MAX_CONFIG_BYTES) {
            throw IOException("Select the emulator user folder or a full user-data ZIP backup")
        }
        val text = config.readText().removePrefix("\uFEFF")
        if (!Regex("(?m)^\\s*\\[(Paths|Core|Renderer|Controls|System)]\\s*$").containsMatchIn(text)) {
            throw IOException("The selected folder does not contain compatible emulator settings")
        }
        return text
    }

    /** No destination file changes until the complete source has been staged and reviewed. */
    fun apply(session: Session, userDirectory: File, reload: () -> Unit) {
        validate(session)
        if (!userDirectory.isDirectory) throw IOException("Eden Duo data folder is unavailable")
        journal(session, "applying")
        if (!userDirectory.renameTo(session.previous)) {
            journal(session, "ready")
            throw IOException("Cannot back up the current Eden Duo data")
        }
        try {
            if (!session.payload.renameTo(userDirectory)) {
                throw IOException(
                    "Cannot install the imported data"
                )
            }
            reload()
            journal(session, "complete")
        } catch (e: Throwable) {
            if (userDirectory.exists() && !userDirectory.renameTo(session.payload)) {
                throw IOException("Import failed. Restart Eden Duo to recover the previous data", e)
            }
            if (!session.previous.renameTo(userDirectory)) {
                throw IOException("Import failed. Restart Eden Duo to recover the previous data", e)
            }
            journal(session, "ready")
            try {
                reload()
            } catch (restoreError: Throwable) {
                e.addSuppressed(restoreError)
            }
            throw e
        }
    }

    /** Called before initializing the emulator after a process death during replacement. */
    fun recover(userDirectory: File) {
        sessions(userDirectory).filter { File(it, "state").readTextOrEmpty() == "applying" }.forEach { dir ->
            val previous = File(dir, "previous")
            if (previous.isDirectory) {
                val interrupted = File(dir, "interrupted")
                if (userDirectory.exists() && !userDirectory.renameTo(interrupted)) {
                    throw IOException("Cannot recover the previous emulator data")
                }
                if (!previous.renameTo(userDirectory)) {
                    interrupted.renameTo(userDirectory)
                    throw IOException("Cannot recover the previous emulator data")
                }
                interrupted.deleteRecursively()
            }
            journal(Session(dir), "recovered")
        }
    }

    fun latestBackup(userDirectory: File): Session? = sessions(userDirectory)
        .filter { File(it, "state").readTextOrEmpty() == "complete" && File(it, "previous").isDirectory }
        .maxByOrNull { File(it, "state").lastModified() }?.let { Session(it) }

    fun restore(backup: Session, userDirectory: File, reload: () -> Unit): Session {
        val session = create(userDirectory)
        session.payload.delete()
        if (!backup.previous.renameTo(session.payload)) {
            throw IOException(
                "Cannot open the previous data backup"
            )
        }
        try {
            apply(session, userDirectory, reload)
        } catch (e: Throwable) {
            session.payload.renameTo(backup.previous)
            session.discard()
            throw e
        }
        return session
    }

    private fun sessions(userDirectory: File) = userDirectory.parentFile?.listFiles().orEmpty()
        .filter { it.isDirectory && it.name.startsWith(PREFIX) }

    private fun journal(session: Session, state: String) {
        val temporary = File(session.directory, "state.tmp")
        FileOutputStream(temporary).use {
            it.write(state.toByteArray())
            it.fd.sync()
        }
        if (!temporary.renameTo(File(session.directory, "state"))) {
            throw IOException("Cannot record emulator import progress")
        }
    }

    private fun File.readTextOrEmpty() = if (isFile) readText() else ""
}
