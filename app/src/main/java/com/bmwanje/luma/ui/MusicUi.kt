package com.bmwanje.luma.ui

import android.net.Uri
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AccessTime
import androidx.compose.material.icons.rounded.ChevronRight
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.FavoriteBorder
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Headphones
import androidx.compose.material.icons.rounded.LibraryMusic
import androidx.compose.material.icons.rounded.MusicNote
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Shuffle
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.media3.common.MediaItem
import coil.compose.AsyncImage
import com.bmwanje.luma.data.MusicTrack

private val LumaLime = Color(0xFFB7F36B)
private val CardColor = Color(0xFF151923)
private val MutedText = Color(0xFFADB5C2)

@Composable
fun HomeScreen(
    tracks: List<MusicTrack>,
    favorites: Set<Long>,
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
    onPlayTrack: (MusicTrack) -> Unit,
    onPlayAll: () -> Unit,
    onShuffle: () -> Unit,
    onOpenLibrary: () -> Unit,
    onOpenSound: () -> Unit,
    onToggleFavorite: (MusicTrack) -> Unit
) {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())
            .padding(horizontal = 20.dp, vertical = 18.dp),
        verticalArrangement = Arrangement.spacedBy(22.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Text("L U M A", color = LumaLime, fontSize = 12.sp, fontWeight = FontWeight.ExtraBold)
                Spacer(Modifier.height(7.dp))
                Text("Find your feeling.", fontSize = 29.sp, fontWeight = FontWeight.Bold, lineHeight = 34.sp)
                Text("Your music, exactly how you like it.", color = MutedText, fontSize = 14.sp)
            }
            IconButton(
                onClick = onOpenSound,
                modifier = Modifier.size(48.dp).clip(CircleShape).background(Color(0xFF1C242B))
            ) {
                Icon(Icons.Rounded.GraphicEq, contentDescription = "Sound settings", tint = LumaLime)
            }
        }

        HeroCard(
            count = tracks.size,
            onPlayAll = if (hasPermission) onPlayAll else onRequestPermission,
            onShuffle = if (hasPermission) onShuffle else onRequestPermission
        )

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
            QuickAction(
                title = "Your library",
                subtitle = tracks.size.toString() + " tracks",
                icon = { Icon(Icons.Rounded.LibraryMusic, contentDescription = null, tint = LumaLime) },
                modifier = Modifier.weight(1f),
                onClick = onOpenLibrary
            )
            QuickAction(
                title = "Make it yours",
                subtitle = "Sound & EQ",
                icon = { Icon(Icons.Rounded.Tune, contentDescription = null, tint = Color(0xFF82D4C4)) },
                modifier = Modifier.weight(1f),
                onClick = onOpenSound
            )
        }

        if (!hasPermission) {
            PermissionCard(onRequestPermission)
        } else if (tracks.isEmpty()) {
            EmptyLibraryCard()
        } else {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Back in rotation", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                    Text("A little music goes a long way.", color = MutedText, fontSize = 13.sp)
                }
                TextButton(onClick = onOpenLibrary) {
                    Text("All songs", color = LumaLime)
                    Icon(Icons.Rounded.ChevronRight, contentDescription = null, tint = LumaLime)
                }
            }

            tracks.sortedByDescending { it.dateAddedSeconds }.take(6).forEach { track ->
                TrackRow(
                    track = track,
                    isFavorite = track.id in favorites,
                    onPlay = { onPlayTrack(track) },
                    onToggleFavorite = { onToggleFavorite(track) }
                )
            }

            if (favorites.isNotEmpty()) {
                Card(
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF171E1A)),
                    shape = RoundedCornerShape(20.dp),
                    modifier = Modifier.fillMaxWidth().clickable(onClick = onOpenLibrary)
                ) {
                    Row(modifier = Modifier.padding(18.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Rounded.Favorite, contentDescription = null, tint = LumaLime)
                        Spacer(Modifier.width(12.dp))
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Your favourites", fontWeight = FontWeight.SemiBold)
                            Text(favorites.size.toString() + " tracks saved for later", color = MutedText, fontSize = 13.sp)
                        }
                        Icon(Icons.Rounded.ChevronRight, contentDescription = null, tint = MutedText)
                    }
                }
            }
        }

        Text(
            "MADE FOR THE WAY YOU LISTEN",
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp, bottom = 6.dp),
            color = Color(0xFF616B7A),
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 2.sp
        )
    }
}

@Composable
private fun HeroCard(count: Int, onPlayAll: () -> Unit, onShuffle: () -> Unit) {
    Box(
        modifier = Modifier.fillMaxWidth().height(238.dp)
            .clip(RoundedCornerShape(28.dp))
            .background(Brush.linearGradient(listOf(Color(0xFF303E35), Color(0xFF25312D), Color(0xFF152128))))
    ) {
        Canvas(Modifier.align(Alignment.CenterEnd).size(220.dp)) {
            val center = Offset(size.width * 0.6f, size.height * 0.5f)
            drawCircle(Color.White.copy(alpha = 0.055f), radius = size.minDimension * 0.49f, center = center)
            drawCircle(Color.White.copy(alpha = 0.08f), radius = size.minDimension * 0.36f, center = center, style = Stroke(2.dp.toPx()))
            drawCircle(LumaLime.copy(alpha = 0.22f), radius = size.minDimension * 0.20f, center = center)
            drawCircle(LumaLime.copy(alpha = 0.8f), radius = size.minDimension * 0.045f, center = center)
        }
        Column(
            modifier = Modifier.fillMaxSize().padding(20.dp),
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text("YOUR MUSIC, YOUR MOOD", color = LumaLime, fontSize = 10.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
                Spacer(Modifier.height(10.dp))
                Text("Press play.\nFeel everything.", fontSize = 26.sp, lineHeight = 30.sp, fontWeight = FontWeight.ExtraBold, color = Color.White)
                Spacer(Modifier.height(7.dp))
                Text(count.toString() + " tracks ready when you are.", color = Color.White.copy(alpha = 0.72f), fontSize = 13.sp)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(9.dp), verticalAlignment = Alignment.CenterVertically) {
                Button(onClick = onPlayAll, shape = RoundedCornerShape(50)) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                    Spacer(Modifier.width(4.dp))
                    Text("Play music")
                }
                FilledTonalButton(onClick = onShuffle, shape = RoundedCornerShape(50)) {
                    Icon(Icons.Rounded.Shuffle, contentDescription = "Shuffle")
                }
            }
        }
    }
}

@Composable
private fun QuickAction(
    title: String,
    subtitle: String,
    icon: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    onClick: () -> Unit
) {
    Card(
        modifier = modifier.height(96.dp).clickable(onClick = onClick),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = CardColor)
    ) {
        Column(modifier = Modifier.fillMaxSize().padding(14.dp), verticalArrangement = Arrangement.SpaceBetween) {
            icon()
            Column {
                Text(title, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text(subtitle, color = MutedText, fontSize = 11.sp)
            }
        }
    }
}

@Composable
fun LibraryScreen(
    tracks: List<MusicTrack>,
    favorites: Set<Long>,
    hasPermission: Boolean,
    onRequestPermission: () -> Unit,
    onRefresh: () -> Unit,
    onPlayTrack: (MusicTrack) -> Unit,
    onToggleFavorite: (MusicTrack) -> Unit
) {
    var search by remember { mutableStateOf("") }
    var filter by remember { mutableStateOf("All songs") }
    val filters = listOf("All songs", "Favourites", "Recently added")
    val base = when (filter) {
        "Favourites" -> tracks.filter { it.id in favorites }
        "Recently added" -> tracks.sortedByDescending { it.dateAddedSeconds }
        else -> tracks
    }
    val visible = base.filter {
        search.isBlank() || it.title.contains(search, true) || it.artist.contains(search, true) || it.album.contains(search, true)
    }

    Column(modifier = Modifier.fillMaxSize().padding(horizontal = 18.dp, vertical = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Text("Your library", fontSize = 28.sp, fontWeight = FontWeight.Bold)
                Text(tracks.size.toString() + " songs stored on this device", color = MutedText, fontSize = 13.sp)
            }
            IconButton(onClick = onRefresh) {
                Icon(Icons.Rounded.Refresh, contentDescription = "Rescan music", tint = LumaLime)
            }
        }
        Spacer(Modifier.height(16.dp))
        OutlinedTextField(
            value = search,
            onValueChange = { search = it },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            shape = RoundedCornerShape(18.dp),
            placeholder = { Text("Search songs, artists, albums…") },
            leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null, tint = MutedText) },
            colors = OutlinedTextFieldDefaults.colors(
                focusedBorderColor = LumaLime,
                unfocusedBorderColor = Color(0xFF303746),
                focusedContainerColor = CardColor,
                unfocusedContainerColor = CardColor
            )
        )
        Spacer(Modifier.height(12.dp))
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            filters.forEach { option ->
                FilterChip(selected = filter == option, onClick = { filter = option }, label = { Text(option) })
            }
        }

        if (!hasPermission) {
            Spacer(Modifier.height(18.dp))
            PermissionCard(onRequestPermission)
        } else if (visible.isEmpty()) {
            Spacer(Modifier.height(26.dp))
            Column(
                modifier = Modifier.fillMaxWidth().padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Icon(Icons.Rounded.LibraryMusic, contentDescription = null, tint = MutedText, modifier = Modifier.size(42.dp))
                Spacer(Modifier.height(12.dp))
                Text(if (search.isBlank()) "Your music will show up here" else "No matches found", fontWeight = FontWeight.SemiBold)
                Text(
                    if (search.isBlank()) "Add audio files to your device, then tap rescan."
                    else "Try another title, artist, or album name.",
                    color = MutedText,
                    fontSize = 13.sp
                )
            }
        } else {
            LazyColumn(modifier = Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                item {
                    Text(
                        visible.size.toString() + if (visible.size == 1) " track" else " tracks",
                        color = MutedText,
                        fontSize = 12.sp,
                        modifier = Modifier.padding(top = 12.dp, bottom = 8.dp)
                    )
                }
                items(visible, key = { it.id }) { track ->
                    TrackRow(
                        track = track,
                        isFavorite = track.id in favorites,
                        onPlay = { onPlayTrack(track) },
                        onToggleFavorite = { onToggleFavorite(track) }
                    )
                }
            }
        }
    }
}

@Composable
fun TrackRow(track: MusicTrack, isFavorite: Boolean, onPlay: () -> Unit, onToggleFavorite: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).clickable(onClick = onPlay)
            .padding(horizontal = 4.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        TrackArtwork(track.artworkUri, track.title, Modifier.size(52.dp), track.id.toInt())
        Spacer(Modifier.width(12.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(track.title, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(3.dp))
            Text(track.artist + "  •  " + track.album, color = MutedText, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        IconButton(onClick = onToggleFavorite, modifier = Modifier.size(40.dp)) {
            Icon(
                if (isFavorite) Icons.Rounded.Favorite else Icons.Rounded.FavoriteBorder,
                contentDescription = if (isFavorite) "Remove from favourites" else "Add to favourites",
                tint = if (isFavorite) LumaLime else MutedText,
                modifier = Modifier.size(20.dp)
            )
        }
    }
}

@Composable
fun TrackArtwork(artworkUri: Uri?, label: String, modifier: Modifier = Modifier, seed: Int = label.hashCode()) {
    val colors = remember(seed) {
        val palette = listOf(
            Color(0xFF284239), Color(0xFF3C304A), Color(0xFF34435C),
            Color(0xFF51402F), Color(0xFF27444C), Color(0xFF4C3540)
        )
        listOf(palette[Math.floorMod(seed, palette.size)], palette[Math.floorMod(seed + 2, palette.size)])
    }
    Box(modifier = modifier.clip(RoundedCornerShape(14.dp)).background(Brush.linearGradient(colors)), contentAlignment = Alignment.Center) {
        Icon(Icons.Rounded.MusicNote, contentDescription = null, tint = Color.White.copy(alpha = 0.7f), modifier = Modifier.size(24.dp))
        if (artworkUri != null) {
            AsyncImage(
                model = artworkUri,
                contentDescription = "Artwork for " + label,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop
            )
        }
    }
}

@Composable
private fun PermissionCard(onRequestPermission: () -> Unit) {
    Card(shape = RoundedCornerShape(22.dp), colors = CardDefaults.cardColors(containerColor = CardColor), modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Icon(Icons.Rounded.Headphones, contentDescription = null, tint = LumaLime, modifier = Modifier.size(30.dp))
            Text("Let your music in", fontSize = 19.sp, fontWeight = FontWeight.Bold)
            Text("Luma scans audio already on your phone. It does not upload your music or need an account.", color = MutedText, fontSize = 13.sp)
            Button(onClick = onRequestPermission) { Text("Find my music") }
        }
    }
}

@Composable
private fun EmptyLibraryCard() {
    Card(shape = RoundedCornerShape(22.dp), colors = CardDefaults.cardColors(containerColor = CardColor), modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Icon(Icons.Rounded.AccessTime, contentDescription = null, tint = LumaLime, modifier = Modifier.size(28.dp))
            Text("Your next favourite is waiting", fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Text("Add a few audio files to your device, then open Library and rescan.", color = MutedText, fontSize = 13.sp)
        }
    }
}

@Composable
fun MiniPlayer(
    item: MediaItem,
    isPlaying: Boolean,
    positionMs: Long,
    durationMs: Long,
    onOpen: () -> Unit,
    onPlayPause: () -> Unit,
    onNext: () -> Unit
) {
    val progress = if (durationMs > 0L) (positionMs.toFloat() / durationMs).coerceIn(0f, 1f) else 0f
    Column(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 5.dp)
            .shadow(10.dp, RoundedCornerShape(20.dp)).clip(RoundedCornerShape(20.dp)).background(Color(0xFF1A2029))
    ) {
        LinearProgressIndicator(
            progress = { progress },
            modifier = Modifier.fillMaxWidth().height(2.dp),
            color = LumaLime,
            trackColor = Color(0xFF303746)
        )
        Row(
            modifier = Modifier.fillMaxWidth().clickable(onClick = onOpen).padding(start = 9.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TrackArtwork(item.mediaMetadata.artworkUri, item.mediaMetadata.title?.toString().orEmpty(), Modifier.size(42.dp), item.mediaId.hashCode())
            Spacer(Modifier.width(10.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text(item.mediaMetadata.title?.toString() ?: "Ready when you are", fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(item.mediaMetadata.artist?.toString() ?: "Luma Music", fontSize = 11.sp, color = MutedText, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            IconButton(onClick = onPlayPause) {
                Icon(
                    if (isPlaying) androidx.compose.material.icons.Icons.Rounded.Pause else Icons.Rounded.PlayArrow,
                    contentDescription = if (isPlaying) "Pause" else "Play",
                    tint = LumaLime
                )
            }
            IconButton(onClick = onNext) {
                Icon(androidx.compose.material.icons.Icons.Rounded.SkipNext, contentDescription = "Next track", tint = Color.White)
            }
        }
    }
}

@Composable
fun SettingsScreen(trackCount: Int, hasPermission: Boolean, onRescan: () -> Unit) {
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 22.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp)
    ) {
        Text("A few good things.", fontSize = 28.sp, fontWeight = FontWeight.Bold)
        Text("The little details that keep listening easy.", color = MutedText, fontSize = 14.sp)
        Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = CardColor)) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Rounded.LibraryMusic, contentDescription = null, tint = LumaLime)
                    Spacer(Modifier.width(12.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text("On-device music", fontWeight = FontWeight.SemiBold)
                        Text(trackCount.toString() + " tracks in your library", color = MutedText, fontSize = 12.sp)
                    }
                }
                Text("Your library is read from Android's media index. Luma does not upload, stream, or analyse your files in the cloud.", color = MutedText, fontSize = 13.sp)
                Text(
                    if (hasPermission) "Library permission: allowed" else "Library permission: not granted",
                    color = if (hasPermission) LumaLime else Color(0xFFE8B96A),
                    fontSize = 12.sp
                )
                Button(onClick = onRescan) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                    Spacer(Modifier.width(8.dp))
                    Text("Rescan music")
                }
            }
        }
        Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Color(0xFF171E1A))) {
            Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("LUMA MUSIC", color = LumaLime, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
                Text("Made for everyday listening", fontSize = 18.sp, fontWeight = FontWeight.Bold)
                Text("Local playback • Lock-screen controls • Saved favourites • Native sound engines • 16-band PEQ", color = MutedText, fontSize = 13.sp)
                Text("Version 1.0.0", color = MutedText, fontSize = 11.sp)
            }
        }
    }
}
