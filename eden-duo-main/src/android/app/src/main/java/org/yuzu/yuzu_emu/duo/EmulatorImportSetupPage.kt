// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.os.Bundle
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.preference.PreferenceManager
import androidx.viewpager2.widget.ViewPager2
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.features.settings.ui.SettingsSubscreen
import org.yuzu.yuzu_emu.model.EmulatorImportViewModel
import org.yuzu.yuzu_emu.model.PageButton
import org.yuzu.yuzu_emu.model.PageState
import org.yuzu.yuzu_emu.model.SetupPage

/**
 * Eden Duo: the setup wizard's "import from another emulator" page (page 1, right after the
 * welcome page). After an import the wizard moves on by itself: to the end when the import
 * covered the keys page (page 2), otherwise to that page.
 *
 * [pages] is the wizard's page list and [viewPager] its pager while the view exists.
 */
class EmulatorImportSetupPage(
    private val fragment: Fragment,
    private val pages: () -> List<SetupPage>,
    private val viewPager: () -> ViewPager2?
) {
    private companion object {
        const val PENDING_IMPORT_GENERATION = "PendingImportGeneration"
    }

    // The import generation when the import screen was opened; null when none is pending.
    private var importGenerationBefore: Long? = null

    /** The wizard page itself; [pageForward] skips it ("start fresh"). */
    fun page(pageForward: () -> Unit): SetupPage =
        SetupPage(
            R.drawable.ic_import,
            R.string.emulator_import_setup_title,
            R.string.emulator_import_setup_description,
            listOf(
                PageButton(
                    R.drawable.ic_import,
                    R.string.emulator_import_setup_title,
                    R.string.emulator_import_short_description,
                    {
                        importGenerationBefore =
                            PreferenceManager.getDefaultSharedPreferences(fragment.requireContext())
                                .getLong(EmulatorImportViewModel.IMPORT_GENERATION, 0)
                        fragment.findNavController().navigate(
                            HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                                SettingsSubscreen.EMULATOR_IMPORT, null
                            )
                        )
                    }
                ),
                PageButton(
                    R.drawable.ic_arrow_forward,
                    R.string.emulator_import_start_fresh,
                    R.string.emulator_data_description,
                    { pageForward() }
                )
            ),
            { PageState.UNDEFINED }
        )

    /** Whether the wizard's Next button stays hidden on page [position]. */
    fun hidesNext(position: Int): Boolean = position == 1

    fun onViewCreated(savedInstanceState: Bundle?) {
        importGenerationBefore =
            savedInstanceState?.getLong(PENDING_IMPORT_GENERATION, -1)?.takeIf { it >= 0 }
    }

    fun onResume() {
        viewPager()?.adapter?.notifyDataSetChanged()
        importGenerationBefore?.let { previous ->
            val generation = PreferenceManager.getDefaultSharedPreferences(fragment.requireContext())
                .getLong(EmulatorImportViewModel.IMPORT_GENERATION, 0)
            if (generation != previous) {
                importGenerationBefore = null
                viewPager()!!.currentItem =
                    if (pages()[2].pageSteps() == PageState.COMPLETE) pages().lastIndex else 2
            }
        }
    }

    fun onSaveInstanceState(outState: Bundle) {
        importGenerationBefore?.let { outState.putLong(PENDING_IMPORT_GENERATION, it) }
    }
}
