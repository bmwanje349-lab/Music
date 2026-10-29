package com.bmwanje.luma

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Home
import androidx.compose.material.icons.rounded.LibraryMusic
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.media3.session.MediaController
import androidx.media3.session.SessionToken
import com.bmwanje.luma.data.FavoritesStore
import com.bmwanje.luma.data.LibraryRepository
import com.bmwanje.luma.data.MusicTrack
import com.bmwanje.luma.playback.DspController
import com.bmwanje.luma.playback.PlaybackService
import com.bmwanje.luma.ui.EqualizerScreen
import com.bmwanje.luma.ui.HomeScreen
import com.bmwanje.luma.ui.LibraryScreen
import com.bmwanje.luma.ui.LumaTheme
import com.bmwanje.luma.ui.MiniPlayer
import com.bmwanje.luma.ui.NowPlayingScreen
import com.bmwanje.luma.ui.SettingsScreen
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = android.graphics.Color.rgb(11, 13, 18)
        window.navigationBarColor = android.graphics.Color.rgb(11, 13, 18)
        DspController.initialize(applicationContext)
        setContent { LumaTheme { LumaMusicApp() } }
    }
}

private enum class MainTab {
    HOME, LIBRARY, SOUND, SETTINGS
}

@Composable
private fun LumaMusicApp() {
    val context = LocalContext.current
    val permission = remember {
        if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO
        else Manifest.permission.READ_EXTERNAL_STORAGE
    }
    var hasPermission by remember {
        mutableStateOf(ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED)
    }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasPermission = granted }

    var tracks by remember { mutableStateOf<List<MusicTrack>>(emptyList()) }
    var favorites by remember { mutableStateOf(FavoritesStore.load(context)) }
    var selectedTab by remember { mutableStateOf(MainTab.HOME) }
    var showNowPlaying by remember { mutableStateOf(false) }
    var currentTick by remember { mutableIntStateOf(0) }
    val controller = rememberMediaController(context)

    LaunchedEffect(hasPermission) {
        if (hasPermission) {
            tracks = withContext(Dispatchers.IO) { LibraryRepository.loadTracks(context) }
            favorites = FavoritesStore.load(context)
        }
    }
    LaunchedEffect(controller) {
        while (isActive) {
            currentTick++
            delay(400)
        }
    }
    @Suppress("UNUSED_VARIABLE")
    val playbackRefresh = currentTick

    val currentItem = controller?.currentMediaItem
    val currentTrack = tracks.firstOrNull { it.id.toString() == currentItem?.mediaId }

    fun playTrack(track: MusicTrack) {
        val player = controller ?: return
        val index = tracks.indexOfFirst { it.id == track.id }
        if (index < 0) return
        player.setMediaItems(tracks.map { it.toMediaItem() }, index, 0L)
        player.prepare()
        player.play()
        showNowPlaying = true
    }

    fun playAll(shuffle: Boolean = false) {
        val player = controller ?: return
        if (tracks.isEmpty()) return
        val queue = if (shuffle) tracks.shuffled() else tracks
        player.setMediaItems(queue.map { it.toMediaItem() }, 0, 0L)
        player.shuffleModeEnabled = false
        player.prepare()
        player.play()
    }

    fun toggleFavorite(track: MusicTrack) {
        favorites = FavoritesStore.toggle(context, track.id)
    }

    if (showNowPlaying && currentItem != null) {
        NowPlayingScreen(
            controller = controller,
            item = currentItem,
            track = currentTrack,
            isFavorite = currentTrack?.let { it.id in favorites } == true,
            onBack = { showNowPlaying = false },
            onToggleFavorite = { currentTrack?.let(::toggleFavorite) },
            queue = tracks
        )
        return
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        bottomBar = {
            Column {
                if (currentItem != null) {
                    MiniPlayer(
                        item = currentItem,
                        isPlaying = controller?.isPlaying == true,
                        positionMs = controller?.currentPosition ?: 0L,
                        durationMs = controller?.duration?.takeIf { it > 0 } ?: currentTrack?.durationMs ?: 0L,
                        onOpen = { showNowPlaying = true },
                        onPlayPause = {
                            if (controller?.isPlaying == true) controller.pause() else controller?.play()
                        },
                        onNext = { controller?.seekToNextMediaItem() }
                    )
                }
                NavigationBar(
                    containerColor = Color(0xFF10131A),
                    contentColor = MaterialTheme.colorScheme.onSurface
                ) {
                    NavigationBarItem(
                        selected = selectedTab == MainTab.HOME,
                        onClick = { selectedTab = MainTab.HOME },
                        icon = { Icon(Icons.Rounded.Home, contentDescription = null) },
                        label = { androidx.compose.material3.Text("Home") }
                    )
                    NavigationBarItem(
                        selected = selectedTab == MainTab.LIBRARY,
                        onClick = { selectedTab = MainTab.LIBRARY },
                        icon = { Icon(Icons.Rounded.LibraryMusic, contentDescription = null) },
                        label = { androidx.compose.material3.Text("Library") }
                    )
                    NavigationBarItem(
                        selected = selectedTab == MainTab.SOUND,
                        onClick = { selectedTab = MainTab.SOUND },
                        icon = { Icon(Icons.Rounded.GraphicEq, contentDescription = null) },
                        label = { androidx.compose.material3.Text("Sound") }
                    )
                    NavigationBarItem(
                        selected = selectedTab == MainTab.SETTINGS,
                        onClick = { selectedTab = MainTab.SETTINGS },
                        icon = { Icon(Icons.Rounded.Settings, contentDescription = null) },
                        label = { androidx.compose.material3.Text("Settings") }
                    )
                }
            }
        }
    ) { contentPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(contentPadding)
                .background(MaterialTheme.colorScheme.background)
        ) {
            when (selectedTab) {
                MainTab.HOME -> HomeScreen(
                    tracks = tracks,
                    favorites = favorites,
                    hasPermission = hasPermission,
                    onRequestPermission = { permissionLauncher.launch(permission) },
                    onPlayTrack = ::playTrack,
                    onPlayAll = { playAll(false) },
                    onShuffle = { playAll(true) },
                    onOpenLibrary = { selectedTab = MainTab.LIBRARY },
                    onOpenSound = { selectedTab = MainTab.SOUND },
                    onToggleFavorite = ::toggleFavorite
                )
                MainTab.LIBRARY -> LibraryScreen(
                    tracks = tracks,
                    favorites = favorites,
                    hasPermission = hasPermission,
                    onRequestPermission = { permissionLauncher.launch(permission) },
                    onRefresh = {
                        if (hasPermission) {
                            tracks = LibraryRepository.loadTracks(context)
                            favorites = FavoritesStore.load(context)
                        }
                    },
                    onPlayTrack = ::playTrack,
                    onToggleFavorite = ::toggleFavorite
                )
                MainTab.SOUND -> EqualizerScreen()
                MainTab.SETTINGS -> SettingsScreen(
                    trackCount = tracks.size,
                    hasPermission = hasPermission,
                    onRescan = {
                        if (hasPermission) tracks = LibraryRepository.loadTracks(context)
                        else permissionLauncher.launch(permission)
                    }
                )
            }
        }
    }
}

@Composable
private fun rememberMediaController(context: Context): MediaController? {
    var controller by remember { mutableStateOf<MediaController?>(null) }
    DisposableEffect(context) {
        val token = SessionToken(context, ComponentName(context, PlaybackService::class.java))
        val future = MediaController.Builder(context, token).buildAsync()
        future.addListener(
            { controller = runCatching { future.get() }.getOrNull() },
            ContextCompat.getMainExecutor(context)
        )
        onDispose {
            MediaController.releaseFuture(future)
            controller = null
        }
    }
    return controller
}
