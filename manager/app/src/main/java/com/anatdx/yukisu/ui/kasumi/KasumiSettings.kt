package com.anatdx.yukisu.ui.kasumi

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
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.ui.util.SnackbarController
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.kasumi.util.KasumiUiAdapter as Controller
import com.anatdx.yukisu.ui.theme.getCardColors
import com.anatdx.yukisu.ui.theme.CardConfig
import com.anatdx.yukisu.ui.theme.getCardElevation
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch
import ui.screen.moreSettings.component.MoreSettingsItemPosition

@Composable
internal fun SettingsTab(
    config: Controller.KagamiConfig,
    kasumiStatus: Controller.KasumiStatus,
    features: Controller.FeaturesResult?,
    snackbarHostState: SnackbarController,
    controlsEnabled: Boolean,
    runtimeApplyPending: Boolean,
    onRetryApply: () -> Unit,
    onConfigChanged: (Controller.KagamiConfig) -> Unit,
    onClearMapRules: () -> Unit,
    onAddMapRule: (Long, Long, Long, Long, String) -> Unit,
    section: KasumiSection,
    scrollable: Boolean = true,
    header: @Composable () -> Unit = {},
) {
    val enabled = controlsEnabled
    val mountEnabled = enabled && config.externalOwner.isEmpty()
    val kernelEnabled = enabled && kasumiStatus == Controller.KasumiStatus.AVAILABLE
    Column(
        modifier = (if (scrollable) Modifier.fillMaxSize().verticalScroll(rememberScrollState()) else Modifier.fillMaxWidth())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        header()
        when (section) {
            KasumiSection.Mount -> ConfigSection(stringResource(R.string.kasumi_workspace_mount), segmented = true) {
                if (config.externalOwner.isNotEmpty()) {
                    KasumiControlGroup {
                        Text(stringResource(R.string.kasumi_external_mount, config.externalOwner),
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                FileSystemChoice(config.fsType, mountEnabled) {
                    onConfigChanged(config.copy(fsType = it))
                }
                SettingSwitch("OverlayFS", stringResource(R.string.kasumi_backend_enable_desc), config.overlayfsEnabled,
                    { onConfigChanged(config.copy(overlayfsEnabled = it)) }, mountEnabled, Icons.Filled.Layers)
                SettingSwitch("Magic Mount", stringResource(R.string.kasumi_backend_enable_desc), config.magicMountEnabled,
                    { onConfigChanged(config.copy(magicMountEnabled = it)) }, mountEnabled, Icons.Filled.AutoFixHigh)
                MountSourceInput(config.mountsource, mountEnabled) { onConfigChanged(config.copy(mountsource = it)) }
                EditableConfigPath(R.string.kasumi_tempdir, R.string.kasumi_tempdir_desc, config.tempdir, mountEnabled, true) {
                    onConfigChanged(config.copy(tempdir = it))
                }
                EditableConfigPath(R.string.kasumi_mirror_path, R.string.kasumi_mirror_path_desc, config.mirrorPath, mountEnabled, true,
                    MoreSettingsItemPosition.Last) {
                    onConfigChanged(config.copy(mirrorPath = it))
                }
            }
            KasumiSection.Isolation -> {
                ConfigSection(stringResource(R.string.kasumi_workspace_isolation), segmented = true) {
                    SettingSwitch(stringResource(R.string.kasumi_stealth), stringResource(R.string.kasumi_stealth_desc), config.enableStealth,
                        { onConfigChanged(config.copy(enableStealth = it)) }, kernelEnabled, Icons.Filled.VisibilityOff,
                        MoreSettingsItemPosition.First)
                    val toggles = listOf(
                        FeatureToggle("mount_hide", R.string.kasumi_mount_hide, R.string.kasumi_mount_hide_desc, config.enableMountHide) { config.copy(enableMountHide = it) },
                        FeatureToggle("maps_spoof", R.string.kasumi_maps_spoof, R.string.kasumi_maps_spoof_desc, config.enableMapsSpoof) { config.copy(enableMapsSpoof = it) },
                        FeatureToggle("overlay_xattr_hide", R.string.kasumi_overlay_xattr, R.string.kasumi_overlay_xattr_desc, config.enableOverlayXattrHide) { config.copy(enableOverlayXattrHide = it) },
                        FeatureToggle("statfs_spoof", R.string.kasumi_statfs_spoof, R.string.kasumi_statfs_spoof_desc, config.enableStatfsSpoof) { config.copy(enableStatfsSpoof = it) },
                    )
                    toggles.forEach { toggle ->
                        val supported = features?.names?.contains(toggle.capability) == true
                        SettingSwitch(stringResource(toggle.title),
                            stringResource(when {
                                supported -> toggle.subtitle
                                toggle.checked -> R.string.kasumi_disable_unsupported
                                else -> R.string.feature_status_unsupported_summary
                            }),
                            toggle.checked, { onConfigChanged(toggle.changed(it)) }, enabled && ((kernelEnabled && supported) || (!supported && toggle.checked)),
                            icon = when (toggle.capability) {
                                "mount_hide" -> Icons.Filled.FolderOff
                                "maps_spoof" -> Icons.Filled.Memory
                                "statfs_spoof" -> Icons.Filled.Storage
                                else -> Icons.Filled.Security
                            },
                            position = if (toggle == toggles.last()) MoreSettingsItemPosition.Last else MoreSettingsItemPosition.Middle)
                        if (toggle.capability == "mount_hide") {
                            ConfigChoice(stringResource(R.string.kasumi_mount_hide_level), config.mountHideMode,
                                if (features?.names?.contains("mount_hide_aggressive") == true) listOf("normal", "aggressive") else listOf("normal"),
                                kernelEnabled && supported && config.enableMountHide, MoreSettingsItemPosition.Middle) {
                                onConfigChanged(config.copy(mountHideMode = it))
                            }
                        }
                    }
                }
                UserHideRulesCard(enabled, snackbarHostState)
                if (config.kernelAvailable && features?.names?.contains("maps_spoof") == true) {
                    MapsSpoofCard(enabled, onClearMapRules, onAddMapRule)
                }
            }
            KasumiSection.Debug -> ConfigSection(stringResource(R.string.kasumi_workspace_debug), segmented = true) {
                SettingSwitch(stringResource(R.string.kasumi_debug), stringResource(R.string.kasumi_debug_desc),
                    config.debug, { onConfigChanged(config.copy(debug = it)) }, enabled,
                    Icons.Filled.BugReport, MoreSettingsItemPosition.First)
                SettingSwitch(stringResource(R.string.kasumi_verbose), stringResource(R.string.kasumi_verbose_desc),
                    config.verbose, { onConfigChanged(config.copy(verbose = it)) }, enabled, Icons.AutoMirrored.Filled.Subject)
                SettingSwitch(stringResource(R.string.kasumi_kernel_debug), stringResource(R.string.kasumi_kernel_debug_desc), config.enableKernelDebug,
                    { onConfigChanged(config.copy(enableKernelDebug = it)) }, kernelEnabled,
                    Icons.Filled.BugReport, MoreSettingsItemPosition.Last)
            }
            else -> Unit
        }
        if (runtimeApplyPending) {
            ConfigSection(title = "") {
                Text(stringResource(R.string.kasumi_apply_failed), color = MaterialTheme.colorScheme.error)
                OutlinedButton(onClick = onRetryApply, enabled = kernelEnabled, modifier = Modifier.fillMaxWidth()) {
                    YukiIcon(Icons.Filled.Refresh, null)
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.kasumi_retry_apply))
                }
            }
        }
    }
}
@Composable
private fun FileSystemChoice(value: String, enabled: Boolean, onSelect: (String) -> Unit) {
    val options = listOf(
        "auto" to R.string.kasumi_fs_type_auto,
        "ext4" to R.string.kasumi_fs_type_ext4,
        "erofs" to R.string.kasumi_fs_type_erofs,
        "tmpfs" to R.string.kasumi_fs_type_tmpfs,
    )
    KasumiControlGroup(MoreSettingsItemPosition.Middle) {
        Text(
            stringResource(R.string.kasumi_fs_type_title),
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

private data class FeatureToggle(val capability: String, val title: Int, val subtitle: Int, val checked: Boolean,
    val changed: (Boolean) -> Controller.KagamiConfig)

@Composable
internal fun ConfigSection(
    title: String,
    segmented: Boolean = false,
    actions: (@Composable RowScope.() -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        if (title.isNotEmpty()) {
            Row(
                modifier = Modifier.fillMaxWidth().padding(start = 8.dp, end = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    title,
                    style = MaterialTheme.typography.titleSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f).padding(vertical = 8.dp).semantics { heading() }
                )
                actions?.invoke(this)
            }
        }
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = MaterialTheme.shapes.medium,
            color = MaterialTheme.colorScheme.surfaceContainerLow.copy(alpha = CardConfig.cardAlpha),
            contentColor = MaterialTheme.colorScheme.onSurface,
        ) {
            Column(
                modifier = if (segmented) Modifier else Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(if (segmented) 0.dp else 8.dp),
                content = content
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun ConfigChoice(title: String, value: String, values: List<String>, enabled: Boolean,
    position: MoreSettingsItemPosition = MoreSettingsItemPosition.Only, selected: (String) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val labels = mapOf("auto" to stringResource(R.string.kasumi_mount_mode_auto), "kasumi" to "Kasumi",
        "overlay" to "OverlayFS", "magic" to "Magic Mount", "none" to stringResource(R.string.kasumi_mount_mode_none),
        "normal" to stringResource(R.string.kasumi_hide_normal), "aggressive" to stringResource(R.string.kasumi_hide_aggressive), "magisk" to "Magisk",
        "all" to stringResource(R.string.kasumi_filter_all), "kasumi-active" to stringResource(R.string.kasumi_filter_active))
    KasumiControlGroup(position) {
        ExposedDropdownMenuBox(expanded = expanded && enabled, onExpandedChange = { if (enabled) expanded = it }) {
            OutlinedTextField(value = labels[value] ?: value, onValueChange = {}, readOnly = true, enabled = enabled,
                modifier = Modifier.fillMaxWidth().menuAnchor(ExposedDropdownMenuAnchorType.PrimaryNotEditable),
                label = { Text(title) },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) })
            ExposedDropdownMenu(expanded = expanded && enabled, onDismissRequest = { expanded = false }) {
                values.forEach { choice -> DropdownMenuItem(text = { Text(labels[choice] ?: choice) },
                    onClick = { expanded = false; selected(choice) }) }
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

    KasumiControlGroup(MoreSettingsItemPosition.Middle) {
        OutlinedTextField(
            value = draft,
            onValueChange = { draft = it },
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled,
            singleLine = true,
            isError = changed && !valid,
            label = { Text(stringResource(R.string.kasumi_mountsource)) },
            placeholder = { Text("KSU") },
            trailingIcon = if (changed) {
                {
                    IconButton(onClick = { save() }, enabled = enabled && valid) {
                        YukiIcon(Icons.Filled.Check, stringResource(R.string.kasumi_rule_save))
                    }
                }
            } else null,
            supportingText = if (changed && !valid) {
                { Text(stringResource(R.string.kasumi_mountsource_invalid)) }
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
        stringResource(R.string.kasumi_absolute_path),
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
        title = stringResource(R.string.kasumi_user_hide_title),
        actions = {
            IconButton(onClick = { refresh() }, enabled = !working) {
                YukiIcon(Icons.Filled.Refresh, stringResource(R.string.kasumi_rules_refresh))
            }
        }
    ) {
        Text(
            stringResource(R.string.kasumi_quick_hide),
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
                        YukiIcon(Icons.Filled.Refresh, stringResource(R.string.kasumi_hide_retry))
                    }
                }
                IconButton(onClick = { change(path, true) }, enabled = enabled && !working) {
                    YukiIcon(Icons.Filled.Delete, stringResource(R.string.kasumi_user_hide_remove))
                }
            }
        }
        OutlinedTextField(
            value = newPath,
            onValueChange = { newPath = it },
            modifier = Modifier.fillMaxWidth(),
            enabled = enabled && !working,
            singleLine = true,
            label = { Text(stringResource(R.string.kasumi_absolute_path)) },
            placeholder = { Text(stringResource(R.string.kasumi_user_hide_placeholder)) },
        )
        Button(
            onClick = { change(newPath.trim(), false) },
            enabled = enabled && !working && newPath.startsWith('/') && newPath != "/system/bin/su",
            modifier = Modifier.align(Alignment.End).heightIn(min = 48.dp),
        ) {
            YukiIcon(Icons.Filled.Add, null, Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.kasumi_user_hide_add))
        }
    }
}

@Composable
internal fun userHideStatus(rule: Controller.ActiveRule): String = when (rule.hideState) {
    -1 -> stringResource(R.string.kasumi_hide_saved)
    0 -> stringResource(R.string.kasumi_hide_pending)
    1 -> stringResource(R.string.kasumi_hide_binding)
    2 -> stringResource(R.string.kasumi_hide_bound)
    4 -> stringResource(R.string.kasumi_hide_suspended)
    else -> when (rule.hideError) {
        -13, -1 -> stringResource(R.string.kasumi_hide_permission)
        -95 -> stringResource(R.string.kasumi_hide_unsupported)
        else -> stringResource(R.string.kasumi_hide_error, -rule.hideError)
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
        shape = MaterialTheme.shapes.medium,
        colors = getCardColors(MaterialTheme.colorScheme.surfaceContainerLow),
        elevation = getCardElevation()
    ) {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
            Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.kasumi_kernel_hooks), Modifier.weight(1f), style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold)
                YukiIcon(if (expanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore, expandLabel)
            }
            if (expanded) Text(hooks, Modifier.padding(top = 8.dp), fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
        }
    }
}
