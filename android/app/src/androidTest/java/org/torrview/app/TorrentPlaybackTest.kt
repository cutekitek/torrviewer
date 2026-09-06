package org.torrview.app

import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.util.concurrent.Executors

@RunWith(AndroidJUnit4::class)
class TorrentPlaybackTest {
    @Test fun torrentWithoutPeersCanBeCancelledWhileMpvWaitsForData() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val video = instrumentation.context.assets.open("video.mp4").use { it.readBytes() }
        val metadata = java.io.File(instrumentation.targetContext.cacheDir, "unavailable.torrent")
        val info = "d6:lengthi${video.size}e4:name9:video.mp412:piece lengthi16384e6:pieces20:".toByteArray() +
            MessageDigest.getInstance("SHA-1").digest(video) + "e".toByteArray()
        metadata.writeBytes("d4:info".toByteArray() + info + "e".toByteArray())
        try {
            ActivityScenario.launch(MainActivity::class.java).use { activity ->
                lateinit var model: PlayerViewModel
                activity.onActivity {
                    model = ViewModelProvider(it)[PlayerViewModel::class.java]
                    model.openDocument(android.net.Uri.fromFile(metadata), torrent = true)
                }
                fun awaitState(condition: (EngineState) -> Boolean) {
                    val deadline = System.nanoTime() + 10_000_000_000L
                    while (!condition(model.state.value) && System.nanoTime() < deadline) Thread.sleep(50)
                    assertTrue(model.state.value.toString(), condition(model.state.value))
                }
                awaitState { it.files.size == 1 }
                activity.onActivity { model.selectFile(model.state.value.files.single().index) }
                awaitState { it.loaded && it.buffering }
                activity.onActivity { model.stop() }
                awaitState { !it.loaded && it.files.isEmpty() }
                assertEquals("", model.state.value.error)
            }
        } finally { metadata.delete() }
    }

    @Test fun magnetMetadataStreamsFromPeerThroughMemoryCacheToMpv() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val video = instrumentation.context.assets.open("video.mp4").use { it.readBytes() }
        TestPeer(video).use { peer ->
            ActivityScenario.launch(MainActivity::class.java).use { activity ->
                lateinit var model: PlayerViewModel
                activity.onActivity {
                    model = ViewModelProvider(it)[PlayerViewModel::class.java]
                    model.openMagnet(peer.magnet)
                }
                fun awaitState(condition: (EngineState) -> Boolean) {
                    val deadline = System.nanoTime() + 20_000_000_000L
                    while (!condition(model.state.value) && System.nanoTime() < deadline) Thread.sleep(50)
                    assertTrue("${model.state.value}; peer=${peer.failure}", condition(model.state.value))
                    assertEquals("", model.state.value.error)
                }
                awaitState { it.files.size == 1 }
                assertEquals(video.size.toLong(), model.state.value.files.single().size)
                activity.onActivity { model.selectFile(model.state.value.files.single().index) }
                awaitState { it.loaded && it.duration > 0 && it.cacheUsed > 0 && it.bufferedRanges.isNotEmpty() }
                assertTrue(model.state.value.cacheUsed <= model.state.value.cacheLimit)
                activity.onActivity { model.seek(6.0); model.play() }
                awaitState { it.position >= 6.0 && !it.paused }
                activity.onActivity { model.stop() }
                awaitState { !it.loaded && it.files.isEmpty() }
                assertTrue(model.state.value.bufferedRanges.isEmpty())
            }
        }
    }

    // A private, one-piece BitTorrent peer with BEP 9 metadata. Tests never need a public swarm.
    private class TestPeer(private val video: ByteArray) : AutoCloseable {
        private val server = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
        private val worker = Executors.newSingleThreadExecutor()
        @Volatile private var socket: Socket? = null
        @Volatile private var closed = false
        @Volatile var failure: String = ""
            private set
        private val info = ("d6:lengthi${video.size}e4:name9:video.mp412:piece lengthi16384e6:pieces20:")
            .toByteArray() + sha1(video) + "e".toByteArray()
        private val hash = sha1(info)
        val magnet = "magnet:?xt=urn:btih:" + hash.joinToString("") { "%02x".format(it) } +
            "&x.pe=127.0.0.1:${server.localPort}"

        init {
            worker.execute {
                while (!closed) try {
                    server.accept().use { client ->
                        socket = client
                        client.soTimeout = 30000
                        val input = DataInputStream(client.getInputStream())
                        val output = DataOutputStream(client.getOutputStream())
                        val handshake = ByteArray(68).also { input.readFully(it) }
                        check(handshake.copyOfRange(28, 48).contentEquals(hash))
                        val reserved = ByteArray(8).also { it[5] = 0x10 }
                        output.write(byteArrayOf(19) + "BitTorrent protocol".toByteArray() + reserved +
                            hash + "-TV0001-123456789012".toByteArray())
                        fun send(id: Int, bytes: ByteArray = byteArrayOf()) {
                            output.writeInt(bytes.size + 1); output.writeByte(id); output.write(bytes); output.flush()
                        }
                        send(5, byteArrayOf(0x80.toByte()))
                        send(1)
                        send(20, byteArrayOf(0) + "d1:md11:ut_metadatai1ee13:metadata_sizei${info.size}ee".toByteArray())
                        var remoteMetadata = 1
                        while (true) {
                            val length = input.readInt()
                            if (length == 0) continue
                            check(length in 1..65536)
                            val id = input.readUnsignedByte()
                            val payload = ByteArray(length - 1).also { input.readFully(it) }
                            if (id == 20 && payload.isNotEmpty()) {
                                if (payload[0].toInt() == 0) {
                                    Regex("11:ut_metadatai(\\d+)e").find(payload.toString(Charsets.ISO_8859_1))
                                        ?.let { remoteMetadata = it.groupValues[1].toInt() }
                                } else if (payload[0].toInt() == 1) {
                                    send(20, byteArrayOf(remoteMetadata.toByte()) +
                                        "d8:msg_typei1e5:piecei0e10:total_sizei${info.size}ee".toByteArray() + info)
                                }
                            } else if (id == 6) {
                                val request = java.nio.ByteBuffer.wrap(payload)
                                check(request.int == 0)
                                val offset = request.int
                                val count = request.int
                                check(offset >= 0 && count > 0 && offset + count <= video.size)
                                send(7, payload.copyOfRange(0, 8) + video.copyOfRange(offset, offset + count))
                            }
                        }
                    }
                } catch (error: Exception) { if (!closed) failure = error.toString() }
            }
        }
        override fun close() {
            closed = true
            server.close(); socket?.close(); worker.shutdownNow()
        }
        companion object {
            private fun sha1(bytes: ByteArray) = MessageDigest.getInstance("SHA-1").digest(bytes)
        }
    }
}
