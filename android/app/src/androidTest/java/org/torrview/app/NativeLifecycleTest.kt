package org.torrview.app

import android.os.ParcelFileDescriptor
import android.net.Uri
import androidx.test.core.app.ActivityScenario
import androidx.lifecycle.ViewModelProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

@RunWith(AndroidJUnit4::class)
class NativeLifecycleTest {
    @Test fun localAudioAndSubtitleTracksCanBeSelectedDuringPlayback() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val video = File(instrumentation.targetContext.cacheDir, "tracks.mp4")
        instrumentation.context.assets.open("tracks.mp4").use { input ->
            video.outputStream().use { input.copyTo(it) }
        }
        try {
            ActivityScenario.launch(MainActivity::class.java).use { activity ->
                lateinit var model: PlayerViewModel
                activity.onActivity {
                    model = ViewModelProvider(it)[PlayerViewModel::class.java]
                    model.openDocument(Uri.fromFile(video), torrent = false)
                }
                fun awaitState(condition: (EngineState) -> Boolean) {
                    val deadline = System.nanoTime() + 10_000_000_000L
                    while (!condition(model.state.value) && System.nanoTime() < deadline) Thread.sleep(50)
                    assertTrue(model.state.value.toString(), condition(model.state.value))
                    assertEquals("", model.state.value.error)
                }
                awaitState { it.audio.size == 2 && it.subtitles.size == 1 }
                assertEquals("tracks.mp4", model.documentTitle.value)
                activity.onActivity { model.audioTrack(1); model.subtitleTrack(0); model.play() }
                awaitState { it.audioIndex == 1 && it.subtitleIndex == 0 && it.position > 1.0 && !it.paused }
                activity.onActivity { model.subtitleTrack(-1); model.audioTrack(0) }
                awaitState { it.audioIndex == 0 && it.subtitleIndex == -1 }
                activity.onActivity { model.stop() }
            }
        } finally { video.delete() }
    }

    @Test fun malformedMediaAndTorrentReportErrorsAndCanBeStopped() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val malformed = File(context.cacheDir, "malformed-media").apply { writeText("not media or bencode") }
        val handle = NativeBindings.create(context.cacheDir.absolutePath.toByteArray(), byteArrayOf(), 64)
        try {
            ParcelFileDescriptor.open(malformed, ParcelFileDescriptor.MODE_READ_ONLY).use {
                NativeBindings.openFd(handle, it.fd, false)
            }
            var state = EngineState.decode(NativeBindings.snapshot(handle))
            val deadline = System.nanoTime() + 5_000_000_000L
            while (state.error.isEmpty() && System.nanoTime() < deadline) {
                Thread.sleep(25)
                state = EngineState.decode(NativeBindings.snapshot(handle))
            }
            assertTrue(state.toString(), state.error.contains("Playback failed"))
            NativeBindings.command(handle, "torrent".toByteArray(), malformed.absolutePath.toByteArray(), 0.0)
            assertTrue(EngineState.decode(NativeBindings.snapshot(handle)).error.contains("Failed to load"))
            NativeBindings.command(handle, "stop".toByteArray(), byteArrayOf(), 0.0)
            assertEquals("", EngineState.decode(NativeBindings.snapshot(handle)).error)
        } finally {
            NativeBindings.release(handle)
            malformed.delete()
        }
    }

    @Test fun videoSurfaceAndEngineSurviveActivityRecreation() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val video = File(context.cacheDir, "test-video.mp4")
        instrumentation.context.assets.open("video.mp4").use { input ->
            video.outputStream().use { input.copyTo(it) }
        }
        ActivityScenario.launch(MainActivity::class.java).use { activity ->
            lateinit var model: PlayerViewModel
            activity.onActivity {
                model = ViewModelProvider(it)[PlayerViewModel::class.java]
                model.openDocument(Uri.fromFile(video), torrent = false)
            }
            fun awaitState(condition: (EngineState) -> Boolean) {
                val deadline = System.nanoTime() + 10_000_000_000L
                while (!condition(model.state.value) && System.nanoTime() < deadline) Thread.sleep(50)
                assertTrue(model.state.value.toString(), condition(model.state.value))
                assertEquals("", model.state.value.error)
            }
            awaitState { it.loaded && it.duration > 0 }
            activity.onActivity { model.pause() }
            awaitState { it.paused }
            repeat(3) {
                activity.recreate()
                activity.onActivity { assertSame(model, ViewModelProvider(it)[PlayerViewModel::class.java]) }
                awaitState { it.loaded && it.paused }
                activity.onActivity { model.seek(3.0); model.play() }
                awaitState { it.position >= 3.0 && !it.paused }
                var blue = 0
                val frameDeadline = System.nanoTime() + 5_000_000_000L
                do {
                    // Playback properties can arrive before SurfaceFlinger presents the first frame.
                    val bitmap = instrumentation.uiAutomation.takeScreenshot()
                    blue = 0
                    for (y in 0 until bitmap.height step 8) for (x in 0 until bitmap.width step 8) {
                        val pixel = bitmap.getPixel(x, y)
                        if (android.graphics.Color.blue(pixel) > 180 && android.graphics.Color.red(pixel) < 50) blue++
                    }
                    bitmap.recycle()
                    if (blue <= 100) Thread.sleep(100)
                } while (blue <= 100 && System.nanoTime() < frameDeadline)
                assertTrue("No blue video frame rendered to the Android surface", blue > 100)
                activity.onActivity { model.pause() }
                awaitState { it.paused }
            }
            activity.moveToState(androidx.lifecycle.Lifecycle.State.CREATED)
            awaitState { it.paused }
            activity.moveToState(androidx.lifecycle.Lifecycle.State.RESUMED)
            awaitState { it.loaded && it.paused }
            activity.onActivity { model.stop() }
        }
        video.delete()
    }

    private fun wave(): ByteArray {
        val samples = 8000
        return ByteBuffer.allocate(44 + samples * 2).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + samples * 2); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(8000); putInt(16000)
            putShort(2); putShort(16); put("data".toByteArray()); putInt(samples * 2)
            repeat(samples) { putShort(0) }
        }.array()
    }

    @Test fun repeatedOpenSeekBackgroundAndRelease() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.cacheDir, "native-test").apply { mkdirs() }
        val media = File(directory, "silence.wav").apply { writeBytes(wave()) }
        repeat(3) {
            val handle = NativeBindings.create(directory.absolutePath.toByteArray(), byteArrayOf(), 64)
            assertNotEquals(0L, handle)
            try {
                NativeBindings.command(handle, "foreground".toByteArray(), byteArrayOf(), 1.0)
                ParcelFileDescriptor.open(media, ParcelFileDescriptor.MODE_READ_ONLY).use { fd ->
                    NativeBindings.openFd(handle, fd.fd, false)
                }
                var state = EngineState.decode(NativeBindings.snapshot(handle))
                val deadline = System.nanoTime() + 5_000_000_000L
                while (state.duration <= 0 && System.nanoTime() < deadline) {
                    Thread.sleep(25)
                    state = EngineState.decode(NativeBindings.snapshot(handle))
                }
                assertTrue("Borrowed descriptor closed before mpv opened it: $state", state.duration > 0)
                NativeBindings.command(handle, "seek".toByteArray(), byteArrayOf(), 0.5)
                NativeBindings.command(handle, "foreground".toByteArray(), byteArrayOf(), 0.0)
                assertTrue(EngineState.decode(NativeBindings.snapshot(handle)).paused)
                NativeBindings.surface(handle, null)
                NativeBindings.command(handle, "stop".toByteArray(), byteArrayOf(), 0.0)
                assertFalse(EngineState.decode(NativeBindings.snapshot(handle)).loaded)
            } finally { NativeBindings.release(handle) }
        }
        media.delete()
    }

    @Test fun nonSeekableProviderIsRejectedWithoutCopying() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val handle = NativeBindings.create(context.cacheDir.absolutePath.toByteArray(), byteArrayOf(), 64)
        val pipe = ParcelFileDescriptor.createPipe()
        try {
            try {
                NativeBindings.openFd(handle, pipe[0].fd, false)
                fail("A non-seekable provider must be rejected")
            } catch (expected: IllegalStateException) {
                assertTrue(expected.message.orEmpty().contains("seeking"))
            }
        } finally {
            pipe.forEach { it.close() }
            NativeBindings.release(handle)
        }
    }
}
