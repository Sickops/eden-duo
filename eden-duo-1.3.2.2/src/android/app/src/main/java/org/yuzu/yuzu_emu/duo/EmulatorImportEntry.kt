// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.ui.SettingsSubscreen
import org.yuzu.yuzu_emu.model.Installable

/** Eden Duo: the "import from another emulator" entry at the top of the Manage data screen. */
object EmulatorImportEntry {
    /** The entry; it opens the emulator import screen from [fragment]. */
    fun installable(fragment: Fragment): Installable =
        Installable(
            R.string.emulator_import_title,
            R.string.emulator_import_short_description,
            install = {
                fragment.findNavController().navigate(
                    HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                        SettingsSubscreen.EMULATOR_IMPORT,
                        null
                    )
                )
            }
        )

    /** The entry (position 0) spans the whole first row of [layoutManager]. */
    fun spanFirstRow(layoutManager: GridLayoutManager) {
        layoutManager.spanSizeLookup = object : GridLayoutManager.SpanSizeLookup() {
            override fun getSpanSize(position: Int) =
                if (position == 0) layoutManager.spanCount else 1
        }
    }
}
