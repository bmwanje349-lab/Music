package com.bmwanje.luma.playback

import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.ForwardingPlayer
import androidx.media3.common.util.UnstableApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import kotlin.math.max

private const val NORMAL_PLAYBACK_VOLUME = 1.0f
private const val AUTO_FADE_OUT_MS = 1_100L
private const val AUTO_FADE_IN_MS = 620L
private const val MANUAL_FADE_IN_MS = 460L
private const val SKIP_FADE_OUT_MS = 170L

/**
 * Fades the outgoing track before a Next/Previous command, including commands sent through
 * Android's media session (headphones, notification and lock-screen controls).
 *
 * The underlying player still owns the real playlist and handles gapless playlist advancement.
 */
@UnstableApi
class TransitioningPlayer(
    private val delegate: Player,
    private val transitionScope: CoroutineScope
) : ForwardingPlayer(delegate), AutoCloseable {
    private var skipJob: Job? = null

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

    private fun fadeThen(command: () -> Unit) {
        if (!delegate.isPlaying || delegate.volume <= 0.001f) {
            command()
            return
        }
        skipJob?.cancel()
        skipJob = transitionScope.launch {
            fadePlayerVolume(delegate, targetVolume = 0f, durationMs = SKIP_FADE_OUT_MS)
            command()
        }
    }

    override fun close() {
        skipJob?.cancel()
        skipJob = null
    }
}

/**
 * Runs in the playback service, so natural end-of-track transitions stay smooth even with the
 * activity hidden. It softly lowers the outgoing track near its end and restores volume after the
 * next item becomes ready. Media3 remains responsible for playlist progression and decoding.
 */
@UnstableApi
class AutomaticPlaybackFader(
    private val player: Player,
    private val scope: CoroutineScope
) : Player.Listener, AutoCloseable {
    private val normalVolume = player.volume.coerceIn(0f, 1f)
    private var autoFadeItemId: String? = null
    private var fadeJob: Job? = null
    private val monitorJob: Job

    init {
        player.addListener(this)
        monitorJob = scope.launch { monitorTrackEnd() }
    }

    private suspend fun monitorTrackEnd() {
        while (scope.isActive) {
            val mediaId = player.currentMediaItem?.mediaId
            val duration = player.duration
            val remaining = duration - player.currentPosition
            val hasFollowingTrack =
                player.repeatMode != Player.REPEAT_MODE_ONE && player.hasNextMediaItem()

            if (
                player.isPlaying &&
                mediaId != null &&
                duration > 0L &&
                remaining in 1L..AUTO_FADE_OUT_MS &&
                hasFollowingTrack &&
                autoFadeItemId != mediaId &&
                player.volume >= normalVolume * 0.98f
            ) {
                autoFadeItemId = mediaId
                fadeJob?.cancel()
                fadeJob = scope.launch {
                    fadePlayerVolume(
                        player,
                        targetVolume = 0f,
                        durationMs = remaining.coerceAtLeast(50L)
                    )
                }
            } else if (
                autoFadeItemId != null &&
                autoFadeItemId == mediaId &&
                (
                    !player.playWhenReady ||
                    !hasFollowingTrack ||
                    remaining > AUTO_FADE_OUT_MS + 200L
                )
            ) {
                restoreAfterCancelledFade()
            }

            delay(45L)
        }
    }

    override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
        val needsFadeIn = player.volume < normalVolume - 0.005f
        autoFadeItemId = null
        fadeJob?.cancel()
        fadeJob = null

        if (!needsFadeIn) return

        fadeJob = scope.launch {
            // On a manually selected item, let Media3 prepare the new source before opening
            // the fade. For already-buffered playlist items this loop exits immediately.
            withTimeoutOrNull(2_500L) {
                while (player.playbackState != Player.STATE_READY) {
                    delay(20L)
                }
            }
            if (player.volume < normalVolume - 0.005f) {
                val fadeInMs = if (reason == Player.MEDIA_ITEM_TRANSITION_REASON_AUTO) {
                    AUTO_FADE_IN_MS
                } else {
                    MANUAL_FADE_IN_MS
                }
                fadePlayerVolume(player, targetVolume = normalVolume, durationMs = fadeInMs)
            }
        }
    }

    override fun onPlayWhenReadyChanged(playWhenReady: Boolean, reason: Int) {
        // If the user pauses during the pre-end fade, don't leave the player quiet on resume.
        if (!playWhenReady && autoFadeItemId != null) {
            restoreAfterCancelledFade()
        }
    }

    private fun restoreAfterCancelledFade() {
        autoFadeItemId = null
        fadeJob?.cancel()
        fadeJob = scope.launch {
            fadePlayerVolume(player, targetVolume = normalVolume, durationMs = 110L)
        }
    }

    override fun close() {
        player.removeListener(this)
        monitorJob.cancel()
        fadeJob?.cancel()
        fadeJob = null
    }
}

/**
 * UI-side queue selection: fade down before replacing the current song/queue. The service fader
 * detects the resulting media-item change and fades the new track back in.
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

            // Safety net for selecting the same track again, where some player versions may
            // keep the same media item and therefore not emit a transition event.
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
        val volumeWasFading = player.volume < 0.99f
        queueChangeJob?.cancel()
        queueChangeJob = null
        // Don't leave background playback attenuated if the UI is torn down mid-transition.
        if (volumeWasFading && player.playbackState == Player.STATE_READY) {
            player.volume = NORMAL_PLAYBACK_VOLUME
        }
    }
}

/** Smoothly changes the Media3 player output gain without stopping or restarting playback. */
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
