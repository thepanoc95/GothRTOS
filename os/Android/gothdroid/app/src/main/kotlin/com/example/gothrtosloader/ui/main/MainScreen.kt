package com.example.gothrtosloader.ui.main

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel as composeViewModel
import com.example.gothrtosloader.R
import com.example.gothrtosloader.runtime.GothRuntime
import com.example.gothrtosloader.ui.theme.GothRTOSLoaderTheme

@Composable
fun MainScreen(
  modifier: Modifier = Modifier,
  viewModel: MainScreenViewModel? = null,
) {
  val context = LocalContext.current.applicationContext
  val activeViewModel =
    viewModel ?: composeViewModel { MainScreenViewModel(GothRuntime(context)) }
  val uiState by activeViewModel.uiState.collectAsStateWithLifecycle()
  MainScreenContent(
    state = uiState,
    onRefresh = activeViewModel::refresh,
    onInstall = activeViewModel::install,
    onStart = activeViewModel::start,
    onStop = activeViewModel::stop,
    onDismissError = activeViewModel::dismissError,
    modifier = modifier,
  )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun MainScreenContent(
  state: MainScreenUiState,
  onRefresh: () -> Unit,
  onInstall: () -> Unit,
  onStart: () -> Unit,
  onStop: () -> Unit,
  onDismissError: () -> Unit,
  modifier: Modifier = Modifier,
) {
  Scaffold(
    modifier = modifier,
    topBar = {
      CenterAlignedTopAppBar(
        title = { Text(stringResource(R.string.app_name)) },
        colors =
          TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.primaryContainer,
            titleContentColor = MaterialTheme.colorScheme.primary,
          ),
      )
    },
  ) { innerPadding ->
    Column(
      modifier =
        Modifier.padding(innerPadding)
          .fillMaxSize()
          .verticalScroll(rememberScrollState())
          .padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
      StatusCard(state)
      if (state.busy) {
        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
      }
      state.error?.let { ErrorCard(message = it, onDismiss = onDismissError) }
      ActionCard(
        state = state,
        onRefresh = onRefresh,
        onInstall = onInstall,
        onStart = onStart,
        onStop = onStop,
      )
      HowItWorksCard()
      LogCard(state.log)
    }
  }
}

@Composable
private fun StatusCard(state: MainScreenUiState) {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      Text("Status", style = MaterialTheme.typography.titleMedium)
      StatusRow(
        label = "Root access",
        value =
          when {
            !state.rootChecked -> "Checking..."
            state.rootAvailable -> "Granted"
            else -> "Denied"
          },
        good = state.rootAvailable,
      )
      StatusRow(
        label = "Runtime",
        value = if (state.runtimeInstalled) "Installed" else "Not installed",
        good = state.runtimeInstalled,
      )
      StatusRow(
        label = "GothRTOS",
        value =
          if (state.runtimeRunning) {
            "Running (pid ${state.runtimePid})"
          } else {
            "Stopped"
          },
        good = state.runtimeRunning,
      )
      HorizontalDivider()
      Text(text = state.status, style = MaterialTheme.typography.bodyMedium)
    }
  }
}

@Composable
private fun StatusRow(label: String, value: String, good: Boolean) {
  Row(
    modifier = Modifier.fillMaxWidth(),
    horizontalArrangement = Arrangement.SpaceBetween,
  ) {
    Text(text = label, style = MaterialTheme.typography.bodyLarge)
    Text(
      text = value,
      style = MaterialTheme.typography.bodyLarge,
      color =
        if (good) {
          MaterialTheme.colorScheme.primary
        } else {
          MaterialTheme.colorScheme.onSurfaceVariant
        },
    )
  }
}

@Composable
private fun ErrorCard(message: String, onDismiss: () -> Unit) {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      Text(
        text = "Could not complete the last action",
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.error,
      )
      Text(text = message, style = MaterialTheme.typography.bodySmall)
      TextButton(onClick = onDismiss) { Text("Dismiss") }
    }
  }
}

@Composable
private fun ActionCard(
  state: MainScreenUiState,
  onRefresh: () -> Unit,
  onInstall: () -> Unit,
  onStart: () -> Unit,
  onStop: () -> Unit,
) {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      Text("Actions", style = MaterialTheme.typography.titleMedium)
      Button(
        onClick = onStart,
        enabled =
          state.rootAvailable &&
            state.runtimeInstalled &&
            !state.runtimeRunning &&
            !state.busy,
        modifier = Modifier.fillMaxWidth(),
      ) {
        Text("Start GothRTOS")
      }
      OutlinedButton(
        onClick = onStop,
        enabled = state.runtimeRunning && !state.busy,
        modifier = Modifier.fillMaxWidth(),
      ) {
        Text("Stop GothRTOS")
      }
      OutlinedButton(
        onClick = onInstall,
        enabled = state.rootAvailable && !state.runtimeInstalled && !state.busy,
        modifier = Modifier.fillMaxWidth(),
      ) {
        Text(if (state.runtimeInstalled) "Reinstall runtime" else "Install runtime")
      }
      TextButton(onClick = onRefresh, enabled = !state.busy) {
        Text("Refresh status")
      }
    }
  }
}

@Composable
private fun HowItWorksCard() {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      Text("How it works", style = MaterialTheme.typography.titleMedium)
      Text(
        text =
          "GothRTOS draws to /dev/graphics/fb0 and reads touch from " +
            "/dev/input/event* through the libgothgfx Lua module.",
        style = MaterialTheme.typography.bodyMedium,
      )
      Text(
        text =
          "Starting stops the Android framework with /system/sbin/stop. " +
            "A detached root supervisor keeps the UI alive and runs " +
            "/system/sbin/start to bring Android back when GothRTOS exits.",
        style = MaterialTheme.typography.bodyMedium,
      )
      Text(
        text = "Root access is required. The screen will be taken over while GothRTOS is running.",
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
      )
    }
  }
}

@Composable
private fun LogCard(log: List<String>) {
  Card(modifier = Modifier.fillMaxWidth()) {
    Column(
      modifier = Modifier.padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
      Text("Runtime log", style = MaterialTheme.typography.titleMedium)
      if (log.isEmpty()) {
        Text(
          text = "No log entries yet.",
          style = MaterialTheme.typography.bodySmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
      } else {
        log.takeLast(80).forEach { line ->
          Text(
            text = line,
            style = MaterialTheme.typography.bodySmall,
            fontFamily = FontFamily.Monospace,
          )
        }
      }
    }
  }
}

@Preview(showBackground = true, showSystemUi = true)
@Composable
fun MainScreenPreview() {
  GothRTOSLoaderTheme {
    MainScreenContent(
      state =
        MainScreenUiState(
          rootAvailable = true,
          rootChecked = true,
          runtimeInstalled = true,
          runtimeRunning = false,
          busy = false,
          status = "Runtime installed. Android is still running.",
          log = listOf("gothdroid: starting SystemUI.lua", "gothdroid: UI finished"),
        ),
      onRefresh = {},
      onInstall = {},
      onStart = {},
      onStop = {},
      onDismissError = {},
    )
  }
}
