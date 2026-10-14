// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.content.Context
import android.view.View
import android.widget.ImageView
import androidx.core.view.isVisible
import androidx.recyclerview.widget.ListAdapter
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.DualScreenModStatus

/**
 * Eden Duo: the companion badge and download marker on the game cards of [adapter]. A change of
 * the supported titles or of a game's install state rebinds only the badges (a partial bind with
 * the [SUPPORT_CHANGED] payload), not the whole card.
 */
class DualScreenBadges(
    private val adapter: ListAdapter<Game, *>,
    private val context: Context
) {
    private companion object {
        const val SUPPORT_CHANGED = "DualScreenSupportChanged"
    }

    private var companionStates = emptyMap<String, DualScreenModStatus.State>()
    private var dualScreenTitleIds = emptySet<String>()

    fun setInstallStates(states: Map<String, DualScreenModStatus.State>) {
        val previous = companionStates
        companionStates = states
        adapter.currentList.forEachIndexed { index, game ->
            if (previous[game.programIdHex] != states[game.programIdHex]) {
                adapter.notifyItemChanged(index, SUPPORT_CHANGED)
            }
        }
    }

    fun setTitleIds(titleIds: Set<String>) {
        if (dualScreenTitleIds == titleIds) return
        val previous = dualScreenTitleIds
        dualScreenTitleIds = titleIds
        adapter.currentList.forEachIndexed { index, game ->
            if ((game.programIdHex in previous) != (game.programIdHex in titleIds)) {
                adapter.notifyItemChanged(index, SUPPORT_CHANGED)
            }
        }
    }

    /**
     * Handles a badge-only partial bind of the card [root] at [position]. Returns false when
     * [payloads] asks for more, and the adapter binds the card as usual.
     */
    fun bindPayload(root: View, position: Int, payloads: List<Any>): Boolean {
        if (payloads.isNotEmpty() && payloads.all { it == SUPPORT_CHANGED }) {
            bind(root, adapter.currentList[position])
            return true
        }
        return false
    }

    fun bind(root: View, model: Game) {
        val state = companionStates[model.programIdHex]
        val missing = state == DualScreenModStatus.State.MISSING
        val outdated = state == DualScreenModStatus.State.UPDATE_AVAILABLE
        root.findViewById<ImageView>(R.id.dual_screen_download).isVisible =
            model.programIdHex in dualScreenTitleIds && (missing || outdated)
        root.findViewById<ImageView>(R.id.dual_screen_badge).apply {
            isVisible = model.programIdHex in dualScreenTitleIds
            contentDescription = context.getString(
                when {
                    missing -> R.string.dual_screen_download_available
                    outdated -> R.string.dual_screen_update_available
                    else -> R.string.dual_screen_companion_available
                },
                model.title
            )
        }
    }
}
