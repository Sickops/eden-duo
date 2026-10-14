// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import com.fasterxml.jackson.databind.JsonNode
import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.TimeUnit
import okhttp3.OkHttpClient
import okhttp3.Request
import org.yuzu.yuzu_emu.model.DualScreenModRelease

/** Published companion packages, matched by title ID rather than a game's display name. */
object DualScreenModRepository {
    private const val REPOSITORY = "igawa6/eden-duo-companions"
    private const val PAGE_SIZE = 100
    private const val MAX_DOWNLOAD_BYTES = 512L * 1024 * 1024
    private val mapper = jacksonObjectMapper()
    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(30, TimeUnit.SECONDS)
        .callTimeout(5, TimeUnit.MINUTES)
        .build()
    private val assetPattern = Regex("^([0-9a-fA-F]{16})(?:-[A-Za-z0-9._-]+)?\\.dsmod\\.zip$")

    /** Fetch every page: releases for different games share one repository. Call on an IO thread. */
    fun fetch(gameTitleId: String): List<DualScreenModRelease> = fetch(gameTitleId, ::readPage)

    fun fetchAll(): List<DualScreenModRelease> = fetchAll(::readPage)

    private fun readPage(page: Int): String {
        val request = Request.Builder()
            .url("https://api.github.com/repos/$REPOSITORY/releases?per_page=$PAGE_SIZE&page=$page")
            .header("Accept", "application/vnd.github+json")
            .build()
        return client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) throw IOException("GitHub: HTTP ${response.code}")
            response.body?.string() ?: throw IOException("GitHub returned an empty response")
        }
    }

    /** Invalid or all-zero title IDs match nothing: [titleIdForAsset] never returns them. */
    internal fun fetch(gameTitleId: String, readPage: (Int) -> String): List<DualScreenModRelease> =
        fetchAll(readPage).filter {
            titleIdForAsset(it.assetName).equals(
                gameTitleId,
                ignoreCase = true
            )
        }

    internal fun fetchAll(readPage: (Int) -> String): List<DualScreenModRelease> {
        val releases = mutableListOf<DualScreenModRelease>()
        var page = 1
        do {
            val entries = mapper.readTree(readPage(page))
            releases += parseReleases(entries)
            page++
        } while (entries.size() == PAGE_SIZE)
        return releases.distinctBy { it.downloadUrl }.sortedByDescending { it.publishedAt }
    }

    fun titleIdForAsset(name: String): String? = assetPattern.matchEntire(name)?.groupValues?.get(1)
        ?.uppercase()?.takeUnless { titleId -> titleId.all { it == '0' } }

    private fun parseReleases(entries: JsonNode): List<DualScreenModRelease> {
        if (!entries.isArray) throw IOException("GitHub returned an invalid release list")
        return entries.flatMap { release ->
            if (release.path("draft").asBoolean() || release.path("prerelease").asBoolean()) {
                return@flatMap emptyList()
            }
            release.path("assets").mapNotNull { asset ->
                val name = asset.path("name").asText()
                val titleId = titleIdForAsset(name)
                val url = asset.path("browser_download_url").asText()
                if (titleId == null ||
                    !url.startsWith("https://github.com/$REPOSITORY/releases/download/")
                ) {
                    return@mapNotNull null
                }
                val digest = asset.path("digest").asText("")
                DualScreenModRelease(
                    title = release.path("name").asText("").ifBlank {
                        release.path("tag_name").asText()
                    },
                    tag = release.path("tag_name").asText(),
                    notes = release.path("body").asText(""),
                    publishedAt = release.path("published_at").asText(""),
                    assetName = name,
                    downloadUrl = url,
                    size = asset.path("size").asLong(),
                    sha256 = digest.removePrefix("sha256:")
                        .takeIf {
                            digest.startsWith("sha256:") && it.matches(
                                Regex("[0-9a-fA-F]{64}")
                            )
                        }
                )
            }
        }.sortedByDescending { it.publishedAt }
    }

    class DownloadCancelledException : IOException()

    /** Caller deletes the returned temporary file after installation. Failed downloads are removed. */
    fun download(
        release: DualScreenModRelease,
        cacheDirectory: File,
        progressCallback: (max: Long, progress: Long) -> Boolean
    ): File {
        if (progressCallback(release.size, 0)) throw DownloadCancelledException()
        if (release.size > MAX_DOWNLOAD_BYTES) throw IOException("Companion package is too large")
        val archive = File.createTempFile("companion-", ".dsmod.zip", cacheDirectory)
        try {
            val request = Request.Builder().url(release.downloadUrl).build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) throw IOException("GitHub: HTTP ${response.code}")
                val body = response.body ?: throw IOException("GitHub returned an empty download")
                val total = body.contentLength().takeIf { it > 0 } ?: release.size
                if (total > MAX_DOWNLOAD_BYTES) throw IOException("Companion package is too large")
                val digest = MessageDigest.getInstance("SHA-256")
                var copied = 0L
                body.byteStream().use { input ->
                    archive.outputStream().use { output ->
                        val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
                        while (true) {
                            if (progressCallback(total, copied)) throw DownloadCancelledException()
                            val read = input.read(buffer)
                            if (read == -1) break
                            copied += read
                            if (copied > MAX_DOWNLOAD_BYTES) {
                                throw IOException(
                                    "Companion package is too large"
                                )
                            }
                            output.write(buffer, 0, read)
                            digest.update(buffer, 0, read)
                        }
                    }
                }
                if (release.size > 0 && copied != release.size) {
                    throw IOException("Companion download is incomplete; fetch the release again")
                }
                val actual = digest.digest().joinToString("") { "%02x".format(it) }
                if (release.sha256 != null && !actual.equals(release.sha256, ignoreCase = true)) {
                    throw IOException(
                        "Companion download checksum mismatch; fetch the release again"
                    )
                }
                if (progressCallback(total, copied)) throw DownloadCancelledException()
            }
            return archive
        } catch (e: Exception) {
            archive.delete()
            throw e
        }
    }
}
