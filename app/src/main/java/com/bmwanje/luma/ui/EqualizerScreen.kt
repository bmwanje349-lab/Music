package com.bmwanje.luma.ui

import androidx.compose.foundation.background
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.SurroundSound
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.bmwanje.luma.playback.DspController
import com.bmwanje.luma.playback.DspPreset
import com.bmwanje.luma.playback.SoundEngine
import java.util.Locale

private val Accent = Color(0xFFB7F36B)
private val SecondaryText = Color(0xFFADB5C2)
private val Panel = Color(0xFF151923)

@Composable
fun EqualizerScreen() {
    val settings by DspController.settings.collectAsState()
    Column(
        modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())
            .padding(horizontal = 18.dp, vertical = 18.dp),
        verticalArrangement = Arrangement.spacedBy(17.dp)
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Text("Make it sound like you.", fontSize = 27.sp, fontWeight = FontWeight.Bold)
            Text("Fun first. Fine-tuning when you want it.", color = SecondaryText, fontSize = 13.sp)
        }

        Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Panel)) {
            Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Rounded.SurroundSound, contentDescription = null, tint = Accent, modifier = Modifier.size(24.dp))
                    Spacer(Modifier.width(10.dp))
                    Column {
                        Text("Choose your sound", fontSize = 17.sp, fontWeight = FontWeight.Bold)
                        Text("Only one enhancement engine runs at once.", color = SecondaryText, fontSize = 12.sp)
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    SoundEngine.entries.forEach { engine ->
                        FilterChip(
                            selected = settings.engine == engine,
                            onClick = { DspController.selectEngine(engine) },
                            label = { Text(engine.label) }
                        )
                    }
                }
                val desc = when (settings.engine) {
                    SoundEngine.OFF -> "Enhancement bypassed. Your EQ and final safety stage remain separate."
                    SoundEngine.CONSUMER -> "Punch, bass presence, clarity and space with one easy control."
                    SoundEngine.HALO_BLOOM -> "Halo Bloom immersion, with its own spacious character."
                }
                Text(desc, color = SecondaryText, fontSize = 13.sp, lineHeight = 19.sp)
                if (settings.engine == SoundEngine.CONSUMER) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("Fun", fontWeight = FontWeight.Bold)
                            Text("Go higher for a more pronounced effect.", color = SecondaryText, fontSize = 11.sp)
                        }
                        Text(settings.funPercent.toInt().toString() + "%", color = Accent, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                    }
                    Slider(
                        value = settings.funPercent,
                        onValueChange = DspController::setFunPercent,
                        valueRange = 0f..100f
                    )
                }
            }
        }

        Card(shape = RoundedCornerShape(24.dp), colors = CardDefaults.cardColors(containerColor = Panel)) {
            Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Rounded.GraphicEq, contentDescription = null, tint = Accent, modifier = Modifier.size(24.dp))
                    Spacer(Modifier.width(10.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text("16-band equalizer", fontSize = 17.sp, fontWeight = FontWeight.Bold)
                        Text("Shape bass, mids and treble to your taste.", color = SecondaryText, fontSize = 12.sp)
                    }
                    Switch(checked = settings.peqEnabled, onCheckedChange = DspController::setPeqEnabled)
                }
                Row(
                    modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(7.dp)
                ) {
                    DspPreset.entries.forEach { preset ->
                        FilterChip(
                            selected = settings.gainsDb == preset.gainsDb,
                            onClick = { DspController.applyPreset(preset) },
                            label = { Text(preset.title) }
                        )
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Band", color = SecondaryText, fontSize = 11.sp, modifier = Modifier.weight(1f))
                    Text("Gain", color = SecondaryText, fontSize = 11.sp)
                }
                DspPreset.frequenciesHz.forEachIndexed { index, frequency ->
                    val gain = settings.gainsDb[index]
                    Column(verticalArrangement = Arrangement.spacedBy(0.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                formatFrequency(frequency),
                                modifier = Modifier.weight(1f),
                                fontSize = 13.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                String.format(Locale.getDefault(), "%+.1f dB", gain),
                                color = if (kotlin.math.abs(gain) < 0.05f) SecondaryText else Accent,
                                fontSize = 12.sp,
                                fontWeight = FontWeight.SemiBold
                            )
                        }
                        Slider(
                            value = gain,
                            onValueChange = { DspController.setBandGain(index, it) },
                            valueRange = -12f..12f,
                            enabled = settings.peqEnabled
                        )
                    }
                }
                Text("Tip: small moves are usually enough. Boosting several bands at once can reduce headroom.", color = SecondaryText, fontSize = 12.sp, lineHeight = 18.sp)
            }
        }

        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Rounded.Tune, contentDescription = null, tint = Accent)
            Spacer(Modifier.width(8.dp))
            Text("Saved automatically on this device.", color = SecondaryText, fontSize = 12.sp)
        }
        Spacer(Modifier.height(8.dp))
    }
}
