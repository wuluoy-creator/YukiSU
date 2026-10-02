package ui.screen.feature

import android.widget.Toast
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.rounded.RemoveCircle
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.res.vectorResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.Natives
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.KsuIsValid
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.screen.SettingItem
import com.anatdx.yukisu.ui.theme.isExpressiveUi
import com.anatdx.yukisu.ui.util.LocalSnackbarHost
import com.anatdx.yukisu.ui.util.getFeatureStatus
import com.anatdx.yukisu.ui.util.getFeatureValue
import com.anatdx.yukisu.ui.util.restartAdbd
import com.anatdx.yukisu.ui.util.setFeatureValue
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.UmountManagerScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import ui.screen.moreSettings.component.MoreSettingsItemPosition
import ui.screen.moreSettings.component.SettingsCard
import ui.screen.moreSettings.component.SwitchSettingItem

internal object FeatureControlState {
    var suLogEnabled by mutableStateOf<Boolean?>(null)
        private set

    fun refreshSuLog() {
        suLogEnabled = getFeatureValue(Natives.FEATURE_SULOG)
    }

    fun updateSuLog(enabled: Boolean) {
        suLogEnabled = enabled
    }
}

internal class FeatureToggleState(initialChecked: Boolean) {
    var checked by mutableStateOf(initialChecked)
    var saving by mutableStateOf(false)
}

@Composable
internal fun rememberFeatureToggleState(
    featureId: Int,
    displayInverted: Boolean = false
): FeatureToggleState = remember(featureId, displayInverted) {
    val enabled = getFeatureValue(featureId)
    FeatureToggleState(if (displayInverted) !enabled else enabled)
}

private fun CoroutineScope.persistFeature(
    state: FeatureToggleState,
    featureId: Int,
    featureName: String,
    kernelEnabled: Boolean,
    displayInverted: Boolean = false,
    relatedStates: List<FeatureToggleState> = emptyList(),
    optimistic: Boolean = false,
    afterPersist: suspend () -> Boolean = { true },
    afterPersistenceFailure: suspend () -> Unit = {},
    afterPostRollback: suspend () -> Unit = {},
    onRuntimeSettled: (Boolean) -> Unit = {},
    onSuccess: suspend () -> Unit = {},
    onFailure: suspend () -> Unit = {}
) {
    val savingStates = relatedStates + state
    if (savingStates.any { it.saving }) return

    val previousKernelEnabled = getFeatureValue(featureId)
    if (optimistic) {
        state.checked = if (displayInverted) !kernelEnabled else kernelEnabled
    }
    savingStates.forEach { it.saving = true }
    launch(start = CoroutineStart.UNDISPATCHED) {
        // Persistence and required runtime follow-ups must survive leaving this screen.
        val success = withContext(NonCancellable) {
            var persisted = setFeatureValue(featureName, kernelEnabled)
            if (persisted && !afterPersist()) {
                setFeatureValue(featureName, previousKernelEnabled)
                afterPostRollback()
                persisted = false
            } else if (!persisted) {
                afterPersistenceFailure()
            }
            val current = getFeatureValue(featureId)
            onRuntimeSettled(current)
            state.checked = if (displayInverted) !current else current
            savingStates.forEach { it.saving = false }
            persisted
        }
        if (success) onSuccess() else onFailure()
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun FeatureControlScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    Scaffold(
        topBar = {
            FeatureControlTopBar(onBack = { navigator.popBackStack() }, scrollBehavior = scrollBehavior)
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { padding ->
        Box(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
            FeatureControlContent(navigator)
        }
    }
}

@Composable
fun FeatureControlContent(
    navigator: DestinationsNavigator,
    onSavingChanged: (Boolean) -> Unit = {},
    scrollable: Boolean = true,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHost = LocalSnackbarHost.current

    val selinuxHide = rememberFeatureToggleState(Natives.FEATURE_SELINUX_HIDE)
    val kernelUmountDisabled = rememberFeatureToggleState(
        Natives.FEATURE_KERNEL_UMOUNT,
        displayInverted = true
    )
    val webViewZygoteUmount = rememberFeatureToggleState(
        Natives.FEATURE_WEBVIEW_ZYGOTE_UMOUNT
    )
    val adbRoot = rememberFeatureToggleState(Natives.FEATURE_ADB_ROOT)
    val hideBootloader = rememberFeatureToggleState(Natives.FEATURE_HIDE_BOOTLOADER)

    val saving = listOf(
        selinuxHide, kernelUmountDisabled, webViewZygoteUmount,
        adbRoot, hideBootloader,
    ).any { it.saving }
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(saving) }
    DisposableEffect(Unit) {
        onDispose { savingCallback(false) }
    }

    val savedRebootMessage = stringResource(R.string.setting_change_saved_reboot)
    val failedMessage = stringResource(R.string.setting_change_failed)

    Column(
        modifier = (if (scrollable) {
            Modifier.fillMaxSize().verticalScroll(rememberScrollState())
        } else {
            Modifier.fillMaxWidth()
        })
            .padding(horizontal = 16.dp)
            .padding(top = 8.dp)
    ) {
        KsuIsValid {
            SettingsCard {
                FeatureSwitchItem(
                    featureId = Natives.FEATURE_SELINUX_HIDE,
                    icon = Icons.Filled.Security,
                    title = stringResource(R.string.settings_selinux_hide),
                    summary = stringResource(R.string.settings_selinux_hide_summary),
                    state = selinuxHide,
                    groupPosition = MoreSettingsItemPosition.First,
                    onChange = { enabled ->
                        scope.persistFeature(
                            state = selinuxHide,
                            featureId = Natives.FEATURE_SELINUX_HIDE,
                            featureName = "selinux_hide",
                            kernelEnabled = enabled,
                            onSuccess = { snackbarHost.showSnackbar(savedRebootMessage) },
                            onFailure = { snackbarHost.showSnackbar(failedMessage) }
                        )
                    }
                )

                FeatureSwitchItem(
                    featureId = Natives.FEATURE_KERNEL_UMOUNT,
                    icon = Icons.Rounded.RemoveCircle,
                    title = stringResource(R.string.settings_disable_kernel_umount),
                    summary = stringResource(R.string.settings_disable_kernel_umount_summary),
                    state = kernelUmountDisabled,
                    onChange = { disabled ->
                        scope.persistFeature(
                            state = kernelUmountDisabled,
                            featureId = Natives.FEATURE_KERNEL_UMOUNT,
                            featureName = "kernel_umount",
                            kernelEnabled = !disabled,
                            displayInverted = true
                        )
                    }
                )

                FeatureSwitchItem(
                    featureId = Natives.FEATURE_WEBVIEW_ZYGOTE_UMOUNT,
                    icon = Icons.Filled.Language,
                    title = stringResource(R.string.settings_webview_zygote_umount),
                    summary = stringResource(R.string.settings_webview_zygote_umount_summary),
                    state = webViewZygoteUmount,
                    onChange = { enabled ->
                        scope.persistFeature(
                            state = webViewZygoteUmount,
                            featureId = Natives.FEATURE_WEBVIEW_ZYGOTE_UMOUNT,
                            featureName = "webview_zygote_umount",
                            kernelEnabled = enabled,
                            onSuccess = { snackbarHost.showSnackbar(savedRebootMessage) },
                            onFailure = { snackbarHost.showSnackbar(failedMessage) }
                        )
                    }
                )

                SettingItem(
                    icon = Icons.Filled.FolderOff,
                    title = stringResource(R.string.nav_umount_paths),
                    summary = stringResource(R.string.nav_umount_paths_summary),
                    enabled = !saving,
                    onClick = { navigator.navigate(UmountManagerScreenDestination) },
                )

                FeatureSwitchItem(
                    featureId = Natives.FEATURE_ADB_ROOT,
                    icon = Icons.Filled.DeveloperMode,
                    title = stringResource(R.string.settings_adb_root),
                    summary = stringResource(R.string.settings_adb_root_summary),
                    state = adbRoot,
                    groupPosition = MoreSettingsItemPosition.Last,
                    onChange = { enabled ->
                        scope.persistFeature(
                            state = adbRoot,
                            featureId = Natives.FEATURE_ADB_ROOT,
                            featureName = "adb_root",
                            kernelEnabled = enabled,
                            afterPersist = {
                                withContext(Dispatchers.IO) { restartAdbd() }
                            },
                            afterPostRollback = {
                                withContext(Dispatchers.IO) { restartAdbd() }
                            },
                            onFailure = {
                                snackbarHost.showSnackbar(failedMessage)
                            }
                        )
                    }
                )
            }

            SettingsCard {
                FeatureSwitchItem(
                    featureId = Natives.FEATURE_HIDE_BOOTLOADER,
                    icon = Icons.Filled.Lock,
                    title = stringResource(R.string.hide_bl_title),
                    summary = if (hideBootloader.checked) {
                        stringResource(R.string.hide_bl_enabled)
                    } else {
                        stringResource(R.string.hide_bl_disabled)
                    },
                    state = hideBootloader,
                    groupPosition = MoreSettingsItemPosition.Only,
                    onChange = { enabled ->
                        scope.persistFeature(
                            state = hideBootloader,
                            featureId = Natives.FEATURE_HIDE_BOOTLOADER,
                            featureName = "hide_bootloader",
                            kernelEnabled = enabled,
                            onSuccess = {
                                Toast.makeText(
                                    context,
                                    if (enabled) {
                                        R.string.hide_bl_enabled_toast
                                    } else {
                                        R.string.hide_bl_disabled_toast
                                    },
                                    Toast.LENGTH_SHORT
                                ).show()
                            },
                            onFailure = {
                                Toast.makeText(
                                    context,
                                    R.string.hide_bl_change_failed,
                                    Toast.LENGTH_SHORT
                                ).show()
                            }
                        )
                    }
                )
            }
        }

        if (scrollable) Spacer(modifier = Modifier.height(8.dp))
    }

}

/** Native isolation controls embedded beside the shared Kasumi settings. */
@Composable
internal fun IsolationKernelSettings(
    enabled: Boolean = true,
    onSavingChanged: (Boolean) -> Unit = {},
) {
    val scope = rememberCoroutineScope()
    val snackbarHost = LocalSnackbarHost.current
    val unshareMnt = rememberFeatureToggleState(Natives.FEATURE_UNSHARE_MNT)
    val ksmSupported = remember { getFeatureStatus(Natives.FEATURE_KASUMI_SUCOMPAT) == "supported" }
    var suPathSaving by remember { mutableStateOf(false) }
    val saving = unshareMnt.saving || suPathSaving
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(saving) }
    DisposableEffect(Unit) { onDispose { savingCallback(false) } }
    val failedMessage = stringResource(R.string.setting_change_failed)

    FeatureSwitchItem(
        featureId = Natives.FEATURE_UNSHARE_MNT,
        icon = ImageVector.vectorResource(R.drawable.ic_mount_view_cleanup),
        title = stringResource(R.string.settings_unshare_mnt),
        summary = stringResource(R.string.settings_unshare_mnt_summary),
        state = unshareMnt,
        groupPosition = if (ksmSupported) MoreSettingsItemPosition.First else MoreSettingsItemPosition.Only,
        enabled = enabled && !saving,
        onChange = { checked ->
            if (enabled && !unshareMnt.saving && !suPathSaving) {
                scope.persistFeature(
                    state = unshareMnt,
                    featureId = Natives.FEATURE_UNSHARE_MNT,
                    featureName = "unshare_mnt",
                    kernelEnabled = checked,
                    onFailure = { snackbarHost.showSnackbar(failedMessage) },
                )
            }
        },
    )
    if (ksmSupported) {
        SuPathSetting(
            enabled = enabled && !saving,
            onSavingChange = { suPathSaving = it },
        )
    }
}

@Composable
internal fun SuperuserLogSetting(
    enabled: Boolean = true,
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Last,
    onSavingChanged: (Boolean) -> Unit = {},
) {
    val scope = rememberCoroutineScope()
    val suLog = rememberFeatureToggleState(Natives.FEATURE_SULOG)
    val savingCallback by rememberUpdatedState(onSavingChanged)
    SideEffect { savingCallback(suLog.saving) }
    DisposableEffect(Unit) { onDispose { savingCallback(false) } }

    FeatureSwitchItem(
        featureId = Natives.FEATURE_SULOG,
        icon = Icons.Filled.Visibility,
        title = stringResource(R.string.settings_disable_sulog),
        summary = stringResource(R.string.settings_disable_sulog_summary),
        state = suLog,
        groupPosition = groupPosition,
        enabled = enabled,
        onChange = { checked ->
            if (enabled && !suLog.saving) {
                scope.persistFeature(
                    state = suLog,
                    featureId = Natives.FEATURE_SULOG,
                    featureName = "sulog",
                    kernelEnabled = checked,
                    onRuntimeSettled = FeatureControlState::updateSuLog,
                )
            }
        },
    )
}

@Composable
internal fun YukiZygiskFeatureSwitch(state: FeatureToggleState, enabled: Boolean = true) {
    val scope = rememberCoroutineScope()
    val snackbarHost = LocalSnackbarHost.current
    val enabledMessage = stringResource(R.string.settings_yukizygisk_toast_on)
    val disabledMessage = stringResource(R.string.settings_yukizygisk_toast_off)
    val failedMessage = stringResource(R.string.settings_yukizygisk_toast_failed)

    FeatureSwitchItem(
        featureId = Natives.FEATURE_YUKIZYGISK,
        icon = Icons.Filled.Extension,
        title = stringResource(R.string.settings_yukizygisk),
        summary = stringResource(R.string.settings_yukizygisk_summary),
        state = state,
        groupPosition = MoreSettingsItemPosition.Only,
        enabled = enabled,
        onChange = { checked ->
            if (enabled && !state.saving) {
                scope.persistFeature(
                    state = state,
                    featureId = Natives.FEATURE_YUKIZYGISK,
                    featureName = "yukizygisk",
                    kernelEnabled = checked,
                    optimistic = true,
                    onSuccess = {
                        snackbarHost.showSnackbar(if (checked) enabledMessage else disabledMessage)
                    },
                    onFailure = { snackbarHost.showSnackbar(failedMessage) },
                )
            }
        },
    )
}

@Composable
private fun FeatureSwitchItem(
    featureId: Int,
    icon: ImageVector,
    title: String,
    summary: String,
    state: FeatureToggleState,
    groupPosition: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle,
    enabled: Boolean = true,
    onChange: (Boolean) -> Unit
) {
    val status = remember(featureId) { getFeatureStatus(featureId) }
    val renderedSummary = when (status) {
        "unsupported" -> stringResource(R.string.feature_status_unsupported_summary)
        "managed" -> stringResource(R.string.feature_status_managed_summary)
        else -> summary
    }

    SwitchSettingItem(
        icon = icon,
        title = title,
        summary = renderedSummary,
        checked = state.checked,
        enabled = enabled && status == "supported" && !state.saving,
        groupPosition = groupPosition,
        onChange = onChange
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun FeatureControlTopBar(
    onBack: () -> Unit,
    scrollBehavior: TopAppBarScrollBehavior
) {
    val title: @Composable () -> Unit = {
        Text(
            text = stringResource(R.string.feature_control),
            fontWeight = if (isExpressiveUi) FontWeight.Normal else null
        )
    }
    val navigationIcon: @Composable () -> Unit = {
        IconButton(onClick = onBack) {
            YukiIcon(
                imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = stringResource(R.string.back)
            )
        }
    }
    val colors = TopAppBarDefaults.topAppBarColors(
        containerColor = MaterialTheme.colorScheme.surfaceContainerLow,
        scrolledContainerColor = MaterialTheme.colorScheme.surfaceContainerLow
    )
    val windowInsets = WindowInsets.safeDrawing.only(
        WindowInsetsSides.Top + WindowInsetsSides.Horizontal
    )

    if (isExpressiveUi) {
        TopAppBar(
            title = title,
            navigationIcon = navigationIcon,
            colors = colors,
            windowInsets = windowInsets,
            scrollBehavior = scrollBehavior
        )
    } else {
        TopAppBar(
            title = title,
            navigationIcon = navigationIcon,
            colors = colors,
            windowInsets = windowInsets,
            scrollBehavior = scrollBehavior
        )
    }
}
