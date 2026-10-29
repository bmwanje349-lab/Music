package com.bmwanje.luma.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val LumaColors = darkColorScheme(
    primary = Color(0xFFB7F36B),
    onPrimary = Color(0xFF16200D),
    secondary = Color(0xFF82D4C4),
    onSecondary = Color(0xFF06211C),
    background = Color(0xFF0B0D12),
    onBackground = Color(0xFFF2F4F0),
    surface = Color(0xFF151923),
    onSurface = Color(0xFFF2F4F0),
    surfaceVariant = Color(0xFF232937),
    onSurfaceVariant = Color(0xFFADB5C2),
    outline = Color(0xFF343B49)
)

@Composable
fun LumaTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = LumaColors, content = content)
}
