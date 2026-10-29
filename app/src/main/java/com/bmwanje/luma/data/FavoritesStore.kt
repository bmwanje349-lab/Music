package com.bmwanje.luma.data

import android.content.Context

object FavoritesStore {
    private const val PREFS = "luma_library"
    private const val KEY_FAVORITES = "favorite_track_ids"

    fun load(context: Context): Set<Long> =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getStringSet(KEY_FAVORITES, emptySet())
            .orEmpty()
            .mapNotNull { it.toLongOrNull() }
            .toSet()

    fun toggle(context: Context, id: Long): Set<Long> {
        val next = load(context).toMutableSet()
        if (!next.add(id)) next.remove(id)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putStringSet(KEY_FAVORITES, next.map(Long::toString).toSet())
            .apply()
        return next
    }
}
