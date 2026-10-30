// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import java.util.concurrent.Executors
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.yuzu.yuzu_emu.utils.ExternalContentBootRules.ContainerStamp
import org.yuzu.yuzu_emu.utils.ExternalContentBootRules.ContainerTitles

class ExternalContentBootRulesTest {
    private val titleA = 0x01007EF00011E000L
    private val titleB = 0x0100000000010000L
    private val stamps = listOf(
        ContainerStamp("update-a", 100, 200),
        ContainerStamp("dlc-a", 300, 400),
        ContainerStamp("update-b", 500, 600)
    )
    private val titles = mapOf(
        "update-a" to listOf(titleA),
        "dlc-a" to listOf(titleA),
        "update-b" to listOf(titleB)
    )

    @Test
    fun coldBootReadsAllOnceAndSubsequentTitleSwitchReadsNone() {
        val first = ExternalContentBootRules.select(titleA, stamps, emptyMap()) {
            titles.getValue(it)
        }
        assertEquals(listOf("update-a", "dlc-a"), first.matches)
        assertEquals(3, first.readCount)
        val next = ExternalContentBootRules.select(titleB, stamps, first.remembered) {
            error("unchanged container must not be reopened: $it")
        }
        assertEquals(listOf("update-b"), next.matches)
        assertEquals(0, next.readCount)
    }

    @Test
    fun serializedCacheSurvivesAColdProcessWithLargeTitleIds() {
        val first = ExternalContentBootRules.select(titleA, stamps, emptyMap()) {
            titles.getValue(it)
        }
        val restored = Json.decodeFromString<Map<String, ContainerTitles>>(
            Json.encodeToString(first.remembered)
        )
        assertEquals(first.remembered, restored)
        val next = ExternalContentBootRules.select(titleB, stamps, restored) {
            error("persistent cache lost its metadata or title precision")
        }
        assertEquals(listOf("update-b"), next.matches)
        assertEquals(0, next.readCount)
    }

    @Test
    fun sizeAndTimestampIndependentlyInvalidateAndDeletedContainersArePruned() {
        val original = ExternalContentBootRules.select(titleA, stamps, emptyMap()) {
            titles.getValue(it)
        }
        val changed = listOf(stamps[0].copy(size = 101), stamps[1].copy(modified = 401))
        val reads = mutableListOf<String>()
        val next = ExternalContentBootRules.select(titleA, changed, original.remembered) {
            reads.add(it)
            listOf(titleB) // Replacing a file can change its title, despite a stable URI.
        }
        assertEquals(listOf("update-a", "dlc-a"), reads)
        assertTrue(next.matches.isEmpty())
        assertEquals(setOf("update-a", "dlc-a"), next.remembered.keys)
    }

    @Test
    fun missingAndNonpositiveMetadataNeverSuppressesARead() {
        for ((size, modified) in listOf(0L to 0L, -1L to -1L)) {
            val unknown = listOf(ContainerStamp("unknown", size, modified))
            val first = ExternalContentBootRules.select(titleA, unknown, emptyMap()) {
                listOf(titleA)
            }
            assertEquals(listOf("unknown"), first.matches)
            assertTrue(first.remembered.isEmpty())
            val second = ExternalContentBootRules.select(titleB, unknown, first.remembered) {
                listOf(titleB)
            }
            assertEquals(listOf("unknown"), second.matches)
            assertEquals(1, second.readCount)
        }
    }

    @Test
    fun sizeOnlyAndTimestampOnlyMustRereadEvenWithAnExistingCacheEntry() {
        for ((size, modified) in listOf(1L to 0L, 0L to 1L)) {
            val stamp = listOf(ContainerStamp("one-stamp", size, modified))
            val stored = mapOf("one-stamp" to ContainerTitles(size, modified, listOf(titleA)))
            val result = ExternalContentBootRules.select(titleA, stamp, stored) {
                listOf(titleB) // Same-size replacement or unchanged timestamp, different title.
            }
            assertEquals(1, result.readCount)
            assertTrue(result.matches.isEmpty())
            assertTrue(result.remembered.isEmpty())
            val next = ExternalContentBootRules.select(titleB, stamp, result.remembered) {
                listOf(titleB)
            }
            assertEquals(listOf("one-stamp"), next.matches)
            assertEquals(1, next.readCount)
        }
    }

    @Test
    fun unreadableOrMissingKeysRetriesAndOverlappingRootsReadOnlyOnce() {
        val first = ExternalContentBootRules.select(titleA, stamps + stamps, emptyMap()) {
            if (it == "update-a") listOf(0L) else titles.getValue(it)
        }
        assertEquals(3, first.readCount)
        assertEquals(listOf("dlc-a"), first.matches)
        val next = ExternalContentBootRules.select(titleA, stamps, first.remembered) {
            assertEquals("update-a", it)
            listOf(titleA, titleB, titleA)
        }
        assertEquals(1, next.readCount)
        assertEquals(listOf("update-a", "dlc-a"), next.matches)
        assertEquals(listOf(titleA, titleB), next.remembered.getValue("update-a").titles)
    }

    @Test
    fun emptyListingDropsOldCacheAndReadFailureDoesNotMutateStoredCache() {
        val stored = mapOf("gone" to ContainerTitles(1, 1, listOf(titleA)))
        assertTrue(ExternalContentBootRules.select(titleA, emptyList(), stored) {
            error("empty listing")
        }.remembered.isEmpty())
        try {
            ExternalContentBootRules.select(titleA, stamps, stored) {
                throw IllegalStateException("provider failed")
            }
            error("exception must reach caller for full-scan fallback")
        } catch (_: IllegalStateException) {
            assertEquals(mapOf("gone" to ContainerTitles(1, 1, listOf(titleA))), stored)
        }
    }

    @Test
    fun concurrentSelectionsKeepTitlesAndSnapshotsIndependent() {
        val stored = ExternalContentBootRules.select(titleA, stamps, emptyMap()) {
            titles.getValue(it)
        }.remembered
        val pool = Executors.newFixedThreadPool(4)
        try {
            val requests = (0 until 32).map { i ->
                pool.submit<List<String>> {
                    ExternalContentBootRules.select(
                        if (i % 2 == 0) titleA else titleB, stamps, stored
                    ) { error("cache must remain valid") }.matches
                }
            }
            requests.forEachIndexed { i, result ->
                assertEquals(
                    if (i % 2 == 0) listOf("update-a", "dlc-a") else listOf("update-b"),
                    result.get()
                )
            }
            assertEquals(3, stored.size)
        } finally {
            pool.shutdownNow()
        }
    }
}
