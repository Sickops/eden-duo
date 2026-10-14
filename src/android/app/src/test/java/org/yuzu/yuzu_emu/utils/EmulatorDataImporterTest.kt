// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class EmulatorDataImporterTest {
    @get:Rule val temporary = TemporaryFolder()
    private val proceed: (Long, Long, String) -> Boolean = { _, _, _ -> false }

    private fun user(): File = temporary.newFolder("files").also {
        File(it, "config").mkdirs()
        File(it, "config/config.ini").writeText("[Core]\nold = true\n")
        File(it, "save").mkdirs()
        File(it, "save/current").writeText("previous save")
    }
    private fun candidate(user: File) = EmulatorDataImporter.create(user).also {
        it.copy("[Core]\nnew = true\n".byteInputStream(), "config/config.ini", proceed)
    }
    private fun zip(vararg entries: Pair<String, String>): ByteArray {
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { stream ->
            entries.forEach { (path, value) ->
                stream.putNextEntry(ZipEntry(path))
                stream.write(value.toByteArray())
                stream.closeEntry()
            }
        }
        return output.toByteArray()
    }

    @Test fun copiesFullDataOnlyWhenAppliedAndCanRestore() {
        val user = user()
        val source = temporary.newFolder("source")
        val data = mapOf(
            "keys/prod.keys" to "keys", "nand/system/Contents/registered/firmware.nca" to "firmware",
            "nand/user/Contents/registered/update.nca" to "update/dlc", "nand/user/save/0/save.dat" to "save",
            "load/0100000000000000/mod/romfs/file" to "mod", "sdmc/Nintendo/file" to "sd",
            "config/custom/0100000000000000.ini" to "[Core]\ncustom = true",
            "config/input/player.ini" to "[Controls]\nbutton_a = button:1",
            "imported/games/game.nsp" to "game"
        )
        val session = candidate(user)
        data.forEach { (path, value) ->
            File(source, path).apply { parentFile!!.mkdirs(); writeText(value) }
            session.copy(File(source, path).inputStream(), path, proceed)
        }
        assertEquals("previous save", File(user, "save/current").readText())
        assertFalse(File(user, "keys/prod.keys").exists())
        EmulatorDataImporter.apply(session, user) {
            assertEquals("keys", File(user, "keys/prod.keys").readText())
        }
        data.forEach { (path, value) ->
            assertEquals(value, File(user, path).readText())
            assertEquals(value, File(source, path).readText())
        }
        assertEquals(session.directory, EmulatorDataImporter.latestBackup(user)!!.directory)
        EmulatorDataImporter.restore(session, user) {}
        assertEquals("previous save", File(user, "save/current").readText())
        assertFalse(File(user, "keys/prod.keys").exists())
        assertEquals(
            "keys",
            File(EmulatorDataImporter.latestBackup(user)!!.previous, "keys/prod.keys").readText()
        )
    }

    @Test fun reloadFailureRollsBackFilesAndKeepsCandidateForRetry() {
        val user = user()
        val session = candidate(user)
        var calls = 0
        assertThrows(IOException::class.java) {
            EmulatorDataImporter.apply(session, user) {
                if (++calls == 1) throw IOException("reload failed")
            }
        }
        assertEquals(2, calls)
        assertEquals("previous save", File(user, "save/current").readText())
        assertTrue(File(session.payload, "config/config.ini").readText().contains("new"))
        EmulatorDataImporter.apply(session, user) {}
        assertTrue(File(user, "config/config.ini").readText().contains("new"))
    }

    @Test fun interruptedReplacementRestoresSnapshotBeforeStartup() {
        val user = user()
        val session = candidate(user)
        File(session.directory, "state").writeText("applying")
        assertTrue(user.renameTo(session.previous))
        assertTrue(session.payload.renameTo(user))
        EmulatorDataImporter.recover(user)
        assertEquals("previous save", File(user, "save/current").readText())
        // The interrupted import is unreachable after a restart, so recovery drops its staging.
        assertFalse(session.directory.exists())
        EmulatorDataImporter.recover(user)
        assertEquals("previous save", File(user, "save/current").readText())
    }

    @Test fun interruptedBeforePayloadRenameRestoresSnapshot() {
        val user = user()
        val session = candidate(user)
        File(session.directory, "state").writeText("applying")
        assertTrue(user.renameTo(session.previous))
        // Android can recreate getExternalFilesDir before recovery runs.
        assertTrue(user.mkdir())
        EmulatorDataImporter.recover(user)
        assertEquals("previous save", File(user, "save/current").readText())
        assertFalse(session.directory.exists())
    }

    @Test fun pruneKeepsOnlyNewestBackupAndLiveSessions() {
        val user = user()
        val first = candidate(user)
        EmulatorDataImporter.apply(first, user) {}
        File(first.directory, "state").setLastModified(1_000)
        val second = candidate(user)
        EmulatorDataImporter.apply(second, user) {}
        val staged = candidate(user)
        val applying = EmulatorDataImporter.create(user).also {
            File(it.directory, "state").writeText("applying")
        }
        EmulatorDataImporter.prune(user, second.directory)
        assertFalse(first.directory.exists())
        assertTrue(second.previous.isDirectory)
        assertTrue(staged.payload.isDirectory)
        assertTrue(applying.directory.exists())

        // Restoring consumes the backup; the new session holds the replaced data.
        val restored = EmulatorDataImporter.restore(second, user) {}
        EmulatorDataImporter.prune(user, restored.directory)
        assertFalse(second.directory.exists())
        assertEquals(restored.directory, EmulatorDataImporter.latestBackup(user)!!.directory)
        assertTrue(staged.payload.isDirectory)
    }

    @Test fun recoverDropsAbandonedStagingButKeepsNewestBackup() {
        val user = user()
        val applied = candidate(user)
        EmulatorDataImporter.apply(applied, user) {}
        val abandoned = candidate(user)
        val failed = candidate(user).also { File(it.directory, "state").writeText("ready") }
        EmulatorDataImporter.recover(user)
        assertFalse(abandoned.directory.exists())
        assertFalse(failed.directory.exists())
        assertTrue(applied.previous.isDirectory)
        assertEquals(applied.directory, EmulatorDataImporter.latestBackup(user)!!.directory)
    }

    @Test fun invalidOrCancelledCopyLeavesOriginalUntouched() {
        val user = user()
        val session = candidate(user)
        assertThrows(EmulatorDataImporter.Cancelled::class.java) {
            session.copy(ByteArray(800_000).inputStream(), "nand/new", { _, _, _ -> true })
        }
        assertEquals("previous save", File(user, "save/current").readText())
        session.discard()
        assertFalse(session.directory.exists())
        assertTrue(user.isDirectory)
    }

    @Test fun rejectsTraversalAndDuplicateFiles() {
        val user = user()
        listOf("../keys/stolen", "/../escape", "//escape", "C:/escape", "a\\b", "a//b", "a/./b").forEach { path ->
            val session = EmulatorDataImporter.create(user)
            assertThrows(IOException::class.java) {
                EmulatorDataImporter.unpack(session, zip(path to "bad").inputStream(), proceed)
            }
            session.discard()
        }
        val session = candidate(user)
        assertThrows(IOException::class.java) {
            session.copy("other".byteInputStream(), "config/config.ini", proceed)
        }
        assertEquals("previous save", File(user, "save/current").readText())
    }

    @Test fun acceptsWrappedAndLegacyBackupsAndPreservesEmptyFolders() {
        val user = user()
        val wrapped = EmulatorDataImporter.create(user)
        EmulatorDataImporter.unpack(
            wrapped,
            zip(
                "yuzu/config/config.ini" to "[Core]\nsetting = 3",
                "yuzu/keys/prod.keys" to "keys",
                "yuzu/games/" to ""
            ).inputStream(),
            proceed
        )
        assertEquals("keys", File(wrapped.payload, "keys/prod.keys").readText())
        assertTrue(File(wrapped.payload, "games").isDirectory)
        val legacy = EmulatorDataImporter.create(user)
        EmulatorDataImporter.unpack(
            legacy,
            zip("/config/config.ini" to "[Paths]\n").inputStream(),
            proceed
        )
        assertTrue(File(legacy.payload, "config/config.ini").isFile)
    }

    @Test fun acceptsUtf8SignatureInExportedSettings() {
        val user = user()
        val session = EmulatorDataImporter.create(user)
        session.copy("\uFEFF[Core]\nvalue = 1".byteInputStream(), "config/config.ini", proceed)
        assertTrue(EmulatorDataImporter.validate(session).startsWith("[Core]"))
    }

    @Test fun rejectsPartialBackupsAndSkipsDerivedCaches() {
        val user = user()
        val session = EmulatorDataImporter.create(user)
        assertThrows(IOException::class.java) {
            EmulatorDataImporter.unpack(
                session,
                zip("keys/prod.keys" to "keys").inputStream(),
                proceed
            )
        }
        session.copy("cache".byteInputStream(), "cache/old", proceed)
        assertFalse(File(session.payload, "cache/old").exists())
        assertEquals("previous save", File(user, "save/current").readText())
    }
}
