// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.content.SharedPreferences
import android.net.Uri
import android.provider.DocumentsContract
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.preference.PreferenceManager
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.utils.ExternalContentBootRules
import org.yuzu.yuzu_emu.utils.ExternalContentBootRules.ContainerStamp
import org.yuzu.yuzu_emu.utils.ExternalContentBootRules.ContainerTitles
import org.yuzu.yuzu_emu.utils.FileUtil
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.utils.NativeConfig

/**
 * Eden Duo: the game library's update/DLC bookkeeping for GameHelper, so a game that boots before
 * (or without) a full library scan still gets its own update and DLC registered.
 *
 * - The data-import lock keeps an emulator data import from running under a library scan or a
 *   boot.
 * - The content index remembers, per base title id, which update/DLC containers the last full
 *   scan found; a boot registers the booting game's containers from it.
 * - For external content folders a boot registers only the containers that carry the booting
 *   title, matched by size and modified time against a stored title list, instead of opening
 *   every container (which over a network share takes minutes).
 */
object GameLibraryContent {
    @PublishedApi
    internal val dataImportLock = ReentrantReadWriteLock()

    inline fun <T> withDataImportRead(action: () -> T): T = dataImportLock.read(action)

    inline fun <T> withDataImportWrite(action: () -> T): T = dataImportLock.write(action)

    private const val KEY_GAME_CONTENT_CONTAINERS = "GameContentContainers"

    // Which base titles each external-content container carries, keyed by container uri and
    // checked against its positive size and modified time. A boot from a cold app (a front-end launch)
    // then registers only the booting game's update/DLC containers instead of opening and parsing
    // every container in every external content folder, which over a network share takes minutes.
    private const val KEY_EXTERNAL_CONTAINER_TITLES = "ExternalContainerTitles"

    // FileSys::GetBaseTitleID: updates (+0x800), DLC (+0x1000 + n) and extra programs of a
    // multi-program title all share their base game's id once the low 13 bits are cleared.
    private const val BASE_TITLE_ID_MASK = 0x1FFFL.inv()

    // Update/DLC containers seen by the last full library scan, keyed by base title id. The scan
    // registers games-folder containers one by one as it walks, so a game booted mid-scan (cold
    // start from the cached list, a shortcut or a front-end) re-registers its own from here.
    @Serializable
    data class ContentContainer(val uri: String, val gameFolder: Boolean)

    @Volatile
    private var contentContainers: Map<Long, List<ContentContainer>>? = null

    /** The containers one library scan finds, keyed by base title id. */
    class ScanIndex {
        private val containers = mutableMapOf<Long, MutableSet<ContentContainer>>()

        /** The finished scan's index; read only once the scan is complete. */
        internal val containersByTitle: Map<Long, List<ContentContainer>> by lazy {
            containers.mapValues { it.value.toList() }
        }

        /**
         * Records [uri] for the base title of [programId]; with [programId] 0 (a DLC-only
         * container), for every title the container carries.
         */
        fun record(uri: String, programId: Long, gameFolder: Boolean) {
            val baseTitleIds = if (programId != 0L) {
                longArrayOf(programId and BASE_TITLE_ID_MASK)
            } else {
                // DLC-only containers have no program id; read the title ids from the container.
                NativeLibrary.getContainerBaseTitleIds(uri)
            }
            baseTitleIds.forEach {
                if (it != 0L) {
                    containers.getOrPut(it) { linkedSetOf() }.add(ContentContainer(uri, gameFolder))
                }
            }
        }
    }

    /** Stores a finished scan's index in [editor], with the scan's other results. */
    fun store(editor: SharedPreferences.Editor, index: ScanIndex) {
        editor.putString(KEY_GAME_CONTENT_CONTAINERS, Json.encodeToString(index.containersByTitle))
    }

    /** The stored index of a finished scan is now the current one. */
    fun remember(index: ScanIndex) {
        contentContainers = index.containersByTitle
    }

    /** An emulator data import replaced the library: everything remembered is dropped. */
    fun forget(editor: SharedPreferences.Editor) {
        contentContainers = null
        editor.remove(KEY_GAME_CONTENT_CONTAINERS)
        editor.remove(KEY_EXTERNAL_CONTAINER_TITLES)
    }

    /**
     * Registers the booting game's update/DLC. [externalContentMounted] says whether every
     * external content container is registered already and [libraryScanCompleted] whether a full
     * scan has walked every games folder since the provider was last cleared. [mountAllExternal]
     * and [mountGameFolder] are GameHelper's registration walks, [externalContentExtensions] the
     * container extensions.
     */
    fun registerContentForBoot(
        gamePath: String,
        programId: String,
        externalContentMounted: Boolean,
        libraryScanCompleted: Boolean,
        externalContentExtensions: Set<String>,
        mountAllExternal: (MutableSet<String>) -> Unit,
        mountGameFolder: (Uri, MutableSet<String>) -> Unit
    ) {
        val mountedContainerUris = mutableSetOf<String>()
        val baseTitleId = (programId.toLongOrNull() ?: 0L) and BASE_TITLE_ID_MASK
        if (!externalContentMounted) {
            // Only this game's containers are registered here, so externalContentMounted stays
            // false and the next library scan still mounts every folder.
            val selective = baseTitleId != 0L &&
                mountExternalContentForTitle(
                    baseTitleId,
                    mountedContainerUris,
                    externalContentExtensions
                )
            if (!selective) {
                mountAllExternal(mountedContainerUris)
            }
        }
        val externalCount = mountedContainerUris.size

        val known = if (baseTitleId != 0L) {
            loadContentContainers()[baseTitleId].orEmpty()
        } else {
            emptyList()
        }
        val registered = mutableListOf<String>()
        known.forEach {
            if (it.uri != gamePath && mountedContainerUris.add(it.uri)) {
                if (it.gameFolder) {
                    NativeLibrary.addGameFolderFileToFilesystemProvider(it.uri)
                } else {
                    NativeLibrary.addFileToFilesystemProvider(it.uri)
                }
                registered.add(it.uri)
            }
        }
        val beforeSiblings = mountedContainerUris.size
        // Before the first scan, or for a game the scan has no content for (a frontend launch of a
        // game outside the library folders), its own folder is the only place left to look.
        if (!libraryScanCompleted || known.isEmpty()) {
            mountGameFolder(Uri.parse(gamePath), mountedContainerUris)
        }

        Log.info(
            "[GameHelper] Boot content for ${baseTitleId.toString(16).padStart(16, '0')}: " +
                "${registered.size} known container(s) $registered, " +
                "${mountedContainerUris.size - beforeSiblings} folder sibling(s), " +
                "$externalCount external container(s)"
        )
    }

    private fun loadContentContainers(): Map<Long, List<ContentContainer>> {
        contentContainers?.let { return it }
        val stored = PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)
            .getString(KEY_GAME_CONTENT_CONTAINERS, null)
        val loaded = stored?.let {
            try {
                Json.decodeFromString<Map<Long, List<ContentContainer>>>(it)
            } catch (_: Exception) {
                null
            }
        } ?: emptyMap()
        contentContainers = loaded
        return loaded
    }

    /**
     * Registers only the external-content containers that carry content for [baseTitleId].
     * Containers already seen with the same size and modified time are matched from the stored
     * title list without being opened; new or changed ones are read once and remembered.
     * Returns false, having registered nothing, when a folder can't be listed, so the caller
     * falls back to registering every container.
     */
    private fun mountExternalContentForTitle(
        baseTitleId: Long,
        mountedContainerUris: MutableSet<String>,
        externalContentExtensions: Set<String>
    ): Boolean {
        val startedAt = System.currentTimeMillis()
        val stamps = mutableListOf<ContainerStamp>()
        val uniqueExternalContentDirs = linkedSetOf<String>()
        NativeConfig.getExternalContentDirs().forEach { externalDir ->
            if (externalDir.isNotEmpty()) {
                uniqueExternalContentDirs.add(externalDir)
            }
        }
        for (externalDir in uniqueExternalContentDirs) {
            val externalDirUri = externalDir.toUri()
            if (!listContainerStamps(externalDirUri, stamps, externalContentExtensions)) {
                return false
            }
        }

        val stored = loadExternalContainerTitles()
        val selection = try {
            ExternalContentBootRules.select(baseTitleId, stamps, stored) {
                NativeLibrary.getContainerBaseTitleIds(it).toList()
            }
        } catch (e: Exception) {
            Log.warning("[GameHelper] Cannot read external content titles: ${e.message}")
            return false
        }
        val remembered = selection.remembered
        val matches = selection.matches
        if (remembered != stored) {
            PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext).edit {
                putString(KEY_EXTERNAL_CONTAINER_TITLES, Json.encodeToString(remembered))
            }
        }

        matches.forEach {
            if (mountedContainerUris.add(it)) {
                NativeLibrary.addFileToFilesystemProvider(it)
            }
        }
        Log.info(
            "[GameHelper] External content for ${baseTitleId.toString(16).padStart(16, '0')}: " +
                "${stamps.size} listed, ${selection.readCount} read, ${matches.size} registered " +
                "in ${System.currentTimeMillis() - startedAt} ms"
        )
        return true
    }

    private fun loadExternalContainerTitles(): Map<String, ContainerTitles> {
        val stored = PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)
            .getString(KEY_EXTERNAL_CONTAINER_TITLES, null) ?: return emptyMap()
        return try {
            Json.decodeFromString<Map<String, ContainerTitles>>(stored)
        } catch (_: Exception) {
            emptyMap()
        }
    }

    /**
     * Collects the nsp/xci files under [folderUri] with their size and modified time, walking as
     * deep as GameHelper's external content walk does. [folderUri] is a tree root or a document
     * inside a tree, as FileUtil.listFiles accepts. Returns false if any folder query fails.
     */
    private fun listContainerStamps(
        folderUri: Uri,
        stamps: MutableList<ContainerStamp>,
        externalContentExtensions: Set<String>
    ): Boolean {
        return try {
            val folderDocumentId = if (FileUtil.isRootTreeUri(folderUri)) {
                DocumentsContract.getTreeDocumentId(folderUri)
            } else {
                DocumentsContract.getDocumentId(folderUri)
            }
            collectContainerStamps(folderUri, folderDocumentId, 3, stamps, externalContentExtensions)
        } catch (e: Exception) {
            Log.warning("[GameHelper] Cannot list external content folder: ${e.message}")
            false
        }
    }

    private fun collectContainerStamps(
        treeUri: Uri,
        parentDocumentId: String,
        depth: Int,
        stamps: MutableList<ContainerStamp>,
        externalContentExtensions: Set<String>
    ): Boolean {
        if (depth <= 0) {
            return true
        }
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED
        )
        val childrenUri =
            DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocumentId)
        val folders = mutableListOf<String>()
        val cursor = YuzuApplication.appContext.contentResolver
            .query(childrenUri, columns, null, null, null) ?: return false
        cursor.use {
            while (cursor.moveToNext()) {
                val documentId = cursor.getString(0)
                val name = cursor.getString(1) ?: ""
                if (cursor.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) {
                    folders.add(documentId)
                } else if (externalContentExtensions.contains(
                        name.substringAfterLast('.', "").lowercase()
                    )
                ) {
                    val uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId)
                    val size = if (cursor.isNull(3)) 0L else cursor.getLong(3)
                    val modified = if (cursor.isNull(4)) 0L else cursor.getLong(4)
                    stamps.add(ContainerStamp(uri.toString(), size, modified))
                }
            }
        }
        return folders.all {
            collectContainerStamps(treeUri, it, depth - 1, stamps, externalContentExtensions)
        }
    }
}
