package org.torrview.app

import android.app.UiModeManager
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.res.Configuration
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color

class MainActivity : ComponentActivity() {
    private val model: PlayerViewModel by viewModels()
    private val videoPicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            try { contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            catch (_: SecurityException) { /* Some providers grant only temporary access. */ }
            model.openDocument(uri, torrent = false)
        }
    }
    private val torrentPicker = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) model.openDocument(uri, torrent = true)
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        volumeControlStream = android.media.AudioManager.STREAM_MUSIC
        val tv = getSystemService(UiModeManager::class.java).currentModeType == Configuration.UI_MODE_TYPE_TELEVISION
        if (savedInstanceState == null) intent.data?.let {
            if (it.scheme.equals("magnet", ignoreCase = true)) model.openMagnet(it.toString())
        }
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = Color(0xFF80CBC4),
                background = Color(0xFF101416), surface = Color(0xFF192125))) {
                androidx.tv.material3.MaterialTheme {
                    TorrviewApp(model, tv, pick = { torrent ->
                        try {
                            if (torrent) torrentPicker.launch(arrayOf("*/*"))
                            else videoPicker.launch(arrayOf("video/*", "application/octet-stream"))
                        } catch (_: ActivityNotFoundException) {
                            model.message.value = "No document picker is installed on this device. Enter a magnet link instead."
                        }
                    }, keepScreenOn = { enabled ->
                        if (enabled) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    })
                }
            }
        }
    }
    override fun onStart() { super.onStart(); model.foreground(true) }
    override fun onStop() {
        if (!isChangingConfigurations) model.foreground(false)
        super.onStop()
    }
    override fun onTrimMemory(level: Int) {
        super.onTrimMemory(level)
        if (level >= TRIM_MEMORY_RUNNING_LOW && level != TRIM_MEMORY_UI_HIDDEN) model.trimMemory()
    }
}
