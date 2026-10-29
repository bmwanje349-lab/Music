package com.bmwanje.luma.playback

import android.content.Context
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.session.MediaSession
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.sin

/**
 * One app-wide focus request shared by both decode pipelines. Requesting focus independently from
 * both ExoPlayers would make them compete with each other during a crossfade.
 */
class PlaybackAudioFocus(
    context: Context,
    private val players: () -> List<ExoPlayer>,
    private val activePlayer: () -> ExoPlayer,
    private val onFocusLost: () -> Unit,
    private val onFocusGained: () -> Unit
) : AutoCloseable {
    private val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    @Volatile private var hasFocus = false
    @Volatile private var resumeAfterTransientLoss = false
    private var focusRequest: AudioFocusRequest? = null

    private val listener = AudioManager.OnAudioFocusChangeListener { change ->
        when (change) {
            AudioManager.AUDIOFOCUS_GAIN -> {
                hasFocus = true
                onFocusGained()
                if (resumeAfterTransientLoss) {
                    resumeAfterTransientLoss = false
                    activePlayer().play()
                }
            }
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
            AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                resumeAfterTransientLoss = activePlayer().playWhenReady || activePlayer().isPlaying
                hasFocus = false
                onFocusLost()
            }
            AudioManager.AUDIOFOCUS_LOSS -> {
                resumeAfterTransientLoss = false
                hasFocus = false
                onFocusLost()
            }
        }
    }

    @Synchronized
    fun request(): Boolean {
        if (hasFocus) return true
        val result = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(
                    android.media.AudioAttributes.Builder()
                        .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                        .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setOnAudioFocusChangeListener(listener)
                .setWillPauseWhenDucked(true)
                .build()
            focusRequest = request
            audioManager.requestAudioFocus(request)
        } else {
            @Suppress("DEPRECATION")
            audioManager.requestAudioFocus(
                listener,
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN
            )
        }
        hasFocus = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        return hasFocus
    }

    @Synchronized
    fun release() {
        resumeAfterTransientLoss = false
        hasFocus = false
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            focusRequest?.let(audioManager::abandonAudioFocusRequest)
            focusRequest = null
        } else {
            @Suppress("DEPRECATION")
            audioManager.abandonAudioFocus(listener)
        }
    }

    override fun close() = release()
}

@UnstableApi
class SmartCrossfadeManager(
    private val players: List<ExoPlayer>,
    private val session: MediaSession,
    private val wrappers: List<TransitioningPlayer>,
    private val scope: CoroutineScope,
    private val focus: PlaybackAudioFocus,
    private val settings: () -> SongTransitionSettings
) : AutoCloseable {
    private var activeIndex = 0
    private var monitorJob: Job? = null
    private var crossfadeJob: Job? = null
    private var fadeJob: Job? = null
    private var isCrossfading = false
    private var preparedForMediaId: String? = null
    private var preparedNextMediaId: String? = null
    private var fadingMediaId: String? = null
    private var observedMediaId: String? = players[0].currentMediaItem?.mediaId
    private val playerListeners = mutableListOf<Pair<Player, Player.Listener>>()

    val activePlayer: ExoPlayer
        get() = players[activeIndex]

    init {
        require(players.size == 2 && wrappers.size == 2) {
            "Luma's crossfade mixer requires exactly two playback pipelines."
        }
        players.forEachIndexed { index, player ->
            val listener = object : Player.Listener {
                override fun onMediaItemTransition(mediaItem: MediaItem?, reason: Int) {
                    if (index != activeIndex || isCrossfading) return
                    onActiveTrackChanged(mediaItem?.mediaId)
                }
                override fun onPlaybackStateChanged(playbackState: Int) {
                    if (index != activeIndex || isCrossfading) return
                    if (playbackState == Player.STATE_ENDED && !player.hasNextMediaItem()) {
                        focus.release()
                    }
                }
            }
            player.addListener(listener)
            playerListeners += player to listener
        }
        monitorJob = scope.launch { monitorPlayback() }
    }

    private fun onActiveTrackChanged(mediaId: String?) {
        if (observedMediaId == mediaId) return
        observedMediaId = mediaId
        fadeJob?.cancel()
        fadeJob = null
        fadingMediaId = null
        // TransitioningPlayer owns incoming manual/fade-mode ramps. Do not cancel its fade job here.
        discardStandby()
    }

    private suspend fun monitorPlayback() {
        while (scope.isActive) {
            if (isCrossfading) {
                delay(35L)
                continue
            }

            val player = activePlayer
            val mediaId = player.currentMediaItem?.mediaId

            if (mediaId != observedMediaId) {
                onActiveTrackChanged(mediaId)
            }

            if (!player.playWhenReady || !player.isPlaying) {
                if (fadingMediaId != null) {
                    fadeJob?.cancel()
                    fadeJob = null
                    fadingMediaId = null
                    wrappers[activeIndex].cancelPendingFadeIn()
                    player.volume = 1f
                }
                delay(70L)
                continue
            }

            val currentSettings = settings()
            val duration = player.duration
            if (duration <= 0L || duration == C.TIME_UNSET) {
                delay(70L)
                continue
            }

            val remaining = duration - player.currentPosition
            val current = player.currentMediaItem
            val nextIndex = player.nextMediaItemIndex
            val next = if (nextIndex in 0 until player.mediaItemCount) {
                player.getMediaItemAt(nextIndex)
            } else null

            if (
                current == null ||
                next == null ||
                player.repeatMode == Player.REPEAT_MODE_ONE ||
                next.mediaId == current.mediaId
            ) {
                discardStandby()
                delay(55L)
                continue
            }

            when (currentSettings.style) {
                SongTransitionStyle.NONE -> {
                    cancelAutomaticFade(player)
                    discardStandby()
                }

                SongTransitionStyle.FADE -> {
                    discardStandby()
                    val fadeLength = currentSettings.durationMs.coerceAtMost(duration)
                    if (remaining in 1L..fadeLength && fadingMediaId != mediaId) {
                        fadingMediaId = mediaId
                        wrappers[activeIndex].fadeInAfterNextChange()
                        fadeJob?.cancel()
                        fadeJob = scope.launch {
                            fadePlayerVolume(player, 0f, remaining.coerceAtLeast(40L))
                        }
                    }
                }

                SongTransitionStyle.SMART,
                SongTransitionStyle.CROSSFADE -> {
                    cancelAutomaticFade(player)
                    val crossfadeAllowed =
                        currentSettings.style == SongTransitionStyle.CROSSFADE ||
                            !sameAlbumRun(current, next)

                    if (!crossfadeAllowed || currentSettings.durationMs <= 0L) {
                        discardStandby()
                        delay(55L)
                        continue
                    }

                    val requestedDuration = currentSettings.durationMs.coerceAtMost(duration)
                    val preloadLead = requestedDuration + 1_800L

                    if (remaining <= preloadLead && preparedForMediaId != mediaId) {
                        prepareStandby(player, nextIndex, current.mediaId, next.mediaId)
                    }

                    val standbyIndex = 1 - activeIndex
                    val standby = players[standbyIndex]
                    if (
                        preparedForMediaId == mediaId &&
                        preparedNextMediaId == next.mediaId &&
                        standby.playbackState == Player.STATE_READY &&
                        remaining in 1L..requestedDuration
                    ) {
                        beginCrossfade(
                            outgoingIndex = activeIndex,
                            incomingIndex = standbyIndex,
                            durationMs = remaining.coerceAtLeast(120L)
                        )
                    }
                }
            }
            delay(35L)
        }
    }

    private fun sameAlbumRun(current: MediaItem, next: MediaItem): Boolean {
        val currentAlbum = current.mediaMetadata.albumTitle?.toString()?.trim().orEmpty()
        val nextAlbum = next.mediaMetadata.albumTitle?.toString()?.trim().orEmpty()
        if (currentAlbum.isBlank() || nextAlbum.isBlank()) return false
        if (!currentAlbum.equals(nextAlbum, ignoreCase = true)) return false

        val currentArtist = current.mediaMetadata.artist?.toString()?.trim().orEmpty()
        val nextArtist = next.mediaMetadata.artist?.toString()?.trim().orEmpty()
        return currentArtist.isBlank() || nextArtist.isBlank() ||
            currentArtist.equals(nextArtist, ignoreCase = true)
    }

    private fun prepareStandby(
        active: ExoPlayer,
        nextIndex: Int,
        currentMediaId: String,
        nextMediaId: String
    ) {
        val standby = players[1 - activeIndex]
        discardStandby()
        if (nextIndex !in 0 until active.mediaItemCount) return

        val queue = (0 until active.mediaItemCount).map(active::getMediaItemAt)
        standby.pause()
        standby.clearMediaItems()
        standby.volume = 0f
        standby.repeatMode = active.repeatMode
        standby.shuffleModeEnabled = active.shuffleModeEnabled
        standby.setMediaItems(queue, nextIndex, 0L)
        standby.prepare()
        preparedForMediaId = currentMediaId
        preparedNextMediaId = nextMediaId
    }

    private fun beginCrossfade(outgoingIndex: Int, incomingIndex: Int, durationMs: Long) {
        if (isCrossfading || outgoingIndex == incomingIndex) return
        val outgoing = players[outgoingIndex]
        val incoming = players[incomingIndex]
        val expectedNextId = preparedNextMediaId
        if (incoming.currentMediaItem?.mediaId != expectedNextId) {
            discardStandby()
            return
        }

        isCrossfading = true
        preparedForMediaId = null
        preparedNextMediaId = null
        fadingMediaId = null
        fadeJob?.cancel()
        fadeJob = null

        incoming.volume = 0f
        session.setPlayer(wrappers[incomingIndex])
        activeIndex = incomingIndex
        observedMediaId = incoming.currentMediaItem?.mediaId
        incoming.play()

        crossfadeJob = scope.launch {
            try {
                crossfadeVolumes(outgoing, incoming, durationMs)
            } finally {
                outgoing.pause()
                outgoing.volume = 0f
                outgoing.clearMediaItems()
                incoming.volume = 1f
                isCrossfading = false
                crossfadeJob = null
            }
        }
    }

    private suspend fun crossfadeVolumes(
        outgoing: Player,
        incoming: Player,
        durationMs: Long
    ) {
        val steps = max(1, (durationMs / 16L).toInt())
        val delayPerStep = (durationMs / steps).coerceAtLeast(1L)
        for (step in 0..steps) {
            if (!scope.isActive) return
            val progress = step.toFloat() / steps
            val angle = progress * (PI.toFloat() / 2f)
            outgoing.volume = cos(angle).coerceIn(0f, 1f)
            incoming.volume = sin(angle).coerceIn(0f, 1f)
            if (step < steps) delay(delayPerStep)
        }
        outgoing.volume = 0f
        incoming.volume = 1f
    }

    private fun cancelAutomaticFade(player: ExoPlayer) {
        if (fadingMediaId == null) return
        fadeJob?.cancel()
        fadeJob = null
        fadingMediaId = null
        wrappers[activeIndex].cancelPendingFadeIn()
        if (player.playWhenReady) player.volume = 1f
    }

    private fun discardStandby() {
        if (isCrossfading) return
        preparedForMediaId = null
        preparedNextMediaId = null
        val standby = players[1 - activeIndex]
        if (standby.mediaItemCount > 0) {
            standby.pause()
            standby.clearMediaItems()
        }
        standby.volume = 0f
    }

    /** Cancels any in-flight blend when the listener explicitly pauses or audio focus is lost. */
    fun recoverAfterPauseOrFocusLoss() {
        crossfadeJob?.cancel()
        crossfadeJob = null
        fadeJob?.cancel()
        fadeJob = null
        isCrossfading = false
        fadingMediaId = null
        preparedForMediaId = null
        preparedNextMediaId = null

        players.forEachIndexed { index, player ->
            player.pause()
            if (index == activeIndex) {
                player.volume = 1f
                wrappers[index].cancelPendingFadeIn()
            } else {
                player.volume = 0f
                player.clearMediaItems()
            }
        }
        observedMediaId = activePlayer.currentMediaItem?.mediaId
    }

    fun pauseByUser() {
        recoverAfterPauseOrFocusLoss()
        focus.release()
    }

    fun focusRegained() {
        players.forEachIndexed { index, player ->
            if (index == activeIndex) {
                player.volume = 1f
            } else {
                player.volume = 0f
                player.pause()
                player.clearMediaItems()
            }
        }
        observedMediaId = activePlayer.currentMediaItem?.mediaId
    }

    override fun close() {
        monitorJob?.cancel()
        crossfadeJob?.cancel()
        fadeJob?.cancel()
        playerListeners.forEach { (player, listener) -> player.removeListener(listener) }
        playerListeners.clear()
        players.forEachIndexed { index, player ->
            player.pause()
            player.volume = if (index == activeIndex) 1f else 0f
        }
    }
}
