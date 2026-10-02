package com.anatdx.yukisu.ui.kasumi

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.HomeSummaryCard
import com.anatdx.yukisu.ui.component.YukiAlertDialog
import com.anatdx.yukisu.ui.kasumi.util.KasumiUiAdapter as Controller
import kotlinx.coroutines.CancellationException

/** Home owns the manager/version guard and its existing pull-to-refresh trigger. */
@Composable
internal fun HomeKasumiCard(
    modifier: Modifier = Modifier,
    canReadStatus: Boolean,
    refreshKey: Long,
) {
    val resources = LocalResources.current
    var loaded by remember { mutableStateOf<Controller.UiState?>(null) }
    var loading by remember { mutableStateOf(canReadStatus) }
    var loadError by remember { mutableStateOf<String?>(null) }
    var retry by remember { mutableIntStateOf(0) }

    LaunchedEffect(canReadStatus, refreshKey, retry) {
        if (!canReadStatus) {
            loaded = null
            loadError = null
            loading = false
            return@LaunchedEffect
        }
        loading = true
        try {
            // Preserve the last successful snapshot while refreshing the dialog or home.
            loaded = Controller.load()
            loadError = null
        } catch (error: Exception) {
            if (error is CancellationException) throw error
            loadError = resources.getString(
                R.string.kasumi_toast_load_error,
                error.message ?: resources.getString(R.string.home_ksud_daemon_unknown),
            )
        } finally {
            loading = false
        }
    }

    KasumiStatusSummaryCard(
        modifier = modifier,
        status = loaded?.status,
        builtin = true,
        version = loaded?.version,
        systemInfo = loaded?.system,
        loading = loading,
        loadError = loadError,
        refreshEnabled = canReadStatus && !loading,
        onRefresh = { retry++ },
    )
}

/** The home tile and legacy status destination share the same information dialog. */
@Composable
internal fun KasumiStatusSummaryCard(
    status: Controller.KasumiStatus?,
    builtin: Boolean,
    version: String?,
    systemInfo: Controller.SystemInfo?,
    onRefresh: () -> Unit,
    modifier: Modifier = Modifier,
    loading: Boolean = false,
    loadError: String? = null,
    refreshEnabled: Boolean = true,
    includeKernel: Boolean = false,
) {
    var showDetails by rememberSaveable { mutableStateOf(false) }
    val statusText = kasumiStatusText(status, builtin)
    val warning = if (systemInfo?.kasumiMismatch == true) {
        systemInfo.mismatchMessage ?: stringResource(R.string.kasumi_mismatch_default)
    } else null
    val versionText = version?.takeIf { status == Controller.KasumiStatus.AVAILABLE }?.let {
        stringResource(R.string.kasumi_version_label, systemValueText(it))
    }
    val description = when {
        loadError != null -> stringResource(R.string.home_status_load_failed)
        warning != null -> stringResource(R.string.home_status_protocol_mismatch)
        loading && status == null -> stringResource(R.string.home_status_loading)
        else -> listOfNotNull(statusText, versionText).joinToString(" · ")
    }
    val hasWarning = loadError != null || warning != null || status in setOf(
        Controller.KasumiStatus.KERNEL_TOO_OLD,
        Controller.KasumiStatus.MODULE_TOO_OLD,
    )

    HomeSummaryCard(
        title = stringResource(R.string.kasumi_kernel_title),
        description = description,
        modifier = modifier,
        onClick = { showDetails = true },
        descriptionColor = if (hasWarning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
    )

    if (showDetails) {
        YukiAlertDialog(
            onDismissRequest = { showDetails = false },
            title = { Text(stringResource(R.string.kasumi_kernel_title)) },
            text = {
                Column(
                    Modifier.fillMaxWidth().verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    if (loading) {
                        Box(Modifier.fillMaxWidth().padding(vertical = 8.dp), contentAlignment = Alignment.Center) {
                            CircularProgressIndicator()
                        }
                    }
                    loadError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                    warning?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                    Text(statusText, style = MaterialTheme.typography.bodyMedium)
                    versionText?.let { Text(it, style = MaterialTheme.typography.bodyMedium) }
                    systemInfo?.let {
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                        KasumiRuntimeDetails(it, includeKernel)
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showDetails = false }) { Text(stringResource(R.string.close)) }
            },
            dismissButton = {
                TextButton(onClick = onRefresh, enabled = refreshEnabled && !loading) {
                    Text(stringResource(R.string.kasumi_rules_refresh))
                }
            },
        )
    }
}

@Composable
private fun KasumiRuntimeDetails(systemInfo: Controller.SystemInfo, includeKernel: Boolean) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (includeKernel) {
            RuntimeInfo(stringResource(R.string.kasumi_info_kernel), systemValueText(systemInfo.kernel), monospace = true)
        }
        RuntimeInfo(stringResource(R.string.kasumi_runtime_views), when (systemInfo.viewsEnabled) {
            true -> stringResource(R.string.kasumi_views_on)
            false -> stringResource(R.string.kasumi_views_off)
            null -> stringResource(R.string.home_ksud_daemon_unknown)
        })
        RuntimeInfo(
            stringResource(R.string.kasumi_mirror_path),
            systemValueText(systemInfo.mountBase.ifBlank { "none" }),
            monospace = true,
        )
        if (systemInfo.activeMounts.isNotEmpty()) {
            RuntimeInfo(
                stringResource(R.string.kasumi_info_active_mounts),
                systemInfo.activeMounts.joinToString("\n"),
                monospace = true,
            )
        }
        if (systemInfo.hooks.isNotBlank()) {
            RuntimeInfo(stringResource(R.string.kasumi_kernel_hooks), systemInfo.hooks, monospace = true)
        }
        systemInfo.mountStats?.let {
            RuntimeInfo(stringResource(R.string.kasumi_mount_total), it.totalMounts.toString())
            RuntimeInfo("OverlayFS", it.overlayfsMounts.toString())
        }
    }
}

@Composable
private fun RuntimeInfo(label: String, value: String, monospace: Boolean = false) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = if (monospace) FontFamily.Monospace else FontFamily.Default,
        )
    }
}

@Composable
private fun kasumiStatusText(status: Controller.KasumiStatus?, builtin: Boolean): String = stringResource(when {
    status == Controller.KasumiStatus.AVAILABLE && builtin -> R.string.kasumi_status_builtin
    status == Controller.KasumiStatus.AVAILABLE -> R.string.kasumi_status_available
    status == Controller.KasumiStatus.NOT_PRESENT -> R.string.kasumi_status_not_present
    status == Controller.KasumiStatus.KERNEL_TOO_OLD -> R.string.kasumi_status_kernel_too_old
    status == Controller.KasumiStatus.MODULE_TOO_OLD -> R.string.kasumi_status_module_too_old
    else -> R.string.home_ksud_daemon_unknown
})

@Composable
private fun systemValueText(value: String): String = when {
    value.equals("none", ignoreCase = true) -> stringResource(R.string.kasumi_mount_mode_none)
    value.isBlank() || value.equals("unknown", ignoreCase = true) || value == "-" || value == "—" -> stringResource(R.string.home_ksud_daemon_unknown)
    else -> value
}
