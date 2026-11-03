package com.example.gothrtosloader.ui.main

import androidx.activity.ComponentActivity
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import com.example.gothrtosloader.ui.theme.GothRTOSLoaderTheme
import org.junit.Rule
import org.junit.Test

/** UI tests for [com.example.gothrtosloader.ui.main.MainScreenContent]. */
class MainScreenTest {

  @get:Rule val composeTestRule = createAndroidComposeRule<ComponentActivity>()

  private fun show(state: MainScreenUiState) {
    composeTestRule.setContent {
      GothRTOSLoaderTheme {
        MainScreenContent(
          state = state,
          onRefresh = {},
          onInstall = {},
          onStart = {},
          onStop = {},
          onDismissError = {},
        )
      }
    }
  }

  @Test
  fun readyState_showsRootAndRuntime() {
    show(
      MainScreenUiState(
        rootAvailable = true,
        rootChecked = true,
        runtimeInstalled = true,
        status = "Runtime installed. Android is still running.",
      )
    )

    composeTestRule.onNodeWithText("Root access").assertExists()
    composeTestRule.onNodeWithText("Granted").assertExists()
    composeTestRule.onNodeWithText("Runtime").assertExists()
    composeTestRule.onNodeWithText("Installed").assertExists()
    composeTestRule.onNodeWithText("Start GothRTOS").assertExists()
    composeTestRule.onNodeWithText("Stop GothRTOS").assertExists()
  }

  @Test
  fun missingRoot_isReported() {
    show(
      MainScreenUiState(
        rootAvailable = false,
        rootChecked = true,
        status = "Root access is required to stop and restart Android.",
      )
    )

    composeTestRule.onNodeWithText("Denied").assertExists()
    composeTestRule.onNodeWithText("Root access is required", substring = true).assertExists()
  }

  @Test
  fun explainsFramebufferAndSupervisor() {
    show(MainScreenUiState(rootChecked = true))

    composeTestRule.onNodeWithText("/dev/graphics/fb0", substring = true).assertExists()
    composeTestRule.onNodeWithText("/dev/input/event*", substring = true).assertExists()
    composeTestRule.onNodeWithText("/system/sbin/stop", substring = true).assertExists()
    composeTestRule.onNodeWithText("/system/sbin/start", substring = true).assertExists()
  }

  @Test
  fun logLines_areShown() {
    show(
      MainScreenUiState(
        rootChecked = true,
        status = "GothRTOS is running.",
        log = listOf("gothdroid: starting SystemUI.lua", "gothdroid: SystemUI ready"),
      )
    )

    composeTestRule.onNodeWithText("Runtime log").assertExists()
    composeTestRule.onNodeWithText("gothdroid: SystemUI ready").assertExists()
  }
}
