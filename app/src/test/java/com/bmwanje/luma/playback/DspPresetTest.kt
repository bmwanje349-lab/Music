package com.bmwanje.luma.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DspPresetTest {
    @Test
    fun everyPresetDefinesExactlySixteenBands() {
        DspPreset.values().forEach { preset ->
            assertEquals(preset.title, 16, preset.gainsDb.size)
        }
    }

    @Test
    fun everyPresetGainStaysInsideTheNativeSafeRange() {
        DspPreset.values().forEach { preset ->
            assertTrue(preset.title, preset.gainsDb.all { it in -12f..12f })
        }
    }

    @Test
    fun bandFrequenciesArePositiveAndStrictlyIncreasing() {
        val frequencies = DspPreset.frequenciesHz
        assertEquals(16, frequencies.size)
        assertTrue(frequencies.all { it > 0f })
        assertTrue(frequencies.zipWithNext().all { (a, b) -> a < b })
    }

    @Test
    fun flatPresetDoesNotChangeAnyBand() {
        assertTrue(DspPreset.FLAT.gainsDb.all { it == 0f })
    }
}
