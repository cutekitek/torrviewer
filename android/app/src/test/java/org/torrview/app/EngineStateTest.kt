package org.torrview.app

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EngineStateTest {
    @Test fun decodesUnicodeLargeFilesAndTrackSelection() {
        val source = """{"loaded":true,"paused":true,"position":12.5,"duration":3600.0,
            "title":"Фильм 🎬","status":"Paused","torrentStatus":"Streaming","error":"",
            "buffering":true,"bufferProgress":0.25,"cacheUsed":67108864,"cacheLimit":134217728,
            "downloadRate":1048576,"peers":3,"activeFile":7,"audioIndex":1,"subtitleIndex":-1,
            "files":[{"index":7,"path":"Season 1/Фильм.mkv","size":8589934592,"video":true}],
            "audio":["English","日本語"],"subtitles":["English"],
            "bufferedRanges":[{"start":0,"end":15},{"start":40,"end":55},{"start":9,"end":2}]}"""
        val state = EngineState.decode(source.toByteArray())
        assertEquals("Фильм 🎬", state.title)
        assertEquals(8589934592L, state.files.single().size)
        assertEquals(7, state.files.single().index)
        assertEquals("日本語", state.audio[state.audioIndex])
        assertEquals(-1, state.subtitleIndex)
        assertTrue(state.buffering)
        assertEquals(0.25, state.bufferProgress, 0.0)
        assertEquals(listOf(BufferedRange(0.0, 15.0), BufferedRange(40.0, 55.0)), state.bufferedRanges)
        val unavailable = JSONObject(source).apply { remove("bufferedRanges") }
        assertTrue(EngineState.decode(unavailable.toString().toByteArray()).bufferedRanges.isEmpty())
        val failed = JSONObject(source).put("error", "Provider closed\nTry another file")
        assertTrue(EngineState.decode(failed.toString().toByteArray()).error.contains('\n'))
    }

    @Test fun constrainsMobileMemoryAndNetworkSettings() {
        val settings = PlaybackSettings(-1, -1, 99999).clamped()
        assertEquals(16, settings.cacheMiB)
        assertEquals(0, settings.uploadKiB)
        assertEquals(1000, settings.connections)
        assertEquals(512, PlaybackSettings(cacheMiB = Int.MAX_VALUE).clamped().cacheMiB)
    }
}
