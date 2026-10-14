// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import java.io.File
import java.io.IOException
import java.net.InetAddress
import java.net.ServerSocket
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.concurrent.thread
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.yuzu.yuzu_emu.model.DualScreenModRelease

class DualScreenModRepositoryTest {
    private val mapper = jacksonObjectMapper()
    private val diamond = "0100000011D90000"
    private val pearl = "010018E011D92000"

    private fun release(
        titleId: String = diamond,
        version: String = "1.0.0",
        publishedAt: String = "2026-10-10T23:07:30Z",
        draft: Boolean = false,
        prerelease: Boolean = false,
        assetName: String = "$titleId-Companion-$version.dsmod.zip",
        repository: String = "igawa6/eden-duo-companions"
    ): Map<String, Any> = mapOf(
        "name" to "Companion $version",
        "tag_name" to "companion-$version",
        "body" to "Supports game update 1.3.0",
        "published_at" to publishedAt,
        "draft" to draft,
        "prerelease" to prerelease,
        "assets" to listOf(
            mapOf(
                "name" to assetName,
                "browser_download_url" to
                    "https://github.com/$repository/releases/download/companion-$version/$assetName",
                "size" to 42,
                "digest" to "sha256:${"a".repeat(64)}"
            )
        )
    )

    // Single-page responses (fewer than 100 releases), served without network access.
    private fun parse(json: String) = DualScreenModRepository.fetchAll { json }
    private fun parse(json: String, titleId: String) = DualScreenModRepository.fetch(titleId) { json }

    @Test
    fun editionsMatchByExactTitleIdAndKeepReleaseNotes() {
        val json = mapper.writeValueAsString(listOf(release(), release(titleId = pearl)))
        val matches = parse(json, diamond.lowercase())
        assertEquals(1, matches.size)
        assertTrue(matches.single().assetName.startsWith(diamond))
        assertEquals("Supports game update 1.3.0", matches.single().notes)
        assertEquals("a".repeat(64), matches.single().sha256)
        assertEquals(1, parse(json, pearl).size)
        assertTrue(parse(json, "01007EF00011E000").isEmpty())
        assertTrue(parse(json, "0000000000000000").isEmpty())
    }

    @Test
    fun publishedPackagesExcludeDraftsPreviewsOtherRepositoriesAndNonPackages() {
        val json = mapper.writeValueAsString(
            listOf(
                release(draft = true),
                release(prerelease = true),
                release(repository = "other/companions"),
                release(assetName = "$diamond-Companion-1.0.0.zip"),
                release(assetName = "$diamond-Companion-1.0.0.dsmod.zip.sha256"),
                release(assetName = "${diamond}0-Companion-1.0.0.dsmod.zip"),
                release(assetName = "$diamond.dsmod.zip")
            )
        )
        assertEquals(
            listOf("$diamond.dsmod.zip"),
            parse(json, diamond).map { it.assetName }
        )
    }

    @Test
    fun olderReleasesRemainAvailableAndNewestPublishedComesFirst() {
        val json = mapper.writeValueAsString(
            listOf(
                release(version = "1.0.0", publishedAt = "2026-09-27T05:35:29Z"),
                release(version = "1.1.0", publishedAt = "2026-10-10T23:07:30Z")
            )
        )
        assertEquals(
            listOf("companion-1.1.0", "companion-1.0.0"),
            parse(json, diamond).map { it.tag }
        )
    }

    @Test
    fun supportCatalogFindsEveryPublishedGameWithoutTreatingDraftsOrZeroIdsAsSupported() {
        val json = mapper.writeValueAsString(
            listOf(
                release(),
                release(version = "0.9.0"),
                release(titleId = pearl),
                release(titleId = "01007EF00011E000", draft = true),
                release(titleId = "0000000000000000")
            )
        )
        val ids = parse(json).mapNotNull {
            DualScreenModRepository.titleIdForAsset(it.assetName)
        }.toSet()
        assertEquals(setOf(diamond, pearl), ids)
    }

    @Test
    fun matchingGameOnLaterPageIsFetchedEvenWhenFirstPageHasNoMatches() {
        val pagesRead = mutableListOf<Int>()
        val matches = DualScreenModRepository.fetch(diamond) { page ->
            pagesRead += page
            mapper.writeValueAsString(
                if (page == 1) List(100) { release(titleId = pearl) } else listOf(release())
            )
        }
        assertEquals(listOf(1, 2), pagesRead)
        assertEquals(1, matches.size)
    }

    @Test
    fun apiErrorObjectIsNotReportedAsNoCompanion() {
        assertThrows(IOException::class.java) {
            parse("""{"message":"rate limit exceeded"}""", diamond)
        }
    }

    private fun withDownload(block: (DualScreenModRelease, File, ByteArray) -> Unit) {
        val content = ByteArray(32768) { (it % 251).toByte() }
        val server = ServerSocket(0, 10, InetAddress.getByName("127.0.0.1"))
        val worker = thread(isDaemon = true) {
            while (!server.isClosed) {
                try {
                    server.accept().use { socket ->
                        socket.soTimeout = 5000
                        val reader = socket.getInputStream().bufferedReader()
                        while (!reader.readLine().isNullOrEmpty()) { /* Read request headers. */ }
                        socket.getOutputStream().use { output ->
                            output.write(
                                "HTTP/1.1 200 OK\r\nContent-Length: ${content.size}\r\nConnection: close\r\n\r\n"
                                    .toByteArray()
                            )
                            output.write(content)
                        }
                    }
                } catch (_: IOException) {
                    // A cancelled download closes the socket before all bytes have been sent.
                }
            }
        }
        val cache = Files.createTempDirectory("companion-fetch-test").toFile()
        try {
            val digest = MessageDigest.getInstance("SHA-256").digest(content)
                .joinToString("") { "%02x".format(it) }
            val release = DualScreenModRelease(
                "Companion",
                "1.0.0",
                "",
                "",
                "$diamond-Companion-1.0.0.dsmod.zip",
                "http://127.0.0.1:${server.localPort}/companion",
                content.size.toLong(),
                digest
            )
            block(release, cache, content)
        } finally {
            server.close()
            worker.join(1000)
            cache.deleteRecursively()
        }
    }

    @Test
    fun downloadVerifiesSizeAndDigestAndReportsCompletion() = withDownload { release, cache, content ->
        var lastProgress = 0L
        val file = DualScreenModRepository.download(release, cache) { _, progress ->
            lastProgress = progress
            false
        }
        assertArrayEquals(content, file.readBytes())
        assertEquals(content.size.toLong(), lastProgress)
    }

    @Test
    fun cancelledOrDamagedDownloadLeavesNoTemporaryFiles() = withDownload { release, cache, _ ->
        assertThrows(DualScreenModRepository.DownloadCancelledException::class.java) {
            DualScreenModRepository.download(release, cache) { _, progress -> progress > 0 }
        }
        assertTrue(cache.listFiles().orEmpty().isEmpty())
        assertThrows(IOException::class.java) {
            DualScreenModRepository.download(release.copy(sha256 = "0".repeat(64)), cache) { _, _ -> false }
        }
        assertTrue(cache.listFiles().orEmpty().isEmpty())
        assertThrows(IOException::class.java) {
            DualScreenModRepository.download(release.copy(size = release.size + 1), cache) { _, _ -> false }
        }
        assertTrue(cache.listFiles().orEmpty().isEmpty())
    }
}
