package org.torrview.app

import android.os.ParcelFileDescriptor
import android.view.Surface
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

internal object NativeBindings {
    init { System.loadLibrary("torrview") }
    external fun create(directory: ByteArray, fonts: ByteArray, cache: Int): Long
    external fun release(handle: Long)
    external fun surface(handle: Long, surface: Surface?, width: Int = 0, height: Int = 0)
    external fun openFd(handle: Long, fd: Int, autoplay: Boolean)
    external fun command(handle: Long, action: ByteArray, argument: ByteArray, amount: Double)
    external fun settings(handle: Long, cache: Int, upload: Int, connections: Int, proxy: ByteArray)
    external fun snapshot(handle: Long): ByteArray
}

// Only this owner can access the native handle; JNI never runs on the UI thread.
class NativeEngine(directory: String, settings: PlaybackSettings, prepareFonts: () -> String,
                   private val save: (EngineState) -> Unit) {
    private val worker = Executors.newSingleThreadScheduledExecutor { runnable ->
        Thread(runnable, "TorrviewEngine")
    }
    private val closed = AtomicBoolean(false)
    private val mutableState = MutableStateFlow(EngineState())
    val state = mutableState.asStateFlow()
    private var handle = 0L
    private var tick = 0
    private var lastError = ""
    private var attachedSurface: Surface? = null

    init {
        worker.execute {
            guarded {
                handle = NativeBindings.create(directory.toByteArray(), prepareFonts().toByteArray(), settings.cacheMiB)
                applySettingsNow(settings)
            }
        }
        worker.scheduleWithFixedDelay({
            if (handle != 0L) guarded {
                val next = EngineState.decode(NativeBindings.snapshot(handle))
                // Alerts run at 20 Hz; UI snapshots are published at 4 Hz.
                if (tick++ % 5 == 0) mutableState.value = next.copy(error = lastError.ifEmpty { next.error })
                if (tick % 40 == 0 && next.loaded && next.duration > 0) save(next)
            }
        }, 0, 50, TimeUnit.MILLISECONDS)
    }

    private fun guarded(block: () -> Unit) {
        try { block() } catch (error: Exception) {
            lastError = error.message ?: "Playback operation failed"
            mutableState.value = mutableState.value.copy(error = lastError)
        } catch (error: LinkageError) {
            lastError = "Cannot load native playback libraries: ${error.message}"
            mutableState.value = mutableState.value.copy(error = lastError)
        }
    }

    @Synchronized private fun enqueue(cleanup: () -> Unit = {}, block: () -> Unit) {
        if (closed.get()) { cleanup(); return }
        worker.execute {
            try { guarded { check(handle != 0L) { lastError.ifEmpty { "Playback engine is unavailable" } }; block() } }
            finally { cleanup() }
        }
    }

    fun command(action: String, argument: String = "", amount: Double = 0.0,
                accepted: () -> Unit = {}) = enqueue {
        if (action == "magnet" || action == "torrent" || action == "file") lastError = ""
        NativeBindings.command(handle, action.toByteArray(), argument.toByteArray(), amount)
        accepted()
    }
    fun surface(surface: Surface, width: Int, height: Int) = enqueue {
        if (surface.isValid) {
            NativeBindings.surface(handle, surface, width, height)
            attachedSurface = surface
        }
    }
    fun detachSurface(surface: Surface) = enqueue {
        // A disposed old AndroidView must not detach its replacement's surface.
        if (attachedSurface === surface) {
            NativeBindings.surface(handle, null)
            attachedSurface = null
        }
    }
    fun resumeTorrent(index: Int, position: Double) = enqueue {
        lastError = ""
        // Keep the two commands in one worker task so tick cannot autoplay between them.
        NativeBindings.command(handle, "file".toByteArray(), byteArrayOf(), index.toDouble())
        NativeBindings.command(handle, "resumePosition".toByteArray(), byteArrayOf(), position)
    }
    fun openFd(descriptor: ParcelFileDescriptor, resumePosition: Double? = null, autoplay: Boolean = true,
               accepted: () -> Unit = {}) = enqueue(
        cleanup = { descriptor.close() },
    ) {
        lastError = ""
        NativeBindings.openFd(handle, descriptor.fd, autoplay && resumePosition == null)
        accepted()
        if (resumePosition != null) {
            NativeBindings.command(handle, "resumePosition".toByteArray(), byteArrayOf(), resumePosition)
        }
    }
    private fun applySettingsNow(settings: PlaybackSettings) = NativeBindings.settings(
        handle, settings.cacheMiB, settings.uploadKiB, settings.connections, settings.proxy.toByteArray(),
    )
    fun settings(settings: PlaybackSettings) = enqueue { applySettingsNow(settings.clamped()) }

    @Synchronized fun close() {
        if (!closed.compareAndSet(false, true)) return
        // Tasks already accepted run before teardown. No waiting on the main thread.
        worker.execute {
            if (handle != 0L) guarded {
                val final = EngineState.decode(NativeBindings.snapshot(handle))
                if (final.loaded) save(final)
            }
            if (handle != 0L) NativeBindings.release(handle)
            handle = 0
        }
        worker.shutdown()
    }
}
