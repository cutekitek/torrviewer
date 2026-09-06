package org.torrview.app

import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import java.util.Locale

@Composable
private fun Action(tv: Boolean, label: String, enabled: Boolean = true, modifier: Modifier = Modifier,
                   onClick: () -> Unit) {
    if (tv) {
        androidx.tv.material3.Button(onClick, modifier = modifier, enabled = enabled) {
            androidx.tv.material3.Text(label)
        }
    } else Button(onClick, modifier = modifier, enabled = enabled) { Text(label) }
}

@Composable
fun TorrviewApp(model: PlayerViewModel, tv: Boolean, pick: (Boolean) -> Unit, keepScreenOn: (Boolean) -> Unit) {
    val state by model.state.collectAsStateWithLifecycle()
    val player by model.playerVisible.collectAsStateWithLifecycle()
    val message by model.message.collectAsStateWithLifecycle()
    val documentTitle by model.documentTitle.collectAsStateWithLifecycle()
    val settings by model.settings.collectAsStateWithLifecycle()
    val previous by model.previousAvailable.collectAsStateWithLifecycle()
    var settingsOpen by rememberSaveable { mutableStateOf(false) }
    var tracks by rememberSaveable { mutableStateOf("") }
    LaunchedEffect(player, state.paused) { keepScreenOn(player && !state.paused) }
    DisposableEffect(Unit) { onDispose { keepScreenOn(false) } }
    BackHandler(player && !settingsOpen && tracks.isEmpty()) { model.stop() }

    Surface(modifier = Modifier.fillMaxSize()) {
        if (player) {
            PlayerScreen(if (state.activeFile < 0) state.copy(title = documentTitle.ifEmpty { state.title }) else state,
                tv, model, message, audio = { tracks = "Audio" },
                subtitles = { tracks = "Subtitles" }, settings = { settingsOpen = true })
        } else {
            SourceScreen(state, tv, message, previous, model::openMagnet, pick, model::selectFile,
                model::restore, { settingsOpen = true })
        }
    }
    if (settingsOpen) SettingsDialog(tv, settings, { settingsOpen = false }) {
        model.settings(it); settingsOpen = false
    }
    if (tracks.isNotEmpty()) {
        val audio = tracks == "Audio"
        Dialog(onDismissRequest = { tracks = "" }) {
            Surface(shape = MaterialTheme.shapes.large) {
                LazyColumn(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    item { Text(tracks, style = MaterialTheme.typography.headlineSmall) }
                    if (!audio) item {
                        Action(tv, if (state.subtitleIndex == -1) "✓ Off" else "Off") {
                            model.subtitleTrack(-1); tracks = ""
                        }
                    }
                    val labels = if (audio) state.audio else state.subtitles
                    items(labels.indices.toList()) { index ->
                        val selected = index == if (audio) state.audioIndex else state.subtitleIndex
                        Action(tv, (if (selected) "✓ " else "") + labels[index]) {
                            if (audio) model.audioTrack(index) else model.subtitleTrack(index)
                            tracks = ""
                        }
                    }
                    item { Action(tv, "Close") { tracks = "" } }
                }
            }
        }
    }
}

@Composable
@OptIn(ExperimentalLayoutApi::class)
private fun SourceScreen(state: EngineState, tv: Boolean, message: String, previous: Boolean,
                         openMagnet: (String, Boolean) -> Unit, pick: (Boolean) -> Unit,
                         select: (Int) -> Unit, restore: () -> Unit, settings: () -> Unit) {
    var magnet by rememberSaveable { mutableStateOf("") }
    LazyColumn(Modifier.fillMaxSize().safeDrawingPadding().padding(if (tv) 40.dp else 20.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween) {
                Column {
                    Text("Torrview", style = MaterialTheme.typography.headlineLarge)
                    Text("Your files. Ready to play.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Action(tv, "Settings", onClick = settings)
            }
        }
        item {
            OutlinedTextField(magnet, { magnet = it }, label = { Text("Magnet link") },
                modifier = Modifier.fillMaxWidth(), maxLines = 3)
        }
        item {
            FlowRow(Modifier.fillMaxWidth().padding(4.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Action(tv, "Open magnet", state.ready && magnet.isNotBlank()) { openMagnet(magnet, false) }
                Action(tv, "Video file", state.ready) { pick(false) }
                Action(tv, "Torrent file", state.ready) { pick(true) }
                if (previous) Action(tv, "Resume previous", state.ready, onClick = restore)
            }
        }
        item { Status(state, message) }
        if (state.files.isNotEmpty()) item {
            Text("Choose a file", style = MaterialTheme.typography.titleLarge)
        }
        items(state.files.sortedByDescending { it.video }, key = { it.index }) { file ->
            Action(tv, "${file.path}  ·  ${file.size / 1048576} MiB", modifier = Modifier.fillMaxWidth()) {
                select(file.index)
            }
        }
    }
}

@Composable
private fun Status(state: EngineState, message: String) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        if (message.isNotEmpty()) Text(message)
        if (state.error.isNotEmpty()) Text(state.error, color = MaterialTheme.colorScheme.error)
        Text(if (state.loaded) state.status else state.torrentStatus.ifEmpty { state.status })
        if (state.buffering) {
            LinearProgressIndicator(progress = { state.bufferProgress.toFloat().coerceIn(0f, 1f) },
                modifier = Modifier.fillMaxWidth())
            Text("Buffering…")
        }
        if (state.files.isNotEmpty()) {
            Text("${state.downloadRate / 1024} KiB/s · ${state.peers} peers · " +
                "${state.cacheUsed / 1048576}/${state.cacheLimit / 1048576} MiB torrent cache",
                style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun PlayerScreen(state: EngineState, tv: Boolean, model: PlayerViewModel, message: String,
                         audio: () -> Unit, subtitles: () -> Unit, settings: () -> Unit) {
    var controls by rememberSaveable { mutableStateOf(true) }
    val focus = remember { FocusRequester() }
    LaunchedEffect(controls) { if (!controls) focus.requestFocus() }
    Box(Modifier.fillMaxSize().background(Color.Black).onPreviewKeyEvent { event ->
        val key = event.nativeKeyEvent
        if (key.action != KeyEvent.ACTION_DOWN) false else when (key.keyCode) {
            KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE -> { model.togglePause(); true }
            KeyEvent.KEYCODE_MEDIA_PLAY -> { model.play(); true }
            KeyEvent.KEYCODE_MEDIA_PAUSE -> { model.pause(); true }
            KeyEvent.KEYCODE_MEDIA_FAST_FORWARD -> { model.seek(state.position + 10); true }
            KeyEvent.KEYCODE_MEDIA_REWIND -> { model.seek(state.position - 10); true }
            KeyEvent.KEYCODE_DPAD_LEFT -> if (!controls) { model.seek(state.position - 10); true } else false
            KeyEvent.KEYCODE_DPAD_RIGHT -> if (!controls) { model.seek(state.position + 10); true } else false
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER ->
                if (!controls) { controls = true; true } else false
            else -> false
        }
    }) {
        AndroidView(modifier = Modifier.fillMaxSize(), factory = { context ->
            SurfaceView(context).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        tag = holder.surface
                    }
                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                        if (width > 0 && height > 0) model.surface(holder.surface, width, height)
                    }
                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        (tag as? android.view.Surface)?.let(model::detachSurface)
                        tag = null
                    }
                })
            }
        }, onRelease = { view ->
            (view.tag as? android.view.Surface)?.let(model::detachSurface)
            view.tag = null
        })
        Box(Modifier.fillMaxSize().focusRequester(focus).focusable().clickable { controls = !controls })
        if (controls) {
            PlayerControls(state, tv, message, model::seek, model::togglePause, model::stop,
                audio, subtitles, settings, hide = { controls = false })
        }
    }
}

internal fun time(seconds: Double): String {
    val total = seconds.coerceAtLeast(0.0).toLong()
    return if (total >= 3600) String.format(Locale.ROOT, "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60)
    else String.format(Locale.ROOT, "%02d:%02d", total / 60, total % 60)
}

@Composable
private fun SettingsDialog(tv: Boolean, settings: PlaybackSettings, close: () -> Unit,
                           save: (PlaybackSettings) -> Unit) {
    var cache by remember { mutableStateOf(settings.cacheMiB.toString()) }
    var upload by remember { mutableStateOf(settings.uploadKiB.toString()) }
    var connections by remember { mutableStateOf(settings.connections.toString()) }
    var proxy by remember { mutableStateOf(settings.proxy) }
    Dialog(onDismissRequest = close) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(Modifier.padding(24.dp).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Text("Settings", style = MaterialTheme.typography.headlineSmall)
                Text("Torrent cache is separate from decoder and player memory.")
                OutlinedTextField(cache, { cache = it }, label = { Text("Torrent cache · 16–512 MiB") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true)
                OutlinedTextField(upload, { upload = it }, label = { Text("Upload KiB/s · 0 = unlimited") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true)
                OutlinedTextField(connections, { connections = it }, label = { Text("Connections · 1–1000") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), singleLine = true)
                OutlinedTextField(proxy, { proxy = it }, label = { Text("Proxy URL · optional") }, singleLine = true)
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Action(tv, "Save", cache.toIntOrNull() != null && upload.toIntOrNull() != null && connections.toIntOrNull() != null) {
                        save(PlaybackSettings(cache.toInt(), upload.toInt(), connections.toInt(), proxy.trim()))
                    }
                    Action(tv, "Cancel", onClick = close)
                }
            }
        }
    }
}
