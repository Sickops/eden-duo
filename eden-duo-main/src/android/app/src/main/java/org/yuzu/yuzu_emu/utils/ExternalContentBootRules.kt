// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import kotlinx.serialization.Serializable

/** Title selection for a complete external-content listing. Called under the provider lock. */
internal object ExternalContentBootRules {
    data class ContainerStamp(val uri: String, val size: Long, val modified: Long)

    @Serializable
    data class ContainerTitles(val size: Long, val modified: Long, val titles: List<Long>)

    data class Selection(
        val remembered: Map<String, ContainerTitles>,
        val matches: List<String>,
        val readCount: Int
    )

    fun select(
        baseTitleId: Long,
        stamps: List<ContainerStamp>,
        stored: Map<String, ContainerTitles>,
        readTitles: (String) -> List<Long>
    ): Selection {
        val remembered = mutableMapOf<String, ContainerTitles>()
        val matches = mutableListOf<String>()
        var readCount = 0
        for (stamp in stamps.distinctBy { it.uri }) {
            val previous = stored[stamp.uri]
            // Without both stamps, a same-size replacement or a timestamp collision is unsafe.
            val stamped = stamp.size > 0L && stamp.modified > 0L
            val entry = if (stamped && previous != null && previous.size == stamp.size &&
                previous.modified == stamp.modified
            ) {
                previous
            } else {
                readCount++
                ContainerTitles(stamp.size, stamp.modified, readTitles(stamp.uri).filter {
                    it != 0L
                }.distinct())
            }
            // Missing keys or unreadable containers must be retried on the next boot.
            if (stamped && entry.titles.isNotEmpty()) {
                remembered[stamp.uri] = entry
            }
            if (baseTitleId in entry.titles) {
                matches.add(stamp.uri)
            }
        }
        return Selection(remembered, matches, readCount)
    }
}
