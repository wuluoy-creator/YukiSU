package com.anatdx.yukisu.ui.kasumi

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.anatdx.yukisu.R
import com.anatdx.yukisu.ui.component.YukiIcon
import com.anatdx.yukisu.ui.component.YukiAlertDialog
import com.anatdx.yukisu.ui.kasumi.util.KasumiManager
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.launch

private val KASUMI_MOUNT_MODES = listOf("auto", "kasumi", "overlay", "magic", "none")

internal val KASUMI_MODE_COLORS = mapOf(
    "auto" to Color(0xFF1976D2),
    "kasumi" to Color(0xFF388E3C),
    "overlay" to Color(0xFFF57C00),
    "magic" to Color(0xFF7B1FA2),
    "none" to Color(0xFF616161)
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun KasumiMountConfigDialog(
    moduleId: String,
    moduleName: String,
    initialInfo: KasumiManager.ModuleInfo?,
    kasumiAvailable: Boolean,
    globalMode: String,
    onDismiss: () -> Unit,
    onSaved: () -> Unit
) {
    val resources = LocalResources.current
    val scope = rememberCoroutineScope()
    var error by remember { mutableStateOf<String?>(null) }
    val selectableModes = KASUMI_MOUNT_MODES.filter { it != "kasumi" || kasumiAvailable }

    var selectedMode by remember(moduleId) {
        mutableStateOf(initialInfo?.mode ?: "auto")
    }
    var rules by remember(moduleId) {
        mutableStateOf(initialInfo?.rules ?: emptyList<KasumiManager.ModuleRule>())
    }
    var newPath by remember { mutableStateOf("") }
    var newMode by remember { mutableStateOf(if (kasumiAvailable) "kasumi" else "overlay") }
    var editingPath by remember { mutableStateOf<String?>(null) }
    var modeExpanded by remember { mutableStateOf(false) }
    var isSaving by remember { mutableStateOf(false) }
    var rulesExpanded by remember { mutableStateOf(false) }

    val modeLabels = mapOf(
        "auto" to stringResource(R.string.kasumi_mount_mode_auto),
        "kasumi" to stringResource(R.string.kasumi_mount_mode_kasumi),
        "overlay" to stringResource(R.string.kasumi_mount_mode_overlay),
        "magic" to stringResource(R.string.kasumi_mount_mode_magic),
        "none" to stringResource(R.string.kasumi_mount_mode_none),
        "hide" to stringResource(R.string.kasumi_mount_mode_hide),
    )

    YukiAlertDialog(
        onDismissRequest = { if (!isSaving) onDismiss() },
        title = {
            Column {
                Text(stringResource(R.string.kasumi_mount_config))
                Text(
                    text = moduleName,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        },
        text = {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                if (globalMode != "auto") Text(stringResource(R.string.kasumi_global_override, globalMode))
                Text(
                    text = stringResource(R.string.kasumi_mount_mode),
                    style = MaterialTheme.typography.titleSmall
                )
                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    selectableModes.forEach { mode ->
                        FilterChip(
                            selected = selectedMode == mode,
                            onClick = { selectedMode = mode },
                            enabled = !isSaving,
                            label = { Text(modeLabels[mode] ?: mode) },
                            leadingIcon = {
                                YukiIcon(
                                    if (selectedMode == mode) Icons.Outlined.Check else when (mode) {
                                        "kasumi" -> Icons.Outlined.Memory
                                        "overlay" -> Icons.Outlined.Layers
                                        "magic" -> Icons.Outlined.AutoFixHigh
                                        "none" -> Icons.Outlined.Block
                                        else -> Icons.Outlined.Settings
                                    },
                                    null, Modifier.size(FilterChipDefaults.IconSize),
                                )
                            }
                        )
                    }
                }

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(min = 48.dp)
                        .clickable(role = Role.Button) { rulesExpanded = !rulesExpanded }
                        .padding(vertical = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = stringResource(R.string.kasumi_module_rules_title),
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.titleSmall
                    )
                    YukiIcon(
                        imageVector = if (rulesExpanded) Icons.Outlined.ExpandLess else Icons.Outlined.ExpandMore,
                        contentDescription = stringResource(if (rulesExpanded) R.string.collapse_menu else R.string.expand_menu)
                    )
                }
                Column(
                    modifier = Modifier.fillMaxWidth()
                ) {
                    if (rulesExpanded) {
                        Column(
                            modifier = Modifier.fillMaxWidth(),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                        rules.forEach { rule: KasumiManager.ModuleRule ->
                            Row(
                                modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                    Text(rule.path, style = MaterialTheme.typography.bodySmall)
                                    Text(
                                        modeLabels[rule.mode] ?: rule.mode,
                                        style = MaterialTheme.typography.labelSmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant
                                    )
                                }
                                IconButton(
                                    enabled = !isSaving,
                                    onClick = { editingPath = rule.path; newPath = rule.path; newMode = rule.mode }
                                ) { YukiIcon(Icons.Outlined.Edit, stringResource(R.string.kasumi_edit_rule)) }
                                IconButton(
                                    enabled = !isSaving,
                                    onClick = {
                                        rules = rules.filter { r -> r != rule }
                                        if (editingPath == rule.path) { editingPath = null; newPath = "" }
                                    }
                                ) {
                                    YukiIcon(Icons.Outlined.Delete, stringResource(R.string.delete))
                                }
                            }
                        }
                        OutlinedTextField(
                            value = newPath,
                            onValueChange = { newPath = it },
                            enabled = !isSaving,
                            isError = newPath.isNotEmpty() && (!newPath.startsWith("/") || rules.any { it.path == newPath.trim() && it.path != editingPath }),
                            label = { Text(stringResource(R.string.kasumi_absolute_path)) },
                            placeholder = { Text(stringResource(R.string.kasumi_module_rules_placeholder)) },
                            modifier = Modifier.fillMaxWidth(),
                            singleLine = true
                        )
                        FlowRow(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Box {
                                FilledTonalButton(
                                    enabled = !isSaving,
                                    onClick = { modeExpanded = true },
                                    modifier = Modifier.heightIn(min = 48.dp)
                                ) {
                                    Text(modeLabels[newMode] ?: newMode)
                                    YukiIcon(Icons.Outlined.ArrowDropDown, contentDescription = null)
                                }
                                DropdownMenu(
                                    expanded = modeExpanded,
                                    onDismissRequest = { modeExpanded = false }
                                ) {
                                    selectableModes.filter { it != "auto" || newMode == "auto" }.forEach { mode ->
                                        DropdownMenuItem(
                                            text = { Text(modeLabels[mode] ?: mode) },
                                            onClick = {
                                                newMode = mode
                                                modeExpanded = false
                                            }
                                        )
                                    }
                                }
                            }
                            FilledTonalButton(
                                enabled = !isSaving && newPath.startsWith('/') && rules.none { it.path == newPath.trim() && it.path != editingPath },
                                onClick = {
                                    if (newPath.startsWith("/")) {
                                        rules = rules.filterNot { it.path == editingPath } + KasumiManager.ModuleRule(newPath.trim(), newMode)
                                        newPath = ""
                                        editingPath = null
                                    }
                                },
                                modifier = Modifier.heightIn(min = 48.dp)
                            ) {
                                Text(stringResource(if (editingPath == null) R.string.kasumi_module_rules_add else R.string.kasumi_rule_save))
                            }
                        }
                        }
                    }
                }
            }
        },
        confirmButton = {
            Button(
                onClick = {
                    if (isSaving) return@Button
                    isSaving = true
                    scope.launch {
                        val rulesToSave = if (newPath.isNotEmpty()) rules.filterNot { it.path == editingPath } +
                            KasumiManager.ModuleRule(newPath.trim(), newMode) else rules
                        try {
                            KasumiManager.saveModule(moduleId, selectedMode, rulesToSave)
                            onSaved()
                        } catch (e: Exception) {
                            if (e is CancellationException) throw e
                            error = e.message ?: resources.getString(R.string.operation_failed)
                        } finally {
                            isSaving = false
                        }
                    }
                },
                enabled = !isSaving && (newPath.isEmpty() || (newPath.startsWith('/') && rules.none { it.path == newPath.trim() && it.path != editingPath }))
            ) {
                Text(stringResource(android.R.string.ok))
            }
        },
        dismissButton = {
            TextButton(enabled = !isSaving, onClick = onDismiss) {
                Text(stringResource(android.R.string.cancel))
            }
        }
    )
}
