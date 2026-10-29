package com.bmwanje.luma.ui

import java.util.Locale

fun formatDuration(durationMs: Long): String {
    if (durationMs <= 0L) return "0:00"
    val totalSeconds = durationMs / 1000
    val minutes = totalSeconds / 60
    val seconds = totalSeconds % 60
    return String.format(Locale.getDefault(), "%d:%02d", minutes, seconds)
}

fun formatFrequency(frequencyHz: Float): String = when {
    frequencyHz >= 1000f -> {
        val khz = frequencyHz / 1000f
        if (khz % 1f == 0f) "${khz.toInt()} kHz" else String.format(Locale.getDefault(), "%.2f kHz", khz)
    }
    frequencyHz % 1f == 0f -> "${frequencyHz.toInt()} Hz"
    else -> String.format(Locale.getDefault(), "%.1f Hz", frequencyHz)
}
