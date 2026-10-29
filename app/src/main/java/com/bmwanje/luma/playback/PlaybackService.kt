package com.bmwanje.luma.playback

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioManager
import androidx.core.content.ContextCompat
import androidx.media3.common.AudioAttributes
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.session.MediaSession
import androidx.media3.session.MediaSessionService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

@UnstableApi
class PlaybackService : MediaSessionService() {
    private var mediaSession: MediaSession? = null
    private lateinit var players: List<ExoPlayer>
    private lateinit var transitionPlayers: List<TransitioningPlayer>
    private lateinit var transitionScope: CoroutineScope
    private lateinit var audioFocus: PlaybackAudioFocus
    private lateinit var crossfadeManager: SmartCrossfadeManager
    private var noisyReceiver: BroadcastReceiver? = null

    override fun onCreate() {
        super.onCreate()
        DspController.initialize(applicationContext)
        SongTransitionSettingsStore.initialize(applicationContext)
        transitionScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

        val mediaAudioAttributes = AudioAttributes.Builder()
            .setUsage(C.USAGE_MEDIA)
            .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
            .build()

        fun buildPlayer(): ExoPlayer {
            val renderersFactory = object : DefaultRenderersFactory(this) {
                override fun buildAudioSink(
                    context: Context,
                    enableFloatOutput: Boolean,
                    enableAudioOutputPlaybackParams: Boolean
                ): AudioSink? {
                    val dsp = DspAudioProcessor()
                    return DefaultAudioSink.Builder(context)
                        .setAudioProcessors(arrayOf(dsp))
                        .setEnableFloatOutput(false)
                        .setEnableAudioOutputPlaybackParameters(enableAudioOutputPlaybackParams)
                        .build()
                }
            }.setEnableAudioFloatOutput(false)

            return ExoPlayer.Builder(this, renderersFactory)
                // One focus request belongs to the service; two ExoPlayers must not compete for it.
                .setAudioAttributes(mediaAudioAttributes, false)
                .setHandleAudioBecomingNoisy(false)
                .build()
        }

        players = listOf(buildPlayer(), buildPlayer())

        audioFocus = PlaybackAudioFocus(
            context = this,
            players = { players },
            activePlayer = { crossfadeManager.activePlayer },
            onFocusLost = {
                if (::crossfadeManager.isInitialized) crossfadeManager.recoverAfterPauseOrFocusLoss()
            },
            onFocusGained = {
                if (::crossfadeManager.isInitialized) crossfadeManager.focusRegained()
            }
        )

        transitionPlayers = players.map { player ->
            TransitioningPlayer(
                delegate = player,
                transitionScope = transitionScope,
                requestAudioFocus = { audioFocus.request() },
                onUserPause = {
                    if (::crossfadeManager.isInitialized) crossfadeManager.pauseByUser()
                }
            )
        }

        mediaSession = MediaSession.Builder(this, transitionPlayers[0]).build()
        crossfadeManager = SmartCrossfadeManager(
            players = players,
            session = mediaSession!!,
            wrappers = transitionPlayers,
            scope = transitionScope,
            focus = audioFocus,
            settings = { SongTransitionSettingsStore.settings.value }
        )

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context?, intent: Intent?) {
                if (intent?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) {
                    crossfadeManager.pauseByUser()
                }
            }
        }
        noisyReceiver = receiver
        ContextCompat.registerReceiver(
            this,
            receiver,
            IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = mediaSession

    override fun onTaskRemoved(rootIntent: Intent?) {
        val active = if (::crossfadeManager.isInitialized) crossfadeManager.activePlayer else null
        if (active == null || !active.playWhenReady || active.mediaItemCount == 0) stopSelf()
    }

    override fun onDestroy() {
        noisyReceiver?.let { runCatching { unregisterReceiver(it) } }
        noisyReceiver = null
        if (::crossfadeManager.isInitialized) crossfadeManager.close()
        if (::transitionPlayers.isInitialized) transitionPlayers.forEach { it.close() }
        mediaSession?.release()
        mediaSession = null
        if (::players.isInitialized) players.forEach { it.release() }
        if (::audioFocus.isInitialized) audioFocus.close()
        if (::transitionScope.isInitialized) transitionScope.cancel()
        super.onDestroy()
    }
}
