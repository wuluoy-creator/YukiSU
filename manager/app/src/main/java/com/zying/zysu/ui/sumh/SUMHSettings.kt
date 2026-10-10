package com.zying.zysu.ui.sumh

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Subject
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.zying.zysu.ui.util.SnackbarController
import com.zying.zysu.R
import com.zying.zysu.ui.component.YukiIcon
import com.zying.zysu.ui.component.YukiPanel
import com.zying.zysu.ui.sumh.util.SUMHUiAdapter as Controller
import com.zying.zysu.ui.theme.getCardColors
import com.zying.zysu.ui.theme.getCardBorder
import com.zying.zysu.ui.theme.getCardElevation
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch
import ui.screen.moreSettings.component.MoreSettingsItemPosition

@Composable
internal fun SettingsTab(
    config: Controller.SUMHPConfig,
    sumhStatus: Controller.SUMHStatus,
    features: Controller.FeaturesResult?,
    snackbarHostState: SnackbarController,
    controlsEnabled: Boolean,
    runtimeApplyPending: Boolean,
    onRetryApply: () -> Unit,
    onConfigChanged: (Controller.SUMHPConfig) -> Unit,
    onClearMapRules: () -> Unit,
    onAddMapRule: (Long, Long, Long, Long, String) -> Unit,
    section: SUMHSection,
    scrollable: Boolean = true,
    header: @Composable () -> Unit = {},
    contentPadding: PaddingValues = PaddingValues(horizontal = 18.dp, vertical = 16.dp),
    headingActions: (@Composable RowScope.() -> Unit)? = null,
) {
    val enabled = controlsEnabled
    val mountEnabled = enabled && config.externalOwner.isEmpty()
    val kernelEnabled = enabled && sumhStatus == Controller.SUMHStatus.AVAILABLE
    Column(
        modifier = (if (scrollable) Modifier.fillMaxSize().verticalScroll(rememberScrollState()) else Modifier.fillMaxWidth())
            .padding(contentPadding),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        header()
        when (section) {
            SUMHSection.Mount -> {
                ConfigSection(stringResource(R.string.sumh_group_mount_backends), segmented = true, actions = headingActions) {
                    if (config.externalOwner.isNotEmpty()) {
                        SUMHControlGroup {
                            Text(stringResource(R.string.sumh_external_mount, config.externalOwner),
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    SettingSwitch("OverlayFS", stringResource(R.string.sumh_backend_enable_desc), config.overlayfsEnabled,
                        { onConfigChanged(config.copy(overlayfsEnabled = it)) }, mountEnabled, Icons.Filled.Layers,
                        MoreSettingsItemPosition.First)
                    SettingSwitch("Magic Mount", stringResource(R.string.sumh_backend_enable_desc), config.magicMountEnabled,
                        { onConfigChanged(config.copy(magicMountEnabled = it)) }, mountEnabled, Icons.Filled.AutoFixHigh,
                        MoreSettingsItemPosition.Last)
                }
                // These values are shared by the mount backends, not owned by either switch.
                ConfigSection(stringResource(R.string.sumh_group_mount_storage), segmented = true) {
                    FileSystemChoice(config.fsType, mountEnabled) {
                        onConfigChanged(config.copy(fsType = it))
                    }
                    MountSourceInput(config.mountsource, mountEnabled) { onConfigChanged(config.copy(mountsource = it)) }
                    EditableConfigPath(R.string.sumh_tempdir, R.string.sumh_tempdir_desc, config.tempdir, mountEnabled, true) {
                        onConfigChanged(config.copy(tempdir = it))
                    }
                    EditableConfigPath(R.string.sumh_mirror_path, R.string.sumh_mirror_path_desc, config.mirrorPath, mountEnabled, true,
                        MoreSettingsItemPosition.Last) {
                        onConfigChanged(config.copy(mirrorPath = it))
                    }
                }
            }
            SUMHSection.Isolation -> {
                val mountHideSupported = features?.names?.contains("mount_hide") == true
                val mapsSupported = features?.names?.contains("maps_spoof") == true
                KernelBuildSpoofCard(
                    config = config,
                    controlsEnabled = enabled,
                    kernelEnabled = kernelEnabled,
                    supported = features?.names?.contains("kernel_build_spoof") == true,
                    onConfigChanged = onConfigChanged,
                    headingActions = headingActions,
                )
                ConfigSection(stringResource(R.string.sumh_group_mount_visibility), segmented = true) {
                    SettingSwitch(stringResource(R.string.sumh_stealth), stringResource(R.string.sumh_stealth_desc), config.enableStealth,
                        { onConfigChanged(config.copy(enableStealth = it)) }, kernelEnabled, Icons.Filled.VisibilityOff,
                        MoreSettingsItemPosition.First)
                    FeatureSettingSwitch(
                        title = R.string.sumh_mount_hide,
                        subtitle = R.string.sumh_mount_hide_desc,
                        checked = config.enableMountHide,
                        supported = mountHideSupported,
                        controlsEnabled = enabled,
                        kernelEnabled = kernelEnabled,
                        icon = Icons.Filled.FolderOff,
                        position = MoreSettingsItemPosition.Last,
                        onCheckedChange = { onConfigChanged(config.copy(enableMountHide = it)) },
                    )
                }
                ConfigSection(stringResource(R.string.sumh_group_filesystem), segmented = true) {
                    FeatureSettingSwitch(
                        title = R.string.sumh_overlay_xattr,
                        subtitle = R.string.sumh_overlay_xattr_desc,
                        checked = config.enableOverlayXattrHide,
                        supported = features?.names?.contains("overlay_xattr_hide") == true,
                        controlsEnabled = enabled,
                        kernelEnabled = kernelEnabled,
                        icon = Icons.Filled.Security,
                        position = MoreSettingsItemPosition.First,
                        onCheckedChange = { onConfigChanged(config.copy(enableOverlayXattrHide = it)) },
                    )
                    FeatureSettingSwitch(
                        title = R.string.sumh_statfs_spoof,
                        subtitle = R.string.sumh_statfs_spoof_desc,
                        checked = config.enableStatfsSpoof,
                        supported = features?.names?.contains("statfs_spoof") == true,
                        controlsEnabled = enabled,
                        kernelEnabled = kernelEnabled,
                        icon = Icons.Filled.Storage,
                        position = MoreSettingsItemPosition.Last,
                        onCheckedChange = { onConfigChanged(config.copy(enableStatfsSpoof = it)) },
                    )
                }
                ConfigSection(stringResource(R.string.sumh_maps_title), segmented = true) {
                    val showMapRules = config.enableMapsSpoof && config.kernelAvailable && mapsSupported
                    FeatureSettingSwitch(
                        title = R.string.sumh_maps_spoof,
                        subtitle = R.string.sumh_maps_spoof_desc,
                        checked = config.enableMapsSpoof,
                        supported = mapsSupported,
                        controlsEnabled = enabled,
                        kernelEnabled = kernelEnabled,
                        icon = Icons.Filled.Memory,
                        position = if (showMapRules) MoreSettingsItemPosition.First else MoreSettingsItemPosition.Only,
                        onCheckedChange = { onConfigChanged(config.copy(enableMapsSpoof = it)) },
                    )
                    if (showMapRules) {
                        MapsSpoofControls(kernelEnabled, onClearMapRules, onAddMapRule)
                    }
                }
                // Custom path rules are independent of the stealth and mount-hide switches.
                UserHideRulesCard(enabled, snackbarHostState)
            }
            SUMHSection.Debug -> ConfigSection(stringResource(R.string.sumh_workspace_debug), segmented = true, actions = headingActions) {
                SettingSwitch(stringResource(R.string.sumh_debug), stringResource(R.string.sumh_debug_desc),
                    config.debug, { onConfigChanged(config.copy(debug = it)) }, enabled,
                    Icons.Filled.BugReport, MoreSettingsItemPosition.First)
                SettingSwitch(stringResource(R.string.sumh_verbose), stringResource(R.string.sumh_verbose_desc),
                    config.verbose, { onConfigChanged(config.copy(verbose = it)) }, enabled, Icons.AutoMirrored.Filled.Subject)
                SettingSwitch(stringResource(R.string.sumh_kernel_debug), stringResource(R.string.sumh_kernel_debug_desc), config.enableKernelDebug,
                    { onConfigChanged(config.copy(enableKernelDebug = it)) }, kernelEnabled,
                    Icons.Filled.BugReport, MoreSettingsItemPosition.Last)
            }
            else -> Unit
        }
        if (runtimeApplyPending) {
            ConfigSection(title = "") {
                Text(stringResource(R.string.sumh_apply_failed), color = MaterialTheme.colorScheme.error)
                OutlinedButton(onClick = onRetryApply, enabled = kernelEnabled, modifier = Modifier.fillMaxWidth()) {
                    YukiIcon(Icons.Filled.Refresh, null)
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.sumh_retry_apply))
                }
            }
        }
    }
}
@Composable
private fun FileSystemChoice(value: String, enabled: Boolean, onSelect: (String) -> Unit) {
    val options = listOf(
        "auto" to R.string.sumh_fs_type_auto,
        "ext4" to R.string.sumh_fs_type_ext4,
        "erofs" to R.string.sumh_fs_type_erofs,
        "tmpfs" to R.string.sumh_fs_type_tmpfs,
    )
    SUMHControlGroup(MoreSettingsItemPosition.First) {
        Text(
            stringResource(R.string.sumh_fs_type_title),
            style = MaterialTheme.typography.bodyLarge,
        )
        Spacer(Modifier.height(4.dp))
        FlowRow(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            options.forEach { (option, title) ->
                FilterChip(
                    selected = value == option,
                    onClick = { onSelect(option) },
                    enabled = enabled,
                    label = { Text(stringResource(title)) },
                    leadingIcon = if (value == option) {
                        { YukiIcon(Icons.Filled.Check, null, Modifier.size(FilterChipDefaults.IconSize)) }
                    } else null,
                )
            }
        }
    }
}

@Composable
private fun FeatureSettingSwitch(
    title: Int,
    subtitle: Int,
    checked: Boolean,
    supported: Boolean,
    controlsEnabled: Boolean,
    kernelEnabled: Boolean,
    icon: ImageVector,
    position: MoreSettingsItemPosition,
    onCheckedChange: (Boolean) -> Unit,
) {
    SettingSwitch(
        title = stringResource(title),
        subtitle = stringResource(when {
            supported -> subtitle
            checked -> R.string.sumh_disable_unsupported
            else -> R.string.feature_status_unsupported_summary
        }),
        checked = checked,
        onCheckedChange = onCheckedChange,
        enabled = controlsEnabled && ((kernelEnabled && supported) || (!supported && checked)),
        icon = icon,
        position = position,
    )
}

@Composable
internal fun ConfigSection(
    title: String = "",
    segmented: Boolean = false,
    actions: (@Composable RowScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (title.isNotEmpty()) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(start = 12.dp, end = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    title,
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.primary,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f).padding(vertical = 8.dp).semantics { heading() }
                )
                actions?.invoke(this)
            }
        }
        YukiPanel {
            Column(
                modifier = if (segmented) Modifier.padding(vertical = 4.dp) else Modifier.padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(if (segmented) 0.dp else 8.dp),
                content = content
            )
        }
    }
}

@Composable
internal fun ConfigChoice(title: String, value: String, values: List<String>, enabled: Boolean,
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Only, selected: (String) -> Unit) {
    val labels = mapOf("auto" to stringResource(R.string.sumh_mount_mode_auto), "sumh" to "SUMH",
        "overlay" to "OverlayFS", "magic" to "Magic Mount", "none" to stringResource(R.string.sumh_mount_mode_none),
        "normal" to stringResource(R.string.sumh_hide_normal), "aggressive" to stringResource(R.string.sumh_hide_aggressive), "magisk" to "Magisk",
        "all" to stringResource(R.string.sumh_filter_all), "sumh-active" to stringResource(R.string.sumh_filter_active))
    SUMHControlGroup(position) {
        ConfigChoiceField(title, labels[value] ?: value, values.map { it to (labels[it] ?: it) }, enabled, selected)
    }
}

/** Shared field for both standalone choices and choices inside an expanded settings card. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ConfigChoiceField(
    title: String,
    valueLabel: String,
    choices: List<Pair<String, String>>,
    enabled: Boolean,
    selected: (String) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    val menuExpanded = expanded && enabled
    ExposedDropdownMenuBox(
        expanded = menuExpanded,
        onExpandedChange = { if (enabled) expanded = it },
    ) {
        OutlinedTextField(
            value = valueLabel,
            onValueChange = {},
            readOnly = true,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth()
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable, enabled = enabled),
            shape = MaterialTheme.shapes.small,
            label = { Text(title) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(menuExpanded) },
        )
        ExposedDropdownMenu(expanded = menuExpanded, onDismissRequest = { expanded = false }) {
            choices.forEach { (choice, label) ->
                DropdownMenuItem(
                    text = { Text(label) },
                    onClick = { expanded = false; selected(choice) },
                )
            }
        }
    }
}

@Composable
internal fun MountSourceInput(value: String, enabled: Boolean, onSave: (String) -> Unit) {
    var draft by rememberSaveable(value) { mutableStateOf(value) }
    val changed = draft != value
    val valid = draft.isNotEmpty() && draft.none { it.isWhitespace() || it.isISOControl() || it == '\\' }

    fun save() {
        if (enabled && changed && valid) onSave(draft)
    }

    SUMHControlGroup(MoreSettingsItemPosition.Middle) {
        OutlinedTextField(
            value = draft,
            onValueChange = { draft = it },
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled,
            singleLine = true,
            shape = MaterialTheme.shapes.small,
            isError = changed && !valid,
            label = { Text(stringResource(R.string.sumh_mountsource)) },
            placeholder = { Text("KSU") },
            trailingIcon = if (changed) {
                {
                    IconButton(onClick = { save() }, enabled = enabled && valid) {
                        YukiIcon(Icons.Filled.Check, stringResource(R.string.sumh_rule_save))
                    }
                }
            } else null,
            supportingText = if (changed && !valid) {
                { Text(stringResource(R.string.sumh_mountsource_invalid)) }
            } else null,
            keyboardOptions = KeyboardOptions(autoCorrectEnabled = false, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { save() }),
        )
    }
}

@Composable
private fun EditableConfigPath(title: Int, subtitle: Int, value: String, enabled: Boolean, allowEmpty: Boolean,
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Middle, save: (String) -> Unit) {
    var draft by rememberSaveable(value) { mutableStateOf(value) }
    var invalid by remember(value) { mutableStateOf(false) }
    SettingTextField(stringResource(title), stringResource(subtitle), draft, { draft = it; invalid = false }, {
        if (draft.startsWith('/') || (allowEmpty && draft.isEmpty())) save(draft)
        else invalid = true
    }, enabled, position = position)
    if (invalid) Text(
        stringResource(R.string.sumh_absolute_path),
        color = MaterialTheme.colorScheme.error,
        style = MaterialTheme.typography.bodySmall,
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
    )
}

@Composable
private fun UserHideRulesCard(enabled: Boolean, snackbar: SnackbarController) {
    val scope = rememberCoroutineScope()
    val resources = LocalResources.current
    var states by remember { mutableStateOf(emptyList<Controller.ActiveRule>()) }
    val paths = states.map { it.src }
    var newPath by rememberSaveable { mutableStateOf("") }
    var working by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        try { states = Controller.userHideStates() }
        catch (error: Exception) { if (error is CancellationException) throw error; snackbar.showSnackbar(error.message.orEmpty()) }
    }
    fun change(path: String, remove: Boolean) {
        if (!enabled || working) return
        working = true
        scope.launch {
            try {
                val ok = if (remove) Controller.removeUserHideRule(path) else Controller.addUserHideRule(path)
                states = Controller.userHideStates()
                working = false
                if (ok) newPath = "" else snackbar.showSnackbar(resources.getString(R.string.operation_failed))
            } catch (error: Exception) { working = false; if (error is CancellationException) throw error; snackbar.showSnackbar(error.message.orEmpty()) }
            finally { working = false }
        }
    }
    fun refresh(path: String? = null) {
        if (working) return
        working = true
        scope.launch {
            try {
                val ok = when {
                    path == null -> true
                    states.find { it.src == path }?.hideState == -1 -> Controller.addUserHideRule(path)
                    else -> Controller.retryUserHide(path)
                }
                states = Controller.userHideStates()
                working = false
                if (!ok) snackbar.showSnackbar(resources.getString(R.string.operation_failed))
            } catch (error: Exception) {
                working = false
                if (error is CancellationException) throw error
                snackbar.showSnackbar(error.message.orEmpty())
            } finally { working = false }
        }
    }
    ConfigSection(
        title = stringResource(R.string.sumh_user_hide_title),
        actions = {
            IconButton(onClick = { refresh() }, enabled = !working) {
                YukiIcon(Icons.Filled.Refresh, stringResource(R.string.sumh_rules_refresh))
            }
        }
    ) {
        Text(
            stringResource(R.string.sumh_quick_hide),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Column {
            val externalStorage = android.os.Environment.getExternalStorageDirectory()
            listOf("/dev/scene", "/dev/cpuset/scene-daemon", externalStorage.resolve("Download/advanced").path, externalStorage.resolve("MT2").path).forEach { path ->
                TextButton(
                    onClick = { change(path, false) },
                    enabled = enabled && !working && path !in paths,
                    modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 8.dp),
                ) {
                    YukiIcon(Icons.Filled.Add, null, Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(
                        path,
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodySmall,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }
        }
        states.forEach { rule ->
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
            Row(
                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                val path = rule.src
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(path, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                    if (rule.hideState != null) Text(userHideStatus(rule), style = MaterialTheme.typography.bodySmall,
                        color = if (rule.hideState == 3) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if (rule.hideState == 3 || rule.hideState == 0 || rule.hideState == -1) {
                    IconButton(onClick = { refresh(path) }, enabled = enabled && !working) {
                        YukiIcon(Icons.Filled.Refresh, stringResource(R.string.sumh_hide_retry))
                    }
                }
                IconButton(onClick = { change(path, true) }, enabled = enabled && !working) {
                    YukiIcon(Icons.Filled.Delete, stringResource(R.string.sumh_user_hide_remove))
                }
            }
        }
        OutlinedTextField(
            value = newPath,
            onValueChange = { newPath = it },
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled && !working,
            singleLine = true,
            label = { Text(stringResource(R.string.sumh_absolute_path)) },
            placeholder = { Text(stringResource(R.string.sumh_user_hide_placeholder)) },
        )
        Button(
            onClick = { change(newPath.trim(), false) },
            enabled = enabled && !working && newPath.startsWith('/') && newPath != "/system/bin/su",
            modifier = Modifier.align(Alignment.End).heightIn(min = 48.dp),
        ) {
            YukiIcon(Icons.Filled.Add, null, Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.sumh_user_hide_add))
        }
    }
}

@Composable
internal fun userHideStatus(rule: Controller.ActiveRule): String = when (rule.hideState) {
    -1 -> stringResource(R.string.sumh_hide_saved)
    0 -> stringResource(R.string.sumh_hide_pending)
    1 -> stringResource(R.string.sumh_hide_binding)
    2 -> stringResource(R.string.sumh_hide_bound)
    4 -> stringResource(R.string.sumh_hide_suspended)
    else -> when (rule.hideError) {
        -13, -1 -> stringResource(R.string.sumh_hide_permission)
        -95 -> stringResource(R.string.sumh_hide_unsupported)
        else -> stringResource(R.string.sumh_hide_error, -rule.hideError)
    }
}

@Composable
internal fun KernelHooksCard(hooks: String) {
    if (hooks.isEmpty()) return
    var expanded by rememberSaveable { mutableStateOf(false) }
    val expandLabel = stringResource(if (expanded) R.string.collapse_menu else R.string.expand_menu)
    Card(
        onClick = { expanded = !expanded },
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        border = getCardBorder(),
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation()
    ) {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.sumh_kernel_hooks), Modifier.weight(1f), style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold)
                YukiIcon(if (expanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore, expandLabel)
            }
            if (expanded) Text(hooks, Modifier.padding(top = 8.dp), fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
    }
}
