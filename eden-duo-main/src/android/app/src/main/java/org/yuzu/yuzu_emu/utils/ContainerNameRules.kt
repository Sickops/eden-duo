// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

/**
 * The Android-free part of the folder scans (unit tested): which entries of a folder listing are
 * game or content containers. The listing already carries each entry's display name, so the
 * extension comes from that name; asking the documents provider for the name again cost one
 * query per file (about 33 ms each on the AYN Thor, 2.4 s for a folder of 73 files) on every
 * launch of a game that lives in that folder.
 */
object ContainerNameRules {
    /**
     * The lower-cased extension of [filename], exactly as FileUtil.getExtension derives it from
     * the display name: everything after the last dot, or the whole name when there is none.
     */
    fun extension(filename: String): String =
        filename.substring(filename.lastIndexOf('.') + 1).lowercase()
}
