// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.app.Activity
import android.content.Context
import android.hardware.display.DisplayManager
import android.os.Handler
import android.os.Looper
import android.view.Display
import androidx.fragment.app.Fragment
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.activities.AuxCompanionActivity
import org.yuzu.yuzu_emu.features.settings.model.IntSetting
import org.yuzu.yuzu_emu.model.Game
import org.yuzu.yuzu_emu.utils.CompanionApp
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.utils.NoCompanion
import org.yuzu.yuzu_emu.utils.SecondScreenPerGame
import org.yuzu.yuzu_emu.views.AuxPresentation

/**
 * Eden Duo: the second screen of the emulation screen -- the DSMod Presentation on a second panel
 * (AYN Thor bottom panel, AYANEO Pocket DS, ...), AuxCompanionActivity when the game itself is on
 * a secondary display, and the "No Companion" setting (Icon, Black, Off, App).
 *
 * EmulationFragment owns one and forwards its lifecycle to it. The lambdas read the fragment's
 * state at call time: [emulationActivity] is the game activity, [programId] the running game's id,
 * [secondScreenTitle] the title whose per-game Second Screen values apply (null for global) and
 * [emulationStarted] whether the game finished booting.
 *
 * The user can hide and show the window during the game (CompanionToggle); while it is hidden,
 * nothing here shows it again on its own.
 */
class DualScreenEmulationController(
    private val fragment: Fragment,
    private val emulationActivity: () -> Activity?,
    private val programId: () -> String?,
    private val secondScreenTitle: () -> String?,
    private val emulationStarted: () -> Boolean,
    /** Eden Duo Fork: the companion pane of a split layout, once the fragment's view exists. */
    private val inlineCompanion: () -> InlineCompanion?
) {
    private companion object {
        const val AUX_RETRY_DELAY_MS = 300L
        const val IDLE_RECHECK_ATTEMPTS = 20 // x AUX_RETRY_DELAY_MS
    }

    // DSMod: second-screen Presentation. It lives from onStart to onStop, so it stays up while the
    // activity is only paused (PiP, a dialog on top), and a DisplayListener brings it back when a
    // panel appears or turns on.
    private var auxPresentation: AuxPresentation? = null
    private var auxWanted = false
    // Eden Duo "No Companion: Off": this game gets no second-screen window (see NoCompanion).
    // Icon and Black need nothing here: the runtime builds the idle page when the game boots.
    // A change made in the in-game settings applies on return (reapplyNoCompanion), except
    // Icon <-> Black, which shows from the next game start. A frontend's intent launch resolves
    // the game asynchronously (EmulationFragment.resolveIntentGameAsync): onStart can show the
    // window before isAuxOff has a programId to answer for, and onGameResolved then closes it
    // once Off is decided for the resolved game. Known limitation: a ROM swap inside a swapped
    // session (game on the second display) into a game without a companion, with Off, closes the
    // companion window and may leave the main display showing the Eden UI.
    private var auxOff = false
    private var auxOffChecked = false
    // Eden Duo "No Companion: App": this boot already tried the chosen app (CompanionApp).
    private var companionAppTried = false
    // No Companion and its app as this game session last applied them (null before boot).
    private var noCompanionApplied: Pair<Int, String?>? = null
    // dropIdleCompanion asked before the game finished loading: ask again a little later.
    private var idleRecheckLeft = 0
    private val idleRecheck = Runnable { dropIdleCompanion() }
    private val auxHandler = Handler(Looper.getMainLooper())
    // Eden Duo companion toggle (CompanionToggle): hidden by the user for this game session.
    // While hidden nothing shows the window automatically -- showAuxPresentation is the one gate.
    private val toggleState = CompanionToggleState()
    private val auxRetry = Runnable { if (auxWanted) showAuxPresentation() }
    private val auxDisplayStates = HashMap<Int, Int>()
    private val auxDisplayListener = object : DisplayManager.DisplayListener {
        override fun onDisplayAdded(displayId: Int) {
            displayState(displayId)?.let { auxDisplayStates[displayId] = it }
            scheduleAuxRetry()
        }

        override fun onDisplayRemoved(displayId: Int) {
            auxDisplayStates.remove(displayId)
            if (auxPresentation?.displayId == displayId) {
                Log.info("[EmulationFragment] DSMod: second-screen display $displayId removed")
                dismissAuxPresentation()
                scheduleAuxRetry()
            } else if (AuxCompanionActivity.isUpOrPending()) {
                // The game's own display may be the one gone, moving it under the companion.
                scheduleAuxRetry()
            }
        }

        override fun onDisplayChanged(displayId: Int) {
            val state = displayState(displayId) ?: return
            val previous = auxDisplayStates.put(displayId, state)
            // Only a display turning on matters: it may be the second panel coming back.
            if (auxPresentation == null && state != Display.STATE_OFF &&
                (previous == null || previous == Display.STATE_OFF)
            ) {
                scheduleAuxRetry()
            }
        }
    }

    /**
     * Before the game's configuration loads: Companion Ratio is per-game, so a previous game's
     * override is dropped first (the per-game load sets it again when this game has one).
     */
    fun onGameConfigLoading() {
        IntSetting.COMPANION_RATIO.global = true
    }

    /**
     * The game's configuration is loaded; [customConfigGame] is the game whose per-game config
     * applies, null for global. Hands this game's No Companion to the idle page the runtime
     * builds at boot.
     */
    fun onGameConfigLoaded(customConfigGame: Game?) {
        SecondScreenPerGame.runningConfigGame = customConfigGame
        NativeLibrary.setNoCompanionOverride(NoCompanion.perGameValue(secondScreenTitle()) ?: -1)
    }

    /**
     * The fragment's `game` is now known after an async intent resolution (see
     * EmulationFragment.resolveIntentGameAsync); [isAuxOff] could not answer for this game before
     * now, so [onStart] may have shown the second-screen window on the assumption that it was
     * wanted. Closes it if No Companion turns out to hide it for the resolved game.
     * launchCompanionApp is not called here: it only runs after boot, from dropIdleCompanion.
     */
    fun onGameResolved() {
        val decidedBefore = auxOffChecked
        val hidesNow = isAuxOff()
        if (!AuxWindowRules.closesWindowOnResolve(decidedBefore, hidesNow)) return
        Log.info(
            "[EmulationFragment] DSMod: No Companion hides the second screen for this game; " +
                "closing the window shown before the title was known"
        )
        dismissCompanionWindows()
    }

    /** The game finished booting. */
    fun onEmulationStarted() {
        idleRecheckLeft = IDLE_RECHECK_ATTEMPTS
        noCompanionApplied = noCompanionNow()
        dropIdleCompanion()
    }

    /** A multi-program title switched program. */
    fun onProgramChanged() {
        // A multi-program title's next program decides No Companion afresh.
        auxOff = false
        auxOffChecked = false
        companionAppTried = false
        // A second screen the previous program dropped comes back if this one wants it.
        scheduleAuxRetry()
    }

    fun onStart() {
        auxWanted = true
        CompanionToggle.register(this)
        displayManager()?.let { dm ->
            auxDisplayStates.clear()
            for (display in dm.displays) auxDisplayStates[display.displayId] = display.state
            dm.registerDisplayListener(auxDisplayListener, auxHandler)
        }
        showAuxPresentation()
    }

    fun onStop() {
        auxWanted = false
        auxHandler.removeCallbacks(auxRetry)
        auxHandler.removeCallbacks(idleRecheck)
        displayManager()?.unregisterDisplayListener(auxDisplayListener)
        dismissCompanionWindows()
    }

    fun onResume() {
        reapplyLayout()
        reapplyNoCompanion()
    }

    /** The window changed size: a split layout follows it (rotation, fold posture). */
    fun onWindowSizeChanged() {
        inlineCompanion()?.onWindowSizeChanged()
    }

    /**
     * The split companion pane became available (the fragment's view was built): apply the
     * layout now, since onStart may have run before the view existed (intent launches).
     */
    fun onInlineCompanionReady() {
        reapplyLayout()
    }

    fun onDestroyView() {
        CompanionToggle.unregister(this)
        auxWanted = false
        auxHandler.removeCallbacks(auxRetry)
        auxHandler.removeCallbacks(idleRecheck)
        dismissCompanionWindows()
        inlineCompanion()?.detach()
    }

    /**
     * True while the companion is placed in the game's own window (CompanionLayout). The fragment
     * then leaves the window geometry to this controller instead of the foldable handler.
     */
    val isSplitLayoutActive: Boolean get() = CompanionLayoutSettings.isSplit()

    /** True while the user hid the second-screen window (CompanionToggle). */
    val isCompanionHiddenByUser: Boolean get() = toggleState.hiddenByUser

    /**
     * Eden Duo Fork: a split companion has no room in a picture-in-picture window. The game takes
     * the whole window while PIP is up; the split comes back when the window is full size again.
     */
    fun onPictureInPictureChanged(inPictureInPicture: Boolean) {
        if (!CompanionLayoutSettings.isSplit()) return
        if (inPictureInPicture) {
            hideInlineCompanion()
        } else {
            reapplyLayout()
        }
    }

    /** The game screen's touch filter for the companion toggle gesture (InputOverlay). */
    val gameTouch: CompanionToggle.GameTouch by lazy {
        val density = fragment.context?.resources?.displayMetrics?.density ?: 1f
        CompanionToggle.GameTouch(density) {
            fragment.context?.let { CompanionToggle.toggle(it) }
        }
    }

    /**
     * One companion toggle (gesture, Quick Settings tile or in-game menu): hides or shows the
     * second-screen window where it was, per CompanionToggleState's decision table. Never loads
     * a companion and never starts the No Companion app. Main thread.
     */
    fun toggleCompanion(nowMs: Long): CompanionToggleOutcome {
        val game = emulationActivity() ?: fragment.activity
        val outcome = toggleState.toggle(nowMs, windowState(), game != null && hasSecondScreen(game))
        Log.info("[EmulationFragment] DSMod: companion toggle: $outcome")
        when (outcome) {
            CompanionToggleOutcome.HIDDEN -> {
                auxHandler.removeCallbacks(auxRetry)
                val swapped = AuxCompanionActivity.isUpOrPending()
                dismissCompanionWindows()
                // The companion activity (Swap Screens) had the focus on its display: give it
                // back to the game rather than to whatever is left there.
                if (swapped && game != null) CompanionApp.returnFocusToGame(game, "companion hidden")
            }

            CompanionToggleOutcome.SHOWN -> {
                if (auxWanted) showAuxPresentation()
                // Another app the user opened on the second screen may hold the focus.
                if (game != null) CompanionApp.returnFocusToGame(game, "companion shown")
            }

            else -> Unit
        }
        return outcome
    }

    private fun windowState(): CompanionWindow {
        if (!isAuxOff()) return CompanionWindow.SHOWN_WHEN_WANTED
        val id = programId()
        return if (id != null && NoCompanion.hasDisabledPackage(id)) {
            CompanionWindow.NONE_ADDON_OFF
        } else {
            CompanionWindow.NONE_NO_COMPANION
        }
    }

    /** A display for the second-screen window exists and is on (the window may be down). */
    private fun hasSecondScreen(game: Activity): Boolean {
        // Eden Duo Fork: a split companion only needs the game's own window.
        if (CompanionLayoutSettings.isSplit()) return true
        if (auxPresentation != null || AuxCompanionActivity.isUpOrPending()) return true
        if (AuxCompanionActivity.isWantedFor(game)) return true // the default display
        val own = AuxCompanionActivity.activityDisplayId(game)
        return AuxPresentation.auxDisplayCandidates(game, own).isNotEmpty()
    }

    private fun displayManager(): DisplayManager? =
        fragment.context?.getSystemService(Context.DISPLAY_SERVICE) as? DisplayManager

    private fun displayState(displayId: Int): Int? = displayManager()?.getDisplay(displayId)?.state

    /** Coalesces bursts of display events into one attempt. */
    private fun scheduleAuxRetry() {
        if (!auxWanted) return
        auxHandler.removeCallbacks(auxRetry)
        auxHandler.postDelayed(auxRetry, AUX_RETRY_DELAY_MS)
    }

    /** True when No Companion is Off and this game has no companion: decided once per game. */
    private fun isAuxOff(): Boolean {
        if (!auxOffChecked) {
            val programId = programId() ?: return false
            auxOffChecked = true
            auxOff = settingHidesWindow(secondScreenTitle()) &&
                NoCompanion.hasPackage(programId) != false
        }
        return auxOff
    }

    /**
     * No Companion takes the window away for this title (Off, or App with no companion). Eden Duo
     * Fork: in a split layout, App (an app opened on the other display) has nowhere to go, so it
     * keeps the companion pane and behaves like Icon.
     */
    private fun settingHidesWindow(title: String?): Boolean {
        val value = NoCompanion.value(title)
        if (CompanionLayoutSettings.isSplit() && value == NoCompanion.APP) return false
        return value == 2 || value == 3
    }

    /**
     * After boot: with No Companion Off, a game whose package the runtime did not take (it is on
     * the idle page) gives its second screen back as well. With App, the chosen app opens there.
     */
    private fun dropIdleCompanion() {
        auxHandler.removeCallbacks(idleRecheck)
        if (!emulationStarted()) return
        if (auxOff) {
            // Closes the window regardless of how onGameResolved and showAuxPresentation raced
            // against boot, so every ordering ends with it down.
            dismissCompanionWindows()
            launchCompanionApp()
            return
        }
        // App that fell back to the Icon page keeps it for this boot.
        if (companionAppTried || !settingHidesWindow(secondScreenTitle())) return
        when (NativeLibrary.isCompanionIdle()) {
            1 -> Unit
            0 -> return
            else -> {
                // No game loaded yet (inconclusive): ask again shortly, a bounded number of times.
                if (idleRecheckLeft-- > 0) {
                    auxHandler.postDelayed(idleRecheck, AUX_RETRY_DELAY_MS)
                }
                return
            }
        }
        Log.info("[EmulationFragment] DSMod: no companion in use; closing the second screen")
        auxOff = true
        auxOffChecked = true
        dismissCompanionWindows()
        launchCompanionApp()
    }

    private fun noCompanionNow(): Pair<Int, String?> =
        NoCompanion.value(secondScreenTitle()) to CompanionApp.flattened(secondScreenTitle())

    /**
     * No Companion or its app changed in the in-game settings: decided again without a restart.
     * Off / App close the second-screen window (App opens the newly chosen app), Icon / Black
     * bring it back.
     */
    private fun reapplyNoCompanion() {
        val before = noCompanionApplied ?: return
        if (!emulationStarted()) return
        val now = noCompanionNow()
        if (now == before) return
        noCompanionApplied = now
        Log.info("[EmulationFragment] No Companion changed in game: ${now.first} ${now.second ?: ""}")
        NativeLibrary.setNoCompanionOverride(NoCompanion.perGameValue(secondScreenTitle()) ?: -1)
        auxOff = false
        auxOffChecked = false
        companionAppTried = false
        if (NoCompanion.hidesWindow(secondScreenTitle())) {
            idleRecheckLeft = IDLE_RECHECK_ATTEMPTS
            dropIdleCompanion()
        } else {
            scheduleAuxRetry()
        }
    }

    /** No Companion App: opens the chosen app, once per boot; on failure, the Icon page. */
    private fun launchCompanionApp() {
        // Eden Duo Fork: with the companion in the game's own window there is no other display
        // to open an app on; the split keeps its Icon page instead.
        if (CompanionLayoutSettings.isSplit()) return
        if (companionAppTried) return
        val game = emulationActivity() ?: return
        companionAppTried = true
        if (CompanionApp.launchForBoot(game, secondScreenTitle()) != CompanionApp.Result.FAILED) {
            return
        }
        auxOff = false
        auxOffChecked = true
        if (auxWanted) showAuxPresentation()
    }

    private fun showAuxPresentation() {
        if (!toggleState.allowsAutoShow()) return
        if (isAuxOff()) {
            hideInlineCompanion()
            dismissAuxPresentation()
            AuxCompanionActivity.dismiss(fragment)
            return
        }
        // Eden Duo Fork: a split layout hosts the companion in the game's own window, in place
        // of a Presentation or the companion activity.
        val split = CompanionLayoutSettings.layout()
        if (split.isSplit) {
            dismissAuxPresentation()
            AuxCompanionActivity.dismiss(fragment)
            inlineCompanion()?.show(split, CompanionLayoutSettings.splitPercent())
            return
        }
        hideInlineCompanion()
        // Eden Duo: with the game on a secondary display the second screen is the default one,
        // where a Presentation cannot go; AuxCompanionActivity hosts it there instead.
        val game = emulationActivity() ?: fragment.activity ?: return
        if (AuxCompanionActivity.isWantedFor(game)) {
            dismissAuxPresentation()
            AuxCompanionActivity.launch(game, fragment)
            return
        }
        AuxCompanionActivity.dismiss(fragment) // the game is back on the default display
        if (auxPresentation != null) return
        val presentation = AuxPresentation.showOnBestDisplay(game) ?: return
        auxPresentation = presentation
        // Presentation cancels itself when its display goes away; drop it and look again.
        presentation.setOnDismissListener {
            if (auxPresentation === presentation) {
                auxPresentation = null
                scheduleAuxRetry()
            }
        }
    }

    private fun inlineCompanion(): InlineCompanion? = inlineCompanion.invoke()

    private fun hideInlineCompanion() {
        inlineCompanion()?.hide()
    }

    /** Closes every kind of companion window: the split pane, the Presentation, the activity. */
    private fun dismissCompanionWindows() {
        hideInlineCompanion()
        dismissAuxPresentation()
        AuxCompanionActivity.dismiss(fragment)
    }

    /**
     * The Companion Layout setting changed (or the game screen came back to the foreground):
     * moves the companion between the split pane and the second display without a restart.
     */
    private fun reapplyLayout() {
        // showAuxPresentation closes whichever window the new layout does not use.
        if (auxWanted) showAuxPresentation()
    }

    private fun dismissAuxPresentation() {
        val presentation = auxPresentation ?: return
        auxPresentation = null
        try {
            presentation.dismiss()
        } catch (e: Exception) {
            Log.error("[EmulationFragment] DSMod: dismiss failed: ${e.message}")
        }
    }
}

/** The Android-free decision of DualScreenEmulationController.onGameResolved (unit tested). */
object AuxWindowRules {
    /**
     * True when a window went up without knowing No Companion for this game ([decidedBefore]
     * false) and it now turns out to hide the window ([hidesNow] true): that window must close.
     * False when the decision was already made before the window was shown (an in-app launch, or
     * a game whose title was known up front), or when No Companion resolves to Icon or Black.
     */
    fun closesWindowOnResolve(decidedBefore: Boolean, hidesNow: Boolean): Boolean =
        !decidedBefore && hidesNow
}
