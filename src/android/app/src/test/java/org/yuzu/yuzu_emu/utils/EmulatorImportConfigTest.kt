// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

import java.io.File
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class EmulatorImportConfigTest {
    @get:Rule val temporary = TemporaryFolder()
    private val source = "dev.eden.eden_emulator.dualscreen.debug.user"
    private val own = "dev.igawa6.edenduo.user"
    private val destination = File("/storage/emulated/0/Android/data/dev.igawa6.edenduo/files")
    private val original = "/storage/emulated/0/Android/data/dev.eden.eden_emulator.dualscreen.debug/files"
    private val games = "content://com.android.externalstorage.documents/tree/primary%3ASwitch%2FGames"
    private val skipped = "content://com.android.externalstorage.documents/tree/primary%3ASkipped"
    private val updates = "content://com.android.externalstorage.documents/tree/ABCD-1234%3AUpdates"
    private val config get() = """
        [Core]
        use_multi_core = false
        [Paths]
        gamedirs\size = 3
        gamedirs\1\path = $games
        gamedirs\1\deep_scan = false
        gamedirs\2\path = $skipped
        gamedirs\3\path = content://$source/tree/root%2Fgames
        gamedirs\3\deep_scan = true
        external_content_dirs\size = 1
        external_content_dirs\1\path = $updates
        nand_directory = $original/nand
        sdmc_directory = $original/sdmc
        save_directory = /storage/ABCD-1234/Saves
        [GpuDriver]
        driver_path = $original/gpu_drivers/turnip/libvulkan.so
        [UnknownForkSection]
        custom = keep me
    """.trimIndent()

    @Test fun findsSeparateLibrariesAndCustomSavesAcrossGlobalAndPerGameSettings() {
        val payload = temporary.newFolder("payload")
        File(payload, "config/custom").mkdirs()
        File(payload, "config/config.ini").writeText(config)
        File(payload, "config/custom/game.ini").writeText(
            "[Paths]\nsdmc_directory = /storage/ABCD-1234/Mods\n"
        )
        val folders = EmulatorImportConfig.requestedFolders(payload, source)
        assertEquals(
            setOf(games, skipped, updates, "/storage/ABCD-1234/Saves", "/storage/ABCD-1234/Mods"),
            folders.map { it.original }.toSet()
        )
        assertEquals(
            EmulatorImportConfig.Kind.CONTENT,
            folders.first { it.original == updates }.kind
        )
        assertEquals(
            EmulatorImportConfig.Kind.SAVES,
            folders.first { it.original.endsWith("Saves") }.kind
        )
        assertFalse(folders.any { it.original.startsWith(original) })
    }

    @Test fun rebasesCopiedFoldersAndRemovesSkippedReferencesWithoutLosingSettings() {
        val result = EmulatorImportConfig.rewriteText(
            config,
            destination,
            own,
            source,
            mapOf(
                games to "imported/games",
                updates to "imported/updates",
                "/storage/ABCD-1234/Saves" to "imported/save"
            )
        )
        assertTrue(result.contains("gamedirs\\size = 2"))
        assertTrue(
            result.contains("gamedirs\\1\\path = content://$own/tree/root%2Fimported%2Fgames")
        )
        assertTrue(result.contains("gamedirs\\1\\deep_scan = false"))
        assertTrue(result.contains("gamedirs\\2\\path = content://$own/tree/root%2Fgames"))
        assertTrue(
            result.contains(
                "external_content_dirs\\1\\path = content://$own/tree/root%2Fimported%2Fupdates"
            )
        )
        assertTrue(result.contains("nand_directory = $destination/nand"))
        assertTrue(result.contains("save_directory = $destination/imported/save"))
        assertTrue(result.contains("driver_path = $destination/gpu_drivers/turnip/libvulkan.so"))
        assertTrue(result.contains("use_multi_core = false"))
        assertTrue(result.contains("[UnknownForkSection]\ncustom = keep me"))
        assertFalse(result.contains(skipped))
        assertFalse(result.contains(source))
        assertFalse(result.contains("gamedirs\\3\\"))
    }

    @Test fun additionalFoldersAreIncludedOnlyInGlobalConfig() {
        val payload = temporary.newFolder("payload")
        File(payload, "config/custom").mkdirs()
        val global = File(payload, "config/config.ini").apply { writeText("[Core]\nvalue = 1\n") }
        val perGame = File(payload, "config/custom/game.ini").apply {
            writeText(
                "[Core]\nvalue = 2\n"
            )
        }
        EmulatorImportConfig.rewrite(
            payload,
            destination,
            own,
            null,
            emptyMap(),
            listOf(
                EmulatorImportConfig.Kind.GAMES to "imported/extra games",
                EmulatorImportConfig.Kind.CONTENT to "imported/extra updates"
            )
        )
        assertTrue(global.readText().contains("root%2Fimported%2Fextra%20games"))
        assertTrue(global.readText().contains("external_content_dirs\\size = 1"))
        assertFalse(perGame.readText().contains("extra"))
        assertTrue(perGame.readText().contains("value = 2"))
    }

    @Test fun skipsCustomStorageAndAcceptsProviderRootOrLegacyAbsolutePaths() {
        val result = EmulatorImportConfig.rewriteText(config, destination, own, source, emptyMap())
        assertTrue(result.contains("save_directory = \n"))
        assertTrue(result.contains("gamedirs\\size = 1"))
        assertEquals(
            "nand",
            EmulatorImportConfig.internalRelative(
                "/sdcard/Android/data/org.yuzu.yuzu_emu/files/nand",
                null
            )
        )
        assertEquals(
            "",
            EmulatorImportConfig.internalRelative("content://$source/tree/root%2F", source)
        )
        assertEquals(
            "load/My Mod",
            EmulatorImportConfig.internalRelative(
                "content://$source/document/root%2Fload%2FMy%20Mod",
                source
            )
        )
        assertNull(
            EmulatorImportConfig.internalRelative("content://other.user/tree/root%2Fnand", source)
        )
        assertNull(EmulatorImportConfig.internalRelative(games, source))
    }

    @Test fun rejectsPathsThatEscapeSourceUserFolder() {
        assertThrows(java.io.IOException::class.java) {
            EmulatorImportConfig.internalRelative("$original/../escape", source)
        }
        assertThrows(java.io.IOException::class.java) {
            EmulatorImportConfig.treeUri(own, "../escape")
        }
        assertNull(
            EmulatorImportConfig.internalRelative(
                "content://$source/tree/root%2F..%2Fescape",
                source
            )
        )
    }

    @Test fun readsAndRewritesPublishedDuoUiPathsAndRetainsFolderGrants() {
        val payload = temporary.newFolder("native-format")
        File(payload, "config").mkdirs()
        val native = """
            [Core]
            use_multi_core=true
            [Data%20Storage]
            nand_directory=$original/nand
            [UI]
            Paths\gamedirs\size=1
            Paths\gamedirs\1\path="$games"
            Paths\gamedirs\1\deep_scan=true
            Paths\gamedirs\1\deep_scan\default=true
            Paths\external_content_dirs\size=1
            Paths\external_content_dirs\1\path="$updates"
            Paths\nand_directory=$original/nand
            Paths\save_directory=/storage/ABCD-1234/Saves
        """.trimIndent()
        File(payload, "config/config.ini").writeText(native)
        assertEquals(3, EmulatorImportConfig.requestedFolders(payload, source).size)
        val rewritten = EmulatorImportConfig.rewriteText(native, destination, own, source,
            mapOf(games to games, updates to "imported/updates", "/storage/ABCD-1234/Saves" to "imported/save"))
        assertTrue(rewritten.contains("Paths\\gamedirs\\1\\path = $games"))
        assertTrue(rewritten.contains("Paths\\gamedirs\\1\\deep_scan = false"))
        assertTrue(rewritten.contains("Paths\\external_content_dirs\\1\\path = content://$own/tree/root%2Fimported%2Fupdates"))
        assertTrue(rewritten.contains("Paths\\save_directory = $destination/imported/save"))
        assertFalse(rewritten.contains(original))
        assertTrue(rewritten.contains("use_multi_core=true"))
    }

    @Test fun recognizesPublishedEdenDsStableNightlyAndRenamedForkProviders() {
        assertFalse(EmulatorImportSources.isCompatibleProvider(
            "org.dolphinemu.dolphinemu", "org.dolphinemu.dolphinemu.user",
            "org.dolphinemu.dolphinemu.features.DocumentProvider"))
        listOf("dev.eden.eden_emulator", "dev.eden.eden_emulator.nightly", "dev.eden.eden_emulator.dualscreen.debug", "org.yuzu.yuzu_emu", "org.suyu.suyu_emu", "org.sudachi.sudachi_emu", "org.citron.citron_emu", "org.lemon.lemon_emu", "com.miHoYo.Yuanshen").forEach { pkg ->
            assertTrue(
                pkg,
                EmulatorImportSources.isCompatibleProvider(
                    pkg,
                    "$pkg.user",
                    "org.yuzu.yuzu_emu.features.DocumentProvider"
                )
            )
        }
        assertFalse(
            EmulatorImportSources.isCompatibleProvider(
                "com.android.files",
                "com.android.externalstorage.documents",
                "DocumentsProvider"
            )
        )
        assertFalse(
            EmulatorImportSources.isCompatibleProvider(
                "org.eden.fake",
                "unrelated.user",
                "org.yuzu.yuzu_emu.features.DocumentProvider"
            )
        )
    }
    @Test fun offersDefaultCopiedSavesSeparatelyFromFirmwareAndAllowsSharing() {
        val payload = temporary.newFolder("default-saves")
        File(payload, "nand/user/save").mkdirs()
        File(payload, "config/custom").mkdirs()
        val text = "[UI]\nPaths\\nand_directory=$original/nand\nPaths\\save_directory=\n"
        File(payload, "config/config.ini").writeText(text)
        File(payload, "config/custom/game.ini").writeText("[Core]\nvalue=1\n")
        val choices = EmulatorImportConfig.requestedFolders(payload, source)
        assertEquals(1, choices.size)
        val saves = choices.single()
        assertEquals(EmulatorImportConfig.Kind.SAVES, saves.kind)
        assertEquals("nand", saves.destination)
        val linked = "content://$source/tree/root%2Fnand"
        val rewritten = EmulatorImportConfig.rewriteText(text, destination, own, source,
            mapOf(saves.selectionKey to linked))
        assertTrue(rewritten.contains("Paths\\save_directory = $linked"))
        assertTrue(rewritten.contains("Paths\\nand_directory = $destination/nand"))
        val copied = EmulatorImportConfig.rewriteText(text, destination, own, source,
            mapOf(saves.selectionKey to saves.destination))
        assertTrue(copied.contains("Paths\\save_directory = $destination/nand"))
    }

    @Test fun sharesCustomSaveRootWithoutTurningUriIntoLocalPath() {
        val uri = "content://com.android.externalstorage.documents/tree/primary%3ASaves"
        val rewritten = EmulatorImportConfig.rewriteText(config, destination, own, source,
            mapOf("save:/storage/ABCD-1234/Saves" to uri))
        assertTrue(rewritten.contains("Paths\\save_directory = $uri"))
        assertFalse(rewritten.contains("$destination/content:"))
    }

}
