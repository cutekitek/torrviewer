package org.torrview.app

import android.app.ActivityManager
import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.net.ConnectivityManager
import android.net.Network
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.view.Surface
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class PlayerViewModel(application: Application) : AndroidViewModel(application) {
    private val prefs = application.getSharedPreferences("torrview", Context.MODE_PRIVATE)
    private val lowMemory = application.getSystemService(ActivityManager::class.java).isLowRamDevice
    private val mutableSettings = MutableStateFlow(PlaybackSettings(
        prefs.getInt("cache", if (lowMemory) 64 else 128), prefs.getInt("upload", 1024),
        prefs.getInt("connections", 100), prefs.getString("proxy", "") ?: "",
    ).clamped())
    val settings = mutableSettings.asStateFlow()
    private val engine = NativeEngine(File(application.cacheDir, "metadata").absolutePath, settings.value, {
        // OpenSSL has no Android CA store integration. Export the platform's default trust roots.
        val trust = javax.net.ssl.TrustManagerFactory.getInstance(javax.net.ssl.TrustManagerFactory.getDefaultAlgorithm())
        trust.init(null as java.security.KeyStore?)
        val manager = trust.trustManagers.filterIsInstance<javax.net.ssl.X509TrustManager>().first()
        val metadata = File(application.cacheDir, "metadata").apply { mkdirs() }
        File(metadata, "cacerts.pem").bufferedWriter().use { out ->
            manager.acceptedIssuers.forEach { certificate ->
                out.appendLine("-----BEGIN CERTIFICATE-----")
                out.appendLine(android.util.Base64.encodeToString(certificate.encoded, android.util.Base64.NO_WRAP)
                    .chunked(64).joinToString("\n"))
                out.appendLine("-----END CERTIFICATE-----")
            }
        }
        val fonts = File(application.filesDir, "fonts").apply { mkdirs() }
        val font = File(fonts, "NotoSans-Regular.ttf")
        if (!font.exists()) application.assets.open("fonts/NotoSans-Regular.ttf").use { input ->
            font.outputStream().use { output -> input.copyTo(output) }
        }
        fonts.absolutePath
    }) {
        prefs.edit().putFloat("position", it.position.toFloat()).putInt("file", it.activeFile).apply()
    }
    val state = engine.state
    val playerVisible = MutableStateFlow(false)
    val message = MutableStateFlow("")
    val documentTitle = MutableStateFlow("")
    val previousAvailable = MutableStateFlow(!prefs.getString("source", "").isNullOrEmpty())
    private val audio = application.getSystemService(AudioManager::class.java)
    private val focus = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
        .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build())
        .setOnAudioFocusChangeListener({ change ->
            if (change != AudioManager.AUDIOFOCUS_GAIN) pause()
        }, Handler(Looper.getMainLooper())).build()
    private val mediaSession = MediaSession(application, "Torrview").apply {
        setCallback(object : MediaSession.Callback() {
            override fun onPlay() = play()
            override fun onPause() = pause()
            override fun onStop() = stop()
            override fun onSeekTo(pos: Long) = seek(pos / 1000.0)
            override fun onFastForward() = seek(state.value.position + 10)
            override fun onRewind() = seek(state.value.position - 10)
        }, Handler(Looper.getMainLooper()))
    }
    private val connectivity = application.getSystemService(ConnectivityManager::class.java)
    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) { engine.command("network") }
        override fun onLost(network: Network) { engine.command("network") }
    }
    private val noisyReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) { pause() }
    }
    private var pendingResume: Pair<Int, Double>? = null
    private var sourceRequest = 0L

    init {
        connectivity.registerDefaultNetworkCallback(networkCallback)
        val filter = IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY)
        if (Build.VERSION.SDK_INT >= 33) {
            application.registerReceiver(noisyReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            application.registerReceiver(noisyReceiver, filter)
        }
        viewModelScope.launch {
            state.collect { snapshot ->
                mediaSession.setPlaybackState(PlaybackState.Builder()
                    .setActions(PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
                        PlaybackState.ACTION_PLAY_PAUSE or PlaybackState.ACTION_SEEK_TO or
                        PlaybackState.ACTION_STOP or PlaybackState.ACTION_FAST_FORWARD or PlaybackState.ACTION_REWIND)
                    .setState(if (snapshot.paused) PlaybackState.STATE_PAUSED else PlaybackState.STATE_PLAYING,
                        (snapshot.position * 1000).toLong(), if (snapshot.paused) 0f else 1f).build())
                pendingResume?.let { (index, position) ->
                    if (snapshot.files.any { it.index == index }) {
                        pendingResume = null
                        playerVisible.value = true
                        engine.resumeTorrent(index, position)
                    }
                }
            }
        }
    }

    private fun rememberSource(source: String, kind: String, restoring: Boolean) {
        val edit = prefs.edit().putString("source", source).putString("kind", kind)
        if (!restoring) edit.putInt("file", -1).putFloat("position", 0f)
        edit.apply()
        previousAvailable.value = true
    }
    fun openMagnet(input: String, restoring: Boolean = false) {
        val magnet = input.trim()
        if (!magnet.startsWith("magnet:?", ignoreCase = true)) {
            message.value = "Enter a magnet link beginning with magnet:?"
            return
        }
        pendingResume = null
        sourceRequest++
        playerVisible.value = false
        message.value = ""
        engine.command("magnet", magnet) { rememberSource(magnet, "magnet", restoring) }
    }

    fun openDocument(uri: Uri, torrent: Boolean, restoring: Boolean = false, position: Double? = null) {
        val request = ++sourceRequest
        pendingResume = null
        message.value = "Opening document…"
        viewModelScope.launch {
            var pendingDescriptor: android.os.ParcelFileDescriptor? = null
            var pendingMetadata: File? = null
            try {
                val app = getApplication<Application>()
                if (torrent) {
                    val file = withContext(Dispatchers.IO) {
                        // Metadata only; no video or torrent payload is copied into app storage.
                        val bytes = app.contentResolver.openInputStream(uri)?.use { stream ->
                            val output = java.io.ByteArrayOutputStream()
                            val buffer = ByteArray(8192)
                            while (true) {
                                val read = stream.read(buffer)
                                if (read < 0) break
                                check(output.size() + read <= 16 * 1024 * 1024) { "Torrent metadata exceeds 16 MiB" }
                                output.write(buffer, 0, read)
                            }
                            output.toByteArray()
                        } ?: error("Cannot read this torrent document")
                        File.createTempFile("metadata-", ".torrent", app.filesDir).also {
                            pendingMetadata = it
                            it.writeBytes(bytes)
                        }
                    }
                    if (request != sourceRequest) {
                        withContext(Dispatchers.IO) { file.delete() }
                        return@launch
                    }
                    playerVisible.value = false
                    engine.command("torrent", file.absolutePath) {
                        val previous = prefs.getString("metadata", null)
                        rememberSource(uri.toString(), "torrent", restoring)
                        prefs.edit().putString("metadata", file.absolutePath).apply()
                        if (previous != null && previous != file.absolutePath) File(previous).delete()
                    }
                    pendingMetadata = null
                } else {
                    val (fd, title) = withContext(Dispatchers.IO) {
                        val descriptor = (app.contentResolver.openFileDescriptor(uri, "r")
                            ?: error("Cannot open this video")).also { pendingDescriptor = it }
                        val displayName = runCatching {
                            app.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME),
                                null, null, null)?.use { cursor ->
                                if (cursor.moveToFirst()) cursor.getString(0) else null
                            }
                        }.getOrNull()
                        descriptor to (displayName ?: uri.lastPathSegment?.substringAfterLast('/') ?: "Torrview")
                    }
                    if (request != sourceRequest) { fd.close(); return@launch }
                    documentTitle.value = title
                    playerVisible.value = true
                    engine.openFd(fd, position, autoplay = position == null && requestFocus()) {
                        rememberSource(uri.toString(), "media", restoring)
                    }
                    pendingDescriptor = null
                }
                message.value = ""
            } catch (error: Exception) {
                if (error is kotlinx.coroutines.CancellationException) throw error
                if (request == sourceRequest) message.value = error.message ?: "Cannot open this document"
            } finally {
                pendingDescriptor?.close()
                pendingMetadata?.let { file -> withContext(kotlinx.coroutines.NonCancellable + Dispatchers.IO) { file.delete() } }
            }
        }
    }

    fun restore() {
        val source = prefs.getString("source", "") ?: return
        val kind = prefs.getString("kind", "") ?: return
        val file = prefs.getInt("file", -1)
        val position = prefs.getFloat("position", 0f).toDouble()
        when (kind) {
            "magnet" -> {
                openMagnet(source, restoring = true)
                if (file >= 0) pendingResume = file to position
            }
            "torrent" -> {
                // A private metadata copy survives eviction of cacheDir and lost provider access.
                val metadata = prefs.getString("metadata", null)?.let(::File)
                if (metadata?.exists() == true) {
                    engine.command("torrent", metadata.absolutePath)
                    if (file >= 0) pendingResume = file to position
                } else openDocument(Uri.parse(source), torrent = true, restoring = true)
            }
            "media" -> openDocument(Uri.parse(source), torrent = false, restoring = true, position = position)
        }
    }
    fun selectFile(index: Int) {
        if (!requestFocus()) return
        pendingResume = null
        playerVisible.value = true
        engine.command("file", amount = index.toDouble())
    }
    private fun requestFocus(): Boolean {
        val granted = audio.requestAudioFocus(focus) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        if (!granted) message.value = "Audio is in use. Try Play again when it becomes available."
        return granted
    }
    fun play() { if (requestFocus()) engine.command("play") }
    fun pause() { engine.command("pause") }
    fun togglePause() { if (state.value.paused) play() else pause() }
    fun seek(seconds: Double) { engine.command("seek", amount = seconds.coerceIn(0.0, state.value.duration.coerceAtLeast(0.0))) }
    fun audioTrack(index: Int) { engine.command("audio", amount = index.toDouble()) }
    fun subtitleTrack(index: Int) { engine.command("subtitle", amount = index.toDouble()) }
    fun stop() {
        sourceRequest++
        pendingResume = null
        engine.command("stop")
        playerVisible.value = false
        audio.abandonAudioFocusRequest(focus)
    }
    fun surface(surface: Surface, width: Int, height: Int) = engine.surface(surface, width, height)
    fun detachSurface(surface: Surface) = engine.detachSurface(surface)
    fun foreground(active: Boolean) {
        engine.command("foreground", amount = if (active) 1.0 else 0.0)
        mediaSession.isActive = active
        if (!active) audio.abandonAudioFocusRequest(focus)
    }
    fun trimMemory() { engine.command("cache", amount = settings.value.cacheMiB.coerceAtMost(64).toDouble()) }
    fun settings(next: PlaybackSettings) {
        val value = next.clamped()
        mutableSettings.value = value
        prefs.edit().putInt("cache", value.cacheMiB).putInt("upload", value.uploadKiB)
            .putInt("connections", value.connections).putString("proxy", value.proxy).apply()
        engine.settings(value)
    }
    override fun onCleared() {
        getApplication<Application>().unregisterReceiver(noisyReceiver)
        connectivity.unregisterNetworkCallback(networkCallback)
        mediaSession.release()
        audio.abandonAudioFocusRequest(focus)
        engine.close()
    }
}
