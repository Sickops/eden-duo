// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.yuzu.yuzu_emu.model.DualScreenModRelease
import org.yuzu.yuzu_emu.utils.DualScreenModStatus.State

class DualScreenModStatusTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    private val title = "01007EF00011E000"

    private fun metadata(version: String, titleId: String = title, type: String = "dual-screen-mod") =
        """{"title_id":"$titleId","type":"$type","version":"$version"}"""

    private fun release(asset: String, tag: String = "unknown") = DualScreenModRelease(
        title = "Companion", tag = tag, notes = "", publishedAt = "2026-10-13T00:00:00Z",
        assetName = asset, downloadUrl = "https://example.test/$asset", size = 1, sha256 = null
    )

    @Test
    fun absentManifestMeansMissingEvenWithoutReleaseMetadata() {
        assertEquals(State.MISSING, DualScreenModStatus.evaluate(emptyList(), title, "1.0.0"))
        assertEquals(State.MISSING, DualScreenModStatus.evaluate(emptyList(), title, null))
    }

    @Test
    fun currentAndNewerLocalVersionsDoNotOfferAnUpdate() {
        assertEquals(State.CURRENT, DualScreenModStatus.evaluate(listOf(metadata("1.2.3")), title, "1.2.3"))
        assertEquals(State.CURRENT, DualScreenModStatus.evaluate(listOf(metadata("2.0.0")), title, "1.2.3"))
        assertEquals(State.UPDATE_AVAILABLE, DualScreenModStatus.evaluate(listOf(metadata("1.2.2")), title, "1.2.3"))
    }

    @Test
    fun unknownOrUnrelatedPackageMetadataDoesNotClaimAnUpdate() {
        val unknown = listOf(
            null, "{broken", "{}", metadata("development"),
            metadata("1.0.0", "0100000000000000"), metadata("1.0.0", type = "texture-pack")
        )
        for (packageJson in unknown) {
            assertEquals(packageJson, State.UNKNOWN, DualScreenModStatus.evaluate(listOf(packageJson), title, "2.0.0"))
        }
        assertEquals(State.CURRENT, DualScreenModStatus.evaluate(listOf(metadata("2.0.0", title.lowercase())), title, "2.0.0"))
    }

    @Test
    fun missingOrInvalidLatestVersionKeepsInstalledStatusUnknown() {
        for (latest in listOf(null, "", "unpublished", "v1.0.0", "1.0")) {
            assertEquals(latest, State.UNKNOWN, DualScreenModStatus.evaluate(listOf(metadata("1.0.0")), title, latest))
        }
    }

    @Test
    fun multipleInstalledFoldersAccountForNewerAndUnknownPackages() {
        assertEquals(State.CURRENT, DualScreenModStatus.evaluate(listOf(metadata("1.0.0"), metadata("2.0.0")), title, "2.0.0"))
        assertEquals(State.UNKNOWN, DualScreenModStatus.evaluate(listOf(metadata("1.0.0"), null), title, "2.0.0"))
        assertEquals(State.CURRENT, DualScreenModStatus.evaluate(listOf(null, metadata("2.0.0")), title, "2.0.0"))
        assertEquals(State.UPDATE_AVAILABLE, DualScreenModStatus.evaluate(listOf(metadata("1.0.0"), metadata("1.5.0")), title, "2.0.0"))
    }

    @Test
    fun versionsCompareNumericComponentsInsteadOfLexicalOrder() {
        assertTrue(DualScreenModStatus.compareVersions("1.10.0", "1.9.0")!! > 0)
        assertTrue(DualScreenModStatus.compareVersions("1.9.0", "1.10.0")!! < 0)
        assertTrue(DualScreenModStatus.compareVersions("999999999999999999999.0.0", "2.0.0")!! > 0)
        assertNull(DualScreenModStatus.compareVersions("nightly", "1.0.0"))
        assertNull(DualScreenModStatus.compareVersions("1.0.0", "1.0"))
    }

    @Test
    fun prereleaseOrderingFollowsSemverAndBuildMetadataIsIgnored() {
        val versions = listOf(
            "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
            "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"
        )
        versions.zipWithNext().forEach { (older, newer) ->
            assertTrue("$older < $newer", DualScreenModStatus.compareVersions(older, newer)!! < 0)
        }
        assertEquals(0, DualScreenModStatus.compareVersions("1.0.0+first", "1.0.0+second"))
        assertEquals(0, DualScreenModStatus.compareVersions("1.0.0-beta.2+first", "1.0.0-beta.2+second"))
        assertEquals(State.UPDATE_AVAILABLE, DualScreenModStatus.evaluate(listOf(metadata("1.0.0-rc.1")), title, "1.0.0"))
    }

    @Test
    fun publishedAssetVersionTakesPrecedenceAndTagsAreFallback() {
        assertEquals("1.10.0", DualScreenModStatus.releaseVersion(release("game-$title-1.10.0.dsmod.zip", "v1.9.0")))
        assertEquals("1.2.3-rc.2+build.7", DualScreenModStatus.releaseVersion(release("game-1.2.3-rc.2+build.7.dsmod.zip")))
        for (tag in listOf("1.2.3", "v1.2.3", "game-v1.2.3", "game-1.2.3")) {
            assertEquals(tag, "1.2.3", DualScreenModStatus.releaseVersion(release("companion.dsmod.zip", tag)))
        }
        assertNull(DualScreenModStatus.releaseVersion(release("companion.dsmod.zip", "nightly")))
    }

    private fun installed(root: File, name: String, packageJson: String? = metadata("1.0.0")) {
        val folder = File(root, name)
        File(folder, "dualscreen").mkdirs()
        File(folder, "dualscreen/manifest.json").writeText("{}")
        packageJson?.let { File(folder, "package.json").writeText(it) }
    }

    @Test
    fun scansIgnoreStagingAndFoldersWithoutAnInstalledManifest() {
        val root = temporaryFolder.newFolder("mods")
        installed(root, ".staging-download", metadata("3.0.0"))
        File(root, "ordinary-mod").mkdirs()
        File(root, "ordinary-mod/package.json").writeText(metadata("3.0.0"))
        assertEquals(State.MISSING, DualScreenModStatus.scan(root, title, "2.0.0"))
        installed(root, "companion", metadata("1.0.0"))
        assertEquals(State.UPDATE_AVAILABLE, DualScreenModStatus.scan(root, title, "2.0.0"))
        File(root, "companion/package.json").writeText(metadata("2.0.0"))
        assertEquals(State.CURRENT, DualScreenModStatus.scan(root, title, "2.0.0"))
    }

    @Test
    fun scansHandleMissingMalformedAndOversizedMetadataConservatively() {
        assertEquals(State.MISSING, DualScreenModStatus.scan(File(temporaryFolder.root, "absent"), title, "2.0.0"))
        val root = temporaryFolder.newFolder("manual-mods")
        installed(root, "companion", null)
        assertEquals(State.UNKNOWN, DualScreenModStatus.scan(root, title, "2.0.0"))
        val packageFile = File(root, "companion/package.json")
        packageFile.writeText("not json")
        assertEquals(State.UNKNOWN, DualScreenModStatus.scan(root, title, "2.0.0"))
        packageFile.writeBytes(ByteArray(1048577))
        assertEquals(State.UNKNOWN, DualScreenModStatus.scan(root, title, "2.0.0"))
    }
}
