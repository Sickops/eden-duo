// SPDX-FileCopyrightText: Copyright 2026 igawa6
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.duo

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.ColorMatrix
import android.graphics.ColorMatrixColorFilter
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.core.view.isVisible
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.R as MaterialR
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.adapters.GameAdapter
import org.yuzu.yuzu_emu.databinding.CardGameGridCompactBinding
import org.yuzu.yuzu_emu.databinding.CardGameListBinding
import org.yuzu.yuzu_emu.utils.ViewUtils.marquee

/**
 * Eden Duo: the library's "Dual-screen games not in your library" section, placed after the
 * owned games (a ConcatAdapter behind GameAdapter). A header row, then one greyscale,
 * non-launchable tile per companion game the library does not have; tapping one explains how to
 * play it. [style] follows the library's view type (grid, compact grid or list).
 */
class UnownedCompanionGamesAdapter(private val context: Context) :
    RecyclerView.Adapter<RecyclerView.ViewHolder>() {
    private companion object {
        const val TYPE_HEADER = 0
        const val TYPE_GRID = 1
        const val TYPE_GRID_COMPACT = 2
        const val TYPE_LIST = 3
        const val ART_BRIGHTNESS = 0.78f
    }

    private var entries = emptyList<CompanionCatalog.Entry>()
    private var tileType = TYPE_GRID

    /** Greyscale, slightly dimmed (the H2 design); the badge and chip keep their colours. */
    private val greyscale = ColorMatrixColorFilter(
        ColorMatrix().apply {
            setSaturation(0f)
            postConcat(
                ColorMatrix().apply { setScale(ART_BRIGHTNESS, ART_BRIGHTNESS, ART_BRIGHTNESS, 1f) }
            )
        }
    )

    @SuppressLint("NotifyDataSetChanged")
    fun setStyle(gameViewType: Int) {
        val type = when (gameViewType) {
            GameAdapter.VIEW_TYPE_GRID_COMPACT -> TYPE_GRID_COMPACT
            GameAdapter.VIEW_TYPE_LIST -> TYPE_LIST
            else -> TYPE_GRID
        }
        if (type != tileType) {
            tileType = type
            notifyDataSetChanged()
        }
    }

    @SuppressLint("NotifyDataSetChanged")
    fun submit(list: List<CompanionCatalog.Entry>) {
        if (list == entries) return
        entries = list
        notifyDataSetChanged()
    }

    /** Whether [position] (in this adapter) is the full-width header row. */
    fun isHeader(position: Int) = entries.isNotEmpty() && position == 0

    val hasEntries get() = entries.isNotEmpty()

    override fun getItemCount() = if (entries.isEmpty()) 0 else entries.size + 1

    override fun getItemViewType(position: Int) = if (position == 0) TYPE_HEADER else tileType

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        val view = when (viewType) {
            TYPE_HEADER -> inflater.inflate(R.layout.item_unowned_games_header, parent, false)
            TYPE_GRID_COMPACT -> CardGameGridCompactBinding.inflate(inflater, parent, false).root.also {
                // The compact card's title sits at the bottom of the art, so the chip goes on top.
                val chip = inflater.inflate(R.layout.view_unowned_chip, parent, false)
                it.findViewById<FrameLayout>(R.id.image_container).addView(
                    chip,
                    FrameLayout.LayoutParams(
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        android.view.Gravity.TOP or android.view.Gravity.START
                    ).apply { setMargins(6.dp, 6.dp, 0, 0) }
                )
            }
            TYPE_LIST -> CardGameListBinding.inflate(inflater, parent, false).root
            else -> inflater.inflate(R.layout.card_game_unowned, parent, false)
        }
        return object : RecyclerView.ViewHolder(view) {}
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        if (position == 0) return
        val entry = entries[position - 1]
        val root = holder.itemView
        val description = context.getString(R.string.companion_unowned_tile_description, entry.name)
        when (holder.itemViewType) {
            TYPE_GRID -> {
                val image = root.findViewById<ImageView>(R.id.image_game_screen)
                val fallback = root.findViewById<View>(R.id.fallback)
                root.findViewById<TextView>(R.id.fallback_title).text = entry.name
                root.findViewById<TextView>(R.id.text_game_title).apply {
                    text = entry.name
                    marquee()
                }
                image.colorFilter = greyscale
                image.setImageDrawable(null)
                fallback.isVisible = true
                CompanionCoverArt.load(entry, image) { fallback.isVisible = !it }
                bindCard(root.findViewById(R.id.card_game_unowned), description)
            }
            TYPE_GRID_COMPACT -> {
                val image = root.findViewById<ImageView>(R.id.image_game_screen_compact)
                root.findViewById<TextView>(R.id.text_game_title_compact).apply {
                    text = entry.name
                    setTextColor(ContextCompat.getColor(context, R.color.companion_unowned_title))
                    marquee()
                }
                bindSmallArt(image, entry)
                bindCard(root.findViewById(R.id.card_game_grid_compact), description)
            }
            TYPE_LIST -> {
                val image = root.findViewById<ImageView>(R.id.image_game_screen)
                root.findViewById<TextView>(R.id.text_game_title).apply {
                    text = entry.name
                    setTextColor(ContextCompat.getColor(context, R.color.companion_unowned_title))
                    marquee()
                }
                root.findViewById<TextView>(R.id.text_game_developer)
                    .setText(R.string.companion_unowned_chip)
                bindSmallArt(image, entry)
                bindCard(root.findViewById(R.id.card_game_list), description)
            }
        }
        bindBadge(root, entry)
    }

    /** Compact and list cards have no fallback layer: the glyph stands in for missing art. */
    private fun bindSmallArt(image: ImageView, entry: CompanionCatalog.Entry) {
        CompanionCoverArt.load(entry, image) { shown ->
            if (shown) {
                image.colorFilter = greyscale
                image.scaleType = ImageView.ScaleType.CENTER_CROP
                image.imageTintList = null
                image.setPadding(0, 0, 0, 0)
            } else {
                // The glyph is drawn grey already; a colour filter would override its tint.
                image.colorFilter = null
                image.setBackgroundColor(ContextCompat.getColor(context, R.color.companion_unowned_fallback))
                image.scaleType = ImageView.ScaleType.FIT_CENTER
                image.setImageResource(R.drawable.ic_dual_screen)
                image.imageTintList = ColorStateList.valueOf(
                    ContextCompat.getColor(context, R.color.companion_unowned_glyph)
                )
                val pad = image.layoutParams.height.coerceAtLeast(0) / 4
                image.setPadding(pad, pad, pad, pad)
            }
        }
    }

    private fun bindCard(card: View, description: String) {
        card.contentDescription = description
        card.setOnClickListener { showDialog() }
        card.setOnLongClickListener {
            showDialog()
            true
        }
    }

    private fun bindBadge(root: View, entry: CompanionCatalog.Entry) {
        root.findViewById<ImageView>(R.id.dual_screen_download)?.isVisible = false
        root.findViewById<ImageView>(R.id.dual_screen_badge)?.apply {
            isVisible = true
            setBackgroundResource(R.drawable.dual_screen_badge_background)
            imageTintList = ColorStateList.valueOf(
                MaterialColors.getColor(this, MaterialR.attr.colorOnPrimaryContainer)
            )
            contentDescription = context.getString(R.string.dual_screen_companion_available, entry.name)
        }
    }

    private fun showDialog() {
        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.companion_unowned_dialog_title)
            .setMessage(R.string.companion_unowned_dialog_message)
            .setPositiveButton(android.R.string.ok, null)
            .setNeutralButton(R.string.companion_unowned_dialog_link) { _, _ ->
                runCatching {
                    context.startActivity(
                        Intent(Intent.ACTION_VIEW, context.getString(R.string.companion_unowned_link).toUri())
                    )
                }
            }
            .show()
    }

    private val Int.dp get() = (this * context.resources.displayMetrics.density).toInt()
}
