// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.annotation.SuppressLint
import android.view.Display
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.fragment.app.Fragment
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.views.AuxScreenHost

/**
 * Eden Duo Fork: the companion pane of a split layout.
 *
 * Upstream Duo hosts the second-screen output in a window of its own: AuxPresentation on a second
 * panel, AuxCompanionActivity when the game sits on the second panel. Here the same output is
 * hosted by an AuxScreenHost SurfaceView added to a pane of the game's own window, so the game and
 * the companion are visible at once on a single-screen device (a foldable's inner screen).
 *
 * The controller owns the geometry: it sizes the game pane and the companion pane along one axis
 * of the window, and can move the divider between them. The native side is untouched: the pane's
 * SurfaceView is handed to the aux window exactly like a Presentation's, and its touches become
 * aux touch for the dsm:u service (AuxScreenHost does both).
 *
 * Haptics keep working: AuxScreenHost publishes itself as the current second screen while shown.
 */
class InlineCompanion(
    private val fragment: Fragment,
    private val screenRoot: View,
    private val gamePane: View,
    private val companionPane: FrameLayout,
    private val divider: View,
    /** The game pane's size changed: [settled] is false during a divider drag. */
    private val onGamePaneResized: (settled: Boolean) -> Unit
) {
    private companion object {
        /** The drag handle: thin enough to read as a seam, thick enough to catch. */
        const val DIVIDER_DP = 12f

        /** Neither pane is allowed below this, whatever the stored share says. */
        const val MIN_PANE_DP = 96f

        const val DRAG_SLOP_DP = 4f
    }

    private var host: AuxScreenHost? = null
    private var shown = false
    private var geometryApplied = false
    private var layout = CompanionLayout.EXTERNAL
    private var gamePercent = CompanionLayoutSettings.DEFAULT_SPLIT
    private var dragStart = 0f
    private var dragging = false

    /** True while the companion pane is up in the game's window. */
    val isShown: Boolean get() = shown

    /**
     * Shows the companion in [layout] with the game taking [percent] of the split axis. Safe to
     * call repeatedly (a display event, a settings change): the geometry is simply reapplied.
     */
    fun show(layout: CompanionLayout, percent: Int) {
        if (!layout.isSplit) {
            hide()
            return
        }
        val unchanged = shown && this.layout == layout &&
            this.gamePercent == CompanionLayoutSettings.clampSplit(percent)
        this.layout = layout
        gamePercent = CompanionLayoutSettings.clampSplit(percent)
        ensureHost()
        val firstShow = !shown
        shown = true
        companionPane.visibility = View.VISIBLE
        divider.visibility = View.VISIBLE
        host?.start()
        if (!unchanged) applyGeometry()
        if (firstShow) {
            Log.info("[InlineCompanion] split companion up: $layout, game $gamePercent%")
        }
    }

    /** Takes the companion pane down and gives the whole window back to the game. */
    fun hide() {
        if (!shown) {
            resetGeometry()
            return
        }
        shown = false
        host?.stop()
        companionPane.visibility = View.GONE
        divider.visibility = View.GONE
        resetGeometry()
        Log.info("[InlineCompanion] split companion down")
    }

    /** The fragment's view is going away: release the surface and the host. */
    fun detach() {
        hide()
        host = null
        companionPane.removeAllViews()
    }

    /** The window changed size (rotation, multi-window): reapply the split to the new extent. */
    fun onWindowSizeChanged() {
        if (shown) applyGeometry()
    }

    // --- the pane's view ---------------------------------------------------------------------

    @SuppressLint("ClickableViewAccessibility")
    private fun ensureHost() {
        if (host != null) return
        val activity = fragment.activity
        val newHost = AuxScreenHost(
            context = companionPane.context,
            window = { activity?.window },
            displayId = { displayId(activity) },
            // A pane of the main window follows the main window's brightness by itself.
            followSystemBrightness = false,
            logTag = "[InlineCompanion]"
        )
        val view = newHost.createView()
        view.layoutParams = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
        companionPane.addView(view)
        host = newHost
        setUpDividerDrag()
        Log.info("[InlineCompanion] companion surface view created")
    }

    @Suppress("DEPRECATION")
    private fun displayId(activity: android.app.Activity?): Int = try {
        activity?.display?.displayId ?: Display.DEFAULT_DISPLAY
    } catch (e: Exception) {
        Display.DEFAULT_DISPLAY
    }

    // --- geometry ----------------------------------------------------------------------------

    private fun paramsOf(view: View): CoordinatorLayout.LayoutParams? =
        view.layoutParams as? CoordinatorLayout.LayoutParams

    private fun applyGeometry(settled: Boolean = true) {
        val width = screenRoot.width
        val height = screenRoot.height
        if (width <= 0 || height <= 0) {
            screenRoot.post { if (shown) applyGeometry(settled) }
            return
        }
        val density = screenRoot.resources.displayMetrics.density
        val dividerPx = (DIVIDER_DP * density).toInt()
        val minPanePx = (MIN_PANE_DP * density).toInt()
        val stacked = layout.isStacked
        val extent = if (stacked) height else width

        val gameExtent = CompanionSplitMath.gameExtent(extent, gamePercent, dividerPx, minPanePx)
        val companionExtent =
            CompanionSplitMath.companionExtent(extent, gamePercent, dividerPx, minPanePx)
        if (gameExtent <= 0 || companionExtent <= 0) return

        val game = paramsOf(gamePane) ?: return
        val companion = paramsOf(companionPane) ?: return
        val seam = paramsOf(divider) ?: return

        // The seam and the game pane are placed by margins along one axis only: clear the
        // margins of the other axis so a previous placement does not offset them.
        game.topMargin = 0
        game.marginStart = 0
        seam.topMargin = 0
        seam.marginStart = 0

        if (stacked) {
            game.width = ViewGroup.LayoutParams.MATCH_PARENT
            game.height = gameExtent
            game.gravity = if (layout.gameFirst) Gravity.TOP else Gravity.BOTTOM
            companion.width = ViewGroup.LayoutParams.MATCH_PARENT
            companion.height = companionExtent
            companion.gravity = if (layout.gameFirst) Gravity.BOTTOM else Gravity.TOP
            seam.width = ViewGroup.LayoutParams.MATCH_PARENT
            seam.height = dividerPx
            seam.gravity = Gravity.TOP
            seam.topMargin = if (layout.gameFirst) gameExtent else companionExtent
        } else {
            game.height = ViewGroup.LayoutParams.MATCH_PARENT
            game.width = gameExtent
            game.gravity = Gravity.TOP or
                (if (layout.gameFirst) Gravity.START else Gravity.END)
            companion.height = ViewGroup.LayoutParams.MATCH_PARENT
            companion.width = companionExtent
            companion.gravity = Gravity.TOP or
                (if (layout.gameFirst) Gravity.END else Gravity.START)
            seam.height = ViewGroup.LayoutParams.MATCH_PARENT
            seam.width = dividerPx
            seam.gravity = Gravity.TOP or Gravity.START
            seam.marginStart = if (layout.gameFirst) gameExtent else companionExtent
        }
        gamePane.requestLayout()
        companionPane.requestLayout()
        divider.requestLayout()
        geometryApplied = true
        onGamePaneResized(settled)
    }

    private fun resetGeometry() {
        if (!geometryApplied) return
        geometryApplied = false
        val game = paramsOf(gamePane) ?: return
        val full = ViewGroup.LayoutParams.MATCH_PARENT
        game.width = full
        game.height = full
        game.gravity = Gravity.TOP or Gravity.START
        game.topMargin = 0
        game.marginStart = 0
        val seam = paramsOf(divider)
        if (seam != null) {
            seam.topMargin = 0
            seam.marginStart = 0
        }
        gamePane.requestLayout()
        onGamePaneResized(true)
    }

    // --- divider drag ------------------------------------------------------------------------

    @SuppressLint("ClickableViewAccessibility")
    private fun setUpDividerDrag() {
        val slop = DRAG_SLOP_DP * screenRoot.resources.displayMetrics.density
        divider.setOnTouchListener { _, event ->
            if (!shown) return@setOnTouchListener false
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dragStart = axisPosition(event)
                    dragging = false
                    true
                }

                MotionEvent.ACTION_MOVE -> {
                    if (!dragging && kotlin.math.abs(axisPosition(event) - dragStart) < slop) {
                        return@setOnTouchListener true
                    }
                    dragging = true
                    dragTo(axisPosition(event))
                    true
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (dragging) {
                        CompanionLayoutSettings.setSplitPercent(gamePercent)
                        Log.info("[InlineCompanion] split set to $gamePercent%")
                    }
                    dragging = false
                    true
                }

                else -> false
            }
        }
    }

    /** The touch position along the split axis, in the screen root's coordinates. */
    private fun axisPosition(event: MotionEvent): Float {
        val location = IntArray(2)
        screenRoot.getLocationOnScreen(location)
        return if (layout.isStacked) {
            event.rawY - location[1]
        } else {
            event.rawX - location[0]
        }
    }

    private fun dragTo(position: Float) {
        val density = screenRoot.resources.displayMetrics.density
        val dividerPx = (DIVIDER_DP * density).toInt()
        val extent = if (layout.isStacked) screenRoot.height else screenRoot.width
        val usable = extent - dividerPx
        if (usable <= 0) return
        // The divider sits at the end of the game pane whichever pane is first.
        val gameExtent = if (layout.gameFirst) position else usable - position
        val percent = (gameExtent * 100 / usable).toInt()
        val clamped = CompanionLayoutSettings.clampSplit(percent)
        if (clamped == gamePercent) return
        gamePercent = clamped
        applyGeometry(settled = false)
    }
}
