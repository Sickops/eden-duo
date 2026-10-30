// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AuxWindowRulesTest {
    @Test
    fun windowShownBeforeTitleKnownAndOffDecidedLaterClosesIt() {
        assertTrue(AuxWindowRules.closesWindowOnResolve(decidedBefore = false, hidesNow = true))
    }

    @Test
    fun decidedBeforeResolveLeavesTheWindowAlone() {
        // An in-app launch: args.game is already set at onStart, so isAuxOff answered before
        // onGameResolved runs, whichever way it answered.
        assertFalse(AuxWindowRules.closesWindowOnResolve(decidedBefore = true, hidesNow = true))
        assertFalse(AuxWindowRules.closesWindowOnResolve(decidedBefore = true, hidesNow = false))
    }

    @Test
    fun resolvingToIconOrBlackLeavesTheWindowAlone() {
        assertFalse(AuxWindowRules.closesWindowOnResolve(decidedBefore = false, hidesNow = false))
    }
}
