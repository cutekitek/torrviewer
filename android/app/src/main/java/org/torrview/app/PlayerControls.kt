package org.torrview.app

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.addPathNodes
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog

// Small glyphs with full 48 dp touch/focus targets, on both phones and TV.
private enum class PlayerGlyph(val path: String) {
    Play("M8,5 L19,12 L8,19 Z"),
    Pause("M6,5 H10 V19 H6 Z M14,5 H18 V19 H14 Z"),
    Back("M20,11 H7.83 L13.42,5.41 L12,4 L4,12 L12,20 L13.42,18.59 L7.83,13 H20 Z"),
    Audio("M12,3 V13.55 A4,4 0,1 0,14,17 V7 H20 V3 Z"),
    Subtitles("M3,4 H21 V20 H3 Z M5,6 V18 H19 V6 Z M6,10 H10 V12 H6 Z M12,10 H18 V12 H12 Z M6,14 H13 V16 H6 Z M15,14 H18 V16 H15 Z"),
    Debug("M4,4 H20 V20 H4 Z M6,6 V18 H18 V6 Z M8,12 H10 V16 H8 Z M11,8 H13 V16 H11 Z M14,10 H16 V16 H14 Z"),
    Settings("M4,6 H20 V8 H4 Z M4,16 H20 V18 H4 Z M8,3 H10 V11 H8 Z M14,13 H16 V21 H14 Z"),
    Hide("M6,9 L12,15 L18,9 L19.4,10.4 L12,17.8 L4.6,10.4 Z"),
}

@Composable
private fun Control(label: String, glyph: PlayerGlyph? = null, text: String = "",
                    enabled: Boolean = true, onClick: () -> Unit) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val vector = remember(glyph) {
        glyph?.let { ImageVector.Builder(it.name, 24.dp, 24.dp, 24f, 24f)
            .addPath(addPathNodes(it.path), fill = SolidColor(Color.White)).build() }
    }
    Box(Modifier.size(48.dp)
        .background(if (focused) Color.White.copy(alpha = 0.25f) else Color.Transparent, CircleShape)
        .border(2.dp, if (focused) Color.White else Color.Transparent, CircleShape)
        .clickable(interactionSource = interaction, indication = null, enabled = enabled,
            role = Role.Button, onClickLabel = label, onClick = onClick)
        .semanticsDescription(label), contentAlignment = Alignment.Center) {
        val color = Color.White.copy(alpha = if (enabled) 1f else 0.35f)
        if (vector != null) Icon(vector, null, Modifier.size(23.dp), tint = color)
        else Text(text, color = color, style = MaterialTheme.typography.labelLarge)
    }
}

private fun Modifier.semanticsDescription(label: String) = semantics { contentDescription = label }

@Composable
internal fun PlayerControls(state: EngineState, tv: Boolean, message: String,
                            seek: (Double) -> Unit, pause: () -> Unit, stop: () -> Unit,
                            audio: () -> Unit, subtitles: () -> Unit, settings: () -> Unit,
                            hide: () -> Unit) {
    var debug by rememberSaveable { mutableStateOf(false) }
    val scrim = Color.Black.copy(alpha = 0.58f)
    Box(Modifier.fillMaxSize()) {
        Row(Modifier.align(Alignment.TopCenter).fillMaxWidth()
            .background(Brush.verticalGradient(listOf(scrim, Color.Transparent)))
            .safeDrawingPadding().padding(horizontal = if (tv) 28.dp else 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Control("Stop playback", PlayerGlyph.Back, onClick = stop)
            val title = state.files.firstOrNull { it.index == state.activeFile }?.path
                ?.substringAfterLast('/') ?: state.title
            Text(title.ifEmpty { "Torrview" }, Modifier.weight(1f).padding(horizontal = 8.dp),
                color = Color.White, style = MaterialTheme.typography.bodyMedium,
                maxLines = 1, overflow = TextOverflow.Ellipsis)
            Control("Debug information", PlayerGlyph.Debug) { debug = true }
            Control("Settings", PlayerGlyph.Settings, onClick = settings)
        }
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth()
            .background(Brush.verticalGradient(listOf(Color.Transparent, scrim)))
            .safeDrawingPadding().padding(horizontal = if (tv) 28.dp else 12.dp)) {
            BufferedSeekBar(state, seek)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically) {
                Control("Audio tracks", PlayerGlyph.Audio, enabled = state.audio.isNotEmpty(), onClick = audio)
                Control("Rewind 10 seconds", text = "−10") { seek(state.position - 10) }
                Control(if (state.paused) "Play" else "Pause",
                    if (state.paused) PlayerGlyph.Play else PlayerGlyph.Pause, onClick = pause)
                Control("Forward 10 seconds", text = "+10") { seek(state.position + 10) }
                Control("Subtitles", PlayerGlyph.Subtitles, onClick = subtitles)
                Control("Hide controls", PlayerGlyph.Hide, onClick = hide)
            }
        }
        val notice = state.error.ifEmpty { message }.ifEmpty {
            if (state.buffering) "Buffering… ${(state.bufferProgress.coerceIn(0.0, 1.0) * 100).toInt()}%"
            else if (!state.loaded || state.status == "Loading") "Opening media…" else ""
        }
        if (notice.isNotEmpty()) Text(notice,
            Modifier.align(Alignment.Center).widthIn(max = 360.dp)
                .background(Color.Black.copy(alpha = 0.65f), MaterialTheme.shapes.small).padding(12.dp),
            color = Color.White, style = MaterialTheme.typography.bodySmall, maxLines = 3)
    }
    if (debug) Dialog(onDismissRequest = { debug = false }) {
        Surface(shape = MaterialTheme.shapes.large, color = Color(0xEB14191D)) {
            Column(Modifier.padding(24.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Debug information", style = MaterialTheme.typography.titleLarge)
                Text(if (state.paused && state.loaded) "Paused" else state.status)
                if (state.activeFile >= 0) {
                    if (state.torrentStatus.isNotEmpty()) Text(state.torrentStatus)
                    Text("Download  ·  ${state.downloadRate / 1024} KiB/s")
                    Text("Peers  ·  ${state.peers}")
                    Text("Torrent cache  ·  ${state.cacheUsed / 1048576} / ${state.cacheLimit / 1048576} MiB")
                }
                Text("Buffered segments  ·  ${state.bufferedRanges.size}")
                Text("Light segments on the seek bar show media buffered by the player.",
                    style = MaterialTheme.typography.bodySmall)
                TextButton(onClick = { debug = false }) { Text("Close") }
            }
        }
    }
}

@Composable
@OptIn(ExperimentalMaterial3Api::class)
internal fun BufferedSeekBar(state: EngineState, seek: (Double) -> Unit) {
    var dragging by remember { mutableStateOf(false) }
    var position by remember { mutableFloatStateOf(0f) }
    val duration = state.duration.toFloat().coerceAtLeast(1f)
    val value = (if (dragging) position else state.position.toFloat()).coerceIn(0f, duration)
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(time(value.toDouble()), color = Color.White, style = MaterialTheme.typography.labelSmall)
        Slider(value = value, onValueChange = { dragging = true; position = it },
            onValueChangeFinished = { seek(position.toDouble()); dragging = false },
            modifier = Modifier.weight(1f).semanticsDescription("Playback position"),
            valueRange = 0f..duration, enabled = state.duration > 0,
            thumb = { Box(Modifier.size(12.dp).background(Color(0xFF2EC4B6), CircleShape)) },
            track = {
                Canvas(Modifier.fillMaxWidth().height(4.dp)) {
                    val y = size.height / 2
                    fun segment(start: Float, end: Float, color: Color) {
                        drawLine(color, Offset(start * size.width, y), Offset(end * size.width, y),
                            strokeWidth = size.height, cap = StrokeCap.Round)
                    }
                    segment(0f, 1f, Color.White.copy(alpha = 0.22f))
                    if (state.duration > 0) state.bufferedRanges.forEach { range ->
                        val start = (range.start / state.duration).toFloat().coerceIn(0f, 1f)
                        val end = (range.end / state.duration).toFloat().coerceIn(0f, 1f)
                        if (end > start) segment(start, end, Color.White.copy(alpha = 0.62f))
                    }
                    segment(0f, value / duration, Color(0xFF2EC4B6))
                }
            })
        Text(time(state.duration), color = Color.White, style = MaterialTheme.typography.labelSmall)
    }
}
