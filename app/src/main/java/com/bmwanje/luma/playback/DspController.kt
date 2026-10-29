package com.bmwanje.luma.playback

import android.content.Context
import java.util.concurrent.CopyOnWriteArraySet
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class SoundEngine(val nativeValue: Int, val label: String) {
    OFF(0, "Off"),
    CONSUMER(1, "Consumer"),
    HALO_BLOOM(2, "Halo Bloom")
}

data class DspSettings(
    val engine: SoundEngine = SoundEngine.OFF,
    val funPercent: Float = 55f,
    val peqEnabled: Boolean = true,
    val gainsDb: List<Float> = List(16) { 0f },
    val preset: DspPreset = DspPreset.FLAT
)

object DspController {
    private const val PREFS = "luma_sound"
    private const val KEY_ENGINE = "engine"
    private const val KEY_FUN = "fun"
    private const val KEY_PEQ = "peq"
    private const val KEY_GAINS = "gains"
    private const val KEY_PRESET = "preset"

    private var initialized = false
    private lateinit var preferences: android.content.SharedPreferences
    // More than one processor is alive during a crossfade. Keep both DSP chains in sync.
    private val processors = CopyOnWriteArraySet<DspAudioProcessor>()

    private val mutableSettings = MutableStateFlow(DspSettings())
    val settings = mutableSettings.asStateFlow()

    @Synchronized
    fun initialize(context: Context) {
        if (initialized) return
        preferences = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val engine = SoundEngine.entries.firstOrNull {
            it.name == preferences.getString(KEY_ENGINE, SoundEngine.OFF.name)
        } ?: SoundEngine.OFF
        val preset = DspPreset.entries.firstOrNull {
            it.name == preferences.getString(KEY_PRESET, DspPreset.FLAT.name)
        } ?: DspPreset.FLAT
        val gains = preferences.getString(KEY_GAINS, null)
            ?.split(",")
            ?.mapNotNull(String::toFloatOrNull)
            ?.takeIf { it.size == 16 }
            ?: preset.gainsDb
        mutableSettings.value = DspSettings(
            engine = engine,
            funPercent = preferences.getFloat(KEY_FUN, 55f).coerceIn(0f, 100f),
            peqEnabled = preferences.getBoolean(KEY_PEQ, true),
            gainsDb = gains.map { it.coerceIn(-12f, 12f) },
            preset = preset
        )
        initialized = true
    }

    internal fun attach(next: DspAudioProcessor) {
        processors.add(next)
        applyTo(next, mutableSettings.value)
    }

    internal fun detach(old: DspAudioProcessor) {
        processors.remove(old)
    }

    fun selectEngine(engine: SoundEngine) = update { it.copy(engine = engine) }
    fun setFunPercent(percent: Float) = update { it.copy(funPercent = percent.coerceIn(0f, 100f)) }
    fun setPeqEnabled(enabled: Boolean) = update { it.copy(peqEnabled = enabled) }

    fun setBandGain(index: Int, gainDb: Float) {
        if (index !in 0 until 16) return
        update {
            val next = it.gainsDb.toMutableList()
            next[index] = gainDb.coerceIn(-12f, 12f)
            it.copy(gainsDb = next)
        }
    }

    fun applyPreset(preset: DspPreset) = update {
        it.copy(gainsDb = preset.gainsDb, preset = preset)
    }

    @Synchronized
    private fun update(transform: (DspSettings) -> DspSettings) {
        val next = transform(mutableSettings.value)
        mutableSettings.value = next
        if (initialized) {
            preferences.edit()
                .putString(KEY_ENGINE, next.engine.name)
                .putFloat(KEY_FUN, next.funPercent)
                .putBoolean(KEY_PEQ, next.peqEnabled)
                .putString(KEY_GAINS, next.gainsDb.joinToString(","))
                .putString(KEY_PRESET, next.preset.name)
                .apply()
        }
        processors.forEach { applyTo(it, next) }
    }

    private fun applyTo(target: DspAudioProcessor, value: DspSettings) {
        target.setEngine(value.engine.nativeValue)
        target.setFunPercent(value.funPercent)
        target.setPeqEnabled(value.peqEnabled)
        value.gainsDb.forEachIndexed { index, gain ->
            target.setBand(
                index = index,
                frequencyHz = DspPreset.frequenciesHz[index],
                gainDb = gain,
                q = 1.0f,
                enabled = value.peqEnabled && kotlin.math.abs(gain) >= 0.05f
            )
        }
    }
}
