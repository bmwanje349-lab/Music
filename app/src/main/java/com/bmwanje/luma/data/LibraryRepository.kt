package com.bmwanje.luma.data

import android.content.ContentUris
import android.content.Context
import android.provider.MediaStore

object LibraryRepository {
    fun loadTracks(context: Context): List<MusicTrack> {
        val resolver = context.contentResolver
        val collection = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI
        val projection = arrayOf(
            MediaStore.Audio.Media._ID,
            MediaStore.Audio.Media.TITLE,
            MediaStore.Audio.Media.ARTIST,
            MediaStore.Audio.Media.ALBUM,
            MediaStore.Audio.Media.DURATION,
            MediaStore.Audio.Media.ALBUM_ID,
            MediaStore.Audio.Media.DATE_ADDED
        )
        val selection = "${MediaStore.Audio.Media.IS_MUSIC} != 0"
        val sortOrder = "${MediaStore.Audio.Media.TITLE} COLLATE NOCASE ASC"
        val tracks = mutableListOf<MusicTrack>()

        resolver.query(collection, projection, selection, null, sortOrder)?.use { cursor ->
            val idColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
            val titleColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
            val artistColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
            val albumColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM)
            val durationColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
            val albumIdColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM_ID)
            val addedColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_ADDED)

            while (cursor.moveToNext()) {
                val id = cursor.getLong(idColumn)
                val title = cursor.getString(titleColumn)?.takeIf { it.isNotBlank() } ?: "Unknown track"
                val artist = cursor.getString(artistColumn)
                    ?.takeIf { it.isNotBlank() && it != "<unknown>" } ?: "Unknown artist"
                val album = cursor.getString(albumColumn)
                    ?.takeIf { it.isNotBlank() && it != "<unknown>" } ?: "Unknown album"
                val duration = cursor.getLong(durationColumn)
                if (duration <= 0L) continue

                val artwork = cursor.getLong(albumIdColumn).takeIf { it > 0L }?.let {
                    android.net.Uri.parse("content://media/external/audio/albumart/$it")
                }
                tracks += MusicTrack(
                    id = id,
                    title = title,
                    artist = artist,
                    album = album,
                    durationMs = duration,
                    uri = ContentUris.withAppendedId(collection, id),
                    artworkUri = artwork,
                    dateAddedSeconds = cursor.getLong(addedColumn)
                )
            }
        }
        return tracks
    }
}
