package com.zying.zysu.ui.sumh

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.sumh.util.SUMHUiAdapter as Controller
import com.zying.zysu.ui.util.rememberSnackbarController
import com.zying.zysu.ui.util.LocalSnackbarHost
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.withContext

enum class SUMHSection(val displayNameRes: Int) {
    Status(R.string.sumh_workspace_status),
    Mount(R.string.sumh_workspace_mount),
    Isolation(R.string.sumh_workspace_isolation),
    Rules(R.string.sumh_workspace_rules),
    Logs(R.string.sumh_workspace_logs),
    Debug(R.string.sumh_workspace_debug),
}

/** The host owns navigation and insets; status and debug may join a host's single scroll. */
@Composable
fun SUMHWorkspace(
    section: SUMHSection,
    scrollable: Boolean = true,
    onSavingChanged: (Boolean) -> Unit = {},
    settingsHeader: @Composable () -> Unit = {},
    contentPadding: PaddingValues = PaddingValues(horizontal = 18.dp, vertical = 16.dp),
    refreshKey: Int = 0,
    externalSaving: Boolean = false,
) {
    val resources = LocalResources.current
    val scope = rememberCoroutineScope()
    val inlineContent = !scrollable && (section == SUMHSection.Debug || section == SUMHSection.Status)
    val localSnackbar = rememberSnackbarController()
    val snackbar = if (inlineContent) LocalSnackbarHost.current else localSnackbar
    val layoutDirection = LocalLayoutDirection.current
    val horizontalPadding = PaddingValues(
        start = contentPadding.calculateStartPadding(layoutDirection),
        end = contentPadding.calculateEndPadding(layoutDirection),
    )
    val sectionState = rememberSaveableStateHolder()
    var isLoading by remember { mutableStateOf(false) }
    var configSaving by remember { mutableStateOf(false) }
    val saving = configSaving || externalSaving
    var dataReady by remember { mutableStateOf(false) }
    var loadError by remember { mutableStateOf<String?>(null) }
    var runtimeApplyPending by rememberSaveable { mutableStateOf(false) }
    var status by remember { mutableStateOf(Controller.SUMHStatus.NOT_PRESENT) }
    var version by remember { mutableStateOf("Unknown") }
    var config by remember { mutableStateOf(Controller.SUMHPConfig()) }
    var rules by remember { mutableStateOf(emptyList<Controller.ActiveRule>()) }
    var system by remember { mutableStateOf(Controller.SystemInfo("", "", emptyList(), emptyList(), false, null)) }
    var features by remember { mutableStateOf<Controller.FeaturesResult?>(null) }
    var rulesRefreshing by remember { mutableStateOf(false) }
    var showKernelLog by rememberSaveable { mutableStateOf(false) }
    var logContent by remember { mutableStateOf("") }
    var logLoading by remember { mutableStateOf(false) }
    var logClearing by remember { mutableStateOf(false) }
    var logRefresh by remember { mutableIntStateOf(0) }
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(configSaving) }
    DisposableEffect(Unit) { onDispose { savingCallback(false) } }

    fun publish(loaded: Controller.UiState) {
        version = loaded.version
        status = loaded.status
        // Keep the complete config, including original JSON and supported fields.
        config = loaded.config
        system = loaded.system
        rules = loaded.rules
        features = loaded.features
        dataReady = true
        loadError = null
    }

    fun loadData() {
        if (isLoading || configSaving || externalSaving) return
        isLoading = true
        scope.launchUi(snackbar) {
            try {
                publish(Controller.load())
            } catch (error: Exception) {
                if (error is CancellationException) throw error
                val message = resources.getString(R.string.sumh_toast_load_error, error.message ?: "unknown")
                loadError = message
                snackbar.showSnackbar(message)
            } finally {
                isLoading = false
            }
        }
    }

    fun saveConfig(newConfig: Controller.SUMHPConfig) {
        if (configSaving || externalSaving || isLoading || !dataReady) return
        configSaving = true
        scope.launchUi(snackbar, start = CoroutineStart.UNDISPATCHED) {
            // The existing backend performs persist/apply atomically. Preserve its result
            // and refresh together if the host is disposed while that operation finishes.
            val message = withContext(NonCancellable) {
                try {
                    val result = Controller.saveConfig(newConfig)
                    if (result.noChanges) return@withContext null
                    if (result.persisted) runtimeApplyPending = result.error != null && newConfig.kernelAvailable
                    publish(Controller.load())
                    when {
                        result.error != null && result.persisted -> resources.getString(R.string.sumh_saved_apply_failed, result.error)
                        result.error != null -> result.error
                        result.applied -> resources.getString(R.string.sumh_saved_applied)
                        else -> resources.getString(R.string.sumh_saved_reboot)
                    }
                } finally {
                    configSaving = false
                }
            }
            if (message != null) snackbar.showSnackbar(message)
        }
    }

    fun retryApply() {
        if (configSaving || externalSaving || isLoading) return
        configSaving = true
        scope.launchUi(snackbar, start = CoroutineStart.UNDISPATCHED) {
            val applyFailed = withContext(NonCancellable) {
                try {
                    runtimeApplyPending = !Controller.retryApply()
                    publish(Controller.load())
                    runtimeApplyPending
                } finally {
                    configSaving = false
                }
            }
            if (applyFailed) snackbar.showSnackbar(resources.getString(R.string.sumh_apply_failed))
        }
    }

    fun refreshRules() {
        if (rulesRefreshing || configSaving || externalSaving || isLoading || !dataReady) return
        rulesRefreshing = true
        scope.launchUi(snackbar) {
            try {
                rules = Controller.getActiveRules()
            } finally {
                rulesRefreshing = false
            }
        }
    }

    fun clearRules() {
        if (configSaving || externalSaving || rulesRefreshing || isLoading || !dataReady) return
        configSaving = true
        scope.launchUi(snackbar, start = CoroutineStart.UNDISPATCHED) {
            val message = withContext(NonCancellable) {
                try {
                    if (Controller.clearAllRules()) {
                        publish(Controller.load())
                        runtimeApplyPending = true
                        R.string.sumh_workspace_rules_cleared
                    } else {
                        R.string.sumh_workspace_rules_clear_failed
                    }
                } finally {
                    configSaving = false
                }
            }
            snackbar.showSnackbar(resources.getString(message))
        }
    }

    fun mutateMapRules(successMessage: Int, failureMessage: Int, mutation: suspend () -> Boolean) {
        if (configSaving || externalSaving || isLoading || !dataReady) return
        if (!config.enableMapsSpoof || status != Controller.SUMHStatus.AVAILABLE ||
            features?.names?.contains("maps_spoof") != true) return
        configSaving = true
        scope.launchUi(snackbar, start = CoroutineStart.UNDISPATCHED) {
            val message = withContext(NonCancellable) {
                try {
                    if (mutation()) successMessage else failureMessage
                } finally {
                    configSaving = false
                }
            }
            snackbar.showSnackbar(resources.getString(message))
        }
    }

    LaunchedEffect(refreshKey) { loadData() }
    LaunchedEffect(section, showKernelLog, logRefresh) {
        if (section != SUMHSection.Logs) return@LaunchedEffect
        logLoading = true
        logContent = ""
        try {
            logContent = if (showKernelLog) Controller.readKernelLog() else Controller.readLog()
        } catch (error: Exception) {
            if (error is CancellationException) throw error
            snackbar.showSnackbar(error.message ?: resources.getString(R.string.operation_failed))
        } finally {
            logLoading = false
        }
    }

    Box(if (inlineContent) Modifier.fillMaxWidth() else Modifier.fillMaxSize()) {
        Column(if (inlineContent) Modifier.fillMaxWidth() else Modifier.fillMaxSize()) {
            if (loadError != null) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(horizontalPadding),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text(
                        text = loadError.orEmpty(),
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error,
                    )
                    IconButton(onClick = ::loadData, enabled = !isLoading && !saving) {
                        YukiIcon(Icons.Filled.Refresh, stringResource(R.string.sumh_rules_refresh))
                    }
                }
            }
            if (isLoading || saving || rulesRefreshing || logLoading || logClearing) {
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            Box(if (inlineContent) Modifier.fillMaxWidth() else Modifier.weight(1f).fillMaxWidth()) {
                sectionState.SaveableStateProvider(section.name) {
                    if (!dataReady && section != SUMHSection.Logs) {
                        if (!inlineContent) Column(
                            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(contentPadding),
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            CompositionLocalProvider(LocalSnackbarHost provides snackbar) { settingsHeader() }
                            if (isLoading) CircularProgressIndicator(Modifier.padding(16.dp))
                        }
                    } else when (section) {
                        SUMHSection.Status -> StatusTab(
                            status, version, system, ::loadData,
                            scrollable = !inlineContent,
                            contentPadding = contentPadding,
                            refreshEnabled = !isLoading && !saving,
                            loading = isLoading,
                            loadError = loadError,
                        )
                        SUMHSection.Mount, SUMHSection.Isolation, SUMHSection.Debug -> SettingsTab(
                            config = config,
                            sumhStatus = status,
                            features = features,
                            snackbarHostState = snackbar,
                            controlsEnabled = dataReady && !saving && !isLoading,
                            runtimeApplyPending = runtimeApplyPending,
                            onRetryApply = ::retryApply,
                            onConfigChanged = ::saveConfig,
                            onClearMapRules = {
                                mutateMapRules(R.string.sumh_maps_cleared, R.string.sumh_maps_clear_failed) {
                                    Controller.clearMapsRules()
                                }
                            },
                            onAddMapRule = { ti, td, si, sd, path ->
                                mutateMapRules(R.string.sumh_maps_add_success, R.string.sumh_maps_add_failed) {
                                    Controller.addMapsRule(ti, td, si, sd, path)
                                }
                            },
                            section = section,
                            scrollable = !inlineContent,
                            header = { CompositionLocalProvider(LocalSnackbarHost provides snackbar) { settingsHeader() } },
                            contentPadding = contentPadding,
                            headingActions = {
                                IconButton(onClick = ::loadData, enabled = !isLoading && !saving) {
                                    YukiIcon(Icons.Filled.Refresh, stringResource(R.string.sumh_rules_refresh))
                                }
                            },
                        )
                        SUMHSection.Rules -> RulesTab(
                            rules, status, dataReady && !saving && !rulesRefreshing && !isLoading,
                            ::refreshRules, ::clearRules,
                        )
                        SUMHSection.Logs -> LogsTab(
                            showKernelLog = showKernelLog,
                            onToggleLogType = { showKernelLog = !showKernelLog },
                            logContent = logContent,
                            onRefreshLog = { logRefresh++ },
                            onClearLog = {
                                if (!logClearing && !showKernelLog) {
                                    logClearing = true
                                    scope.launchUi(snackbar) {
                                        try {
                                            if (Controller.clearLog()) logRefresh++
                                            else snackbar.showSnackbar(resources.getString(R.string.operation_failed))
                                        } finally { logClearing = false }
                                    }
                                }
                            },
                        )
                    }
                }
            }
        }
        if (!inlineContent) SnackbarHost(snackbar.hostState, Modifier.align(Alignment.BottomCenter))
    }
}
