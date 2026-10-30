// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.junit.Assert.assertEquals
import org.junit.Test

class ContainerNameRulesTest {
    /** The rule FileUtil.getExtension applied to a display name fetched per file. */
    private fun legacy(fileName: String): String =
        fileName.substring(fileName.lastIndexOf(".") + 1).lowercase()

    private val names = listOf(
        "Game [0100000000010000][v0].nsp",
        "Update [0100000000010800][v131072].NSP",
        "dlc.XCI",
        "Archive.tar.xci",
        "no extension",
        ".hidden",
        "trailing.",
        "nsp",
        "folder.name/does.not.matter.nsp",
        "",
        "日本語タイトル.nsp"
    )

    @Test
    fun extensionMatchesTheNameLookupItReplaces() {
        for (name in names) {
            assertEquals(name, legacy(name), ContainerNameRules.extension(name))
        }
    }

    @Test
    fun containerFilterKeepsTheSameFiles() {
        val containers = setOf("nsp", "xci")
        val expected = names.filter { containers.contains(legacy(it)) }
        val actual = names.filter { containers.contains(ContainerNameRules.extension(it)) }
        assertEquals(expected, actual)
        assertEquals(
            listOf(
                "Game [0100000000010000][v0].nsp",
                "Update [0100000000010800][v131072].NSP",
                "dlc.XCI",
                "Archive.tar.xci",
                "nsp", // a name without a dot is its own "extension", as before
                "folder.name/does.not.matter.nsp",
                "日本語タイトル.nsp"
            ),
            actual
        )
    }
}
