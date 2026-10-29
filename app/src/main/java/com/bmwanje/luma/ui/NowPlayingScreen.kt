package com.bmwanje.luma.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Repeat
import androidx.compose.material.icons.rounded.RepeatOne
import androidx.compose.material.icons.rounded.Shuffle
import androidx.compose.material.icons.rounded.SkipNext
import androidx.compose.material.icons.rounded.SkipPrevious
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.session.MediaController
import com.bmwanje.luma.data.MusicTrack
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

private val Accent = Color(0xFFB7F36B)
private val SecondaryText = Color(0xFFADB5C2)

@Composable
fun NowPlayingScreen(
    controller: MediaController?,
    item: MediaItem,
    track: MusicTrack?,
    isFavorite: Boolean,
    onBack: () -> Unit,
    onToggleFavorite: () -> Unit,
    queue: List<MusicTrack>
) {
    var tick by remember { mutableIntStateOf(0) }
    var dragPosition by remember { mutableFloatStateOf(-1f) }
    LaunchedEffect(controller) {
        while (isActive) {
            tick++
            delay(250)
        }
    }
    @Suppress("UNUSED_VARIABLE")
    val refresh = tick

    val title = item.mediaMetadata.title?.toString()?.takeIf { it.isNotBlank() } ?: "Unknown track"
    val artist = item.mediaMetadata.artist?.toString()?.takeIf { it.isNotBlank() } ?: "Unknown artist"
    val duration = (controller?.duration?.takeIf { it > 0L } ?: track?.durationMs ?: 0L).coerceAtLeast(0L)
    val position = (controller?.currentPosition ?: 0L).coerceIn(0L, duration.coerceAtLeast(0L))
    val displayedPosition = if (dragPosition >= 0f) dragPosition.toLong() else position
    val hasQueue = queue.size > 1
    val repeat = controller?.repeatMode ?: Player.REPEAT_MODE_OFF
    val shuffle = controller?.shuffleModeEnabled == true

    Box(
        modifier = Modifier.fillMaxSize().background(
            Brush.verticalGradient(
                listOf(Color(0xFF25352E), Color(0xFF11161D), Color(0xFF0B0D12), Color(0xFF0B0D12))
            )
        )
    ) {
        Column(
            modifier = Modifier.fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 26.dp, vertical = 14.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                IconButton(onClick = onBack, modifier = Modifier.size(46.dp)) {
                    Icon(Icons.Rounded.ArrowBack, contentDescription = "Back", tint = Color.White)
                }
                Spacer(Modifier.weight(1f))
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("NOW PLAYING", color = Accent, fontSize = 10.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
                    Text("From your library", color = SecondaryText, fontSize = 11.sp)
                }
                Spacer(Modifier.weight(1f))
                Spacer(Modifier.size(46.dp))
            }

            Spacer(Modifier.height(22.dp))
            Box(
                modifier = Modifier.fillMaxWidth().aspectRatio(1f)
                    .clip(RoundedCornerShape(28.dp))
                    .background(Color(0xFF1A2420))
            ) {
                TrackArtwork(
                    artworkUri = item.mediaMetadata.artworkUri,
                    label = title,
                    modifier = Modifier.fillMaxSize(),
                    seed = item.mediaId.hashCode()
                )
            }
            Spacer(Modifier.height(28.dp))

            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(title, fontSize = 24.sp, fontWeight = FontWeight.Bold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    Spacer(Modifier.height(5.dp))
                    Text(artist, color = SecondaryText, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (track?.album?.isNotBlank() == true) {
                        Spacer(Modifier.height(3.dp))
                        Text(track.album, color = SecondaryText.copy(alpha = 0.7f), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                IconButton(onClick = onToggleFavorite, modifier = Modifier.size(52.dp)) {
                    Icon(
                        if (isFavorite) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                        contentDescription = if (isFavorite) "Remove favourite" else "Add favourite",
                        tint = if (isFavorite) Accent else Color.White,
                        modifier = Modifier.size(27.dp)
                    )
                }
            }

            Spacer(Modifier.height(18.dp))
            Slider(
                value = displayedPosition.toFloat().coerceIn(0f, duration.coerceAtLeast(1L).toFloat()),
                onValueChange = { dragPosition = it },
                onValueChangeFinished = {
                    if (dragPosition >= 0f) controller?.seekTo(dragPosition.toLong())
                    dragPosition = -1f
                },
                valueRange = 0f..duration.coerceAtLeast(1L).toFloat(),
                enabled = controller != null && duration > 0L
            )
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(formatDuration(displayedPosition), color = SecondaryText, fontSize = 11.sp)
                Text("-" + formatDuration((duration - displayedPosition).coerceAtLeast(0L)), color = SecondaryText, fontSize = 11.sp)
            }

            Spacer(Modifier.height(12.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                IconButton(onClick = { controller?.shuffleModeEnabled = !shuffle }, enabled = controller != null) {
                    Icon(Icons.Rounded.Shuffle, contentDescription = "Shuffle", tint = if (shuffle) Accent else SecondaryText)
                }
                IconButton(onClick = { controller?.seekToPreviousMediaItem() }, enabled = controller != null && hasQueue) {
                    Icon(Icons.Rounded.SkipPrevious, contentDescription = "Previous track", tint = Color.White, modifier = Modifier.size(32.dp))
                }
                IconButton(
                    onClick = {
                        if (controller?.isPlaying == true) controller.pause() else controller?.play()
                    },
                    modifier = Modifier.size(76.dp).clip(CircleShape).background(Accent),
                    enabled = controller != null
                ) {
                    Icon(
                        if (controller?.isPlaying == true) Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                        contentDescription = if (controller?.isPlaying == true) "Pause" else "Play",
                        tint = Color(0xFF14200C),
                        modifier = Modifier.size(43.dp)
                    )
                }
                IconButton(onClick = { controller?.seekToNextMediaItem() }, enabled = controller != null && hasQueue) {
                    Icon(Icons.Rounded.SkipNext, contentDescription = "Next track", tint = Color.White, modifier = Modifier.size(32.dp))
                }
                IconButton(
                    onClick = {
                        controller?.repeatMode = when (repeat) {
                            Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                            Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                            else -> Player.REPEAT_MODE_OFF
                        }
                    },
                    enabled = controller != null
                ) {
                    Icon(
                        if (repeat == Player.REPEAT_MODE_ONE) Icons.Rounded.RepeatOne else Icons.Rounded.Repeat,
                        contentDescription = "Repeat mode",
                        tint = if (repeat != Player.REPEAT_MODE_OFF) Accent else SecondaryText
                    )
                }
            }

            Spacer(Modifier.height(18.dp))
            Text(
                "GOOD MUSIC. GOOD MOOD.",
                color = Color(0xFF657269),
                fontSize = 10.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp,
                textAlign = TextAlign.Center
            )
            Spacer(Modifier.height(22.dp))
        }
    }
}
