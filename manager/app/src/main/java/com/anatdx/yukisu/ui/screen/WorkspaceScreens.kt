package com.anatdx.yukisu.ui.screen

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.anatdx.yukisu.BuildConfig
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.KsuIsValid
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.kasumi.KasumiSection
import com.anatdx.yukisu.ui.kasumi.KasumiWorkspace
import com.anatdx.yukisu.ui.kasumi.ConfigSection
import com.anatdx.yukisu.ui.util.LocalNavigationLeaveGuard
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.*
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import ui.screen.feature.FeatureControlContent
import ui.screen.feature.FeatureControlState
import ui.screen.moreSettings.MoreSettingsContent
import ui.screen.moreSettings.PreferenceCategory

/** Frequent task switches are immediate. Native tabs expose selection and wrap large text. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun WorkspaceTabs(
    labels: List<String>,
    selected: Int,
    onSelected: (Int) -> Unit,
    enabled: Boolean = true,
) {
    val focusManager = LocalFocusManager.current
    SecondaryTabRow(
        modifier = Modifier.fillMaxWidth(),
        selectedTabIndex = selected,
        containerColor = MaterialTheme.colorScheme.background,
        divider = { HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant) },
    ) {
        labels.forEachIndexed { index, label ->
            Tab(
                selected = index == selected,
                enabled = enabled,
                modifier = Modifier.heightIn(min = 48.dp),
                onClick = {
                    focusManager.clearFocus()
                    onSelected(index)
                },
            ) {
                Text(
                    label,
                    modifier = Modifier.padding(horizontal = 4.dp, vertical = 12.dp),
                    style = MaterialTheme.typography.titleSmall,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}

/** A single inset owner for embedded pages; content always receives a bounded viewport. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun WorkspaceScaffold(
    title: String,
    navigator: DestinationsNavigator,
    showBack: Boolean = true,
    navigationEnabled: Boolean = true,
    tabs: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = { Text(title) },
                    navigationIcon = {
                        if (showBack) IconButton(enabled = navigationEnabled, onClick = { navigator.navigateUp() }) {
                            YukiIcon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back))
                        }
                    },
                    colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
                    windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                )
                tabs()
            }
        },
        contentWindowInsets = WindowInsets.safeDrawing,
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) { content() }
    }
}

@Composable
internal fun AuthorizationWorkspace(navigator: DestinationsNavigator) {
    SuperUserPage(navigator)
}

@Destination<RootGraph>
@Composable
fun AuthorizationPoliciesScreen(navigator: DestinationsNavigator) {
    WorkspaceScaffold(stringResource(R.string.nav_policies), navigator) {
        AuthorizationSettingsContent(navigator)
    }
}

@Destination<RootGraph>
@Composable
fun AuthorizationRecordsScreen(navigator: DestinationsNavigator) {
    KsuIsValid {
        if (rememberAuthorizationLogsEnabled()) {
            LogViewerPage(navigator)
        } else {
            WorkspaceScaffold(stringResource(R.string.nav_records), navigator) {
                Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
                    Text(stringResource(R.string.nav_records_disabled), style = MaterialTheme.typography.bodyLarge)
                    Spacer(Modifier.height(16.dp))
                    OutlinedButton(onClick = { navigator.navigate(KernelPolicyScreenDestination(initialPage = 3)) }) {
                        Text(stringResource(R.string.nav_manage_kernel_features))
                    }
                }
            }
        }
    }
}

@Destination<RootGraph>
@Composable
fun ExtensionsScreen(navigator: DestinationsNavigator) {
    var selected by rememberSaveable { mutableIntStateOf(0) }
    val pages = rememberSaveableStateHolder()
    val labels = listOf(stringResource(R.string.module), stringResource(R.string.plugin))
    BackHandler(selected != 0) { selected = 0 }
    pages.SaveableStateProvider(selected) {
        if (selected == 0) {
            ModulePage(navigator, workspace = true) { WorkspaceTabs(labels, selected, { selected = it }) }
        } else {
            PluginPage(navigator, workspace = true) { enabled -> WorkspaceTabs(labels, selected, { selected = it }, enabled) }
        }
    }
}

@Destination<RootGraph>
@Composable
fun ExtensionRepositoryScreen(navigator: DestinationsNavigator, initialPage: Int = 0) {
    var selected by rememberSaveable { mutableIntStateOf(initialPage.coerceIn(0, 1)) }
    val pages = rememberSaveableStateHolder()
    val labels = listOf(stringResource(R.string.module), stringResource(R.string.plugin))
    pages.SaveableStateProvider(selected) {
        if (selected == 0) {
            ModuleRepositoryPage(navigator, workspace = true) { WorkspaceTabs(labels, selected, { selected = it }) }
        } else {
            PluginRepositoryPage(navigator, workspace = true) { enabled -> WorkspaceTabs(labels, selected, { selected = it }, enabled) }
        }
    }
}

@Destination<RootGraph>
@Composable
fun KernelPolicyScreen(navigator: DestinationsNavigator, initialPage: Int = 0) {
    var selected by rememberSaveable { mutableIntStateOf(initialPage.coerceIn(0, 4)) }
    var saving by remember { mutableStateOf(false) }
    val pages = rememberSaveableStateHolder()
    WorkspaceOperationGuard(saving, KernelPolicyScreenDestination.route)
    val labels = listOf(stringResource(R.string.nav_mount), stringResource(R.string.nav_isolation), stringResource(R.string.nav_active_rules), stringResource(R.string.nav_kernel_features), stringResource(R.string.nav_advanced_security))
    WorkspaceScaffold(stringResource(R.string.nav_kernel_policy), navigator, navigationEnabled = !saving, tabs = {
        WorkspaceTabs(labels, selected, { selected = it }, enabled = !saving)
    }) {
        KsuIsValid {
            pages.SaveableStateProvider(if (selected < 3) "kasumi" else selected.toString()) {
            when (selected) {
                0, 1, 2 -> KasumiWorkspace(
                    section = when (selected) {
                        0 -> KasumiSection.Mount
                        1 -> KasumiSection.Isolation
                        else -> KasumiSection.Rules
                    },
                    onSavingChanged = { saving = it },
                    settingsHeader = {
                        if (selected == 1) ConfigSection(title = "", segmented = true) {
                            SettingItem(
                                icon = Icons.Outlined.FolderOff,
                                title = stringResource(R.string.nav_umount_paths),
                                summary = stringResource(R.string.nav_umount_paths_summary),
                                enabled = !saving,
                                groupPosition = SettingsItemPosition.Only,
                                onClick = { navigator.navigate(UmountManagerScreenDestination) },
                            )
                        }
                    },
                )
                3 -> FeatureControlContent(navigator, onSavingChanged = { saving = it })
                else -> MoreSettingsContent(PreferenceCategory.Advanced)
            }
            }
        }
    }
}

@Destination<RootGraph>
@Composable
fun DiagnosticsScreen(navigator: DestinationsNavigator) {
    var selected by rememberSaveable { mutableIntStateOf(0) }
    var saving by remember { mutableStateOf(false) }
    val pages = rememberSaveableStateHolder()
    WorkspaceOperationGuard(saving, DiagnosticsScreenDestination.route)
    val rootAvailable = remember { Natives.isManager && Natives.version != null }
    val currentPage = if (rootAvailable) selected else 0
    val labels = buildList {
        add(stringResource(R.string.nav_diagnostic_options))
        if (rootAvailable) {
            add(stringResource(R.string.nav_kernel_logs))
            if (BuildConfig.DEBUG) add(stringResource(R.string.nav_webui_debug))
        }
    }
    WorkspaceScaffold(stringResource(R.string.nav_diagnostics), navigator, navigationEnabled = !saving, tabs = {
        if (labels.size > 1) WorkspaceTabs(labels, currentPage.coerceAtMost(labels.lastIndex), { selected = it }, enabled = !saving)
    }) {
        pages.SaveableStateProvider(currentPage) {
        when (currentPage) {
            0 -> Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(bottom = 16.dp)) {
                val logsEnabled = if (rootAvailable) rememberAuthorizationLogsEnabled() else false
                DiagnosticExportActions(additionalActions = if (logsEnabled) {
                    {
                        SettingItem(
                            icon = Icons.Outlined.History,
                            title = stringResource(R.string.nav_records),
                            summary = stringResource(R.string.nav_records_summary),
                            enabled = !saving,
                            groupPosition = SettingsItemPosition.Last,
                            onClick = { navigator.navigate(AuthorizationRecordsScreenDestination) },
                        )
                    }
                } else null)
                KsuIsValid {
                    KasumiWorkspace(KasumiSection.Debug, scrollable = false, onSavingChanged = { saving = it })
                }
            }
            1 -> KsuIsValid { KasumiWorkspace(KasumiSection.Logs) }
            else -> MoreSettingsContent(PreferenceCategory.WebUI)
        }
        }
    }
}

/** Refresh availability when returning from the kernel controls or another app. */
@Composable
private fun rememberAuthorizationLogsEnabled(): Boolean {
    val lifecycleOwner = LocalLifecycleOwner.current
    var nativeEnabled by remember { mutableStateOf(Natives.isSuLogEnabled()) }
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                FeatureControlState.refreshSuLog()
                nativeEnabled = Natives.isSuLogEnabled()
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }
    return FeatureControlState.suLogEnabled ?: nativeEnabled
}

/** Prevent navigation from disposing a plugin operation while its progress is visible. */
@Composable
internal fun WorkspaceOperationGuard(busy: Boolean, route: String) {
    val guard = LocalNavigationLeaveGuard.current
    val owner = remember { Any() }
    DisposableEffect(busy, route, guard) {
        if (busy) guard.register(owner, route) { _, onIntercepted -> onIntercepted() }
        onDispose { guard.unregister(owner) }
    }
    BackHandler(busy) {}
}
