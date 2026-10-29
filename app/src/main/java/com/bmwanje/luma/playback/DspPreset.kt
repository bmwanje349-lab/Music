package com.bmwanje.luma.playback

enum class DspPreset(val title: String, val gainsDb: List<Float>) {
    FLAT("Flat", List(16) { 0f }),
    VIBE("Vibe", listOf(2.4f, 2.1f, 1.8f, 1.4f, 0.6f, -0.4f, -0.7f, -0.4f, 0.2f, 0.5f, 0.9f, 1.3f, 1.0f, 0.8f, 0.5f, 0.2f)),
    WARM("Warm", listOf(2.2f, 2.0f, 1.6f, 1.0f, 0.4f, 0f, 0f, 0.1f, 0.2f, 0.1f, -0.2f, -0.5f, -0.8f, -1.0f, -1.2f, -1.5f)),
    VOCAL("Vocal", listOf(-1.5f, -1.2f, -0.8f, -0.5f, 0f, 0.3f, 0.5f, 0.6f, 0.8f, 1.1f, 1.5f, 1.4f, 0.8f, 0.4f, 0.1f, 0f)),
    BRIGHT("Bright", listOf(-0.5f, -0.4f, -0.3f, 0f, 0.1f, 0f, -0.2f, 0f, 0.2f, 0.5f, 0.9f, 1.2f, 1.5f, 1.6f, 1.4f, 1.2f));

    companion object {
        val frequenciesHz = listOf(
            31.0f, 50.0f, 80.0f, 125.0f, 200.0f, 315.0f, 500.0f, 800.0f,
            1250.0f, 2000.0f, 3150.0f, 5000.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f
        )
    }
}
