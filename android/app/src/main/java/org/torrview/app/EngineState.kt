package org.torrview.app

import org.json.JSONObject

data class TorrentFile(val index: Int, val path: String, val size: Long, val video: Boolean)
data class BufferedRange(val start: Double, val end: Double)

data class EngineState(
    val ready: Boolean = false,
    val loaded: Boolean = false,
    val paused: Boolean = true,
    val position: Double = 0.0,
    val duration: Double = 0.0,
    val bufferedRanges: List<BufferedRange> = emptyList(),
    val title: String = "",
    val status: String = "Starting playback engine…",
    val torrentStatus: String = "",
    val error: String = "",
    val buffering: Boolean = false,
    val bufferProgress: Double = 0.0,
    val cacheUsed: Long = 0,
    val cacheLimit: Long = 0,
    val downloadRate: Int = 0,
    val peers: Int = 0,
    val activeFile: Int = -1,
    val audioIndex: Int = -1,
    val subtitleIndex: Int = -1,
    val files: List<TorrentFile> = emptyList(),
    val audio: List<String> = emptyList(),
    val subtitles: List<String> = emptyList(),
) {
    companion object {
        fun decode(bytes: ByteArray): EngineState {
            val json = JSONObject(bytes.toString(Charsets.UTF_8))
            fun tracks(key: String): List<String> {
                val values = json.getJSONArray(key)
                return List(values.length()) { values.getString(it) }
            }
            val files = json.getJSONArray("files")
            val ranges = json.optJSONArray("bufferedRanges")
            return EngineState(
                ready = true, loaded = json.getBoolean("loaded"), paused = json.getBoolean("paused"),
                position = json.getDouble("position"), duration = json.getDouble("duration"),
                bufferedRanges = if (ranges == null) emptyList() else List(ranges.length()) {
                    val range = ranges.getJSONObject(it)
                    BufferedRange(range.getDouble("start"), range.getDouble("end"))
                }.filter { it.start.isFinite() && it.end.isFinite() && it.end > it.start },
                title = json.getString("title"), status = json.getString("status"),
                torrentStatus = json.getString("torrentStatus"), error = json.getString("error"),
                buffering = json.getBoolean("buffering"), bufferProgress = json.getDouble("bufferProgress"),
                cacheUsed = json.getLong("cacheUsed"), cacheLimit = json.getLong("cacheLimit"),
                downloadRate = json.getInt("downloadRate"), peers = json.getInt("peers"),
                activeFile = json.getInt("activeFile"), audioIndex = json.getInt("audioIndex"),
                subtitleIndex = json.getInt("subtitleIndex"), audio = tracks("audio"),
                subtitles = tracks("subtitles"), files = List(files.length()) {
                    val file = files.getJSONObject(it)
                    TorrentFile(file.getInt("index"), file.getString("path"),
                        file.getLong("size"), file.getBoolean("video"))
                },
            )
        }
    }
}

data class PlaybackSettings(
    val cacheMiB: Int = 128,
    val uploadKiB: Int = 1024,
    val connections: Int = 100,
    val proxy: String = "",
) {
    fun clamped() = copy(cacheMiB = cacheMiB.coerceIn(16, 512),
        uploadKiB = uploadKiB.coerceIn(0, 1048576), connections = connections.coerceIn(1, 1000))
}
