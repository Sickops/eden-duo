// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import java.io.File
import java.security.MessageDigest
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.GameMetadata

/**
 * Eden Duo: game icons kept on disk, so a fresh start shows the library's icons at once instead
 * of decoding every game file again (Coil only had a memory cache). Keyed by path, program id and
 * version; GameIconUtils.refreshGameIcon rewrites an entry when the game's icon changes.
 */
object GameIconCache {
    private val directory: File by lazy {
        File(YuzuApplication.appContext.cacheDir, "game-icons").apply { mkdirs() }
    }

    private fun fileFor(game: Game): File {
        val digest = MessageDigest.getInstance("SHA-1")
            .digest("${game.path}|${game.programId}|${game.version}".toByteArray())
        return File(directory, digest.joinToString("") { "%02x".format(it) } + ".icon")
    }

    /** The cached icon, else the icon read from the game file (cached for next time). */
    fun read(game: Game): ByteArray {
        val file = fileFor(game)
        runCatching { file.readBytes() }.getOrNull()?.takeIf { it.isNotEmpty() }?.let { return it }
        val data = runCatching { NativeLibrary.readGameIcon(game.path) }.getOrNull()
            ?.takeIf { it.isNotEmpty() }
            ?: GameMetadata.getIcon(game.path)
        store(game, data)
        return data
    }

    /** Writes [data] as the cached icon of [game] when it differs from what is stored. */
    fun store(game: Game, data: ByteArray) {
        if (data.isEmpty()) return
        val file = fileFor(game)
        runCatching {
            val same = file.isFile && file.length() == data.size.toLong() &&
                file.readBytes().contentEquals(data)
            if (same) return
            val temp = File(directory, file.name + ".tmp")
            temp.writeBytes(data)
            if (!temp.renameTo(file)) temp.delete()
        }
    }
}
