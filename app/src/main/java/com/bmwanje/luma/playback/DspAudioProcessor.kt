package com.bmwanje.luma.playback

import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.BaseAudioProcessor
import java.nio.ByteBuffer
import java.nio.ByteOrder

class DspAudioProcessor : BaseAudioProcessor() {
    private var nativeHandle: Long = 0L

    init {
        nativeHandle = nativeCreate()
        DspController.attach(this)
    }

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        // DSP runs on stereo PCM16. Other layouts remain playable through the normal Media3 path.
        if (inputAudioFormat.encoding != C.ENCODING_PCM_16BIT || inputAudioFormat.channelCount != 2) {
            return AudioProcessor.AudioFormat.NOT_SET
        }
        if (!nativeConfigure(nativeHandle, inputAudioFormat.sampleRate)) {
            throw AudioProcessor.UnhandledAudioFormatException(inputAudioFormat)
        }
        DspController.attach(this)
        return inputAudioFormat
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val originalPosition = inputBuffer.position()
        val byteCount = inputBuffer.remaining()
        val frameBytes = 4 // stereo, 16-bit PCM
        val alignedBytes = byteCount - (byteCount % frameBytes)
        if (alignedBytes <= 0 || nativeHandle == 0L) {
            inputBuffer.position(inputBuffer.limit())
            return
        }

        inputBuffer.order(ByteOrder.nativeOrder())
        val source = if (inputBuffer.position() == 0) inputBuffer else inputBuffer.slice().order(ByteOrder.nativeOrder())
        val outputBuffer = replaceOutputBuffer(alignedBytes)
        outputBuffer.order(ByteOrder.nativeOrder())
        val producedFrames = nativeProcess(nativeHandle, source, outputBuffer, alignedBytes).coerceAtLeast(0)
        inputBuffer.position((originalPosition + alignedBytes).coerceAtMost(inputBuffer.limit()))
        outputBuffer.position(0)
        outputBuffer.limit((producedFrames * frameBytes).coerceAtMost(alignedBytes))
        if (inputBuffer.hasRemaining()) inputBuffer.position(inputBuffer.limit())
    }

    override fun onQueueEndOfStream() {
        if (nativeHandle == 0L) return
        val outputBuffer = replaceOutputBuffer(MAX_DRAIN_FRAMES * 4)
        outputBuffer.order(ByteOrder.nativeOrder())
        val producedFrames = nativeDrain(nativeHandle, outputBuffer, MAX_DRAIN_FRAMES).coerceIn(0, MAX_DRAIN_FRAMES)
        outputBuffer.position(0)
        outputBuffer.limit(producedFrames * 4)
    }

    override fun onFlush() {
        if (nativeHandle != 0L) {
            nativeReset(nativeHandle)
            DspController.attach(this)
        }
    }

    override fun onReset() {
        DspController.detach(this)
        if (nativeHandle != 0L) {
            nativeRelease(nativeHandle)
            nativeHandle = 0L
        }
    }

    internal fun setEngine(engine: Int) {
        if (nativeHandle != 0L) nativeSetEngine(nativeHandle, engine)
    }

    internal fun setFunPercent(percent: Float) {
        if (nativeHandle != 0L) nativeSetEnhancerPercent(nativeHandle, percent)
    }

    internal fun setPeqEnabled(enabled: Boolean) {
        if (nativeHandle != 0L) nativeSetPeqEnabled(nativeHandle, enabled)
    }

    internal fun setBand(index: Int, frequencyHz: Float, gainDb: Float, q: Float, enabled: Boolean) {
        if (nativeHandle != 0L) nativeSetBand(nativeHandle, index, frequencyHz, gainDb, q, enabled)
    }

    companion object {
        private const val MAX_DRAIN_FRAMES = 8192

        init {
            System.loadLibrary("luma_dsp")
        }

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeRelease(handle: Long)
        @JvmStatic private external fun nativeConfigure(handle: Long, sampleRate: Int): Boolean
        @JvmStatic private external fun nativeReset(handle: Long)
        @JvmStatic private external fun nativeSetEngine(handle: Long, engine: Int)
        @JvmStatic private external fun nativeSetEnhancerPercent(handle: Long, percent: Float)
        @JvmStatic private external fun nativeSetPeqEnabled(handle: Long, enabled: Boolean)
        @JvmStatic private external fun nativeSetBand(
            handle: Long, index: Int, frequencyHz: Float, gainDb: Float, q: Float, enabled: Boolean
        )
        @JvmStatic private external fun nativeProcess(
            handle: Long, input: ByteBuffer, output: ByteBuffer, inputBytes: Int
        ): Int
        @JvmStatic private external fun nativeDrain(handle: Long, output: ByteBuffer, maxFrames: Int): Int
    }
}
