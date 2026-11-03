package com.example.gothrtosloader.ui.main

import com.example.gothrtosloader.runtime.GothRuntimeApi
import com.example.gothrtosloader.runtime.RootResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class MainScreenViewModelTest {

  private val dispatcher = StandardTestDispatcher()

  @Before
  fun setUp() {
    Dispatchers.setMain(dispatcher)
  }

  @After
  fun tearDown() {
    Dispatchers.resetMain()
  }

  @Test
  fun refresh_reportsInstalledRuntime() = runTest(dispatcher) {
    val runtime = FakeRuntime(runningPid = 4242)
    val viewModel = MainScreenViewModel(runtime, dispatcher)

    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertTrue(state.rootAvailable)
    assertTrue(state.rootChecked)
    assertTrue(state.runtimeInstalled)
    assertTrue(state.runtimeRunning)
    assertEquals(4242, state.runtimePid)
    assertFalse(state.busy)
  }

  @Test
  fun refresh_withoutRoot_explainsRequirement() = runTest(dispatcher) {
    val runtime = FakeRuntime(rootAvailable = false)
    val viewModel = MainScreenViewModel(runtime, dispatcher)

    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertFalse(state.rootAvailable)
    assertTrue(state.rootChecked)
    assertFalse(state.runtimeInstalled)
    assertNull(state.runtimePid)
    assertTrue(state.status.contains("Root"))
  }

  @Test
  fun install_makesRuntimeAvailable() = runTest(dispatcher) {
    val runtime = FakeRuntime(installed = false)
    val viewModel = MainScreenViewModel(runtime, dispatcher)
    advanceUntilIdle()

    viewModel.install()
    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertEquals(1, runtime.installCalls)
    assertTrue(state.runtimeInstalled)
    assertTrue(state.status.contains("installed", ignoreCase = true))
    assertNull(state.error)
  }

  @Test
  fun installFailure_isSurfaced() = runTest(dispatcher) {
    val runtime =
      FakeRuntime(installed = false, installResult = RootResult(1, "no gothdroid binary for ABI x86"))
    val viewModel = MainScreenViewModel(runtime, dispatcher)
    advanceUntilIdle()

    viewModel.install()
    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertFalse(state.runtimeInstalled)
    assertTrue(state.error.orEmpty().contains("no gothdroid binary"))
    assertFalse(state.busy)
  }

  @Test
  fun start_marksRuntimeRunning() = runTest(dispatcher) {
    val runtime = FakeRuntime(installed = true)
    val viewModel = MainScreenViewModel(runtime, dispatcher)
    advanceUntilIdle()

    viewModel.start()
    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertEquals(1, runtime.startCalls)
    assertTrue(state.runtimeRunning)
    assertEquals(4242, state.runtimePid)
    assertTrue(state.status.contains("running", ignoreCase = true))
  }

  @Test
  fun stop_clearsRunningState() = runTest(dispatcher) {
    val runtime = FakeRuntime(installed = true, runningPid = 4242)
    val viewModel = MainScreenViewModel(runtime, dispatcher)
    advanceUntilIdle()
    assertTrue(viewModel.uiState.value.runtimeRunning)

    viewModel.stop()
    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertEquals(1, runtime.stopCalls)
    assertFalse(state.runtimeRunning)
    assertNull(state.runtimePid)
  }

  @Test
  fun start_withoutInstall_doesNotInvokeSupervisor() = runTest(dispatcher) {
    val runtime = FakeRuntime(installed = false)
    val viewModel = MainScreenViewModel(runtime, dispatcher)
    advanceUntilIdle()

    viewModel.start()
    advanceUntilIdle()

    val state = viewModel.uiState.value
    assertEquals(0, runtime.startCalls)
    assertTrue(state.error.orEmpty().contains("not installed", ignoreCase = true))
  }
}

private class FakeRuntime(
  var rootAvailable: Boolean = true,
  var installed: Boolean = true,
  var runningPid: Int? = null,
  var installResult: RootResult = RootResult(0, "installed"),
  var startResult: RootResult = RootResult(0, "started"),
  var stopResult: RootResult = RootResult(0, "stopped"),
) : GothRuntimeApi {
  var installCalls = 0
  var startCalls = 0
  var stopCalls = 0
  var log = mutableListOf("gothdroid: ready")

  override fun isRootAvailable(): Boolean = rootAvailable

  override fun isRuntimeInstalled(): Boolean = installed

  override fun installRuntime(): RootResult {
    installCalls++
    if (installResult.ok) {
      installed = true
    }
    return installResult
  }

  override fun startRuntime(): RootResult {
    startCalls++
    if (startResult.ok) {
      runningPid = 4242
    }
    return startResult
  }

  override fun stopRuntime(): RootResult {
    stopCalls++
    if (stopResult.ok) {
      runningPid = null
    }
    return stopResult
  }

  override fun runtimePid(): Int? = runningPid

  override fun readLog(maxLines: Int): List<String> = log.takeLast(maxLines)

  override fun readStopRequested(): Boolean = false
}
