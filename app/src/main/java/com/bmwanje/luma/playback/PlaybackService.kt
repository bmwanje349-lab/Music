package com.bmwanje.luma.playback

import android.content.Context
import android.content.Intent
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
    private lateinit var player: ExoPlayer
    private lateinit var transitionScope: CoroutineScope
    private var transitionPlayer: TransitioningPlayer? = null
    private var automaticFader: AutomaticPlaybackFader? = null

    override fun onCreate() {
        super.onCreate()
        DspController.initialize(applicationContext)
        transitionScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

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

        player = ExoPlayer.Builder(this, renderersFactory)
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(C.USAGE_MEDIA)
                    .setContentType(C.AUDIO_CONTENT_TYPE_MUSIC)
                    .build(),
                true
            )
            .setHandleAudioBecomingNoisy(true)
            .build()

        // The service-owned fader works even when the activity is off-screen or the phone is locked.
        automaticFader = AutomaticPlaybackFader(player, transitionScope)
        transitionPlayer = TransitioningPlayer(player, transitionScope)
        mediaSession = MediaSession.Builder(this, transitionPlayer!!).build()
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaSession? = mediaSession

    override fun onTaskRemoved(rootIntent: Intent?) {
        if (!player.playWhenReady || player.mediaItemCount == 0) stopSelf()
    }

    override fun onDestroy() {
        automaticFader?.close()
        automaticFader = null
        transitionPlayer?.close()
        transitionPlayer = null
        mediaSession?.release()
        mediaSession = null
        if (::player.isInitialized) player.release()
        if (::transitionScope.isInitialized) transitionScope.cancel()
        super.onDestroy()
    }
}
