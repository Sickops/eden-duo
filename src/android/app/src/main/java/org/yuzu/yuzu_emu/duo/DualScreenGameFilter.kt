// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.content.SharedPreferences
import android.widget.TextView
import androidx.core.content.edit
import androidx.lifecycle.LifecycleOwner
import com.google.android.material.button.MaterialButton
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.adapters.GameAdapter
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.model.GamesViewModel
import org.yuzu.yuzu_emu.utils.ViewUtils.setVisible
import org.yuzu.yuzu_emu.utils.collect

/**
 * Eden Duo: the games list's dual-screen part -- the "dual-screen games only" filter button, the
 * companion badges' data and the empty-list notice that knows about the filter.
 *
 * [refilter] re-runs the fragment's filter and search; [adapter] is the fragment's GameAdapter.
 */
class DualScreenGameFilter(
    private val preferences: SharedPreferences,
    private val gamesViewModel: () -> GamesViewModel,
    private val adapter: () -> GameAdapter,
    private val refilter: () -> Unit
) {
    private companion object {
        const val PREF_DUAL_SCREEN_ONLY = "GamesDualScreenOnly"
    }

    private var dualScreenOnly = preferences.getBoolean(PREF_DUAL_SCREEN_ONLY, false)

    /** [games] reduced to the titles with a companion while the filter is on. */
    fun filter(games: List<Game>): List<Game> =
        if (dualScreenOnly) {
            games.filter { it.programIdHex in gamesViewModel().dualScreen.dualScreenTitleIds.value }
        } else {
            games
        }

    /** Feeds the companion badges and refilters when the supported titles change. */
    fun collect(owner: LifecycleOwner) {
        gamesViewModel().dualScreen.dualScreenInstallStates.collect(owner) {
            adapter().dualScreenBadges.setInstallStates(it)
        }
        gamesViewModel().dualScreen.dualScreenTitleIds.collect(owner) {
            adapter().dualScreenBadges.setTitleIds(it)
            refilter()
        }
    }

    fun bindButton(button: MaterialButton) {
        updateButton(button)
        button.setOnClickListener {
            dualScreenOnly = !dualScreenOnly
            preferences.edit { putBoolean(PREF_DUAL_SCREEN_ONLY, dualScreenOnly) }
            updateButton(button)
            refilter()
        }
    }

    /** The list's empty notice: no games at all, none with a companion, or none matching. */
    fun updateNotice(notice: TextView, games: List<Game>) {
        notice.setText(
            when {
                gamesViewModel().games.value.isEmpty() -> R.string.empty_gamelist
                dualScreenOnly -> R.string.dual_screen_filter_empty
                else -> R.string.game_filter_empty
            }
        )
        notice.setVisible(
            visible = games.isEmpty() && !gamesViewModel().isReloading.value,
            gone = false
        )
    }

    private fun updateButton(button: MaterialButton) {
        button.apply {
            isChecked = dualScreenOnly
            contentDescription = context.getString(
                if (dualScreenOnly) R.string.dual_screen_filter_show_all else R.string.dual_screen_filter
            )
        }
    }
}
