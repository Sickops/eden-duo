// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

/**
 * Eden Duo: the "game to fall back on while deferring setup" decision EmulationFragment makes
 * when emulation is still stopping and it can't call finishGameSetup() yet. Pulled out of
 * EmulationFragment so the one piece of real decision logic in the intent-launch async-resolve
 * fix (see EmulationFragment.continueGameSetupAfterIntentResolved) has a plain-JVM unit test,
 * independent of Fragment, Game or any other Android type.
 */
object IntentGameSetupRules {
    /**
     * [currentGame] wins if an earlier deferred pass already set it - never clobber it with a
     * possibly stale fallback. Otherwise prefer [argsGame] (a library launch's already-resolved
     * Game) over [intentGame] (an intent launch's Game, resolved either synchronously or off the
     * UI thread).
     */
    fun <T> deferredGame(currentGame: T?, argsGame: T?, intentGame: T?): T? =
        currentGame ?: argsGame ?: intentGame
}
