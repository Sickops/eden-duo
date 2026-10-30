// SPDX-FileCopyrightText: Copyright 2026 Eden DSMod fork
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class IntentGameSetupRulesTest {
    @Test
    fun existingCurrentGameIsNeverReplaced() {
        assertEquals("current", IntentGameSetupRules.deferredGame("current", "args", "intent"))
        assertEquals("current", IntentGameSetupRules.deferredGame("current", null, null))
    }

    @Test
    fun argsGameWinsOverIntentGameWhenCurrentIsUnset() {
        assertEquals("args", IntentGameSetupRules.deferredGame(null, "args", "intent"))
    }

    @Test
    fun intentGameIsTheLastResortWhenNeitherCurrentNorArgsAreSet() {
        assertEquals("intent", IntentGameSetupRules.deferredGame(null, null, "intent"))
    }

    @Test
    fun allUnsetStaysNull() {
        assertNull(IntentGameSetupRules.deferredGame<String>(null, null, null))
    }
}
