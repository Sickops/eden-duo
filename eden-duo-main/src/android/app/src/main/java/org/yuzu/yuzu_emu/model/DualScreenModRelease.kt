// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.model

import android.os.Parcelable
import kotlinx.parcelize.Parcelize

@Parcelize
data class DualScreenModRelease(
    val title: String,
    val tag: String,
    val notes: String,
    val publishedAt: String,
    val assetName: String,
    val downloadUrl: String,
    val size: Long,
    val sha256: String?
) : Parcelable
