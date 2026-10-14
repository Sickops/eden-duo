// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import android.net.Uri
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.preference.PreferenceManager
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import kotlinx.serialization.encodeToString
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.utils.GameHelper
import org.yuzu.yuzu_emu.utils.DualScreenModRepository
import org.yuzu.yuzu_emu.utils.DualScreenModStatus
import java.io.File
import org.yuzu.yuzu_emu.utils.NativeConfig
import java.util.concurrent.atomic.AtomicBoolean

class GamesViewModel : ViewModel() {
    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)

    private var lastImportGeneration = preferences.getLong(
        EmulatorImportViewModel.IMPORT_GENERATION,
        0
    )

    fun refreshAfterEmulatorImport() {
        val generation = preferences.getLong(EmulatorImportViewModel.IMPORT_GENERATION, 0)
        if (generation != lastImportGeneration && !reloading.get()) {
            lastImportGeneration = generation
            getGameDirsAndExternalContent()
            reloadGames(directoriesChanged = true)
        }
    }

    private val _dualScreenTitleIds = MutableStateFlow(
        preferences.getStringSet(DUAL_SCREEN_TITLE_IDS, null)?.toSet()
            ?: YuzuApplication.appContext.resources
                .getStringArray(R.array.dual_screen_supported_title_ids).toSet()
    )
    val dualScreenTitleIds = _dualScreenTitleIds.asStateFlow()
    private val _dualScreenInstallStates = MutableStateFlow(emptyMap<String, DualScreenModStatus.State>())
    val dualScreenInstallStates = _dualScreenInstallStates.asStateFlow()
    private var statusJob: kotlinx.coroutines.Job? = null
    private var latestVersions: Map<String, String> = runCatching {
        Json.decodeFromString<Map<String, String>>(preferences.getString("DualScreenVersions", "{}")!!)
    }.getOrDefault(emptyMap())

    private fun refreshInstalledCompanions() {
        viewModelScope.launch {
            statusJob?.cancel()
            statusJob = viewModelScope.launch {
                val supported = _dualScreenTitleIds.value
                val versions = latestVersions
                val library = _games.value.toList()
                val states = withContext(Dispatchers.IO) {
                    library.filter { it.programIdHex in supported }.associate { game ->
                        val state = runCatching {
                            val path = game.addonDir
                            if (path.startsWith("content://")) {
                                // The installer cannot write to a linked folder, so never offer a
                                // download/update there; an installed current companion still shows.
                                DualScreenModStatus.evaluate(
                                    org.yuzu.yuzu_emu.utils.DocumentFileAccess.companionPackages(path),
                                    game.programIdHex, versions[game.programIdHex]
                                ).takeIf { it == DualScreenModStatus.State.CURRENT }
                                    ?: DualScreenModStatus.State.UNKNOWN
                            } else DualScreenModStatus.scan(File(path), game.programIdHex, versions[game.programIdHex])
                        }.getOrDefault(DualScreenModStatus.State.UNKNOWN)
                        game.programIdHex to state
                    }
                }
                _dualScreenInstallStates.value = states
            }
        }
    }

    private val catalogRefreshing = AtomicBoolean(false)
    private var lastCatalogAttempt = 0L

    /** Show cached support immediately and refresh once per minute when the library opens. */
    fun refreshDualScreenSupport(force: Boolean = false) {
        refreshInstalledCompanions()
        val now = android.os.SystemClock.elapsedRealtime()
        if ((!force && lastCatalogAttempt != 0L && now - lastCatalogAttempt < 60_000L) ||
            !catalogRefreshing.compareAndSet(false, true)
        ) {
            return
        }
        lastCatalogAttempt = now
        viewModelScope.launch {
            try {
                val releases = withContext(Dispatchers.IO) { DualScreenModRepository.fetchAll() }
                val latest = releases.groupBy { DualScreenModRepository.titleIdForAsset(it.assetName) }
                    .filterKeys { it != null }.mapValues { it.value.first() }
                val ids = latest.keys.filterNotNull().toSet()
                latestVersions = latest.entries.mapNotNull { (id, release) ->
                    DualScreenModStatus.releaseVersion(release)?.let { id!! to it }
                }.toMap()
                preferences.edit().putStringSet(DUAL_SCREEN_TITLE_IDS, ids)
                    .putString("DualScreenVersions", Json.encodeToString(latestVersions)).apply()
                _dualScreenTitleIds.value = ids
                refreshInstalledCompanions()
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (_: Exception) {
                // Keep the last successful catalog when offline or GitHub is unavailable.
            } finally {
                catalogRefreshing.set(false)
            }
        }
    }

    private companion object {
        const val DUAL_SCREEN_TITLE_IDS = "DualScreenCompanionTitleIds"
    }

    val games: StateFlow<List<Game>> get() = _games
    private val _games = MutableStateFlow(emptyList<Game>())

    val isReloading: StateFlow<Boolean> get() = _isReloading
    private val _isReloading = MutableStateFlow(false)

    private val reloading = AtomicBoolean(false)

    val shouldSwapData: StateFlow<Boolean> get() = _shouldSwapData
    private val _shouldSwapData = MutableStateFlow(false)

    val shouldScrollToTop: StateFlow<Boolean> get() = _shouldScrollToTop
    private val _shouldScrollToTop = MutableStateFlow(false)

    val searchFocused: StateFlow<Boolean> get() = _searchFocused
    private val _searchFocused = MutableStateFlow(false)

    val shouldScrollAfterReload: StateFlow<Boolean> get() = _shouldScrollAfterReload
    private val _shouldScrollAfterReload = MutableStateFlow(false)

    private val _folders = MutableStateFlow(mutableListOf<GameDir>())
    val folders = _folders.asStateFlow()

    private val _filteredGames = MutableStateFlow<List<Game>>(emptyList())

    var lastScrollPosition: Int = 0

    init {
        // Ensure keys are loaded so that ROM metadata can be decrypted.
        NativeLibrary.reloadKeys()

        getGameDirsAndExternalContent()
        reloadGames(directoriesChanged = false, firstStartup = true)
    }

    fun setGames(games: List<Game>) {
        val sortedList = games.sortedWith(
            compareBy(
                { it.title.lowercase(Locale.getDefault()) },
                { it.path }
            )
        )

        _games.value = sortedList
        refreshInstalledCompanions()
    }

    fun setShouldSwapData(shouldSwap: Boolean) {
        _shouldSwapData.value = shouldSwap
    }

    fun setShouldScrollToTop(shouldScroll: Boolean) {
        _shouldScrollToTop.value = shouldScroll
    }

    fun setShouldScrollAfterReload(shouldScroll: Boolean) {
        _shouldScrollAfterReload.value = shouldScroll
    }

    fun setSearchFocused(searchFocused: Boolean) {
        _searchFocused.value = searchFocused
    }

    fun setFilteredGames(games: List<Game>) {
        _filteredGames.value = games
    }

    fun reloadGames(directoriesChanged: Boolean, firstStartup: Boolean = false) {
        if (reloading.get()) {
            return
        }
        reloading.set(true)
        _isReloading.value = true

        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                try {
                    if (firstStartup) {
                        // Retrieve list of cached games
                        val storedGames =
                            PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)
                                .getStringSet(GameHelper.KEY_GAMES, emptySet())
                        if (storedGames!!.isNotEmpty()) {
                            val deserializedGames = mutableSetOf<Game>()
                            storedGames.forEach {
                                val game: Game
                                try {
                                    game = Json.decodeFromString(it)
                                } catch (e: Exception) {
                                    // We don't care about any errors related to parsing the game cache
                                    return@forEach
                                }

                                val gameExists =
                                    DocumentFile.fromSingleUri(
                                        YuzuApplication.appContext,
                                        Uri.parse(game.path)
                                    )?.exists()
                                if (gameExists == true) {
                                    deserializedGames.add(game)
                                }
                            }
                            setGames(deserializedGames.toList())
                        }
                    }

                    setGames(GameHelper.getGames())
                    _shouldScrollAfterReload.value = true

                    if (directoriesChanged) {
                        setShouldSwapData(true)
                    }
                } finally {
                    reloading.set(false)
                    _isReloading.value = false
                }
            }
        }
    }

    fun addFolder(gameDir: GameDir, savedFromGameFragment: Boolean) =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                when (gameDir.type) {
                    DirectoryType.GAME -> {
                        NativeConfig.addGameDir(gameDir)
                        val isFirstTimeSetup = PreferenceManager.getDefaultSharedPreferences(YuzuApplication.appContext)
                            .getBoolean(org.yuzu.yuzu_emu.features.settings.model.Settings.PREF_FIRST_APP_LAUNCH, true)
                        getGameDirsAndExternalContent(!isFirstTimeSetup)
                    }
                    DirectoryType.EXTERNAL_CONTENT -> {
                        addExternalContentDir(gameDir.uriString)
                        NativeConfig.saveGlobalConfig()
                        getGameDirsAndExternalContent()
                    }
                }
            }

            if (savedFromGameFragment) {
                NativeConfig.saveGlobalConfig()
                Toast.makeText(
                    YuzuApplication.appContext,
                    R.string.add_directory_success,
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

    fun removeFolder(gameDir: GameDir) =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                val gameDirs = _folders.value.toMutableList()
                val removedDirIndex = gameDirs.indexOf(gameDir)
                if (removedDirIndex != -1) {
                    gameDirs.removeAt(removedDirIndex)
                    when (gameDir.type) {
                        DirectoryType.GAME -> {
                            NativeConfig.setGameDirs(gameDirs.filter { it.type == DirectoryType.GAME }.toTypedArray())
                        }
                        DirectoryType.EXTERNAL_CONTENT -> {
                            removeExternalContentDir(gameDir.uriString)
                        }
                    }
                    getGameDirsAndExternalContent()
                }
            }
        }

    fun updateGameDirs() =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                val gameDirs = _folders.value.filter { it.type == DirectoryType.GAME }
                NativeConfig.setGameDirs(gameDirs.toTypedArray())
                getGameDirsAndExternalContent()
            }
        }

    fun onOpenGameFoldersFragment() =
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                getGameDirsAndExternalContent()
            }
        }

    fun onCloseGameFoldersFragment() {
        NativeConfig.saveGlobalConfig()
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                getGameDirsAndExternalContent(true)
            }
        }
    }

    private fun getGameDirsAndExternalContent(reloadList: Boolean = false) {
        val gameDirs = NativeConfig.getGameDirs().toMutableList()
        val externalContentDirs = NativeConfig.getExternalContentDirs().map {
            GameDir(it, false, DirectoryType.EXTERNAL_CONTENT)
        }
        gameDirs.addAll(externalContentDirs)
        _folders.value = gameDirs
        if (reloadList) {
            reloadGames(true)
        }
    }

    private fun addExternalContentDir(path: String) {
        val currentDirs = NativeConfig.getExternalContentDirs().toMutableList()
        if (!currentDirs.contains(path)) {
            currentDirs.add(path)
            NativeConfig.setExternalContentDirs(currentDirs.toTypedArray())
            NativeConfig.saveGlobalConfig()
        }
    }

    private fun removeExternalContentDir(path: String) {
        val currentDirs = NativeConfig.getExternalContentDirs().toMutableList()
        currentDirs.remove(path)
        NativeConfig.setExternalContentDirs(currentDirs.toTypedArray())
        NativeConfig.saveGlobalConfig()
    }
}
