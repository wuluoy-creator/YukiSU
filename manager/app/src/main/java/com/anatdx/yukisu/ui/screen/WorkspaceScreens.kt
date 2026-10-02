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
import com.anatdx.yukisu.ui.util.LocalNavigationLeaveGuard
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.*
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import ui.screen.feature.FeatureControlContent
import ui.screen.feature.FeatureControlState
import ui.screen.moreSettings.MoreSettingsContent
import ui.screen.moreSettings.PreferenceCategory
import ui.screen.yukizygisk.InjectionSection
import ui.screen.yukizygisk.InjectionWorkspace

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
    SecondaryScrollableTabRow(
        selectedTabIndex = selected,
        containerColor = MaterialTheme.colorScheme.background,
        edgePadding = 16.dp,
        divider = { HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant) },
    ) {
        labels.forEachIndexed { index, label ->
            Tab(
                selected = index == selected,
                enabled = enabled,
                onClick = {
                    focusManager.clearFocus()
                    onSelected(index)
                },
                text = { Text(label, style = MaterialTheme.typography.titleSmall) },
            )
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
    var selected by rememberSaveable { mutableIntStateOf(0) }
    val pages = rememberSaveableStateHolder()
    val labels = listOf(stringResource(R.string.nav_apps), stringResource(R.string.nav_policies), stringResource(R.string.nav_records))
    val tabs: @Composable () -> Unit = { WorkspaceTabs(labels, selected, { selected = it }) }
    BackHandler(selected != 0) { selected = 0 }
    pages.SaveableStateProvider(selected) {
        when (selected) {
            0 -> SuperUserPage(navigator, sectionNavigation = tabs)
            1 -> WorkspaceScaffold(stringResource(R.string.nav_authorization), navigator, showBack = false, tabs = tabs) {
                AuthorizationSettingsContent(navigator)
            }
            else -> KsuIsValid {
                if (rememberAuthorizationLogsEnabled()) {
                    LogViewerPage(navigator, showBack = false, sectionNavigation = tabs)
                } else {
                    WorkspaceScaffold(stringResource(R.string.nav_authorization), navigator, showBack = false, tabs = tabs) {
                        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp)) {
                            Text(stringResource(R.string.nav_records_disabled), style = MaterialTheme.typography.bodyLarge)
                            Spacer(Modifier.height(16.dp))
                            OutlinedButton(onClick = { navigator.navigate(KernelPolicyScreenDestination) }) {
                                Text(stringResource(R.string.nav_manage_kernel_features))
                            }
                        }
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
fun ExtensionRuntimeScreen(navigator: DestinationsNavigator, initialPage: Int = 0) {
    var selected by rememberSaveable { mutableIntStateOf(initialPage.coerceIn(0, 2)) }
    var kasumiSaving by remember { mutableStateOf(false) }
    var injectionSaving by remember { mutableStateOf(false) }
    val saving = kasumiSaving || injectionSaving
    val pages = rememberSaveableStateHolder()
    WorkspaceOperationGuard(saving, ExtensionRuntimeScreenDestination.route)
    val labels = listOf(stringResource(R.string.nav_mount), stringResource(R.string.nav_injection_monitor), stringResource(R.string.nav_injection_config))
    WorkspaceScaffold(stringResource(R.string.nav_extension_runtime), navigator, navigationEnabled = !saving, tabs = {
        WorkspaceTabs(labels, selected, { selected = it }, enabled = !saving)
    }) {
        KsuIsValid {
            pages.SaveableStateProvider(if (selected == 0) "mount" else "injection") {
                if (selected == 0) KasumiWorkspace(KasumiSection.Mount, onSavingChanged = { kasumiSaving = it })
                else InjectionWorkspace(if (selected == 1) InjectionSection.Overview else InjectionSection.Configuration, onSavingChanged = { injectionSaving = it })
            }
        }
    }
}

@Destination<RootGraph>
@Composable
fun KernelPolicyScreen(navigator: DestinationsNavigator) {
    var selected by rememberSaveable { mutableIntStateOf(0) }
    var saving by remember { mutableStateOf(false) }
    val pages = rememberSaveableStateHolder()
    WorkspaceOperationGuard(saving, KernelPolicyScreenDestination.route)
    val labels = listOf(stringResource(R.string.nav_isolation), stringResource(R.string.nav_active_rules), stringResource(R.string.nav_kernel_features), stringResource(R.string.nav_advanced_security))
    WorkspaceScaffold(stringResource(R.string.nav_kernel_policy), navigator, navigationEnabled = !saving, tabs = {
        WorkspaceTabs(labels, selected, { selected = it }, enabled = !saving)
    }) {
        KsuIsValid {
            pages.SaveableStateProvider(if (selected < 2) "isolation" else selected.toString()) {
            when (selected) {
                0, 1 -> KasumiWorkspace(
                    section = if (selected == 0) KasumiSection.Isolation else KasumiSection.Rules,
                    onSavingChanged = { saving = it },
                    settingsHeader = {
                        if (selected == 0) SettingItem(
                            icon = Icons.Outlined.FolderOff,
                            title = stringResource(R.string.nav_umount_paths),
                            summary = stringResource(R.string.nav_umount_paths_summary),
                            enabled = !saving,
                            onClick = { navigator.navigate(UmountManagerScreenDestination) },
                        )
                    },
                )
                2 -> FeatureControlContent(navigator, onSavingChanged = { saving = it })
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
    var kasumiSaving by remember { mutableStateOf(false) }
    var injectionSaving by remember { mutableStateOf(false) }
    val saving = kasumiSaving || injectionSaving
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
                DiagnosticExportActions()
                KsuIsValid {
                    if (rememberAuthorizationLogsEnabled()) SettingItem(
                        icon = Icons.Outlined.History,
                        title = stringResource(R.string.nav_records),
                        summary = stringResource(R.string.nav_records_summary),
                        enabled = !saving,
                        onClick = { navigator.navigate(LogViewerScreenDestination) },
                    )
                    KasumiWorkspace(KasumiSection.Debug, scrollable = false, onSavingChanged = { kasumiSaving = it })
                    InjectionWorkspace(InjectionSection.Diagnostics, scrollable = false, onSavingChanged = { injectionSaving = it })
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
