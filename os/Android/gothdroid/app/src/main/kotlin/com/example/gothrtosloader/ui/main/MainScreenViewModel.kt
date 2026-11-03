package com.example.gothrtosloader.ui.main

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.gothrtosloader.runtime.GothRuntimeApi
import com.example.gothrtosloader.runtime.RootResult
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex

/**
 * Everything the launcher screen needs to render.
 *
 * The screen is intentionally a single immutable value: the ViewModel is the
 * only writer and the Compose preview/tests can feed it directly.
 */
data class MainScreenUiState(
  val rootAvailable: Boolean = false,
  val rootChecked: Boolean = false,
  val runtimeInstalled: Boolean = false,
  val runtimeRunning: Boolean = false,
  val runtimePid: Int? = null,
  val busy: Boolean = true,
  val status: String = "Checking root access...",
  val log: List<String> = emptyList(),
  val error: String? = null,
)

class MainScreenViewModel(
  private val runtime: GothRuntimeApi,
  private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ViewModel() {

  private val _uiState = MutableStateFlow(MainScreenUiState())
  val uiState: StateFlow<MainScreenUiState> = _uiState.asStateFlow()

  private val actionLock = Mutex()

  init {
    refresh()
  }

  fun refresh() {
    runAction("Refreshing runtime status...") {
      reload()
    }
  }

  fun install() {
    runAction("Installing the GothRTOS runtime...") {
      val result = runtime.installRuntime()
      if (!result.ok) {
        fail("Install failed", result)
        return@runAction
      }
      val installed = runtime.isRuntimeInstalled()
      reload(
        status =
          if (installed) {
            "Runtime installed. Start GothRTOS when you are ready."
          } else {
            "Assets copied, but the native gothdroid binary is missing for this ABI."
          },
      )
      if (!installed) {
        _uiState.update {
          it.copy(
            error =
              "The APK did not contain a gothdroid binary for this device. " +
                "Build it with os/Android/build-native.sh and rebuild the app.",
          )
        }
      }
    }
  }

  fun start() {
    runAction("Stopping Android and starting GothRTOS...") {
      if (!runtime.isRuntimeInstalled()) {
        _uiState.update {
          it.copy(
            status = "Install the runtime first.",
            error = "The GothRTOS runtime is not installed yet.",
          )
        }
        return@runAction
      }
      val result = runtime.startRuntime()
      if (!result.ok) {
        fail("Could not start GothRTOS", result)
        return@runAction
      }
      // Android's stop command normally kills this app process shortly after
      // startRuntime() returns. If we are still alive, refresh so the screen
      // shows the supervisor state instead of pretending nothing happened.
      delay(1200)
      reload()
      _uiState.update {
        it.copy(
          status =
            if (it.runtimeRunning) {
              "GothRTOS is running. The Android UI is stopped."
            } else {
              "GothRTOS is starting. Android may close this app now."
            },
        )
      }
    }
  }

  fun stop() {
    runAction("Requesting GothRTOS shutdown...") {
      val result = runtime.stopRuntime()
      if (!result.ok) {
        fail("Could not stop GothRTOS", result)
        return@runAction
      }
      delay(700)
      reload("Shutdown requested. Android is restarting.")
    }
  }

  fun dismissError() {
    _uiState.update { it.copy(error = null) }
  }

  private suspend fun reload(status: String? = null) {
    val root = runtime.isRootAvailable()
    if (!root) {
      _uiState.update {
        it.copy(
          rootAvailable = false,
          rootChecked = true,
          runtimeInstalled = false,
          runtimeRunning = false,
          runtimePid = null,
          log = emptyList(),
          status = status ?: "Root access is required to stop and restart Android.",
        )
      }
      return
    }

    val installed = runtime.isRuntimeInstalled()
    val pid = if (installed) runtime.runtimePid() else null
    val log = if (installed) runtime.readLog() else emptyList()
    val running = pid != null
    _uiState.update {
      it.copy(
        rootAvailable = true,
        rootChecked = true,
        runtimeInstalled = installed,
        runtimeRunning = running,
        runtimePid = pid,
        log = log,
        status =
          status
            ?: when {
              running -> "GothRTOS is running."
              installed -> "Runtime installed. Android is still running."
              else -> "Runtime not installed. Install it from this screen."
            },
      )
    }
  }

  private fun fail(prefix: String, result: RootResult) {
    val detail = result.output.lineSequence().filter { it.isNotBlank() }.toList().takeLast(6).joinToString("\n")
    _uiState.update {
      it.copy(
        status = "$prefix.",
        error = if (detail.isBlank()) prefix else "$prefix:\n$detail",
      )
    }
  }

  private fun runAction(label: String, block: suspend () -> Unit) {
    viewModelScope.launch(dispatcher) {
      if (!actionLock.tryLock()) {
        return@launch
      }
      _uiState.update { it.copy(busy = true, status = label, error = null) }
      try {
        block()
      } catch (error: Throwable) {
        _uiState.update {
          it.copy(error = error.message ?: error.javaClass.simpleName)
        }
      } finally {
        _uiState.update { it.copy(busy = false) }
        actionLock.unlock()
      }
    }
  }
}
