package com.bmwanje.luma.playback

import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.ForwardingPlayer
import androidx.media3.common.util.UnstableApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.math.max

private const val NORMAL_PLAYBACK_VOLUME = 1.0f
private const val SKIP_FADE_OUT_MS = 180L
private const val MANUAL_FADE_IN_MS = 320L

/**
 * Session-facing player wrapper. Manual skips get a short click-safe fade, while automatic
 * crossfades are owned by SmartCrossfadeManager so two prepared sources can overlap correctly.
 */
@UnstableApi
class TransitioningPlayer(
    private val delegate: Player,
    private val transitionScope: CoroutineScope,
    private val requestAudioFocus: () -> Boolean = { true },
    private val onUserPause: () -> Unit = {},
    private val onBeforeManualChange: () -> Unit = {}
) : ForwardingPlayer(delegate), AutoCloseable {
    private var skipJob: Job? = null
    private var fadeInJob: Job? = null
    @Volatile private var fadeInOnNextTransition = false

    private val listener = object : Player.Listener {
        override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
            if (!fadeInOnNextTransition) return
            fadeInOnNextTransition = false
            fadeInJob?.cancel()
            fadeInJob = transitionScope.launch {
                withTimeoutOrNull(2_500L) {
                    while (delegate.playbackState != Player.STATE_READY) delay(20L)
                }
                if (delegate.playWhenReady && delegate.volume < NORMAL_PLAYBACK_VOLUME - 0.005f) {
                    fadePlayerVolume(delegate, NORMAL_PLAYBACK_VOLUME, MANUAL_FADE_IN_MS)
                }
            }
        }
    }

    init {
        delegate.addListener(listener)
    }

    override fun play() {
        if (requestAudioFocus()) delegate.play()
    }

    override fun pause() {
        onUserPause()
        delegate.pause()
    }

    override fun seekToNext() {
        if (!hasNextMediaItem()) {
            delegate.seekToNext()
            return
        }
        fadeThen { delegate.seekToNext() }
    }

    override fun seekToNextMediaItem() {
        if (!hasNextMediaItem()) {
            delegate.seekToNextMediaItem()
            return
        }
        fadeThen { delegate.seekToNextMediaItem() }
    }

    override fun seekToPrevious() {
        fadeThen { delegate.seekToPrevious() }
    }

    override fun seekToPreviousMediaItem() {
        if (!hasPreviousMediaItem()) {
            delegate.seekToPreviousMediaItem()
            return
        }
        fadeThen { delegate.seekToPreviousMediaItem() }
    }

    override fun setMediaItems(mediaItems: List<MediaItem>) {
        onBeforeManualChange()
        markFadeForQueueReplacement()
        delegate.setMediaItems(mediaItems)
    }

    override fun setMediaItems(mediaItems: List<MediaItem>, resetPosition: Boolean) {
        onBeforeManualChange()
        markFadeForQueueReplacement()
        delegate.setMediaItems(mediaItems, resetPosition)
    }

    override fun setMediaItems(mediaItems: List<MediaItem>, startIndex: Int, startPositionMs: Long) {
        onBeforeManualChange()
        markFadeForQueueReplacement()
        delegate.setMediaItems(mediaItems, startIndex, startPositionMs)
    }

    internal fun fadeInAfterNextChange() {
        fadeInOnNextTransition = true
    }

    internal fun cancelPendingFadeIn() {
        fadeInOnNextTransition = false
        fadeInJob?.cancel()
        fadeInJob = null
    }

    private fun markFadeForQueueReplacement() {
        if (delegate.playWhenReady && delegate.volume < NORMAL_PLAYBACK_VOLUME - 0.005f) {
            fadeInOnNextTransition = true
        }
    }

    private fun fadeThen(command: () -> Unit) {
        onBeforeManualChange()
        if (!delegate.isPlaying || delegate.volume <= 0.001f) {
            command()
            return
        }
        skipJob?.cancel()
        fadeInOnNextTransition = true
        skipJob = transitionScope.launch {
            fadePlayerVolume(delegate, targetVolume = 0f, durationMs = SKIP_FADE_OUT_MS)
            command()
        }
    }

    override fun close() {
        delegate.removeListener(listener)
        skipJob?.cancel()
        fadeInJob?.cancel()
        skipJob = null
        fadeInJob = null
    }
}

/**
 * Fades down before replacing the queue after the listener selects a track. The session-facing
 * wrapper detects the resulting item transition and fades the incoming song back in.
 */
class PlaybackTransitionCommands(
    private val player: Player,
    private val scope: CoroutineScope
) : AutoCloseable {
    private var queueChangeJob: Job? = null

    fun playQueue(mediaItems: List<MediaItem>, startIndex: Int) {
        if (mediaItems.isEmpty() || startIndex !in mediaItems.indices) return
        queueChangeJob?.cancel()
        queueChangeJob = scope.launch {
            val wasPlaying = player.isPlaying
            val previousVolume = player.volume.coerceIn(0f, 1f)

            if (wasPlaying && previousVolume > 0.001f) {
                fadePlayerVolume(player, targetVolume = 0f, durationMs = SKIP_FADE_OUT_MS)
            }

            player.shuffleModeEnabled = false
            player.setMediaItems(mediaItems, startIndex, 0L)
            player.prepare()
            player.play()

            // If the same item was selected, some player versions may not emit a transition event.
            if (wasPlaying) {
                delay(1_200L)
                if (player.isPlaying && player.playbackState == Player.STATE_READY && player.volume < 0.04f) {
                    fadePlayerVolume(
                        player,
                        targetVolume = previousVolume.coerceAtLeast(NORMAL_PLAYBACK_VOLUME * 0.01f),
                        durationMs = MANUAL_FADE_IN_MS
                    )
                }
            }
        }
    }

    override fun close() {
        queueChangeJob?.cancel()
        queueChangeJob = null
    }
}

/** Smooth volume envelope with eased endpoints, used by manual fades and fallback transitions. */
suspend fun fadePlayerVolume(player: Player, targetVolume: Float, durationMs: Long) {
    val target = targetVolume.coerceIn(0f, 1f)
    val start = player.volume.coerceIn(0f, 1f)
    if (durationMs <= 0L || kotlin.math.abs(target - start) < 0.001f) {
        player.volume = target
        return
    }

    val steps = max(1, (durationMs / 16L).toInt())
    val delayPerStep = (durationMs / steps).coerceAtLeast(1L)
    for (step in 1..steps) {
        val linear = step.toFloat() / steps
        val eased = linear * linear * (3f - 2f * linear)
        player.volume = start + (target - start) * eased
        delay(delayPerStep)
    }
    player.volume = target
}
