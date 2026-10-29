package com.bmwanje.luma.playback

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class SongTransitionStyle(val label: String, val description: String) {
    SMART("Smart", "Crossfade between different albums; keep album sequencing natural."),
    CROSSFADE("Crossfade", "Overlap every eligible next song."),
    FADE("Fade", "Fade out, then fade in. No overlap."),
    NONE("Off", "No automatic fade or overlap.")
}

data class SongTransitionSettings(
    val style: SongTransitionStyle = SongTransitionStyle.SMART,
    val durationMs: Long = 3_000L
)

object SongTransitionSettingsStore {
    private const val PREFS = "luma_transitions"
    private const val KEY_STYLE = "style"
    private const val KEY_DURATION = "duration_ms"

    private lateinit var preferences: android.content.SharedPreferences
    private var initialized = false

    private val mutableSettings = MutableStateFlow(SongTransitionSettings())
    val settings = mutableSettings.asStateFlow()

    @Synchronized
    fun initialize(context: Context) {
        if (initialized) return
        preferences = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val style = SongTransitionStyle.entries.firstOrNull {
            it.name == preferences.getString(KEY_STYLE, SongTransitionStyle.SMART.name)
        } ?: SongTransitionStyle.SMART
        mutableSettings.value = SongTransitionSettings(
            style = style,
            durationMs = preferences.getLong(KEY_DURATION, 3_000L).coerceIn(1_000L, 8_000L)
        )
        initialized = true
    }

    @Synchronized
    fun setStyle(style: SongTransitionStyle) {
        update(mutableSettings.value.copy(style = style))
    }

    @Synchronized
    fun setDurationMs(durationMs: Long) {
        update(mutableSettings.value.copy(durationMs = durationMs.coerceIn(1_000L, 8_000L)))
    }

    @Synchronized
    private fun update(settings: SongTransitionSettings) {
        mutableSettings.value = settings
        if (initialized) {
            preferences.edit()
                .putString(KEY_STYLE, settings.style.name)
                .putLong(KEY_DURATION, settings.durationMs)
                .apply()
        }
    }
}
