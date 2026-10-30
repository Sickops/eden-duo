// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import androidx.core.content.edit
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.features.settings.model.AbstractIntSetting
import org.yuzu.yuzu_emu.utils.SecondScreenPerGame

/**
 * Eden Duo Fork: where the companion window lives. Upstream Duo always puts it on a second
 * display (a Presentation, or AuxCompanionActivity when the game itself is on the second
 * display). The fold builds add split placements: the game and the companion share the one
 * window the emulator runs in, one above the other or side by side, on foldables with a large
 * inner screen (Honor Magic V6 and similar).
 *
 * [value] is what the settings store; the order is the order of the settings row.
 */
enum class CompanionLayout(val value: Int) {
    /** A second display, exactly as upstream Duo (default). */
    EXTERNAL(0),

    /** Split screen, game on the upper pane, companion below. */
    GAME_TOP(1),

    /** Split screen, companion on the upper pane, game below. */
    GAME_BOTTOM(2),

    /** Split screen, game on the left pane, companion on the right. */
    GAME_LEFT(3),

    /** Split screen, companion on the left pane, game on the right. */
    GAME_RIGHT(4);

    /** True when the companion shares the game's window instead of using another display. */
    val isSplit: Boolean get() = this != EXTERNAL

    /** Panes stacked top/bottom (true) or side by side (false). */
    val isStacked: Boolean get() = this == GAME_TOP || this == GAME_BOTTOM

    /** True when the game pane comes first (top or left, depending on the axis). */
    val gameFirst: Boolean get() = this == GAME_TOP || this == GAME_LEFT

    /** The settings row label. */
    val labelRes: Int
        get() = when (this) {
            EXTERNAL -> R.string.companion_layout_external
            GAME_TOP -> R.string.companion_layout_game_top
            GAME_BOTTOM -> R.string.companion_layout_game_bottom
            GAME_LEFT -> R.string.companion_layout_game_left
            GAME_RIGHT -> R.string.companion_layout_game_right
        }

    companion object {
        fun fromValue(value: Int): CompanionLayout =
            entries.firstOrNull { it.value == value } ?: EXTERNAL
    }
}

/**
 * Eden Duo Fork: the split placement and the share of the screen the game pane takes. Stored in
 * Eden Duo's own SharedPreferences (like Swap Screens and No Companion) because the layout must
 * be known before the game window is built, and because it is not part of the emulated settings.
 */
object CompanionLayoutSettings {
    const val KEY_LAYOUT = "companion_layout"
    const val KEY_SPLIT = "companion_split_percent"

    /** The game pane takes a little more than half by default: the game stays readable. */
    const val DEFAULT_SPLIT = 55

    /** Neither pane can be squeezed below this share of the screen... */
    const val MIN_SPLIT = 30
    const val MAX_SPLIT = 75

    private val prefs get() = SecondScreenPerGame.prefs(YuzuApplication.appContext)

    fun layout(): CompanionLayout =
        CompanionLayout.fromValue(prefs.getInt(KEY_LAYOUT, CompanionLayout.EXTERNAL.value))

    /** True when the running game shows the companion in its own window. */
    fun isSplit(): Boolean = layout().isSplit

    /** The game pane's share of the split axis, in percent. */
    fun splitPercent(): Int = clampSplit(prefs.getInt(KEY_SPLIT, DEFAULT_SPLIT))

    fun setSplitPercent(percent: Int) {
        prefs.edit { putInt(KEY_SPLIT, clampSplit(percent)) }
    }

    fun clampSplit(percent: Int): Int = percent.coerceIn(MIN_SPLIT, MAX_SPLIT)

    /** "Companion window" in Graphics -> Second Screen. Applies on return to the game. */
    val layoutSetting: AbstractIntSetting = object : AbstractIntSetting {
        override fun getInt(needsGlobal: Boolean): Int = layout().value
        override fun setInt(value: Int) {
            prefs.edit { putInt(KEY_LAYOUT, CompanionLayout.fromValue(value).value) }
        }

        override val key: String = KEY_LAYOUT
        override val isRuntimeModifiable: Boolean = true // EmulationFragment.onResume
        override val pairedSettingKey: String = ""
        override val isSwitchable: Boolean = false
        override var global: Boolean = true
        override val isSaveable: Boolean = true
        override val defaultValue: Int = CompanionLayout.EXTERNAL.value
        override fun getValueAsString(needsGlobal: Boolean): String = getInt(needsGlobal).toString()
        override fun reset() = prefs.edit { remove(KEY_LAYOUT) }
    }

    /** "Split size" in Graphics -> Second Screen; a game pane share from [MIN_SPLIT] to [MAX_SPLIT]. */
    val splitSetting: AbstractIntSetting = object : AbstractIntSetting {
        override fun getInt(needsGlobal: Boolean): Int = splitPercent()
        override fun setInt(value: Int) = setSplitPercent(value)

        override val key: String = KEY_SPLIT
        override val isRuntimeModifiable: Boolean = true // EmulationFragment.onResume
        override val pairedSettingKey: String = ""
        override val isSwitchable: Boolean = false
        override var global: Boolean = true
        override val isSaveable: Boolean = true
        override val defaultValue: Int = DEFAULT_SPLIT
        override fun getValueAsString(needsGlobal: Boolean): String = getInt(needsGlobal).toString()
        override fun reset() = prefs.edit { remove(KEY_SPLIT) }
    }
}

/**
 * Eden Duo Fork: the Android-free arithmetic of the split layout (unit tested).
 *
 * The panes are laid out along one axis of the window: [gameExtent] pixels for the game, the
 * rest for the companion, minus the divider. The game's share comes from the user, but neither
 * pane may fall under [minPanePx] (a 30 % pane on a small window would be unusable) and the
 * divider must fit, hence the clamping.
 */
object CompanionSplitMath {
    /**
     * The game pane's extent along the split axis, in pixels, for a window of [extentPx] pixels,
     * a divider of [dividerPx], and a game share of [gamePercent]. Returns 0 when the window is
     * not measured yet.
     */
    fun gameExtent(extentPx: Int, gamePercent: Int, dividerPx: Int, minPanePx: Int): Int {
        if (extentPx <= 0) return 0
        val divider = dividerPx.coerceIn(0, extentPx)
        val usable = extentPx - divider
        if (usable <= 0) return 0
        val minPane = minPanePx.coerceIn(1, usable / 2)
        val wanted = (usable.toLong() * gamePercent.coerceIn(0, 100) / 100L).toInt()
        return wanted.coerceIn(minPane, usable - minPane)
    }

    /** The companion pane's extent, complementing [gameExtent]. */
    fun companionExtent(extentPx: Int, gamePercent: Int, dividerPx: Int, minPanePx: Int): Int {
        val game = gameExtent(extentPx, gamePercent, dividerPx, minPanePx)
        if (game == 0) return 0
        return (extentPx - dividerPx.coerceIn(0, extentPx) - game).coerceAtLeast(0)
    }
}
