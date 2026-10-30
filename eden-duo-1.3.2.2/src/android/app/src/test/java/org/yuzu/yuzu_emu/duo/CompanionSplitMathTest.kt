// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import org.junit.Assert.assertEquals
import org.junit.Test

class CompanionSplitMathTest {
    private val divider = 20
    private val minPane = 96

    @Test
    fun unmeasuredWindowGivesNoPanes() {
        assertEquals(0, CompanionSplitMath.gameExtent(0, 55, divider, minPane))
        assertEquals(0, CompanionSplitMath.gameExtent(-1, 55, divider, minPane))
        assertEquals(0, CompanionSplitMath.companionExtent(0, 55, divider, minPane))
    }

    @Test
    fun panesAndDividerFillTheAxis() {
        val extent = 2000
        val game = CompanionSplitMath.gameExtent(extent, 55, divider, minPane)
        val companion = CompanionSplitMath.companionExtent(extent, 55, divider, minPane)
        assertEquals(1089, game) // (2000 - 20) * 55 %
        assertEquals(891, companion)
        assertEquals(extent, game + divider + companion)
    }

    @Test
    fun neitherPaneFallsUnderTheMinimum() {
        val extent = 2000
        val usable = extent - divider
        assertEquals(usable - minPane, CompanionSplitMath.gameExtent(extent, 100, divider, minPane))
        assertEquals(minPane, CompanionSplitMath.gameExtent(extent, 0, divider, minPane))
    }

    @Test
    fun tinyWindowSplitsEvenly() {
        // A window too small for the minimum: the minimum shrinks to half of the usable extent.
        val extent = 100
        val game = CompanionSplitMath.gameExtent(extent, 55, divider, minPane)
        assertEquals(40, game)
        assertEquals(40, CompanionSplitMath.companionExtent(extent, 55, divider, minPane))
    }

    @Test
    fun dividerLargerThanWindowGivesNoPanes() {
        assertEquals(0, CompanionSplitMath.gameExtent(50, 55, 50, minPane))
        assertEquals(0, CompanionSplitMath.gameExtent(50, 55, 80, minPane))
    }

    @Test
    fun storedShareIsClampedWhenRead() {
        assertEquals(CompanionLayoutSettings.MIN_SPLIT, CompanionLayoutSettings.clampSplit(0))
        assertEquals(CompanionLayoutSettings.MAX_SPLIT, CompanionLayoutSettings.clampSplit(100))
        assertEquals(55, CompanionLayoutSettings.clampSplit(55))
    }
}
