// SPDX-License-Identifier: GPL-3.0-or-later
package org.yuzu.yuzu_emu.utils

import com.fasterxml.jackson.module.kotlin.jacksonObjectMapper
import java.io.File
import java.math.BigInteger
import org.yuzu.yuzu_emu.model.DualScreenModRelease

/** Unknown versions never claim an update: manually installed/private builds may be newer. */
object DualScreenModStatus {
    enum class State { MISSING, UPDATE_AVAILABLE, CURRENT, UNKNOWN }
    private val mapper = jacksonObjectMapper()
    private val versionPattern = Regex("""(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?""")

    fun releaseVersion(release: DualScreenModRelease): String? {
        val asset = release.assetName.removeSuffix(".dsmod.zip")
        return Regex("""-(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$""")
            .find(asset)?.groupValues?.get(1)
            ?: Regex("""(?:^|[-v])(\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?)$""")
                .find(release.tag)?.groupValues?.get(1)
    }

    fun compareVersions(left: String, right: String): Int? {
        val a = versionPattern.matchEntire(left) ?: return null
        val b = versionPattern.matchEntire(right) ?: return null
        for (i in 1..3) {
            val difference = BigInteger(a.groupValues[i]).compareTo(BigInteger(b.groupValues[i]))
            if (difference != 0) return difference
        }
        val ap = a.groupValues[4]; val bp = b.groupValues[4]
        if (ap == bp) return 0
        if (ap.isEmpty()) return 1
        if (bp.isEmpty()) return -1
        val aa = ap.split('.'); val bb = bp.split('.')
        for (i in 0 until minOf(aa.size, bb.size)) {
            val an = aa[i].toBigIntegerOrNull(); val bn = bb[i].toBigIntegerOrNull()
            val difference = when {
                an != null && bn != null -> an.compareTo(bn)
                an != null -> -1
                bn != null -> 1
                else -> aa[i].compareTo(bb[i])
            }
            if (difference != 0) return difference
        }
        return aa.size.compareTo(bb.size)
    }

    /** Each entry represents an installed manifest and its optional package metadata. */
    fun evaluate(packages: List<String?>, titleId: String, latest: String?): State {
        if (packages.isEmpty()) return State.MISSING
        val versions = packages.map { metadata ->
            runCatching {
                val json = mapper.readTree(metadata ?: return@runCatching null)
                if (!json.path("title_id").asText().equals(titleId, true) ||
                    json.path("type").asText() != "dual-screen-mod") null
                else json.path("version").asText().takeIf { versionPattern.matches(it) }
            }.getOrNull()
        }
        if (latest == null || !versionPattern.matches(latest)) return State.UNKNOWN
        if (versions.filterNotNull().any { (compareVersions(it, latest) ?: -1) >= 0 }) {
            return State.CURRENT
        }
        if (versions.any { it == null }) return State.UNKNOWN
        return State.UPDATE_AVAILABLE
    }

    fun scan(directory: File, titleId: String, latest: String?): State {
        if (directory.exists() && directory.listFiles() == null) return State.UNKNOWN
        val packages = directory.listFiles().orEmpty().filter {
            !it.name.startsWith(".") && File(it, "dualscreen/manifest.json").isFile
        }.map { folder ->
            runCatching {
                File(folder, "package.json").takeIf { it.length() in 1..1048576 }
                    ?.readText()
            }.getOrNull()
        }
        return evaluate(packages, titleId, latest)
    }
}
