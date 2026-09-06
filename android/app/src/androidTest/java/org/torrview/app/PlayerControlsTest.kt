package org.torrview.app

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class PlayerControlsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun compactControlsKeepStatsInDebugAndSupportSeeking() {
        val state = mutableStateOf(EngineState(loaded = true, title = "Playback sample", duration = 120.0,
            position = 15.0, activeFile = 0, downloadRate = 1048576, peers = 4,
            cacheUsed = 16777216, cacheLimit = 67108864,
            bufferedRanges = listOf(BufferedRange(0.0, 30.0), BufferedRange(60.0, 90.0))))
        var seek = -1.0
        var paused = false
        compose.setContent {
            MaterialTheme {
                Box(Modifier.fillMaxSize().background(Color(0xFF335A70))) {
                    PlayerControls(state.value, false, "", { seek = it }, { paused = true }, {}, {}, {}, {}, {})
                }
            }
        }
        compose.onNodeWithText("Download", substring = true).assertDoesNotExist()
        compose.onNodeWithContentDescription("Play").performClick()
        compose.runOnIdle { assertTrue(paused) }
        compose.onNodeWithContentDescription("Playback position")
            .performSemanticsAction(SemanticsActions.SetProgress) { it(60f) }
        compose.runOnIdle { assertEquals(60.0, seek, 0.01) }
        compose.onNodeWithContentDescription("Debug information").performClick()
        compose.onNodeWithText("Download  ·  1024 KiB/s").assertIsDisplayed()
        compose.onNodeWithText("Peers  ·  4").assertIsDisplayed()
        compose.runOnIdle { state.value = state.value.copy(peers = 7) }
        compose.onNodeWithText("Peers  ·  7").assertIsDisplayed()
        compose.onNodeWithText("Close").performClick()
        compose.onNodeWithText("Download", substring = true).assertDoesNotExist()
        compose.onNodeWithContentDescription("Subtitles").assertIsDisplayed()
        compose.onNodeWithContentDescription("Hide controls").assertIsDisplayed()
    }
}
